/** Expose initialization only; classification stays in the actual engine implementation. */
struct FormulaKindUnitB12 : PFBaseUnit
{
	FormulaKindUnitB12(PFWorld* world, const NDb::Unit* db, NDb::EUnitType kind)
		: PFBaseUnit(world, CVec3(5, 5, 0), db)
	{
		InitData data;
		data.pObjectDesc = db; data.type = kind;
		data.faction = NDb::FACTION_BURN; data.playerId = -1;
		Initialize(data);
	}
};

/** Real initialized unit kinds exercise FormulaBuilder's receiver predicates. */
static void UnitKindFormulasB12(Checks& check, PFWorld* world)
{
	using LinuxBootstrap::EvaluateUnitNumericFormula;
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 20);
	NDb::Ptr<NDb::Ability> abilityDb = new NDb::Ability;
	CObj<PFAbilityData> registered = new PFAbilityData(sender.GetPtr(), abilityDb, NDb::ABILITYTYPEID_SPECIAL);
	CObj<PFAbilityData> detached = new PFAbilityData(sender.GetPtr(), abilityDb, NDb::ABILITYTYPEID_SPECIAL, false);
	CObj<PFAbilityData> noOwner = new PFAbilityData(CPtr<PFBaseUnit>(), abilityDb, NDb::ABILITYTYPEID_SPECIAL);
	check(registered->GetWorld() == world && world->GetObjectById(registered->GetObjectId()) == registered.GetPtr(),
		"B12 registered ability retains its exact owner world and weak registry identity");
	check(!registered->IsRealWorldObject() && !registered->NeedToBeStepped(),
		"B12 ability registration never adds an independent tick owner");
	check(!detached->GetWorld() && !noOwner->GetWorld(), "B12 explicitly unregistered and ownerless ability data remain detached");
	for (int kind = 0; kind < NDb::KnownEnum<NDb::EUnitType>::SizeOf(); ++kind)
	{
		CObj<PFBaseUnit> target = new FormulaKindUnitB12(world, sender->DbUnitDesc(), static_cast<NDb::EUnitType>(kind));
		for (const char* expression : {"tUnitKind", "rUnitKind"})
			check(EvaluateUnitNumericFormula(expression, sender, target).value == kind,
				"B12 target kind binds the actual initialized engine enum");
		check(EvaluateUnitNumericFormula("sUnitKind", target, sender).value == kind,
			"B12 sender kind does not accidentally bind the receiver");
		check(EvaluateUnitNumericFormula("ut_bldg(7, 11)", sender, target).value ==
			(target->UnitCheck(UNITCHECKID_ISBUILDING) ? 7 : 11), "B12 building switch follows engine UnitCheck");
		check(EvaluateUnitNumericFormula("ut_hero(13, 17)", sender, target).value ==
			(target->UnitCheck(UNITCHECKID_ISHERO) ? 13 : 17), "B12 hero switch follows engine UnitCheck");
	}
	for (const char* expression : {"sUnitKindSuffix", "tUnitKind", "ut_bldg(1,2)", "ut_hero(1,2)"})
		check(!EvaluateUnitNumericFormula(expression, sender, nullptr).Succeeded(), "B12 unavailable kind context fails closed");
	for (const char* expression : {"ut_bldg()", "ut_hero(1)", "ut_bldg(1,2,3)",
		"0 ? ut_bldg(1) : 4", "ut_bldg(1e39,2)", "ut_hero(1,1e39)", "ut_bldg(1,missing)"})
		check(!EvaluateUnitNumericFormula(expression, sender, sender).Succeeded(), "B12 predicate arguments remain checked float values");
	check(EvaluateUnitNumericFormula("0 ? ut_bldg(missing,1/0) : 4", nullptr, nullptr).value == 4,
		"B12 unselected predicate call does not require context or evaluate arguments");
}

