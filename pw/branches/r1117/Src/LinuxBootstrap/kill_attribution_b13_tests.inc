#include "kill_damage_totals.h"

/** Seed serialized rating/map metadata without widening production interfaces or changing layouts. */
struct KillRatingTagB13
{
	using Pointer = float PFBaseHero::*;
	friend Pointer KillFieldB13(KillRatingTagB13);
};
struct KillMapTagB13
{
	using Pointer = NDb::Ptr<NDb::AdvMapDescription> PFWorld::*;
	friend Pointer KillFieldB13(KillMapTagB13);
};
struct KillTeamTagB13
{
	using Pointer = int PFBaseHero::*;
	friend Pointer KillFieldB13(KillTeamTagB13);
};
template<class Tag, typename Tag::Pointer member> struct KillAccessB13
{
	friend typename Tag::Pointer KillFieldB13(Tag) { return member; }
};
template struct KillAccessB13<KillRatingTagB13, &PFBaseHero::raiting>;
template struct KillAccessB13<KillMapTagB13, &PFWorld::advMapDescription>;
template struct KillAccessB13<KillTeamTagB13, &PFBaseHero::inTeamId>;

/** Actual units, instant damage applicators and original histories own every attribution candidate. */
struct KillAttributionFixtureB13
{
	Checks& check;
	PFWorld* world;
	NDb::KillTimeIntervals saved;
	NDb::Ptr<NDb::AdvMapDescription> savedMap;
	std::vector<CObj<PFBaseUnit>> units;
	explicit KillAttributionFixtureB13(Checks& check, PFWorld* world)
		: check(check), world(world), saved(world->GetAIWorld()->GetAIParameters().killTimeIntervals), savedMap(world->GetMapDescription())
	{
		auto& times = const_cast<NDb::AILogicParameters&>(world->GetAIWorld()->GetAIParameters()).killTimeIntervals;
		times.creepKillInterval = 2; times.buildingKillInterval = 4;
		times.heroKillerDeadInterval = 3; times.heroKillDamageInterval = 5;
		world->*ApplicatorHistoryMemberB13(ApplicatorHistoryClockTagB13()) = 10;
	}
	CObj<PFBaseUnit> Unit(bool hero, NDb::EFaction faction = NDb::FACTION_FREEZE, bool building = false)
	{
		CObj<PFBaseUnit> unit;
		if (hero) unit = B9MaleHero(world, faction).GetPtr();
		else if (building) unit = ::Unit(world, faction, 40);
		else unit = B8BehaviourMoving(world, faction).GetPtr();
		unit->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); unit->AddFlag(NDb::UNITFLAG_FORBIDREWARDS);
		units.push_back(unit); return unit;
	}
	CObj<PFApplDamage> Hit(PFBaseUnit* sender, PFBaseUnit* victim, float age = 0, const char* amount = "1")
	{
		auto* raw = new NDb::Ability; raw->manaCost.sString = "0";
		NDb::Ptr<NDb::Ability> db = raw; CObj<PFAbilityData> ability = new PFAbilityData(sender, db, NDb::ABILITYTYPEID_SPECIAL);
		const Target target(victim); CObj<PFAbilityInstance> cast = new PFAbilityInstance(ability, target, true);
		PFApplCreatePars pars(cast, target); pars.pDBAppl = DamageHistoryFixtureB13::DamageDb(amount);
		CObj<PFBaseApplicator> app = CreateApplicator(pars);
		check(app && ActivateApplicator(app, cast), "B13 killer fixture delivers actual damage through the original factory");
		auto* damage = dynamic_cast<PFApplDamage*>(app.GetPtr());
		check(damage && damage->IsLinkedToAppliedRing(), "B13 actual damage is retained in victim history for attribution");
		if (damage) damage->SetMarkerTime(world->GetTimeElapsed() - age);
		return damage;
	}
	void Map(const char* path)
	{
		world->*KillFieldB13(KillMapTagB13()) = new ApplicatorQueryDbB12<NDb::AdvMapDescription>(path);
	}
	void Expect(PFBaseUnit* victim, PFBaseUnit* actual, PFBaseUnit* expected, const PFApplDamage* expectedDamage = nullptr)
	{
		CPtr<PFBaseUnit> killer(actual); const PFApplDamage* damage = nullptr;
		world->GetAIWorld()->GetKiller(CPtr<PFBaseUnit>(victim), killer, damage);
		check(killer.GetPtr() == expected && damage == expectedDamage,
			"B13 qualified killer and optional history applicator match original attribution precedence");
		killer = actual; world->GetAIWorld()->GetKiller(CPtr<PFBaseUnit>(victim), killer);
		check(killer.GetPtr() == expected, "B13 both original killer overloads preserve the same owner identity");
	}
	~KillAttributionFixtureB13()
	{
		for (auto& unit : units) unit->SetMaster(nullptr);
		for (auto& unit : units) unit->Cleanup();
		const_cast<NDb::AILogicParameters&>(world->GetAIWorld()->GetAIParameters()).killTimeIntervals = saved;
		world->*KillFieldB13(KillMapTagB13()) = savedMap;
	}
};

