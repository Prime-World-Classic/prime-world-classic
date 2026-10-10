#include "../System/systemStdAfx.h"
#include "hero_presentation_probe.h"
#include "../System/Crc32Checksum.h"
#include "../Render/DBRenderResources.h"
#include "../PF_GameLogic/StringExecutorBootstrap.h"
#include "../PF_GameLogic/DBUnit.h"
#include "../PF_GameLogic/DBHeroesList.h"
#include "../PF_GameLogic/DBAdvMap.h"
#include "../PF_GameLogic/DBGameLogic.h"
#include "../PF_GameLogic/DBServer.h"
#include "../PF_GameLogic/DBSessionRoots.h"
#include "../PF_GameLogic/PFHero.h"
#include "../PW_Client/LoadingHeroes.h"
#include "../PW_Client/LoadingFlashInterface.h"
#include "../PW_Client/LoadingScreenLogic.h"

namespace
{
/// Give each mock texture a distinct identity and path without opening an asset file.
NDb::Ptr<NDb::Texture> MakeTexture(const char* filename)
{
	NDb::Texture* texture = new NDb::Texture;
	texture->textureFileName = filename;
	return texture;
}

void Check(bool condition, const char* name, int* failures)
{
	printf("Hero presentation probe: %s=%s\n", name, condition ? "pass" : "FAIL");
	if (!condition)
		++*failures;
}

/// Exercise the production force policy and Flash capture before and after an eligible teammate.
void CheckForceDisplay(const char* name, const NDb::AdvMapDescription* map,
	const Game::HeroInfo& firstHero, const wchar_t* left, const wchar_t* right, int* failures)
{
	Strong<Game::LoadingScreenLogic> logic = new Game::LoadingScreenLogic(0, 0, false, false);
	logic->OnLoadedScreenLayout();
	Game::LoadingFlashInterface* flash = logic->GetLoadingFlashInterface();
	if (!flash)
	{
		Check(false, name, failures);
		return;
	}
	logic->SetMap(map, 0);
	NCore::PlayerStartInfo player;
	player.userID = 11;
	player.teamID = player.originalTeamID = NCore::ETeam::Team1;
	player.nickname = L"First";
	logic->AddPlayer(player.userID, player, firstHero);
	logic->ShowTeamForce();
	Check(flash->GetLeftTeamForce() == left &&
		flash->GetRightTeamForce() == (left[0] ? L"0" : L""),
		(string(name) + "-initial").c_str(), failures);

	Game::HeroInfo secondHero;
	secondHero.heroId = firstHero.heroId;
	secondHero.team = secondHero.originalTeam = NCore::ETeam::Team2;
	secondHero.basket = NCore::EBasket::Normal;
	secondHero.force = 74.9f;
	player.userID = 12;
	player.teamID = player.originalTeamID = NCore::ETeam::Team2;
	player.nickname = L"Second";
	logic->AddPlayer(player.userID, player, secondHero);
	logic->ShowTeamForce();
	Check(flash->GetLeftTeamForce() == left && flash->GetRightTeamForce() == right,
		(string(name) + "-after-eligible").c_str(), failures);
}

/// The standalone headless probe owns this mock session root; no assets or Flash VM are needed.
void ProbeLoadingForceDisplay(const NDb::HeroesDB* catalog, uint heroId, int* failures)
{
	NDb::AILogicParameters* ai = new NDb::AILogicParameters;
	ai->levelToExperienceTable = new NDb::DBLevelToExperience;
	NDb::SessionLogicRoot* sessionLogic = new NDb::SessionLogicRoot;
	sessionLogic->aiLogic = ai;
	sessionLogic->heroes = catalog;
	NDb::SessionRoot* session = new NDb::SessionRoot;
	session->logicRoot = sessionLogic;
	NDb::SessionRoot::InitRoot(session);

	NDb::MapMMakingSettings* matchmaking = new NDb::MapMMakingSettings;
	NDb::AdvMapDescription* map = new NDb::AdvMapDescription;
	NDb::Ptr<NDb::AdvMapDescription> mapOwner = map;
	map->teamSize = 5;
	map->matchmakingSettings = matchmaking;
	for (int i = 0; i < 3; ++i)
	{
		NDb::MMakingRank rank;
		rank.lowRating = i * 1000;
		rank.useForceMM = i != 1;
		matchmaking->ranks.push_back(rank);
	}

	struct ForceCase
	{
		const char* name;
		NDb::EMapType mapType;
		int rating;
		bool novice;
		NCore::EBasket::Enum basket;
		uint party;
		bool visible;
	};
	const ForceCase cases[] = {
		{"force-eligible", NDb::MAPTYPE_PVP, 0, false, NCore::EBasket::Normal, 0, true},
		{"force-novice", NDb::MAPTYPE_PVP, 0, true, NCore::EBasket::Normal, 0, false},
		{"force-newbie-basket", NDb::MAPTYPE_PVP, 0, false, NCore::EBasket::Newbie, 0, false},
		{"force-party", NDb::MAPTYPE_PVP, 0, false, NCore::EBasket::Normal, 7, false},
		{"force-non-pvp", NDb::MAPTYPE_COOPERATIVE, 0, false, NCore::EBasket::Normal, 0, false},
		{"force-before-rank-boundary", NDb::MAPTYPE_PVP, 999, false, NCore::EBasket::Normal, 0, true},
		{"force-disabled-rank", NDb::MAPTYPE_PVP, 1000, false, NCore::EBasket::Normal, 0, false},
		{"force-reenabled-rank", NDb::MAPTYPE_PVP, 2000, false, NCore::EBasket::Normal, 0, true}
	};
	Game::HeroInfo hero;
	hero.heroId = heroId;
	hero.team = hero.originalTeam = NCore::ETeam::Team1;
	hero.force = 101.9f;
	for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		map->mapType = cases[i].mapType;
		hero.raiting = cases[i].rating;
		hero.isNovice = cases[i].novice;
		hero.basket = cases[i].basket;
		hero.partyId = cases[i].party;
		CheckForceDisplay(cases[i].name, map, hero,
			cases[i].visible ? L"202" : L"", cases[i].visible ? L"148" : L"", failures);
	}

