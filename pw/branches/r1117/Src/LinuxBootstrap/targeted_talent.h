#pragma once

namespace NCore { class WorldCommand; }

namespace NWorld
{
class PFBaseMaleHero;
class PFBaseUnit;
class PFTalent;
class Target;

/** Read current talent readiness for HUD selection; caller still owns session/input permission checks. */
bool CanArmLinuxTargetedTalent(PFBaseMaleHero* hero, int row, int column);

/** Original HUD object selection only; a rejected object may still yield a LAND click before cast checks. */
bool CanSelectLinuxTalentUnit(PFBaseMaleHero* hero, const PFTalent* talent, PFBaseUnit* unit);

/** Validate one exact human-controlled target before scheduler submission, without executing a cast. */
bool CanUseLinuxTargetedTalent(PFBaseMaleHero* hero, int row, int column, const Target& target, int clientId);

/** Capture exact HUD ownership for a unit cast; submit through the normal scheduler with its client ID. */
NCore::WorldCommand* CreateCmdUseLinuxTargetedTalent(PFBaseMaleHero* hero, int row, int column, const Target& target);
}
