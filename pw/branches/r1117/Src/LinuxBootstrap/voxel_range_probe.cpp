#include "../System/systemStdAfx.h"
namespace NWorld { class PFBaseUnit; }
/** Reference the engine cast instead of emitting a competing weak probe cast. */
template<> NWorld::PFBaseUnit* CastToUserObjectImpl<NWorld::PFBaseUnit>(
	CObjectBase*, NWorld::PFBaseUnit*, CObjectBase*);
#include "voxel_range_probe.h"
#include "../PF_GameLogic/StringExecutorBootstrap.h"
#include "../PF_GameLogic/PFBaseUnit.h"
#include "../PF_GameLogic/PFWorld.h"
#include "../PF_GameLogic/PFAIWorld.h"
#include "../PF_GameLogic/PFResourcesCollectionClient.h"
#include "../PF_GameLogic/DBAdvMap.h"
#include "../PF_GameLogic/DBSessionRoots.h"
#include "../Terrain/DBTerrain.h"
#include "../PF_GameLogic/TileMap.h"

#include <algorithm>
#include <cerrno>
#include <cfenv>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <vector>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#if !defined(__linux__) || !defined(PW_LINUX_NULL_RENDER)
#error The voxel range probe requires the Linux null-render engine.
#endif

namespace
{
using namespace NWorld;

/** Release checks retain fixture context without depending on engine assertions. */
struct Checks
{
	int count = 0;
	int failures = 0;
	bool wide = true;
	int density = 4;
	void Check(bool result, const char* name)
	{
		++count;
		if (!result)
		{
			++failures;
			std::printf("Voxel range FAIL [wide=%d density=%d]: %s\n", wide, density, name);
		}
	}
};

/** Real objects use world-assigned IDs and normal AI registration. */
class Object : public PFLogicObject
{
public:
	Object(PFWorld* world, const CVec3& pos) : PFLogicObject(world, pos, 0)
	{
		InitData data;
		data.type = NDb::UNITTYPE_TREE;
		Initialize(data);
	}
};

/** Movement goes through the actual voxel unlink/reinsert path. */
class Unit : public PFBaseUnit
{
public:
	Unit(PFWorld* world, const NDb::Unit* db, const CVec3& pos) : PFBaseUnit(world, pos, db)
	{
		InitData data;
		data.type = NDb::UNITTYPE_BUILDING;
		data.faction = NDb::FACTION_FREEZE;
		data.pObjectDesc = db;
		Initialize(data);
		AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		CloseWarFog(true);
	}
	void Move(const CVec3& pos)
	{
		position = pos;
		GetWorld()->GetAIWorld()->OnUnitMove(*this);
	}
};

/** Keep mock DB/world lifetimes identical to the existing visibility probes. */
struct Fixture
{
	NDb::Ptr<NDb::Unit> db;
	NDb::Ptr<NDb::AdvMapDescription> description;
	CObj<NCore::IWorldBase> owner;
	PFWorld* world = 0;
	PFAIWorld* map = 0;
	std::vector<CObj<Unit> > units;
	std::vector<CObj<Object> > objects;
	float width = 0;
	float height = 0;
	explicit Fixture(Checks& checks)
	{
		NDb::StatsContainer* stats = new NDb::StatsContainer;
		for (NDb::EStat id : {NDb::STAT_LIFE, NDb::STAT_ENERGY, NDb::STAT_VISIBILITYRANGE})
		{
			NDb::UnitStat stat;
			stat.statId = id;
			stat.value.sString = id == NDb::STAT_VISIBILITYRANGE ? "0" : "100";
			stat.increment.sString = "0";
			stats->stats.push_back(stat);
		}
		NDb::Unit* unitDb = new NDb::Unit;
		unitDb->stats = stats;
		db = unitDb;
		NDb::Terrain* terrain = new NDb::Terrain;
		terrain->elemXCount = checks.wide ? 12 : 8;
		terrain->elemYCount = checks.wide ? 8 : 12;
		terrain->tilesPerElement = checks.density;
		NDb::AdvMap* mapDb = new NDb::AdvMap;
		mapDb->terrain = terrain;
		NDb::AdvMapDescription* desc = new NDb::AdvMapDescription;
		desc->map = mapDb;
		description = desc;
		owner = PFWorld::CreatePFWorld();
		world = dynamic_cast<PFWorld*>(owner.GetPtr());
		const bool loaded = world && world->LoadMap(description, 0, NCore::TPlayersStartInfo(), 0, false,
			PFResourcesCollection::TalentMap());
		checks.Check(loaded, "rectangular engine map loads");
		if (!loaded) return;
		map = world->GetAIWorld();
		width = world->GetTileMap()->GetSizeX() * world->GetTileMap()->GetTileSize();
		height = world->GetTileMap()->GetSizeY() * world->GetTileMap()->GetTileSize();
	}
	Unit* AddUnit(float x, float y)
	{
		units.push_back(CObj<Unit>(new Unit(world, db, CVec3(x, y, 0))));
		return units.back().GetPtr();
	}
	void AddObject(float x, float y)
	{
		objects.push_back(CObj<Object>(new Object(world, CVec3(x, y, 0))));
	}
};

/** Store exact identity/order; no substitute spatial index is used by the oracle. */
struct Collect
{
	std::vector<PFLogicObject*> seen;
	void operator()(PFLogicObject& object) { seen.push_back(&object); }
};

/** Select both public ring kinds and count predicate invocations. */
struct Predicate
{
	bool objects = true;
	bool units = true;
	bool accept = true;
	mutable int calls = 0;
	bool TraverseObjects() const { return objects; }
	bool TraverseUnits() const { return units; }
	bool operator()(const PFLogicObject&) const { ++calls; return accept; }
};

/** Preserve the legacy inclusive float distance, including float boundary rounding. */
bool Inside(const PFLogicObject* object, const CVec3& center, float radius)
{
	const float x = object->GetPosition().x - center.x;
	const float y = object->GetPosition().y - center.y;
	return x * x + y * y <= radius * radius;
}

/** The range walk is column-major, then ID-sorted inside each intrusive ring. */
bool RangeOrder(const PFLogicObject* a, const PFLogicObject* b)
{
	const int ax = static_cast<int>(std::floor(a->GetPosition().x * 0.1f));
	const int bx = static_cast<int>(std::floor(b->GetPosition().x * 0.1f));
	if (ax != bx) return ax < bx;
	const int ay = static_cast<int>(std::floor(a->GetPosition().y * 0.1f));
	const int by = static_cast<int>(std::floor(b->GetPosition().y * 0.1f));
	return ay != by ? ay < by : a->GetObjectId() < b->GetObjectId();
}

/** Derive membership from the actual full scan, then apply documented range order. */
std::vector<PFLogicObject*> Expected(PFVoxelMap& map, const CVec3& center, float radius, bool objects)
{
	Collect all;
	if (objects) map.ForAllObjects(all); else map.ForAllUnits(all);
	std::vector<PFLogicObject*> result;
	for (PFLogicObject* object : all.seen)
		if (Inside(object, center, radius)) result.push_back(object);
	std::sort(result.begin(), result.end(), RangeOrder);
	return result;
}

/** Check all range entry points and traversal selectors against the full scan. */
void Compare(Checks& checks, PFVoxelMap& map, const CVec3& center, float radius)
{
	const auto units = Expected(map, center, radius, false);
	const auto objects = Expected(map, center, radius, true);
	Collect actualUnits, actualObjects, all;
	map.ForAllUnitsInRange(center, radius, actualUnits);
	map.ForAllObjectsInRange(center, radius, actualObjects);
	checks.Check(actualUnits.seen == units, "unit identity and column-major/ID order match full-scan oracle");
	checks.Check(actualObjects.seen == objects, "object identity and order match full-scan oracle");
	std::vector<PFLogicObject*> both = objects;
	both.insert(both.end(), units.begin(), units.end());
	map.ForAllInRange(center, radius, all);
	checks.Check(all.seen == both, "combined traversal is objects first, then units");
	for (bool selectObjects : {false, true})
		for (bool selectUnits : {false, true})
		{
			Predicate p;
			p.objects = selectObjects;
			p.units = selectUnits;
			Collect selected;
			map.ForAllInRange(center, radius, selected, p);
			std::vector<PFLogicObject*> expected;
			if (selectObjects) expected = objects;
			if (selectUnits) expected.insert(expected.end(), units.begin(), units.end());
			checks.Check(selected.seen == expected && p.calls == static_cast<int>(expected.size()), "selector gates and range-before-predicate contract");
		}
	Predicate reject;
	reject.accept = false;
	Collect rejected;
	map.ForAllUnitsInRange(center, radius, rejected, reject);
	map.ForAllObjectsInRange(center, radius, rejected, reject);
	checks.Check(rejected.seen.empty() && reject.calls == static_cast<int>(both.size()), "rejecting predicate suppresses callbacks");
}

/** Isolate malformed queries: old assertions, conversions or unbounded loops become failures.
 * Floating invalid/overflow traps detect unsafe work even if no callback is delivered.
 */
void BoundedCase(Checks& checks, PFVoxelMap& map, const CVec3& center, float radius, int expected)
{
	std::fflush(0);
	const pid_t pid = fork();
	if (pid == 0)
	{
		const rlimit noCore = {0, 0};
		setrlimit(RLIMIT_CORE, &noCore);
		alarm(2);
		std::feclearexcept(FE_ALL_EXCEPT);
		feenableexcept(FE_INVALID | FE_OVERFLOW);
		Collect units, objects, both;
		Predicate p;
		map.ForAllUnitsInRange(center, radius, units, p);
		map.ForAllObjectsInRange(center, radius, objects, p);
		map.ForAllInRange(center, radius, both, p);
		const bool ok = static_cast<int>(units.seen.size() + objects.seen.size()) == expected &&
			static_cast<int>(both.seen.size()) == expected && p.calls == expected * 2 &&
			std::fetestexcept(FE_INVALID | FE_OVERFLOW) == 0;
		_exit(ok ? 0 : 1);
	}
	int status = 0;
	pid_t waited = -1;
	if (pid > 0)
		do { waited = waitpid(pid, &status, 0); } while (waited < 0 && errno == EINTR);
	checks.Check(pid > 0 && waited == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0,
		"bounded query has exact result and no assertion, FP exception or timeout");
}

/** Cover four corners/edges, voxel boundaries, disjoint circles and dense rectangular layouts. */
void Geometry(Checks& checks)
{
	Fixture f(checks);
	if (!f.map) return;
	for (float x : {0.0f, 0.25f, 9.75f, 10.0f, 10.25f, 19.75f, 20.0f, 23.0f, f.width - 0.25f})
		for (float y : {0.0f, 0.25f, 10.0f, 20.0f, 24.0f, f.height - 0.25f})
		{
			f.AddUnit(x, y);
			f.AddObject(x, y);
		}
	const float nan = std::numeric_limits<float>::quiet_NaN();
	for (float x : {-2.0f, 0.0f, 10.0f, 20.0f, f.width * 0.5f, f.width - 0.25f, f.width + 2.0f})
		for (float y : {-2.0f, 0.0f, 10.0f, 20.0f, f.height * 0.5f, f.height - 0.25f, f.height + 2.0f})
			for (float radius : {0.25f, 5.0f, 10.0f, 32.0f, 1000.0f})
				Compare(checks, *f.map, CVec3(x, y, nan), radius);
	Unit* boundary = f.AddUnit(23, 24);
	Collect included;
	f.map->ForAllUnitsInRange(CVec3(20, 20, 0), 5, included);
	checks.Check(std::find(included.seen.begin(), included.seen.end(), boundary) != included.seen.end(), "3-4-5 inclusive boundary and ignored height");
	boundary->Move(CVec3(23, std::nextafter(24.0f, 25.0f), 500));
	Collect excluded;
	f.map->ForAllUnitsInRange(CVec3(20, 20, 0), 5, excluded);
	checks.Check(std::find(excluded.seen.begin(), excluded.seen.end(), boundary) == excluded.seen.end(), "one-ULP beyond circle excluded after movement");
}

/** Query input validation precedes Range construction and all float-to-index conversions. */
void InvalidAndHuge(Checks& checks)
{
	Fixture f(checks);
	if (!f.map) return;
	f.AddUnit(20, 20);
	f.AddObject(20, 20);
	const float inf = std::numeric_limits<float>::infinity();
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float huge = std::numeric_limits<float>::max();
	for (float radius : {0.0f, -0.0f, -1.0f, nan, inf, -inf, huge, -huge, std::numeric_limits<float>::denorm_min()})
		BoundedCase(checks, *f.map, CVec3(20, 20, 0), radius, 0);
	for (float invalid : {nan, inf, -inf, huge, -huge, 1e12f, -1e12f})
	{
		BoundedCase(checks, *f.map, CVec3(invalid, 20, 0), 1, 0);
		BoundedCase(checks, *f.map, CVec3(20, invalid, 0), 1, 0);
	}
	BoundedCase(checks, *f.map, CVec3(20, 20, nan), 1e12f, 2);
	BoundedCase(checks, *f.map, CVec3(-1e12f, 20, inf), 2e12f, 2);
	BoundedCase(checks, *f.map, CVec3(1e19f, 20, 0), 1, 0);
	CObj<PFVoxelMap> empty = new PFVoxelMap(f.world);
	BoundedCase(checks, *empty, CVec3(20, 20, 0), 1, 0);
}

/** Exercise the existing live-ring contract; relocation can revisit a later voxel. */
void Callbacks(Checks& checks)
{
	for (int action = 0; action < 5; ++action)
	{
		Fixture f(checks);
		if (!f.map) return;
		Unit* first = f.AddUnit(21, 21);
		Unit* second = f.AddUnit(22, 21);
		Unit* third = f.AddUnit(23, 21);
		// Reinsert in reverse order: IDs, not insertion order, control each ring.
		third->Move(third->GetPosition());
		second->Move(second->GetPosition());
		first->Move(first->GetPosition());
		struct Mutate : Collect
		{
			PFAIWorld* map;
			Unit* first;
			Unit* second;
			int action;
			bool changed = false;
			std::vector<PFLogicObject*> nested;
			void operator()(PFBaseUnit& unit)
			{
				Collect::operator()(unit);
				if (&unit != first || changed) return;
				changed = true;
				if (action == 0) map->RemoveObject(unit);
				if (action == 1) map->RemoveObject(*second);
				if (action == 2) first->Move(CVec3(31, 21, 0));
				if (action == 3) first->Move(CVec3(12, 21, 0));
				if (action == 4)
				{
					struct Inner : Collect
					{
						PFAIWorld* map;
						Unit* remove;
						void operator()(PFBaseUnit& current)
						{
							Collect::operator()(current);
							if (remove) { map->RemoveObject(*remove); remove = 0; }
						}
					} inner;
					inner.map = map;
					inner.remove = second;
					map->ForAllUnitsInRange(CVec3(25, 25, 0), 20, inner);
					nested = inner.seen;
				}
			}
		} mutate;
		mutate.map = f.map;
		mutate.first = first;
		mutate.second = second;
		mutate.action = action;
		f.map->ForAllUnitsInRange(CVec3(25, 25, 0), 20, mutate);
		std::vector<PFLogicObject*> expected = {first, second, third};
		// Removal of cached next still visits that live retained object; the handler recovers its successor.
		if (action == 2) expected.push_back(first);
		checks.Check(mutate.seen == expected, "live removal/movement callback sequence preserved");
		if (action == 4) checks.Check(mutate.nested == expected, "nested removal recovers both iterator levels");
		Compare(checks, *f.map, CVec3(25, 25, 0), 20);
	}
	Fixture f(checks);
	if (!f.map) return;
	f.AddObject(21, 21);
	f.AddObject(22, 21);
	Unit* unit = f.AddUnit(21, 21);
	struct RemoveCurrent : Collect
	{
		PFAIWorld* map;
		void operator()(PFLogicObject& object)
		{
			Collect::operator()(object);
			map->RemoveObject(object);
		}
	} remove;
	remove.map = f.map;
	f.map->ForAllInRange(CVec3(25, 25, 0), 20, remove);
	const std::vector<PFLogicObject*> order = {f.objects[0].GetPtr(), f.objects[1].GetPtr(), unit};
	checks.Check(remove.seen == order, "self-removal works for both rings in the combined entry point");
	Collect remaining;
	f.map->ForAllInRange(CVec3(25, 25, 0), 20, remaining);
	checks.Check(remaining.seen.empty(), "combined self-removal leaves no indexed objects or units");
}
}

/** Early dispatch runs actual engine code without starting graphics or the native client loop. */
bool RunPrimeWorldLinuxVoxelRangeProbe()
{
	const NDb::Ptr<NDb::SessionRoot> saved = NDb::SessionRoot::GetRoot();
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
			Geometry(checks);
			InvalidAndHuge(checks);
			Callbacks(checks);
		}
	NDb::SessionRoot::InitRoot(saved);
	std::printf("Voxel range: %d checks, %d failures\n", checks.count, checks.failures);
	return checks.failures == 0;
}
