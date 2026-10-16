#include "../System/systemStdAfx.h"
#include "../PF_GameLogic/StringExecutorBootstrap.h"
#include "formula_context.h"
#include "../PF_GameLogic/PFAbilityData.h"
#include "../PF_GameLogic/PFBaseUnit.h"
#include "../PF_GameLogic/PFHero.h"
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
 * ZZimaSex retains its integer getter and original player-versus-hero semantics.
 * Derived getters are explicitly listed; other economy, object and boolean
 * properties are not inferred. Combat accessors below have a separate whitelist.
 */
struct UnitNumericProperty
{
	const char* name;
	float (IUnitFormulaPars::*getter)() const;
	int (IUnitFormulaPars::*integerGetter)() const = nullptr;
};

constexpr UnitNumericProperty unitNumericProperties[] = {
	{"ZZimaSex", nullptr, &IUnitFormulaPars::GetZZimaSex},
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

/** Authored Ex masks are exact FormulaPars names joined by single '|', not general C++.
 * Bound the small structural translation; muParser still evaluates the interval expression.
 */
bool BindDamageMask(const std::string& source, unsigned& result)
{
	static const struct { const char* name; unsigned value; } masks[] = {
		{"DMG_UNIT", DMG_UNIT},
		{"DMG_APPLICATOR_ABILITY", DMG_APPLICATOR_ABILITY},
		{"DMG_ABILITY_ATTACK", DMG_ABILITY_ATTACK},
		{"DMG_ABILITY_SPECIAL", DMG_ABILITY_SPECIAL},
		{"DMG_ABILITY1", DMG_ABILITY1},
		{"DMG_ABILITY2", DMG_ABILITY2},
		{"DMG_ABILITY3", DMG_ABILITY3},
		{"DMG_ABILITY4", DMG_ABILITY4},
		{"DMG_ABILITY5", DMG_ABILITY5},
		{"DMG_TALENT", DMG_TALENT},
		{"DMG_TALENT_CLASS", DMG_TALENT_CLASS},
		{"DMG_TALENT_ORDINARY", DMG_TALENT_ORDINARY},
		{"DMG_TALENT_GOOD", DMG_TALENT_GOOD},
		{"DMG_TALENT_EXCELLENT", DMG_TALENT_EXCELLENT},
		{"DMG_TALENT_MAGNIFICENT", DMG_TALENT_MAGNIFICENT},
		{"DMG_TALENT_EXCLUSIVE", DMG_TALENT_EXCLUSIVE},
		{"DMG_TALENT_OUTSTANDING", DMG_TALENT_OUTSTANDING},
		{"DMG_TALENT_TIER1", DMG_TALENT_TIER1},
		{"DMG_TALENT_TIER2", DMG_TALENT_TIER2},
		{"DMG_TALENT_TIER3", DMG_TALENT_TIER3},
		{"DMG_TALENT_TIER4", DMG_TALENT_TIER4},
		{"DMG_TALENT_TIER5", DMG_TALENT_TIER5},
		{"DMG_TALENT_TIER6", DMG_TALENT_TIER6},
		{"DMG_APPL_ABLT", DMG_APPL_ABLT},
		{"DMG_ATTACK", DMG_ATTACK},
		{"DMG_SPECIAL", DMG_SPECIAL},
		{"DMG_ABLT1", DMG_ABLT1},
		{"DMG_ABLT2", DMG_ABLT2},
		{"DMG_ABLT3", DMG_ABLT3},
		{"DMG_ABLT4", DMG_ABLT4},
		{"DMG_ABLT5", DMG_ABLT5},
		{"DMG_TAL", DMG_TAL},
		{"DMG_CLASS", DMG_CLASS},
		{"DMG_ORDINARY", DMG_ORDINARY},
		{"DMG_GOOD", DMG_GOOD},
		{"DMG_EXCELLENT", DMG_EXCELLENT},
		{"DMG_MAGNIFICENT", DMG_MAGNIFICENT},
		{"DMG_EXCLUSIVE", DMG_EXCLUSIVE},
		{"DMG_OUTSTANDING", DMG_OUTSTANDING},
		{"DMG_TAL1", DMG_TAL1},
		{"DMG_TAL2", DMG_TAL2},
		{"DMG_TAL3", DMG_TAL3},
		{"DMG_TAL4", DMG_TAL4},
		{"DMG_TAL5", DMG_TAL5},
		{"DMG_TAL6", DMG_TAL6},
	};
	result = 0;
	std::size_t start = 0, terms = 0;
	for (;;)
	{
		if (++terms > NumericFormulaMaxSymbols) return false;
		const std::size_t end = source.find('|', start);
		const std::string part = source.substr(start, end == std::string::npos ? end : end - start);
		const auto first = part.find_first_not_of(" \t\r\n");
		if (first == std::string::npos) return false;
		const std::string name = part.substr(first, part.find_last_not_of(" \t\r\n") - first + 1);
		bool found = false;
		for (const auto& mask : masks)
			if (name == mask.name) { result |= mask.value; found = true; break; }
		if (!found) return false;
		if (end == std::string::npos) return true;
		start = end + 1;
	}
}

/** Split only one known method's argument boundaries, retaining nested numeric syntax verbatim. */
bool DamageArguments(const std::string& source, std::size_t& pos, std::vector<std::string>& args)
{
	std::size_t depth = 1, start = ++pos;
	for (; pos < source.size(); ++pos)
	{
		if (source[pos] == '(' && ++depth > NumericFormulaMaxNesting) return false;
		if (source[pos] == ')' && --depth == 0)
		{
			args.push_back(source.substr(start, pos - start)); ++pos;
			return args.size() == 2 || args.size() == 3;
		}
		if (source[pos] == ',' && depth == 1)
		{
			args.push_back(source.substr(start, pos - start)); start = pos + 1;
			if (args.size() > 2) return false;
		}
	}
	return false;
}

/** Lower closed scalar calls; scale and original target faction permit bounded parent traversal. */
bool BindMiscScalar(const std::string& source, const std::string& root, std::size_t& pos, std::string& result)
{
	std::size_t end = pos;
	unsigned depth = root == "mObjectParent" ? 1 : 0;
	std::string separator = root == "appl" ? "." : "->";
	for (;;)
	{
		const std::string parent = separator + "GetObjectParent()";
		if (source.compare(end, parent.size(), parent) != 0) break;
		if (++depth > 16) return false;
		end += parent.size(); separator = "->";
	}
	const std::string targetFaction = separator + "GetObject(\"Target\")->GetFctn()";
	if (source.compare(end, targetFaction.size(), targetFaction) == 0)
	{
		result += " __misc_target_faction_" + std::to_string(depth) + " ";
		pos = end + targetFaction.size(); return true;
	}
	const std::string scale = separator + "GetScale()";
	const std::string parentScale = separator + "GetParentScale()";
	if (source.compare(end, scale.size(), scale) == 0)
	{
		result += depth ? " __misc_parent_scale_" + std::to_string(depth) + " " : " mScale ";
		pos = end + scale.size(); return true;
	}
	if (depth == 0 && source.compare(end, parentScale.size(), parentScale) == 0)
	{
		result += " mParentScale "; pos = end + parentScale.size(); return true;
	}
	const std::string night = separator + "IsNight()";
	if (depth == 0 && source.compare(end, night.size(), night) == 0)
	{
		result += " mIsNight "; pos = end + night.size(); return true;
	}
	if (depth == 0) for (const char* property : {"Preparedness", "Cooldown"})
	{
		const std::string getter = separator + "Get" + property + "()";
		if (source.compare(end, getter.size(), getter) != 0) continue;
		result += std::string(" m") + property + " "; pos = end + getter.size(); return true;
	}
	return false;
}

/** An evaluation-local query descriptor, never a numeric encoding of an engine pointer. */
struct NamedFormulaQuery
{
	std::string name, variable;
	ApplicatorSearchType scope = GLOBAL;
	bool receiver = false, equal = false;
};

/** Lower a named macro or exact three-argument FindApplicator call with the same closed suffix. */
bool BindNamedQuery(const std::string& source, std::size_t start, std::size_t& pos,
	bool receiver, bool direct, std::map<std::string, NamedFormulaQuery>& queries, std::string& result)
{
	const auto nameChar = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || (c >= '0' && c <= '9'); };
	const auto space = [&]() { while (pos < source.size() && (source[pos] == ' ' || source[pos] == '\t' || source[pos] == '\r' || source[pos] == '\n')) ++pos; };
	NamedFormulaQuery query;
	query.receiver = receiver;
	if (direct)
	{
		space(); if (pos == source.size() || source[pos++] != '(') return false;
		space(); if (pos == source.size() || source[pos++] != '"') return false;
	}
	if (pos == source.size() || !nameChar(source[pos]) || (source[pos] >= '0' && source[pos] <= '9') ||
		(!direct && (source[pos] < 'A' || source[pos] > 'Z'))) return false;
	const std::size_t begin = pos;
	while (pos < source.size() && nameChar(source[pos])) ++pos;
	query.name = source.substr(begin, pos - begin);
	if (direct)
	{
		if (pos == source.size() || source[pos++] != '"') return false;
		space(); if (pos == source.size() || source[pos++] != ',') return false;
		space(); if (source.compare(pos, 5, "pMisc") != 0) return false;
		pos += 5; space(); if (pos == source.size() || source[pos++] != ',') return false;
		space();
	}
	else if (pos == source.size() || source[pos++] != '(') return false;
	const std::size_t scopeBegin = pos;
	while (pos < source.size() && nameChar(source[pos])) ++pos;
	const std::string scopeName = source.substr(scopeBegin, pos - scopeBegin);
	bool scopeFound = false;
	for (const auto& scope : {std::make_pair("GLOBAL", GLOBAL), std::make_pair("UNIT", UNIT),
		std::make_pair("ABILITY", ABILITY), std::make_pair("DISPATCH", DISPATCH), std::make_pair("UNIT_AND_SUMMONS", UNIT_AND_SUMMONS)})
	{
		if (scopeName != scope.first) continue;
		query.scope = scope.second; scopeFound = true; break;
	}
	if (!scopeFound) return false;
	if (direct) space();
	if (pos == source.size() || source[pos++] != ')') return false;
	space();
	const std::string getter = direct ? "->GetVariable" : ".GetVariable";
	if (source.compare(pos, getter.size(), getter) == 0)
	{
		pos += getter.size(); space();
		if (pos == source.size() || source[pos++] != '(') return false;
		space(); if (pos == source.size() || source[pos++] != '"') return false;
		const std::size_t variable = pos;
		if (pos == source.size() || !nameChar(source[pos]) || (source[pos] >= '0' && source[pos] <= '9')) return false;
		while (pos < source.size() && nameChar(source[pos])) ++pos;
		query.variable = source.substr(variable, pos - variable);
		// Other getters (notably statMod) can evaluate fresh formulas or consume RNG.
		// Admit only audited stored fields until those callbacks have a checked transaction.
		if (query.variable != "LifeTime" && query.variable != "Duration" && query.variable != "glyphNumber") return false;
		if (pos == source.size() || source[pos++] != '"') return false;
		space(); if (pos == source.size() || source[pos++] != ')') return false;
	}
	else
	{
		// Identity is a whole boolean operand. Consuming a prefix of pointer arithmetic
		// would silently change its precedence into arithmetic on a boolean transport.
		const std::size_t preceding = source.find_last_not_of(" \t\r\n", start ? start - 1 : 0);
		if (start && preceding != std::string::npos && source[preceding] != '(' && source[preceding] != ',' &&
			source[preceding] != '?' && source[preceding] != ':' &&
			!(preceding && (source.compare(preceding - 1, 2, "&&") == 0 || source.compare(preceding - 1, 2, "||") == 0))) return false;
		query.equal = source.compare(pos, 2, "==") == 0;
		if (!query.equal && source.compare(pos, 2, "!=") != 0) return false;
		pos += 2; space();
		if (source.compare(pos, 5, "pMisc") != 0) return false;
		pos += 5; space();
		if (pos < source.size() && source[pos] != ')' && source[pos] != ',' && source[pos] != '?' && source[pos] != ':' &&
			source.compare(pos, 2, "&&") != 0 && source.compare(pos, 2, "||") != 0) return false;
	}
	if (queries.size() >= NumericFormulaMaxSymbols) return false;
	const std::string symbol = "__named_" + std::to_string(queries.size());
	queries.emplace(symbol, query);
	result += " " + symbol + " ";
	return true;
}

