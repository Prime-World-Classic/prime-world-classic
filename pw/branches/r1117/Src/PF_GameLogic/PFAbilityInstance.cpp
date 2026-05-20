#include "stdafx.h"

#if defined( PW_LINUX_NULL_RENDER )

#include "PFAbilityInstance.h"
#include "PFAbilityData.h"
#include "PFBaseUnit.h"
#include "PFDispatch.h"
#include "PFDispatchFactory.h"

namespace NWorld
{

PFAbilityInstance::PFAbilityInstance( const CObj<PFAbilityData>& _pAbilityData, const Target& _target, bool _passive )
  : PFWorldObjectBase( _pAbilityData ? _pAbilityData->GetWorld() : 0, 0 )
  , pAbilityData( _pAbilityData )
  , pOwner( _pAbilityData ? _pAbilityData->GetOwner() : 0 )
  , activeApplicatorsCount( 0 )
  , abilityApplicatorsFinished( false )
  , target( _target )
  , passive( _passive )
  , rank( _pAbilityData ? _pAbilityData->rank : 0 )
  , activateDelay( 0.0f )
  , clientActivateDelay( 0.0f )
  , castTime( 0.0f )
  , warfogOpened( NDb::KnownEnum<NDb::EFaction>::SizeOf(), false )
  , castPosition( VNULL2 )
  , castMasterPosition( VNULL2 )
{
  if ( IsValid( pAbilityData ) )
  {
    activateDelay = passive ? 0.0f : pAbilityData->GetTimeOffset();
    clientActivateDelay = passive ? 0.0f : pAbilityData->GetTimeOffset( true );
    castTime = passive ? 0.0f : pAbilityData->GetWorkTime();
  }

  if ( IsValid( pOwner ) )
  {
    castPosition = pOwner->GetPos();
    castMasterPosition = IsValid( pOwner->GetMasterUnit() ) ? pOwner->GetMasterUnit()->GetPos() : pOwner->GetPos();
  }
}

void PFAbilityInstance::ApplyPassive( bool permanentStatModsOnly )
{
  if ( !IsValid( pOwner ) || !IsValid( pAbilityData ) || !pAbilityData->pDBDesc )
  {
    abilityApplicatorsFinished = true;
    return;
  }

  PFApplCreatePars cp(this, target, 0, 0, true);

  for ( vector<NDb::Ptr<NDb::BaseApplicator>>::const_iterator iAppl = pAbilityData->pDBDesc->passiveApplicators.begin(), iEnd = pAbilityData->pDBDesc->passiveApplicators.end(); iAppl != iEnd; ++iAppl )
  {
    if ( *iAppl && ( !permanentStatModsOnly || (*iAppl)->GetObjectTypeID() == NDb::PermanentStatModApplicator::typeId ) )
    {
      cp.pDBAppl = *iAppl;
      CObj<PFBaseApplicator> pApplicator(CreateApplicator(cp));
      ActivateApplicator(pApplicator, this);
    }
  }

  abilityApplicatorsFinished = true;
}

void PFAbilityInstance::Cancel()
{
  activateDelay = -1.0f;
  castTime = -1.0f;
  dispatch.Cancel();
  if ( IsValid( pOwner ) )
    pOwner->SetChannellingProgress( 0.0f );
}

void PFAbilityInstance::Activate()
{
  if ( passive )
    return;

  if ( IsValid( pAbilityData ) && pAbilityData->IsTargetValid( target ) )
  {
    if ( pAbilityData->IsMultiState() )
      pAbilityData->SwitchOn();

    if ( !pAbilityData->IsMultiState() )
      pAbilityData->RecalculateAndRestartCooldown();

    dispatch.Start();
    pAbilityData->OnDispatchStarted();
    pAbilityData->SpendMana();
    pAbilityData->NotifyCastProcessed();

    if ( IsValid( pOwner ) && GetData()->abilityType != NDb::ABILITYTYPEID_SPECIAL )
      pOwner->StartGlobalCooldown();
  }
  else
  {
    NotifyAbilityEnd();
  }

  activateDelay = -1.0f;
  abilityApplicatorsFinished = true;
}

bool PFAbilityInstance::ApplyToTarget()
{
  if ( !IsValid( pAbilityData ) || !IsValid( pOwner ) || !pAbilityData->GetDBDesc() )
    return false;

  Target const source( pOwner );
  PFDispatch *pDispatch = CreateDispatch( this, NULL, source, target, pAbilityData->GetDBDesc(), 0, false, clientActivateDelay );
  if ( !pDispatch )
    return false;

  CObj<PFDispatch> dispatchGuard(pDispatch);
  if ( activateDelay < EPS_VALUE )
  {
    if ( IsValid( pAbilityData ) && pAbilityData->IsTargetValid( target ) )
    {
      if ( pAbilityData->IsMultiState() )
        pAbilityData->SwitchOn();

      if ( !pAbilityData->IsMultiState() )
        pAbilityData->RecalculateAndRestartCooldown();

      if ( pDispatch && !pDispatch->IsObjectDead() )
      {
        pDispatch->PFDispatch::Start();
        pDispatch->PFDispatch::Step(0.0f);
      }

      pAbilityData->OnDispatchStarted();
      pAbilityData->SpendMana();
      pAbilityData->NotifyCastProcessed();

      if ( IsValid( pOwner ) && GetData()->abilityType != NDb::ABILITYTYPEID_SPECIAL )
        pOwner->StartGlobalCooldown();
    }
    else
    {
      NotifyAbilityEnd();
    }

    activateDelay = -1.0f;
    abilityApplicatorsFinished = true;
    return true;
  }

  dispatch.Attach(pDispatch);

  return true;
}

void PFAbilityInstance::OnDestroyContents()
{
  PFWorldObjectBase::OnDestroyContents();
  Remove();
  pAbilityData = NULL;
}

void PFAbilityInstance::Remove()
{
  if ( IsValid( pAbilityData ) )
  {
    if ( GetActiveApplicatorsCount() && target.IsUnitValid( true ) )
      RemoveApplicatorsFrom( target.GetUnit() );

    pAbilityData->OnAbilityInstanceRemoved(this);
  }
}

bool PFAbilityInstance::Update(float dt)
{
  castTime -= dt;
  if ( !IsActivated() )
  {
    activateDelay -= dt;
    if ( activateDelay < EPS_VALUE )
      Activate();
  }
  return IsFinished();
}

bool PFAbilityInstance::IsFinished() const
{
  return IsCastFinished() && abilityApplicatorsFinished && activeApplicatorsCount <= 0;
}

bool PFAbilityInstance::IsCastFinished() const
{
  return IsActivated() && IsCastDone();
}

void PFAbilityInstance::RemoveApplicatorsFrom(CPtr<PFBaseUnit> const& pUnit) const
{
  if ( !IsValid( pUnit ) )
    return;

  struct RemoveApplicatorFunc
  {
    explicit RemoveApplicatorFunc(PFAbilityInstance const *pAbility)
      : pAbility(pAbility)
    {
    }

    void operator()(CObj<PFBaseApplicator> &pAppl)
    {
      if (pAppl && pAppl->GetAbility() == pAbility)
      {
        pAppl->Stop();
        MemorizeApplicator( pAppl );
      }
    }

    PFAbilityInstance const *pAbility;
  } func(this);

  pUnit->ForAllAppliedApplicatorsSafe(func);
}

void PFAbilityInstance::NotifyChannelingCreateStop(bool fire)
{
  (void)fire;
}

void PFAbilityInstance::LogAbilityUsed() const
{
}

void PFAbilityInstance::Interrupt()
{
  castTime = -1.0f;
  activateDelay = -1.0f;
}

bool PFAbilityInstance::IsOn() const
{
  return IsValid( pAbilityData ) && pAbilityData->IsOn();
}

float PFAbilityInstance::GetScale() const
{
  return IsValid( pAbilityData ) ? pAbilityData->GetScale() : 1.0f;
}

bool PFAbilityInstance::GetWarFogOpened(NDb::EFaction faction) const
{
  return faction >= 0 && faction < warfogOpened.size() ? warfogOpened[faction] : false;
}

void PFAbilityInstance::SetWarFogOpened(NDb::EFaction faction, bool opened)
{
  if ( faction >= 0 && faction < warfogOpened.size() )
    warfogOpened[faction] = opened;
}

ELookKind PFAbilityInstance::IsCasterShouldLookAtTarget() const
{
  return DontLook;
}

void PFAbilityInstance::NotifyApplicatorStopped( PFBaseUnit * target )
{
  (void)target;
  if ( activeApplicatorsCount > 0 )
    --activeApplicatorsCount;
}

void PFAbilityInstance::NotifyApplicatorStarted( PFBaseUnit * target )
{
  (void)target;
  ++activeApplicatorsCount;
}

void PFAbilityInstance::NotifyAbilityEnd()
{
  abilityApplicatorsFinished = true;
  if ( IsValid( pOwner ) && ( GetFlags() & NDb::ABILITYFLAGS_WAITFORCHANNELING ) != 0 )
    pOwner->SetChannellingProgress( 0.0f );
}

unsigned int PFAbilityInstance::GetFlags() const
{
  return IsValid( pAbilityData ) ? pAbilityData->GetFlags() : 0;
}

void PFAbilityInstance::PFDispatchHolder::Start()
{
  CObj<PFDispatch> dispatchGuard(pDispatch.GetValidPtr());
  if ( IsValid(dispatchGuard) && !dispatchGuard->IsObjectDead() )
  {
    dispatchGuard->PFDispatch::Start();
    dispatchGuard->PFDispatch::Step(0.0f);
    pDispatch = NULL;
  }
}

void PFAbilityInstance::PFDispatchHolder::Cancel()
{
  if ( IsValid(pDispatch) )
  {
    pDispatch->Cancel();
    pDispatch = NULL;
  }
}

} // namespace NWorld

