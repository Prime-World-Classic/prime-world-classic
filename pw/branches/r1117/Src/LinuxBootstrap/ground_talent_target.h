#pragma once

#include <cmath>
#include <limits>

namespace NWorld
{
class PFBaseMaleHero;
class Target;

/**
 * @brief Validate one exact human-owned ground talent against current Linux state.
 *
 * Implemented in hero_actions_bootstrap.cpp for PW_LINUX_NULL_RENDER. Requires
 * the supplied hero to be the live registered hero of the playing human whose UID
 * equals clientId. Never substitutes another hero, talent, or position. No LOS or
 * alternative-target abilities are supported. Does not execute or enqueue a cast.
 *
 * This is deliberately limited to manual human position casts, not unit targets,
 * scripted/bot behavior, complete command authorization, or Windows state parity.
 * UI/session permissions remain the caller's responsibility. Recheck immediately
 * before submission; CmdUseTalent also rechecks this path before execution.
 */
bool CanUseLinuxGroundTalent(PFBaseMaleHero* hero, int row, int column,
	const Target& target, int clientId);
}

namespace LinuxBootstrap
{
/**
 * @brief Engine-free live facts consumed by the engine wrapper and regression probe.
 *
 * exactHero means matching player->GetHero(), world player lookup by clientId,
 * and the same live world object, not a guessed/nearest/local fallback. usable is
 * talent->CanBeUsed(); inRange must come from hero->IsTargetInRange(target, range),
 * never a replacement distance calculation. No facts survive an engine call.
 */
struct GroundTalentTargetState
{
	int row = -1;
	int column = -1;
	int clientId = -1;
	int ownerClientId = -1;
	bool exactHero = false;
	bool human = false;
	bool playing = false;
	bool alive = false;
	bool controlsAllowed = false;
	bool bought = false;
	bool active = false;
	bool usable = false;
	bool position = false;
	bool land = false;
	bool targetValid = false;
	bool castAllowed = false;
	bool lineOfSight = false; ///< Either SPELLTARGET_LINEOFSIGHT or DB requireLineOfSight.
	bool alternativeTargets = false; ///< DB alternatives or an already attached alternative.
	bool outOfRangeAllowed = false;
	bool inRange = false;
	float x = 0;
	float y = 0;
	float z = 0;
	float width = 0;
	float height = 0;
	float useRange = std::numeric_limits<float>::quiet_NaN();
};

/** Gate before target-dependent formulas; finite map extents use half-open XY bounds. */
inline bool IsLinuxGroundTalentReady(const GroundTalentTargetState& state) noexcept
{
	return state.row >= 0 && state.row < 6 && state.column >= 0 && state.column < 6 &&
		state.clientId >= 0 && state.ownerClientId == state.clientId && state.exactHero &&
		state.human && state.playing && state.alive && state.controlsAllowed &&
		state.bought && state.active && state.usable && state.position && state.land &&
		!state.lineOfSight && !state.alternativeTargets &&
		std::isfinite(state.x) && std::isfinite(state.y) && std::isfinite(state.z) &&
		std::isfinite(state.width) && std::isfinite(state.height) &&
		state.width > 0 && state.height > 0 &&
		state.x >= 0 && state.x < state.width && state.y >= 0 && state.y < state.height;
}

/**
 * @brief Final shared policy: readiness, engine target checks and production range rule.
 * Finite nonpositive range and CANUSEOUTOFRANGE bypass distance, not any other
 * check. Infinite/NaN range is always rejected, including when bypass is enabled.
 */
inline bool CanUseLinuxGroundTalentState(const GroundTalentTargetState& state) noexcept
{
	return IsLinuxGroundTalentReady(state) && state.targetValid && state.castAllowed &&
		std::isfinite(state.useRange) &&
		(state.outOfRangeAllowed || state.useRange <= 0 || state.inRange);
}

/**
 * @brief Restrict the added command checks to nonscript, non-bot position commands.
 * An unresolved hero is NOT a known bot: reject that ambiguous manual-ground path
 * rather than invoking the bootstrap hero fallback. Known bots/scripts retain
 * their previous behavior; this selector is not a general security boundary.
 */
inline bool NeedsLinuxGroundTalentValidation(bool issuedByScript, bool position,
	bool knownBot) noexcept
{
	return !issuedByScript && position && !knownBot;
}
}
