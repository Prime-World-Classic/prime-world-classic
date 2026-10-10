#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

/**
 * @brief Engine/VM-independent snapshot of the local hero's authored HUD inputs.
 *
 * Absent groups produce no calls, not zero-valued substitute game data. Required
 * numbers in an engaged group start invalid (-1 or NaN); required Boolean values
 * must also be supplied. Integers are deliberately wide so builders can reject
 * values outside ActionScript's signed 32-bit range before any narrowing.
 *
 * Text is UTF-8, at most MaxTextBytes bytes, without embedded NULs. Resource names
 * are Data-relative (UI/a.dds), database-rooted (/UI/a.dds), or native (:/UI/a.dds).
 * Builders add only the :/ resource prefix; they do not rewrite path components,
 * invent assets, round stats, clamp values, or check filesystem existence.
 *
 * Unsupported: selection, teammates, custom-energy names/colors, animated avatars,
 * talents, inventory, scores, chat state, localization, and world-to-DTO extraction.
 * Energy values occupy the production mana slots (including custom energy when
 * supplied by the caller); custom-energy presentation needs separate integration.
 */
struct PwRuffleHudState
{
	/** Input-size limits, not inferred gameplay limits. */
	static constexpr std::size_t MaxTextBytes = 4096;
	static constexpr std::size_t MaxResourceBytes = 4096;
	static constexpr std::size_t MaxTableEntries = 256;

	/** One actual DBUIData.forceColors entry; color is the packed AS uint value. */
	struct ForceColor
	{
		std::int64_t force = -1;
		std::int64_t color = -1;
	};

	/** SetFriendlyHeroIdentity and SetOurHeroIdententity inputs, not mock defaults. */
	struct HeroIdentity
	{
		std::int64_t heroId = -1; ///< Player ID, not the world's unit/object ID.
		std::string heroName; ///< Player name, including an existing guild prefix.
		std::string heroClass; ///< Localized hero description; empty is supported.
		std::string portraitPath; ///< Empty means no supplied portrait.
		std::optional<bool> isMale;
		std::optional<bool> isBot;
		std::int64_t force = -1;
		std::int64_t faction = -1; ///< DBStats EFaction: neutral=0, freeze=1, burn=2.
		std::int64_t originalFaction = -1;
		double rating = -1;
		std::int64_t damageType = -1; ///< DBAbility EApplicatorDamageType: 0..4.
		std::vector<ForceColor> forceColors; ///< Required, nonempty, ascending thresholds.
		std::vector<std::int64_t> leaguePlaces; ///< Empty means no league table supplied.
		std::string rankPath;
		std::string rankTooltip;
		bool isPremium = false; ///< Optional decoration; false is the authored default.
		std::int64_t partyId = 0; ///< Zero means no party marker.
		std::string flagPath;
		std::string flagTooltip;
		std::int64_t leagueIndex = 0; ///< Flash presentation index, NOT a DB league ID.
		std::int64_t ownLeaguePlace = 0;
	};

	/** Complete SetHeroParams tuple; no partial update exists in the authored API. */
	struct HeroValues
	{
		std::int64_t level = -1; ///< Nonnegative AS int; caller supplies the actual level.
		std::int64_t health = -1;
		std::int64_t maxHealth = -1; ///< Positive; health must be in [0, maxHealth].
		std::int64_t energy = -1;
		std::int64_t maxEnergy = -1; ///< Zero is allowed for a hero without energy.
		std::optional<bool> isVisible;
		std::optional<bool> isPickable;
		std::optional<std::int64_t> resurrectionSeconds; ///< -1 means alive, as in PFHero.
		double channeling = std::numeric_limits<double>::quiet_NaN(); ///< Fraction [0, 1].
		double healthRegen = std::numeric_limits<double>::quiet_NaN(); ///< Signed, finite.
		double energyRegen = std::numeric_limits<double>::quiet_NaN(); ///< Signed, finite.
		std::optional<bool> isCameraLocked;
		double ultimateCooldown = std::numeric_limits<double>::quiet_NaN(); ///< -1 or [0, 1].
	};

	/** Both currency values are mandatory; prime and ZZ gold are different resources. */
	struct Development
	{
		std::int64_t prime = -1;
		std::int64_t gold = -1;
	};

	/** Complete SetGameProgress tuple; a time-only call would invent terrain data. */
	struct MatchProgress
	{
		double humanTerrain = std::numeric_limits<double>::quiet_NaN(); ///< Fraction [0, 1].
		double elfTerrain = std::numeric_limits<double>::quiet_NaN(); ///< Fraction [0, 1].
		std::int64_t matchSeconds = -1;
		std::optional<std::int64_t> creepSpawnSeconds; ///< Signed AS int; <=0 hides countdown.
	};

	std::optional<HeroIdentity> hero;
	std::optional<HeroValues> values; ///< Requires hero to identify the recipient.
	std::optional<Development> development; ///< Requires a local hero.
	std::optional<MatchProgress> match; ///< May be emitted without a local hero.
};
