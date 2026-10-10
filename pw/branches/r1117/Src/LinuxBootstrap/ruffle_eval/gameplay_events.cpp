#include "gameplay_events.h"
#include "action_state.h"

#include <nlohmann/json.hpp>
#include <array>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace
{
using Json = nlohmann::json;
using Event = PwRuffleGameplayEvent;
using Kind = Event::Kind;
using Limits = PwRuffleGameplayEventLimits;

/** Reject before the caller can observe any part of the decoded batch. */
void Require(bool condition, const char* error)
{
	if (!condition) throw std::invalid_argument(error);
}

/** Match the native whitespace-separated grammar without locale dependence. */
bool Space(char value)
{
	return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
		value == '\v' || value == '\f';
}

/** Bound text and exclude truncating controls, including in unsupported callbacks. */
void ValidateText(std::string_view command, std::string_view args)
{
	Require(!command.empty() && command.size() <= Limits::MaxCommandBytes,
		"Gameplay callback command byte limit");
	Require(args.size() <= Limits::MaxArgumentBytes, "Gameplay callback argument byte limit");
	for (const char byte : command)
		Require((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
			(byte >= '0' && byte <= '9') || byte == '_', "Gameplay callback command is not an ASCII identifier");
	for (const unsigned char byte : args)
		Require((byte >= 32 && byte != 127) || Space(static_cast<char>(byte)),
			"Gameplay callback argument contains a control byte");
}

/** Split only the two/three-token allowlist grammar; never allocate per token. */
std::array<std::string_view, 3> Tokens(std::string_view args, std::size_t expected)
{
	std::array<std::string_view, 3> tokens{};
	std::size_t count = 0;
	while (!args.empty())
	{
		if (Space(args.front()))
		{
			args.remove_prefix(1);
			continue;
		}
		Require(count < expected, "Gameplay callback has extra arguments");
		std::size_t size = 0;
		while (size < args.size() && !Space(args[size])) ++size;
		tokens[count++] = args.substr(0, size);
		args.remove_prefix(size);
	}
	Require(count == expected, "Gameplay callback has missing arguments");
	return tokens;
}

/** Exact bounded decimal integer; reject overflow, fractions and trailing text. */
int Integer(std::string_view token, int maximum)
{
	int value = -1;
	const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value, 10);
	Require(parsed.ec == std::errc() && parsed.ptr == token.data() + token.size() &&
		value >= 0 && value <= maximum, "Gameplay callback integer is malformed or outside range");
	return value;
}

/** Native consumers use float: parse directly to float, rejecting under/overflow. */
float Coordinate(std::string_view token)
{
	float value = 0;
	const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value,
		std::chars_format::general);
	Require(parsed.ec == std::errc() && parsed.ptr == token.data() + token.size() && std::isfinite(value),
		"Gameplay callback coordinate is malformed, nonfinite or outside float range");
	return value;
}

/** Exact-name allowlist; unknown production/UI callbacks remain inert. */
Event Decode(std::string_view command, std::string_view args)
{
	ValidateText(command, args);
	Event result;
	result.command = command;
	if (command == "TalentClicked" || command == "TalentToolTip" || command == "TalentActionToolTip")
	{
		const bool click = command == "TalentClicked";
		const auto tokens = Tokens(args, click ? 2 : 3);
		const std::size_t offset = click ? 0 : 1;
		if (!click) Integer(tokens[0], 1);
		const int column = Integer(tokens[offset], static_cast<int>(PwRuffleActionState::Columns) - 1);
		const int row = Integer(tokens[offset + 1], static_cast<int>(PwRuffleActionState::Rows) - 1);
		result.kind = click ? Kind::TalentClicked : Kind::Informational;
		if (click)
		{
			result.column = column;
			result.row = row;
		}
		return result;
	}
	if (command == "MinimapMouseOver") result.kind = Kind::MinimapMouseOver;
	else if (command == "MinimapMouseDown") result.kind = Kind::MinimapMouseDown;
	else if (command == "MinimapMouseUp") result.kind = Kind::MinimapMouseUp;
	else if (command == "MinimapActionMove") result.kind = Kind::MinimapActionMove;
	else return result;

	const bool move = result.kind == Kind::MinimapActionMove;
	const auto tokens = Tokens(args, move ? 2 : 3);
	const std::size_t offset = move ? 0 : 1;
	if (!move) result.flag = Integer(tokens[0], 1) != 0;
	result.x = Coordinate(tokens[offset]);
	result.y = Coordinate(tokens[offset + 1]);
	return result;
}
}

std::vector<PwRuffleGameplayEvent> PwRuffleDecodeGameplayEvents(std::string_view json)
{
	Require(!json.empty() && json.size() <= Limits::MaxBatchBytes, "Gameplay callback batch byte limit");
	Json batch;
	try
	{
		std::size_t count = 0;
		batch = Json::parse(json.begin(), json.end(),
			[&count](int depth, Json::parse_event_t event, Json&)
			{
				Require(event != Json::parse_event_t::object_start &&
					!(event == Json::parse_event_t::array_start && depth > 1),
					"Gameplay callback JSON must contain only a queue of flat pairs");
				if (event == Json::parse_event_t::array_start && depth == 1)
					Require(++count <= Limits::MaxEvents, "Gameplay callback queue limit");
				return true;
			});
	}
	catch (const Json::exception&)
	{
		throw std::invalid_argument("Gameplay callback JSON is malformed");
	}
	Require(batch.is_array() && batch.size() <= Limits::MaxEvents, "Gameplay callback reply must be an array");
	std::vector<Event> result;
	result.reserve(batch.size());
	for (const auto& pair : batch)
	{
		Require(pair.is_array() && pair.size() == 2 && pair[0].is_string() && pair[1].is_string(),
			"Gameplay callback must be a [command, argumentString] pair");
		result.push_back(Decode(pair[0].get_ref<const std::string&>(), pair[1].get_ref<const std::string&>()));
	}
	return result;
}
