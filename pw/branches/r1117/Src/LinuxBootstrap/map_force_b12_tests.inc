/** Temporarily author force inputs; production calculations and objects remain real. */
struct MapForceDbScopeB12
{
	NDb::AILogicParameters& ai;
	PFAIWorld::StatsCoeffs& coeffs;
	NDb::Ptr<NDb::StatsBudgetContainer> savedBudget;
	PFAIWorld::StatsCoeffs savedCoeffs;
	MapForceDbScopeB12(PFWorld* world)
		: ai(const_cast<NDb::AILogicParameters&>(world->GetAIWorld()->GetAIParameters())),
		coeffs(const_cast<PFAIWorld::StatsCoeffs&>(world->GetAIWorld()->GetStatsCoeffs())),
		savedBudget(ai.statsBudget), savedCoeffs(coeffs)
	{
		NDb::StatsBudgetContainer* budgets = new NDb::StatsBudgetContainer;
		for (NDb::EStat stat : {NDb::STAT_LIFE, NDb::STAT_STRENGTH, NDb::STAT_DEXTERITY,
			NDb::STAT_CRITCHANCEBONUS, NDb::STAT_ENERGY})
		{
			NDb::StatBudget budget;
			budget.stat = stat;
			budget.budget = 7;
			budget.upgradesWithForce = stat != NDb::STAT_ENERGY;
			budgets->stats.push_back(budget);
		}
		ai.statsBudget = budgets;
		coeffs.coeffsByFaction[NDb::FACTION_FREEZE] = 2;
		coeffs.coeffsByFaction[NDb::FACTION_BURN] = 3;
		coeffs.coeffsByFaction[NDb::FACTION_NEUTRAL] = 0.5f;
		for (float& factor : coeffs.botsCoeffsByFaction) factor = 99;
	}
	~MapForceDbScopeB12() { ai.statsBudget = savedBudget; coeffs = savedCoeffs; }
};

/** No overridden stat or level methods: all literal DB values use the unit initializer. */
static NDb::Ptr<NDb::Creature> MapForceCreatureDbB12()
{
	NDb::StatsContainer* stats = new NDb::StatsContainer;
	for (const auto& entry : {nstl::pair<NDb::EStat, float>(NDb::STAT_LIFE, 100),
		nstl::pair<NDb::EStat, float>(NDb::STAT_ENERGY, 40),
		nstl::pair<NDb::EStat, float>(NDb::STAT_STRENGTH, 20),
		nstl::pair<NDb::EStat, float>(NDb::STAT_DEXTERITY, 30),
		nstl::pair<NDb::EStat, float>(NDb::STAT_STAMINA, 10),
		nstl::pair<NDb::EStat, float>(NDb::STAT_CRITCHANCEBONUS, -10)})
	{
		NDb::UnitStat stat; stat.statId = entry.first;
		stat.value.sString = NStr::StrFmt("%.9g", entry.second);
		stat.increment.sString = NStr::StrFmt("%.9g", entry.second / 10);
		if (entry.first == NDb::STAT_DEXTERITY)
		{
			stat.increment.sString = "99";
			for (int level : {3, 2})
			{
				NDb::StatUpgrade upgrade; upgrade.level = level;
				upgrade.increment = float(level * 2 + 1);
				stat.levelUpgrades.push_back(upgrade);
			}
		}
		stats->stats.push_back(stat);
	}
	NDb::Creature* db = new NDb::Creature; db->stats = stats;
	db->targetingParams = new NDb::UnitTargetingParameters;
	return db;
}

static CObj<PFBaseUnit> MapForceUnitB12(PFWorld* world, NDb::EFaction faction = NDb::FACTION_FREEZE)
{
	NDb::Ptr<NDb::Creature> db = MapForceCreatureDbB12();
	return new ProbeUnit(world, db.GetPtr(), faction);
}

static float MapForceStoredB12(const PFBaseUnit& unit)
{
	return unit.GetStatContainer() ? unit.GetStatContainer()->GetIncrementForceModifier() : 1;
}

