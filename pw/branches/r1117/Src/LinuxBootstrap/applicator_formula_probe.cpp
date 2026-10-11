#include "../System/systemStdAfx.h"
#include "../PF_GameLogic/StringExecutorBootstrap.h"
#include "applicator_formula_probe.h"
#include "formula_context.h"
#include "../PF_GameLogic/PFApplInstant.h"
#include "../PF_GameLogic/PFAbilityData.h"
#include "../PF_GameLogic/PFAbilityInstance.h"
#include "../PF_GameLogic/PFBaseUnitStates.h"
#include "../PF_GameLogic/PFBaseAttackData.h"
#include "../PF_GameLogic/PFHero.h"
#include "../PF_GameLogic/PFCreep.h"
#include "../PF_GameLogic/PFPredefinedUnitVariables.h"
#include "../PF_GameLogic/DBHeroesList.h"
#include "../PF_GameLogic/PFWorld.h"
#include "../PF_GameLogic/PFAIWorld.h"
#include "../PF_GameLogic/PFResourcesCollectionClient.h"
#include "../PF_GameLogic/DBAdvMap.h"
#include "../PF_GameLogic/DBSessionRoots.h"
#include "../Terrain/DBTerrain.h"
#include "../libdb/DbResourceCache.h"
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
static void CriticalDamage(Checks& check, PFWorld* world, bool deadSender = false)
{
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
	CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 20);
	// In-flight damage keeps its valid owner even after that owner dies.
	if (deadSender) sender->ChangeHealth(-1000);
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

/** Exercise configured reduction through actual damage, including delegation's current receiver. */
static void DefenceReduction(Checks& check, PFWorld* world, NDb::AILogicParameters* ai)
{
	const auto savedPhysical = ai->physicalDamageReduction, savedEnergy = ai->energyDamageReduction;
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
	CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 20);
	ai->energyDamageReduction.sString = "pMisc->GetVariable(\"damageDealed\")*.25";
	const auto run = [&](const char* formula, NDb::EApplicatorDamageType kind, float expected, bool bypass = false) {
		ai->physicalDamageReduction.sString = formula;
		NDb::DamageApplicator* raw = new NDb::DamageApplicator;
		raw->damage.sString = "100"; raw->damageType = kind;
		NDb::Ptr<NDb::DamageApplicator> db = raw;
		const Target target(receiver.GetPtr()); PFApplCreatePars pars(CObj<PFAbilityInstance>(), target);
		pars.pWorld = world; pars.pOwner = sender; pars.pDBAppl = db;
		CObj<Damage> app = new Damage(pars);
		receiver->SetHealth(1000); receiver->SetEnergy(200);
		check(app->Init(), "defence damage initialized");
		PFBaseUnit::DamageDesc desc; desc.pSender = sender; desc.amount = 100; desc.damageType = kind;
		desc.flags = 0; desc.damageMode = NDb::DAMAGEMODE_ZERO; desc.dontAttackBack = false;
		desc.pDealerApplicator = app; desc.ignoreDefences = true;
		if (bypass) app->SetDelegated(&desc);
		check(app->Run(), "defence damage completes");
		check(app->GetDamageDealed() == expected, formula);
		check(receiver->GetHealth() == 1000 - (kind == NDb::APPLICATORDAMAGETYPE_MANA ? 0 : expected), "defence resource delta");
	};
	run("pMisc->GetVariable(\"damageDealed\")*.5", NDb::APPLICATORDAMAGETYPE_MATERIAL, 50);
	run("pMisc->GetVariable(\"damageDealed\")*.5", NDb::APPLICATORDAMAGETYPE_NATIVE, 50);
	run("pMisc->GetVariable(\"damageDealed\")*.5", NDb::APPLICATORDAMAGETYPE_ENERGY, 25);
	run("missing", NDb::APPLICATORDAMAGETYPE_PURE, 100);
	run("missing", NDb::APPLICATORDAMAGETYPE_MANA, 100);
	run("missing", NDb::APPLICATORDAMAGETYPE_MATERIAL, 100, true);
	run("rStrength+sStrength", NDb::APPLICATORDAMAGETYPE_MATERIAL, 60);
	run("", NDb::APPLICATORDAMAGETYPE_MATERIAL, 100);
	for (const char* invalid : {"missing", "1/0", "1e39", "-1", "0"})
		run(invalid, NDb::APPLICATORDAMAGETYPE_MATERIAL, 0);
	ai->physicalDamageReduction.compiledString = "unsupported-bytecode";
	run("", NDb::APPLICATORDAMAGETYPE_MATERIAL, 0);
	ai->physicalDamageReduction = savedPhysical; ai->energyDamageReduction = savedEnergy;
	// The original dealer remains attached, but r getters belong to the redirected victim.
	CObj<PFBaseUnit> redirected = Unit(world, NDb::FACTION_BURN, 70);
	NDb::DamageApplicator* raw = new NDb::DamageApplicator;
	raw->damage.sString = "25"; raw->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
	NDb::Ptr<NDb::DamageApplicator> db = raw;
	const Target target(receiver.GetPtr()); PFApplCreatePars pars(CObj<PFAbilityInstance>(), target);
	pars.pWorld = world; pars.pOwner = sender; pars.pDBAppl = db;
	CObj<Damage> app = new Damage(pars); receiver->SetHealth(1000);
	check(app->Init() && app->Run(), "redirected damage dealer initialized");
	ai->physicalDamageReduction.sString = "rStrength+pMisc->GetVariable(\"damageDealed\")";
	PFBaseUnit::DamageDesc desc; desc.pSender = sender; desc.amount = 100;
	desc.damageType = NDb::APPLICATORDAMAGETYPE_MATERIAL; desc.flags = 0;
	desc.damageMode = NDb::DAMAGEMODE_ZERO; desc.dontAttackBack = false; desc.pDealerApplicator = app;
	check(redirected->OnDamage(desc) == 95 && redirected->GetHealth() == 905, "reduction uses redirected recipient with original dealer");
	check(!LinuxBootstrap::EvaluateDamageReductionNumericFormula("random(1,2)", sender, receiver, app).Succeeded(),
		"defence evaluation cannot advance the world RNG");
	ai->physicalDamageReduction = savedPhysical;
}

