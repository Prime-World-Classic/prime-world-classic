/** Authored member spellings bind actual endpoint history, not a general C++ member evaluator. */
static void DamageFormulaBindingsB13(Checks& check)
{
	B8BehaviourForeignWorld world(check); if (!world.world) return;
	DamageHistoryFixtureB13 f(check, world.world); f.Tick(10);
	f.Hit("7.75"); f.Hit("2.75", 2, f.sender);
	f.sender->GetVariableVWM("Window")->SetBaseValue(1);
	const auto run = [&](const char* expression, float expected) {
		const auto result = LinuxBootstrap::EvaluateUnitNumericFormula(expression, f.sender, f.receiver);
		check(result.Succeeded() && result.value == expected, expression);
	};
	for (const char* expression : {"sender.GetDamageDealt(1)", "pFirst->GetDamageDealt(1)",
		"receiver.GetDamageTaken(1)", "pSecond->GetDamageTaken(1)", "receiver.GetDamageTaken ( 1.f )",
		"receiver.GetDamageTaken(min(2, sender.vWindow))", "receiver.GetDamageTaken(sender.GetDamageTaken(1))"})
		run(expression, 7);
	for (const char* expression : {"sender.GetDamageTaken(1)", "pFirst->GetDamageTaken(1)"}) run(expression, 2);
	for (const char* expression : {"receiver.GetDamageDealt(1)", "pSecond->GetDamageDealt(1)",
		"receiver.GetDamageTaken(0)", "receiver.GetDamageTaken(-1)"}) run(expression, 0);
	run("receiver.GetDamageTaken(1) + sender.GetDamageTaken(1)", 9);
	// Shipped Items/Consumables/status_Stealth.APLR predicate, evaluated on the damaged sender.
	run("sender.GetDamageTaken(0.2) <= 0", 0);
	f.Tick(.25f);
	run("sender.GetDamageTaken(0.2) <= 0", 1);
	f.Tick(.75f);
	run("receiver.GetDamageTaken(1)", 0);
	f.Hit("5.75");
	run("receiver.GetDamageTaken(1)", 5);
	run("sender.GetDamageDealt(1)", 5);
	for (const char* bad : {"sender.GetDamageTaken()", "receiver.GetDamageDealt(1,2)",
		"1 ? 7 : sender.GetDamageTaken()", "1 ? 7 : receiver.GetDamageTaken(1,2)",
		"sender.GetDamageTakenEx(1,0,pMisc)", "other.GetDamageTaken(1)", "sender->GetDamageTaken(1)",
		"pFirst.GetDamageTaken(1)", "sender.GetDamageTakenExtra(1)", "receiver.GetDamageTaken(1).x",
		"sender.GetDamageTaken(\"1\")", "__history_s_taken(1)", "0 ? __history_r_dealt(1) : 7",
		"sender.GetDamageTaken(1e39)", "receiver.GetDamageDealt(1/0)", "sender.GetDamageTaken(missing)"})
		check(!LinuxBootstrap::EvaluateUnitNumericFormula(bad, f.sender, f.receiver).Succeeded(),
			"damage binding rejects invalid arity, unrelated member syntax, injection and unsafe arguments");
	for (const char* good : {"1 ? 7 : receiver.GetDamageTaken(1/0)", "1 ? 7 : sender.GetDamageDealt(missing)",
		"1 ? 7 : pSecond->GetDamageDealt(1)"})
		check(LinuxBootstrap::EvaluateUnitNumericFormula(good, nullptr, nullptr).value == 7,
			"unselected valid history calls need neither endpoint context nor evaluated arguments");
	check(!LinuxBootstrap::EvaluateUnitNumericFormula("sender.GetDamageTaken(1)", nullptr, f.receiver).Succeeded(),
		"selected sender query never substitutes receiver");
	check(!LinuxBootstrap::EvaluateUnitNumericFormula("receiver.GetDamageTaken(1)", f.sender, nullptr).Succeeded(),
		"selected receiver query never substitutes sender");
	CObj<PFBaseUnit> noWorld = Unit(nullptr, NDb::FACTION_FREEZE, 10);
	check(!LinuxBootstrap::EvaluateUnitNumericFormula("sender.GetDamageTaken(1) <= 0", noWorld, nullptr).Succeeded(),
		"unavailable history clock cannot grant a no-recent-damage predicate");
	noWorld->Cleanup();
}

