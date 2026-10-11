#include "../System/systemStdAfx.h"
#include "../PF_GameLogic/StringExecutorBootstrap.h"
#include "applicator_formula_probe.h"
#include "formula_context.h"
#include "../PF_GameLogic/PFApplInstant.h"
#include "../PF_GameLogic/PFAbilityData.h"
#include "../PF_GameLogic/PFAbilityInstance.h"
#include "../PF_GameLogic/PFHero.h"
#include "../PF_GameLogic/DBHeroesList.h"
#include "../PF_GameLogic/PFWorld.h"
#include "../PF_GameLogic/PFAIWorld.h"
#include "../PF_GameLogic/PFResourcesCollectionClient.h"
#include "../PF_GameLogic/DBAdvMap.h"
#include "../PF_GameLogic/DBSessionRoots.h"
#include "../Terrain/DBTerrain.h"
#include <cstdio>
#include <limits>

namespace
{
using namespace NWorld;
/** Release-mode assertions report the expression without replacing engine behavior. */
struct Checks
{
	int count = 0, failed = 0;
	void operator()(bool ok, const char* label)
	{
		++count;
		if (!ok) { ++failed; std::printf("Applicator formula FAIL: %s\n", label); }
	}
};

/** Only expose the existing start hook; target resolution still uses Init(). */
struct Damage : PFApplDamage
{
	explicit Damage(const PFApplCreatePars& p) : PFApplDamage(p) {}
	bool Run() { return Start(); }
};

struct Heal : PFApplHeal
{
	explicit Heal(const PFApplCreatePars& p) : PFApplHeal(p) {}
	bool Run() { return Start(); }
};

struct Refresh : PFApplRefreshCooldown
{
	explicit Refresh(const PFApplCreatePars& p) : PFApplRefreshCooldown(p) {}
	bool Run() { return Start(); }
};

/** Access the protected initializer without overriding gameplay or formula getters. */
struct ProbeUnit : PFBaseUnit
{
	int damageCalls = 0;
	int beforeCalls = 0;
	float delegateAmount = 0, observedBefore = 0, observedDuring = 0;
	float OnBeforeDamage(const DamageDesc& desc) override
	{
		++beforeCalls;
		observedBefore = desc.pDealerApplicator ? desc.pDealerApplicator->GetVariable("damageDealed") : -1;
		desc.delegatedDamage = delegateAmount;
		return desc.amount;
	}
	DamageDesc lastDamage;
	float OnDamage(const DamageDesc& desc) override
	{
		++damageCalls; lastDamage = desc;
		observedDuring = desc.pDealerApplicator ? desc.pDealerApplicator->GetVariable("damageDealed") : -1;
		return PFBaseUnit::OnDamage(desc);
	}
	ProbeUnit(PFWorld* world, const NDb::Unit* db, NDb::EFaction faction) : PFBaseUnit(world, CVec3(5, 5, 0), db)
	{
		InitData data; data.pObjectDesc = db; data.type = NDb::UNITTYPE_BUILDING;
		data.faction = faction; data.playerId = -1; Initialize(data);
		AddFlag(NDb::UNITFLAG_FORBIDREWARDS);
	}
};

/** Distinct literal stats exercise the real unit initializer and resource pools. */
CObj<PFBaseUnit> Unit(PFWorld* world, NDb::EFaction faction, float strength)
{
	NDb::StatsContainer* stats = new NDb::StatsContainer;
	for (const auto& entry : {nstl::pair<NDb::EStat, float>(NDb::STAT_LIFE, 1000),
		nstl::pair<NDb::EStat, float>(NDb::STAT_ENERGY, 200),
		nstl::pair<NDb::EStat, float>(NDb::STAT_STRENGTH, strength),
		nstl::pair<NDb::EStat, float>(NDb::STAT_BASEATTACK, 1)})
	{
		NDb::UnitStat stat; stat.statId = entry.first;
		stat.value.sString = NStr::StrFmt("%.9g", entry.second);
		stat.increment.sString = "0"; stats->stats.push_back(stat);
	}
	NDb::Unit* raw = new NDb::Unit; raw->stats = stats;
	NDb::Ptr<NDb::Unit> db = raw;
	CObj<PFBaseUnit> unit = new ProbeUnit(world, db, faction);
	return unit;
}

void DamageFormulas(Checks& check, PFWorld* world)
{
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 105);
	CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 15);
	NDb::Ability* rawAbility = new NDb::Ability;
	NDb::UnitConstantsContainer* constants = new NDb::UnitConstantsContainer;
	NDb::UnitConstant* constant = new NDb::UnitConstant;
	constant->name = "Damage"; constant->var.sString = "sStrength+tStrength";
	constants->vars.push_back(constant); rawAbility->constants = constants;
	NDb::Ptr<NDb::Ability> abilityDb = rawAbility;
	CObj<PFAbilityData> ability = new PFAbilityData(sender.GetPtr(), abilityDb, NDb::ABILITYTYPEID_SPECIAL, false, false);
	const auto run = [&](const char* expression, float expected, const char* enabled = "true", bool compiledOnly = false) {
		NDb::DamageApplicator* raw = new NDb::DamageApplicator;
		raw->damage.sString = expression; raw->enabled.sString = enabled;
		if (compiledOnly) raw->damage.compiledString = "unsupported-native-bytecode";
		raw->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
		NDb::Ptr<NDb::DamageApplicator> db = raw;
		const Target target(receiver.GetPtr());
		PFApplCreatePars pars(CObj<PFAbilityInstance>(), target);
		pars.pWorld = world; pars.pOwner = sender; pars.pUpgraderAbilityData = ability; pars.pDBAppl = db;
		CObj<Damage> app = new Damage(pars);
		receiver->SetHealth(1000);
		check(app->Init(), "damage target initialization");
		check(app->Run(), "instant damage completes");
		check(receiver->GetHealth() == 1000 - expected, expression);
		check(app->GetDamageDealed() == expected, "reported actual damage matches resource delta");
	};
	run("25", 25); run("sStrength+tStrength", 120); run("cDamage", 120);
	run("damageScale(sStrength,10,30,false)", 20);
	run("max(0,tMaxLife-tLife)+7", 7); run("1 ? 13 : cMissing", 13);
	for (const char* expression : {"", "0", "-1", "1/0", "1e39", "cMissing", "unknown(sStrength)",
		"0*(sDamageMin+sDamageMax)", "missing+sDamageMin+sDamageMax"}) run(expression, 0);
	run("", 0, "true", true);
	run("25", 0, "false");
	sender->GetStat(NDb::STAT_STRENGTH)->SetCoreValue(115);
	run("cDamage", 130);
	world->GetRndGen()->SetSeed(731);
	NRandom::RandomGenerator expected(731);
	for (int i = 0; i < 12; ++i)
		run("random(10,20)", float(expected.Next(10, 20)));
	run("random(f2l(sDamageMin),f2l(sDamageMax))", float(expected.Next(103, 126)));
	check(world->GetRndGen()->Next() == expected.Next(), "base attack uses the existing seeded world stream");
	world->GetRndGen()->SetSeed(732); expected.SetSeed(732);
	run("random(5,5)", 5); run("random(5,3)", 5);
	for (const char* expression : {"random(2,4)+missing", "random(2,4)+1/0", "random(-2147483648,2147483647)",
		"random(f2l(1e39),3)", "random(1,2,3)", "f2l(2147483648)"}) run(expression, 0);
	run("random(10,20)", 0, "false");
	run("1 ? 7 : random(10,20)", 7);
	check(world->GetRndGen()->Next() == expected.Next(), "invalid disabled and unselected formulas do not advance RNG");
	run("f2l(-3.9)+10", 7);
}
}