/** Construct real base-unit, creep, and hero recipients with the probe's DB stats. */
static CObj<PFBaseUnit> HealingReceiver(PFWorld* world, int kind)
{
	CObj<PFBaseUnit> seed = Unit(world, NDb::FACTION_FREEZE, 20);
	if (kind == 0)
		return seed;
	const Placement placement(CVec3(5, 5, 0), QNULL, CVec3(1, 1, 1));
	if (kind == 1)
	{
		NDb::Creature* raw = new NDb::Creature; raw->stats = seed->DbUnitDesc()->stats;
		NDb::Ptr<NDb::Creature> db = raw;
		return new PFBaseCreep(world, *db, NDb::FACTION_FREEZE, NDb::UNITTYPE_CREEP, -1, placement);
	}
	NDb::Hero* raw = new NDb::Hero; raw->stats = seed->DbUnitDesc()->stats;
	NDb::Ptr<NDb::Hero> db = raw;
	PFBaseHero::SpawnInfo spawn; spawn.pHero = db; spawn.placement = placement;
	return new PFBaseHero(world, spawn, NDb::UNITTYPE_HEROMALE, NDb::FACTION_FREEZE, NDb::FACTION_FREEZE);
}

/** Direct calls cover Windows signed/zero semantics; applicators keep their own admission checks. */
static void HealingModifiers(Checks& check, PFWorld* world)
{
	for (int kind = 0; kind != 3; ++kind)
	{
		CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_BURN, 40);
		CObj<PFBaseUnit> receiver = HealingReceiver(world, kind);
		check(kind != 1 || dynamic_cast<PFBaseCreep*>(receiver.GetPtr()), "healing real creep fixture");
		check(kind != 2 || dynamic_cast<PFBaseHero*>(receiver.GetPtr()), "healing real hero fixture");
		sender->GetVariableVWM(UnitVariables::szHealingScale)->SetBaseValue(9);
		sender->GetVariableVWM(UnitVariables::szHealingBias)->SetBaseValue(800);
		const auto run = [&](float amount, float scale, float bias, float expected,
			bool ignore = false, float start = 400.0f, bool forbidden = false) {
			receiver->SetHealth(start); receiver->SetEnergy(50);
			receiver->GetVariableVWM(UnitVariables::szHealingScale)->SetBaseValue(scale);
			receiver->GetVariableVWM(UnitVariables::szHealingBias)->SetBaseValue(bias);
			if (forbidden) receiver->AddFlag(NDb::UNITFLAG_FORBIDLIFERESTORE);
			const float actual = receiver->OnHeal(sender.GetPtr(), amount, ignore);
			check(actual == expected, "healing modifiers report actual signed delta");
			check(receiver->GetHealth() == start + expected, "healing modifiers change recipient health only");
			check(receiver->GetMana() == 50, "health modifiers do not change energy");
			check(sender->GetHealth() == 1000, "healing sender is not the recipient");
			if (forbidden) receiver->RemoveFlag(NDb::UNITFLAG_FORBIDLIFERESTORE);
		};
		run(100, 0, 0, 100);
		run(100, 0.5f, 10, 160);
		run(100, 0.5f, 10, 100, true);
		run(100, -0.5f, -30, 20);
		run(10, 0, -30, -20);
		run(10, -2, 0, -10);
		run(10, 0, -20, -10, false, 1000);
		run(100, 0.5f, 10, 50, false, 950);
		run(100, 0.5f, 10, 0, false, 1000);
		run(0, 0, 15, 15);
		run(0, 0, 15, 0, true);
		run(-25, 0, 0, -25);
		run(-25, 0.5f, 10, -25, true);
		run(100, 0.5f, 10, 0, false, 400, true);
		run(100, 0.5f, 10, 0, true, 400, true);
		for (const float invalid : {std::numeric_limits<float>::quiet_NaN(),
			std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
		{
			run(invalid, 0.5f, 10, 0);
			run(invalid, 0.5f, 10, 0, true);
		}
		run(std::numeric_limits<float>::max(), 1.0f, 10, 0);
		run(std::numeric_limits<float>::max(), -3.0f, 10, 0);
		run(std::numeric_limits<float>::max(), 1.0f, 10, 600, true);

		// Real variable modifiers must participate, not just their raw base values.
		receiver->SetHealth(400);
		ValueWithModifiers* scale = receiver->GetVariableVWM(UnitVariables::szHealingScale);
		scale->SetBaseValue(0.25f);
		const int modifier = scale->AddModifier(1, 0.25f, 71);
		receiver->GetVariableVWM(UnitVariables::szHealingBias)->SetBaseValue(10);
		check(receiver->OnHeal(sender.GetPtr(), 100) == 160, "healing reads current variable modifiers");
		scale->RemoveModifier(modifier);
		receiver->SetHealth(400);
		check(receiver->OnHeal(sender.GetPtr(), 100) == 135, "healing observes modifier removal");

		// Use real applicator dispatch as well as direct virtual callbacks.
		for (const bool ignore : {false, true})
		{
			receiver->SetHealth(400);
			scale->SetBaseValue(0.5f);
			NDb::HealApplicator* raw = new NDb::HealApplicator;
			raw->amount.sString = "100"; raw->enabled.sString = "true";
			raw->healTarget = NDb::HEALTARGET_HEALTH; raw->ignoreHealingMods = ignore;
			NDb::Ptr<NDb::HealApplicator> db = raw;
			const Target target(receiver.GetPtr());
			PFApplCreatePars pars(CObj<PFAbilityInstance>(), target);
			pars.pWorld = world; pars.pOwner = sender; pars.pDBAppl = db;
			CObj<Heal> app = new Heal(pars);
			const float expected = ignore ? 100 : 160;
			check(app->Init() && app->Run(), "modified heal applicator completes");
			check(receiver->GetHealth() == 400 + expected, "authored ignoreHealingMods reaches unit callback");
			check(app->GetAmountRestored() == expected && app->GetVariable("amountRestored") == expected,
				"modified heal reports actual restoration");
		}
		receiver->SetEnergy(50);
		check(receiver->OnHealEnergy(sender.GetPtr(), 20) == 20 && receiver->GetMana() == 70,
			"health scale and bias do not affect energy healing");

		// A damaging healing modifier must perform teardown, not just clamp life to zero.
		receiver->SetHealth(400);
		receiver->AssignTarget(sender.GetPtr(), true);
		check(receiver->GetCurrentTarget() == sender.GetPtr(), "fatal healing has a target to clean up");
		scale->SetBaseValue(0);
		receiver->GetVariableVWM(UnitVariables::szHealingBias)->SetBaseValue(-500);
		check(receiver->OnHeal(sender.GetPtr(), 10) == -400, "fatal healing reports post-lifecycle health delta");
		check(receiver->IsDead() && receiver->GetHealth() == 0, "fatal healing enters the death lifecycle");
		check(!receiver->GetCurrentTarget(), "fatal healing cleans up the attack target");
	}
}

/** Expose serialized isolation state without overriding hero combat callbacks. */
struct ProbeHero : PFBaseHero
{
	ProbeHero(PFWorld* world, const SpawnInfo& spawn, NDb::EFaction faction)
		: PFBaseHero(world, spawn, NDb::UNITTYPE_HEROMALE, faction, faction) {}
	void Isolate(bool value) { isolated = value; }
};

/** Build actual creep or hero objects; neither drain stat getter is overridden. */
static CObj<PFBaseUnit> DrainParticipant(PFWorld* world, NDb::EFaction faction, bool hero)
{
	CObj<PFBaseUnit> seed = Unit(world, faction, 20);
	const Placement placement(CVec3(5, 5, 0), QNULL, CVec3(1, 1, 1));
	if (!hero)
	{
		NDb::Creature* raw = new NDb::Creature; raw->stats = seed->DbUnitDesc()->stats;
		NDb::Ptr<NDb::Creature> db = raw;
		return new PFBaseCreep(world, *db, faction, NDb::UNITTYPE_CREEP, -1, placement);
	}
	NDb::Hero* raw = new NDb::Hero; raw->stats = seed->DbUnitDesc()->stats;
	NDb::Ptr<NDb::Hero> db = raw;
	PFBaseHero::SpawnInfo spawn; spawn.pHero = db; spawn.placement = placement;
	return new ProbeHero(world, spawn, faction);
}

/** Exercise private ApplyDrains through real OnDamage/applicator dispatch. */
static void DamageDrains(Checks& check, PFWorld* world)
{
	const int life = NDb::DAMAGEMODE_APPLYLIFEDRAINS;
	const int energy = NDb::DAMAGEMODE_APPLYENERGYDRAINS;
	for (const bool heroSender : {false, true})
	{
		const auto run = [&](int flags, float lifeStat, float energyStat, float amount,
			float gainedLife, float gainedEnergy, float burned,
			NDb::EUnitType receiverType = NDb::UNITTYPE_CREEP,
			bool forbidLife = false, bool forbidEnergy = false, bool deadSender = false,
			bool nullSender = false, float victimHealth = 400, float victimEnergy = 50,
			float senderHealth = 400, float senderEnergy = 50, bool invulnerable = false,
			bool applicator = false) {
			CObj<PFBaseUnit> sender = DrainParticipant(world, NDb::FACTION_FREEZE, heroSender);
			CObj<PFBaseUnit> receiver = DrainParticipant(world, NDb::FACTION_BURN, false);
			receiver->AddFlag(NDb::UNITFLAG_FORBIDREWARDS);
			check(heroSender ? dynamic_cast<PFBaseHero*>(sender.GetPtr()) != nullptr
				: dynamic_cast<PFBaseCreep*>(sender.GetPtr()) != nullptr, "drain real sender class");
			receiver->SetUnitType(receiverType);
			sender->SetHealth(senderHealth); sender->SetEnergy(senderEnergy);
			receiver->SetHealth(victimHealth); receiver->SetEnergy(victimEnergy);
			if (victimEnergy == 0) receiver->ChangeMana(-receiver->GetMana());
			sender->GetStat(NDb::STAT_LIFEDRAIN)->SetCoreValue(lifeStat);
			sender->GetStat(NDb::STAT_ENERGYDRAIN)->SetCoreValue(energyStat);
			if (forbidLife) sender->AddFlag(NDb::UNITFLAG_FORBIDLIFERESTORE);
			if (forbidEnergy) sender->AddFlag(NDb::UNITFLAG_FORBIDENERGYRESTORE);
			if (deadSender) sender->ChangeHealth(-1000);
			if (invulnerable) receiver->SetVulnerable(false);
			PFBaseUnit::DamageDesc desc;
			desc.pSender = nullSender ? nullptr : sender.GetPtr();
			desc.amount = amount; desc.damageType = NDb::APPLICATORDAMAGETYPE_PURE;
			desc.flags = 0; desc.damageMode = static_cast<NDb::EDamageMode>(flags);
			desc.dontAttackBack = true; desc.ignoreDefences = true; desc.pDealerApplicator = nullptr;
			float actual;
			if (!applicator)
				actual = receiver->OnDamage(desc);
			else
			{
				NDb::DamageApplicator* raw = new NDb::DamageApplicator;
				raw->damage.sString = NStr::StrFmt("%.9g", amount); raw->enabled.sString = "true";
				raw->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
				raw->damageMode = static_cast<NDb::EDamageMode>(flags | NDb::DAMAGEMODE_DONTATTACKBACK);
				NDb::Ptr<NDb::DamageApplicator> db = raw;
				const Target target(receiver.GetPtr());
				PFApplCreatePars pars(CObj<PFAbilityInstance>(), target);
				pars.pWorld = world; pars.pOwner = sender; pars.pDBAppl = db;
				CObj<Damage> app = new Damage(pars);
				check(app->Init() && app->Run(), "drain damage applicator completes");
				actual = app->GetDamageDealed();
			}
			const float dealt = invulnerable ? 0 : Min(victimHealth, Max(0.0f, amount));
			check(actual == dealt && receiver->GetHealth() == victimHealth - dealt,
				"drains preserve the victim's actual damage amount");
			check(sender->GetHealth() == (deadSender ? 0 : senderHealth) + gainedLife,
				"life drain changes sender by the capped flat stat amount");
			check(sender->GetMana() == senderEnergy + gainedEnergy, "energy drain restores only permitted actual burn");
			check(receiver->GetMana() == victimEnergy - burned, "energy drain burns victim independently of sender capacity");
		};
		run(0, 30, 20, 100, 0, 0, 0);
		run(life, 30, 20, 100, 30, 0, 0);
		run(energy, 30, 20, 100, 0, 20, 20);
		run(life | energy, 30, 20, 100, 30, 20, 20);
		run(life | energy, 300, 200, 10, 10, 10, 10);
		run(life | energy, 30, 200, 100, 30, 50, 50);
		run(life | energy, 0, 0, 100, 0, 0, 0);
		run(life | energy, -30, -20, 100, 0, 0, 0);
		run(life | energy, 30, 20, 0, 0, 0, 0);
		run(life | energy, 30, 20, -5, 0, 0, 0);
		run(life | energy, 30, 20, 100, 0, 20, 20, NDb::UNITTYPE_CREEP, true);
		run(life | energy, 30, 20, 100, 30, 0, 20, NDb::UNITTYPE_CREEP, false, true);
		run(life | energy, 30, 20, 100, 0, 0, 20, NDb::UNITTYPE_CREEP, true, true);
		for (const auto type : {NDb::UNITTYPE_BUILDING, NDb::UNITTYPE_MAINBUILDING, NDb::UNITTYPE_TOWER})
			run(life | energy, 30, 20, 100, 0, 0, 0, type);
		run(life | energy, 30, 20, 100, 0, 0, 0, NDb::UNITTYPE_CREEP, false, false, true);
		run(life | energy, 30, 20, 100, 0, 0, 0, NDb::UNITTYPE_CREEP, false, false, false, true);
		run(life | energy, 30, 20, 100, 10, 10, 10,
			NDb::UNITTYPE_CREEP, false, false, false, false, 10);
		run(life | energy, 30, 20, 100, 30, 7, 7,
			NDb::UNITTYPE_CREEP, false, false, false, false, 400, 7);
		run(life | energy, 30, 20, 100, 30, 0, 0,
			NDb::UNITTYPE_CREEP, false, false, false, false, 400, 0);
		run(life | energy, 30, 20, 100, 5, 5, 20,
			NDb::UNITTYPE_CREEP, false, false, false, false, 400, 50, 995, 195);
		run(life | energy, 30, 20, 100, 0, 0, 20,
			NDb::UNITTYPE_CREEP, false, false, false, false, 400, 50, 1000, 200);
		run(life | energy, 30, 20, 100, 0, 0, 0,
			NDb::UNITTYPE_CREEP, false, false, false, false, 400, 50, 400, 50, true);
		run(life | energy, 30, 20, 100, 30, 20, 20,
			NDb::UNITTYPE_CREEP, false, false, false, false, 400, 50, 400, 50, false, true);
	}
}

/** Add only after both independent chunks: life drain uses the sender's healing modifiers. */
static void HealingDrainComposition(Checks& check, PFWorld* world)
{
	CObj<PFBaseUnit> sender = DrainParticipant(world, NDb::FACTION_FREEZE, true);
	CObj<PFBaseUnit> receiver = DrainParticipant(world, NDb::FACTION_BURN, false);
	sender->SetHealth(400); sender->SetEnergy(50);
	receiver->SetHealth(400); receiver->SetEnergy(50);
	sender->GetStat(NDb::STAT_LIFEDRAIN)->SetCoreValue(20);
	sender->GetStat(NDb::STAT_ENERGYDRAIN)->SetCoreValue(10);
	sender->GetVariableVWM(UnitVariables::szHealingScale)->SetBaseValue(0.5f);
	sender->GetVariableVWM(UnitVariables::szHealingBias)->SetBaseValue(5);
	// The victim's heal modifiers must not contaminate the sender's restoration.
	receiver->GetVariableVWM(UnitVariables::szHealingScale)->SetBaseValue(9);
	receiver->GetVariableVWM(UnitVariables::szHealingBias)->SetBaseValue(500);
	PFBaseUnit::DamageDesc desc;
	desc.pSender = sender; desc.amount = 100; desc.damageType = NDb::APPLICATORDAMAGETYPE_PURE;
	desc.flags = 0; desc.dontAttackBack = true; desc.ignoreDefences = true; desc.pDealerApplicator = nullptr;
	desc.damageMode = static_cast<NDb::EDamageMode>(NDb::DAMAGEMODE_APPLYLIFEDRAINS | NDb::DAMAGEMODE_APPLYENERGYDRAINS);
	check(receiver->OnDamage(desc) == 100 && receiver->GetHealth() == 300, "modified drain preserves actual damage");
	check(sender->GetHealth() == 435, "life drain uses recipient scale and bias through real hero healing");
	check(receiver->GetMana() == 40 && sender->GetMana() == 60, "life modifiers do not modify energy drain");
}

/** Observe the real incoming event and supply controlled numeric/metadata changes. */
struct IncomingListener : CObjectBase, PFBaseUnitEventListener
{
	OBJECT_BASIC_METHODS(IncomingListener);
public:
	int calls = 0, lastHits = 0;
	float replacement = 7, initial = 0;
	unsigned int OnEvent(const PFBaseUnitEvent* event) override
	{
		if (event->GetType() == NDb::BASEUNITEVENT_LASTHIT) ++lastHits;
		if (const auto* damage = dynamic_cast<const PFBaseUnitDamageEvent*>(event))
		{
			++calls; initial = damage->damage2Deal;
			damage->damage2Deal = replacement;
			damage->pDesc->delegatedDamage = 3;
		}
		return 0;
	}
};

/** Invalid direct calls cannot be rescued by listeners or turn into lethal damage/drains. */
static void IncomingBoundaries(Checks& check, PFWorld* world, NDb::AILogicParameters* ai)
{
	const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
	const auto run = [&](float amount, float replacement, bool invulnerable = false, bool dead = false) {
		CObj<PFBaseUnit> sender = DrainParticipant(world, NDb::FACTION_FREEZE, true);
		CObj<PFBaseUnit> receiver = DrainParticipant(world, NDb::FACTION_BURN, false);
		sender->SetHealth(400); sender->SetEnergy(50); receiver->SetHealth(400); receiver->SetEnergy(50);
		sender->GetStat(NDb::STAT_LIFEDRAIN)->SetCoreValue(30);
		sender->GetStat(NDb::STAT_ENERGYDRAIN)->SetCoreValue(20);
		receiver->AddFlag(NDb::UNITFLAG_FORBIDREWARDS);
		if (dead) receiver->ChangeHealth(-1000);
		if (invulnerable) receiver->SetVulnerable(false);
		CObj<IncomingListener> listener = new IncomingListener; listener->replacement = replacement;
		receiver->AddEventListener(listener.GetPtr());
		PFBaseUnit::DamageDesc desc; desc.pSender = sender; desc.amount = amount;
		desc.damageType = NDb::APPLICATORDAMAGETYPE_PURE; desc.flags = 0; desc.dontAttackBack = true;
		desc.ignoreDefences = true; desc.pDealerApplicator = nullptr;
		desc.damageMode = static_cast<NDb::EDamageMode>(NDb::DAMAGEMODE_APPLYLIFEDRAINS | NDb::DAMAGEMODE_APPLYENERGYDRAINS);
		const bool event = std::isfinite(amount) && !invulnerable;
		const float expected = event && !dead && std::isfinite(replacement) ? Min(400.0f, Max(0.0f, replacement)) : 0;
		check(receiver->OnDamage(desc) == expected, "incoming damage reports finite actual amount");
		check(listener->calls == (event ? 1 : 0), "direct numeric validation precedes incoming listeners");
		if (event) check(listener->initial == amount, "incoming listener sees unclamped finite original amount");
		check(receiver->GetHealth() == (dead ? 0 : 400) - expected, "incoming invalid result cannot kill");
		check(sender->GetHealth() == 400 + Min(30.0f, expected), "incoming amount caps life drain");
		check(receiver->GetMana() == 50 - Min(20.0f, expected), "incoming amount caps victim energy drain");
		check(sender->GetMana() == 50 + Min(20.0f, expected), "incoming amount caps restored energy");
		check(listener->lastHits == (expected == 400 ? 1 : 0), "only finite lethal damage emits last hit");
		check(desc.delegatedDamage == (event ? 3 : 0), "incoming delegated metadata reaches caller");
		receiver->RemoveEventListener(listener.GetPtr());
	};
	for (float bad : {nan, inf, -inf}) { run(bad, 7); run(100, bad); }
	for (float value : {-5.0f, 0.0f, 7.0f, 75.0f, std::numeric_limits<float>::max()}) run(100, value);
	run(std::numeric_limits<float>::max(), 7); run(0, 7); run(-5, 7);
	run(100, 7, true); run(100, 7, false, true);
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40), receiver = Unit(world, NDb::FACTION_BURN, 20);
	CObj<IncomingListener> listener = new IncomingListener; receiver->AddEventListener(listener.GetPtr());
	NDb::DamageApplicator* raw = new NDb::DamageApplicator; raw->damage.sString = "25"; raw->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
	NDb::Ptr<NDb::DamageApplicator> db = raw;
	const Target target(receiver.GetPtr()); PFApplCreatePars pars(CObj<PFAbilityInstance>(), target);
	pars.pWorld = world; pars.pOwner = sender; pars.pDBAppl = db;
	CObj<Damage> app = new Damage(pars);
	check(app->Init() && app->Run(), "incoming metadata actual applicator completes");
	check(app->GetDamageDealed() == 7 && app->GetVariable("damageDealed") == 10, "applicator reports listener delegated total");
	CObj<OutgoingListener> outgoing = new OutgoingListener; sender->AddEventListener(outgoing.GetPtr());
	for (float amount : {0.0f, -5.0f})
	{
		outgoing->replacement = amount; listener->calls = 0; receiver->SetHealth(1000);
		app = new Damage(pars);
		check(app->Init() && app->Run(), "finite outgoing cancellation completes");
		check(listener->calls == 1 && listener->initial == amount, "finite outgoing result reaches incoming hooks");
		check(app->GetDamageDealed() == 7 && receiver->GetHealth() == 993, "incoming modifier can adjust finite outgoing zero");
	}
	sender->RemoveEventListener(outgoing.GetPtr());
	const auto saved = ai->physicalDamageReduction;
	ai->physicalDamageReduction.sString = "missing";
	raw->damageType = NDb::APPLICATORDAMAGETYPE_MATERIAL;
	receiver->SetHealth(1000); listener->calls = 0;
	app = new Damage(pars);
	check(app->Init() && app->Run(), "invalid defence application completes");
	check(app->GetDamageDealed() == 0 && receiver->GetHealth() == 1000, "incoming modifier cannot revive an invalid defence formula");
	check(listener->calls == 0, "invalid defence stops before incoming events");
	ai->physicalDamageReduction = saved;
	receiver->RemoveEventListener(listener.GetPtr());
}

