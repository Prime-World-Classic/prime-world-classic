#ifndef PW_RUFFLE_EVAL_CLIENT_INSPECTION_H
#define PW_RUFFLE_EVAL_CLIENT_INSPECTION_H

#include "native_host.h"
#include "hud_state.h"
#include <cstddef>

/** Explicitly opt-in combat SWF inspection, not the live game HUD backend.
 * Live hero data is displayed; game inputs and gameplay callbacks are not routed.
 * Failed initialization,
 * rendering, or runtime diagnostics disable this instance and let the caller
 * retain its existing presentation. Reset before destroying the GLX context.
 */
class PwRuffleClientInspection final
{
public:
	/** Initialize once, advance bounded time, and draw the original combat movie. */
	bool Draw(const std::string& library, const std::string& data, unsigned width, unsigned height, double deltaMs,
		const PwRuffleHudState& hud = {});
	/** Release native resources; diagnostics/counters survive for final logging. */
	bool Reset() { return host_.Reset(); }
	bool WasAttempted() const { return attempted_; }
	bool IsReady() const { return host_.IsReady(); }
	size_t Frames() const { return frames_; }
	size_t DiscardedCallbacks() const { return callbacks_; }
	/** Count error flags already pending before any call into the inspection host. */
	size_t PriorGlErrors() const { return priorGlErrors_; }
	/** Successful authored hero calls, excluding unchanged snapshots. */
	size_t HudCalls() const { return hudCalls_; }
	const std::string& Error() const { return error_; }
private:
	PwRuffleNativeHost host_;
	bool attempted_ = false;
	size_t frames_ = 0, callbacks_ = 0;
	size_t priorGlErrors_ = 0;
	std::string error_;
	std::string identity_, values_;
	size_t hudCalls_ = 0;
};

#endif
