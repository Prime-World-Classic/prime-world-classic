/** Compare the documented inclusive tile rectangle without depending on rectangle operators. */
static bool TileMaskSameRectB13(const CTRect<int>& a, const CTRect<int>& b)
{
	return a.x1 == b.x1 && a.y1 == b.y1 && a.x2 == b.x2 && a.y2 == b.y2;
}

/**
 * Independent small-input oracle: invert a planar affine transform algebraically,
 * enumerate the original center/four-corner samples, and search the authored list.
 * It neither calls engine matrix helpers nor expands/indexes a scratch mask.
 * Callers use Z-only rotations and modest coordinates away from rounding ties.
 */
static vector<SVector> TileMaskReferenceB13(float mapTile, const CTRect<float>& collision,
	float snap, const vector<CTPoint<int>>& mask, const Placement& placement, CTRect<int>& bounds)
{
	if (snap <= 0) snap = 0.5f;
	const double left = floor(double(collision.x1) / snap) * snap;
	const double bottom = floor(double(collision.y1) / snap) * snap;
	const double right = ceil(double(collision.x2) / snap) * snap;
	const double top = ceil(double(collision.y2) / snap) * snap;
	const double cosine = 1 - 2 * double(placement.rot.z) * placement.rot.z;
	const double sine = 2 * double(placement.rot.z) * placement.rot.w;
	const double a = cosine * placement.scale.x, b = -sine * placement.scale.y;
	const double c = sine * placement.scale.x, d = cosine * placement.scale.y;
	const double determinant = a * d - b * c;
	double minX = std::numeric_limits<double>::max(), minY = minX;
	double maxX = -minX, maxY = -minY;
	for (int corner = 0; corner != 4; ++corner)
	{
		const double x = (corner & 1) ? right : left;
		const double y = (corner & 2) ? top : bottom;
		const double worldX = a * x + b * y + placement.pos.x;
		const double worldY = c * x + d * y + placement.pos.y;
		if (worldX < minX) minX = worldX;
		if (worldY < minY) minY = worldY;
		if (worldX > maxX) maxX = worldX;
		if (worldY > maxY) maxY = worldY;
	}
	const auto clampTile = [mapTile](double coordinate) -> int
	{
		const double tile = floor(coordinate / mapTile);
		return tile <= 0 ? 0 : tile >= 63 ? 63 : int(tile);
	};
	bounds = CTRect<int>(clampTile(minX), clampTile(minY), clampTile(maxX), clampTile(maxY));
	const int directions[5][2] = {{0, 0}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
	vector<SVector> result;
	for (int y = bounds.y1; y <= bounds.y2; ++y)
	{
		for (int x = bounds.x1; x <= bounds.x2; ++x)
		{
			for (int point = 0; point != 5; ++point)
			{
				const double dx = double(x) * mapTile + directions[point][0] * double(mapTile * 0.49f) - placement.pos.x;
				const double dy = double(y) * mapTile + directions[point][1] * double(mapTile * 0.49f) - placement.pos.y;
				const double localX = (d * dx - b * dy) / determinant;
				const double localY = (a * dy - c * dx) / determinant;
				if (localX < left || localX > right || localY < bottom || localY > top) continue;
				bool occupied = mask.empty();
				if (!occupied)
				{
					const int cellX = int(floor((localX - left) / snap));
					const int cellY = int(floor((localY - bottom) / snap));
					const int cellsX = int((right - left) / snap), cellsY = int((top - bottom) / snap);
					if (cellX < 0 || cellY < 0 || cellX >= cellsX || cellY >= cellsY) continue;
					for (int entry = 0; entry != mask.size(); ++entry)
						if (mask[entry].x == cellX && mask[entry].y == cellY) occupied = true;
				}
				if (!occupied) continue;
				result.push_back(SVector(x, y));
				break;
			}
		}
	}
	return result;
}

/** Run a valid call with an existing prefix, checking exact membership, order and bounds. */
static void TileMaskCompareB13(Checks& check, TileMap& map, const CTRect<float>& collision,
	float snap, const vector<CTPoint<int>>& mask, const Placement& placement, const char* label)
{
	CTRect<int> expectedBounds, actualBounds(-11, -12, -13, -14);
	const vector<SVector> expected = TileMaskReferenceB13(map.GetTileSize(), collision, snap, mask, placement, expectedBounds);
	vector<SVector> actual;
	actual.push_back(SVector(-7, -9));
	map.FillOccupiedTiles(actual, collision, snap, mask, placement, &actualBounds);
	bool same = actual.size() == expected.size() + 1 && actual[0] == SVector(-7, -9);
	for (int i = 0; i != expected.size(); ++i)
		same &= i + 1 < actual.size() && actual[i + 1] == expected[i];
	check(same && TileMaskSameRectB13(actualBounds, expectedBounds), label);
	vector<SVector> withoutBounds;
	map.FillOccupiedTiles(withoutBounds, collision, snap, mask, placement);
	check(withoutBounds == expected, "tile numeric: omitting optional bounds preserves the exact ordered result");
}

/** Numeric rejection is atomic; current map scale, not the first call, defines sample offsets. */
static void TileMaskNumericB13(Checks& check)
{
	TileMap map;
	vector<CTPoint<int>> emptyMask, oneCell;
	oneCell.push_back(CTPoint<int>(0, 0));

	// Keep this defined-behavior regression ahead of inputs unsafe in the old implementation.
	for (float tile : {1.0f, 4.0f, 0.25f, 2.0f, 0.5f, 8.0f, 1.0f})
	{
		map.Prepare(64, 64, tile);
		Placement placement = NULLPLACEMENT;
		placement.pos = CVec3(10.28125f * tile, 10.28125f * tile, 0);
		const CTRect<float> collision(0, 0, 0.25f * tile, 0.25f * tile);
		vector<SVector> tiles;
		CTRect<int> bounds(-1, -2, -3, -4);
		map.FillOccupiedTiles(tiles, collision, 0.25f * tile, emptyMask, placement, &bounds);
		check(tiles.size() == 1 && tiles[0] == SVector(10, 10)
			&& TileMaskSameRectB13(bounds, CTRect<int>(10, 10, 10, 10)),
			"tile numeric: re-Prepare uses current tile-size corner offsets, independently of first call");
		TileMap separate;
		separate.Prepare(64, 64, tile);
		TileMaskCompareB13(check, separate, collision, 0.25f * tile, oneCell, placement,
			"tile numeric: separate maps do not inherit another map's first sample offsets");
	}

	vector<CTPoint<int>> sparse;
	for (const CTPoint<int>& point : {CTPoint<int>(0, 0), CTPoint<int>(2, 3), CTPoint<int>(5, 1),
		CTPoint<int>(8, 9), CTPoint<int>(7, 4)}) sparse.push_back(point);
	const CTRect<float> modest(-0.625f, -0.375f, 1.375f, 1.875f);
	for (float tile : {0.25f, 0.5f, 1.0f, 2.0f, 4.0f})
	{
		map.Prepare(64, 64, tile);
		for (int variant = 0; variant != 4; ++variant)
		{
			Placement placement = NULLPLACEMENT;
			placement.pos = CVec3(9.317f * tile, 11.193f * tile, 37);
			if (variant == 1) placement.rot = CQuat(0.61f, CVec3(0, 0, 1));
			if (variant == 2) placement.scale = CVec3(-1.25f, 0.75f, 2);
			if (variant == 3)
			{
				placement.rot = CQuat(-0.83f, CVec3(0, 0, 1));
				placement.scale = CVec3(0.75f, -1.5f, 0.5f);
			}
			TileMaskCompareB13(check, map, modest, 0.25f, sparse, placement,
				"tile numeric: masked identity/rotation/reflection matches independent five-point oracle");
			TileMaskCompareB13(check, map, modest, 0.25f, emptyMask, placement,
				"tile numeric: unmasked identity/rotation/reflection preserves inclusive rectangle and row order");
		}
	}

	map.Prepare(64, 64, 1);
	Placement good = NULLPLACEMENT;
	good.pos = CVec3(10.3125f, 12.1875f, 0);
	for (float fallback : {0.0f, -0.0f, -1.0f, -std::numeric_limits<float>::max()})
		TileMaskCompareB13(check, map, modest, fallback, sparse, good,
			"tile numeric: every finite nonpositive authored snap size retains the original 0.5 fallback");

	const int maxInt = std::numeric_limits<int>::max(), minInt = std::numeric_limits<int>::min();
	vector<CTPoint<int>> invalidEntries;
	for (const CTPoint<int>& point : {CTPoint<int>(maxInt, maxInt), CTPoint<int>(minInt, minInt),
		CTPoint<int>(0, maxInt), CTPoint<int>(0, minInt), CTPoint<int>(maxInt, 0),
		CTPoint<int>(minInt, 0), CTPoint<int>(-1, 1), CTPoint<int>(4, 0), CTPoint<int>(0, 4)})
		invalidEntries.push_back(point);
	const CTRect<float> square(0, 0, 4, 4);
	TileMaskCompareB13(check, map, square, 1, invalidEntries, good,
		"tile numeric: an entirely invalid nonempty list occupies nothing, without overflowing index arithmetic");
	invalidEntries.push_back(CTPoint<int>(0, 0));
	invalidEntries.push_back(CTPoint<int>(1, 1));
	TileMaskCompareB13(check, map, square, 1, invalidEntries, good,
		"tile numeric: INT_MIN/INT_MAX entries are ignored before multiplication, retaining valid entries and bounds");

	/** Check rejection and then reuse the same map and static scratch through a known valid call. */
	const auto reject = [&](const CTRect<float>& collision, float snap, const Placement& placement, const char* label)
	{
		vector<SVector> output;
		output.push_back(SVector(17, 19)); output.push_back(SVector(-3, -5));
		const vector<SVector> before = output;
		CTRect<int> bounds(31, 32, 33, 34);
		map.FillOccupiedTiles(output, collision, snap, oneCell, placement, &bounds);
		check(output == before && TileMaskSameRectB13(bounds, CTRect<int>(31, 32, 33, 34)), label);
		map.Prepare(64, 64, 1);
		TileMaskCompareB13(check, map, modest, 0.25f, sparse, good,
			"tile numeric: rejected numeric input leaves the map/scratch usable by a subsequent valid call");
	};
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float infinity = std::numeric_limits<float>::infinity();
	const float largest = std::numeric_limits<float>::max();
	{
		vector<SVector> output; output.push_back(SVector(17, 19));
		const int capacity = output.capacity();
		const SVector* address = &output[0];
		vector<CTPoint<int>> manyCells;
		for (int i = 0; i <= capacity; ++i) manyCells.push_back(CTPoint<int>(0, 0));
		Placement singular = good; singular.scale.x = 0;
		CTRect<int> bounds(31, 32, 33, 34);
		map.FillOccupiedTiles(output, square, 1, manyCells, singular, &bounds);
		check(output.size() == 1 && output[0] == SVector(17, 19) && output.capacity() == capacity
			&& &output[0] == address && TileMaskSameRectB13(bounds, CTRect<int>(31, 32, 33, 34)),
			"tile numeric: rejected geometry preserves caller storage and outstanding element addresses");
	}
	char label[192];
	for (float bad : {nan, infinity, -infinity})
	{
		for (int component = 0; component != 4; ++component)
		{
			CTRect<float> collision(-1, -1, 1, 1);
			float* fields[] = {&collision.x1, &collision.y1, &collision.x2, &collision.y2};
			*fields[component] = bad;
			std::snprintf(label, sizeof(label), "tile numeric: nonfinite collision component %d preserves output/bounds", component);
			reject(collision, 0.5f, good, label);
		}
		reject(square, bad, good, "tile numeric: nonfinite authored snap size must not use the finite fallback");
		map.Prepare(64, 64, bad);
		reject(square, 1, good, "tile numeric: nonfinite map tile size preserves output/bounds");
		for (int component = 0; component != 10; ++component)
		{
			Placement placement = good;
			float* fields[] = {&placement.pos.x, &placement.pos.y, &placement.pos.z,
				&placement.rot.x, &placement.rot.y, &placement.rot.z, &placement.rot.w,
				&placement.scale.x, &placement.scale.y, &placement.scale.z};
			*fields[component] = bad;
			std::snprintf(label, sizeof(label), "tile numeric: nonfinite placement component %d preserves output/bounds", component);
			reject(square, 1, placement, label);
		}
	}
	for (float badMapTile : {0.0f, -1.0f})
	{
		map.Prepare(64, 64, badMapTile);
		reject(square, 1, good, "tile numeric: nonpositive map scale is invalid, unlike the authored snap fallback");
	}
	for (int component = 0; component != 4; ++component)
	{
		CTRect<float> collision(-1, -1, 1, 1);
		float* fields[] = {&collision.x1, &collision.y1, &collision.x2, &collision.y2};
		*fields[component] = component < 2 ? -largest : largest;
		reject(collision, 0.5f, good, "tile numeric: finite FLT_MAX collision extents cannot become integer mask dimensions");
		Placement placement = good;
		placement.rot = CQuat(CVec4(1, 1, 1, 1));
		float* quaternion[] = {&placement.rot.x, &placement.rot.y, &placement.rot.z, &placement.rot.w};
		*quaternion[component] = largest;
		reject(square, 1, placement, "tile numeric: finite extreme quaternion producing a nonfinite matrix is rejected");
	}
	for (int component = 0; component != 3; ++component)
	{
		for (float scale : {0.0f, std::numeric_limits<float>::denorm_min(), largest})
		{
			Placement placement = good;
			if (scale == largest) placement.rot = CQuat(CVec4(1, 1, 1, 1));
			float* fields[] = {&placement.scale.x, &placement.scale.y, &placement.scale.z};
			*fields[component] = scale;
			reject(square, 1, placement,
				"tile numeric: zero scale, nonrepresentable inverse, or nonfinite composed matrix is rejected atomically");
		}
	}
	reject(CTRect<float>(-1, -1, 1, 1), largest, good,
		"tile numeric: finite extreme snap with an unrepresentable snapped span preserves output/bounds");
	for (float tiny : {1.0e-10f, std::numeric_limits<float>::min(), std::numeric_limits<float>::denorm_min()})
		reject(square, tiny, good, "tile numeric: tiny positive snap cannot overflow dimension conversion or allocation");
	reject(CTRect<float>(0, 0, float(maxInt), 1), 1, good,
		"tile numeric: float-rounded INT_MAX dimension is outside the representable integer range");
	reject(CTRect<float>(0, 0, 46341, 46341), 1, good,
		"tile numeric: dimension product beyond signed integer range is checked before multiplication");
	reject(CTRect<float>(0, 0, 4097, 4096), 1, good,
		"tile numeric: scratch mask just beyond 16*1024*1024 cells is rejected");
	reject(CTRect<float>(0, 0, 1, 1), 1.0f / 8192, good,
		"tile numeric: finite small snap with representable dimensions still respects the scratch limit");

	// The limit applies to allocated mask cells, not empty-list rectangle rasterization.
	Placement origin = NULLPLACEMENT;
	TileMaskCompareB13(check, map, CTRect<float>(0, 0, 4097, 4096), 1, emptyMask, origin,
		"tile numeric: an unmasked large rectangle needs no expanded-mask allocation");
	vector<SVector> boundary;
	CTRect<int> boundaryRect(-1, -2, -3, -4);
	map.FillOccupiedTiles(boundary, CTRect<float>(0, 0, 4096, 4096), 1, oneCell, origin, &boundaryRect);
	check(boundary.size() == 4 && boundary[0] == SVector(0, 0) && boundary[1] == SVector(1, 0)
		&& boundary[2] == SVector(0, 1) && boundary[3] == SVector(1, 1)
		&& TileMaskSameRectB13(boundaryRect, CTRect<int>(0, 0, 63, 63)),
		"tile numeric: exactly 16*1024*1024 scratch cells remain supported");
	boundary.clear();
	origin.pos = CVec3(10, 10, 0);
	map.FillOccupiedTiles(boundary, CTRect<float>(0, 0, 1, 1), 1.0f / 4096, oneCell, origin, &boundaryRect);
	check(boundary.size() == 1 && boundary[0] == SVector(10, 10)
		&& TileMaskSameRectB13(boundaryRect, CTRect<int>(10, 10, 11, 11)),
		"tile numeric: small positive snap exactly at the scratch-cell limit remains supported");

	for (float far : {1.0e30f, -1.0e30f, largest, -largest})
	{
		for (int axis = 0; axis != 3; ++axis)
		{
			Placement placement = NULLPLACEMENT;
			placement.pos = CVec3(axis == 1 ? 10.25f : far, axis == 0 ? 10.25f : far, 0);
			vector<SVector> output;
			output.push_back(SVector(-7, -9));
			CTRect<int> bounds(-1, -2, -3, -4);
			map.FillOccupiedTiles(output, CTRect<float>(-1, -1, 1, 1), 1, oneCell, placement, &bounds);
			const int edge = far < 0 ? 0 : 63;
			const CTRect<int> expected(axis == 1 ? 9 : edge, axis == 0 ? 9 : edge,
				axis == 1 ? 11 : edge, axis == 0 ? 11 : edge);
			check(output.size() == 1 && output[0] == SVector(-7, -9) && TileMaskSameRectB13(bounds, expected),
				"tile numeric: finite far-off-map position clamps before integer conversion and retains clamped rectangle");
		}
	}
	good.pos.z = largest;
	TileMaskCompareB13(check, map, modest, 0.25f, sparse, good,
		"tile numeric: finite FLT_MAX placement Z remains ignored by the original planar projection");
	good.pos.z = 0;
	for (float mapScale : {largest, std::numeric_limits<float>::min()})
	{
		map.Prepare(64, 64, mapScale);
		vector<SVector> output;
		CTRect<int> bounds(-1, -2, -3, -4);
		map.FillOccupiedTiles(output, CTRect<float>(0, 0, 1, 1), 1, oneCell, good, &bounds);
		const int edge = mapScale == largest ? 0 : 63;
		check(output.empty() && TileMaskSameRectB13(bounds, CTRect<int>(edge, edge, edge, edge)),
			"tile numeric: finite extreme positive map scale uses bounded tile conversion without requiring occupied tiles");
	}
	map.Prepare(64, 64, 1);
	TileMaskCompareB13(check, map, modest, 0.25f, sparse, good,
		"tile numeric: ordinary masked rasterization recovers after all numeric boundary cases");
}
