#include "stdafx.h"

#if defined( PW_LINUX_NULL_RENDER )

#include "PFAIHelper.h"
#include "PFMaleHero.h"
#include "PFTalent.h"
#include "PFWorld.h"
#include "PFWorldNatureMap.h"
#include "PFPickupable.h"
#include "PFFlagpole.h"
#include "PFBuildings.h"
#include "HeroActions.h"
#include "TargetSelectorHelper.hpp"

namespace NWorld
{

TalentWrapper::TalentWrapper() : level(0), slot(0), pTalent(NULL) {}
TalentWrapper::TalentWrapper( CPtr<PFBaseMaleHero> _pUnit, int _level, int _slot )
  : level(_level), slot(_slot), pUnit(_pUnit)
{
  pTalent = ::IsValid(pUnit) ? pUnit->GetTalent(level, slot) : NULL;
}
bool TalentWrapper::operator<( const TalentWrapper& other ) const { return level == other.level ? slot < other.slot : level < other.level; }
bool TalentWrapper::operator!=( const TalentWrapper& other ) const { return pUnit != other.pUnit || level != other.level || slot != other.slot; }
TalentWrapper TalentWrapper::operator++()
{
  if ( !IsValid() )
    return *this;

  TalentWrapper current = *this;

  const int numSlots  = NDb::KnownEnum<NDb::ETalentSlot>::sizeOf;
  const int numLevels = NDb::KnownEnum<NDb::ETalentLevel>::sizeOf;

  if ( slot < numSlots - 1 )
  {
    *this = TalentWrapper( pUnit, level, slot + 1 );
  }
  else if ( level < numLevels - 1 )
  {
    *this = TalentWrapper( pUnit, level + 1, 0 );
  }
  else
  {
    pTalent = 0;
    level = -1;
    slot = -1;
  }

  return current;
}
bool TalentWrapper::IsPreferable( const TalentWrapper& other ) const
{
  if ( !other.pTalent || !other.CanBeActivated() )
    return true;

  if ( !pTalent || !CanBeActivated() )
    return false;

  if ( pTalent->GetAIPriority() > other.pTalent->GetAIPriority() )
    return true;

  if ( pTalent->GetAIPriority() < other.pTalent->GetAIPriority() )
    return false;

  return pTalent->GetNaftaCost() > other.pTalent->GetNaftaCost();
}
int TalentWrapper::GetPriority() const { return pTalent ? pTalent->GetAIPriority() : -1; }
bool TalentWrapper::IsActive() const { return pTalent ? pTalent->IsActive() : false; }
bool TalentWrapper::IsActivated() const { return pTalent ? pTalent->IsActivated() : false; }
bool TalentWrapper::CanBeUsed() const { return ::IsValid(pUnit) && pUnit->CanUseTalent(level, slot); }
bool TalentWrapper::CanBeActivated() const { return ::IsValid(pUnit) && pUnit->CanActivateTalent(level, slot) == ETalentActivation::Ok; }
string TalentWrapper::GetName() const { return pTalent ? NStr::ToMBCS(pTalent->GetDBDesc()->name.GetText()) : "Empty talent here!"; }

bool GetRoute( PFWorld* pWorld, NDb::EFaction faction, int roadIndex, vector<CVec2>& road )
{
  road.clear();
  if (!pWorld || !pWorld->GetNatureMap() || roadIndex < 0 || roadIndex >= NDb::KnownEnum<NDb::ENatureRoad>::sizeOf)
    return false;
  road = pWorld->GetNatureMap()->GetLogicRoad((NDb::ENatureRoad)roadIndex);
  if (faction != NDb::FACTION_FREEZE)
    reverse(road.begin(), road.end());
  return !road.empty();
}

int GetNextRoutePoint( const vector<CVec2>& road, const CVec2& unitPos )
{
  if (road.empty())
    return 0;
  int nearestPoint = 0;
  float nearestDist = FP_MAX_VALUE;
  for (int i = 0; i < road.size(); ++i)
  {
    const float dist = fabs2(unitPos - road[i]);
    if (dist < nearestDist)
    {
      nearestDist = dist;
      nearestPoint = i;
    }
  }
  return Min(nearestPoint + 1, (int)road.size());
}

bool CompareRoutePoints( const vector<CVec2>& road, const CVec2& pos1, const CVec2& pos2 )
{
  return GetNextRoutePoint(road, pos1) <= GetNextRoutePoint(road, pos2);
}

EConsumableType IdentifyConsumable( const NDb::Consumable* pConsumable )
{
  if (!pConsumable)
    return OBJECT_UNKNOWN;
  string dbFileName = pConsumable->GetDBID().GetFileName();
  NStr::ToLower(&dbFileName);
  if (strstr(dbFileName.c_str(), "healingpotion."))
    return OBJECT_HEALING_POTION;
  if (strstr(dbFileName.c_str(), "energypotion."))
    return OBJECT_ENERGY_POTION;
  if (strstr(dbFileName.c_str(), "teleportscroll."))
    return OBJECT_TELEPORT_SCROLL;
  return OBJECT_UNKNOWN;
}

void ShiftWaypoint(CVec2 &wayPoint, CVec2 &refferenceWayPoint, int shift, float shiftDistance)
{
  const CVec2 delta = wayPoint - refferenceWayPoint;
  const float dist = fabs(delta.x, delta.y);
  if (dist <= EPS_VALUE)
    return;
  CVec2 normal(delta.y / dist, -delta.x / dist);
  if (shift < 0)
    normal = -normal;
  wayPoint += normal * shiftDistance;
}

PFAIHelper::PFAIHelper( PFBaseHero* unit, NCore::ITransceiver* pTransceiver )
  : transceiver(pTransceiver)
  , needResetHealing(false)
{
  pUnit = dynamic_cast<PFBaseMaleHero*>(unit);
  pDBBots = IsValid(pUnit) && pUnit->GetWorld() ? pUnit->GetWorld()->GetBotsSettings() : 0;
}

bool PFAIHelper::SendGameCommand( NCore::WorldCommand* _command, int kind, const PFLogicObject* target )
{
  CObj<NCore::WorldCommand> command(_command);
  const bool sent = command && IsValid(transceiver);
  if (sent)
  {
    const bool isPlayerCommand = IsValid(pUnit) && pUnit->IsLocal();
    transceiver->SendCommand(command, isPlayerCommand);
  }

  if (PFWorld* world = GetWorld())
  {
    world->RecordLinuxAICommand(
      static_cast<PFWorld::LinuxAICommandKind>(kind),
      pUnit,
      target,
      sent);
  }
  return sent;
}

void RecordLinuxAIDirectFallback(PFBaseMaleHero* hero, int kind, const PFLogicObject* target)
{
  if (IsValid(hero) && hero->GetWorld())
  {
    hero->GetWorld()->RecordLinuxAICommandDirectFallback(
      static_cast<PFWorld::LinuxAICommandKind>(kind),
      hero,
      target);
  }
}

PFLogicObject* ResolveLinuxAITargetObject(const Target& target)
{
  if (!target.IsObject())
    return 0;
  return target.GetObject();
}

void PFAIHelper::GetLife( float& current, float& maximal ) { current = IsValid(pUnit) ? pUnit->GetLife() : 0.0f; maximal = IsValid(pUnit) ? pUnit->GetMaxLife() : 0.0f; }
void PFAIHelper::GetMana( float& current, float& maximal ) { current = IsValid(pUnit) ? pUnit->GetMana() : 0.0f; maximal = IsValid(pUnit) ? pUnit->GetMaxMana() : 0.0f; }
PFWorld* PFAIHelper::GetWorld() { return IsValid(pUnit) ? pUnit->GetWorld() : 0; }
bool PFAIHelper::IsMoving() const { return IsValid(pUnit) && pUnit->IsMoving(); }
bool PFAIHelper::IsHolding() const { return IsValid(pUnit) && pUnit->IsHoldingPosition(); }
void PFAIHelper::MoveTo( const CVec2& pos )
{
  if (!SendGameCommand(CreateCmdMoveHero(pUnit, pos, false), PFWorld::LinuxAICommandMove) && IsValid(pUnit))
  {
    RecordLinuxAIDirectFallback(pUnit, PFWorld::LinuxAICommandMove, 0);
    pUnit->Move(pos);
  }
}
void PFAIHelper::CombatMoveTo( const CVec2& pos )
{
  if (!SendGameCommand(CreateCmdCombatMoveHero(pUnit, pos), PFWorld::LinuxAICommandCombatMove) && IsValid(pUnit))
  {
    RecordLinuxAIDirectFallback(pUnit, PFWorld::LinuxAICommandCombatMove, 0);
    pUnit->Move(pos);
  }
}
void PFAIHelper::Stop()
{
  if (!SendGameCommand(CreateCmdStopHero(pUnit), PFWorld::LinuxAICommandStop) && IsValid(pUnit))
  {
    RecordLinuxAIDirectFallback(pUnit, PFWorld::LinuxAICommandStop, 0);
    pUnit->Stop();
  }
}
void PFAIHelper::Follow( PFBaseUnit* target, float followRange, float forceFollowRange )
{
  if (!SendGameCommand(CreateCmdFollowUnit(pUnit, target, followRange, forceFollowRange, false), PFWorld::LinuxAICommandFollow, target) &&
      IsValid(pUnit) &&
      IsValid(target))
  {
    RecordLinuxAIDirectFallback(pUnit, PFWorld::LinuxAICommandFollow, target);
    pUnit->Move(target->GetPosition().AsVec2D());
  }
}
void PFAIHelper::Attack( PFBaseUnit* target )
{
  if (!SendGameCommand(CreateCmdAttackTarget(pUnit, target, false), PFWorld::LinuxAICommandAttack, target) &&
      IsValid(pUnit) &&
      IsValid(target))
  {
    RecordLinuxAIDirectFallback(pUnit, PFWorld::LinuxAICommandAttack, target);
    pUnit->AssignTarget(target, true);
  }
}
void PFAIHelper::ActivateTalent( int level, int slot )
{
  if (!SendGameCommand(CreateCmdActivateTalent(pUnit, level, slot), PFWorld::LinuxAICommandActivateTalent) &&
      IsValid(pUnit))
  {
    RecordLinuxAIDirectFallback(pUnit, PFWorld::LinuxAICommandActivateTalent, 0);
    pUnit->ActivateTalent(level, slot);
  }
}
void PFAIHelper::ActivateTalent( const TalentWrapper& iTalent ) { ActivateTalent(iTalent.GetLevel(), iTalent.GetSlot()); }
void PFAIHelper::UseTalent( int level, int slot, const Target& target )
{
  PFLogicObject* targetUnit = ResolveLinuxAITargetObject(target);
  if (!SendGameCommand(CreateCmdUseTalent(pUnit, level, slot, target, false), PFWorld::LinuxAICommandUseTalent, targetUnit) &&
      IsValid(pUnit))
  {
    RecordLinuxAIDirectFallback(pUnit, PFWorld::LinuxAICommandUseTalent, targetUnit);
    pUnit->UseTalent(level, slot, target);
  }
}
void PFAIHelper::UseTalent( const TalentWrapper& iTalent, const Target& target ) { UseTalent(iTalent.GetLevel(), iTalent.GetSlot(), target); }
void PFAIHelper::BuyConsumable( PFShop* pShop, int index )
{
  SendGameCommand(CreateCmdBuyConsumable(pUnit, pShop, index, -1), PFWorld::LinuxAICommandBuyConsumable);
}
void PFAIHelper::UseConsumable( int slot, const Target& target )
{
  PFLogicObject* targetUnit = ResolveLinuxAITargetObject(target);
  if (!SendGameCommand(CreateCmdUseConsumable(pUnit, slot, target), PFWorld::LinuxAICommandUseConsumable, targetUnit) &&
      IsValid(pUnit))
  {
    RecordLinuxAIDirectFallback(pUnit, PFWorld::LinuxAICommandUseConsumable, targetUnit);
    pUnit->ExecuteCommandUseConsumable(slot, target, false);
  }
}
void PFAIHelper::UsePortal( const Target& target )
{
  SendGameCommand(CreateCmdUsePortal(pUnit, target, false), PFWorld::LinuxAICommandUsePortal);
}
void PFAIHelper::PickupObject( PFPickupableObjectBase* pObject )
{
  SendGameCommand(CreateCmdPickupObject(pUnit, pObject ? pObject->GetObjectId() : -1), PFWorld::LinuxAICommandPickupObject, pObject);
}
void PFAIHelper::RaiseFlag( PFFlagpole* pFlagpole )
{
  SendGameCommand(CreateCmdRaiseFlag(pUnit, pFlagpole, false), PFWorld::LinuxAICommandRaiseFlag, pFlagpole);
}
int PFAIHelper::HasConsumable( EConsumableType type, int* firstIndex ) { (void)type; if (firstIndex) *firstIndex = -1; return 0; }
PFBaseUnit* PFAIHelper::FindEnemyNear()
{
  if (!IsValid(pUnit))
    return 0;
  const float searchRadius = Max(pUnit->GetVisibilityRange(), pUnit->GetTargetingRange());
  return pUnit->FindTarget(searchRadius, true);
}

void TalentPart::ActivateTalents( PFAIHelper &aiHelper )
{
  if ( --activateTimeLeft > 0 )
    return;
  activateTimeLeft = activateDelay;

  TalentWrapper toActivate( aiHelper.pUnit, 0, 0 );

  for ( TalentWrapper i = GetFirstTalent(aiHelper); i.IsValid(); ++i )
  {
    if ( i.CanBeActivated() && i.IsPreferable( toActivate ) )
      toActivate = i;
  }

  if ( toActivate.CanBeActivated() )
    aiHelper.ActivateTalent( toActivate );
}

void TalentPart::UseTalents( PFAIHelper &aiHelper )
{
  if ( --useTimeLeft > 0 )
    return;
  useTimeLeft = useDelay;

  struct ToUse
  {
    TalentWrapper talentWrapper;
    Target target;

    ToUse( const TalentWrapper& _talentWrapper, Target _target ) : talentWrapper( _talentWrapper ), target( _target ) {}
    ToUse() : talentWrapper(), target() {}
  };

  nstl::vector<ToUse> talentsToUse;
  int bestPriority = -1;

  for ( TalentWrapper i = GetFirstTalent(aiHelper); i.IsValid(); ++i )
  {
    if ( !i.IsActivated() || !i.IsActive() )
      continue;

    const PFTalent* pTalent = i.GetTalent();
    if ( !pTalent )
      continue;

    if ( pTalent->IsMultiState() && pTalent->IsOn() )
      continue;

    const CheckValidAbilityTargetCondition condition;
    Target target;
    if ( !pTalent->FindMicroAITargetTemp( target, condition ) )
      continue;

    if ( !( target.IsObject() || target.IsPosition() ) )
      continue;

    const int priority = i.GetPriority();
    if ( priority >= bestPriority )
    {
      if ( priority > bestPriority )
      {
        bestPriority = priority;
        talentsToUse.clear();
      }
      talentsToUse.push_back( ToUse( i, target ) );
    }
  }

  if ( talentsToUse.empty() )
    return;

  aiHelper.UseTalent( talentsToUse.front().talentWrapper, talentsToUse.front().target );
}
TalentWrapper TalentPart::GetFirstTalent( PFAIHelper &aiHelper ) { return TalentWrapper(aiHelper.pUnit, 0, 0); }
TalentWrapper TalentPart::GetLastTalent( PFAIHelper &aiHelper )
{
  const int numSlots  = NDb::KnownEnum<NDb::ETalentSlot>::sizeOf;
  const int numLevels = NDb::KnownEnum<NDb::ETalentLevel>::sizeOf;

  return TalentWrapper( aiHelper.pUnit, numLevels - 1, numSlots - 1 );
}

}

