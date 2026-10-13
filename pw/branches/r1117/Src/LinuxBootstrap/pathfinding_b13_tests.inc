#include "../PF_GameLogic/TileMap.h"
#include "../PF_GameLogic/PFSimpleObject.h"
#include "../PF_GameLogic/CommonPathFinder.h"
#include "../PF_GameLogic/RegionPointChecker.h"
#include "../System/Commands.h"

/** Real initialized mover; the fixture overrides no movement, collision or stepping. */
struct RoutingMoverB13 : PFBaseMovingUnit
{
	RoutingMoverB13(PFWorld* owner, const NDb::Unit& db, const CVec2& at)
		: PFBaseMovingUnit(owner, CVec3(at, 0), CVec2(1, 0), db)
	{
		InitData init; init.pObjectDesc = &db; init.type = NDb::UNITTYPE_CREEP;
		init.faction = NDb::FACTION_FREEZE; init.playerId = -1; Initialize(init);
		AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); AddFlag(NDb::UNITFLAG_FORBIDREWARDS);
		SetObjectSize(1); SetUnitSpeed(12); SetMoveFlags(MOVE_FLAG_OVERRIDE_SPEED);
	}
	void Footprint(int size) { SetObjectSizes(1, size, size); }
};

/** Rectangular loaded-world geometry and literal DB stats keep routing engine-owned. */
struct RoutingFixtureB13
{
	CObj<NCore::IWorldBase> owner;
	PFWorld* world = nullptr;
	NDb::Ptr<NDb::Unit> unitDb;
	NDb::Ptr<NDb::AdvMapDescription> mapDb;
	explicit RoutingFixtureB13(Checks& check, int x = 20, int y = 12, int tiles = 10, bool authoredObstacle = false)
	{
		auto* terrain = new NDb::Terrain;
		terrain->elemXCount = x; terrain->elemYCount = y; terrain->tilesPerElement = tiles;
		auto* map = new NDb::AdvMap; map->terrain = terrain;
		if (authoredObstacle)
		{
			auto* obstacle = new NDb::AdvMapObstacle;
			obstacle->LockMode = NDb::BOOLEANLOCKMODE_LOCK; obstacle->opaque = false;
			obstacle->collision = CTRect<float>(0, 0, 5, 61); obstacle->lockMask.tileSize = 1;
			for (int cx = 0; cx < 5; ++cx) for (int cy = 0; cy < 61; ++cy)
				obstacle->lockMask.mask.push_back(CTPoint<int>(cx, cy));
			NDb::AdvMapObject object; object.gameObject = obstacle; object.lockMap = true;
			object.offset = CPlacement(CVec3(70, 30, 0), QNULL, CVec3(1, 1, 1));
			map->objects.push_back(object);
		}
		auto* desc = new NDb::AdvMapDescription; desc->map = map; mapDb = desc;
		owner = PFWorld::CreatePFWorld(); world = dynamic_cast<PFWorld*>(owner.GetPtr());
		if (!world || !world->LoadMap(mapDb, 0, NCore::TPlayersStartInfo(), 0, false, PFResourcesCollection::TalentMap()))
			world = nullptr;
		check(world != nullptr, "routing: real rectangular mock DB world loads");
		auto* stats = new NDb::StatsContainer;
		for (const auto& entry : {nstl::pair<NDb::EStat, const char*>(NDb::STAT_LIFE, "100"),
			nstl::pair<NDb::EStat, const char*>(NDb::STAT_MOVESPEED, "12")})
		{
			NDb::UnitStat stat; stat.statId = entry.first; stat.value.sString = entry.second;
			stat.increment.sString = "0"; stats->stats.push_back(stat);
		}
		auto* db = new NDb::Unit; db->stats = stats; db->pathFindingSteps = 1; unitDb = db;
	}
	CObj<RoutingMoverB13> Mover(const CVec2& at) { return new RoutingMoverB13(world, *unitDb, at); }
	void Tick(float dt = .1f) { world->Step(dt, dt); }
	void Wall(int layer, bool mark = true)
	{
		vector<SVector> cells;
		for (int x = 70; x <= 74; ++x) for (int y = 30; y <= 90; ++y) cells.push_back(SVector(x, y));
		world->GetTileMap()->MarkObject(cells, mark, layer);
	}
};

