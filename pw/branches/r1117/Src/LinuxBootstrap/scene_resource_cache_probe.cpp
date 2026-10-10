#include "scene_resource_cache.h"
#include <cassert>
#include <stdexcept>

/** Mock renderer resource checks ownership without requiring a graphics context. */
struct Resource
{
	static int live;
	int samples = 0;
	Resource() { ++live; }
	~Resource() { --live; }
};
int Resource::live = 0;

int main()
{
	using LinuxBootstrap::SceneResourceCache;
	int loads = 0;
	auto factory = [&]() { ++loads; return std::make_unique<Resource>(); };
	{
		SceneResourceCache<Resource> cache;
		assert(!cache.Get(1, 1, factory) && loads == 0);
		for (int frame = 0; frame < 1000; ++frame)
			++cache.Get(0, 2, factory)->samples;
		assert(loads == 1 && cache.Get(0, 2, factory)->samples == 1000);
		assert(cache.builds == 1 && cache.hits == 1000 && Resource::live == 1);
		int failedLoads = 0;
		auto missing = [&]() { ++failedLoads; return std::unique_ptr<Resource>(); };
		for (int frame = 0; frame < 100; ++frame) assert(!cache.Get(1, 2, missing));
		assert(failedLoads == 1 && cache.failures == 1);
		cache.Clear(); // Same-sized map replacement must not reuse either entry.
		assert(Resource::live == 0 && cache.Size() == 0);
		assert(cache.Get(0, 2, factory)->samples == 0 && loads == 2);
		assert(cache.Get(1, 2, factory) && loads == 3);
		assert(Resource::live == 2);
		assert(cache.Get(0, 1, factory) && loads == 4 && Resource::live == 1);
		cache.Clear();
		try { cache.Get(0, 1, []() -> std::unique_ptr<Resource> { throw std::runtime_error("load"); }); }
		catch (const std::runtime_error&) {}
		assert(cache.Get(0, 1, factory) && loads == 5);
	}
	assert(Resource::live == 0);
}
