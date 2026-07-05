#include "stdafx.h"
#include "HeroActions.h"
#include "PFAbilityInstance.h"

#include "PFBaseUnitStates.h"
#include "PFBuildings.h"//для доступа к позиции здания
#include "PFMinigamePlace.h"

#ifndef VISUAL_CUTTED
#include "Minimap.h"
#endif
#include "PFAbilityData.h"
#include "PFAIWorld.h"
#include "PFTalent.h"
#include "PFChest.h"
#include "PFFlagpole.h"
#include "PFHero.h"
#include "PFHeroStates.h"
#include "PFLogicDebug.h"
#include "PFEaselPlayer.h"
#include "PFAIContainer.h"
#if defined(PW_LINUX_NULL_RENDER)
#include "PFConsumable.h"
#endif

#include "TileMap.h"
#include "StaticPathInternal.h"

#include "SessionEventType.h"

#ifndef VISUAL_CUTTED
#include "AdventureScreen.h"
#include "MarkersController.h"
#include "AdventureScreenEvents.h"
#include "PFClientHero.h"
#include "Core/CoreFSM.h"
#else
#include "Game/PF/Audit/ClientStubs.h"
#endif

#include "TargetSelectorHelper.hpp"

namespace
{
  bool g_bDisableImmHeroMove = false;
  bool g_disableHeroCheck = false;

#if defined(PW_LINUX_NULL_RENDER)
  NWorld::LinuxHeroMoveCommandDiagnostics g_linuxHeroMoveCommandDiagnostics = {};
  NWorld::LinuxHeroGameplayCommandDiagnostics g_linuxHeroGameplayCommandDiagnostics = {};

  void CaptureLinuxHeroMoveCommandContext(
    NWorld::LinuxHeroMoveCommandDiagnostics* diagnostics,
    const NWorld::PFBaseHero* hero,
    int commandId,
    const CVec2& target,
    bool issuedByScript)
  {
    if (!diagnostics)
      return;

    diagnostics->lastCommandId = commandId;
    diagnostics->lastIssuedByScript = issuedByScript ? 1 : 0;
    diagnostics->lastTargetX = target.x;
    diagnostics->lastTargetY = target.y;
    diagnostics->lastHeroPlayerId = -1;
    diagnostics->lastHeroUserId = -1;
    diagnostics->lastHeroIsLocal = 0;
    diagnostics->lastHeroIsBot = 0;
    diagnostics->lastHeroIsPlaying = 0;
    diagnostics->lastSourceX = 0.0f;
    diagnostics->lastSourceY = 0.0f;
    diagnostics->lastSpeedBefore = 0.0f;
    diagnostics->lastMoveFlagsBefore = 0;
    diagnostics->lastIsMovingBefore = 0;

    if (!hero)
      return;

    const CVec2 position = hero->GetPosition().AsVec2D();
    diagnostics->lastSourceX = position.x;
    diagnostics->lastSourceY = position.y;
    diagnostics->lastHeroPlayerId = hero->GetPlayerId();
    diagnostics->lastHeroIsLocal = hero->IsLocal() ? 1 : 0;
    diagnostics->lastSpeedBefore = hero->GetUnitSpeed();
    diagnostics->lastMoveFlagsBefore = hero->GetMoveFlags();
    diagnostics->lastIsMovingBefore = hero->IsMoving() ? 1 : 0;

    if (const NWorld::PFPlayer* player = hero->GetPlayer())
    {
      diagnostics->lastHeroUserId = player->GetUserID();
      diagnostics->lastHeroIsBot = player->IsBot() ? 1 : 0;
      diagnostics->lastHeroIsPlaying = player->IsPlaying() ? 1 : 0;
    }
  }
  int GetLinuxCommandTargetObjectId(const NWorld::Target& target)
  {
    if (target.IsObjectValid(true))
      return target.GetObject()->GetObjectId();
    return -1;
  }

  int GetLinuxCommandTargetFaction(const NWorld::Target& target)
  {
    if (target.IsObjectValid(true))
      return static_cast<int>(target.GetObject()->GetFaction());
    return -1;
  }

  void CaptureLinuxGameplayTargetUnit(
    int* objectId,
    int* kind,
    int* faction,
    int* playerId,
    const NWorld::PFBaseUnit* target)
  {
    if (objectId)
      *objectId = target ? target->GetObjectId() : -1;
    if (kind)
      *kind = target ? static_cast<int>(target->GetUnitKind()) : -1;
    if (faction)
      *faction = target ? static_cast<int>(target->GetFaction()) : -1;
    if (playerId)
      *playerId = target ? target->GetPlayerId() : -1;
  }
#endif

  inline bool CheckHero(const NWorld::PFBaseHero* pHero, int userId)
  {
    if (g_disableHeroCheck)
      return true;

    if (pHero)
      if (const NWorld::PFPlayer * pPlayer = pHero->GetPlayer())
        if (pPlayer->GetUserID() == userId || !pPlayer->IsPlaying() || pPlayer->IsBot())
          return true;

    DebugTrace("Wrong hero received in command (uid=%d)", userId);
    return false;
  }

  inline bool CheckAdventureControls( const NWorld::PFBaseHero* pHero, const bool issuedByScript )
  {
    return issuedByScript || !pHero->IsLocal() || NGameX::AdventureScreen::Instance()->GetAdventureControlsEnabled();
  }

  inline bool CheckPlayerControlAndMinigame( const NWorld::PFBaseHero* pHero )
  {
    return !pHero->CheckFlagType( NDb::UNITFLAGTYPE_FORBIDPLAYERCONTROL ) && !pHero->CheckFlagType( NDb::UNITFLAGTYPE_INMINIGAME );
  }

  bool UseTalent( NWorld::PFBaseMaleHero * pHero, NWorld::PFTalent * talent,  NWorld::Target const & target )
  {
    if ( pHero->IsDead() )
      return false;

    NI_VERIFY( talent, "Wrong talent", return false; );

    if ( !talent->CanBeUsed() )
      return false;

    //Redunant target type check to prevent crashes in formulas (NUM_TASK)
    unsigned targetTypes = talent->GetTargetType();
    if ( targetTypes ) //else - ability does not need target
    {
      const float useRange = talent->GetUseRange(target);
      if ( !useRange && !target.IsObject() ) //Ability wants an onject as a target
        return false;

      if ( target.IsObject() || target.IsUnit() )
      {
        if ( !target.IsObjectValid(true) )
          return false;

        // NUM_TASK
        if (!NWorld::CheckValidAbilityTargetCondition()(target, talent))
          return false;

        const bool bSelf = pHero == target.GetObject();
        if ( bSelf )
        {
          if ( (targetTypes & NDb::SPELLTARGET_SELF) == 0 && !( talent->IsMultiState() && talent->IsOn() ) )
            return false;
        }
        else
        {
          if ( (targetTypes & (1 << target.GetObject()->GetUnitKind())) == 0 )
            return false;

          if ( target.GetObject()->GetUnitKind() == NDb::UNITTYPE_FLAGPOLE )
            if ( target.GetObject()->GetFaction() == NDb::FACTION_NEUTRAL )
              return false;

          const bool bSameFaction = pHero->GetFaction() == target.GetObject()->GetFaction();
          if ( !(bSameFaction && (targetTypes & NDb::SPELLTARGET_ALLY)
            || !bSameFaction && (targetTypes & NDb::SPELLTARGET_ENEMY)) )
          {
            return false;
          }
        }
      }
    }

    pHero->EnqueueState( new NWorld::PFHeroUseTalentState( pHero, talent, target ), true );
    return true;
  }

} // namespace


namespace NWorld
{
#if defined(PW_LINUX_NULL_RENDER)
  LinuxHeroMoveCommandDiagnostics GetLinuxHeroMoveCommandDiagnostics()
  {
    return g_linuxHeroMoveCommandDiagnostics;
  }

  void ResetLinuxHeroMoveCommandDiagnostics()
  {
    g_linuxHeroMoveCommandDiagnostics = LinuxHeroMoveCommandDiagnostics();
  }

  LinuxHeroGameplayCommandDiagnostics GetLinuxHeroGameplayCommandDiagnostics()
  {
    return g_linuxHeroGameplayCommandDiagnostics;
  }

  void ResetLinuxHeroGameplayCommandDiagnostics()
  {
    g_linuxHeroGameplayCommandDiagnostics = LinuxHeroGameplayCommandDiagnostics();
  }

