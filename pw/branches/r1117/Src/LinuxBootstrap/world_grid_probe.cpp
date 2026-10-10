#include "../System/systemStdAfx.h"
#include "world_grid_probe.h"
#include "../PF_GameLogic/StringExecutorBootstrap.h"
#include "../PF_GameLogic/PFWorld.h"
#include "../PF_GameLogic/PFResourcesCollectionClient.h"
#include "../PF_GameLogic/DBAdvMap.h"
#include "../PF_GameLogic/DBSessionRoots.h"
#include "../Terrain/DBTerrain.h"
#include "../PF_GameLogic/TileMap.h"
#include "../PF_GameLogic/WarFog.h"
#include <cstdio>
#include <climits>

bool RunPrimeWorldLinuxWorldGridProbe()
{
	// Production sessions own the AI resource; avoid the legacy static fallback.
	NDb::SessionLogicRoot* logic = new NDb::SessionLogicRoot;
	logic->aiLogic = new NDb::AILogicParameters;
	NDb::SessionRoot* session = new NDb::SessionRoot;
	session->logicRoot = logic;
	NDb::SessionRoot::InitRoot(session);
	int checks = 0, failures = 0;
	const auto check = [&](bool result, const char* name)
	{
		++checks;
		if (!result) { ++failures; std::printf("World grid FAIL: %s\n", name); }
	};
	for (int density : {4, 10, 20, 0, -1, INT_MAX})
	{
		NDb::Terrain* terrain = new NDb::Terrain;
		terrain->elemXCount = 12;
		terrain->elemYCount = 12;
		terrain->tilesPerElement = density;
		NDb::AdvMap* map = new NDb::AdvMap;
		map->terrain = terrain;
		NDb::AdvMapDescription* description = new NDb::AdvMapDescription;
		description->map = map;
		NDb::Ptr<NDb::AdvMapDescription> owner = description;
		CObj<NCore::IWorldBase> worldOwner = NWorld::PFWorld::CreatePFWorld();
		auto* world = dynamic_cast<NWorld::PFWorld*>(worldOwner.GetPtr());
		const bool loaded = world->LoadMap(description, 0, NCore::TPlayersStartInfo(), 0, false,
			NWorld::PFResourcesCollection::TalentMap());
		if (density <= 0 || density == INT_MAX)
		{
			check(!loaded, "reject invalid or excessive grid");
			continue;
		}
		check(loaded, "mock map load");
		if (!loaded) continue;
		auto* tiles = world->GetTileMap();
		check(tiles && tiles->GetSizeX() == 12 * density && tiles->GetSizeY() == 12 * density, "tile counts");
		check(world->GetMapSize() == CVec2(120.f, 120.f), "world meters independent of density");
		check(tiles->GetTileSize() == 10.f / density, "terrain meters per tile");
		const SVector center = tiles->GetTile(60.f, 40.f);
		check(center == SVector(6 * density, 4 * density), "world center to tile");
		check(tiles->GetTile(tiles->GetPointByTile(center)) == center, "tile-center round trip");
		check(tiles->IsPointOutsideMap(tiles->GetTile(120.f, 40.f).x, center.y), "world edge excluded");
		check(!tiles->IsPointOutsideMap(tiles->GetTile(119.f, 79.f).x, tiles->GetTile(119.f, 79.f).y), "world interior");
		if (tiles->IsPointOutsideMap(center.x, center.y)) continue;
		{
			NWorld::MapModeChanger mode(NWorld::MAP_MODE_BUILDING, tiles);
			check(tiles->CanUnitGo(1, center), "clear movement cell");
			vector<SVector> blocked;
			blocked.push_back(center);
			tiles->MarkObject(blocked, true, NWorld::MAP_MODE_BUILDING);
			check(!tiles->CanUnitGo(1, tiles->GetTile(60.f, 40.f)), "obstacle in world meters");
			tiles->MarkObject(blocked, false, NWorld::MAP_MODE_BUILDING);
			check(tiles->CanUnitGo(1, center), "released movement cell");
		}
		auto* fog = world->GetFogOfWar();
		check(fog != 0, "fog initialized");
		const int id = fog->AddObject(center, 1, tiles->GetLenghtInTiles(15.f));
		fog->StepVisibility(0.1f);
		check(fog->IsTileVisible(tiles->GetTile(60.f, 40.f), 1), "fog world center visible");
		check(!fog->IsTileVisible(tiles->GetTile(95.f, 40.f), 1), "fog outside meter radius hidden");
		check(!fog->IsTileVisible(center, 2), "fog faction isolation");
		fog->RemoveObject(id);
	}
	std::printf("World grid: %d checks, %d failures\n", checks, failures);
	NDb::SessionRoot::InitRoot(0);
	return failures == 0;
}
