/** Observe the authored parent notification without replacing dispatch execution. */
struct DispatchAdmissionParentB13 : PFBaseApplicator
{
	OBJECT_METHODS(PFBaseApplicator::typeId, DispatchAdmissionParentB13);
public:
	DispatchAdmissionParentB13() = default;
	explicit DispatchAdmissionParentB13(const PFApplCreatePars& cp) : PFBaseApplicator(cp) {}
	int misses = 0;
	const PFDispatch* last = nullptr;
	std::function<void(const PFDispatch*)> action;
	void OnDispatchMissed(const PFDispatch* dispatch) override
	{
		++misses; last = dispatch;
		if (action) action(dispatch);
	}
};

/** Inject malformed query results after stat clamping; admission remains the production method. */
struct DispatchChanceUnitB13 : ProbeUnit
{
	float miss = 0, evasion = 0;
	DispatchChanceUnitB13(PFWorld* world, const NDb::Unit* db, NDb::EFaction faction) : ProbeUnit(world, db, faction) {}
	float GetMissChance() const override { return miss; }
	float GetEvasion() const override { return evasion; }
	void SeedSleep() { bSleep = true; }
};

/** Own a real unstarted immediate dispatch and observe only its engine admission events. */
struct DispatchAdmissionFixtureB13
{
	CObj<PFBaseUnit> sender, receiver;
	NDb::Ptr<NDb::BaseAttack> db;
	CObj<PFAbilityData> data;
	CObj<PFAbilityInstance> cast;
	CObj<DispatchAdmissionParentB13> parent;
	CObj<PFDispatch> dispatch;
	CObj<EventDispatchB7Listener> listener;
	std::vector<NDb::EBaseUnitEvent> events;
	bool cancel = false;

	DispatchAdmissionFixtureB13(PFBaseUnit* source, PFBaseUnit* target, bool base = true,
		bool evadable = true, bool always = false) : sender(source), receiver(target)
	{
		db = B11AttackDescriptor(0);
		auto* raw = const_cast<NDb::DispatchImmediate*>(static_cast<const NDb::DispatchImmediate*>(db->dispatch.GetPtr()));
		raw->instantApply = true; raw->alwaysApply = always;
		raw->flags = evadable ? NDb::DISPATCHFLAGS_EVADABLE : NDb::EDispatchFlags(0);
		data = base ? static_cast<PFAbilityData*>(new PFBaseAttackData(sender.GetPtr(), db))
			: new PFAbilityData(sender.GetPtr(), db.GetPtr(), NDb::ABILITYTYPEID_SPECIAL);
		cast = new PFAbilityInstance(data, Target(receiver.GetPtr()), false);
		const Target parentTarget(receiver.GetPtr());
		PFApplCreatePars cp(cast, parentTarget); cp.pWorld = sender->GetWorld(); cp.pOwner = sender;
		cp.pDBAppl = new NDb::DamageApplicator;
		parent = new DispatchAdmissionParentB13(cp);
		const NDb::Ptr<NDb::Spell> spell = db.GetPtr();
		dispatch = CreateDispatch(cast, parent.GetPtr(), Target(sender.GetPtr()), Target(receiver.GetPtr()), spell, 0, false, 0);
		listener = new EventDispatchB7Listener;
		listener->action = [this](EventDispatchB7Listener&, const PFBaseUnitEvent* event) {
			if (const auto* admission = dynamic_cast<const PFBaseUnitDispatchEvent*>(event))
			{
				events.push_back(event->GetType());
				if (cancel && event->GetType() == NDb::BASEUNITEVENT_DISPATCHAPPLIED) admission->CancelDispatch();
			}
			return 0u;
		};
		receiver->AddEventListener(listener.GetPtr());
	}

	/** Remove the borrowed fixture callback before any captured members are released. */
	~DispatchAdmissionFixtureB13()
	{
		receiver->RemoveEventListener(listener.GetPtr()); listener->action = nullptr;
		parent->action = nullptr;
		if (dispatch) dispatch->Cancel();
	}
};