  void RecordLinuxAIConsumableInventoryProbe(
    PFBaseMaleHero const* hero,
    int requestedType,
    int slotCount,
    int matches,
    int firstIndex,
    int firstType,
    int firstQuantity)
  {
    g_linuxHeroGameplayCommandDiagnostics.aiConsumableProbeHeroObjectId = hero ? hero->GetObjectId() : -1;
    g_linuxHeroGameplayCommandDiagnostics.aiConsumableProbeRequestedType = requestedType;
    g_linuxHeroGameplayCommandDiagnostics.aiConsumableProbeSlotCount = slotCount;
    g_linuxHeroGameplayCommandDiagnostics.aiConsumableProbeMatches = matches;
    g_linuxHeroGameplayCommandDiagnostics.aiConsumableProbeFirstIndex = firstIndex;
    g_linuxHeroGameplayCommandDiagnostics.aiConsumableProbeFirstType = firstType;
    g_linuxHeroGameplayCommandDiagnostics.aiConsumableProbeFirstQuantity = firstQuantity;
  }
#endif

  DEFINE_2_PARAM_CMD_CHECK( 0x2C5B9CC0, CmdCombatMoveHero, CPtr<PFBaseHero>, pHero, CVec2, target);
  DEFINE_3_PARAM_CMD_CHECK( 0x2C59C380, CmdMoveHero,       CPtr<PFBaseHero>, pHero, CVec2, target, bool, issuedByScript);
  DEFINE_1_PARAM_CMD_CHECK( 0x2C5B9481, CmdStopHero,       CPtr<PFBaseHero>, pHero);
  DEFINE_3_PARAM_CMD_CHECK( 0x2C5B9480, CmdAttackTarget,   CPtr<PFBaseHero>, pHero, CPtr<PFBaseUnit>, pTarget, bool, issuedByScript);
  DEFINE_4_PARAM_CMD_CHECK( 0x2C61F340, CmdBuyConsumable,    CPtr<PFBaseHero>, pHero, CPtr<PFShop>, pShop, int, index, int, slotIndex);
  
  DEFINE_5_PARAM_CMD_CHECK( 0x2C6A2BC0, CmdFollowUnit,       CPtr<PFBaseHero>, pHero, CPtr<PFBaseUnit>, pUnit, float, followRange, float, forceFollowRange, bool, issuedByScript );
  DEFINE_1_PARAM_CMD_CHECK( 0x2C6BBB40, CmdHold,             CPtr<PFBaseHero>, pHero );
  DEFINE_3_PARAM_CMD_CHECK( 0xB76AAC0,  CmdRaiseFlag,        CPtr<PFBaseHero>, pHero, CPtr<PFFlagpole>, pFlagpole, bool, issuedByScript);

  DEFINE_3_PARAM_CMD_CHECK( 0xF5CC401,  CmdUseConsumable,    CPtr<PFBaseMaleHero>, pHero, INT32, slot, AbilityTarget, target);
  DEFINE_3_PARAM_CMD_CHECK( 0xB695200,  CmdActivateTalent,   CPtr<PFBaseMaleHero>, pHero, INT32, level, INT32, slot);
  DEFINE_5_PARAM_CMD_CHECK( 0xB695201,  CmdUseTalent,        CPtr<PFBaseMaleHero>, pHero, INT32, level, INT32, slot, AbilityTarget, target, bool, issuedByScript );
  DEFINE_3_PARAM_CMD_CHECK( 0x6294CD01, CmdUsePortal,        CPtr<PFBaseMaleHero>, pHero, AbilityTarget, target, bool, issuedByScript );

  DEFINE_2_PARAM_CMD_CHECK( 0xA05CCB40, CmdPickupObject,   CPtr<PFBaseHero>, pHero, CPtr<PFPickupableObjectBase>, pPickupable);
  DEFINE_5_PARAM_CMD_CHECK( 0xB622CC0,  CmdMinimapSignal,  CPtr<PFBaseHero>, pHero, CPtr<PFBaseUnit>, pSelected, Target, target, NDb::EFaction, faction, bool, issuedByScript);

  DEFINE_2_PARAM_CMD_CHECK( 0x9D62D440, CmdInitMinigame,   CPtr<PFEaselPlayer>, easelPlayer, INT32, objId );
  DEFINE_1_PARAM_CMD_CHECK( 0x9D62D441, CmdLeaveMinigame,  CPtr<PFEaselPlayer>, easelPlayer );

  //DEFINE_2_PARAM_CMD_CHECK( 0xF659340,  CmdDenyBuilding,   CPtr<PFBaseHero>, pHero, CPtr<PFBuilding>, pBuilding );
  //DEFINE_2_PARAM_CMD_CHECK( 0x2C6614C0, CmdEmote,          CPtr<PFBaseHero>, pHero, NDb::EEmotion, emotion );

  DEFINE_1_PARAM_CMD_CHECK( 0xE78854C0, CmdCancelChannelling, CPtr<PFBaseHero>, pHero );
  DEFINE_2_PARAM_CMD_CHECK( 0xE78B5B00, CmdUseUnit      , CPtr<PFBaseHero>, pHero, CPtr<PFBaseUnit>, pUnit );

  DEFINE_0_PARAM_CMD( 0x229AD400, CmdKeepAlive );

  DEFINE_1_PARAM_CMD_CHECK( 0x228DA404, CmdSetTimescale, float, scale );

  NCore::WorldCommand* CreateCmdMoveHero(PFBaseHero* pHero, const CVec2& target, bool issuedByScript)
  {
    if( IsValid( pHero ) )
    {
		  pHero->PlayAskSound( NDb::ASKSOUNDS_MOVE );

      return new CmdMoveHero( pHero, target, issuedByScript );
    }

    NI_ALWAYS_ASSERT("Hero object must exist!");
    return 0;
  }

  bool CmdMoveHero::CanExecute() const
  {
    pHero->EventHappened( NWorld::PFHeroEventWantMoveTo( target ) );

#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroMoveCommandDiagnostics.canExecuteChecks;
    CaptureLinuxHeroMoveCommandContext(
      &g_linuxHeroMoveCommandDiagnostics,
      pHero,
      GetId(),
      target,
      issuedByScript);
    const bool heroCheck = CheckHero(pHero, GetId());
    const bool controlsCheck = heroCheck && CheckAdventureControls( pHero, issuedByScript );
    const bool canMoveCheck = controlsCheck && pHero->CanMove();
    g_linuxHeroMoveCommandDiagnostics.lastHeroCheck = heroCheck ? 1 : 0;
    g_linuxHeroMoveCommandDiagnostics.lastControlsCheck = controlsCheck ? 1 : 0;
    g_linuxHeroMoveCommandDiagnostics.lastCanMoveCheck = canMoveCheck ? 1 : 0;
    if (heroCheck && controlsCheck && canMoveCheck)
      ++g_linuxHeroMoveCommandDiagnostics.canExecuteAccepted;
    return heroCheck && controlsCheck && canMoveCheck;
#else
    return CheckHero(pHero, GetId()) && CheckAdventureControls( pHero, issuedByScript ) && pHero->CanMove();
#endif
  }

  void CmdMoveHero::Execute( NCore::IWorldBase* /*pHolder*/)
  {
    LogLogicObject(pHero, NStr::StrFmt("CMD MOVE to (%f %f)", target.x, target.y), false);

#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroMoveCommandDiagnostics.executeCalls;
    CaptureLinuxHeroMoveCommandContext(
      &g_linuxHeroMoveCommandDiagnostics,
      pHero,
      GetId(),
      target,
      issuedByScript);
#endif
#if defined(PW_LINUX_NULL_RENDER)
    pHero->DropTarget();
#endif
    pHero->Move(target);
#if defined(PW_LINUX_NULL_RENDER)
    const CVec2 afterPosition = pHero->GetPosition().AsVec2D();
    g_linuxHeroMoveCommandDiagnostics.lastAfterX = afterPosition.x;
    g_linuxHeroMoveCommandDiagnostics.lastAfterY = afterPosition.y;
    g_linuxHeroMoveCommandDiagnostics.lastSpeedAfter = pHero->GetUnitSpeed();
    g_linuxHeroMoveCommandDiagnostics.lastMoveFlagsAfter = pHero->GetMoveFlags();
    g_linuxHeroMoveCommandDiagnostics.lastIsMovingAfter = pHero->IsMoving() ? 1 : 0;
#endif
  }

  NCore::WorldCommand* CreateCmdStopHero(PFBaseHero* pHero)
  {
    NI_ASSERT(0 != pHero, "Hero object must exist!");
    if(0 != pHero)
      return new CmdStopHero( pHero );

    return 0;
  }

