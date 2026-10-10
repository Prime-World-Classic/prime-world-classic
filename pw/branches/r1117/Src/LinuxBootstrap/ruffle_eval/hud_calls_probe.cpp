#include "hud_calls.h"

#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
using Json = nlohmann::json;
using State = PwRuffleHudState;
int checks = 0;

/** Keep assertions active even when the independent probe is built with NDEBUG. */
void Check(bool condition, const std::string& message)
{
	++checks;
	if (!condition)
		throw std::runtime_error(message);
}

/** Deliberately distinct mock values expose argument swaps; never used in production. */
State Fixture()
{
	State state;
	auto& hero = state.hero.emplace();
	hero.heroId = 7;
	hero.heroName = u8"[QA] \u0410\u043d\u0434\u0440\u0435\u0439 \U0001f680";
	hero.heroClass = u8"\u6cd5\u5e08";
	hero.portraitPath = u8"/UI/Portraits/\u0433\u0435\u0440\u043e\u0439.dds";
	hero.isMale = false;
	hero.isBot = true;
	hero.force = 123;
	hero.faction = 2;
	hero.originalFaction = 1;
	hero.rating = 1729.5;
	hero.damageType = 3;
	hero.forceColors = {{100, 0xff112233}, {200, 0xff445566}};
	hero.leaguePlaces = {2300, 1900, 1700};
	hero.rankPath = "UI/Ranks/rank.dds";
	hero.rankTooltip = u8"\u0420\u0430\u043d\u0433\n\"Test\"";
	hero.isPremium = true;
	hero.partyId = 19;
	hero.flagPath = ":/UI/Flags/flag.dds";
	hero.flagTooltip = "Test flag";
	hero.leagueIndex = 12;
	hero.ownLeaguePlace = 23;
	auto& values = state.values.emplace();
	values.level = 11;
	values.health = 301;
	values.maxHealth = 907;
	values.energy = 59;
	values.maxEnergy = 311;
	values.isVisible = true;
	values.isPickable = false;
	values.resurrectionSeconds = 13;
	values.channeling = 0.25;
	values.healthRegen = -2.5;
	values.energyRegen = 3.75;
	values.isCameraLocked = true;
	values.ultimateCooldown = 0.75;
	state.development = State::Development{751, 29};
	state.match = State::MatchProgress{0.3125, 0.625, 1234, -3};
	return state;
}

/** Check each complete invoke envelope, not just a few positional fields. */
void Call(const Json& actual, const char* method, const Json& args)
{
	Check(actual == Json({{"action", "invoke"}, {"path", "mainInterface"},
		{"method", method}, {"args", args}}), std::string("Contract mismatch: ") + method);
}

/** Every invalid fixture must fail in both builders with a field-specific diagnostic. */
void Reject(const std::string& field, const std::function<void(State&)>& change)
{
	State state = Fixture();
	change(state);
	for (const auto builder : {PwRuffleHeroIdentityCalls, PwRuffleHeroValueCalls})
	{
		bool rejected = false;
		try { static_cast<void>(builder(state)); }
		catch (const std::invalid_argument& error)
		{
			rejected = true;
			Check(std::string(error.what()).find(field) != std::string::npos,
				"Wrong diagnostic for " + field + ": " + error.what());
		}
		Check(rejected, "Accepted invalid " + field);
	}
}

/** Pin the production tuples and UTF-8/resource handling independently of any VM. */
void Contracts()
{
	const State state = Fixture();
	const auto identity = PwRuffleHeroIdentityCalls(state);
	Check(identity.is_array() && identity.size() == 4, "Identity call count");
	Call(identity[0], "SetOurHeroIdententity", Json::array({7, 2, 3, {2300, 1900, 1700}, 12, 23}));
	Call(identity[1], "SetForceColors", Json::array({{100, 200}, {0xff112233, 0xff445566}}));
	Call(identity[2], "SetFriendlyHeroIdentity", Json::array({7, state.hero->heroName,
		state.hero->heroClass, u8":/UI/Portraits/\u0433\u0435\u0440\u043e\u0439.dds", false, true,
		123, 2, 1, 1729.5, {2300, 1900, 1700}, ":/UI/Ranks/rank.dds",
		state.hero->rankTooltip, true, 19, ":/UI/Flags/flag.dds", "Test flag", false, 12, 23}));
	Call(identity[3], "ShowHeroPortrait", Json::array({7, false}));
	Check(Json::parse(identity.dump()) == identity, "Lossless UTF-8/JSON round trip");
	Check(identity[2]["args"][1] == state.hero->heroName, "UTF-8 changed");
	const auto values = PwRuffleHeroValueCalls(state);
	Check(values.is_array() && values.size() == 3, "Value call count");
	Call(values[0], "SetHeroParams", Json::array({7, 11, 301, 907, 59, 311,
		true, false, 13, 0.25, -2.5, 3.75, true, 0.75}));
	Call(values[1], "SetHeroDevelopmentParams", Json::array({751, 29}));
	Call(values[2], "SetGameProgress", Json::array({0.3125, 0.625, 1234, -3}));
	Check(values[0]["args"][2].is_number_integer(), "Health is not an AS int");
	Check(values[0]["args"][6].is_boolean(), "Visibility is not an AS Boolean");
	Check(values[0]["args"][9].is_number_float(), "Channeling is not an AS Number");
}

