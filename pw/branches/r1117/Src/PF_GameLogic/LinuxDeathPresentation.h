#pragma once

namespace NWorld
{
/** Native visual snapshot only; world corpse timers remain authoritative and serialized separately. */
struct LinuxDeathPresentation
{
	/** Select the authored terminal state independently of world retirement and ordinary falling. */
	enum class Kind : unsigned char { Death, Unsummon };
	unsigned long long sequence=0;
	double startedAt=-1,fallStartedAt=-1;
	float fallSpeed=0;
	bool active=false;
	Kind kind=Kind::Death;
};
}
