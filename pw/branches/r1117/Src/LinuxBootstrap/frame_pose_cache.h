#pragma once

#include <map>
#include <vector>

namespace LinuxBootstrap
{
/** Frame-local CPU poses. Assets must stay immutable/alive until this cache dies.
 * Sample time is part of identity; placement, tint and texture state are not.
 * A new instance per frame prevents stale poses after asset replacement or rewind.
 */
template<class Vertex>
class FramePoseCache
{
	struct Entry
	{
		bool ready = false;
		double time = 0;
		std::vector<Vertex> vertices;
	};
	std::map<const void*, Entry> entries;
public:
	std::size_t builds = 0, hits = 0;
	template<class Factory>
	const std::vector<Vertex>& Get(const void* asset, double time, Factory factory)
	{
		auto& entry = entries[asset];
		if (!entry.ready || entry.time != time)
		{
			entry.vertices = factory();
			entry.time = time;
			entry.ready = true;
			++builds;
		}
		else ++hits;
		return entry.vertices;
	}
};
}
