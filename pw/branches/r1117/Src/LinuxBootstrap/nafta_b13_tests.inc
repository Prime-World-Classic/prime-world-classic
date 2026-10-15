/** Observe exact native balances without changing storage, mutation or the production type layout. */
struct NaftaBalanceUnitB13 : PFBaseUnit
{
	explicit NaftaBalanceUnitB13(PFWorld* world) : PFBaseUnit(world, CVec3(5, 5, 0), nullptr) {}
	float Balance() const { return gold; }
	/** Type labels alone cannot grant concrete hero-only functionality. */
	void InitializeAs(const NDb::Unit* db, NDb::EUnitType type)
	{
		InitData init; init.pObjectDesc = db; init.type = type;
		init.faction = NDb::FACTION_FREEZE; init.playerId = -1; Initialize(init);
	}
};

/** Register the real ability so random formulas have the same world as their owner. */
struct NaftaFixtureB13 : WrapperFactoryFixtureB12
{
	explicit NaftaFixtureB13(PFWorld* world) : WrapperFactoryFixtureB12(world)
	{
		ability = new PFAbilityData(sender.GetPtr(), abilityDb, NDb::ABILITYTYPEID_SPECIAL, true, false);
		instance = new PFAbilityInstance(ability, Target(receiver.GetPtr()), true);
	}
};

