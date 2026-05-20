#include "stdafx.h"

#if defined( PW_LINUX_NULL_RENDER )

namespace NWorld
{
class PFAbilityData;
class PFAbilityInstance;
class PFBaseApplicator;
class PFBaseBehaviour;
class PFApplStatus;
class PFApplTaunt;
class PFBehaviourGroup;
class PFDispatchUniformLinearMove;
}

#define PW_LINUX_INLINE_NULL_USER_CAST(TypeName) \
template<> inline TypeName* CastToUserObjectImpl<TypeName>(CObjectBase*, TypeName*, CObjectBase*) { return 0; }
PW_LINUX_INLINE_NULL_USER_CAST(NWorld::PFAbilityData)
PW_LINUX_INLINE_NULL_USER_CAST(NWorld::PFAbilityInstance)
PW_LINUX_INLINE_NULL_USER_CAST(NWorld::PFBaseApplicator)
PW_LINUX_INLINE_NULL_USER_CAST(NWorld::PFBaseBehaviour)
PW_LINUX_INLINE_NULL_USER_CAST(NWorld::PFApplStatus)
PW_LINUX_INLINE_NULL_USER_CAST(NWorld::PFApplTaunt)
PW_LINUX_INLINE_NULL_USER_CAST(NWorld::PFBehaviourGroup)
PW_LINUX_INLINE_NULL_USER_CAST(NWorld::PFDispatchUniformLinearMove)
#undef PW_LINUX_INLINE_NULL_USER_CAST

#include "../Game/PF/Audit/ClientStubs.h"
#include "PFMainBuilding.h"

namespace NWorld
{

PFMainBuilding::~PFMainBuilding() {}

PFMainBuilding::PFMainBuilding(PFWorld* pWorld, NDb::AdvMapObject const& dbObject)
  : PFBattleBuilding(pWorld, dbObject)
  , activated(false)
  , aoeCooldown(0.0f)
  , selectedAttack(Invalid)
  , aoeUnitsCount(0)
  , aoeRadius(0.0f)
  , minAOEDelay(0.0f)
  , maxAOEDelay(0.0f)
  , aoeUnitsTypes(0)
  , aoeUnitsFactions(0)
  , pRangedAttack(0)
  , pAOEAttack(0)
  , pAOEInstance(0)
  , aoePending(false)
{
  pDesc = dynamic_cast<NDb::MainBuilding const*>(dbObject.gameObject.GetPtr());
  if (!pDesc)
    return;

  Init(dbObject, pDesc, NDb::UNITTYPE_MAINBUILDING);

  aoeUnitsCount = pDesc->aoeUnitsCount;
  minAOEDelay = pDesc->minAOEDelay;
  maxAOEDelay = pDesc->maxAOEDelay;
  aoeRadius = pDesc->aoeRadius;
  aoeUnitsTypes = pDesc->aoeUnitsTypes;
  aoeUnitsFactions = pDesc->aoeUnitsFactions;

  SetBaseAngle(dbObject.offset.GetEulerRotation().z);
  SetVulnerable(true);
}

void PFMainBuilding::Reset()
{
  PFBattleBuilding::Reset();
  activated = false;
  aoePending = false;
  selectedAttack = Invalid;
  aoeCooldown = 0.0f;
}

bool PFMainBuilding::Step(float dtInSeconds)
{
  if (aoeCooldown > 0.0f)
    aoeCooldown = Max(0.0f, aoeCooldown - dtInSeconds);
  return PFBattleBuilding::Step(dtInSeconds);
}

void PFMainBuilding::Activate(bool activate)
{
  if (activate == activated)
    return;
  activated = activate;
  OnActivated(activated);
}

void PFMainBuilding::OnActivated(bool)
{
  ContolTurret(false);
}

void PFMainBuilding::StartAOECooldown()
{
  aoeCooldown = maxAOEDelay > 0.0f ? maxAOEDelay : 0.0f;
}

bool PFMainBuilding::CanUseAOE() const { return false; }
void PFMainBuilding::UseAOE() {}
bool PFMainBuilding::IsAOEInProcess() const { return selectedAttack == AOE && aoePending; }
bool PFMainBuilding::IsAOEAttackReady() const { return false; }

void PFMainBuilding::SelectAttack()
{
  selectedAttack = IsTargetValid(GetCurrentTarget()) ? Ranged : Invalid;
}

void PFMainBuilding::OnUnitDie(CPtr<PFBaseUnit> pKiller, int flags, PFBaseUnitDamageDesc const* pDamageDesc)
{
  if (GetWorld())
    GetWorld()->SetDefeatedFaction(GetFaction());
  PFBattleBuilding::OnUnitDie(pKiller, flags, pDamageDesc);
}

void PFMainBuilding::OnDie()
{
  pAOEInstance = 0;
  pRangedAttack = 0;
  pAOEAttack = 0;
  PFBattleBuilding::OnDie();
}

} // namespace NWorld