REGISTER_WORLD_OBJECT_NM(PFAbilityInstance, NWorld);

#else

#include "DBConsumable.h"
#include "PFDispatch.h"
#include "PFAbilityInstance.h"
#include "PFAbilityData.h"
#include "PFBaseUnit.h"
#include "PFDispatchFactory.h"
#include "PFHero.h"
#include "PFHeroStatistics.h"
#include "SessionEventType.h"
#include "PFTalent.h"
#include "../System/Crc32Checksum.h"
#include "GameLogicStatisticsTypes.h"

namespace NWorld
{

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
PFAbilityInstance::PFAbilityInstance( const CObj<PFAbilityData>& _pAbilityData, const Target& _target, bool _passive )
: PFWorldObjectBase( _pAbilityData->GetWorld(), 0 )
, pOwner( _pAbilityData->GetOwner() )
, activeApplicatorsCount( 0 )
, pAbilityData( _pAbilityData )
, target( _target )
, rank( _pAbilityData->rank )
, passive( _passive )
, abilityApplicatorsFinished( false )
, warfogOpened(NDb::KnownEnum<NDb::EFaction>::SizeOf(), false)
, castPosition( _pAbilityData->GetOwner()->GetPos() )
{
  NI_ASSERT( pAbilityData.GetPtr(), "Invalid ability data!" );

  activateDelay =       passive ? 0.0f : pAbilityData->GetTimeOffset();
  clientActivateDelay = passive ? 0.0f : pAbilityData->GetTimeOffset( true );
  castTime =            passive ? 0.0f : pAbilityData->GetWorkTime();

  const CPtr<NWorld::PFBaseUnit> pOwner = _pAbilityData->GetOwner();
  if ((pOwner->GetUnitType() == NDb::UNITTYPE_SUMMON || pOwner->GetUnitType() == NDb::UNITTYPE_DUMMYUNIT) &&
    IsValid(pOwner->GetMasterUnit()))
  {
    castMasterPosition = pOwner->GetMasterUnit()->GetPos();
  }
  else
  {
    castMasterPosition = pOwner->GetPos();
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFAbilityInstance::ApplyPassive( bool permanentStatModsOnly )
{
  NI_ASSERT( IsValid( pOwner ), "Unit must be valid on attach passive part of ability!" );
  
  PFApplCreatePars cp(this, target, 0, 0, true);

  for ( vector<NDb::Ptr<NDb::BaseApplicator>>::const_iterator iAppl = pAbilityData->pDBDesc->passiveApplicators.begin(), iEnd = pAbilityData->pDBDesc->passiveApplicators.end(); iAppl != iEnd; ++iAppl )
  {
		if ( *iAppl )
		{
			if ( !permanentStatModsOnly || (*iAppl)->GetObjectTypeID() == NDb::PermanentStatModApplicator::typeId )
			{
				cp.pDBAppl = *iAppl;
				CObj<PFBaseApplicator> pApplicator(CreateApplicator(cp));
				ActivateApplicator(pApplicator, this );
			}
		}
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFAbilityInstance::Cancel()
{
  activateDelay = -1.0f;
  dispatch.Cancel();
  pOwner->SetChannellingProgress( 0.0f );
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFAbilityInstance::Activate()
{
  if ( passive )
  {
    return;
  }

  if ( pAbilityData->IsTargetValid( target ) )
  {
    if ( pAbilityData->IsMultiState() )
      pAbilityData->SwitchOn();

    // Restart cooldown
    if ( !pAbilityData->IsMultiState() )
    {
      pAbilityData->RecalculateAndRestartCooldown();
    }

    dispatch.Start();
    pAbilityData->OnDispatchStarted();
    
    // Workaround to prevent early state quitting 
    if ( ( GetFlags() & NDb::ABILITYFLAGS_WAITFORCHANNELING ) != 0 )
    {
      pOwner->SetChannellingProgress( 0.0001f );
    }

    // Take mana and log only if there is no channeling creation, which may be cancelled by player, or interrupted
    if ( ( GetFlags() & NDb::ABILITYFLAGS_CHANNELINGCREATE ) == 0 )
    {
      NDb::EBaseUnitEvent eventType = NDb::BASEUNITEVENT_CASTMAGIC;
      if ( pAbilityData->GetEventTypeByAbilityTypeId( eventType ) )
        pOwner->EventHappened(PFBaseUnitUseAbilityEvent(eventType, this));

      pAbilityData->SpendMana();
      LogAbilityUsed();

      pAbilityData->NotifyCastProcessed();
    }
    
    activateDelay = -1.0f;
    // special abilities should not start global cd
    if ( GetData()->abilityType != NDb::ABILITYTYPEID_SPECIAL )
      pOwner->StartGlobalCooldown();
  }
  else
  {
    NotifyAbilityEnd();
  }

  activateDelay = -1.0f;
  dispatch.Cancel();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool PFAbilityInstance::ApplyToTarget()
{
  Target const source( pOwner );
  PFDispatch *pDispatch = CreateDispatch( this, NULL, source, target, pAbilityData->GetDBDesc(), 0, false, clientActivateDelay );
  if (!pDispatch)
    return false;

  dispatch.Attach(pDispatch);

  if( activateDelay < EPS_VALUE )
    Activate();

  return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFAbilityInstance::OnDestroyContents()
{
	PFWorldObjectBase::OnDestroyContents();
  Remove();
  pAbilityData = NULL;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFAbilityInstance::Remove()
{
  if ( pAbilityData )
  {
    if ( GetActiveApplicatorsCount() )
    {
      if ( target.IsUnitValid( true ) )
        RemoveApplicatorsFrom( target.GetUnit() );
    }

    pAbilityData->OnAbilityInstanceRemoved(this);
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool PFAbilityInstance::Update(float dt)
{
  castTime      -= dt;

  if (!IsActivated())
  {
    activateDelay -= dt;

    if( activateDelay < EPS_VALUE ) 
      Activate();
  }

	return IsCastFinished() && !GetActiveApplicatorsCount();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool PFAbilityInstance::IsFinished() const
{
  return IsCastFinished() && abilityApplicatorsFinished;
}

//////////////////////////////////////////////////////////////////////////
bool PFAbilityInstance::IsCastFinished() const
{
  return IsActivated() && IsCastDone();
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFAbilityInstance::RemoveApplicatorsFrom(CPtr<PFBaseUnit> const& pUnit) const
{
  struct RemoveApplicatorFunc
  {
    RemoveApplicatorFunc(PFAbilityInstance const *pAbility)
      : pAbility(pAbility)
    {
    }

    void operator()(CObj<PFBaseApplicator> &pAppl) 
    {
      if (pAppl->GetAbility() == pAbility)
      {
        pAppl->Stop();
        MemorizeApplicator( pAppl );
      }
    }
    PFAbilityInstance const *pAbility;
  } func(this);

  pUnit->ForAllAppliedApplicatorsSafe(func);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFAbilityInstance::NotifyChannelingCreateStop(bool fire)
{
  if (fire)
  {
    NDb::EBaseUnitEvent eventType = NDb::BASEUNITEVENT_CASTMAGIC;
    if ( pAbilityData->GetEventTypeByAbilityTypeId( eventType ) )
      pOwner->EventHappened(PFBaseUnitUseAbilityEvent(eventType, this));

    LogAbilityUsed();
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFAbilityInstance::LogAbilityUsed() const
{
  if (!IsValid(pOwner) || !pOwner->IsHero()) // Log only hero talents and consumables
    return;

  if (PFBaseHero * pHero = dynamic_cast<PFBaseHero *>(pOwner.GetPtr()))
  {
    if (PFTalent const * pTalent = dynamic_cast<PFTalent const *>(pAbilityData.GetPtr()))
    {
      if (pTalent->GetTalentDesc())
      {
        pHero->LogSessionEvent(SessionEventType::TalentUsed, 
          Crc32Checksum().AddString( pTalent->GetTalentDesc()->persistentId.c_str() ).Get());
      }
    }
    else if (GetData()->GetAbilityTypeId() == NDb::ABILITYTYPEID_CONSUMABLE )
    {
      if ( NDb::Consumable const* pDBConsumable = dynamic_cast<NDb::Consumable const*>(GetData()->GetDBDesc()) )
      {
        StatisticService::RPC::SessionEventInfo eventParams;
        eventParams.intParam1 = pDBConsumable->GetDBID().GetHashKey();
        pHero->LogSessionEvent( SessionEventType::ConsumableUsed, eventParams );

        if ( pDBConsumable->isPotion )
        {
          pHero->GetHeroStatistics()->AddUsedPotion();
        }
      }
    }
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFAbilityInstance::Interrupt()
{
  castTime      = -1.0f;
  activateDelay = -1.0f;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool PFAbilityInstance::IsOn() const
{
  return pAbilityData->IsOn();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float PFAbilityInstance::GetScale() const
{
  return pAbilityData->GetScale();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool PFAbilityInstance::GetWarFogOpened(NDb::EFaction faction) const
{
  NI_VERIFY( faction >= 0 && faction < warfogOpened.size(), 
    "Wrong faction selected, while trying to open war fog!", return false; );

  return warfogOpened[faction];
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFAbilityInstance::SetWarFogOpened(NDb::EFaction faction, bool opened /*= true*/)
{
  NI_VERIFY( faction >= 0 && faction < warfogOpened.size(), 
    "Wrong faction selected, while trying to set war fog opened flag!", return; );

  warfogOpened[faction] = opened;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
ELookKind PFAbilityInstance::IsCasterShouldLookAtTarget() const
{
  if ( target.IsUnit() && target.GetUnit() == GetOwner() || ( GetFlags() & NDb::ABILITYFLAGS_FOCUSONTARGET ) == 0 )
    return DontLook;

  if ( ( GetFlags() & NDb::ABILITYFLAGS_FOCUSINSTANTLY ) != 0 )
    return TurnInstantly;

  return Turn;
}

//////////////////////////////////////////////////////////////////////////
void PFAbilityInstance::NotifyApplicatorStopped( PFBaseUnit * target )
{
  activeApplicatorsCount--; 
  NI_ASSERT(activeApplicatorsCount >= 0, "Applicator control logic failed");
}

//////////////////////////////////////////////////////////////////////////
void PFAbilityInstance::NotifyApplicatorStarted( PFBaseUnit * target )
{
  activeApplicatorsCount++;
}

//////////////////////////////////////////////////////////////////////////
void PFAbilityInstance::NotifyAbilityEnd()
{
  abilityApplicatorsFinished = true;

  if ( ( GetFlags() & NDb::ABILITYFLAGS_WAITFORCHANNELING ) != 0 )
  {
    pOwner->SetChannellingProgress( 0.0f );
  }
}

//////////////////////////////////////////////////////////////////////////
unsigned int PFAbilityInstance::GetFlags() const
{
  const NDb::AlternativeActivity* pAltActivity = GetTarget().GetDBAlternativeTarget() ? GetTarget().GetDBAlternativeTarget()->alternativeActivity : 0;

  return pAltActivity ? pAltActivity->flags : pAbilityData->GetFlags();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFAbilityInstance::PFDispatchHolder::Start()
{
  if ( IsValid(pDispatch) && !pDispatch->IsObjectDead() )
  {
    pDispatch->Start();
    pDispatch = NULL;
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFAbilityInstance::PFDispatchHolder::Cancel()
{
  if ( IsValid(pDispatch) )
  {
    pDispatch->Cancel();
    pDispatch = 0;
  }
}

} //namespace NWorld

REGISTER_WORLD_OBJECT_NM(PFAbilityInstance, NWorld);

#endif
