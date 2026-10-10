#pragma once

#include "formula_numeric.h"

struct IUnitFormulaPars;

namespace LinuxBootstrap
{
/** Resolve the supported s/t unit properties through existing live engine getters.
 * Unknown properties or selected missing units fail, never silently substitute zero.
 * No unit pointers or evaluated values are retained after the call.
 */
NumericFormulaResult EvaluateUnitNumericFormula(const std::string& expression,
	const IUnitFormulaPars* sender, const IUnitFormulaPars* target);
}
