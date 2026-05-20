#include "stdafx.h"

#if defined( PW_LINUX_NULL_RENDER )

#include "PFApplicatorHistoryAnalysis.h"
#include "PFAbilityInstance.h"

namespace NWorld
{

float GetDamageDealed(CPtr<PFBaseUnit> unit, float deltaTime, PFAbilityInstance *ability)
{
  (void)unit;
  (void)deltaTime;
  (void)ability;
  return 0.0f;
}

}

#else

#include "PFApplInstant.h"
#include "PFApplicatorHistoryAnalysis.h"

namespace NWorld
{

namespace
{

struct DamageCounter_
{
  DamageCounter_(PFAbilityInstance *ability): damageDealed(0.f), ability(ability) {}
  void operator()(CObj<PFBaseApplicator> &app)
  {
    PFApplDamage *ad = dynamic_cast<PFApplDamage *>(app.GetPtr());
    if (ad && ad->GetAbility() == ability)
    {
      damageDealed += ad->GetDamageDealed();
    }
  }
  float damageDealed;
  PFAbilityInstance *ability;
};

}

float GetDamageDealed(CPtr<PFBaseUnit> unit, float deltaTime, PFAbilityInstance *ability)
{
  DamageCounter_ c(ability);
  unit->ForAllSentApplicatorsInHistoryLess(c, deltaTime);
  return c.damageDealed;
}

}

#endif