/** Lower only the original target macro's read-only variable getter, not a C++ object API. */
bool BindTargetVariable(const std::string& source, std::size_t& pos, std::string& result)
{
	const std::string prefix = "$ApplTarget->GetVariable";
	if (source.compare(pos, prefix.size(), prefix) != 0) return false;
	pos += prefix.size();
	const auto space = [&]() { while (pos < source.size() && (source[pos] == ' ' || source[pos] == '\t' || source[pos] == '\r' || source[pos] == '\n')) ++pos; };
	const auto letter = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; };
	space(); if (pos == source.size() || source[pos++] != '(') return false;
	space(); if (pos == source.size() || source[pos++] != '"') return false;
	if (pos == source.size() || !letter(source[pos])) return false;
	const std::size_t begin = pos++;
	while (pos < source.size() && (letter(source[pos]) || (source[pos] >= '0' && source[pos] <= '9'))) ++pos;
	const std::string variable = source.substr(begin, pos - begin);
	if (pos == source.size() || source[pos++] != '"') return false;
	space(); if (pos == source.size() || source[pos++] != ')') return false;
	result += " __applvar_" + variable + " ";
	return true;
}

/** Lower a closed pair of original XY getters, leaving arithmetic and lazy evaluation to muParser. */
bool BindDistance(const std::string& source, std::size_t& pos, std::string& result)
{
	pos = source.find_first_not_of(" \t\r\n", pos);
	if (pos == std::string::npos || source[pos] != '(') return false;
	std::vector<std::string> args;
	if (!DamageArguments(source, pos, args) || args.size() != 2) return false;
	std::string symbol = " __distance_";
	for (const auto& argument : args)
	{
		const auto first = argument.find_first_not_of(" \t\r\n");
		if (first == std::string::npos) return false;
		const auto point = argument.substr(first, argument.find_last_not_of(" \t\r\n") - first + 1);
		if (point == "sPos") symbol += 's';
		else if (point == "rPos" || point == "tPos") symbol += 'r';
		else if (point == "pMisc->GetTargetPos()" || point == "appl.GetTargetPos()") symbol += 'm';
		else return false;
	}
	result += symbol + " ";
	return true;
}

