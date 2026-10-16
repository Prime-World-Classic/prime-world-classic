/** Real placement selector and serialized inner selector; no mocked collision or movement callbacks. */
struct LandingFixtureB13 : RoutingFixtureB13
{
	CObj<RoutingMoverB13> unit;
	NDb::Ptr<NDb::UnitPlaceCorrector> descriptor;
	CObj<PFAbilityData> ability;
	CObj<PFAbilityInstance> instance;
	explicit LandingFixtureB13(Checks& check) : RoutingFixtureB13(check)
	{
		if (!world) return;
		unit = Mover(CVec2(50.5f, 60.5f)); unit->Footprint(1);
		auto* db = new NDb::UnitPlaceCorrector; db->radius = 8;
		auto* inner = new NDb::SelectApplicatorTarget; inner->selectTarget = NDb::APPLICATORAPPLYTARGET_APPLICATORTARGET;
		db->targetSelector = inner; descriptor = db;
		auto* spell = new NDb::Ability; spell->manaCost.sString = "0";
		ability = new PFAbilityData(unit.GetPtr(), spell, NDb::ABILITYTYPEID_SPECIAL, true, false);
		instance = new PFAbilityInstance(ability, Target(unit.GetPtr()), true);
	}
	bool Select(const CVec3& goal, Target& result)
	{
		auto* raw = WrapperFactoryFixtureB12::Stat(); raw->applyTarget = NDb::APPLICATORAPPLYTARGET_ABILITYOWNER;
		NDb::Ptr<NDb::BaseApplicator> db = raw;
		const Target point(goal); PFApplCreatePars pars(instance, point); pars.pDBAppl = db;
		auto app = CreateApplicator(pars); if (!app) return false;
		CObj<PFSingleTargetSelector> selector = static_cast<PFSingleTargetSelector*>(descriptor->Create(world));
		const Target requester(unit.GetPtr()); const PFTargetSelector::RequestParams request(*app, requester);
		return selector && selector->FindTarget(request, result);
	}
	~LandingFixtureB13() { if (unit) unit->Cleanup(); }
};

