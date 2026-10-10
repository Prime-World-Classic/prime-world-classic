#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace LinuxBootstrap
{
/** Own per-payload presentation resources until map reload or renderer teardown.
 * Call Clear before replacing payloads, even if the replacement has the same size.
 * Failed loads are cached too; no repeated disk work for an unavailable resource.
 */
template<class Resource>
class SceneResourceCache
{
	struct Entry
	{
		bool attempted = false;
		std::unique_ptr<Resource> resource;
	};
	std::vector<Entry> entries;
public:
	std::size_t builds = 0, hits = 0, failures = 0;
	void Clear() { entries.clear(); }
	std::size_t Size() const { return entries.size(); }
	template<class Factory>
	Resource* Get(std::size_t index, std::size_t count, Factory factory)
	{
		if (index >= count) return nullptr;
		if (entries.size() != count)
		{
			Clear();
			entries.resize(count);
		}
		auto& entry = entries[index];
		if (!entry.attempted)
		{
			entry.resource = factory();
			entry.attempted = true;
			++builds;
			if (!entry.resource) ++failures;
		}
		else ++hits;
		return entry.resource.get();
	}
};
}
