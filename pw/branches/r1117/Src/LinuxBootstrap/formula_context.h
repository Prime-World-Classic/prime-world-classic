#pragma once

#include "formula_numeric.h"

struct IUnitFormulaPars;
namespace NWorld { class PFAbilityData; class PFBaseApplicator; }

namespace LinuxBootstrap
{
/** Resolve the supported s/t/r unit properties through existing live engine getters.
 * r is the authored receiver prefix; t remains a compatibility alias.
 * UnitKind binds the integer engine enum. ut_bldg/ut_hero use the receiver's
 * original UnitCheck and float-argument SwitchByBool, including eager arguments.
 * Four exact combat accessors bind scalars: pMisc->GetVariable("damageDealed"),
 * pMisc->GetVariable("isAutoAttack"), sender.vSpellPierce and receiver.vSpellDef.
 * The two pMisc accessors require an applicator; no general object/string API.
 * Unknown properties or selected missing units fail, never silently substitute zero.
 * Constants use the existing local/global ability lookup. Recursion is limited
 * to 16 active names and 128 expressions; cycles and missing constants fail.
 * abilityScale/damageScale use the existing ability's STAT scaling with three
 * arguments and optional fourth rounding flag (default true). Missing worlds,
 * invalid endpoints/intermediates and unsafe integer rounding fail closed.
 * No unit pointers or evaluated values are retained after the call.
 */
NumericFormulaResult EvaluateUnitNumericFormula(const std::string& expression,
	const IUnitFormulaPars* sender, const IUnitFormulaPars* target,
	const NWorld::PFAbilityData* ability = nullptr);

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
