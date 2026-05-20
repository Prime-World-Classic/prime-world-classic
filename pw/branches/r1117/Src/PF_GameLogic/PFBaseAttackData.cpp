#include "stdafx.h"

#if defined( PW_LINUX_NULL_RENDER )

#include "PFBaseAttackData.h"
#include "PFBaseUnit.h"
#include "PFDispatchFactory.h"

namespace NWorld
{

PFBaseAttackInstance::PFBaseAttackInstance(CObj<PFBaseAttackData> const& pAttackData, Target const& target, bool _allowAllies)
  : PFAbilityInstance(static_cast<PFAbilityData*>(pAttackData.GetPtr()), target, false)
  , attackDelay(pAttackData ? pAttackData->GetTimeOffset() : 0.0f)
  , rawAttackDelay(pAttackData ? pAttackData->GetTimeOffset(true) : 0.0f)
  , isAttackFinished(false)
  , allowAllies(_allowAllies)
{
}

void PFBaseAttackInstance::ApplyAttack()
{
  PFBaseAttackData const* pAttackData = static_cast<PFBaseAttackData const*>(GetData());
  if (!pAttackData || !pAttackData->GetDBDesc())
    return;

  Target const source(pAttackData->GetOwner());
  NDb::Spell const* pSpell = pAttackData->GetDBDesc();

  PFDispatch* pDispatch =
    CreateDispatch(this, NULL, source, target, pSpell, PFBaseApplicator::FLAG_BASE_ATTACK, false, rawAttackDelay);
  if (!pDispatch)
    return;

  dispatch.Attach(pDispatch);

  if (attackDelay < EPS_VALUE)
    DoAttack();
}

void PFBaseAttackInstance::DoAttack()
{
  if ( IsUnitValid( target.GetUnit() ) && ( allowAllies || !IsValid( pOwner ) || pOwner->GetFaction() != target.GetUnit()->GetFaction() ) )
  {
    if ( IsValid( pOwner ) )
      pOwner->OnAttackDispatchStarted();
    dispatch.Start();
  }
  isAttackFinished = true;
  Cancel();
}

void PFBaseAttackInstance::Cancel()
{
  attackDelay = 0.0f;
  target = AbilityTarget();
  dispatch.Cancel();
}

bool PFBaseAttackInstance::IsReadyToDie() const
{
  return isAttackFinished && !GetActiveApplicatorsCount();
}

bool PFBaseAttackInstance::Update(float dt)
{
  attackDelay -= dt;
  if ( attackDelay < EPS_VALUE && !isAttackFinished )
    DoAttack();
  return IsReadyToDie();
}

PFBaseAttackData::PFBaseAttackData(CPtr<PFBaseUnit> const& pOwner, NDb::Ptr<NDb::BaseAttack> const& pDBDesc)
  : PFAbilityData( pOwner, pDBDesc.GetPtr(), NDb::ABILITYTYPEID_BASEATTACK )
  , processInstances(false)
  , delayedCancel(false)
  , damageType(IsValid(pOwner) && pOwner->DbUnitDesc() ? pOwner->GetNativeDamageType() : NDb::APPLICATORDAMAGETYPE_NATIVE)
{
  if (!pDBDesc)
    return;

  for (vector<NDb::Ptr<NDb::BaseApplicator> >::const_iterator iAppl = pDBDesc->applicators.begin(),
    iEnd = pDBDesc->applicators.end(); iAppl != iEnd; ++iAppl)
  {
    NDb::Ptr<NDb::BaseApplicator> const& pAppl = *iAppl;
    if (pAppl && pAppl->GetObjectTypeID() == NDb::DamageApplicator::typeId)
    {
      damageType = static_cast<NDb::DamageApplicator const*>(pAppl.GetPtr())->damageType;
      if (damageType == NDb::APPLICATORDAMAGETYPE_NATIVE && IsValid(pOwner) && pOwner->DbUnitDesc())
        damageType = pOwner->GetNativeDamageType();
      break;
    }
  }
}

PFBaseAttackData::PFBaseAttackData()
  : processInstances(false)
  , delayedCancel(false)
  , damageType(NDb::APPLICATORDAMAGETYPE_NATIVE)
{
}

bool PFBaseAttackData::DoAttack(Target const& target, bool allowAllies)
{
  if ( !target.IsUnitValid() || !CanBeUsed() )
    return false;

  CObj<PFBaseAttackInstance> pInst = new PFBaseAttackInstance( this, target, allowAllies );
  pInst->ApplyAttack();
  rgAttackInstances.push_back(pInst);
  AddInstance(pInst.GetPtr());

  const float fAttackSpeed = GetSpeed();
  RestartCooldown( fAttackSpeed < EPS_VALUE ? FP_MAX_VALUE : (1.0f / fAttackSpeed) );
  return true;
}

void PFBaseAttackData::Update(float dt, bool fullUpdate)
{
  struct AttackInstancesUpdater
  {
    float dt;
    AttackInstancesUpdater(float dt) : dt(dt) {}
    bool operator()( CObj<PFBaseAttackInstance>& inst ) { return !IsValid(inst) || inst->Update(dt); }
  } updater(dt);

  processInstances = true;
  rgAttackInstances.erase( remove_if( rgAttackInstances.begin(), rgAttackInstances.end(), updater ), rgAttackInstances.end() );
  processInstances = false;
  if (delayedCancel)
    Cancel();

  PFAbilityData::Update(dt, fullUpdate);
}

void PFBaseAttackData::Cancel()
{
  for (AttackInstances::iterator it = rgAttackInstances.begin(); it != rgAttackInstances.end(); ++it)
    if (IsValid(*it))
      (*it)->Cancel();

  if (processInstances)
  {
    delayedCancel = true;
    return;
  }

  rgAttackInstances.clear();
  delayedCancel = false;
}

float PFBaseAttackData::GetWorkTime() const
{
  return GetTimeOffset();
}

float PFBaseAttackData::GetSpeed() const
{
  const float speed = IsValid(GetOwner()) ? GetOwner()->GetAttacksPerSecond() : 0.0f;
  return speed < EPS_VALUE ? 0.0f : speed;
}

float PFBaseAttackData::GetAttackNodeDuration( NScene::SceneObject* pSO ) const
{
  (void)pSO;
  return 0.0f;
}

bool PFBaseAttackData::IsMelee() const
{
  NDb::BaseAttack const* pAttack = dynamic_cast<NDb::BaseAttack const*>(GetDBDesc());
  return pAttack ? pAttack->isMelee : false;
}

} // namespace NWorld

