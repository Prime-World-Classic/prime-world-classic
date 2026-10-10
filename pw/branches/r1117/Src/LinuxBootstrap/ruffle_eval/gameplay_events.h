#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

/**
 * @brief Decoded request/notification only; owns no engine objects and executes nothing.
 *
 * TalentClicked uses column(slot), row(level), each 0..5. Talent.as::Press sends
 * the SAME callback for CANBUY and BOUGHT; TalentShortCut.as::Press delegates to
 * it. ActionBarController::UseSlot is native-to-Flash, NOT an outbound callback.
 * The wire cannot identify the action-bar index or distinguish purchase from use.
 * Revalidate the exact GetTalent(row, column) against the controlled live hero:
 * CanActivateTalent for purchase; CanUseTalent and target checks for use. Submit
 * through existing client/transceiver commands, never mutate a hero from here.
 *
 * Minimap coordinates are mouseX/width, mouseY/height, NOT pixels/world meters.
 * Out/drag events can leave [0,1]; preserve finite float-range values without
 * clamping or axis inversion. Flag is mouseIn for Over, isLeft for Down/Up.
 * MinimapController.cpp consumes these as %d %f %f (Move: %f %f), despite stale
 * integer-only comments in FlashFSCommands.h. No pointer/camera state is tracked.
 */
struct PwRuffleGameplayEvent
{
	/** Only these production callback names can carry a decoded request. */
	enum class Kind
	{
		Unsupported,
		Informational, ///< TalentToolTip/TalentActionToolTip; not a gameplay request.
		TalentClicked,
		MinimapMouseOver,
		MinimapMouseDown,
		MinimapMouseUp,
		MinimapActionMove
	};

	Kind kind = Kind::Unsupported;
	std::string command; ///< Exact name for diagnostics, never a dispatch instruction.
	int column = -1; ///< Only TalentClicked; native GetTalent takes row FIRST.
	int row = -1; ///< Only TalentClicked; preserve wire ordering in named fields.
	bool flag = false; ///< Only minimap Over/Down/Up; wire values strictly 0 or 1.
	float x = 0; ///< Only minimap callbacks.
	float y = 0; ///< Only minimap callbacks.
};

/** @brief Resource bounds for one runtime.rs action="events" reply. */
struct PwRuffleGameplayEventLimits
{
	static constexpr std::size_t MaxBatchBytes = 256 * 1024;
	static constexpr std::size_t MaxEvents = 1024; ///< Matches runtime.rs queue cap.
	static constexpr std::size_t MaxCommandBytes = 64;
	static constexpr std::size_t MaxArgumentBytes = 256;
};

/**
 * @brief Decode the host's JSON array of [command, argumentString] pairs in order.
 * @return One record per callback, including duplicates and unsupported names.
 * @throws std::invalid_argument For invalid JSON/UTF-8, nesting, envelope, bounds,
 * or malformed supported arguments. Failure returns no partial batch.
 *
 * Reuses nlohmann JSON; checks byte size before parsing and rejects nested
 * containers beyond the pair arrays while parsing. Names are nonempty ASCII
 * identifiers. Arguments reject NUL/control bytes except ASCII whitespace.
 * Supported arguments have exact arity and fully consumed decimal tokens with
 * ASCII whitespace separators; no coercion, overflow, NaN or infinity accepted.
 * Unsupported arguments stay opaque after envelope/text checks, are discarded,
 * and MUST NOT be forwarded for execution. Informational talent tooltips validate
 * "show column row" (show=0/1) but expose no actionable talent coordinates.
 * No dispatch, gameplay state, asset access, slot mapping, or command creation.
 */
std::vector<PwRuffleGameplayEvent> PwRuffleDecodeGameplayEvents(std::string_view json);
