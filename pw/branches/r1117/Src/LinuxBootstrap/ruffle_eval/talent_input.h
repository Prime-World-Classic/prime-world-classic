#pragma once

#include "gameplay_events.h"

/** Live engine facts for one exact talent. Never cached across world steps. */
struct PwRuffleTalentInputState
{
	bool controlsAllowed = false;
	bool exists = false;
	bool bought = false;
	bool canBuy = false;
	bool canUse = false;
	bool active = false;
	bool alive = false;
	bool on = false;
	bool usesAttackTarget = false;
	bool hasAttackTarget = false;
	bool targetless = false;
};

/** Target selection remains explicit: never guess an enemy or substitute a slot. */
enum class PwRuffleTalentRequest { Reject, Buy, Self, AttackTarget, NeedsTarget };

/** Match the immediate branches of AdventureScreen::OnTalentSetButtonClick.
 * NeedsTarget is inert until native two-step targeting is implemented. Target
 * validity/cast limitations must still be checked by the caller and world command.
 */
inline PwRuffleTalentRequest PwRuffleResolveTalentInput(const PwRuffleGameplayEvent& event,
	const PwRuffleTalentInputState& state)
{
	using Request = PwRuffleTalentRequest;
	if (event.kind != PwRuffleGameplayEvent::Kind::TalentClicked ||
		event.row < 0 || event.row >= 6 || event.column < 0 || event.column >= 6 ||
		!state.controlsAllowed || !state.exists) return Request::Reject;
	if (!state.bought) return state.canBuy ? Request::Buy : Request::Reject;
	if (!state.alive || !state.active || !state.canUse) return Request::Reject;
	if (state.usesAttackTarget)
		return state.hasAttackTarget ? Request::AttackTarget : Request::Reject;
	if (state.on || state.targetless) return Request::Self;
	return Request::NeedsTarget;
}
