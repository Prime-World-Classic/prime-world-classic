#include "../System/systemStdAfx.h"
namespace NWorld { class PFBaseHero; class PFBaseUnit; }
// Use the engine specialization; a new weak template copy could hide a link-order bug.
template<> NWorld::PFBaseHero* CastToUserObjectImpl<NWorld::PFBaseHero>(
	CObjectBase*, NWorld::PFBaseHero*, CObjectBase*);
template<> NWorld::PFBaseUnit* CastToUserObjectImpl<NWorld::PFBaseUnit>(
	CObjectBase*, NWorld::PFBaseUnit*, CObjectBase*);
#include "visibility_lifecycle_probe.h"
#include "../PF_GameLogic/StringExecutorBootstrap.h"
#include "../PF_GameLogic/PFBaseMovingUnit.h"
#include "../PF_GameLogic/PFHero.h"
#include "../PF_GameLogic/PFBaseAttackData.h"
#include "../PF_GameLogic/PFBaseUnitStates.h"
#include "../PF_GameLogic/PFWorld.h"
#include "../PF_GameLogic/PFResourcesCollectionClient.h"
#include "../PF_GameLogic/DBAdvMap.h"
#include "../PF_GameLogic/DBSessionRoots.h"
#include "../PF_GameLogic/DBHeroesList.h"
#include "../Terrain/DBTerrain.h"
#include "../PF_GameLogic/TileMap.h"
#include "../PF_GameLogic/WarFog.h"
#include "../PF_GameLogic/HeroActions.h"
#include "../PF_GameLogic/PointersHolder.h"
#include "../PF_GameLogic/PointChecking.h"
#include "../Core/WorldCommand.h"
#include "../Core/GameCommand.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>

#if !defined(PW_LINUX_NULL_RENDER)
#error The visibility lifecycle probe requires PW_LINUX_NULL_RENDER.
#endif

namespace
{
using namespace NWorld;
const NDb::EFaction Ally = NDb::FACTION_FREEZE;
const NDb::EFaction Enemy = NDb::FACTION_BURN;

/** Release-build checks remain active and identify map scale in failures. */
struct Checks
{
	int count = 0;
	int failures = 0;
	int density = 0;
	bool wide = true;
	void Check(bool result, const char* name)
	{
		++count;
		if (!result)
		{
			++failures;
			std::printf("Visibility lifecycle FAIL [density=%d, wide=%d]: %s\n", density, wide, name);
		}
	}
};

/** Literal mock DB stats exercise real initialization without numeric integration. */
void AddStat(NDb::StatsContainer& stats, NDb::EStat id, const char* value)
{
	NDb::UnitStat stat;
	stat.statId = id;
	stat.value.sString = value;
	stat.increment.sString = "0";
	stats.stats.push_back(stat);
}

void Configure(NDb::Unit& unit)
{
	NDb::StatsContainer* stats = new NDb::StatsContainer;
	AddStat(*stats, NDb::STAT_LIFE, "100");
	AddStat(*stats, NDb::STAT_ENERGY, "100");
	AddStat(*stats, NDb::STAT_VISIBILITYRANGE, "12");
	unit.stats = stats;
	NDb::UnitDeathParameters* death = new NDb::UnitDeathParameters;
	death->observeOffset = 0.5f;
	unit.deathParameters = death;
}

/** Production map fixture follows world_grid_probe, with a rectangular terrain. */
struct Fixture
{
	NDb::Ptr<NDb::AdvMapDescription> description;
	CObj<NCore::IWorldBase> owner;
	PFWorld* world = 0;
	bool loaded = false;

