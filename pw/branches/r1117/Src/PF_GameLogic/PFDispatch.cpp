#include "stdafx.h"

#if defined( PW_LINUX_NULL_RENDER )

#include "PFDispatch.h"
#include "PFAbilityInstance.h"
#include "PFAbilityData.h"
#include "PFBaseUnit.h"
#include "PFDispatchFactory.h"
#include "PFApplMod.h"
#include "PFAIWorld.h"

namespace NWorld
{

void PFDispatch::OnDie()
{
  RegisterAggression();

  Clear(applicators);
  pAbility = NULL;
  pSender = NULL;
}

void PFDispatch::AddApplicators( vector<NDb::Ptr<NDb::BaseApplicator>> const& newApplicators, PFAbilityData* upgrader )
{
  PFApplCreatePars cp(pAbility, target, pParentApplicator, this, false, upgrader);

  for (vector<NDb::Ptr<NDb::BaseApplicator>>::const_iterator it = newApplicators.begin(); it != newApplicators.end(); ++it)
  {
    cp.pDBAppl = *it;
    if (!cp.pDBAppl)
      continue;

    CObj<PFBaseApplicator> pApplicator(CreateApplicator(cp));
    if (pApplicator)
    {
      pApplicator->AddFlags(flagsForApplicators);
      AddApplicator(pApplicator);
    }
  }
}

void PFDispatch::UpgradeBeforeApply()
{
  if (!IsValid(pSender))
    return;

  vector<vector<NDb::Ptr<NDb::BaseApplicator>>> newApplicators;
  vector<vector<NDb::Ptr<NDb::BaseApplicator>>> newPersistentApplicators;
  vector<PFAbilityData*> upgraders;

  PFApplAbilityUpgrade::Ring const &upgrades = pSender->GetAbilityUpgradeApplicators();
  bool useOriginal = true;

  for (ring::Range<PFApplAbilityUpgrade::Ring> it(upgrades); it;)
  {
    newApplicators.push_back();
    newPersistentApplicators.push_back();

    PFApplAbilityUpgrade *pUpgrade = &(*it);
    ++it;
    upgraders.push_back(pUpgrade->GetAbility() ? pUpgrade->GetAbility()->GetData() : 0);
    pUpgrade->UpgradeAbilityApplicators(pAbility ? pAbility->GetData() : 0, newApplicators.back(), newPersistentApplicators.back(), useOriginal);
  }

  if (!useOriginal)
  {
    while (!applicators.empty())
      applicators.remove(applicators.first());
  }

  for (int i = 0, count = upgraders.size(); i < count; ++i)
  {
    AddApplicators(newApplicators[i], upgraders[i]);
    AddPersistentApplicators(newPersistentApplicators[i], upgraders[i]);
  }
}

void PFDispatch::_ApplyInternal()
{
  if (target.IsUnit() && IsValid(target.GetUnit()) && !target.GetUnit()->CanApplyDispatch() && (!pDBDispatch || !pDBDispatch->alwaysApply))
  {
    state = STATE_WAIT_TO_APPLY;
    return;
  }

  if (!IsValid(pAbility) || !pAbility->GetData())
  {
    state = STATE_APPLIED;
    return;
  }

  if (!originalTarget.IsUnit() || (pAbility->GetData()->IsTargetValid(target) && target.IsUnit() && IsValid(target.GetUnit()) && target.GetUnit()->OnDispatchApply(*this)))
  {
    while (!applicators.empty())
    {
      CObj<PFBaseApplicator> pHolder(applicators.first());
      PFBaseApplicator::AppliedRing::remove(pHolder);
      ActivateApplicator(pHolder, pAbility);
    }
  }
  else if (!IsValid(pParentApplicator))
  {
    pAbility->NotifyAbilityEnd();
  }

  state = STATE_APPLIED;
}

void PFDispatch::Apply( bool playApplyEffect )
{
  (void)playApplyEffect;
  _ApplyInternal();

  if (state == STATE_APPLIED && pDBDispatch && pDBDispatch->dieAfterApply)
    Die();
}

bool PFDispatch::IsBaseAttack() const
{
  return pUpgraderAbilityData ? pUpgraderAbilityData->IsAutoAttack() : ( IsValid( pAbility ) && pAbility->GetData() && pAbility->GetData()->IsAutoAttack() );
}

void PFDispatch::Start()
{
  if ( state != STATE_INIT )
    return;

  if (target.IsUnit() && IsValid(target.GetUnit()))
  {
    targetFaction = target.GetUnit()->GetFaction();
    targetWarfogFaction = target.GetUnit()->GetWarfogFaction();
  }

  state = STATE_PROCEED;
  OnStart();
}

bool PFDispatch::Step(float dtInSeconds)
{
  (void)dtInSeconds;
  if ( IsObjectDead() )
    return false;
  if ( state == STATE_PROCEED || state == STATE_READY_TO_APPLY || state == STATE_WAIT_TO_APPLY )
    Apply(false);
  return !IsObjectDead();
}

float PFDispatch::RetrieveParam( const ExecutableFloatString& par, float defaultValue )
{
  IUnitFormulaPars * const pOriginalTarget = originalTarget.IsUnit() ? originalTarget.GetUnit().GetPtr() : pSender.GetPtr();
  IMiscFormulaPars * const pMisc = IsValid( pParentApplicator ) ? (IMiscFormulaPars*)pParentApplicator.GetPtr() : ( IsValid( pAbility ) ? (IMiscFormulaPars*)pAbility->GetData() : 0 );
  return par( pSender, pOriginalTarget, pMisc, defaultValue );
}

void PFDispatch::OpenWarFog()
{
  if ( !IsValid( pAbility ) || !IsValid( pSender ) || pAbility->GetWarFogOpened( targetWarfogFaction ) )
    return;

  if ( pAbility->GetFlags() & NDb::ABILITYFLAGS_DONTOPENWARFOG )
    return;

  PFAbilityData const* const pAbilityData = pAbility->GetData();
  NDb::Ability const* const pAbilityDesc = pAbilityData ? pAbilityData->GetDBDesc() : 0;
  if ( !pAbilityDesc )
    return;

  float warFogRemoveTime = pAbilityDesc->warFogRemoveTime;
  float warFogRemoveRadius = pAbilityDesc->warFogRemoveRadius;

  if ( warFogRemoveTime <= 0.0f || warFogRemoveRadius <= 0.0f )
  {
    PFWorld const* const pWorld = GetWorld();
    PFAIWorld const* const pAIWorld = pWorld ? pWorld->GetAIWorld() : 0;
    if ( pAIWorld )
    {
      const NDb::AILogicParameters& aiParams = pAIWorld->GetAIParameters();
      if ( warFogRemoveTime <= 0.0f )
        warFogRemoveTime = aiParams.warFogRemoveTime;
      if ( warFogRemoveRadius <= 0.0f )
        warFogRemoveRadius = aiParams.warFogRemoveRadius;
    }
  }

  if ( warFogRemoveTime > 0.0f && warFogRemoveRadius > 0.0f )
  {
    pSender->OpenWarFog( targetWarfogFaction, warFogRemoveTime, warFogRemoveRadius );
    pAbility->SetWarFogOpened( targetWarfogFaction );
  }
}

void PFDispatch::Reset()
{
  PFWorldObjectBase::Reset();
  if ( target.IsObject() && IsObjectValid( target.GetObject() ) )
    target.GetObject()->DoReset();
  if ( originalTarget.IsObject() && IsObjectValid( originalTarget.GetObject() ) )
    originalTarget.GetObject()->DoReset();
  if ( source.IsObject() && IsObjectValid( source.GetObject() ) )
    source.GetObject()->DoReset();
  state = STATE_INIT;
}

void PFDispatch::SetNewTarget( const Target & _target )
{
  target = _target;
  originalTarget = _target;

  if ( target.IsUnitValid( true ) )
  {
    targetFaction = target.GetUnit()->GetFaction();
    targetWarfogFaction = target.GetUnit()->GetWarfogFaction();
  }

  PFBaseApplicator * const last = applicators.last();
  for (PFBaseApplicator *applicator = applicators.first(); applicator != last; applicator = applicators.next(applicator))
  {
    applicator->SetTarget( _target );
    applicator->Init();
  }
}

void PFDispatch::OnMissed() const
{
  if ( IsValid( pParentApplicator ) )
    pParentApplicator->OnDispatchMissed( this );
}

bool PFDispatch::CheckEffectEnabled( const PF_Core::BasicEffect &effect )
{
  bool enabled = true;

  if ( effect.GetDBDesc() && !effect.GetDBDesc()->enableCondition.IsEmpty() )
  {
    IUnitFormulaPars* const pFirst = GetSender();
    IUnitFormulaPars* pSecond = 0;
    IMiscFormulaPars* const pMisc = IsValid( GetAbility() ) ? (IMiscFormulaPars*)GetAbility()->GetData() : 0;

    if ( GetTarget().IsUnit() )
      pSecond = GetTarget().GetUnit();

    enabled = effect.GetDBDesc()->enableCondition->condition( pFirst, pSecond, pMisc, enabled );
  }

  return enabled;
}

void PFDispatch::RegisterAggression()
{
  if ( state != STATE_APPLIED )
    return;

  if ( !IsValid( pSender ) || !IsValid( pAbility ) || !pAbility->GetData() )
    return;

  if ( !target.IsUnitValid( true ) )
    return;

  if ( pAbility->GetData()->IsInteractionAbility() )
    return;

  if ( pSender->GetFaction() != targetFaction )
    pSender->SetLastAttackDataEx( target.GetUnit(), IsBaseAttack(), IsAggressive(), targetFaction );

  if ( pSender->GetWarfogFaction() != targetWarfogFaction )
    OpenWarFog();
}

bool PFDispatch::IsAggressive() const
{
  return IsValid( pDBDispatch ) && ( pDBDispatch->flags & NDb::DISPATCHFLAGS_AGGRESSIVE );
}

} // namespace NWorld

