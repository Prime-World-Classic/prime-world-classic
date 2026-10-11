#include "../System/systemStdAfx.h"
#include "../PF_GameLogic/StringExecutorBootstrap.h"
#include "formula_context.h"
#include "../PF_GameLogic/PFAbilityData.h"
#include "../PF_GameLogic/PFBaseUnit.h"
#include "../PF_GameLogic/PFWorld.h"
#include "../PF_GameLogic/PFAIWorld.h"
#include <algorithm>
#include <map>
#include <vector>

namespace LinuxBootstrap
{
namespace
{

/** Exact IUnitFormulaPars getter names after the s/t prefix; no aliases or inferred Base names.
 * This fixed 40-property whitelist covers pools/maxima, direct combat/movement stats,
 * regeneration/drain/evasion/critical multiplier and their corresponding base values.
 * BaseAttack is a modified stat; BaseBaseAttack is its unmodified counterpart.
 * Derived stats, economy and object/boolean/misc properties are not in this table.
 */
struct UnitNumericProperty
{
	const char* name;
	float (IUnitFormulaPars::*getter)() const;
};

constexpr UnitNumericProperty unitNumericProperties[] = {
	{"Life", &IUnitFormulaPars::GetLife},
	{"Energy", &IUnitFormulaPars::GetEnergy},
	{"MaxLife", &IUnitFormulaPars::GetMaxLife},
	{"MaxEnergy", &IUnitFormulaPars::GetMaxEnergy},
	{"Range", &IUnitFormulaPars::GetRange},
	{"MoveSpeed", &IUnitFormulaPars::GetMoveSpeed},
	{"AttackSpeed", &IUnitFormulaPars::GetAttackSpeed},
	{"CritMult", &IUnitFormulaPars::GetCritMult},
	{"LifeRegen", &IUnitFormulaPars::GetLifeRegen},
	{"EnergyRegen", &IUnitFormulaPars::GetEnergyRegen},
	{"LifeRegenAbs", &IUnitFormulaPars::GetLifeRegenAbs},
	{"EnergyRegenAbs", &IUnitFormulaPars::GetEnergyRegenAbs},
	{"Evasion", &IUnitFormulaPars::GetEvasion},
	{"LifeDrain", &IUnitFormulaPars::GetLifeDrain},
	{"EnergyDrain", &IUnitFormulaPars::GetEnergyDrain},
	{"Strength", &IUnitFormulaPars::GetStrength},
	{"Intellect", &IUnitFormulaPars::GetIntellect},
	{"Dexterity", &IUnitFormulaPars::GetDexterity},
	{"BaseAttack", &IUnitFormulaPars::GetBaseAttack},
	{"Stamina", &IUnitFormulaPars::GetStamina},
	{"Will", &IUnitFormulaPars::GetWill},
	{"BaseLife", &IUnitFormulaPars::GetBaseLife},
	{"BaseEnergy", &IUnitFormulaPars::GetBaseEnergy},
	{"BaseRange", &IUnitFormulaPars::GetBaseRange},
	{"BaseMoveSpeed", &IUnitFormulaPars::GetBaseMoveSpeed},
	{"BaseAttackSpeed", &IUnitFormulaPars::GetBaseAttackSpeed},
	{"BaseCriticalMultiplier", &IUnitFormulaPars::GetBaseCriticalMultiplier},
	{"BaseLifeDrain", &IUnitFormulaPars::GetBaseLifeDrain},
	{"BaseEnergyDrain", &IUnitFormulaPars::GetBaseEnergyDrain},
	{"BaseEvasion", &IUnitFormulaPars::GetBaseEvasion},
	{"BaseLifeRegeneration", &IUnitFormulaPars::GetBaseLifeRegeneration},
	{"BaseLifeRegenerationAbsolute", &IUnitFormulaPars::GetBaseLifeRegenerationAbsolute},
	{"BaseEnergyRegeneration", &IUnitFormulaPars::GetBaseEnergyRegeneration},
	{"BaseEnergyRegenerationAbsolute", &IUnitFormulaPars::GetBaseEnergyRegenerationAbsolute},
	{"BaseStrength", &IUnitFormulaPars::GetBaseStrength},
	{"BaseIntellect", &IUnitFormulaPars::GetBaseIntellect},
	{"BaseDexterity", &IUnitFormulaPars::GetBaseDexterity},
	{"BaseBaseAttack", &IUnitFormulaPars::GetBaseBaseAttack},
	{"BaseStamina", &IUnitFormulaPars::GetBaseStamina},
	{"BaseWill", &IUnitFormulaPars::GetBaseWill}
};

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
		}, [&](const std::string& name, const double* args, std::size_t count, double& value) {
			return Scale(name, args, count, value);
		});
	}

	/** Validate before the existing helper can mask invalid arithmetic or narrow to int. */
	bool Scale(const std::string& name, const double* args, std::size_t count, double& value)
	{
		if ((name != "abilityScale" && name != "damageScale") || (count != 3 && count != 4) || !ability) return false;
		const NWorld::PFBaseUnit* owner = ability->GetOwner();
		const auto* world = owner ? owner->GetWorld() : nullptr;
		const auto* ai = world ? world->GetAIWorld() : nullptr;
		if (!ai) return false;
		for (std::size_t i = 0; i < count; ++i)
			if (!std::isfinite(args[i]) || (i < 3 && std::fabs(args[i]) > std::numeric_limits<float>::max())) return false;
		const bool damage = name == "damageScale";
		const bool rounded = count == 3 || args[3] != 0;
		const auto& params = ai->GetAIParameters().abilityAndDamageScalingParams;
		const float left = damage ? params.damageScaleStatLeft : params.abilityScaleStatLeft;
		const float right = damage ? params.damageScaleStatRight : params.abilityScaleStatRight;
		const float stat = float(args[0]), low = float(args[1]), high = float(args[2]);
		const float width = right - left, offset = stat - left, span = high - low;
		if (!std::isfinite(left) || !std::isfinite(right) || !std::isfinite(width) || width == 0 ||
			!std::isfinite(offset) || !std::isfinite(span)) return false;
		const float product = offset * span;
		if (!std::isfinite(product)) return false;
		const float quotient = product / width;
		const float candidate = quotient + low;
		if (!std::isfinite(quotient) || !std::isfinite(candidate)) return false;
		if (rounded)
		{
			const float result = candidate > low ? candidate : low;
			const float integerInput = result >= 0 ? result + .5f : result - .5f;
			if (double(integerInput) < std::numeric_limits<int>::min() ||
				double(integerInput) > std::numeric_limits<int>::max()) return false;
		}
		value = ability->GetAbilityScale(damage, stat, ABILITYSCALEMODE_STAT, low, high, rounded);
		return std::isfinite(value);
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
		for (const auto& binding : unitNumericProperties)
			if (property == binding.name)
			{
				value = (unit->*binding.getter)();
				return true;
			}
		return false;
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
