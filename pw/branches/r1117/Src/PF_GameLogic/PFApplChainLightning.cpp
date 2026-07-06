#include "stdafx.h"

#if defined( PW_LINUX_NULL_RENDER )

#include "PFApplChainLightning.h"
#include "PFBaseUnit.h"
#include "PFWorld.h"
#include "PFAIWorld.h"
#include "PFDispatchFactory.h"

namespace NWorld
{

void PFApplChainLightning::operator()(PFBaseUnit& unit)
{
  CPtr<PFBaseUnit> pTargetUnit = &unit;
  if (unit.IsDead() || unit.IsInvalidAbilityTarget())
    return;
  if (find(affectedUnits.begin(), affectedUnits.end(), pTargetUnit) != affectedUnits.end())
    return;

  if (!IsValid(pBestTarget) || !IsValid(pPrevTargetUnit))
  {
    pBestTarget = pTargetUnit;
    return;
  }

  float distanceOld = fabs2(pPrevTargetUnit->GetPosition() - pBestTarget->GetPosition());
  float distance = fabs2(pPrevTargetUnit->GetPosition() - pTargetUnit->GetPosition());
  if (distance <= distanceOld)
    pBestTarget = pTargetUnit;
}

void PFApplChainLightning::ApplyEffect(const NDb::Ptr<NDb::EffectBase>&, PFBaseUnit*)
{
}

void PFApplChainLightning::DoIt()
{
  CPtr<PFBaseUnit> const& pSender = GetAbilityOwner();
  if (!IsValid(pSender) || !pSender->GetWorld() || !pSender->GetWorld()->GetAIWorld())
  {
    numJumpsLeft = 0;
    return;
  }

  pBestTarget = NULL;
  pSender->GetWorld()->GetAIWorld()->ForAllUnitsInRange(
    startDispatchPos, range, *this, UnitMaskingPredicate(pSender, GetDB().targetFilter));

  if (IsValid(pBestTarget))
  {
    AttackUnit(pBestTarget);
    return;
  }

  numJumpsLeft = 0;
}

void PFApplChainLightning::AttackUnit(CPtr<PFBaseUnit> const& pTargetUnit)
{
  if (!IsValid(pTargetUnit))
  {
    numJumpsLeft = 0;
    return;
  }

  affectedUnits.push_back(pTargetUnit);

  Target const targ(pTargetUnit.GetPtr());
  Target const source(IsValid(pPrevTargetUnit) ? pPrevTargetUnit.GetPtr() : GetAbilityOwner().GetPtr());
  CreateDispatch(pAbility, this, source, targ, GetDB().spell);

  pPrevTargetUnit = pTargetUnit;
  startDispatchPos = pTargetUnit->GetPosition();
  timeTillJump = betweenJumpsTimeInterval;
  if (numJumpsLeft > 0)
    --numJumpsLeft;
  scale *= onJumpSpellScale;
}

bool PFApplChainLightning::Start()
{
  if (PFBaseApplicator::Start() || numJumpsLeft <= 0 || !GetTarget().IsUnitValid())
    return true;

  affectedUnits.reserve(numJumpsLeft);
  pPrevTargetUnit = GetAbilityOwner();
  startDispatchPos = IsValid(pPrevTargetUnit) ? pPrevTargetUnit->GetPosition() : VNULL3;
  AttackUnit(GetTarget().GetUnit());

  return numJumpsLeft <= 0;
}

bool PFApplChainLightning::Step(float dtInSeconds)
{
  if (numJumpsLeft <= 0)
    return true;

  timeTillJump -= dtInSeconds;
  if (timeTillJump <= 0.0f)
    DoIt();

  return numJumpsLeft <= 0;
}

void PFApplChainLightning::OnDispatchMissed(const PFDispatch*)
{
  if (!RetrieveParam(GetDB().jumpOnEvade, true))
    numJumpsLeft = 0;
}

}

REGISTER_WORLD_OBJECT_NM(PFApplChainLightning, NWorld);

#else
#include "PFApplChainLightning.h"
#include "PFBaseUnit.h"
#include "PFWorld.h"
#include "PFDispatchFactory.h"
#include "PFAIWorld.h"
#include "PFClientBaseUnit.h"
#include "AdventureScreen.h"

#ifndef VISUAL_CUTTED
#include "PFClientApplicators.h"
#else
#include "../Game/PF/Audit/ClientStubs.h"
#endif