/** Missing optional groups never emit fictitious health, currencies or terrain. */
void OmissionsAndBounds()
{
	Check(PwRuffleHeroIdentityCalls({}) == Json::array(), "Default identity is not empty");
	Check(PwRuffleHeroValueCalls({}) == Json::array(), "Default values are not empty");
	State state = Fixture();
	state.values.reset();
	state.development.reset();
	state.match.reset();
	Check(PwRuffleHeroValueCalls(state).empty(), "Identity invented values");
	state.hero->portraitPath.clear();
	state.hero->rankPath.clear();
	state.hero->flagPath.clear();
	state.hero->leaguePlaces.clear();
	const auto identity = PwRuffleHeroIdentityCalls(state);
	for (const auto index : {3, 11, 15})
		Check(identity[2]["args"][index] == "", "Empty resource gained a fake asset");
	Check(identity[0]["args"][3] == Json::array(), "Empty league became null/object");
	state.hero.reset();
	state.match = Fixture().match;
	Check(PwRuffleHeroIdentityCalls(state).empty(), "Time-only snapshot invented a hero");
	Check(PwRuffleHeroValueCalls(state).size() == 1, "Complete match-only tuple omitted");
	state = Fixture();
	constexpr auto maxInt = std::numeric_limits<std::int32_t>::max();
	state.hero->heroId = maxInt;
	state.hero->force = maxInt;
	state.hero->heroName = std::string(State::MaxTextBytes, 'a');
	state.hero->portraitPath = ":/" + std::string(State::MaxResourceBytes - 6, 'a') + ".dds";
	state.hero->forceColors.clear();
	for (std::size_t i = 0; i < State::MaxTableEntries; ++i)
		state.hero->forceColors.push_back({static_cast<std::int64_t>(i), 0xffffffff});
	state.hero->leaguePlaces.assign(State::MaxTableEntries, maxInt);
	state.values->health = state.values->maxHealth = maxInt;
	state.values->energy = state.values->maxEnergy = 0;
	state.values->channeling = 0;
	state.values->ultimateCooldown = -1;
	state.values->healthRegen = -std::numeric_limits<float>::max();
	state.development = State::Development{0, maxInt};
	state.match = State::MatchProgress{0, 1, maxInt, std::numeric_limits<std::int32_t>::min()};
	Check(PwRuffleHeroIdentityCalls(state).size() == 4, "Valid identity boundary rejected");
	const auto calls = PwRuffleHeroValueCalls(state);
	Check(calls[0]["args"][4] == 0 && calls[0]["args"][5] == 0, "No-energy hero changed");
	Check(calls[0]["args"][13] == -1, "Unavailable ultimate sentinel changed");
	Check(calls[1]["args"][1] == maxInt, "Maximum gold was narrowed");
	state.values->health = 0;
	state.values->channeling = state.values->ultimateCooldown = 1;
	state.match->humanTerrain = 1;
	state.match->elfTerrain = 0;
	Check(PwRuffleHeroValueCalls(state)[0]["args"][2] == 0, "Dead hero was revived");
	state.values->health = 1;
	state.values->resurrectionSeconds = -1;
	Check(PwRuffleHeroValueCalls(state)[0]["args"][8] == -1,
		"PFHero alive/no-respawn sentinel rejected or changed");
}

/** Invalid UTF-8 is rejected, never repaired by replacement characters. */
void InvalidTextAndPaths()
{
	for (const std::string& text : {std::string("bad\0name", 8), std::string("\xc0\xaf"),
		std::string("\xed\xa0\x80"), std::string("\xf4\x90\x80\x80"), std::string("\xe2\x82"),
		std::string(State::MaxTextBytes + 1, 'x')})
		Reject("heroName", [text](State& s) { s.hero->heroName = text; });
	Reject("rankTooltip", [](State& s) { s.hero->rankTooltip = "\xff"; });
	Reject("heroClass", [](State& s) { s.hero->heroClass = "\x80"; });
	Reject("flagTooltip", [](State& s) { s.hero->flagTooltip = "\xff"; });
	for (const std::string path : {"../escape.dds", ":/UI/../escape.dds", ":/UI/./a.dds",
		":/UI//a.dds", "https://invalid/a.dds", "file:///a.dds", "C:/a.dds", "UI\\a.dds",
		":/", "/", ":UI/a.dds", "//host/a.dds", "UI/a.dds/", "UI/\na.dds", "UI/\xff.dds"})
		Reject("portraitPath", [path](State& s) { s.hero->portraitPath = path; });
	Reject("portraitPath", [](State& s) { s.hero->portraitPath = std::string(State::MaxResourceBytes, 'a'); });
	Reject("rankPath", [](State& s) { s.hero->rankPath = "../rank.dds"; });
	Reject("flagPath", [](State& s) { s.hero->flagPath = "http://flag.dds"; });
}

