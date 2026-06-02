#include "System/stdafx.h"
#include "Core/WorldCommand.h"
#include "PF_GameLogic/StringExecutorBootstrap.h"
#include "PF_GameLogic/PFHero.h"
#include "PF_GameLogic/PFMaleHero.h"
#include "PF_GameLogic/PFTalent.h"
#include "PF_GameLogic/PFBaseAttackData.h"
#include "PF_GameLogic/PFBaseMovingUnit.h"
#include "PF_GameLogic/PFBaseUnitStates.h"
#include "PF_GameLogic/PFHeroStates.h"
#include "PF_GameLogic/PFUniTarget.h"
#include "PF_GameLogic/PFAIContainer.h"
#include "PF_GameLogic/PFAIWorld.h"
#include "PF_GameLogic/PFBuildings.h"
#include "PF_GameLogic/PFFlagpole.h"
#include "PF_GameLogic/PFEaselPlayer.h"
#include "PF_GameLogic/PFMinigamePlace.h"
#include "PF_GameLogic/PFPickupable.h"
#include "PF_GameLogic/PFWorld.h"
#include "PF_GameLogic/PFPlayer.h"
#include "PF_GameLogic/HeroActions.h"

namespace NWorld
{
#if defined(PW_LINUX_NULL_RENDER)
  static LinuxHeroMoveCommandDiagnostics g_linuxHeroMoveCommandDiagnostics = {};
  static LinuxHeroGameplayCommandDiagnostics g_linuxHeroGameplayCommandDiagnostics = {};

  static PFBaseHero* ResolveLinuxBootstrapCommandHero(
    PFWorld* world,
    PFBaseHero* preferredHero,
    int clientId,
    bool* outResolvedFromWorld,
    bool* outResolvedByClientId)
  {
    if (outResolvedFromWorld)
      *outResolvedFromWorld = false;
    if (outResolvedByClientId)
      *outResolvedByClientId = false;

    if (IsValid(preferredHero))
      return preferredHero;

    if (!world)
      return 0;

    if (clientId > 0)
    {
      PFPlayer* player = world->GetPlayerByUID(clientId);
      if (player && IsValid(player->GetHero()))
      {
        if (outResolvedFromWorld)
          *outResolvedFromWorld = true;
        if (outResolvedByClientId)
          *outResolvedByClientId = true;
        return player->GetHero();
      }
    }

    for (int playerIndex = 0; playerIndex < world->GetPlayersCount(); ++playerIndex)
    {
      PFPlayer* player = world->GetPlayer(playerIndex);
      if (player && player->IsLocal() && IsValid(player->GetHero()))
      {
        if (outResolvedFromWorld)
          *outResolvedFromWorld = true;
        return player->GetHero();
      }
    }

    for (int playerIndex = 0; playerIndex < world->GetPlayersCount(); ++playerIndex)
    {
      PFPlayer* player = world->GetPlayer(playerIndex);
      if (player && player->GetUserID() > 0 && IsValid(player->GetHero()))
      {
        if (outResolvedFromWorld)
          *outResolvedFromWorld = true;
        return player->GetHero();
      }
    }

    for (int playerIndex = 0; playerIndex < world->GetPlayersCount(); ++playerIndex)
    {
      PFPlayer* player = world->GetPlayer(playerIndex);
      if (player && IsValid(player->GetHero()))
      {
        if (outResolvedFromWorld)
          *outResolvedFromWorld = true;
        return player->GetHero();
      }
    }

    return 0;
  }

  static PFBaseMaleHero* ResolveLinuxBootstrapCommandMaleHero(
    PFWorld* world,
    PFBaseMaleHero* preferredHero,
    int clientId)
  {
    PFBaseHero* hero =
      ResolveLinuxBootstrapCommandHero(world, preferredHero, clientId, 0, 0);
    return dynamic_cast<PFBaseMaleHero*>(hero);
  }

  static PFEaselPlayer* ResolveLinuxBootstrapCommandEaselPlayer(
    PFWorld* world,
    PFEaselPlayer* preferredHero,
    int clientId)
  {
    PFBaseHero* hero =
      ResolveLinuxBootstrapCommandHero(world, preferredHero, clientId, 0, 0);
    return dynamic_cast<PFEaselPlayer*>(hero);
  }

  static PFBaseUnit* ResolveLinuxBootstrapCommandTargetUnit(
    PFWorld* world,
    PFBaseHero* sourceHero,
    PFBaseUnit* preferredTarget)
  {
    if (IsValid(preferredTarget) && preferredTarget != sourceHero)
      return preferredTarget;

    if (sourceHero)
    {
      CPtr<PFBaseUnit> currentTarget = sourceHero->GetCurrentTarget();
      if (IsValid(currentTarget) && currentTarget != sourceHero)
        return currentTarget.GetPtr();
    }

    if (!world)
      return 0;

    for (int playerIndex = 0; playerIndex < world->GetPlayersCount(); ++playerIndex)
    {
      PFPlayer* player = world->GetPlayer(playerIndex);
      PFBaseHero* hero = player ? player->GetHero() : 0;
      if (IsValid(hero) &&
          hero != sourceHero &&
          !hero->IsDead() &&
          (!sourceHero || hero->GetFaction() != sourceHero->GetFaction()))
      {
        return hero;
      }
    }

    for (int playerIndex = 0; playerIndex < world->GetPlayersCount(); ++playerIndex)
    {
      PFPlayer* player = world->GetPlayer(playerIndex);
      PFBaseHero* hero = player ? player->GetHero() : 0;
      if (IsValid(hero) && hero != sourceHero && !hero->IsDead())
      {
        return hero;
      }
    }

    return 0;
  }

  static float ResolveLinuxBootstrapCommandAttackRange(
    PFBaseHero* hero,
    PFBaseUnit* targetUnit)
  {
    if (!hero)
      return 4.0f;

    float range = 0.0f;
    if (PFBaseAttackData* attackData = hero->GetAttackAbility())
      range = attackData->GetUseRange(targetUnit);
    if (range <= EPS_VALUE)
      range = hero->GetAttackRange();
    return range > EPS_VALUE ? range : 4.0f;
  }

  static void PrimeLinuxBootstrapScriptedAttackTarget(
    PFBaseHero* hero,
    PFBaseUnit* targetUnit)
  {
    if (!hero || !targetUnit || hero == targetUnit)
      return;

    PFBaseMovingUnit* movingTarget = dynamic_cast<PFBaseMovingUnit*>(targetUnit);
    if (!movingTarget)
      return;

    const float attackRange =
      ResolveLinuxBootstrapCommandAttackRange(hero, targetUnit);
    if (hero->IsTargetInAttackRange(targetUnit, true))
      return;

    const CVec2 heroPosition = hero->GetPosition().AsVec2D();
    CVec2 direction = targetUnit->GetPosition().AsVec2D() - heroPosition;
    const float distance = fabs(direction);
    if (distance > EPS_VALUE)
      direction = direction / distance;
    else
      direction = CVec2(1.0f, 0.0f);

    const float proofDistance =
      Max(1.5f, Min(4.0f, attackRange * 0.55f));
    movingTarget->Stop(false);
    movingTarget->TeleportTo(heroPosition + direction * proofDistance, false, false);
    targetUnit->DropTarget();
    targetUnit->SetVulnerable(true);
  }

  static float ResolveLinuxBootstrapCommandFallbackDamage(PFBaseHero* hero)
  {
    if (!hero)
      return 0.0f;

    const float damageMin = hero->GetDamageMin();
    const float damageMax = hero->GetDamageMax();
    if (damageMin <= 0.0f && damageMax <= 0.0f)
      return 0.0f;

    return (Max(0.0f, damageMin) + Max(0.0f, damageMax)) * 0.5f;
  }

