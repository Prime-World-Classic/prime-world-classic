#pragma once

#include "action_state.h"
#include <nlohmann/json.hpp>

/**
 * @brief Initialize talents on a fresh/reset authored HUD, AFTER hero identity.
 * @return Array of native {action:invoke,path:mainInterface,method,args} requests.
 * @throws std::invalid_argument For invalid data, collisions, or insufficient slots.
 *
 * All SetTalentIcon calls (9 args) precede any state changes so explicitly reserved
 * positions cannot be taken by automatic placements. Bought active talents receive
 * NOTENOUGH_DEV_POINTS -> BOUGHT (3 args each), then their actual SetTalentStatus
 * (6 args). This intermediate state is a presentation bootstrap, NOT a claim about
 * affordability: MainInterface creates shortcuts only from CANBUY/NOTENOUGH_DEV_POINTS.
 * Other talents receive their actual purchase state directly. One final
 * OnTalentsStateChanged call follows. No hero reset or fake game action is emitted.
 *
 * Submit each request sequentially and check native/AS errors. Do not replay init:
 * it creates shortcuts and cannot safely rebind an already populated action bar.
 * Caller must keep the successfully applied snapshot for UpdateCalls. On partial
 * failure, stop and recover/reset the HUD rather than treating the snapshot as applied.
 *
 * Extraction (PF_GameLogic/TalentPanelNew.cpp and ActionBarController.cpp):
 * - PFBaseMaleHero::GetTalent(level, slot), NOT GetTalent(slot, level).
 * - GetTalentDesc()->image/imageSecondState->textureFileName, upgradeLevel,
 *   rarity == NDb::TALENTRARITY_CLASS; validate nullable DB texture pointers.
 * - IsActive(), GetActionBarIndex(), GetNaftaCost().
 * - IsActivated() => Bought; otherwise hero.CanActivateTalent(level, slot):
 *   ETalentActivation::Ok => CanBuy, NoMoney => NotEnoughPrime, Denied => NotEnoughDevPoints.
 * - Prefer ActionBarController::GetTalentStatus(talent): slotState, cooldown,
 *   maxCooldown. Its underlying getters are GetCurrentCooldown(), GetCooldown(),
 *   CanBeUsed(), IsMultiState(), IsOn(), IsCastSelfLimitationPassed(), IsEnoughMana(),
 *   DoesSpendLifeInsteadEnergy(), IsForbidded(), IsActive(). Preserve its precedence.
 * - If the live controller's GetTargetingTalent() equals (slot, level), override
 *   status with Chosen as ActionBarController::Update does.
 * - alternativeState = (IsOn() && IsMultiState()) || IsSecondState().
 */
nlohmann::json PwRuffleActionInitCalls(const PwRuffleActionState& state);

/**
 * @brief Diff runtime fields against the last successfully applied snapshot.
 * @throws std::invalid_argument For invalid snapshots, changed membership/icon
 * metadata, or Bought -> unbought transitions (respec needs an external HUD reset).
 *
 * Membership and icon/placement metadata must be unchanged; vector order may vary.
 * A NotEnoughPrime -> Bought active transition first supplies the authored bridge
 * state; repeated Bought does not create duplicate shortcuts. Actual status is
 * resent on purchase, even if unchanged, so the new shortcut receives its cooldown.
 * Unchanged snapshots emit []; status-only updates do not notify purchase changes.
 * Cooldowns are finite nonnegative float-range seconds. Remaining may exceed the
 * recalculated maximum (PFAbilityData::RecalculateCooldown); values are not clamped.
 * No internal cache, VM state, player command, or fake cooldown is introduced.
 */
nlohmann::json PwRuffleActionUpdateCalls(const PwRuffleActionState& state,
	const PwRuffleActionState& previous);
