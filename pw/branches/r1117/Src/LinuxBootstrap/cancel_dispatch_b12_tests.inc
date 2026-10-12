/** Observe the production target-drop hook without replacing target or ring logic. */
struct CancelAttackerB12 : ProbeUnit
{
	int drops = 0;
	std::function<void()> action;
	CancelAttackerB12(PFWorld* world, const NDb::Unit* db)
		: ProbeUnit(world, db, NDb::FACTION_FREEZE) {}
	void OnTargetDropped() override { ++drops; if (action) action(); }
};

/** Mock only the authored parent callback; dispatch travel and registration remain real. */
struct CancelDispatchParentB12 : PFBaseApplicator
{
	std::vector<const PFDispatch*> dropped;
	std::function<void(const PFDispatch*)> action;
	explicit CancelDispatchParentB12(const PFApplCreatePars& cp) : PFBaseApplicator(cp) {}
	void OnDispatchTargetDropped(const PFDispatch* dispatch) override
	{
		dropped.push_back(dispatch);
		if (action) action(dispatch);
	}
};

/** Literal authored payloads use the existing linear/link factories and real damage. */
static NDb::Ptr<NDb::Ability> CancelDispatchDbB12(NDb::EDispatchAiming aiming, bool linked = false)
{
	NDb::Ability* raw = new NDb::Ability;
	raw->manaCost.sString = "7";
	NDb::DispatchLinear* linear = linked ? new NDb::DispatchLinearWithLink : new NDb::DispatchLinear;
	linear->speed = 5; linear->aiming = aiming; linear->maxDeviation = 180;
	raw->dispatch = linear;
	NDb::DamageApplicator* damage = new NDb::DamageApplicator;
	damage->damage.sString = "25"; damage->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
	raw->applicators.push_back(damage);
	return raw;
}

/** Match pointer identity, clear strong targeting, and leave unrelated attackers alone. */
static void CancelDispatchAttackersB12(Checks& check, PFWorld* world)
{
	CObj<PFBaseUnit> seed = Unit(world, NDb::FACTION_FREEZE, 40);
	CObj<CancelAttackerB12> selected = new CancelAttackerB12(world, seed->DbUnitDesc());
	CObj<PFBaseUnit> other = Unit(world, NDb::FACTION_FREEZE, 40);
	CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 20);
	CObj<PFBaseUnit> replacement = Unit(world, NDb::FACTION_BURN, 20);
	selected->AssignTarget(receiver.GetPtr(), true);
	other->AssignTarget(receiver.GetPtr(), true);
	check(selected->GetCurrentTarget() == receiver.GetPtr() && other->GetCurrentTarget() == receiver.GetPtr(),
		"real target assignment populates the receiver attacker ring");
	receiver->CancelAllDispatchFromUnit(nullptr);
	receiver->CancelAllDispatchFromUnit(seed.GetPtr());
	check(selected->drops == 0 && selected->IsCurrentTargetStrong(), "null and unrelated pointer do not substitute an attacker");
	receiver->CancelAllDispatchFromUnit(selected.GetPtr());
	check(!IsValid(selected->GetCurrentTarget()) && !selected->IsCurrentTargetStrong() && selected->drops == 1,
		"selected attacker loses target and strong-target state through the real DropTarget hook");
	check(other->GetCurrentTarget() == receiver.GetPtr() && other->IsCurrentTargetStrong(),
		"same-faction same-stat attacker with different identity is untouched");
	receiver->CancelAllDispatchFromUnit(selected.GetPtr());
	check(selected->drops == 1, "repeated cancellation does not repeat attacker target-drop hooks");
	selected->AssignTarget(receiver.GetPtr(), false);
	selected->action = [&] {
		other->DropTarget();
		selected->AssignTarget(replacement.GetPtr(), true);
	};
	receiver->CancelAllDispatchFromUnit(selected.GetPtr());
	check(selected->drops == 2 && selected->GetCurrentTarget() == replacement.GetPtr() && selected->IsCurrentTargetStrong(),
		"attacker callback can remove the next ring entry and retarget itself without a second drop");
	check(!IsValid(other->GetCurrentTarget()), "attacker callback ring mutation completes safely");
	selected->action = {};
	selected->DropTarget();
}

