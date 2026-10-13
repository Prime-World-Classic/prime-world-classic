/** Exercise the engine's reusable collision-mask array through growth, reuse and process exit. */
static void TileMaskOwnershipB13(Checks& check)
{
	RoutingFixtureB13 fixture(check); if (!fixture.world) return;
	TileMap* map = fixture.world->GetTileMap();
	Placement placement = NULLPLACEMENT; placement.pos = CVec3(10, 10, 0);
	for (int size : {4, 8, 2, 8, 12, 3})
	{
		const CTRect<float> collision(0, 0, float(size), float(size));
		vector<CTPoint<int>> mask;
		for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x)
			mask.push_back(CTPoint<int>(x, y));
		vector<SVector> full;
		map->FillOccupiedTiles(full, collision, 1, mask, placement);
		bool exact = full.size() == (size + 1) * (size + 1);
		int index = 0;
		for (int y = 10; y <= 10 + size; ++y) for (int x = 10; x <= 10 + size; ++x, ++index)
			exact &= index < full.size() && full[index] == SVector(x, y);
		check(exact, "tile mask: growth and reuse preserve exact fully occupied tiles");

		mask.clear(); mask.push_back(CTPoint<int>(0, 0));
		mask.push_back(CTPoint<int>(0, 0)); // Duplicate authored entries do not duplicate output tiles.
		mask.push_back(CTPoint<int>(-1, 0)); mask.push_back(CTPoint<int>(size, 0));
		mask.push_back(CTPoint<int>(0, -1)); mask.push_back(CTPoint<int>(0, size));
		vector<SVector> sparse; sparse.push_back(SVector(1, 1));
		map->FillOccupiedTiles(sparse, collision, 1, mask, placement);
		check(sparse.size() == 5 && sparse[0] == SVector(1, 1)
			&& sparse[1] == SVector(10, 10) && sparse[2] == SVector(11, 10)
			&& sparse[3] == SVector(10, 11) && sparse[4] == SVector(11, 11),
			"tile mask: reuse clears stale bits, ignores invalid entries, and appends unique tiles");
		vector<SVector> unmasked; mask.clear();
		map->FillOccupiedTiles(unmasked, collision, 1, mask, placement);
		check(unmasked == full, "tile mask: an empty mask still represents the full collision rectangle");
		CTRect<int> bounds(1, 2, 3, 4);
		map->FillOccupiedTiles(sparse, CTRect<float>(0, 0, 0, 2), 1, mask, placement, &bounds);
		check(sparse.size() == 5 && bounds.x1 == 1 && bounds.y1 == 2 && bounds.x2 == 3 && bounds.y2 == 4,
			"tile mask: zero-area collision leaves append output and optional bounds unchanged");
	}
}
