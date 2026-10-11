/** Build authored attack data; the production dispatch remains responsible for all travel and damage. */
static NDb::BaseAttack* B11AttackDescriptor(int kind)
{
	NDb::BaseAttack* raw = new NDb::BaseAttack;
	raw->useRange.sString = "20";
	if (kind == 0) raw->dispatch = new NDb::DispatchImmediate;
	if (kind == 1)
	{
		NDb::DispatchByTime* timed = new NDb::DispatchByTime;
		timed->lifeTime.sString = "2";
		raw->dispatch = timed;
	}
	if (kind == 2)
	{
		NDb::DispatchLinear* linear = new NDb::DispatchLinear;
		linear->speed = 5;
		raw->dispatch = linear;
	}
	NDb::DamageApplicator* damage = new NDb::DamageApplicator;
	damage->damage.sString = "25";
	damage->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
	raw->applicators.push_back(damage);
	return raw;
}

/** Actual unit ticks retain launched attacks, preserve timing and keep cancellation terminal. */
static void DispatchAttackB11(Checks& check, PFWorld* world, int supportedKinds)
{
	for (int kind = 0; kind < supportedKinds; ++kind)
	for (int termination = 0; termination < 3; ++termination)
	{
		CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
		CObj<B8BehaviourCreep> receiver = B8BehaviourMoving(world, NDb::FACTION_BURN);
		check(receiver->TeleportTo(CVec2(15, 5)), "B11 attack fixture positions its real receiver");
		CObj<PFBaseAttackData> attack = new PFBaseAttackData(sender.GetPtr(), B11AttackDescriptor(kind));
		sender->ReplaceBaseAttack(attack, false);
		sender->AssignTarget(receiver.GetPtr(), true);
		check(sender->DoAttack(false), "B11 deferred base attack admitted through the actual unit");
		sender->AddFlag(NDb::UNITFLAG_FORBIDATTACK);
		sender->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		check(attack->GetAttackInstances().size() == 1, "B11 launched attack retains its real instance");
		if (attack->GetAttackInstances().empty()) continue;
		CObj<PFBaseAttackInstance> instance = attack->GetAttackInstances()[0];
		CObj<PFDispatch> dispatch = B11FindDispatch(world, instance.GetPtr());
		check(dispatch && !instance->IsReadyToDie() && receiver->GetHealth() == 1000,
			"B11 launch cleanup does not cancel the pending authored payload");
		// Invalid ticks must not poison the attack timer or advance travel/cooldown.
		const float cooldown = attack->GetCurrentCooldown();
		for (float dt : {0.0f, -1.0f, std::numeric_limits<float>::infinity(),
			std::numeric_limits<float>::quiet_NaN()})
		{
			attack->Update(dt, true);
			check(receiver->GetHealth() == 1000 && attack->GetCurrentCooldown() == cooldown,
				"B11 invalid attack-data ticks do not advance pending dispatches or cooldown");
		}
		if (termination == 1)
		{
			attack->Cancel(); attack->Cancel();
			instance->DoAttack();
			check(instance->IsReadyToDie() && (!dispatch || dispatch->IsObjectDead()),
				"B11 explicit attack cancellation is terminal even after a repeated launch request");
		}
		if (termination == 2) receiver->ChangeHealth(-1000);
		sender->Step(.5f);
		const float initialHealth = termination == 2 ? 0 : 1000;
		check(receiver->GetHealth() == (termination == 0 && kind == 0 ? 975 : initialHealth),
			"B11 first unit tick applies only immediate attacks, never a timed or travelling payload");
		if (termination == 0 && kind == 2 && dispatch)
		{
			PFDispatchUniformLinearMove* linear = dynamic_cast<PFDispatchUniformLinearMove*>(dispatch.GetPtr());
			check(linear && linear->GetCurPos() == CVec2(7.5f, 5),
				"B11 launched projectile advances exactly one authored speed increment per unit tick");
		}
		sender->Step(1.0f);
		check(receiver->GetHealth() == (termination == 0 && kind == 0 ? 975 : initialHealth),
			"B11 two-second attack payload is not delivered at one and a half seconds");
		sender->Step(.5f);
		check(receiver->GetHealth() == (termination == 0 ? 975 : initialHealth),
			"B11 live unit scheduling delivers the exact authored attack payload at arrival");
		check(instance->IsReadyToDie() && attack->GetAttackInstances().empty(),
			"B11 terminal attack instances leave the actual attack update collection");
		sender->Step(2);
		check(receiver->GetHealth() == (termination == 0 ? 975 : initialHealth),
			"B11 dead, cancelled and completed attack dispatches cannot replay damage");
	}
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
	CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 20);
	CObj<PFBaseAttackData> attack = new PFBaseAttackData(sender.GetPtr(), B11AttackDescriptor(1));
	CObj<PFBaseAttackInstance> pending = new PFBaseAttackInstance(attack, Target(receiver.GetPtr()));
	pending->Cancel();
	check(pending->IsReadyToDie(), "B11 cancelling an unlaunched attack marks the instance terminal");
	// Only test repeated ApplyAttack after terminal admission; the old path dereferences its cleared target.
	if (pending->IsReadyToDie()) { pending->ApplyAttack(); pending->DoAttack(); }
	check(pending->Update(3) && pending->IsReadyToDie() && receiver->GetHealth() == 1000,
		"B11 cancelling an unlaunched attack prevents later public launch paths from rearming it");
}
