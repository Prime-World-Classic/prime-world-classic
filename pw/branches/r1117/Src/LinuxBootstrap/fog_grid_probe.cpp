#include "../System/systemStdAfx.h"
#include "fog_grid_probe.h"
#include "../PF_GameLogic/StringExecutorBootstrap.h"
#include "../PF_GameLogic/WarFog.h"
#include "../Scene/HeightsController.h"

#include <cstdio>
#include <initializer_list>

#if !defined(PW_LINUX_NULL_RENDER)
#error The fog grid probe requires PW_LINUX_NULL_RENDER.
#endif

namespace
{
/** Keep checks active in release builds and include fixture dimensions in failures. */
struct Checks
{
	int count = 0;
	int failures = 0;
	int width = 0;
	int height = 0;
	int tileSize = 1;

	void Check(bool condition, const char* name)
	{
		++count;
		if (condition) return;
		++failures;
		std::printf("Fog grid FAIL [%dx%d, visTile=%d]: %s\n", width, height, tileSize, name);
	}
};

/** Convert a logical fog cell to the normal tile coordinates consumed by FogOfWar. */
SVector Tile(int x, int y, int size)
{
	return SVector(x * size, y * size);
}

/** Direct production fixture, matching WarFog.test.cpp without an asset-backed world. */
CObj<NWorld::FogOfWar> NewFog(const Checks& checks)
{
	return new NWorld::FogOfWar(0, 3, checks.width, checks.height, checks.tileSize, checks.tileSize);
}

/** Check raw exported row-major cells against public queries across the whole grid. */
void CheckMaps(Checks& checks, const NWorld::FogOfWar& fog, int object = NWorld::WAR_FOG_BAD_ID)
{
	bool agrees = true;
	bool explored = true;
	bool objectAgrees = true;
	const NWorld::FogOfWar::VisMap& map = *fog.GetVisMap(1);
	const NWorld::FogOfWar::VisMapMask& mask = *fog.GetVisMapMask(1);
	checks.Check(map.GetSizeX() == fog.GetWidth() && map.GetSizeY() == fog.GetHeight(), "exported dimensions");
	checks.Check(mask.GetSizeX() == map.GetSizeX() && mask.GetSizeY() == map.GetSizeY(), "mask dimensions");
	for (int y = 0; y < fog.GetHeight(); ++y)
		for (int x = 0; x < fog.GetWidth(); ++x)
		{
			const SVector tile = Tile(x, y, checks.tileSize);
			const bool visible = fog.IsTileVisible(tile, 1);
			agrees = agrees && (visible == (map[y][x] != 0));
			explored = explored && (!visible || mask[y][x]);
			if (object != NWorld::WAR_FOG_BAD_ID)
				objectAgrees = objectAgrees && (visible == fog.CanObjectSeePosition(object, tile));
		}
	checks.Check(agrees, "public visibility agrees with row-major export");
	checks.Check(explored, "visible cells appear in the exploration mask");
	checks.Check(objectAgrees, "object-local cache agrees with its sole global reveal");
}

/** Reveal off the square overlap, move, remove, then exercise direct fill/unfill. */
void MovementAndMasks(Checks& checks)
{
	CObj<NWorld::FogOfWar> fog = NewFog(checks);
	const bool wide = checks.width > checks.height;
	const SVector startCell((wide ? 95 : 40) / checks.tileSize, (wide ? 40 : 95) / checks.tileSize);
	const SVector start = Tile(startCell.x, startCell.y, checks.tileSize);
	const SVector endCell(5, 11);
	const SVector end = Tile(endCell.x, endCell.y, checks.tileSize);
	std::printf("Fog grid [%dx%d, visTile=%d]: rectangular query/reveal\n", checks.width, checks.height, checks.tileSize);
	std::fflush(stdout);
	checks.Check(!fog->IsTileVisible(start, 1), "empty off-axis cell hidden (legacy crash regression)");
	const int object = fog->AddObject(start, 1, 2 * checks.tileSize);
	fog->StepVisibility(0);
	checks.Check(fog->IsTileVisible(start, 1), "off-axis object revealed");
	checks.Check(!fog->IsTileVisible(start, 2), "faction isolation");
	checks.Check((*fog->GetVisMap(1))[startCell.y][startCell.x] == 1, "off-axis raw cell stores one reveal");
	CheckMaps(checks, *fog, object);
	fog->MoveObject(object, end);
	fog->StepVisibility(0);
	checks.Check(!fog->IsTileVisible(start, 1) && fog->IsTileVisible(end, 1), "move unmarks old cell and reveals new cell");
	checks.Check((*fog->GetVisMapMask(1))[startCell.y][startCell.x], "old explored cell retained");
	checks.Check((*fog->GetVisMapMask(1))[endCell.y][endCell.x], "new cell explored");
	checks.Check(!fog->IsTileVisible(Tile(endCell.y, endCell.x, checks.tileSize), 1), "transposed cell stays hidden");
	CheckMaps(checks, *fog, object);
	fog->RemoveObject(object);
	checks.Check(!fog->IsTileVisible(end, 1), "removal unmarks immediately");
	fog->StepVisibility(0);
	checks.Check((*fog->GetVisMapMask(1))[endCell.y][endCell.x], "removal keeps exploration");
	fog->ResetVisibility();
	checks.Check(!(*fog->GetVisMapMask(1))[endCell.y][endCell.x], "reset clears exploration");

	const SVector edge = Tile(fog->GetWidth() - 1, fog->GetHeight() - 1, checks.tileSize);
	fog->FillVisibilityMap(edge, 0, 1);
	fog->StepVisibility(0);
	checks.Check(fog->IsTileVisible(edge, 1), "last valid cell including partial visibility tiles");
	CheckMaps(checks, *fog);
	fog->FillVisibilityMap(edge, 0, 1, true);
	checks.Check(!fog->IsTileVisible(edge, 1), "direct unfill at bottom-right edge");
	fog->FillVisibilityMap(end, 2 * checks.tileSize, 1);
	fog->StepVisibility(0);
	checks.Check(fog->IsTileVisible(end, 1), "direct radial fill");
	CheckMaps(checks, *fog);
	fog->FillVisibilityMap(end, 2 * checks.tileSize, 1, true);
	checks.Check(!fog->IsTileVisible(end, 1), "direct radial unfill");
}

/** An east-facing wall makes visibility asymmetric, exposing local-cache swaps. */
void Obstacles(Checks& checks)
{
	CObj<NWorld::FogOfWar> fog = NewFog(checks);
	const SVector start = Tile(4, 7, checks.tileSize);
	const SVector target = Tile(9, 7, checks.tileSize);
	const int object = fog->AddObject(start, 1, 8 * checks.tileSize);
	fog->StepVisibility(0);
	checks.Check(fog->IsTileVisible(target, 1), "target visible before wall");
	vector<SVector> wall;
	for (int y = 6; y <= 8; ++y) wall.push_back(Tile(6, y, checks.tileSize));
	fog->AddObstacle(wall);
	fog->StepVisibility(0);
	checks.Check(!fog->IsTileVisible(target, 1), "wall blocks east target");
	checks.Check(!fog->CanObjectSeePosition(object, target), "wall blocks object-local target");
	checks.Check(fog->CanObjectSeePosition(object, Tile(4, 10, checks.tileSize)), "wall does not block north target");
	CheckMaps(checks, *fog, object);
	fog->RemoveObstacle(wall);
	fog->StepVisibility(0);
	checks.Check(fog->IsTileVisible(target, 1), "wall removal restores visibility");
	CheckMaps(checks, *fog, object);
	fog->RemoveObject(object);

	const int edgeObject = fog->AddObject(Tile(0, 0, checks.tileSize), 1, 4 * checks.tileSize);
	fog->StepVisibility(0);
	checks.Check(!fog->IsTileVisible(SVector(-1, 0), 1), "negative X does not truncate into visible border");
	checks.Check(!fog->IsTileVisible(SVector(0, -1), 1), "negative Y does not truncate into visible border");
	const NWorld::FogOfWar::VisMap before = *fog->GetVisMap(1);
	vector<SVector> outside;
	outside.push_back(Tile(fog->GetWidth(), 0, checks.tileSize));
	outside.push_back(Tile(0, fog->GetHeight(), checks.tileSize));
	outside.push_back(Tile(fog->GetWidth(), fog->GetHeight(), checks.tileSize));
	fog->AddObstacle(outside);
	fog->StepVisibility(0);
	bool unchanged = true;
	const NWorld::FogOfWar::VisMap& after = *fog->GetVisMap(1);
	for (int y = 0; y < fog->GetHeight(); ++y)
		for (int x = 0; x < fog->GetWidth(); ++x)
			unchanged = unchanged && before[y][x] == after[y][x];
	checks.Check(unchanged, "obstacles equal to width/height are rejected without cell aliasing");
	fog->RemoveObstacle(outside);
	fog->RemoveObject(edgeObject);
}

/** Deterministic terrain mock with a raised east wall and per-tile sampling counts. */
class MockHeights : public NScene::IHeightsController
{
public:
	mutable CArray2D<int> visits;
	mutable int invalid = 0;
	int tileSize;

