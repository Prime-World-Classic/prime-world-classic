#ifndef PW_RUFFLE_EVAL_CLIENT_INSPECTION_H
#define PW_RUFFLE_EVAL_CLIENT_INSPECTION_H

#include "native_host.h"
#include "hud_state.h"
#include "action_state.h"
#include "minimap_frame.h"
#include "pointer_capture.h"
#include "gameplay_event_queue.h"
#include <cstddef>

/** Borrowed artwork and owned marker snapshot, consumed synchronously by Draw. */
struct PwRuffleMinimapState
{
	PwRuffleMinimapBackgroundView background;
	PwRuffleMinimapWorldBounds bounds;
	std::vector<PwRuffleMinimapMarker> markers;
	std::optional<int> matchSeconds; ///< Actual simulation clock, not presentation uptime.
};

/** Explicitly opt-in combat SWF inspection, not the live game HUD backend.
 * Live hero data and pointer events are bound; gameplay callbacks are not executed.
 * Failed initialization,
 * rendering, or runtime diagnostics disable this instance and let the caller
 * retain its existing presentation. Reset before destroying the GLX context.
 */
class PwRuffleClientInspection final
{
public:
	/** Initialize once, advance bounded time, and draw the original combat movie. */
	bool Draw(const std::string& library, const std::string& data, unsigned width, unsigned height, double deltaMs,
		const PwRuffleHudState& hud = {}, const PwRuffleActionState& actions = {},
		const PwRuffleMinimapState& minimap = {});
	/** Forward native pointer events; true means the native world must not consume
	 * the same event. Visible HUD pixels and the authored modal shield reserve input.
	 * This does not dispatch gameplay FSCommands, keyboard/text input or IME.
	 */
	bool Pointer(PwRufflePointerCapture::Kind kind, int x, int y, unsigned button,
		double wheelLines, unsigned width, unsigned height);
	/** Release VM buttons on native focus loss; duplicate notifications are skipped. */
	void Focus(bool focused);
	/** Release native resources; diagnostics/counters survive for final logging. */
	bool Reset() { ++inputEpoch_; ++controlEpoch_; minimapBounds_.reset(); events_.Clear(); pointerCapture_.Reset(); focused_.reset(); return host_.Reset(); }
	/** Consume validated requests once. The caller must revalidate live gameplay state. */
	std::vector<PwRuffleGameplayEvent> TakeGameplayEvents() { return events_.Drain(); }
	bool WasAttempted() const { return attempted_; }
	bool IsReady() const { return host_.IsReady(); }
	size_t Frames() const { return frames_; }
	/** Native draw stages, independent of host initialization and UI calls. */
	PwRuffleNativeHost::FrameTiming Timing() const { return host_.Timing(); }
	size_t DiscardedCallbacks() const { return events_.Discarded(); }
	size_t ReceivedCallbacks() const { return events_.Received(); }
	size_t PendingCallbacks() const { return events_.Pending(); }
	/** Reset held gestures after draining prior-frame callbacks when this changes. */
	size_t InputEpoch() const { return inputEpoch_; }
	/** Focus/viewport/session cancellation, excluding pointer leave (selection survives it). */
	size_t ControlEpoch() const { return controlEpoch_; }
	/** Bounds of the last successfully composed native bitmap, not a guessed world extent. */
	const std::optional<PwRuffleMinimapWorldBounds>& MinimapBounds() const { return minimapBounds_; }
	/** Count error flags already pending before any call into the inspection host. */
	size_t PriorGlErrors() const { return priorGlErrors_; }
	/** Successful authored hero calls, excluding unchanged snapshots. */
	size_t HudCalls() const { return hudCalls_; }
	size_t ActionCalls() const { return actionCalls_; }
	size_t MinimapUploads() const { return minimapUploads_; }
	size_t PointerEvents() const { return pointerEvents_; }
	size_t ConsumedPointerEvents() const { return consumedPointerEvents_; }
	const std::string& Error() const { return error_; }
private:
	PwRuffleNativeHost host_;
	bool attempted_ = false;
	size_t frames_ = 0;
	PwRuffleGameplayEventQueue events_;
	size_t inputEpoch_ = 0;
	size_t controlEpoch_ = 0;
	std::optional<PwRuffleMinimapWorldBounds> minimapBounds_;
	size_t priorGlErrors_ = 0;
	std::string error_;
	std::string identity_, values_;
	size_t hudCalls_ = 0;
	std::optional<PwRuffleActionState> actions_;
	size_t actionCalls_ = 0;
	std::string minimapBitmap_;
	std::vector<uint8_t> minimapPixels_;
	size_t minimapUploads_ = 0;
	int matchSeconds_ = -1;
	PwRufflePointerCapture pointerCapture_;
	std::optional<bool> focused_;
	size_t pointerEvents_ = 0, consumedPointerEvents_ = 0;
};

#endif
