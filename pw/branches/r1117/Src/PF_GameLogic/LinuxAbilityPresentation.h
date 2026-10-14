#pragma once

namespace NWorld
{
/** Latest accepted ordinary cast; bounded value snapshot, never serialized or consulted by simulation. */
struct LinuxAbilityPresentation
{
	unsigned long long sequence=0;
	int instanceObjectId=-1;
	double startedAt=-1;
	float workTime=0,fireOffset=0;
	char node[129]={},marker[129]={};
	float targetX=0,targetY=0;
	/** Original ELookKind value; zero means that even a valid target must not change facing. */
	int lookKind=0;
	bool hasTarget=false,syncVisual=false,cancelled=false;
};
}
