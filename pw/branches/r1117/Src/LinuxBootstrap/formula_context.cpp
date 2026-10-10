#include "../System/systemStdAfx.h"
#include "../PF_GameLogic/StringExecutorBootstrap.h"
#include "formula_context.h"
#include "../PF_GameLogic/PFAbilityData.h"
#include <algorithm>
#include <map>
#include <vector>

namespace LinuxBootstrap
{
namespace
{

/** Evaluation-local ownership bounds recursive constants without stale stat caches. */
struct Context
{
	const IUnitFormulaPars* sender;
	const IUnitFormulaPars* target;
	const NWorld::PFAbilityData* ability;
	unsigned expressions = 0;
	std::vector<std::string> active;
	std::map<std::string, float> constants;

	NumericFormulaResult Evaluate(const std::string& expression)
	{
		if (++expressions > 128) return {NumericFormulaError::LimitExceeded};
		return EvaluateNumericFormula(expression, [&](const std::string& name, double& value) {
			return Resolve(name, value);
		});
	}

	bool Resolve(const std::string& name, double& value)
	{
		if (name.size() > 1 && name[0] == 'c')
		{
			if (!ability || active.size() >= 16 || std::find(active.begin(), active.end(), name) != active.end()) return false;
			const auto cached = constants.find(name);
			if (cached != constants.end()) { value = cached->second; return true; }
			const auto* constant = ability->GetConstant(name.c_str() + 1);
			if (!constant) return false;
			active.push_back(name);
			const auto result = Evaluate(constant->var.sString.c_str());
			active.pop_back();
			if (!result.Succeeded()) return false;
			constants.emplace(name, result.value);
			value = result.value;
			return true;
		}
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
	}
};
}

NumericFormulaResult EvaluateUnitNumericFormula(const std::string& expression,
	const IUnitFormulaPars* sender, const IUnitFormulaPars* target,
	const NWorld::PFAbilityData* ability)
{
	Context context{sender, target, ability};
	return context.Evaluate(expression);
}
}