/** Direct energy callbacks are independently guarded, not only their applicator callers. */
static void EnergyBoundaries(Checks& check, PFWorld* world)
{
	for (int kind = 0; kind < 3; ++kind)
	for (float amount : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
		-std::numeric_limits<float>::infinity(), -1.0f, 0.0f, 7.0f, 500.0f, std::numeric_limits<float>::max()})
	for (bool forbidden : {false, true})
	{
		CObj<PFBaseUnit> receiver = HealingReceiver(world, kind);
		receiver->SetHealth(400); receiver->SetEnergy(50);
		if (forbidden) receiver->AddFlag(NDb::UNITFLAG_FORBIDENERGYRESTORE);
		const float admitted = std::isfinite(amount) && amount > 0 ? amount : 0;
		const float burned = Min(admitted, 50.0f), healed = forbidden ? 0 : Min(admitted, 150.0f);
		check(receiver->OnBurnEnergy(nullptr, amount) == burned && receiver->GetMana() == 50-burned, "finite direct energy burn");
		receiver->SetEnergy(50);
		check(receiver->OnHealEnergy(nullptr, amount) == healed && receiver->GetMana() == 50+healed, "finite direct energy restoration");
		check(receiver->GetHealth() == 400, "direct energy callbacks preserve health");
	}
}

