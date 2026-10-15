#pragma once

#include "../PF_GameLogic/PFHero.h"
// Keep the existing native cross-TU pointer boundary until the statistics runtime is linked.
template<> CObjectBase* CastToObjectBaseImpl<NWorld::PFHeroStatistics>(NWorld::PFHeroStatistics*, void*);
template<> NWorld::PFHeroStatistics* CastToUserObjectImpl<NWorld::PFHeroStatistics>(CObjectBase*, NWorld::PFHeroStatistics*, void*);
template<> NWorld::PFHeroStatistics* CastToUserObjectImpl<NWorld::PFHeroStatistics>(CObjectBase*, NWorld::PFHeroStatistics*, CObjectBase*);
#include "../PF_GameLogic/PFHeroStatistics.h"
#include <cmath>

namespace LinuxBootstrap
{
/** Original six team-slot accumulation and first-slot tie order for qualified hero kills.
 * PFAIWorld supplies registered heroes and the existing statistics history suffix.
 * This collector neither records damage nor substitutes applicator history for statistics.
 */
struct KillDamageTotals
{
	struct Entry { CPtr<NWorld::PFBaseUnit> hero; float damage = 0.f; } entries[6];
	NDb::EFaction faction;
	bool valid = true;
	explicit KillDamageTotals(NDb::EFaction faction) : faction(faction) {}
	void operator()(NWorld::PFBaseHero& unit)
	{
		const int id = unit.GetInTeamId();
		if (id < 0 || id >= 6) { valid = false; return; }
		entries[id].hero = &unit; entries[id].damage = 0.f;
	}
	void operator()(const NWorld::PFDamageHistoryData& record)
	{
		if (!IsValid(record.pHero) || !std::isfinite(record.time)) { valid = false; return; }
		if (!record.pHero->IsTrueHero() || record.pHero->GetFaction() != faction) return;
		const auto* unit = dynamic_cast<const NWorld::PFBaseHero*>(record.pHero.GetPtr());
		const int id = unit ? unit->GetInTeamId() : -1;
		if (id < 0 || id >= 6 || !std::isfinite(record.appliedDamage)) { valid = false; return; }
		entries[id].damage += record.appliedDamage;
		if (!std::isfinite(entries[id].damage)) valid = false;
	}
	int Best() const
	{
		int best = 0;
		for (int i = 1; i < 6; ++i) if (entries[i].damage > entries[best].damage) best = i;
		return best;
	}
	/** Neutral damage wins ties; absent registration cannot award a populated numeric slot. */
	NWorld::PFBaseUnit* Qualified(float neutralDamage) const
	{
		if (!valid || !std::isfinite(neutralDamage)) return nullptr;
		const auto& best = entries[Best()];
		return IsValid(best.hero) && best.damage > neutralDamage ? best.hero.GetPtr() : nullptr;
	}
};
}