/** Unit scheduling completes dropped projectiles without damage or a payment refund. */
static void CancelDispatchPaymentsB12(Checks& check, PFWorld* world)
{
	for (NDb::EDispatchAiming aiming : {NDb::DISPATCHAIMING_STRAIGHT, NDb::DISPATCHAIMING_HOMING, NDb::DISPATCHAIMING_PURSUE})
	for (bool linked : {false, true})
	{
		CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
		CObj<PFBaseUnit> other = Unit(world, NDb::FACTION_FREEZE, 40);
		CObj<B8BehaviourCreep> receiver = B8BehaviourMoving(world, NDb::FACTION_BURN);
		check(receiver->TeleportTo(CVec2(15, 5), true, false), "paid dispatch fixture positions the real receiver");
		sender->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		other->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		NDb::Ptr<NDb::Ability> db = CancelDispatchDbB12(aiming, linked);
		CObj<PFAbilityData> data = new PFAbilityData(sender.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL);
		CObj<PFAbilityData> otherData = new PFAbilityData(other.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL);
		vector<CObj<PFAbilityInstance> > casts;
		vector<CObj<PFDispatch> > dispatches;
		for (int index = 0; index < 2; ++index)
		{
			casts.push_back(sender->CreateAbilityInstance(data, Target(receiver.GetPtr())));
			dispatches.push_back(casts.back() ? B11FindDispatch(world, casts.back().GetPtr()) : CObj<PFDispatch>());
		}
		CObj<PFAbilityInstance> otherCast = other->CreateAbilityInstance(otherData, Target(receiver.GetPtr()));
		CObj<PFDispatch> otherDispatch = otherCast ? B11FindDispatch(world, otherCast.GetPtr()) : CObj<PFDispatch>();
		check(dispatches[0] && dispatches[1] && otherDispatch && sender->GetMana() == 186 && other->GetMana() == 193,
			"actual ability activation launches three paid projectiles with distinct owners");
		if (!dispatches[0] || !dispatches[1] || !otherDispatch) continue;
		sender->Step(.25f); other->Step(.25f);
		receiver->CancelAllDispatchFromUnit(sender.GetPtr());
		for (int index = 0; index < 2; ++index)
		{
			check(dispatches[index]->GetTarget().IsPosition() &&
				dispatches[index]->GetTarget().AcquirePosition().AsVec2D() == CVec2(15, 5),
				"selected linear or linked payload drops every aiming mode to the last target position");
			check(!dispatches[index]->IsObjectDead() && !casts[index]->IsFinished(),
				"target cancellation does not kill the launched dispatch or cancel its ability");
		}
		check(otherDispatch->GetTarget().IsUnit() && otherDispatch->GetTarget().GetUnit() == receiver.GetPtr(),
			"unrelated sender retains its live payload target");
		check(receiver->GetHealth() == 1000 && sender->GetMana() == 186,
			"target drop produces no immediate damage or mana refund");
		for (int tick = 0; tick < 12; ++tick) { sender->Step(.25f); other->Step(.25f); }
		check(receiver->GetHealth() == 975 && sender->GetMana() == 186 && other->GetMana() == 193,
			"only the unrelated projectile deals damage and payment remains exact");
		check(casts[0]->IsFinished() && casts[1]->IsFinished() && otherCast->IsFinished(),
			"dropped and retained payloads finish through actual unit-driven ability scheduling");
	}
}

