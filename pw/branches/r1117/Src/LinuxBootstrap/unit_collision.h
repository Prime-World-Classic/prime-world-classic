#pragma once

#include <algorithm>
#include <cmath>

namespace LinuxBootstrap
{
/** Convert the engine's full unit diameter to a radius shared by placement/resolution.
 * Retain the one-meter bootstrap fallback when no usable footprint was authored.
 */
inline float UnitCollisionRadius(float diameter)
{
	return std::isfinite(diameter) && diameter > 0 ? std::max(diameter * .5f, .5f) : 1.f;
}
}
