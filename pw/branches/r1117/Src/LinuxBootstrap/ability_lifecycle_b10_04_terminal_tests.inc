/** Cancelled or interrupted pending work must become terminal, not merely activated. */
static void AbilityPendingTerminationB10(Checks& check, PFWorld* world)
{
	for (bool interrupt : {false, true})
	{
		CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
		NDb::Talent* raw = new NDb::Talent;
		raw->manaCost.sString = "70";
		raw->cooldownTime.sString = "5";
		NDb::Ptr<NDb::Talent> db = raw;
		CObj<PFAbilityData> data = new B9TimedTalent(hero.GetPtr(), db);
		CObj<PFAbilityInstance> pending = new PFAbilityInstance(data, Target(hero.GetPtr()), false);
		check(!pending->IsActivated() && !pending->IsFinished(), "unsubmitted pending instance is unfinished");
		if (interrupt) pending->Interrupt(); else pending->Cancel();
		check(pending->IsFinished() && pending->IsCastFinished(),
			"pending cancellation or interruption marks both cast and effect completion");
		check(pending->Update(0) && pending->Update(10) && !pending->ApplyToTarget(),
			"terminated pending work cannot linger in updates or admit a dispatch");
		check(hero->GetMana() == 200 && data->GetCurrentCooldown() == 0,
			"termination before activation neither charges mana nor starts cooldown");
		if (interrupt) pending->Interrupt(); else pending->Cancel();
		check(pending->IsFinished(), "repeated pending termination is idempotent");
	}
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
	NDb::Ability* raw = new NDb::Ability;
	CObj<PFAbilityData> data = new PFAbilityData(sender.GetPtr(), raw, NDb::ABILITYTYPEID_SPECIAL);
	CObj<PFAbilityInstance> active = data->ApplyToTarget(Target(sender.GetPtr()));
	check(active && active->IsActivated(), "active fixture uses real dispatch admission");
	if (active)
	{
		active->NotifyApplicatorStarted(sender.GetPtr());
		active->Interrupt();
		check(!active->IsFinished(), "interruption after activation preserves live applicator ownership");
		active->NotifyApplicatorStopped(sender.GetPtr());
		check(active->IsFinished(), "live effect completion releases interrupted cast work");
	}
}
