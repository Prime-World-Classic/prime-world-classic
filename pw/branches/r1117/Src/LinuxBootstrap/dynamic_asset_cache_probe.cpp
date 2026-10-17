#include "dynamic_asset_cache.h"
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace
{
/** Mock graph borrowers make destruction-before-invalidation fail immediately. */
struct Asset
{
	static std::set<const Asset*> borrowed;
	static int live;
	static int peak;
	int samples = 0;
	Asset() { peak = std::max(peak, ++live); }
	~Asset()
	{
		if (borrowed.count(this)) std::abort();
		--live;
	}
};
std::set<const Asset*> Asset::borrowed;
int Asset::live = 0;
int Asset::peak = 0;
}

/** Ranked mock scene keys exercise the production working-set policy without GL or DB assets. */
int main()
{
	int checks = 0, failures = 0;
	auto check = [&](bool condition, const char* description) {
		++checks;
		if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", description); }
	};
	std::map<std::string, std::unique_ptr<Asset>> cache;
	std::set<std::string> missing;
	std::set<std::string> nowAvailable;
	std::map<std::string, int> loads;
	int releases = 0;
	auto load = [&](const std::string& key, std::unique_ptr<Asset>& asset) {
		++loads[key];
		asset = std::make_unique<Asset>();
		return key.find("missing") != 0 || nowAvailable.count(key);
	};
	auto release = [&](const std::string&, std::unique_ptr<Asset>& asset) {
		check(asset != nullptr && Asset::live > 0, "borrower invalidation precedes asset destruction");
		Asset::borrowed.erase(asset.get());
		++releases;
	};
	std::vector<std::string> ranked;
	for (int index = 0; index < 32; ++index) ranked.push_back("scene" + std::to_string(index));
	auto update = [&](const std::vector<std::string>& keys, std::size_t capacity = 32) {
		return LinuxBootstrap::UpdateDynamicAssetCache(cache, missing, keys, capacity, load, release);
	};
	const auto cold = update(ranked);
	check(cold.loads == 32 && cache.size() == 32 && Asset::live == 32, "cold working set admits 32 real resources");
	std::map<std::string, Asset*> identities;
	for (auto& item : cache) {
		identities[item.first] = item.second.get();
		Asset::borrowed.insert(item.second.get());
		item.second->samples = 7;
	}
	for (int frame = 0; frame < 100; ++frame) {
		const auto warm = update(ranked);
		check(warm.loads == 0 && warm.evicted == 0 && warm.hits == 32, "warm frames never reload or invalidate retained scenes");
	}
	for (const auto& item : cache)
		check(item.second.get() == identities[item.first] && item.second->samples == 7, "retained cache objects keep identity and state");
	std::reverse(ranked.begin(), ranked.end());
	const auto reordered = update(ranked);
	check(reordered.loads == 0 && reordered.evicted == 0, "camera ranking changes do not rebuild an unchanged working set");
	std::reverse(ranked.begin(), ranked.end());

	// The world collector puts a newly selected scene before the previous distance-ranked set.
	ranked.insert(ranked.begin(), "selectedLate");
	const auto selected = update(ranked);
	check(cache.size() == 32 && cache.count("selectedLate") && !cache.count("scene31"), "late selected scene displaces the lowest-ranked scene");
	check(selected.loads == 1 && selected.evicted == 1 && releases == 1, "selected admission loads and releases exactly one scene");
	check(cache.at("scene0").get() == identities["scene0"] && Asset::borrowed.count(identities["scene0"]), "retained needed assets are not invalidated by policy");
	check(update(ranked).loads == 0 && loads["selectedLate"] == 1, "selected scene is reused on subsequent frames");
	check(update(ranked, 100).loads == 0 && cache.size() == 32, "caller cannot raise the 32-scene hard limit");

	// Failed scenes consume negative-cache entries, not successful mesh slots.
	std::vector<std::string> withMissing;
	for (int index = 0; index < 40; ++index) withMissing.push_back("missing" + std::to_string(index));
	withMissing.insert(withMissing.end(), ranked.begin(), ranked.end());
	const auto failed = update(withMissing);
	check(failed.failures == 40 && missing.size() == 40 && cache.size() == 32 && Asset::live == 32,
		"missing assets do not starve ready scenes or retain failed allocations");
	check(update(withMissing).loads == 0 && update(withMissing).evicted == 0, "visible failures are remembered without re-probing or churn");
	check(loads["missing0"] == 1 && loads["selectedLate"] == 1, "negative and positive entries remain warm together");
	withMissing.insert(withMissing.begin(), "newSelection");
	const auto mixed = update(withMissing);
	check(mixed.loads == 1 && mixed.failures == 0 && cache.count("newSelection") && cache.size() == 32,
		"new selection enters a full cache with visible missing assets");

	std::vector<std::string> small = {"", "newSelection", "newSelection", "scene0", "missing0", "missing0"};
	const auto reduced = update(small);
	check(reduced.loads == 0 && cache.size() == 2 && missing.size() == 1 && Asset::live == 2,
		"duplicates and empty keys consume no slots; offscreen resources and failures are released");
	check(update(small).loads == 0 && loads["missing0"] == 1, "duplicate missing keys never cause retries");
	check(update(small, 1).evicted == 1 && cache.count("newSelection") && cache.size() == 1,
		"smaller budgets retain the first ranked usable scene");
	check(update(small, 0).evicted == 1 && cache.empty() && Asset::live == 0,
		"zero budget releases resources without loading replacements");

	std::vector<std::string> manyMissing;
	for (int index = 0; index < 1000; ++index) manyMissing.push_back("missingBound" + std::to_string(index));
	const auto bounded = update(manyMissing);
	check(bounded.loads == 640 && missing.size() == 640 && Asset::live == 0,
		"negative cache and per-frame distinct input are bounded by the marker limit");
	check(update(manyMissing).loads == 0, "bounded negative cache does not churn on stable input");
	update({});
	check(cache.empty() && missing.empty() && Asset::borrowed.empty() && Asset::live == 0,
		"empty presentation releases all owned state");
	update({"missing0", "returning"});
	check(loads["missing0"] == 2 && cache.count("returning") && missing.size() == 1,
		"a failed key may be retried after leaving the visible working set");
	// Availability changes are observed on re-entry, not by re-probing stable negative entries.
	Asset* retained = cache.at("returning").get();
	nowAvailable.insert("missing0");
	const auto stillVisible = update({"missing0", "returning"});
	check(stillVisible.loads == 0 && loads["missing0"] == 2 && missing.count("missing0") && !cache.count("missing0"),
		"availability changes do not turn a visible negative entry into a per-frame probe");
	const auto leave = update({"returning"});
	check(leave.loads == 0 && missing.empty() && cache.at("returning").get() == retained,
		"leaving the working set forgets the failure without rebuilding the retained scene");
	const auto recovered = update({"missing0", "returning"});
	check(recovered.loads == 1 && recovered.failures == 0 && loads["missing0"] == 3 && missing.empty() &&
		cache.count("missing0") && cache.size() == 2 && cache.at("returning").get() == retained && Asset::live == 2,
		"a formerly missing asset is loaded successfully when it returns after becoming available");
	Asset* recoveredIdentity = cache.at("missing0").get();
	const auto recoveredWarm = update({"missing0", "returning"});
	check(recoveredWarm.loads == 0 && recoveredWarm.hits == 2 && recoveredWarm.evicted == 0 &&
		cache.at("missing0").get() == recoveredIdentity && loads["missing0"] == 3,
		"a recovered asset retains its new identity and stays warm");
	update({});
	check(Asset::peak <= 33, "peak ownership never exceeds 32 cached scenes and one temporary load");
	std::printf("Dynamic asset cache probe: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
