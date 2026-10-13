/** Match FormulaBuilder's sender/receiver vUppercase identifier spelling via real unit variables. */
static void UnitVariableFormulasB12(Checks& check, PFWorld* world)
{
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 30);
	CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 15);
	sender->GetVariableVWM("IsPet")->SetBaseValue(1);
	sender->GetVariableVWM("Power_2")->SetBaseValue(4);
	receiver->GetVariableVWM("Power_2")->SetBaseValue(9);
	receiver->GetVariableVWM("SpellPierce")->SetBaseValue(.5f);
	const auto run = [&](const char* expression, float expected) {
		const auto result = LinuxBootstrap::EvaluateUnitNumericFormula(expression, sender, receiver);
		check(result.Succeeded() && result.value == expected, expression);
	};
	run("sender.vIsPet", 1);
	run("sender.vIsPet == 0", 0);
	run("sender.vPower_2 + receiver.vPower_2", 13);
	run("receiver.vSpellPierce", .5f);
	run("sender.vMissing", 0);
	check(!sender->SearchVariableVWM("Missing"), "formula lookup never creates a missing unit variable");
	run("1 ? 7 : receiver.vMissing", 7);
	sender->GetVariableVWM("IsPet")->SetBaseValue(0);
	run("sender.vIsPet == 0", 1);
	sender->GetVariableVWM("Power_2")->AddModifier(2, 1, 0);
	run("sender.vPower_2 + receiver.vPower_2", 18);
	for (const char* bad : {"sender.v", "sender.visPet", "sender.v1Power", "receiver.v_Name",
		"other.vIsPet", "xsender.vIsPet", "sender.vIsPet()", "receiver.vIsPet.member",
		"sender.vIsPet=2", "sender.vIsPet++", "sender.vIsPet[0]", "sender->vIsPet",
		"sender.GetVariable(\"IsPet\")", "__unitvar_s_IsPet", "1 ? 7 : __unitvar_r_Power_2",
		"__combat_damageDealed", "sender.vIsPet receiver.vPower_2"})
		check(!LinuxBootstrap::EvaluateUnitNumericFormula(bad, sender, receiver).Succeeded(),
			"unsupported member syntax and internal binding injection fail closed");
	check(!LinuxBootstrap::EvaluateUnitNumericFormula("sender.vIsPet", nullptr, receiver).Succeeded(),
		"selected sender variable requires a live sender context");
	check(!LinuxBootstrap::EvaluateUnitNumericFormula("receiver.vPower_2", sender, nullptr).Succeeded(),
		"selected receiver variable requires a live receiver context");
	check(LinuxBootstrap::EvaluateUnitNumericFormula("1 ? 3 : receiver.vPower_2", sender, nullptr).value == 3,
		"unselected valid variable syntax preserves lazy conditional evaluation");

	NDb::Ability* rawAbility = new NDb::Ability;
	NDb::UnitConstantsContainer* constants = new NDb::UnitConstantsContainer;
	NDb::UnitConstant* constant = new NDb::UnitConstant;
	constant->name = "Power"; constant->var.sString = "sender.vPower_2+receiver.vPower_2";
	constants->vars.push_back(constant); rawAbility->constants = constants;
	NDb::Ptr<NDb::Ability> db = rawAbility;
	CObj<PFAbilityData> ability = new PFAbilityData(sender.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL, false, false);
	check(LinuxBootstrap::EvaluateUnitNumericFormula("cPower", sender, receiver, ability).value == 18,
		"ability constants bind the same live unit variables");
	receiver->GetVariableVWM("Power_2")->SetBaseValue(12);
	check(LinuxBootstrap::EvaluateUnitNumericFormula("cPower", sender, receiver, ability).value == 21,
		"recursive constants do not cache unit variables across formula evaluations");
	for (const char* expression : {"sender.vPower_2+receiver.vPower_2", "cPower"})
	{
		NDb::DamageApplicator* raw = new NDb::DamageApplicator;
		raw->damage.sString = expression; raw->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
		NDb::Ptr<NDb::DamageApplicator> damageDb = raw;
		// Creation parameters borrow this target until the applicator copies it.
		const Target target(receiver.GetPtr());
		PFApplCreatePars pars(CObj<PFAbilityInstance>(), target);
		pars.pWorld = world; pars.pOwner = sender; pars.pDBAppl = damageDb; pars.pUpgraderAbilityData = ability;
		CObj<Damage> app = new Damage(pars);
		receiver->SetHealth(1000);
		check(app->Init() && app->Run() && app->GetDamageDealed() == 21 && receiver->GetHealth() == 979,
			"real applicator damage consumes live variables directly and through ability constants");
	}
}