/** Bind whitelisted unit variables, recent-history calls and exact applicator scalars.
 * Unit variables follow FormulaBuilder's v[A-Z][A-Za-z_0-9]* rule, not a C++ member API.
 * Reserved names cannot enter from source; muParser retains all arithmetic/grammar checks.
 */
bool BindCombatAccessors(const std::string& source, std::string& result, bool positionContext,
	std::map<std::string,std::string>& talents, std::map<std::string, NamedFormulaQuery>& queries, unsigned nesting = 0)
{
	if (source.size() > NumericFormulaMaxLength || nesting > NumericFormulaMaxNesting) return false;
	const auto nameChar = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || (c >= '0' && c <= '9'); };
	for (std::size_t pos = 0; pos < source.size();)
	{
		if (source[pos] == '$')
		{
			if (!BindTargetVariable(source, pos, result)) return false;
			continue;
		}
		if (!nameChar(source[pos])) { result += source[pos++]; continue; }
		const std::size_t start = pos;
		while (pos < source.size() && nameChar(source[pos])) ++pos;
		const std::string name = source.substr(start, pos - start);
		if (name.compare(0, 8, "__combat") == 0 || name.compare(0, 9, "__unitvar") == 0 ||
			name.compare(0, 10, "__position") == 0 || name.compare(0, 10, "__history_") == 0 ||
			name.compare(0, 9, "__talent_") == 0 || name.compare(0, 7, "__misc_") == 0 ||
			name.compare(0, 11, "__recommend") == 0 || name.compare(0, 8, "__named_") == 0 ||
			name.compare(0, 10, "__applvar_") == 0 || name.compare(0, 11, "__distance_") == 0) return false;
		if (name == "d")
		{
			if (!BindDistance(source, pos, result)) return false;
		}
		else if (name == "tal")
		{
			// Only one bounded ASCII literal is accepted; no C++ escapes, expressions or concatenation.
			const auto space = [&]() {
				while (pos < source.size() && (source[pos]==' ' || source[pos]=='\t' || source[pos]=='\r' || source[pos]=='\n')) ++pos;
			};
			space(); if (pos == source.size() || source[pos++] != '(') return false;
			space(); if (pos == source.size() || source[pos++] != '"') return false;
			const std::size_t begin = pos;
			while (pos < source.size() && source[pos] != '"')
			{
				if (source[pos] < 32 || source[pos] > 126 || source[pos] == '\\') return false;
				++pos;
			}
			if (pos == source.size()) return false;
			const std::string id = source.substr(begin,pos-begin); ++pos;
			space(); if (pos == source.size() || source[pos++] != ')') return false;
			auto existing = std::find_if(talents.begin(),talents.end(),[&](const auto& item) { return item.second == id; });
			if (existing == talents.end())
			{
				if (talents.size() >= NumericFormulaMaxSymbols) return false;
				existing = talents.emplace("__talent_"+std::to_string(talents.size()),id).first;
			}
			result += " " + existing->first + " ";
		}
		else if (name == "mObjectParent")
		{
			if (!BindMiscScalar(source, name, pos, result)) return false;
		}
		else if (name == "pMisc")
		{
			if (BindMiscScalar(source, name, pos, result)) continue;
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
			if (BindMiscScalar(source, name, pos, result)) continue;
			const std::string suffix = ".GetNatureTypeInPos(rPos2D)";
			if (!positionContext || source.compare(pos, suffix.size(), suffix) != 0) return false;
			result += " __position_nature "; pos += suffix.size();
		}
		else if (name == "sender" || name == "receiver" || name == "pFirst" || name == "pSecond")
		{
			const bool pointer = name == "pFirst" || name == "pSecond";
			const std::string find = std::string(pointer ? "->" : ".") + "FindApplicator";
			if (source.compare(pos, find.size(), find) == 0)
			{
				pos += find.size();
				if (!BindNamedQuery(source, start, pos, name == "receiver" || name == "pSecond", true, queries, result)) return false;
				continue;
			}
			const std::string named = std::string(pointer ? "->" : ".") + "appl";
			if (source.compare(pos, named.size(), named) == 0)
			{
				pos += named.size();
				if (!BindNamedQuery(source, start, pos, name == "receiver" || name == "pSecond", false, queries, result)) return false;
				continue;
			}
			const std::string recommendation = std::string(pointer ? "->" : ".") + "IsRecommended";
			if (source.compare(pos, recommendation.size(), recommendation) == 0)
			{
				std::size_t open = pos + recommendation.size();
				while (open < source.size() && (source[open] == ' ' || source[open] == '\t' || source[open] == '\r' || source[open] == '\n')) ++open;
				if (open == source.size() || source[open] != '(') return false;
				result += name == "sender" || name == "pFirst" ? " __recommend_s" : " __recommend_r";
				pos = open; continue;
			}
			bool history = false;
			for (const char* method : {"GetDamageTakenEx", "GetDamageDealtEx", "GetDamageTaken", "GetDamageDealt"})
			{
				const std::string suffix = std::string(pointer ? "->" : ".") + method;
				if (source.compare(pos, suffix.size(), suffix) != 0) continue;
				std::size_t open = pos + suffix.size();
				while (open < source.size() && (source[open] == ' ' || source[open] == '\t' || source[open] == '\r' || source[open] == '\n')) ++open;
				if (open == source.size() || source[open] != '(') return false;
				const std::string methodName(method);
				const std::string boundName = std::string(name == "sender" || name == "pFirst" ? " __history_s_" : " __history_r_") +
					(methodName.compare(0, 14, "GetDamageTaken") == 0 ? "taken" : "dealt");
				if (methodName == "GetDamageTakenEx" || methodName == "GetDamageDealtEx")
				{
					std::vector<std::string> args;
					if (!DamageArguments(source, open, args)) return false;
					unsigned mask;
					if (!BindDamageMask(args[1], mask)) return false;
					if (args.size() == 3)
					{
						const auto first = args[2].find_first_not_of(" \t\r\n");
						if (first == std::string::npos || args[2].substr(first,
							args[2].find_last_not_of(" \t\r\n") - first + 1) != "pMisc") return false;
					}
					std::string interval;
					if (!BindCombatAccessors(args[0], interval, positionContext, talents, queries, nesting + 1)) return false;
					result += boundName + (args.size() == 3 ? "_ex_misc(" : "_ex(") + interval + "," + std::to_string(mask) + ")";
					pos = open; history = true; break;
				}
				result += boundName;
				pos = open; history = true; break;
			}
			if (history) continue;
			// Exact zero-argument getters are alternate spellings of the existing numeric whitelist.
			bool property = false;
			for (const auto& binding : unitNumericProperties)
			{
				// FormulaBuilder maps sender.Life/receiver.Life and pointer aliases to GetLife().
				const std::string field = std::string(pointer ? "->" : ".") + binding.name;
				const std::size_t fieldEnd = pos + field.size();
				if (source.compare(pos, field.size(), field) == 0
					&& (fieldEnd == source.size() || !nameChar(source[fieldEnd])))
				{
					result += std::string(name == "sender" || name == "pFirst" ? " s" : " r") + binding.name + " ";
					pos = fieldEnd; property = true; break;
				}
				const std::string suffix = std::string(pointer ? "->Get" : ".Get") + binding.name;
				if (source.compare(pos, suffix.size(), suffix) != 0) continue;
				std::size_t end = pos + suffix.size();
				const auto space = [&]() {
					while (end < source.size() && (source[end] == ' ' || source[end] == '\t' || source[end] == '\r' || source[end] == '\n')) ++end;
				};
				space();
				if (end == source.size() || source[end] != '(') continue;
				++end; space();
				if (end == source.size() || source[end] != ')') return false;
				result += std::string(name == "sender" || name == "pFirst" ? " s" : " r") + binding.name + " ";
				pos = end + 1; property = true; break;
			}
			if (property) continue;
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

/** Accept only an authored return-expression terminator with an optional final line annotation.
 * Preserve quoted talent IDs. No statements, embedded comments, escapes or line continuations
 * are introduced; the existing binders and muParser still validate the complete expression.
 */
bool AuthoredExpression(const std::string& source, std::string& expression)
{
	if (source.size() > NumericFormulaMaxLength || source.find('\0') != std::string::npos) return false;
	bool quoted = false;
	for (std::size_t pos = 0; pos < source.size(); ++pos)
	{
		if (source[pos] == '\\') return false;
		if (source[pos] == '"') quoted = !quoted;
		if (quoted || source[pos] != ';') continue;
		std::size_t tail = source.find_first_not_of(" \t\r\n", pos + 1);
		if (tail != std::string::npos)
		{
			if (source.compare(tail, 2, "//") != 0 || source.find('\\', tail) != std::string::npos) return false;
			const auto end = source.find_first_of("\r\n", tail + 2);
			if (end != std::string::npos && source.find_first_not_of(" \t\r\n", end) != std::string::npos) return false;
		}
		expression = source.substr(0, pos);
		return true;
	}
	expression = source;
	return !quoted;
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
		std::string authored, bound;
		std::map<std::string,std::string> talents;
		std::map<std::string, NamedFormulaQuery> queries;
		if (!AuthoredExpression(expression, authored)
			|| !BindCombatAccessors(authored, bound, position != nullptr, talents, queries)) return {NumericFormulaError::UnsupportedSyntax};
		return EvaluateNumericFormula(bound, [&](const std::string& name, double& value) {
			const auto query = queries.find(name);
			if (query != queries.end()) return ReadNamed(query->second, value);
			const auto found = talents.find(name);
			if (found != talents.end())
			{
				// Nonhero base implementations assert; missing hero talents instead mean false.
				const auto* hero = dynamic_cast<const NWorld::PFBaseHero*>(sender);
				if (!hero) return false;
				const auto* talent = hero->GetTalent(found->second.c_str());
				value = talent && talent->IsTalentBought() ? 1 : 0;
				return true;
			}
			return Resolve(name, value);
		}, [&](const std::string& name, const double* args, std::size_t count, double& value) {
			return Scale(name, args, count, value);
		});
	}

	/** Use original scope/order and missing-match fallback; keep objects out of the scalar parser. */
	bool ReadNamed(const NamedFormulaQuery& query, double& value) const
	{
		const auto* unit = dynamic_cast<const NWorld::PFBaseUnit*>(query.receiver ? target : sender);
		const IMiscFormulaPars* misc = applicator ? static_cast<const IMiscFormulaPars*>(applicator) : ability;
		if (!unit || !misc) return false;
		const auto* found = unit->FindApplicator(query.name.c_str(), misc, query.scope);
		if (!found) return false;
		if (query.variable.empty())
		{
			value = query.equal ? found == misc : found != misc;
			return true;
		}
		const CObj<NWorld::PFBaseApplicator> held = const_cast<NWorld::PFBaseApplicator*>(dynamic_cast<const NWorld::PFBaseApplicator*>(found));
		value = found->GetVariable(query.variable.c_str());
		return std::isfinite(value);
	}

	/** Validate before the existing helper can mask invalid arithmetic or narrow to int. */
	bool Scale(const std::string& name, const double* args, std::size_t count, double& value)
	{
		if (name == "s")
		{
			if (count != 3 || !std::isfinite(args[0])) return false;
			for (std::size_t i = 1; i < count; ++i)
				if (!std::isfinite(args[i]) || std::fabs(args[i]) > std::numeric_limits<float>::max()) return false;
			value = SwitchByBool(args[0] != 0, static_cast<float>(args[1]), static_cast<float>(args[2]));
			return true;
		}
		if (name == "sFlag" || name == "rFlag")
		{
			const auto* unit = name == "sFlag" ? sender : target;
			if (!unit || count != 1 || !std::isfinite(args[0]) || args[0] < 0
				|| args[0] > std::numeric_limits<unsigned>::max()) return false;
			value = unit->GetFlag(static_cast<unsigned>(args[0]));
			return true;
		}
		if (name == "__recommend_s" || name == "__recommend_r")
		{
			const auto* unit = name == "__recommend_s" ? sender : target;
			if (!unit || count != 1 || !std::isfinite(args[0]) || args[0] < std::numeric_limits<int>::min()
				|| args[0] > std::numeric_limits<int>::max()) return false;
			value = unit->IsRecommended(static_cast<int>(args[0])) ? 1 : 0;
			return true;
		}
		if (name == "refineScale")
		{
			if (!ability || count != 2 || ability->GetRefineRate() == std::numeric_limits<int>::min()) return false;
			for (std::size_t i = 0; i < count; ++i)
				if (!std::isfinite(args[i]) || std::fabs(args[i]) > std::numeric_limits<float>::max()) return false;
			// Applicator ability lookup already preserves original upgrader precedence.
			value = ability->GetRefineAbilityScale(static_cast<float>(args[0]), static_cast<float>(args[1]));
			return std::isfinite(value);
		}
		if (name == "__history_s_taken_ex" || name == "__history_s_dealt_ex" ||
			name == "__history_r_taken_ex" || name == "__history_r_dealt_ex" ||
			name == "__history_s_taken_ex_misc" || name == "__history_s_dealt_ex_misc" ||
			name == "__history_r_taken_ex_misc" || name == "__history_r_dealt_ex_misc")
		{
			if (count != 2 || !std::isfinite(args[0]) || std::fabs(args[0]) > std::numeric_limits<float>::max() ||
				!std::isfinite(args[1]) || args[1] < 0 || args[1] > std::numeric_limits<unsigned>::max() || std::floor(args[1]) != args[1]) return false;
			const auto* unit = dynamic_cast<const NWorld::PFBaseUnit*>(name.compare(0, 12, "__history_s_") == 0 ? sender : target);
			if (!unit) return false;
			float result;
			const auto* context = name.find("_misc") != std::string::npos ? applicator : nullptr;
			// Original TakenEx and DealtEx both read sent history with the same classification.
			if (!unit->TryGetDamageDealtEx(static_cast<float>(args[0]), static_cast<unsigned>(args[1]), context, result)) return false;
			value = result; return true;
		}
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

	/** Hold and validate real ancestry before original recursive getters or read-only target access. */
	const IMiscFormulaPars* MiscAncestor(unsigned depth, std::vector<CObj<NWorld::PFBaseApplicator>>& owners)
	{
		const IMiscFormulaPars* current = applicator ? static_cast<const IMiscFormulaPars*>(applicator)
			: static_cast<const IMiscFormulaPars*>(ability);
		if (!current) return nullptr;
		for (const auto* ancestor = applicator; ancestor;)
		{
			if (owners.size() >= 128 || std::find_if(owners.begin(), owners.end(), [&](const auto& held) {
				return held.GetPtr() == ancestor;
			}) != owners.end()) return nullptr;
			// Reference ownership only; the formula never mutates an applicator.
			owners.emplace_back(const_cast<NWorld::PFBaseApplicator*>(ancestor));
			const auto& parent = ancestor->GetParentAppl();
			ancestor = IsValid(parent) ? parent.GetPtr() : nullptr;
		}
		for (unsigned i = 0; i < depth; ++i)
		{
			current = current->GetObjectParent();
			if (!current) return nullptr;
		}
		return current;
	}

	bool MiscScale(unsigned depth, bool parentScale, double& value)
	{
		std::vector<CObj<NWorld::PFBaseApplicator>> owners;
		const auto* current = MiscAncestor(depth, owners);
		if (!current) return false;
		value = parentScale ? current->GetParentScale() : current->GetScale();
		return std::isfinite(value);
	}

	/** Query the original raw unit target, including corpses and retained stopped parents, never a receiver fallback. */
	bool MiscTargetFaction(unsigned depth, double& value)
	{
		if (!applicator) return false;
		std::vector<CObj<NWorld::PFBaseApplicator>> owners;
		const auto* current = MiscAncestor(depth, owners);
		CObj<NWorld::PFWorld> world(const_cast<NWorld::PFWorld*>(applicator->GetWorld()));
		if (!current || !world) return false;
		for (const auto& owner : owners) if (owner->GetWorld() != world.GetPtr()) return false;
		const auto* target = dynamic_cast<const NWorld::PFBaseUnit*>(current->GetObject("Target"));
		if (!target) return false;
		CObj<NWorld::PFBaseUnit> held(const_cast<NWorld::PFBaseUnit*>(target));
		if (held->IsObjectDead() || held->GetWorld() != world.GetPtr()
			|| world->FindLinuxUnitByObjectId(held->GetObjectId()) != held.GetPtr()) return false;
		const int faction = held->GetFctn();
		if (faction < 0 || faction >= NDb::KnownEnum<NDb::EFaction>::SizeOf()) return false;
		value = faction; return true;
	}

	bool Resolve(const std::string& name, double& value)
	{
		if (name.size() == 13 && name.compare(0, 11, "__distance_") == 0)
		{
			CVec2 points[2];
			for (unsigned i = 0; i < 2; ++i)
			{
				if (name[11 + i] == 'm')
				{
					// AbilityData has no target position and its original getter asserts.
					if (!applicator) return false;
					const auto& raw = applicator->GetTarget();
					if (!raw.IsPosition() && (!raw.IsObject() || !IsValid(raw.GetObject()))) return false;
					points[i] = applicator->GetTargetPos();
				}
				else
				{
					const auto* unit = name[11 + i] == 's' ? sender : target;
					if (!unit) return false;
					points[i] = unit->GetPos();
				}
				if (!std::isfinite(points[i].x) || !std::isfinite(points[i].y)) return false;
			}
			// Preserve FormulaPars float arithmetic, including rejection of overflowing intermediates.
			const CVec2 delta = points[0] - points[1];
			if (!std::isfinite(delta.x) || !std::isfinite(delta.y)
				|| !std::isfinite(delta.x * delta.x + delta.y * delta.y)) return false;
			value = d(points[0], points[1]);
			return std::isfinite(value);
		}
		if (name.compare(0, 10, "__applvar_") == 0)
		{
			if (!applicator) return false;
			const auto* unit = dynamic_cast<const NWorld::PFBaseUnit*>(applicator->GetObject("Target"));
			if (!unit) return false;
			value = unit->GetVariable(name.c_str() + 10);
			return std::isfinite(value);
		}
		if (name == "mPreparedness" || name == "mCooldown")
		{
			if (!ability || (ability->GetState() != EAbilityState::First && ability->GetState() != EAbilityState::Second)) return false;
			// The original readiness getter's comparisons can conceal NaN as fully ready.
			if (!std::isfinite(ability->GetCooldown()) || !std::isfinite(ability->GetCurrentCooldown())) return false;
			value = name == "mCooldown" ? ability->GetCooldown() : ability->GetPreparedness();
			return std::isfinite(value);
		}
		if (name == "mRefineRate")
		{
			if (!ability) return false;
			value = ability->GetRefineRate(); return true;
		}
		if (name == "mIsNight")
		{
			// Logical night is the original miscellaneous context's state, not its lighting blend or receiver's world.
			const IMiscFormulaPars* misc = applicator ? static_cast<const IMiscFormulaPars*>(applicator) : ability;
			if (!misc) return false;
			value = misc->IsNight() ? 1 : 0; return true;
		}
		if (name == "mScale" || name == "mParentScale") return MiscScale(0, name == "mParentScale", value);
		if (name.compare(0, 22, "__misc_target_faction_") == 0)
		{
			for (unsigned depth = 0; depth <= 16; ++depth)
				if (name == "__misc_target_faction_" + std::to_string(depth)) return MiscTargetFaction(depth, value);
			return false;
		}
		if (name.compare(0, 20, "__misc_parent_scale_") == 0)
		{
			for (unsigned depth = 1; depth <= 16; ++depth)
				if (name == "__misc_parent_scale_" + std::to_string(depth)) return MiscScale(depth, false, value);
			return false;
		}
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
		if (name == "ZZSEX_UNDEFINED") { value = ZZSEX_UNDEFINED; return true; }
		if (name == "ZZSEX_MALE") { value = ZZSEX_MALE; return true; }
		if (name == "ZZSEX_FEMALE") { value = ZZSEX_FEMALE; return true; }
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
				value = binding.integerGetter ? (unit->*binding.integerGetter)() : (unit->*binding.getter)();
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

NumericFormulaResult EvaluateSelectorConditionFormula(const std::string& expression,
	const IUnitFormulaPars* sender, const IUnitFormulaPars* candidate, const IMiscFormulaPars* miscellaneous)
{
	const auto* app = dynamic_cast<const NWorld::PFBaseApplicator*>(miscellaneous);
	const auto* ability = app ? app->GetAbilityData() : dynamic_cast<const NWorld::PFAbilityData*>(miscellaneous);
	const CPtr<NWorld::PFBaseUnit> owner = app ? app->GetAbilityOwner() : ability ? ability->GetOwner() : nullptr;
	NWorld::PFWorld* world = IsValid(owner) ? owner->GetWorld() : nullptr;
	if (app && app->GetWorld() != world) world = nullptr;
	NRandom::RandomGenerator staged = world ? *world->GetRndGen() : NRandom::RandomGenerator();
	Context context{sender, candidate, ability, world ? &staged : nullptr, app};
	const auto result = context.Evaluate(expression);
	if (result.Succeeded() && world) *world->GetRndGen() = staged;
	return result;
}

NumericFormulaResult EvaluateDamageReductionNumericFormula(const std::string& expression,
	const IUnitFormulaPars* sender, const IUnitFormulaPars* receiver,
	const NWorld::PFBaseApplicator* app)
{
	Context context{sender, receiver, app ? app->GetAbilityData() : nullptr, nullptr, app};
	return context.Evaluate(expression);
}
}
