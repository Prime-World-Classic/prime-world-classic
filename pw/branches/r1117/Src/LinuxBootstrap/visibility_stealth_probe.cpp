#include "../System/systemStdAfx.h"
namespace NWorld { class PFBaseUnit; }
/** Reference the engine cast, not a probe-local weak template implementation. */
template<> NWorld::PFBaseUnit* CastToUserObjectImpl<NWorld::PFBaseUnit>(
	CObjectBase*, NWorld::PFBaseUnit*, CObjectBase*);
#include "visibility_stealth_probe.h"
#include "../PF_GameLogic/StringExecutorBootstrap.h"
#include "../PF_GameLogic/PFBaseMovingUnit.h"
#include "../PF_GameLogic/PFApplMod.h"
#include "../PF_GameLogic/PFWorld.h"
#include "../PF_GameLogic/PFAIWorld.h"
#include "../PF_GameLogic/PFResourcesCollectionClient.h"
#include "../PF_GameLogic/DBAdvMap.h"
#include "../PF_GameLogic/DBSessionRoots.h"
#include "../Terrain/DBTerrain.h"
#include "../PF_GameLogic/TileMap.h"
#include "../PF_GameLogic/WarFog.h"

#include <cstdio>
#include <initializer_list>
#include <limits>
#include <cmath>
#include <vector>

#if !defined(PW_LINUX_NULL_RENDER)
#error The stealth probe requires PW_LINUX_NULL_RENDER.
#endif