REGISTER_WORLD_OBJECT_NM(PFBaseAttackInstance, NWorld);
REGISTER_WORLD_OBJECT_NM(PFBaseAttackData, NWorld);

#else

#include "PFBaseAttackData.h"
#include "PFAbilityInstance.h"
#include "PFAIWorld.h"

#include "PFBaseUnit.h"
#include "PFDispatchFactory.h"
#ifndef VISUAL_CUTTED
#include "../Scene/AnimatedSceneComponent.h"
#include "../Scene/SceneObjectCreation.h"
#endif


namespace NWorld
{

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// PFBaseAttackInstance
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

PFBaseAttackInstance::PFBaseAttackInstance(CObj<PFBaseAttackData> const& pAttackData, Target const& target, bool _allowAllies /*= false*/)
  : PFAbilityInstance( static_cast<PFAbilityData*>(pAttackData.GetPtr()), target, false )
  , attackDelay( pAttackData->GetTimeOffset() )
  , rawAttackDelay( pAttackData->GetTimeOffset( true ) )
  , isAttackFinished( false )
  , allowAllies(_allowAllies)
{
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFBaseAttackInstance::ApplyAttack()
{
  PFBaseAttackData const* pAttackData = static_cast<PFBaseAttackData const*>( GetData() );
  Target const source( pAttackData->GetOwner() );
  NDb::Spell const* pSpell = pAttackData->GetDBDesc();

  PFDispatch *pDispatch = CreateDispatch(this, NULL, source, target, pSpell, PFBaseApplicator::FLAG_BASE_ATTACK, false, rawAttackDelay);
  if (!pDispatch)
    return;

  dispatch.Attach(pDispatch);

  if (attackDelay < EPS_VALUE)
    DoAttack();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFBaseAttackInstance::DoAttack()
{
  if( IsUnitValid( target.GetUnit() ) && ( allowAllies || pOwner->GetFaction() != target.GetUnit()->GetFaction() ) )
  {
    if( IsValid( pOwner ) )
    {
      pOwner->OnAttackDispatchStarted();
    }
    dispatch.Start();
  }
  isAttackFinished = true;
  Cancel(); // CleanUp
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFBaseAttackInstance::Cancel()
{
  attackDelay = 0.0f;  
  target = AbilityTarget();
  dispatch.Cancel();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool PFBaseAttackInstance::IsReadyToDie() const
{
  return isAttackFinished && !GetActiveApplicatorsCount();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool PFBaseAttackInstance::Update(float dt)
{
	if ( attackDelay > 0 )
	{
		attackDelay -= dt;
		if( attackDelay < EPS_VALUE  ) 
			DoAttack();
	}

	return IsReadyToDie();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// PFBaseAttackData
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

PFBaseAttackData::PFBaseAttackData(CPtr<PFBaseUnit> const& pOwner, NDb::Ptr<NDb::BaseAttack> const& pDBDesc)
  : PFAbilityData( pOwner, pDBDesc.GetPtr(), NDb::ABILITYTYPEID_BASEATTACK )
  , damageType(pOwner->GetNativeDamageType())
  , processInstances( false )
  , delayedCancel( false )
{
  // collect damage type from applicators
  for ( vector<NDb::Ptr<NDb::BaseApplicator>>::const_iterator iAppl = pDBDesc->applicators.begin(), iEnd = pDBDesc->applicators.end(); iAppl != iEnd; ++iAppl )
  {
    NDb::Ptr<NDb::BaseApplicator> const& pAppl = *iAppl;
    if (pAppl && pAppl->GetObjectTypeID() == NDb::DamageApplicator::typeId)
    {
      damageType = static_cast<NDb::DamageApplicator const*>(pAppl.GetPtr())->damageType;
      if ( damageType == NDb::APPLICATORDAMAGETYPE_NATIVE )
        damageType = pOwner->GetNativeDamageType();
      break;
    }
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
PFBaseAttackData::PFBaseAttackData()
  : processInstances(false)
  , delayedCancel(false)
{
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool PFBaseAttackData::DoAttack(Target const& target, bool allowAllies /*= false*/)
{
  if ( !target.IsUnitValid() || !CanBeUsed() )
    return false;

  NI_VERIFY( GetOwner()->IsTargetInAttackRange( target, true ), "Target is out of attack range!", return false );
  CObj<PFBaseAttackInstance> pInst = new PFBaseAttackInstance( this, target, allowAllies );
  pInst->ApplyAttack();

  NI_VERIFY( !processInstances, "Cannot push_back to rgAttackInstances duiring self update", return false );
  rgAttackInstances.push_back(pInst);
  
  AddInstance(pInst.GetPtr());

  // recalculate cooldown from attack speed
  const float fAttackSpeed = GetSpeed();
  RestartCooldown( fAttackSpeed < EPS_VALUE ? FP_MAX_VALUE : (1.0f / fAttackSpeed) );
  
  //NI_ALWAYS_ASSERT("Implement critical!");
  
  return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFBaseAttackData::Update(float dt, bool fullUpdate)
{
  struct AttackInstancesUpdater
  {
    float dt;
    AttackInstancesUpdater(float dt) : dt(dt) {}
    bool operator()( CObj<PFBaseAttackInstance>& inst ) { return !IsValid(inst) || inst->Update(dt); }
  } updater(dt);

  processInstances = true;
  rgAttackInstances.erase( remove_if( rgAttackInstances.begin(), rgAttackInstances.end(), updater ), rgAttackInstances.end() );
  processInstances = false;
  if ( delayedCancel )
    Cancel();

  PFAbilityData::Update(dt, fullUpdate);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void PFBaseAttackData::Cancel()
{
  for (AttackInstances::iterator iAttack = rgAttackInstances.begin(), iEnd = rgAttackInstances.end(); iAttack != iEnd; ++iAttack )
    (*iAttack)->Cancel();

  NI_VERIFY( !processInstances, "Cannot clear rgAttackInstances duiring self update", { delayedCancel = true; return; } );
  rgAttackInstances.clear();
  delayedCancel = false;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float PFBaseAttackData::GetWorkTime() const
{
	float calcWorkTime = 0.0f;

	bool needDelete = false;
	NScene::SceneObject* pSO = GetSO(needDelete);
	if ( !pSO )
	{
		calcWorkTime = 0.0f;
	}
	else
	{
		calcWorkTime = GetAttackNodeDuration( pSO );
		if ( needDelete )
			delete pSO;
	}

  return calcWorkTime;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float PFBaseAttackData::GetSpeed() const
{
  const float speed = GetOwner()->GetAttacksPerSecond();//GetStatValue(NDb::STAT_ATTACKSPEED);
  return speed < EPS_VALUE ? 0.0f : speed;
  //const float baseAttackTime = GetOwner()->GetWorld()->GetAIWorld()->GetAIParameters().baseAttackTime;
 //return baseAttackTime < EPS_VALUE ? 0.0f : 1e-2f * (speed / baseAttackTime);
}

float PFBaseAttackData::GetAttackNodeDuration( NScene::SceneObject* pSO ) const
{
#if defined(VISUAL_CUTTED)
  (void)pSO;
  return 0.0f;
#else
	float duration = 0.0f;
	nstl::string nodeName = "attack";
	::DiAnimGraph* pAG = GetAG( pSO );
	if ( !pAG )
	{
		return 0.0f;
	}

	uint nodeId = pAG->GetNodeIDByNameSlowQuite( nodeName.c_str() );
	if ( nodeId >= pAG->GetNumNodes() )
	{
		return 0.0f;
	}
	DiAnimNode* node = pAG->GetNodeData( nodeId );
	if ( !node )
	{
		NI_ALWAYS_ASSERT( NStr::StrFmt("Can not find node %s in %s in %s", nodeName.c_str(), pAG->GetDBFileName().c_str(), pSO->GetRootComponent()->GetDBID().GetFileName().c_str()) );
		return 0.0f;
	}

	if ( node->IsSwitcher() )
	{
		struct FindNode : public INeiFunctor
		{
			bool isFind;
			DiAnimNode* animNode;
			DiAnimGraph* pAG;
			float duration;

			FindNode( DiAnimGraph* ag ) : isFind( false ), animNode( 0 ), pAG( ag ), duration( 0.0f ) {}
			virtual void operator()( DiUInt32 nodeId )
			{
				if ( isFind )
					return;

				DiAnimNode* node = pAG->GetNodeData( nodeId );
				if ( node->IsSubNode() )
				{
					duration = pAG->GetNodeDuration( nodeId );
					animNode = node;
					isFind = true;
				}
			}
		} f( pAG );
		pAG->ForAllNeighbours( nodeId, &f );

		if ( f.isFind )
		{
			duration = f.duration;
		}
	}
	else
	{
		duration = pAG->GetNodeDuration( nodeId );
	}

	return duration;
#endif
}

bool PFBaseAttackData::IsMelee() const
{
  return static_cast<NDb::BaseAttack const*>(GetDBDesc())->isMelee;
}

} //namespace NWorld

REGISTER_WORLD_OBJECT_NM(PFBaseAttackInstance, NWorld);
REGISTER_WORLD_OBJECT_NM(PFBaseAttackData, NWorld);

#endif
