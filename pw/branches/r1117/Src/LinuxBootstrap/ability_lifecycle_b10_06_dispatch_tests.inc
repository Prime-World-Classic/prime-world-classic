/** Activated dispatches still waiting for application must retain their payload and owner. */
static void AbilityWaitingDispatchB10(Checks& check, PFWorld* world)
{
	for (bool cancel : {false, true})
	{
		CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
		CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 20);
		receiver->EnableDispatch(false);
		NDb::Ability* raw = new NDb::Ability;
		raw->manaCost.sString = "70";
		NDb::DamageApplicator* damage = new NDb::DamageApplicator;
		damage->damage.sString = "25"; damage->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
		raw->applicators.push_back(damage);
		NDb::Ptr<NDb::Ability> db = raw;
		CObj<PFAbilityData> data = new PFAbilityData(sender.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL);
		CObj<PFAbilityInstance> cast = sender->CreateAbilityInstance(data, Target(receiver.GetPtr()));
		check(cast && cast->IsCastFinished() && !cast->IsFinished()
			&& sender->GetMana() == 130 && receiver->GetHealth() == 1000,
			"activation payment does not imply completion of a waiting dispatch");
		if (!cast) continue;
		check(!cast->Update(.25f) && receiver->GetHealth() == 1000,
			"still-blocked dispatch does not release unit-owned instance work");
		if (cancel)
		{
			cast->Cancel();
			check(cast->IsFinished() && cast->GetActiveApplicatorsCount() == 0,
				"cancellation releases waiting work without activating applicators");
		}
		receiver->EnableDispatch(true);
		check(cast->Update(.25f) && receiver->GetHealth() == (cancel ? 1000 : 975) && sender->GetMana() == 130,
			"dispatch admission resumes effects once, or remains cancelled, without paying twice");
		cast->Update(.25f);
		check(receiver->GetHealth() == (cancel ? 1000 : 975), "resumed waiting dispatch cannot replay payload");
	}
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
	CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 20);
	NDb::Ability* raw = new NDb::Ability;
	NDb::StatModApplicator* modifier = new NDb::StatModApplicator;
	modifier->lifeTime.sString = "2";
	modifier->modifier.stat = NDb::STAT_STRENGTH;
	modifier->modifier.multValue.sString = "1";
	modifier->modifier.addValue.sString = "5";
	raw->applicators.push_back(modifier);
	CObj<PFAbilityData> data = new PFAbilityData(sender.GetPtr(), raw, NDb::ABILITYTYPEID_SPECIAL);
	CObj<PFAbilityInstance> cast = sender->CreateAbilityInstance(data, Target(receiver.GetPtr()));
	check(cast && cast->GetActiveApplicatorsCount() == 1 && !cast->IsFinished()
		&& receiver->GetStatValue(NDb::STAT_STRENGTH) == 25,
		"real lasting stat modifier keeps dispatched ability work unfinished");
	receiver->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
	receiver->Step(2);
	check(cast && cast->GetActiveApplicatorsCount() == 0 && cast->IsFinished()
		&& receiver->GetStatValue(NDb::STAT_STRENGTH) == 20,
		"actual applicator expiry releases effect ownership and restores the stat");
}