	hero.raiting = 0;
	hero.force = 0;
	CheckForceDisplay("force-zero-still-eligible", map, hero, L"0", L"148", failures);
	hero.force = 101.9f;
	matchmaking->ranks.clear();
	CheckForceDisplay("force-empty-ranks", map, hero, L"", L"", failures);
	map->matchmakingSettings = 0;
	// Windows skips accumulation without matchmaking settings and retains its initial visibility.
	CheckForceDisplay("force-no-matchmaking", map, hero, L"0", L"0", failures);
	NDb::SessionRoot::InitRoot(0);
}
}

bool RunPrimeWorldLinuxHeroPresentationProbe()
{
	int failures = 0;
	NDb::Hero* hero = new NDb::Hero;
	NDb::Ptr<NDb::Hero> owner = hero;
	hero->id = "portrait-fixture";
	hero->image = MakeTexture("mock/generic.dds");
	hero->heroImageA = MakeTexture("mock/freeze.dds");
	hero->heroImageB = MakeTexture("mock/burn.dds");
	hero->minimapIconA = MakeTexture("mock/class-a.dds");
	hero->minimapIconB = MakeTexture("mock/class-b.dds");
	NDb::HeroSkin* decoy = new NDb::HeroSkin;
	decoy->persistentId = "decoy";
	decoy->heroImageA = MakeTexture("mock/decoy.dds");
	hero->heroSkins.push_back(decoy);
	hero->heroSkins.push_back(NDb::Ptr<NDb::HeroSkin>());
	NDb::HeroSkin* partial = new NDb::HeroSkin;
	partial->persistentId = "partial";
	partial->heroImageA = MakeTexture("mock/partial-a.dds");
	hero->heroSkins.push_back(partial);
	NDb::HeroSkin* full = new NDb::HeroSkin;
	full->persistentId = "full";
	full->heroImageA = MakeTexture("mock/full-a.dds");
	full->heroImageB = MakeTexture("mock/full-b.dds");
	hero->heroSkins.push_back(full);

	using NWorld::PFBaseHero;
	Check(!PFBaseHero::GetHeroSkin(hero, ""), "empty-skin-is-base", &failures);
	Check(!PFBaseHero::GetHeroSkin(hero, "default"), "default-skin-is-base", &failures);
	Check(!PFBaseHero::GetHeroSkin(hero, "missing"), "unknown-skin-is-base", &failures);
	Check(PFBaseHero::GetHeroSkin(hero, "partial") == partial, "skin-id-not-first-entry", &failures);
	Check(PFBaseHero::GetHeroSkin(hero, "full") == full, "skip-null-skin-entry", &failures);
	Check(!PFBaseHero::GetHeroSkin(0, "full"), "null-hero-skin", &failures);
	struct PortraitCase
	{
		const char* name;
		NDb::EFaction faction;
		const char* skin;
		const NDb::Texture* expected;
	};
	const PortraitCase cases[] = {
		{"freeze-base", NDb::FACTION_FREEZE, "", hero->heroImageA},
		{"burn-base", NDb::FACTION_BURN, "", hero->heroImageB},
		{"neutral-base", NDb::FACTION_NEUTRAL, "", hero->image},
		{"default-id-base", NDb::FACTION_FREEZE, "default", hero->heroImageA},
		{"unknown-id-base", NDb::FACTION_BURN, "missing", hero->heroImageB},
		{"partial-skin-override", NDb::FACTION_FREEZE, "partial", partial->heroImageA},
		{"partial-skin-fallback", NDb::FACTION_BURN, "partial", hero->heroImageB},
		{"full-skin-freeze", NDb::FACTION_FREEZE, "full", full->heroImageA},
		{"full-skin-burn", NDb::FACTION_BURN, "full", full->heroImageB},
		{"neutral-ignores-skin", NDb::FACTION_NEUTRAL, "full", hero->image}
	};
	for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
		Check(PFBaseHero::GetUiAvatarImage(hero, cases[i].faction, cases[i].skin) == cases[i].expected,
			cases[i].name, &failures);
	Check(!PFBaseHero::GetUiAvatarImage(0, NDb::FACTION_FREEZE, "full"), "null-hero-portrait", &failures);

	// Construct real native heroes with mock DB data, without a world or graphics context.
	PFBaseHero::SpawnInfo spawn;
	spawn.pHero = owner;
	spawn.placement = Placement(VNULL3, QNULL, CVec3(1.0f, 1.0f, 1.0f));
	spawn.playerInfo.heroSkin = "partial";
	CObj<PFBaseHero> instance = new PFBaseHero(0, spawn, NDb::UNITTYPE_HEROMALE,
		NDb::FACTION_FREEZE, NDb::FACTION_NEUTRAL);
	Check(instance->GetOriginalFaction() == NDb::FACTION_FREEZE &&
		instance->GetUiAvatarImage() == partial->heroImageA, "instance-default-original-faction", &failures);
	instance->ChangeFaction(NDb::FACTION_BURN);
	Check(instance->GetFaction() == NDb::FACTION_BURN &&
		instance->GetUiAvatarImage() == partial->heroImageA, "instance-preserves-original-portrait", &failures);
	instance->SetSkin("full");
	Check(instance->GetUiAvatarImage() == full->heroImageA, "instance-skin-change", &failures);
	CObj<PFBaseHero> clone = new PFBaseHero(0, spawn, NDb::UNITTYPE_HEROMALE,
		NDb::FACTION_FREEZE, NDb::FACTION_BURN);
	Check(clone->GetOriginalFaction() == NDb::FACTION_BURN &&
		clone->GetUiAvatarImage() == hero->heroImageB, "instance-explicit-original-faction", &failures);

	// Exercise the production loading handoff, including a different original/manoeuvre faction.
	NDb::HeroesDB* catalog = new NDb::HeroesDB;
	NDb::Ptr<NDb::HeroesDB> catalogOwner = catalog;
	catalog->heroes.push_back(owner);
	Strong<Game::LoadingFlashInterface> flash = new Game::LoadingFlashInterface(0, "");
	Strong<Game::LoadingHeroes> loading = new Game::LoadingHeroes(flash, catalogOwner);
	Game::HeroInfo info;
	info.heroId = Crc32Checksum().AddString(hero->id.c_str()).Get();
	info.partyId = 7;
	string flag;
	wstring tooltip;
	loading->AddUser(11, L"Player", true, NCore::ETeam::Team1, NCore::ETeam::Team2, info, flag, tooltip, "partial", 0);
	loading->AddUser(12, L"Bot", false, NCore::ETeam::Team1, NCore::ETeam::None, info, flag, tooltip, "full", 0);
	const vector<Game::LoadingFlashHeroState>& states = flash->GetHeroes();
	Check(states.size() == 2 && states[0].iconPath == "mock/burn.dds" &&
		states[0].classIcon == "mock/class-b.dds" && states[0].faction == NDb::FACTION_FREEZE &&
		states[0].playerName == L"Player" && states[0].partyId == 7,
		"loading-original-faction", &failures);
	Check(states.size() == 2 && states[1].iconPath == "mock/full-a.dds" &&
		states[1].classIcon == "mock/class-a.dds" && !states[1].isMale,
		"loading-selected-skin", &failures);
	loading->AddUser(12, L"Duplicate", true, NCore::ETeam::Team2, NCore::ETeam::None, info, flag, tooltip, "", 0);
	Check(states.size() == 2 && states[1].iconPath == "mock/full-a.dds", "loading-duplicate-unchanged", &failures);
	ProbeLoadingForceDisplay(catalog, info.heroId, &failures);

	hero->heroImageB = 0;
	Check(!PFBaseHero::GetUiAvatarImage(hero, NDb::FACTION_BURN, "partial"), "missing-portrait-not-generic", &failures);
	printf("Hero presentation probe: failures=%d\n", failures);
	return failures == 0;
}
