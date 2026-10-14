#pragma once

#include "formula_numeric.h"

struct IUnitFormulaPars;
struct ICustomFormulaPars;
namespace NWorld { class PFAbilityData; class PFBaseApplicator; }

namespace LinuxBootstrap
{
/** Resolve the supported s/t/r unit properties through existing live engine getters.
 * r is the authored receiver prefix; t remains a compatibility alias.
 * TerrainType reads the existing unit cache, with exact TERR_N/A/B constants;
 * invalid cached terrain fails. nt(native, other) lazily compares sender terrain
 * with sender faction, including neutral equality, without a fresh grid query.
 * UnitKind and Fctn bind the integer engine enums. ut_bldg/ut_hero use the receiver's
 * original UnitCheck and float-argument SwitchByBool, including eager arguments.
 * sender.vName and receiver.vName bind live GetVariable values; Name follows
 * FormulaBuilder's [A-Z][A-Za-z_0-9]* rule. Missing variables retain the engine's
 * zero default without creating a variable. No general member/string API is exposed.
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
 * are the only applicator accessors and require an applicator context.
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

/** Reduction uses the actual damage recipient, which can differ after delegation.
 * Applicator constants/variables remain available, but reduction cannot consume RNG.
 */
NumericFormulaResult EvaluateDamageReductionNumericFormula(const std::string& expression,
	const IUnitFormulaPars* sender, const IUnitFormulaPars* receiver,
	const NWorld::PFBaseApplicator* applicator);
}