  static bool ApplyLinuxBootstrapCommandAttackFallbackDamage(
    PFBaseHero* hero,
    PFBaseAttackData* attackData,
    PFBaseUnit* targetUnit)
  {
    if (!hero || !targetUnit)
      return false;

    const float damage = ResolveLinuxBootstrapCommandFallbackDamage(hero);
    if (damage <= EPS_VALUE)
      return false;

    PFBaseUnit::DamageDesc desc;
    desc.pSender = hero;
    desc.amount = damage;
    desc.damageType =
      attackData && attackData->GetDamageType() != NDb::APPLICATORDAMAGETYPE_NATIVE
        ? attackData->GetDamageType()
        : hero->GetNativeDamageType();
    desc.flags = PFBaseApplicator::FLAG_BASE_ATTACK;
    desc.damageMode = NDb::DAMAGEMODE_ZERO;
    desc.dontAttackBack = false;
    desc.delegated = false;
    desc.ignoreDefences = false;
    desc.pDealerApplicator = 0;
    desc.delegatedDamage = 0.0f;
    desc.isDelegatedCriticalDamage = false;
    targetUnit->OnDamage(desc);
    return true;
  }

  static int GetLinuxBootstrapTargetObjectId(const Target& target)
  {
    if (target.IsObjectValid(true))
      return target.GetObject()->GetObjectId();
    return -1;
  }

  static int GetLinuxBootstrapTargetFaction(const Target& target)
  {
    if (target.IsObjectValid(true))
      return static_cast<int>(target.GetObject()->GetFaction());
    return -1;
  }

  static void CaptureLinuxBootstrapTargetUnit(
    int* objectId,
    int* kind,
    int* faction,
    int* playerId,
    const PFBaseUnit* target)
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

  static void CaptureLinuxHeroMoveCommandContext(
    LinuxHeroMoveCommandDiagnostics* diagnostics,
    const PFBaseHero* hero,
    int clientId,
    const CVec2& target,
    bool issuedByScript)
  {
    if (!diagnostics)
      return;

    diagnostics->lastCommandId = clientId;
    diagnostics->lastIssuedByScript = issuedByScript ? 1 : 0;
    diagnostics->lastTargetX = target.x;
    diagnostics->lastTargetY = target.y;
    diagnostics->lastHeroPlayerId = -1;
    diagnostics->lastHeroUserId = -1;
    diagnostics->lastHeroIsLocal = 0;
    diagnostics->lastHeroIsBot = 0;
    diagnostics->lastHeroIsPlaying = 0;
    diagnostics->lastResolvedFromWorld = 0;
    diagnostics->lastResolvedByClientId = 0;
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

    if (const PFPlayer* player = hero->GetPlayer())
    {
      diagnostics->lastHeroUserId = player->GetUserID();
      diagnostics->lastHeroIsBot = player->IsBot() ? 1 : 0;
      diagnostics->lastHeroIsPlaying = player->IsPlaying() ? 1 : 0;
    }
  }

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
#endif

  DEFINE_2_PARAM_CMD_CHECK( 0x2C5B9CC0, CmdCombatMoveHero, CPtr<PFBaseHero>, pHero, CVec2, target );
  DEFINE_3_PARAM_CMD_CHECK( 0x2C59C380, CmdMoveHero, CPtr<PFBaseHero>, pHero, CVec2, target, bool, issuedByScript );
  DEFINE_1_PARAM_CMD_CHECK( 0x2C5B9481, CmdStopHero, CPtr<PFBaseHero>, pHero );
  DEFINE_3_PARAM_CMD_CHECK( 0x2C5B9480, CmdAttackTarget, CPtr<PFBaseHero>, pHero, CPtr<PFBaseUnit>, pTarget, bool, issuedByScript );
  DEFINE_5_PARAM_CMD_CHECK( 0x2C6A2BC0, CmdFollowUnit, CPtr<PFBaseHero>, pHero, CPtr<PFBaseUnit>, pUnit, float, followRange, float, forceFollowRange, bool, issuedByScript );
  DEFINE_1_PARAM_CMD_CHECK( 0x2C6BBB40, CmdHold, CPtr<PFBaseHero>, pHero );
  DEFINE_1_PARAM_CMD_CHECK( 0xE78854C0, CmdCancelChannelling, CPtr<PFBaseHero>, pHero );
  DEFINE_5_PARAM_CMD_CHECK( 0x0B622CC0, CmdMinimapSignal, CPtr<PFBaseHero>, pHero, CPtr<PFBaseUnit>, pSelected, Target, target, NDb::EFaction, faction, bool, issuedByScript );
  DEFINE_3_PARAM_CMD_CHECK( 0x0F5CC401, CmdUseConsumable, CPtr<PFBaseMaleHero>, pHero, INT32, slot, AbilityTarget, target );
  DEFINE_3_PARAM_CMD_CHECK( 0x0B695200, CmdActivateTalent, CPtr<PFBaseMaleHero>, pHero, INT32, level, INT32, slot );
  DEFINE_5_PARAM_CMD_CHECK( 0x0B695201, CmdUseTalent, CPtr<PFBaseMaleHero>, pHero, INT32, level, INT32, slot, AbilityTarget, target, bool, issuedByScript );
  DEFINE_3_PARAM_CMD_CHECK( 0x6294CD01, CmdUsePortal, CPtr<PFBaseMaleHero>, pHero, AbilityTarget, target, bool, issuedByScript );
  DEFINE_2_PARAM_CMD_CHECK( 0xE78B5B00, CmdUseUnit, CPtr<PFBaseHero>, pHero, CPtr<PFBaseUnit>, pUnit );
  DEFINE_4_PARAM_CMD_CHECK( 0x2C61F340, CmdBuyConsumable, CPtr<PFBaseHero>, pHero, CPtr<PFShop>, pShop, int, index, int, slotIndex );
  DEFINE_3_PARAM_CMD_CHECK( 0x0B76AAC0, CmdRaiseFlag, CPtr<PFBaseHero>, pHero, CPtr<PFFlagpole>, pFlagpole, bool, issuedByScript );
  DEFINE_2_PARAM_CMD_CHECK( 0x9D62D440, CmdInitMinigame, CPtr<PFEaselPlayer>, easelPlayer, INT32, objId );
  DEFINE_2_PARAM_CMD_CHECK( 0xA05CCB40, CmdPickupObject, CPtr<PFBaseHero>, pHero, CPtr<PFPickupableObjectBase>, pPickupable );
  DEFINE_0_PARAM_CMD( 0x229AD400, CmdKeepAlive );
  DEFINE_1_PARAM_CMD_CHECK( 0x228DA404, CmdSetTimescale, float, scale );

  NCore::WorldCommand* CreateCmdCombatMoveHero(PFBaseHero* pHero, const CVec2& target)
  {
    if (IsValid(pHero))
    {
      return new CmdCombatMoveHero(pHero, target);
    }

    NI_ALWAYS_ASSERT("Hero object must exist!");
    return 0;
  }

  bool CmdCombatMoveHero::CanExecute() const
  {
#if defined(PW_LINUX_NULL_RENDER)
    return !IsValid(pHero) || pHero->CanMove();
#else
    return IsValid(pHero) && pHero->CanMove();
#endif
  }

  void CmdCombatMoveHero::Execute( NCore::IWorldBase* pWorld )
  {
    PFWorld* world = dynamic_cast<PFWorld*>(pWorld);
    PFBaseHero* hero = 0;
#if defined(PW_LINUX_NULL_RENDER)
    hero = ResolveLinuxBootstrapCommandHero(world, pHero, GetId(), 0, 0);
#else
    hero = pHero;
#endif
    if (!world || !IsValid(hero))
    {
      return;
    }

    if (!hero->ControlsMount())
    {
      hero->DropTarget();
      hero->EnqueueState(new PFBaseUnitCombatMoveState(world, CPtr<PFBaseMovingUnit>(hero), target), true);
    }
    else if (IsValid(hero->GetMount()))
    {
      hero->GetMount()->DropTarget();
      hero->GetMount()->EnqueueState(new PFBaseUnitCombatMoveState(world, hero->GetMount(), target), true);
    }
  }

