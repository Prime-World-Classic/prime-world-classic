#include "../System/systemStdAfx.h"
#include "formula_range_probe.h"
#include "formula_context.h"
#include "../PF_GameLogic/StringExecutorBootstrap.h"
#include "../PF_GameLogic/PFAbilityData.h"
#include "../PF_GameLogic/PFBaseUnit.h"
#include "../PF_GameLogic/PFUniTarget.h"
#include "../PF_GameLogic/DBTalent.h"
#include "../PF_GameLogic/DBAdvMap.h"
#include "../PF_GameLogic/DBSessionRoots.h"
#include "../PF_GameLogic/PFWorld.h"
#include "../PF_GameLogic/PFResourcesCollectionClient.h"
#include "../Terrain/DBTerrain.h"
#include "../System/FileSystem/WinFileSystem.h"
#include "../libdb/DbResourceCache.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <limits>
#include <vector>

namespace
{
/** Existing virtual engine getters separate base values from modified live stats. */
class RangeUnit : public NWorld::PFBaseUnit
{
public:
	RangeUnit(NWorld::PFWorld* world = nullptr) : PFBaseUnit(world, CVec3(0, 0, 0), nullptr) {}
	float strength = 55, baseIntellect = 54, intellect = 54, range = 14;
	float GetBaseStrength() const override { return strength; }
	float GetBaseIntellect() const override { return baseIntellect; }
	float GetIntellect() const override { return intellect; }
	float GetRange() const override { return range; }
	float costModifier = 1;
	float GetManaCostModifier(bool = false) const override { return costModifier; }
	/** Set deterministic pools without production regeneration or stat loading. */
	void SetResources(float life, float mana)
	{
		maxHealth = maxEnergy = 1000;
		health = life; energy = mana;
	}
};

/** Explicit mock values are independent of the resolver's name-to-getter table. */
struct NumericPropertyStat
{
	const char* liveName;
	const char* baseName;
	NDb::EStat stat;
	int senderBase;
	int targetBase;
};

const NumericPropertyStat numericPropertyStats[] = {
	{"MaxLife", "BaseLife", NDb::STAT_LIFE, 101, 1001},
	{"MaxEnergy", "BaseEnergy", NDb::STAT_ENERGY, 103, 1003},
	{"Range", "BaseRange", NDb::STAT_RANGE, 107, 1007},
	{"MoveSpeed", "BaseMoveSpeed", NDb::STAT_MOVESPEED, 109, 1009},
	{"AttackSpeed", "BaseAttackSpeed", NDb::STAT_ATTACKSPEED, 113, 1013},
	{"CritMult", "BaseCriticalMultiplier", NDb::STAT_CRITICALMULTIPLIER, 127, 1027},
	{"LifeDrain", "BaseLifeDrain", NDb::STAT_LIFEDRAIN, 131, 1031},
	{"EnergyDrain", "BaseEnergyDrain", NDb::STAT_ENERGYDRAIN, 137, 1037},
	{"Evasion", "BaseEvasion", NDb::STAT_EVASION, 139, 1039},
	{"LifeRegen", "BaseLifeRegeneration", NDb::STAT_LIFEREGENERATION, 149, 1049},
	{"LifeRegenAbs", "BaseLifeRegenerationAbsolute", NDb::STAT_LIFEREGENERATIONABSOLUTE, 151, 1051},
	{"EnergyRegen", "BaseEnergyRegeneration", NDb::STAT_ENERGYREGENERATION, 157, 1057},
	{"EnergyRegenAbs", "BaseEnergyRegenerationAbsolute", NDb::STAT_ENERGYREGENERATIONABSOLUTE, 163, 1063},
	{"Strength", "BaseStrength", NDb::STAT_STRENGTH, 167, 1067},
	{"Intellect", "BaseIntellect", NDb::STAT_INTELLECT, 173, 1073},
	{"Dexterity", "BaseDexterity", NDb::STAT_DEXTERITY, 179, 1079},
	{"BaseAttack", "BaseBaseAttack", NDb::STAT_BASEATTACK, 181, 1081},
	{"Stamina", "BaseStamina", NDb::STAT_STAMINA, 191, 1091},
	{"Will", "BaseWill", NDb::STAT_WILL, 193, 1093}
};

/** Load literal mock DB stats through the actual engine initializer, without getter overrides. */
class NumericPropertyUnit : public NWorld::PFBaseUnit
{
public:
	explicit NumericPropertyUnit(const NDb::Unit* db) : PFBaseUnit(nullptr, CVec3(0, 0, 0), db)
	{
		InitData data;
		data.faction = NDb::FACTION_FREEZE;
		data.type = NDb::UNITTYPE_BUILDING;
		data.playerId = -1;
		data.pObjectDesc = db;
		Initialize(data);
	}
};

/** Give each side a separate, owned DB; no world, asset or ability context is required. */
NDb::Ptr<NDb::Unit> MakeNumericPropertyUnitDb(bool target)
{
	NDb::StatsContainer* stats = new NDb::StatsContainer;
	for (const auto& entry : numericPropertyStats)
	{
		NDb::UnitStat stat;
		stat.statId = entry.stat;
		stat.value.sString = std::to_string(target ? entry.targetBase : entry.senderBase).c_str();
		stat.increment.sString = "0";
		stats->stats.push_back(stat);
	}
	NDb::Unit* unit = new NDb::Unit;
	unit->stats = stats;
	return unit;
}

/** Exercise real engine stats and modifiers, not another implementation of the getter mapping. */
template<class Check>
void CheckNumericUnitProperties(const Check& check)
{
	using LinuxBootstrap::EvaluateUnitNumericFormula;
	using LinuxBootstrap::NumericFormulaError;
	const NDb::Ptr<NDb::Unit> senderDb = MakeNumericPropertyUnitDb(false);
	const NDb::Ptr<NDb::Unit> targetDb = MakeNumericPropertyUnitDb(true);
	CObj<NumericPropertyUnit> sender = new NumericPropertyUnit(senderDb);
	CObj<NumericPropertyUnit> target = new NumericPropertyUnit(targetDb);
	std::vector<int> targetModifiers;
	for (const auto& entry : numericPropertyStats)
	{
		sender->GetStat(entry.stat)->AddModifier(1.0f, 100.0f, 731);
		targetModifiers.push_back(target->GetStat(entry.stat)->AddModifier(1.0f, 200.0f, 732));
	}
	sender->InitializeLifeEnergy();
	target->InitializeLifeEnergy();
	sender->SetHealth(31); sender->SetEnergy(37);
	target->SetHealth(41); target->SetEnergy(43);

	const auto expect = [&](const std::string& expression, float expected,
		const IUnitFormulaPars* first, const IUnitFormulaPars* second) {
		const auto result = EvaluateUnitNumericFormula(expression, first, second);
		check(result.Succeeded() && result.value == expected, ("numeric property: " + expression).c_str());
	};
	const auto missing = [&](const std::string& expression,
		const IUnitFormulaPars* first, const IUnitFormulaPars* second) {
		const auto result = EvaluateUnitNumericFormula(expression, first, second);
		check(result.error == NumericFormulaError::UnknownSymbol && std::isnan(result.value),
			("numeric property missing/unknown: " + expression).c_str());
	};
	for (const auto& entry : numericPropertyStats)
	{
		const std::string liveSender = std::string("s") + entry.liveName;
		const std::string liveTarget = std::string("t") + entry.liveName;
		const std::string baseSender = std::string("s") + entry.baseName;
		const std::string baseTarget = std::string("t") + entry.baseName;
		expect(liveSender, entry.senderBase + 100, sender, target);
		expect(liveTarget, entry.targetBase + 200, sender, target);
		expect(baseSender, entry.senderBase, sender, target);
		expect(baseTarget, entry.targetBase, sender, target);
		expect(liveSender, entry.senderBase + 100, sender, nullptr);
		expect(liveTarget, entry.targetBase + 200, nullptr, target);
		missing(liveSender, nullptr, target);
		missing(liveTarget, sender, nullptr);
		missing(baseSender, nullptr, target);
		missing(baseTarget, sender, nullptr);
	}
	expect("sLife", 31, sender, target); expect("tLife", 41, sender, target);
	expect("sEnergy", 37, sender, target); expect("tEnergy", 43, sender, target);
	missing("sLife", nullptr, target); missing("tLife", sender, nullptr);
	missing("sEnergy", nullptr, target); missing("tEnergy", sender, nullptr);
	expect("sMaxLife-sLife", 170, sender, target);
	expect("tMaxEnergy-tEnergy", 1160, sender, target);
	expect("sStrength-tBaseStrength", -800, sender, target);
	expect("sBaseAttack-sBaseBaseAttack", 100, sender, target);

	for (const char* name : {"sHealth", "sMana", "sMaxHealth", "tMaxMana", "sCriticalMultiplier",
		"tBaseCritMult", "sLifeRegeneration", "sBaseLifeRegen", "sCoreLife", "sAttack", "tSpeed",
		"sstrength", "sSTRENGTH", "rStrength", "sStrengthExtra", "tLifeDrainExtra", "s",
		"sLifeRegenTotal", "sBaseVisibilityRange", "sBaseCriticalChance", "sCritChance",
		"sNafta", "sObjectTarget", "sIsHero", "mRank"})
		missing(name, sender, target);
	expect("1 ? sStrength : tStrength", 267, sender, nullptr);
	expect("0 ? sLife : tEnergy", 43, nullptr, target);
	expect("1 ? sBaseBaseAttack : sMana", 181, sender, nullptr);
	expect("0 ? tUnknown : 23", 23, nullptr, nullptr);
	missing("0 ? sStrength : tStrength", sender, nullptr);
	missing("1 ? sMana : sStrength", sender, target);

	NDb::Ability* raw = new NDb::Ability;
	raw->useRange.sString = "sBaseAttack+tBaseBaseAttack";
	const NDb::Ptr<NDb::Ability> abilityDb = raw;
	CObj<NWorld::PFAbilityData> ability = new NWorld::PFAbilityData(sender.GetPtr(), abilityDb,
		NDb::ABILITYTYPEID_SPECIAL, false, false);
	check(ability->GetUseRange(target.GetPtr()) == 1362, "numeric property: real ability use-range consumes new bindings");

	std::size_t index = 0;
	for (const auto& entry : numericPropertyStats)
	{
		sender->GetStat(entry.stat)->SetCoreValue(entry.senderBase + 7);
		target->GetStat(entry.stat)->UpdateModifierAdd(targetModifiers[index++], 209.0f);
	}
	sender->InitializeLifeEnergy();
	target->InitializeLifeEnergy();
	sender->SetHealth(47); sender->SetEnergy(53);
	target->SetHealth(59); target->SetEnergy(61);
	for (const auto& entry : numericPropertyStats)
	{
		expect(std::string("s") + entry.liveName, entry.senderBase + 107, sender, target);
		expect(std::string("t") + entry.liveName, entry.targetBase + 209, sender, target);
		expect(std::string("s") + entry.baseName, entry.senderBase + 7, sender, target);
		expect(std::string("t") + entry.baseName, entry.targetBase, sender, target);
	}
	expect("sLife", 47, sender, target); expect("tLife", 59, sender, target);
	expect("sEnergy", 53, sender, target); expect("tEnergy", 61, sender, target);
	check(ability->GetUseRange(target.GetPtr()) == 1369, "numeric property: ability re-evaluates changed base and modified stats");
}
}

