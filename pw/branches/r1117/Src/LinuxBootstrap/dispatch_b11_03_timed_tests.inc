/** Authored literal lifetimes schedule actual payloads through the real sender's unit step. */
static void DispatchTimedB11(Checks& check, PFWorld* world)
{
	for (int ending = 0; ending < 5; ++ending)
	{
		CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
		CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 20);
		sender->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		NDb::Ability* raw = new NDb::Ability;
		raw->manaCost.sString = "70";
		raw->cooldownTime.sString = "5";
		NDb::DispatchByTime* timed = new NDb::DispatchByTime;
		timed->lifeTime.sString = "2";
		timed->cancelOnSenderDeath = ending != 4;
		raw->dispatch = timed;
		NDb::DamageApplicator* damage = new NDb::DamageApplicator;
		damage->damage.sString = "25";
		damage->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
		raw->applicators.push_back(damage);
		CObj<PFAbilityData> data = new PFAbilityData(sender.GetPtr(), raw, NDb::ABILITYTYPEID_ABILITY1);
		CObj<B10AbilityEvents> events = new B10AbilityEvents;
		sender->AddEventListener(events.GetPtr());
		CObj<PFAbilityInstance> cast = sender->CreateAbilityInstance(data, Target(receiver.GetPtr()));
		check(cast && cast->IsActivated() && !cast->IsFinished() && receiver->GetHealth() == 1000,
			"B11 timed dispatch starts with unapplied authored payload");
		check(sender->GetMana() == 130 && data->GetCurrentCooldown() == 5
			&& events->casts == 1 && events->last == cast.GetPtr(),
			"B11 travel does not postpone or duplicate cast payment and event identity");
		if (!cast) { sender->RemoveEventListener(events.GetPtr()); continue; }
		sender->Step(.5f);
		sender->Step(.5f);
		check(receiver->GetHealth() == 1000 && !cast->IsFinished(),
			"B11 timed payload stays unapplied before its authored deadline");
		if (ending == 1) receiver->EnableDispatch(false);
		if (ending == 2) cast->Cancel();
		if (ending >= 3) sender->ChangeHealth(-1000);
		sender->Step(.25f);
		check(receiver->GetHealth() == (ending == 3 ? 975 : 1000),
			"B11 original sender-death policy applies early only when authored to finish on death");
		sender->Step(.75f);
		check(receiver->GetHealth() == (ending == 1 || ending == 2 ? 1000 : 975)
			&& cast->IsFinished() == (ending != 1),
			"B11 deadline delivers, waits for admission, or remains explicitly cancelled");
		receiver->EnableDispatch(true);
		sender->Step(.25f);
		sender->Step(.25f);
		check(receiver->GetHealth() == (ending == 2 ? 1000 : 975) && cast->IsFinished()
			&& sender->GetMana() == 130 && events->casts == 1,
			"B11 deferred admission drains once without restarting duration or payment");
		sender->RemoveEventListener(events.GetPtr());
	}
}