/** Exercise original hero-only awards through actual factory, activation and retirement. */
static void NaftaApplicatorB13(Checks& check, PFWorld* world)
{
	const int before = check.count, failed = check.failed;
	struct Case { const char* expression; float expected; };
	for (const Case test : {Case{"3", 3}, {"2.75", 2.75f}, {"sLife/100+tLife/100+cWrapperBonus", 14},
		{"f2l(7.9)", 7}, {"true ? 13 : cMissing", 13}, {"false ? cMissing : 17", 17},
		{"", 0}, {"0", 0}, {"-3", 0}, {"cMissing", 0}, {"1/0", 0}, {"1e39", 0}, {"compiled-only", 0}})
	{
		NaftaFixtureB13 f(world); f.receiver->AddGold(100);
		auto* raw = new NDb::AddNaftaApplicator; raw->enabled.sString = "true"; raw->nafta.sString = test.expression;
		if (std::strcmp(test.expression, "compiled-only") == 0) { raw->nafta.sString = ""; raw->nafta.compiledString = "unsupported"; }
		NDb::Ptr<NDb::AddNaftaApplicator> db = raw; auto app = f.Create(db);
		check(app && f.Activate(app), "B13 nafta factory admits an actual hero and retires the instant effect");
		check(f.receiver->GetGold() == int(100 + test.expected) && f.sender->GetGold() == 0,
			"B13 checked award uses actual endpoints/constants and cannot charge unsupported negative requests");
		check(app && app->GetVariable("nafta") == test.expected && app->GetVariable("Nafta") == test.expected,
			"B13 original lowercase nafta receipt and retained native alias report the accepted award");
		check(f.instance->GetActiveApplicatorsCount() == 0, "B13 instant award balances ability ownership on every outcome");
	}
	for (const char* condition : {"true", "", "false", "sLife < rLife", "sLife > rLife", "cMissing", "1/0", "compiled-only"})
	{
		NaftaFixtureB13 f(world); auto* raw = new NDb::AddNaftaApplicator;
		raw->enabled.sString = condition; raw->nafta.sString = "random(10,20)";
		if (std::strcmp(condition, "compiled-only") == 0) { raw->enabled.sString = ""; raw->enabled.compiledString = "unsupported"; }
		NDb::Ptr<NDb::AddNaftaApplicator> db = raw;
		NRandom::RandomGenerator expectedRng(*world->GetRndGen());
		const bool enabled = !*condition || std::strcmp(condition, "true") == 0 || std::strcmp(condition, "sLife < rLife") == 0;
		const int expected = enabled ? expectedRng.Next(10, 20) : 0;
		auto app = f.Create(db); check(app && f.Activate(app), "B13 conditional award follows real activation");
		std::printf("B13 conditional nafta enabled='%s' gold=%d expected=%d rng=%d world=%d/%d\n", condition,
			f.receiver->GetGold(), expected, GlyphRngEqualB13(*world->GetRndGen(), expectedRng),
			app && app->GetWorld() == world, f.sender->GetWorld() == world);
		check(f.receiver->GetGold() == expected && GlyphRngEqualB13(*world->GetRndGen(), expectedRng),
			"B13 disabled or invalid award conditions cannot consume money or a random draw");
	}
	for (NDb::EUnitType type : {NDb::UNITTYPE_BUILDING, NDb::UNITTYPE_CREEP, NDb::UNITTYPE_HEROMALE})
	{
		NaftaFixtureB13 f(world); auto seed = Unit(world, NDb::FACTION_FREEZE, 40);
		CObj<NaftaBalanceUnitB13> impostor = new NaftaBalanceUnitB13(world);
		impostor->InitializeAs(seed->DbUnitDesc(), type);
		auto* raw = new NDb::AddNaftaApplicator; raw->nafta.sString = "3";
		NDb::Ptr<NDb::AddNaftaApplicator> db = raw; const Target target(impostor.GetPtr());
		PFApplCreatePars pars(f.instance, target); pars.pDBAppl = db; auto app = CreateApplicator(pars);
		if (app) ActivateApplicator(app, f.instance);
		check((type == NDb::UNITTYPE_HEROMALE || !app) && impostor->GetGold() == 0,
			"B13 award rejects nonhero targets and cannot trust a forged hero unit-type label");
	}
	for (bool clone : {false, true})
	{
		NaftaFixtureB13 f(world); PFBaseHero::SpawnInfo spawn; spawn.pHero = f.receiver->GetDbHero(); spawn.isClone = clone;
		spawn.placement = Placement(CVec3(5, 5, 0), QNULL, CVec3(1, 1, 1));
		CObj<PFBaseHero> female = new PFBaseHero(world, spawn, NDb::UNITTYPE_HEROFEMALE, NDb::FACTION_FREEZE, NDb::FACTION_FREEZE);
		auto* raw = new NDb::AddNaftaApplicator; raw->nafta.sString = "3";
		NDb::Ptr<NDb::AddNaftaApplicator> db = raw; const Target target(female.GetPtr());
		PFApplCreatePars pars(f.instance, target); pars.pDBAppl = db; auto app = CreateApplicator(pars);
		check(app && ActivateApplicator(app, f.instance) && female->GetGold() == 3 && app->GetVariable("nafta") == 3,
			"B13 original hero mask includes female heroes and clones without requiring player attachment");
		female->Cleanup();
	}
	std::printf("B13 nafta applicator checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Gold is exposed as int by the production API; invalid float balances must never be published. */
static void NaftaBalanceB13(Checks& check, PFWorld* world)
{
	const int before = check.count, failed = check.failed;
	for (float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
		-std::numeric_limits<float>::infinity(), std::numeric_limits<float>::max(), 2147483648.f})
	{
		CObj<NaftaBalanceUnitB13> unit = new NaftaBalanceUnitB13(nullptr);
		unit->OnAddGold(unit.GetPtr(), 100.75f);
		unit->OnAddGold(unit.GetPtr(), bad);
		check(unit->Balance() == 100.75f, "B13 invalid or int-overflowing deposits preserve the exact existing balance");
		if (std::isfinite(bad) && bad > 0) continue;
		const float removed = unit->OnRemoveGold(unit.GetPtr(), bad);
		check(removed == 0 && unit->Balance() == 100.75f, "B13 nonfinite withdrawals preserve both balance and removal receipt");
	}
	{
		CObj<NaftaBalanceUnitB13> unit = new NaftaBalanceUnitB13(nullptr);
		const float limit = std::nextafter(2147483648.f, 0.f);
		unit->OnAddGold(unit.GetPtr(), limit);
		check(unit->Balance() == limit, "B13 largest float representable by GetGold's int conversion remains usable");
		unit->OnAddGold(unit.GetPtr(), 128);
		check(unit->Balance() == limit, "B13 finite deposits cannot overflow the integer-facing balance through addition");
		unit->OnRemoveGold(unit.GetPtr(), limit);
		unit->OnAddGold(unit.GetPtr(), 7.75f); unit->OnAddGold(unit.GetPtr(), -.5f);
		check(unit->Balance() == 7.25f && unit->OnRemoveGold(unit.GetPtr(), 2.5f) == 2.5f && unit->Balance() == 4.75f,
			"B13 finite fractional deposit, existing signed adjustment and withdrawal retain native arithmetic");
		check(unit->OnRemoveGold(unit.GetPtr(), -3) == 0 && unit->Balance() == 4.75f,
			"B13 negative withdrawal remains rejected rather than adopting the Windows subtraction sign bug");
		check(unit->OnRemoveGold(unit.GetPtr(), 100) == 4.75f && unit->Balance() == 0,
			"B13 ordinary overdraw still returns only the amount actually removed");
	}
	std::printf("B13 nafta balance checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Distinguish original target variables from redirected receivers, with closed syntax and lazy reads. */
static void NaftaTargetVariableB13(Checks& check, PFWorld* world)
{
	const int before = check.count, failed = check.failed;
	NaftaFixtureB13 f(world);
	f.sender->GetVariableVWM("Clock_2")->SetBaseValue(7);
	f.receiver->GetVariableVWM("Clock_2")->SetBaseValue(19);
	auto* raw = new NDb::FlagsApplicator; raw->applyTarget = NDb::APPLICATORAPPLYTARGET_ABILITYOWNER;
	NDb::Ptr<NDb::BaseApplicator> db = raw; auto app = f.Create(db);
	check(app && app->GetReceiver() == f.sender.GetPtr(), "B13 source target is intentionally different from the application receiver");
	if (!app) return;
	for (const char* expression : {"$ApplTarget->GetVariable(\"Clock_2\")", "$ApplTarget->GetVariable ( \"Clock_2\" )",
		"false ? cMissing : $ApplTarget->GetVariable(\"Clock_2\")"})
	{
		const auto result = LinuxBootstrap::EvaluateApplicatorNumericFormula(expression, *app);
		check(result.Succeeded() && result.value == 19, "B13 target macro reads original GetObject(Target), not owner or redirected receiver");
	}
	const auto missing = LinuxBootstrap::EvaluateApplicatorNumericFormula("$ApplTarget->GetVariable(\"Unwritten\")", *app);
	check(missing.Succeeded() && missing.value == 0 && !f.receiver->SearchVariableVWM("Unwritten"),
		"B13 target variable preserves missing-name zero without allocating a variable");
	f.receiver->GetVariableVWM("Clock_2")->SetBaseValue(23);
	check(LinuxBootstrap::EvaluateApplicatorNumericFormula("$ApplTarget->GetVariable(\"Clock_2\")", *app).value == 23,
		"B13 target reads cannot cache values across evaluations");
	for (const char* invalid : {"$ApplTarget", "$Other->GetVariable(\"Clock_2\")", "$ApplTarget->GetVariable(\"\")",
		"$ApplTarget->GetVariable(\"1Clock\")", "$ApplTarget->GetVariable(\"Clock-2\")", "$ApplTarget->GetVariable(Clock_2)",
		"$ApplTarget->GetVariable(\"Clock\" \"_2\")", "$ApplTarget->GetVariable(\"Clock_2\",0)",
		"$ApplTarget->GetVariable(\"Clock_2\").member", "$ApplTarget->GetVariable(\"Clock_2\")=3",
		"$ApplTarget->GetVariable(\"Clock_2\")[0]", "$ApplTarget->SetVariable(\"Clock_2\",3)",
		"$ApplTargetX->GetVariable(\"Clock_2\")", "__applvar_Clock_2", "true ? 1 : __applvar_Clock_2"})
		check(!LinuxBootstrap::EvaluateApplicatorNumericFormula(invalid, *app).Succeeded(),
			"B13 target binding rejects pointer exposure, arbitrary methods/strings and reserved transport injection");
	check(!LinuxBootstrap::EvaluateUnitNumericFormula("$ApplTarget->GetVariable(\"Clock_2\")", f.sender, f.receiver, f.ability).Succeeded(),
		"B13 target variable cannot substitute an ability or ordinary unit context for a real applicator");
	const auto lazy = LinuxBootstrap::EvaluateUnitNumericFormula("true ? 11 : $ApplTarget->GetVariable(\"Clock_2\")", nullptr, nullptr);
	check(lazy.Succeeded() && lazy.value == 11, "B13 unselected valid target syntax retains lazy context checks");
	const Target ground(CVec2(6, 6)); PFApplCreatePars pars(f.instance, ground); pars.pDBAppl = db;
	auto groundApp = CreateApplicator(pars); check(groundApp && groundApp->GetReceiver() == f.sender.GetPtr(), "B13 ground-target effect retains a separate valid owner receiver");
	if (groundApp)
	{
		const NRandom::RandomGenerator expectedRng(*world->GetRndGen());
		check(!LinuxBootstrap::EvaluateApplicatorNumericFormula("random(1,9)+$ApplTarget->GetVariable(\"Clock_2\")", *groundApp).Succeeded()
			&& GlyphRngEqualB13(*world->GetRndGen(), expectedRng), "B13 selected position target rejects lookup and rolls back staged random draws");
	}
	std::printf("B13 target variable checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** The original main-building query deliberately bypasses ordinary ability target filtering. */
struct NaftaRejectConditionB13 : ITargetCondition
{
	mutable int calls = 0;
	bool operator()(const Target&, const PFAbilityData*) const override { ++calls; return false; }
};

/** Use the actual voxel traversal as the ordering oracle, including missing and removed buildings. */
static void NaftaMainBuildingB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	B8BehaviourForeignWorld f(check), other(check); if (!f.world || !other.world) return;
	auto hero = B9MaleHero(f.world, NDb::FACTION_FREEZE);
	const auto create = [&](PFWorld* world, NDb::EFaction faction, float x) {
		auto* db = new NDb::MainBuilding; db->stats = hero->DbUnitDesc()->stats; db->faction = faction;
		NDb::AdvMapObject object; object.gameObject = db; object.offset = CPlacement(CVec3(x, 10, 0), QNULL, CVec3(1, 1, 1));
		return CObj<PFMainBuilding>(new PFMainBuilding(world, object));
	};
	auto foreign = create(other.world, NDb::FACTION_FREEZE, 5);
	auto enemy = create(f.world, NDb::FACTION_BURN, 10);
	NDb::Ptr<NDb::MainBuildingTargetSelector> db = new NDb::MainBuildingTargetSelector;
	CObj<PFTargetSelector> selector = db->Create(f.world);
	auto* single = dynamic_cast<PFSingleTargetSelector*>(selector.GetPtr());
	check(single != nullptr, "B13 main-building factory creates the original selector"); if (!single) return;
	const Target request(hero.GetPtr()); NaftaRejectConditionB13 reject;
	PFTargetSelector::RequestParams pars(hero.GetPtr(), nullptr, request, reject);
	Target selected(CVec2(7, 8));
	check(!single->FindTarget(pars, selected) && selected.IsPosition() && selected.GetPosition().AsVec2D() == CVec2(7, 8),
		"B13 foreign or opposing main buildings cannot replace the caller's output on no match");
	auto first = create(f.world, NDb::FACTION_FREEZE, 15), second = create(f.world, NDb::FACTION_FREEZE, 20);
	PFBaseUnit* expected = nullptr;
	auto observe = [&](PFBaseUnit& unit) { if (unit.GetUnitType() == NDb::UNITTYPE_MAINBUILDING && unit.GetFaction() == NDb::FACTION_FREEZE) expected = &unit; };
	f.world->GetAIWorld()->ForAllUnits(observe);
	check(single->FindTarget(pars, selected) && selected.IsUnit() && selected.GetUnit() == expected && reject.calls == 0,
		"B13 main-building query keeps original last-voxel-match order and does not invoke ordinary target conditions");
	f.world->GetAIWorld()->RemoveObject(*second);
	check(single->FindTarget(pars, selected) && selected.GetUnit() == first.GetPtr(),
		"B13 voxel-removed building cannot survive selection through a global unit cache");
	f.world->GetAIWorld()->RemoveObject(*first); selected.SetPosition(CVec3(7, 8, 0));
	check(!single->FindTarget(pars, selected) && selected.IsPosition() && reject.calls == 0,
		"B13 repeated lookup clears its previous match and preserves output when all local buildings are removed");
	hero->Cleanup();
	std::printf("B13 main-building lookup checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Read original assets; supply the authored building-clock value explicitly in small engine worlds. */
static void NaftaRuneAuthoredB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		for (bool aram : {false, true}) for (NDb::EFaction faction : {NDb::FACTION_FREEZE, NDb::FACTION_BURN})
			for (float periods : {0.f, .999f, 1.f, 3.f})
		{
			const auto glyphDb = NDb::Get<NDb::Glyph>(NDb::DBID(aram ? "/Glyphs/PrimeRune_ARAM.GLPH.xdb" : "/Glyphs/PrimeRune.GLPH.xdb"));
			check(glyphDb && glyphDb->applicators.size() == 1, "B13 original prime rune descriptor loads without asset edits");
			if (!glyphDb) continue;
			B8BehaviourForeignWorld f(check); if (!f.world) continue;
			auto hero = B9MaleHero(f.world, faction), ally = B9MaleHero(f.world, faction);
			auto enemy = B9MaleHero(f.world, faction == NDb::FACTION_FREEZE ? NDb::FACTION_BURN : NDb::FACTION_FREEZE);
			for (auto* unit : {hero.GetPtr(), ally.GetPtr(), enemy.GetPtr()}) unit->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
			auto* raw = new NDb::MainBuilding; raw->stats = hero->DbUnitDesc()->stats; raw->faction = faction;
			NDb::AdvMapObject object; object.gameObject = raw;
			object.offset = CPlacement(CVec3(20, 10, 0), QNULL, CVec3(1, 1, 1));
			CObj<PFMainBuilding> base = new PFMainBuilding(f.world, object); base->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
			const float duration = periods * (aram ? 90.f : 300.f);
			base->GetVariableVWM("GameDuration")->SetBaseValue(duration);
			NDb::Ptr<NDb::MainBuildingTargetSelector> selectorDb = new NDb::MainBuildingTargetSelector;
			CObj<PFTargetSelector> selector = selectorDb->Create(f.world);
			const Target request(hero.GetPtr()); PFTargetSelector::RequestParams pars(hero.GetPtr(), nullptr, request);
			Target selected; auto* single = dynamic_cast<PFSingleTargetSelector*>(selector.GetPtr());
			const bool found = single && single->FindTarget(pars, selected);
			check(found && selected.IsUnit() && selected.GetUnit() == base.GetPtr(),
				"B13 main-building lookup stays in the owner's actual world instead of an earlier world's global-cache entry");
			CObj<PFGlyph> glyph = new PFGlyph(f.world, glyphDb, CVec3(5, 5, 0));
			check(glyph->PickUp(hero.GetPtr()) && glyph->IsObjectDead(), "B13 original prime rune enters its actual pickup ability");
			for (int i = 0; i < 8; ++i) f.world->Step(.25f, .25f);
			const int expected = (aram ? 100 : 70) + int(std::floor(periods)) * (aram ? 20 : 30);
			std::printf("B13 authored prime aram=%d faction=%d duration=%g copied=%g gold=%d/%d/%d expected=%d\n",
				aram, int(faction), duration, hero->GetVariable("GameDuration"), hero->GetGold(), ally->GetGold(), enemy->GetGold(), expected);
			check(hero->GetVariable("GameDuration") == duration, "B13 original rune reads the selected main building clock before its award");
			check(hero->GetGold() == expected && ally->GetGold() == expected && enemy->GetGold() == 0,
				"B13 original MOBA/ARAM formula pays the exact clock-scaled award to allied heroes only");
			for (int i = 0; i < 8; ++i) f.world->Step(.25f, .25f);
			check(hero->GetGold() == expected && ally->GetGold() == expected, "B13 completed rune cannot repeat its prime award on later ticks");
			hero->Cleanup(); ally->Cleanup(); enemy->Cleanup(); base->Cleanup();
		}
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 authored nafta rune checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