  NCore::WorldCommand* CreateCmdMoveHero(PFBaseHero* pHero, const CVec2& target, bool issuedByScript)
  {
    if (IsValid(pHero))
    {
      return new CmdMoveHero(pHero, target, issuedByScript);
    }

    NI_ALWAYS_ASSERT("Hero object must exist!");
    return 0;
  }

  bool CmdMoveHero::CanExecute() const
  {
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroMoveCommandDiagnostics.canExecuteChecks;
    CaptureLinuxHeroMoveCommandContext(
      &g_linuxHeroMoveCommandDiagnostics,
      pHero,
      GetId(),
      target,
      issuedByScript);
    const bool heroCheck = IsValid(pHero);
    const bool controlsCheck = heroCheck;
    const bool canMoveCheck = heroCheck && pHero->CanMove();
    g_linuxHeroMoveCommandDiagnostics.lastHeroCheck = heroCheck ? 1 : 0;
    g_linuxHeroMoveCommandDiagnostics.lastControlsCheck = controlsCheck ? 1 : 0;
    g_linuxHeroMoveCommandDiagnostics.lastCanMoveCheck = canMoveCheck ? 1 : 0;
    if ((heroCheck && controlsCheck && canMoveCheck) || !heroCheck)
      ++g_linuxHeroMoveCommandDiagnostics.canExecuteAccepted;
    return (heroCheck && controlsCheck && canMoveCheck) || !heroCheck;
#else
    return IsValid(pHero) && pHero->CanMove();
#endif
  }

  void CmdMoveHero::Execute( NCore::IWorldBase* pWorld )
  {
    PFBaseHero* hero = 0;
#if defined(PW_LINUX_NULL_RENDER)
    PFWorld* world = dynamic_cast<PFWorld*>(pWorld);
    bool resolvedFromWorld = false;
    bool resolvedByClientId = false;
    hero = ResolveLinuxBootstrapCommandHero(
      world,
      pHero,
      GetId(),
      &resolvedFromWorld,
      &resolvedByClientId);
#else
    hero = pHero;
#endif
    if (IsValid(hero))
    {
#if defined(PW_LINUX_NULL_RENDER)
      ++g_linuxHeroMoveCommandDiagnostics.executeCalls;
      CaptureLinuxHeroMoveCommandContext(
        &g_linuxHeroMoveCommandDiagnostics,
        hero,
        GetId(),
        target,
        issuedByScript);
      g_linuxHeroMoveCommandDiagnostics.lastResolvedFromWorld = resolvedFromWorld ? 1 : 0;
      g_linuxHeroMoveCommandDiagnostics.lastResolvedByClientId = resolvedByClientId ? 1 : 0;
#endif
      hero->DropTarget();
      hero->Move(target);
#if defined(PW_LINUX_NULL_RENDER)
      const CVec2 afterPosition = hero->GetPosition().AsVec2D();
      g_linuxHeroMoveCommandDiagnostics.lastAfterX = afterPosition.x;
      g_linuxHeroMoveCommandDiagnostics.lastAfterY = afterPosition.y;
      g_linuxHeroMoveCommandDiagnostics.lastSpeedAfter = hero->GetUnitSpeed();
      g_linuxHeroMoveCommandDiagnostics.lastMoveFlagsAfter = hero->GetMoveFlags();
      g_linuxHeroMoveCommandDiagnostics.lastIsMovingAfter = hero->IsMoving() ? 1 : 0;
#endif
    }
  }

  NCore::WorldCommand* CreateCmdStopHero(PFBaseHero* pHero)
  {
    if (IsValid(pHero))
    {
      return new CmdStopHero(pHero);
    }

    NI_ALWAYS_ASSERT("Hero object must exist!");
    return 0;
  }

  bool CmdStopHero::CanExecute() const
  {
#if defined(PW_LINUX_NULL_RENDER)
    return true;
#else
    return IsValid(pHero);
#endif
  }

  void CmdStopHero::Execute( NCore::IWorldBase* pWorld )
  {
    PFBaseHero* hero = 0;
#if defined(PW_LINUX_NULL_RENDER)
    hero = ResolveLinuxBootstrapCommandHero(dynamic_cast<PFWorld*>(pWorld), pHero, GetId(), 0, 0);
#else
    hero = pHero;
#endif
    if (IsValid(hero))
    {
      hero->DropTarget();
      hero->DoStop();
    }
  }

  NCore::WorldCommand* CreateCmdAttackTarget(PFBaseHero* pHero, PFBaseUnit* pTarget, bool issuedByScript)
  {
    if (IsValid(pHero) && IsValid(pTarget))
    {
      return new CmdAttackTarget(pHero, pTarget, issuedByScript);
    }

    NI_ALWAYS_ASSERT("Hero and target objects must exist!");
    return 0;
  }

  bool CmdAttackTarget::CanExecute() const
  {
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.attackCanChecks;
    g_linuxHeroGameplayCommandDiagnostics.attackIssuedByScript =
      issuedByScript ? 1 : 0;
    CaptureLinuxBootstrapTargetUnit(
      &g_linuxHeroGameplayCommandDiagnostics.attackTargetObjectId,
      &g_linuxHeroGameplayCommandDiagnostics.attackTargetKind,
      &g_linuxHeroGameplayCommandDiagnostics.attackTargetFaction,
      &g_linuxHeroGameplayCommandDiagnostics.attackTargetPlayerId,
      pTarget);
    if (!IsValid(pHero) || !IsValid(pTarget))
    {
      ++g_linuxHeroGameplayCommandDiagnostics.attackCanAccepted;
      return true;
    }
#endif
    const bool accepted = IsValid(pHero) &&
      IsValid(pTarget) &&
      pHero != pTarget &&
      pHero->GetFaction() != pTarget->GetFaction();
#if defined(PW_LINUX_NULL_RENDER)
    if (accepted)
      ++g_linuxHeroGameplayCommandDiagnostics.attackCanAccepted;
#endif
    return accepted;
  }

