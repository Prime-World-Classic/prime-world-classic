/** Inject malformed derived getter output, after the stat container's own numeric clamping. */
struct ResourceBoundaryB12 : ProbeUnit
{
	float regeneration = 0;
	ResourceBoundaryB12(PFWorld* world, const NDb::Unit* db) : ProbeUnit(world, db, NDb::FACTION_FREEZE) {}
	float GetLifeRegenTotal() const override { return regeneration; }
	float GetEnergyRegenTotal() const override { return regeneration; }
};

/** Run all resource restoration through the existing world/unit loop for each recipient class. */
static void ResourceTicksB12(Checks& check)
{
	for (int kind = 0; kind < 3; ++kind)
	{
		B8BehaviourForeignWorld fixture(check);
		if (!fixture.world) continue;
		PFWorld* world = fixture.world;
		CObj<PFBaseUnit> unit = HealingReceiver(world, kind);
		unit->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		unit->GetStat(NDb::STAT_LIFEREGENERATIONABSOLUTE)->SetCoreValue(10);
		unit->GetStat(NDb::STAT_LIFEREGENERATION)->SetCoreValue(2);
		unit->GetStat(NDb::STAT_ENERGYREGENERATIONABSOLUTE)->SetCoreValue(5);
		unit->GetStat(NDb::STAT_ENERGYREGENERATION)->SetCoreValue(1);
		unit->SetHealth(400); unit->SetEnergy(80);
		world->Step(.5f, .5f);
		check(unit->GetHealth() == 415 && unit->GetEnergy() == 83.5f, "B12 absolute and percentage regeneration advance each pool once");
		unit->AddFlag(NDb::UNITFLAG_FORBIDLIFERESTORE);
		world->Step(.5f, .5f);
		check(unit->GetHealth() == 415 && unit->GetEnergy() == 87, "B12 life restore forbid does not suppress energy regeneration");
		unit->AddFlag(NDb::UNITFLAG_FORBIDENERGYRESTORE);
		unit->GetStat(NDb::STAT_LIFE)->SetCoreValue(2000);
		unit->GetStat(NDb::STAT_ENERGY)->SetCoreValue(400);
		world->Step(.5f, .5f);
		check(unit->GetMaxHealth() == 2000 && unit->GetMaxEnergy() == 400 && unit->GetHealth() == 830 && unit->GetEnergy() == 174,
			"B12 changed pool limits preserve filled percentages even while restoration is forbidden");
		unit->GetStat(NDb::STAT_LIFE)->SetCoreValue(1000);
		unit->GetStat(NDb::STAT_ENERGY)->SetCoreValue(200);
		world->Step(.5f, .5f);
		check(unit->GetHealth() == 415 && unit->GetEnergy() == 87, "B12 reducing maximum stats preserves the original filled proportions");
		unit->RemoveFlag(NDb::UNITFLAG_FORBIDLIFERESTORE | NDb::UNITFLAG_FORBIDENERGYRESTORE);
		unit->GetStat(NDb::STAT_LIFEREGENERATION)->SetCoreValue(0);
		unit->GetStat(NDb::STAT_ENERGYREGENERATION)->SetCoreValue(0);
		unit->GetStat(NDb::STAT_LIFEREGENERATIONABSOLUTE)->SetCoreValue(-1000);
		unit->GetStat(NDb::STAT_ENERGYREGENERATIONABSOLUTE)->SetCoreValue(-1000);
		world->Step(1, 1);
		check(unit->GetHealth() == 1 && unit->GetEnergy() == 0 && !unit->IsDead(), "B12 signed regeneration retains the Windows life floor and zero energy floor");
		unit->GetStat(NDb::STAT_LIFEREGENERATIONABSOLUTE)->SetCoreValue(10000);
		unit->GetStat(NDb::STAT_ENERGYREGENERATIONABSOLUTE)->SetCoreValue(10000);
		world->Step(1, 1);
		check(unit->GetHealth() == 1000 && unit->GetEnergy() == 200, "B12 regeneration clamps to each current maximum");
		unit->SetHealth(400); unit->SetEnergy(80);
		unit->GetStat(NDb::STAT_LIFEREGENERATIONABSOLUTE)->SetCoreValue(0);
		unit->GetStat(NDb::STAT_ENERGYREGENERATIONABSOLUTE)->SetCoreValue(0);
		unit->GetStat(NDb::STAT_LIFE)->SetCoreValue(1000); unit->GetStat(NDb::STAT_ENERGY)->SetCoreValue(0);
		world->Step(.1f, .1f);
		check(unit->GetEnergy() == 0 && unit->GetMaxEnergy() == 0, "B12 authored zero-energy capacity is supported");
		unit->GetStat(NDb::STAT_ENERGY)->SetCoreValue(200);
		world->Step(.1f, .1f);
		check(unit->GetEnergy() == 200, "B12 gaining an energy pool follows the Windows empty-capacity policy");
		unit->ChangeHealth(-unit->GetHealth());
		unit->GetStat(NDb::STAT_LIFEREGENERATIONABSOLUTE)->SetCoreValue(100);
		world->Step(.1f, .1f);
		check(unit->IsDead() && unit->GetHealth() == 0, "B12 regeneration cannot resurrect a dead unit");
	}
	B8BehaviourForeignWorld fixture(check);
	if (!fixture.world) return;
	CObj<PFBaseUnit> seed = Unit(fixture.world, NDb::FACTION_FREEZE, 20);
	CObj<ResourceBoundaryB12> boundary = new ResourceBoundaryB12(fixture.world, seed->DbUnitDesc());
	boundary->SetHealth(400); boundary->SetEnergy(80);
	for (float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::max()})
	{
		boundary->regeneration = bad;
		fixture.world->Step(2, 2);
		check(boundary->GetHealth() == 400 && boundary->GetEnergy() == 80,
			"B12 nonfinite or overflowing derived regeneration cannot poison pools");
	}
	boundary->regeneration = 10;
	for (float dt : {0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		boundary->Step(dt);
		check(boundary->GetHealth() == 400 && boundary->GetEnergy() == 80,
			"B12 invalid direct unit ticks do not restore resources");
	}
}
