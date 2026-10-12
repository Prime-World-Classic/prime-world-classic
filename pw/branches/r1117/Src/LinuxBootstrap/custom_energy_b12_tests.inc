#include "../Render/DBRender.h"

/** Exercise the real post-read rebinding callback without replacing serialized fields. */
struct CustomEnergyRestoreB12 : B10ActivationTimerRestore
{
	void DataChunk(chunk_id, void*, int, int) override {}
};

/** Real hero/resource descriptors, with explicit variables authored outside the engine. */
struct CustomEnergyFixtureB12
{
	NDb::Ptr<NDb::HeroResource> resource;
	NDb::Ptr<NDb::Hero> db;
	CObj<B9CastHero> hero;
	explicit CustomEnergyFixtureB12(PFWorld* world, int invalid = -1)
	{
		CObj<B9CastHero> seed = B9MaleHero(world, NDb::FACTION_FREEZE);
		NDb::HeroResource* rawResource = new NDb::HeroResource;
		rawResource->formulaNameValue = "UniquePower";
		rawResource->formulaNameMaximum = "UniqueMaximum";
		rawResource->formulaNameRegeneration = "UniqueRegeneration";
		if (invalid != 0) rawResource->overtipManaProgressMaterial = new NDb::UIBaseMaterial;
		if (invalid == 1) rawResource->formulaNameValue.clear();
		if (invalid == 2) rawResource->formulaNameMaximum.clear();
		if (invalid == 3) rawResource->formulaNameRegeneration.clear();
		resource = rawResource;
		NDb::Hero* raw = new NDb::Hero;
		raw->Assign(*static_cast<const NDb::Hero*>(seed->GetDbHero())); raw->uniqueResource = resource;
		db = raw;
		PFBaseHero::SpawnInfo spawn; spawn.pHero = db;
		spawn.placement = Placement(CVec3(5, 5, 0), QNULL, CVec3(1, 1, 1));
		hero = new B9CastHero(world, spawn, NDb::FACTION_FREEZE);
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
	}
	void Values(float current, float maximum, float regeneration)
	{
		hero->GetVariableVWM("UniquePower")->SetBaseValue(current);
		hero->GetVariableVWM("UniqueMaximum")->SetBaseValue(maximum);
		hero->GetVariableVWM("UniqueRegeneration")->SetBaseValue(regeneration);
	}
};

