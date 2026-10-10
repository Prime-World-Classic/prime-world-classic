#include "frame_pose_cache.h"
#include <cassert>
#include <stdexcept>

/** Mock assets test same-frame sharing, time changes and fresh-frame ownership. */
int main()
{
	int first = 1, second = 2, samples = 0;
	for (int frame = 0; frame < 100; ++frame)
	{
		LinuxBootstrap::FramePoseCache<int> cache;
		auto sample = [&]() { ++samples; return std::vector<int>{first, frame}; };
		for (int replica = 0; replica < 48; ++replica)
			assert(cache.Get(&first, frame, sample) == (std::vector<int>{first, frame}));
		assert(cache.builds == 1 && cache.hits == 47 && samples == frame * 3 + 1);
		assert(cache.Get(&second, frame, sample).size() == 2 && cache.builds == 2);
		assert(cache.Get(&first, frame + 0.5, sample).size() == 2 && cache.builds == 3);
	}
	LinuxBootstrap::FramePoseCache<int> cache;
	try { cache.Get(&first, 0, []() -> std::vector<int> { throw std::runtime_error("sample"); }); }
	catch (const std::runtime_error&) {}
	assert(cache.builds == 0);
	assert(cache.Get(&first, 0, []() { return std::vector<int>{7}; })[0] == 7);
	assert(cache.Get(&first, -1, []() { return std::vector<int>{8}; })[0] == 8);
	assert(cache.Get(&second, 0, []() { return std::vector<int>{}; }).empty());
	assert(cache.Get(&second, 0, []() { return std::vector<int>{9}; }).empty());
}
