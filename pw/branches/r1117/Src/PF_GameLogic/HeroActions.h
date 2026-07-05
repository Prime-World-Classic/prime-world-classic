#pragma once

#include "../Core/WorldCommand.h"
#include "PFUniTarget.h"

namespace NDb
{
  enum EEmotion;
};

namespace NWorld
{
  class PFBaseUnit;
  class PFBaseHero;
  class PFBaseMaleHero;
  class PFBuilding;
  class PFFlagpole;
  class PFEaselPlayer;
  class PFShop;
  class PFUsableBuilding;

#if defined(PW_LINUX_NULL_RENDER)
  struct LinuxHeroMoveCommandDiagnostics
  {
    int canExecuteChecks;
    int canExecuteAccepted;
    int executeCalls;
    int lastCommandId;
    int lastHeroPlayerId;
    int lastHeroUserId;
    int lastHeroIsLocal;
    int lastHeroIsBot;
    int lastHeroIsPlaying;
    int lastIssuedByScript;
    int lastHeroCheck;
    int lastControlsCheck;
    int lastCanMoveCheck;
    int lastResolvedFromWorld;
    int lastResolvedByClientId;
    int lastIsMovingBefore;
    int lastIsMovingAfter;
    unsigned int lastMoveFlagsBefore;
    unsigned int lastMoveFlagsAfter;
    float lastSourceX;
    float lastSourceY;
    float lastTargetX;
    float lastTargetY;
    float lastAfterX;
    float lastAfterY;
    float lastSpeedBefore;
    float lastSpeedAfter;
  };

  struct LinuxHeroGameplayCommandDiagnostics
  {
    int attackCanChecks;
    int attackCanAccepted;
    int attackExecuteCalls;
    int attackActionAccepted;
    int attackIssuedByScript;
    int attackTargetObjectId;
    int attackTargetKind;
    int attackTargetFaction;
    int attackTargetPlayerId;
    int attackCurrentTargetObjectId;
    int attackCanAttack;
    int attackInRangeBeforePrime;
    int attackInRangeAfterPrime;
    int attackReadyBeforeDrop;
    int attackReadyAfterDrop;
    int attackDoAttackResult;
    int attackFallbackDamageApplied;
    float attackRange;
    float attackDistanceBeforePrime;
    float attackDistanceAfterPrime;
    float attackLifeBefore;
    float attackLifeAfter;

    int useUnitCanChecks;
    int useUnitCanAccepted;
    int useUnitExecuteCalls;
    int useUnitActionAccepted;
    int useUnitCanBeUsed;
    int useUnitTargetObjectId;
    int useUnitTargetKind;
    int useUnitTargetFaction;
    int useUnitTargetPlayerId;
    int useUnitAbilityInstanceCreated;
    int useUnitHeroMinigameBefore;
    int useUnitHeroMinigameAfter;
    int useUnitTargetMinigameUserBefore;
    int useUnitTargetMinigameUserAfter;
    int useUnitHeroIsolatedBefore;
    int useUnitHeroIsolatedAfter;

    int activateTalentCanChecks;
    int activateTalentCanAccepted;
    int activateTalentExecuteCalls;
    int activateTalentActionAccepted;
    int activateTalentCanActivate;
    int activateTalentLevel;
    int activateTalentSlot;
    int activateTalentHeroLevelBefore;
    int activateTalentHeroLevelAfter;
    int activateTalentDevPointsBefore;
    int activateTalentDevPointsAfter;
    int activateTalentGoldBefore;
    int activateTalentGoldAfter;

    int useTalentCanChecks;
    int useTalentCanAccepted;
    int useTalentExecuteCalls;
    int useTalentActionAccepted;
    int useTalentCanUse;
    int useTalentLevel;
    int useTalentSlot;
    int useTalentTargetType;
    int useTalentTargetObjectId;
    int useTalentTargetFaction;
    int useTalentLastUseStepBefore;
    int useTalentLastUseStepAfter;
    int useTalentActiveInstancesBefore;
    int useTalentActiveInstancesAfter;
    float useTalentCooldownBefore;
    float useTalentCooldownAfter;

    int usePortalCanChecks;
    int usePortalCanAccepted;
    int usePortalExecuteCalls;
    int usePortalActionAccepted;
    int usePortalCanUse;
    float usePortalTargetX;
    float usePortalTargetY;

    int useConsumableCanChecks;
    int useConsumableCanAccepted;
    int useConsumableExecuteCalls;
    int useConsumableActionAccepted;
    int useConsumableCanUse;
    int useConsumableSlot;
    int useConsumableTargetType;
    int useConsumableTargetObjectId;
    int useConsumableTargetFaction;
    int useConsumableQuantityBefore;
    int useConsumableQuantityAfter;
    int useConsumableSlotOccupiedBefore;
    int useConsumableSlotOccupiedAfter;
    float useConsumableCooldownBefore;
    float useConsumableCooldownAfter;

    int buyConsumableCanChecks;
    int buyConsumableCanAccepted;
    int buyConsumableExecuteCalls;
    int buyConsumableActionAccepted;
    int buyConsumableCanBuy;
    int buyConsumableTook;
    int buyConsumableShopObjectId;
    int buyConsumableIndex;
    int buyConsumableSlotIndex;
    int buyConsumableCommandHeroObjectId;
    int buyConsumableLiveHeroObjectId;
    int buyConsumableResolvedHeroFromWorld;
    int buyConsumableHeroPlayerId;
    int buyConsumableHeroUserId;
    int buyConsumableSlotCountBefore;
    int buyConsumableSlotCountAfter;
    int buyConsumableResultSlotIndex;
    int buyConsumableResultSlotType;
    int buyConsumableResultSlotQuantity;
    int aiConsumableProbeHeroObjectId;
    int aiConsumableProbeRequestedType;
    int aiConsumableProbeSlotCount;
    int aiConsumableProbeMatches;
    int aiConsumableProbeFirstIndex;
    int aiConsumableProbeFirstType;
    int aiConsumableProbeFirstQuantity;

