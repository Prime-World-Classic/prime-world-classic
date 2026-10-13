#include "../System/systemStdAfx.h"
#include "../PF_GameLogic/StringExecutorBootstrap.h"
#include "formula_context.h"
#include "../PF_GameLogic/PFAbilityData.h"
#include "../PF_GameLogic/PFBaseUnit.h"
#include "../PF_GameLogic/PFWorld.h"
#include "../PF_GameLogic/PFAIWorld.h"
#include "../PF_GameLogic/PFApplicator.h"
#include <algorithm>
#include <map>
#include <vector>

namespace LinuxBootstrap
{
namespace
{

/** Exact IUnitFormulaPars getter names after the s/t prefix; no aliases or inferred Base names.
 * This fixed whitelist covers pools/maxima, direct combat/movement stats,
 * regeneration/drain/evasion/critical multiplier and their corresponding base values.
 * BaseAttack is a modified stat; BaseBaseAttack is its unmodified counterpart.
 * Derived getters are explicitly listed; other economy, object and boolean
 * properties are not inferred. Combat accessors below have a separate whitelist.
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
	{"DamageMin", &IUnitFormulaPars::GetDamageMin},
	{"DamageMax", &IUnitFormulaPars::GetDamageMax},
	{"BodyDefence", &IUnitFormulaPars::GetBodyDefence},
	{"SpiritDefence", &IUnitFormulaPars::GetSpiritDefence},
	{"PenetrationTotal", &IUnitFormulaPars::GetPenetrationTotal},
	{"MaterialDamageDecrease", &IUnitFormulaPars::GetMaterialDamageDecrease},
	{"EnergyDamageDecrease", &IUnitFormulaPars::GetEnergyDamageDecrease},
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

/** Bind whitelisted unit variables, recent-history calls and exact applicator scalars.
 * Unit variables follow FormulaBuilder's v[A-Z][A-Za-z_0-9]* rule, not a C++ member API.
 * Reserved names cannot enter from source; muParser retains all arithmetic/grammar checks.
 */
bool BindCombatAccessors(const std::string& source, std::string& result, bool positionContext)
{
	if (source.size() > NumericFormulaMaxLength) return false;
	const auto nameChar = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || (c >= '0' && c <= '9'); };
	for (std::size_t pos = 0; pos < source.size();)
	{
		if (!nameChar(source[pos])) { result += source[pos++]; continue; }
		const std::size_t start = pos;
		while (pos < source.size() && nameChar(source[pos])) ++pos;
		const std::string name = source.substr(start, pos - start);
		if (name.compare(0, 8, "__combat") == 0 || name.compare(0, 9, "__unitvar") == 0 ||
			name.compare(0, 10, "__position") == 0 || name.compare(0, 10, "__history_") == 0) return false;
		if (name == "pMisc")
		{
			bool found = false;
			for (const char* variable : {"damageDealed", "isAutoAttack"})
			{
				const std::string suffix = std::string("->GetVariable(\"") + variable + "\")";
				if (source.compare(pos, suffix.size(), suffix) != 0) continue;
				result += std::string(" __combat_") + variable + " "; pos += suffix.size(); found = true; break;
			}
			if (!found) return false;
		}
		else if (name == "appl")
		{
			const std::string suffix = ".GetNatureTypeInPos(rPos2D)";
			if (!positionContext || source.compare(pos, suffix.size(), suffix) != 0) return false;
			result += " __position_nature "; pos += suffix.size();
		}
		else if (name == "sender" || name == "receiver" || name == "pFirst" || name == "pSecond")
		{
			const bool pointer = name == "pFirst" || name == "pSecond";
			bool history = false;
			for (const char* method : {"GetDamageTaken", "GetDamageDealt"})
			{
				const std::string suffix = std::string(pointer ? "->" : ".") + method;
				if (source.compare(pos, suffix.size(), suffix) != 0) continue;
				std::size_t open = pos + suffix.size();
				while (open < source.size() && (source[open] == ' ' || source[open] == '\t' || source[open] == '\r' || source[open] == '\n')) ++open;
				if (open == source.size() || source[open] != '(') return false;
				result += std::string(name == "sender" || name == "pFirst" ? " __history_s_" : " __history_r_") +
					(std::string(method) == "GetDamageTaken" ? "taken" : "dealt");
				pos = open; history = true; break;
			}
			if (history) continue;
			if (pointer) return false;
			if (source.compare(pos, 2, ".v") != 0 || pos + 2 >= source.size() ||
				source[pos + 2] < 'A' || source[pos + 2] > 'Z') return false;
			pos += 2;
			const std::size_t variableStart = pos;
			while (pos < source.size() && nameChar(source[pos])) ++pos;
			result += std::string(name == "sender" ? " __unitvar_s_" : " __unitvar_r_") +
				source.substr(variableStart, pos - variableStart) + " ";
		}
		else result += name;
	}
	return true;
}

/** Evaluation-local ownership bounds recursive constants without stale stat caches. */
struct Context
{
	const IUnitFormulaPars* sender;
	const IUnitFormulaPars* target;
	const NWorld::PFAbilityData* ability;
	NRandom::RandomGenerator* random = nullptr;
	const NWorld::PFBaseApplicator* applicator = nullptr;
	const ICustomFormulaPars* position = nullptr;
	unsigned expressions = 0;
	std::vector<std::string> active;
	std::map<std::string, float> constants;