/** Initialize every caller-owned descriptor field, as production applicators do. */
static PFBaseUnitDamageDesc DirectDamage()
{
	PFBaseUnitDamageDesc desc; desc.amount = 0; desc.damageType = NDb::APPLICATORDAMAGETYPE_PURE;
	desc.flags = 0; desc.damageMode = NDb::DAMAGEMODE_ZERO;
	desc.dontAttackBack = false; desc.pDealerApplicator = nullptr;
	return desc;
}

/** Hero damage masks use the existing predicate and both pre/incoming callbacks. */
static void HeroDamageModifiers(Checks& check, PFWorld* world)
{
	for (bool matching : {false, true})
	for (bool missingSender : {false, true})
	for (float scale : {0.0f, 0.5f, 2.0f, -1.0f,
		std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
	{
		CObj<PFBaseUnit> sender = DrainParticipant(world, NDb::FACTION_FREEZE, false);
		CObj<PFBaseUnit> receiver = DrainParticipant(world, NDb::FACTION_BURN, true);
		PFBaseHero* hero = dynamic_cast<PFBaseHero*>(receiver.GetPtr());
		hero->SetAdditionalDamageParam(scale, NDb::ESpellTarget(NDb::SPELLTARGET_ENEMY |
			(matching ? NDb::SPELLTARGET_CREEP : NDb::SPELLTARGET_HEROMALE)));
		PFBaseUnitDamageDesc desc = DirectDamage(); desc.pSender = missingSender ? nullptr : sender.GetPtr();
		desc.amount = 20; desc.damageType = NDb::APPLICATORDAMAGETYPE_PURE;
		const bool applies = matching && !missingSender;
		const float expectedBefore = applies ? 20 * scale : 20;
		const float before = hero->OnBeforeDamage(desc);
		check(std::isnan(expectedBefore) ? std::isnan(before) : before == expectedBefore,
			"hero pre-damage applies only the authored sender mask");
		const float expected = std::isfinite(expectedBefore) ? Max(0.0f, expectedBefore) : 0;
		check(hero->OnDamage(desc) == expected, "hero incoming modifier produces finite actual damage");
		check(hero->GetHealth() == 1000 - expected, "hero incoming modifier changes actual health");
		check(desc.amount == 20, "hero scaling does not mutate caller damage amount");
	}
	CObj<PFBaseUnit> receiver = DrainParticipant(world, NDb::FACTION_BURN, true);
	ProbeHero* hero = static_cast<ProbeHero*>(receiver.GetPtr());
	PFBaseUnitDamageDesc desc = DirectDamage(); desc.amount = 25;
	hero->Isolate(true);
	check(hero->OnDamage(desc) == 0 && hero->GetHealth() == 1000, "isolated hero cannot receive damage");
	hero->Isolate(false);
	check(hero->OnDamage(desc) == 25 && hero->GetHealth() == 975, "isolation removal restores damage");
}

/** Observe the actual behavior hook, including its post-health/pre-death timing. */
struct DamageBehaviour : PFBaseBehaviour
{
	int calls = 0, deaths = 0, stops = 0;
	float observedHealth = -1;
	PFBaseUnitDamageDesc observed;
	explicit DamageBehaviour(PFBaseMovingUnit* unit) : PFBaseBehaviour(unit) {}
	void OnDamage(const PFBaseUnitDamageDesc& desc) override
	{
		++calls; observed = desc; observedHealth = pUnit->GetHealth();
	}
	void OnUnitDie(CPtr<PFBaseUnit>) override { ++deaths; }
	void OnStop() override { ++stops; }
};

static void BehaviourDamage(Checks& check, PFWorld* world)
{
	for (bool enabled : {false, true})
	for (bool automaticOnly : {false, true})
	for (float amount : {0.0f, 20.0f, 2000.0f})
	{
		CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 20);
		CObj<PFBaseUnit> receiver = DrainParticipant(world, NDb::FACTION_BURN, false);
		NDb::UnitTargetingParameters* raw = new NDb::UnitTargetingParameters;
		raw->onlyAutoTargeting = automaticOnly;
		receiver->OverrideTargetingParams(raw);
		CObj<DamageBehaviour> behavior = new DamageBehaviour(static_cast<PFBaseMovingUnit*>(receiver.GetPtr()));
		receiver->AddBehaviourOnTop(behavior.GetPtr());
		if (!enabled) behavior->Disable();
		PFBaseUnitDamageDesc desc = DirectDamage(); desc.pSender = sender; desc.amount = amount;
		desc.damageType = NDb::APPLICATORDAMAGETYPE_PURE; desc.dontAttackBack = true;
		check(receiver->OnDamage(desc) == Min(amount, 1000.0f), "behavior damage preserves capped damage");
		const int expectedCalls = enabled && !automaticOnly ? 1 : 0;
		check(behavior->calls == expectedCalls, "damage notifies enabled nonautomatic behavior once");
		if (expectedCalls)
		{
			check(behavior->observedHealth == Max(0.0f, 1000-amount), "behavior runs after health changes");
			check(behavior->observed.pSender == sender.GetPtr() && behavior->observed.amount == amount
				&& behavior->observed.dontAttackBack, "behavior retains original combat metadata");
		}
		check(behavior->deaths == (amount >= 1000 ? 1 : 0), "death behavior remains after damage callback");
	}
}

/** Last-hit listeners observe any death-prevention restoration before cleanup. */
struct LastHitObserver : CObjectBase, PFBaseUnitEventListener
{
	OBJECT_BASIC_METHODS(LastHitObserver);
public:
	PFBaseUnit* unit = nullptr;
	int calls = 0;
	float observedHealth = -1;
	bool rescue = false;
	unsigned int OnEvent(const PFBaseUnitEvent* event) override
	{
		if (event->GetType() == NDb::BASEUNITEVENT_LASTHIT)
		{
			++calls; observedHealth = unit->GetHealth();
			if (rescue) unit->OnHeal(nullptr, 25, true);
		}
		return 0;
	}
};

static void DeferredDeath(Checks& check, PFWorld* world)
{
	for (bool hero : {false, true})
	for (int flag : {0, int(NDb::UNITFLAG_FORBIDDEATH), int(NDb::UNITFLAG_LIVEAFTERDEATH)})
	for (bool rescue : {false, true})
	{
		CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 20);
		CObj<PFBaseUnit> receiver = DrainParticipant(world, NDb::FACTION_BURN, hero);
		receiver->AddFlag(NDb::UNITFLAG_FORBIDREWARDS);
		if (flag) receiver->AddFlag(NDb::EUnitFlag(flag));
		receiver->SetHealth(50); receiver->AssignTarget(sender.GetPtr(), true);
		CObj<DamageBehaviour> behavior = new DamageBehaviour(static_cast<PFBaseMovingUnit*>(receiver.GetPtr()));
		receiver->AddBehaviourOnTop(behavior.GetPtr());
		CObj<LastHitObserver> listener = new LastHitObserver; listener->unit = receiver.GetPtr(); listener->rescue = rescue;
		receiver->AddEventListener(listener.GetPtr());
		PFBaseUnitDamageDesc desc = DirectDamage(); desc.pSender = sender; desc.amount = 100;
		desc.damageType = NDb::APPLICATORDAMAGETYPE_PURE;
		check(receiver->OnDamage(desc) == 50, "lethal damage retains the original health cap");
		check(listener->calls == 1 && listener->observedHealth == (flag ? 1 : 0), "last-hit observes restored survival health");
		const bool dies = !flag && !rescue, deferred = flag == NDb::UNITFLAG_LIVEAFTERDEATH;
		check(receiver->GetHealth() == (dies ? 0 : (flag ? 1 : 0) + (rescue ? 25 : 0)), "last-hit rescue and deferred death retain live pools");
		check(behavior->deaths == (dies || deferred ? 1 : 0), "only real and deferred death notify death behavior");
		check(receiver->HasBehaviour() == !dies, "deferred death retains behavior ownership");
		check(behavior->stops == (dies ? 1 : 0), "final death stops behavior but deferred death does not");
		check(bool(receiver->GetCurrentTarget()) == !(dies || deferred), "deferred death cancels combat target");
		if (hero && !dies)
			check(static_cast<PFBaseHero*>(receiver.GetPtr())->GetTimeToRespawn() < 0, "surviving hero does not start respawn");
		receiver->RemoveEventListener(listener.GetPtr());
		if (deferred)
		{
			receiver->RemoveFlag(NDb::UNITFLAG_LIVEAFTERDEATH);
			receiver->KillUnit(sender.GetPtr(), PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
			check(receiver->IsDead() && !receiver->HasBehaviour(), "deferred unit can finish normal death later");
		}
	}
}

/** Give mock resources real distinct DB identities without a database cache. */
struct ProbeResourceCache : NDb::DbResourceCache
{
	static void MarkLoaded(NDb::DbResource* resource) { SetResourceLoaded(resource, true); }
};
struct IdentifiedDamage : NDb::DamageApplicator
{
	explicit IdentifiedDamage(const char* path)
	{
		SetDbId(NDb::DBID(path)); ProbeResourceCache::MarkLoaded(this);
	}
};

static void DamageLimiter(Checks& check, PFWorld* world)
{
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 20);
	CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 20);
	const Target target(receiver.GetPtr());
	const auto make = [&](const char* identity, float limit) {
		IdentifiedDamage* raw = new IdentifiedDamage(identity);
		raw->damage.sString = "10"; raw->damageType = NDb::APPLICATORDAMAGETYPE_PURE; raw->timeLimiter = limit;
		NDb::Ptr<NDb::DamageApplicator> db = raw;
		PFApplCreatePars pars(CObj<PFAbilityInstance>(), target);
		pars.pWorld = world; pars.pOwner = sender; pars.pDBAppl = db;
		return CObj<Damage>(new Damage(pars));
	};
	const auto hit = [&](const char* identity, float limit, float expected) {
		CObj<Damage> app = make(identity, limit);
		const float before = receiver->GetHealth();
		check(app->Init() && app->Run(), "time-limited damage completes");
		check(app->GetDamageDealed() == expected && receiver->GetHealth() == before-expected,
			"time limiter gates the same DB identity before resource mutation");
	};
	hit("/probe/limited-a.xdb", 1, 10); hit("/probe/limited-a.xdb", 1, 0);
	receiver->StepLastAttackData(1);
	hit("/probe/limited-a.xdb", 1, 0); // The Windows comparison includes equality.
	receiver->StepLastAttackData(0.125f);
	hit("/probe/limited-a.xdb", 1, 10); hit("/probe/limited-a.xdb", 1, 0);
	hit("/probe/limited-b.xdb", 1, 10); hit("/probe/limited-a.xdb", 1, 10);
	hit("/probe/unlimited.xdb", -1, 10); hit("/probe/unlimited.xdb", -1, 10);
	hit("/probe/zero.xdb", 0, 10); hit("/probe/zero.xdb", 0, 0);
	for (float elapsed : {-1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
		receiver->StepLastAttackData(elapsed);
	hit("/probe/zero.xdb", 0, 0);
	receiver->StepLastAttackData(0.125f);
	hit("/probe/zero.xdb", 0, 10);
	receiver->Step(0.125f);
	hit("/probe/zero.xdb", 0, 10);
	hit("/probe/invalid.xdb", std::numeric_limits<float>::quiet_NaN(), 0);
	hit("/probe/invalid.xdb", 1, 10);
	hit("/probe/infinite.xdb", std::numeric_limits<float>::infinity(), 0);
	hit("/probe/infinite.xdb", 1, 10);
	receiver->StepLastAttackData(std::numeric_limits<float>::max());
	receiver->StepLastAttackData(std::numeric_limits<float>::max());
	hit("/probe/infinite.xdb", std::numeric_limits<float>::max(), 0);
	hit("/probe/infinite.xdb", 1, 10);
	// Direct callbacks without a damage applicator are independent of the limiter.
	PFBaseUnitDamageDesc direct = DirectDamage(); direct.pSender = sender; direct.amount = 10;
	check(receiver->OnDamage(direct) == 10 && receiver->OnDamage(direct) == 10, "direct damage has no DB limiter");
}

#include "event_dispatch_tests.inc"

/** Observe scheduling through the existing state interface, not a replacement FSM. */
struct SchedulingState : PFBaseUnitState
{
	int entries = 0, steps = 0, leaves = 0;
	float elapsed = 0;
	bool complete = false;
	explicit SchedulingState(PFBaseUnit* unit) : PFBaseUnitState(unit) {}
	void OnEnter() override { ++entries; }
	bool OnStep(float dt) override { ++steps; elapsed += dt; return complete; }
	void OnLeave() override { ++leaves; }
};

static void UnitStateScheduling(Checks& check, PFWorld* world)
{
	CObj<PFBaseUnit> unit = Unit(world, NDb::FACTION_FREEZE, 20);
	CObj<SchedulingState> state = new SchedulingState(unit.GetPtr());
	unit->EnqueueState(state.GetPtr(), true);
	unit->Step(0.25f);
	check(state->entries == 1 && state->steps == 1 && state->elapsed == 0,
		"queued unit state enters once at the existing zero-time boundary");
	unit->Step(0.25f);
	check(state->steps == 2 && state->elapsed == 0.25f, "unit state advances once per simulation step");
	unit->PauseFSM(true); unit->Step(0.25f);
	check(state->steps == 2, "paused unit state does not advance");
	unit->PauseFSM(false); state->complete = true; unit->Step(0.25f);
	check(state->leaves == 1 && unit->IsIdle(), "completed state leaves once and releases current order");
	state = new SchedulingState(unit.GetPtr()); unit->PushState(state.GetPtr()); unit->Step(0.25f);
	check(state->entries == 1 && state->elapsed == 0.25f, "directly pushed state uses the same unit schedule");
	unit->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS); unit->Step(0.25f);
	check(state->leaves == 1 && state->steps == 1 && unit->IsIdle(), "dead unit clears and does not step its order");

	CObj<PFBaseUnit> moving = DrainParticipant(world, NDb::FACTION_FREEZE, false);
	PFBaseMovingUnit* creep = static_cast<PFBaseMovingUnit*>(moving.GetPtr());
	creep->SetUnitSpeed(4);
	creep->EnqueueState(new PFBaseUnitMoveToState(creep, CVec2(12, 5), 0.5f), true);
	for (int i = 0; i < 80; ++i) creep->Step(0.1f);
	check(creep->GetPosition().x >= 11.5f && creep->GetPosition().x <= 12.01f,
		"authored move state reaches its target using native movement");
	check(creep->IsIdle(), "arrived movement order leaves its state");
	for (float speed : {0.0f, 0.0001f})
	{
		creep->TeleportTo(CVec2(5, 5), false, false);
		creep->SetMoveFlags(MOVE_FLAG_OVERRIDE_SPEED | MOVE_FLAG_NO_COLLIDE);
		creep->SetUnitSpeed(speed); creep->MoveTo(CVec2(12, 5));
		creep->Step(0.01f);
		check(creep->GetPosition().x < 5.001f, "zero or tiny movement speed never becomes a full-distance jump");
		creep->Stop(false);
	}
	creep->TeleportTo(CVec2(5, 5), false, false);
	creep->SetMoveFlags(MOVE_FLAG_NO_COLLIDE);
	creep->GetStat(NDb::STAT_MOVESPEED)->SetCoreValue(5);
	const float speed = creep->GetMoveSpeedMpS();
	creep->MoveTo(CVec2(12, 5));
	MovingUnit::UpdateMovements(world->GetAIWorld(), nullptr, world->GetTileMap(), 0.1f);
	check(speed > 0 && fabs(creep->GetPosition().x - (5+speed*0.1f)) < 0.001f,
		"ordinary movement consumes the live engine move-speed stat");
}

