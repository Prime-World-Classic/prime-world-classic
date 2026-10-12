/** Literal mock DB; construction, stat upgrades and resource pools stay engine-owned. */
static NDb::Ptr<NDb::AdvMapCreep> CreepPowerDbB12(bool lateThreshold = false)
{
	NDb::StatsContainer* stats = new NDb::StatsContainer;
	for (const auto& entry : {nstl::pair<NDb::EStat, float>(NDb::STAT_LIFE, 100),
		nstl::pair<NDb::EStat, float>(NDb::STAT_ENERGY, 40),
		nstl::pair<NDb::EStat, float>(NDb::STAT_STRENGTH, 20),
		nstl::pair<NDb::EStat, float>(NDb::STAT_DEXTERITY, 30)})
	{
		NDb::UnitStat stat;
		stat.statId = entry.first;
		stat.value.sString = NStr::StrFmt("%.9g", entry.second);
		stat.increment.sString = NStr::StrFmt("%.9g", entry.second / 10);
		if (entry.first == NDb::STAT_DEXTERITY)
		{
			stat.increment.sString = "99";
			// Deliberately unsorted: thresholds replace, rather than add to, increment.
			for (int level : {3, 1, 2})
			{
				if (lateThreshold && level == 1) continue;
				NDb::StatUpgrade upgrade;
				upgrade.level = level;
				upgrade.increment = float(2 * level + 1);
				stat.levelUpgrades.push_back(upgrade);
			}
		}
		stats->stats.push_back(stat);
	}
	NDb::AdvMapCreep* raw = new NDb::AdvMapCreep;
	raw->stats = stats;
	raw->targetingParams = new NDb::UnitTargetingParameters;
	raw->creepType = NDb::CREEPTYPE_FACTIONALNORMAL;
	return raw;
}

static CObj<PFCommonCreep> CreepPowerUnitB12(PFWorld* world, bool lateThreshold = false)
{
	NDb::Ptr<NDb::AdvMapCreep> db = CreepPowerDbB12(lateThreshold);
	return new PFCommonCreep(world, *db, NDb::FACTION_FREEZE,
		Placement(CVec3(5, 5, 0), QNULL, CVec3(1, 1, 1)), vector<CVec2>());
}

/** Windows loads base values at level zero, then applies the level-one increment. */
static void CreepPowerLevelsB12(Checks& check, PFWorld* world)
{
	CObj<PFCommonCreep> creep = CreepPowerUnitB12(world);
	check(creep->GetNaftaLevel() == 1, "common creep starts at level one, not two");
	check(creep->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 110, "level one retains the constant first increment");
	check(creep->GetStat(NDb::STAT_DEXTERITY)->GetCoreValue() == 33, "level one uses threshold one, not threshold two");
	creep->SetPower(3, vector<float>());
	check(creep->GetNaftaLevel() == 3, "spawn power reaches requested level three");
	check(creep->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 130, "spawn power includes exactly three constant increments");
	check(creep->GetStat(NDb::STAT_DEXTERITY)->GetCoreValue() == 45, "spawn power sums authored thresholds one through three");
	check(creep->GetHealth() == 130 && creep->GetEnergy() == 52, "empty multipliers still refill leveled resources");
	creep->DoLevelups(2);
	check(creep->GetNaftaLevel() == 5, "later engine levelups advance from spawn level");
	check(creep->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 150, "later constant increments are neither skipped nor doubled");
	check(creep->GetStat(NDb::STAT_DEXTERITY)->GetCoreValue() == 59, "last authored threshold persists at later levels");
	CObj<PFCommonCreep> late = CreepPowerUnitB12(world, true);
	check(late->GetStat(NDb::STAT_DEXTERITY)->GetCoreValue() == 30, "no increment before the first authored threshold");
	late->SetPower(2, vector<float>());
	check(late->GetStat(NDb::STAT_DEXTERITY)->GetCoreValue() == 35, "first threshold applies when level two is reached");
	NDb::Ptr<NDb::AdvMapCreep> db = CreepPowerDbB12();
	CObj<PFBaseCreep> base = new PFBaseCreep(world, *db, NDb::FACTION_NEUTRAL,
		NDb::UNITTYPE_NEUTRALCREEP, -1, Placement(CVec3(7, 7, 0), QNULL, CVec3(1, 1, 1)));
	check(base->GetNaftaLevel() == 1 && base->GetStat(NDb::STAT_DEXTERITY)->GetCoreValue() == 33,
		"ordinary base creep constructor has the same initial level and increment");
	CObj<PFBaseUnit> unrelated = Unit(world, NDb::FACTION_FREEZE, 20);
	check(unrelated->GetNaftaLevel() == 1, "creep correction does not alter base-unit initialization");
}

