/** Authored zero-argument getter spellings must resolve the same live values as numeric aliases. */
static void PeriodicGettersB13(Checks& check, PFWorld* world)
{
	const int before = check.count, failed = check.failed;
	TargetTriggerFixtureB13 f(world);
	for (const char* property : {"Life", "Energy", "MaxLife", "MaxEnergy", "Strength", "Intellect",
		"BaseAttack", "BaseBaseAttack", "MoveSpeed", "LifeRegen", "CritMult", "BaseLife"})
	for (const char* object : {"sender", "receiver", "pFirst", "pSecond"})
	{
		const bool sender = strcmp(object, "sender") == 0 || strcmp(object, "pFirst") == 0;
		const std::string call = std::string(object) + (object[0] == 'p' ? "->Get" : ".Get") + property + "()";
		const auto expected = LinuxBootstrap::EvaluateUnitNumericFormula(std::string(sender ? "s" : "r") + property, f.sender, f.receiver);
		const auto actual = LinuxBootstrap::EvaluateUnitNumericFormula(call, f.sender, f.receiver);
		check(expected.Succeeded() && actual.Succeeded() && actual.value == expected.value,
			"B13 exact read-only getters reuse the existing unit property whitelist and live endpoints");
	}
	for (const char* expression : {"receiver.GetMaxLife(1)", "receiver.GetMaxLife(,)", "receiver.GetUnknown()",
		"receiver.GetLife", "receiver.GetLife()()", "receiver.GetLife().GetLife()", "sender->GetLife()",
		"pSecond.GetLife()", "receiver.GetGold()", "receiver.GetBaseBaseLife()"})
	{
		check(!LinuxBootstrap::EvaluateUnitNumericFormula(expression, f.sender, f.receiver).Succeeded(),
			"B13 numeric getter support does not expose arbitrary methods, arguments or nested objects");
		check(!LinuxBootstrap::EvaluateUnitNumericFormula(std::string("true ? 1 : ") + expression, f.sender, f.receiver).Succeeded(),
			"B13 unsupported getter syntax remains invalid even in an unselected branch");
	}
	for (const char* expression : {"receiver.GetMaxLife()", "pSecond->GetMaxLife ( \t )"})
	{
		check(!LinuxBootstrap::EvaluateUnitNumericFormula(expression, f.sender, nullptr).Succeeded(),
			"B13 selected getters cannot substitute a missing receiver");
		const auto lazy = LinuxBootstrap::EvaluateUnitNumericFormula(std::string("false ? ") + expression + " : 7", f.sender, nullptr);
		check(lazy.Succeeded() && lazy.value == 7, "B13 valid unselected getters preserve lazy missing-context admission");
	}
	const auto candidate = LinuxBootstrap::EvaluateSelectorConditionFormula("receiver.GetLife()", f.sender, f.sender, nullptr);
	check(candidate.Succeeded() && candidate.value == 400, "B13 selector getter uses the selected candidate, not another ability receiver");
	std::printf("B13 periodic getters checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Retain original dispatches and permanent stat changes; only test descriptors and timing are synthetic. */
struct PeriodicFixtureB13 : TargetTriggerFixtureB13
{
	explicit PeriodicFixtureB13(PFWorld* world) : TargetTriggerFixtureB13(world) {}
	static float Scale(const PFBaseApplicator& app) { return static_cast<const IMiscFormulaPars&>(app).GetScale(); }
	static NDb::SpellPeriodicallyApplicator* Descriptor(const char* period = "2", const char* offset = "0")
	{
		auto* db = new NDb::SpellPeriodicallyApplicator;
		db->enabled.sString = "true"; db->startCondition.sString = "true"; db->lifeTime.sString = "-1";
		db->period.sString = period; db->startOffset.sString = offset;
		db->scaleMode = NDb::SCALECALCULATIONMODE_SCALEISCOUNT; db->spell = Spell(); return db;
	}
};

/** Compare strict boundaries and callback-visible scale with the original Windows recurrence. */
static void PeriodicTimingB13(Checks& check, PFWorld* world)
{
	const int before = check.count, failed = check.failed;
	struct Timing { const char* text; float value; };
	for (const auto& period : {Timing{"", 0}, Timing{"0", 0}, Timing{"1e-8", 1e-8f}, Timing{"2", 2}, Timing{"cWrapperBonus", 2}})
	for (const auto& offset : {Timing{"", 0}, Timing{"-1", -1}, Timing{"0.5", .5f}, Timing{"cWrapperBonus", 2}})
	for (auto mode : {NDb::SCALECALCULATIONMODE_SCALEISCOUNT, NDb::SCALECALCULATIONMODE_SCALEISTIME, NDb::SCALECALCULATIONMODE_SCALEISVPAR})
	{
		PeriodicFixtureB13 f(world); auto* raw = f.Descriptor(period.text, offset.text); raw->scaleMode = mode;
		NDb::Ptr<NDb::BaseApplicator> db = raw; auto app = f.Create(db);
		if (!app) { check(false, "B13 original periodic factory exists"); continue; }
		std::vector<float> observed; CObj<EventDispatchB7Listener> observer = new EventDispatchB7Listener;
		observer->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent* event) {
			const auto* applied = dynamic_cast<const PFBaseUnitApplicatorAppliedEvent*>(event);
			if (applied && applied->GetApplicator()->GetParentAppl() == app.GetPtr()) observed.push_back(f.Scale(*app));
			return 0u;
		};
		f.receiver->AddEventListener(observer.GetPtr());
		int strikes = 0; float timer = 0, current = offset.value > 0 ? offset.value : period.value;
		const auto scale = [&]() {
			return mode == NDb::SCALECALCULATIONMODE_SCALEISCOUNT ? float(strikes) :
				mode == NDb::SCALECALCULATIONMODE_SCALEISTIME ? (strikes - 1) * period.value + timer + offset.value : 0.f;
		};
		float expectedObserved = scale();
		if (offset.value < EPS_VALUE) ++strikes;
		check(f.Activate(app) && f.Delta() == strikes, "B13 periodic initial strike uses the authored offset and original epsilon admission");
		if (strikes) check(observed.size() == 1 && fabsf(observed.back() - expectedObserved) < .00001f,
			"B13 initial response observes pre-increment count and original time scale");
		for (float dt : {0.f, 1e-8f, .25f, .25f, .5f, 1.f, 2.f, 7.5f, 0.f})
		{
			const bool due = period.value == 0 ? true : (timer += dt) > current;
			if (due)
			{
				expectedObserved = scale(); ++strikes;
				if (period.value != 0) { timer = fmodf(timer, current); current = period.value; }
			}
			check(!app->Step(dt) && f.Delta() == strikes, "B13 strict period boundary and large elapsed time produce at most one original strike per Step");
			check(observed.size() == static_cast<size_t>(strikes) && (!due || fabsf(observed.back() - expectedObserved) < .00001f),
				"B13 response observes pre-modulo timer and pre-increment count, including offset transition");
			check(fabsf(f.Scale(*app) - scale()) < .00001f, "B13 post-strike scale retains the original timer remainder");
		}
		for (float dt : {-1.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
			check(!app->Step(dt) && f.Delta() == strikes && fabsf(f.Scale(*app) - scale()) < .00001f,
				"B13 invalid elapsed time cannot strike, poison the timer or mutate periodic scale");
		app->Stop(); check(app->Step(3) && f.Delta() == strikes && f.instance->GetActiveApplicatorsCount() == 0,
			"B13 stopped periodic effects never resume or retain ability accounting");
		f.receiver->RemoveEventListener(observer.GetPtr()); observer->action = nullptr;
	}
	std::printf("B13 periodic timing checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Finite buffs expire before a coincident strike; original scale may be negative and is not clamped at zero. */
static void PeriodicLifetimeB13(Checks& check, PFWorld* world)
{
	const int before = check.count, failed = check.failed;
	for (bool delayed : {false, true})
	for (auto mode : {NDb::SCALECALCULATIONMODE_SCALEISCOUNT, NDb::SCALECALCULATIONMODE_SCALEISTIME, NDb::SCALECALCULATIONMODE_SCALEISVPAR})
	{
		PeriodicFixtureB13 f(world); auto* raw = f.Descriptor("2", delayed ? "1" : "0");
		raw->lifeTime.sString = "6"; raw->scaleMode = mode;
		NDb::Ptr<NDb::BaseApplicator> db = raw; auto app = f.Create(db); if (!app) { check(false, "B13 finite periodic factory exists"); continue; }
		std::vector<float> observed; CObj<EventDispatchB7Listener> observer = new EventDispatchB7Listener;
		observer->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent* event) {
			const auto* applied = dynamic_cast<const PFBaseUnitApplicatorAppliedEvent*>(event);
			if (applied && applied->GetApplicator()->GetParentAppl() == app.GetPtr()) observed.push_back(f.Scale(*app));
			return 0u;
		};
		f.receiver->AddEventListener(observer.GetPtr());
		check(f.Activate(app) && f.Delta() == int(!delayed), "B13 finite periodic start preserves the authored initial delay");
		for (int tick = 1; tick <= 6; ++tick)
		{
			const int strikes = delayed ? (tick >= 2) + (tick >= 5) : 1 + (tick >= 3) + (tick >= 5);
			check(app->Step(1) == (tick == 6) && f.Delta() == strikes,
				"B13 finite periodic effects retire before striking on the lifetime boundary");
		}
		const std::vector<float> times = delayed ? std::vector<float>{1, 4} : std::vector<float>{-2, 3, 5};
		check(observed.size() == times.size(), "B13 finite response count matches original pre-expiry ordering");
		for (size_t i = 0; i < observed.size() && i < times.size(); ++i)
		{
			const float expected = mode == NDb::SCALECALCULATIONMODE_SCALEISCOUNT ? float(i)
				: mode == NDb::SCALECALCULATIONMODE_SCALEISTIME ? times[i] : times[i] / 6;
			check(fabsf(observed[i] - expected) < .00001f, "B13 finite count/time/VPAR scales preserve original callback values including negative initial time");
		}
		app->Stop(); check(f.instance->GetActiveApplicatorsCount() == 0 && app->Step(10), "B13 finite periodic retirement releases its original ability owner");
		f.receiver->RemoveEventListener(observer.GetPtr()); observer->action = nullptr;
	}
	for (unsigned seed = 0; seed < 6; ++seed)
	{
		PeriodicFixtureB13 f(world); auto* raw = f.Descriptor("random(1,3)", "random(0,2)");
		NDb::Ptr<NDb::BaseApplicator> db = raw;
		world->GetRndGen()->SetSeed(seed); NRandom::RandomGenerator expected(seed);
		expected.Next(1, 3); const int offset = expected.Next(0, 2);
		auto app = f.Apply(check, db);
		check(app && app->IsAlive() && f.Delta() == int(offset == 0), "B13 seeded periodic setup commits both authored timer expressions exactly once");
		raw->period.sString = "1/0"; raw->startOffset.sString = "random(0,2)";
		if (app) app->Step(.01f);
		check(world->GetRndGen()->Next(0, 1000) == expected.Next(0, 1000), "B13 periodic stepping uses cached timers without reevaluating authored randomness");
	}
	{
		PeriodicFixtureB13 f(world); NDb::Ptr<NDb::BaseApplicator> db = f.Descriptor("3.402823466e38"); auto app = f.Apply(check, db);
		check(app && !app->Step(std::numeric_limits<float>::max() / 2) && f.Delta() == 1,
			"B13 finite large periods remain valid without overflowing elapsed state");
		check(app && app->Step(std::numeric_limits<float>::max()) && f.Delta() == 1,
			"B13 overflowing elapsed accumulation retires without another dispatch or invalid modulo");
	}
	std::printf("B13 periodic lifetime checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Malformed timers and missing selectors cannot become zero-period, unselected-target spells. */
static void PeriodicAdmissionB13(Checks& check, PFWorld* world)
{
	const int before = check.count, failed = check.failed;
	for (bool offset : {false, true}) for (const char* text : {"cMissing", "1/0", "1e39", "compiled-only", "random(1,3)+cMissing"})
	{
		PeriodicFixtureB13 f(world); auto* raw = f.Descriptor("random(1,3)", "0");
		auto& expression = offset ? raw->startOffset : raw->period; expression.sString = text;
		if (strcmp(text, "compiled-only") == 0) { expression.sString = ""; expression.compiledString = "unsupported"; }
		NDb::Ptr<NDb::BaseApplicator> db = raw; NRandom::RandomGenerator expected(*world->GetRndGen()); auto app = f.Apply(check, db);
		check(app && !app->IsAlive() && f.Delta() == 0 && f.instance->GetActiveApplicatorsCount() == 0,
			"B13 invalid periodic formulas retire without a substituted zero interval or initial strike");
		check(world->GetRndGen()->Next(0, 1000) == expected.Next(0, 1000),
			"B13 rejected periodic setup restores randomness even when its first timer expression succeeded");
	}
	{
		PeriodicFixtureB13 f(world); NDb::Ptr<NDb::BaseApplicator> db = f.Descriptor("-1"); auto app = f.Apply(check, db);
		check(app && !app->IsAlive() && f.Delta() == 0, "B13 negative periods cannot execute the original invalid modulo path");
	}
	{
		struct Missing : NDb::MultipleTargetSelector { PFTargetSelector* Create(PFWorld*) const override { return nullptr; } };
		PeriodicFixtureB13 f(world); auto* raw = f.Descriptor(); raw->targetSelector = new Missing;
		NDb::Ptr<NDb::BaseApplicator> db = raw; auto app = f.Apply(check, db);
		check(app && !app->IsAlive() && f.Delta() == 0, "B13 unavailable authored selector cannot silently dispatch to the application target");
	}
	{
		PeriodicFixtureB13 f(world); auto* raw = f.Descriptor(); NDb::Ptr<NDb::BaseApplicator> db = raw; auto app = f.Apply(check, db);
		check(app && !app->Step(1) && f.Delta() == 1, "B13 periodic fixture has one initial strike and a partial interval");
		raw->enabled.sString = "false";
		check(!app->Step(5) && f.Delta() == 1 && f.Scale(*app) == 0, "B13 disabled periodic effects reset count but retain their timer");
		raw->enabled.sString = "true";
		check(!app->Step(1) && f.Delta() == 1 && f.Scale(*app) == 0, "B13 reenable retains strict equality at the old interval boundary");
		check(!app->Step(.25f) && f.Delta() == 2 && f.Scale(*app) == 1, "B13 reenabled strike resumes the retained interval without an extra initial response");
	}
	for (bool duringSelection : {false, true})
	{
		PeriodicFixtureB13 f(world); auto* list = new TriggerTargetsDbB13;
		list->targets.push_back(Target(f.receiver.GetPtr())); list->targets.push_back(Target(f.receiver.GetPtr()));
		auto* raw = f.Descriptor(); raw->targetSelector = list; NDb::Ptr<NDb::BaseApplicator> db = raw;
		auto app = f.Create(db); if (!app) { check(false, "B13 periodic cancellation fixture exists"); continue; }
		if (duringSelection) list->stopOnEnumeration = app.GetPtr();
		CObj<EventDispatchB7Listener> observer = new EventDispatchB7Listener;
		observer->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent* event) {
			const auto* applied = dynamic_cast<const PFBaseUnitApplicatorAppliedEvent*>(event);
			if (applied && applied->GetApplicator()->GetParentAppl() == app.GetPtr()) app->Stop();
			return 0u;
		};
		f.receiver->AddEventListener(observer.GetPtr());
		check(f.Activate(app) && !app->IsAlive() && f.Delta() == (duringSelection ? 0 : 1),
			"B13 callback retirement cancels remaining selected targets without discarding an already admitted child");
		check(f.instance->GetActiveApplicatorsCount() == 0, "B13 initial strike cancellation balances parent and immediate child ownership");
		f.receiver->RemoveEventListener(observer.GetPtr()); observer->action = nullptr;
	}
	for (bool enabled : {false, true}) for (bool starts : {false, true})
	{
		PeriodicFixtureB13 f(world); auto* raw = f.Descriptor("random(1,3)");
		raw->enabled.sString = enabled ? "true" : "false"; raw->startCondition.sString = starts ? "true" : "false";
		NDb::Ptr<NDb::BaseApplicator> db = raw; NRandom::RandomGenerator expected(*world->GetRndGen()); if (starts) expected.Next(1, 3);
		auto app = f.Apply(check, db);
		check(app && app->IsAlive() == starts && f.Delta() == int(starts && enabled),
			"B13 rejected start conditions retire before setup; merely disabled buffs retain original cached timers");
		check(world->GetRndGen()->Next(0, 1000) == expected.Next(0, 1000), "B13 periodic setup randomness follows original start-condition versus enabled ordering");
	}
	for (bool stopped : {false, true}) for (bool selector : {false, true})
	{
		PeriodicFixtureB13 f(world); auto* raw = f.Descriptor("2", "10"); raw->enabled.sString = "false";
		NDb::Ptr<NDb::BaseApplicator> db = raw; auto app = f.Apply(check, db); if (!app) continue;
		if (stopped) app->Stop();
		auto* list = new TriggerTargetsDbB13; NDb::Ptr<TriggerTargetsDbB13> listDb = list;
		list->targets.push_back(Target(f.receiver.GetPtr())); CObj<PFTargetSelector> targets = list->Create(world);
		check(SendSpell2Targets(app, raw->spell, selector ? targets.GetPtr() : nullptr) == 1 && f.Delta() == 1,
			"B13 shared dispatch helper preserves explicit stop/cancel spell delivery from disabled or stopped parents");
	}
	std::printf("B13 periodic admission checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Load the complete shipped ordinary-hero potion and advance it through real inventory and world steps. */
static void PeriodicPotionB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		const auto potion = NDb::Get<NDb::Consumable>(NDb::DBID("/Items/Consumables/HealingPotion.ARCT.xdb"));
		check(potion && potion->applicators.size() == 1, "B13 original healing potion retains its authored parent status and constants");
		if (potion)
		{
			B8BehaviourForeignWorld isolated(check);
			if (isolated.world)
			{
				auto hero = B9MaleHero(isolated.world, NDb::FACTION_FREEZE);
				hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); hero->SetHealth(100);
				check(hero->GetLifeRegenAbs() == 0 && hero->GetLifeRegen() == 0 && hero->GetVariable("PantherIsAlive") == 0,
					"B13 ordinary-hero potion fixture has no passive regen or summoned alternate branch");
				check(hero->TakeConsumable(potion, 2, NDb::CONSUMABLEORIGIN_DEFAULT, 0), "B13 original potion enters real inventory");
				CObj<PFAbilityInstance> cast = hero->UseConsumable(0, Target(hero.GetPtr()), false);
				check(cast != nullptr, "B13 original potion casts through the actual inventory ability");
				for (int i = 0; i < 4; ++i) isolated.world->Step(.01f, .01f);
				check(hero->GetLife() == 140, "B13 original potion immediately heals its authored forty points, not a formula fallback");
				check(hero->GetConsumable(0) && hero->GetConsumable(0)->GetQuantity() == 1,
					"B13 original potion activation consumes exactly one inventory charge");
				for (int i = 0; i < 15; ++i) isolated.world->Step(.1f, .1f);
				check(hero->GetLife() == 140, "B13 authored two-second interval never heals on every simulation step");
				for (int i = 0; i < 10; ++i) isolated.world->Step(.1f, .1f);
				check(hero->GetLife() == 180, "B13 ordinary potion delivers its second original tick after the interval");
				for (int i = 0; i < 180; ++i) isolated.world->Step(.1f, .1f);
				check(hero->GetLife() == 500 && cast && cast->GetActiveApplicatorsCount() == 0,
					"B13 twenty-second potion expires after ten forty-point ticks and releases status ownership");
				for (int i = 0; i < 20; ++i) isolated.world->Step(.1f, .1f);
				check(hero->GetLife() == 500, "B13 expired potion cannot produce another healing tick");
				hero->Cleanup();
			}
		}
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 authored periodic potion checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
