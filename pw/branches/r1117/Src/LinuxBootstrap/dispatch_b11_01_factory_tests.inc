/** Authored dispatch factories must select their engine subtype or reject unsupported data. */
static void DispatchFactoryB11(Checks& check, PFWorld* world)
{
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
	CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 20);
	NDb::Ability* raw = new NDb::Ability;
	NDb::DispatchImmediate* immediate = new NDb::DispatchImmediate;
	immediate->instantApply = true;
	raw->dispatch = immediate;
	NDb::DamageApplicator* damage = new NDb::DamageApplicator;
	damage->damage.sString = "25";
	damage->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
	raw->applicators.push_back(damage);
	NDb::Ptr<NDb::Ability> db = raw;
	NDb::Ptr<NDb::Spell> spell = raw;
	CObj<PFAbilityData> data = new PFAbilityData(sender.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL);
	CObj<PFAbilityInstance> cast = new PFAbilityInstance(data, Target(receiver.GetPtr()), false);
	CObj<PFDispatch> dispatch = CreateDispatch(cast, nullptr, Target(sender.GetPtr()),
		Target(receiver.GetPtr()), spell, 0, false, .25f);
	check(dispatch && dynamic_cast<PFDispatchImmediate*>(dispatch.GetPtr()),
		"B11 factory selects authored immediate subtype");
	check(dispatch && !dispatch->Started() && receiver->GetHealth() == 1000
		&& !dispatch->GetApplicators().empty(), "B11 unstarted factory retains real payload");
	if (dispatch)
	{
		dispatch->Start();
		check(receiver->GetHealth() == 975 && dispatch->IsObjectDead(),
			"B11 instant immediate starts actual damage through its authored hook");
		dispatch->Cancel();
	}
	for (bool cursor : {false, true})
	{
		raw->dispatch = cursor ? static_cast<NDb::Dispatch*>(new NDb::DispatchCursor) : new NDb::DispatchRockmanMace;
		CObj<PFDispatch> unsupported = CreateDispatch(cast, nullptr, Target(sender.GetPtr()),
			Target(receiver.GetPtr()), spell, 0, false, 0);
		check(cursor ? (unsupported && !dynamic_cast<PFDispatchImmediate*>(unsupported.GetPtr()))
			: dynamic_cast<PFDispatchRockmanMace*>(unsupported.GetPtr()) != nullptr,
			"cursor and mace use authored travel without silently becoming immediate damage");
		if (unsupported) unsupported->Cancel();
	}
}
