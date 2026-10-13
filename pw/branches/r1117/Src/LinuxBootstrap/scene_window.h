#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace LinuxBootstrap
{
/// Native map presentation policy only; never a camera, loader or global cache.
enum class SceneWindowKind { Static, Terrain, Animated };

/// Inclusive world-space XY AABB. Z stays in the original placement/mesh payload.
struct SceneWindowBounds
{
	double minX = 0, minY = 0, maxX = 0, maxY = 0;
	bool Valid() const
	{
		return std::isfinite(minX) && std::isfinite(minY) && std::isfinite(maxX)
			&& std::isfinite(maxY) && minX <= maxX && minY <= maxY;
	}
	bool Contains(const SceneWindowBounds& other) const
	{
		return minX <= other.minX && minY <= other.minY && maxX >= other.maxX && maxY >= other.maxY;
	}
	bool Overlaps(const SceneWindowBounds& other) const
	{
		return maxX >= other.minX && maxY >= other.minY && minX <= other.maxX && minY <= other.maxY;
	}
	double DistanceSquared(double x, double y) const
	{
		const double dx = std::max({minX - x, 0.0, x - maxX});
		const double dy = std::max({minY - y, 0.0, y - maxY});
		return dx * dx + dy * dy;
	}
	SceneWindowBounds Expanded(double margin) const
	{
		return {minX - margin, minY - margin, maxX + margin, maxY + margin};
	}
};

/// Caller supplies visible ground bounds and focus; no renderer or camera dependency.
struct SceneWindowRequest
{
	SceneWindowBounds bounds;
	double focusX = 0, focusY = 0;
	double guardBand = 12;
	bool Valid() const
	{
		return bounds.Valid() && bounds.maxX > bounds.minX && bounds.maxY > bounds.minY
			&& std::isfinite(focusX) && std::isfinite(focusY) && std::isfinite(guardBand)
			&& guardBand >= 0 && guardBand <= 1024 && bounds.Expanded(guardBand).Valid();
	}
};

/// Immutable metadata identity. Geometry filenames alone never identify placements.
struct SceneWindowCandidate
{
	SceneWindowKind kind = SceneWindowKind::Static;
	std::size_t payloadIndex = 0, objectIndex = 0, componentIndex = 0;
	SceneWindowBounds bounds;
};

/// Snapshot of the last selection. Outside-window candidates are not budget drops.
struct SceneWindowMetrics
{
	std::size_t scanned = 0, overlapping = 0, guardCandidates = 0, outsideWindow = 0, invalidBounds = 0;
	std::size_t activeBudgetDrops = 0, textureBudgetDrops = 0, cumulativeDrops = 0;
	std::size_t unsupportedDrops = 0, unavailableDrops = 0, textureLoads = 0, textureReuses = 0;
	std::size_t admittedStatic = 0, admittedTerrain = 0, admittedAnimated = 0;
};

/// Fixed presentation limits plus conservative process-lifetime asset admission limits.
struct SceneWindowLimits
{
	static constexpr std::size_t staticCount = 256, terrainCount = 192, animatedCount = 48, textureCount = 96;
	static constexpr std::size_t catalogCandidates = 65536, catalogObjects = 262144, catalogComponents = 262144;
	static constexpr std::size_t catalogTextures = 8192, catalogStringBytes = 32 * 1024 * 1024;
	static constexpr std::size_t pathBytes = 4096, materialsPerPayload = 16, skinsPerPayload = 32;
	std::size_t maxGeometryAssets = 4096, maxTextureAssets = 1024;
	std::size_t maxGeometrySourceBytes = 512 * 1024 * 1024;
	std::size_t maxTextureSourceBytes = 256 * 1024 * 1024;
	std::size_t maxTextureDecodedBytes = 512 * 1024 * 1024;
};

/// Count only after all dependent assets have been admitted successfully.
struct SceneActiveBudget
{
	std::array<std::size_t, 3> counts{};
	bool HasRoom(SceneWindowKind kind) const
	{
		const std::size_t limits[] = {SceneWindowLimits::staticCount, SceneWindowLimits::terrainCount,
			SceneWindowLimits::animatedCount};
		const auto slot = static_cast<std::size_t>(kind);
		return slot < counts.size() && counts[slot] < limits[slot];
	}
	bool Admit(SceneWindowKind kind)
	{
		if (!HasRoom(kind)) return false;
		++counts[static_cast<std::size_t>(kind)];
		return true;
	}
};

/// Traverse only on guard-band transitions. Ranking performs no IO or decoding.
inline std::vector<std::size_t> RankSceneWindow(const std::vector<SceneWindowCandidate>& candidates,
	const SceneWindowRequest& request, SceneWindowMetrics& metrics)
{
	metrics = {};
	std::vector<std::size_t> result;
	if (!request.Valid()) return result;
	const auto outer = request.bounds.Expanded(request.guardBand);
	for (std::size_t i = 0; i < candidates.size(); ++i)
	{
		++metrics.scanned;
		const auto& bounds = candidates[i].bounds;
		if (!bounds.Valid()) { ++metrics.invalidBounds; continue; }
		if (!bounds.Overlaps(outer)) { ++metrics.outsideWindow; continue; }
		if (bounds.Overlaps(request.bounds)) ++metrics.overlapping;
		else ++metrics.guardCandidates;
		result.push_back(i);
	}
	const auto key = [&](std::size_t index) {
		const auto& candidate = candidates[index];
		return std::make_tuple(!candidate.bounds.Overlaps(request.bounds),
			candidate.bounds.DistanceSquared(request.focusX, request.focusY),
			candidate.kind, candidate.objectIndex, candidate.componentIndex, candidate.payloadIndex, index);
	};
	std::sort(result.begin(), result.end(), [&](std::size_t left, std::size_t right) { return key(left) < key(right); });
	return result;
}

/// Selection epoch. Slot identities, not vector sizes, decide whether caches are stale.
class SceneWindowState
{
	bool ready = false;
	SceneWindowRequest previous;
	std::vector<std::size_t> selected;
	std::size_t revision = 0;
public:
	bool NeedsUpdate(const SceneWindowRequest& request) const
	{
		if (!request.Valid()) return false;
		if (!ready) return true;
		const double dx = request.focusX - previous.focusX, dy = request.focusY - previous.focusY;
		const double threshold = previous.guardBand * 0.5;
		return request.guardBand != previous.guardBand
			|| !previous.bounds.Expanded(previous.guardBand).Contains(request.bounds)
			|| dx * dx + dy * dy > threshold * threshold
			|| request.bounds.maxX - request.bounds.minX < (previous.bounds.maxX - previous.bounds.minX) * 0.75
			|| request.bounds.maxY - request.bounds.minY < (previous.bounds.maxY - previous.bounds.minY) * 0.75;
	}
	bool Publish(const SceneWindowRequest& request, std::vector<std::size_t> identities)
	{
		if (!request.Valid()) return false;
		const bool changed = !ready || identities != selected;
		previous = request;
		ready = true;
		selected = std::move(identities);
		if (changed) ++revision;
		return changed;
	}
	const std::vector<std::size_t>& Selected() const { return selected; }
	std::size_t Revision() const { return revision; }
};

enum class SceneAssetKind { Geometry, Texture };
struct SceneAssetCost
{
	SceneAssetKind kind = SceneAssetKind::Geometry;
	std::string key;
	std::size_t sourceBytes = 0, decodedBytes = 0;
};
enum class SceneAssetAdmission { Accepted, InvalidAsset, ChangedAsset, CumulativeLimit };

/** Ledger for original global pools, which cannot evict individual mesh resources.
 * Keep for the process lifetime, including failed loads and map changes. This bounds
 * admitted unique inputs, NOT total renderer memory or unrelated hero/effect assets.
 * Source sizes are measured before loading; texture decoded cost is base-level RGBA.
 * A rejected multi-asset transaction consumes nothing. Reuse consumes no new budget.
 */
class SceneAssetLedger
{
	SceneWindowLimits limits;
	std::map<std::pair<SceneAssetKind, std::string>, SceneAssetCost> entries;
	std::size_t geometryCount = 0, textureCount = 0, geometryBytes = 0, textureBytes = 0, decodedBytes = 0;
public:
	explicit SceneAssetLedger(const SceneWindowLimits& value = {}) : limits(value) {}
	SceneAssetAdmission Admit(const std::vector<SceneAssetCost>& costs)
	{
		decltype(entries) pending;
		std::size_t gc = geometryCount, tc = textureCount, gb = geometryBytes, tb = textureBytes, db = decodedBytes;
		for (const auto& cost : costs)
		{
			if ((cost.kind != SceneAssetKind::Geometry && cost.kind != SceneAssetKind::Texture) ||
				cost.key.empty() || cost.key.size() > SceneWindowLimits::pathBytes + 32 || !cost.sourceBytes
				|| (cost.kind == SceneAssetKind::Texture && !cost.decodedBytes)) return SceneAssetAdmission::InvalidAsset;
			const auto key = std::make_pair(cost.kind, cost.key);
			const auto existing = entries.find(key);
			const auto staged = pending.find(key);
			const auto* previous = existing != entries.end() ? &existing->second
				: staged != pending.end() ? &staged->second : nullptr;
			if (previous)
			{
				if (previous->sourceBytes != cost.sourceBytes || previous->decodedBytes != cost.decodedBytes)
					return SceneAssetAdmission::ChangedAsset;
				continue;
			}
			if (cost.kind == SceneAssetKind::Geometry)
			{
				if (gc >= limits.maxGeometryAssets || cost.sourceBytes > limits.maxGeometrySourceBytes - gb)
					return SceneAssetAdmission::CumulativeLimit;
				++gc; gb += cost.sourceBytes;
			}
			else
			{
				if (tc >= limits.maxTextureAssets || cost.sourceBytes > limits.maxTextureSourceBytes - tb
					|| cost.decodedBytes > limits.maxTextureDecodedBytes - db) return SceneAssetAdmission::CumulativeLimit;
				++tc; tb += cost.sourceBytes; db += cost.decodedBytes;
			}
			pending.emplace(key, cost);
		}
		entries.merge(pending);
		geometryCount = gc; textureCount = tc; geometryBytes = gb; textureBytes = tb; decodedBytes = db;
		return SceneAssetAdmission::Accepted;
	}
	std::size_t GeometryCount() const { return geometryCount; }
	std::size_t TextureCount() const { return textureCount; }
	std::size_t GeometryBytes() const { return geometryBytes; }
	std::size_t TextureBytes() const { return textureBytes; }
	std::size_t TextureDecodedBytes() const { return decodedBytes; }
};
}