REGISTER_WORLD_OBJECT_NM(PFMainBuilding, NWorld)

#else

#include "PFMainBuilding.h"

#include "PFAIWorld.h"
#include "PFBaseAttackData.h"
#ifndef VISUAL_CUTTED
#include "PFClientBuilding.h"
#else
#include "../Game/PF/Audit/ClientStubs.h"
#endif
#include "PFStatistics.h"

#include <System/hwbreak.h>

namespace 
{
  #ifdef VISUAL_CUTTED
  const nstl::string& MainBuildingClientNodeName()
  {
    return NGameX::ClientStubEmptyNodeName();
  }
  #else
  const char* MainBuildingClientNodeName()
  {
    return BADNODENAME;
  }
  #endif

  struct UnitCounter
  {
    int count;
    explicit UnitCounter() : count(0) {}
    void operator () ( NWorld::PFBaseUnit const& unit)
    {
      if( !unit.IsDead() && !unit.CheckFlag(NDb::UNITFLAG_FORBIDAUTOTARGETME) )
        ++count;
    } 
  };

}

namespace NWorld
{

class PFMBGuardState : public PFMainBuildingState
{
  WORLD_OBJECT_METHODS(0x2C6CE400, PFMBGuardState);

  ZDATA_(PFMainBuildingState)
  CPtr<PFWorld> pWorld;
public:
  ZEND int operator&( IBinSaver &f ) { f.Add(1,(PFMainBuildingState*)this); f.Add(2,&pWorld); return 0; }