/** Real base attacks must report authored results, including intentional zero damage. */
static void BaseAttackExecution(Checks& check, PFWorld* world)
{
	for (const char* expression : {"13", "0", "-1", "missing", "0.125"})
	for (bool enabled : {false, true})
	for (bool cancelled : {false, true})
	{
		CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
		CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 20);
		NDb::DamageApplicator* damage = new NDb::DamageApplicator;
		damage->damage.sString = expression; damage->enabled.sString = enabled ? "true" : "false";
		damage->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
		NDb::BaseAttack* raw = new NDb::BaseAttack;
		raw->useRange.sString = "20"; raw->manaCost.sString = "0"; raw->applicators.push_back(damage);
		CObj<PFBaseAttackData> attack = new PFBaseAttackData(sender.GetPtr(), raw);
		sender->ReplaceBaseAttack(attack, false); sender->AssignTarget(receiver.GetPtr(), true);
		CObj<IncomingListener> listener = new IncomingListener;
		if (cancelled) { listener->replacement = 0; receiver->AddEventListener(listener.GetPtr()); }
		const float authored = nstl::string(expression) == "13" ? 13 : nstl::string(expression) == "0.125" ? 0.125f : 0;
		const float expected = enabled && !cancelled ? authored : 0;
		check(sender->DoAttack(false), "actual base attack starts through the engine instance");
		check(receiver->GetHealth() == 1000-expected, "base attack uses only authored damage without synthesis");
		check(!cancelled || listener->calls == (enabled && authored > 0 ? 1 : 0),
			"cancelled authored attack does not emit a second fallback damage event");
		if (cancelled) receiver->RemoveEventListener(listener.GetPtr());
	}
}

