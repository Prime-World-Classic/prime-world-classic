#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

/**
 * @brief Pending authored talent and native gesture ownership, without engine types.
 *
 * Feed ordered Down/Up/Move events even when no talent is pending. Mark only
 * native world hits as world=true; HUD/outside presses cannot request a target.
 * A fresh world left Down requests validation/submission once; Up NEVER casts.
 * Right/Escape Down cancels on either surface and owns the complete gesture.
 * Duplicate Downs cannot acquire ownership, repeat a request, or cancel a new Arm.
 *
 * Arm replaces the exact hero/row/column/epoch, including an identical key. It
 * invalidates outstanding requests without releasing captured buttons. Likewise,
 * Invalidate/Revalidate/Complete preserve release consumption: clearing selection
 * must not turn the tail of a HUD/targeting gesture into a native world command.
 * A left press cannot request while right/Escape is held. Moves are consumed while
 * a captured pointer button is held. Orphan/outside releases cannot request a cast;
 * world releases (and Escape Up) are also consumed while a selection is pending.
 * A press that discovers invalid permissions still captures its release, but
 * clears selection and emits no request instead of falling through to the world.
 *
 * Call Revalidate each frame, including frames with no input. Context::allowed is
 * the caller's CURRENT permission/usability decision for the exact pending talent,
 * not a cached HUD flag. Hero/epoch changes and denied permission clear selection.
 * Call Invalidate on blur, teardown, queue loss, or other lifecycle cancellation;
 * preserve this object until held releases drain. A new session may use a fresh
 * object only after its upstream pointer ownership has also been reset.
 *
 * For Decision::request, resolve the clicked position/unit from that same Down,
 * revalidate engine ownership/target rules, submit through the transceiver, then
 * call Complete(request, submitted). No pointer, target math, mask, cooldown,
 * command, or engine validation lives here. Tokens reject stale completions even
 * after rearming the same key. Only successful submission clears the selection.
 */
class PwRuffleTalentTargetInput
{
public:
	/** Exact authored cell, scoped to a live hero and the parent's input epoch. */
	struct Key
	{
		int heroObjectId = -1;
		int row = -1;
		int column = -1;
		std::size_t epoch = 0;

		bool operator==(const Key& other) const noexcept
		{
			return heroObjectId == other.heroObjectId && row == other.row &&
				column == other.column && epoch == other.epoch;
		}
	};

	/** Recomputed native facts. Default construction denies all new requests. */
	struct Context
	{
		int heroObjectId = -1;
		std::size_t epoch = 0;
		bool allowed = false;
	};

	/** Escape has explicit edges so repeats and its release cannot leak. */
	enum class Kind { LeftDown, LeftUp, RightDown, RightUp, EscapeDown, EscapeUp, Move };
	struct Event
	{
		Kind kind = Kind::Move;
		bool world = false; ///< Left acquisition requires a native world hit.
	};

	/** Value-only attempt. Copy it unchanged to Complete; zero is never issued. */
	struct Request
	{
		Key key;
		std::uint64_t token = 0;
	};
	struct Decision
	{
		bool consume = false; ///< Suppress this event in native world controls.
		bool canceled = false; ///< This event/context cleared an existing selection.
		std::optional<Request> request; ///< At most one validation/submission attempt.
	};

	/** Replace selection; malformed/denied arms clear any previous selection. */
	bool Arm(Key key, const Context& context) noexcept
	{
		Invalidate();
		if (!Matches(key, context) || key.row < 0 || key.row >= 6 ||
			key.column < 0 || key.column >= 6)
			return false;
		pending_ = key;
		return true;
	}

	/** Clear selection and attempts, but retain ownership until physical releases. */
	void Invalidate() noexcept
	{
		pending_.reset();
		attempt_.reset();
	}

	/** Clear on changed hero/epoch or denied permission; return whether still pending. */
	bool Revalidate(const Context& context) noexcept
	{
		if (pending_ && !Matches(*pending_, context)) Invalidate();
		return pending_.has_value();
	}

	/** Current exact selection for HUD Chosen; never exposes an engine object. */
	const std::optional<Key>& Pending() const noexcept { return pending_; }
	bool IsChosen(const Key& key) const noexcept { return pending_ && *pending_ == key; }

	/** Apply live permissions, then route one edge without executing gameplay. */
	Decision Consume(const Event& event, const Context& context) noexcept
	{
		Decision result;
		const bool hadPending = pending_.has_value();
		Revalidate(context);
		result.canceled = hadPending && !pending_;
		switch (event.kind)
		{
		case Kind::LeftDown:
			if (owners_[0] == Owner::Released)
			{
				owners_[0] = event.world && hadPending ? Owner::Captured : Owner::External;
				if (pending_ && owners_[0] == Owner::Captured && owners_[1] == Owner::Released &&
					owners_[2] == Owner::Released && !attempt_ &&
					nextToken_ != std::numeric_limits<std::uint64_t>::max())
				{
					attempt_ = Request{*pending_, ++nextToken_};
					result.request = attempt_;
				}
			}
			result.consume = owners_[0] == Owner::Captured;
			break;
		case Kind::RightDown:
		case Kind::EscapeDown:
		{
			auto& owner = owners_[event.kind == Kind::RightDown ? 1 : 2];
			if (owner == Owner::Released)
			{
				owner = hadPending ? Owner::Captured : Owner::External;
				if (owner == Owner::Captured)
				{
					result.canceled = true;
					Invalidate();
				}
			}
			result.consume = owner == Owner::Captured;
			break;
		}
		case Kind::LeftUp:
		case Kind::RightUp:
		case Kind::EscapeUp:
		{
			const auto button = event.kind == Kind::LeftUp ? 0 : event.kind == Kind::RightUp ? 1 : 2;
			result.consume = owners_[button] == Owner::Captured ||
				(hadPending && (event.world || event.kind == Kind::EscapeUp));
			owners_[button] = Owner::Released;
			break;
		}
		case Kind::Move:
			result.consume = owners_[0] == Owner::Captured || owners_[1] == Owner::Captured;
			break;
		default:
			result.canceled = result.canceled || pending_.has_value();
			result.consume = true;
			Invalidate();
			break;
		}
		return result;
	}

	/**
	 * @brief Complete the current token once; false submission retains selection.
	 * @return False for stale, altered or already completed requests (no mutation).
	 * This is bookkeeping, not permission to cast. Revalidate before engine dispatch.
	 */
	bool Complete(const Request& request, bool submitted) noexcept
	{
		if (!attempt_ || !pending_ || request.token != attempt_->token ||
			!(request.key == attempt_->key) || !(request.key == *pending_))
			return false;
		attempt_.reset();
		if (submitted) pending_.reset();
		return true;
	}

private:
	/** External presses remain external if a talent is armed while held. */
	enum class Owner { Released, External, Captured };
	static bool Matches(const Key& key, const Context& context) noexcept
	{
		return context.allowed && key.heroObjectId >= 0 &&
			key.heroObjectId == context.heroObjectId && key.epoch == context.epoch;
	}
	std::optional<Key> pending_;
	std::optional<Request> attempt_;
	std::array<Owner, 3> owners_{}; ///< Left, right, Escape; selection reset preserves these.
	std::uint64_t nextToken_ = 0; ///< Never recycle tokens, including on lifecycle reset.
};