REGISTER_WORLD_OBJECT_NM(PFDispatch, NWorld)

#else

#include "PFBaseUnit.h"
#include "PFDispatch.h"
#include "PFLogicDebug.h"

#include "PFAbilityInstance.h"
#include "PFAbilityData.h"

#include "PFDispatchFactory.h"

#ifndef VISUAL_CUTTED
#include "PFClientDispatch.h"
#else
#include "../Game/PF/Audit/ClientStubs.h"
#endif

#include "WarFog.h"
#include "TileMap.h"
#include "PFApplSpecial.h"
#include "PFAIWorld.h"

namespace NWorld
{

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFDispatch::OnDie()
{
  RegisterAggression();

  Clear(applicators);

  pAbility = NULL;
  pSender  = NULL;
}

void PFDispatch::AddApplicators( vector<NDb::Ptr<NDb::BaseApplicator>> const& newApplicators, PFAbilityData* upgrader /* = NULL */ )
{
  PFApplCreatePars cp(pAbility, target, pParentApplicator, this, false, upgrader );

  for ( vector<NDb::Ptr<NDb::BaseApplicator>>::const_iterator iAppl = newApplicators.begin(), iEnd = newApplicators.end(); iAppl != iEnd; ++iAppl )
  {
    cp.pDBAppl = *iAppl;
    NI_VERIFY(cp.pDBAppl, NStr::StrFmt( "Void applicator in list %s ", pDBDispatch->GetDBID().GetFormatted().c_str() ), continue; );
    CObj<PFBaseApplicator> pApplicator(CreateApplicator(cp));
    if (pApplicator)
    {
      pApplicator->AddFlags(flagsForApplicators);
      AddApplicator(pApplicator);
    }
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFDispatch::UpgradeBeforeApply()
{
  vector<vector<NDb::Ptr<NDb::BaseApplicator>>> newApplicators;
  vector<vector<NDb::Ptr<NDb::BaseApplicator>>> newPersistentApplicators;
  vector<PFAbilityData*> upgraders;

  PFApplAbilityUpgrade::Ring const & upgrades = pSender->GetAbilityUpgradeApplicators();
  bool useOriginal = true;

  for (ring::Range<PFApplAbilityUpgrade::Ring> it(upgrades); it;)
  {
    newApplicators.push_back();
    newPersistentApplicators.push_back();

    PFApplAbilityUpgrade *pUpgrade = &(*it);
    ++it;
    upgraders.push_back( pUpgrade->GetAbility()->GetData() );
    pUpgrade->UpgradeAbilityApplicators( pAbility->GetData(), newApplicators.back(), newPersistentApplicators.back(), useOriginal );
  }

  // Upgrading dispatch applicators by PFApplAbilityUpgrade, attached to sender, before apply
  // newApplicators contains new applicators to inject, useOriginal means if we not need to drop original applicators before add new
  
  if ( !useOriginal )
  {
    while (!applicators.empty())
      applicators.remove(applicators.first());
  }

  for ( int i = 0, sz = upgraders.size(); i < sz; ++i )
  {
    AddApplicators( newApplicators[i], upgraders[i] );
    AddPersistentApplicators( newPersistentApplicators[i], upgraders[i] );
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFDispatch::_ApplyInternal()
{
  if ( target.IsUnit() && !target.GetUnit()->CanApplyDispatch() && !GetDBBase()->alwaysApply )
  {
    state = STATE_WAIT_TO_APPLY;
    return;
  }

  NI_VERIFY(IsValid(pAbility) && IsValid(pAbility->GetData()), "Ability is not valid", state = STATE_APPLIED; return);
  if ( !originalTarget.IsUnit() || (pAbility->GetData()->IsTargetValid(target) && target.IsUnit()
    && target.GetUnit()->OnDispatchApply(*this) ) )
  {
    LogDispatch(*this, "DISPATCH APPLY\n");

    PFBaseApplicator * const last = applicators.last();
    for (PFBaseApplicator *applicator = applicators.first(); applicator != last && !applicators.empty();)
    {
      PFBaseApplicator *applicatorNext = applicators.next(applicator);
      CObj<PFBaseApplicator> pHolder(applicator); 
      PFBaseApplicator::AppliedRing::remove(pHolder);
      ActivateApplicator(pHolder, pAbility);
      applicator = applicatorNext;
    }

    NI_ASSERT(applicators.empty(), "Internal error");
  }
  else if ( !IsValid(pParentApplicator) ) // dispatch created by ability, not an applicator
  {
    pAbility->NotifyAbilityEnd();
  }

  state = STATE_APPLIED;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFDispatch::Apply( bool playApplyEffect /* = true*/ )
{
  NI_ASSERT( state == STATE_PROCEED || state == STATE_READY_TO_APPLY, "Dispatch can't be applied right now" );

  // play apply effect
  if ( playApplyEffect && ( target.IsUnit() && IsUnitValid(target.GetUnit()) || !originalTarget.IsUnit() ) )
  {
    if (GetDBBase())
    {
      NGameX::PFDispatchPlayApplyEffect(this, GetWorld(), target);
    }
  }

  _ApplyInternal();

  if (state == STATE_APPLIED && pDBDispatch->dieAfterApply)
  {
    Die();
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool PFDispatch::IsBaseAttack() const
{
  return pUpgraderAbilityData ? pUpgraderAbilityData->IsAutoAttack() : pAbility->GetData()->IsAutoAttack();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFDispatch::Start()
{
  NI_VERIFY( state == STATE_INIT, "Dispatch was already started!", return; );
  
  // Save original target's faction for use on dispach's death, because 
  // some applicators the dispatch applies may change target's faction
  if (target.IsUnit() && IsValid(target.GetUnit()))
  {
    targetFaction = target.GetUnit()->GetFaction();
    targetWarfogFaction = target.GetUnit()->GetWarfogFaction();
  }

  state = STATE_PROCEED;
  LogDispatch(*this, "DISPATCH STARTED\n");
  OnStart();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool PFDispatch::Step(float dtInSeconds)
{
	if ( IsObjectDead() )
		return false;

  if (state == STATE_WAIT_TO_APPLY)
  {
    _ApplyInternal();
    
    if (state == STATE_APPLIED && pDBDispatch->dieAfterApply)
    {
      Die();
    }
  }
  return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float PFDispatch::RetrieveParam( const ExecutableFloatString& par, float defaultValue )
{
  IUnitFormulaPars * const pOriginalTarget = originalTarget.IsUnit() ? originalTarget.GetUnit().GetPtr() : pSender.GetPtr();
  return par( pSender
    , pOriginalTarget
    , IsValid( pParentApplicator ) ? (IMiscFormulaPars*)pParentApplicator.GetPtr() : (IMiscFormulaPars*)pAbility->GetData()
    , defaultValue ); 
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFDispatch::OpenWarFog()
{
  if (!pAbility || !pSender || pAbility->GetWarFogOpened(targetWarfogFaction))
    return;

  if (pAbility->GetFlags() & NDb::ABILITYFLAGS_DONTOPENWARFOG)
    return;

  if (PFAbilityData const * pAbilityData = pAbility->GetData())
  {
    if (NDb::Ability const * pAbilityDesc = pAbilityData->GetDBDesc())
    {
      float warFogRemoveTime = pAbilityDesc->warFogRemoveTime;
      float warFogRemoveRadius = pAbilityDesc->warFogRemoveRadius;

      // If ability specific values aren't set - try to get defaults from AI Logic Parameters
      if (warFogRemoveTime <= 0.0f || warFogRemoveRadius <= 0.0f)
      {
        if (const PFWorld * pWorld = GetWorld())
        {
          if (const NWorld::PFAIWorld * pAIWorld = pWorld->GetAIWorld())
          {
            const NDb::AILogicParameters * pAILogicParameters = &pAIWorld->GetAIParameters();
            if (warFogRemoveTime <= 0.0f)
              warFogRemoveTime = pAILogicParameters->warFogRemoveTime;
            if (warFogRemoveRadius <= 0.0f)
              warFogRemoveRadius = pAILogicParameters->warFogRemoveRadius;
          }
        }
      }

      if (warFogRemoveTime > 0.0f && warFogRemoveRadius > 0.0f)
      {
        pSender->OpenWarFog(targetWarfogFaction, warFogRemoveTime, warFogRemoveRadius);
        pAbility->SetWarFogOpened(targetWarfogFaction);
      }
    }
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFDispatch::Reset()
{
	PFWorldObjectBase::Reset();
	if ( target.IsObject() && IsObjectValid(target.GetObject()) )
		target.GetObject()->DoReset();
	if ( originalTarget.IsObject() && IsObjectValid(originalTarget.GetObject()) )
		originalTarget.GetObject()->DoReset();
	if ( source.IsObject() && IsObjectValid(source.GetObject()) )
		source.GetObject()->DoReset();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFDispatch::SetNewTarget( const Target & _target )
{
  NI_ASSERT( state < STATE_READY_TO_APPLY, "Trying to set new target for an arrived dispatch" );

  target = _target;
  originalTarget = _target;

  if ( target.IsUnitValid( true ) )
  {
    targetFaction = target.GetUnit()->GetFaction();
    targetWarfogFaction = target.GetUnit()->GetWarfogFaction();
  }

  PFBaseApplicator * const last = applicators.last();
  for (PFBaseApplicator *applicator = applicators.first(); applicator != last; applicator = applicators.next(applicator) )
  {
    applicator->SetTarget( _target );
    applicator->Init();
  }
}

void PFDispatch::OnMissed() const
{
  if ( IsValid( pParentApplicator ) )
    pParentApplicator->OnDispatchMissed( this );
}

bool PFDispatch::CheckEffectEnabled( const PF_Core::BasicEffect &effect )
{
  bool bEnable = true;

  if(effect.GetDBDesc() && !effect.GetDBDesc()->enableCondition.IsEmpty())
  {
    IUnitFormulaPars* pFirst = this->GetSender();
    IUnitFormulaPars* pSecond = NULL;
    IMiscFormulaPars* pMisc = (IMiscFormulaPars*)this->GetAbility()->GetData();

    if(this->GetTarget().IsUnit())
      pSecond = this->GetTarget().GetUnit();

    bEnable = effect.GetDBDesc()->enableCondition->condition(pFirst,pSecond, pMisc, bEnable);
  }

  return bEnable;
}

void PFDispatch::RegisterAggression()
{
  if (state != STATE_APPLIED)
    return;

  if (!IsValid(pSender))
    return;
  if (!target.IsUnitValid(true))
    return;

  if (pAbility->GetData()->IsInteractionAbility())
    return;

  if (pSender->GetFaction() != targetFaction)
    pSender->SetLastAttackDataEx(target.GetUnit(), IsBaseAttack(), IsAggressive(), targetFaction);

  if (pSender->GetWarfogFaction() != targetWarfogFaction)
    OpenWarFog();
}

bool PFDispatch::IsAggressive() const
{
  if (!IsValid(pDBDispatch))
    return false;

  return !!(pDBDispatch->flags & NDb::DISPATCHFLAGS_AGGRESSIVE);
}

} // namespace NWorld

REGISTER_WORLD_OBJECT_NM(PFDispatch, NWorld)

#endif