/** Health and energy are independent pools; report actual capped restoration. */
static void HealingFormulas(Checks& check, NWorld::PFWorld* world, bool heroTarget = false)
{
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
	CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_FREEZE, 20);
	if (heroTarget)
	{
		// Preserve the same stats so virtual hero healing is compared with ordinary units.
		NDb::Hero* raw = new NDb::Hero; raw->stats = receiver->DbUnitDesc()->stats;
		NDb::Ptr<NDb::Hero> db = raw;
		PFBaseHero::SpawnInfo spawn; spawn.pHero = db;
		spawn.placement = Placement(CVec3(5, 5, 0), QNULL, CVec3(1, 1, 1));
		receiver = new PFBaseHero(world, spawn, NDb::UNITTYPE_HEROMALE, NDb::FACTION_FREEZE, NDb::FACTION_FREEZE);
	}
	const auto run = [&](const char* expression, NDb::EHealTarget pool, float restored,
		bool enabled = true, bool dead = false, bool forbidden = false) {
		NDb::HealApplicator* raw = new NDb::HealApplicator;
		raw->amount.sString = expression; raw->healTarget = pool; raw->enabled.sString = enabled ? "true" : "false";
		NDb::Ptr<NDb::HealApplicator> db = raw;
		const Target target(receiver.GetPtr());
		PFApplCreatePars pars(CObj<PFAbilityInstance>(), target);
		pars.pWorld = world; pars.pOwner = sender; pars.pDBAppl = db;
		CObj<Heal> app = new Heal(pars);
		receiver->SetHealth(400); receiver->SetEnergy(50);
		if (dead) receiver->ChangeHealth(-1000);
		if (forbidden) receiver->AddFlag(NDb::UNITFLAG_FORBIDLIFERESTORE | NDb::UNITFLAG_FORBIDENERGYRESTORE);
		check(app->Init() && app->Run(), "heal initializes and completes");
		check(receiver->GetHealth() == (dead ? 0 : 400) + (pool == NDb::HEALTARGET_HEALTH ? restored : 0), expression);
		check(receiver->GetMana() == 50 + (pool == NDb::HEALTARGET_ENERGY ? restored : 0), "energy pool delta");
		check(app->GetAmountRestored() == restored && app->GetVariable("amountRestored") == restored, "authored restoration variable reports actual delta");
		if (forbidden) receiver->RemoveFlag(NDb::UNITFLAG_FORBIDLIFERESTORE | NDb::UNITFLAG_FORBIDENERGYRESTORE);
	};
	for (const auto pool : {NDb::HEALTARGET_HEALTH, NDb::HEALTARGET_ENERGY})
	{
		run("sStrength+tStrength", pool, 60);
		run("999", pool, pool == NDb::HEALTARGET_HEALTH ? 600 : 150);
		run("25", pool, 0, false); run("25", pool, 0, true, true); run("25", pool, 0, true, false, true);
		for (const char* bad : {"", "0", "-1", "1/0", "1e39", "cMissing"}) run(bad, pool, 0);
	}
	run("(tMaxLife-tLife)*.25", NDb::HEALTARGET_HEALTH, 150);
	run("tMaxEnergy-tEnergy", NDb::HEALTARGET_ENERGY, 150);
	run("25", static_cast<NDb::EHealTarget>(99), 0);
}

