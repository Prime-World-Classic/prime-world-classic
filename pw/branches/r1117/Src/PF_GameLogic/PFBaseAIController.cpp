#include "stdafx.h"
#include "PFBaseAIController.h"
#include "PFAIHelper.h"
#include "PFMaleHero.h"
#include "PFWorld.h"
#include "PFAIStates.h"
#include "Core/Scheduler.h"

namespace NWorld
{

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
PFBaseAIController::PFBaseAIController(PFBaseHero * _hero, NCore::ITransceiver * transceiver)
: aiHelper(_hero, transceiver)
, isDead(true)  // The hero is "dead" initially and respawns when session starts
{
  hero = dynamic_cast<PFBaseMaleHero*>( _hero );
}

#if defined(PW_LINUX_NULL_RENDER)
void PFBaseAIController::RefreshLinuxHeroReference()
{
  if ( !IsValid(hero) || !hero->GetWorld() )
    return;

  PFBaseMaleHero* liveHero = dynamic_cast<PFBaseMaleHero*>(hero->GetWorld()->FindLinuxUnitByObjectId(hero->GetObjectId()));
  if ( liveHero )
  {
    hero = liveHero;
    aiHelper.pUnit = liveHero;
  }
}
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFBaseAIController::Step( float timeDelta )
{
#if defined(PW_LINUX_NULL_RENDER)
  RefreshLinuxHeroReference();
#endif

  if ( hero->IsDead() )
  {
    if ( !isDead )
    {
      // Just dead
      isDead = true;
      OnDie();
    }
  }
  else
  {
    if ( isDead )
    {
      // Just respawned
      isDead = false;
      OnRespawn();
    }
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
AIBaseState* PFBaseAIController::CurrentState()
{
  return dynamic_cast<AIBaseState*>( GetCurrentState() );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const char* PFBaseAIController::GetCurrentStateName()
{
  AIBaseState* state = CurrentState();
  return state ? PFAIStatesEnum_ToString(state->stateType) : PFAIStatesEnum_ToString(NONE);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
PFWorld * PFBaseAIController::GetWorld()
{
  return IsValid(hero) ? hero->GetWorld() : 0;
};

} // namespace NWorld
