#pragma once

#include "ClientVisibilityFlags.hpp"
#include "System/StarForce/StarForce.h"

namespace NWorld
{
  class PFLogicObject;
  class PFPlayer;
}

namespace PF_Core
{
  class ClientObjectBase;
}

namespace NGameX
{
  class PFClientBaseUnit;
  class PFClientCreature;
  class PFClientLogicObject;

  class ClientVisibilityHelper : public NonCopyable
  {
  public:
    STARFORCE_FORCE_INLINE static bool IsSpectatorMode()
    {
      return false;
    }

    STARFORCE_FORCE_INLINE static bool IsSharedVisionMode()
    {
      return false;
    }

    STARFORCE_FORCE_INLINE static bool IsPointVisible(const CVec2&)
    {
      return true;
    }

    STARFORCE_FORCE_INLINE static NDb::EFaction GetPlayerFaction()
    {
      return NDb::FACTION_NEUTRAL;
    }

    STARFORCE_FORCE_INLINE static bool IsPartialVisibilityApplicable(const ClientVisibilityFlags& flags)
    {
      if (!flags.enemy || flags.sharedVision)
        return false;

      if (!flags.placementVisible || flags.objectVisible)
        return false;

      if (!flags.hasInvisibility || flags.hasIgnoreInvisibility)
        return false;

      return true;
    }

    STARFORCE_FORCE_INLINE static bool IsVisibleForPlayer(const ClientVisibilityFlags& flags)
    {
      if (!flags.enemy || flags.sharedVision)
        return true;

      return flags.objectVisible && flags.placementVisible;
    }

    STARFORCE_FORCE_INLINE static void UpdateFlags(const NWorld::PFLogicObject* const worldObject, ClientVisibilityFlags& flags)
    {
      flags.Reset();

      if (!worldObject)
        return;

      flags.objectVisible = true;
      flags.placementVisible = true;
      flags.sharedVision = true;
    }

    STARFORCE_FORCE_INLINE static void UpdateFlags(const NWorld::PFPlayer* const, const NWorld::PFLogicObject* const worldObject, ClientVisibilityFlags& flags)
    {
      UpdateFlags(worldObject, flags);
    }

    STARFORCE_FORCE_INLINE static ClientVisibilityFlags GetFlags(const PFClientLogicObject* const clientObject)
    {
      ClientVisibilityFlags flags;

      if (clientObject)
      {
        flags.objectVisible = true;
        flags.placementVisible = true;
        flags.sharedVision = true;
      }

      return flags;
    }

    STARFORCE_FORCE_INLINE static ClientVisibilityFlags GetFlags(const PF_Core::ClientObjectBase* const clientObject)
    {
      ClientVisibilityFlags flags;

      if (clientObject)
      {
        flags.objectVisible = true;
        flags.placementVisible = true;
        flags.sharedVision = true;
      }

      return flags;
    }

    STARFORCE_FORCE_INLINE static ClientVisibilityFlags GetFlags(const NWorld::PFLogicObject* const worldObject)
    {
      ClientVisibilityFlags flags;

      UpdateFlags(worldObject, flags);

      return flags;
    }

    STARFORCE_FORCE_INLINE static bool IsPartialVisibilityApplicable(const PFClientLogicObject* const clientObject)
    {
      const ClientVisibilityFlags flags(GetFlags(clientObject));

      return IsPartialVisibilityApplicable(flags);
    }

    STARFORCE_FORCE_INLINE static bool IsPartialVisibilityApplicable(const PF_Core::ClientObjectBase* const clientObject)
    {
      const ClientVisibilityFlags flags(GetFlags(clientObject));

      return IsPartialVisibilityApplicable(flags);
    }

    STARFORCE_FORCE_INLINE static bool IsPartialVisibilityApplicable(const NWorld::PFLogicObject* const worldObject)
    {
      const ClientVisibilityFlags flags(GetFlags(worldObject));

      return IsPartialVisibilityApplicable(flags);
    }

    STARFORCE_FORCE_INLINE static bool IsVisibleForPlayer(const PFClientLogicObject* const clientObject)
    {
      const ClientVisibilityFlags flags(GetFlags(clientObject));

      return IsVisibleForPlayer(flags);
    }

    STARFORCE_FORCE_INLINE static bool IsVisibleForPlayer(const PF_Core::ClientObjectBase* const clientObject)
    {
      const ClientVisibilityFlags flags(GetFlags(clientObject));

      return IsVisibleForPlayer(flags);
    }

    STARFORCE_FORCE_INLINE static bool IsVisibleForPlayer(const NWorld::PFLogicObject* const worldObject)
    {
      const ClientVisibilityFlags flags(GetFlags(worldObject));

      return IsVisibleForPlayer(flags);
    }

  private:
    ClientVisibilityHelper();
    ~ClientVisibilityHelper();
  };

  class DeveloperClientVisibilityHelper : public NonCopyable
  {
  public:
    STARFORCE_FORCE_INLINE static bool CanCreatureSleep(const NGameX::PFClientCreature* const)
    {
      return true;
    }

    STARFORCE_FORCE_INLINE static void ForceUnitVisibilityIfNecessary(NGameX::PFClientBaseUnit* const)
    {
    }

  private:
    DeveloperClientVisibilityHelper();
    ~DeveloperClientVisibilityHelper();
  };
}