/** Refresh uses an actual hero/ability and independently checks both cooldown states. */
static void CooldownFormulas(Checks& check, PFWorld* world)
{
	NDb::Hero* rawHero = new NDb::Hero;
	CObj<PFBaseUnit> source = Unit(world, NDb::FACTION_FREEZE, 40);
	rawHero->stats = source->DbUnitDesc()->stats;
	NDb::Ptr<NDb::Hero> heroDb = rawHero;
	PFBaseHero::SpawnInfo spawn; spawn.pHero = heroDb;
	spawn.placement = Placement(CVec3(5, 5, 0), QNULL, CVec3(1, 1, 1));
	CObj<PFBaseHero> hero = new PFBaseHero(world, spawn, NDb::UNITTYPE_HEROMALE, NDb::FACTION_FREEZE, NDb::FACTION_FREEZE);
	const auto run = [&](const char* expression, float expected, bool percent = false,
		bool increase = false, bool enabled = true, bool compiledOnly = false, bool heroTarget = true) {
		NDb::Ability* rawAbility = new NDb::Ability;
		rawAbility->cooldownTime.sString = "20"; rawAbility->cooldownTimeSecondState.sString = "20";
		NDb::Ptr<NDb::Ability> abilityDb = rawAbility;
		CObj<PFAbilityData> ability = new PFAbilityData(hero.GetPtr(), abilityDb, NDb::ABILITYTYPEID_SPECIAL, false, false);
		ability->RestartCooldown(); ability->SetState(EAbilityState::Second); ability->RestartCooldown();
		ability->Update(10, false);
		const Target target(heroTarget ? static_cast<PFBaseUnit*>(hero.GetPtr()) : source.GetPtr());
		CObj<PFAbilityInstance> instance = new PFAbilityInstance(ability, target, false);
		NDb::RefreshCooldownApplicator* raw = new NDb::RefreshCooldownApplicator;
		raw->refreshThis = true; raw->flags = 0; raw->reductionAmount.sString = expression;
		raw->reduceByPercent = percent; raw->allowCdIncrease = increase;
		raw->enabled.sString = enabled ? "true" : "false";
		if (compiledOnly) raw->reductionAmount.compiledString = "unsupported-native-bytecode";
		NDb::Ptr<NDb::RefreshCooldownApplicator> db = raw;
		PFApplCreatePars pars(instance, target); pars.pDBAppl = db;
		CObj<Refresh> app = new Refresh(pars);
		check(app->Init() && app->Run(), "cooldown applicator completes");
		check(ability->GetCurrentCooldown() == expected, expression);
		ability->SetState(EAbilityState::First);
		check(ability->GetCurrentCooldown() == expected, "refresh applies to both states");
	};
	run("sStrength/10", 6); run(".25", 7.5f, true); run("2", 0, true);
	run("0", 0); run("", 0); run("0", 10, false, true);
	run("-5", 0); run("-5", 15, false, true); run("-.5", 15, true, true);
	run("-30", 40, false, true); run("-.5", 10, true, true, false);
	run("5", 10, false, false, false);
	for (const char* bad : {"cMissing", "1/0", "1e39"}) run(bad, 10);
	run("", 10, false, false, true, true);
	run("5", 10, false, false, true, false, false);
}