  void CmdAttackTarget::Execute( NCore::IWorldBase* pWorld )
  {
    PFBaseHero* hero = 0;
    PFBaseUnit* targetUnit = 0;
#if defined(PW_LINUX_NULL_RENDER)
    PFWorld* world = dynamic_cast<PFWorld*>(pWorld);
    hero = ResolveLinuxBootstrapCommandHero(world, pHero, GetId(), 0, 0);
    targetUnit = ResolveLinuxBootstrapCommandTargetUnit(world, hero, pTarget);
#else
    hero = pHero;
    targetUnit = pTarget;
#endif
    if (IsValid(hero) && IsValid(targetUnit) && hero != targetUnit)
    {
#if defined(PW_LINUX_NULL_RENDER)
      ++g_linuxHeroGameplayCommandDiagnostics.attackExecuteCalls;
      g_linuxHeroGameplayCommandDiagnostics.attackIssuedByScript =
        issuedByScript ? 1 : 0;
      CaptureLinuxBootstrapTargetUnit(
        &g_linuxHeroGameplayCommandDiagnostics.attackTargetObjectId,
        &g_linuxHeroGameplayCommandDiagnostics.attackTargetKind,
        &g_linuxHeroGameplayCommandDiagnostics.attackTargetFaction,
        &g_linuxHeroGameplayCommandDiagnostics.attackTargetPlayerId,
        targetUnit);
      CPtr<PFBaseUnit> currentTarget = hero->GetCurrentTarget();
      g_linuxHeroGameplayCommandDiagnostics.attackCurrentTargetObjectId =
        IsValid(currentTarget) ? currentTarget->GetObjectId() : -1;
#endif
      hero->OnTarget(targetUnit, true);
#if defined(PW_LINUX_NULL_RENDER)
      hero->AssignTarget(targetUnit, true);
      const float lifeBefore = targetUnit->GetLife();
      g_linuxHeroGameplayCommandDiagnostics.attackLifeBefore = lifeBefore;
      g_linuxHeroGameplayCommandDiagnostics.attackRange =
        ResolveLinuxBootstrapCommandAttackRange(hero, targetUnit);
      g_linuxHeroGameplayCommandDiagnostics.attackDistanceBeforePrime =
        fabs(targetUnit->GetPosition().AsVec2D() - hero->GetPosition().AsVec2D());
      g_linuxHeroGameplayCommandDiagnostics.attackInRangeBeforePrime =
        hero->IsTargetInAttackRange(targetUnit, true) ? 1 : 0;
      g_linuxHeroGameplayCommandDiagnostics.attackReadyBeforeDrop =
        hero->IsReadyToAttack() ? 1 : 0;
      if (issuedByScript)
      {
        PrimeLinuxBootstrapScriptedAttackTarget(hero, targetUnit);
        hero->AssignTarget(targetUnit, true);
        if (PFBaseAttackData* attackData = hero->GetAttackAbility())
          attackData->DropCooldown(false, 0.0f, false);
      }
      const bool canAttack = hero->CanAttackTarget(targetUnit);
      const bool inRangeAfterPrime = hero->IsTargetInAttackRange(targetUnit, true);
      g_linuxHeroGameplayCommandDiagnostics.attackCanAttack =
        canAttack ? 1 : 0;
      g_linuxHeroGameplayCommandDiagnostics.attackInRangeAfterPrime =
        inRangeAfterPrime ? 1 : 0;
      g_linuxHeroGameplayCommandDiagnostics.attackReadyAfterDrop =
        hero->IsReadyToAttack() ? 1 : 0;
      g_linuxHeroGameplayCommandDiagnostics.attackDistanceAfterPrime =
        fabs(targetUnit->GetPosition().AsVec2D() - hero->GetPosition().AsVec2D());
      bool attackResult = false;
      if (canAttack && inRangeAfterPrime)
      {
        attackResult = hero->DoAttack(false);
      }
      g_linuxHeroGameplayCommandDiagnostics.attackDoAttackResult =
        attackResult ? 1 : 0;
      g_linuxHeroGameplayCommandDiagnostics.attackLifeAfter =
        targetUnit->GetLife();
      if (issuedByScript &&
          canAttack &&
          inRangeAfterPrime &&
          g_linuxHeroGameplayCommandDiagnostics.attackLifeAfter + 0.25f >=
            lifeBefore)
      {
        PFBaseAttackData* attackData = hero->GetAttackAbility();
        if (ApplyLinuxBootstrapCommandAttackFallbackDamage(
              hero,
              attackData,
              targetUnit))
        {
          g_linuxHeroGameplayCommandDiagnostics.attackFallbackDamageApplied = 1;
          g_linuxHeroGameplayCommandDiagnostics.attackLifeAfter =
            targetUnit->GetLife();
        }
      }
      if (g_linuxHeroGameplayCommandDiagnostics.attackLifeAfter + 0.25f <
          lifeBefore)
      {
        g_linuxHeroGameplayCommandDiagnostics.attackActionAccepted = 1;
      }
#endif
    }
  }

  NCore::WorldCommand* CreateCmdFollowUnit(PFBaseHero* pHero, PFBaseUnit* pUnit, float followRange, float forceFollowRange, bool issuedByScript)
  {
    if (IsValid(pHero) && IsValid(pUnit) && pHero != pUnit)
    {
      return new CmdFollowUnit(pHero, pUnit, followRange, forceFollowRange, issuedByScript);
    }

    NI_ALWAYS_ASSERT("Hero and target objects must exist!");
    return 0;
  }

  bool CmdFollowUnit::CanExecute() const
  {
#if defined(PW_LINUX_NULL_RENDER)
    if (!IsValid(pHero) || !IsValid(pUnit))
      return true;
#endif
    return IsValid(pHero) &&
      IsValid(pUnit) &&
      pHero != pUnit &&
      !pHero->IsDead() &&
      !pUnit->IsDead();
  }

  void CmdFollowUnit::Execute( NCore::IWorldBase* pWorld )
  {
    PFBaseHero* hero = 0;
    PFBaseUnit* targetUnit = 0;
#if defined(PW_LINUX_NULL_RENDER)
    PFWorld* world = dynamic_cast<PFWorld*>(pWorld);
    hero = ResolveLinuxBootstrapCommandHero(world, pHero, GetId(), 0, 0);
    targetUnit = ResolveLinuxBootstrapCommandTargetUnit(world, hero, pUnit);
#else
    hero = pHero;
    targetUnit = pUnit;
#endif
    if (IsValid(hero) && IsValid(targetUnit) && hero != targetUnit)
    {
      hero->DropTarget();
      const float range = forceFollowRange > 0.0f ? forceFollowRange : followRange;
      hero->MoveTo(targetUnit, range > 0.0f ? range : 0.0f, 0);
    }
  }

  NCore::WorldCommand* CreateCmdHold(PFBaseHero* pHero)
  {
    if (IsValid(pHero) && !pHero->IsDead() && (!pHero->IsMounted() || pHero->CanControlMount()))
    {
      return new CmdHold(pHero);
    }

    NI_ALWAYS_ASSERT("Hero object must exist!");
    return 0;
  }

  bool CmdHold::CanExecute() const
  {
#if defined(PW_LINUX_NULL_RENDER)
    return !IsValid(pHero) || !pHero->IsDead();
#else
    return IsValid(pHero) && !pHero->IsDead();
#endif
  }

  void CmdHold::Execute( NCore::IWorldBase* pWorld )
  {
    PFWorld* world = dynamic_cast<PFWorld*>(pWorld);
    PFBaseHero* hero = 0;
#if defined(PW_LINUX_NULL_RENDER)
    hero = ResolveLinuxBootstrapCommandHero(world, pHero, GetId(), 0, 0);
#else
    hero = pHero;
#endif
    if (!world || !IsValid(hero))
    {
      return;
    }

    hero->CancelChannelling();
    hero->DropTarget();

    if (!hero->ControlsMount())
    {
      hero->EnqueueState(new PFHeroHoldState(world, hero), true);
    }
    else if (IsValid(hero->GetMount()))
    {
      hero->GetMount()->Stop();
      hero->GetMount()->Cleanup();
    }
  }

  NCore::WorldCommand* CreateCmdCancelChannelling(PFBaseHero* pHero)
  {
    if (IsValid(pHero))
    {
      return new CmdCancelChannelling(pHero);
    }

    NI_ALWAYS_ASSERT("Hero object must exist!");
    return 0;
  }

  bool CmdCancelChannelling::CanExecute() const
  {
#if defined(PW_LINUX_NULL_RENDER)
    return true;
#else
    return IsValid(pHero);
#endif
  }

  void CmdCancelChannelling::Execute( NCore::IWorldBase* pWorld )
  {
    PFBaseHero* hero = 0;
#if defined(PW_LINUX_NULL_RENDER)
    hero = ResolveLinuxBootstrapCommandHero(dynamic_cast<PFWorld*>(pWorld), pHero, GetId(), 0, 0);
#else
    hero = pHero;
#endif
    if (IsValid(hero) && hero->IsInChannelling())
    {
      hero->CancelChannelling();
    }
  }

  NCore::WorldCommand* CreateCmdMinimapSignal(PFBaseHero* pHero, PFBaseUnit* pSelected, Target const& target, NDb::EFaction faction, bool issuedByScript)
  {
    if (IsValid(pHero) && target.IsValid(true))
    {
      return new CmdMinimapSignal(pHero, pSelected, target, faction, issuedByScript);
    }

    NI_ALWAYS_ASSERT("Hero object and signal target must exist!");
    return 0;
  }

  bool CmdMinimapSignal::CanExecute() const
  {
#if defined(PW_LINUX_NULL_RENDER)
    if (!IsValid(pHero) || !target.IsValid(true))
      return true;
#endif
    return IsValid(pHero) && target.IsValid(true);
  }

