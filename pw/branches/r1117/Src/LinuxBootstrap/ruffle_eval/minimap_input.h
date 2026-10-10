#pragma once

#include "gameplay_events.h"
#include "minimap_frame.h"

#include <cmath>

/**
 * @brief Engine-free destination request, never a native command or target lookup.
 *
 * Move means an explicit ground move, not attack-or-move or automatic selection.
 * Camera means reposition the camera if native permissions/camera lock allow it.
 * NoAction always has zero coordinates; no signal, talent or unit target is inferred.
 */
struct PwRuffleMinimapRequest
{
	/** A right press requests Move; a left press/drag requests Camera. */
	enum class Action { NoAction, Move, Camera };
	Action action = Action::NoAction;
	double worldX = 0; ///< World meters, meaningful only when action is not NoAction.
	double worldY = 0; ///< No height/terrain query is performed by this policy.
};

/**
 * @brief Project finite normalized coordinates inside the authored minimap circle.
 * @return False on invalid coordinates/bounds/extents or outside the circle;
 * both output references remain unchanged on failure. Never clamp to an edge.
 *
 * Reuses minimap_frame.h's bounds, including nonzero origins/rectangular worlds.
 * The authored Minimap.as circle and PwRuffleClipMinimapFrame use center (135,135)
 * and radius 132 on a 270-unit surface. Test that circle BEFORE projection; do
 * not stretch its visible diameter over the full world rectangle. Bounds must
 * have finite coordinates and finite, strictly positive width/height.
 *
 * With invertY=true (required for minimap_frame's north-up image), worldX is
 * minX+x*width and worldY is maxY-y*height. False is only for caller-supplied
 * artwork/markers whose world Y increases down the image. No rotation/offset
 * transform from the native Minimap class is inferred beyond the given bounds.
 *
 * Callback values are mouseX/width, mouseY/height, not pixel indices. Projection
 * is continuous: do not divide them by 269 or round to a texel. The renderer
 * separately rounds normalized marker centers across pixels 0..269. The closed
 * circle is tested against the supplied values, with no outward epsilon padding.
 */
inline bool PwRuffleProjectMinimapInput(double x, double y,
	const PwRuffleMinimapWorldBounds& bounds, double& worldX, double& worldY,
	bool invertY = true) noexcept
{
	if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1 ||
		!std::isfinite(bounds.minX) || !std::isfinite(bounds.minY) ||
		!std::isfinite(bounds.maxX) || !std::isfinite(bounds.maxY))
		return false;
	const double width = bounds.maxX - bounds.minX;
	const double height = bounds.maxY - bounds.minY;
	if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0)
		return false;
	const double dx = x * PwRuffleMinimapFrame::Side - 135;
	const double dy = y * PwRuffleMinimapFrame::Side - 135;
	if (dx * dx + dy * dy > 132 * 132)
		return false;
	worldX = bounds.minX + x * width;
	worldY = invertY ? bounds.maxY - y * height : bounds.minY + y * height;
	return true;
}

/**
 * @brief One minimap's single-button gesture owner; no engine/renderer/JSON linkage.
 *
 * Production MinimapController decodes Over.flag=mouseIn, Down/Up.flag=isLeft;
 * AdventureScreen starts actions on Down and cancels camera drag on ANY Up/Out.
 * This restricted policy only supports ground moves and camera positioning:
 * - A valid right Down emits one Move; right drag and release emit nothing.
 * - A valid left Down emits Camera, then changed valid Move positions emit Camera.
 * - Hover cannot acquire ownership; a Down does not require a preceding Over.
 * - Repeated same-button Down and unchanged Move are ignored without retargeting.
 * - A competing button Down cancels ownership and emits nothing, never switches it.
 * - Any Up (including a different/stale button) or Out cancels, even with bad coords.
 * - Other malformed/outside minimap events or changed bounds/orientation cancel.
 * - Known non-minimap events are ignored; invalid Kind values cancel defensively.
 *
 * Parent integration: keep one instance per UI session; call Consume(event,
 * theSameBoundsAsTheFrame) for ordered decoded callbacks, then dispatch only
 * request.action with request.worldX/worldY through existing native command or
 * camera paths. Native code still validates controls, hero ownership/liveness,
 * camera lock, terrain height and any narrowing to engine float coordinates.
 * No signals, attack targets, object picking or ability targeting are implied.
 *
 * Call Reset on blur, external release, disabled controls, queue loss/clear,
 * decoder failure, world/session replacement, or UI teardown. Those notifications
 * are not encoded in gameplay_events.h. Reset must accompany parent queue clear;
 * this policy cannot detect replayed batches or timestamp stale releases because
 * the wire has no gesture IDs. An Up is therefore always cancellation, never an
 * action. A new Down is required after cancellation; hover cannot resume a drag.
 * Requests/bounds are value copies; no pointers to events or engine state survive.
 */
class PwRuffleMinimapInput
{
public:
	/** @brief Consume one decoded callback; emit at most one action plus world meters. */
	PwRuffleMinimapRequest Consume(const PwRuffleGameplayEvent& event,
		const PwRuffleMinimapWorldBounds& bounds, bool invertY = true) noexcept
	{
		using Kind = PwRuffleGameplayEvent::Kind;
		using Action = PwRuffleMinimapRequest::Action;
		switch (event.kind)
		{
		case Kind::Unsupported:
		case Kind::Informational:
		case Kind::TalentClicked:
			return {};
		case Kind::MinimapMouseUp:
			Reset();
			return {};
		case Kind::MinimapMouseOver:
			if (!event.flag)
			{
				Reset();
				return {};
			}
			break;
		case Kind::MinimapMouseDown:
		case Kind::MinimapActionMove:
			break;
		default:
			Reset();
			return {};
		}

		PwRuffleMinimapRequest request;
		if (!PwRuffleProjectMinimapInput(event.x, event.y, bounds, request.worldX, request.worldY, invertY) ||
			(owner_ != Owner::Unowned && (bounds.minX != bounds_.minX || bounds.minY != bounds_.minY ||
				bounds.maxX != bounds_.maxX || bounds.maxY != bounds_.maxY || invertY != invertY_)))
		{
			Reset();
			return {};
		}
		if (event.kind == Kind::MinimapMouseDown)
		{
			const auto pressed = event.flag ? Owner::Left : Owner::Right;
			if (owner_ != Owner::Unowned)
			{
				if (owner_ != pressed) Reset();
				return {};
			}
			owner_ = pressed;
			bounds_ = bounds;
			invertY_ = invertY;
			request.action = event.flag ? Action::Camera : Action::Move;
		}
		else if (event.kind == Kind::MinimapActionMove && owner_ == Owner::Left &&
			(event.x != lastX_ || event.y != lastY_))
			request.action = Action::Camera;
		else
			return {};
		lastX_ = event.x;
		lastY_ = event.y;
		return request;
	}

	/** @brief Cancel ownership and duplicate suppression; idempotent and emits nothing. */
	void Reset() noexcept
	{
		owner_ = Owner::Unowned;
		lastX_ = lastY_ = 0;
		bounds_ = {};
		invertY_ = true;
	}

private:
	/** Remember even a right press until cancellation so duplicate Downs stay inert. */
	enum class Owner { Unowned, Left, Right };
	Owner owner_ = Owner::Unowned;
	float lastX_ = 0;
	float lastY_ = 0;
	PwRuffleMinimapWorldBounds bounds_;
	bool invertY_ = true;
};