  bool CmdStopHero::CanExecute() const
  {
    return CheckHero(pHero, GetId()) && CheckPlayerControlAndMinigame( pHero );
  }

  void CmdStopHero::Execute( NCore::IWorldBase* /*pHolder*/)
  {
    if(!pHero->CheckFlagType(NDb::UNITFLAGTYPE_FORBIDMOVE) || pHero->IsHoldingPosition())
    {
      LogLogicObject(pHero, "CMD STOP", false);
      pHero->EventHappened( NWorld::PFHeroEventWantMoveTo( VNULL2 ) );
#if defined(PW_LINUX_NULL_RENDER)
      pHero->DropTarget();
#endif
      pHero->DoStop();
    }
  }

  NCore::WorldCommand* CreateCmdKeepAlive()
  {
    return new CmdKeepAlive();
  }

  void CmdKeepAlive::Execute( NCore::IWorldBase* /*pHolder*/)
  {
    // Do nothing. It's just an anti-afk message.
  }

  NCore::WorldCommand* CreateCmdCombatMoveHero(PFBaseHero* pHero, const CVec2& target)
  {
    if( NULL != pHero)
    {
      return new CmdCombatMoveHero( pHero, target );
    }

    NI_ALWAYS_ASSERT("Hero object must exist!");
    return 0;
  }

  NCore::WorldCommand* CreateCmdSetTimescale(float timescale)
  {
    if (timescale < 0.5f || timescale > 1.5f)
    {
      NI_ALWAYS_ASSERT("Wrong timescale!");
      return 0;
    }

    return new CmdSetTimescale( timescale );
  }

  bool CmdSetTimescale::CanExecute() const
  {
    return true;
  }

  void CmdSetTimescale::Execute( NCore::IWorldBase * pWorld)
  {
    CPtr<PFWorld> world = dynamic_cast<PFWorld*>(pWorld);
    if(IsValid(world))
    {
      float s = Clamp(scale, 0.5f, 1.5f);
      world->GetIAdventureScreen()->SetTimeScale(s);
      world->GetIAdventureScreen()->OnTimeScaleChanged(s);
    }
  }

  bool CmdCombatMoveHero::CanExecute() const
  {
    return CheckHero(pHero, GetId()) && CheckPlayerControlAndMinigame( pHero );
  }

  void CmdCombatMoveHero::Execute( NCore::IWorldBase* pWorld)
  {
    if ( !pHero->ControlsMount() )
    {
      LogLogicObject(pHero, "CMD COMBAT MOVE", false);
#if defined(PW_LINUX_NULL_RENDER)
      pHero->DropTarget();
#endif
      pHero->EnqueueState( new PFBaseUnitCombatMoveState( dynamic_cast<PFWorld*>( pWorld ), CPtr<PFBaseMovingUnit>( pHero ), target ), true );
    }
    else
    {
      CPtr<PFBaseMovingUnit> const& pMount = pHero->GetMount();
#if defined(PW_LINUX_NULL_RENDER)
      if (IsValid(pMount))
        pMount->DropTarget();
#endif
      pMount->EnqueueState( new PFBaseUnitCombatMoveState( dynamic_cast<PFWorld*>( pWorld ), pMount, target ), true );
    }
  }

  NCore::WorldCommand* CreateCmdAttackTarget( PFBaseHero* pHero,  PFBaseUnit* pTarget, bool issuedByScript )
  {
    if(NULL != pHero && NULL != pTarget)
    {
      // copy-paste from execute
      pHero->PlayAskSound(NDb::ASKSOUNDS_ATTACK);

      return new CmdAttackTarget( pHero, pTarget, issuedByScript );
    }

    NI_ASSERT(0 != pHero,   "Hero object must exist!");
    NI_ASSERT(0 != pTarget, "Target object must exist!");

    return 0;
  }

  bool CmdAttackTarget::CanExecute() const
  {
    if ( CheckHero(pHero, GetId()) && CheckAdventureControls( pHero, issuedByScript ) && CheckPlayerControlAndMinigame( pHero ) )
    {
      if ( !IsUnitValid(pTarget) )
        return false;

      const bool bSameFaction = pTarget->GetFaction() == pHero->GetFaction();

      if ( pTarget->GetUnitKind() == NDb::UNITTYPE_FLAGPOLE )
        if ( pTarget->GetFaction() == NDb::FACTION_NEUTRAL || bSameFaction )
          return false;

      return true;
    }
    return false;
  }

  void CmdAttackTarget::Execute( NCore::IWorldBase* pWorld)
  {
    if ( !IsUnitValid(pHero) )
      return;

    if ( !pHero->ControlsMount() )
    {
      if ( !pHero->CanAttackTarget( pTarget ) || pTarget==pHero || pHero->CheckFlagType(NDb::UNITFLAGTYPE_FORBIDATTACK) )
        return;

      LogLogicObject(pHero, "CMD ATTACT TARGET", false);
      pHero->OnTarget(pTarget, true);
#if defined(PW_LINUX_NULL_RENDER)
      pHero->AssignTarget(pTarget, true);
      if (pHero->CanAttackTarget(pTarget) &&
          pHero->IsTargetInAttackRange(pTarget, true) &&
          pHero->IsReadyToAttack())
      {
        pHero->DoAttack(false);
      }
#endif
    }
    else
    {
      CPtr<PFBaseMovingUnit> const& pMount = pHero->GetMount();
      if ( !pMount->CanAttackTarget( pTarget ) || pTarget==pMount || pMount->CheckFlagType(NDb::UNITFLAGTYPE_FORBIDATTACK) )
        return;

      // code patch from PFBaseHero::OnTarget
      const IPFState* state = pMount->GetCurrentState();
      if ( !state || state->GetTypeId() != PFBaseUnitAttackState::typeId || static_cast<const PFBaseUnitAttackState*>( state )->GetTarget() != pTarget )
        pMount->EnqueueState( new PFBaseUnitAttackState(dynamic_cast<PFWorld*>( pWorld ), pMount, pTarget, true), true );
    }
  }

  NCore::WorldCommand* CreateCmdUseConsumable( PFBaseMaleHero* pHero, INT32 slot, Target const & target )
  {
    NI_VERIFY(NULL != pHero, "Hero object must exist!", return 0; );
    return new CmdUseConsumable( pHero, slot, target );
  }

  bool CmdUseConsumable::CanExecute() const
  {
    const bool accepted = CheckHero(pHero, GetId()) && CheckPlayerControlAndMinigame( pHero );
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.useConsumableCanChecks;
    if (accepted)
      ++g_linuxHeroGameplayCommandDiagnostics.useConsumableCanAccepted;
    g_linuxHeroGameplayCommandDiagnostics.useConsumableSlot = slot;
    g_linuxHeroGameplayCommandDiagnostics.useConsumableTargetType = static_cast<int>(target.GetType());
    g_linuxHeroGameplayCommandDiagnostics.useConsumableTargetObjectId = GetLinuxCommandTargetObjectId(target);
    g_linuxHeroGameplayCommandDiagnostics.useConsumableTargetFaction = GetLinuxCommandTargetFaction(target);
#endif
    return accepted;
  }

  void CmdUseConsumable::Execute( NCore::IWorldBase* /*pHolder*/)
  {
    LogLogicObject(pHero, "CMD USE ARTEFACT", false);
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.useConsumableExecuteCalls;
    g_linuxHeroGameplayCommandDiagnostics.useConsumableSlot = slot;
    g_linuxHeroGameplayCommandDiagnostics.useConsumableTargetType = static_cast<int>(target.GetType());
    g_linuxHeroGameplayCommandDiagnostics.useConsumableTargetObjectId = GetLinuxCommandTargetObjectId(target);
    g_linuxHeroGameplayCommandDiagnostics.useConsumableTargetFaction = GetLinuxCommandTargetFaction(target);
#endif
    const bool canUseConsumable = !pHero->IsDead() && pHero->CanUseConsumable(slot);
#if defined(PW_LINUX_NULL_RENDER)
    g_linuxHeroGameplayCommandDiagnostics.useConsumableCanUse = canUseConsumable ? 1 : 0;
#endif
    if( canUseConsumable )
    {
#if defined(PW_LINUX_NULL_RENDER)
      CObj<PFAbilityInstance> instance = pHero->UseConsumable(slot, target, pHero->IsLocal());
      if (instance)
        ++g_linuxHeroGameplayCommandDiagnostics.useConsumableActionAccepted;
#else
      pHero->EnqueueState( new PFHeroUseConsumableState( pHero, slot, target ), true );
#endif
    }
  }

