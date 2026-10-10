#pragma once

#include "formula_numeric.h"

struct IUnitFormulaPars;
namespace NWorld { class PFAbilityData; }

namespace LinuxBootstrap
{
/** Resolve the supported s/t unit properties through existing live engine getters.
 * Unknown properties or selected missing units fail, never silently substitute zero.
 * Constants use the existing local/global ability lookup. Recursion is limited
 * to 16 active names and 128 expressions; cycles and missing constants fail.
 * No unit pointers or evaluated values are retained after the call.
 */
NumericFormulaResult EvaluateUnitNumericFormula(const std::string& expression,
	const IUnitFormulaPars* sender, const IUnitFormulaPars* target,
	const NWorld::PFAbilityData* ability = nullptr);
}
