#pragma once

#include "formula_numeric.h"

struct IUnitFormulaPars;
struct ICustomFormulaPars;
struct IMiscFormulaPars;
namespace NWorld { class PFAbilityData; class PFBaseApplicator; }

namespace LinuxBootstrap
{
/** Resolve the supported s/t/r unit properties through existing live engine getters.
 * An authored expression may end with one semicolon and an optional final // line
 * annotation. The original source-length bound includes that suffix. Quoted talent
 * IDs are preserved; statements, embedded/block comments, NUL, escapes and line
 * continuations remain unsupported. This context-only normalization runs before
 * binding and does not extend the standalone scalar parser or execute comment text.
 * r is the authored receiver prefix; t remains a compatibility alias.
 * TerrainType reads the existing unit cache, with exact TERR_N/A/B constants;
 * invalid cached terrain fails. nt(native, other) lazily compares sender terrain
 * with sender faction, including neutral equality, without a fresh grid query.
 * UnitKind and Fctn bind the integer engine enums. ut_bldg/ut_hero use the receiver's
 * original UnitCheck and float-argument SwitchByBool, including eager arguments.
 * ZZimaSex uses the original integer player-sex getter, including undefined for
 * nonheroes and authored gender for unattached heroes/bots. Exact ZZSEX_UNDEFINED,
 * ZZSEX_MALE and ZZSEX_FEMALE constants do not require unit context.
 * sender.vName and receiver.vName bind live GetVariable values; Name follows
 * FormulaBuilder's [A-Z][A-Za-z_0-9]* rule. Missing variables retain the engine's
 * zero default without creating a variable. No general member/string API is exposed.
 * $ApplTarget->GetVariable("Name") reads the original applicator GetObject("Target"),
 * not its redirected receiver or ability owner. Only a single nonempty ASCII
 * identifier literal is accepted, with argument whitespace but no escapes or
 * concatenation. Selected reads require a real applicator and unit target, return
 * finite values and retain missing-variable zero without allocation. Position
 * targets fail rather than substituting a receiver; lazy reads and staged RNG
 * remain intact. Reserved transport names, other macros/methods and pointer
 * exposure are rejected. No generic GetObject/GetVariable call syntax is added.
 * sender/receiver.Get<Property>() and pFirst/pSecond->Get<Property>() are exact
 * zero-argument aliases for the existing numeric-property whitelist. Receiver
 * getters retain candidate context and lazy missing-unit validation; no arbitrary
 * methods, inferred properties or object paths are enabled.
 * sender/receiver.IsRecommended(stat) and pFirst/pSecond->IsRecommended(stat)
 * call the original live recommendation getter with one checked integer argument.
 * Finite in-range fractional arguments truncate as in C++; missing selected units
 * and unsafe conversions fail. Receiver queries keep actual candidate context,
 * and the reserved transport names cannot be injected by authored expressions.
 * sender/receiver.applName(SCOPE), including pFirst/pSecond-> spellings, delegates
 * to the original live named ring. SCOPE is exactly GLOBAL, UNIT, ABILITY, DISPATCH
 * or UNIT_AND_SUMMONS. Only whole-operand ==/!= pMisc comparisons and numeric
 * .GetVariable reads of exactly "LifeTime", "Duration" and "glyphNumber" are
 * exposed, never pointer values or mutation. Other variables, including statMod's
 * formula-recalculating getter, require separate checked transaction support.
 * Preserve first-match order, disabled live effects, original scope filtering and
 * missing-match fallback to the actual miscellaneous context (not null). Selected
 * reads require a real unit and ability/applicator context; unknown miscellaneous
 * wrappers remain unsupported. Getter results must be finite; lazy context checks
 * and staged RNG remain intact. At most 64 evaluation-local query descriptors are
 * accepted, with ASCII identifier literals and no escapes or concatenation.
 * Exact sender/receiver.FindApplicator("Name", pMisc, SCOPE) and corresponding
 * pFirst/pSecond-> calls share the same binding, including ->GetVariable suffixes.
 * Direct names are case-sensitive nonempty ASCII identifiers; argument whitespace
 * is accepted, but indices, alternate miscellaneous expressions and arbitrary
 * string syntax are not. No second registry, lookup policy or pointer API is added.
 * mScale/mParentScale and exact pMisc->/appl. getter spellings read original
 * miscellaneous scale values. mObjectParent->...->GetScale() and equivalent
 * GetObjectParent() calls allow at most 16 explicit parent hops. Original root
 * self-parent behavior is distinct from GetParentScale's default one. Applicator
 * ancestry is held and checked for cycles/depth > 128 before recursive getters;
 * nonfinite values and selected missing contexts fail, with transactional RNG.
 * refineScale(base, increment) and mRefineRate use the original ability/upgrader
 * refinement getters. Keep signed and zero rates; reject INT_MIN before the
 * original rate-minus-one subtraction, unsafe float arguments and nonfinite
 * results. Calls remain lazy and participate in the existing RNG transaction.
 * mPreparedness/mCooldown and exact pMisc->/appl. zero-argument getter aliases
 * use the actual ability/upgrader's current state. Cooldown is the total duration,
 * not remaining time; preparedness retains original elapsed/total and zero-duration
 * behavior. Invalid state indices and nonfinite running/total timers fail before
 * the original getter can conceal corruption as readiness. No parent traversal
 * or generic cooldown mutation API is exposed; lazy reads and staged RNG remain.
 * sender/receiver.GetDamageTaken(dt)/GetDamageDealt(dt), plus exact pFirst/pSecond->
 * spellings, use checked production PFBaseUnit history with one finite float argument.
 * Valid nonpositive intervals return zero. Unavailable clocks and malformed visited
 * records fail rather than becoming no-damage predicates. Unselected calls stay lazy;
 * Extended TakenEx/DealtEx(dt, mask[, pMisc]) both use original sent history. Masks
 * accept exact FormulaPars DMG_* names/aliases joined by single | (at most 64 terms),
 * not numeric masks, arbitrary expressions or generic bitwise operators. The optional
 * third argument is exactly pMisc; ability-identity selection requires a real applicator.
 * No arbitrary object paths or generic virtual fallback is added.
 * pMisc->GetVariable("damageDealed") and pMisc->GetVariable("isAutoAttack")
 * are the only direct miscellaneous variable accessors and require an applicator context.
 * Unknown properties or selected missing units fail, never silently substitute zero.
 * Constants use the existing local/global ability lookup. Recursion is limited
 * to 16 active names and 128 expressions; cycles and missing constants fail.
 * tal("id") lazily queries the real sender hero's GetTalent/IsTalentBought.
 * Missing/unbought talents are false; missing/nonhero senders fail when selected.
 * IDs are single ASCII literals without escapes/concatenation, bounded by the
 * expression limit. No other talent macros or general string APIs are exposed.
 * Repeated IDs share the evaluation-local symbol snapshot; nothing is cached
 * across calls, purchases or set replacements. Reserved binding injection fails.
 * abilityScale/damageScale use the existing ability's STAT scaling with three
 * arguments and optional fourth rounding flag (default true). Missing worlds,
 * invalid endpoints/intermediates and unsafe integer rounding fail closed.
 * No unit pointers or evaluated values are retained after the call.
 */
NumericFormulaResult EvaluateUnitNumericFormula(const std::string& expression,
	const IUnitFormulaPars* sender, const IUnitFormulaPars* target,
	const NWorld::PFAbilityData* ability = nullptr);

/** Evaluate a position receiver through its real ICustomFormulaPars interface.
 * Only the exact appl.GetNatureTypeInPos(rPos2D) accessor and TERR_N enum are added;
 * sender properties/constants remain live, but no receiver unit is substituted.
 * Nature lookup is lazy and retains the parser's evaluation-local symbol snapshot.
 * It checks finite inclusive world bounds and the terrain enum,
 * and calls the ability's existing getter. No terrain runtime or C++ API is emulated.
 */
NumericFormulaResult EvaluatePositionNumericFormula(const std::string& expression,
	const ICustomFormulaPars& position, const NWorld::PFAbilityData& ability);

/** Evaluate an applicator with live receiver/owner/upgrader context and seeded random.
 * RNG is staged locally and committed only after successful numeric evaluation.
 * Missing worlds cannot provide random; pure formulas remain usable without one.
 */
NumericFormulaResult EvaluateApplicatorNumericFormula(const std::string& expression,
	const NWorld::PFBaseApplicator& applicator);

/** Evaluate a selector condition with the original candidate and miscellaneous context.
 * Applicator/upgrader constants and variables are retained without substituting its
 * receiver for the candidate. Ability/applicator RNG commits only after success;
 * unknown miscellaneous implementations cannot supply constants or randomness.
 */
NumericFormulaResult EvaluateSelectorConditionFormula(const std::string& expression,
	const IUnitFormulaPars* sender, const IUnitFormulaPars* candidate,
	const IMiscFormulaPars* miscellaneous);

/** Reduction uses the actual damage recipient, which can differ after delegation.
 * Applicator constants/variables remain available, but reduction cannot consume RNG.
 */
NumericFormulaResult EvaluateDamageReductionNumericFormula(const std::string& expression,
	const IUnitFormulaPars* sender, const IUnitFormulaPars* receiver,
	const NWorld::PFBaseApplicator* applicator);
}
