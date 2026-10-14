#pragma once

#include <cmath>
#include <memory>

namespace NDb { struct Unit; }

namespace NWorld
{
/** World-owned, nonserialized access to authored animation timing, independent of visibility.
 * A null marker requests node duration. Implementations must not advance animation or world RNG.
 */
class LinuxAbilityAnimationTiming
{
public:
	virtual ~LinuxAbilityAnimationTiming() = default;
	virtual bool Query(const NDb::Unit& unit, const char* node, const char* marker, float& seconds) = 0;
};
}