/** Record physical admissibility during real world ticks, not only a linked path API. */
static bool RoutingArriveB13(RoutingFixtureB13& f, RoutingMoverB13* unit, const CVec2& goal,
	float stopDistance, bool& admissible, bool& detoured, int ticks = 600)
{
	admissible = true; detoured = false;
	for (int i = 0; i < ticks && unit->IsMoving(); ++i)
	{
		f.Tick();
		const CVec2 at = unit->GetPosition().AsVec2D();
		detoured |= at.y < 30.f || at.y >= 91.f;
		NWorld::MapModeChanger mode(NWorld::MAP_MODE_ALL_STATICS, f.world->GetTileMap());
		admissible &= std::isfinite(at.x) && std::isfinite(at.y)
			&& f.world->GetTileMap()->CanUnitGo(unit->GetUnitRadius(), f.world->GetTileMap()->GetTile(at));
	}
	const bool arrived = !unit->IsMoving() && fabs(unit->GetPosition().AsVec2D() - goal) <= stopDistance + .05f;
	if (!arrived)
		std::printf("Routing context: at=(%.6f,%.6f) goal=(%.6f,%.6f) moving=%d tileSize=%d dynSize=%d pathLength=%d\n",
			unit->GetPosition().x, unit->GetPosition().y, goal.x, goal.y, unit->IsMoving(),
			unit->GetObjectTileSize(), unit->GetObjectDynamicTileSize(),
			f.world->GetPathFinder() ? f.world->GetPathFinder()->GetPathLength() : -1);
	return arrived;
}

/** Independent layer masks, authored collision geometry, and the original ghost stack. */
static void RoutingMasksB13(Checks& check)
{
	for (int layer : {NWorld::MAP_MODE_STATIC, NWorld::MAP_MODE_BUILDING})
		for (int ghost : {int(NDb::GHOSTMOVEMODE_IGNORESTATIC), int(NDb::GHOSTMOVEMODE_IGNOREBUILDINGS),
			int(NDb::GHOSTMOVEMODE_IGNOREDYNAMIC)})
		{
			RoutingFixtureB13 f(check); if (!f.world) continue; f.Wall(layer);
			CObj<RoutingMoverB13> mover = f.Mover(CVec2(20.5f, 60.5f));
			const int token = mover->SetGhostMode(ghost, false);
			const bool ignored = (layer == NWorld::MAP_MODE_STATIC && ghost == NDb::GHOSTMOVEMODE_IGNORESTATIC)
				|| (layer == NWorld::MAP_MODE_BUILDING && ghost == NDb::GHOSTMOVEMODE_IGNOREBUILDINGS);
			const CVec2 goal(150.25f, 60.75f); mover->MoveTo(goal, 0);
			bool admissible, detoured;
			check(RoutingArriveB13(f, mover, goal, 0, admissible, detoured), "routing: independent ghost mask reaches destination");
			check(detoured != ignored, "routing: planner and segment checks agree on the selected static/building layer");
			check(ignored || admissible, "routing: a nonignored layer cannot be crossed by ghost routing");
			mover->ResetGhostMode(token);
		}
	{
		RoutingFixtureB13 f(check, 20, 12, 10, true); if (!f.world) return;
		check(!f.world->GetTileMap()->CanUnitGo(1, SVector(72, 60)), "routing: actual loaded DB obstacle marks passability");
		CObj<RoutingMoverB13> mover = f.Mover(CVec2(20.5f, 60.5f));
		mover->MoveTo(CVec2(150.25f, 60.75f), 3);
		bool admissible, detoured;
		check(RoutingArriveB13(f, mover, CVec2(150.25f, 60.75f), 3, admissible, detoured),
			"routing: authored collision descriptor participates in world-stepped arrival");
		check(admissible && detoured, "routing: loaded geometry is not replaced by a mock production query");
		check(fabs(mover->GetPosition().AsVec2D() - CVec2(150.25f, 60.75f)) >= 2.99f,
			"routing: stopping distance belongs to the far destination, not each corner");
		const int old = mover->SetGhostMode(NDb::GHOSTMOVEMODE_IGNORESTATIC, false);
		const int overrideToken = mover->SetGhostMode(NDb::GHOSTMOVEMODE_IGNOREBUILDINGS, true);
		const int newest = mover->SetGhostMode(NDb::GHOSTMOVEMODE_IGNOREDYNAMIC, false);
		check(mover->GetGhostMode() == NDb::GHOSTMOVEMODE_IGNOREBUILDINGS, "routing: latest original override terminates ghost accumulation");
		mover->ResetGhostMode(overrideToken);
		check(mover->GetGhostMode() == (NDb::GHOSTMOVEMODE_IGNORESTATIC | NDb::GHOSTMOVEMODE_IGNOREDYNAMIC),
			"routing: removing ghost override restores remaining masks");
		mover->ResetGhostMode(old); mover->ResetGhostMode(newest);
	}
}

