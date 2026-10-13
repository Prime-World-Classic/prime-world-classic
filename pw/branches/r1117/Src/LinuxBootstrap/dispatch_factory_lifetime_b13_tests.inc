/** Factory ownership must cross synchronous Start callbacks, including worldless nested spells. */
static void DispatchFactoryLifetimeB13(Checks& check, PFWorld* world)
{
	using Result = decltype(CreateDispatch(CObj<PFAbilityInstance>(), CPtr<PFBaseApplicator>(),
		Target(), Target(), NDb::Ptr<NDb::Spell>()));
	const bool ownsResult = std::is_same<Result, CObj<PFDispatch>>::value;
	check(ownsResult, "native dispatch factory transfers strong ownership instead of a callback-invalidated raw pointer");
	// The old factory has a reproduced ASan error; do not dereference its freed result in normal tests.
	if (!ownsResult) return;
	for (bool registered : {false, true}) for (bool start : {false, true}) for (int mode = 0; mode < 5; ++mode)
	{
		CObj<PFBaseUnit> sender = Unit(registered ? world : nullptr, NDb::FACTION_FREEZE, 40);
		CObj<PFBaseUnit> receiver = Unit(registered ? world : nullptr, NDb::FACTION_BURN, 20);
		auto* raw = new NDb::Ability;
		if (mode == 2 || (mode == 3 && !registered)) {
			// Timed dispatch client creation requires a world; deferred immediate does not.
			auto* immediate = new NDb::DispatchImmediate; immediate->instantApply = mode == 2;
			raw->dispatch = immediate;
		} else if (mode == 3) {
			auto* timed = new NDb::DispatchByTime; timed->lifeTime.sString = "0.1";
			raw->dispatch = timed;
		}
		if (mode) {
			auto* damage = new NDb::DamageApplicator;
			damage->damage.sString = "25"; damage->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
			if (mode == 4) {
				auto* nested = new NDb::Spell; nested->applicators.push_back(damage);
				auto* spell = new NDb::SpellApplicator; spell->spell = nested;
				raw->applicators.push_back(spell);
			} else raw->applicators.push_back(damage);
		}
		NDb::Ptr<NDb::Ability> db = raw;
		CObj<PFAbilityData> data = new PFAbilityData(sender.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL);
		CObj<PFAbilityInstance> cast = new PFAbilityInstance(data, Target(receiver.GetPtr()), false);
		CObj<PFDispatch> dispatch = CreateDispatch(cast, nullptr, Target(sender.GetPtr()),
			Target(receiver.GetPtr()), db.GetPtr(), 0, start, 0);
		check(dispatch && dispatch->GetObjCount() > 0 && dispatch->GetSizeOf() >= sizeof(PFDispatch),
			"factory returns an owned concrete dispatch even after immediate completion");
		if (!dispatch) continue;
		check(receiver->GetHealth() == (start && mode && mode != 3 ? 975 : 1000),
			"creation preserves exact instant versus deferred damage timing");
		if (!start) {
			check(!dispatch->Started(), "unstarted worldless factory result survives before caller activation");
			dispatch->Start();
			if (!raw->dispatch) dispatch->Step(0);
		}
		if (mode == 3) dispatch->Step(.1f);
		check(dispatch->HasApplied() && receiver->GetHealth() == (mode ? 975 : 1000),
			"owned dispatch completes empty, instant, timed and nested payloads exactly once");
		dispatch->Cancel(); dispatch->Cancel(); dispatch = nullptr;
		check(receiver->GetHealth() == (mode ? 975 : 1000) && cast->GetActiveApplicatorsCount() == 0,
			"final caller release and repeated cancellation leave no active instant payload or replayed damage");
		sender->Cleanup(); receiver->Cleanup();
	}
}
