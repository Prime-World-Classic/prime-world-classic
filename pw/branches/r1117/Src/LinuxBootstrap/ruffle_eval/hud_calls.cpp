#include "hud_calls.h"

#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace
{
using Json = nlohmann::json;
using State = PwRuffleHudState;
constexpr std::int64_t MaxInt = std::numeric_limits<std::int32_t>::max();
constexpr double MaxNumber = std::numeric_limits<float>::max();

/** Fail before exposing a partial request list; never coerce invalid game data. */
void Require(bool condition, const char* field, const char* rule)
{
	if (!condition)
		throw std::invalid_argument(std::string("HUD ") + field + ": " + rule);
}

void Integer(std::int64_t value, const char* field, std::int64_t min = 0, std::int64_t max = MaxInt)
{
	Require(value >= min && value <= max, field, "integer outside supported range");
}

void Number(double value, const char* field, double min = -MaxNumber, double max = MaxNumber)
{
	Require(std::isfinite(value) && value >= min && value <= max, field, "non-finite or out-of-range number");
}

template<class T>
const T& Required(const std::optional<T>& value, const char* field)
{
	Require(value.has_value(), field, "required value is absent");
	return *value;
}

/** Reuse the JSON library's strict UTF-8 encoder rather than implement a decoder. */
void Text(const std::string& value, const char* field)
{
	Require(value.size() <= State::MaxTextBytes && value.find('\0') == std::string::npos,
		field, "text exceeds byte limit or contains NUL");
	try { static_cast<void>(Json(value).dump()); }
	catch (const Json::type_error&)
	{
		throw std::invalid_argument(std::string("HUD ") + field + ": invalid UTF-8");
	}
}

/** Only convert the documented Data-root prefix; reject ambiguous/escaping paths. */
std::string Resource(const std::string& value, const char* field)
{
	Text(value, field);
	if (value.empty())
		return {};
	std::string_view body(value);
	if (body.substr(0, 2) == ":/")
		body.remove_prefix(2);
	else if (body.front() == '/')
		body.remove_prefix(1);
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

/** Validate all engaged groups consistently in both public builders. */
void Validate(const State& state)
{
	Require(state.hero || (!state.values && !state.development), "hero", "hero values require a local identity");
	if (state.hero)
	{
		const auto& h = *state.hero;
		Integer(h.heroId, "heroId");
		Text(h.heroName, "heroName");
		Text(h.heroClass, "heroClass");
		Resource(h.portraitPath, "portraitPath");
		Required(h.isMale, "isMale");
		Required(h.isBot, "isBot");
		Integer(h.force, "force");
		Integer(h.faction, "faction", 0, 2);
		Integer(h.originalFaction, "originalFaction", 0, 2);
		Number(h.rating, "rating", 0);
		Integer(h.damageType, "damageType", 0, 4);
		Require(!h.forceColors.empty() && h.forceColors.size() <= State::MaxTableEntries,
			"forceColors", "nonempty bounded table required by FillPlayerData");
		std::int64_t previousForce = -1;
		for (const auto& entry : h.forceColors)
		{
			Integer(entry.force, "forceColors.force");
			Integer(entry.color, "forceColors.color", 0, std::numeric_limits<std::uint32_t>::max());
			Require(entry.force >= previousForce, "forceColors", "thresholds must be ascending");
			previousForce = entry.force;
		}
		Require(h.leaguePlaces.size() <= State::MaxTableEntries, "leaguePlaces", "table exceeds entry limit");
		for (const auto place : h.leaguePlaces)
			Integer(place, "leaguePlaces");
		Resource(h.rankPath, "rankPath");
		Text(h.rankTooltip, "rankTooltip");
		Integer(h.partyId, "partyId");
		Resource(h.flagPath, "flagPath");
		Text(h.flagTooltip, "flagTooltip");
		Integer(h.leagueIndex, "leagueIndex");
		Integer(h.ownLeaguePlace, "ownLeaguePlace");
	}
	if (state.values)
	{
		const auto& v = *state.values;
		Integer(v.level, "level");
		Integer(v.maxHealth, "maxHealth", 1);
		Integer(v.health, "health", 0, v.maxHealth);
		Integer(v.maxEnergy, "maxEnergy");
		Integer(v.energy, "energy", 0, v.maxEnergy);
		Required(v.isVisible, "isVisible");
		Required(v.isPickable, "isPickable");
		Integer(Required(v.resurrectionSeconds, "resurrectionSeconds"), "resurrectionSeconds", -1);
		Number(v.channeling, "channeling", 0, 1);
		Number(v.healthRegen, "healthRegen");
		Number(v.energyRegen, "energyRegen");
		Required(v.isCameraLocked, "isCameraLocked");
		if (v.ultimateCooldown != -1)
			Number(v.ultimateCooldown, "ultimateCooldown", 0, 1);
	}
	if (state.development)
	{
		Integer(state.development->prime, "prime");
		Integer(state.development->gold, "gold");
	}
	if (state.match)
	{
		const auto& m = *state.match;
		Number(m.humanTerrain, "humanTerrain", 0, 1);
		Number(m.elfTerrain, "elfTerrain", 0, 1);
		Integer(m.matchSeconds, "matchSeconds");
		Integer(Required(m.creepSpawnSeconds, "creepSpawnSeconds"), "creepSpawnSeconds",
			std::numeric_limits<std::int32_t>::min());
	}
}

/** Match the native Ruffle invoke envelope without depending on the host or ABI. */
Json Invoke(const char* method, Json args)
{
	return {{"action", "invoke"}, {"path", "mainInterface"}, {"method", method}, {"args", std::move(args)}};
}
}

nlohmann::json PwRuffleHeroIdentityCalls(const PwRuffleHudState& state)
{
	Validate(state);
	auto calls = Json::array();
	if (!state.hero)
		return calls;
	const auto& h = *state.hero;
	// Preserve native arity: leaguePlaces comes BEFORE leagueIndex/ownLeaguePlace.
	calls.push_back(Invoke("SetOurHeroIdententity", Json::array({h.heroId, h.faction,
		h.damageType, h.leaguePlaces, h.leagueIndex, h.ownLeaguePlace})));
	auto forces = Json::array();
	auto colors = Json::array();
	for (const auto& entry : h.forceColors)
	{
		forces.push_back(entry.force);
		colors.push_back(entry.color);
	}
	calls.push_back(Invoke("SetForceColors", Json::array({forces, colors})));
	calls.push_back(Invoke("SetFriendlyHeroIdentity", Json::array({h.heroId, h.heroName,
		h.heroClass, Resource(h.portraitPath, "portraitPath"), *h.isMale, *h.isBot,
		h.force, h.faction, h.originalFaction, h.rating, h.leaguePlaces,
		Resource(h.rankPath, "rankPath"), h.rankTooltip, h.isPremium, h.partyId,
		Resource(h.flagPath, "flagPath"), h.flagTooltip, false, h.leagueIndex, h.ownLeaguePlace})));
	calls.push_back(Invoke("ShowHeroPortrait", Json::array({h.heroId, false})));
	return calls;
}

nlohmann::json PwRuffleHeroValueCalls(const PwRuffleHudState& state)
{
	Validate(state);
	auto calls = Json::array();
	if (state.values)
	{
		const auto& v = *state.values;
		calls.push_back(Invoke("SetHeroParams", Json::array({state.hero->heroId, v.level,
			v.health, v.maxHealth, v.energy, v.maxEnergy, *v.isVisible, *v.isPickable,
			*v.resurrectionSeconds, v.channeling, v.healthRegen, v.energyRegen,
			*v.isCameraLocked, v.ultimateCooldown})));
	}
	if (state.development)
		calls.push_back(Invoke("SetHeroDevelopmentParams", Json::array({state.development->prime,
			state.development->gold})));
	if (state.match)
	{
		const auto& m = *state.match;
		calls.push_back(Invoke("SetGameProgress", Json::array({m.humanTerrain, m.elfTerrain,
			m.matchSeconds, *m.creepSpawnSeconds})));
	}
	return calls;
}
