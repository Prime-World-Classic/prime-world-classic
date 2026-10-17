#pragma once

#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace LinuxBootstrap
{
struct DynamicAssetCacheUpdate
{
	std::size_t loads = 0, hits = 0, failures = 0, evicted = 0;
};

/** Maintain at most 32 usable dynamic scene assets in caller-supplied presentation order.
 * The caller provides observer/selection/camera-ranked keys, already visibility-filtered.
 * Retained map entries are never replaced, so mesh identities and warm resources remain stable.
 * Missing assets use a separate, current-input-only negative cache of at most 640 keys;
 * failures therefore cannot consume mesh slots or trigger repeated probes on stable frames.
 * Run between frames. BeforeErase MUST release all borrowers (graphs, deferred draws, etc.)
 * before an entry is destroyed. Shared texture owners must be pruned separately after the update.
 * Load receives a temporary value; failed values are immediately destroyed. Peak ownership is
 * the bounded cache plus one temporary load. A failed load never displaces a usable entry.
 */
template<class Asset, class Load, class BeforeErase>
DynamicAssetCacheUpdate UpdateDynamicAssetCache(
	std::map<std::string, Asset>& cache,
	std::set<std::string>& missing,
	const std::vector<std::string>& rankedKeys,
	std::size_t capacity,
	Load load,
	BeforeErase beforeErase)
{
	DynamicAssetCacheUpdate result;
	capacity = std::min<std::size_t>(capacity, 32);
	std::vector<std::string> order;
	std::map<std::string, std::size_t> ranks;
	for (const auto& key : rankedKeys) {
		if (!key.empty() && ranks.emplace(key, order.size()).second) order.push_back(key);
		if (order.size() == 640) break;
	}
	for (auto it = missing.begin(); it != missing.end();) {
		if (!ranks.count(*it) || cache.count(*it)) it = missing.erase(it);
		else ++it;
	}
	std::set<std::string> retained;
	auto erase = [&](typename std::map<std::string, Asset>::iterator it) {
		beforeErase(it->first, it->second);
		cache.erase(it);
		++result.evicted;
	};
	for (const auto& key : order) {
		if (retained.size() == capacity) break;
		if (missing.count(key)) continue;
		if (cache.count(key)) ++result.hits;
		else {
			Asset asset{};
			++result.loads;
			if (!load(key, asset)) {
				missing.insert(key);
				++result.failures;
				continue;
			}
			// Evict the lowest-ranked unretained entry, not a later-needed warm entry at random.
			while (cache.size() >= capacity) {
				auto victim = cache.end();
				std::size_t worst = 0;
				for (auto it = cache.begin(); it != cache.end(); ++it) {
					if (retained.count(it->first)) continue;
					const auto rank = ranks.find(it->first);
					const std::size_t value = rank == ranks.end() ? order.size() : rank->second;
					if (victim == cache.end() || value > worst) { victim = it; worst = value; }
				}
				erase(victim);
			}
			cache.emplace(key, std::move(asset));
		}
		retained.insert(key);
	}
	for (auto it = cache.begin(); it != cache.end();) {
		if (!retained.count(it->first)) { auto victim = it++; erase(victim); }
		else ++it;
	}
	return result;
}
}