/** Reusable real factory inputs; only the parent callback is a test implementation. */
struct CancelDispatchFixtureB12
{
	CObj<PFBaseUnit> sender, other, receiver, replacement;
	NDb::Ptr<NDb::Ability> db;
	CObj<PFAbilityData> data;
	CObj<PFAbilityInstance> cast;
	CObj<CancelDispatchParentB12> parent;
	vector<CObj<PFDispatch> > owned;
	explicit CancelDispatchFixtureB12(PFWorld* world)
		: sender(Unit(world, NDb::FACTION_FREEZE, 40)), other(Unit(world, NDb::FACTION_FREEZE, 40)),
		receiver(B8BehaviourMoving(world, NDb::FACTION_BURN).GetPtr()), replacement(Unit(world, NDb::FACTION_BURN, 20)),
		db(CancelDispatchDbB12(NDb::DISPATCHAIMING_PURSUE))
	{
		data = new PFAbilityData(sender.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL);
		cast = new PFAbilityInstance(data, Target(receiver.GetPtr()), false);
		const Target target(receiver.GetPtr());
		PFApplCreatePars cp(cast, target); cp.pWorld = world; cp.pOwner = sender;
		cp.pDBAppl = new NDb::DamageApplicator;
		parent = new CancelDispatchParentB12(cp);
	}
	CObj<PFDispatch> Launch(PFBaseUnit* visualSource = nullptr)
	{
		NDb::Ptr<NDb::Spell> spell = db.GetPtr();
		CObj<PFDispatch> dispatch = CreateDispatch(cast, parent.GetPtr(),
			Target(visualSource ? visualSource : sender.GetPtr()), Target(receiver.GetPtr()), spell);
		owned.push_back(dispatch);
		return dispatch;
	}
	~CancelDispatchFixtureB12()
	{
		parent->action = {};
		for (const auto& dispatch : owned) if (dispatch) dispatch->Cancel();
	}
};

/** Exact sender beats visual source, faction changes, original targets and unrelated dispatch kinds. */
static void CancelDispatchIdentityB12(Checks& check, PFWorld* world)
{
	CancelDispatchFixtureB12 f(world);
	CObj<PFDispatch> dispatch = f.Launch(f.other.GetPtr());
	check(dispatch && dispatch->GetSender().GetPtr() == f.sender.GetPtr() && dispatch->GetSource().GetUnit() == f.other.GetPtr(),
		"real dispatch factory keeps actual sender distinct from visual source");
	if (!dispatch) return;
	f.receiver->CancelAllDispatchFromUnit(f.other.GetPtr());
	check(dispatch->GetTarget().IsUnit() && f.parent->dropped.empty(), "visual source is not substituted for actual dispatch sender");
	dispatch->SetNewTarget(Target(f.replacement.GetPtr()));
	f.receiver->CancelAllDispatchFromUnit(f.sender.GetPtr());
	check(dispatch->GetTarget().IsUnit() && dispatch->GetTarget().GetUnit() == f.replacement.GetPtr(),
		"old receiver cannot cancel a dispatch after inbound registration transfers");
	f.sender->ChangeFaction(NDb::FACTION_BURN);
	f.replacement->CancelAllDispatchFromUnit(f.sender.GetPtr());
	check(dispatch->GetTarget().IsPosition() && f.parent->dropped.size() == 1,
		"actual sender remains cancellable after joining the receiver faction, including pursue payloads");
	f.replacement->CancelAllDispatchFromUnit(f.sender.GetPtr());
	check(f.parent->dropped.size() == 1, "already positional dispatch never replays target-drop callbacks");
	NDb::Ability* timedDb = new NDb::Ability;
	NDb::DispatchByTime* timedDesc = new NDb::DispatchByTime; timedDesc->lifeTime.sString = "2";
	timedDb->dispatch = timedDesc;
	NDb::Ptr<NDb::Spell> spell = timedDb;
	CObj<PFDispatch> timed = CreateDispatch(f.cast, f.parent.GetPtr(), Target(f.sender.GetPtr()), Target(f.receiver.GetPtr()), spell);
	f.owned.push_back(timed);
	f.receiver->CancelAllDispatchFromUnit(f.sender.GetPtr());
	check(timed && timed->GetTarget().IsUnit() && !timed->IsObjectDead(), "nonlinear timed dispatch is outside the original inbound-linear contract");
}

