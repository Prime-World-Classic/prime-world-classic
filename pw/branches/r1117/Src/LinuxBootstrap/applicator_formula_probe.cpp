#include "../System/systemStdAfx.h"
#include "../PF_GameLogic/StringExecutorBootstrap.h"
#include "applicator_formula_probe.h"
#include "../PF_GameLogic/PFApplInstant.h"
#include "../PF_GameLogic/PFAbilityData.h"
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

/** Access the protected initializer without overriding gameplay or formula getters. */
struct ProbeUnit : PFBaseUnit
{
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
		PFApplCreatePars pars(CObj<PFAbilityInstance>(), Target(receiver.GetPtr()));
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
static void HealingFormulas(Checks& check, NWorld::PFWorld* world)
{
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
	CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_FREEZE, 20);
	const auto run = [&](const char* expression, NDb::EHealTarget pool, float restored,
		bool enabled = true, bool dead = false, bool forbidden = false) {
		NDb::HealApplicator* raw = new NDb::HealApplicator;
		raw->amount.sString = expression; raw->healTarget = pool; raw->enabled.sString = enabled ? "true" : "false";
		NDb::Ptr<NDb::HealApplicator> db = raw;
		PFApplCreatePars pars(CObj<PFAbilityInstance>(), Target(receiver.GetPtr()));
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
		if (loaded) { DamageFormulas(check, world); HealingFormulas(check, world); }
	}
	NDb::SessionRoot::InitRoot(saved);
	std::printf("Applicator formulas: %d checks, %d failures\n", check.count, check.failed);
	return check.failed == 0;
}
