// Model the global X11 macro before including the public header.
#define None 0L
#include "talent_target_input.h"
#undef None

#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>

namespace
{
using Input = PwRuffleTalentTargetInput;
using Key = Input::Key;
using Context = Input::Context;
using Kind = Input::Kind;
using Event = Input::Event;
using Request = Input::Request;

constexpr Key first{7, 2, 4, 11};
constexpr Key second{7, 5, 1, 11};
constexpr Context live{7, 11, true};
std::size_t checks = 0;

/** Stop at the first policy mismatch; sanitizer failures remain independently fatal. */
void Check(bool condition, const char* label)
{
	++checks;
	if (condition) return;
	std::fprintf(stderr, "Talent target input failed: %s (check %zu)\n", label, checks);
	std::exit(1);
}

/** Check all slot boundaries and every permission/identity/epoch combination. */
void ArmMatrix()
{
	for (int hero : {-1, 0, 7})
	for (int row = -1; row <= 6; ++row)
	for (int column = -1; column <= 6; ++column)
	for (unsigned flags = 0; flags < 8; ++flags)
	{
		Input input;
		Check(input.Arm(first, live), "baseline arm");
		const Key key{hero, row, column, 11};
		const Context context{flags & 1 ? hero : hero + 1, flags & 2 ? 11u : 12u,
			(flags & 4) != 0};
		const bool valid = hero >= 0 && row >= 0 && row < 6 && column >= 0 && column < 6 && flags == 7;
		Check(input.Arm(key, context) == valid, "arm authorization and authored bounds");
		Check(input.Pending().has_value() == valid, "invalid arm clears old selection");
		Check(input.IsChosen(key) == valid, "Chosen uses the exact key");
		if (!valid) continue;
		for (const Key& other : {Key{hero + 1, row, column, 11}, Key{hero, row + 1, column, 11},
			Key{hero, row, column + 1, 11}, Key{hero, row, column, 12}})
			Check(!input.IsChosen(other), "Chosen never aliases another hero/cell/epoch");
		Check(input.Revalidate(context), "valid frame preserves pending");
	}
}

/** Directly specify handoff regressions, independent of the exhaustive model below. */
void GestureRegressions()
{
	for (bool releaseWorld : {false, true})
	for (bool submitted : {false, true})
	{
		Input input;
		input.Arm(first, live);
		auto down = input.Consume({Kind::LeftDown, true}, live);
		Check(down.consume && down.request && !down.canceled, "fresh world Down requests target");
		Check(input.Complete(*down.request, submitted), "completion matches issued request");
		Check(input.IsChosen(first) == !submitted, "failure retains and success clears Chosen");
		Check(!input.Complete(*down.request, !submitted), "completion is exactly once");
		auto repeat = input.Consume({Kind::LeftDown, false}, live);
		Check(repeat.consume && !repeat.request, "held Down cannot repeat after completion");
		Check(input.Consume({Kind::Move, false}, live).consume, "completed gesture retains Move ownership");
		auto up = input.Consume({Kind::LeftUp, releaseWorld}, live);
		Check(up.consume && !up.request, "release consumed even after successful submission");
		Check(!input.Consume({Kind::Move, true}, live).consume, "released gesture no longer captures Move");
		auto retry = input.Consume({Kind::LeftDown, true}, live);
		Check(retry.request.has_value() == !submitted, "failed target permits fresh retry only");
		if (retry.request) Check(retry.request->token > down.request->token, "retry gets a fresh token");
	}

	for (bool initiallyPending : {false, true})
	for (bool pressWorld : {false, true})
	{
		Input input;
		if (initiallyPending) input.Arm(first, live);
		auto before = input.Consume({Kind::LeftDown, pressWorld}, live);
		input.Arm(second, live);
		auto held = input.Consume({Kind::LeftDown, true}, live);
		Check(!held.request, "arming/replacing cannot acquire a held world/HUD press");
		if (before.request) Check(!input.Complete(*before.request, true), "replacement rejects old completion");
		auto up = input.Consume({Kind::LeftUp, true}, live);
		Check(up.consume && !up.request && input.IsChosen(second), "held HUD release cannot cast replacement");
		Check(input.Consume({Kind::LeftDown, true}, live).request.has_value(), "replacement requires fresh world Down");
	}

	for (Kind cancel : {Kind::RightDown, Kind::EscapeDown})
	for (bool world : {false, true})
	for (bool leftHeld : {false, true})
	{
		Input input;
		input.Arm(first, live);
		std::optional<Request> attempt;
		if (leftHeld) attempt = input.Consume({Kind::LeftDown, true}, live).request;
		auto canceled = input.Consume({cancel, world}, live);
		Check(canceled.consume && canceled.canceled && !canceled.request && !input.Pending(), "cancel consumes and clears");
		if (attempt) Check(!input.Complete(*attempt, true), "cancel invalidates outstanding completion");
		input.Arm(second, live);
		auto repeat = input.Consume({cancel, !world}, live);
		Check(repeat.consume && !repeat.canceled && input.IsChosen(second), "cancel repeat cannot cancel replacement");
		const auto left = input.Consume({Kind::LeftDown, true}, live);
		Check(left.consume && !left.request, "held cancel button blocks target requests");
		const auto cancelUp = input.Consume({cancel == Kind::RightDown ? Kind::RightUp : Kind::EscapeUp, !world}, live);
		Check(cancelUp.consume && !cancelUp.request, "cancel release is consumed after rearm");
		Check(input.Consume({Kind::LeftUp, world}, live).consume, "left tail consumed after cancellation");
		Check(input.Consume({Kind::LeftDown, true}, live).request.has_value(), "fresh press works after cancel release");
	}

	Input input;
	input.Arm(first, live);
	for (Kind kind : {Kind::LeftUp, Kind::RightUp, Kind::EscapeUp})
	{
		const auto orphan = input.Consume({kind, true}, live);
		Check(orphan.consume && !orphan.request && !orphan.canceled && input.IsChosen(first), "orphan Up cannot cast or cancel");
	}
}

/** Exercise delayed/mutated completions and no-input lifecycle transitions. */
void CompletionRegressions()
{
	for (unsigned flags = 0; flags < 8; ++flags)
	{
		Input input;
		input.Arm(first, live);
		const Request old = *input.Consume({Kind::LeftDown, true}, live).request;
		const Context context{flags & 1 ? 7 : 8, flags & 2 ? 11u : 12u, (flags & 4) != 0};
		Check(input.Revalidate(context) == (flags == 7), "frame lifecycle authorization matrix");
		if (flags != 7)
		{
			Check(!input.Complete(old, true) && !input.Pending(), "invalidation rejects delayed completion");
			Check(input.Consume({Kind::LeftUp, false}, context).consume, "denied frame still drains captured release");
		}
	}

	Input input;
	input.Arm(first, live);
	const Request old = *input.Consume({Kind::LeftDown, true}, live).request;
	input.Consume({Kind::LeftUp, true}, live);
	Check(!input.Consume({Kind::LeftDown, true}, live).request, "uncompleted request prevents another attempt");
	input.Consume({Kind::LeftUp, true}, live);
	input.Arm(first, live);
	const Request fresh = *input.Consume({Kind::LeftDown, true}, live).request;
	Check(fresh.token > old.token && !input.Complete(old, true), "same-key ABA replacement rejects old token");
	for (int field = 0; field < 5; ++field)
	{
		Request altered = fresh;
		if (field == 0) ++altered.key.heroObjectId;
		if (field == 1) ++altered.key.row;
		if (field == 2) ++altered.key.column;
		if (field == 3) ++altered.key.epoch;
		if (field == 4) ++altered.token;
		Check(!input.Complete(altered, true) && input.IsChosen(first), "altered completion cannot clear pending");
	}
	Check(!input.Complete(Request{}, true), "default token cannot complete");
	Check(input.Complete(fresh, false), "bad completions did not discard genuine request");
	input.Invalidate();
	input.Invalidate();
	Check(input.Consume({Kind::LeftUp, false}, live).consume && !input.Pending(), "idempotent reset preserves release drain");
}

/** Permission loss on the current press must not turn cancellation into world input. */
void PermissionEdgeRegressions()
{
	for (unsigned flags = 0; flags < 7; ++flags)
	for (Kind kind : {Kind::LeftDown, Kind::RightDown, Kind::EscapeDown})
	for (bool world : {false, true})
	{
		Input input;
		input.Arm(first, live);
		const Context denied{flags & 1 ? 7 : 8, flags & 2 ? 11u : 12u, (flags & 4) != 0};
		const bool capture = kind != Kind::LeftDown || world;
		const auto canceled = input.Consume({kind, world}, denied);
		Check(canceled.canceled && !canceled.request && !input.Pending(), "press revalidates permission before requesting");
		Check(canceled.consume == capture, "permission cancellation press cannot leak to world");
		const Kind up = kind == Kind::LeftDown ? Kind::LeftUp : kind == Kind::RightDown ? Kind::RightUp : Kind::EscapeUp;
		const auto release = input.Consume({up, !world}, denied);
		Check(release.consume == capture && !release.request, "permission cancellation also drains release");
	}
	Input input;
	input.Arm(first, live);
	Check(input.Arm(*input.Pending(), live) && input.IsChosen(first), "Arm accepts its own pending key by value");
}

/**
 * Finite reference model, encoded independently as a press ownership table:
 * 0=released, 1=external, 2=consumed. A queued attempt has no coordinates/engine.
 * Serial numbers are deliberately excluded from its reachable-state identity;
 * token monotonicity and stale-token behavior have separate regression coverage.
 */
struct Model
{
	int selected = 0; ///< 0=unarmed, 1=first, 2=second.
	std::array<int, 3> buttons{};
	bool waiting = false;