  PFMBGuardState(CPtr<PFWorld> const& pWorld, CPtr<PFMainBuilding> const& pOwner);

protected:
  virtual bool OnStep(float dt);
  virtual void OnLeave();
  PFMBGuardState() {}  
};

PFMBGuardState::PFMBGuardState(CPtr<PFWorld> const& pWorld, CPtr<PFMainBuilding> const& pOwner)
  : PFMainBuildingState(pOwner)
  , pWorld(pWorld)
{
}

bool PFMBGuardState::OnStep(float dt)
{
  if( !IsUnitValid(pOwner) )
    return true;
  
  if( pOwner->CanUseAOE() && pOwner->IsAttackFinished())
  {
    pOwner->UseAOE();
    return false;
  }

  if( pOwner->IsAOEInProcess() )
    return false;
  
  CPtr<PFBaseUnit> pTarget = pOwner->FindTarget( pOwner->GetAttackRange() );
  if ( IsUnitValid(pTarget) )
  {
    pOwner->AssignTarget(pTarget, false);
    pOwner->SelectAttack();
    if ( pOwner->IsReadyToAttack() )
      pOwner->DoAttack();
  }
  else
  {
    pOwner->DropTarget();
  }

  return false;
}

void PFMBGuardState::OnLeave()
{
  if ( IsUnitValid(pOwner) )
    pOwner->DropTarget();
}

PFMainBuilding::~PFMainBuilding()
{
}

void PFMainBuilding::Reset()
{
	PFBattleBuilding::Reset();
	NGameX::PFClientLogicObject::CreatePars cp( GetWorld()->GetScene(), MainBuildingClientNodeName(), dbObjectCopy );
	CreateClientObject<NGameX::PFClientMainBuilding>( cp, GetWorld()->GetScene(), pDesc.GetPtr() );
}

bool PFMainBuilding::Step(float dtInSeconds)
{
  NI_PROFILE_FUNCTION
  
  PFBattleBuilding::Step(dtInSeconds);
  Activate(true);

  if( aoePending && IsAOEAttackReady() )
  {    
    CVec3 position = GetPosition();
    pAOEInstance   = CreateAbilityInstance( pAOEAttack, Target(position) );
    CALL_CLIENT_2ARGS(OnAttack, pAOEAttack->GetTimeOffset( true ), position.AsVec2D() );
    
    aoePending = false;
  }

  if( IsAOEInProcess() && pAOEInstance && pAOEInstance->IsCastDone() )
  {
    SelectAttack();
    ForceTargetAngle(false);

    pAOEInstance = NULL;
  }
  
  aoeCooldown = max( 0.0f, aoeCooldown - dtInSeconds );

  return true;
}

void PFMainBuilding::Activate(bool activate)
{
  if( activate == activated )
    return;
  
  activated   = activate;

  CALL_CLIENT_1ARGS(OnActivate, activate);
  ContolTurret(false);
}

void PFMainBuilding::OnActivated(bool activated)
{
  if( activated )
  {
    StartAOECooldown();
    PushState(new PFMBGuardState(GetWorld(), this) );
  }

  CALL_CLIENT_1ARGS(OnActivated, activated);
  ContolTurret(false);
}

void PFMainBuilding::StartAOECooldown()
{
  aoeCooldown = GetWorld()->GetRndGen()->Next( minAOEDelay * 1e+3, maxAOEDelay * 1e+3) * 1e-3f;
}

bool PFMainBuilding::CanUseAOE() const
{
  if( NULL == pAOEAttack )
    return false;

  if( !IsAOEReady() )
    return false;

  UnitCounter counter;
  GetWorld()->GetAIWorld()->ForAllUnitsInRange(GetPosition(), aoeRadius, counter, 
    UnitMaskingPredicate(GetOppositeFactionFlags(), NDb::SPELLTARGET_ALL | NDb::SPELLTARGET_AFFECTMOUNTED | NDb::SPELLTARGET_FLYING ) );
  
  return aoeUnitsCount <= counter.count;
}

bool PFMainBuilding::IsAOEInProcess() const
{
  return AOE == selectedAttack;
}

void PFMainBuilding::UseAOE()
{
  if( pAOEAttack && IsAOEReady() )
  {  
    DropTarget();
    SelectAttack();
    StartAOECooldown();

    selectedAttack = AOE;
    aoePending     = true;

    ForceTargetAngle( true, ToDegree(baseAngle) );
  }
}

void PFMainBuilding::SelectAttack()
{
  CPtr<PFBaseUnit> pTarget = GetCurrentTarget();
  if( IsTargetValid( pTarget ) )
  {
    if (Ranged != selectedAttack)
    {
      selectedAttack = Ranged;
      ReplaceBaseAttack(pRangedAttack);
    }
    return;
  }
  
  selectedAttack = Invalid;
  pAttackAbility = NULL;
}

bool PFMainBuilding::IsAOEAttackReady() const
{
  return (pAOEAttack ? (1.0f - pAOEAttack->GetPreparedness() < EPS_VALUE) : false) && IsInTargetPosition();
}

void PFMainBuilding::OnUnitDie( CPtr<PFBaseUnit> pKiller, int flags, PFBaseUnitDamageDesc const* pDamageDesc /*= 0*/ )
{
  const NDb::EFaction faction = GetFaction();
  GetWorld()->SetDefeatedFaction(faction);

  GetWorld()->GetStatistics()->SetMainBuildingKiller(pKiller);
  PFBattleBuilding::OnUnitDie(pKiller, flags, pDamageDesc);

  GetWorld()->OnGameFinished( faction );
}

#define DESTROY( p ) { if( p ) p->Cancel(); p = NULL; }

void PFMainBuilding::OnDie()
{
  DESTROY(pAOEInstance);
  DESTROY(pRangedAttack);
   
  pAOEAttack = NULL;
  
  PFBattleBuilding::OnDie();
}

#pragma code_seg(push, "~")

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
PFMainBuilding::PFMainBuilding(PFWorld* pWorld, NDb::AdvMapObject const& dbObject)
: PFBattleBuilding(pWorld, dbObject)
, activated(false)
, aoeUnitsCount(0)
, minAOEDelay(0.0f)
, maxAOEDelay(0.0f)
, selectedAttack(Invalid)
, aoeCooldown(0.0f)
, aoeRadius(0.0f)
, aoePending(false)
, aoeUnitsTypes( 0 )
, aoeUnitsFactions( 0 )
{
  STARFORCE_STOPWATCH();

  pDesc = dynamic_cast<NDb::MainBuilding const*>(dbObject.gameObject.GetPtr());
  NI_VERIFY( pDesc, "Invalid game object for the building", return; );

  NGameX::PFClientLogicObject::CreatePars cp( pWorld->GetScene(), MainBuildingClientNodeName(), dbObject );

  CreateClientObject<NGameX::PFClientMainBuilding>( cp, pWorld->GetScene(), pDesc.GetPtr() );

  Init(dbObject, pDesc, NDb::UNITTYPE_MAINBUILDING);

  aoeUnitsCount    = pDesc->aoeUnitsCount;
  minAOEDelay      = pDesc->minAOEDelay;
  maxAOEDelay      = pDesc->maxAOEDelay;
  aoeRadius        = pDesc->aoeRadius; 
  aoeUnitsTypes    = pDesc->aoeUnitsTypes;
  aoeUnitsFactions = pDesc->aoeUnitsFactions;

  // ranged attack is a default attack
  pRangedAttack    = pAttackAbility;
  pAOEAttack       = pDesc->aoeAttack    ? new PFAbilityData( CPtr<PFBaseUnit>(this), pDesc->aoeAttack, NDb::ABILITYTYPEID_SPECIAL ) : NULL;

  SetBaseAngle( dbObject.offset.GetEulerRotation().z );

  NDb::AILogicParameters const& params = GetWorld()->GetAIWorld()->GetAIParameters();
  bool isTeamA = NDb::FACTION_FREEZE == GetFaction();

  SetTurretParams(params.mainBuildingTurretParams[ isTeamA ? NDb::TEAMID_A : NDb::TEAMID_B ] );
  OnActivated(true);
}

#pragma code_seg(pop)

}; // namespace NWorld

REGISTER_WORLD_OBJECT_WITH_CLIENT_NM(PFMainBuilding,  NWorld);
REGISTER_WORLD_OBJECT_NM(PFMBGuardState, NWorld);

#endif
