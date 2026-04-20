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

  NCore::WorldCommand* CreateCmdCombatMoveHero( PFBaseHero* pHero, CVec2 const& target );
  NCore::WorldCommand* CreateCmdMoveHero( PFBaseHero* pHero, CVec2 const& target, bool issuedByScript = false );
  NCore::WorldCommand* CreateCmdStopHero( PFBaseHero* pHero);
  NCore::WorldCommand* CreateCmdAttackTarget( PFBaseHero* pHero,  PFBaseUnit* pTarget, bool issuedByScript = false );
  NCore::WorldCommand* CreateCmdPickupObject( PFBaseHero* pHero, int32_t objId );
  NCore::WorldCommand* CreateCmdFollowUnit( PFBaseHero* pHero, PFBaseUnit* pUnit, float followRange = 0.f, float forceFollowRange = 0.f, bool issuedByScript = false );
  NCore::WorldCommand* CreateCmdHold(PFBaseHero* pHero);
  NCore::WorldCommand* CreateCmdRaiseFlag(PFBaseHero *pHero, PFFlagpole *pFlagpole, bool issuedByScript = false );
  
  //////////////////////////////////////////////////////////////////////////
  NCore::WorldCommand* CreateCmdUseConsumable( PFBaseMaleHero* pHero, int32_t slot, Target const & target);
  NCore::WorldCommand* CreateCmdBuyConsumable( PFBaseHero* pHero, PFShop* pShop, int index, int slotIndex);

  NCore::WorldCommand* CreateCmdActivateTalent( PFBaseMaleHero *pHero, int32_t level, int32_t slot );
  NCore::WorldCommand* CreateCmdUseTalent( PFBaseMaleHero *pHero, int32_t level, int32_t slot, Target const & target, bool issuedByScript = false );

  NCore::WorldCommand* CreateCmdUsePortal( PFBaseMaleHero *pHero, Target const & target, bool issuedByScript = false );

  NCore::WorldCommand* CreateCmdMinimapSignal( PFBaseHero *pHero, PFBaseUnit* pSelected, Target const & target, NDb::EFaction faction, bool issuedByScript = false );

  NCore::WorldCommand* CreateCmdInitMinigame( PFEaselPlayer* easelPlayer, int32_t objId );

  //NCore::WorldCommand* CreateCmdDenyBuilding( PFBaseHero * pHero, PFBuilding* pBuilding );
  //NCore::WorldCommand* CreateCmdEmote( PFBaseHero* pHero, NDb::EEmotion emotion );

  NCore::WorldCommand* CreateCmdCancelChannelling( PFBaseHero* pHero );
  NCore::WorldCommand* CreateCmdUseUnit( PFBaseHero *pHero, PFBaseUnit *pUnit );
  NCore::WorldCommand* CreateCmdSetTimescale( float scale );
  NCore::WorldCommand* CreateCmdKeepAlive();
}