/** Observe the real OnDamage path while preserving its resource/event implementation. */
static void DamageRouting(Checks& check, PFWorld* world)
{
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
	CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 20);
	ProbeUnit* observed = static_cast<ProbeUnit*>(receiver.GetPtr());
	const auto run = [&](NDb::EApplicatorDamageType kind, const char* expression, float dealt,
		bool dead = false, const PFBaseUnit::DamageDesc* delegated = nullptr) {
		NDb::DamageApplicator* raw = new NDb::DamageApplicator;
		raw->damageType = kind; raw->damage.sString = expression;
		raw->damageMode = NDb::DAMAGEMODE_DONTATTACKBACK;
		NDb::Ptr<NDb::DamageApplicator> db = raw;
		const Target target(receiver.GetPtr());
		PFApplCreatePars pars(CObj<PFAbilityInstance>(), target);
		pars.pWorld = world; pars.pOwner = sender; pars.pDBAppl = db;
		CObj<Damage> app = new Damage(pars);
		receiver->SetHealth(400); receiver->SetEnergy(50); observed->damageCalls = 0;
		if (dead) receiver->ChangeHealth(-1000);
		check(app->Init(), "routing target initializes");
		if (delegated) app->SetDelegated(delegated);
		check(app->Run(), "routing applicator completes");
		const bool mana = kind == NDb::APPLICATORDAMAGETYPE_MANA;
		check(receiver->GetHealth() == (dead ? 0 : 400) - (mana ? 0 : dealt), "damage routing health");
		check(receiver->GetMana() == 50 - (mana ? dealt : 0), "damage routing mana");
		check(app->GetDamageDealed() == dealt, "routing reports actual delta");
		check(app->GetVariable("damageDealed") == dealt, "authored damage variable");
		check(app->GetVariable("isCritical") == (delegated && dealt > 0 ? 1 : 0), "delegated critical variable");
		check(observed->damageCalls == (!mana && dealt > 0 ? 1 : 0), "only valid health damage invokes callback");
		if (!mana && dealt > 0)
		{
			const auto& actual = observed->lastDamage;
			check(actual.dontAttackBack, "do-not-retaliate flag survives");
			if (delegated)
			{
				check(actual.pSender == delegated->pSender && actual.pDealerApplicator == delegated->pDealerApplicator,
					"delegation preserves sender and dealer");
				check(actual.damageType == delegated->damageType && actual.damageMode == delegated->damageMode
					&& actual.flags == delegated->flags && actual.delegated == delegated->delegated
					&& actual.ignoreDefences == delegated->ignoreDefences
					&& actual.isDelegatedCriticalDamage == delegated->isDelegatedCriticalDamage,
					"delegation preserves damage metadata");
			}
		}
	};
	run(NDb::APPLICATORDAMAGETYPE_MANA, "sStrength+tStrength", 50);
	run(NDb::APPLICATORDAMAGETYPE_MANA, "25", 25);
	run(NDb::APPLICATORDAMAGETYPE_MANA, "25", 0, true);
	run(NDb::APPLICATORDAMAGETYPE_PURE, "25", 0, true);
	run(NDb::APPLICATORDAMAGETYPE_PURE, "25", 25);
	run(static_cast<NDb::EApplicatorDamageType>(99), "25", 0);
	for (const char* bad : {"-1", "1/0", "1e39", "cMissing"}) run(NDb::APPLICATORDAMAGETYPE_MANA, bad, 0);
	PFBaseUnit::DamageDesc delegated;
	delegated.pSender = receiver; delegated.amount = 17; delegated.damageType = NDb::APPLICATORDAMAGETYPE_ENERGY;
	delegated.flags = 9; delegated.damageMode = NDb::DAMAGEMODE_DONTATTACKBACK;
	delegated.dontAttackBack = true; delegated.delegated = true; delegated.ignoreDefences = true;
	delegated.isDelegatedCriticalDamage = true; delegated.pDealerApplicator = nullptr;
	run(NDb::APPLICATORDAMAGETYPE_PURE, "cMissing", 17, false, &delegated);
	run(NDb::APPLICATORDAMAGETYPE_MANA, "25", 0, false, &delegated);
	delegated.amount = std::numeric_limits<float>::infinity();
	run(NDb::APPLICATORDAMAGETYPE_PURE, "25", 0, false, &delegated);
}

