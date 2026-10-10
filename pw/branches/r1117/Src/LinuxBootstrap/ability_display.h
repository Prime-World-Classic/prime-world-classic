#pragma once

#include <cmath>

namespace LinuxBootstrap
{
struct AbilityCooldownDisplay
{
	float current;
	float maximum;
	bool supported;
};

/** Project engine failure values to a disabled, finite HUD representation.
 * This never changes gameplay values or turns an unsupported formula into a free cast.
 * The caller must give unsupported status precedence over other visual states.
 */
inline AbilityCooldownDisplay MakeAbilityCooldownDisplay(float current, float maximum, float cost)
{
	const bool supported = std::isfinite(current) && current >= 0
		&& std::isfinite(maximum) && maximum >= 0 && std::isfinite(cost) && cost >= 0;
	return supported ? AbilityCooldownDisplay{current, maximum, true}
		: AbilityCooldownDisplay{0, 0, false};
}
}
