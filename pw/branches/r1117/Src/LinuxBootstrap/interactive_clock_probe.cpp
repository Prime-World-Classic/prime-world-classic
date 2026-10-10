#include "interactive_clock.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace
{
unsigned checks = 0;
void Check(bool ok, const char* label)
{
	++checks;
	if (!ok) { std::fprintf(stderr, "Interactive clock FAIL: %s\n", label); std::exit(1); }
}
}

/** Synthetic frame schedules expose acceleration, the old 650-step stall and excess debt. */
int main()
{
	using LinuxBootstrap::InteractiveClock;
	for (int fps : {5, 10, 14, 30, 60, 144, 240})
	{
		InteractiveClock clock;
		Check(clock.Advance(0, true) == 0, "initial epoch");
		for (int frame = 1; frame <= fps * 180; ++frame)
			Check(clock.Advance(static_cast<double>(frame) / fps, true) <= 3, "bounded frame work");
		Check(clock.ticks == 1800, "three minutes advance at wall rate, beyond old cap");
		Check(clock.discardedSeconds == 0, "normal frame schedule loses no time");
	}
	InteractiveClock clock;
	clock.Advance(0, true);
	Check(clock.Advance(0.09, true) == 0, "substep");
	Check(clock.Advance(0.1, true) == 1, "fractional accumulation");
	Check(clock.Advance(0.1, true) == 0, "duplicate call");
	Check(clock.Advance(0.05, true) == 0 && clock.Advance(0.2, true) == 1, "backwards time");
	Check(clock.Advance(std::numeric_limits<double>::quiet_NaN(), true) == 0, "NaN");
	Check(clock.Advance(std::numeric_limits<double>::infinity(), true) == 0, "infinity");
	Check(clock.Advance(10.2, true) == 3, "stall bounds");
	Check(std::abs(clock.discardedSeconds - 9.5) < 1e-8, "discarded debt reported");
	Check(clock.Advance(10.2, true) == 0, "duplicate does not drain catch-up");
	Check(clock.Advance(10.21, true) == 2, "retained debt drains next frame");
	Check(clock.PendingSeconds() < 0.1, "bounded remainder");
	Check(clock.Advance(40, false) == 0 && clock.PendingSeconds() == 0, "loading resets debt");
	Check(clock.Advance(50, true) == 0 && clock.Advance(50.1, true) == 1, "loading duration excluded");
	clock.Reset();
	Check(clock.Advance(60, true) == 0, "new world epoch");
	std::printf("Interactive clock: %u checks passed\n", checks);
}
