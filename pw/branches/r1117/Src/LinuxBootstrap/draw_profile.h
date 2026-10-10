#pragma once

#include <array>
#include <chrono>
#include <cstddef>

namespace LinuxBootstrap
{
/** CPU submission/wait timings, not asynchronous GPU execution times. */
struct DrawProfile
{
	enum Stage { Setup, Surface, Terrain, Static, Animated, Debug, Units, Overlays, Swap, Count };
	struct Sample
	{
		std::size_t calls = 0;
		double totalMs = 0;
		double peakMs = 0;
	};
	std::array<Sample, Count> samples{};
	static const char* Name(Stage stage)
	{
		static const char* names[] = { "setup", "surface", "terrain", "static", "animated", "debug", "units", "overlays", "swap" };
		return names[stage];
	}
};

/** Consecutive marks partition a frame; a null sink permits non-runtime previews. */
template<class Clock = std::chrono::steady_clock>
class DrawProfileLaps
{
	DrawProfile* profile;
	typename Clock::time_point previous;
public:
	explicit DrawProfileLaps(DrawProfile* sink) : profile(sink), previous(Clock::now()) {}
	void Mark(DrawProfile::Stage stage)
	{
		const auto now = Clock::now();
		if (profile)
		{
			auto& sample = profile->samples[stage];
			const double ms = std::chrono::duration<double, std::milli>(now - previous).count();
			++sample.calls;
			sample.totalMs += ms;
			if (ms > sample.peakMs) sample.peakMs = ms;
		}
		previous = now;
	}
};
}