/** Multipliers update cores once; ordinary modifiers and future increments remain intact. */
static void CreepPowerMultipliersB12(Checks& check, PFWorld* world)
{
	CObj<PFCommonCreep> creep = CreepPowerUnitB12(world);
	StatValueWithModifiers* life = creep->GetStat(NDb::STAT_LIFE);
	const int modifier = life->AddModifier(1.5f, 7, 12007);
	creep->SetHealth(11);
	creep->SetEnergy(4);
	vector<float> factors(NDb::KnownEnum<NDb::EStat>::SizeOf(), 1.0f);
	factors[NDb::STAT_LIFE] = 2;
	factors[NDb::STAT_ENERGY] = 3;
	factors[NDb::STAT_STRENGTH] = 0.5f;
	factors[NDb::STAT_DEXTERITY] = 2;
	creep->SetPower(3, factors);
	check(life->GetCoreValue() == 260, "power multiplies the post-levelup life core");
	check(life->GetValue() == 397, "power preserves the existing additive and multiplicative modifier");
	check(creep->GetStat(NDb::STAT_ENERGY)->GetCoreValue() == 156, "power scales authored energy independently");
	check(creep->GetStat(NDb::STAT_STRENGTH)->GetCoreValue() == 13, "positive fractional factors are supported");
	check(creep->GetStat(NDb::STAT_DEXTERITY)->GetCoreValue() == 90, "power follows the complete threshold upgrade");
	check(creep->GetHealth() == 397 && creep->GetMaxHealth() == 397, "spawn power refills health to the modified maximum");
	check(creep->GetEnergy() == 156 && creep->GetMaxEnergy() == 156, "spawn power refills energy to its maximum");
	life->RemoveModifier(modifier);
	check(life->GetValue() == 260, "removing an unrelated modifier exposes the scaled core");
	creep->DoLevelups(1);
	check(life->GetCoreValue() == 270, "power does not multiply future constant increments");
	check(creep->GetStat(NDb::STAT_DEXTERITY)->GetCoreValue() == 97, "power does not multiply future threshold increments");
	creep->SetHealth(12);
	creep->SetEnergy(5);
	creep->SetPower(4, vector<float>());
	check(creep->GetNaftaLevel() == 4 && life->GetCoreValue() == 270, "existing absolute native power level does not double-level");
	check(creep->GetHealth() == 270 && creep->GetEnergy() == 160, "empty factors refill even without a level change");
	creep->SetPower(1, vector<float>());
	check(creep->GetNaftaLevel() == 4 && life->GetCoreValue() == 270, "lower requested power does not delevel native creeps");
}