/** Critical hits consume exactly the existing world Roll, including 0/100 percent. */
static void CriticalDamage(Checks& check, PFWorld* world)
{
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
	CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 20);
	for (float bonus : {0.0f, 70.0f, 200.0f})
	for (bool enabled : {false, true})
	for (auto kind : {NDb::APPLICATORDAMAGETYPE_PURE, NDb::APPLICATORDAMAGETYPE_MANA})
	{
		sender->GetStat(NDb::STAT_CRITCHANCEBONUS)->SetCoreValue(bonus);
		sender->GetStat(NDb::STAT_CRITICALMULTIPLIER)->SetCoreValue(2);
		world->GetRndGen()->SetSeed(631); NRandom::RandomGenerator oracle(631);
		for (int i = 0; i < 12; ++i)
		{
			NDb::DamageApplicator* raw = new NDb::DamageApplicator;
			raw->damage.sString = "25"; raw->damageType = kind;
			raw->damageMode = enabled ? NDb::DAMAGEMODE_COULDBECRITICAL : NDb::DAMAGEMODE_ZERO;
			NDb::Ptr<NDb::DamageApplicator> db = raw;
			const Target target(receiver.GetPtr()); PFApplCreatePars pars(CObj<PFAbilityInstance>(), target);
			pars.pWorld = world; pars.pOwner = sender; pars.pDBAppl = db;
			CObj<Damage> app = new Damage(pars);
			receiver->SetHealth(1000); receiver->SetEnergy(200);
			const bool critical = enabled && oracle.Roll(sender->GetCritChance());
			const float expected = critical ? 50 : 25;
			check(app->Init() && app->Run(), "critical applicator completes");
			check(app->GetDamageDealed() == expected, "seeded critical amount");
			check(app->GetVariable("isCritical") == (critical ? 1 : 0), "critical authored variable");
			check(receiver->GetHealth() == 1000 - (kind == NDb::APPLICATORDAMAGETYPE_PURE ? expected : 0), "critical health delta");
			check(receiver->GetMana() == 200 - (kind == NDb::APPLICATORDAMAGETYPE_MANA ? expected : 0), "critical mana delta");
			if (kind == NDb::APPLICATORDAMAGETYPE_PURE)
				check(static_cast<ProbeUnit*>(receiver.GetPtr())->lastDamage.isDelegatedCriticalDamage == critical, "critical descriptor metadata");
		}
		check(world->GetRndGen()->Next() == oracle.Next(), "critical world RNG stream matches engine Roll");
	}
}

/** Real event registration with a controlled listener result, not a replacement damage path. */
struct OutgoingListener : CObjectBase, PFBaseUnitEventListener
{
	OBJECT_BASIC_METHODS(OutgoingListener);
public:
	int calls = 0;
	int cancellations = 0;
	float replacement = 11;
	const PFBaseUnit* target = nullptr;
	unsigned int OnEvent(const PFBaseUnitEvent* event) override
	{
		if (event->GetType() == NDb::BASEUNITEVENT_CANCELINVISIBILITY) ++cancellations;
		if (const auto* outgoing = dynamic_cast<const PFOutgoingDamageEvent*>(event))
		{
			++calls; target = outgoing->pTaker; outgoing->damage = replacement;
		}
		return 0;
	}
};