/** Parent callbacks may remove self/next, retarget later candidates, and append new work. */
static void CancelDispatchMutationB12(Checks& check, PFWorld* world)
{
	CancelDispatchFixtureB12 f(world);
	CObj<PFDispatch> first = f.Launch(), removed = f.Launch(), moved = f.Launch();
	CObj<PFDispatch> added;
	check(first && removed && moved, "mutation fixture launches real registered dispatches");
	if (!first || !removed || !moved) return;
	f.parent->action = [&](const PFDispatch* dropped) {
		if (dropped != first.GetPtr()) return;
		removed->Cancel();
		moved->SetNewTarget(Target(f.replacement.GetPtr()));
		first->Cancel();
		added = f.Launch();
	};
	f.receiver->CancelAllDispatchFromUnit(f.sender.GetPtr());
	check(f.parent->dropped.size() == 1 && f.parent->dropped[0] == first.GetPtr(),
		"snapshot traversal tolerates self removal and does not notify a removed or retargeted neighbor");
	check(first->IsObjectDead() && removed->IsObjectDead(), "callback cancels current and next payload through real dispatch death");
	check(moved->GetTarget().IsUnit() && moved->GetTarget().GetUnit() == f.replacement.GetPtr(),
		"late target recheck preserves the callback's replacement receiver");
	check(added && added->GetTarget().IsUnit(), "dispatch added during callbacks is deferred to the next cancellation snapshot");
	f.parent->action = {};
	f.receiver->CancelAllDispatchFromUnit(f.sender.GetPtr());
	check(added && added->GetTarget().IsPosition() && f.parent->dropped.size() == 2,
		"next explicit cancellation processes the new live inbound dispatch once");
}

/** Nested cancellation observes target changes from the outer callback and remains bounded. */
static void CancelDispatchReentryB12(Checks& check, PFWorld* world)
{
	CancelDispatchFixtureB12 f(world);
	CObj<PFDispatch> first = f.Launch(), second = f.Launch(), third = f.Launch();
	check(first && second && third, "reentry fixture launches three engine payloads");
	if (!first || !second || !third) return;
	bool nested = false;
	f.parent->action = [&](const PFDispatch*) {
		if (nested) return;
		nested = true;
		f.receiver->CancelAllDispatchFromUnit(f.sender.GetPtr());
	};
	f.receiver->CancelAllDispatchFromUnit(f.sender.GetPtr());
	check(nested && f.parent->dropped.size() == 3, "reentrant target-loss callback drops each eligible payload exactly once");
	check(first->GetTarget().IsPosition() && second->GetTarget().IsPosition() && third->GetTarget().IsPosition(),
		"inner and outer passes jointly complete all actual target drops");
	f.receiver->CancelAllDispatchFromUnit(f.sender.GetPtr());
	check(f.parent->dropped.size() == 3, "completed nested cancellation cannot replay callbacks");
}

/** Match Windows phase order: finish attacker target loss before snapshotting inbound dispatches. */
static void CancelDispatchPhaseOrderB12(Checks& check, PFWorld* world)
{
	CancelDispatchFixtureB12 f(world);
	CObj<CancelAttackerB12> actor = new CancelAttackerB12(world, f.sender->DbUnitDesc());
	f.sender = actor.GetPtr();
	f.data = new PFAbilityData(actor.GetPtr(), f.db, NDb::ABILITYTYPEID_SPECIAL);
	f.cast = new PFAbilityInstance(f.data, Target(f.receiver.GetPtr()), false);
	actor->AssignTarget(f.receiver.GetPtr(), true);
	std::vector<int> trace;
	CObj<PFDispatch> launched;
	actor->action = [&] { trace.push_back(1); launched = f.Launch(); };
	f.parent->action = [&](const PFDispatch*) { trace.push_back(2); };
	f.receiver->CancelAllDispatchFromUnit(actor.GetPtr());
	check(trace == std::vector<int>({1, 2}) && launched && launched->GetTarget().IsPosition(),
		"attacker hook runs before the inbound phase and its new projectile participates in that phase");
	actor->action = {};
}

/** One entrypoint, using only actual engine targets, rings, dispatches and simulation. */
static void CancelDispatchB12(Checks& check, PFWorld* world)
{
	CancelDispatchAttackersB12(check, world);
	CancelDispatchPaymentsB12(check, world);
	CancelDispatchIdentityB12(check, world);
	CancelDispatchMutationB12(check, world);
	CancelDispatchReentryB12(check, world);
	CancelDispatchPhaseOrderB12(check, world);
}
