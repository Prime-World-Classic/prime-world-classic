#pragma once

#include <array>
#include <cstddef>
#include <optional>

/** Recognize production VK_ESCAPE and the bootstrap's synthetic X11 Escape. */
constexpr bool PwRuffleIsEscapeKey(int key) noexcept
{
	return key == 27 || key == 65307;
}

/** X11 edge identity before conversion loses physical keycode and server time. */
struct PwRuffleX11KeyEdge
{
	bool down = false;
	unsigned long window = 0;
	unsigned int keycode = 0;
	unsigned long time = 0;
};

/** Only adjacent release/press edges with identical X11 identity form a repeat. */
constexpr bool PwRuffleIsX11AutoRepeatPair(const PwRuffleX11KeyEdge& release,
	const PwRuffleX11KeyEdge& press) noexcept
{
	return !release.down && press.down && release.window == press.window &&
		release.keycode == press.keycode && release.time == press.time;
}

/**
 * @brief Engine-free ownership of native number-key gestures, not key bindings.
 *
 * Feed all ordered KEY_DOWN/KEY_UP edges, including when gameplay is disallowed.
 * Pass stable ASCII digit identities: 1..9 select slots 0..8, and 0 selects slot 9.
 * KEY_CHAR is text and MUST NOT be treated as another Down. The caller supplies
 * current ownership, focus, text/modal and modifier permissions in allowed, and
 * marks normalized X11 autorepeat Downs with repeat=true (native nRep > 1).
 *
 * Every digit edge is consumed, including blocked presses and orphan releases;
 * unrelated keys pass through. Only a fresh, allowed, nonrepeat Down requests a
 * slot. Repeated or blocked presses cannot become actions until physical Up, even
 * if modifiers, permissions, hero/session, or input epoch change while held.
 * Epoch changes invalidate held actions but NEVER release their ownership. Keep
 * this object alive across blur/reset until releases drain; do not recreate it
 * on focus gain. Initial repeated Downs are quarantined until physical release.
 *
 * The caller must validate authored slot mapping and live engine permissions.
 * Failure/cancellation after a slot request does not release this gesture, and
 * no engine command, Flash call, modifier mask, or gameplay pointer lives here.
 */
class PwRuffleActionKeyInput
{
public:
	struct Decision
	{
		bool consume = false; ///< Suppress this edge in legacy gameplay handlers.
		std::optional<int> slot; ///< Zero-based authored action slot, only on Down.
	};

	/** Only top-row ASCII digits participate; keypad and shifted text do not. */
	static constexpr int SlotForKey(int key) noexcept
	{
		return key == '0' ? 9 : key >= '1' && key <= '9' ? key - '1' : -1;
	}

	/** Route one native key edge. repeat applies to Down, never to physical Up. */
	Decision Key(bool down, int key, bool allowed, std::size_t epoch, bool repeat = false) noexcept
	{
		if (epoch_ != epoch)
		{
			for (auto& phase : phases_)
				if (phase == Phase::Captured) phase = Phase::Blocked;
			epoch_ = epoch;
		}
		const int slot = SlotForKey(key);
		if (slot < 0) return {};
		auto& phase = phases_[static_cast<std::size_t>(slot)];
		Decision result{true, std::nullopt};
		if (!down)
		{
			phase = Phase::Released;
			return result;
		}
		if (phase != Phase::Released) return result;
		phase = allowed && !repeat ? Phase::Captured : Phase::Blocked;
		if (phase == Phase::Captured) result.slot = slot;
		return result;
	}

private:
	enum class Phase { Released, Captured, Blocked };
	std::array<Phase, 10> phases_{};
	std::size_t epoch_ = 0;
};
