/** Wrapper metadata owns enabled policy even when typed effect data is unwrapped. */
static void ApplicatorWrapperB12(Checks& check, PFWorld* world)
{
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 30);
	CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 15);
	for (bool innerEnabled : {false, true}) for (bool outerEnabled : {false, true})
	{
		NDb::DamageApplicator* inner = new NDb::DamageApplicator;
		inner->enabled.sString = innerEnabled ? "true" : "false";
		inner->damage.sString = "10"; inner->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
		NDb::ApplicatorNameOverrider* outer = new NDb::ApplicatorNameOverrider;
		outer->applicator = inner; outer->enabled.sString = outerEnabled ? "true" : "false";
		NDb::Ptr<NDb::BaseApplicator> db = outer;
		const Target target(receiver.GetPtr());
		PFApplCreatePars pars(CObj<PFAbilityInstance>(), target);
		pars.pWorld = world; pars.pOwner = sender; pars.pDBAppl = db;
		CObj<Damage> app = new Damage(pars); receiver->SetHealth(1000);
		check(app->Init() && app->GetDBBase().GetPtr() == inner && app->Run(), "wrapped damage uses typed inner effect data");
		check(app->IsEnabled() == outerEnabled && receiver->GetHealth() == (outerEnabled ? 990 : 1000),
			"outer applicator condition alone gates the actual damage mutation");
	}
	for (bool innerEnabled : {false, true})
	{
		NDb::StatModApplicator* inner = new NDb::StatModApplicator;
		inner->enabled.sString = innerEnabled ? "true" : "false";
		inner->lifeTime.sString = "-1";
		NDb::ApplicatorNameOverrider* outer = new NDb::ApplicatorNameOverrider;
		outer->applicator = inner; outer->enabled.sString = "sender.vGate != 0";
		NDb::Ptr<NDb::BaseApplicator> db = outer;
		const Target target(receiver.GetPtr());
		PFApplCreatePars pars(CObj<PFAbilityInstance>(), target);
		pars.pWorld = world; pars.pOwner = sender; pars.pDBAppl = db;
		sender->GetVariableVWM("Gate")->SetBaseValue(0);
		CObj<EnabledBuffB12> app = new EnabledBuffB12(pars);
		check(app->Init() && !app->Start() && !app->IsEnabled() && app->enabledCalls == 0,
			"outer false condition preserves a disabled wrapped buff");
		sender->GetVariableVWM("Gate")->SetBaseValue(1);
		check(!app->Step(.1f) && app->IsEnabled() && app->enabledCalls == 1,
			"dynamic wrapper condition enables independently of the inner condition");
		outer->enabled.sString = "false";
		check(!app->Step(.1f) && !app->IsEnabled() && app->disabledCalls == 1,
			"dynamic wrapper condition disables independently of the inner condition");
		app->Stop();
	}
}
