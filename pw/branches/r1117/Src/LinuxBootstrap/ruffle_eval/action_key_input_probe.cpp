// Exercise compatibility with the global macro in native X11 translation units.
#define None 0L
#include "action_key_input.h"
#undef None

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

namespace
{
using Input = PwRuffleActionKeyInput;
using Edge = PwRuffleX11KeyEdge;
constexpr char digits[] = "1234567890";
std::size_t checks = 0;
std::size_t transitions = 0;

/** Fail independently of NDEBUG and leave sanitizer diagnostics intact. */
void Check(bool condition, const char* message)
{
	++checks;
	if (condition) return;
	std::fprintf(stderr, "Action key input failed: %s (check %zu)\n", message, checks);
	std::exit(1);
}

/** Independent observable model: epochs never make a depressed key fresh. */
struct Model
{
	unsigned held = 0;

	Input::Decision Key(bool down, int key, bool allowed, bool repeat)
	{
		int slot = -1;
		for (int i = 0; i < 10; ++i)
			if (key == digits[i]) slot = i;
		if (slot < 0) return {};
		const unsigned mask = 1u << slot;
		const bool wasHeld = (held & mask) != 0;
		held = down ? held | mask : held & ~mask;
		return {true, down && !wasHeld && allowed && !repeat ?
			std::optional<int>(slot) : std::nullopt};
	}
};

/** Apply one real policy transition and compare both outputs with the model. */
void Compare(Input& input, Model& model, bool down, int key, bool allowed,
	std::size_t epoch, bool repeat)
{
	const auto expected = model.Key(down, key, allowed, repeat);
	const auto actual = input.Key(down, key, allowed, epoch, repeat);
	Check(actual.consume == expected.consume, "gesture ownership differs from model");
	Check(actual.slot == expected.slot, "slot request differs from model");
	++transitions;
}

/** ASCII boundaries, native/synthetic Escape, and integer extremes are explicit. */
void Mapping()
{
	for (int key = -1; key <= 65536; ++key)
	{
		int expected = -1;
		for (int slot = 0; slot < 10; ++slot)
			if (digits[slot] == key) expected = slot;
		Check(Input::SlotForKey(key) == expected, "exact ASCII slot mapping");
		Check(PwRuffleIsEscapeKey(key) == (key == 27 || key == 65307), "Escape aliases");
	}
	for (const int key : {std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
	{
		Check(Input::SlotForKey(key) == -1, "extreme key code rejected");
		Check(!PwRuffleIsEscapeKey(key), "extreme key is not Escape");
	}
	static_assert(PwRuffleIsEscapeKey(27), "Native VK_ESCAPE must cancel");
	static_assert(PwRuffleIsEscapeKey(65307), "Synthetic XK_Escape must cancel");
	static_assert(Input::SlotForKey('0') == 9, "Zero is the tenth slot");
}

/** Regression traces assert behavior without relying on the reference model. */
void Regressions()
{
	for (const int key : digits)
	{
		if (!key) continue;
		Input input;
		const int slot = Input::SlotForKey(key);
		auto result = input.Key(false, key, true, 0);
		Check(result.consume && !result.slot, "orphan Up never activates or leaks");
		result = input.Key(true, key, true, 0);
		Check(result.consume && result.slot == slot, "fresh allowed Down activates once");
		for (const std::size_t epoch : {std::size_t{0}, std::size_t{1},
			std::numeric_limits<std::size_t>::max(), std::size_t{0}})
		for (const bool allowed : {false, true})
		for (const bool repeat : {false, true})
		{
			result = input.Key(true, key, allowed, epoch, repeat);
			Check(result.consume && !result.slot, "held key cannot rearm after focus/epoch/permission change");
		}
		result = input.Key(false, key, false, 9);
		Check(result.consume && !result.slot, "successful or canceled request still owns release");
		result = input.Key(true, key, true, 9);
		Check(result.slot == slot, "physical release allows a new action");
	}

	for (const bool blocked : {false, true})
	for (const bool repeat : {false, true})
	{
		Input input;
		auto result = input.Key(true, '1', !blocked, 7, repeat);
		Check(result.consume && result.slot.has_value() == (!blocked && !repeat), "initial permission/repeat gate");
		result = input.Key(true, '1', true, 8, false);
		Check(result.consume && !result.slot, "blocked/modifier-held/repeated press cannot acquire later");
		result = input.Key(false, '1', false, 8);
		Check(result.consume && !result.slot, "blocked release remains owned");
		Check(input.Key(true, '1', true, 8).slot == 0, "new physical press after block is allowed");
	}

	Input input;
	Check(input.Key(true, '1', true, 1).slot == 0, "first simultaneous key");
	Check(input.Key(true, '0', true, 1).slot == 9, "second simultaneous key");
	Check(input.Key(false, '1', true, 2).consume, "release one key across epoch change");
	Check(!input.Key(true, '0', true, 2).slot, "releasing another key does not rearm held zero");
	Check(input.Key(true, '1', true, 2).slot == 0, "released key operates independently");
	const int unrelated[] = {27, 65307, '!', 'a', 0, 0x60};
	for (const int key : unrelated)
	{
		auto result = input.Key(true, key, true, 3);
		Check(!result.consume && !result.slot, "unrelated keys, Escape and numpad pass through");
	}
	Check(!input.Key(true, '0', true, 3).slot, "unrelated events never release digits");
}

/** All 1,024 observable held sets, edge/permission/repeat/epoch flags and slots. */
void ExhaustiveTransitions()
{
	const int keys[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
		27, 65307, '!', 'A', 0, std::numeric_limits<int>::max()};
	for (unsigned held = 0; held < 1024; ++held)
	{
		Input baseline;
		Model initial;
		for (int slot = 0; slot < 10; ++slot)
			if (held & (1u << slot))
				Compare(baseline, initial, true, digits[slot], (held + slot) % 2 == 0, 7, false);
		for (const int key : keys)
		for (unsigned flags = 0; flags < 16; ++flags)
		{
			Input input = baseline;
			Model model = initial;
			Compare(input, model, flags & 1, key, flags & 2, flags & 4 ? 8 : 7, flags & 8);
			Compare(input, model, true, key, true, 9, false);
			Compare(input, model, false, key, false, 10, false);
			Compare(input, model, true, key, true, 10, false);
			Compare(input, model, false, key, false, 11, true);
		}
	}
}

/** Exercise the exact predicate consumed by the opt-in native X11 pump. */
void RepeatPairMatrix()
{
	const unsigned long windows[] = {0, 1, 2, std::numeric_limits<unsigned long>::max()};
	const unsigned int keycodes[] = {0, 1, 255, std::numeric_limits<unsigned int>::max()};
	const unsigned long times[] = {0, 1, 2, std::numeric_limits<unsigned long>::max()};
	for (const bool releaseDown : {false, true})
	for (const bool pressDown : {false, true})
	for (unsigned w1 = 0; w1 < 4; ++w1)
	for (unsigned w2 = 0; w2 < 4; ++w2)
	for (unsigned k1 = 0; k1 < 4; ++k1)
	for (unsigned k2 = 0; k2 < 4; ++k2)
	for (unsigned t1 = 0; t1 < 4; ++t1)
	for (unsigned t2 = 0; t2 < 4; ++t2)
	{
		const Edge release{releaseDown, windows[w1], keycodes[k1], times[t1]};
		const Edge press{pressDown, windows[w2], keycodes[k2], times[t2]};
		Check(PwRuffleIsX11AutoRepeatPair(release, press) ==
			(!releaseDown && pressDown && w1 == w2 && k1 == k2 && t1 == t2),
			"repeat pairs require exact direction/window/keycode/server time");
	}
}

/** Mock the pump's adjacency rule, sharing its predicate and the real owner. */
void RepeatStream()
{
	struct NativeEdge { bool down; bool repeat; };
	const auto normalize = [](const std::vector<Edge>& edges)
	{
		std::vector<NativeEdge> result;
		for (std::size_t i = 0; i < edges.size(); ++i)
		{
			if (i + 1 < edges.size() && PwRuffleIsX11AutoRepeatPair(edges[i], edges[i + 1]))
			{
				result.push_back({true, true});
				++i;
			}
			else result.push_back({edges[i].down, false});
		}
		return result;
	};
	const auto repeated = normalize({{true, 4, 10, 1}, {false, 4, 10, 2}, {true, 4, 10, 2},
		{false, 4, 10, 3}, {true, 4, 10, 3}, {false, 4, 10, 4}});
	Check(repeated.size() == 4, "two X11 repeat pairs collapse to two marked Downs");
	Check(repeated[0].down && !repeated[0].repeat && repeated[1].down && repeated[1].repeat &&
		repeated[2].down && repeated[2].repeat && !repeated[3].down, "repeat stream has no fake Up");
	Input input;
	unsigned actions = 0;
	for (const auto& edge : repeated)
	{
		const auto result = input.Key(edge.down, '1', true, 0, edge.repeat);
		Check(result.consume, "entire repeat stream remains owned");
		actions += result.slot.has_value();
	}
	Check(actions == 1, "X11 repeat stream emits exactly one action");
	Check(input.Key(true, '1', true, 0).slot == 0, "final physical Up releases repeat gesture");

	const auto rapid = normalize({{true, 4, 10, 1}, {false, 4, 10, 2},
		{true, 4, 10, 3}, {false, 4, 10, 4}});
	Check(rapid.size() == 4, "different timestamps preserve rapid physical taps");
	Input tapped;
	actions = 0;
	for (const auto& edge : rapid)
		actions += tapped.Key(edge.down, '1', true, 0, edge.repeat).slot.has_value();
	Check(actions == 2, "two genuine taps emit two actions");

	Input regained;
	for (const auto& edge : normalize({{false, 4, 10, 2}, {true, 4, 10, 2}}))
	{
		const auto result = regained.Key(edge.down, '1', true, 99, edge.repeat);
		Check(result.consume && !result.slot, "initial repeat after focus regain cannot become a fresh action");
	}
	Check(!regained.Key(true, '1', true, 100).slot, "repeat quarantine survives epoch change");
	Check(regained.Key(false, '1', false, 100).consume, "repeat quarantine owns physical release");
	Check(regained.Key(true, '1', true, 100).slot == 0, "repeat quarantine ends only at physical Up");
}
}

int main()
{
	Mapping();
	Regressions();
	ExhaustiveTransitions();
	RepeatPairMatrix();
	RepeatStream();
	std::printf("Action key input: %zu transitions, %zu checks passed\n", transitions, checks);
}