static void OutgoingDamage(Checks& check, PFWorld* world)
{
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
	CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 20);
	CObj<OutgoingListener> listener = new OutgoingListener;
	sender->AddEventListener(listener.GetPtr());
	for (float amount : {11.0f, 75.0f, 0.0f, -3.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	for (bool mana : {false, true})
	{
		NDb::DamageApplicator* raw = new NDb::DamageApplicator;
		raw->damage.sString = "25"; raw->damageType = mana ? NDb::APPLICATORDAMAGETYPE_MANA : NDb::APPLICATORDAMAGETYPE_PURE;
		NDb::Ptr<NDb::DamageApplicator> db = raw;
		const Target target(receiver.GetPtr()); PFApplCreatePars pars(CObj<PFAbilityInstance>(), target);
		pars.pWorld = world; pars.pOwner = sender; pars.pDBAppl = db;
		CObj<Damage> app = new Damage(pars);
		receiver->SetHealth(1000); receiver->SetEnergy(200);
		listener->calls = 0; listener->target = nullptr; listener->replacement = amount;
		const float expected = mana ? 25 : (std::isfinite(amount) && amount > 0 ? amount : 0);
		check(app->Init() && app->Run(), "outgoing applicator completes");
		check(listener->calls == (mana ? 0 : 1), "outgoing health-only event delivered once");
		check(listener->target == (mana ? nullptr : receiver.GetPtr()), "outgoing event receiver identity");
		check(app->GetDamageDealed() == expected, "outgoing modified amount reported");
		check(receiver->GetHealth() == 1000 - (mana ? 0 : expected), "outgoing health delta");
		check(receiver->GetMana() == (mana ? 175 : 200), "outgoing event does not affect mana routing");
	}
	sender->RemoveEventListener(listener.GetPtr());
}

/** Pre-damage return populates the applicator context, not a replacement descriptor amount. */
static void PreDamage(Checks& check, PFWorld* world)
{
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
	CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 20);
	ProbeUnit* observed = static_cast<ProbeUnit*>(receiver.GetPtr());
	CObj<OutgoingListener> listener = new OutgoingListener;
	listener->replacement = 17; sender->AddEventListener(listener.GetPtr());
	for (bool delegated : {false, true})
	{
		NDb::DamageApplicator* raw = new NDb::DamageApplicator;
		raw->damage.sString = "25"; raw->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
		NDb::Ptr<NDb::DamageApplicator> db = raw;
		const Target target(receiver.GetPtr()); PFApplCreatePars pars(CObj<PFAbilityInstance>(), target);
		pars.pWorld = world; pars.pOwner = sender; pars.pDBAppl = db;
		CObj<Damage> app = new Damage(pars);
		PFBaseUnit::DamageDesc desc; desc.pSender = sender; desc.amount = 13;
		desc.damageType = NDb::APPLICATORDAMAGETYPE_PURE; desc.flags = 0;
		desc.damageMode = NDb::DAMAGEMODE_ZERO; desc.dontAttackBack = false; desc.pDealerApplicator = nullptr;
		observed->beforeCalls = observed->damageCalls = listener->calls = 0;
		observed->delegateAmount = 7; receiver->SetHealth(1000);
		check(app->Init(), "pre-damage target initialization");
		if (delegated) app->SetDelegated(&desc);
		check(app->Run(), "pre-damage completes");
		check(observed->beforeCalls == (delegated ? 0 : 1), "pre-hook once only on original damage");
		check(listener->calls == (delegated ? 0 : 1), "delegation does not repeat outgoing event");
		check(observed->damageCalls == 1, "single receiver damage call");
		check(app->GetDamageDealed() == (delegated ? 13 : 17), "actual direct damage");
		check(app->GetVariable("damageDealed") == (delegated ? 13 : 24), "original reports direct plus delegated damage");
		if (!delegated)
		{
			check(observed->observedBefore == 25, "pre-hook sees original post-critical damage context");
			check(observed->observedDuring == 17, "damage reduction sees pre-hook return after outgoing modifier");
		}
	}
	sender->RemoveEventListener(listener.GetPtr());
}

