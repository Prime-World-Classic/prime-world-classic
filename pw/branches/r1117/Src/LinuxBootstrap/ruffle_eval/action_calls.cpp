#include "action_calls.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace
{
using Json = nlohmann::json;
using State = PwRuffleActionState;
using Talent = State::Talent;
using Purchase = State::PurchaseState;
using Grid = std::array<const Talent*, State::Columns * State::Rows>;

/** Reject the whole batch before any caller can submit a partial request list. */
void Require(bool condition, const char* field, const char* rule)
{
	if (!condition)
		throw std::invalid_argument(std::string("Action HUD ") + field + ": " + rule);
}

void Integer(std::int64_t value, const char* field, std::int64_t min, std::int64_t max)
{
	Require(value >= min && value <= max, field, "integer outside supported range");
}

void Seconds(double value, const char* field)
{
	Require(std::isfinite(value) && value >= 0 && value <= std::numeric_limits<float>::max(),
		field, "expected finite nonnegative float-range seconds");
}

/** Strict UTF-8 is supplied by nlohmann; only the documented Data prefix is added. */
std::string Resource(const std::string& value, const char* field, bool optional = false)
{
	Require(value.size() <= State::MaxResourceBytes && value.find('\0') == std::string::npos,
		field, "resource exceeds byte limit or contains NUL");
	if (value.empty())
	{
		Require(optional, field, "actual talent image is required");
		return {};
	}
	try { static_cast<void>(Json(value).dump()); }
	catch (const Json::type_error&)
	{
		throw std::invalid_argument(std::string("Action HUD ") + field + ": invalid UTF-8");
	}
	std::string_view body(value);
	if (body.substr(0, 2) == ":/") body.remove_prefix(2);
	else if (body.front() == '/') body.remove_prefix(1);
	Require(!body.empty() && body.front() != '/' && body.back() != '/' &&
		body.find("//") == std::string_view::npos && body.size() + 2 <= State::MaxResourceBytes,
		field, "invalid Data-relative resource path or byte limit");
	for (const unsigned char byte : body)
		Require(byte > 31 && byte != 127 && byte != ':' && byte != '\\',
			field, "resource contains a control, scheme, or backslash");
	for (const auto& part : std::filesystem::path(body))
		Require(part != "." && part != "..", field, "resource dot/traversal component");
	return ":/" + std::string(body);
}

/** Validate bounded native inputs and index by authored row-major cell order. */
Grid Validate(const State& state)
{
	Require(state.talents.size() <= State::Rows * State::Columns, "talents", "more than 36 cells");
	Grid grid{};
	std::array<bool, State::ActionSlots> reserved{};
	std::size_t reservedCount = 0;
	std::size_t boughtAutomatic = 0;
	for (const auto& t : state.talents)
	{
		Integer(t.column, "column", 0, State::Columns - 1);
		Integer(t.row, "row", 0, State::Rows - 1);
		const auto cell = static_cast<std::size_t>(t.row) * State::Columns + static_cast<std::size_t>(t.column);
		Require(!grid[cell], "talents", "duplicate grid cell");
		grid[cell] = &t;
		Resource(t.iconPath, "iconPath");
		Resource(t.alternativeIconPath, "alternativeIconPath", true);
		Require(t.active.has_value(), "active", "required Boolean absent");
		Require(t.classTalent.has_value(), "classTalent", "required Boolean absent");
		Require(t.alternativeState.has_value(), "alternativeState", "required Boolean absent");
		Integer(t.desiredIndex, "desiredIndex", -2, State::ActionSlots - 1);
		Integer(t.upgradeLevel, "upgradeLevel", 0, 3);
		Integer(t.cost, "cost", 0, std::numeric_limits<std::int32_t>::max());
		Integer(static_cast<std::int32_t>(t.purchase), "purchase", 0, 3);
		Integer(static_cast<std::int32_t>(t.status), "status", 0, 6);
		Seconds(t.cooldown, "cooldown");
		Seconds(t.maxCooldown, "maxCooldown");
		if (*t.active && t.desiredIndex >= 0)
		{
			const auto index = static_cast<std::size_t>(t.desiredIndex);
			Require(!reserved[index], "desiredIndex", "active talent position collision");
			reserved[index] = true;
			++reservedCount;
		}
		if (*t.active && t.desiredIndex == -1 && t.purchase == Purchase::Bought)
			++boughtAutomatic;
	}
	Require(reservedCount + boughtAutomatic <= State::ActionSlots, "capacity",
		"reserved and bought automatic talents exceed the 10 action slots");
	return grid;
}

Json Invoke(const char* method, Json args)
{
	return {{"action", "invoke"}, {"path", "mainInterface"}, {"method", method}, {"args", std::move(args)}};
}

/** AdventureFlashInterface::SetTalentIcon: column BEFORE row, nine arguments. */
Json IconArgs(const Talent& t)
{
	return Json::array({t.column, t.row, Resource(t.iconPath, "iconPath"),
		Resource(t.alternativeIconPath, "alternativeIconPath", true), *t.active,
		t.desiredIndex, t.upgradeLevel, *t.classTalent, t.cost});
}

/** Status uses seconds and an explicit alternative-state Boolean, not a ratio. */
Json StatusArgs(const Talent& t)
{
	return Json::array({t.column, t.row, static_cast<std::int32_t>(t.status),
		t.cooldown, t.maxCooldown, *t.alternativeState});
}

/** Bootstrap only a new bought shortcut; repeated Bought must not allocate another. */
bool PurchaseCalls(Json& calls, const Talent& t, const Talent* previous)
{
	if (previous && previous->purchase == t.purchase)
		return false;
	if (t.purchase == Purchase::Bought && *t.active &&
		(!previous || previous->purchase == Purchase::NotEnoughPrime))
		calls.push_back(Invoke("SetTalentState", Json::array({t.column, t.row,
			static_cast<std::int32_t>(Purchase::NotEnoughDevPoints)})));
	calls.push_back(Invoke("SetTalentState", Json::array({t.column, t.row, static_cast<std::int32_t>(t.purchase)})));
	return true;
}
}