/** Original map-mask policy, grid rounding and first legal ring candidate remain deterministic. */
static void LandingMasksB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (int layer : {MAP_MODE_STATIC, MAP_MODE_BUILDING, MAP_MODE_DYNAMIC})
	for (int mask : {0, int(NDb::TRACEMODE_STATIC), int(NDb::TRACEMODE_BUILDING), int(NDb::TRACEMODE_DYNAMIC)})
	{
		LandingFixtureB13 f(check); if (!f.world) continue;
		auto* db = const_cast<NDb::UnitPlaceCorrector*>(f.descriptor.GetPtr());
		db->passabilityCheckMode = mask; db->checkByRangeToRequester = false;
		vector<SVector> tiles; tiles.push_back(SVector(70, 60));
		f.world->GetTileMap()->MarkObject(tiles, true, layer);
		const int matching = layer == MAP_MODE_STATIC ? NDb::TRACEMODE_STATIC : layer == MAP_MODE_BUILDING
			? NDb::TRACEMODE_BUILDING : NDb::TRACEMODE_DYNAMIC;
		const bool blocked = mask == 0 || mask == matching;
		const int oldMode = f.world->GetTileMap()->GetMode(); const CVec2 origin = f.unit->GetPos();
		Target result(CVec3(1, 2, 3));
		check(f.Select(CVec3(70.6f, 60.6f, 9), result), "B13 landing selector finds a legal nearby grid position");
		const CVec2 expected(70.5f, blocked ? 61.5f : 60.5f);
		check(result.IsPosition() && result.GetPosition() == CVec3(expected, 0),
			"B13 landing uses original layer selection and positive-Y first ring order, not the blocked raw point");
		check(f.unit->GetPos() == origin && f.world->GetTileMap()->GetMode() == oldMode,
			"B13 landing query neither relocates its owner nor leaks a temporary map mode");
	}
	for (bool bounded : {false, true})
	{
		LandingFixtureB13 f(check); if (!f.world) continue;
		vector<SVector> tiles; tiles.push_back(SVector(70, 60)); f.world->GetTileMap()->MarkObject(tiles, true, MAP_MODE_STATIC);
		CVec2 found(-1, -1);
		const bool success = bounded ? f.unit->FindFreePlace2(CVec2(70.6f, 60.6f), CVec2(65.5f, 60.5f), 5, found, false, false, MAP_MODE_STATIC)
			: f.unit->FindFreePlace(CVec2(70.6f, 60.6f), 5, found, false, false, MAP_MODE_STATIC);
		check(success && found == (bounded ? CVec2(69.5f, 60.5f) : CVec2(70.5f, 61.5f)),
			"B13 requester-bounded placement excludes nearby cells outside its original radius before fallback");
	}
	std::printf("B13 landing masks checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Ignoring dynamic units must not erase static placement, visibility or authored native-terrain policy. */
static void LandingPolicyB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (bool ignoreUnits : {false, true})
	{
		LandingFixtureB13 f(check); if (!f.world) continue;
		vector<SVector> tiles; tiles.push_back(SVector(70, 60)); f.world->GetTileMap()->MarkObject(tiles, true, MAP_MODE_STATIC);
		CVec2 found;
		check(f.unit->FindFreePlace(CVec2(70.5f, 60.5f), 5, found, ignoreUnits)
			&& found == CVec2(70.5f, 61.5f), "B13 ignoreUnits skips only dynamic occupancy, never authored static geometry");
	}
	{
		LandingFixtureB13 f(check); if (!f.world) return;
		auto blocker = f.Mover(CVec2(70.5f, 60.5f)); blocker->Footprint(1);
		for (int mask : {0, int(MAP_MODE_STATIC), int(MAP_MODE_DYNAMIC)})
		{
			CVec2 found;
			check(f.unit->FindFreePlace(CVec2(70.5f, 60.5f), 5, found, false, false, mask)
				&& ((found == CVec2(70.5f, 60.5f)) == (mask == MAP_MODE_STATIC)),
				"B13 placement preserves native moving-unit circle occupancy only for selected dynamic layers");
		}
		CVec2 found;
		check(!f.unit->FindFreePlace(CVec2(150.5f, 100.5f), 1, found, false, false, MAP_MODE_STATIC, true),
			"B13 personal line-of-sight policy cannot be replaced with an unrestricted placement result");
		check(f.unit->FindFreePlace(CVec2(150.5f, 100.5f), 1, found, false, false, MAP_MODE_STATIC, false),
			"B13 disabling the optional sight check retains an otherwise admissible distant cell");
		blocker->Cleanup();
	}
	{
		NatureOverrideFixtureB13 f(check); CVec2 found;
		for (bool native : {false, true})
		{
			const bool success = f.hero->FindFreePlace(CVec2(25.5f, 10.5f), 1, found, true, native, MAP_MODE_STATIC);
			check(success == !native, "B13 freeze placement rejects the actual authored burn cell when native terrain is required");
		}
		f.Map()->Override(NDb::FACTION_FREEZE, CCircle(CVec2(25, 10), 3), 1, 9801);
		check(f.hero->FindFreePlace(CVec2(25.5f, 10.5f), 1, found, true, true, MAP_MODE_STATIC),
			"B13 placement reads a live original terrain override instead of a permanent native stub");
		f.World()->Step(1, 1);
		check(!f.hero->FindFreePlace(CVec2(25.5f, 10.5f), 1, found, true, true, MAP_MODE_STATIC),
			"B13 terrain override expiry restores the original placement restriction");
	}
	std::printf("B13 landing policy checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** A walkability query must use its supplied footprint, not the previous shared path search's size. */
static void LandingWalkabilityB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	LandingFixtureB13 f(check); if (!f.world) return;
	auto* map = f.world->GetTileMap(); auto* finder = f.world->GetPathFinder();
	vector<SVector> cells; cells.push_back(SVector(60, 61)); map->MarkObject(cells, true, MAP_MODE_STATIC);
	MapModeChanger mode(MAP_MODE_STATIC, map);
	check(finder->CreatePath(1, 1, SVector(50, 60), SVector(70, 60), SVector(50, 60), nullptr, -1, 1),
		"B13 drop fixture primes the shared original finder with a one-cell route");
	check(finder->Walkable(SVector(50, 60), SVector(70, 60), 1, 1), "B13 one-cell walkability clears an adjacent obstruction");
	check(!finder->Walkable(SVector(50, 60), SVector(70, 60), 4, 4),
		"B13 explicit larger footprint rejects an adjacent obstruction regardless of a preceding smaller path query");
	check(finder->Walkable(SVector(50, 60), SVector(70, 60), 1, 1), "B13 footprint query restores reusable finder state");
	CVec2 found;
	check(f.unit->FindFreePlaceForDrop(CVec2(70.5f, 60.5f), 0, found, 1, true)
		&& found == CVec2(70.5f, 60.5f), "B13 reachable small-footprint drop keeps its original grid cell");
	check(!f.unit->FindFreePlaceForDrop(CVec2(70.5f, 60.5f), 0, found, 4, true),
		"B13 drop placement cannot cross the obstructed segment with its requested larger footprint");
	std::printf("B13 landing walkability checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Rounding a previously checked continuous candidate must not reintroduce collision at commit. */
static void LandingCollisionB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (float x : {.1f, .5f, .9f}) for (float y : {.1f, .5f, .9f})
	{
		RoutingFixtureB13 f(check); if (!f.world) continue;
		auto a = f.Mover(CVec2(50 + x, 60 + y)); auto b = f.Mover(a->GetPos());
		a->Footprint(1); b->Footprint(1); f.Tick();
		check(fabs2(a->GetPos() - b->GetPos()) >= 1.f - EPS_VALUE,
			"B13 collision resolver validates the committed rounded cell against its anchor, not just the continuous proposal");
		a->Cleanup(); b->Cleanup();
	}
	std::printf("B13 landing collision checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Checked geometry, original ring tie-breaking, live personal sight and range fallback. */
static void LandingBoundariesB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	LandingFixtureB13 f(check); if (!f.world) return;
	auto* map = f.world->GetTileMap(); const int originalMode = map->GetMode();
	const SVector ring[] = {SVector(70, 61), SVector(70, 59), SVector(69, 60), SVector(71, 60),
		SVector(69, 61), SVector(71, 59), SVector(69, 59), SVector(71, 61)};
	vector<SVector> blocked; blocked.push_back(SVector(70, 60));
	for (const auto& next : ring)
	{
		map->MarkObject(blocked, true, MAP_MODE_STATIC); CVec2 found;
		check(f.unit->FindFreePlace(CVec2(70.5f, 60.5f), 0, found, false, false, MAP_MODE_STATIC)
			&& found == map->GetPointByTile(next), "B13 all eight first-ring ties follow the original deterministic candidate order");
		map->MarkObject(blocked, false, MAP_MODE_STATIC); blocked.push_back(next);
	}
	map->MarkObject(blocked, true, MAP_MODE_STATIC);
	CVec2 found(1, 2);
	check(!f.unit->FindFreePlace(CVec2(70.5f, 60.5f), 0, found, true) && found == CVec2(1, 2),
		"B13 exhausted placement preserves output instead of returning a blocked center");
	map->MarkObject(blocked, false, MAP_MODE_STATIC);
	for (const CVec2& bad : {CVec2(-1, 60), CVec2(200, 60), CVec2(70, 120), CVec2(FLT_MAX, 60),
		CVec2(std::numeric_limits<float>::quiet_NaN(), 60), CVec2(70, std::numeric_limits<float>::infinity())})
	{
		found = CVec2(1, 2);
		check(!f.unit->FindFreePlace(bad, 5, found, true) && found == CVec2(1, 2),
			"B13 invalid or off-map placement fails before tile conversion and preserves output");
		check(!f.unit->FindFreePlace2(CVec2(70.5f, 60.5f), bad, 5, found), "B13 bounded placement validates its attractor before integer geometry");
	}
	for (float radius : {-1.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
		check(!f.unit->FindFreePlace(CVec2(70.5f, 60.5f), radius, found), "B13 invalid search radius fails without unbounded rings");
	check(f.unit->FindFreePlace(CVec2(70.5f, 60.5f), FLT_MAX, found), "B13 huge finite radius caps to map coverage before integer narrowing");
	for (float size : {0.f, .5f, 17.f, FLT_MAX, std::numeric_limits<float>::quiet_NaN()})
		check(!f.unit->FindFreePlaceForDrop(CVec2(70.5f, 60.5f), 1, found, size), "B13 unsupported explicit footprints fail before map or path indexing");
	f.unit->GetStat(NDb::STAT_VISIBILITYRANGE)->SetCoreValue(30); f.unit->OpenWarFog(); f.Tick();
	check(f.unit->CanSee(SVector(70, 60)) && f.unit->FindFreePlace(CVec2(70.5f, 60.5f), 0, found, false, false, MAP_MODE_STATIC, true),
		"B13 a live personal fog observer admits a visible landing cell");
	auto* db = const_cast<NDb::UnitPlaceCorrector*>(f.descriptor.GetPtr());
	db->radius = 20; db->checkByRangeToRequester = true; db->passabilityCheckMode = NDb::TRACEMODE_STATIC;
	Target result; check(f.Select(CVec3(100.5f, 60.5f, 9), result) && result.GetPosition() == CVec3(70.5f, 60.5f, 0),
		"B13 selector first clamps an out-of-range point around its requester then rounds the admissible result");
	vector<SVector> fallback; fallback.push_back(SVector(70, 60)); fallback.push_back(SVector(69, 60));
	for (int y = 40; y <= 80; ++y) for (int x = 30; x <= 70; ++x)
		if (fabs2(SVector(x - 50, y - 60)) <= 400 && !(y == 60 && (x == 69 || x == 70))) fallback.push_back(SVector(x, y));
	map->MarkObject(fallback, true, MAP_MODE_STATIC);
	check(f.Select(CVec3(100.5f, 60.5f, 9), result) && result.GetPosition() == CVec3(70.5f, 61.5f, 0),
		"B13 exhausted requester-bounded search retains original radial fallback outside the requester circle");
	map->MarkObject(fallback, false, MAP_MODE_STATIC);
	check(map->GetMode() == originalMode && f.unit->GetPos() == CVec2(50.5f, 60.5f), "B13 success and rejection paths preserve map mode and owner position");
	std::printf("B13 landing boundaries checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** A grid-corrected endpoint must not make a clear oblique flight stop one ULP short of a trace sample. */
static void LandingFlightB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (bool ghost : {false, true}) for (const CVec2& delta : {CVec2(24.5f, 1.5f), CVec2(-24.5f, 1.5f),
		CVec2(1.5f, 24.5f), CVec2(1.5f, -24.5f)})
	{
		RoutingFixtureB13 f(check); if (!f.world) continue;
		const CVec2 start(50, 60), goal = start + delta; auto mover = f.Mover(start);
		mover->SetUnitSpeed(30);
		if (ghost) mover->SetGhostMode(NDb::GHOSTMOVEMODE_IGNORESTATIC | NDb::GHOSTMOVEMODE_IGNOREBUILDINGS
			| NDb::GHOSTMOVEMODE_IGNOREDYNAMIC, false);
		check(mover->MoveToSpecial(goal, false), "B13 oblique corrected landing begins real special movement");
		for (int i = 0; i < 9; ++i)
		{
			f.Tick();
			const float travelled = Min((i + 1) * 3.f, fabs(delta));
			check(fabs(mover->GetPos() - (start + delta * (travelled / fabs(delta)))) < .0001f,
				"B13 exact trace endpoint retains clear oblique motion across ordinary and ghost masks");
		}
		check(!mover->IsMovingSpecial() && mover->GetPos() == goal, "B13 clear oblique flight reaches the exact final endpoint");
		mover->Cleanup();
	}
	std::printf("B13 landing flight checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Push placement must collect and separate neighbors around the corrected center. */
static void LandingPushB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (bool obstacle : {false, true})
	{
		LandingFixtureB13 f(check); if (!f.world) continue;
		const CVec2 proposed = obstacle ? CVec2(70.6f, 60.6f) : CVec2(70.99f, 60.99f);
		const CVec2 corrected = obstacle ? CVec2(70.5f, 61.5f) : CVec2(70.5f, 60.5f);
		auto neighbor = f.Mover(corrected + (obstacle ? CVec2(0, .9f) : CVec2(-.9f, 0))); neighbor->Footprint(1);
		if (obstacle)
		{
			vector<SVector> cells; cells.push_back(SVector(70, 60)); f.world->GetTileMap()->MarkObject(cells, true, MAP_MODE_STATIC);
		}
		f.unit->PlaceUnitWithPush(proposed, 2, true);
		check(f.unit->GetPos() == corrected && fabs2(f.unit->GetPos() - neighbor->GetPos()) >= 1.f - EPS_VALUE,
			"B13 landing push separates real neighbors around the corrected static-safe center before any resolver tick");
		neighbor->Cleanup();
	}
	std::printf("B13 landing push checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