  NCore::WorldCommand* CreateCmdPickupObject( PFBaseHero* pHero, INT32 objId )
  {
    if( NULL == pHero)
    {
      NI_ALWAYS_ASSERT("Hero object must exist!");
      return 0;
    }

    PFWorld* pWorld = pHero->GetWorld();
    PFPickupableObjectBase* pPickupable = (NULL == pWorld) ? NULL :
      dynamic_cast<PFPickupableObjectBase*>( pWorld->GetObjectById(objId) );

    if( NULL == pPickupable )
      return 0; // object can be already picked up

    return new CmdPickupObject( pHero, pPickupable );
  }

  bool CmdPickupObject::CanExecute() const
  {
    const bool accepted = CheckHero(pHero, GetId()) && CheckPlayerControlAndMinigame( pHero );
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.pickupObjectCanChecks;
    if (accepted)
      ++g_linuxHeroGameplayCommandDiagnostics.pickupObjectCanAccepted;
    g_linuxHeroGameplayCommandDiagnostics.pickupObjectId = IsValid(pPickupable) ? pPickupable->GetObjectId() : -1;
#endif
    return accepted;
  }

  void CmdPickupObject::Execute( NCore::IWorldBase* pWorld )
  {
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.pickupObjectExecuteCalls;
    g_linuxHeroGameplayCommandDiagnostics.pickupObjectId = IsValid(pPickupable) ? pPickupable->GetObjectId() : -1;
#endif
    const bool canPickup = IsUnitValid(pHero) && IsValid(pPickupable) && pPickupable->CanBePickedUpBy( pHero );
#if defined(PW_LINUX_NULL_RENDER)
    g_linuxHeroGameplayCommandDiagnostics.pickupObjectCanPickup = canPickup ? 1 : 0;
#endif
    if( !canPickup )
      return;  // dead or invalid heroes can not pickup objects

    LogLogicObject(pHero, "CMD PICKUP", false);
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.pickupObjectActionAccepted;
#endif
    pHero->EnqueueState( new PFHeroPickupObjectState( pHero, pPickupable ), true );
  }

  bool CmdFollowUnit::CanExecute() const
  {
    return CheckHero(pHero, GetId()) && CheckAdventureControls( pHero, issuedByScript ) && CheckPlayerControlAndMinigame( pHero );
  }

  void CmdFollowUnit::Execute( NCore::IWorldBase * pWorld)
  {
    if( IsUnitValid(pHero) && IsUnitValid(pUnit) && pUnit != pHero)
    {
#if defined(PW_LINUX_NULL_RENDER)
      pHero->DropTarget();
#endif
      pHero->EnqueueState(new PFHeroFollowUnitState(pHero, pUnit, followRange, forceFollowRange), true);
    }
  }

  NCore::WorldCommand* CreateCmdFollowUnit(PFBaseHero* pHero, PFBaseUnit* pUnit, float followRange, float forceFollowRange, bool issuedByScript)
  {
    if( NULL == pHero || pHero->IsDead() ||  NULL == pUnit || pUnit->IsDead() || pHero == pUnit || pUnit->GetMasterUnit() == pHero)
      return 0;

    return new CmdFollowUnit( pHero, pUnit, followRange, forceFollowRange, issuedByScript );
  }

  bool CmdHold::CanExecute() const
  {
    return CheckHero(pHero, GetId()) && CheckPlayerControlAndMinigame( pHero );
  }

  void CmdHold::Execute( NCore::IWorldBase * pWorld)
  {
    if( !IsUnitValid(pHero) )
      return;

    pHero->CancelChannelling();

    if ( !pHero->ControlsMount() )
    {
      if(!pHero->IsIdle())
      {
        CPtr<PFWorld> world = dynamic_cast<PFWorld*>(pWorld);
        if(IsValid(world))
        {
          pHero->EnqueueState( new PFHeroHoldState(world, pHero), true);
        }
      }
    }
    else
    {
      CPtr<PFBaseMovingUnit> const& pMount = pHero->GetMount();
      pMount->Stop();
      pMount->Cleanup();
    }

    pHero->EventHappened( NWorld::PFHeroEventWantMoveTo( VNULL2 ) );
  }

  NCore::WorldCommand* CreateCmdHold(PFBaseHero* pHero)
  {
    if( NULL == pHero || pHero->IsDead() || ( pHero->IsMounted() && !pHero->CanControlMount() ) )
      return 0;

    return new CmdHold( pHero );
  }

  bool CmdRaiseFlag::CanExecute() const
  {
    const bool accepted = CheckHero(pHero, GetId()) && CheckAdventureControls( pHero, issuedByScript ) && CheckPlayerControlAndMinigame( pHero );
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.raiseFlagCanChecks;
    if (accepted)
      ++g_linuxHeroGameplayCommandDiagnostics.raiseFlagCanAccepted;
    g_linuxHeroGameplayCommandDiagnostics.raiseFlagObjectId = IsValid(pFlagpole) ? pFlagpole->GetObjectId() : -1;
    g_linuxHeroGameplayCommandDiagnostics.raiseFlagFaction = IsValid(pFlagpole) ? static_cast<int>(pFlagpole->GetFaction()) : -1;
#endif
    return accepted;
  }

  void CmdRaiseFlag::Execute( NCore::IWorldBase * pWorld)
  {
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.raiseFlagExecuteCalls;
    g_linuxHeroGameplayCommandDiagnostics.raiseFlagObjectId = IsValid(pFlagpole) ? pFlagpole->GetObjectId() : -1;
    g_linuxHeroGameplayCommandDiagnostics.raiseFlagFaction = IsValid(pFlagpole) ? static_cast<int>(pFlagpole->GetFaction()) : -1;
#endif
    const bool canRaise = IsUnitValid(pHero) && IsUnitValid(pFlagpole) && pFlagpole->CanRaise( pHero->GetFaction() ) && pHero->CheckFlag(NDb::UNITFLAG_FORBIDINTERACT) == false;
#if defined(PW_LINUX_NULL_RENDER)
    g_linuxHeroGameplayCommandDiagnostics.raiseFlagCanRaise = canRaise ? 1 : 0;
#endif
    if (canRaise)
    {
#if defined(PW_LINUX_NULL_RENDER)
      ++g_linuxHeroGameplayCommandDiagnostics.raiseFlagActionAccepted;
#endif
      pHero->EnqueueState( new PFCreatureRaiseFlagState(pHero, pFlagpole), true );
    }
  }

  NCore::WorldCommand* CreateCmdRaiseFlag(PFBaseHero *pHero, PFFlagpole *pFlagpole, bool issuedByScript)
  {
    if (NULL == pHero || NULL == pFlagpole || pFlagpole->GetFaction() != NDb::FACTION_NEUTRAL)
      return 0;
    return new CmdRaiseFlag( pHero, pFlagpole, issuedByScript );
  }

  NCore::WorldCommand* CreateCmdInitMinigame( PFEaselPlayer* easelPlayer, INT32 objId )
  {
    if( NULL == easelPlayer )
    {
      NI_ALWAYS_ASSERT("Priestess object must exist!");
      return 0;
    }

    return new CmdInitMinigame( easelPlayer, objId );
  }

  bool CmdInitMinigame::CanExecute() const
  {
    const bool accepted = CheckHero(dynamic_cast<PFBaseHero*>(easelPlayer.GetPtr()), GetId())
             && !easelPlayer->CheckFlagType( NDb::UNITFLAGTYPE_FORBIDPLAYERCONTROL );
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.initMinigameCanChecks;
    if (accepted)
      ++g_linuxHeroGameplayCommandDiagnostics.initMinigameCanAccepted;
    g_linuxHeroGameplayCommandDiagnostics.initMinigameObjectId = objId;
#endif
    return accepted;
  }

