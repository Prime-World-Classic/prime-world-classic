#include "../System/systemStdAfx.h"
#include "../PF_GameLogic/StringExecutorBootstrap.h"
#include "formula_context.h"

namespace LinuxBootstrap
{
NumericFormulaResult EvaluateUnitNumericFormula(const std::string& expression,
	const IUnitFormulaPars* sender, const IUnitFormulaPars* target)
{
	return EvaluateNumericFormula(expression, [&](const std::string& name, double& value) {
		if (name.size() < 2 || (name[0] != 's' && name[0] != 't')) return false;
		const auto* unit = name[0] == 's' ? sender : target;
		if (!unit) return false;
		const auto property = name.substr(1);
		if (property == "BaseStrength") value = unit->GetBaseStrength();
		else if (property == "BaseIntellect") value = unit->GetBaseIntellect();
		else if (property == "Range") value = unit->GetRange();
		else if (property == "Intellect") value = unit->GetIntellect();
		else return false;
		return true;
	});
}
}