/** Attack history is health-only; only original authored damage cancels invisibility. */
static void DamageNotifications(Checks& check, PFWorld* world)
{
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
	CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 20);
	CObj<OutgoingListener> listener = new OutgoingListener;
	sender->AddEventListener(listener.GetPtr());
	for (bool mana : {false, true})
	for (bool cancel : {false, true})
	for (bool dontAttack : {false, true})
	for (bool delegated : {false, true})
	for (bool enabled : {false, true})
	{
		if (mana && delegated) continue;
		NDb::DamageApplicator* raw = new NDb::DamageApplicator;
		raw->damage.sString = "25"; raw->enabled.sString = enabled ? "true" : "false";
		raw->damageType = mana ? NDb::APPLICATORDAMAGETYPE_MANA : NDb::APPLICATORDAMAGETYPE_PURE;
		raw->damageMode = dontAttack ? NDb::DAMAGEMODE_DONTATTACKBACK : NDb::DAMAGEMODE_ZERO;
		raw->cancelInvisibility = cancel;
		NDb::Ptr<NDb::DamageApplicator> db = raw;
		const Target target(receiver.GetPtr()); PFApplCreatePars pars(CObj<PFAbilityInstance>(), target);
		pars.pWorld = world; pars.pOwner = sender; pars.pDBAppl = db;
		CObj<Damage> app = new Damage(pars);
		PFBaseUnit::DamageDesc desc; desc.pSender = sender; desc.amount = 13;
		desc.damageType = NDb::APPLICATORDAMAGETYPE_PURE; desc.flags = 0;
		desc.damageMode = raw->damageMode; desc.dontAttackBack = dontAttack; desc.pDealerApplicator = nullptr;
		sender->RemoveLastAttackData(); listener->cancellations = 0; receiver->SetHealth(1000); receiver->SetEnergy(200);
		check(app->Init(), "notification target initialization");
		if (delegated) app->SetDelegated(&desc);
		check(app->Run(), "notification application completes");
		check(listener->cancellations == (enabled && cancel && !delegated ? 1 : 0), "authored invisibility cancellation policy");
		const bool remember = enabled && !mana && !dontAttack;
		check(sender->GetLastAttackData()->pVictim.GetPtr() == (remember ? receiver.GetPtr() : nullptr), "health attack history and dont-attack-back policy");
		if (remember)
		{
			check(sender->GetLastAttackData()->originalFaction == NDb::FACTION_BURN, "history stores original victim faction");
			check(!sender->GetLastAttackData()->byAutoAttack, "special effect is not an auto attack");
		}
	}
	sender->RemoveEventListener(listener.GetPtr());
}

