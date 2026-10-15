/** Formula bindings must preserve original named-ring scope, order and missing-match context identity. */
static void NamedFormulaB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	ApplicatorQueryFixtureB12 f(check, isolated.world);
	auto dispatch0 = f.Dispatch(0), dispatch1 = f.Dispatch(0);
	if (!dispatch0 || !dispatch1) return;
	auto first = f.Apply(f.Stat("Shared", "true", "10"), 0, dispatch0.GetPtr());
	auto second = f.Apply(f.Stat("Shared", "false", "20"), 0, dispatch1.GetPtr());
	auto third = f.Apply(f.Stat("Shared", "true", "30"), 1);
	auto foreign = f.Apply(f.Stat("Shared", "true", "40"), 2);
	auto summon = f.Apply(f.Stat("Shared", "true", "50"), 3);
	if (!first || !second || !third || !foreign || !summon) return;
	struct Scope { const char* name; ApplicatorSearchType type; };
	const Scope scopes[] = {{"GLOBAL", GLOBAL}, {"UNIT", UNIT}, {"ABILITY", ABILITY},
		{"DISPATCH", DISPATCH}, {"UNIT_AND_SUMMONS", UNIT_AND_SUMMONS}};
	const auto verify = [&]() {
		for (const IMiscFormulaPars* misc : {static_cast<const IMiscFormulaPars*>(first.GetPtr()),
			static_cast<const IMiscFormulaPars*>(second.GetPtr()), static_cast<const IMiscFormulaPars*>(third.GetPtr()),
			static_cast<const IMiscFormulaPars*>(foreign.GetPtr()), static_cast<const IMiscFormulaPars*>(f.data[0].GetPtr())})
		for (const auto& scope : scopes) for (const char* root : {"sender.", "receiver.", "pFirst->", "pSecond->"})
		{
			const auto* unit = std::strcmp(root, "sender.") == 0 || std::strcmp(root, "pFirst->") == 0 ? f.sender.GetPtr() : f.receiver.GetPtr();
			const auto* found = unit->FindApplicator("Shared", misc, scope.type);
			const std::string lookup = std::string(root) + "applShared(" + scope.name + ")";
			for (bool equal : {false, true})
			{
				const auto value = LinuxBootstrap::EvaluateSelectorConditionFormula(lookup + (equal ? " == pMisc" : " != pMisc"), f.sender, f.receiver, misc);
				check(value.Succeeded() && value.value == float(equal ? found == misc : found != misc),
					"B13 named formula identity uses original owner/cast/dispatch/master scope and actual candidate ring");
			}
			const auto value = LinuxBootstrap::EvaluateSelectorConditionFormula(lookup + ".GetVariable(\"Duration\")", f.sender, f.receiver, misc);
			check(value.Succeeded() && value.value == found->GetVariable("Duration"),
				"B13 named variable reads use the original selected object, including context fallback on a missing match");
		}
	};
	verify();
	first->Stop();
	check(f.receiver->FindApplicator("Shared", third.GetPtr(), GLOBAL) == second.GetPtr() && !second->IsEnabled(),
		"B13 stopped named entries disappear while disabled live entries retain original lookup precedence");
	verify();
	const auto missing = LinuxBootstrap::EvaluateApplicatorNumericFormula("receiver.applAbsent(UNIT)==pMisc", *third);
	check(missing.Succeeded() && missing.value == 1, "B13 a missing named effect equals the actual miscellaneous context, not a fabricated null pointer");
	const auto combined = LinuxBootstrap::EvaluateApplicatorNumericFormula(
		"receiver.applShared(GLOBAL)!=pMisc && sender.applAbsent(UNIT)==pMisc ? receiver.applShared(GLOBAL).GetVariable(\"Duration\") : -1", *third);
	check(combined.Succeeded() && combined.value == 20, "B13 multiple named bindings keep boolean grammar and separate receiver/sender identity");
	for (const char* expression : {"receiver.applShared(GLOBAL)", "receiver.applShared(GLOBAL)+1", "receiver.applShared(0)==pMisc",
		"receiver.applShared(GLOBAL)==pMisc+1", "2*receiver.applShared(GLOBAL)==pMisc", "!receiver.applShared(GLOBAL)==pMisc",
		"receiver.applShared(UNKNOWN)==pMisc", "receiver.applshared(GLOBAL)==pMisc", "receiver.applShared(GLOBAL)==nullptr",
		"receiver.applShared(GLOBAL).GetVariable()", "receiver.applShared(GLOBAL).GetVariable(1)",
		"receiver.applShared(GLOBAL).GetVariable(\"Duration\",1)", "receiver.applShared(GLOBAL).GetVariable(\"\")",
		"receiver.applShared(GLOBAL).GetVariable(\"statMod\")", "receiver.applShared(GLOBAL).GetVariable(\"Unreviewed\")",
		"receiver.applShared(GLOBAL).GetVariable(\"Dur\" \"ation\")", "receiver.applShared(GLOBAL).GetVariable(\"Dur\\x61tion\")",
		"receiver.applShared(GLOBAL).SetDuration(2)", "__named_0"})
		for (bool lazy : {false, true})
			check(!LinuxBootstrap::EvaluateApplicatorNumericFormula(std::string(lazy ? "true ? 7 : " : "") + expression, *third).Succeeded(),
				"B13 named queries expose no pointer arithmetic, generic methods, unsafe literals, unknown scopes or internal bindings");
	for (const char* expression : {"sender.applShared(GLOBAL)==pMisc", "receiver.applShared(GLOBAL).GetVariable(\"Duration\")"})
	{
		check(!LinuxBootstrap::EvaluateUnitNumericFormula(expression, f.sender, f.receiver).Succeeded(),
			"B13 named identity and fallback reads require the actual miscellaneous ability or applicator");
		const auto lazy = LinuxBootstrap::EvaluateUnitNumericFormula(std::string("true ? 7 : ") + expression, nullptr, nullptr);
		check(lazy.Succeeded() && lazy.value == 7, "B13 unselected named queries require neither unit nor miscellaneous context");
	}
	const auto absentCandidate = LinuxBootstrap::EvaluateSelectorConditionFormula("receiver.applAbsent(GLOBAL)==pMisc", f.sender, nullptr, third);
	check(!absentCandidate.Succeeded(), "B13 a missing selected candidate cannot be replaced by the applicator receiver");
	std::string many;
	for (unsigned i = 0; i < LinuxBootstrap::NumericFormulaMaxSymbols; ++i)
	{
		if (!many.empty()) many += "+";
		many += "receiver.applShared(GLOBAL).GetVariable(\"Duration\")";
	}
	const auto bounded = LinuxBootstrap::EvaluateApplicatorNumericFormula(many, *third);
	check(bounded.Succeeded() && bounded.value == 20 * LinuxBootstrap::NumericFormulaMaxSymbols, "B13 named bindings admit the bounded descriptor budget");
	check(!LinuxBootstrap::EvaluateApplicatorNumericFormula(many + "+receiver.applShared(GLOBAL).GetVariable(\"Duration\")", *third).Succeeded(),
		"B13 named bindings reject excess descriptors before evaluation");
	const auto grouped = LinuxBootstrap::EvaluateApplicatorNumericFormula("!( receiver.applShared(GLOBAL) == pMisc ) ? 3 : 7", *third);
	check(grouped.Succeeded() && grouped.value == 3, "B13 explicit grouping permits normal boolean negation without admitting pointer negation");
	const auto nested = LinuxBootstrap::EvaluateApplicatorNumericFormula("sender.GetDamageDealtEx(receiver.applShared(GLOBAL).GetVariable(\"Duration\"), DMG_UNIT)", *third);
	check(nested.Succeeded() && nested.value == 0, "B13 nested history arguments share bounded query descriptors without a second expression evaluator");
	for (const char* variable : {"LifeTime", "glyphNumber"})
	{
		const auto value = LinuxBootstrap::EvaluateApplicatorNumericFormula(std::string("receiver.applShared(GLOBAL).GetVariable(\"") + variable + "\")", *third);
		check(value.Succeeded() && value.value == second->GetVariable(variable), "B13 reviewed named variables preserve the original field or absent-field default");
	}

	// Lookup-only fixture abilities are worldless; randomness needs a genuinely world-owned context.
	CObj<PFAbilityData> randomAbility = new PFAbilityData(f.sender.GetPtr(), f.db, NDb::ABILITYTYPEID_SPECIAL, true, false);
	CObj<PFAbilityInstance> randomCast = new PFAbilityInstance(randomAbility.GetPtr(), Target(f.receiver.GetPtr()), false);
	const Target randomTarget(f.receiver.GetPtr()); PFApplCreatePars randomPars(randomCast, randomTarget);
	NDb::Ptr<NDb::BaseApplicator> randomDb = new NDb::HealApplicator; randomPars.pDBAppl = randomDb;
	CObj<PFBaseApplicator> randomApp = CreateApplicator(randomPars);
	check(randomApp && randomApp->Init(), "B13 random named queries use an actual world-owned miscellaneous context");
	auto* buff = dynamic_cast<PFApplBuff*>(second.GetPtr());
	check(buff != nullptr, "B13 named variable fixture exposes the original buff duration setter");
	if (buff && randomApp) for (bool invalid : {false, true})
	{
		buff->SetDuration(invalid ? std::numeric_limits<float>::quiet_NaN() : 20.f);
		NRandom::RandomGenerator expected(*isolated.world->GetRndGen()); const int roll = invalid ? 0 : expected.Next(1, 3);
		const auto value = LinuxBootstrap::EvaluateApplicatorNumericFormula("random(1,3)+receiver.applShared(GLOBAL).GetVariable(\"Duration\")", *randomApp);
		check(value.Succeeded() == !invalid && (invalid || value.value == roll + 20), "B13 named variable reads reject nonfinite original getters without inventing values");
		check(isolated.world->GetRndGen()->Next(0, 1000) == expected.Next(0, 1000), "B13 rejected named variable reads roll back preceding randomness");
	}
	if (buff) buff->SetDuration(20);
	std::printf("B13 named formula checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Read the original damage fields without recursively precaching the containing Hero resource. */
struct NamedDamageFieldsB13
{
	NDb::DamageApplicator* db;
	int operator&(IXmlSaver& saver)
	{
		saver.Add("applyTarget", &db->applyTarget); saver.Add("cancelInvisibility", &db->cancelInvisibility);
		saver.Add("damage", &db->damage); saver.Add("damageMode", &db->damageMode); saver.Add("damageType", &db->damageType);
		saver.Add("enabled", &db->enabled); saver.Add("formulaName", &db->formulaName);
		saver.Add("timeLimiter", &db->timeLimiter);
		return 0;
	}
};

/** Exercise Plane's original damage fields while real named status/ward lifecycles gate them. */
static void AuthoredNamedFormulaB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		const auto talentDb = NDb::Get<NDb::Talent>(NDb::DBID("/Items/Talents/Class/Plane/Ability_A2.TALENT.xdb"));
		const auto statusDb = NDb::Get<NDb::StatusApplicator>(NDb::DBID("/Items/Talents/Class/Plane/status_A2_AddAttackSpeed.APLR.xdb"));
		const auto constant = NDb::Get<NDb::UnitConstant>(NDb::DBID("/Items/Talents/Class/Plane/const_A2_Damage.xdb"));
		auto* attackRaw = B11AttackDescriptor(0); NDb::Ptr<NDb::BaseAttack> attackDb = attackRaw;
		auto* constants = new NDb::UnitConstantsContainer; constants->vars.push_back(constant); attackRaw->constants = constants;
		NDb::Ptr<NDb::DamageApplicator> damageDb;
		CObj<Stream> xml = files->OpenFile("Heroes/Plane/_.HROB.xdb", FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
		if (xml && xml->IsOk())
		{
			CObj<IXmlSaver> saver = CreateXmlSaver(xml, true); bool found = true;
			for (const auto& part : {std::make_pair("Hero", 0), {"attackAbility", 0}, {"BaseAttack", 0}, {"applicators", 0},
				{"Item", 1}, {"SpellSwitchApplicator", 0}, {"spells", 0}, {"Item", 1}, {"Spell", 0}, {"applicators", 0},
				{"Item", 1}, {"SpellApplicator", 0}, {"spell", 0}, {"Spell", 0}, {"applicators", 0}, {"Item", 2},
				{"VariableProxyApplicator", 0}, {"applicators", 0}, {"Item", 2}, {"applicator", 0}})
				found = found && saver->StartChunk(part.first, part.second);
			if (found)
			{
				auto* raw = new ApplicatorQueryDbB12<NDb::DamageApplicator>("/Mock/PlaneNamedDamage.xdb");
				NamedDamageFieldsB13 fields{raw}; saver->Add("DamageApplicator", &fields); damageDb = raw;
			}
		}
		check(talentDb && statusDb && constant && damageDb && statusDb->formulaName == "A2PlaneStatus"
			&& damageDb->formulaName == "Plane_A2Damage" && damageDb->damage.sString == "cA2_Damage",
			"B13 named fixture loads original Plane A2 status, damage fields and constant without replacing their formulas");
		B8BehaviourForeignWorld isolated(check);
		if (talentDb && statusDb && constant && damageDb && isolated.world)
		{
			auto hero = CommandHeroB10(isolated.world);
			auto victim = Unit(isolated.world, NDb::FACTION_BURN, 40);
			hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); victim->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
			victim->GetStat(NDb::STAT_LIFE)->SetCoreValue(1000); victim->SetHealth(1000);
			hero->InstallTalent(talentDb, false); hero->AddGold(10000);
			check(hero->ActivateTalent(0, 0), "B13 named damage gate uses the real purchased Plane A2 talent");
			CObj<PFBaseAttackData> attack = new PFBaseAttackData(hero.GetPtr(), attackDb);
			CObj<PFAbilityInstance> hit = new PFAbilityInstance(attack.GetPtr(), Target(victim.GetPtr()), false);
			CObj<PFAbilityInstance> cast = new PFAbilityInstance(hero->GetTalent(0, 0), Target(hero.GetPtr()), false);
			const auto apply = [&](const NDb::BaseApplicator* db, PFAbilityInstance* instance, PFBaseUnit* receiver) {
				const Target target(receiver); PFApplCreatePars pars(instance, target); pars.pDBAppl = db; pars.pWorld = isolated.world;
				CObj<PFBaseApplicator> app = CreateApplicator(pars);
				check(app != nullptr, "B13 authored named effects use their original production factories");
				if (app) ActivateApplicator(app, instance);
				return app;
			};
			const auto damage = [&](bool allowed) {
				const float beforeLife = victim->GetLife();
				auto app = apply(damageDb, hit.GetPtr(), victim.GetPtr());
				const auto* dealt = dynamic_cast<const PFApplDamage*>(app.GetPtr());
				const float delta = beforeLife - victim->GetLife();
				check(dealt && (allowed ? delta > 0 && std::fabs(delta - dealt->GetDamageDealed()) < .0001f : delta == 0),
					"B13 shipped Plane A2 damage changes actual health only with a live sender status, purchased talent and no receiver ward");
			};
			damage(false);
			auto status = apply(statusDb, cast.GetPtr(), hero.GetPtr());
			check(status && status->IsAlive() && hero->FindApplicator("A2PlaneStatus", attack.GetPtr(), GLOBAL) == status.GetPtr(),
				"B13 original Plane status joins its actual sender named ring");
			damage(true);
			auto ward = apply(ApplicatorQueryFixtureB12::Stat("Ward"), hit.GetPtr(), victim.GetPtr());
			damage(false);
			if (ward) ward->Stop();
			damage(true);
			for (int i = 0; i < 62; ++i) isolated.world->Step(.1f, .1f);
			check(status && !status->IsAlive(), "B13 natural six-second Plane status expiry removes the damage-enabling named entry");
			damage(false);
			status = apply(statusDb, cast.GetPtr(), hero.GetPtr());
			hero->GetTalentsSet()->CleanSet();
			damage(false);
			if (status) status->Stop();
			hero->Cleanup(); victim->Cleanup();
		}
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 authored named formula checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
