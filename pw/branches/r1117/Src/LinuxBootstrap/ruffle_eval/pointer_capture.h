#pragma once

#include <array>
#include <stdexcept>

/** Pointer ownership for the opt-in HUD. A press belongs to its initial surface
 * until release, even when dragged across the HUD/world boundary. This prevents
 * one gesture from both activating Flash and issuing a native map command.
 * Hit testing is supplied by the caller; this class does not invent UI geometry.
 */
class PwRufflePointerCapture
{
public:
	enum class Kind { Move, Down, Up, Wheel, Leave };
	struct Decision { bool forward = false; bool consume = false; };
	/** Button index: left=0, middle=1, right=2; only relevant to Down/Up. */
	Decision Route(Kind kind, bool hit, unsigned button = 0)
	{
		if (kind == Kind::Down || kind == Kind::Up)
		{
			if (button >= owners_.size()) throw std::invalid_argument("Invalid pointer button");
			auto& owner = owners_[button];
			if (kind == Kind::Down)
			{
				if (owner == Owner::Released) owner = hit ? Owner::Hud : Owner::World;
				return {owner == Owner::Hud, owner == Owner::Hud};
			}
			const bool hud = owner == Owner::Hud;
			const bool orphan = owner == Owner::Released;
			owner = Owner::Released;
			return {hud, hud || (orphan && hit)};
		}
		if (kind == Kind::Wheel) return {hit, hit};
		if (kind == Kind::Leave) return {true, false};
		if (kind != Kind::Move) throw std::invalid_argument("Invalid pointer kind");
		bool hud = false, world = false;
		for (const auto owner : owners_)
		{
			hud |= owner == Owner::Hud;
			world |= owner == Owner::World;
		}
		return {hud || !world, hud};
	}
	/** Called on focus loss, resize or host shutdown after VM controls are released. */
	void Reset() { owners_.fill(Owner::Released); }
private:
	enum class Owner { Released, Hud, World };
	std::array<Owner, 3> owners_{};
};
