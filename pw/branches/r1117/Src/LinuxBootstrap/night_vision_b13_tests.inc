/** Authored return-expression suffixes must not enlarge the scalar grammar or commit failed RNG. */
static void FormulaSuffixB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	NaftaFixtureB13 f(isolated.world);
	NDb::Ptr<NDb::BaseApplicator> db = new NDb::HealApplicator; auto app = f.Create(db);
	for (const char* suffix : {";", " ; \t\r\n", "; // original annotation", ";\t// ; random(1,9) /* ignored */", "; // note\r\n \t"})
	{
		const auto scalar = LinuxBootstrap::EvaluateApplicatorNumericFormula(std::string("2+3*4") + suffix, *app);
		const auto identity = LinuxBootstrap::EvaluateApplicatorNumericFormula(
			std::string("receiver.applAbsent(GLOBAL) == pMisc") + suffix, *app);
		check(scalar.Succeeded() && scalar.value == 14 && identity.Succeeded() && identity.value == 1,
			"B13 one authored trailing semicolon and optional line annotation preserve scalar precedence and named identity");
		const auto lazy = LinuxBootstrap::EvaluateUnitNumericFormula(std::string("true ? 7 : mIsNight") + suffix, nullptr, nullptr);
		check(lazy.Succeeded() && lazy.value == 7, "B13 suffix normalization preserves inactive missing-context branches");
	}
	for (const char* expression : {";", "; // empty", "1;;", "1;2", "1;\n2", "1 // no terminator",
		"1//2", "1/* note */+2", "1; /* block */", "1; // note\n+2", "1; // note\r2", "min(1;2)",
		"random(1,9); random(1,9)", "true ? 7 : receiver.SetLife(1); // invalid inactive call",
		"receiver.applAbsent(GLOBAL) == pMisc + 1; // pointer arithmetic", "__named_0; // reserved",
		"true ? 7 : (1;2)", "1; // continued\\\n", "tal(\"unterminated; // note)", "1; // note\n// second line"})
	{
		NRandom::RandomGenerator expected(*isolated.world->GetRndGen());
		check(!LinuxBootstrap::EvaluateApplicatorNumericFormula(expression, *app).Succeeded()
			&& GlyphRngEqualB13(*isolated.world->GetRndGen(), expected),
			"B13 suffix support rejects statements, embedded comments, invalid inactive syntax and continuations without consuming RNG");
	}
	for (const char* id : {"missing;//literal", "missing; //literal", "missing//literal"})
	{
		const auto result = LinuxBootstrap::EvaluateApplicatorNumericFormula(std::string("tal(\"") + id + "\"); // annotation", *app);
		check(result.Succeeded() && result.value == 0, "B13 suffix scanning preserves semicolons and slashes inside allowed talent ID literals");
	}
	const std::string bounded = "1; //" + std::string(LinuxBootstrap::NumericFormulaMaxLength - 5, 'x');
	const auto limit = LinuxBootstrap::EvaluateApplicatorNumericFormula(bounded, *app);
	check(limit.Succeeded() && limit.value == 1, "B13 the original maximum source length includes annotations");
	check(!LinuxBootstrap::EvaluateApplicatorNumericFormula(bounded + "x", *app).Succeeded(),
		"B13 stripping annotations cannot bypass the original source-length budget");
	check(!LinuxBootstrap::EvaluateApplicatorNumericFormula(std::string("1; //") + '\0' + "hidden", *app).Succeeded(),
		"B13 embedded NUL is rejected even inside an annotation");
	NRandom::RandomGenerator expected(*isolated.world->GetRndGen()); const int draw = expected.Next(1, 9);
	const auto randomValue = LinuxBootstrap::EvaluateApplicatorNumericFormula("random(1,9); // exactly one draw", *app);
	check(randomValue.Succeeded() && randomValue.value == draw && GlyphRngEqualB13(*isolated.world->GetRndGen(), expected),
		"B13 a successful annotated formula commits exactly its selected random draw");
	expected = *isolated.world->GetRndGen();
	check(!LinuxBootstrap::EvaluateApplicatorNumericFormula("random(1,9)+cMissing; // rollback", *app).Succeeded()
		&& GlyphRngEqualB13(*isolated.world->GetRndGen(), expected), "B13 unresolved annotated formulas still roll back RNG");
	check(!LinuxBootstrap::EvaluateNumericFormula("1; // context-only syntax").Succeeded(),
		"B13 the standalone numeric parser continues rejecting statement syntax");
	std::printf("B13 authored formula suffix checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Transition formulas must use live context and reject errors before activating another child. */
static void ProgramTransitionsB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	const auto descriptor = []() {
		auto* db = new NDb::ProgramApplicator;
		db->enabled.sString = db->startCondition.sString = "true"; db->lifeTime.sString = "-1";
		db->behaviorFlags = NDb::BUFFBEHAVIOR_REMOVECHILDREN;
		for (const char* amount : {"1", "3"})
		{
			NDb::ApplicatorToExecute entry; auto* child = WrapperFactoryFixtureB12::Stat(amount);
			child->lifeTime.sString = "-1"; entry.applicator = child;
			entry.delay.sString = "-1"; entry.continueCondition.sString = entry.stopCondition.sString = "false";
			entry.continueEvents = NDb::PARENTNOTIFICATIONFLAGS_DONE; entry.stopEvents = NDb::PARENTNOTIFICATIONFLAGS_ZERO;
			db->applicators.push_back(entry);
		}
		return db;
	};
	for (int mode : {0, 1, 2})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		NaftaFixtureB13 f(isolated.world); NDb::Ptr<NDb::ProgramApplicator> db = descriptor();
		auto& entry = const_cast<NDb::ProgramApplicator*>(db.GetPtr())->applicators[0];
		if (mode == 0) entry.continueCondition.sString = "receiver.vGate > 0";
		if (mode == 1) entry.stopCondition.sString = "receiver.vGate > 0";
		if (mode == 2) entry.delay.sString = "cWrapperBonus - 1.75; // quarter second";
		auto app = f.Create(db); check(app && f.Activate(app) && f.Delta() == 1, "B13 transition fixture starts only its first real stat child");
		isolated.world->Step(.125f, .125f);
		check(app && app->IsAlive() && f.Delta() == 1, "B13 false conditions and a newly armed delay keep the first child active");
		if (mode < 2) f.receiver->GetVariableVWM("Gate")->SetBaseValue(1);
		else
		{
			isolated.world->Step(.25f, .25f);
			check(app && app->IsAlive() && f.Delta() == 1, "B13 the original program delay advances only below zero, not at equality");
		}
		isolated.world->Step(.125f, .125f);
		check(app && (mode == 1 ? !app->IsAlive() && f.Delta() == 0 : app->IsAlive() && f.Delta() == 4),
			"B13 live continuation, stop and computed delay follow original program state and child-cleanup semantics");
	}
	for (int field : {0, 1, 2}) for (const char* expression : {"cMissing", "1/0", "1e39", "random(1,9)+cMissing", "true ? 7 : unsupported()", "compiled-only"})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		NaftaFixtureB13 f(isolated.world); NDb::Ptr<NDb::ProgramApplicator> db = descriptor();
		auto& entry = const_cast<NDb::ProgramApplicator*>(db.GetPtr())->applicators[0];
		if (field == 0) entry.stopCondition.sString = expression;
		if (field == 1) entry.continueCondition.sString = expression;
		if (field == 2) entry.delay.sString = expression;
		if (std::strcmp(expression, "compiled-only") == 0)
		{
			ExecutableString& selected = field == 0 ? static_cast<ExecutableString&>(entry.stopCondition)
				: field == 1 ? static_cast<ExecutableString&>(entry.continueCondition) : static_cast<ExecutableString&>(entry.delay);
			selected.sString.clear(); selected.compiledString = "unsupported";
		}
		auto app = f.Create(db); check(app && f.Activate(app) && f.Delta() == 1, "B13 invalid-transition fixture enters the original first child");
		NRandom::RandomGenerator expected(*isolated.world->GetRndGen()); isolated.world->Step(.1f, .1f);
		check(app && !app->IsAlive() && f.Delta() == 0 && GlyphRngEqualB13(*isolated.world->GetRndGen(), expected),
			"B13 invalid selected program transitions retire without a later child, fallback mutation or a failed formula's RNG draw");
	}
	for (int mode : {0, 1, 2, 3})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		NaftaFixtureB13 f(isolated.world); NDb::Ptr<NDb::ProgramApplicator> db = descriptor();
		auto& entry = const_cast<NDb::ProgramApplicator*>(db.GetPtr())->applicators[0];
		entry.stopCondition.sString = entry.continueCondition.sString = entry.delay.sString = "";
		if (mode == 1)
		{
			entry.stopCondition.sString = "receiver.vGate == 0";
			entry.continueCondition.sString = entry.delay.sString = "random(1,9)+cMissing";
		}
		if (mode == 2) entry.continueCondition.sString = "false ? cMissing : true";
		if (mode == 3) entry.delay.sString = "random(1,1)*.25";
		auto app = f.Create(db); check(app && f.Activate(app), "B13 ordered program fixture activates with original empty-field defaults");
		NRandom::RandomGenerator expected(*isolated.world->GetRndGen()); if (mode == 3) expected.Next(1, 1);
		isolated.world->Step(.125f, .125f);
		check(app && app->IsAlive() == (mode != 1) && f.Delta() == (mode == 1 ? 0 : mode == 2 ? 4 : 1)
			&& GlyphRngEqualB13(*isolated.world->GetRndGen(), expected),
			"B13 empty transitions retain defaults, stop short-circuits later formulas, and valid lazy conditions preserve RNG order");
		if (mode == 3)
		{
			entry.delay.sString = "cMissing"; isolated.world->Step(.25f, .25f);
			check(app && app->IsAlive() && f.Delta() == 1 && GlyphRngEqualB13(*isolated.world->GetRndGen(), expected),
				"B13 an armed original timer does not reevaluate or redraw its delay expression");
			isolated.world->Step(.125f, .125f);
			check(app && app->IsAlive() && f.Delta() == 4 && GlyphRngEqualB13(*isolated.world->GetRndGen(), expected),
				"B13 the armed timer advances through its original expiration without consulting a changed delay formula");
		}
	}
	std::printf("B13 program transition checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Execute the shipped graph, retaining its real selector, duplicate guard, delays and child cleanup. */
static void NightVisionAuthoredB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed; MapClockSwitchScopeB13 switches;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		const auto db = NDb::Get<NDb::SpellApplicator>(NDb::DBID("/Maps/Multiplayer/MOBA/Mains/VisionDelta.APLR.xdb"));
		const auto* program = db && db->spell && db->spell->applicators.size() == 1
			? dynamic_cast<const NDb::ProgramApplicator*>(db->spell->applicators[0].GetPtr()) : nullptr;
		check(program && program->applicators.size() == 6 && program->formulaName == "NightVisionProgram"
			&& program->startCondition.sString == "receiver.applNightVisionProgram(GLOBAL) == pMisc; // no double reduction",
			"B13 unchanged original vision graph retains all six modifiers and its annotated identity guard");
		if (program) for (bool burn : {false, true})
		{
			B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
			NaftaFixtureB13 f(isolated.world); f.sender->ChangeFaction(burn ? NDb::FACTION_BURN : NDb::FACTION_FREEZE);
			auto enemy = B9MaleHero(isolated.world, NDb::FACTION_BURN);
			enemy->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
			auto creep = Unit(isolated.world, NDb::FACTION_BURN, 40); creep->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
			PFBaseUnit* units[] = {f.sender.GetPtr(), f.receiver.GetPtr(), enemy.GetPtr(), creep.GetPtr()};
			for (auto* unit : units)
				unit->GetStat(NDb::STAT_VISIBILITYRANGE)->SetCoreValue(15);
			const auto step = [&](int count) { for (int i = 0; i < count; ++i) isolated.world->Step(.1f, .1f); };
			const auto broadcast = [&]() {
				auto root = f.Create(db); check(root && f.Activate(root), "B13 original vision spell broadcasts through the actual hero selector and dispatch");
			};
			const auto ranges = [&](float normal, float ignored) {
				std::printf("B13 night vision burn=%d night=%d ranges=%g/%g/%g creep=%g expected=%g/%g\n", burn,
					isolated.world->IsNight(), f.sender->GetVisibilityRange(), f.receiver->GetVisibilityRange(), enemy->GetVisibilityRange(),
					creep->GetVisibilityRange(), normal, ignored);
				check(f.sender->GetVisibilityRange() == 15 && f.receiver->GetVisibilityRange() == normal
					&& enemy->GetVisibilityRange() == ignored && creep->GetVisibilityRange() == 15,
					"B13 original night-vision stages affect both hero factions, honor IgnoreNight and exclude the caster and ordinary creeps");
			};
			isolated.world->GetDayNightController()->Switch(NDb::DAYNIGHTSTATE_NIGHT);
			enemy->GetVariableVWM("IgnoreNight")->SetBaseValue(1);
			broadcast(); ranges(14, 15);
			const CObj<PFBaseApplicator> first = const_cast<PFBaseApplicator*>(dynamic_cast<const PFBaseApplicator*>(
				f.receiver->FindApplicator("NightVisionProgram", f.ability.GetPtr(), GLOBAL)));
			check(first && first->IsAlive(), "B13 original night program joins the live named ring");
			broadcast(); ranges(14, 15);
			check(first && f.receiver->FindApplicator("NightVisionProgram", f.ability.GetPtr(), GLOBAL) == first.GetPtr(),
				"B13 repeated broadcasts preserve the existing program rather than stacking reduction");
			step(12); ranges(13, 15); step(12); ranges(12, 15); step(20); ranges(12, 15);
			HeroCorpseKillB13(isolated.world, f.receiver);
			check(f.receiver->IsDead() && first && first->IsAlive() && f.receiver->GetVisibilityRange() == 12,
				"B13 original DONTSTOPONDEATH flags retain the night program and modifiers through corpse cleanup");
			f.receiver->Resurrect();
			check(!f.receiver->IsDead() && first && first->IsAlive() && f.receiver->GetVisibilityRange() == 12,
				"B13 original night visibility survives native resurrection without restarting or duplicating its program");
			isolated.world->GetDayNightController()->Switch(NDb::DAYNIGHTSTATE_DAY);
			step(1); ranges(13, 15); step(12); ranges(14, 15); step(12); ranges(15, 15);
			check(first && first->IsAlive(), "B13 the original program waits through daytime after restoring range");
			isolated.world->GetDayNightController()->Switch(NDb::DAYNIGHTSTATE_NIGHT); step(1);
			check(first && !first->IsAlive() && f.receiver->FindApplicator("NightVisionProgram", f.ability.GetPtr(), GLOBAL) == f.ability.GetPtr(),
				"B13 next night naturally completes the prior program and removes its named entry");
			ranges(15, 15);
			enemy->GetVariableVWM("IgnoreNight")->SetBaseValue(0); broadcast(); step(24); ranges(12, 12);
			const CObj<PFBaseApplicator> second = const_cast<PFBaseApplicator*>(dynamic_cast<const PFBaseApplicator*>(
				f.receiver->FindApplicator("NightVisionProgram", f.ability.GetPtr(), GLOBAL)));
			check(second && second != first && second->IsAlive(), "B13 a later night admits a fresh original program after retirement");
			if (second) second->Stop();
			check(f.receiver->GetVisibilityRange() == 15, "B13 original REMOVECHILDREN behavior restores visibility on program cancellation");
			enemy->Cleanup(); creep->Cleanup();
		}
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 original night vision checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
