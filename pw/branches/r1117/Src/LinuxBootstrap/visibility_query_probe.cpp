#include "../System/systemStdAfx.h"
namespace NWorld { class PFBaseUnit; }
/** Use the engine cast; a probe-local weak template could mask its null specializations. */
template<> NWorld::PFBaseUnit* CastToUserObjectImpl<NWorld::PFBaseUnit>(
	CObjectBase*, NWorld::PFBaseUnit*, CObjectBase*);
#include "visibility_query_probe.h"
#include "../PF_GameLogic/StringExecutorBootstrap.h"
#include "../PF_GameLogic/PFBaseUnit.h"
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

#if !defined(PW_LINUX_NULL_RENDER)
#error The visibility query probe requires PW_LINUX_NULL_RENDER.
#endif

namespace
{
using namespace NWorld;
const NDb::EFaction Ally = NDb::FACTION_FREEZE;
const NDb::EFaction Enemy = NDb::FACTION_BURN;
const NDb::EFaction Neutral = NDb::FACTION_NEUTRAL;

/** Keep checks enabled in release builds and identify each rectangular fixture. */
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
			std::printf("Visibility queries FAIL [density=%d, wide=%d]: %s\n", density, wide, name);
		}
	}
};

/** Literal mock stats use the same real initialization as visibility_lifecycle_probe. */
void AddStat(NDb::StatsContainer& stats, NDb::EStat id, const char* value)
{
	NDb::UnitStat stat;
	stat.statId = id;
	stat.value.sString = value;
	stat.increment.sString = "0";
	stats.stats.push_back(stat);
}

/** Own a mock DB and rectangular map; do not substitute a visibility model. */
struct Fixture
{
	NDb::Ptr<NDb::Unit> unitDb;
	NDb::Ptr<NDb::AdvMapDescription> description;
	CObj<NCore::IWorldBase> owner;
	PFWorld* world = 0;
	bool loaded = false;