/** Preserve original miss precedence, two RNG draws, event cancellation and payload ownership. */
static void DispatchAdmissionB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	RoutingFixtureB13 world(check); if (!world.world) return;
	for (bool cancelEvent : {false, true})
	{
		auto sender = Unit(world.world, NDb::FACTION_FREEZE, 20);
		auto target = Unit(world.world, NDb::FACTION_BURN, 20);
		sender->GetStat(NDb::STAT_MISSCHANCE)->SetCoreValue(cancelEvent ? 0 : 100);
		DispatchAdmissionFixtureB13 f(sender.GetPtr(), target.GetPtr()); f.cancel = cancelEvent;
		if (!f.dispatch) { check(false, "B13 reentrant parent fixture creates a dispatch"); continue; }
		f.parent->action = [&](const PFDispatch*) { f.dispatch->Cancel(); };
		const float health = target->GetHealth(); f.dispatch->Start();
		check(f.dispatch->IsObjectDead() && target->GetHealth() == health && f.parent->misses == 1,
			"B13 parent miss notification may cancel the current dispatch without delivery or duplicate callbacks");
		check(f.events.size() == size_t(cancelEvent ? 2 : 1) && f.events.back() == NDb::BASEUNITEVENT_DISPATCHMISSED,
			"B13 parent cancellation retains the original final miss event");
	}
	for (bool base : {false, true})
	{
		auto sender = Unit(nullptr, NDb::FACTION_FREEZE, 20), target = Unit(nullptr, NDb::FACTION_BURN, 20);
		DispatchAdmissionFixtureB13 f(sender.GetPtr(), target.GetPtr(), base);
		if (!f.dispatch) { check(false, "B13 worldless fixture creates a dispatch"); continue; }
		check(target->OnDispatchApply(*f.dispatch) == !base && f.events.size() == size_t(!base),
			"B13 missing RNG world rejects evadable attacks without rejecting ordinary diagnostic spells");
	}
	for (bool hostile : {false, true})
	for (bool cancel : {false, true})
	{
		auto sender = Unit(world.world, hostile ? NDb::FACTION_FREEZE : NDb::FACTION_NEUTRAL, 20);
		CObj<DispatchChanceUnitB13> target = new DispatchChanceUnitB13(world.world, sender->DbUnitDesc(), NDb::FACTION_NEUTRAL);
		target->SeedSleep();
		DispatchAdmissionFixtureB13 f(sender.GetPtr(), target.GetPtr(), false, false); f.cancel = cancel;
		if (!f.dispatch) { check(false, "B13 neutral wakeup fixture creates a dispatch"); continue; }
		check(target->OnDispatchApply(*f.dispatch) == !cancel && target->IsSleep() == !(hostile && !cancel),
			"B13 only admitted hostile dispatch wakes a sleeping neutral unit");
	}
	for (bool hero : {false, true})
	for (bool base : {false, true})
	for (bool hostile : {false, true})
	for (bool evadable : {false, true})
	{
		auto sender = Unit(world.world, NDb::FACTION_BURN, 20);
		CObj<PFBaseUnit> target = hero ? CObj<PFBaseUnit>(B9MaleHero(world.world, NDb::FACTION_FREEZE).GetPtr())
			: Unit(world.world, NDb::FACTION_FREEZE, 20);
		if (!hostile) sender->ChangeFaction(target->GetFaction());
		sender->GetStat(NDb::STAT_MISSCHANCE)->SetCoreValue(100);
		target->GetStat(NDb::STAT_EVASION)->SetCoreValue(100);
		DispatchAdmissionFixtureB13 f(sender.GetPtr(), target.GetPtr(), base, evadable);
		check(f.dispatch != nullptr, "B13 dispatch admission fixture owns the real authored dispatch");
		if (!f.dispatch) continue;
		world.world->GetRndGen()->SetSeed(918); NRandom::RandomGenerator expected(918);
		const bool rejects = base && hostile && evadable;
		if (rejects) { expected.Next(100); expected.Next(100); }
		check(target->OnDispatchApply(*f.dispatch) != rejects,
			"B13 only hostile evadable base attacks use miss/evasion admission, including heroes");
		check(world.world->GetRndGen()->Next() == expected.Next(),
			"B13 eligible admission consumes exactly two original RNG draws even when the first misses");
		check(f.events.size() == 1 && f.events[0] == (rejects ? NDb::BASEUNITEVENT_DISPATCHMISSED : NDb::BASEUNITEVENT_DISPATCHAPPLIED),
			"B13 miss takes precedence over evasion and emits one original dispatch event");
		check(f.parent->misses == int(rejects) && (!rejects || f.parent->last == f.dispatch.GetPtr()),
			"B13 original parent receives the exact missed dispatch once");
	}
	for (float miss : {-10.f, 0.f, .9f, 34.9f, 100.f, 1.e30f})
	for (float evasion : {0.f, 49.9f, 100.f})
	for (unsigned seed : {3u, 831u, 918u})
	{
		auto sender = Unit(world.world, NDb::FACTION_FREEZE, 20);
		auto target = Unit(world.world, NDb::FACTION_BURN, 20);
		sender->GetStat(NDb::STAT_MISSCHANCE)->SetCoreValue(miss);
		target->GetStat(NDb::STAT_EVASION)->SetCoreValue(evasion);
		DispatchAdmissionFixtureB13 f(sender.GetPtr(), target.GetPtr());
		if (!f.dispatch) { check(false, "B13 probability fixture creates a dispatch"); continue; }
		world.world->GetRndGen()->SetSeed(seed); NRandom::RandomGenerator expected(seed);
		const bool missed = expected.Next(100) < int(Clamp(sender->GetMissChance(), 0.f, 100.f));
		const bool evaded = expected.Next(100) < int(Clamp(target->GetEvasion(), 0.f, 100.f));
		check(target->OnDispatchApply(*f.dispatch) == !(missed || evaded),
			"B13 authored finite chances retain truncation, boundaries and original seeded outcome");
		check(world.world->GetRndGen()->Next() == expected.Next(), "B13 probability sampling preserves the world RNG stream");
		const auto event = missed ? NDb::BASEUNITEVENT_DISPATCHMISSED : evaded ? NDb::BASEUNITEVENT_DISPATCHEVADED : NDb::BASEUNITEVENT_DISPATCHAPPLIED;
		check(f.events.size() == 1 && f.events[0] == event, "B13 probability outcome selects the exact applied/missed/evaded event");
	}
	for (bool hero : {false, true})
	for (int outcome = 0; outcome < 4; ++outcome)
	{
		auto sender = Unit(world.world, NDb::FACTION_BURN, 20);
		CObj<PFBaseUnit> target = hero ? CObj<PFBaseUnit>(B9MaleHero(world.world, NDb::FACTION_FREEZE).GetPtr())
			: Unit(world.world, NDb::FACTION_FREEZE, 20);
		sender->GetStat(NDb::STAT_MISSCHANCE)->SetCoreValue(outcome == 1 ? 100 : 0);
		target->GetStat(NDb::STAT_EVASION)->SetCoreValue(outcome == 2 ? 100 : 0);
		DispatchAdmissionFixtureB13 f(sender.GetPtr(), target.GetPtr()); f.cancel = outcome == 3;
		if (!f.dispatch) { check(false, "B13 payload fixture creates a dispatch"); continue; }
		const float health = target->GetHealth();
		f.dispatch->Start();
		check(target->GetHealth() == health - (outcome == 0 ? 25 : 0) && f.dispatch->IsObjectDead(),
			"B13 real dispatch delivers damage only after successful admission and retires every outcome");
		check(outcome != 3 || (f.events.size() == 2 && f.events[0] == NDb::BASEUNITEVENT_DISPATCHAPPLIED
			&& f.events[1] == NDb::BASEUNITEVENT_DISPATCHMISSED),
			"B13 cancelling the shared applied event reports one miss without delivering the payload");
		check(f.parent->misses == int(outcome != 0), "B13 rejected real payload notifies its authored parent once");
		const size_t count = f.events.size(); f.dispatch->Step(.1f);
		check(f.events.size() == count && target->GetHealth() == health - (outcome == 0 ? 25 : 0),
			"B13 terminal dispatch cannot repeat damage or admission events");
	}
	for (bool hero : {false, true})
	for (bool always : {false, true})
	{
		auto sender = Unit(world.world, NDb::FACTION_BURN, 20);
		CObj<PFBaseUnit> target = hero ? CObj<PFBaseUnit>(B9MaleHero(world.world, NDb::FACTION_FREEZE).GetPtr())
			: Unit(world.world, NDb::FACTION_FREEZE, 20);
		target->EnableDispatch(false);
		DispatchAdmissionFixtureB13 f(sender.GetPtr(), target.GetPtr(), false, false, always);
		if (!f.dispatch) { check(false, "B13 waiting fixture creates a dispatch"); continue; }
		const float health = target->GetHealth(); f.dispatch->Start();
		check(target->GetHealth() == health - (always ? 25 : 0) && f.events.size() == size_t(always),
			"B13 alwaysApply bypasses dispatch waiting, not admission events");
		target->EnableDispatch(true); f.dispatch->Step(.1f);
		check(target->GetHealth() == health - 25 && f.events.size() == 1 && f.dispatch->IsObjectDead(),
			"B13 releasing waiting dispatch delivers exactly one real payload and applied event");
	}
	for (bool badSender : {false, true})
	for (float value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
		-std::numeric_limits<float>::infinity()})
	{
		auto seed = Unit(world.world, NDb::FACTION_FREEZE, 20);
		CObj<DispatchChanceUnitB13> sender = new DispatchChanceUnitB13(world.world, seed->DbUnitDesc(), NDb::FACTION_FREEZE);
		CObj<DispatchChanceUnitB13> target = new DispatchChanceUnitB13(world.world, seed->DbUnitDesc(), NDb::FACTION_BURN);
		if (badSender) sender->miss = value; else target->evasion = value;
		DispatchAdmissionFixtureB13 f(sender.GetPtr(), target.GetPtr());
		if (!f.dispatch) { check(false, "B13 malformed probability fixture creates a dispatch"); continue; }
		world.world->GetRndGen()->SetSeed(918); NRandom::RandomGenerator expected(918);
		check(!target->OnDispatchApply(*f.dispatch), "B13 malformed probability cannot admit a hostile evadable attack");
		check(world.world->GetRndGen()->Next() == expected.Next() && f.events.empty() && f.parent->misses == 0,
			"B13 malformed probability rejects before RNG consumption or invented miss/evasion events");
	}
	for (bool hostile : {false, true})
	{
		RoutingFixtureB13 fworld(check); if (!fworld.world) return;
		auto sender = B9MaleHero(fworld.world, hostile ? NDb::FACTION_BURN : NDb::FACTION_FREEZE);
		auto target = B9MaleHero(fworld.world, NDb::FACTION_FREEZE);
		auto* db = new NDb::Tower; db->stats = target->DbUnitDesc()->stats; db->faction = NDb::FACTION_FREEZE;
		db->targetingParams = target->GetTargetingParamsPtr();
		NDb::AdvMapObject object; object.gameObject = db;
		object.offset = CPlacement(CVec3(5, 7, 0), QNULL, CVec3(1, 1, 1));
		CObj<PFTower> tower = new PFTower(fworld.world, object); tower->GetStat(NDb::STAT_RANGE)->SetCoreValue(8);
		DispatchAdmissionFixtureB13 f(sender.GetPtr(), target.GetPtr(), false, false);
		if (!f.dispatch) { check(false, "B13 tower-help fixture creates a dispatch"); continue; }
		const float health = target->GetHealth();
		check(target->OnDispatchApply(*f.dispatch) && target->GetHealth() == health,
			"B13 dispatch arrival can request tower help before any damage applicator executes");
		check((tower->GetCurrentTarget() == sender.GetPtr()) == hostile && tower->IsPriorityTarget(sender.GetPtr()) == hostile,
			"B13 hostile hero dispatch calls original tower help while friendly arrival does not");
	}
	std::printf("Dispatch admission B13: checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
