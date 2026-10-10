#pragma once

#include "hud_state.h"
#include <nlohmann/json.hpp>

/**
 * @brief Build ordered native invoke requests for ONE fresh local-hero HUD binding.
 * @return An array of {action:invoke, path:mainInterface, method, args} objects.
 * @throws std::invalid_argument On any invalid engaged snapshot group, before
 * returning any requests. Diagnostics identify the invalid field.
 *
 * Caller must finish the authored movie/localization initialization first, then
 * submit each request individually, in order, checking native/AS errors. This
 * function does not execute calls or prove the movie is initialized.
 *
 * Order (TeamInfoNew/UnitInfoNew): SetOurHeroIdententity, SetForceColors,
 * SetFriendlyHeroIdentity, ShowHeroPortrait. Our identity sets ourID and resets
 * the HUD; friendly identity then sets IsOurHero and the portrait/flag metadata.
 * The force table must precede friendly identity: FillPlayerData reads its last
 * entry unconditionally. The misspelled SetOurHeroIdententity name is authored.
 *
 * Do NOT replay each frame: SetForceColors appends and SetOurHeroIdententity resets
 * inventories/talents. Rebinding an existing movie needs separate lifecycle work.
 * See PF_GameLogic/AdventureFlashInterface.cpp and Session UI/classes/MainInterface.as.
 */
nlohmann::json PwRuffleHeroIdentityCalls(const PwRuffleHudState& state);

/**
 * @brief Build optional SetHeroParams, SetHeroDevelopmentParams, SetGameProgress
 * requests in that order, preserving production argument order and arity (14/2/4).
 * @return An array; a default snapshot produces [] with no fabricated statistics.
 * @throws std::invalid_argument On any invalid engaged snapshot group.
 *
 * Apply identity calls successfully before hero updates. No null placeholders or
 * invented values fill missing groups. A complete development or match tuple is
 * required even when only gold or time changed. No diffing or VM state is retained.
 */
nlohmann::json PwRuffleHeroValueCalls(const PwRuffleHudState& state);
