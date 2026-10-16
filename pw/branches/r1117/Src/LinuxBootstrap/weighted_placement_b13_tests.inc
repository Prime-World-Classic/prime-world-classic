#include "../System/Bresenham.h"

/** Original tile-only weighted search oracle, independent of the native placement helpers. */
static bool WeightedReferenceB13(TileMap* map, SVector tile, SVector attractor, int radius, float power, SVector& result)
{
	MapModeChanger mode(MAP_MODE_STATIC, map);
	if (map->CanUnitGo(1, tile)) { result = tile; return true; }
	const float weight = .5f + Clamp(power, 0.f, 1.f) / 2;
	float best = radius * 1000.f; bool found = false;
	CBres line; line.InitPoint(tile, attractor); SVector current = tile;
	const SVector shift = abs(tile.x - attractor.x) < abs(tile.y - attractor.y) ? SVector(1, 0) : SVector(0, 1);
	for (int dx = 0; dx < radius && current != attractor; ++dx)
	{
		float score = dx * (1 - weight);
		if (score > best) break;
		if (map->CanUnitGo(1, current)) { result = current; return true; }
		for (int dy = 1; dy < radius / 2; ++dy)
		{
			score = dy * weight + dx * (1 - weight);
			if (score > best) break;
			SVector candidate = current + shift * dy;
			if (!map->CanUnitGo(1, candidate))
			{
				candidate = current - shift * dy;
				if (!map->CanUnitGo(1, candidate)) continue;
			}
			best = score; found = true; result = candidate;
		}
		line.MakeStep(); current += line.GetDirection();
	}
	return found;
}