/** Direct/neutral/allied kills retain identity; summons resolve only complete, acyclic owner chains. */
static void KillAttributionB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	using F = KillAttributionFixtureB13;
	{
		F f(check, isolated.world); auto victim = f.Unit(false, NDb::FACTION_BURN);
		for (bool hero : {false, true}) for (auto faction : {NDb::FACTION_FREEZE, NDb::FACTION_BURN, NDb::FACTION_NEUTRAL})
		{
			auto actual = f.Unit(hero, faction); f.Expect(victim, actual, actual);
		}
		f.Expect(victim, nullptr, nullptr); auto actual = f.Unit(true); f.Expect(nullptr, actual, actual);
	}
	for (int length : {1, 2, 4, 127, 128}) for (bool hero : {false, true})
	{
		F f(check, isolated.world); auto victim = f.Unit(false, NDb::FACTION_BURN), actual = f.Unit(false), last = actual;
		for (int i = 0; i < length; ++i) { auto next = f.Unit(i == length - 1 && hero); last->SetMaster(next.GetPtr()); last = next; }
		f.Expect(victim, actual, length < 128 ? last.GetPtr() : actual.GetPtr());
	}
	for (int cycle : {0, 1, 2})
	{
		F f(check, isolated.world); auto victim = f.Unit(false, NDb::FACTION_BURN), a = f.Unit(false), b = f.Unit(false), c = f.Unit(false);
		a->SetMaster(cycle ? b.GetPtr() : a.GetPtr());
		if (cycle == 1) b->SetMaster(a.GetPtr()); if (cycle == 2) { b->SetMaster(c.GetPtr()); c->SetMaster(b.GetPtr()); }
		f.Expect(victim, a, a);
	}
	for (bool building : {false, true}) for (float rating : {900.f, 1100.f, 1300.f, 1500.f, 1700.f})
	for (float extra : {-.25f, 0.f, .125f, .25f, .375f, .5f})
	{
		F f(check, isolated.world); auto victim = f.Unit(false, NDb::FACTION_BURN, building), actual = f.Unit(false), hero = f.Unit(true);
		static_cast<PFBaseHero*>(hero.GetPtr())->*KillFieldB13(KillRatingTagB13()) = rating;
		const float interval = building ? 4 : 2; auto damage = f.Hit(hero, victim, interval + extra);
		if (!damage) continue;
		const float age = f.world->GetTimeElapsed() - damage->GetMarkerTime();
		const float bonus = std::min(1.f, std::max(0.f, (1500 - rating) / 400)) * .4f;
		const bool recent = age < interval + bonus;
		f.Expect(victim, actual, recent ? hero.GetPtr() : actual.GetPtr(), recent ? damage.GetPtr() : nullptr);
	}
	for (const char* map : {"Maps/Multiplayer/ARAM/_.ADMPDSCR.xdb", "Maps/Multiplayer/MOBA/_.ADMPDSCR.xdb",
		"Maps/Multiplayer/Tournament/_1.ADMPDSCR.xdb", "Maps/Multiplayer/MOBA/_.ADMPDSCR.xdb"})
	{
		F f(check, isolated.world); f.Map(map); auto victim = f.Unit(false, NDb::FACTION_BURN), actual = f.Unit(false), hero = f.Unit(true);
		static_cast<PFBaseHero*>(hero.GetPtr())->*KillFieldB13(KillRatingTagB13()) = 1100;
		auto damage = f.Hit(hero, victim, 2.125f);
		const bool ordinary = std::strstr(map, "MOBA") != nullptr;
		f.Expect(victim, actual, ordinary ? hero.GetPtr() : actual.GetPtr(), ordinary ? damage.GetPtr() : nullptr);
	}
	{
		F f(check, isolated.world); auto victim = f.Unit(false, NDb::FACTION_BURN), actual = f.Unit(false);
		auto first = f.Unit(true), recent = f.Unit(true), enemy = f.Unit(true, NDb::FACTION_BURN);
		f.Hit(first, victim, 1); auto chosen = f.Hit(recent, victim, .5f, "0"); f.Hit(enemy, victim, .25f);
		f.Expect(victim, actual, recent, chosen);
		const PFApplDamage* retained = chosen; CPtr<PFBaseUnit> direct(first.GetPtr());
		f.world->GetAIWorld()->GetKiller(CPtr<PFBaseUnit>(victim.GetPtr()), direct, retained);
		check(direct == first && retained == chosen, "B13 early direct-hero return preserves caller-owned history output");
	}
	for (bool newestDead : {false, true}) for (bool olderDead : {false, true})
	{
		F f(check, isolated.world); auto victim = f.Unit(true, NDb::FACTION_BURN), actual = f.Unit(false);
		auto first = f.Unit(true), recent = f.Unit(true);
		f.Hit(first, victim, 1); f.Hit(recent, victim, .5f);
		if (olderDead) first->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
		if (newestDead) recent->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
		f.Expect(victim, actual, newestDead ? recent.GetPtr() : actual.GetPtr());
	}
	for (float age : {2.75f, 3.f, 3.25f})
	{
		F f(check, isolated.world); auto victim = f.Unit(true, NDb::FACTION_BURN), actual = f.Unit(false), hero = f.Unit(true);
		f.Hit(hero, victim, age); hero->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
		f.Expect(victim, actual, age < 3 ? hero.GetPtr() : actual.GetPtr());
	}
	{
		F f(check, isolated.world); auto victim = f.Unit(false, NDb::FACTION_BURN), actual = f.Unit(false), hero = f.Unit(true);
		f.Hit(hero, victim, 0); f.Hit(actual, victim, 3);
		f.Expect(victim, actual, actual);
	}
	for (int invalid = 0; invalid < 5; ++invalid)
	{
		F f(check, isolated.world); auto victim = f.Unit(false, NDb::FACTION_BURN), actual = f.Unit(false), hero = f.Unit(true);
		auto damage = f.Hit(hero, victim); if (!damage) continue;
		const float nan = std::numeric_limits<float>::quiet_NaN();
		if (invalid == 0) damage->SetMarkerTime(nan);
		if (invalid == 1) damage->SetMarkerTime(11);
		if (invalid == 2) static_cast<PFBaseHero*>(hero.GetPtr())->*KillFieldB13(KillRatingTagB13()) = nan;
		if (invalid == 3) const_cast<NDb::AILogicParameters&>(f.world->GetAIWorld()->GetAIParameters()).killTimeIntervals.creepKillInterval = nan;
		if (invalid == 4) f.world->*ApplicatorHistoryMemberB13(ApplicatorHistoryClockTagB13()) = nan;
		const PFApplDamage* retained = damage; CPtr<PFBaseUnit> killer(actual.GetPtr());
		f.world->GetAIWorld()->GetKiller(CPtr<PFBaseUnit>(victim.GetPtr()), killer, retained);
		check(killer == actual && retained == damage, "B13 malformed selected history rating interval or clock preserves both caller outputs");
		f.world->*ApplicatorHistoryMemberB13(ApplicatorHistoryClockTagB13()) = 10;
	}
	{
		B8BehaviourForeignWorld foreign(check); if (foreign.world)
		{
			F f(check, isolated.world), other(check, foreign.world);
			auto victim = f.Unit(false, NDb::FACTION_BURN), actual = f.Unit(false), master = other.Unit(true);
			actual->SetMaster(master.GetPtr()); f.Expect(victim, actual, actual); actual->SetMaster(nullptr);
		}
	}
	{
		F f(check, isolated.world); auto victim = f.Unit(true, NDb::FACTION_BURN), actual = f.Unit(false);
		auto first = f.Unit(true), second = f.Unit(true), allied = f.Unit(true, NDb::FACTION_BURN);
		auto* a = static_cast<PFBaseHero*>(first.GetPtr()); auto* b = static_cast<PFBaseHero*>(second.GetPtr());
		a->*KillFieldB13(KillTeamTagB13()) = 0; b->*KillFieldB13(KillTeamTagB13()) = 1;
		for (float secondDamage : {3.f, 4.f, 5.f}) for (float neutral : {0.f, 3.f, 4.f, 5.f, 6.f})
		{
			LinuxBootstrap::KillDamageTotals totals(NDb::FACTION_FREEZE); totals(*b); totals(*a);
			totals(PFDamageHistoryData(a, 1.25f, 9)); totals(PFDamageHistoryData(a, 2.75f, 10));
			totals(PFDamageHistoryData(b, secondDamage, 10)); totals(PFDamageHistoryData(CPtr<PFBaseUnit>(allied.GetPtr()), 100, 10));
			PFBaseUnit* expected = std::max(4.f, secondDamage) <= neutral ? nullptr : secondDamage > 4 ? b : a;
			check(totals.valid && totals.Qualified(neutral) == expected,
				"B13 original record collector sums fractional hero damage, preserves team-slot ties and lets neutrals win equal damage");
		}
	}
	for (int invalid = 0; invalid < 6; ++invalid)
	{
		F f(check, isolated.world); auto hero = f.Unit(true); auto* a = static_cast<PFBaseHero*>(hero.GetPtr());
		a->*KillFieldB13(KillTeamTagB13()) = invalid == 0 ? -1 : invalid == 1 ? 6 : 0;
		LinuxBootstrap::KillDamageTotals totals(NDb::FACTION_FREEZE); totals(*a);
		const float nan = std::numeric_limits<float>::quiet_NaN();
		totals(PFDamageHistoryData(CPtr<PFBaseUnit>(invalid == 2 ? nullptr : hero.GetPtr()), invalid == 3 ? nan : 5, invalid == 4 ? nan : 10));
		check(totals.Qualified(invalid == 5 ? nan : 0) == nullptr,
			"B13 malformed team slots, records or neutral totals cannot produce a qualified hero");
	}
	{
		F f(check, isolated.world); auto hero = f.Unit(true); auto* a = static_cast<PFBaseHero*>(hero.GetPtr());
		a->*KillFieldB13(KillTeamTagB13()) = 0;
		LinuxBootstrap::KillDamageTotals missing(NDb::FACTION_FREEZE); missing(PFDamageHistoryData(a, 5, 10));
		check(!missing.Qualified(0), "B13 unregistered statistical slot does not fabricate a hero owner");
		LinuxBootstrap::KillDamageTotals overflow(NDb::FACTION_FREEZE); overflow(*a);
		for (int i = 0; i < 2; ++i) overflow(PFDamageHistoryData(a, std::numeric_limits<float>::max(), 10));
		check(!overflow.valid && !overflow.Qualified(0), "B13 finite record sums cannot overflow into a winning total");
		LinuxBootstrap::KillDamageTotals signedAmounts(NDb::FACTION_FREEZE); signedAmounts(*a);
		signedAmounts(PFDamageHistoryData(a, 5, 10)); signedAmounts(PFDamageHistoryData(a, -2, 10));
		check(signedAmounts.Qualified(2) == a && !signedAmounts.Qualified(3), "B13 original signed history accumulation is not clamped per record");
	}
	std::printf("B13 kill attribution checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