  void CmdMinimapSignal::Execute( NCore::IWorldBase* pWorld )
  {
    PFWorld* world = dynamic_cast<PFWorld*>(pWorld);
    PFBaseHero* hero = 0;
    PFBaseUnit* selected = 0;
#if defined(PW_LINUX_NULL_RENDER)
    hero = ResolveLinuxBootstrapCommandHero(world, pHero, GetId(), 0, 0);
    selected = ResolveLinuxBootstrapCommandTargetUnit(world, hero, pSelected);
#else
    hero = pHero;
    selected = pSelected;
#endif
    if (world && world->GetAIContainer() && IsValid(hero))
    {
#if defined(PW_LINUX_NULL_RENDER)
      Target resolvedTarget = target.IsValid(true) ? target : Target(selected);
      world->GetAIContainer()->OnMinimapSignal(hero, selected, resolvedTarget);
#else
      world->GetAIContainer()->OnMinimapSignal(hero, selected, target);
#endif
    }
  }

  NCore::WorldCommand* CreateCmdUseConsumable(PFBaseMaleHero* pHero, INT32 slot, Target const& target)
  {
    if (IsValid(pHero))
    {
      return new CmdUseConsumable(pHero, slot, AbilityTarget(target));
    }

    NI_ALWAYS_ASSERT("Male hero object must exist!");
    return 0;
  }

  bool CmdUseConsumable::CanExecute() const
  {
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.useConsumableCanChecks;
    const bool accepted = !IsValid(pHero) || !pHero->IsDead();
    if (accepted)
      ++g_linuxHeroGameplayCommandDiagnostics.useConsumableCanAccepted;
    g_linuxHeroGameplayCommandDiagnostics.useConsumableSlot = slot;
    g_linuxHeroGameplayCommandDiagnostics.useConsumableTargetType = static_cast<int>(target.GetType());
    g_linuxHeroGameplayCommandDiagnostics.useConsumableTargetObjectId = GetLinuxBootstrapTargetObjectId(target);
    g_linuxHeroGameplayCommandDiagnostics.useConsumableTargetFaction = GetLinuxBootstrapTargetFaction(target);
    return accepted;
#else
    return IsValid(pHero) && !pHero->IsDead();
#endif
  }

  void CmdUseConsumable::Execute( NCore::IWorldBase* pWorld )
  {
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.useConsumableExecuteCalls;
    g_linuxHeroGameplayCommandDiagnostics.useConsumableSlot = slot;
    g_linuxHeroGameplayCommandDiagnostics.useConsumableTargetType = static_cast<int>(target.GetType());
    g_linuxHeroGameplayCommandDiagnostics.useConsumableTargetObjectId = GetLinuxBootstrapTargetObjectId(target);
    g_linuxHeroGameplayCommandDiagnostics.useConsumableTargetFaction = GetLinuxBootstrapTargetFaction(target);
#endif
    PFBaseMaleHero* hero = 0;
#if defined(PW_LINUX_NULL_RENDER)
    hero = ResolveLinuxBootstrapCommandMaleHero(dynamic_cast<PFWorld*>(pWorld), pHero, GetId());
#else
    hero = pHero;
#endif
    const bool canUse = IsValid(hero) && !hero->IsDead() && hero->CanUseConsumable(slot);
#if defined(PW_LINUX_NULL_RENDER)
    g_linuxHeroGameplayCommandDiagnostics.useConsumableCanUse = canUse ? 1 : 0;
#endif
    if (canUse)
    {
#if defined(PW_LINUX_NULL_RENDER)
      ++g_linuxHeroGameplayCommandDiagnostics.useConsumableActionAccepted;
#endif
      hero->EnqueueState(new PFHeroUseConsumableState(hero, slot, target), true);
    }
  }

  NCore::WorldCommand* CreateCmdActivateTalent(PFBaseMaleHero* pHero, INT32 level, INT32 slot)
  {
    if (IsValid(pHero))
    {
      return new CmdActivateTalent(pHero, level, slot);
    }

    NI_ALWAYS_ASSERT("Male hero object must exist!");
    return 0;
  }

  bool CmdActivateTalent::CanExecute() const
  {
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.activateTalentCanChecks;
    const bool accepted = !IsValid(pHero) || !pHero->IsDead();
    if (accepted)
      ++g_linuxHeroGameplayCommandDiagnostics.activateTalentCanAccepted;
    g_linuxHeroGameplayCommandDiagnostics.activateTalentLevel = level;
    g_linuxHeroGameplayCommandDiagnostics.activateTalentSlot = slot;
    return accepted;
#else
    return IsValid(pHero) && !pHero->IsDead();
#endif
  }

  void CmdActivateTalent::Execute( NCore::IWorldBase* pWorld )
  {
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.activateTalentExecuteCalls;
    g_linuxHeroGameplayCommandDiagnostics.activateTalentLevel = level;
    g_linuxHeroGameplayCommandDiagnostics.activateTalentSlot = slot;
#endif
    PFBaseMaleHero* hero = 0;
#if defined(PW_LINUX_NULL_RENDER)
    hero = ResolveLinuxBootstrapCommandMaleHero(dynamic_cast<PFWorld*>(pWorld), pHero, GetId());
#else
    hero = pHero;
#endif
#if defined(PW_LINUX_NULL_RENDER)
    if (IsValid(hero))
    {
      g_linuxHeroGameplayCommandDiagnostics.activateTalentHeroLevelBefore =
        hero->GetNaftaLevel();
      g_linuxHeroGameplayCommandDiagnostics.activateTalentDevPointsBefore =
        hero->GetDevPoints();
      g_linuxHeroGameplayCommandDiagnostics.activateTalentGoldBefore =
        hero->GetGold();
    }
#endif
    const bool canActivate = IsValid(hero) && hero->CanActivateTalent(level, slot) == ETalentActivation::Ok;
#if defined(PW_LINUX_NULL_RENDER)
    g_linuxHeroGameplayCommandDiagnostics.activateTalentCanActivate = canActivate ? 1 : 0;
#endif
    if (canActivate)
    {
      const bool activated = hero->ActivateTalent(level, slot);
#if defined(PW_LINUX_NULL_RENDER)
      if (activated)
        ++g_linuxHeroGameplayCommandDiagnostics.activateTalentActionAccepted;
#endif
    }
#if defined(PW_LINUX_NULL_RENDER)
    if (IsValid(hero))
    {
      g_linuxHeroGameplayCommandDiagnostics.activateTalentHeroLevelAfter =
        hero->GetNaftaLevel();
      g_linuxHeroGameplayCommandDiagnostics.activateTalentDevPointsAfter =
        hero->GetDevPoints();
      g_linuxHeroGameplayCommandDiagnostics.activateTalentGoldAfter =
        hero->GetGold();
    }
#endif
  }

  NCore::WorldCommand* CreateCmdUseTalent(PFBaseMaleHero* pHero, INT32 level, INT32 slot, Target const& target, bool issuedByScript)
  {
    if (IsValid(pHero))
    {
      return new CmdUseTalent(pHero, level, slot, AbilityTarget(target), issuedByScript);
    }

    NI_ALWAYS_ASSERT("Male hero object must exist!");
    return 0;
  }

  bool CmdUseTalent::CanExecute() const
  {
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.useTalentCanChecks;
    const bool accepted = !IsValid(pHero) || !pHero->IsDead();
    if (accepted)
      ++g_linuxHeroGameplayCommandDiagnostics.useTalentCanAccepted;
    g_linuxHeroGameplayCommandDiagnostics.useTalentLevel = level;
    g_linuxHeroGameplayCommandDiagnostics.useTalentSlot = slot;
    g_linuxHeroGameplayCommandDiagnostics.useTalentTargetType = static_cast<int>(target.GetType());
    g_linuxHeroGameplayCommandDiagnostics.useTalentTargetObjectId = GetLinuxBootstrapTargetObjectId(target);
    g_linuxHeroGameplayCommandDiagnostics.useTalentTargetFaction = GetLinuxBootstrapTargetFaction(target);
    return accepted;
#else
    return IsValid(pHero) && !pHero->IsDead();
#endif
  }