static void CustomEnergyB12(Checks& check, PFWorld* world)
{
	{
		CObj<B9CastHero> ordinary = B9MaleHero(world, NDb::FACTION_FREEZE);
		check(!ordinary->HasCustomEnergy() && ordinary->GetMana() == 200 &&
			ordinary->GetCustomEnergyValue() == 0, "ordinary heroes retain their original mana resource");
	}
	{
		CustomEnergyFixtureB12 f(world);
		check(f.hero->HasCustomEnergy() && f.hero->GetMana() == 0 && !f.hero->HasMana(),
			"authored unique resource replaces ordinary mana at real hero construction");
		for (auto stat : {NDb::STAT_ENERGY, NDb::STAT_ENERGYDRAIN, NDb::STAT_ENERGYREGENERATION, NDb::STAT_ENERGYREGENERATIONABSOLUTE})
		{
			f.hero->GetStat(stat)->SetBaseValue(500);
			check(f.hero->GetStatValue(stat) == 0, "original owned constraints disable all four ordinary energy stats");
		}
		check(f.hero->SearchVariableVWM("UniquePower") && f.hero->SearchVariableVWM("UniqueMaximum") &&
			f.hero->SearchVariableVWM("UniqueRegeneration"), "custom resource variables are bound during initialization");
		f.Values(10, 100, 4);
		check(f.hero->GetCustomEnergyValue() == 10 && f.hero->GetCustomEnergyMaximum() == 100 &&
			f.hero->GetCustomEnergyRegeneration() == 4, "resource queries read the actual named variable values");
		f.hero->Simulate(.5f);
		check(f.hero->GetCustomEnergyValue() == 12 && f.hero->GetMana() == 0,
			"hero stepping regenerates custom energy independently from disabled mana");
		f.Values(99, 100, 4); f.hero->Simulate(1);
		check(f.hero->GetCustomEnergyValue() == 100, "positive regeneration clamps at the current maximum");
		f.Values(3, 100, -4); f.hero->Simulate(1);
		check(f.hero->GetCustomEnergyValue() == 0, "signed custom resource drain clamps at zero");
		f.Values(-8, -10, -4);
		check(f.hero->GetCustomEnergyValue() == 0 && f.hero->GetCustomEnergyMaximum() == 0,
			"new value and maximum variables retain original lower-limit constraints");
		f.Values(20, 100, 4);
		const int count = f.hero->SearchVariableVWM("UniquePower")->GetModifiers().size();
		CustomEnergyRestoreB12 restore;
		f.hero->PFBaseHero::operator&(restore); f.hero->PFBaseHero::operator&(restore);
		check(f.hero->HasCustomEnergy() && f.hero->GetCustomEnergyValue() == 20 &&
			f.hero->SearchVariableVWM("UniquePower")->GetModifiers().size() == count,
			"repeated post-read rebinding preserves values and does not duplicate constraint ownership");
		f.hero->ChangeHealth(-f.hero->GetHealth()); f.hero->Simulate(.1f);
		check(f.hero->GetCustomEnergyValue() == 20, "dead heroes do not regenerate their custom resource");
	}
	{
		CustomEnergyFixtureB12 f(world); f.Values(10, 100, 4);
		NDb::Talent* raw = new NDb::Talent;
		raw->manaCost.sString = "0"; raw->cooldownTime.sString = "0"; raw->naftaCost = 0;
		NDb::ConditionCastLimitation* condition = new NDb::ConditionCastLimitation;
		condition->condition.sString = "sender.vUniquePower >= 3";
		NDb::Ptr<NDb::ConditionCastLimitation> conditionOwner = condition;
		NDb::PermanentStatModApplicator* consume = new NDb::PermanentStatModApplicator;
		consume->modifier.variable = "UniquePower"; consume->modifier.multValue.sString = "1";
		consume->modifier.addValue.sString = "-3"; raw->applicators.push_back(consume);
		NDb::Ptr<NDb::Talent> db = raw;
		CObj<PFTalent> talent = new PFTalent(f.hero.GetPtr(), db, 1, 0, 0, false); talent->Activate();
		check(f.hero->UseTalent(talent, Target(f.hero.GetPtr())) && f.hero->GetCustomEnergyValue() == 7 && f.hero->GetMana() == 0,
			"actual talent consumes its authored custom variable without inventing mana payment");
		f.hero->Simulate(.5f);
		check(f.hero->GetCustomEnergyValue() == 9, "custom regeneration continues after an actual variable-consuming cast");
		f.Values(2, 100, 4);
		raw->castLimitations.push_back(conditionOwner);
		check(!f.hero->UseTalent(talent, Target(f.hero.GetPtr())) && f.hero->GetCustomEnergyValue() == 2,
			"denied condition admission remains fail-closed without spending the custom resource");
	}
	{
		B8BehaviourForeignWorld loaded(check);
		if (loaded.world)
		{
			CustomEnergyFixtureB12 f(loaded.world); f.Values(10, 100, 4);
			loaded.world->Step(.5f, .5f);
			check(f.hero->GetCustomEnergyValue() == 12,
				"the real world movement scheduler regenerates each hero custom resource once");
		}
	}
	{
		CustomEnergyFixtureB12 f(world); f.Values(20, 100, 4);
		NDb::HeroResource* resource = const_cast<NDb::HeroResource*>(f.resource.GetPtr());
		resource->formulaNameValue = "ExistingPower"; resource->formulaNameMaximum = "ExistingMaximum";
		ValueWithModifiers* value = f.hero->GetVariableVWM("ExistingPower"); value->SetBaseValue(20);
		const int foreign = value->AddModifier(1, 4, 91);
		ValueWithModifiers* maximum = f.hero->GetVariableVWM("ExistingMaximum"); maximum->SetBaseValue(50);
		CustomEnergyRestoreB12 restore; f.hero->PFBaseHero::operator&(restore);
		check(f.hero->GetCustomEnergyValue() == 24 && f.hero->GetCustomEnergyMaximum() == 50,
			"post-read rebinding uses existing named variables rather than stale raw pointers");
		check(value->GetModifiers().size() == 1 && value->Retrieve(foreign) && maximum->GetModifiers().empty(),
			"existing variables retain foreign ownership and do not gain new initialization constraints");
		f.hero->Simulate(.5f);
		check(value->GetBaseValue() == 26 && f.hero->GetCustomEnergyValue() == 30,
			"custom regeneration preserves original modified-value to base-value assignment semantics");
	}
	for (int invalid = 0; invalid != 4; ++invalid)
	{
		CustomEnergyFixtureB12 f(world, invalid);
		check(!f.hero->HasCustomEnergy() && f.hero->GetCustomEnergyValue() == 0,
			"invalid resource material or variable names cannot expose a partial custom resource");
		check(!f.hero->SearchVariableVWM(""), "invalid custom resource names never create an empty variable");
	}
	{
		CustomEnergyFixtureB12 f(world); f.Values(20, 100, 4);
		for (float dt : {-1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
		{
			f.hero->Simulate(dt);
			check(f.hero->GetCustomEnergyValue() == 20, "invalid elapsed time cannot mutate custom resource values");
		}
		// Engine variables clamp to 1e10, so overflow must arise in elapsed-time multiplication.
		f.Values(20, 100, 1e10f); f.hero->Simulate(std::numeric_limits<float>::max());
		check(f.hero->GetCustomEnergyValue() == 20, "overflowing custom regeneration is rejected before base-value mutation");
		const_cast<NDb::Hero*>(f.db.GetPtr())->uniqueResource = 0;
		check(f.hero->GetCustomEnergyValue() == 0 && f.hero->GetCustomEnergyMaximum() == 0 &&
			f.hero->GetCustomEnergyRegeneration() == 0, "removed resource metadata cannot expose stale bound variables");
		f.hero->Simulate(.1f);
		check(!f.hero->HasCustomEnergy(), "the next living tick disables removed custom resource metadata");
	}
}
