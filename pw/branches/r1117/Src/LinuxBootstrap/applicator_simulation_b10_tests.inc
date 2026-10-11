/** Timed effects must expire through actual unit simulation, not a probe-only ring call. */
static void ApplicatorSimulationB10(Checks& check, PFWorld* world)
{
	for (bool heroReceiver : {false, true})
	{
		CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
		CObj<PFBaseUnit> receiver = heroReceiver
			? static_cast<PFBaseUnit*>(B9MaleHero(world, NDb::FACTION_FREEZE).GetPtr())
			: Unit(world, NDb::FACTION_FREEZE, 20).GetPtr();
		receiver->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		const float before = receiver->GetStatValue(NDb::STAT_STRENGTH);
		NDb::Ability* raw = new NDb::Ability;
		raw->manaCost.sString = "0";
		NDb::StatModApplicator* modifier = new NDb::StatModApplicator;
		modifier->lifeTime.sString = "2";
		modifier->modifier.stat = NDb::STAT_STRENGTH;
		modifier->modifier.multValue.sString = "1";
		modifier->modifier.addValue.sString = "5";
		raw->applicators.push_back(modifier);
		CObj<PFAbilityData> data = new PFAbilityData(sender.GetPtr(), raw, NDb::ABILITYTYPEID_SPECIAL);
		CObj<PFAbilityInstance> cast = sender->CreateAbilityInstance(data, Target(receiver.GetPtr()));
		check(cast && cast->GetActiveApplicatorsCount() == 1
			&& receiver->GetStatValue(NDb::STAT_STRENGTH) == before + 5,
			"simulation fixture applies a real timed stat modifier");
		receiver->Step(0); receiver->Step(1);
		check(cast && cast->GetActiveApplicatorsCount() == 1
			&& receiver->GetStatValue(NDb::STAT_STRENGTH) == before + 5,
			"unit effect remains active before its authored deadline");
		receiver->Step(1);
		check(cast && cast->GetActiveApplicatorsCount() == 0 && cast->IsFinished()
			&& receiver->GetStatValue(NDb::STAT_STRENGTH) == before,
			"unit step expires its real effect and releases ability ownership");
		receiver->Step(2);
		check(cast && cast->GetActiveApplicatorsCount() == 0
			&& receiver->GetStatValue(NDb::STAT_STRENGTH) == before,
			"later simulation cannot repeat effect removal");
	}
}
