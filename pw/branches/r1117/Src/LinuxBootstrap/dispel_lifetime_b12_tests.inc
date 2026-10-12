/** Do not retain status CObjs in the fixture: removal must survive only engine ownership. */
static void DispelLifetimeB12(Checks& check, PFWorld* world)
{
	{
		CObj<PFBaseUnit> sender = Unit(nullptr, NDb::FACTION_FREEZE, 40);
		CObj<PFBaseUnit> receiver = Unit(nullptr, NDb::FACTION_NEUTRAL, 40);
		CObj<PFBaseUnit> remover = Unit(nullptr, NDb::FACTION_BURN, 40);
		auto* status = new NDb::StatusApplicator; status->lifeTime.sString = "10";
		status->dispellPriority = NDb::DISPELLPRIORITY_HIGH;
		auto* raw = new NDb::Ability; raw->type = NDb::ABILITYTYPE_ACTIVE; raw->targetType = NDb::SPELLTARGET_ALL;
		raw->applicators.push_back(status); NDb::Ptr<NDb::Ability> db = raw;
		CObj<PFAbilityInstance> cast = sender->UseExternalAbility(db, Target(receiver.GetPtr()));
		check(cast && cast->GetActiveApplicatorsCount() == 1, "worldless startup external cast leaves exactly one engine-owned status");
		auto* dispel = new NDb::DispellApplicator; dispel->maxEffectsToDispell.sString = "1";
		auto* removal = new NDb::Ability; removal->type = NDb::ABILITYTYPE_ACTIVE; removal->targetType = NDb::SPELLTARGET_ALL;
		removal->applicators.push_back(dispel); NDb::Ptr<NDb::Ability> removalDb = removal;
		CObj<PFAbilityInstance> removed = remover->UseExternalAbility(removalDb, Target(receiver.GetPtr()));
		check(removed && cast && cast->GetActiveApplicatorsCount() == 0, "startup external dispel safely retires its borrowed status");
		if (cast) cast->Cancel(); if (removed) removed->Cancel();
	}
	for (int count : {1, 3})
	for (PFWorld* context : {world, static_cast<PFWorld*>(nullptr)})
	for (bool children : {false, true})
	{
		StatusStackFixtureB12 f(context); f.other->ChangeFaction(NDb::FACTION_BURN);
		f.instance[1] = new PFAbilityInstance(f.ability[1], Target(f.hero.GetPtr()), false);
		auto* rawStatus = StatusStackFixtureB12::Db("/Mock/UnheldDispel.xdb", "3");
		rawStatus->stackCount = count; rawStatus->dispellPriority = NDb::DISPELLPRIORITY_HIGH;
		if (!children) rawStatus->applicators.clear();
		vector<NDb::Ptr<NDb::BaseApplicator>> descriptors; descriptors.push_back(rawStatus);
		for (int i = 0; i < count; ++i)
			CreateAndActivateApplicators(descriptors, f.instance[1], Target(f.hero.GetPtr()), nullptr);
		check(f.Delta() == (children ? 3 * count : 0) && f.instance[1]->GetActiveApplicatorsCount() == (children ? 2 * count : count),
			"unheld statuses and their stat children are owned solely by real engine rings");
		vector<PFBaseApplicator*> rawSelected;
		check(PFApplDispell::SearchStatus2Dispell(vector<const PFBaseUnit*>(1, f.hero.GetPtr()),
			f.hero->GetOppositeFactionFlags(), Target(f.hero.GetPtr()), rawSelected) == 1 && rawSelected.size() == count,
			"dispel groups equal DB identities while exposing only borrowed pointers");
		vector<CPtr<PFBaseApplicator>> weak;
		for (auto* status : rawSelected) weak.push_back(status);
		auto* dispel = new NDb::DispellApplicator; dispel->maxEffectsToDispell.sString = "1";
		vector<NDb::Ptr<NDb::BaseApplicator>> removal; removal.push_back(dispel);
		CreateAndActivateApplicators(removal, f.instance[0], Target(f.hero.GetPtr()), nullptr);
		check(f.Delta() == 0 && f.instance[1]->GetActiveApplicatorsCount() == 0,
			"real dispel safely stops and memorizes every unheld status without use-after-free");
		for (const auto& status : weak) check(!IsValid(status) || !status->IsAlive(), "every selected sibling becomes terminal exactly once");
		check(f.instance[0]->GetActiveApplicatorsCount() == 0, "instant dispel leaves no extra active ownership");
	}
}
