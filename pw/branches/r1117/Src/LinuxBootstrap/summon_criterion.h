#pragma once

#include "formula_context.h"
#include "../PF_GameLogic/PFBaseUnit.h"
#include "../PF_GameLogic/PFBehaviour.h"
#include "../PF_GameLogic/PFApplicator.h"

namespace LinuxBootstrap
{
/** Evaluate an original unsummon criterion against its real candidate, without sorting side effects.
 * Empty source/bytecode retains zero; compiled-only data and unknown miscellaneous
 * contexts fail. Pure candidate reads retain applicator constants and named-effect
 * identity. Selected random calls are unsupported because they cannot define a
 * consistent ordering. Mutation callers must inspect Succeeded(), never use an
 * error as an equal/default score. No candidate or evaluated value is retained.
 */
inline NumericFormulaResult EvaluateSummonCriterion(const NWorld::UnsummonCriterion& criterion,
	const NWorld::PFSummonBehaviour* behaviour)
{
	if (!criterion.criterionFormula || !behaviour || !IsValid(behaviour->GetUnit()))
		return {NumericFormulaError::ResolverFailure};
	const auto* app = dynamic_cast<const NWorld::PFBaseApplicator*>(criterion.pMisc);
	if (criterion.pMisc && !app) return {NumericFormulaError::ResolverFailure};
	const auto& source = *criterion.criterionFormula;
	if (source.sString.empty() && source.compiledString.empty()) return {NumericFormulaError::None, 0.f};
	return EvaluateReadOnlyApplicatorNumericFormula(source.sString.c_str(), criterion.pOwner, behaviour->GetUnit(), app);
}
}