namespace
{
using namespace NWorld;
const NDb::EFaction Ally = NDb::FACTION_FREEZE;
const NDb::EFaction Enemy = NDb::FACTION_BURN;
const NDb::EFaction Neutral = NDb::FACTION_NEUTRAL;

/** Release checks identify the actual rectangular world fixture. */
struct Checks
{
	int count = 0;
	int failures = 0;
	int density = 0;
	bool wide = true;
	NDb::AILogicParameters* ai = 0;
	void Check(bool result, const char* name)
	{
		++count;
		if (!result)
		{
			++failures;
			std::printf("Visibility stealth FAIL [density=%d, wide=%d]: %s\n", density, wide, name);
		}
	}
};

/** Literal DB stats exercise the normal Linux initializer. */
void AddStat(NDb::StatsContainer& stats, NDb::EStat id, const char* value)
{
	NDb::UnitStat stat;
	stat.statId = id;
	stat.value.sString = value;
	stat.increment.sString = "0";
	stats.stats.push_back(stat);
}

/** Follow visibility_lifecycle_probe without sharing or changing that probe. */
struct Fixture
{
	NDb::Ptr<NDb::Unit> unitDb;
	NDb::Ptr<NDb::AdvMapDescription> description;
	CObj<NCore::IWorldBase> owner;
	PFWorld* world = 0;
	bool loaded = false;
	explicit Fixture(Checks& checks)
	{
		checks.ai->maxTrueSightRange = 32;
		NDb::StatsContainer* stats = new NDb::StatsContainer;
		AddStat(*stats, NDb::STAT_LIFE, "100");
		AddStat(*stats, NDb::STAT_ENERGY, "100");
		AddStat(*stats, NDb::STAT_VISIBILITYRANGE, "12");
		NDb::Unit* unit = new NDb::Unit;
		unit->stats = stats;
		unitDb = unit;
		NDb::Terrain* terrain = new NDb::Terrain;
		terrain->elemXCount = checks.wide ? 12 : 8;
		terrain->elemYCount = checks.wide ? 8 : 12;
		terrain->tilesPerElement = checks.density;
		NDb::AdvMap* map = new NDb::AdvMap;
		map->terrain = terrain;
		NDb::AdvMapDescription* db = new NDb::AdvMapDescription;
		db->map = map;
		description = db;
		owner = PFWorld::CreatePFWorld();
		world = dynamic_cast<PFWorld*>(owner.GetPtr());
		loaded = world && world->LoadMap(description, 0, NCore::TPlayersStartInfo(), 0, false,
			PFResourcesCollection::TalentMap());
		checks.Check(loaded, "mock map loads");
	}
	void Tick(float dt = 0.0f) { world->Step(dt, dt); }
};

/** Count virtual reveal updates and observe existing target-drop callbacks. */
class Stationary : public PFBaseUnit
{
public:
	int reveals = 0;
	int gameplay = 0;
	int drops = 0;
	bool mounted = false;
	mutable int flightChecks = 0;
	Stationary(PFWorld* world, const NDb::Unit* db, NDb::EFaction faction, const CVec3& pos = CVec3(20, 20, 0))
		: PFBaseUnit(world, pos, db)
	{
		InitData data;
		data.faction = faction;
		data.type = NDb::UNITTYPE_BUILDING;
		data.playerId = -1;
		data.pObjectDesc = db;
		Initialize(data);
		AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
	}
	void StepInvisibility() override { ++reveals; PFBaseUnit::StepInvisibility(); }
	bool Step(float dt) override { ++gameplay; return PFBaseUnit::Step(dt); }
	void OnTargetDropped() override { ++drops; }
	bool IsMounted() const override { return mounted; }
	/** The real UnitMaskingPredicate calls IsFlying before checking faction/type. */
	bool IsFlying() const override { ++flightChecks; return PFBaseUnit::IsFlying(); }
	/** Invalid query origins must not pass through the separate movement-ingestion path. */
	void QueryPosition(const CVec3& value) { position = value; }
	void Radius(float value) { GetStat(NDb::STAT_VISIBILITYRANGE)->SetCoreValue(value); }
	void Relocate(const CVec3& value)
	{
		position = value;
		if (GetWorld()) GetWorld()->GetAIWorld()->OnUnitMove(*this);
	}
};

/** Real movement supplies detector positions before the visibility-only world phase. */
class Mover : public PFBaseMovingUnit
{
public:
	int reveals = 0;
	int gameplay = 0;
	Mover(PFWorld* world, const NDb::Unit* db, NDb::EFaction faction, const CVec3& pos)
		: PFBaseMovingUnit(world, pos, CVec2(1, 0), *db)
	{
		InitData data;
		data.faction = faction;
		data.type = NDb::UNITTYPE_CREEP;
		data.playerId = -1;
		data.pObjectDesc = db;
		Initialize(data);
		AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
	}
	void StepInvisibility() override { ++reveals; PFBaseMovingUnit::StepInvisibility(); }
	bool Step(float dt) override { ++gameplay; return PFBaseMovingUnit::Step(dt); }
};

/** Expose real applicator transitions, not an alternate reveal-state implementation. */
class Invisibility : public PFApplInvisibility
{
public:
	Invisibility(const PFApplCreatePars& pars, PFBaseUnit* receiver) : PFApplInvisibility(pars) { pReceiver = receiver; }
	void Hide() { becomeInvisible(); }
	void Show() { becomeVisible(); }
};

/** These checks fail against the no-op StepInvisibility before producer changes. */
void FlagTransitions(Checks& checks)
{
	Fixture f(checks);
	if (!f.loaded) return;
	CObj<Stationary> subject = new Stationary(f.world, f.unitDb, Ally);
	subject->OpenWarFog(Enemy, -1, 12);
	f.Tick();
	checks.Check(subject->IsVisibleForEnemy(Enemy), "ordinary unit visible in enemy fog");
	subject->AddFlag(NDb::UNITFLAG_INVISIBLE);
	f.Tick();
	checks.Check(subject->IsPlacementVisible(Enemy), "stealth does not close observer fog");
	checks.Check(!subject->IsVisibleForEnemy(Enemy) && !subject->IsVisibleForFactionInternal(Neutral), "stealth clears enemy and neutral caches");
	checks.Check(subject->IsVisibleForFaction(Ally) && subject->CanSee(*subject), "own faction and self query contracts retained");
	subject->AddFlag(NDb::UNITFLAG_INVISIBLE);
	subject->RemoveFlag(NDb::UNITFLAG_INVISIBLE);
	subject->UpdateInvisibility();
	checks.Check(!subject->IsVisibleForEnemy(-1), "stacked invisible flag remains hidden");
	subject->AddFlag(NDb::UNITFLAG_IGNOREINVISIBLE);
	subject->UpdateInvisibility();
	checks.Check(subject->IsVisibleForFactionInternal(Enemy) && subject->IsVisibleForFactionInternal(Neutral), "ignore-invisible restores both caches immediately");
	subject->RemoveFlag(NDb::UNITFLAG_IGNOREINVISIBLE);
	subject->UpdateInvisibility();
	checks.Check(!subject->IsVisibleForEnemy(-1), "removing ignore-invisible restores stealth");
	subject->RemoveFlag(NDb::UNITFLAG_INVISIBLE);
	f.Tick();
	checks.Check(subject->IsVisibleForEnemy(-1), "final invisible reference removal restores visibility");
	subject->Hide(true);
	f.Tick();
	checks.Check(!subject->IsVisibleForEnemy(-1), "existing hide flags feed reveal cache");
	subject->Hide(false);
	f.Tick();
	checks.Check(subject->IsVisibleForEnemy(-1), "existing unhide flags restore reveal cache");
	subject->CloseWarFog(true);
}

/** Match Windows opposing-faction, neutral, mounted, flying and minigame rules. */
void TrueSight(Checks& checks)
{
	Fixture f(checks);
	if (!f.loaded) return;
	CObj<Stationary> subject = new Stationary(f.world, f.unitDb, Ally);
	CObj<Stationary> detector = new Stationary(f.world, f.unitDb, Ally, CVec3(22, 20, 0));
	subject->AddFlag(NDb::UNITFLAG_INVISIBLE);
	detector->AddFlag(NDb::UNITFLAG_CANSEEINVISIBLE);
	f.Tick();
	checks.Check(!subject->IsVisibleForEnemy(-1), "allied true sight cannot reveal subject to enemies");
	detector->ChangeFaction(Enemy);
	f.Tick();
	checks.Check(subject->IsVisibleForEnemy(Enemy), "enemy true sight plus enemy fog reveals");
	checks.Check(!subject->IsVisibleForFactionInternal(Neutral), "enemy true sight does not reveal to neutrals");
	detector->CloseWarFog(true);
	checks.Check(subject->IsVisibleForEnemy(-1) && !subject->IsVisibleForEnemy(Enemy), "true sight cache does not substitute for faction fog");
	detector->mounted = true;
	detector->AddFlag(NDb::UNITFLAG_FLYING | NDb::UNITFLAG_INVISIBLE);
	f.Tick();
	checks.Check(subject->IsVisibleForEnemy(-1), "mounted flying invisible detector is eligible");
	subject->AddFlag(NDb::UNITFLAG_INMINIGAME);
	f.Tick();
	checks.Check(!subject->IsVisibleForEnemy(-1), "in-minigame excludes true sight");
	subject->AddFlag(NDb::UNITFLAG_IGNOREINVISIBLE);
	f.Tick();
	checks.Check(subject->IsVisibleForEnemy(-1), "ignore-invisible still overrides minigame stealth");
	subject->RemoveFlag(NDb::UNITFLAG_INMINIGAME | NDb::UNITFLAG_IGNOREINVISIBLE);
	detector->ChangeFaction(Neutral);
	f.Tick();
	checks.Check(!subject->IsVisibleForEnemy(-1) && subject->IsVisibleForFactionInternal(Neutral), "neutral-only true sight uses independent cache");
	CObj<Stationary> enemy = new Stationary(f.world, f.unitDb, Enemy, CVec3(24, 20, 0));
	enemy->AddFlag(NDb::UNITFLAG_CANSEEINVISIBLE);
	f.Tick();
	checks.Check(subject->IsVisibleForEnemy(-1) && subject->IsVisibleForFactionInternal(Neutral), "enemy and neutral detectors can reveal simultaneously");
	subject->ChangeFaction(Neutral);
	f.Tick();
	checks.Check(subject->IsVisibleForEnemy(-1), "neutral subject is revealed by opposing team");
	enemy->RemoveFlag(NDb::UNITFLAG_CANSEEINVISIBLE);
	f.Tick();
	checks.Check(!subject->IsVisibleForEnemy(-1), "same-faction neutral detector cannot reveal neutral subject");
	subject->ChangeFaction(Ally);
	// Gameplay death unregisters detectors; OnDie alone is only partial Linux teardown.
	detector->KillUnit(0, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
	f.Tick();
	checks.Check(!subject->IsVisibleForFactionInternal(Neutral), "unregistered detector no longer reveals");
	subject->CloseWarFog(true);
	enemy->CloseWarFog(true);
}

/** Inclusive global search radius and strict detector range are separate Windows gates. */
void RangeAndInvalid(Checks& checks)
{
	Fixture f(checks);
	if (!f.loaded) return;
	CObj<Stationary> subject = new Stationary(f.world, f.unitDb, Ally);
	CObj<Stationary> detector = new Stationary(f.world, f.unitDb, Enemy, CVec3(26, 20, 0));
	subject->AddFlag(NDb::UNITFLAG_INVISIBLE);
	detector->AddFlag(NDb::UNITFLAG_CANSEEINVISIBLE);
	detector->Radius(6);
	subject->UpdateInvisibility();
	checks.Check(!subject->IsVisibleForEnemy(-1), "detector range equality excluded");
	detector->Radius(6.25f);
	checks.ai->maxTrueSightRange = 6;
	subject->UpdateInvisibility();
	checks.Check(subject->IsVisibleForEnemy(-1), "global radius equality included");
	checks.ai->maxTrueSightRange = 5.75f;
	subject->UpdateInvisibility();
	checks.Check(!subject->IsVisibleForEnemy(-1), "global search radius bounds larger detector range");
	const float nan = std::numeric_limits<float>::quiet_NaN();
	for (float invalid : {0.0f, -1.0f, nan, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::max()})
	{
		checks.ai->maxTrueSightRange = invalid;
		subject->UpdateInvisibility();
		checks.Check(!subject->IsVisibleForEnemy(-1), "invalid global true sight radius fails closed");
		checks.ai->maxTrueSightRange = 32;
	}
	for (float radius : {0.0f, -1.0f})
	{
		detector->Radius(radius);
		subject->UpdateInvisibility();
		checks.Check(!subject->IsVisibleForEnemy(-1), "nonpositive detector radius fails closed");
	}
	/** These core inputs are clamped by ValueWithModifiers before the consumer sees them. */
	detector->CloseWarFog(true);
	for (float core : {nan, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::max()})
	{
		detector->Radius(core);
		checks.Check(detector->GetVisibilityRange() == 1e10f, "fixture: extreme core radius becomes finite live stat");
		subject->UpdateInvisibility();
		checks.Check(subject->IsVisibleForEnemy(-1), "true sight uses clamped live range without a personal fog handle");
		checks.ai->maxTrueSightRange = 5.75f;
		subject->UpdateInvisibility();
		checks.Check(!subject->IsVisibleForEnemy(-1), "global true sight radius still bounds clamped detector range");
		checks.ai->maxTrueSightRange = 32;
	}
	detector->Radius(12);
	subject->Relocate(CVec3(1, 1, 0));
	detector->Relocate(CVec3(2, 1, 0));
	subject->UpdateInvisibility();
	checks.Check(subject->IsVisibleForEnemy(-1), "near-map-edge true sight does not walk out-of-map voxels");
	CObj<Stationary> detached = new Stationary(0, f.unitDb, Ally);
	detached->AddFlag(NDb::UNITFLAG_INVISIBLE);
	detached->UpdateInvisibility();
	checks.Check(!detached->IsVisibleForEnemy(-1) && !detached->IsVisibleForFactionInternal(Neutral), "missing world cannot retain revealed stealth cache");
	subject->CloseWarFog(true);
	detector->CloseWarFog(true);
}

/** Reveal loss calls the existing target-ring hook only on enemy-cache transitions. */
void TargetLossAndApplicator(Checks& checks)
{
	Fixture f(checks);
	if (!f.loaded) return;
	CObj<Stationary> subject = new Stationary(f.world, f.unitDb, Ally);
	CObj<Stationary> attacker = new Stationary(f.world, f.unitDb, Enemy, CVec3(22, 20, 0));
	attacker->AssignTarget(subject.GetPtr(), true);
	checks.Check(attacker->GetCurrentTarget() == subject.GetPtr(), "fixture: attack ring linked");
	subject->AddFlag(NDb::UNITFLAG_INVISIBLE);
	subject->UpdateInvisibility();
	checks.Check(!attacker->GetCurrentTarget() && attacker->drops == 1, "enemy reveal loss drops attack target once");
	attacker->AssignTarget(subject.GetPtr(), true);
	subject->UpdateInvisibility();
	checks.Check(attacker->GetCurrentTarget() == subject.GetPtr() && attacker->drops == 1, "unchanged stealth does not repeat target-drop transition");
	CObj<Stationary> neutral = new Stationary(f.world, f.unitDb, Neutral, CVec3(22, 22, 0));
	neutral->AddFlag(NDb::UNITFLAG_CANSEEINVISIBLE);
	subject->UpdateInvisibility();
	checks.Check(subject->IsVisibleForFactionInternal(Neutral), "fixture: neutral-only reveal begins");
	neutral->RemoveFlag(NDb::UNITFLAG_CANSEEINVISIBLE);
	subject->UpdateInvisibility();
	checks.Check(!subject->IsVisibleForFactionInternal(Neutral) && attacker->GetCurrentTarget() == subject.GetPtr(), "neutral-only reveal loss does not trigger enemy target-drop transition");
	subject->RemoveFlag(NDb::UNITFLAG_INVISIBLE);
	subject->UpdateInvisibility();
	attacker->AssignTarget(subject.GetPtr(), true);
	const Target target(subject.GetPtr());
	PFApplCreatePars pars(CObj<PFAbilityInstance>(), target);
	pars.pWorld = f.world;
	pars.pOwner = subject.GetPtr();
	pars.pDBAppl = new NDb::InvisibilityApplicator;
	CObj<Invisibility> applicator = new Invisibility(pars, subject.GetPtr());
	applicator->Hide();
	checks.Check(!subject->IsVisibleForEnemy(-1) && attacker->drops == 2, "applicator entry refreshes cache and cancels target immediately");
	applicator->Show();
	checks.Check(subject->IsVisibleForEnemy(-1), "applicator exit refreshes cache without world tick");
	checks.Check(!attacker->GetCurrentTarget(), "visibility restoration does not reacquire dropped target");
	subject->CloseWarFog(true);
	attacker->CloseWarFog(true);
	neutral->CloseWarFog(true);
}

/** Frozen full-scan oracle for finite, ordinary inputs: independent of the voxel range walker. */
struct FullScanSight
{
	CVec2 origin;
	float radiusSquared;
	bool enemy = false;
	bool neutral = false;
	FullScanSight(const CVec3& center, float radius) : origin(center.AsVec2D()), radiusSquared(radius * radius) {}
	void operator()(PFBaseUnit& detector)
	{
		if ((enemy && neutral) || !detector.CheckFlag(NDb::UNITFLAG_CANSEEINVISIBLE)) return;
		const float distance = fabs2(detector.GetPosition().AsVec2D() - origin);
		const float range = detector.GetVisibilityRange();
		const float square = range * range;
		if (!std::isfinite(distance) || distance > radiusSquared || !std::isfinite(range) ||
			range <= 0 || !std::isfinite(square) || distance >= square) return;
		if (detector.GetFaction() == Neutral) neutral = true; else enemy = true;
	}
};

/** Compare final caches to the previous algorithm across edges, radii and changing membership. */
void BoundedTrueSight(Checks& checks)
{
	Fixture f(checks);
	if (!f.loaded) return;
	CObj<Stationary> subject = new Stationary(f.world, f.unitDb, Ally);
	subject->AddFlag(NDb::UNITFLAG_INVISIBLE);
	subject->CloseWarFog(true);
	const float width = f.world->GetTileMap()->GetSizeX() * f.world->GetTileMap()->GetTileSize();
	const float height = f.world->GetTileMap()->GetSizeY() * f.world->GetTileMap()->GetTileSize();
	std::vector<CObj<Stationary> > population;
	for (float x = 1; x < width; x += 19)
		for (float y = 1; y < height; y += 17)
		{
			const int index = static_cast<int>(population.size());
			CObj<Stationary> unit = new Stationary(f.world, f.unitDb,
				index % 3 == 0 ? Neutral : index % 3 == 1 ? Enemy : Ally, CVec3(x, y, 0));
			unit->CloseWarFog(true);
			unit->Radius(index % 2 ? 18 : 7);
			if (index % 4) unit->AddFlag(NDb::UNITFLAG_CANSEEINVISIBLE);
			unit->mounted = index % 2;
			if (index % 3) unit->AddFlag(NDb::UNITFLAG_FLYING | NDb::UNITFLAG_INVISIBLE);
			population.push_back(unit);
		}
	const int targets = NDb::SPELLTARGET_ALL | NDb::SPELLTARGET_AFFECTMOUNTED | NDb::SPELLTARGET_FLYING;
	for (float x : {0.0f, 1.0f, 10.0f, width * 0.5f, width - 1.0f, width + 1.0f})
		for (float y : {0.0f, 1.0f, 10.0f, height * 0.5f, height - 1.0f, height + 1.0f})
			for (float radius : {0.25f, 5.0f, 18.0f, 32.0f, 1000.0f})
			{
				subject->Relocate(CVec3(x, y, 100));
				checks.ai->maxTrueSightRange = radius;
				FullScanSight expected(subject->GetPosition(), radius);
				f.world->GetAIWorld()->ForAllUnits(expected, UnitMaskingPredicate(subject->GetOppositeFactionFlags(), targets));
				subject->UpdateInvisibility();
				checks.Check(subject->IsVisibleForEnemy(-1) == expected.enemy &&
					subject->IsVisibleForFactionInternal(Neutral) == expected.neutral,
					"bounded true sight equals old full scan on rectangular edges/densities");
			}
	// Count predicate work in production StepInvisibility, not an alternate query implementation.
	subject->Relocate(CVec3(20, 20, 0));
	checks.ai->maxTrueSightRange = 6;
	for (auto& unit : population) unit->flightChecks = 0;
	FullScanSight expected(subject->GetPosition(), 6);
	f.world->GetAIWorld()->ForAllUnits(expected, UnitMaskingPredicate(subject->GetOppositeFactionFlags(), targets));
	int fullCalls = 0;
	for (auto& unit : population) { fullCalls += unit->flightChecks; unit->flightChecks = 0; }
	subject->UpdateInvisibility();
	int boundedCalls = 0;
	int local = 0;
	for (auto& unit : population)
	{
		boundedCalls += unit->flightChecks;
		const bool inside = fabs2(unit->GetPosition().AsVec2D() - subject->GetPosition().AsVec2D()) <= 36.0f;
		if (inside) ++local;
		checks.Check(unit->flightChecks == (inside ? 1 : 0), "production range predicate never touches out-of-circle population");
	}
	checks.Check(fullCalls == static_cast<int>(population.size()) && boundedCalls == local && boundedCalls < fullCalls,
		"actual predicate-call reduction without timing assumptions");
	std::printf("True sight work [wide=%d density=%d population=%zu]: full predicates=%d bounded predicates=%d\n",
		checks.wide, checks.density, population.size(), fullCalls, boundedCalls);
	checks.Check(subject->IsVisibleForEnemy(-1) == expected.enemy &&
		subject->IsVisibleForFactionInternal(Neutral) == expected.neutral, "work reduction preserves both reveal caches");
	// Existing TrueSight/TickOwnership cover death, foreign worlds, genuine MoveTo and TeleportTo.
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();
	for (float invalid : {nan, inf, -inf, std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()})
		for (bool xAxis : {true, false})
		{
			subject->QueryPosition(xAxis ? CVec3(invalid, 20, 0) : CVec3(20, invalid, 0));
			for (auto& unit : population) unit->flightChecks = 0;
			subject->UpdateInvisibility();
			int calls = 0;
			for (auto& unit : population) calls += unit->flightChecks;
			checks.Check(!subject->IsVisibleForEnemy(-1) && !subject->IsVisibleForFactionInternal(Neutral) && calls == 0,
				"invalid or distant query origin cannot reveal or invoke detector predicates");
		}
	subject->QueryPosition(CVec3(20, 20, 0));
	checks.ai->maxTrueSightRange = 32;
}

/** World visibility owns one scheduled update; explicit applicator refreshes are additional. */
void TickOwnership(Checks& checks)
{
	Fixture a(checks), b(checks);
	if (!a.loaded || !b.loaded) return;
	CObj<Stationary> stationary = new Stationary(a.world, a.unitDb, Ally);
	CObj<Mover> moving = new Mover(a.world, a.unitDb, Enemy, CVec3(40, 20, 0));
	CObj<Stationary> foreign = new Stationary(b.world, b.unitDb, Enemy, CVec3(22, 20, 0));
	stationary->AddFlag(NDb::UNITFLAG_INVISIBLE);
	foreign->AddFlag(NDb::UNITFLAG_CANSEEINVISIBLE);
	int stationaryBefore = stationary->reveals, movingBefore = moving->reveals, foreignBefore = foreign->reveals;
	a.Tick();
	checks.Check(stationary->reveals == stationaryBefore + 1 && moving->reveals == movingBefore + 1, "zero-delta visibility flush updates each unit once");
	checks.Check(foreign->reveals == foreignBefore && !stationary->IsVisibleForEnemy(-1), "world phase and detector scan exclude another world");
	stationaryBefore = stationary->reveals;
	movingBefore = moving->reveals;
	moving->Step(0);
	checks.Check(moving->reveals == movingBefore, "moving gameplay Step does not duplicate world reveal update");
	moving->AddFlag(NDb::UNITFLAG_CANSEEINVISIBLE);
	moving->SetUnitSpeed(16);
	checks.Check(moving->MoveTo(CVec2(24, 20), 0, 0), "real detector movement requested");
	const int gameplayBefore = moving->gameplay;
	a.Tick(1);
	checks.Check(moving->GetPosition().x < 30, "real movement enters detector sight range");
	checks.Check(moving->gameplay == gameplayBefore + 1 && stationary->gameplay == 0, "moving gameplay once; stationary gameplay not stepped");
	checks.Check(stationary->reveals == stationaryBefore + 1 && moving->reveals == movingBefore + 1, "positive world tick refreshes moving and stationary once");
	checks.Check(stationary->IsVisibleForEnemy(-1), "reveal phase consumes post-movement detector position");
	checks.Check(moving->TeleportTo(CVec2(60, 20), false, false), "detector teleports out of range");
	stationary->UpdateInvisibility();
	checks.Check(!stationary->IsVisibleForEnemy(-1), "explicit refresh consumes teleported detector position");
	stationary->CloseWarFog(true);
	moving->CloseWarFog(true);
	foreign->CloseWarFog(true);
}
}

/** Run the simulation-only producer probe through its early dedicated dispatch. */
bool RunPrimeWorldLinuxVisibilityStealthProbe()
{
	const NDb::Ptr<NDb::SessionRoot> savedRoot = NDb::SessionRoot::GetRoot();
	NDb::SessionLogicRoot* logic = new NDb::SessionLogicRoot;
	NDb::AILogicParameters* ai = new NDb::AILogicParameters;
	logic->aiLogic = ai;
	NDb::SessionRoot* root = new NDb::SessionRoot;
	root->logicRoot = logic;
	NDb::SessionRoot::InitRoot(root);
	Checks checks;
	checks.ai = ai;
	for (bool wide : {true, false})
		for (int density : {4, 10, 20})
		{
			checks.wide = wide;
			checks.density = density;
			FlagTransitions(checks);
			TrueSight(checks);
			RangeAndInvalid(checks);
			TargetLossAndApplicator(checks);
			TickOwnership(checks);
			BoundedTrueSight(checks);
		}
	NDb::SessionRoot::InitRoot(savedRoot);
	std::printf("Visibility stealth: %d checks, %d failures\n", checks.count, checks.failures);
	return checks.failures == 0;
}