	NumericFormulaResult Evaluate(const std::string& expression)
	{
		if (++expressions > 128) return {NumericFormulaError::LimitExceeded};
		std::string bound;
		if (!BindCombatAccessors(expression, bound, position != nullptr)) return {NumericFormulaError::UnsupportedSyntax};
		return EvaluateNumericFormula(bound, [&](const std::string& name, double& value) {
			return Resolve(name, value);
		}, [&](const std::string& name, const double* args, std::size_t count, double& value) {
			return Scale(name, args, count, value);
		});
	}

	/** Validate before the existing helper can mask invalid arithmetic or narrow to int. */
	bool Scale(const std::string& name, const double* args, std::size_t count, double& value)
	{
		if (name == "__history_s_taken" || name == "__history_s_dealt" ||
			name == "__history_r_taken" || name == "__history_r_dealt")
		{
			if (count != 1 || !std::isfinite(args[0]) || std::fabs(args[0]) > std::numeric_limits<float>::max()) return false;
			const bool fromSender = name == "__history_s_taken" || name == "__history_s_dealt";
			const auto* unit = dynamic_cast<const NWorld::PFBaseUnit*>(fromSender ? sender : target);
			if (!unit) return false;
			float result;
			const bool valid = name == "__history_s_taken" || name == "__history_r_taken"
				? unit->TryGetDamageTaken(static_cast<float>(args[0]), result)
				: unit->TryGetDamageDealt(static_cast<float>(args[0]), result);
			if (!valid) return false;
			value = result; return true;
		}
		if (name == "ut_bldg" || name == "ut_hero")
		{
			if (!target || count != 2) return false;
			for (std::size_t i = 0; i < count; ++i)
				if (!std::isfinite(args[i]) || std::fabs(args[i]) > std::numeric_limits<float>::max()) return false;
			// FormulaBuilder uses these receiver checks and float-argument SwitchByBool.
			value = SwitchByBool(target->UnitCheck(name == "ut_bldg" ? UNITCHECKID_ISBUILDING : UNITCHECKID_ISHERO),
				static_cast<float>(args[0]), static_cast<float>(args[1]));
			return true;
		}
		if (name == "random")
		{
			if (!random || count != 2) return false;
			for (std::size_t i = 0; i < count; ++i)
				if (!std::isfinite(args[i]) || args[i] < std::numeric_limits<int>::min() || args[i] > std::numeric_limits<int>::max()) return false;
			const int low = static_cast<int>(args[0]), high = static_cast<int>(args[1]);
			if (low < high && static_cast<long long>(high) - low + 1 > std::numeric_limits<int>::max()) return false;
			value = random->Next(low, high);
			return true;
		}
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
		if (name == "__position_nature")
		{
			if (!position || !ability || !IsValid(ability->GetOwner())) return false;
			const auto* world = ability->GetOwner()->GetWorld();
			if (!world || !world->GetNatureMap()) return false;
			const CVec2 point = position->GetPos2D(), size = world->GetMapSize();
			// The original grid narrows coordinates to int; reject unsafe geometry before lookup.
			if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
				!std::isfinite(size.x) || !std::isfinite(size.y) || size.x <= 0 || size.y <= 0 ||
				point.x < 0 || point.y < 0 || point.x > size.x || point.y > size.y) return false;
			const int nature = ability->GetNatureTypeInPos(point);
			if (nature < TERR_N || nature > TERR_B) return false;
			value = nature; return true;
		}
		// FormulaPars terrain constants are valid in ordinary unit formulas too.
		if (name == "TERR_N") { value = TERR_N; return true; }
		if (name == "TERR_A") { value = TERR_A; return true; }
		if (name == "TERR_B") { value = TERR_B; return true; }
		if (name == "__combat_damageDealed" || name == "__combat_isAutoAttack")
		{
			if (!applicator) return false;
			value = applicator->GetVariable(name.c_str() + 9); return true;
		}
		if (name.compare(0, 12, "__unitvar_s_") == 0 || name.compare(0, 12, "__unitvar_r_") == 0)
		{
			const auto* unit = name[10] == 's' ? sender : target;
			if (!unit) return false;
			value = unit->GetVariable(name.c_str() + 12); return true;
		}
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
		if (name.size() < 2 || (name[0] != 's' && name[0] != 't' && name[0] != 'r')) return false;
		const auto* unit = name[0] == 's' ? sender : target;
		if (!unit) return false;
		const auto property = name.substr(1);
		if (property == "TerrainType")
		{
			// Read the original unit cache, not a fresh position query or the owner's terrain.
			const int nature = unit->GetTerrainType();
			if (nature < TERR_N || nature > TERR_B) return false;
			value = nature;
			return true;
		}
		if (property == "Fctn")
		{
			value = unit->GetFctn();
			return true;
		}
		if (property == "UnitKind")
		{
			value = unit->GetUnitKind();
			return true;
		}
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

NumericFormulaResult EvaluateApplicatorNumericFormula(const std::string& expression,
	const NWorld::PFBaseApplicator& app)
{
	const CPtr<NWorld::PFBaseUnit> owner = app.GetAbilityOwner();
	NWorld::PFWorld* world = IsValid(owner) ? owner->GetWorld() : nullptr;
	if (world != app.GetWorld()) world = nullptr;
	NRandom::RandomGenerator staged = world ? *world->GetRndGen() : NRandom::RandomGenerator();
	Context context{owner, app.GetReceiver(), app.GetAbilityData(), world ? &staged : nullptr, &app};
	const auto result = context.Evaluate(expression);
	if (result.Succeeded() && world) *world->GetRndGen() = staged;
	return result;
}

NumericFormulaResult EvaluatePositionNumericFormula(const std::string& expression,
	const ICustomFormulaPars& position, const NWorld::PFAbilityData& ability)
{
	Context context{ability.GetOwner(), nullptr, &ability};
	context.position = &position;
	return context.Evaluate(expression);
}

NumericFormulaResult EvaluateDamageReductionNumericFormula(const std::string& expression,
	const IUnitFormulaPars* sender, const IUnitFormulaPars* receiver,
	const NWorld::PFBaseApplicator* app)
{
	Context context{sender, receiver, app ? app->GetAbilityData() : nullptr, nullptr, app};
	return context.Evaluate(expression);
}
}
