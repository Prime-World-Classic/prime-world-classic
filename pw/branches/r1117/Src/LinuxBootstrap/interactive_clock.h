#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace LinuxBootstrap
{
/** Fixed 100 ms local-world budget, independent of render rate and --seconds.
 * The caller supplies monotonic seconds and runs the existing scheduler/transceiver.
 * Keep at most 500 ms of debt and execute at most three steps per call. Long stalls
 * deliberately discard excess wall time instead of creating an unbounded catch-up.
 * Loading resets the epoch; focus changes do not pause the offline world.
 */
class InteractiveClock
{
	bool initialized = false;
	double previous = 0, pending = 0;
public:
	static constexpr double StepSeconds = 0.1;
	std::size_t ticks = 0, pumps = 0;
	double discardedSeconds = 0;
	void Reset() { initialized = false; pending = 0; }
	double PendingSeconds() const { return pending; }
	std::size_t Advance(double now, bool ready)
	{
		if (!ready) { Reset(); return 0; }
		if (!std::isfinite(now)) return 0;
		if (!initialized) { initialized = true; previous = now; return 0; }
		if (now <= previous) return 0;
		const double delta = now - previous;
		previous = now;
		++pumps;
		const double debt = pending + delta;
		pending = std::min(0.5, debt);
		discardedSeconds += debt - pending;
		const auto count = std::min<std::size_t>(3, static_cast<std::size_t>((pending + 1e-9) / StepSeconds));
		pending = std::max(0.0, pending - count * StepSeconds);
		ticks += count;
		return count;
	}
};
}