/** BASE and LEVELUP are independent, and only authored force-enabled budgets are weighted. */
static void MapForceModesB12(Checks& check, PFWorld* world)
{
	for (int mode = 0; mode <= NDb::MAPFORCESTATMODIFIERAPPLICATION_ALL; ++mode)
	{
		CObj<PFBaseUnit> unit = MapForceUnitB12(world);
		unit->SetHealth(25); unit->SetEnergy(10);
		unit->ModifyStatsByForce(NDb::MapForceStatModifierApplication(mode));
		const float base = (mode & NDb::MAPFORCESTATMODIFIERAPPLICATION_BASE) ? 2 : 1;
		const float inc = (mode & NDb::MAPFORCESTATMODIFIERAPPLICATION_LEVELUP) ? 2 : 1;
		check(unit->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 100 * base, "force BASE bit controls only initial core scaling");
		check(MapForceStoredB12(*unit) == inc, "force LEVELUP bit retains the engine increment coefficient");
		check(unit->GetNaftaLevel() == 1, "map force never changes the unrelated base-unit initial level");
		check(unit->GetHealth() == 25 && unit->GetEnergy() == 10 && unit->GetMaxHealth() == 100,
			"ModifyStatsByForce does not refill resources or refresh resource maxima");
		unit->DoLevelups(2);
		check(unit->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 100 * base + 20 * inc,
			"force weights the actual constant upgrade by its stored coefficient");
		check(unit->GetStat(NDb::STAT_DEXTERITY)->GetCoreValue() == 30 * base + 12 * inc,
			"force weights authored level thresholds without changing their selection");
		check(unit->GetStat(NDb::STAT_ENERGY)->GetCoreValue() == 48, "force-disabled budget does not scale base or increment");
		check(unit->GetStat(NDb::STAT_STAMINA)->GetCoreValue() == 12, "missing budget leaves base and increment unchanged");
		check(unit->GetStat(NDb::STAT_CRITCHANCEBONUS)->GetCoreValue() == -10 * base - 2 * inc,
			"negative authored cores and increments retain their sign");
		check(unit->GetNaftaLevel() == 3, "only the explicit engine levelup changes level");
		unit->UpgradeStats(1);
		check(unit->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 100 * base + 30 * inc && unit->GetNaftaLevel() == 3,
			"UpgradeStats shares the force weighting without advancing level");
		unit->UpgradeHeroStats(1, 0, 0.5f);
		check(unit->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 100 * base + 35 * inc,
			"existing fractional upgrade path also applies the stored force coefficient");
	}
}

/** Reapply uses Windows' old increment coefficient in non-shipping builds. */
static void MapForceReapplyB12(Checks& check, PFWorld* world, MapForceDbScopeB12& db)
{
	CObj<PFBaseUnit> unit = MapForceUnitB12(world);
	StatValueWithModifiers* life = unit->GetStat(NDb::STAT_LIFE);
	const int modifier = life->AddModifier(1.5f, 7, 12012);
	unit->ModifyStatsByForce();
	check(life->GetCoreValue() == 200 && life->GetValue() == 307, "force modifies core but preserves ordinary stat modifiers");
	unit->ModifyStatsByForce();
#ifndef _SHIPPING
	check(life->GetCoreValue() == 200, "non-shipping reapply divides by the stored previous coefficient");
#else
	check(life->GetCoreValue() == 400, "shipping reapply retains original multiplicative policy");
#endif
	db.coeffs.coeffsByFaction[NDb::FACTION_FREEZE] = 1.5f;
	unit->ModifyStatsByForce();
#ifndef _SHIPPING
	check(life->GetCoreValue() == 150, "live map-force change removes previous weighting before applying new weighting");
#else
	check(life->GetCoreValue() == 600, "shipping uses the new coefficient directly");
#endif
	check(MapForceStoredB12(*unit) == 1.5f, "live force update replaces the stored increment coefficient");
	life->RemoveModifier(modifier);
	const float core = life->GetCoreValue();
	unit->DoLevelups(1);
	check(life->GetCoreValue() == core + 15, "later levelup uses the updated coefficient");
	db.coeffs.coeffsByFaction[NDb::FACTION_FREEZE] = 2;
	CObj<PFBaseUnit> baseOnly = MapForceUnitB12(world);
	baseOnly->ModifyStatsByForce(NDb::MAPFORCESTATMODIFIERAPPLICATION_BASE);
	baseOnly->ModifyStatsByForce(NDb::MAPFORCESTATMODIFIERAPPLICATION_BASE);
	check(baseOnly->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 400 && MapForceStoredB12(*baseOnly) == 1,
		"BASE-only reapplication retains Windows behavior without inventing another force cache");
	for (NDb::EFaction faction : {NDb::FACTION_FREEZE, NDb::FACTION_BURN, NDb::FACTION_NEUTRAL})
	{
		CObj<PFBaseUnit> factionUnit = MapForceUnitB12(world, faction);
		factionUnit->ModifyStatsByForce();
		check(factionUnit->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 100 * db.coeffs.coeffsByFaction[faction],
			"force uses the unit faction coefficient, never bots coefficients");
	}
}