  void CmdInitMinigame::Execute( NCore::IWorldBase* pWorld )
  {
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.initMinigameExecuteCalls;
    g_linuxHeroGameplayCommandDiagnostics.initMinigameObjectId = objId;
#endif
    PFWorld* pPFWorld = dynamic_cast<PFWorld*>(pWorld);
    NI_VERIFY( pPFWorld, "Another paranoid validity check ... ", return; );

    if( !IsUnitValid(easelPlayer) )
      return;

#if defined(PW_LINUX_NULL_RENDER)
    PFMinigamePlace* minigamePlace = dynamic_cast<PFMinigamePlace*>(pPFWorld->GetObjectById(objId));
    if (!minigamePlace)
      minigamePlace = pPFWorld->FindLinuxFirstAvailableMinigamePlaceForHero(easelPlayer.GetPtr());

    const bool available = minigamePlace && minigamePlace->IsAvailable();
    const bool canUse = available && minigamePlace->CanBeUsedBy(easelPlayer.GetPtr());
    PFAIWorld* aiWorld = pPFWorld->GetAIWorld();
    const bool battleReady = !aiWorld || aiWorld->GetBattleStartDelay() <= 0;
    g_linuxHeroGameplayCommandDiagnostics.initMinigameAvailable = available ? 1 : 0;
    g_linuxHeroGameplayCommandDiagnostics.initMinigameCanUse = canUse ? 1 : 0;
    g_linuxHeroGameplayCommandDiagnostics.initMinigameBattleReady = battleReady ? 1 : 0;
    g_linuxHeroGameplayCommandDiagnostics.initMinigameObjectId =
      minigamePlace ? minigamePlace->GetObjectId() : -1;
    if (!available || !canUse || !battleReady)
      return;

    PFMinigamePlace* heroMinigameBefore = easelPlayer->GetMinigamePlace();
    PFEaselPlayer* placeUserBefore = minigamePlace->CurrentEaselPlayer();
    g_linuxHeroGameplayCommandDiagnostics.initMinigameHeroPlaceBefore =
      heroMinigameBefore ? heroMinigameBefore->GetObjectId() : -1;
    g_linuxHeroGameplayCommandDiagnostics.initMinigamePlaceUserBefore =
      placeUserBefore ? placeUserBefore->GetObjectId() : -1;
    g_linuxHeroGameplayCommandDiagnostics.initMinigameHeroIsolatedBefore =
      easelPlayer->IsIsolated() ? 1 : 0;

    if ( PFInteractObjectState* st = dynamic_cast<PFInteractObjectState*>(easelPlayer->GetCurrentState()) )
    {
      st->NeedStopOnLeave( false );
    }

    minigamePlace->Use(easelPlayer.GetPtr());
    PFMinigamePlace* heroMinigameAfter = easelPlayer->GetMinigamePlace();
    PFEaselPlayer* placeUserAfter = minigamePlace->CurrentEaselPlayer();
    g_linuxHeroGameplayCommandDiagnostics.initMinigameHeroPlaceAfter =
      heroMinigameAfter ? heroMinigameAfter->GetObjectId() : -1;
    g_linuxHeroGameplayCommandDiagnostics.initMinigamePlaceUserAfter =
      placeUserAfter ? placeUserAfter->GetObjectId() : -1;
    g_linuxHeroGameplayCommandDiagnostics.initMinigameHeroIsolatedAfter =
      easelPlayer->IsIsolated() ? 1 : 0;
    if (g_linuxHeroGameplayCommandDiagnostics.initMinigameHeroPlaceBefore !=
          g_linuxHeroGameplayCommandDiagnostics.initMinigameHeroPlaceAfter ||
        g_linuxHeroGameplayCommandDiagnostics.initMinigamePlaceUserBefore !=
          g_linuxHeroGameplayCommandDiagnostics.initMinigamePlaceUserAfter ||
        g_linuxHeroGameplayCommandDiagnostics.initMinigameHeroIsolatedBefore !=
          g_linuxHeroGameplayCommandDiagnostics.initMinigameHeroIsolatedAfter)
    {
      ++g_linuxHeroGameplayCommandDiagnostics.initMinigameActionAccepted;
    }
#else
    if ( PF_Core::WorldObjectBase* pWO = pPFWorld->GetObjectById( objId ) )
    {
#ifndef VISUAL_CUTTED
      CDynamicCast<PFMinigamePlace> minigamePlace = pWO;

      NI_VERIFY(minigamePlace, "Object is not minigame place!", return; );

      const bool available = minigamePlace->IsAvailable();
      const bool canUse = minigamePlace->CanBeUsedBy( easelPlayer );
      const bool battleReady = pPFWorld->GetAIWorld()->GetBattleStartDelay() <= 0;
      if ( available && canUse && battleReady )
      {
        LogLogicObject(easelPlayer, "CMD MGLOBBY ENTER", false);
        if ( PFInteractObjectState* st = dynamic_cast<PFInteractObjectState*>(easelPlayer->GetCurrentState()) )
        {
          st->NeedStopOnLeave( false );
        }
        easelPlayer->EnqueueState( new PFHeroUseUnitState( easelPlayer, minigamePlace), true );
      }
#endif
    }
#endif
  }

  NCore::WorldCommand* CreateCmdLeaveMinigame( PFEaselPlayer* easelPlayer )
  {
    if( NULL == easelPlayer )
    {
      NI_ALWAYS_ASSERT("Priestess object must exist!");
      return 0;
    }

    return new CmdLeaveMinigame( easelPlayer );
  }

  bool CmdLeaveMinigame::CanExecute() const
  {
    PFBaseHero* hero = dynamic_cast<PFBaseHero*>(easelPlayer.GetPtr());
    const bool accepted = CheckHero(hero, GetId())
      && IsValid(easelPlayer)
      && !easelPlayer->IsDead()
      && (easelPlayer->GetMinigamePlace()
        || easelPlayer->IsIsolated()
        || easelPlayer->CheckFlag(NDb::UNITFLAG_INMINIGAME));
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.leaveMinigameCanChecks;
    if (accepted)
      ++g_linuxHeroGameplayCommandDiagnostics.leaveMinigameCanAccepted;
    g_linuxHeroGameplayCommandDiagnostics.leaveMinigameHeroObjectId =
      IsValid(easelPlayer) ? easelPlayer->GetObjectId() : -1;
#endif
    return accepted;
  }

  void CmdLeaveMinigame::Execute( NCore::IWorldBase* pWorld )
  {
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.leaveMinigameExecuteCalls;
#endif
    if( !IsUnitValid(easelPlayer) )
      return;

    PFMinigamePlace* minigamePlaceBefore = easelPlayer->GetMinigamePlace();
#if defined(PW_LINUX_NULL_RENDER)
    PFEaselPlayer* placeUserBefore =
      minigamePlaceBefore ? minigamePlaceBefore->CurrentEaselPlayer() : 0;
    g_linuxHeroGameplayCommandDiagnostics.leaveMinigameHeroObjectId =
      easelPlayer->GetObjectId();
    g_linuxHeroGameplayCommandDiagnostics.leaveMinigameHeroPlaceBefore =
      minigamePlaceBefore ? minigamePlaceBefore->GetObjectId() : -1;
    g_linuxHeroGameplayCommandDiagnostics.leaveMinigamePlaceUserBefore =
      placeUserBefore ? placeUserBefore->GetObjectId() : -1;
    g_linuxHeroGameplayCommandDiagnostics.leaveMinigameHeroIsolatedBefore =
      easelPlayer->IsIsolated() ? 1 : 0;
    g_linuxHeroGameplayCommandDiagnostics.leaveMinigameHeroFlagBefore =
      easelPlayer->CheckFlag(NDb::UNITFLAG_INMINIGAME) ? 1 : 0;
    g_linuxHeroGameplayCommandDiagnostics.leaveMinigameVisualStateBefore =
      minigamePlaceBefore ? static_cast<int>(minigamePlaceBefore->GetVisualState()) : -1;
    g_linuxHeroGameplayCommandDiagnostics.leaveMinigamePlacementApplyBefore =
      minigamePlaceBefore ? static_cast<int>(minigamePlaceBefore->GetPlacementApplyType()) : -1;
    const bool hadMinigame =
      minigamePlaceBefore ||
      easelPlayer->IsIsolated() ||
      easelPlayer->CheckFlag(NDb::UNITFLAG_INMINIGAME);
#endif

    const bool leftMinigame = easelPlayer->OnLeaveMinigameCmd();
#if !defined(PW_LINUX_NULL_RENDER)
    (void)leftMinigame;
#endif

#if defined(PW_LINUX_NULL_RENDER)
    PFMinigamePlace* minigamePlaceAfter = easelPlayer->GetMinigamePlace();
    PFEaselPlayer* placeUserAfter =
      minigamePlaceBefore ? minigamePlaceBefore->CurrentEaselPlayer() : 0;
    g_linuxHeroGameplayCommandDiagnostics.leaveMinigameHeroPlaceAfter =
      minigamePlaceAfter ? minigamePlaceAfter->GetObjectId() : -1;
    g_linuxHeroGameplayCommandDiagnostics.leaveMinigamePlaceUserAfter =
      placeUserAfter ? placeUserAfter->GetObjectId() : -1;
    g_linuxHeroGameplayCommandDiagnostics.leaveMinigameHeroIsolatedAfter =
      easelPlayer->IsIsolated() ? 1 : 0;
    g_linuxHeroGameplayCommandDiagnostics.leaveMinigameHeroFlagAfter =
      easelPlayer->CheckFlag(NDb::UNITFLAG_INMINIGAME) ? 1 : 0;
    g_linuxHeroGameplayCommandDiagnostics.leaveMinigameVisualStateAfter =
      minigamePlaceBefore ? static_cast<int>(minigamePlaceBefore->GetVisualState()) : -1;
    g_linuxHeroGameplayCommandDiagnostics.leaveMinigamePlacementApplyAfter =
      minigamePlaceBefore ? static_cast<int>(minigamePlaceBefore->GetPlacementApplyType()) : -1;
    if (hadMinigame &&
        leftMinigame &&
        g_linuxHeroGameplayCommandDiagnostics.leaveMinigameHeroPlaceAfter < 0 &&
        g_linuxHeroGameplayCommandDiagnostics.leaveMinigamePlaceUserAfter < 0 &&
        g_linuxHeroGameplayCommandDiagnostics.leaveMinigameHeroIsolatedAfter == 0 &&
        g_linuxHeroGameplayCommandDiagnostics.leaveMinigameHeroFlagAfter == 0)
    {
      ++g_linuxHeroGameplayCommandDiagnostics.leaveMinigameActionAccepted;
    }
#endif
  }

