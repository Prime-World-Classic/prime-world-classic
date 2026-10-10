#pragma once

#include <cstddef>
#include <limits>

namespace LinuxBootstrap
{
/** Resolve a DB cost sentinel from the AI level table; unavailable costs must not
 * make a talent free. Explicit zero remains a valid authored price.
 */
inline int ResolveTalentCost(int authored, int level, const int* levels, std::size_t count)
{
	if (authored >= 0) return authored;
	if (!levels || level < 0 || static_cast<std::size_t>(level) >= count || levels[level] < 0)
		return std::numeric_limits<int>::max();
	return levels[level];
}
}