/** Exercise the production creep constructor and its mana-cost force reader. */
static void MapForceCreepB12(Checks& check, PFWorld* world)
{
	NDb::Ptr<NDb::Creature> db = MapForceCreatureDbB12();
	for (int mode = 0; mode <= NDb::MAPFORCESTATMODIFIERAPPLICATION_ALL; ++mode)
	{
		CObj<PFBaseCreep> creep = new PFBaseCreep(world, *db, NDb::FACTION_FREEZE,
			NDb::UNITTYPE_CREEP, -1, Placement(CVec3(7, 7, 0), QNULL, CVec3(1, 1, 1)),
			0, 0, false, NDb::MapForceStatModifierApplication(mode));
		const float base = (mode & NDb::MAPFORCESTATMODIFIERAPPLICATION_BASE) ? 2 : 1;
		const float inc = (mode & NDb::MAPFORCESTATMODIFIERAPPLICATION_LEVELUP) ? 2 : 1;
		check(creep->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 100 * base + 10 * inc,
			"real creep constructor applies base force before its first weighted constant increment");
		check(creep->GetManaCostModifier(false) == inc && creep->GetManaCostModifier(true) == inc,
			"creep mana-cost force reader observes the original stat-container field");
		check(creep->GetStat(NDb::STAT_ENERGY)->GetCoreValue() == 44, "real creep leaves force-disabled energy untouched");
	}
}

