#include "../System/systemStdAfx.h"
#include "formula_range_probe.h"
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
	}
	NDb::SessionRoot::InitRoot(nullptr);
	NDb::SetResourceCache(nullptr);
	RootFileSystem::UnregisterFileSystem(files);
	std::printf("Formula range: %d checks, %d failures\n", checks, failures);
	return failures == 0;
}
