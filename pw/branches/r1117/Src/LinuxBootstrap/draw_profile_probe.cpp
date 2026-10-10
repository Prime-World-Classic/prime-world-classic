#include "draw_profile.h"
#include <cassert>
#include <cstring>

/** Deterministic clock keeps timing partition tests independent of host load. */
struct MockClock
{
	using time_point = std::chrono::steady_clock::time_point;
	static time_point value;
	static time_point now() { return value; }
	static void Advance(int ms) { value += std::chrono::milliseconds(ms); }
};
MockClock::time_point MockClock::value{};

int main()
{
	using namespace LinuxBootstrap;
	DrawProfile profile;
	DrawProfileLaps<MockClock> laps(&profile);
	MockClock::Advance(7);
	laps.Mark(DrawProfile::Static);
	MockClock::Advance(3);
	laps.Mark(DrawProfile::Units);
	MockClock::Advance(2);
	laps.Mark(DrawProfile::Static);
	laps.Mark(DrawProfile::Swap);
	assert(profile.samples[DrawProfile::Static].calls == 2);
	assert(profile.samples[DrawProfile::Static].totalMs == 9);
	assert(profile.samples[DrawProfile::Static].peakMs == 7);
	assert(profile.samples[DrawProfile::Units].totalMs == 3);
	assert(profile.samples[DrawProfile::Swap].calls == 1);
	assert(profile.samples[DrawProfile::Swap].totalMs == 0);
	assert(profile.samples[DrawProfile::Terrain].calls == 0);
	assert(std::strcmp(DrawProfile::Name(DrawProfile::Static), "static") == 0);
	DrawProfileLaps<MockClock> absent(nullptr);
	absent.Mark(DrawProfile::Setup);
	DrawProfileLaps<MockClock> next(&profile);
	MockClock::Advance(11);
	next.Mark(DrawProfile::Static);
	assert(profile.samples[DrawProfile::Static].totalMs == 20);
	assert(profile.samples[DrawProfile::Static].peakMs == 11);
}
