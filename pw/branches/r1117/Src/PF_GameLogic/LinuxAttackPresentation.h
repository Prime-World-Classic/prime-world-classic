#pragma once

namespace NWorld
{
/** Latest accepted native attack, copied to presentation only; never serialized or used by combat.
 * The object ID distinguishes replaced abilities. Cancellation retains the sequence for deduplication.
 */
struct LinuxAttackPresentation
{
	unsigned long long sequence=0;
	int abilityObjectId=-1;
	double startedAt=-1;
	float period=0;
	/** Snapshot before synchronous damage callbacks; never retain or revisit a possibly removed victim. */
	float targetX=0,targetY=0;
	bool hasTarget=false;
	bool cancelled=false;
};
}