namespace NWorld
{

void PFApplChainLightning::operator()( PFBaseUnit &unit )
{
  CPtr<PFBaseUnit> pTargetUnit = &unit;
  if(pTargetUnit->IsDead() || find(affectedUnits.begin(), affectedUnits.end(), pTargetUnit) != affectedUnits.end())
    return;

  if (unit.IsInvalidAbilityTarget())
    return;

  if (IsValid(pBestTarget))
  {
    float distanceOld = fabs2(pPrevTargetUnit->GetPosition() - pBestTarget->GetPosition());
    float distance    = fabs2(pPrevTargetUnit->GetPosition() - pTargetUnit->GetPosition());
    if (distance > distanceOld)
      return;
  }
  pBestTarget = pTargetUnit;
}

void PFApplChainLightning::ApplyEffect( const NDb::Ptr<NDb::EffectBase>& pDBEffect, PFBaseUnit* pTarget )
{
  if ( pDBEffect->GetObjectTypeID() == NDb::EffectList::typeId )
  {
    const NDb::EffectList* pDBList = static_cast<const NDb::EffectList*>(pDBEffect.GetPtr());
    for ( int i = 0, count = pDBList->effects.size(); i < count; ++i )
    {
      ApplyEffect( pDBList->effects[i], pTarget );
    }
  }
  else if ( pDBEffect->GetObjectTypeID() == NDb::LightningEffect::typeId )
  {
    // I know what i'm doing, however reinterpret_cast looks awkward
    // CreateLightningEffect sets new state to Ptr, so want to change original Ptr, not local one
    CObj<PF_Core::LightningEffect> pEffect = NGameX::CreateLightningEffect( reinterpret_cast<const NDb::Ptr<NDb::LightningEffect>&>(pDBEffect), GetAbilityOwner()->GetSkinId() );
    pEffect->Apply(CPtr<PF_Core::ClientObjectBase>(pPrevTargetUnit->ClientObject()), CPtr<PF_Core::ClientObjectBase>(pTarget->ClientObject()));
  }
  else
  {
    NI_ALWAYS_ASSERT( "Expected LightningEffect or EffectList" );
  }
}

void PFApplChainLightning::DoIt()
{
	const CPtr<PFBaseUnit> &pSender = GetAbilityOwner();

  // find all targets in area and attack best one...
  pBestTarget = NULL;
  pSender->GetWorld()->GetAIWorld()->ForAllUnitsInRange(startDispatchPos, range, *this, UnitMaskingPredicate(pSender, GetDB().targetFilter ));
  
  if (IsValid(pBestTarget))
  {
    AttackUnit(pBestTarget);
    return;
  }

  // no more targets
  numJumpsLeft = 0;
}

void PFApplChainLightning::AttackUnit(CPtr<PFBaseUnit> const &pTargetUnit)
{
  affectedUnits.push_back(pTargetUnit);

  NDb::EFaction playerFaction = NGameX::AdventureScreen::Instance()->GetPlayerFaction();

  if ( pPrevTargetUnit->IsVisibleForFaction( playerFaction ) || pTargetUnit->IsVisibleForFaction( playerFaction ) )
  {
    int index = RetrieveParam( GetDB().effectIndex, 0 );
    const NDb::Ptr<NDb::EffectBase>& pDBEffect = GetDB().effects[index];
    ApplyEffect( pDBEffect, pTargetUnit );
  }

  // Create dispatch and tune it
  Target const targ(pTargetUnit);
  Target const source( pPrevTargetUnit );
  CreateDispatch(pAbility, this, source, targ, GetDB().spell);
  pPrevTargetUnit = pTargetUnit;

  startDispatchPos = pTargetUnit->GetPosition(); // position for next jump
  timeTillJump = betweenJumpsTimeInterval;
  if ( numJumpsLeft > 0 )
    --numJumpsLeft;

  scale *= GetDB().onJumpSpellScale;
}

bool PFApplChainLightning::Start()
{
  if (PFBaseApplicator::Start() || numJumpsLeft <= 0 || !IsUnitValid(GetTarget().GetUnit()))
    return true;

  affectedUnits.reserve(numJumpsLeft);

	pPrevTargetUnit = GetAbilityOwner();
  startDispatchPos = pPrevTargetUnit->GetPosition();
  AttackUnit(GetTarget().GetUnit());  

  return false;
}

bool PFApplChainLightning::Step(float dtInSeconds)
{
  NI_PROFILE_FUNCTION

  if (0 >= numJumpsLeft )
  {
    return true;
  }

  timeTillJump -= dtInSeconds;

  if (timeTillJump <= 0.f)
  {
    DoIt();
  }
  return false; // not done. need more steps.
}

// Посланный этим аппликатором диспатч промахнулся
void PFApplChainLightning::OnDispatchMissed( const PFDispatch* _pDispatch )
{
  if ( !RetrieveParam( GetDB().jumpOnEvade, true ) )
  {
    numJumpsLeft = 0;
  }
}


}

REGISTER_WORLD_OBJECT_NM(PFApplChainLightning,        NWorld);
#endif
