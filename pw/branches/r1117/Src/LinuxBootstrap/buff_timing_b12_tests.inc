/** Inject only the existing duration-policy hook; all formula and lifecycle behavior stays real. */
struct TimedBuffB12 : EnabledBuffB12
{
	float durationScale = 1;
	explicit TimedBuffB12(const PFApplCreatePars& pars) : EnabledBuffB12(pars) {}
	float GetModifiedDuration(float original) const override { return original * durationScale; }
};

/** Exercise original defaults, checked conditions, computed duration and owned expiry. */
static void BuffTimingB12(Checks& check)
{
	B8BehaviourForeignWorld fixture(check);
	if (!fixture.world) return;
	PFWorld* world = fixture.world;
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 30);
	CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 15);
	const auto applied = [&](const PFBaseApplicator* expected) {
		bool found = false;
		const auto find = [&](const CObj<PFBaseApplicator>& app) { found = found || app.GetPtr() == expected; };
		receiver->ForAllAppliedApplicators(find);
		return found;
	};
	sender->GetVariableVWM("AllowBuff")->SetBaseValue(1);
	const auto create = [&](const char* lifetime, const char* condition, bool compiled = false, float scale = 1) {
		NDb::StatModApplicator* raw = new NDb::StatModApplicator;
		raw->lifeTime.sString = lifetime; raw->startCondition.sString = condition;
		if (compiled) raw->lifeTime.compiledString = "unsupported-bytecode";
		NDb::Ptr<NDb::StatModApplicator> db = raw;
		const Target target(receiver.GetPtr());
		PFApplCreatePars pars(CObj<PFAbilityInstance>(), target);
		pars.pWorld = world; pars.pOwner = sender; pars.pDBAppl = db;
		CObj<TimedBuffB12> app = new TimedBuffB12(pars); app->durationScale = scale;
		return app;
	};
	for (const char* expression : {"sStrength/30", "receiver.vMissing+1", "1 ? 1 : cMissing"})
	{
		CObj<TimedBuffB12> app = create(expression, "sender.vAllowBuff != 0", false, 2);
		check(app->Init() && app->GetLifetime() == 1 && app->GetDuration() == 2,
			"computed lifetime flows through the original duration modifier hook");
		check(ActivateApplicator(CObj<PFBaseApplicator>(app.GetPtr()), CObj<PFAbilityInstance>()) &&
			app->IsLinkedToAppliedRing() && app->enabledCalls == 1, "timed authored buff joins actual applied ring");
		world->Step(1, 1);
		check(app->IsAlive() && app->GetDuration() == 1 && app->disabledCalls == 0, "world consumes computed duration exactly once");
		world->Step(1, 1);
		check(!app->IsAlive() && !applied(app.GetPtr()) && app->disabledCalls == 1,
			"computed buff expires and cleans up through its real ring owner");
	}
	for (const char* invalid : {"cMissing", "1/0", "1e39", "sqrt(-1)", "unknown(sLife)"})
	{
		CObj<TimedBuffB12> app = create(invalid, "true");
		check(!app->Init() && app->enabledCalls == 0, "invalid lifetime cannot silently become an infinite buff");
	}
	check(!create("", "true", true)->Init(), "compiled-only lifetime fails without pretending to run Windows bytecode");
	CObj<TimedBuffB12> compiledStart = create("1", "");
	const_cast<NDb::BuffApplicator*>(dynamic_cast<const NDb::BuffApplicator*>(compiledStart->GetDBBase().GetPtr()))
		->startCondition.compiledString = "unsupported-bytecode";
	check(compiledStart->Init() && ActivateApplicator(CObj<PFBaseApplicator>(compiledStart.GetPtr()), CObj<PFAbilityInstance>()) &&
		!compiledStart->IsAlive() && compiledStart->enabledCalls == 0 && !applied(compiledStart.GetPtr()),
		"compiled-only start condition cannot admit an effect");
	for (float scale : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
		std::numeric_limits<float>::max()})
		check(!create("2", "true", false, scale)->Init(), "invalid or overflowing modified duration fails initialization");
	for (const char* condition : {"sender.vAllowBuff == 0", "sStrength < tStrength", "cMissing", "1/0"})
	{
		CObj<TimedBuffB12> app = create("1", condition);
		check(app->Init() && ActivateApplicator(CObj<PFBaseApplicator>(app.GetPtr()), CObj<PFAbilityInstance>()),
			"failed start condition completes through the original admission path");
		check(!app->IsAlive() && app->enabledCalls == 0 && !applied(app.GetPtr()),
			"false or invalid start condition installs neither an effect nor a ticking buff");
	}
	CObj<TimedBuffB12> infinite = create("", "");
	check(infinite->Init() && infinite->GetLifetime() == -1 && !infinite->Start() && !infinite->Step(100),
		"empty lifecycle fields preserve original true and infinite defaults");
	infinite->Stop();
	check(infinite->Step(.1f) && !infinite->IsEnabled() && infinite->enabledCalls == 1,
		"a stopped infinite buff reports terminal without re-enabling or touching its duration");
	CObj<TimedBuffB12> finite = create("2", "true");
	check(finite->Init() && !finite->Start(), "finite delta-boundary fixture starts");
	for (float dt : {-1.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
		check(!finite->Step(dt) && finite->GetDuration() == 2, "invalid delta cannot extend or poison a timed buff");
	check(!finite->Step(0) && finite->GetDuration() == 2, "zero delta retains the authored duration");
	finite->Stop();
	for (const char* lifetime : {"0", "-2"})
	{
		CObj<TimedBuffB12> expired = create(lifetime, "true");
		check(expired->Init() && !expired->Start() && expired->Step(0),
			"only minus one is infinite; other nonpositive authored durations expire at a zero-time check");
		expired->Stop();
	}
	for (float duration : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
	{
		CObj<TimedBuffB12> corrupt = create("2", "true");
		check(corrupt->Init() && !corrupt->Start(), "external duration mutation fixture starts");
		corrupt->SetDuration(duration);
		check(corrupt->Step(.1f), "nonfinite externally modified duration requests termination rather than remaining immortal");
		corrupt->Stop();
	}
}