#else

#include "../Core/GameCommand.h"
#include "../Core/Scheduler.h"
#include "PFMaleHero.h"
#include "PFTalent.h"
#include "HeroActions.h"
#include "DBConsumable.h"
#include "PFConsumable.h"
#include "PFWorldNatureMap.h"
#include "PFPickupable.h"

#include "PFAIHelper.h"
#include "TargetSelectorHelper.hpp"

namespace NWorld
{
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TalentWrapper
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
TalentWrapper::TalentWrapper()
{
  pUnit = NULL;
  level = 0;
  slot = 0;
  pTalent = NULL;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
TalentWrapper::TalentWrapper( CPtr<PFBaseMaleHero> _pUnit, int _level, int _slot )
: pUnit ( _pUnit )
, level ( _level )
, slot ( _slot )
{
  pTalent = pUnit ? pUnit->GetTalent( level, slot ) : NULL;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool TalentWrapper::operator<( const TalentWrapper& other ) const
{
  if ( level == other.level ) 
    return slot < other.slot;
  else
    return level < other.level;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool TalentWrapper::operator!=( const TalentWrapper& other ) const
{
  return ( ( pUnit != other.pUnit ) || ( level != other.level ) || ( slot  != other.slot  ) );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
TalentWrapper TalentWrapper::operator++()
{
  if ( !IsValid() )
    return *this;

  TalentWrapper current = *this;

  const int numSlots  = NDb::KnownEnum<NDb::ETalentSlot>::sizeOf;
  const int numLevels = NDb::KnownEnum<NDb::ETalentLevel>::sizeOf;

  if ( slot < numSlots - 1 )
  {
    *this = TalentWrapper( pUnit, level, slot + 1 );
  }
  else
  {
    if ( level < numLevels - 1 )
    {
      *this = TalentWrapper( pUnit, level + 1, 0 );
    }
    else
    {
      pTalent = 0;
      level = -1;
      slot = -1;
    }
  }

  return current;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool TalentWrapper::IsPreferable( const TalentWrapper& other ) const
{
  if ( !other.pTalent || !other.CanBeActivated() )
  {
    return true;
  }

  if ( !pTalent || !CanBeActivated() )
  {
    return false;
  }

  if ( pTalent->GetAIPriority() > other.pTalent->GetAIPriority() )
  {
    return true;
  }

  if ( pTalent->GetAIPriority() < other.pTalent->GetAIPriority() )
  {
    return false;
  }

  return ( pTalent->GetNaftaCost() > other.pTalent->GetNaftaCost() );
}

int TalentWrapper::GetPriority()     const { return pTalent ? pTalent->GetAIPriority()  : -1; }
bool TalentWrapper::IsActive()       const { return pTalent ?   pTalent->IsActive()                : false; }
bool TalentWrapper::IsActivated()    const { return pTalent   ? pTalent->IsActivated()             : false; }
bool TalentWrapper::CanBeUsed()      const { return pUnit   ?   pUnit->CanUseTalent( level, slot ) : false; }
bool TalentWrapper::CanBeActivated() const { return pUnit   ? ( pUnit->CanActivateTalent( level, slot ) == ETalentActivation::Ok ) : false; }
string TalentWrapper::GetName() const { return pTalent ? NStr::ToMBCS( pTalent->GetDBDesc()->name.GetText() ) : "Empty talent here!"; }

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Utility
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool GetRoute( PFWorld* pWorld, NDb::EFaction faction, int roadIndex, vector<CVec2>& road )
{
  road.clear();
  if ( !pWorld )
  {
    return false;
  }
  if ( !pWorld->GetNatureMap() )
  {
    return false;
  }
  if ( roadIndex < 0 || roadIndex >= NDb::KnownEnum<NDb::ENatureRoad>::sizeOf )
  {
    return false;
  }

  road = pWorld->GetNatureMap()->GetLogicRoad( ( NDb::ENatureRoad ) roadIndex );
  if ( faction != NDb::FACTION_FREEZE )		// opposite direction for this faction
  {
    reverse( road.begin(), road.end() );
  }
  return true;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int GetNextRoutePoint( const vector<CVec2>& road, const CVec2& unitPos )
{
  int i;

  int roadSize = road.size();
  // find nearest point
  int nearestPoint = 0;
  float nearestDist = 1e20f;
  for ( i = 0; i < roadSize; i++ )
  {
    float dist = fabs2( unitPos - road[i] ) +  AiConst::MOVE_BY_LINE_SENS();
    if ( dist < nearestDist )
    {
      nearestDist  = dist;
      nearestPoint = i;
    }
  }
  // check other points to find bypassed
  int nextPoint = nearestPoint;
  for ( i = nearestPoint; i < roadSize; i++ )
  {
    CVec2 p0, dirByRoad, dirToUnit;
    p0 = road[i];
    if ( i < roadSize - 1 )
    {
      // regular section
      dirByRoad = road[i+1] - p0;
    }
    else
    {
      // last section - use direction from previous section
      dirByRoad = p0 - road[i-1];
    }
    dirToUnit = unitPos - p0;
    float dot = dirByRoad * dirToUnit;
    if ( dot < 0 )
    {
      break;
    }
    nextPoint = i+1;
  }
  if ( nextPoint >= 0 && nextPoint < roadSize )
  {
    if ( (fabs2( road[nextPoint] - unitPos ) + AiConst::MOVE_BY_LINE_SENS()) < sqr( AiConst::MOVE_BY_LINE_SENS() + 2.0f ) )
    {
      nextPoint++;			// near to current point - start from next one
    }
  }
  return nextPoint;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// return true if pos1 is more near to road start than pos2
bool CompareRoutePoints( const vector<CVec2>& road, const CVec2& pos1, const CVec2& pos2 )
{
  int p1 = GetNextRoutePoint( road, pos1 );
  int p2 = GetNextRoutePoint( road, pos2 );
  if ( p1 < p2 )
  {
    return true;
  }
  if ( p1 > p2 )
  {
    return false;
  }
  // on the same segment
  if ( p1 >= road.size() )
  {
    // both points are after road end
    return true; // return SOMETHING
  }
  const CVec2& pos0 = road[p1]; // next point
  return fabs2( pos1 - pos0 ) > fabs2( pos2 - pos0 );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
EConsumableType IdentifyConsumable( const NDb::Consumable* pConsumable )
{
  string dbFileName = pConsumable->GetDBID().GetFileName();
  NStr::ToLower( &dbFileName );
  if ( strstr( dbFileName.c_str(), "healingpotion." ) )
  {
    return OBJECT_HEALING_POTION;
  }
  if ( strstr( dbFileName.c_str(), "energypotion." ) )
  {
    return OBJECT_ENERGY_POTION;
  }
  if ( strstr( dbFileName.c_str(), "teleportscroll." ) )
  {
    return OBJECT_TELEPORT_SCROLL;
  }

  return OBJECT_UNKNOWN;
}
void ShiftWaypoint(CVec2 &wayPoint, CVec2 &refferenceWayPoint, int shift, float shiftDistance)
{
  float dX = wayPoint.x - refferenceWayPoint.x;
  float dY = wayPoint.y - refferenceWayPoint.y;
  float dist = fabs(dX, dY);

  if (dist)
  {
    dX /= dist;
    dY /= dist;

    if (shift > 0)
    {
      wayPoint.x += shiftDistance*dY;
      wayPoint.y -= shiftDistance*dX;
    }
    else
    {
      wayPoint.x -= shiftDistance*dY;
      wayPoint.y += shiftDistance*dX;
    }
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// PFAIHelper
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

PFAIHelper::PFAIHelper( PFBaseHero* unit, NCore::ITransceiver* pTransceiver )
:transceiver(pTransceiver)
,needResetHealing(false)
{
  pUnit = ( dynamic_cast<PFBaseMaleHero*>( unit ) );
  pDBBots = pUnit->GetWorld()->GetBotsSettings();
}

bool PFAIHelper::SendGameCommand( NCore::WorldCommand* _command, int kind, const PFLogicObject* target )
{
  (void)kind;
  (void)target;
  NI_VERIFY( IsValid( transceiver ), "Transceiver is not provided to client. Cannot send command", return false );
  if ( !_command )
    return false;
  CObj<NCore::WorldCommand> command( _command );
  bool isPlayerCommand = IsValid(pUnit) && pUnit->IsLocal();
  transceiver->SendCommand( command, isPlayerCommand );
  return true;
}
void PFAIHelper::GetLife( float& current, float& maximal )
{
  current = pUnit->GetLife();
  maximal = pUnit->GetMaxLife();
}

void PFAIHelper::GetMana( float& current, float& maximal )
{
  current = pUnit->GetMana();
  maximal = pUnit->GetMaxMana();
}
PFWorld* PFAIHelper::GetWorld() { return pUnit->GetWorld(); }
bool  PFAIHelper::IsMoving() const    { return pUnit->ControlsMount() ? pUnit->GetMount()->IsMoving() : pUnit->IsMoving(); }
bool  PFAIHelper::IsHolding() const    { return pUnit->IsHoldingPosition(); }

/////////////////////////////////////////////////////////////////////////////
// low level commands (wrappers around HeroActions.cpp CreateCmd...())
/////////////////////////////////////////////////////////////////////////////

void PFAIHelper::MoveTo(       const CVec2& pos ) { SendGameCommand( CreateCmdMoveHero(       pUnit, pos, false ), 0 ); }
void PFAIHelper::CombatMoveTo( const CVec2& pos ) { SendGameCommand( CreateCmdCombatMoveHero( pUnit, pos ), 0 ); }
void PFAIHelper::Stop()	{	SendGameCommand( CreateCmdStopHero( pUnit ), 0 ); }
void PFAIHelper::Follow( PFBaseUnit* target, float followRange, float forceFollowRange ) { SendGameCommand( CreateCmdFollowUnit( pUnit, target, followRange, forceFollowRange, false ), 0, target ); }
void PFAIHelper::Attack( PFBaseUnit* target ) { SendGameCommand( CreateCmdAttackTarget( pUnit, target, false ), 0, target ); }
void PFAIHelper::ActivateTalent( int level, int slot ) { SendGameCommand( CreateCmdActivateTalent( pUnit, level, slot ), 0 ); }
void PFAIHelper::ActivateTalent( const TalentWrapper& iTalent ) { ActivateTalent( iTalent.GetLevel(), iTalent.GetSlot() ); }
void PFAIHelper::UseTalent( const TalentWrapper& iTalent, const Target& target ) { UseTalent( iTalent.GetLevel(), iTalent.GetSlot(), target ); }
void PFAIHelper::BuyConsumable( PFShop* pShop, int index ) { SendGameCommand( CreateCmdBuyConsumable( pUnit, pShop, index, -1 ), 0 ); }
void PFAIHelper::UseConsumable( int slot, const Target& target ){ SendGameCommand( CreateCmdUseConsumable( pUnit, slot, target ), 0 ); }
void PFAIHelper::UseTalent( int level, int slot, const Target& target )
{
#if LOG_AI
  const string name = NStr::ToMBCS( pUnit->GetTalent( level, slot )->GetDBDesc()->name.GetText() );
  if ( target.IsObject() )
  {
    PFLogicObject* obj = target.GetObject();
    DBG( "UseTalent %s to unit at ( %g, %g ) faction = %d type = %s", name, obj->GetPosition().x, obj->GetPosition().y,
      (int)obj->GetFaction(), obj->GetObjectTypeName() );
  }
  else
  {
    DBG( "UseTalent %s", name );
  }
#endif //LOG_AI
  SendGameCommand( CreateCmdUseTalent( pUnit, level, slot, target, false ), 0 );
}
void PFAIHelper::UsePortal( const Target& target )
{
  SendGameCommand( CreateCmdUsePortal( pUnit, target), 0 );
}
void PFAIHelper::PickupObject( PFPickupableObjectBase* pObject )
{
  SendGameCommand( CreateCmdPickupObject( pUnit, pObject->GetObjectId() ), 0, pObject );
}
int PFAIHelper::HasConsumable( EConsumableType type, int* firstIndex )
{
  int count = 0;
  // iterate all inventory slots
  for ( int slot = 0; slot < pUnit->GetSlotCount(); slot++ )
  {
    PFConsumable const* pConsumable = pUnit->GetConsumable( slot );
    if ( !pConsumable )
    {
      continue;				// empty slot
    }
    //			if (!CanUseConsumable(slot))
    //				continue;				// this consumable is disabled for some reason
    if ( IdentifyConsumable( pConsumable->GetDBDesc() ) != type )
    {
      continue;				// different type
    }
    // found an item
    if ( count == 0 )
    {
      // first appearance
      if ( firstIndex )
      {
        *firstIndex = slot;
      }
    }
    count += pConsumable->GetQuantity();
  }
  return count;
}

void PFAIHelper::RaiseFlag( PFFlagpole* pFlagpole )
{
  SendGameCommand( CreateCmdRaiseFlag(pUnit, pFlagpole), 0, pFlagpole );
}

PFBaseUnit* PFAIHelper::FindEnemyNear()
{
  float searchRadius = pUnit->GetVisibilityRange();
  int targetTypesToFind = NDb::SPELLTARGET_ALL | ( pUnit->CanAttackFlying() ? NDb::SPELLTARGET_FLYING : 0 ) ;

  struct TargetFinder
  {
    TargetFinder(const PFBaseUnit *owner) : found(false), owner(owner)  {}
    const CPtr<PFBaseUnit>& GetTarget() { return pTarget; }

    bool operator()( PFBaseUnit& _unit )
    {
      if ( !_unit.IsVisibleForEnemy( owner->GetFaction() ) )
        return false;

      const int unitTypeMask = NDb::SPELLTARGET_ALLWOBUILDINGS;
      bool checkFogOfWar = (unitTypeMask & (1L << _unit.GetUnitType())) == 0;

      if ( owner->CanSelectTarget(&_unit, checkFogOfWar) )
      {
        found = true;
        pTarget = &_unit;
        return true;
      }

      return false;
    }
    bool found;
    PFLogicObject* unit;
    private:
    const PFBaseUnit *owner;
    CPtr<PFBaseUnit> pTarget;

  } targetFinder(pUnit);

  pUnit->GetWorld()->GetAIWorld()->ForAllUnitsInRange( pUnit->GetPosition(), searchRadius, targetFinder,
    UnitMaskingPredicate( pUnit->GetOppositeFactionFlags(), targetTypesToFind, pUnit ) );

  return targetFinder.GetTarget();
}

/////////////////////////////////////////////////////////////////////////////
// TalentPart
/////////////////////////////////////////////////////////////////////////////

void TalentPart::ActivateTalents( PFAIHelper &aiHelper )
{
  // process delay
  if ( --activateTimeLeft > 0 )
  {
    return;
  }
  activateTimeLeft = activateDelay;

  // find talent to activate
  TalentWrapper toActivate( aiHelper.pUnit, 0, 0 );

  for ( TalentWrapper i = GetFirstTalent(aiHelper); i.IsValid(); ++i )
  {
    if ( i.CanBeActivated() && i.IsPreferable( toActivate ) )
    {
      toActivate = i;
    }
  }

  if ( !toActivate.CanBeActivated() )
  {
    return;								// nothing to activate
  }

  // activate
  DBG( "*** Activating talent %s ***", toActivate.GetName() );
  aiHelper.ActivateTalent( toActivate );
}

void TalentPart::UseTalents( PFAIHelper &aiHelper )
{
  // process delay
  if ( --useTimeLeft > 0 )
  {
    return;
  }
  useTimeLeft = useDelay;

  // enumerate abilities and use when possible
  struct ToUse
  {
    TalentWrapper talentWrapper;
    Target target;

    ToUse( const TalentWrapper& _talentWrapper, Target _target ) : talentWrapper( _talentWrapper ), target( _target ) { }

    ToUse()
    {
      talentWrapper = TalentWrapper();
      target = Target();
    }
  };

  nstl::vector<ToUse> talentsToUse;
  int bestPriority = -1;

  for ( TalentWrapper i = GetFirstTalent(aiHelper); i.IsValid(); ++i )
  {
    if ( !i.IsActivated() || !i.IsActive() )
    {
      continue;
    }

    // get ability
    const PFTalent* pTalent = i.GetTalent();
    if ( !pTalent )
    {
      continue;
    }

    int priority = i.GetPriority();

    // If switchable ability was selected and it is on - do nothing (recast would toggle it off before the timeout)
    if ( pTalent->IsMultiState() && pTalent->IsOn() )
    {
      continue;
    }

    // check usability and find any target for this ability
    const CheckValidAbilityTargetCondition condition;

    Target target;
    if ( !pTalent->FindMicroAITargetTemp( target, condition ) )
    {
      continue;
    }

    NI_VERIFY( ( target.IsObject() || target.IsPosition() ), "Wrong ability target", continue; );

    if ( priority >= bestPriority )
    {
      if ( priority > bestPriority )
      {
        bestPriority = priority;
        talentsToUse.clear();
      }
      talentsToUse.push_back( ToUse( i, target ) );
    }
  }

  // use random of the bests talent

  const unsigned int numTalents = talentsToUse.size();
  if ( numTalents == 0 )
  {
    return;
  }

  int talentIndex = 0;
  if ( numTalents > 1 )
  {
    talentIndex = NRandom::Random( numTalents - 1 );
  }

  TalentWrapper toUse( talentsToUse[talentIndex].talentWrapper );
  Target target = talentsToUse[talentIndex].target;
  aiHelper.UseTalent( toUse, target );
}
TalentWrapper TalentPart::GetFirstTalent( PFAIHelper &aiHelper )
{
  return TalentWrapper( aiHelper.pUnit, 0, 0 );
}
TalentWrapper TalentPart::GetLastTalent( PFAIHelper &aiHelper )
{
  const int numSlots  = NDb::KnownEnum<NDb::ETalentSlot>::sizeOf;
  const int numLevels = NDb::KnownEnum<NDb::ETalentLevel>::sizeOf;

  return TalentWrapper( aiHelper.pUnit, numLevels - 1, numSlots - 1 );
}

}

#endif
