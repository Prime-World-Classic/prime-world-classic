// CPU-only admission regressions. No renderer, DB, GL, or third-party dependencies.
#include "scene_window.h"
#include "map_camera.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <memory>
#include <vector>

using namespace LinuxBootstrap;

namespace
{
std::size_t checks = 0;
void Check(bool value)
{
	++checks;
	if (!value) { std::fprintf(stderr, "check %zu failed\n", checks); std::abort(); }
}

SceneWindowCandidate Candidate(SceneWindowKind kind, std::size_t object,
	double x, double y, double radius = 1.0)
{
	SceneWindowCandidate result;
	result.kind = kind;
	result.objectIndex = object;
	result.bounds = {x - radius, y - radius, x + radius, y + radius};
	return result;
}

void BoundsAndIdentity()
{
	SceneWindowRequest request;
	request.bounds = {90, 90, 110, 110};
	request.focusX = request.focusY = 100;
	request.guardBand = 5;
	std::vector<SceneWindowCandidate> candidates;
	for (std::size_t i = 0; i < 3000; ++i)
		candidates.push_back(Candidate(SceneWindowKind::Static, i, -100, -100));
	candidates.push_back(Candidate(SceneWindowKind::Static, 9001, 100, 100));
	candidates.push_back(Candidate(SceneWindowKind::Static, 9002, 114, 100));
	// Distant origin, but its large world AABB reaches the view.
	candidates.push_back(Candidate(SceneWindowKind::Static, 9003, 300, 100, 202));
	SceneWindowMetrics metrics;
	const auto ranked = RankSceneWindow(candidates, request, metrics);
	Check(ranked.size() == 3);
	Check(candidates[ranked[0]].objectIndex == 9001);
	Check(candidates[ranked[1]].objectIndex == 9003);
	Check(candidates[ranked[2]].objectIndex == 9002);
	Check(metrics.outsideWindow == 3000);
	Check(metrics.overlapping == 2);
	Check(metrics.guardCandidates == 1);
	// Equal-distance candidates break ties by original identity, not catalog order.
	std::swap(candidates[3000], candidates[3002]);
	const auto reranked = RankSceneWindow(candidates, request, metrics);
	Check(candidates[reranked[0]].objectIndex == 9001);
	Check(candidates[reranked[1]].objectIndex == 9003);
	Check(!SceneWindowBounds{1, 0, -1, 2}.Valid());
	Check(!SceneWindowBounds{0, 0, std::numeric_limits<double>::infinity(), 2}.Valid());
	Check(SceneWindowBounds{0, 0, 1, 1}.Overlaps({1, 1, 2, 2}));
	candidates.push_back(Candidate(SceneWindowKind::Terrain, 10000, 100, 100));
	candidates.back().bounds.minX = std::numeric_limits<double>::quiet_NaN();
	RankSceneWindow(candidates, request, metrics);
	Check(metrics.invalidBounds == 1);
}

void WindowEpochs()
{
	SceneWindowState state;
	SceneWindowRequest request;
	request.bounds = {-10, -10, 10, 10};
	request.guardBand = 12;
	Check(state.NeedsUpdate(request));
	Check(state.Publish(request, {4, 5}));
	Check(state.Revision() == 1);
	for (int i = 0; i < 1000; ++i) Check(!state.NeedsUpdate(request));
	request.bounds = {-9, -9, 11, 11};
	request.focusX = request.focusY = 1;
	Check(!state.NeedsUpdate(request));
	request.bounds = {100, 100, 120, 120};
	request.focusX = request.focusY = 110;
	Check(state.NeedsUpdate(request));
	Check(state.Publish(request, {9, 10})); // Same count must invalidate caches.
	Check(state.Revision() == 2);
	Check(!state.Publish(request, {9, 10}));
	Check(state.Revision() == 2);
	Check(state.Publish(request, {10, 9})); // Order defines cache-slot identity too.
	Check(state.Publish(request, {}));
	Check(state.Revision() == 4);
	request.bounds = {108, 108, 112, 112};
	Check(state.NeedsUpdate(request)); // Substantial zoom-in must reprioritize.
	request.bounds.maxX = std::numeric_limits<double>::infinity();
	Check(!state.NeedsUpdate(request));
}

void BudgetsAndReuse()
{
	SceneWindowLimits limits;
	limits.maxGeometryAssets = 2;
	limits.maxTextureAssets = 2;
	limits.maxGeometrySourceBytes = 12;
	limits.maxTextureSourceBytes = 16;
	limits.maxTextureDecodedBytes = 32;
	SceneAssetLedger ledger(limits);
	const SceneAssetCost a{SceneAssetKind::Geometry, "static:one", 6, 0};
	const SceneAssetCost b{SceneAssetKind::Geometry, "static:two", 6, 0};
	const SceneAssetCost t{SceneAssetKind::Texture, "texture:one", 8, 16};
	Check(ledger.Admit({a, a, t}) == SceneAssetAdmission::Accepted);
	Check(ledger.GeometryCount() == 1 && ledger.TextureCount() == 1);
	Check(ledger.GeometryBytes() == 6 && ledger.TextureBytes() == 8);
	Check(ledger.Admit({a, t}) == SceneAssetAdmission::Accepted);
	Check(ledger.GeometryCount() == 1);
	Check(ledger.Admit({b}) == SceneAssetAdmission::Accepted);
	Check(ledger.GeometryCount() == 2);
	Check(ledger.Admit({{SceneAssetKind::Geometry, "static:three", 1, 0}})
		== SceneAssetAdmission::CumulativeLimit);
	Check(ledger.Admit({{SceneAssetKind::Texture, "texture:two", 8, 17}})
		== SceneAssetAdmission::CumulativeLimit);
	Check(ledger.TextureCount() == 1); // Failed transaction consumes nothing.
	Check(ledger.Admit({{SceneAssetKind::Texture, "texture:two", 8, 16}})
		== SceneAssetAdmission::Accepted);
	Check(ledger.TextureDecodedBytes() == 32);
	Check(ledger.Admit({a, b, t}) == SceneAssetAdmission::Accepted);
	// Changed bytes under the same key are not an admitted old asset.
	Check(ledger.Admit({{SceneAssetKind::Geometry, "static:one", 7, 0}})
		== SceneAssetAdmission::ChangedAsset);
	Check(ledger.Admit({{SceneAssetKind::Geometry, "", 1, 0}})
		== SceneAssetAdmission::InvalidAsset);
	Check(ledger.Admit({{SceneAssetKind::Geometry, "overflow", std::numeric_limits<std::size_t>::max(), 0}})
		== SceneAssetAdmission::CumulativeLimit);
	SceneAssetLedger fresh;
	Check(fresh.Admit({a, {SceneAssetKind::Geometry, "static:one", 9, 0}})
		== SceneAssetAdmission::ChangedAsset);
	Check(fresh.GeometryCount() == 0);
	Check(fresh.Admit({{static_cast<SceneAssetKind>(7), "bad-kind", 1, 1}}) == SceneAssetAdmission::InvalidAsset);
	SceneActiveBudget active;
	for (std::size_t i = 0; i < 256; ++i) Check(active.Admit(SceneWindowKind::Static));
	Check(!active.Admit(SceneWindowKind::Static));
	for (std::size_t i = 0; i < 192; ++i) Check(active.Admit(SceneWindowKind::Terrain));
	Check(!active.Admit(SceneWindowKind::Terrain));
	for (std::size_t i = 0; i < 48; ++i) Check(active.Admit(SceneWindowKind::Animated));
	Check(!active.Admit(SceneWindowKind::Animated));
	for (std::size_t i = 0; i < 64; ++i) Check(active.Admit(SceneWindowKind::Sky));
	Check(!active.Admit(SceneWindowKind::Sky));
	Check(!active.Admit(static_cast<SceneWindowKind>(4)));
	Check(!active.HasRoom(static_cast<SceneWindowKind>(-1)));
}
}

int main()
{
	const auto max = std::numeric_limits<double>::max();
	const MapCameraFrame extreme(0, 0, max, max, 1280, 720, 8, 56, 0, 0, 0, true, 0);
	Check(!extreme.valid);
	const MapCameraFrame camera(0, 0, 410, 410, 1280, 720, 8, 56, 0, 0, 0, true, 0);
	MapCameraFrame::Point origin{1, 2, 3}, direction{4, 5, 6};
	Check(!camera.Ray(max, max, origin, direction) && origin.x == 1 && origin.y == 2 && origin.z == 3 && direction.x == 4);
	BoundsAndIdentity();
	WindowEpochs();
	BudgetsAndReuse();
	std::printf("scene-window checks=%zu failures=0\n", checks);
}