nlohmann::json PwRuffleActionInitCalls(const PwRuffleActionState& state)
{
	const auto grid = Validate(state);
	auto calls = Json::array();
	for (const auto* t : grid)
		if (t) calls.push_back(Invoke("SetTalentIcon", IconArgs(*t)));
	for (const auto* t : grid)
	{
		if (!t) continue;
		PurchaseCalls(calls, *t, nullptr);
		calls.push_back(Invoke("SetTalentStatus", StatusArgs(*t)));
	}
	if (!state.talents.empty())
		calls.push_back(Invoke("OnTalentsStateChanged", Json::array()));
	return calls;
}

nlohmann::json PwRuffleActionUpdateCalls(const PwRuffleActionState& state,
	const PwRuffleActionState& previous)
{
	const auto grid = Validate(state);
	const auto oldGrid = Validate(previous);
	auto calls = Json::array();
	bool purchaseChanged = false;
	for (std::size_t i = 0; i < grid.size(); ++i)
	{
		const auto* t = grid[i];
		const auto* old = oldGrid[i];
		Require((t == nullptr) == (old == nullptr), "talents", "membership changes require a HUD reset/init");
		if (!t) continue;
		Require(IconArgs(*t) == IconArgs(*old), "talents", "icon metadata changes require a HUD reset/init");
		Require(old->purchase != Purchase::Bought || t->purchase == Purchase::Bought,
			"purchase", "Bought -> unbought needs a HUD reset/init");
		const bool changed = PurchaseCalls(calls, *t, old);
		purchaseChanged = purchaseChanged || changed;
		if ((changed && t->purchase == Purchase::Bought) || StatusArgs(*t) != StatusArgs(*old))
			calls.push_back(Invoke("SetTalentStatus", StatusArgs(*t)));
	}
	if (purchaseChanged)
		calls.push_back(Invoke("OnTalentsStateChanged", Json::array()));
	return calls;
}