/** Exercise the numeric/required-field failures without GL, engine, or Rust dependencies. */
void InvalidValues()
{
	Reject("hero", [](State& s) { s.hero.reset(); });
	Reject("heroId", [](State& s) { s.hero->heroId = -1; });
	Reject("heroId", [](State& s) { s.hero->heroId = std::int64_t{1} << 31; });
	Reject("faction", [](State& s) { s.hero->faction = 3; });
	Reject("originalFaction", [](State& s) { s.hero->originalFaction = -1; });
	Reject("damageType", [](State& s) { s.hero->damageType = 5; });
	Reject("isMale", [](State& s) { s.hero->isMale.reset(); });
	Reject("isBot", [](State& s) { s.hero->isBot.reset(); });
	Reject("force", [](State& s) { s.hero->force = -1; });
	Reject("partyId", [](State& s) { s.hero->partyId = std::int64_t{1} << 31; });
	Reject("leagueIndex", [](State& s) { s.hero->leagueIndex = -1; });
	Reject("ownLeaguePlace", [](State& s) { s.hero->ownLeaguePlace = -1; });
	Reject("forceColors", [](State& s) { s.hero->forceColors.clear(); });
	Reject("forceColors", [](State& s) { s.hero->forceColors[1].force = 99; });
	Reject("forceColors", [](State& s) { s.hero->forceColors[0].color = std::int64_t{1} << 32; });
	Reject("forceColors", [](State& s) { s.hero->forceColors.resize(State::MaxTableEntries + 1); });
	Reject("leaguePlaces", [](State& s) { s.hero->leaguePlaces[0] = -1; });
	Reject("leaguePlaces", [](State& s) { s.hero->leaguePlaces.resize(State::MaxTableEntries + 1); });
	Reject("level", [](State& s) { s.values->level = -1; });
	Reject("health", [](State& s) { s.values->health = s.values->maxHealth + 1; });
	Reject("health", [](State& s) { s.values->health = -1; });
	Reject("maxHealth", [](State& s) { s.values->maxHealth = 0; });
	Reject("energy", [](State& s) { s.values->energy = s.values->maxEnergy + 1; });
	Reject("maxEnergy", [](State& s) { s.values->maxEnergy = -1; });
	Reject("isVisible", [](State& s) { s.values->isVisible.reset(); });
	Reject("isPickable", [](State& s) { s.values->isPickable.reset(); });
	Reject("isCameraLocked", [](State& s) { s.values->isCameraLocked.reset(); });
	Reject("resurrectionSeconds", [](State& s) { s.values->resurrectionSeconds = -2; });
	Reject("resurrectionSeconds", [](State& s) { s.values->resurrectionSeconds.reset(); });
	Reject("prime", [](State& s) { s.development->prime = -1; });
	Reject("gold", [](State& s) { s.development->gold = std::int64_t{1} << 31; });
	Reject("matchSeconds", [](State& s) { s.match->matchSeconds = -1; });
	Reject("creepSpawnSeconds", [](State& s) { s.match->creepSpawnSeconds.reset(); });
	Reject("creepSpawnSeconds", [](State& s) { s.match->creepSpawnSeconds = std::int64_t{1} << 31; });
	for (const double bad : {std::numeric_limits<double>::quiet_NaN(),
		std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
		std::numeric_limits<double>::max()})
	{
		Reject("rating", [bad](State& s) { s.hero->rating = bad; });
		Reject("channeling", [bad](State& s) { s.values->channeling = bad; });
		Reject("healthRegen", [bad](State& s) { s.values->healthRegen = bad; });
		Reject("energyRegen", [bad](State& s) { s.values->energyRegen = bad; });
		Reject("ultimateCooldown", [bad](State& s) { s.values->ultimateCooldown = bad; });
		Reject("humanTerrain", [bad](State& s) { s.match->humanTerrain = bad; });
		Reject("elfTerrain", [bad](State& s) { s.match->elfTerrain = bad; });
	}
	Reject("rating", [](State& s) { s.hero->rating = -0.1; });
	Reject("channeling", [](State& s) { s.values->channeling = 1.01; });
	Reject("channeling", [](State& s) { s.values->channeling = -0.01; });
	Reject("ultimateCooldown", [](State& s) { s.values->ultimateCooldown = -0.5; });
	Reject("ultimateCooldown", [](State& s) { s.values->ultimateCooldown = 1.01; });
	Reject("humanTerrain", [](State& s) { s.match->humanTerrain = -0.01; });
	Reject("elfTerrain", [](State& s) { s.match->elfTerrain = 1.01; });
}
}

/** Standalone headless executable: link only hud_calls.cpp and nlohmann/json headers. */
int main()
{
	try
	{
		Contracts();
		OmissionsAndBounds();
		InvalidTextAndPaths();
		InvalidValues();
		std::cout << "HUD calls probe: " << checks << " checks passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "HUD calls probe: " << error.what() << '\n';
		return 1;
	}
}