    int raiseFlagCanChecks;
    int raiseFlagCanAccepted;
    int raiseFlagExecuteCalls;
    int raiseFlagActionAccepted;
    int raiseFlagCanRaise;
    int raiseFlagObjectId;
    int raiseFlagFaction;

    int initMinigameCanChecks;
    int initMinigameCanAccepted;
    int initMinigameExecuteCalls;
    int initMinigameActionAccepted;
    int initMinigameAvailable;
    int initMinigameCanUse;
    int initMinigameBattleReady;
    int initMinigameObjectId;
    int initMinigameHeroPlaceBefore;
    int initMinigameHeroPlaceAfter;
    int initMinigamePlaceUserBefore;
    int initMinigamePlaceUserAfter;
    int initMinigameHeroIsolatedBefore;
    int initMinigameHeroIsolatedAfter;

    int leaveMinigameCanChecks;
    int leaveMinigameCanAccepted;
    int leaveMinigameExecuteCalls;
    int leaveMinigameActionAccepted;
    int leaveMinigameHeroObjectId;
    int leaveMinigameHeroPlaceBefore;
    int leaveMinigameHeroPlaceAfter;
    int leaveMinigamePlaceUserBefore;
    int leaveMinigamePlaceUserAfter;
    int leaveMinigameHeroIsolatedBefore;
    int leaveMinigameHeroIsolatedAfter;
    int leaveMinigameHeroFlagBefore;
    int leaveMinigameHeroFlagAfter;
    int leaveMinigameVisualStateBefore;
    int leaveMinigameVisualStateAfter;
    int leaveMinigamePlacementApplyBefore;
    int leaveMinigamePlacementApplyAfter;

    int pickupObjectCanChecks;
    int pickupObjectCanAccepted;
    int pickupObjectExecuteCalls;
    int pickupObjectActionAccepted;
    int pickupObjectCanPickup;
    int pickupObjectId;
  };

  LinuxHeroMoveCommandDiagnostics GetLinuxHeroMoveCommandDiagnostics();
  void ResetLinuxHeroMoveCommandDiagnostics();
  LinuxHeroGameplayCommandDiagnostics GetLinuxHeroGameplayCommandDiagnostics();
  void ResetLinuxHeroGameplayCommandDiagnostics();
  void RecordLinuxAIConsumableInventoryProbe(
    PFBaseMaleHero const* hero,
    int requestedType,
    int slotCount,
    int matches,
    int firstIndex,
    int firstType,
    int firstQuantity);
#endif

  NCore::WorldCommand* CreateCmdCombatMoveHero( PFBaseHero* pHero, CVec2 const& target );
  NCore::WorldCommand* CreateCmdMoveHero( PFBaseHero* pHero, CVec2 const& target, bool issuedByScript = false );
  NCore::WorldCommand* CreateCmdStopHero( PFBaseHero* pHero);
  NCore::WorldCommand* CreateCmdAttackTarget( PFBaseHero* pHero,  PFBaseUnit* pTarget, bool issuedByScript = false );
  NCore::WorldCommand* CreateCmdPickupObject( PFBaseHero* pHero, INT32 objId );
  NCore::WorldCommand* CreateCmdFollowUnit( PFBaseHero* pHero, PFBaseUnit* pUnit, float followRange = 0.f, float forceFollowRange = 0.f, bool issuedByScript = false );
  NCore::WorldCommand* CreateCmdHold(PFBaseHero* pHero);
  NCore::WorldCommand* CreateCmdRaiseFlag(PFBaseHero *pHero, PFFlagpole *pFlagpole, bool issuedByScript = false );
  
  //////////////////////////////////////////////////////////////////////////
  NCore::WorldCommand* CreateCmdUseConsumable( PFBaseMaleHero* pHero, INT32 slot, Target const & target);
  NCore::WorldCommand* CreateCmdBuyConsumable( PFBaseHero* pHero, PFShop* pShop, int index, int slotIndex);

  NCore::WorldCommand* CreateCmdActivateTalent( PFBaseMaleHero *pHero, INT32 level, INT32 slot );
  NCore::WorldCommand* CreateCmdUseTalent( PFBaseMaleHero *pHero, INT32 level, INT32 slot, Target const & target, bool issuedByScript = false );

  NCore::WorldCommand* CreateCmdUsePortal( PFBaseMaleHero *pHero, Target const & target, bool issuedByScript = false );

  NCore::WorldCommand* CreateCmdMinimapSignal( PFBaseHero *pHero, PFBaseUnit* pSelected, Target const & target, NDb::EFaction faction, bool issuedByScript = false );

  NCore::WorldCommand* CreateCmdInitMinigame( PFEaselPlayer* easelPlayer, INT32 objId );
  NCore::WorldCommand* CreateCmdLeaveMinigame( PFEaselPlayer* easelPlayer );

  //NCore::WorldCommand* CreateCmdDenyBuilding( PFBaseHero * pHero, PFBuilding* pBuilding );
  //NCore::WorldCommand* CreateCmdEmote( PFBaseHero* pHero, NDb::EEmotion emotion );

  NCore::WorldCommand* CreateCmdCancelChannelling( PFBaseHero* pHero );
  NCore::WorldCommand* CreateCmdUseUnit( PFBaseHero *pHero, PFBaseUnit *pUnit );
  NCore::WorldCommand* CreateCmdSetTimescale( float scale );
  NCore::WorldCommand* CreateCmdKeepAlive();
}