	explicit Fixture(Checks& checks)
	{
		NDb::StatsContainer* stats = new NDb::StatsContainer;
		AddStat(*stats, NDb::STAT_LIFE, "100");
		AddStat(*stats, NDb::STAT_ENERGY, "100");
		AddStat(*stats, NDb::STAT_VISIBILITYRANGE, "12");
		NDb::Unit* unit = new NDb::Unit;
		unit->stats = stats;
		NDb::UnitDeathParameters* death = new NDb::UnitDeathParameters;
		death->observeOffset = 0.5f;
		unit->deathParameters = death;
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
	FogOfWar& Fog() { return *world->GetFogOfWar(); }
	SVector Tile(float x, float y) const { return world->GetTileMap()->GetTile(x, y); }
};

/** Inject cached stealth outputs only; flag production belongs to a separate chunk. */
class ProbeUnit : public PFBaseUnit
{
	NDb::EFaction vision;
public:
	ProbeUnit(PFWorld* world, const NDb::Unit* db, NDb::EFaction faction,
		const CVec3& position, NDb::EFaction fogFaction)
		: PFBaseUnit(world, position, db), vision(fogFaction)
	{
		InitData data;
		data.faction = faction;
		data.type = NDb::UNITTYPE_BUILDING;
		data.playerId = -1;
		data.pObjectDesc = db;
		Initialize(data);
	}
	NDb::EFaction GetWarfogFaction() const override { return vision; }
	void Cache(bool enemy, bool neutral) { visibleForEnemy = enemy; visibleForNeutral = neutral; }
	void Radius(float meters) { GetStat(NDb::STAT_VISIBILITYRANGE)->SetCoreValue(meters); }
	void Relocate(const CVec3& value) { position = value; }
	void VisionFaction(NDb::EFaction value) { vision = value; }
	int Id() const
	{
		return vision >= 0 && vision < visUnitData.size() ? visUnitData[vision].warFogObjectID : WAR_FOG_BAD_ID;
	}
};

/** Regress the permissive stubs before adding edge cases or cached stealth inputs. */
void PersonalAndShared(Checks& checks)
{
	Fixture fixture(checks);
	if (!fixture.loaded) return;
	CObj<ProbeUnit> viewer = new ProbeUnit(fixture.world, fixture.unitDb, Ally, CVec3(20, 20, 0), Ally);
	CObj<ProbeUnit> target = new ProbeUnit(fixture.world, fixture.unitDb, Enemy, CVec3(60, 20, 0), Enemy);
	fixture.Tick();
	checks.Check(!fixture.Fog().IsTileVisible(fixture.Tile(60, 20), Ally), "fixture: remote enemy outside ally fog");
	checks.Check(!viewer->CanSee(CVec2(60, 20)), "remote CVec2 rejected");
	checks.Check(!viewer->CanSee(CVec3(60, 20, 100)), "remote CVec3 rejected");
	checks.Check(!viewer->CanSee(fixture.Tile(60, 20)), "remote tile rejected");
	checks.Check(!viewer->CanSee(*target), "remote enemy object rejected");
	checks.Check(!target->IsPlacementVisible(Ally), "remote placement rejected");
	checks.Check(!target->IsVisibleForEnemy(Ally), "remote enemy faction rejected");
	checks.Check(!target->IsVisibleForFaction(Ally), "remote faction rejected");
	checks.Check(!viewer->CanSelectTarget(target, true), "existing target consumer honors missing faction fog");
	checks.Check(viewer->CanSelectTarget(target, false), "explicit target selection without visibility remains unchanged");
	checks.Check(target->IsVisibleForFaction(Enemy), "own faction shortcut");
	checks.Check(target->IsVisibleForFactionInternal(Ally), "internal cache ignores fog");
	checks.Check(target->IsVisibleForEnemy(-1), "negative sentinel ignores fog");
	checks.Check(viewer->CanSee(*viewer), "self shortcut");
	checks.Check(viewer->CanSee(CVec2(22, 20)) && viewer->CanSee(fixture.Tile(22, 20)), "near point and tile visible");
	checks.Check(viewer->CanSee(CVec3(22, 20, std::numeric_limits<float>::quiet_NaN())), "CVec3 ignores height like Windows");
	CObj<ProbeUnit> teammate = new ProbeUnit(fixture.world, fixture.unitDb, Ally, CVec3(60, 20, 0), Ally);
	fixture.Tick();
	checks.Check(target->IsVisibleForEnemy(Ally) && target->IsVisibleForFaction(Ally), "shared faction reveal accepted");
	checks.Check(viewer->CanSelectTarget(target, true), "existing target consumer accepts shared faction fog");
	checks.Check(!viewer->CanSee(*target) && !viewer->CanSee(*teammate), "shared sight does not extend personal sight");
	teammate->Relocate(CVec3(20, 60, 0));
	fixture.Tick();
	checks.Check((*fixture.Fog().GetVisMapMask(Ally))[fixture.Tile(60, 20).y / 4][fixture.Tile(60, 20).x / 4], "fixture: former reveal is explored");
	checks.Check(!target->IsPlacementVisible(Ally), "explored does not mean currently visible");
	target->Relocate(CVec3(22, 20, 0));
	fixture.Tick();
	checks.Check(viewer->CanSee(*target), "near enemy object visible");
	viewer->CloseWarFog(true);
	checks.Check(!viewer->CanSee(CVec2(20, 20)) && !viewer->CanSee(fixture.Tile(20, 20)), "closed observer rejects personal queries");
	checks.Check(viewer->CanSee(*viewer), "closed observer preserves self shortcut");
	viewer->OpenWarFog();
	fixture.Tick();
	checks.Check(viewer->CanSee(*target), "reopened observer restores query");
	viewer->OnUnitDie(0, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
	fixture.Tick(0.25f);
	checks.Check(viewer->IsPlacementVisible(Ally), "existing death offset retains shared reveal");
	checks.Check(!viewer->CanSee(CVec2(22, 20)), "temporary death reveal is not personal sight");
	fixture.Tick(1.0f);
	fixture.Tick();
	checks.Check(!target->IsPlacementVisible(Ally), "existing death reveal expiry reaches consumers");
	teammate->CloseWarFog(true);
	target->CloseWarFog(true);
}

/** Neutral and enemy caches are independent and do not duplicate fog or true sight. */
void CachedVisibility(Checks& checks)
{
	Fixture fixture(checks);
	if (!fixture.loaded) return;
	CObj<ProbeUnit> viewer = new ProbeUnit(fixture.world, fixture.unitDb, Ally, CVec3(20, 20, 0), Ally);
	CObj<ProbeUnit> target = new ProbeUnit(fixture.world, fixture.unitDb, Enemy, CVec3(22, 20, 0), Enemy);
	target->OpenWarFog(Neutral, -1, 12);
	fixture.Tick();
	for (bool enemy : {false, true})
		for (bool neutral : {false, true})
		{
			target->Cache(enemy, neutral);
			checks.Check(target->IsVisibleForEnemy(Ally) == enemy, "enemy placement plus enemy cache");
			checks.Check(target->IsVisibleForEnemy(Neutral) == neutral, "neutral placement plus neutral cache");
			checks.Check(target->IsVisibleForEnemy(-1) == enemy && target->IsVisibleForEnemy(-2) == enemy, "negative faction uses enemy cache");
			checks.Check(target->IsVisibleForFactionInternal(Ally) == enemy, "internal enemy cache");
			checks.Check(target->IsVisibleForFactionInternal(Neutral) == neutral, "internal neutral cache");
			checks.Check(target->IsVisibleForFaction(Enemy) && target->IsVisibleForFactionInternal(Enemy), "own faction bypasses both caches");
			checks.Check(viewer->CanSee(*target) == enemy, "personal enemy object requires cached visibility");
			checks.Check(target->IsPlacementVisible(Ally), "placement query ignores caches");
		}
	target->Cache(true, true);
	target->CloseWarFog(true);
	fixture.Tick();
	checks.Check(!target->IsVisibleForEnemy(Neutral), "neutral cache does not create neutral fog");
	checks.Check(target->IsVisibleForFactionInternal(Neutral), "neutral internal query ignores missing fog");
	target->ChangeFaction(Ally);
	target->Cache(false, false);
	checks.Check(viewer->CanSee(*target), "near allied object bypasses cached stealth");
	viewer->CloseWarFog(true);
	target->CloseWarFog(true);
}

/** Query observer ownership through GetWarfogFaction, but object allegiance through GetFaction. */
void IndependentVisionAndRange(Checks& checks)
{
	Fixture fixture(checks);
	if (!fixture.loaded) return;
	CObj<ProbeUnit> viewer = new ProbeUnit(fixture.world, fixture.unitDb, Ally, CVec3(20, 20, 0), Enemy);
	fixture.Tick();
	checks.Check(!fixture.Fog().IsTileVisible(fixture.Tile(20, 20), Ally), "fixture: independent vision has no ally fog");
	checks.Check(viewer->CanSee(CVec2(22, 20)), "personal query selects warfog faction slot");
	checks.Check(viewer->IsVisibleForFaction(Ally), "own faction still uses logical allegiance");
	viewer->CloseWarFog(true);
	viewer->OpenWarFog(Enemy, -1, 32);
	fixture.Tick();
	const CVec2 point(26, 20);
	checks.Check(fixture.Fog().CanObjectSeePosition(viewer->Id(), fixture.Tile(point.x, point.y)), "fixture: personal fog covers range boundary");
	const float padding = fixture.world->GetAIWorld()->GetMaxObjectSize() * 0.5f;
	checks.Check(padding < 6.0f, "fixture: padding leaves positive boundary radius");
	viewer->Radius(6.0f - padding);
	checks.Check(!viewer->CanSee(point), "strict Windows distance boundary excludes equality");
	viewer->Radius(6.25f - padding);
	checks.Check(viewer->CanSee(point), "distance boundary includes max object padding");
	const SVector tile = fixture.Tile(26, 20);
	const CVec2 center = fixture.world->GetTileMap()->GetPointByTile(tile);
	checks.Check(viewer->CanSee(tile) == viewer->CanSee(center), "tile overload measures distance from tile center");
	viewer->CloseWarFog(true);
}

/** Invalid contexts fail closed before coordinate conversion or observer indexing. */
void InvalidQueries(Checks& checks)
{
	Fixture fixture(checks);
	if (!fixture.loaded) return;
	CObj<ProbeUnit> viewer = new ProbeUnit(fixture.world, fixture.unitDb, Ally, CVec3(20, 20, 0), Ally);
	CObj<ProbeUnit> detached = new ProbeUnit(0, fixture.unitDb, Ally, CVec3(20, 20, 0), Ally);
	fixture.Tick();
	checks.Check(!detached->CanSee(CVec2(20, 20)) && !detached->CanSee(SVector(0, 0)), "missing world rejects personal queries");
	checks.Check(!detached->IsPlacementVisible(Ally) && !detached->IsVisibleForFaction(Enemy), "missing world rejects placement queries");
	checks.Check(detached->CanSee(*detached) && detached->IsVisibleForFaction(Ally), "missing world preserves Windows identity shortcuts");
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();
	for (float value : {-0.01f, nan, inf, std::numeric_limits<float>::max()})
	{
		checks.Check(!viewer->CanSee(CVec2(value, 20)) && !viewer->CanSee(CVec2(20, value)), "invalid world coordinate rejected");
		viewer->Relocate(CVec3(value, 20, 0));
		checks.Check(!viewer->IsPlacementVisible(Ally), "invalid placement coordinate rejected");
		checks.Check(!viewer->CanSee(CVec2(20, 20)), "invalid observer position rejected");
		viewer->Relocate(CVec3(20, 20, 0));
	}
	checks.Check(!viewer->CanSee(CVec2(fixture.world->GetMapSize().x, 20)), "exact outer x boundary rejected");
	checks.Check(!viewer->CanSee(CVec2(20, fixture.world->GetMapSize().y)), "exact outer y boundary rejected");
	checks.Check(!viewer->CanSee(SVector(-1, 0)) && !viewer->CanSee(SVector(0, -1)), "negative tile rejected before fog division");
	checks.Check(!viewer->CanSee(SVector(fixture.world->GetTileMap()->GetSizeX(), 0)), "outer x tile rejected");
	checks.Check(!viewer->CanSee(SVector(0, fixture.world->GetTileMap()->GetSizeY())), "outer y tile rejected");
	const int invalid = NDb::KnownEnum<NDb::EFaction>::SizeOf();
	checks.Check(!viewer->IsPlacementVisible(-1) && !viewer->IsPlacementVisible(invalid), "placement has no negative sentinel");
	checks.Check(!viewer->IsVisibleForEnemy(invalid) && !viewer->IsVisibleForFaction(invalid), "invalid positive faction rejected");
	checks.Check(!viewer->IsVisibleForFactionInternal(static_cast<NDb::EFaction>(-1)) &&
		!viewer->IsVisibleForFactionInternal(static_cast<NDb::EFaction>(invalid)), "internal query rejects unknown factions");
	for (NDb::EFaction faction : {static_cast<NDb::EFaction>(-1), static_cast<NDb::EFaction>(invalid)})
	{
		viewer->VisionFaction(faction);
		checks.Check(!viewer->CanSee(CVec2(20, 20)), "invalid observer faction cannot index slots");
	}
	viewer->VisionFaction(Ally);
	for (float radius : {0.0f, -1.0f, nan, inf, std::numeric_limits<float>::max()})
	{
		viewer->Radius(radius);
		checks.Check(!viewer->CanSee(CVec2(20, 20)), "invalid live radius cannot reuse old observer visibility");
	}
	viewer->CloseWarFog(true);
}
}

/** Run only through the early, non-rendering engine-probe dispatch. */
bool RunPrimeWorldLinuxVisibilityQueryProbe()
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
			PersonalAndShared(checks);
			CachedVisibility(checks);
			IndependentVisionAndRange(checks);
			InvalidQueries(checks);
		}
	NDb::SessionRoot::InitRoot(savedRoot);
	std::printf("Visibility queries: %d checks, %d failures\n", checks.count, checks.failures);
	return checks.failures == 0;
}
