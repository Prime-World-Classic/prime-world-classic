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