	unsigned Identity() const
	{
		unsigned value = static_cast<unsigned>(selected);
		for (int button : buttons) value = value * 3 + static_cast<unsigned>(button);
		return value * 2 + (waiting ? 1u : 0u);
	}
	void Clear() { selected = 0; waiting = false; }
};

struct Branch
{
	Input input;
	Model model;
	std::optional<Request> issued;
	std::uint64_t lastToken = 0;
};

/** Compare externally visible selection, including the complete Chosen identity. */
void CompareSelection(const Branch& branch)
{
	Check(branch.input.Pending().has_value() == (branch.model.selected != 0), "model pending presence");
	Check(branch.input.IsChosen(first) == (branch.model.selected == 1), "model first Chosen");
	Check(branch.input.IsChosen(second) == (branch.model.selected == 2), "model second Chosen");
	if (branch.model.selected)
		Check(*branch.input.Pending() == (branch.model.selected == 1 ? first : second), "model exact pending key");
}

/** Exhaust every public transition from every reachable abstract ownership state. */
void ExhaustiveTransitions()
{
	std::vector<Branch> queue(1);
	std::set<unsigned> visited{queue.front().model.Identity()};
	std::size_t transitions = 0;
	const auto enqueue = [&](const Branch& branch)
	{
		CompareSelection(branch);
		++transitions;
		if (visited.insert(branch.model.Identity()).second) queue.push_back(branch);
	};

	for (std::size_t index = 0; index < queue.size(); ++index)
	{
		const Branch base = queue[index];
		for (int slot : {1, 2})
		for (unsigned flags = 0; flags < 8; ++flags)
		{
			Branch branch = base;
			const Context context{flags & 1 ? 7 : 8, flags & 2 ? 11u : 12u, (flags & 4) != 0};
			const bool valid = flags == 7;
			Check(branch.input.Arm(slot == 1 ? first : second, context) == valid, "model replace authorization");
			branch.model.selected = valid ? slot : 0;
			branch.model.waiting = false;
			enqueue(branch);
		}
		for (unsigned flags = 0; flags < 8; ++flags)
		{
			Branch branch = base;
			const Context context{flags & 1 ? 7 : 8, flags & 2 ? 11u : 12u, (flags & 4) != 0};
			if (flags != 7) branch.model.Clear();
			Check(branch.input.Revalidate(context) == (branch.model.selected != 0), "model idle-frame permissions");
			enqueue(branch);
		}
		{
			Branch branch = base;
			branch.input.Invalidate();
			branch.model.Clear();
			enqueue(branch);
		}
		for (bool submitted : {false, true})
		{
			Branch branch = base;
			const bool expected = branch.model.waiting;
			Check(branch.input.Complete(branch.issued.value_or(Request{}), submitted) == expected, "model completion outcome");
			if (expected)
			{
				branch.model.waiting = false;
				if (submitted) branch.model.selected = 0;
			}
			enqueue(branch);
		}

		// The eighth kind is deliberately malformed; its only safe action is cancellation.
		for (Kind kind : {Kind::LeftDown, Kind::LeftUp, Kind::RightDown, Kind::RightUp,
			Kind::EscapeDown, Kind::EscapeUp, Kind::Move, static_cast<Kind>(99)})
		for (bool world : {false, true})
		for (unsigned flags = 0; flags < 8; ++flags)
		{
			Branch branch = base;
			auto& model = branch.model;
			const Context context{flags & 1 ? 7 : 8, flags & 2 ? 11u : 12u, (flags & 4) != 0};
			bool consume = false, request = false;
			const bool selectedAtEntry = model.selected != 0;
			bool canceled = selectedAtEntry && flags != 7;
			if (flags != 7) model.Clear();
			const bool down = kind == Kind::LeftDown || kind == Kind::RightDown || kind == Kind::EscapeDown;
			const bool up = kind == Kind::LeftUp || kind == Kind::RightUp || kind == Kind::EscapeUp;
			if (down || up)
			{
				const int button = kind == Kind::LeftDown || kind == Kind::LeftUp ? 0 :
					kind == Kind::RightDown || kind == Kind::RightUp ? 1 : 2;
				const int prior = model.buttons[button];
				if (up)
				{
					consume = prior == 2 || (selectedAtEntry && (world || button == 2));
					model.buttons[button] = 0;
				}
				else if (prior != 0)
					consume = prior == 2;
				else
				{
					consume = selectedAtEntry && (button != 0 || world);
					model.buttons[button] = consume ? 2 : 1;
					if (consume && button != 0)
					{
						canceled = true;
						model.Clear();
					}
					else if (consume && model.selected && !model.buttons[1] && !model.buttons[2] && !model.waiting)
						request = model.waiting = true;
				}
			}
			else if (kind == Kind::Move)
				consume = model.buttons[0] == 2 || model.buttons[1] == 2;
			else
			{
				consume = true;
				canceled = canceled || model.selected != 0;
				model.Clear();
			}
			const auto result = branch.input.Consume(Event{kind, world}, context);
			Check(result.consume == consume, "model event consumption");
			Check(result.canceled == canceled, "model cancellation report");
			Check(result.request.has_value() == request, "model target attempt");
			if (result.request)
			{
				Check(result.consume && kind == Kind::LeftDown && world && flags == 7, "request implies fresh permitted world Down");
				Check(result.request->key == (model.selected == 1 ? first : second), "request preserves exact key");
				Check(result.request->token > branch.lastToken, "tokens increase through all reachable transitions");
				branch.lastToken = result.request->token;
				branch.issued = result.request;
			}
			enqueue(branch);
		}
	}
	Check(visited.size() > 80, "explored combinations of pending/attempt/button ownership");
	std::printf("Talent target model: %zu reachable states, %zu transitions\n", visited.size(), transitions);
}
}

/** Standalone engine-free C++17 probe; no X11, renderer, Ruffle, or DB linkage. */
int main()
{
	ArmMatrix();
	GestureRegressions();
	CompletionRegressions();
	PermissionEdgeRegressions();
	ExhaustiveTransitions();
	std::printf("Talent target input: %zu checks, 0 failures\n", checks);
	return 0;
}