/** Authored attraction changes the best cell, not just a preferred step away from the attractor. */
static void WeightedDirectionB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	LandingFixtureB13 f(check); if (!f.world) return;
	auto* map = f.world->GetTileMap(); vector<SVector> blocked;
	blocked.push_back(SVector(70, 60)); blocked.push_back(SVector(69, 60)); map->MarkObject(blocked, true, MAP_MODE_STATIC);
	for (float power : {-1.f, 0.f, .25f, .5f, 1.f, 2.f})
	{
		CVec2 found(1, 2); const CVec2 expected = power < .5f ? CVec2(70.5f, 61.5f) : CVec2(68.5f, 60.5f);
		check(f.unit->FindFreePlace(CVec2(70.6f, 60.6f), CVec2(60.5f, 60.5f), 8, power, found, false, false, MAP_MODE_STATIC)
			&& found == expected, "B13 weighted placement preserves original perpendicular preference versus movement toward the attractor");
	}
	CVec2 found(1, 2);
	check(!f.unit->FindFreePlace(CVec2(70.5f, 60.5f), CVec2(70.5f, 60.5f), 8, 1, found)
		&& found == CVec2(1, 2), "B13 blocked coincident attractor terminates without inventing radial fallback");
	for (int radius : {0, 1})
		check(!f.unit->FindFreePlace(CVec2(70.5f, 60.5f), CVec2(60.5f, 60.5f), radius, 1, found),
			"B13 short weighted search does not borrow radial footprint expansion");
	std::printf("B13 weighted direction checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Differential original Bresenham/tie/extent checks on deterministic mock obstacle layouts. */
static void WeightedSearchB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	LandingFixtureB13 f(check); if (!f.world) return;
	auto* map = f.world->GetTileMap(); const int originalMode = map->GetMode(); const CVec2 origin = f.unit->GetPos();
	for (int layout = 0; layout < 8; ++layout)
	{
		vector<SVector> cells;
		for (int x = 61; x < 80; ++x) for (int y = 51; y < 70; ++y)
			if ((x == 70 && y == 60) || ((x * 17 + y * 31 + layout * 7) % (layout + 2) == 0)) cells.push_back(SVector(x, y));
		map->MarkObject(cells, true, MAP_MODE_STATIC);
		for (const SVector& direction : {SVector(-9, 0), SVector(9, 0), SVector(0, -9), SVector(0, 9),
			SVector(-9, -4), SVector(-4, 9), SVector(9, -4), SVector(4, 9)})
		for (int radius : {2, 3, 4, 8}) for (float power : {0.f, .25f, .5f, 1.f})
		{
			const SVector tile(70, 60), attractor = tile + direction; SVector expected;
			const bool reference = WeightedReferenceB13(map, tile, attractor, radius, power, expected);
			CVec2 found(1, 2);
			const bool actual = f.unit->FindFreePlace(CVec2(70.6f, 60.6f), map->GetPointByTile(attractor), radius, power,
				found, false, false, MAP_MODE_STATIC);
			check(actual == reference && (actual ? found == map->GetPointByTile(expected) : found == CVec2(1, 2)),
				"B13 weighted result matches original Bresenham order, perpendicular sign preference, score ties and exclusive extents");
		}
		map->MarkObject(cells, false, MAP_MODE_STATIC);
	}
	check(map->GetMode() == originalMode && f.unit->GetPos() == origin, "B13 weighted queries preserve map mode and never move their owner");
	std::printf("B13 weighted search checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Weighted placement retains independent tile layers and the native unit-circle occupancy contract. */
static void WeightedPolicyB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (int layer : {MAP_MODE_STATIC, MAP_MODE_BUILDING, MAP_MODE_DYNAMIC})
	for (int mask : {0, int(MAP_MODE_STATIC), int(MAP_MODE_BUILDING), int(MAP_MODE_DYNAMIC)})
	for (bool ignore : {false, true})
	{
		LandingFixtureB13 f(check); if (!f.world) continue;
		vector<SVector> cells; cells.push_back(SVector(70, 60)); f.world->GetTileMap()->MarkObject(cells, true, layer);
		const bool blocked = mask ? (mask & layer) != 0 : layer != MAP_MODE_DYNAMIC || !ignore;
		CVec2 found;
		check(f.unit->FindFreePlace(CVec2(70.6f, 60.6f), CVec2(60.5f, 60.5f), 8, 1, found, ignore, false, mask)
			&& found == CVec2(blocked ? 69.5f : 70.5f, 60.5f), "B13 weighted explicit masks override default ignoreUnits policy without skipping static geometry");
	}
	{
		LandingFixtureB13 f(check); if (!f.world) return;
		auto blocker = f.Mover(CVec2(70.5f, 60.5f)); blocker->Footprint(1);
		for (int mask : {0, int(MAP_MODE_STATIC), int(MAP_MODE_DYNAMIC)})
		{
			CVec2 found;
			check(f.unit->FindFreePlace(CVec2(70.5f, 60.5f), CVec2(60.5f, 60.5f), 8, 1, found, false, false, mask)
				&& found == CVec2(mask == MAP_MODE_STATIC ? 70.5f : 69.5f, 60.5f), "B13 weighted placement honors live unit circles only for selected dynamic layers");
		}
		blocker->Cleanup();
		CVec2 found;
		check(!f.unit->FindFreePlace(CVec2(150.5f, 100.5f), CVec2(140.5f, 100.5f), 2, 1, found, false, false, MAP_MODE_STATIC, true),
			"B13 weighted placement cannot ignore its requested personal line of sight");
	}
	{
		NatureOverrideFixtureB13 f(check); CVec2 found;
		for (bool native : {false, true})
			check(f.hero->FindFreePlace(CVec2(25.5f, 10.5f), CVec2(15.5f, 10.5f), 1, 1, found, true, native, MAP_MODE_STATIC) == !native,
				"B13 weighted native terrain reads actual authored nature independently of ignored unit occupancy");
		f.Map()->Override(NDb::FACTION_FREEZE, CCircle(CVec2(25, 10), 3), 1, 9901);
		check(f.hero->FindFreePlace(CVec2(25.5f, 10.5f), CVec2(15.5f, 10.5f), 1, 1, found, true, true, MAP_MODE_STATIC),
			"B13 weighted placement observes a live original native-terrain override");
		f.World()->Step(1, 1);
		check(!f.hero->FindFreePlace(CVec2(25.5f, 10.5f), CVec2(15.5f, 10.5f), 1, 1, found, true, true, MAP_MODE_STATIC),
			"B13 weighted terrain acceptance expires with the actual override");
	}
	std::printf("B13 weighted policy checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Reject unsafe numeric inputs before tile narrowing, without leaking query output or ghost/mode state. */
static void WeightedSafetyB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	LandingFixtureB13 f(check); if (!f.world) return;
	const CVec2 point(70.5f, 60.5f), attractor(60.5f, 60.5f), sentinel(1, 2);
	CVec2 found = sentinel;
	for (const CVec2& bad : {CVec2(-1, 60), CVec2(200, 60), CVec2(70, 120), CVec2(FLT_MAX, 60),
		CVec2(std::numeric_limits<float>::quiet_NaN(), 60), CVec2(70, std::numeric_limits<float>::infinity())})
	{
		check(!f.unit->FindFreePlace(bad, attractor, 8, 1, found, true) && found == sentinel,
			"B13 weighted placement rejects invalid destinations before floor-to-int conversion");
		found = sentinel;
		check(!f.unit->FindFreePlace(point, bad, 8, 1, found, true) && found == sentinel,
			"B13 weighted placement validates its attractor even when the destination is unblocked");
		found = sentinel;
	}
	for (float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
	{
		check(!f.unit->FindFreePlace(point, attractor, bad, 1, found, true) && found == sentinel,
			"B13 weighted nonfinite radius fails without entering an unbounded search");
		found = sentinel;
		check(!f.unit->FindFreePlace(point, attractor, 8, bad, found, true) && found == sentinel,
			"B13 weighted nonfinite attraction fails before scoring even with a legal center");
		found = sentinel;
	}
	check(!f.unit->FindFreePlace(point, attractor, -1, 1, found), "B13 weighted negative radius is not an unlimited search");
	for (int size : {0, MAX_UNIT_SIZE + 1})
	{
		f.unit->Footprint(size); found = sentinel;
		check(!f.unit->FindFreePlace(point, attractor, 8, 1, found, true) && found == sentinel,
			"B13 weighted placement validates its owner footprint before map indexing");
	}
	f.unit->Footprint(1);
	check(f.unit->FindFreePlace(point, attractor, FLT_MAX, 1, found) && found == point,
		"B13 finite extreme radius is safely bounded to reachable map coverage");
	vector<SVector> cells; cells.push_back(SVector(70, 60)); f.world->GetTileMap()->MarkObject(cells, true, MAP_MODE_STATIC);
	for (int ghost : {int(NDb::GHOSTMOVEMODE_IGNORESTATIC), int(NDb::GHOSTMOVEMODE_IGNOREBUILDINGS), int(NDb::GHOSTMOVEMODE_IGNOREDYNAMIC)})
	{
		const int token = f.unit->SetGhostMode(ghost, false);
		check(f.unit->FindFreePlace(point, attractor, 8, 1, found) && found == CVec2(ghost == NDb::GHOSTMOVEMODE_IGNORESTATIC ? 70.5f : 69.5f, 60.5f),
			"B13 weighted default policy retains independent original static and building ghost bits");
		check(f.unit->FindFreePlace(point, attractor, 8, 1, found, false, false, MAP_MODE_STATIC) && found == CVec2(69.5f, 60.5f),
			"B13 weighted explicit static mask retains priority over the owner's default ghost policy");
		f.unit->ResetGhostMode(token);
	}
	std::printf("B13 weighted safety checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Preserve the original weighted sight quirk: test the line anchor, not the chosen perpendicular cell. */
static void WeightedSightB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	LandingFixtureB13 f(check); if (!f.world) return;
	auto* map = f.world->GetTileMap(); SVector anchor; bool located = false;
	for (float range : {2.f, 3.f, 4.f, 8.f, 12.f})
	{
		f.unit->GetStat(NDb::STAT_VISIBILITYRANGE)->SetCoreValue(range); f.unit->OpenWarFog(); f.Tick();
		for (int x = 40; x <= 62 && !located; ++x) for (int y = 50; y <= 74 && !located; ++y)
			if (f.unit->CanSee(SVector(x, y)) && !f.unit->CanSee(SVector(x, y + 1))) { anchor = SVector(x, y); located = true; }
		if (located) break;
	}
	check(located, "B13 sight fixture finds an actual personal observer boundary without mocked visibility callbacks");
	if (located)
	{
		vector<SVector> cells; for (int dx = 0; dx < 4; ++dx) cells.push_back(anchor + SVector(dx, 0));
		map->MarkObject(cells, true, MAP_MODE_STATIC); CVec2 found;
		check(f.unit->FindFreePlace(map->GetPointByTile(anchor), map->GetPointByTile(anchor + SVector(10, 0)), 4, 0,
			found, false, false, MAP_MODE_STATIC, true) && found == map->GetPointByTile(anchor + SVector(0, 1)),
			"B13 original weighted visibility accepts the perpendicular cell when its line anchor is visible");
		map->MarkObject(cells, false, MAP_MODE_STATIC);
		cells.clear(); cells.push_back(anchor + SVector(0, 1)); cells.push_back(anchor + SVector(0, 2));
		map->MarkObject(cells, true, MAP_MODE_STATIC); found = CVec2(1, 2);
		check(!f.unit->FindFreePlace(map->GetPointByTile(anchor + SVector(0, 1)), map->GetPointByTile(anchor + SVector(1, 1)), 4, 0,
			found, false, false, MAP_MODE_STATIC, true) && found == CVec2(1, 2),
			"B13 visible opposite candidate cannot replace the original line-anchor sight policy or include the attractor endpoint");
		map->MarkObject(cells, false, MAP_MODE_STATIC);
	}
	std::printf("B13 weighted sight checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Original push placement tries weighted attraction before its radial fallback. */
static void WeightedPushB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (bool large : {false, true})
	{
		LandingFixtureB13 f(check); if (!f.world) continue;
		auto neighbor = f.Mover(large ? CVec2(71.5f, 60.5f) : CVec2(70.6f, 60.5f)); neighbor->Footprint(1);
		if (large) neighbor->SetObjectSize(3);
		f.unit->PlaceUnitWithPush(CVec2(70.5f, 60.5f), 8, true);
		const CVec2 expected = large ? CVec2(71.5f, 62.5f) : CVec2(70.5f, 61.5f);
		check(f.unit->GetPos() == CVec2(70.5f, 60.5f) && neighbor->GetPos() == expected,
			"B13 original push uses weighted perpendicular placement or radial fallback from the old position, not an invented outward step");
		const float clearance = (f.unit->GetObjectSize() + neighbor->GetObjectSize()) * .5f;
		check(fabs2(neighbor->GetPos() - f.unit->GetPos()) >= clearance * clearance - EPS_VALUE,
			"B13 original push search preserves native diameter-based unit separation before the next world tick");
		neighbor->Cleanup();
	}
	std::printf("B13 weighted push checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