/** Bind only the authored combat accessors; the existing numeric parser owns grammar. */
static void CombatFormulaContext(Checks& check, PFWorld* world)
{
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
	CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 20);
	sender->GetVariableVWM("SpellPierce")->SetBaseValue(.125f);
	receiver->GetVariableVWM("SpellDef")->SetBaseValue(.25f);
	receiver->GetStat(NDb::STAT_MATERIALDAMAGEDECREASE)->SetCoreValue(10);
	receiver->GetStat(NDb::STAT_ENERGYDAMAGEDECREASE)->SetCoreValue(20);
	NDb::Ability* rawAbility = new NDb::Ability;
	NDb::UnitConstantsContainer* constants = new NDb::UnitConstantsContainer;
	for (const auto& pair : {nstl::pair<const char*, const char*>("SpellPierce", "pMisc->GetVariable(\"isAutoAttack\") ? 0.0f : sender.vSpellPierce"),
		nstl::pair<const char*, const char*>("SpellDef", "pMisc->GetVariable(\"isAutoAttack\") ? 0.0f : receiver.vSpellDef"),
		nstl::pair<const char*, const char*>("Combat", "cSpellDef-cSpellPierce")})
	{
		NDb::UnitConstant* value = new NDb::UnitConstant; value->name = pair.first; value->var.sString = pair.second;
		constants->vars.push_back(value);
	}
	rawAbility->constants = constants;
	NDb::Ptr<NDb::Ability> abilityDb = rawAbility;
	CObj<PFAbilityData> ability = new PFAbilityData(sender.GetPtr(), abilityDb, NDb::ABILITYTYPEID_SPECIAL, false, false);
	NDb::DamageApplicator* raw = new NDb::DamageApplicator;
	raw->damage.sString = "25"; raw->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
	NDb::Ptr<NDb::DamageApplicator> db = raw;
	const Target target(receiver.GetPtr()); PFApplCreatePars pars(CObj<PFAbilityInstance>(), target);
	pars.pWorld = world; pars.pOwner = sender; pars.pDBAppl = db;
	pars.pUpgraderAbilityData = ability;
	CObj<Damage> app = new Damage(pars); check(app->Init() && app->Run(), "combat context actual applicator");
	const auto run = [&](const char* expression, float expected) {
		const auto result = LinuxBootstrap::EvaluateApplicatorNumericFormula(expression, *app);
		check(result.Succeeded() && std::fabs(result.value - expected) < .00001f, expression);
	};
	run("rStrength+tStrength", 40);
	run("rMaterialDamageDecrease", 10); run("rEnergyDamageDecrease", 20);
	run("rBodyDefence", receiver->GetBodyDefence()); run("rSpiritDefence", receiver->GetSpiritDefence());
	run("sPenetrationTotal", sender->GetPenetrationTotal());
	run("pMisc->GetVariable(\"damageDealed\")", 25);
	run("pMisc->GetVariable(\"isAutoAttack\")", 0);
	run("sender.vSpellPierce+receiver.vSpellDef", .375f);
	run("cCombat", .125f);
	run("pMisc->GetVariable(\"isAutoAttack\") ? 0 : receiver.vSpellDef", .25f);
	run("max(pMisc->GetVariable(\"damageDealed\"),0)*(1-rMaterialDamageDecrease*.01f)", 22.5f);
	run("1 ? 3 : pMisc->GetVariable(\"damageDealed\")", 3);
	for (const char* bad : {"pMisc->GetVariable(\"unknown\")", "pMisc->GetVariable(\"damageDealed\",1)",
		"xpMisc->GetVariable(\"damageDealed\")", "sender.vSpellPierceOther", "receiver.vSpellPierce",
		"sender.vOther", "receiver.vSpellDef()", "sender.vSpellPierce=1", "1 ? 3 : sender.vOther", "__combat_damageDealed"})
		check(!LinuxBootstrap::EvaluateApplicatorNumericFormula(bad, *app).Succeeded(), "unsupported combat access fails closed");
	check(!LinuxBootstrap::EvaluateUnitNumericFormula("pMisc->GetVariable(\"damageDealed\")", sender, receiver).Succeeded(),
		"ordinary formula has no applicator variables");
	receiver->GetVariableVWM("SpellDef")->SetBaseValue(.5f);
	run("receiver.vSpellDef", .5f);
	run("cCombat", .375f);
}

bool RunPrimeWorldLinuxApplicatorFormulaProbe()
{
	const NDb::Ptr<NDb::SessionRoot> saved = NDb::SessionRoot::GetRoot();
	NDb::AILogicParameters* ai = new NDb::AILogicParameters;
	ai->abilityAndDamageScalingParams.damageScaleStatLeft = 50;
	ai->abilityAndDamageScalingParams.damageScaleStatRight = 160;
	NDb::SessionLogicRoot* logic = new NDb::SessionLogicRoot; logic->aiLogic = ai;
	NDb::SessionRoot* root = new NDb::SessionRoot; root->logicRoot = logic;
	NDb::SessionRoot::InitRoot(root);
	Checks check;
	{
		NDb::Terrain* terrain = new NDb::Terrain; terrain->elemXCount = 4; terrain->elemYCount = 3; terrain->tilesPerElement = 4;
		NDb::AdvMap* map = new NDb::AdvMap; map->terrain = terrain;
		NDb::AdvMapDescription* description = new NDb::AdvMapDescription; description->map = map;
		NDb::Ptr<NDb::AdvMapDescription> db = description;
		CObj<NCore::IWorldBase> owner = PFWorld::CreatePFWorld();
		PFWorld* world = dynamic_cast<PFWorld*>(owner.GetPtr());
		const bool loaded = world && world->LoadMap(db, 0, NCore::TPlayersStartInfo(), 0, false, PFResourcesCollection::TalentMap());
		check(loaded, "applicator mock map loads");
		if (loaded)
		{
			DamageFormulas(check, world); HealingFormulas(check, world); HealingFormulas(check, world, true);
			CooldownFormulas(check, world); DamageRouting(check, world);
			CriticalDamage(check, world);
			OutgoingDamage(check, world);
			PreDamage(check, world);
			DamageNotifications(check, world);
			CombatFormulaContext(check, world);
		}
	}
	NDb::SessionRoot::InitRoot(saved);
	std::printf("Applicator formulas: %d checks, %d failures\n", check.count, check.failed);
	return check.failed == 0;
}
