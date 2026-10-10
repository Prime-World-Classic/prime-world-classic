#pragma once

#include <cmath>
#include <limits>

namespace LinuxBootstrap
{
/** Production float division/truncation, with invalid DB values rejected before casting. */
inline int ResolveStartingPrime(float primePerTeam, int teamSize)
{
	if (teamSize <= 0 || !std::isfinite(primePerTeam) || primePerTeam < 0) return 0;
	const float perPlayer = primePerTeam / static_cast<float>(teamSize);
	if (static_cast<double>(perPlayer) > std::numeric_limits<int>::max()) return 0;
	return static_cast<int>(perPlayer);
}
}
