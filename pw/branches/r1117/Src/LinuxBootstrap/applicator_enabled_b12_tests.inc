/** Count virtual transitions while retaining original buff start, duration and ring ownership. */
struct EnabledBuffB12 : PFApplBuff
{
	using PFApplBuff::Init;
	using PFApplBuff::Start;
	using PFApplBuff::Step;
	using PFApplBuff::Stop;
	using PFBaseApplicator::Block;
	using PFBaseApplicator::Unblock;
	int enabledCalls = 0, disabledCalls = 0;
	explicit EnabledBuffB12(const PFApplCreatePars& pars) : PFApplBuff(pars) {}
	void Enable() override { ++enabledCalls; PFApplBuff::Enable(); }
	void Disable() override { ++disabledCalls; PFApplBuff::Disable(); }
	void Delegate() { AddFlags(FLAG_DELEGATED); }
};

/** Nonliteral conditions use the same live sender/receiver/constant context as their effects. */
static void ApplicatorEnabledB12(Checks& check, PFWorld* world)
{
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 30);
	CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 15);
	sender->GetVariableVWM("IsPet")->SetBaseValue(1);
	struct Example { const char* condition; bool enabled; bool compiled; };
	for (const auto& test : {Example{"sender.vIsPet == 0", false, false},
		Example{"sender.vIsPet == 1", true, false}, Example{"sStrength < tStrength", false, false},
		Example{"sStrength > tStrength", true, false}, Example{"true", true, false},
		Example{"false", false, false}, Example{"", true, false}, Example{"", false, true},
		Example{"cMissing", false, false}, Example{"unsupported(sLife)", false, false},
		Example{"1/0", false, false}, Example{"1 ? 1 : cMissing", true, false}})
	{
		NDb::DamageApplicator* raw = new NDb::DamageApplicator;
		raw->enabled.sString = test.condition; raw->damage.sString = "10";
		if (test.compiled) raw->enabled.compiledString = "unsupported-bytecode";
		raw->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
		NDb::Ptr<NDb::DamageApplicator> db = raw;
		PFApplCreatePars pars(CObj<PFAbilityInstance>(), Target(receiver.GetPtr()));
		pars.pWorld = world; pars.pOwner = sender; pars.pDBAppl = db;
		CObj<Damage> app = new Damage(pars); receiver->SetHealth(1000);
		check(app->Init() && app->Run(), "conditional damage uses real target initialization and start");
		check(app->IsEnabled() == test.enabled && receiver->GetHealth() == (test.enabled ? 990 : 1000),
			"checked enabled condition determines whether an instant effect can mutate its receiver");
	}
}

/** Drive dynamic enable/disable through actual world ticks and applied applicator rings. */
static void ApplicatorEnabledTickB12(Checks& check)
{
	B8BehaviourForeignWorld fixture(check);
	if (!fixture.world) return;
	PFWorld* world = fixture.world;
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 30);
	CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 15);
	NDb::StatModApplicator* raw = new NDb::StatModApplicator;
	raw->enabled.sString = "sender.vEnabled != 0"; raw->lifeTime.sString = "-1";
	raw->behaviorFlags = NDb::BUFFBEHAVIOR_DONTSTOPONSENDERDEATH;
	NDb::Ptr<NDb::BuffApplicator> db = raw;
	PFApplCreatePars pars(CObj<PFAbilityInstance>(), Target(receiver.GetPtr()));
	pars.pWorld = world; pars.pOwner = sender; pars.pDBAppl = db;
	CObj<EnabledBuffB12> app = new EnabledBuffB12(pars);
	check(app->Init() && ActivateApplicator(CObj<PFBaseApplicator>(app.GetPtr()), CObj<PFAbilityInstance>()), "dynamic buff joins real applied ring");
	check(!app->IsEnabled() && app->enabledCalls == 0 && app->IsLinkedToAppliedRing(),
		"disabled nonterminal buff remains attached without enabling its effect");
	sender->GetVariableVWM("Enabled")->SetBaseValue(1);
	world->Step(.1f, .1f);
	check(app->IsEnabled() && app->enabledCalls == 1, "world tick enables a newly true authored condition");
	world->Step(.1f, .1f);
	check(app->enabledCalls == 1 && app->disabledCalls == 0, "unchanged condition does not duplicate transition callbacks");
	sender->GetVariableVWM("Enabled")->SetBaseValue(0);
	world->Step(.1f, .1f);
	check(!app->IsEnabled() && app->disabledCalls == 1, "world tick disables a newly false authored condition");
	sender->GetVariableVWM("Enabled")->SetBaseValue(1);
	world->Step(.1f, .1f);
	app->Block(); app->Block();
	check(!app->IsEnabled() && app->IsBlocked() && app->disabledCalls == 2,
		"repeated blocks disable once rather than repeatedly removing effects");
	world->Step(.1f, .1f);
	check(!app->IsEnabled() && app->enabledCalls == 2, "blocked buff cannot reenable from a true condition");
	app->Unblock(); app->Unblock();
	check(app->IsEnabled() && !app->IsBlocked() && app->enabledCalls == 3,
		"repeated unblock preserves original immediate single enable transition");
	raw->enabled.sString = "cMissing";
	world->Step(.1f, .1f);
	check(!app->IsEnabled() && app->disabledCalls == 3, "invalid dynamic condition fails closed without retaining an enabled effect");
	raw->enabled.sString = "true";
	world->Step(.1f, .1f);
	sender->ChangeHealth(-sender->GetHealth());
	world->Step(.1f, .1f);
	check(app->IsAlive() && !app->IsEnabled(), "retained buff still disables when its sender dies by original policy");
	raw->behaviorFlags = NDb::EBuffBehavior(raw->behaviorFlags | NDb::BUFFBEHAVIOR_ENABLEDONSENDERDEATH);
	world->Step(.1f, .1f);
	check(app->IsAlive() && app->IsEnabled(), "authored sender-death exception keeps a retained buff enabled");
	app->Stop();
	check(app->PFBaseApplicator::Step(.1f) && !app->IsLinkedToAppliedRing(), "stopped base applicator cannot restart through a later step");
	CObj<EnabledBuffB12> delegated = new EnabledBuffB12(pars);
	raw->enabled.sString = "false"; delegated->Delegate();
	check(delegated->Init() && !delegated->Start() && delegated->IsEnabled(),
		"delegated applicator preserves original forced-enable startup policy");
	delegated->Step(.1f);
	check(!delegated->IsEnabled(), "delegation does not bypass later dynamic enabled conditions");
	delegated->Stop();
}