/** Input rejection runs through gameplay APIs, before pathfinder conversion or movement. */
static void RoutingAdmissionB13(Checks& check)
{
	RoutingFixtureB13 f(check); if (!f.world) return;
	CObj<RoutingMoverB13> mover = f.Mover(CVec2(20.5f, 60.5f));
	for (float bad : {-1.f, 200.f, std::numeric_limits<float>::max(), std::numeric_limits<float>::infinity(),
		std::numeric_limits<float>::quiet_NaN()})
	{
		const CVec2 before = mover->GetPosition().AsVec2D();
		const bool accepted = mover->MoveTo(CVec2(bad, 60.5f), 0);
		check(!accepted, "routing: out-of-map/nonfinite/unrepresentable destination rejected");
		// Contain the unfixed baseline before it performs an undefined float-to-int conversion.
		if (accepted && (!std::isfinite(bad) || bad == std::numeric_limits<float>::max())) mover->Stop(false);
		f.Tick(); check(mover->GetPosition().AsVec2D() == before && !mover->IsMoving(), "routing: invalid destination cannot mutate position");
	}
	for (float bad : {-1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		check(!mover->MoveTo(CVec2(150.5f, 60.5f), bad), "routing: invalid arrival radius cannot become zero/default");
		const CVec2 before = mover->GetPosition().AsVec2D(); f.Tick();
		check(mover->GetPosition().AsVec2D() == before, "routing: invalid radius leaves position unchanged");
	}
	mover->MoveTo(CVec2(150.5f, 60.5f), 0);
	for (float dt : {0.f, -1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		const CVec2 before = mover->GetPosition().AsVec2D(); f.Tick(dt);
		check(mover->GetPosition().AsVec2D() == before, "routing: rejected world delta never consumes route distance");
	}
	mover->SetUnitSpeed(std::numeric_limits<float>::max());
	const CVec2 before = mover->GetPosition().AsVec2D(); f.Tick(2);
	check(mover->GetPosition().AsVec2D() == before, "routing: finite speed-times-delta overflow fails before moving");
	mover->SetUnitSpeed(12); mover->Stop(false);
	for (int bad : {-1, 0, 17, INT_MAX})
	{
		mover->Footprint(bad); mover->MoveTo(CVec2(150.5f, 60.5f), 0); f.Tick();
		check(mover->GetPosition().AsVec2D() == before && !mover->IsMoving(), "routing: malformed clearance size cannot enter packed tile arithmetic");
	}
	mover->Footprint(1); f.Wall(NWorld::MAP_MODE_STATIC);
	for (int bad : {-1, INT_MAX})
	{
		const_cast<NDb::Unit*>(f.unitDb.GetPtr())->pathFindingSteps = bad;
		mover->MoveTo(CVec2(150.5f, 60.5f), 0); f.Tick();
		check(mover->GetPosition().AsVec2D() == before && !mover->IsMoving(), "routing: invalid authored lookahead is not an unbounded search budget");
	}
	const_cast<NDb::Unit*>(f.unitDb.GetPtr())->pathFindingSteps = 0;
	mover->MoveTo(CVec2(150.5f, 60.5f), 0); bool admissible, detoured;
	check(RoutingArriveB13(f, mover, CVec2(150.5f, 60.5f), 0, admissible, detoured), "routing: original zero lookahead remains supported");
}

/** Budget the original square scratch, not only the map product; never attempt huge allocation. */
static void RoutingAllocationB13(Checks& check)
{
	for (const auto& size : {nstl::pair<int, int>(4097, 1), nstl::pair<int, int>(1, 4097),
		nstl::pair<int, int>(65536, 1), nstl::pair<int, int>(0, 10), nstl::pair<int, int>(INT_MAX, INT_MAX)})
	{
		RoutingFixtureB13 f(check); if (!f.world) continue;
		auto* terrain = new NDb::Terrain; terrain->elemXCount = size.first; terrain->elemYCount = size.second; terrain->tilesPerElement = 1;
		auto* map = new NDb::AdvMap; map->terrain = terrain;
		auto* desc = new NDb::AdvMapDescription; desc->map = map; NDb::Ptr<NDb::AdvMapDescription> held = desc;
		const int width = f.world->GetTileMap()->GetSizeX(), height = f.world->GetTileMap()->GetSizeY();
		auto* finder = f.world->GetPathFinder();
		const bool loaded = f.world->LoadMap(held, 0, NCore::TPlayersStartInfo(), 0, false, PFResourcesCollection::TalentMap());
		check(!loaded, "routing: skinny/overflow/invalid map rejected before square finder allocation");
		check(f.world->GetTileMap()->GetSizeX() == width && f.world->GetTileMap()->GetSizeY() == height
			&& f.world->GetPathFinder() == finder, "routing: rejected geometry does not mutate the current grid/finder");
	}
	RoutingFixtureB13 boundary(check, 4096, 1, 1);
	check(boundary.world && boundary.world->GetPathFinder(), "routing: inclusive 4096 dimension bound remains loadable");
}

/** Replanning and cleanup use existing owned route members, not a fixture's route model. */
static void RoutingChangesB13(Checks& check)
{
	{
		RoutingFixtureB13 f(check); if (!f.world) return;
		CObj<RoutingMoverB13> mover = f.Mover(CVec2(20.5f, 60.5f));
		const CVec2 goal(150.25f, 60.75f); mover->MoveTo(goal, 0); f.Tick(); f.Wall(NWorld::MAP_MODE_BUILDING);
		bool admissible, detoured;
		check(RoutingArriveB13(f, mover, goal, 0, admissible, detoured) && admissible && detoured,
			"routing: newly inserted geometry is replanned on a real world tick");
		mover->TeleportTo(CVec2(20.5f, 60.5f)); mover->MoveTo(goal, 0); f.Tick(); f.Wall(NWorld::MAP_MODE_BUILDING, false);
		check(RoutingArriveB13(f, mover, goal, 0, admissible, detoured) && admissible,
			"routing: removal exposes a checked direct segment without stale cursor motion");
		f.Wall(NWorld::MAP_MODE_STATIC); mover->TeleportTo(CVec2(20.5f, 60.5f)); mover->MoveTo(goal, 0); f.Tick();
		mover->Reset(); const CVec2 reset = mover->GetPosition().AsVec2D(); f.Tick();
		check(!mover->IsMoving() && mover->GetPosition().AsVec2D() == reset, "routing: unit Reset releases route ownership");
		mover->SetMoveFlags(MOVE_FLAG_OVERRIDE_SPEED); mover->SetUnitSpeed(12); mover->MoveTo(goal, 0); f.Tick();
		mover->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
		const CVec2 dead = mover->GetPosition().AsVec2D(); f.Tick();
		check(mover->IsDead() && mover->GetPosition().AsVec2D() == dead, "routing: dead mover cannot retain executable route progress");
	}
	{
		RoutingFixtureB13 f(check); if (!f.world) return;
		CObj<RoutingMoverB13> mover = f.Mover(CVec2(20.5f, 60.5f));
		CObj<RoutingMoverB13> blocker = f.Mover(CVec2(28.5f, 60.5f));
		mover->MoveTo(CVec2(40.5f, 60.5f), 0);
		for (int i = 0; i < 40 && mover->IsMoving(); ++i) f.Tick();
		check(mover->GetPosition().x < 28 && !mover->IsMoving(), "routing: ordinary paths retain continuous dynamic-circle blocking");
		mover->SetGhostMode(NDb::GHOSTMOVEMODE_IGNOREDYNAMIC, false);
		mover->MoveTo(CVec2(40.5f, 60.5f), 0); bool admissible, detoured;
		check(RoutingArriveB13(f, mover, CVec2(40.5f, 60.5f), 0, admissible, detoured), "routing: ignore-dynamic does not change static route policy");
	}
	{
		RoutingFixtureB13 f(check); if (!f.world) return; f.Wall(NWorld::MAP_MODE_STATIC);
		CObj<RoutingMoverB13> mover = f.Mover(CVec2(20.5f, 60.5f));
		mover->MoveToSpecial(CVec2(150.25f, 60.75f), false); bool admissible, detoured;
		const bool arrived = RoutingArriveB13(f, mover, CVec2(150.25f, 60.75f), 0, admissible, detoured);
		check(!arrived && admissible && !detoured, "routing: special movement remains direct and does not acquire ordinary detours");
		mover->TeleportTo(CVec2(20.5f, 60.5f)); mover->SetMoveFlags(MOVE_FLAG_OVERRIDE_SPEED | MOVE_FLAG_NO_COLLIDE);
		mover->MoveTo(CVec2(150.25f, 60.75f), 0);
		check(RoutingArriveB13(f, mover, CVec2(150.25f, 60.75f), 0, admissible, detoured) && !detoured,
			"routing: explicit no-collide movement stays direct without entering map mode zero");
	}
}

/** Generation wrap and interleaved owned cursors must not expose reusable finder scratch. */
static void RoutingOwnershipB13(Checks& check)
{
	RoutingFixtureB13 f(check); if (!f.world) return; f.Wall(NWorld::MAP_MODE_STATIC);
	CObj<RoutingMoverB13> first = f.Mover(CVec2(20.5f, 60.5f));
	first->SetGhostMode(NDb::GHOSTMOVEMODE_IGNOREDYNAMIC, false);
	bool valid = true;
	for (int i = 0; i < 270; ++i)
	{
		first->TeleportTo(CVec2(20.5f, 60.5f)); first->MoveTo(CVec2(150.25f, 60.75f), 0); f.Tick(.01f);
		valid &= first->IsMoving() && f.world->GetPathFinder() && f.world->GetPathFinder()->GetPathLength() > 0;
		first->Stop(false);
	}
	check(valid, "routing: repeated real route admissions survive marker-generation wrap");
	first->TeleportTo(CVec2(20.5f, 60.5f)); first->MoveTo(CVec2(150.25f, 60.75f), 0); f.Tick();
	CObj<RoutingMoverB13> second = f.Mover(CVec2(25.5f, 65.5f));
	second->SetGhostMode(NDb::GHOSTMOVEMODE_IGNOREDYNAMIC, false);
	second->MoveTo(CVec2(155.25f, 65.75f), 0);
	for (int i = 0; i < 600 && (first->IsMoving() || second->IsMoving()); ++i) f.Tick();
	check(fabs(first->GetPosition().AsVec2D() - CVec2(150.25f, 60.75f)) < .05f
		&& fabs(second->GetPosition().AsVec2D() - CVec2(155.25f, 65.75f)) < .05f,
		"routing: two long paths own independent cursor state while sharing the finder");
}

/** Original region endpoints, partial routes and corner clearance retain distinct meanings. */
static void RoutingEndpointsB13(Checks& check)
{
	{
		RoutingFixtureB13 f(check); if (!f.world) return; f.Wall(NWorld::MAP_MODE_STATIC);
		CObj<RoutingMoverB13> mover = f.Mover(CVec2(20.5f, 60.5f));
		vector<SVector> occupied; occupied.push_back(SVector(150, 60));
		CObj<RegionPointChecker> region = new RegionPointChecker(f.world, occupied, 10);
		mover->SetUnitSpeed(100); mover->MoveTo(CVec2(150.5f, 60.5f), 0, region);
		for (int i = 0; i < 5 && mover->IsMoving(); ++i) f.Tick(1);
		check(!mover->IsMoving() && region->IsGoodTile(f.world->GetTileMap()->GetTile(mover->GetPosition().AsVec2D())),
			"routing: original region checker stops a large-budget routed tick at its accepted endpoint");
		check(mover->GetPosition().x < 149, "routing: region completion is not replaced by the geometric destination");
		const CVec2 accepted = mover->GetPosition().AsVec2D(); mover->SetUnitSpeed(0);
		mover->MoveTo(CVec2(150.5f, 60.5f), 0, region); f.Tick();
		check(!mover->IsMoving() && mover->GetPosition().AsVec2D() == accepted,
			"routing: an already-accepted region completes even at zero speed");
		mover->SetUnitSpeed(12); mover->TeleportTo(CVec2(20.5f, 60.5f));
		mover->MoveTo(CVec2(72.5f, 60.5f), 0);
		for (int i = 0; i < 300 && mover->IsMoving(); ++i) f.Tick();
		check(!mover->IsMoving() && mover->GetPosition().x < 70 && mover->GetPosition().x > 20.5f,
			"routing: blocked goal retains original reachable partial progress without false arrival");
		const CVec2 stopped = mover->GetPosition().AsVec2D(); f.Tick();
		check(mover->GetPosition().AsVec2D() == stopped, "routing: a partial endpoint has no no-progress replan loop");
		mover->TeleportTo(CVec2(20.5f, 20.5f)); mover->MoveTo(CVec2(20.75f, 20.875f), 0); f.Tick();
		check(!mover->IsMoving() && fabs(mover->GetPosition().AsVec2D() - CVec2(20.75f, 20.875f)) < .001f,
			"routing: same-tile fractional destination does not require a nonempty path");
	}
	{
		RoutingFixtureB13 f(check); if (!f.world) return;
		vector<SVector> corner; corner.push_back(SVector(21, 20)); corner.push_back(SVector(20, 21));
		f.world->GetTileMap()->MarkObject(corner, true, NWorld::MAP_MODE_STATIC);
		CObj<RoutingMoverB13> mover = f.Mover(CVec2(20.5f, 20.5f));
		mover->MoveTo(CVec2(21.5f, 21.5f), 0);
		f.Tick();
		check(!mover->IsMoving() && mover->GetPosition().AsVec2D() == CVec2(20.5f, 20.5f),
			"routing: original blocked adjacent step terminates at its reachable partial endpoint");
		mover->MoveTo(CVec2(25.5f, 25.5f), 0);
		float travelled = 0; bool valid = true;
		for (int i = 0; i < 300 && mover->IsMoving(); ++i)
		{
			const CVec2 before = mover->GetPosition().AsVec2D(); f.Tick(.01f);
			const CVec2 after = mover->GetPosition().AsVec2D(); travelled += fabs(after - before);
			valid &= f.world->GetTileMap()->CanUnitGo(1, f.world->GetTileMap()->GetTile(after));
		}
		check(!mover->IsMoving() && fabs(mover->GetPosition().AsVec2D() - CVec2(25.5f, 25.5f)) < .05f,
			"routing: diagonal corner obstruction can be routed around");
		check(valid && travelled > 7.1f, "routing: two blocked orthogonal neighbors cannot be squeezed diagonally");
	}
	for (int size : {1, 3, 4, 15, 16})
	{
		RoutingFixtureB13 f(check); if (!f.world) continue; f.Wall(NWorld::MAP_MODE_STATIC);
		CObj<RoutingMoverB13> mover = f.Mover(CVec2(20.5f, 60.5f)); mover->Footprint(size);
		mover->MoveTo(CVec2(150.25f, 60.75f), 0); bool admissible, detoured;
		check(RoutingArriveB13(f, mover, CVec2(150.25f, 60.75f), 0, admissible, detoured) && admissible,
			"routing: original odd/even and maximum-size clearance conventions remain navigable");
	}
}

/** Target refresh, live mode changes and attachment invalidate only this unit's route. */
static void RoutingTargetLifecycleB13(Checks& check)
{
	RoutingFixtureB13 f(check); if (!f.world) return; f.Wall(NWorld::MAP_MODE_BUILDING);
	CObj<RoutingMoverB13> mover = f.Mover(CVec2(20.5f, 60.5f));
	CObj<RoutingMoverB13> target = f.Mover(CVec2(150.25f, 60.75f));
	mover->MoveTo(target, 2); f.Tick(); target->TeleportTo(CVec2(155.25f, 50.75f));
	bool admissible, detoured;
	check(RoutingArriveB13(f, mover, CVec2(155.25f, 50.75f), 2, admissible, detoured) && admissible,
		"routing: a moving target replaces the old cursor and preserves stopping distance");
	mover->TeleportTo(CVec2(20.5f, 60.5f)); mover->MoveTo(CVec2(150.25f, 60.75f), 0); f.Tick();
	const int ghost = mover->SetGhostMode(NDb::GHOSTMOVEMODE_IGNOREBUILDINGS, false);
	check(RoutingArriveB13(f, mover, CVec2(150.25f, 60.75f), 0, admissible, detoured) && !detoured,
		"routing: changing the active mask invalidates the outstanding detour");
	mover->ResetGhostMode(ghost); mover->TeleportTo(CVec2(20.5f, 60.5f));
	mover->MoveTo(CVec2(150.25f, 60.75f), 0); f.Tick();
	target->AttachUnit(CPtr<NWorld::PFBaseMovingUnit>(mover.GetPtr()));
	const CVec2 mounted = mover->GetPosition().AsVec2D(); f.Tick();
	check(mover->IsMounted() && mover->GetPosition().AsVec2D() == mounted, "routing: mounting cancels the passenger's executable route");
	target->DetachUnit(); mover->TeleportTo(CVec2(20.5f, 60.5f));
	mover->MoveTo(CVec2(150.25f, 60.75f), 0);
	check(RoutingArriveB13(f, mover, CVec2(150.25f, 60.75f), 0, admissible, detoured),
		"routing: detached unit admits a fresh independently owned route");
	check(!f.world->LoadMap(f.mapDb, 0, NCore::TPlayersStartInfo(), 0, false, PFResourcesCollection::TalentMap()),
		"routing: live geometry replacement is rejected before invalidating unit map ownership");
}

/** Original nonpositive factors disable shortening; positive narrowing must be checked. */
static void RoutingFactorB13(Checks& check)
{
	struct FactorScope
	{
		NGlobal::VariantValue saved;
		FactorScope() : saved(NGlobal::GetVar("path_upper_limit_factor", NGlobal::VariantValue(5.f))) {}
		~FactorScope() { NGlobal::SetVar("path_upper_limit_factor", saved, STORAGE_NONE); }
	} factorScope;
	RoutingFixtureB13 f(check); if (!f.world) return; f.Wall(NWorld::MAP_MODE_STATIC);
	CObj<RoutingMoverB13> mover = f.Mover(CVec2(20.5f, 60.5f));
	for (float factor : {-1.f, -std::numeric_limits<float>::max(), 0.f})
	{
		NGlobal::SetVar("path_upper_limit_factor", NGlobal::VariantValue(factor), STORAGE_NONE);
		mover->TeleportTo(CVec2(20.5f, 60.5f)); mover->MoveTo(CVec2(150.25f, 60.75f), 0);
		bool admissible, detoured;
		check(RoutingArriveB13(f, mover, CVec2(150.25f, 60.75f), 0, admissible, detoured) && admissible && detoured,
			"routing: negative/zero finite path factor retains original unshortened search policy");
	}
	for (float factor : {std::numeric_limits<float>::max(), std::numeric_limits<float>::infinity(),
		-std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		NGlobal::SetVar("path_upper_limit_factor", NGlobal::VariantValue(factor), STORAGE_NONE);
		mover->TeleportTo(CVec2(20.5f, 60.5f)); mover->MoveTo(CVec2(150.25f, 60.75f), 0); f.Tick();
		check(!mover->IsMoving() && mover->GetPosition().AsVec2D() == CVec2(20.5f, 60.5f),
			"routing: overflow/nonfinite path factor cannot install a candidate or mutate position");
	}
}

static void PathfindingB13(Checks& check)
{
	// Isolate literal movement policy without changing the probe's loaded session root.
	struct RootScope
	{
		NDb::Ptr<NDb::SessionRoot> saved;
		RootScope() : saved(NDb::SessionRoot::GetRoot())
		{
			auto* logic = new NDb::SessionLogicRoot; logic->aiLogic = new NDb::AILogicParameters;
			auto* root = new NDb::SessionRoot; root->logicRoot = logic; NDb::SessionRoot::InitRoot(root);
		}
		~RootScope() { NDb::SessionRoot::InitRoot(saved); }
	} rootScope;
	RoutingMasksB13(check);
	RoutingAdmissionB13(check);
	RoutingAllocationB13(check);
	RoutingChangesB13(check);
	RoutingOwnershipB13(check);
	RoutingEndpointsB13(check);
	RoutingTargetLifecycleB13(check);
	RoutingFactorB13(check);
	for (int layer : {NWorld::MAP_MODE_STATIC, NWorld::MAP_MODE_BUILDING})
	{
		RoutingFixtureB13 f(check); if (!f.world) continue;
		check(f.world->GetPathFinder() != nullptr, "routing: LoadMap owns the real common finder");
		f.Wall(layer);
		CObj<RoutingMoverB13> mover = f.Mover(CVec2(20.5f, 60.5f));
		const CVec2 goal(150.25f, 60.75f);
		mover->MoveTo(goal, 0);
		bool admissible, detoured;
		check(RoutingArriveB13(f, mover, goal, 0, admissible, detoured),
			"routing: world-stepped unit reaches fractional goal around a real wall");
		check(admissible, "routing: routed motion never occupies a static/building obstacle");
		check(detoured, "routing: route crosses around the wall instead of stopping straight");
		const CVec2 finish = mover->GetPosition().AsVec2D(); f.Tick();
		check(mover->GetPosition().AsVec2D() == finish, "routing: completed route has no residual movement");
	}
	{
		RoutingFixtureB13 f(check); if (!f.world) return;
		CObj<RoutingMoverB13> mover = f.Mover(CVec2(20.5f, 60.5f));
		mover->MoveTo(CVec2(150.25f, 60.75f), 0);
		const CVec2 before = mover->GetPosition().AsVec2D(); f.Tick(.25f);
		check(fabs(mover->GetPosition().AsVec2D() - before) <= 3.001f,
			"routing: real world scheduler spends at most one speed-times-dt budget");
		mover->Stop(false); const CVec2 stopped = mover->GetPosition().AsVec2D(); f.Tick();
		check(mover->GetPosition().AsVec2D() == stopped, "routing: Stop invalidates outstanding path consumption");
		mover->MoveTo(CVec2(150.25f, 60.75f), 0); f.Tick();
		mover->TeleportTo(CVec2(25.5f, 20.5f)); const CVec2 teleported = mover->GetPosition().AsVec2D(); f.Tick();
		check(mover->GetPosition().AsVec2D() == teleported, "routing: teleport invalidates the old route");
	}
}
