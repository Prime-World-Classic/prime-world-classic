#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

/**
 * @brief Plain snapshot of real local-hero talents; no engine, VM, or JSON types.
 *
 * An empty snapshot emits no calls. Include only existing talents, not invented
 * empty-slot icons. Every field of an included talent must be known; invalid
 * sentinels/unset Booleans are rejected, not converted into playable defaults.
 * Paths are UTF-8 Data-relative, /Data-root-relative, or :/ resource names; the
 * builders preserve bytes except for adding the :/ prefix. No filesystem reads.
 *
 * Unsupported: inventory, portal, global cooldown, tooltip text, activation/input
 * dispatch, drag/drop persistence, respec, loadout replacement, and VM ownership.
 * Initialize after hero identity has reset the HUD, before inventory/drag changes.
 */
struct PwRuffleActionState
{
	static constexpr std::size_t Columns = 6; ///< DBTalent ETalentSlot and authored RAW.
	static constexpr std::size_t Rows = 6; ///< DBTalent ETalentLevel.
	static constexpr std::size_t ActionSlots = 10; ///< ActionBar.actionButtons, not portal.
	static constexpr std::size_t MaxResourceBytes = 4096;

	/** Exact native TalentUIState wire values; authored DISABLE=4 is not implemented. */
	enum class PurchaseState : std::int32_t
	{
		Invalid = -1,
		Bought = 0,
		CanBuy = 1,
		NotEnoughDevPoints = 2,
		NotEnoughPrime = 3
	};

	/** Exact ActionBarSlotState wire values (native spells value 2 ActivetedSpecial). */
	enum class SlotState : std::int32_t
	{
		Invalid = -1,
		Active = 0,
		ActiveSpecial = 1,
		ActivatedSpecial = 2,
		NotEnoughMana = 3,
		Disabled = 4,
		Chosen = 5,
		NotEnoughLife = 6
	};

	/** One complete icon/purchase/status tuple, addressed by slot(column), level(row). */
	struct Talent
	{
		std::int64_t column = -1;
		std::int64_t row = -1;
		std::string iconPath; ///< Required actual DB image, never a placeholder.
		std::string alternativeIconPath; ///< Empty means DB has no second-state image.
		std::optional<bool> active; ///< IsActive(), NOT IsActivated() (purchased).
		std::int64_t desiredIndex = -3; ///< -1 automatic, -2 excluded, or 0..9.
		std::int64_t upgradeLevel = -1; ///< Authored Talent.maxUpgradeLevel is 3.
		std::optional<bool> classTalent;
		std::int64_t cost = -1; ///< Actual GetNaftaCost(), nonnegative signed AS int.
		PurchaseState purchase = PurchaseState::Invalid;
		SlotState status = SlotState::Invalid;
		double cooldown = std::numeric_limits<double>::quiet_NaN(); ///< Seconds, not fraction.
		double maxCooldown = std::numeric_limits<double>::quiet_NaN(); ///< Seconds; zero allowed.
		std::optional<bool> alternativeState; ///< (IsOn() && IsMultiState()) || IsSecondState().
	};

	std::vector<Talent> talents; ///< At most 36 unique cells; builders use row-major order.
};