	explicit Fixture(Checks& checks)
	{
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
	FogOfWar& Fog() { return *world->GetFogOfWar(); }
	SVector Tile(float x, float y) const { return world->GetTileMap()->GetTile(x, y); }
	bool Visible(float x, float y, NDb::EFaction team = Ally)
	{
		return Fog().IsTileVisible(Tile(x, y), team);
	}
	int Count(float x, float y, NDb::EFaction team = Ally)
	{
		const SVector tile = Tile(x, y);
		// Native mock worlds use four normal tiles per fog cell, as PFWorld::LoadMap does.
		return (*Fog().GetVisMap(team))[tile.y / 4][tile.x / 4];
	}
};

/** Expose handles, not alternate fog logic. The faction override models creeps. */
class ProbeUnit : public PFBaseUnit
{
	bool separate;
	NDb::EFaction vision;
public:
	ProbeUnit() : PFBaseUnit(), separate(false), vision(Ally) {}
	ProbeUnit(PFWorld* world, const NDb::Unit* db, bool independentVision = false)
		: PFBaseUnit(world, CVec3(20, 20, 0), db), separate(independentVision), vision(Enemy)
	{
		InitData data;
		data.faction = Ally;
		data.type = NDb::UNITTYPE_BUILDING;
		data.playerId = -1;
		data.pObjectDesc = db;
		Initialize(data);
	}
	NDb::EFaction GetWarfogFaction() const override { return separate ? vision : GetFaction(); }
	int Slots() const { return visUnitData.size(); }
	int Id(NDb::EFaction team = Ally) const
	{
		return team >= 0 && team < visUnitData.size() ? visUnitData[team].warFogObjectID : WAR_FOG_BAD_ID;
	}
	float Timeout(NDb::EFaction team) const
	{
		return team >= 0 && team < visUnitData.size() ? visUnitData[team].timeOut : std::numeric_limits<float>::quiet_NaN();
	}
	float Radius(NDb::EFaction team) const
	{
		return team >= 0 && team < visUnitData.size() ? visUnitData[team].visRadius : std::numeric_limits<float>::quiet_NaN();
	}
	void Radius(float meters) { GetStat(NDb::STAT_VISIBILITYRANGE)->SetCoreValue(meters); }
	void Relocate(const CVec3& value) { position = value; }
};

/** Real movement and base stepping; only handle inspection is added for assertions. */
class ProbeMover : public PFBaseMovingUnit
{
public:
	ProbeMover(PFWorld* world, const NDb::Unit& db, const CVec3& position)
		: PFBaseMovingUnit(world, position, CVec2(1, 0), db)
	{
		InitData data;
		data.faction = Ally;
		data.type = NDb::UNITTYPE_CREEP;
		data.playerId = -1;
		data.pObjectDesc = &db;
		Initialize(data);
		AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
	}
	int Id(NDb::EFaction team = Ally) const
	{
		return team >= 0 && team < visUnitData.size() ? visUnitData[team].warFogObjectID : WAR_FOG_BAD_ID;
	}
	float Timeout(NDb::EFaction team) const
	{
		return team >= 0 && team < visUnitData.size() ? visUnitData[team].timeOut : std::numeric_limits<float>::quiet_NaN();
	}
};

/** Initialize, change stats, reset repeatedly and expire stationary foreign observers. */
void StationaryLifecycle(Checks& checks)
{
	Fixture fixture(checks);
	if (!fixture.loaded) return;
	NDb::Unit* raw = new NDb::Unit;
	Configure(*raw);
	NDb::Ptr<NDb::Unit> db = raw;
	CObj<ProbeUnit> unit = new ProbeUnit(fixture.world, db);
	checks.Check(unit->Slots() == NDb::KnownEnum<NDb::EFaction>::SizeOf(), "live constructor sizes slots");
	checks.Check(unit->Id() != WAR_FOG_BAD_ID, "Initialize opens own observer");
	fixture.Tick();
	checks.Check(fixture.Visible(20, 20) && !fixture.Visible(60, 20), "world tick commits bounded reveal");
	checks.Check(!fixture.Visible(20, 20, Enemy), "faction isolation");
	checks.Check(fixture.Fog().CanObjectSeePosition(unit->Id(), fixture.Tile(20, 20)), "engine personal query sees center");
	checks.Check(!fixture.Fog().CanObjectSeePosition(WAR_FOG_BAD_ID, fixture.Tile(20, 20)), "bad observer is not implicit visibility");
	CObj<ProbeUnit> teammate = new ProbeUnit(fixture.world, db);
	teammate->Relocate(CVec3(60, 20, 0));
	fixture.Tick();
	checks.Check(fixture.Visible(60, 20) && !fixture.Fog().CanObjectSeePosition(unit->Id(), fixture.Tile(60, 20)), "team reveal does not replace personal range");
	teammate->CloseWarFog(true);
	unit->OpenWarFog();
	fixture.Tick();
	checks.Check(fixture.Count(20, 20) == 1, "repeated open does not duplicate reveal");
	unit->Radius(32);
	fixture.Tick(0.125f);
	checks.Check(fixture.Visible(44, 20), "stationary live stat change updates radius");
	checks.Check(unit->Radius(Ally) == 32, "stored radius remains world meters");
	unit->OpenWarFog(Enemy, 0.5f, 12);
	unit->OpenWarFog(Enemy, 0.25f, 12);
	fixture.Tick(0.125f);
	checks.Check(fixture.Visible(20, 20, Enemy), "stationary foreign reveal opens");
	checks.Check(unit->Timeout(Enemy) == 0.375f, "stationary timeout stepped once");
	for (int i = 0; i < 3; ++i)
	{
		unit->Reset();
		fixture.Tick();
		checks.Check(fixture.Count(20, 20) == 1 && fixture.Count(20, 20, Enemy) == 1, "reset has no duplicate observer");
		checks.Check(fixture.Visible(44, 20) && unit->Timeout(Enemy) == 0.375f, "reset preserves meters and remaining time");
	}
	fixture.Tick(0.375f);
	checks.Check(!fixture.Visible(20, 20, Enemy) && unit->Id(Enemy) == WAR_FOG_BAD_ID, "foreign slot expires at zero");
	checks.Check(fixture.Visible(20, 20), "own observer remains permanent");
	unit->OpenWarFog(Enemy, -1, 12);
	unit->OpenWarFog(Enemy, 0.5f, 12);
	fixture.Tick(1);
	checks.Check(fixture.Visible(20, 20, Enemy) && unit->Timeout(Enemy) == -1, "explicit permanent reveal survives timed extension");
	unit->CloseWarFog(true);
	unit->CloseWarFog(true);
	checks.Check(!fixture.Visible(20, 20) && !fixture.Visible(20, 20, Enemy), "immediate close removes all handles once");
	fixture.Tick();
	checks.Check(!fixture.Visible(20, 20), "world phase does not implicitly reopen closed observer");
}

/** Observe normal motion and also flush after teleport without a world synchronization pass. */
void Movement(Checks& checks)
{
	Fixture fixture(checks);
	if (!fixture.loaded) return;
	NDb::Unit* raw = new NDb::Unit;
	Configure(*raw);
	NDb::Ptr<NDb::Unit> db = raw;
	CObj<ProbeMover> unit = new ProbeMover(fixture.world, *db, CVec3(20, 20, 0));
	unit->OpenWarFog(Enemy, 1.0f, 12);
	fixture.Tick();
	unit->SetUnitSpeed(32);
	checks.Check(unit->MoveTo(CVec2(60, 20), 0, 0), "engine move requested");
	fixture.Tick(0.5f);
	checks.Check(unit->GetPosition().x > 20, "engine movement advances");
	checks.Check(unit->Timeout(Enemy) == 0.5f, "moving timeout not double-stepped");
	checks.Check(unit->TeleportTo(CVec2(60, 20), false, false), "teleport succeeds");
	fixture.Fog().StepVisibility(0);
	checks.Check(fixture.Visible(60, 20) && fixture.Visible(60, 20, Enemy), "teleport moves every faction handle before world pass");
	checks.Check(!fixture.Visible(20, 20) && !fixture.Visible(20, 20, Enemy), "old teleport location no longer visible");
	checks.Check((*fixture.Fog().GetVisMapMask(Ally))[fixture.Tile(20, 20).y / 4][fixture.Tile(20, 20).x / 4], "exploration is not current visibility");
	CObj<ProbeMover> rider = new ProbeMover(fixture.world, *db, CVec3(20, 60, 0));
	unit->AttachUnit(rider.GetPtr());
	fixture.Fog().StepVisibility(0);
	checks.Check(fixture.Fog().CanObjectSeePosition(rider->Id(), fixture.Tile(60, 20)), "attachment uses position hook");
	checks.Check(!fixture.Fog().CanObjectSeePosition(rider->Id(), fixture.Tile(20, 60)), "attachment clears old personal cache");
	unit->DetachUnit();
	unit->CloseWarFog(true);
	rider->CloseWarFog(true);
}

/** Nested hide, faction changes and independent vision ownership must not leak reveals. */
void HideFactionAndDeath(Checks& checks)
{
	Fixture fixture(checks);
	if (!fixture.loaded) return;
	NDb::Unit* raw = new NDb::Unit;
	Configure(*raw);
	NDb::Ptr<NDb::Unit> db = raw;
	CObj<ProbeUnit> unit = new ProbeUnit(fixture.world, db);
	fixture.Tick();
	unit->Hide(true);
	unit->Hide(true);
	unit->ChangeFaction(Enemy);
	fixture.Tick();
	checks.Check(!fixture.Visible(20, 20) && !fixture.Visible(20, 20, Enemy), "hidden faction change does not reveal");
	unit->Hide(false);
	fixture.Tick();
	checks.Check(!fixture.Visible(20, 20, Enemy), "nested hide requires matching unhide");
	unit->Hide(false);
	fixture.Tick();
	checks.Check(!fixture.Visible(20, 20) && fixture.Visible(20, 20, Enemy), "unhide opens new faction only");
	unit->ChangeFaction(Ally);
	fixture.Tick();
	checks.Check(fixture.Visible(20, 20) && !fixture.Visible(20, 20, Enemy), "visible faction change transfers reveal");
	unit->OnUnitDie(0, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
	checks.Check(unit->Id() == WAR_FOG_BAD_ID, "death releases owned handle");
	fixture.Tick(0.25f);
	checks.Check(fixture.Visible(20, 20), "death offset creates engine temporary observer");
	fixture.Tick(0.25f);
	checks.Check(fixture.Visible(20, 20), "engine temporary lifetime reaches zero before removal tick");
	fixture.Tick();
	checks.Check(!fixture.Visible(20, 20), "next engine flush removes expired death reveal");
	unit->OpenWarFog();
	fixture.Tick();
	checks.Check(!fixture.Visible(20, 20), "dead unit cannot reopen");
	CObj<ProbeUnit> separate = new ProbeUnit(fixture.world, db, true);
	fixture.Tick();
	checks.Check(!fixture.Visible(20, 20) && fixture.Visible(20, 20, Enemy), "observer uses GetWarfogFaction override");
	separate->OnUnitDie(0, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS | PFBaseUnit::UNITDIEFLAGS_DEFERREDDEATH);
	fixture.Tick();
	checks.Check(fixture.Visible(20, 20, Enemy), "deferred death preserves observer until finalization");
	separate->OnUnitDie(0, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
	fixture.Tick(0.25f);
	checks.Check(fixture.Visible(20, 20, Enemy), "death offset follows observer faction rather than unit faction");
	fixture.Tick(1);
	fixture.Tick();
	checks.Check(!fixture.Visible(20, 20, Enemy), "independent-faction death reveal expires");
}

/** Invalid context and failed conversions must not create observers or index empty slots. */
void InvalidAndWorldIsolation(Checks& checks)
{
	CObj<ProbeUnit> empty = new ProbeUnit;
	checks.Check(empty->Slots() == NDb::KnownEnum<NDb::EFaction>::SizeOf(), "default constructor sizes slots");
	NDb::Unit* raw = new NDb::Unit;
	Configure(*raw);
	NDb::Ptr<NDb::Unit> db = raw;
	CObj<ProbeUnit> noWorld = new ProbeUnit(0, db);
	noWorld->OpenWarFog();
	noWorld->CloseWarFog();
	noWorld->Reset();
	checks.Check(noWorld->Id() == WAR_FOG_BAD_ID, "missing world keeps invalid handle");
	Fixture a(checks), b(checks);
	if (!a.loaded || !b.loaded) return;
	CObj<ProbeUnit> unit = new ProbeUnit(a.world, db);
	CObj<ProbeUnit> other = new ProbeUnit(b.world, db);
	unit->OpenWarFog(Enemy, 0.5f, 12);
	other->OpenWarFog(Enemy, 0.5f, 12);
	a.Tick(0.125f);
	checks.Check(unit->Timeout(Enemy) == 0.375f && other->Timeout(Enemy) == 0.5f, "global registry pass filters owning world");
	unit->CloseWarFog(true);
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();
	unit->OpenWarFog(static_cast<NDb::EFaction>(-1), 1, 12);
	unit->OpenWarFog(static_cast<NDb::EFaction>(NDb::KnownEnum<NDb::EFaction>::SizeOf()), 1, 12);
	for (float timeout : {0.0f, -2.0f, nan, inf}) unit->OpenWarFog(Enemy, timeout, 12);
	for (float radius : {0.0f, -1.0f, nan, inf, std::numeric_limits<float>::max()}) unit->OpenWarFog(Enemy, 1, radius);
	checks.Check(unit->Id(Enemy) == WAR_FOG_BAD_ID, "invalid faction timeout and radius rejected");
	unit->Relocate(CVec3(a.world->GetMapSize().x, 20, 0));
	unit->OpenWarFog();
	checks.Check(unit->Id() == WAR_FOG_BAD_ID, "exact outer map boundary rejected");
	unit->Relocate(CVec3(-1, 20, 0));
	unit->OpenWarFog();
	checks.Check(unit->Id() == WAR_FOG_BAD_ID, "negative coordinate rejected before truncation");
	unit->Relocate(CVec3(nan, 20, 0));
	unit->OpenWarFog();
	checks.Check(unit->Id() == WAR_FOG_BAD_ID, "nonfinite coordinate rejected before integer conversion");
	unit->OpenWarFog(Enemy, 1, 12);
	checks.Check(unit->Id(Enemy) == WAR_FOG_BAD_ID, "invalid position does not create foreign observer");
	unit->Relocate(CVec3(20, 20, 0));
	unit->OpenWarFog();
	a.Tick();
	unit->Radius(0);
	a.Tick();
	checks.Check(!a.Visible(20, 20) && unit->Id() == WAR_FOG_BAD_ID, "zero live radius removes observer");
	unit->Radius(12);
	unit->OpenWarFog();
	a.Tick();
	unit->OnDie();
	checks.Check(!a.Visible(20, 20), "final world-object death closes immediately");
	unit->OpenWarFog();
	a.Tick();
	unit->OnDestroyContents();
	checks.Check(!a.Visible(20, 20), "destruction removes still-live observer");
	other->CloseWarFog(true);
}

/** Real hero death and resurrection verify the missing reopen hook. */
void HeroRespawn(Checks& checks)
{
	Fixture fixture(checks);
	if (!fixture.loaded) return;
	NDb::Hero* raw = new NDb::Hero;
	Configure(*raw);
	NDb::Ptr<NDb::Hero> db = raw;
	PFBaseHero::SpawnInfo spawn;
	spawn.pHero = db;
	spawn.placement = Placement(CVec3(20, 20, 0), QNULL, CVec3(1, 1, 1));
	CObj<PFBaseHero> hero = new PFBaseHero(fixture.world, spawn, NDb::UNITTYPE_HEROMALE, Ally, Ally);
	hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
	PFBaseHero* exactHero = hero.GetPtr();
	checks.Check(CastToUserObject(static_cast<CObjectBase*>(exactHero), exactHero) == exactHero,
		"hero cast retains concrete object identity");
	checks.Check(CastToUserObject(static_cast<CObjectBase*>(fixture.world), exactHero) == nullptr,
		"hero cast rejects other world object types");
	CObj<NCore::WorldCommand> move = CreateCmdMoveHero(exactHero, CVec2(40, 20), false);
	CObj<NCore::PackedWorldCommand> packed = new NCore::PackedWorldCommand(move,
		fixture.world->GetPointerSerialization(), 1984, 0);
	CObj<NCore::WorldCommand> restored = packed->GetWorldCommand(fixture.world->GetPointerSerialization());
	checks.Check(restored && restored->CanExecute(), "actual packed hero command restores");
	if (restored) restored->Execute(fixture.world);
	checks.Check(hero->IsMoving(), "packed command retains its intended hero");
	hero->Stop(false);
	{
		Fixture other(checks);
		move->Execute(nullptr);
		checks.Check(!hero->IsMoving(), "move execution rejects missing world");
		hero->Stop(false);
		move->Execute(other.world);
		checks.Check(!hero->IsMoving(), "move execution rejects a foreign-world hero");
		hero->Stop(false);
		for (NCore::WorldCommand* rawCommand : {CreateCmdMoveHero(exactHero, CVec2(40, 20)),
			CreateCmdCombatMoveHero(exactHero, CVec2(40, 20)), CreateCmdStopHero(exactHero),
			CreateCmdHold(exactHero), CreateCmdCancelChannelling(exactHero)})
		{
			CObj<NCore::WorldCommand> command = rawCommand;
			CObj<NCore::PackedWorldCommand> packet = new NCore::PackedWorldCommand(command,
				fixture.world->GetPointerSerialization(), 1984, 0);
			CObj<NCore::WorldCommand> missing = packet->GetWorldCommand(other.world->GetPointerSerialization());
			checks.Check(missing && !missing->CanExecute(), "restored command rejects missing actor");
		}
	}
	// Restored unit targets must retain identity even for concrete derived classes.
	CObj<ProbeUnit> enemy = new ProbeUnit(fixture.world, db);
	enemy->ChangeFaction(Enemy);
	fixture.Tick();
	checks.Check(enemy->IsVisibleForFaction(hero->GetFaction()), "packed positive target has actual committed faction fog");
	PFBaseUnit* exactUnit = enemy.GetPtr();
	checks.Check(CastToUserObject(static_cast<CObjectBase*>(exactUnit), exactUnit) == exactUnit,
		"unit cast retains derived object identity");
	checks.Check(CastToUserObject(static_cast<CObjectBase*>(exactHero), exactUnit) == exactHero,
		"unit cast accepts a hero subclass");
	checks.Check(CastToUserObject(static_cast<CObjectBase*>(fixture.world), exactUnit) == nullptr,
		"unit cast rejects other world object types");
	CObj<NCore::WorldCommand> attack = CreateCmdAttackTarget(exactHero, exactUnit, false);
	CObj<NCore::PackedWorldCommand> packedAttack = new NCore::PackedWorldCommand(attack,
		fixture.world->GetPointerSerialization(), 1984, 0);
	CObj<NCore::WorldCommand> restoredAttack = packedAttack->GetWorldCommand(fixture.world->GetPointerSerialization());
	checks.Check(restoredAttack && restoredAttack->CanExecute(), "packed attack command restores");
	checks.Check(GetLinuxHeroGameplayCommandDiagnostics().attackTargetObjectId == exactUnit->GetObjectId(),
		"packed attack retains the exact target before execution");
	{
		CObj<PointersHolder> missingTarget = new PointersHolder(fixture.world, 0);
		missingTarget->Add(exactHero, fixture.world->GetPointerSerialization()->GetObjectID(exactHero));
		for (NCore::WorldCommand* rawCommand : {CreateCmdAttackTarget(exactHero, exactUnit),
			CreateCmdFollowUnit(exactHero, exactUnit), CreateCmdUseUnit(exactHero, exactUnit)})
		{
			CObj<NCore::WorldCommand> command = rawCommand;
			CObj<NCore::PackedWorldCommand> packet = new NCore::PackedWorldCommand(command,
				fixture.world->GetPointerSerialization(), 1984, 0);
			CObj<NCore::WorldCommand> missing = packet->GetWorldCommand(missingTarget);
			checks.Check(missing && !missing->CanExecute(), "restored command rejects missing target");
			hero->AssignTarget(exactUnit, true);
			ResetLinuxHeroGameplayCommandDiagnostics();
			if (missing) missing->Execute(fixture.world);
			checks.Check(GetLinuxHeroGameplayCommandDiagnostics().attackExecuteCalls == 0,
				"missing target does not attack the current selection");
			checks.Check(hero->GetCurrentTarget() == exactUnit, "missing target preserves current order");
			hero->DropTarget();
		}
		Fixture other(checks);
		CObj<ProbeUnit> foreign = new ProbeUnit(other.world, db);
		foreign->ChangeFaction(Enemy);
		CObj<NCore::WorldCommand> foreignAttack = CreateCmdAttackTarget(exactHero, foreign);
		checks.Check(!foreignAttack->CanExecute(), "foreign-world target rejected at admission");
		foreignAttack->Execute(fixture.world);
		checks.Check(!hero->GetCurrentTarget(), "foreign-world target rejected at execution");
		hero->DropTarget();
		foreign->CloseWarFog(true);
	}
	enemy->ChangeFaction(Ally);
	checks.Check(!attack->CanExecute(), "queued attack rejects a target that becomes allied");
	attack->Execute(fixture.world);
	checks.Check(!hero->GetCurrentTarget(), "execution does not attack a newly allied target");
	enemy->ChangeFaction(Enemy);
	enemy->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
	checks.Check(!attack->CanExecute(), "queued attack rejects a target that dies");
	attack->Execute(fixture.world);
	checks.Check(!hero->GetCurrentTarget(), "execution does not acquire a dead target");
	enemy->CloseWarFog(true);
	hero->SetForbidRespawn(true);
	fixture.Tick();
	checks.Check(fixture.Visible(20, 20), "hero initial observer");
	hero->KillUnit(0, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
	fixture.Tick(1);
	fixture.Tick();
	checks.Check(!fixture.Visible(20, 20), "dead hero observation expires");
	hero->Resurrect();
	fixture.Tick();
	checks.Check(!hero->IsDead() && fixture.Visible(20, 20), "hero resurrect reopens observer");
	hero->CloseWarFog(true);
}

/** Round-trip both pointers and issuedByScript through the real packed-command serializer. */
CObj<NCore::WorldCommand> PackedAttack(Fixture& fixture, PFBaseHero* hero, PFBaseUnit* target,
	bool script, IPointerHolder* restore = 0)
{
	CObj<NCore::WorldCommand> command = CreateCmdAttackTarget(hero, target, script);
	CObj<NCore::PackedWorldCommand> packet = new NCore::PackedWorldCommand(command,
		fixture.world->GetPointerSerialization(), 1984, 0);
	return packet->GetWorldCommand(restore ? restore : fixture.world->GetPointerSerialization());
}

/** Observe real order/resource state, including scripted priming side effects, without mocking policy. */
struct AttackSnapshot
{
	PFBaseUnit* current;
	const IPFState* state;
	bool moving;
	CVec3 heroPosition;
	CVec2 destination;
	CVec3 targetPosition;
	float life;
	bool vulnerable;
	AttackSnapshot(PFBaseHero& hero, PFBaseUnit& target)
		: current(hero.GetCurrentTarget()), state(hero.GetCurrentState()), moving(hero.IsMoving()),
		  heroPosition(hero.GetPosition()), destination(hero.GetDestination()), targetPosition(target.GetPosition()), life(target.GetLife()),
		  vulnerable(target.IsVulnerable()) {}
	void Check(Checks& checks, PFBaseHero& hero, PFBaseUnit& target) const
	{
		checks.Check(hero.GetCurrentTarget() == current && hero.GetCurrentState() == state && hero.IsMoving() == moving &&
			fabs2(hero.GetDestination() - destination) == 0,
			"rejected attack preserves exact current target, state and movement");
		checks.Check(fabs2(hero.GetPosition() - heroPosition) == 0 && fabs2(target.GetPosition() - targetPosition) == 0 &&
			target.GetLife() == life && target.IsVulnerable() == vulnerable,
			"rejected attack cannot prime, relocate, change vulnerability or damage");
	}
};

/** Execute even after rejection, as a queued command can be invalidated after its original admission. */
void RejectedAttack(Checks& checks, Fixture& fixture, NCore::WorldCommand* command,
	PFBaseHero& hero, PFBaseUnit& target, const char* reason)
{
	const AttackSnapshot before(hero, target);
	checks.Check(command && !command->CanExecute(), reason);
	before.Check(checks, hero, target);
	ResetLinuxHeroGameplayCommandDiagnostics();
	if (command) command->Execute(fixture.world);
	checks.Check(GetLinuxHeroGameplayCommandDiagnostics().attackExecuteCalls == 0,
		"invalid queued attack rejected before execution side effects");
	before.Check(checks, hero, target);
}

/** Acceptance must acquire the serialized target, not whichever target was already selected. */
void AcceptedAttack(Checks& checks, Fixture& fixture, NCore::WorldCommand* command,
	PFBaseHero& hero, PFBaseUnit& target, const char* reason)
{
	checks.Check(command && command->CanExecute(), reason);
	checks.Check(GetLinuxHeroGameplayCommandDiagnostics().attackTargetObjectId == target.GetObjectId(),
		"admitted packed attack retains exact target identity");
	ResetLinuxHeroGameplayCommandDiagnostics();
	if (command) command->Execute(fixture.world);
	checks.Check(hero.GetCurrentTarget() == &target &&
		GetLinuxHeroGameplayCommandDiagnostics().attackExecuteCalls == 1,
		"accepted packed attack acquires the requested target at execution");
}

/** Enforce manual visibility and shared legality at both stages, without broad auto-target policy. */
void AttackCommandPolicy(Checks& checks)
{
	Fixture fixture(checks);
	if (!fixture.loaded) return;
	NDb::Hero* raw = new NDb::Hero;
	Configure(*raw);
	NDb::Ptr<NDb::Hero> db = raw;
	PFBaseHero::SpawnInfo spawn;
	spawn.pHero = db;
	spawn.placement = Placement(CVec3(20, 20, 0), QNULL, CVec3(1, 1, 1));
	CObj<PFBaseHero> hero = new PFBaseHero(fixture.world, spawn, NDb::UNITTYPE_HEROMALE, Ally, Ally);
	hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
	CObj<ProbeUnit> target = new ProbeUnit(fixture.world, db);
	target->ChangeFaction(Enemy);
	target->Relocate(CVec3(24, 20, 0));
	CObj<ProbeUnit> previous = new ProbeUnit(fixture.world, db);
	previous->ChangeFaction(Enemy);
	previous->Relocate(CVec3(22, 20, 0));
	fixture.Tick();
	checks.Check(target->IsVisibleForFaction(Ally) && hero->CanAttackTarget(target), "fixture: visible attackable enemy");
	checks.Check(!hero->CanAttackFlying(), "fixture: ordinary hero cannot attack flying units");
	for (bool script : {false, true})
	{
		hero->AssignTarget(previous.GetPtr(), true);
		CObj<NCore::WorldCommand> positive = PackedAttack(fixture, hero, target, script);
		AcceptedAttack(checks, fixture, positive, *hero, *target, "visible legal attack admitted for humans and scripts");
		hero->AssignTarget(previous.GetPtr(), true);
		// Both packets are admitted while visible; only the script may retain visibility bypass.
		CObj<NCore::WorldCommand> queued = PackedAttack(fixture, hero, target, script);
		checks.Check(queued && queued->CanExecute(), "visible attack admitted before invisibility transition");
		target->AddFlag(NDb::UNITFLAG_INVISIBLE);
		target->UpdateInvisibility();
		checks.Check(target->IsPlacementVisible(Ally) && !target->IsVisibleForFaction(Ally), "fixture: stealth hidden inside real faction fog");
		checks.Check(hero->GetCurrentTarget() == previous.GetPtr(), "new target becoming invisible leaves old order available");
		CObj<NCore::WorldCommand> hidden = PackedAttack(fixture, hero, target, script);
		if (script)
		{
			AcceptedAttack(checks, fixture, hidden, *hero, *target, "script bypasses invisibility at admission");
			hero->AssignTarget(previous.GetPtr(), true);
			AcceptedAttack(checks, fixture, queued, *hero, *target, "script bypasses invisibility after queueing");
		}
		else
		{
			hero->Move(CVec2(40, 20));
			hero->AssignTarget(previous.GetPtr(), true);
			checks.Check(hero->IsMoving(), "fixture: old movement remains active while hidden command is rejected");
			RejectedAttack(checks, fixture, hidden, *hero, *target, "human attack cannot admit an invisible target");
			RejectedAttack(checks, fixture, queued, *hero, *target, "human queued attack rejects newly invisible target");
			hero->Stop(false);
		}
		hero->AssignTarget(previous.GetPtr(), true);
		target->RemoveFlag(NDb::UNITFLAG_INVISIBLE);
		target->UpdateInvisibility();
		CObj<NCore::WorldCommand> queuedFog = PackedAttack(fixture, hero, target, script);
		checks.Check(queuedFog && queuedFog->CanExecute(), "visible attack admitted before fog is removed");
		hero->CloseWarFog(true);
		fixture.Tick();
		checks.Check(target->IsVisibleForEnemy(-1) && !target->IsVisibleForFaction(Ally), "fixture: fog hidden without stealth");
		CObj<NCore::WorldCommand> fogHidden = PackedAttack(fixture, hero, target, script);
		if (script)
		{
			AcceptedAttack(checks, fixture, fogHidden, *hero, *target, "script bypasses fog at admission");
			hero->AssignTarget(previous.GetPtr(), true);
			AcceptedAttack(checks, fixture, queuedFog, *hero, *target, "script bypasses fog after queueing");
		}
		else
		{
			RejectedAttack(checks, fixture, fogHidden, *hero, *target, "human attack cannot admit fog-hidden target");
			RejectedAttack(checks, fixture, queuedFog, *hero, *target, "human queued attack rejects fog loss without replacing old order");
		}
		hero->AssignTarget(previous.GetPtr(), true);
		hero->OpenWarFog();
		fixture.Tick();
		for (int restriction = 0; restriction < 3; ++restriction)
		{
			CObj<NCore::WorldCommand> admitted = PackedAttack(fixture, hero, target, script);
			checks.Check(admitted && admitted->CanExecute(), "legal attack admitted before capability/flag transition");
			if (restriction == 0) hero->AddFlag(NDb::UNITFLAG_FORBIDATTACK);
			if (restriction == 1) target->SetVulnerable(false);
			if (restriction == 2) target->AddFlag(NDb::UNITFLAG_FLYING);
			checks.Check(restriction == 0 ? hero->CheckFlagType(NDb::UNITFLAGTYPE_FORBIDATTACK) : !hero->CanAttackTarget(target),
				"fixture: actual forbid-attack or CanAttackTarget query rejects");
			CObj<NCore::WorldCommand> blocked = PackedAttack(fixture, hero, target, script);
			RejectedAttack(checks, fixture, blocked, *hero, *target, "forbid-attack, invulnerability and unsupported flight reject admission including scripts");
			RejectedAttack(checks, fixture, admitted, *hero, *target, "forbid-attack, invulnerability and unsupported flight reject queued execution including scripts");
			if (restriction == 0) hero->RemoveFlag(NDb::UNITFLAG_FORBIDATTACK);
			if (restriction == 1) target->SetVulnerable(true);
			if (restriction == 2) target->RemoveFlag(NDb::UNITFLAG_FLYING);
		}
		// Restored pointers retain the previous hard identity boundaries for both command origins.
		for (bool retainHero : {false, true})
		{
			CObj<PointersHolder> incomplete = new PointersHolder(fixture.world, 0);
			PFBaseUnit* retained = retainHero ? static_cast<PFBaseUnit*>(hero.GetPtr()) : target.GetPtr();
			incomplete->Add(retained, fixture.world->GetPointerSerialization()->GetObjectID(retained));
			CObj<NCore::WorldCommand> missing = PackedAttack(fixture, hero, target, script, incomplete);
			RejectedAttack(checks, fixture, missing, *hero, *target, "script visibility bypass cannot repair a missing actor or target pointer");
		}
		Fixture other(checks);
		if (other.loaded)
		{
			CObj<ProbeUnit> foreign = new ProbeUnit(other.world, db);
			foreign->ChangeFaction(Enemy);
			// A world's serializer rejects foreign pointers before command policy; use a real explicit holder.
			CObj<PointersHolder> crossWorld = new PointersHolder(fixture.world, 0);
			crossWorld->Add(hero.GetPtr(), 101);
			crossWorld->Add(foreign.GetPtr(), 102);
			CObj<NCore::WorldCommand> foreignRaw = CreateCmdAttackTarget(hero, foreign, script);
			CObj<NCore::PackedWorldCommand> foreignPacket = new NCore::PackedWorldCommand(foreignRaw, crossWorld, 1984, 0);
			CObj<NCore::WorldCommand> foreignTarget = foreignPacket->GetWorldCommand(crossWorld);
			RejectedAttack(checks, fixture, foreignTarget, *hero, *foreign, "foreign-world target cannot bypass identity through script flag");
			CObj<NCore::WorldCommand> foreignExecution = PackedAttack(fixture, hero, target, script);
			checks.Check(foreignExecution && foreignExecution->CanExecute(), "fixture: local command admitted before foreign-world execution");
			const AttackSnapshot before(*hero, *target);
			ResetLinuxHeroGameplayCommandDiagnostics();
			if (foreignExecution) foreignExecution->Execute(other.world);
			before.Check(checks, *hero, *target);
			checks.Check(GetLinuxHeroGameplayCommandDiagnostics().attackExecuteCalls == 0, "foreign-world execution cannot bypass actor identity");
			foreign->CloseWarFog(true);
		}
		CObj<NCore::WorldCommand> allied = PackedAttack(fixture, hero, target, script);
		checks.Check(allied && allied->CanExecute(), "enemy admitted before faction change");
		target->ChangeFaction(Ally);
		RejectedAttack(checks, fixture, allied, *hero, *target, "script bypass cannot attack a newly allied target");
		target->ChangeFaction(Enemy);
		fixture.Tick();
	}
	// Regress the script-only priming path with an actual distant moving unit.
	CObj<ProbeMover> movingTarget = new ProbeMover(fixture.world, *db, CVec3(60, 20, 0));
	movingTarget->ChangeFaction(Enemy);
	CObj<NCore::WorldCommand> queuedPrime = PackedAttack(fixture, hero, movingTarget, true);
	checks.Check(queuedPrime && queuedPrime->CanExecute(), "script can queue a distant attackable moving target");
	movingTarget->SetVulnerable(false);
	RejectedAttack(checks, fixture, queuedPrime, *hero, *movingTarget, "script cannot prime an invulnerable moving target after queueing");
	movingTarget->SetVulnerable(true);
	movingTarget->CloseWarFog(true);
	// Faction visibility, not the hero's personal observer, authorizes a manual attack.
	hero->CloseWarFog(true);
	CObj<ProbeUnit> allyObserver = new ProbeUnit(fixture.world, db);
	fixture.Tick();
	checks.Check(!hero->CanSee(*target) && target->IsVisibleForFaction(Ally), "fixture: teammate shares fog while personal observer is closed");
	CObj<NCore::WorldCommand> sharedSight = PackedAttack(fixture, hero, target, false);
	AcceptedAttack(checks, fixture, sharedSight, *hero, *target, "manual attack accepts shared faction visibility");
	hero->AssignTarget(previous.GetPtr(), true);
	// Explicit attacks must not inherit the unrelated automatic-selection exclusion.
	target->AddFlag(NDb::UNITFLAG_FORBIDAUTOTARGETME);
	CObj<NCore::WorldCommand> explicitTarget = PackedAttack(fixture, hero, target, false);
	AcceptedAttack(checks, fixture, explicitTarget, *hero, *target, "manual attack does not accidentally adopt auto-target restrictions");
	target->RemoveFlag(NDb::UNITFLAG_FORBIDAUTOTARGETME);
	hero->AssignTarget(previous.GetPtr(), true);
	// Flying is a capability gate, not an unconditional command-level ban.
	NDb::BaseAttack* flyingDb = new NDb::BaseAttack;
	flyingDb->targetType = static_cast<NDb::ESpellTarget>(NDb::SPELLTARGET_ALL | NDb::SPELLTARGET_FLYING);
	CObj<PFBaseAttackData> flyingAttack = new PFBaseAttackData(hero.GetPtr(), flyingDb);
	CObj<PFBaseAttackData> originalAttack = hero->ReplaceBaseAttack(flyingAttack, false);
	target->AddFlag(NDb::UNITFLAG_FLYING);
	checks.Check(hero->CanAttackFlying() && hero->CanAttackTarget(target), "fixture: real attack ability permits flying target");
	CObj<NCore::WorldCommand> supportedFlight = PackedAttack(fixture, hero, target, false);
	AcceptedAttack(checks, fixture, supportedFlight, *hero, *target, "manual attack accepts supported flying target");
	target->RemoveFlag(NDb::UNITFLAG_FLYING);
	hero->ReplaceBaseAttack(originalAttack, false);
	hero->AssignTarget(previous.GetPtr(), true);
	for (bool script : {false, true})
	{
		CObj<ProbeUnit> doomed = new ProbeUnit(fixture.world, db);
		doomed->ChangeFaction(Enemy);
		fixture.Tick();
		CObj<NCore::WorldCommand> dying = PackedAttack(fixture, hero, doomed, script);
		checks.Check(dying && dying->CanExecute(), "living target admitted before death");
		doomed->KillUnit(0, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
		RejectedAttack(checks, fixture, dying, *hero, *doomed, "script visibility bypass never bypasses target liveness");
	}
	CObj<NCore::WorldCommand> humanActorDies = PackedAttack(fixture, hero, target, false);
	CObj<NCore::WorldCommand> scriptActorDies = PackedAttack(fixture, hero, target, true);
	checks.Check(humanActorDies && humanActorDies->CanExecute() && scriptActorDies && scriptActorDies->CanExecute(),
		"living actor commands admitted before death");
	hero->SetForbidRespawn(true);
	hero->KillUnit(0, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
	RejectedAttack(checks, fixture, humanActorDies, *hero, *target, "human queued attack rejects dead actor");
	RejectedAttack(checks, fixture, scriptActorDies, *hero, *target, "script visibility bypass never bypasses actor liveness");
	allyObserver->CloseWarFog(true);
	previous->CloseWarFog(true);
	target->CloseWarFog(true);
	hero->CloseWarFog(true);
}
}

/** Invoke from an isolated engine probe process after applying the observer patch. */
/** Serialized attack orders must leave combat to simulation, with no bootstrap priming. */
static void AttackOrderScheduling(Checks& checks)
{
	Fixture fixture(checks); if (!fixture.loaded) return;
	for (bool script : {false, true})
	{
		NDb::Hero* raw = new NDb::Hero; Configure(*raw);
		NDb::StatsContainer* stats = const_cast<NDb::StatsContainer*>(raw->stats.GetPtr());
		AddStat(*stats, NDb::STAT_RANGE, "4"); AddStat(*stats, NDb::STAT_BASEATTACK, "20");
		NDb::BaseAttack* attack = new NDb::BaseAttack;
		attack->useRange.sString = "4"; attack->manaCost.sString = "0";
		raw->attackAbility = attack;
		NDb::Ptr<NDb::Hero> db = raw;
		PFBaseHero::SpawnInfo spawn; spawn.pHero = db;
		spawn.placement = Placement(CVec3(20, 20, 0), QNULL, CVec3(1, 1, 1));
		CObj<PFBaseHero> hero = new PFBaseHero(fixture.world, spawn, NDb::UNITTYPE_HEROMALE, Ally, Ally);
		spawn.placement.pos.x = 28;
		CObj<PFBaseHero> target = new PFBaseHero(fixture.world, spawn, NDb::UNITTYPE_HEROMALE, Enemy, Enemy);
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); target->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		fixture.Tick(); hero->GetAttackAbility()->RestartCooldown(0.5f);
		hero->SetUnitSpeed(4); hero->SetMoveFlags(MOVE_FLAG_NO_COLLIDE);
		const float prepared = hero->GetAttackAbility()->GetPreparedness();
		const CVec3 position = target->GetPosition(); const float health = target->GetHealth();
		CObj<NCore::WorldCommand> command = PackedAttack(fixture, hero, target, script);
		checks.Check(command && command->CanExecute(), "real packed distant attack is admitted");
		if (command) command->Execute(fixture.world);
		checks.Check(fabs2(target->GetPosition()-position) == 0, "attack order never teleports its target");
		checks.Check(target->GetHealth() == health, "attack command never deals command-time fallback damage");
		checks.Check(hero->GetAttackAbility()->GetPreparedness() == prepared, "attack order does not reset cooldown");
		checks.Check(hero->HasQueuedStates(), "attack command queues the existing combat state");
		hero->Step(0.1f);
		checks.Check(dynamic_cast<const PFBaseUnitAttackState*>(hero->GetCurrentState()) != nullptr,
			"simulation owns the queued attack state");
		for (int i = 0; i < 30; ++i) hero->Step(0.1f);
		checks.Check(hero->IsTargetInAttackRange(target.GetPtr(), true) && !hero->IsMoving(),
			"native attack order approaches and stops within attack range");
		checks.Check(target->GetHealth() == health, "attack without authored damage remains nondamaging in simulation");
		CObj<NCore::WorldCommand> stop = CreateCmdStopHero(hero.GetPtr()); stop->Execute(fixture.world);
		hero->Step(0.1f);
		checks.Check(!hero->GetCurrentTarget() && hero->IsIdle() && !hero->IsMoving(), "stop cancels the combat order");
		command->Execute(fixture.world); hero->Step(0.1f);
		CObj<NCore::WorldCommand> move = CreateCmdMoveHero(hero.GetPtr(), CVec2(20, 25)); move->Execute(fixture.world);
		checks.Check(!hero->GetCurrentTarget() && !dynamic_cast<const PFBaseUnitAttackState*>(hero->GetCurrentState()),
			"movement replaces the old attack order");
	}
}

/** Current attack states revalidate combat legality and stop when their order ends. */
static void AttackStateLifecycle(Checks& checks)
{
	Fixture fixture(checks); if (!fixture.loaded) return;
	for (int reason = 0; reason < 5; ++reason)
	for (bool script : {false, true})
	{
		NDb::Hero* raw = new NDb::Hero; Configure(*raw);
		NDb::StatsContainer* stats = const_cast<NDb::StatsContainer*>(raw->stats.GetPtr());
		AddStat(*stats, NDb::STAT_RANGE, "4");
		NDb::Ptr<NDb::Hero> db = raw;
		PFBaseHero::SpawnInfo spawn; spawn.pHero = db;
		spawn.placement = Placement(CVec3(20, 20, 0), QNULL, CVec3(1, 1, 1));
		CObj<PFBaseHero> hero = new PFBaseHero(fixture.world, spawn, NDb::UNITTYPE_HEROMALE, Ally, Ally);
		spawn.placement.pos.x = 28;
		CObj<PFBaseHero> target = new PFBaseHero(fixture.world, spawn, NDb::UNITTYPE_HEROMALE, Enemy, Enemy);
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); target->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		fixture.Tick();
		hero->EnqueueState(new PFBaseUnitAttackState(fixture.world, hero.GetPtr(), target.GetPtr(), true, false, script, false, reason != 4), true);
		hero->SetUnitSpeed(4); hero->SetMoveFlags(MOVE_FLAG_NO_COLLIDE);
		hero->Step(0.1f);
		if (reason == 0) target->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
		if (reason == 1) target->ChangeFaction(Ally);
		if (reason == 2) { target->AddFlag(NDb::UNITFLAG_INVISIBLE); PFBaseUnit::StepLinuxWarFog(fixture.world, 0.0f); }
		if (reason == 3) hero->AddFlag(NDb::UNITFLAG_FORBIDMOVE);
		hero->Step(0.1f);
		const bool survives = reason == 2 && script;
		if (hero->IsIdle() == survives || (!survives && (hero->GetCurrentTarget() || hero->IsMoving())))
			std::printf("Attack state context: reason=%d script=%d idle=%d moving=%d visible=%d forbidMove=%d\n",
				reason, script, hero->IsIdle(), hero->IsMoving(), target->IsVisibleForFaction(Ally),
				hero->CheckFlagType(NDb::UNITFLAGTYPE_FORBIDMOVE));
		checks.Check(hero->IsIdle() != survives, "attack state ends on illegal target or forbidden chase");
		checks.Check(survives || (!hero->GetCurrentTarget() && !hero->IsMoving()),
			"ended attack order drops target and movement together");
		hero->Cleanup(); hero->Stop(false);
	}
}

#include "movement_order_tests.inc"
#include "building_order_tests.inc"

bool RunPrimeWorldLinuxVisibilityLifecycleProbe()
{
	const NDb::Ptr<NDb::SessionRoot> savedRoot = NDb::SessionRoot::GetRoot();
	NDb::SessionLogicRoot* logic = new NDb::SessionLogicRoot;
	logic->aiLogic = new NDb::AILogicParameters;
	NDb::SessionRoot* root = new NDb::SessionRoot;
	root->logicRoot = logic;
	NDb::SessionRoot::InitRoot(root);
	Checks checks;
	for (bool wide : {true, false})
		for (int density : {4, 10, 20})
		{
			checks.wide = wide;
			checks.density = density;
			StationaryLifecycle(checks);
			Movement(checks);
			HideFactionAndDeath(checks);
			InvalidAndWorldIsolation(checks);
			HeroRespawn(checks);
			AttackCommandPolicy(checks);
			AttackOrderScheduling(checks);
			AttackStateLifecycle(checks);
			AuthoredTargetRanges(checks);
			DefaultTargetRanges(checks);
			AttackFormulaPolicy(checks);
			PointMoveLifecycle(checks);
			ChaseLifecycle(checks);
			PathMoveLifecycle(checks);
			BuildingMoveLifecycle(checks);
		}
	NDb::SessionRoot::InitRoot(savedRoot);
	std::printf("Visibility lifecycle: %d checks, %d failures\n", checks.count, checks.failures);
	return checks.failures == 0;
}