/** Malformed vector lengths are ignored; invalid entries cannot poison adjacent stats. */
static void CreepPowerGuardsB12(Checks& check, PFWorld* world)
{
	const int count = NDb::KnownEnum<NDb::EStat>::SizeOf();
	for (int length : {0, count - 1, count + 1})
	{
		CObj<PFCommonCreep> creep = CreepPowerUnitB12(world);
		creep->SetHealth(10);
		creep->SetEnergy(2);
		creep->SetPower(1, vector<float>(length, 2.0f));
		check(creep->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 110, "malformed or empty vector never partially scales cores");
		check(creep->GetHealth() == 110 && creep->GetEnergy() == 44, "ignored factor vector still completes resource initialization");
	}
	for (float invalid : {0.0f, -1.0f, EPS_VALUE, EPS_VALUE * 0.5f,
		std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
		-std::numeric_limits<float>::infinity(), std::numeric_limits<float>::max()})
	{
		CObj<PFCommonCreep> creep = CreepPowerUnitB12(world);
		vector<float> factors(count, 1.0f);
		factors[NDb::STAT_LIFE] = invalid;
		factors[NDb::STAT_ENERGY] = 2;
		creep->SetPower(1, factors);
		check(creep->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 110, "nonpositive nonfinite or overflowing product leaves life core intact");
		check(creep->GetHealth() == 110, "bad life factor does not corrupt health or its maximum");
		check(creep->GetEnergy() == 88, "valid neighboring factor still applies and refills");
	}
	CObj<PFCommonCreep> creep = CreepPowerUnitB12(world);
	vector<float> factors(count, 1.0f);
	factors[NDb::STAT_STRENGTH] = 2 * EPS_VALUE;
	creep->SetPower(1, factors);
	check(creep->GetStat(NDb::STAT_STRENGTH)->GetCoreValue() == 22 * (2 * EPS_VALUE), "factor strictly above EPS_VALUE is accepted");
	StatValueWithModifiers* strength = creep->GetStat(NDb::STAT_STRENGTH);
	strength->SetCoreValue(1);
	factors[NDb::STAT_STRENGTH] = std::numeric_limits<float>::max();
	creep->SetPower(1, factors);
	check(strength->GetCoreValue() == std::numeric_limits<float>::max(), "largest finite product is retained");
	strength->SetCoreValue(-std::numeric_limits<float>::max());
	factors[NDb::STAT_STRENGTH] = 2;
	creep->SetPower(1, factors);
	check(strength->GetCoreValue() == -std::numeric_limits<float>::max(), "negative product overflow is also rejected");
	strength->SetCoreValue(std::numeric_limits<float>::quiet_NaN());
	creep->SetPower(1, factors);
	check(std::isnan(strength->GetCoreValue()) && creep->GetHealth() == 110, "preexisting invalid non-resource core is skipped without spreading");
	StatValueWithModifiers* crit = creep->GetStat(NDb::STAT_CRITCHANCEBONUS);
	crit->SetCoreValue(-2);
	factors[NDb::STAT_CRITCHANCEBONUS] = 2;
	creep->SetPower(1, factors);
	check(crit->GetCoreValue() == -4, "valid negative authored cores retain Windows multiplication policy");
}

/** Exercise the actual spawner initialization call, with authored barracks multipliers. */
static void CreepPowerSpawnerB12(Checks& check, PFWorld* world)
{
	CObj<PFCreepSpawner> spawner = CreepSpawnerB11(world, false);
	vector<float>& authored = const_cast<NDb::AILogicParameters&>(
		world->GetAIWorld()->GetAIParameters()).quarterDestroyStatsMultipliers;
	const vector<float> saved = authored;
	authored.resize(NDb::KnownEnum<NDb::EStat>::SizeOf());
	for (float& factor : authored) factor = 2;
	spawner->SetCreepsPower(PFCreepSpawner::powerLarge);
	authored = saved;
	CObj<PFCommonCreep> creep = CreepPowerUnitB12(world);
	spawner->InitializeCreep(creep.GetPtr(), Render::HDRColor());
	check(creep->GetNaftaLevel() == 1, "production spawner passes the requested power level");
	check(creep->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 220, "production spawner applies authored barracks multiplier");
	check(creep->GetHealth() == 220 && creep->GetEnergy() == 88, "production spawner power refills both resources");
}
