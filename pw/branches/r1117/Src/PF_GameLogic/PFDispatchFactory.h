#pragma once

#include "DBAbility.h"
#if defined(PW_LINUX_NULL_RENDER)
#include "PFDispatch.h"
#endif

namespace NWorld
{

class PFWorld;
class PFAbilityInstance;
class PFBaseApplicator;
class PFApplCreatePars;
class Target;
class PFDispatch;
class PFBaseUnit;

CObj<PFBaseApplicator> CreateApplicator(PFApplCreatePars const &cp);
#if defined(PW_LINUX_NULL_RENDER)
/** Retain native dispatches through synchronous callbacks and transfer ownership to the caller.
 * A completed dispatch can be dead but remains inspectable until this handle is released.
 */
CObj<PFDispatch> CreateDispatch(CObj<PFAbilityInstance> const& pAbility,
	CPtr<PFBaseApplicator> const& pParent, Target const& source, Target const& target,
	NDb::Ptr<NDb::Spell> const& pSpell, int flagsForApplicators = 0,
	bool start = true, float startDelay = 0.0f);
#else
PFDispatch *CreateDispatch(CObj<PFAbilityInstance> const &pAbility,
                           CPtr<PFBaseApplicator> const& pParent,
                           Target const &source, 
                           Target const &target,
                           NDb::Ptr<NDb::Spell> const& pSpell,
                           int flagsForApplicators = 0,
                           bool start = true, 
                           float startDelay = 0.0f );
#endif

}