  NCore::WorldCommand* CreateCmdBuyConsumable( PFBaseHero* pHero, PFShop* pShop, int index, int slotIndex)
  {
    NI_VERIFY( pHero, "", return 0 );
    NI_VERIFY( pShop, "", return 0 );
    return new CmdBuyConsumable( pHero, pShop, index, slotIndex );
  }

  bool CmdBuyConsumable::CanExecute() const
  {
    const bool accepted = CheckHero(pHero, GetId()) && CheckPlayerControlAndMinigame( pHero );
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.buyConsumableCanChecks;
    if (accepted)
      ++g_linuxHeroGameplayCommandDiagnostics.buyConsumableCanAccepted;
    g_linuxHeroGameplayCommandDiagnostics.buyConsumableShopObjectId = IsValid(pShop) ? pShop->GetObjectId() : -1;
    g_linuxHeroGameplayCommandDiagnostics.buyConsumableIndex = index;
    g_linuxHeroGameplayCommandDiagnostics.buyConsumableSlotIndex = slotIndex;
    g_linuxHeroGameplayCommandDiagnostics.buyConsumableCommandHeroObjectId = IsValid(pHero) ? pHero->GetObjectId() : -1;
#endif
    return accepted;
  }

  void CmdBuyConsumable::Execute( NCore::IWorldBase* pWorld )
  {
#if defined(PW_LINUX_NULL_RENDER)
    PFBaseHero* commandHero = pHero.GetPtr();
    PFBaseHero* originalCommandHero = commandHero;
    PFShop* commandShop = pShop.GetPtr();
    int resolvedHeroFromWorld = 0;
    g_linuxHeroGameplayCommandDiagnostics.buyConsumableCommandHeroObjectId = commandHero ? commandHero->GetObjectId() : -1;
    if ( PFWorld* pPFWorld = dynamic_cast<PFWorld*>(pWorld) )
    {
      if ( commandHero )
      {
        if ( PFBaseHero* liveHero = dynamic_cast<PFBaseHero*>(pPFWorld->FindLinuxUnitByObjectId(commandHero->GetObjectId())) )
        {
          commandHero = liveHero;
          resolvedHeroFromWorld = commandHero != originalCommandHero ? 1 : 0;
        }
      }
      if ( commandShop )
      {
        if ( PFShop* liveShop = dynamic_cast<PFShop*>(pPFWorld->GetObjectById(commandShop->GetObjectId())) )
          commandShop = liveShop;
      }
    }
    g_linuxHeroGameplayCommandDiagnostics.buyConsumableLiveHeroObjectId = commandHero ? commandHero->GetObjectId() : -1;
    g_linuxHeroGameplayCommandDiagnostics.buyConsumableResolvedHeroFromWorld = resolvedHeroFromWorld;
#else
    PFBaseHero* commandHero = pHero.GetPtr();
    PFShop* commandShop = pShop.GetPtr();
#endif

#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.buyConsumableExecuteCalls;
    g_linuxHeroGameplayCommandDiagnostics.buyConsumableShopObjectId = commandShop ? commandShop->GetObjectId() : -1;
    g_linuxHeroGameplayCommandDiagnostics.buyConsumableIndex = index;
    g_linuxHeroGameplayCommandDiagnostics.buyConsumableSlotIndex = slotIndex;
#endif
    if (!commandShop)
      return;
    if ( !commandHero )
      return;

#if defined(PW_LINUX_NULL_RENDER)
    g_linuxHeroGameplayCommandDiagnostics.buyConsumableSlotCountBefore = commandHero->GetSlotCount();
    g_linuxHeroGameplayCommandDiagnostics.buyConsumableHeroPlayerId = commandHero->GetPlayerId();
    g_linuxHeroGameplayCommandDiagnostics.buyConsumableHeroUserId = -1;
    if ( const PFPlayer* player = commandHero->GetPlayer() )
      g_linuxHeroGameplayCommandDiagnostics.buyConsumableHeroUserId = player->GetUserID();
#endif
    const bool canBuy = commandShop->CanBuyConsumable(commandHero, index);
#if defined(PW_LINUX_NULL_RENDER)
    g_linuxHeroGameplayCommandDiagnostics.buyConsumableCanBuy = canBuy ? 1 : 0;
#endif
    if (canBuy)
    {
      NDb::Ptr<NDb::Consumable> pConsumableDesc = commandShop->GetConsumableDesc(index);

      LogLogicObject(commandHero, "CMD BUY ARTEFACT", false);
      
      const bool took = commandHero->TakeConsumable( pConsumableDesc, 1, NDb::CONSUMABLEORIGIN_SHOP, slotIndex );
#if defined(PW_LINUX_NULL_RENDER)
      g_linuxHeroGameplayCommandDiagnostics.buyConsumableTook = took ? 1 : 0;
      g_linuxHeroGameplayCommandDiagnostics.buyConsumableSlotCountAfter = commandHero->GetSlotCount();
      g_linuxHeroGameplayCommandDiagnostics.buyConsumableResultSlotIndex = slotIndex >= 0 ? slotIndex : commandHero->GetSlotCount() - 1;
      g_linuxHeroGameplayCommandDiagnostics.buyConsumableResultSlotType = -1;
      g_linuxHeroGameplayCommandDiagnostics.buyConsumableResultSlotQuantity = 0;
      if ( PFConsumable const* pBoughtConsumable = commandHero->GetConsumable(g_linuxHeroGameplayCommandDiagnostics.buyConsumableResultSlotIndex) )
        g_linuxHeroGameplayCommandDiagnostics.buyConsumableResultSlotQuantity = pBoughtConsumable->GetQuantity();
#endif
      if ( !took )
      {
        commandHero->GetWorld()->GetIAdventureScreen()->NotifyOfSimpleUIEvent( commandHero, NDb::ERRORMESSAGETYPE_OUTOFINVENTORY);
        return;
      }

#if defined(PW_LINUX_NULL_RENDER)
      ++g_linuxHeroGameplayCommandDiagnostics.buyConsumableActionAccepted;
#endif
      int cost = commandHero->GetConsumableCost(pConsumableDesc);
      commandHero->TakeGold( cost);

      StatisticService::RPC::SessionEventInfo params;
      params.intParam1 = pConsumableDesc->GetDBID().GetHashKey();
      params.intParam2 = cost;
      commandHero->LogSessionEvent(SessionEventType::ConsumableBought, params);
    }
  }

  bool CmdActivateTalent::CanExecute() const
  {
    const bool accepted = CheckHero(pHero, GetId());
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.activateTalentCanChecks;
    if (accepted)
      ++g_linuxHeroGameplayCommandDiagnostics.activateTalentCanAccepted;
    g_linuxHeroGameplayCommandDiagnostics.activateTalentLevel = level;
    g_linuxHeroGameplayCommandDiagnostics.activateTalentSlot = slot;
#endif
    return accepted;
  }