/** Mock authored DB, production common-creep behavior, fog, world ticks and attack payload. */
static void CreepDamageFormulaB12(Checks& check)
{
	B8BehaviourForeignWorld fixture(check);
	if (!fixture.world) return;
	PFWorld* world = fixture.world;
	NDb::Ptr<NDb::Creature> seed = B8BehaviourCreatureDb(world, NDb::FACTION_FREEZE);
	NDb::StatsContainer* stats = new NDb::StatsContainer;
	stats->stats = seed->stats->stats;
	for (const auto& entry : {nstl::pair<NDb::EStat, float>(NDb::STAT_VISIBILITYRANGE, 20),
		nstl::pair<NDb::EStat, float>(NDb::STAT_RANGE, 4)})
	{
		NDb::UnitStat stat; stat.statId = entry.first; stat.value.sString = NStr::StrFmt("%g", entry.second);
		stat.increment.sString = "0"; stats->stats.push_back(stat);
	}
	NDb::AdvMapCreep* raw = new NDb::AdvMapCreep;
	raw->stats = stats; raw->targetingParams = seed->targetingParams;
	raw->creepType = NDb::CREEPTYPE_FACTIONALNORMAL;
	NDb::BaseAttack* attackDb = new NDb::BaseAttack;
	attackDb->dispatch = new NDb::DispatchImmediate;
	attackDb->useRange.sString = "4";
	NDb::UnitConstantsContainer* constants = new NDb::UnitConstantsContainer;
	NDb::UnitConstant* constant = new NDb::UnitConstant;
	constant->name = "CreepBuildingMod"; constant->var.sString = "0.5";
	constants->vars.push_back(constant); attackDb->constants = constants;
	NDb::DamageApplicator* damage = new NDb::DamageApplicator;
	damage->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
	// Shipped Creeps/DefaultStats/PvP/BaseAttackDmg_Creep.APLR expression, unchanged.
	damage->damage.sString = "random ( f2l(sDamageMin), f2l(sDamageMax)) * (sUnitKind == 3 ? ut_bldg(cCreepBuildingMod, 1) : 1) ";
	attackDb->applicators.push_back(damage); raw->attackAbility = attackDb;
	NDb::Ptr<NDb::AdvMapCreep> db = raw;
	CObj<PFCommonCreep> attacker = new PFCommonCreep(world, *db, NDb::FACTION_FREEZE,
		Placement(CVec3(5, 5, 0), QNULL, CVec3(1, 1, 1)), vector<CVec2>(), 0, 0, nullptr);
	CObj<PFCommonCreep> receiver = new PFCommonCreep(world, *db, NDb::FACTION_BURN,
		Placement(CVec3(7, 5, 0), QNULL, CVec3(1, 1, 1)), vector<CVec2>(), 0, 0, nullptr);
	attacker->SetMoveFlags(MOVE_FLAG_NO_COLLIDE); receiver->SetMoveFlags(MOVE_FLAG_NO_COLLIDE);
	receiver->AddFlag(NDb::UNITFLAG_FORBIDATTACK);
	world->Step(0, 0);
	check(receiver->IsVisibleForFaction(attacker->GetFaction()), "B12 actual creep vision reveals its opposing target");
	const float life = receiver->GetHealth();
	CObj<PFBaseAttackInstance> instance = new PFBaseAttackInstance(attacker->GetAttackAbility(), Target(receiver.GetPtr()));
	check(instance->GetWorld() == world, "B12 base-attack instance inherits its real world context");
	const Target target(receiver.GetPtr());
	PFApplCreatePars pars(CObj<PFAbilityInstance>(instance.GetPtr()), target); pars.pDBAppl = damage;
	CObj<Damage> app = new Damage(pars);
	check(app->Init(), "B12 authored creep damage initializes its receiver");
	check(app->GetWorld() == world, "B12 base-attack damage inherits world RNG context");
	const auto evaluated = LinuxBootstrap::EvaluateApplicatorNumericFormula(damage->damage.sString.c_str(), *app);
	check(evaluated.Succeeded() && evaluated.value >= 18 && evaluated.value <= 22, "B12 complete shipped creep formula evaluates in production context");
	for (int tick = 0; tick < 60; ++tick) world->Step(.1f, .1f);
	check(receiver->GetHealth() < life - 20, "B12 world-stepped creep repeatedly damages with the authored shipped formula");
	check(attacker->GetAttackAbility()->GetCurrentCooldown() > 0, "B12 creep attacks consume production cooldowns");
}