/** Malformed context and numeric inputs cannot install invalid force or corrupt finite cores. */
static void MapForceGuardsB12(Checks& check, PFWorld* world, MapForceDbScopeB12& db)
{
	for (float invalid : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
	{
		db.coeffs.coeffsByFaction[NDb::FACTION_FREEZE] = invalid;
		CObj<PFBaseUnit> unit = MapForceUnitB12(world);
		unit->ModifyStatsByForce();
		check(unit->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 100 && MapForceStoredB12(*unit) == 1,
			"nonpositive or nonfinite faction force is rejected before mutation");
		unit->DoLevelups(1);
		check(unit->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 110, "rejected force cannot poison future increments");
	}
	db.coeffs.coeffsByFaction[NDb::FACTION_FREEZE] = 2;
	for (int faction : {-1, 3, std::numeric_limits<int>::max()})
	{
		CObj<PFBaseUnit> unit = MapForceUnitB12(world, NDb::EFaction(faction));
		unit->ModifyStatsByForce();
		check(unit->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 100 && MapForceStoredB12(*unit) == 1,
			"invalid faction fails closed before indexing the fixed coefficient array");
	}
	CObj<PFBaseUnit> noWorld = MapForceUnitB12(nullptr);
	noWorld->ModifyStatsByForce();
	check(noWorld->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 100, "missing world is a safe no-op");
	CObj<PFBaseUnit> wrongMode = MapForceUnitB12(world);
	wrongMode->ModifyStatsByForce(NDb::MapForceStatModifierApplication(7));
	check(wrongMode->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 100 && MapForceStoredB12(*wrongMode) == 1,
		"unsupported force application bits fail closed");
	const NDb::Ptr<NDb::StatsBudgetContainer> budgets = db.ai.statsBudget;
	db.ai.statsBudget = nullptr;
	CObj<PFBaseUnit> missingBudget = MapForceUnitB12(world);
	missingBudget->ModifyStatsByForce();
	check(missingBudget->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 100 && MapForceStoredB12(*missingBudget) == 1,
		"BASE without a budget fails closed without partial LEVELUP mutation");
	missingBudget->ModifyStatsByForce(NDb::MAPFORCESTATMODIFIERAPPLICATION_LEVELUP);
	check(MapForceStoredB12(*missingBudget) == 2, "LEVELUP-only needs no base-budget enumeration");
	missingBudget->DoLevelups(1);
	check(missingBudget->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 110, "no budget means no stat is eligible for increment weighting");
	db.ai.statsBudget = budgets;
	missingBudget->DoLevelups(1);
	check(missingBudget->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 130, "budget lookup remains live on subsequent upgrades");
	CObj<PFBaseUnit> corrupt = MapForceUnitB12(world);
	corrupt->GetStat(NDb::STAT_STRENGTH)->SetCoreValue(std::numeric_limits<float>::quiet_NaN());
	corrupt->ModifyStatsByForce();
	check(std::isnan(corrupt->GetStat(NDb::STAT_STRENGTH)->GetCoreValue()) &&
		corrupt->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 200, "invalid preexisting core is skipped without suppressing valid neighbors");
	db.coeffs.coeffsByFaction[NDb::FACTION_FREEZE] = std::numeric_limits<float>::max();
	NDb::Ptr<NDb::Creature> overflowDb = MapForceCreatureDbB12();
	const_cast<NDb::StatsContainer*>(overflowDb->stats.GetPtr())->stats.back().increment.sString = "-2";
	CObj<PFBaseUnit> overflow = new ProbeUnit(world, overflowDb.GetPtr(), NDb::FACTION_FREEZE);
	overflow->GetStat(NDb::STAT_STRENGTH)->SetCoreValue(1);
	overflow->ModifyStatsByForce();
	check(overflow->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 100, "base multiplication overflow is rejected per stat");
	check(overflow->GetStat(NDb::STAT_STRENGTH)->GetCoreValue() == std::numeric_limits<float>::max(),
		"largest representable base product is accepted");
	overflow->DoLevelups(1);
	check(overflow->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 100 && overflow->GetStat(NDb::STAT_ENERGY)->GetCoreValue() == 44,
		"overflowing weighted increment is skipped while unweighted increment still applies");
	check(overflow->GetStat(NDb::STAT_CRITCHANCEBONUS)->GetCoreValue() == -10,
		"negative base and increment overflow are rejected too");
	db.coeffs.coeffsByFaction[NDb::FACTION_FREEZE] = 2;
	// A large finite increment is authored through the actual DB loader.
	NDb::Ptr<NDb::Creature> hugeDb = MapForceCreatureDbB12();
	NDb::StatsContainer* hugeStats = const_cast<NDb::StatsContainer*>(hugeDb->stats.GetPtr());
	hugeStats->stats[0].increment.sString = "2e38";
	CObj<PFBaseUnit> sumOverflow = new ProbeUnit(world, hugeDb.GetPtr(), NDb::FACTION_FREEZE);
	sumOverflow->GetStat(NDb::STAT_LIFE)->SetCoreValue(2e38f);
	db.coeffs.coeffsByFaction[NDb::FACTION_FREEZE] = 1;
	sumOverflow->ModifyStatsByForce(NDb::MAPFORCESTATMODIFIERAPPLICATION_LEVELUP);
	sumOverflow->DoLevelups(1);
	check(sumOverflow->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 2e38f, "finite weighted increment cannot overflow core addition");
	db.coeffs.coeffsByFaction[NDb::FACTION_FREEZE] = 2;
}

/** Keep original budget traversal/first-match semantics without unsafe enum indexing. */
static void MapForceBudgetsB12(Checks& check, PFWorld* world, MapForceDbScopeB12& db)
{
	vector<NDb::StatBudget>& budgets = const_cast<NDb::StatsBudgetContainer*>(db.ai.statsBudget.GetPtr())->stats;
	const vector<NDb::StatBudget> saved = budgets;
	for (int id : {-1, NDb::KnownEnum<NDb::EStat>::SizeOf()})
	{
		NDb::StatBudget invalid; invalid.stat = NDb::EStat(id); invalid.upgradesWithForce = true;
		budgets.push_back(invalid);
	}
	CObj<PFBaseUnit> invalidIds = MapForceUnitB12(world);
	invalidIds->ModifyStatsByForce();
	check(invalidIds->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 200, "invalid budget stat IDs cannot prevent valid entries from applying");
	budgets.push_back(budgets[0]);
	CObj<PFBaseUnit> duplicate = MapForceUnitB12(world);
	duplicate->ModifyStatsByForce();
	check(duplicate->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 400, "BASE retains original per-budget-entry traversal");
	duplicate->DoLevelups(1);
	check(duplicate->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 420, "increment budget lookup applies force once, not once per duplicate");
	budgets[0].upgradesWithForce = false;
	CObj<PFBaseUnit> firstMatch = MapForceUnitB12(world);
	firstMatch->ModifyStatsByForce();
	firstMatch->DoLevelups(1);
	check(firstMatch->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 210, "increment eligibility uses the engine's first matching budget entry");
	budgets = saved;
#ifndef _SHIPPING
	for (float invalid : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
	{
		CObj<PFBaseUnit> corrupt = MapForceUnitB12(world);
		corrupt->ModifyStatsByForce(NDb::MAPFORCESTATMODIFIERAPPLICATION_LEVELUP);
		PFStatContainer* stats = const_cast<PFStatContainer*>(corrupt->GetStatContainer());
		if (stats) stats->SetIncrementForceModifier(invalid);
		corrupt->ModifyStatsByForce(NDb::MAPFORCESTATMODIFIERAPPLICATION_BASE);
		check(corrupt->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 100, "invalid previous coefficient cannot be used as a divisor");
		corrupt->DoLevelups(1);
		check(corrupt->GetStat(NDb::STAT_LIFE)->GetCoreValue() == 100 && corrupt->GetStat(NDb::STAT_ENERGY)->GetCoreValue() == 44,
			"corrupt stored force skips weighted increments but preserves ordinary upgrades");
	}
#endif
}

/** Single integration entrypoint, restoring authored world inputs before returning. */
static void MapForceB12(Checks& check, PFWorld* world)
{
	MapForceDbScopeB12 db(world);
	MapForceModesB12(check, world);
	MapForceReapplyB12(check, world, db);
	MapForceCreepB12(check, world);
	MapForceGuardsB12(check, world, db);
	MapForceBudgetsB12(check, world, db);
}