  void CmdActivateTalent::Execute( NCore::IWorldBase * pWorld)
  {
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.activateTalentExecuteCalls;
    g_linuxHeroGameplayCommandDiagnostics.activateTalentLevel = level;
    g_linuxHeroGameplayCommandDiagnostics.activateTalentSlot = slot;
    if (IsValid(pHero))
    {
      g_linuxHeroGameplayCommandDiagnostics.activateTalentHeroLevelBefore = pHero->GetNaftaLevel();
      g_linuxHeroGameplayCommandDiagnostics.activateTalentDevPointsBefore = pHero->GetDevPoints();
      g_linuxHeroGameplayCommandDiagnostics.activateTalentGoldBefore = pHero->GetGold();
    }
#endif
    const bool canActivate = IsValid(pHero) && pHero->CanActivateTalent(level, slot) == ETalentActivation::Ok;
#if defined(PW_LINUX_NULL_RENDER)
    g_linuxHeroGameplayCommandDiagnostics.activateTalentCanActivate = canActivate ? 1 : 0;
#endif
    if ( canActivate )
    {
      const bool activated = pHero->ActivateTalent(level, slot);
#if defined(PW_LINUX_NULL_RENDER)
      if (activated)
        ++g_linuxHeroGameplayCommandDiagnostics.activateTalentActionAccepted;
#endif
    }
#if defined(PW_LINUX_NULL_RENDER)
    if (IsValid(pHero))
    {
      g_linuxHeroGameplayCommandDiagnostics.activateTalentHeroLevelAfter = pHero->GetNaftaLevel();
      g_linuxHeroGameplayCommandDiagnostics.activateTalentDevPointsAfter = pHero->GetDevPoints();
      g_linuxHeroGameplayCommandDiagnostics.activateTalentGoldAfter = pHero->GetGold();
    }
#endif
  }

  NCore::WorldCommand* CreateCmdActivateTalent( PFBaseMaleHero *pHero, INT32 level, INT32 slot )
  {
    NI_VERIFY(NULL != pHero, "Hero object must exist!", return 0; );

    return new CmdActivateTalent( pHero, level, slot );
  }

  NCore::WorldCommand* CreateCmdUseTalent( PFBaseMaleHero *pHero, INT32 level, INT32 slot, Target const & target, bool issuedByScript )
  {
    TempDebugTrace(NStr::StrFmt("CreateCmdUseTalent() level=%d, slot=%d", level, slot));
    NI_VERIFY(NULL != pHero, "Hero object must exist!", return 0; );
    return new CmdUseTalent( pHero, level, slot, target, issuedByScript );
  }

  bool CmdUseTalent::CanExecute() const
  {
    TempDebugTrace(NStr::StrFmt("CmdUseTalent::CanExecute() level=%d, slot=%d", level, slot));
    const bool accepted = CheckHero(pHero, GetId()) && CheckAdventureControls( pHero, issuedByScript ) && CheckPlayerControlAndMinigame( pHero );
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.useTalentCanChecks;
    if (accepted)
      ++g_linuxHeroGameplayCommandDiagnostics.useTalentCanAccepted;
    g_linuxHeroGameplayCommandDiagnostics.useTalentLevel = level;
    g_linuxHeroGameplayCommandDiagnostics.useTalentSlot = slot;
    g_linuxHeroGameplayCommandDiagnostics.useTalentTargetType = static_cast<int>(target.GetType());
    g_linuxHeroGameplayCommandDiagnostics.useTalentTargetObjectId = GetLinuxCommandTargetObjectId(target);
    g_linuxHeroGameplayCommandDiagnostics.useTalentTargetFaction = GetLinuxCommandTargetFaction(target);
#endif
    return accepted;
  }

  void CmdUseTalent::Execute( NCore::IWorldBase * pWorld)
  {
    TempDebugTrace(NStr::StrFmt("CmdUseTalent::Execute() level=%d, slot=%d", level, slot));
    LogLogicObject(pHero, "CMD USE ARTEFACT", false);
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.useTalentExecuteCalls;
    g_linuxHeroGameplayCommandDiagnostics.useTalentLevel = level;
    g_linuxHeroGameplayCommandDiagnostics.useTalentSlot = slot;
    g_linuxHeroGameplayCommandDiagnostics.useTalentTargetType = static_cast<int>(target.GetType());
    g_linuxHeroGameplayCommandDiagnostics.useTalentTargetObjectId = GetLinuxCommandTargetObjectId(target);
    g_linuxHeroGameplayCommandDiagnostics.useTalentTargetFaction = GetLinuxCommandTargetFaction(target);
#endif

    if ( pHero->IsDead() )
      return;
   
    const bool used = UseTalent(pHero, pHero->GetTalent( level, slot ), target );
#if defined(PW_LINUX_NULL_RENDER)
    g_linuxHeroGameplayCommandDiagnostics.useTalentCanUse = used ? 1 : 0;
    if (used)
      ++g_linuxHeroGameplayCommandDiagnostics.useTalentActionAccepted;
#endif
  }

  NCore::WorldCommand* CreateCmdUsePortal( PFBaseMaleHero *pHero, Target const & target, bool issuedByScript )
  {
    TempDebugTrace(NStr::StrFmt("CreateCmdUsePortal() "));
    NI_VERIFY(NULL != pHero, "Hero object must exist!", return 0; );
    return new CmdUsePortal( pHero, target, issuedByScript );
  }

  bool CmdUsePortal::CanExecute() const
  {
    TempDebugTrace(NStr::StrFmt("CmdUsePortal::CanExecute() "));
    const bool accepted = CheckHero(pHero, GetId()) && CheckAdventureControls( pHero, issuedByScript ) && CheckPlayerControlAndMinigame( pHero );
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.usePortalCanChecks;
    if (accepted)
      ++g_linuxHeroGameplayCommandDiagnostics.usePortalCanAccepted;
    const CVec3 portalTarget = target.AcquirePosition();
    g_linuxHeroGameplayCommandDiagnostics.usePortalTargetX = portalTarget.x;
    g_linuxHeroGameplayCommandDiagnostics.usePortalTargetY = portalTarget.y;
#endif
    return accepted;
  }

  void CmdUsePortal::Execute( NCore::IWorldBase * pWorld)
  {
    TempDebugTrace(NStr::StrFmt("CmdUsePortal::Execute()"));
    LogLogicObject(pHero, "CMD USE PORTAL", false);
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.usePortalExecuteCalls;
    const CVec3 portalTarget = target.AcquirePosition();
    g_linuxHeroGameplayCommandDiagnostics.usePortalTargetX = portalTarget.x;
    g_linuxHeroGameplayCommandDiagnostics.usePortalTargetY = portalTarget.y;
#endif

    const bool used = UseTalent(pHero, pHero->GetPortal(), target );
#if defined(PW_LINUX_NULL_RENDER)
    g_linuxHeroGameplayCommandDiagnostics.usePortalCanUse = used ? 1 : 0;
    if (used)
      ++g_linuxHeroGameplayCommandDiagnostics.usePortalActionAccepted;
#endif
  }

  bool CmdMinimapSignal::CanExecute() const
  {
    return CheckHero(pHero, GetId()) && CheckAdventureControls( pHero, issuedByScript );
  }

  void CmdMinimapSignal::Execute( NCore::IWorldBase * pWorld)
  {
    PFWorld* pPFWorld = dynamic_cast<PFWorld*>(pWorld);
    pPFWorld->GetAIContainer()->OnMinimapSignal( pHero, pSelected, target );
#ifndef VISUAL_CUTTED
    NI_VERIFY(NDb::FACTION_FREEZE == faction || NDb::FACTION_BURN == faction, "Unknown faction!", return;)
    if ( target.IsPosition() )
    {
      if ( NGameX::Minimap *pMinimap = NGameX::AdventureScreen::Instance()->GetMinimap() )
      {
        pMinimap->AddSignal( target.AcquirePosition(), faction );
      }
    }
    else if ( target.IsUnitValid() && NGameX::AdventureScreen::Instance()->GetPlayerFaction() == faction )
    {
      if ( NGameX::MarkersController *pMarkersController = NGameX::AdventureScreen::Instance()->GetMarkersController() )
      {
        pMarkersController->AddMarker( target.GetUnit(), faction );
      }
    }
#endif
  }

  NCore::WorldCommand* CreateCmdMinimapSignal( PFBaseHero *pHero, PFBaseUnit* pSelected, Target const& target, NDb::EFaction faction, bool issuedByScript )
  {
    NI_VERIFY(NULL != pHero, "Hero object should exists", return 0);

#ifndef VISUAL_CUTTED
    return new CmdMinimapSignal( pHero, pSelected, target, faction, issuedByScript );
#else
    return 0;
#endif
  }