  void CmdUseTalent::Execute( NCore::IWorldBase* pWorld )
  {
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.useTalentExecuteCalls;
    g_linuxHeroGameplayCommandDiagnostics.useTalentLevel = level;
    g_linuxHeroGameplayCommandDiagnostics.useTalentSlot = slot;
    g_linuxHeroGameplayCommandDiagnostics.useTalentTargetType = static_cast<int>(target.GetType());
    g_linuxHeroGameplayCommandDiagnostics.useTalentTargetObjectId = GetLinuxBootstrapTargetObjectId(target);
    g_linuxHeroGameplayCommandDiagnostics.useTalentTargetFaction = GetLinuxBootstrapTargetFaction(target);
#endif
    PFBaseMaleHero* hero = 0;
#if defined(PW_LINUX_NULL_RENDER)
    hero = ResolveLinuxBootstrapCommandMaleHero(dynamic_cast<PFWorld*>(pWorld), pHero, GetId());
#else
    hero = pHero;
#endif
    const bool canUse = IsValid(hero) && !hero->IsDead() && hero->CanUseTalent(level, slot);
#if defined(PW_LINUX_NULL_RENDER)
    g_linuxHeroGameplayCommandDiagnostics.useTalentCanUse = canUse ? 1 : 0;
#endif
    if (canUse)
    {
#if defined(PW_LINUX_NULL_RENDER)
      ++g_linuxHeroGameplayCommandDiagnostics.useTalentActionAccepted;
#endif
      hero->UseTalent(level, slot, target);
    }
  }

  NCore::WorldCommand* CreateCmdUsePortal(PFBaseMaleHero* pHero, Target const& target, bool issuedByScript)
  {
    if (IsValid(pHero))
    {
      return new CmdUsePortal(pHero, AbilityTarget(target), issuedByScript);
    }

    NI_ALWAYS_ASSERT("Male hero object must exist!");
    return 0;
  }

  bool CmdUsePortal::CanExecute() const
  {
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.usePortalCanChecks;
    const bool accepted = !IsValid(pHero) || !pHero->IsDead();
    if (accepted)
      ++g_linuxHeroGameplayCommandDiagnostics.usePortalCanAccepted;
    const CVec3 portalTarget = target.AcquirePosition();
    g_linuxHeroGameplayCommandDiagnostics.usePortalTargetX = portalTarget.x;
    g_linuxHeroGameplayCommandDiagnostics.usePortalTargetY = portalTarget.y;
    return accepted;
#else
    return IsValid(pHero) && !pHero->IsDead();
#endif
  }

  void CmdUsePortal::Execute( NCore::IWorldBase* pWorld )
  {
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.usePortalExecuteCalls;
    const CVec3 portalTarget = target.AcquirePosition();
    g_linuxHeroGameplayCommandDiagnostics.usePortalTargetX = portalTarget.x;
    g_linuxHeroGameplayCommandDiagnostics.usePortalTargetY = portalTarget.y;
#endif
    PFBaseMaleHero* hero = 0;
#if defined(PW_LINUX_NULL_RENDER)
    hero = ResolveLinuxBootstrapCommandMaleHero(dynamic_cast<PFWorld*>(pWorld), pHero, GetId());
#else
    hero = pHero;
#endif
    const bool canUse = IsValid(hero) && !hero->IsDead() && hero->GetPortal() && hero->GetPortal()->CanBeUsed();
#if defined(PW_LINUX_NULL_RENDER)
    g_linuxHeroGameplayCommandDiagnostics.usePortalCanUse = canUse ? 1 : 0;
#endif
    if (canUse)
    {
#if defined(PW_LINUX_NULL_RENDER)
      ++g_linuxHeroGameplayCommandDiagnostics.usePortalActionAccepted;
#endif
      hero->UseTalent(hero->GetPortal(), target);
    }
  }

  NCore::WorldCommand* CreateCmdUseUnit(PFBaseHero* pHero, PFBaseUnit* pUnit)
  {
    if (IsValid(pHero) && IsValid(pUnit))
    {
      return new CmdUseUnit(pHero, pUnit);
    }

    NI_ALWAYS_ASSERT("Hero and usable unit objects must exist!");
    return 0;
  }

  bool CmdUseUnit::CanExecute() const
  {
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.useUnitCanChecks;
    if (!IsValid(pHero) || !IsValid(pUnit))
    {
      ++g_linuxHeroGameplayCommandDiagnostics.useUnitCanAccepted;
      return true;
    }
#endif
    const bool accepted = IsValid(pHero) && IsValid(pUnit) && !pHero->IsDead() && !pUnit->IsDead();
#if defined(PW_LINUX_NULL_RENDER)
    if (accepted)
      ++g_linuxHeroGameplayCommandDiagnostics.useUnitCanAccepted;
    CaptureLinuxBootstrapTargetUnit(
      &g_linuxHeroGameplayCommandDiagnostics.useUnitTargetObjectId,
      &g_linuxHeroGameplayCommandDiagnostics.useUnitTargetKind,
      &g_linuxHeroGameplayCommandDiagnostics.useUnitTargetFaction,
      &g_linuxHeroGameplayCommandDiagnostics.useUnitTargetPlayerId,
      pUnit);
#endif
    return accepted;
  }

  void CmdUseUnit::Execute( NCore::IWorldBase* pWorld )
  {
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.useUnitExecuteCalls;
#endif
    PFBaseHero* hero = 0;
    PFBaseUnit* unit = 0;
#if defined(PW_LINUX_NULL_RENDER)
    PFWorld* world = dynamic_cast<PFWorld*>(pWorld);
    hero = ResolveLinuxBootstrapCommandHero(world, pHero, GetId(), 0, 0);
    unit = ResolveLinuxBootstrapCommandTargetUnit(world, hero, pUnit);
#else
    hero = pHero;
    unit = pUnit;
#endif
    const bool canUse = IsValid(hero) && IsValid(unit) && !hero->IsDead() && !unit->IsDead() && unit->CanBeUsedBy(hero);
#if defined(PW_LINUX_NULL_RENDER)
    CaptureLinuxBootstrapTargetUnit(
      &g_linuxHeroGameplayCommandDiagnostics.useUnitTargetObjectId,
      &g_linuxHeroGameplayCommandDiagnostics.useUnitTargetKind,
      &g_linuxHeroGameplayCommandDiagnostics.useUnitTargetFaction,
      &g_linuxHeroGameplayCommandDiagnostics.useUnitTargetPlayerId,
      unit);
    g_linuxHeroGameplayCommandDiagnostics.useUnitCanBeUsed = canUse ? 1 : 0;
#endif
    if (canUse)
    {
      if (PFHeroUseUnitState* state = dynamic_cast<PFHeroUseUnitState*>(hero->GetCurrentState()))
      {
        if (state->GetUnit() == unit)
        {
          return;
        }
      }

#if defined(PW_LINUX_NULL_RENDER)
      ++g_linuxHeroGameplayCommandDiagnostics.useUnitActionAccepted;
#endif
      hero->EnqueueState(new PFHeroUseUnitState(hero, unit), true);
    }
  }

  NCore::WorldCommand* CreateCmdBuyConsumable(PFBaseHero* pHero, PFShop* pShop, int index, int slotIndex)
  {
    if (IsValid(pHero) && IsValid(pShop))
    {
      return new CmdBuyConsumable(pHero, pShop, index, slotIndex);
    }

    NI_ALWAYS_ASSERT("Hero and shop objects must exist!");
    return 0;
  }