bool RunPrimeWorldLinuxFormulaRangeProbe(const char* dataRoot)
{
	if (!dataRoot || !*dataRoot) return false;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false);
	RootFileSystem::RegisterFileSystem(files);
	auto* cache = NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor());
	NDb::SetResourceCache(cache);
	int checks = 0, failures = 0;
	auto check = [&](bool ok, const char* name) {
		++checks;
		if (!ok) { ++failures; std::printf("Formula range FAIL: %s\n", name); }
	};
	CheckNumericUnitProperties(check);
	{
		NDb::Ptr<NDb::Talent> talent = NDb::Get<NDb::Talent>(NDb::DBID("/Items/Talents/Class/Plane/Ability_A1.TALENT.xdb"));
		check(talent && talent->useRange.sString.find("sBaseStrength") != string::npos, "shipped native Plane A1 expression loaded");
		if (talent)
		{
			CObj<RangeUnit> unit = new RangeUnit;
			NDb::Ptr<NDb::Ability> db = talent.GetPtr();
			CObj<NWorld::PFAbilityData> ability = new NWorld::PFAbilityData(unit.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL, false, false);
			check(ability->GetUseRange() == 14, "strength branch uses live range, not zero fallback");
			check(ability->CalcParam("A1_ManaCost", unit.GetPtr(), unit.GetPtr(), nullptr) == 70, "shipped local constant");
			check(ability->GetManaCost() == 70, "shipped talent costs seventy mana");
			unit->SetResources(100, 69);
			check(!ability->IsEnoughMana(), "insufficient authored mana rejected");
			unit->SetResources(100, 70);
			check(ability->IsEnoughMana(), "exact mana cost accepted");
			ability->SpendMana();
			check(unit->GetMana() == 0 && unit->GetLife() == 100, "actual authored mana deducted");
			check(unit->IsTargetInRange(NWorld::Target(CVec3(14, 0, 0)), ability->GetUseRange()), "exact range boundary accepted");
			check(!unit->IsTargetInRange(NWorld::Target(CVec3(14.01f, 0, 0)), ability->GetUseRange()), "outside range rejected");
			unit->baseIntellect = 55; unit->intellect = 160;
			check(ability->GetUseRange() == 14, "equal base stats choose strength branch");
			unit->strength = 54; unit->intellect = 80;
			check(ability->GetUseRange() == 20, "intellect branch arithmetic");
			unit->intellect = 40;
			check(ability->GetUseRange() == 19.5f, "live fractional stat change");
			unit->range = 10;
			check(ability->GetUseRange() == 15.5f, "range is not cached across changes");
		}
		CObj<RangeUnit> unit = new RangeUnit;
		for (const char* expression : {"sRange/2", "0", "", "-1", "cMissing", "1/0"})
		{
			NDb::Ability* raw = new NDb::Ability; raw->cooldownTime.sString = expression;
			NDb::Ptr<NDb::Ability> db = raw;
			CObj<NWorld::PFAbilityData> ability = new NWorld::PFAbilityData(unit.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL, false, false);
			const bool valid = expression[0] == 's' || expression[0] == '0' || !*expression;
			const float expected = expression[0] == 's' ? 7.f : 0.f;
			check(valid ? ability->GetCooldown() == expected : std::isnan(ability->GetCooldown()), "checked cooldown duration");
			check(ability->IsReady() == valid, "invalid duration cannot be initially ready");
			ability->RestartCooldown();
			check(ability->IsReady() == (valid && expected == 0), "restart honors valid duration");
			if (!valid)
			{
				ability->Update(10, false); ability->DropCooldown(true, 0, false);
				check(!ability->IsReady(), "update and reset cannot bypass invalid duration");
			}
		}
		{
			NDb::Ability* raw = new NDb::Ability;
			raw->cooldownTime.sString = "sRange/2"; raw->cooldownTimeSecondState.sString = "cMissing";
			NDb::Ptr<NDb::Ability> db = raw;
			CObj<NWorld::PFAbilityData> ability = new NWorld::PFAbilityData(unit.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL, false, false);
			ability->SetState(EAbilityState::Second);
			check(!ability->IsReady(), "second-state invalid formula isolated");
			ability->SetState(EAbilityState::First);
			check(ability->IsReady(), "first state still usable");
			ability->RestartCooldown(); ability->Update(2.5f, false);
			check(ability->GetCurrentCooldown() == 4.5f, "cooldown advances by elapsed time");
			ability->Update(4.5f, false); check(ability->IsReady(), "cooldown expiry");
			unit->range = 16; ability->RecalculateAndRestartCooldown();
			check(ability->GetCurrentCooldown() == 8, "restart recalculates live duration");
			ability->DropCooldown(false, .5f, true); ability->DropCooldown(false, 1, false);
			check(ability->GetCurrentCooldown() == 3, "percentage then absolute reduction");
			for (float invalid : {-1.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
			{
				ability->Update(invalid, false); ability->DropCooldown(false, invalid, false);
				check(ability->GetCurrentCooldown() == 3, "invalid time and reduction are inert");
			}
			ability->RestartCooldown(std::numeric_limits<float>::quiet_NaN());
			check(!ability->IsReady(), "invalid explicit restart fails closed");
			ability->RecalculateAndRestartCooldown();
			check(ability->GetCurrentCooldown() == 8, "valid recalculation recovers invalid restart");
			unit->range = 14;
		}
		for (const char* expression : {"sRange*2", "-1", "cMissing", "1/0"})
		{
			NDb::Ability* raw = new NDb::Ability; raw->manaCost.sString = expression;
			NDb::Ptr<NDb::Ability> db = raw;
			unit->SetResources(100, 100);
			CObj<NWorld::PFAbilityData> ability = new NWorld::PFAbilityData(unit.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL, false, false);
			const bool valid = expression[0] == 's';
			check(valid ? ability->GetManaCost() == 28 : std::isnan(ability->GetManaCost()), "checked cost expression");
			check(ability->IsEnoughMana() == valid, "invalid costs are unaffordable");
			ability->SpendMana();
			check(unit->GetMana() == (valid ? 72 : 100), "invalid costs cannot alter mana");
			if (valid)
			{
				unit->range = 16; ability->Update(0, true);
				check(ability->GetManaCost() == 32, "cost refresh follows live stats");
				unit->SetResources(100, 31); ability->SpendMana();
				check(unit->GetMana() == 31, "insufficient mana is not partially spent");
				unit->range = 14;
			}
		}
		for (float modifier : {-1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::max()})
		{
			NDb::Ability* raw = new NDb::Ability; raw->manaCost.sString = "20";
			NDb::Ptr<NDb::Ability> db = raw;
			unit->SetResources(100, 100); unit->costModifier = modifier;
			CObj<NWorld::PFAbilityData> ability = new NWorld::PFAbilityData(unit.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL, false, false);
			check(std::isnan(ability->GetManaCost()) && !ability->IsEnoughMana(), "invalid or overflowing modifier fails closed");
			ability->SpendMana(); check(unit->GetMana() == 100, "invalid modifier cannot mutate resource");
		}
		unit->costModifier = 1;
		for (const char* expression : {"0", ""})
		{
			NDb::Ability* raw = new NDb::Ability; raw->manaCost.sString = expression;
			NDb::Ptr<NDb::Ability> db = raw;
			unit->SetResources(100, 0);
			CObj<NWorld::PFAbilityData> ability = new NWorld::PFAbilityData(unit.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL, false, false);
			check(ability->GetManaCost() == 0 && ability->IsEnoughMana(), "authored free ability accepted");
			unit->SetResources(100, std::numeric_limits<float>::infinity());
			check(!ability->IsEnoughMana(), "nonfinite resource pool rejected");
		}
		{
			NDb::Ability* raw = new NDb::Ability; raw->manaCost.compiledString = "unavailable-bytecode";
			NDb::Ptr<NDb::Ability> db = raw;
			CObj<NWorld::PFAbilityData> ability = new NWorld::PFAbilityData(unit.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL, false, false);
			check(std::isnan(ability->GetManaCost()), "compiled-only cost is not an absent default");
		}
		{
			NDb::Ability* raw = new NDb::Ability; raw->manaCost.sString = "sRange+6";
			raw->flags = NDb::ABILITYFLAGS_SPENDLIFEINSTEADENERGY;
			NDb::Ptr<NDb::Ability> db = raw;
			unit->SetResources(100, 100); unit->costModifier = 2;
			CObj<NWorld::PFAbilityData> ability = new NWorld::PFAbilityData(unit.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL, false, false);
			check(ability->GetManaCost() == 40 && ability->IsEnoughMana(), "life cost applies owner modifier");
			ability->SpendMana();
			check(unit->GetLife() == 60 && unit->GetMana() == 100, "life cost preserves mana");
			unit->SetResources(39, 100);
			check(!ability->IsEnoughMana(), "life affordability uses health");
			unit->costModifier = 1;
		}
		{
			NDb::Ability* raw = new NDb::Ability;
			NDb::UnitConstantsContainer* constants = new NDb::UnitConstantsContainer;
			raw->constants = constants;
			const auto add = [&](const char* name, const char* expression) {
				NDb::UnitConstant* constant = new NDb::UnitConstant;
				constant->name = name; constant->var.sString = expression;
				constants->vars.push_back(constant);
			};
			add("Leaf", "sRange+1"); add("Root", "cLeaf*2");
			add("Cycle", "cOther"); add("Other", "cCycle");
			add("Lazy", "1 ? cRoot : cCycle"); add("Bad", "cAbsent+1");
			for (int i = 0; i < 17; ++i)
				add((std::string("Depth") + std::to_string(i)).c_str(),
					i == 16 ? "3" : (std::string("cDepth") + std::to_string(i + 1)).c_str());
			raw->useRange.sString = "cRoot";
			NDb::Ptr<NDb::Ability> db = raw;
			CObj<NWorld::PFAbilityData> ability = new NWorld::PFAbilityData(unit.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL, false, false);
			check(ability->CalcParam("Root", unit.GetPtr(), unit.GetPtr(), nullptr) == 30, "nested local constants");
			check(ability->GetUseRange() == 30, "range binds constants");
			check(ability->CalcParam("Lazy", unit.GetPtr(), unit.GetPtr(), nullptr) == 30, "unselected cycle is not evaluated");
			check(std::isnan(ability->CalcParam("Cycle", unit.GetPtr(), unit.GetPtr(), nullptr)), "cyclic constants fail closed");
			check(std::isnan(ability->CalcParam("Bad", unit.GetPtr(), unit.GetPtr(), nullptr)), "nested missing constant fails closed");
			check(std::isnan(ability->CalcParam("Absent", unit.GetPtr(), unit.GetPtr(), nullptr)), "missing constant differs from zero");
			check(ability->CalcParam("Depth1", unit.GetPtr(), unit.GetPtr(), nullptr) == 3, "sixteen constants accepted");
			check(std::isnan(ability->CalcParam("Depth0", unit.GetPtr(), unit.GetPtr(), nullptr)), "seventeen constants rejected");
			unit->range = 10;
			check(ability->GetUseRange() == 22, "nested values follow live context");
			unit->range = 14;
		}
		{
			CObj<RangeUnit> target = new RangeUnit;
			target->range = 7;
			NDb::Ability* raw = new NDb::Ability;
			raw->useRange.sString = "sRange+tRange";
			NDb::Ptr<NDb::Ability> db = raw;
			CObj<NWorld::PFAbilityData> ability = new NWorld::PFAbilityData(unit.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL, false, false);
			check(ability->GetUseRange(target.GetPtr()) == 21, "explicit target context");
			check(ability->GetUseRange() == 28, "implicit target is owner");
			CObj<NWorld::PFAbilityData> missingOwner = new NWorld::PFAbilityData(nullptr, db, NDb::ABILITYTYPEID_SPECIAL, false, false);
			check(std::isnan(missingOwner->GetUseRange()), "missing owner fails closed");
		}
		for (const char* expression : {"0.0f", "-1", "missing", "1/0", "sUnsupported"})
		{
			NDb::Ability* raw = new NDb::Ability;
			raw->useRange.sString = expression;
			NDb::Ptr<NDb::Ability> db = raw;
			CObj<NWorld::PFAbilityData> ability = new NWorld::PFAbilityData(unit.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL, false, false);
			const float value = ability->GetUseRange();
			check(expression[0] == '0' ? value == 0 : expression[0] == '-' ? value == -1 : std::isnan(value),
				"legitimate nonpositive range differs from failed evaluation");
		}
	}
	// Own the session resource as in production; avoid the legacy static AI fallback.
	NDb::SessionLogicRoot* logic = new NDb::SessionLogicRoot;
	NDb::AILogicParameters* ai = new NDb::AILogicParameters;
	NDb::UnitConstantsContainer* globals = new NDb::UnitConstantsContainer;
	NDb::UnitConstant* global = new NDb::UnitConstant;
	global->name = "Global"; global->var.sString = "9";
	globals->vars.push_back(global); ai->globalConstants = globals; logic->aiLogic = ai;
	NDb::SessionRoot* session = new NDb::SessionRoot;
	session->logicRoot = logic; NDb::SessionRoot::InitRoot(session);
	{
		NDb::Terrain* terrain = new NDb::Terrain;
		terrain->elemXCount = 2; terrain->elemYCount = 2; terrain->tilesPerElement = 4;
		NDb::AdvMap* map = new NDb::AdvMap; map->terrain = terrain;
		NDb::AdvMapDescription* description = new NDb::AdvMapDescription; description->map = map;
		NDb::Ptr<NDb::AdvMapDescription> mapOwner = description;
		CObj<NCore::IWorldBase> worldOwner = NWorld::PFWorld::CreatePFWorld();
		auto* world = dynamic_cast<NWorld::PFWorld*>(worldOwner.GetPtr());
		check(world->LoadMap(description, 0, NCore::TPlayersStartInfo(), 0, false,
			NWorld::PFResourcesCollection::TalentMap()), "global constant mock world");
		CObj<RangeUnit> unit = new RangeUnit(world);
		NDb::Ability* raw = new NDb::Ability; raw->useRange.sString = "cGlobal+sRange";
		NDb::Ptr<NDb::Ability> db = raw;
		CObj<NWorld::PFAbilityData> ability = new NWorld::PFAbilityData(unit.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL, false, false);
		check(ability->GetUseRange() == 23, "global constant resolved through AI world");
		NDb::UnitConstantsContainer* local = new NDb::UnitConstantsContainer;
		NDb::UnitConstant* shadow = new NDb::UnitConstant; shadow->name = "Global"; shadow->var.sString = "2";
		local->vars.push_back(shadow); raw->constants = local;
		CObj<NWorld::PFAbilityData> shadowAbility = new NWorld::PFAbilityData(unit.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL, false, false);
		check(shadowAbility->GetUseRange() == 16, "local shadows global");
		check(ability->GetUseRange() == 23, "ability constant maps remain isolated");
		// Explicit endpoints distinguish ability/damage contexts and live parameter refresh.
		auto& scaling = ai->abilityAndDamageScalingParams;
		scaling.abilityScaleStatLeft = 50; scaling.abilityScaleStatRight = 160;
		scaling.damageScaleStatLeft = 60; scaling.damageScaleStatRight = 170;
		const auto scale = [&](const char* expression) {
			return LinuxBootstrap::EvaluateUnitNumericFormula(expression, unit.GetPtr(), unit.GetPtr(), ability.GetPtr());
		};
		const auto expectScale = [&](const char* expression, float expected) {
			const auto value = scale(expression);
			check(value.Succeeded() && fabs(value.value - expected) < .0001f, expression);
		};
		for (const auto& entry : std::vector<std::pair<const char*, float>>{
			{"abilityScale(50,4,30)", 4}, {"abilityScale(160,4,30)", 30},
			{"abilityScale(0,4,30)", 4}, {"abilityScale(270,4,30)", 56},
			{"abilityScale(105,4,31,false)", 17.5f}, {"abilityScale(105,4,31)", 18},
			{"abilityScale(105,-30,-5)", -18}, {"abilityScale(105,30,4,false)", 30},
			{"damageScale(115,10,21,false)", 15.5f}, {"damageScale(115,10,21,true)", 16},
			{"abilityScale(max(sBaseStrength,sBaseIntellect),5,26.5)", 6},
			{"abilityScale(105,4,30)+damageScale(115,4,30)", 34},
			{"abilityScale(105,4,damageScale(115,10,22),false)", 10}})
			expectScale(entry.first, entry.second);
		NDb::Ptr<NDb::UnitConstant> authored = NDb::Get<NDb::UnitConstant>(
			NDb::DBID("/Items/Talents/Class/Plane/const_A4_BaseDamage.xdb"));
		check(authored && authored->var.sString.find("damageScale") != string::npos, "shipped scaling constant loaded");
		unit->intellect = 115;
		if (authored) expectScale(authored->var.sString.c_str(), 784);
		raw->useRange.sString = "abilityScale(sIntellect,4,26,false)";
		check(ability->GetUseRange() == 17, "actual range consumes checked scaling");
		scaling.abilityScaleStatRight = 180;
		check(ability->GetUseRange() == 15, "scaling parameters refresh between evaluations");
		scaling.abilityScaleStatRight = 160;
		for (const char* expression : {"abilityScale()", "abilityScale(1,2)", "abilityScale(1,2,3,4,5)",
			"1 ? 2 : damageScale(1,2)", "abilityScaleLife(1,2)", "abilityScale((1,2),3,4)",
			"abilityScale(1e39,4,30)", "damageScale(115,1e38,1e38)",
			"abilityScale(1e38,-1e38,1e38,false)", "abilityScale(105,4,30,1/0)"})
			check(!scale(expression).Succeeded(), expression);
		for (float invalid : {50.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
		{
			scaling.abilityScaleStatRight = invalid;
			check(!scale("abilityScale(105,4,30)").Succeeded(), "invalid scaling endpoints fail closed");
			expectScale("0 ? abilityScale(105,4,30) : 7", 7);
		}
		scaling.abilityScaleStatRight = 160;
		check(!LinuxBootstrap::EvaluateUnitNumericFormula("abilityScale(105,4,30)", unit, unit).Succeeded(), "scaling requires ability context");
		CObj<RangeUnit> detached = new RangeUnit;
		CObj<NWorld::PFAbilityData> detachedAbility = new NWorld::PFAbilityData(detached.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL, false, false);
		check(!LinuxBootstrap::EvaluateUnitNumericFormula("abilityScale(105,4,30)", detached, detached, detachedAbility).Succeeded(), "scaling requires owner world");
		check(LinuxBootstrap::EvaluateUnitNumericFormula("1 ? 9 : abilityScale(105,4,30)", nullptr, nullptr).value == 9, "unselected scaling needs no context");
		global->var.sString = "abilityScale(105,4,31,false)";
		check(ability->CalcParam("Global", unit, unit, nullptr) == 17.5f, "nested constant retains scaling context");
		raw->manaCost.sString = "abilityScale(105,4,30)";
		raw->cooldownTime.sString = "damageScale(115,2,4)";
		CObj<NWorld::PFAbilityData> resources = new NWorld::PFAbilityData(unit.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL, false, false);
		check(resources->GetManaCost() == 17, "mana cost consumes scaling");
		resources->RestartCooldown();
		check(resources->GetCurrentCooldown() == 3, "cooldown consumes scaling");
		expectScale("abilityScale(105,2147483520,2147483520)", 2147483520.f);
		check(!scale("abilityScale(105,2147483648,2147483648)").Succeeded(), "integer round upper bound rejected");
		expectScale("abilityScale(105,2147483648,2147483648,false)", 2147483648.f);
		expectScale("abilityScale(105,-2147483648,-2147483648)", -2147483648.f);
		check(!scale("abilityScale(105,-2147483904,-2147483904)").Succeeded(), "integer round lower bound rejected");
	}
	NDb::SessionRoot::InitRoot(nullptr);
	NDb::SetResourceCache(nullptr);
	RootFileSystem::UnregisterFileSystem(files);
	std::printf("Formula range: %d checks, %d failures\n", checks, failures);
	return failures == 0;
}
