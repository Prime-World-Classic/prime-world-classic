#include "talent_input.h"
#include <cstdio>

/** Exhaustive mock live-state combinations, including contradictory snapshots. */
int main()
{
	using Request = PwRuffleTalentRequest;
	PwRuffleGameplayEvent event;
	event.kind = PwRuffleGameplayEvent::Kind::TalentClicked;
	event.row = 2;
	event.column = 4;
	int checks = 0, failures = 0;
	const auto check = [&](bool result) { ++checks; if (!result) ++failures; };
	for (unsigned mask = 0; mask < 2048; ++mask)
	{
		PwRuffleTalentInputState state;
		bool* flags[] = {&state.controlsAllowed, &state.exists, &state.bought, &state.canBuy,
			&state.canUse, &state.active, &state.alive, &state.on, &state.usesAttackTarget,
			&state.hasAttackTarget, &state.targetless};
		for (unsigned bit = 0; bit < 11; ++bit) *flags[bit] = (mask & (1u << bit)) != 0;
		const auto request = PwRuffleResolveTalentInput(event, state);
		if (!state.controlsAllowed || !state.exists) check(request == Request::Reject);
		else if (!state.bought) check(request == (state.canBuy ? Request::Buy : Request::Reject));
		else if (!state.alive || !state.active || !state.canUse) check(request == Request::Reject);
		else if (state.usesAttackTarget) check(request == (state.hasAttackTarget ? Request::AttackTarget : Request::Reject));
		else check(request == ((state.on || state.targetless) ? Request::Self : Request::NeedsTarget));
	}
	PwRuffleTalentInputState buy;
	buy.controlsAllowed = buy.exists = buy.canBuy = true;
	for (int row = -1; row <= 6; ++row)
	for (int column = -1; column <= 6; ++column)
	{
		event.row = row; event.column = column;
		check(PwRuffleResolveTalentInput(event, buy) ==
			(row >= 0 && row < 6 && column >= 0 && column < 6 ? Request::Buy : Request::Reject));
	}
	event.row = event.column = 0;
	event.kind = PwRuffleGameplayEvent::Kind::MinimapMouseDown;
	check(PwRuffleResolveTalentInput(event, buy) == Request::Reject);
	std::printf("Talent input: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