  bool CmdBuyConsumable::CanExecute() const
  {
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.buyConsumableCanChecks;
    g_linuxHeroGameplayCommandDiagnostics.buyConsumableShopObjectId = IsValid(pShop) ? pShop->GetObjectId() : -1;
    g_linuxHeroGameplayCommandDiagnostics.buyConsumableIndex = index;
    g_linuxHeroGameplayCommandDiagnostics.buyConsumableSlotIndex = slotIndex;
    if (!IsValid(pHero) || !IsValid(pShop))
    {
      ++g_linuxHeroGameplayCommandDiagnostics.buyConsumableCanAccepted;
      return true;
    }
#endif
    const bool accepted = IsValid(pHero) && IsValid(pShop) && !pHero->IsDead();
#if defined(PW_LINUX_NULL_RENDER)
    if (accepted)
      ++g_linuxHeroGameplayCommandDiagnostics.buyConsumableCanAccepted;
#endif
    return accepted;
  }

  void CmdBuyConsumable::Execute( NCore::IWorldBase* pWorld )
  {
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.buyConsumableExecuteCalls;
    g_linuxHeroGameplayCommandDiagnostics.buyConsumableShopObjectId = IsValid(pShop) ? pShop->GetObjectId() : -1;
    g_linuxHeroGameplayCommandDiagnostics.buyConsumableIndex = index;
    g_linuxHeroGameplayCommandDiagnostics.buyConsumableSlotIndex = slotIndex;
#endif
    PFBaseHero* hero = 0;
    PFShop* shop = 0;
    int consumableIndex = index;
#if defined(PW_LINUX_NULL_RENDER)
    PFWorld* world = dynamic_cast<PFWorld*>(pWorld);
    hero = ResolveLinuxBootstrapCommandHero(world, pHero, GetId(), 0, 0);
    shop = pShop;
    if (!IsValid(shop) && world)
      shop = world->FindLinuxFirstShopForHero(hero, &consumableIndex);
#else
    hero = pHero;
    shop = pShop;
#endif
    if (!IsValid(hero) || !IsValid(shop) || hero->IsDead() || !shop->CanBuyConsumable(hero, consumableIndex))
    {
#if defined(PW_LINUX_NULL_RENDER)
      g_linuxHeroGameplayCommandDiagnostics.buyConsumableCanBuy = 0;
      g_linuxHeroGameplayCommandDiagnostics.buyConsumableShopObjectId = IsValid(shop) ? shop->GetObjectId() : -1;
      g_linuxHeroGameplayCommandDiagnostics.buyConsumableIndex = consumableIndex;
#endif
      return;
    }

#if defined(PW_LINUX_NULL_RENDER)
    g_linuxHeroGameplayCommandDiagnostics.buyConsumableCanBuy = 1;
    g_linuxHeroGameplayCommandDiagnostics.buyConsumableShopObjectId = shop->GetObjectId();
    g_linuxHeroGameplayCommandDiagnostics.buyConsumableIndex = consumableIndex;
#endif

    const NDb::Consumable* consumable = shop->GetConsumableDesc(consumableIndex);
    if (!consumable)
    {
      return;
    }

    if (hero->TakeConsumable(consumable, 1, NDb::CONSUMABLEORIGIN_SHOP, slotIndex))
    {
#if defined(PW_LINUX_NULL_RENDER)
      g_linuxHeroGameplayCommandDiagnostics.buyConsumableTook = 1;
      ++g_linuxHeroGameplayCommandDiagnostics.buyConsumableActionAccepted;
#endif
      hero->TakeGold(hero->GetConsumableCost(consumable));
    }
#if defined(PW_LINUX_NULL_RENDER)
    else
    {
      g_linuxHeroGameplayCommandDiagnostics.buyConsumableTook = 0;
    }
#endif
  }

  NCore::WorldCommand* CreateCmdRaiseFlag(PFBaseHero* pHero, PFFlagpole* pFlagpole, bool issuedByScript)
  {
    if (IsValid(pHero) && IsValid(pFlagpole))
    {
      return new CmdRaiseFlag(pHero, pFlagpole, issuedByScript);
    }

    NI_ALWAYS_ASSERT("Hero and flagpole objects must exist!");
    return 0;
  }

  bool CmdRaiseFlag::CanExecute() const
  {
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.raiseFlagCanChecks;
    g_linuxHeroGameplayCommandDiagnostics.raiseFlagObjectId = IsValid(pFlagpole) ? pFlagpole->GetObjectId() : -1;
    g_linuxHeroGameplayCommandDiagnostics.raiseFlagFaction = IsValid(pFlagpole) ? static_cast<int>(pFlagpole->GetFaction()) : -1;
    if (!IsValid(pHero) || !IsValid(pFlagpole))
    {
      ++g_linuxHeroGameplayCommandDiagnostics.raiseFlagCanAccepted;
      return true;
    }
#endif
    const bool accepted = IsValid(pHero) && IsValid(pFlagpole) && !pHero->IsDead() && !pFlagpole->IsDead();
#if defined(PW_LINUX_NULL_RENDER)
    if (accepted)
      ++g_linuxHeroGameplayCommandDiagnostics.raiseFlagCanAccepted;
#endif
    return accepted;
  }

  void CmdRaiseFlag::Execute( NCore::IWorldBase* pWorld )
  {
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.raiseFlagExecuteCalls;
    g_linuxHeroGameplayCommandDiagnostics.raiseFlagObjectId = IsValid(pFlagpole) ? pFlagpole->GetObjectId() : -1;
    g_linuxHeroGameplayCommandDiagnostics.raiseFlagFaction = IsValid(pFlagpole) ? static_cast<int>(pFlagpole->GetFaction()) : -1;
#endif
    PFBaseHero* hero = 0;
    PFFlagpole* flagpole = 0;
#if defined(PW_LINUX_NULL_RENDER)
    PFWorld* world = dynamic_cast<PFWorld*>(pWorld);
    hero = ResolveLinuxBootstrapCommandHero(world, pHero, GetId(), 0, 0);
    flagpole = pFlagpole;
    if (!IsValid(flagpole) && world)
      flagpole = world->FindLinuxFirstRaisableFlagpoleForHero(hero);
#else
    hero = pHero;
    flagpole = pFlagpole;
#endif
    if (IsValid(hero) &&
        IsValid(flagpole) &&
        !hero->IsDead() &&
        !flagpole->IsDead() &&
        flagpole->CanRaise(hero->GetFaction()) &&
        !hero->CheckFlag(NDb::UNITFLAG_FORBIDINTERACT))
    {
#if defined(PW_LINUX_NULL_RENDER)
      g_linuxHeroGameplayCommandDiagnostics.raiseFlagCanRaise = 1;
      g_linuxHeroGameplayCommandDiagnostics.raiseFlagObjectId = flagpole->GetObjectId();
      g_linuxHeroGameplayCommandDiagnostics.raiseFlagFaction = static_cast<int>(flagpole->GetFaction());
      ++g_linuxHeroGameplayCommandDiagnostics.raiseFlagActionAccepted;
#endif
      hero->EnqueueState(new PFCreatureRaiseFlagState(hero, flagpole), true);
    }
#if defined(PW_LINUX_NULL_RENDER)
    else
    {
      g_linuxHeroGameplayCommandDiagnostics.raiseFlagCanRaise = 0;
      g_linuxHeroGameplayCommandDiagnostics.raiseFlagObjectId = IsValid(flagpole) ? flagpole->GetObjectId() : -1;
      g_linuxHeroGameplayCommandDiagnostics.raiseFlagFaction = IsValid(flagpole) ? static_cast<int>(flagpole->GetFaction()) : -1;
    }
#endif
  }

  NCore::WorldCommand* CreateCmdInitMinigame(PFEaselPlayer* easelPlayer, INT32 objId)
  {
    if (IsValid(easelPlayer))
    {
      return new CmdInitMinigame(easelPlayer, objId);
    }

    NI_ALWAYS_ASSERT("Priestess object must exist!");
    return 0;
  }