  /*NCore::WorldCommand* CreateCmdDenyBuilding( PFBaseHero* pHero, PFBuilding* pBuilding )
  {
    NI_VERIFY( IsUnitValid( CPtr<PFBaseHero>(pHero) ) && IsUnitValid( CPtr<PFBuilding>(pBuilding) ), "Deny tower: Hero and building must be valid!", return 0; );
    return new CmdDenyBuilding( pHero, pBuilding );
  }

  bool CmdDenyBuilding::CanExecute() const
  {
    return CheckHero(pHero, GetId());
  }

  void CmdDenyBuilding::Execute( NCore::IWorldBase *pIWorld )
  {
    if ( !IsUnitValid(pHero) || !IsTargetValid(pBuilding) )
      return;

    if ( pBuilding->CanDenyBuilding(pHero) )
      pBuilding->DenyBuilding(pHero);
  }


  NCore::WorldCommand* CreateCmdEmote( PFBaseHero* pHero, NDb::EEmotion emotion )
  {
    NI_VERIFY(pHero, "Invalid hero!", return 0);
    return new CmdEmote( pHero, emotion );
  }

  bool CmdEmote::CanExecute() const
  {
    return CheckHero(pHero, GetId()) && CheckPlayerControlAndMinigame( pHero );
  }

  void CmdEmote::Execute( NCore::IWorldBase*)
  {
    if( IsUnitValid(pHero) )
      pHero->Emote(emotion);
  }*/

  NCore::WorldCommand* CreateCmdCancelChannelling( PFBaseHero* pHero )
  {
    NI_VERIFY(IsValid(pHero), "Invalid hero!", return 0);
    return new CmdCancelChannelling( pHero );
  }

  bool CmdCancelChannelling::CanExecute() const
  {
    return CheckHero(pHero, GetId());
  }

  void CmdCancelChannelling::Execute( NCore::IWorldBase* )
  {
    if ( !IsValid( pHero ) )
      return;

    if ( pHero->IsInChannelling() )
    {
      pHero->CancelChannelling();
    }

    // allow use channelling cancellation for local player
    if ( pHero->IsLocal() )
    {
      NGameX::AdventureScreen* advScreen = NGameX::AdventureScreen::Instance();
      advScreen->SetChannellingCancelComplete();
    }
  }

  //////////////////////////////////////////////////////////////////////////
  bool CmdUseUnit::CanExecute() const
  {
    const bool accepted = CheckHero(pHero, GetId()) && CheckPlayerControlAndMinigame( pHero );
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.useUnitCanChecks;
    if (accepted)
      ++g_linuxHeroGameplayCommandDiagnostics.useUnitCanAccepted;
    CaptureLinuxGameplayTargetUnit(
      &g_linuxHeroGameplayCommandDiagnostics.useUnitTargetObjectId,
      &g_linuxHeroGameplayCommandDiagnostics.useUnitTargetKind,
      &g_linuxHeroGameplayCommandDiagnostics.useUnitTargetFaction,
      &g_linuxHeroGameplayCommandDiagnostics.useUnitTargetPlayerId,
      pUnit);
#endif
    return accepted;
  }

  void CmdUseUnit::Execute( NCore::IWorldBase * pWorld)
  {
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.useUnitExecuteCalls;
    CaptureLinuxGameplayTargetUnit(
      &g_linuxHeroGameplayCommandDiagnostics.useUnitTargetObjectId,
      &g_linuxHeroGameplayCommandDiagnostics.useUnitTargetKind,
      &g_linuxHeroGameplayCommandDiagnostics.useUnitTargetFaction,
      &g_linuxHeroGameplayCommandDiagnostics.useUnitTargetPlayerId,
      pUnit);
    PFEaselPlayer* easelPlayer = dynamic_cast<PFEaselPlayer*>(pHero);
    PFMinigamePlace* minigamePlace = dynamic_cast<PFMinigamePlace*>(pUnit);
    PFMinigamePlace* heroMinigameBefore =
      easelPlayer ? easelPlayer->GetMinigamePlace() : 0;
    PFEaselPlayer* targetMinigameUserBefore =
      minigamePlace ? minigamePlace->CurrentEaselPlayer() : 0;
    g_linuxHeroGameplayCommandDiagnostics.useUnitHeroMinigameBefore =
      heroMinigameBefore ? heroMinigameBefore->GetObjectId() : -1;
    g_linuxHeroGameplayCommandDiagnostics.useUnitTargetMinigameUserBefore =
      targetMinigameUserBefore ? targetMinigameUserBefore->GetObjectId() : -1;
    g_linuxHeroGameplayCommandDiagnostics.useUnitHeroIsolatedBefore =
      IsValid(pHero) && pHero->IsIsolated() ? 1 : 0;
#endif
    const bool canUseUnit = IsUnitValid( pHero ) && IsUnitValid( pUnit ) && pUnit->CanBeUsedBy( pHero );
#if defined(PW_LINUX_NULL_RENDER)
    g_linuxHeroGameplayCommandDiagnostics.useUnitCanBeUsed = canUseUnit ? 1 : 0;
#endif
    if ( canUseUnit )
    {
#if defined(PW_LINUX_NULL_RENDER)
      CObj<PFAbilityInstance> instance = pUnit->Use(pHero);
      PFMinigamePlace* heroMinigameAfter =
        easelPlayer ? easelPlayer->GetMinigamePlace() : 0;
      PFEaselPlayer* targetMinigameUserAfter =
        minigamePlace ? minigamePlace->CurrentEaselPlayer() : 0;
      g_linuxHeroGameplayCommandDiagnostics.useUnitAbilityInstanceCreated =
        instance ? 1 : 0;
      g_linuxHeroGameplayCommandDiagnostics.useUnitHeroMinigameAfter =
        heroMinigameAfter ? heroMinigameAfter->GetObjectId() : -1;
      g_linuxHeroGameplayCommandDiagnostics.useUnitTargetMinigameUserAfter =
        targetMinigameUserAfter ? targetMinigameUserAfter->GetObjectId() : -1;
      g_linuxHeroGameplayCommandDiagnostics.useUnitHeroIsolatedAfter =
        IsValid(pHero) && pHero->IsIsolated() ? 1 : 0;
      if (instance ||
          g_linuxHeroGameplayCommandDiagnostics.useUnitHeroMinigameBefore !=
            g_linuxHeroGameplayCommandDiagnostics.useUnitHeroMinigameAfter ||
          g_linuxHeroGameplayCommandDiagnostics.useUnitTargetMinigameUserBefore !=
            g_linuxHeroGameplayCommandDiagnostics.useUnitTargetMinigameUserAfter ||
          g_linuxHeroGameplayCommandDiagnostics.useUnitHeroIsolatedBefore !=
            g_linuxHeroGameplayCommandDiagnostics.useUnitHeroIsolatedAfter)
      {
        ++g_linuxHeroGameplayCommandDiagnostics.useUnitActionAccepted;
      }
#else
      if ( PFHeroUseUnitState* st = dynamic_cast<PFHeroUseUnitState*>( pHero->GetCurrentState() ) )
        if (st->GetUnit() == pUnit)
          return; // Ignore concurrent use command for the same unit
      pHero->EnqueueState( new PFHeroUseUnitState( pHero, pUnit ), true );
#endif
    }
  }

  NCore::WorldCommand* CreateCmdUseUnit( PFBaseHero *pHero, PFBaseUnit *pUnit )
  {
    if ( NULL == pHero || NULL == pUnit )
      return 0;
    return new CmdUseUnit( pHero, pUnit );
  }


} // End of namespace NWorld

REGISTER_DEV_VAR( "lock_immmove", g_bDisableImmHeroMove , STORAGE_NONE );
REGISTER_DEV_VAR( "disable_hero_check", g_disableHeroCheck, STORAGE_NONE );

REGISTER_SAVELOAD_CLASS_NM( CmdSetTimescale, NWorld )

REGISTER_SAVELOAD_CLASS_NM( CmdCombatMoveHero, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdMoveHero, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdStopHero, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdAttackTarget, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdUseConsumable, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdActivateTalent, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdUseTalent, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdUsePortal, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdPickupObject, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdFollowUnit, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdHold, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdRaiseFlag, NWorld );
REGISTER_SAVELOAD_CLASS_NM( CmdBuyConsumable, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdMinimapSignal, NWorld)
REGISTER_SAVELOAD_CLASS_NM( CmdInitMinigame, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdLeaveMinigame, NWorld )
//REGISTER_SAVELOAD_CLASS_NM( CmdDenyBuilding, NWorld )
//REGISTER_SAVELOAD_CLASS_NM( CmdEmote, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdCancelChannelling, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdUseUnit, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdKeepAlive, NWorld )
