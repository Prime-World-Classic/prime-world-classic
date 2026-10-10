#include "../System/systemStdAfx.h"
#include "formula_range_probe.h"
#include "../PF_GameLogic/StringExecutorBootstrap.h"
#include "../PF_GameLogic/PFAbilityData.h"
#include "../PF_GameLogic/PFBaseUnit.h"
#include "../PF_GameLogic/PFUniTarget.h"
#include "../PF_GameLogic/DBTalent.h"
#include "../System/FileSystem/WinFileSystem.h"
#include "../libdb/DbResourceCache.h"
#include <cmath>
#include <cstdio>

namespace
{
/** Existing virtual engine getters separate base values from modified live stats. */
class RangeUnit : public NWorld::PFBaseUnit
{
public:
	RangeUnit() : PFBaseUnit(nullptr, CVec3(0, 0, 0), nullptr) {}
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
	NDb::SetResourceCache(nullptr);
	RootFileSystem::UnregisterFileSystem(files);
	std::printf("Formula range: %d checks, %d failures\n", checks, failures);
	return failures == 0;
}