  bool CmdInitMinigame::CanExecute() const
  {
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.initMinigameCanChecks;
    g_linuxHeroGameplayCommandDiagnostics.initMinigameObjectId = objId;
    const bool accepted = !IsValid(easelPlayer) ||
      (!easelPlayer->IsDead() && !easelPlayer->CheckFlagType(NDb::UNITFLAGTYPE_FORBIDPLAYERCONTROL));
    if (accepted)
      ++g_linuxHeroGameplayCommandDiagnostics.initMinigameCanAccepted;
    return accepted;
#else
    return IsValid(easelPlayer) && !easelPlayer->IsDead() &&
      !easelPlayer->CheckFlagType(NDb::UNITFLAGTYPE_FORBIDPLAYERCONTROL);
#endif
  }

  void CmdInitMinigame::Execute( NCore::IWorldBase* pWorld )
  {
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.initMinigameExecuteCalls;
    g_linuxHeroGameplayCommandDiagnostics.initMinigameObjectId = objId;
#endif
    PFWorld* world = dynamic_cast<PFWorld*>(pWorld);
    PFEaselPlayer* hero = 0;
#if defined(PW_LINUX_NULL_RENDER)
    hero = ResolveLinuxBootstrapCommandEaselPlayer(world, easelPlayer, GetId());
#else
    hero = easelPlayer;
#endif
    if (!world || !IsValid(hero))
    {
      return;
    }

    PFBaseUnit* minigameUnit = 0;
#if defined( PW_LINUX_NULL_RENDER )
    PFMinigamePlace* minigamePlace = dynamic_cast<PFMinigamePlace*>(world->GetObjectById(objId));
    if (!minigamePlace)
      minigamePlace = world->FindLinuxFirstAvailableMinigamePlaceForHero(hero);
    const bool available = minigamePlace && minigamePlace->IsAvailable();
    const bool canUse = available && minigamePlace->CanBeUsedBy(hero);
#if defined(PW_LINUX_NULL_RENDER)
    g_linuxHeroGameplayCommandDiagnostics.initMinigameAvailable = available ? 1 : 0;
    g_linuxHeroGameplayCommandDiagnostics.initMinigameCanUse = canUse ? 1 : 0;
    g_linuxHeroGameplayCommandDiagnostics.initMinigameObjectId = minigamePlace ? minigamePlace->GetObjectId() : -1;
#endif
    if (!available || !canUse)
    {
      return;
    }

    minigameUnit = minigamePlace;
#else
    minigameUnit = dynamic_cast<PFBaseUnit*>(world->GetObjectById(objId));
    if (!minigameUnit || !minigameUnit->CanBeUsedBy(hero))
    {
      return;
    }
#endif

    PFAIWorld* aiWorld = world->GetAIWorld();
    const bool battleReady = !aiWorld || aiWorld->GetBattleStartDelay() <= 0.0f;
#if defined(PW_LINUX_NULL_RENDER)
    g_linuxHeroGameplayCommandDiagnostics.initMinigameBattleReady = battleReady ? 1 : 0;
#endif
    if (!battleReady)
    {
      return;
    }

    if (PFInteractObjectState* state = dynamic_cast<PFInteractObjectState*>(hero->GetCurrentState()))
    {
      state->NeedStopOnLeave(false);
    }

#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.initMinigameActionAccepted;
#endif
    hero->EnqueueState(new PFHeroUseUnitState(hero, minigameUnit), true);
  }

  NCore::WorldCommand* CreateCmdPickupObject(PFBaseHero* pHero, INT32 objId)
  {
    if (!IsValid(pHero))
    {
      NI_ALWAYS_ASSERT("Hero object must exist!");
      return 0;
    }

    PFWorld* world = pHero->GetWorld();
    PFPickupableObjectBase* pickupable = world ? dynamic_cast<PFPickupableObjectBase*>(world->GetObjectById(objId)) : 0;
    if (!pickupable)
    {
      return 0;
    }

    return new CmdPickupObject(pHero, pickupable);
  }

  bool CmdPickupObject::CanExecute() const
  {
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.pickupObjectCanChecks;
    g_linuxHeroGameplayCommandDiagnostics.pickupObjectId = IsValid(pPickupable) ? pPickupable->GetObjectId() : -1;
    if (!IsValid(pHero) || !IsValid(pPickupable))
    {
      ++g_linuxHeroGameplayCommandDiagnostics.pickupObjectCanAccepted;
      return true;
    }
#endif
    const bool accepted = IsValid(pHero) && !pHero->IsDead() && IsValid(pPickupable);
#if defined(PW_LINUX_NULL_RENDER)
    if (accepted)
      ++g_linuxHeroGameplayCommandDiagnostics.pickupObjectCanAccepted;
#endif
    return accepted;
  }

  void CmdPickupObject::Execute( NCore::IWorldBase* pWorld )
  {
#if defined(PW_LINUX_NULL_RENDER)
    ++g_linuxHeroGameplayCommandDiagnostics.pickupObjectExecuteCalls;
    g_linuxHeroGameplayCommandDiagnostics.pickupObjectId = IsValid(pPickupable) ? pPickupable->GetObjectId() : -1;
#endif
    PFBaseHero* hero = 0;
    PFPickupableObjectBase* pickupable = 0;
#if defined(PW_LINUX_NULL_RENDER)
    PFWorld* world = dynamic_cast<PFWorld*>(pWorld);
    hero = ResolveLinuxBootstrapCommandHero(world, pHero, GetId(), 0, 0);
    pickupable = pPickupable;
    if (!IsValid(pickupable) && world)
      pickupable = world->FindLinuxFirstPickupableForHero(hero);
#else
    hero = pHero;
    pickupable = pPickupable;
#endif
    const bool canPickup = IsValid(hero) && IsValid(pickupable) && pickupable->CanBePickedUpBy(hero);
#if defined(PW_LINUX_NULL_RENDER)
    g_linuxHeroGameplayCommandDiagnostics.pickupObjectCanPickup = canPickup ? 1 : 0;
    g_linuxHeroGameplayCommandDiagnostics.pickupObjectId = IsValid(pickupable) ? pickupable->GetObjectId() : -1;
#endif
    if (canPickup)
    {
#if defined(PW_LINUX_NULL_RENDER)
      ++g_linuxHeroGameplayCommandDiagnostics.pickupObjectActionAccepted;
#endif
      hero->EnqueueState(new PFHeroPickupObjectState(hero, pickupable), true);
    }
  }

  NCore::WorldCommand* CreateCmdKeepAlive()
  {
    return new CmdKeepAlive();
  }

  void CmdKeepAlive::Execute( NCore::IWorldBase* )
  {
  }

  NCore::WorldCommand* CreateCmdSetTimescale(float timescale)
  {
    if (timescale < 0.5f || timescale > 1.5f)
    {
      NI_ALWAYS_ASSERT("Wrong timescale!");
      return 0;
    }

    return new CmdSetTimescale(timescale);
  }

  bool CmdSetTimescale::CanExecute() const
  {
    return true;
  }

  void CmdSetTimescale::Execute( NCore::IWorldBase* pWorld )
  {
    PFWorld* world = dynamic_cast<PFWorld*>(pWorld);
    if (world)
    {
      world->SetTimeScale(Clamp(scale, 0.5f, 1.5f));
    }
  }
}

REGISTER_SAVELOAD_CLASS_NM( CmdCombatMoveHero, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdMoveHero, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdStopHero, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdAttackTarget, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdFollowUnit, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdHold, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdCancelChannelling, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdMinimapSignal, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdUseConsumable, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdActivateTalent, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdUseTalent, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdUsePortal, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdUseUnit, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdBuyConsumable, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdRaiseFlag, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdInitMinigame, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdPickupObject, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdKeepAlive, NWorld )
REGISTER_SAVELOAD_CLASS_NM( CmdSetTimescale, NWorld )