	MockHeights(int width, int height, int size) : visits(width, height), tileSize(size)
	{
		visits.FillZero();
	}

	/** Unused interface paths fail explicitly rather than fabricate heights. */
	bool GetHeight(float, float, int, float*, CVec3*) const override { return false; }
	bool GetHeightsOfSquareArea(int, float, float, float&, float&, float[]) const override { return false; }

	/** Return finite heights even for rejected requests so a failing probe is deterministic. */
	bool GetHeightByTile(int x, int y, int layer, float* height, CVec3*) const override
	{
		*height = 0;
		if (x < 0 || y < 0 || x >= visits.GetSizeX() || y >= visits.GetSizeY() || layer != 1)
		{
			++invalid;
			return false;
		}
		++visits[y][x];
		if (x / tileSize == 6 && y / tileSize >= 6 && y / tileSize <= 8) *height = 10.f;
		return true;
	}
};

/** Rectangular source sampling, height-limited visibility and teammate assistance. */
void Heights(Checks& checks)
{
	CObj<NWorld::FogOfWar> fog = NewFog(checks);
	CArray2D<float> source(checks.width, checks.height);
	source.FillZero();
	MockHeights heights(checks.width, checks.height, checks.tileSize);
	fog->ApplyHeightMap(source, heights);
	bool sampledOnce = heights.invalid == 0;
	for (int y = 0; y < checks.height; ++y)
		for (int x = 0; x < checks.width; ++x)
			sampledOnce = sampledOnce && heights.visits[y][x] == 1;
	checks.Check(sampledOnce, "height source samples width-by-height exactly once without swapped bounds");
	fog->ApplyHeightSettings(true, 1.f);
	const SVector target = Tile(9, 7, checks.tileSize);
	const int object = fog->AddObject(Tile(4, 7, checks.tileSize), 1, 8 * checks.tileSize);
	fog->StepVisibility(0);
	checks.Check(!fog->IsTileVisible(target, 1), "height wall hides east target");
	checks.Check(!fog->CanObjectSeePosition(object, target), "height-limited target needs teammate visibility");
	checks.Check(fog->CanObjectSeePosition(object, Tile(4, 10, checks.tileSize)), "height wall leaves north target visible");
	CheckMaps(checks, *fog, object);
	const int teammate = fog->AddObject(target, 1, 0);
	fog->StepVisibility(0);
	checks.Check(fog->CanObjectSeePosition(object, target), "teammate reveals potentially-visible local cell");
	fog->RemoveObject(teammate);
	checks.Check(!fog->CanObjectSeePosition(object, target), "removing teammate restores height limitation");
	fog->RemoveObject(object);
}
}

bool RunPrimeWorldLinuxFogGridProbe()
{
	Checks checks;
	for (const bool wide : {true, false})
		for (const int tileSize : {1, 3})
		{
			checks.width = wide ? 120 : 80;
			checks.height = wide ? 80 : 120;
			checks.tileSize = tileSize;
			MovementAndMasks(checks);
			Obstacles(checks);
			Heights(checks);
		}
	std::printf("Fog grid: %d checks, %d failures\n", checks.count, checks.failures);
	return checks.failures == 0;
}