#include "behaviour_tests/cleanup_tests.inc"
#include "behaviour_tests/retaliation_tests.inc"
#include "behaviour_tests/hero_tests.inc"
#include "behaviour_tests/dispatch_tests.inc"
#include "base_use_tests.inc"
#include "hero_cast_tests.inc"
#include "consumable_state_tests.inc"
#include "hero_follow_lifecycle_tests.inc"
#include "hero_follow_combat_tests.inc"
#include "hero_hold_tests.inc"
#include "hero_suspend_tests.inc"
#include "hero_interaction_approach_tests.inc"
#include "hero_interaction_action_tests.inc"
#include "hero_use_unit_tests.inc"

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
			CriticalDamage(check, world); CriticalDamage(check, world, true);
			OutgoingDamage(check, world);
			PreDamage(check, world);
			DamageNotifications(check, world);
			CombatFormulaContext(check, world);
			DefenceReduction(check, world, ai);
			HealingModifiers(check, world);
			DamageDrains(check, world); HealingDrainComposition(check, world);
			IncomingBoundaries(check, world, ai); EnergyBoundaries(check, world);
			HeroDamageModifiers(check, world);
			BehaviourDamage(check, world);
			DeferredDeath(check, world);
			DamageLimiter(check, world);
			EventOrderingB7Regression(check, world);
			EventReentrancyB7Regression(check, world);
			UnitStateScheduling(check, world);
			BaseAttackExecution(check, world);
			BehaviourCleanupB8A(check, world); BehaviourAllyB8A(check, world);
			BehaviourRetaliationB8B(check, world); BehaviourMountedRetaliationB8B(check, world);
			BehaviourDamageDispatchB8B(check, world);
			BehaviourHeroDamageB8C(check, world); BehaviourHeroMountedB8C(check, world);
			BehaviourHeroScreamsB8C(check, world); BehaviourHeroTargetB8C(check, world);
			BehaviourTargetDispatchB8(check, world); BehaviourHeroDispatchB8(check, world);
			BehaviourHeroConstructionB8(check, world);
			BaseUseRangeB9(check, world);
			BaseUseLifetimeB9(check, world);
			HeroUseApproachB9(check, world);
			HeroTalentLifecycleB9(check, world);
			HeroConsumableLifecycleB9(check, world);
			HeroFollowLifecycleB9(check, world);
			HeroFollowCombatB9(check, world);
			HeroHoldB9(check, world);
			HeroSuspendB9(check, world);
			HeroInteractionApproachB9(check, world);
			HeroInteractionActionB9(check, world);
			HeroUseUnitB9(check, world);
		}
	}
	NDb::SessionRoot::InitRoot(saved);
	std::printf("Applicator formulas: %d checks, %d failures\n", check.count, check.failed);
	return check.failed == 0;
}