/** Checked history failure must not be converted into a successful zero predicate. */
static void DamageFormulaCorruptionB13(Checks& check)
{
	B8BehaviourForeignWorld world(check); if (!world.world) return;
	DamageHistoryFixtureB13 f(check, world.world); f.Tick(10);
	auto hit = f.Hit("7");
	float& amount = hit.GetPtr()->*DamageHistoryAmountMemberB13(DamageHistoryAmountTagB13());
	for (float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 2147483648.f})
	{
		amount = bad;
		for (const char* expression : {"receiver.GetDamageTaken(1) <= 0", "sender.GetDamageDealt(1) <= 0",
			"min(receiver.GetDamageTaken(1),0)", "pFirst->GetDamageDealt(1) ? 2 : 3"})
			check(!LinuxBootstrap::EvaluateUnitNumericFormula(expression, f.sender, f.receiver).Succeeded(),
				"selected malformed damage history rejects the whole formula before a zero comparison");
		check(LinuxBootstrap::EvaluateUnitNumericFormula("1 ? 4 : receiver.GetDamageTaken(1)", f.sender, f.receiver).value == 4,
			"malformed unselected history remains lazy");
	}
	amount = 7;
	for (float marker : {-1.f, 11.f, std::numeric_limits<float>::quiet_NaN()})
	{
		hit->SetMarkerTime(marker);
		float sentinel = 123;
		check(!f.receiver->TryGetDamageTaken(1, sentinel) && sentinel == 123,
			"checked received history preserves output on malformed markers");
		check(!f.sender->TryGetDamageDealt(1, sentinel) && sentinel == 123,
			"checked sent history preserves output on malformed markers");
		check(!LinuxBootstrap::EvaluateUnitNumericFormula("receiver.GetDamageTaken(1) <= 0", f.sender, f.receiver).Succeeded(),
			"invalid visited history timestamps fail the checked formula");
	}
	hit->SetMarkerTime(10);
	float result = -1;
	check(f.receiver->TryGetDamageTaken(1, result) && result == 7, "checked positive history returns success separately from its number");
	check(f.receiver->TryGetDamageTaken(0, result) && result == 0, "checked finite empty interval is a successful zero");
	amount = -2.75f;
	check(f.receiver->TryGetDamageTaken(1, result) && result == -2, "checked signed original history is not an error sentinel");
	amount = 7;
	check(LinuxBootstrap::EvaluateUnitNumericFormula("receiver.GetDamageTaken(1)", f.sender, f.receiver).value == 7,
		"valid repaired history recovers without cached failure or consumed ring entries");
}

/** Constants, actual damage and dynamic stat enablement consume the same checked call path. */
static void DamageFormulaConsumersB13(Checks& check)
{
	B8BehaviourForeignWorld world(check); if (!world.world) return;
	DamageHistoryFixtureB13 f(check, world.world); f.Tick(10);
	auto* constants = new NDb::UnitConstantsContainer;
	auto* window = new NDb::UnitConstant;
	window->name = "NonCombatTime"; window->var.sString = "1"; constants->vars.push_back(window);
	const_cast<NDb::Ability*>(f.db.GetPtr())->constants = constants;
	// The fixture constructed ability data before adding constants; use its real descriptor reload hook.
	f.data[0]->Reset();
	check(f.data[0]->GetConstant("NonCombatTime") == window, "authored history fixture populates the real ability constant map");
	// Shipped G247 trigger predicates; unrelated named-applicator conditions remain unsupported.
	const char* inCombat = "sender.GetDamageDealt(cNonCombatTime) > 0 || sender.GetDamageTaken(cNonCombatTime) > 0";
	const char* outOfCombat = "sender.GetDamageDealt(cNonCombatTime) <= 0 && sender.GetDamageTaken(cNonCombatTime) <= 0";
	const auto evaluate = [&](const char* expression) {
		return LinuxBootstrap::EvaluateUnitNumericFormula(expression, f.sender, f.receiver, f.data[0]);
	};
	check(evaluate(inCombat).value == 0 && evaluate(outOfCombat).value == 1, "authored combat predicates distinguish initially empty real history");
	f.Hit("7.75");
	check(evaluate(inCombat).value == 1 && evaluate(outOfCombat).value == 0, "authored combat predicates see actual newly memorized damage");
	auto damage = f.Hit("receiver.GetDamageTaken(1) + 1");
	check(damage->GetDamageDealed() == 8 && f.receiver->GetHealth() == 984.25f,
		"real damage factory consumes preceding history without counting its own in-flight hit");
	auto stat = f.Apply(f.Stat("CombatStat", inCombat));
	check(stat->IsEnabled() && f.receiver->GetStatValue(NDb::STAT_STRENGTH) == 23,
		"actual stat modifier enables from checked combat history and ability constants");
	f.Tick(1);
	check(stat->IsAlive() && !stat->IsEnabled() && f.receiver->GetStatValue(NDb::STAT_STRENGTH) == 20,
		"world-ticked stat effect disables exactly at the strict recent-history boundary");
	f.Hit("2"); f.Tick(.125f);
	check(stat->IsEnabled() && f.receiver->GetStatValue(NDb::STAT_STRENGTH) == 23,
		"later real damage reenables the same stat effect without a stale query cache");
	window->var.sString = "sender.GetDamageTaken(cNonCombatTime)";
	check(!evaluate(inCombat).Succeeded(), "history calls preserve existing recursive constant cycle rejection");
}

static void DamageFormulasB13(Checks& check)
{
	DamageFormulaBindingsB13(check);
	DamageFormulaCorruptionB13(check);
	DamageFormulaConsumersB13(check);
}
