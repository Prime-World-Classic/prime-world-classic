#pragma once

namespace NWorld
{
/** Native visual snapshot only; world corpse timers remain authoritative and serialized separately. */
struct LinuxDeathPresentation
{
	unsigned long long sequence=0;
	double startedAt=-1,fallStartedAt=-1;
	float fallSpeed=0;
	bool active=false;
};
}
