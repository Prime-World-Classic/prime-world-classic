/** Immediate means next simulation step unless the descriptor explicitly requests instantApply. */
static void DispatchImmediateB11(Checks& check, PFWorld* world)
{
	for (bool instant : {false, true})
	for (bool cancel : {false, true})
	{
		CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
		sender->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 20);
		NDb::Ability* raw = new NDb::Ability;
		raw->manaCost.sString = "70";
		NDb::DispatchImmediate* immediate = new NDb::DispatchImmediate;
		immediate->instantApply = instant;
		raw->dispatch = immediate;
		NDb::DamageApplicator* damage = new NDb::DamageApplicator;
		damage->damage.sString = "25";
		damage->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
		raw->applicators.push_back(damage);
		CObj<PFAbilityData> data = new PFAbilityData(sender.GetPtr(), raw, NDb::ABILITYTYPEID_SPECIAL);
		CObj<PFAbilityInstance> cast = sender->CreateAbilityInstance(data, Target(receiver.GetPtr()));
		check(cast && sender->GetMana() == 130 && receiver->GetHealth() == (instant ? 975 : 1000),
			"B11 instantApply controls synchronous damage, not activation payment");
		check(cast && cast->IsFinished() == instant, "B11 deferred immediate remains instance-owned");
		if (!cast) continue;
		if (cancel) cast->Cancel();
		sender->Step(.125f);
		const float expected = instant || !cancel ? 975 : 1000;
		check(receiver->GetHealth() == expected && cast->IsFinished(),
			"B11 unit step delivers or cancels the real immediate payload");
		sender->Step(.125f);
		check(receiver->GetHealth() == expected && sender->GetMana() == 130,
			"B11 repeated unit step cannot pay or deliver an immediate payload twice");
	}
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
	CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 20);
	NDb::Ability* raw = new NDb::Ability;
	raw->dispatch = new NDb::DispatchImmediate;
	NDb::DamageApplicator* damage = new NDb::DamageApplicator;
	damage->damage.sString = "25";
	damage->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
	raw->applicators.push_back(damage);
	NDb::Ptr<NDb::Spell> spell = raw;
	CObj<PFAbilityData> data = new PFAbilityData(sender.GetPtr(), raw, NDb::ABILITYTYPEID_SPECIAL);
	CObj<PFAbilityInstance> cast = new PFAbilityInstance(data, Target(receiver.GetPtr()), false);
	CObj<PFDispatch> dispatch = CreateDispatch(cast, nullptr, Target(sender.GetPtr()),
		Target(receiver.GetPtr()), spell, 0, true, 0);
	check(dispatch && dispatch->Started() && receiver->GetHealth() == 1000,
		"B11 factory start does not inject an unrequested simulation step");
	if (dispatch) dispatch->Cancel();
}
