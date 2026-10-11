/** Authored timed damage shared by ordinary casts and real detached spell creation. */
static NDb::Ability* B11TimedPayload(const char* duration)
{
	NDb::Ability* raw = new NDb::Ability;
	NDb::DispatchByTime* timed = new NDb::DispatchByTime;
	timed->lifeTime.sString = duration;
	raw->dispatch = timed;
	NDb::DamageApplicator* damage = new NDb::DamageApplicator;
	damage->damage.sString = "25";
	damage->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
	raw->applicators.push_back(damage);
	return raw;
}

/** Production periodic effects, not a test travel loop, launch the nested spell. */
static NDb::Ability* B11PeriodicPayload(const NDb::Spell* spell, bool delayed = false)
{
	NDb::Ability* raw = new NDb::Ability;
	if (delayed)
	{
		NDb::DispatchByTime* timed = new NDb::DispatchByTime;
		timed->lifeTime.sString = "0.5";
		raw->dispatch = timed;
	}
	else
	{
		NDb::DispatchImmediate* immediate = new NDb::DispatchImmediate;
		immediate->instantApply = true;
		raw->dispatch = immediate;
	}
	NDb::SpellPeriodicallyApplicator* periodic = new NDb::SpellPeriodicallyApplicator;
	periodic->lifeTime.sString = "10";
	periodic->period.sString = "10";
	periodic->startOffset.sString = "0";
	periodic->spell = spell;
	raw->applicators.push_back(periodic);
	return raw;
}

/** Exercise cancellation, additions and ring removal from an actual damage callback during a world phase. */
struct B11WorldDispatchMutation : CObjectBase, PFBaseUnitEventListener
{
	OBJECT_BASIC_METHODS(B11WorldDispatchMutation);
public:
	CObj<PFDispatch> first, second, child;
	CObj<PFAbilityInstance> childAbility;
	CPtr<PFBaseUnit> sender, receiver;
	NDb::Ptr<NDb::Spell> childSpell;
	bool fired = false;
	unsigned int OnEvent(const PFBaseUnitEvent* event) override
	{
		const PFOutgoingDamageEvent* damage = dynamic_cast<const PFOutgoingDamageEvent*>(event);
		if (!fired && damage && damage->pDesc->pDealerApplicator)
		{
			fired = true;
			const PFAbilityInstance* active = damage->pDesc->pDealerApplicator->GetAbility();
			if (first && first->GetAbility() != active) first->Cancel();
			if (second && second->GetAbility() != active) second->Cancel();
			child = CreateDispatch(childAbility, nullptr, Target(sender.GetPtr()), Target(receiver.GetPtr()),
				childSpell, 0, true, 0);
			// Deliberately flush registration while the caller holds its stable dispatch-only snapshot.
			sender->GetWorld()->ProcessAddRemove();
		}
		return 0;
	}
};

/** World stepping must not double-step moving units, their casts, or their launched base attacks. */
static void DispatchWorldMixedB11(Checks& check)
{
	B8BehaviourForeignWorld isolated(check);
	PFWorld* world = isolated.world;
	if (!world) return;
	CObj<B8BehaviourCreep> sender = B8BehaviourMoving(world, NDb::FACTION_FREEZE);
	CObj<B8BehaviourCreep> receiver = B8BehaviourMoving(world, NDb::FACTION_BURN);
	receiver->TeleportTo(CVec2(15, 5));
	sender->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
	receiver->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
	CObj<PFBaseAttackData> attack = new PFBaseAttackData(sender.GetPtr(), B11AttackDescriptor(1));
	sender->ReplaceBaseAttack(attack, false);
	sender->AssignTarget(receiver.GetPtr(), true);
	check(sender->DoAttack(false), "B11 mixed world admits an authored deferred base attack");
	sender->AddFlag(NDb::UNITFLAG_FORBIDATTACK);
	CObj<PFAbilityData> ordinary = new PFAbilityData(sender.GetPtr(), B11TimedPayload("2"), NDb::ABILITYTYPEID_SPECIAL);
	CObj<PFAbilityInstance> cast = sender->CreateAbilityInstance(ordinary, Target(receiver.GetPtr()));
	CObj<PFAbilityData> periodic = new PFAbilityData(sender.GetPtr(), B11PeriodicPayload(B11TimedPayload("2")),
		NDb::ABILITYTYPEID_SPECIAL);
	CObj<PFAbilityInstance> outer = sender->CreateAbilityInstance(periodic, Target(receiver.GetPtr()));
	check(cast && outer && outer->GetActiveApplicatorsCount() == 1 && receiver->GetHealth() == 1000,
		"B11 mixed world retains ordinary, attack and real periodic delayed payloads together");
	sender->GetStat(NDb::STAT_MOVESPEED)->SetCoreValue(2);
	const float speed = sender->GetMoveSpeedMpS();
	check(speed > 0 && std::isfinite(speed), "B11 mixed world uses the actual derived movement speed");
	check(sender->MoveTo(CVec2(5, 25)), "B11 mixed world starts actual movement alongside authored dispatches");
	for (int tick = 1; tick <= 4; ++tick)
	{
		world->Step(.5f, .5f);
		check(fabs(sender->GetPos().x - 5) < .001f && fabs(sender->GetPos().y - (5 + tick * .5f * speed)) < .001f,
			"B11 dispatch phase does not double-step world-owned moving units");
		check(receiver->GetHealth() == (tick == 4 ? 925 : 1000),
			"B11 ordinary, attack and detached timers each advance once per real world tick");
	}
	check(cast && cast->IsFinished() && attack->GetAttackInstances().empty(),
		"B11 world movement retires completed primary and attack holders");
	world->Step(.5f, .5f);
	check(receiver->GetHealth() == 925, "B11 completed mixed-world payloads never replay");
	if (outer) outer->Cancel();
	sender->Stop();
}

/** Actual registered detached dispatches remain inert until started and cannot survive explicit cancel. */
static void DispatchWorldLifecycleB11(Checks& check)
{
	for (int mode = 0; mode < 4; ++mode)
	{
		B8BehaviourForeignWorld isolated(check);
		PFWorld* world = isolated.world;
		if (!world) continue;
		CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
		CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 20);
		NDb::Ptr<NDb::Ability> db = B11TimedPayload("1");
		CObj<PFAbilityData> data = new PFAbilityData(sender.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL);
		CObj<PFAbilityInstance> cast = new PFAbilityInstance(data, Target(receiver.GetPtr()), false);
		CObj<PFDispatch> dispatch = CreateDispatch(cast, nullptr, Target(sender.GetPtr()), Target(receiver.GetPtr()),
			db.GetPtr(), 0, mode != 1, 0);
		check(dispatch && receiver->GetHealth() == 1000, "B11 detached lifecycle creates an actual timed dispatch");
		if (!dispatch) continue;
		if (mode == 1)
		{
			world->Step(2, 2);
			check(!dispatch->Started() && receiver->GetHealth() == 1000,
				"B11 registered but unstarted detached dispatch is not advanced by the world");
			dispatch->Start();
		}
		if (mode == 2) { dispatch->Cancel(); dispatch->Cancel(); }
		if (mode == 3) receiver->ChangeHealth(-1000);
		world->Step(.5f, .5f);
		check(receiver->GetHealth() == (mode == 3 ? 0 : 1000), "B11 detached payload does not arrive early");
		world->Step(.5f, .5f);
		check(dispatch->IsObjectDead() && receiver->GetHealth() == (mode == 3 ? 0 : mode == 2 ? 1000 : 975),
			"B11 detached arrival respects dead targets and terminal cancellation");
		world->Step(1, 1);
		check(receiver->GetHealth() == (mode == 3 ? 0 : mode == 2 ? 1000 : 975),
			"B11 dead detached objects are not replayed from stale registration");
	}
	B8BehaviourForeignWorld isolated(check);
	PFWorld* world = isolated.world;
	if (!world) return;
	CObj<B8BehaviourCreep> sender = B8BehaviourMoving(world, NDb::FACTION_FREEZE);
	sender->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
	sender->GetStat(NDb::STAT_MOVESPEED)->SetCoreValue(2);
	const float speed = sender->GetMoveSpeedMpS();
	sender->MoveTo(CVec2(5, 25));
	CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 20);
	NDb::Ptr<NDb::Ability> db = B11TimedPayload("1");
	CObj<PFAbilityData> data = new PFAbilityData(sender.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL);
	CObj<PFAbilityInstance> cast = new PFAbilityInstance(data, Target(receiver.GetPtr()), false);
	CObj<PFDispatch> dispatch = CreateDispatch(cast, nullptr, Target(sender.GetPtr()), Target(receiver.GetPtr()), db.GetPtr(), 0, true, 0);
	const float time = world->GetTimeElapsed();
	const int step = world->GetStepNumber();
	bool valid = true;
	for (float dt : {0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		world->Step(dt, dt);
		valid = world->GetTimeElapsed() == time && world->GetStepNumber() == step
			&& sender->GetPos() == CVec2(5, 5) && receiver->GetHealth() == 1000;
		check(valid, "B11 invalid world ticks cannot advance dispatches, movement or poison the simulation clock");
		// Avoid feeding further malformed deltas into a baseline world already proven corrupt.
		if (!valid) break;
	}
	if (valid)
	{
		world->Step(1, 1);
		check(dispatch && dispatch->IsObjectDead() && receiver->GetHealth() == 975
			&& fabs(sender->GetPos().y - (5 + speed)) < .001f,
			"B11 a valid world tick still delivers and moves correctly after rejected invalid ticks");
	}
}

/** Registration mutations cannot invalidate a running phase or consume a newborn child's lifetime. */
static void DispatchWorldMutationB11(Checks& check)
{
	B8BehaviourForeignWorld isolated(check);
	PFWorld* world = isolated.world;
	if (!world) return;
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
	CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 20);
	NDb::Ptr<NDb::Ability> db = B11TimedPayload("0.5");
	CObj<PFAbilityData> data = new PFAbilityData(sender.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL);
	CObj<PFAbilityInstance> first = new PFAbilityInstance(data, Target(receiver.GetPtr()), false);
	CObj<PFAbilityInstance> second = new PFAbilityInstance(data, Target(receiver.GetPtr()), false);
	CObj<B11WorldDispatchMutation> events = new B11WorldDispatchMutation;
	events->sender = sender; events->receiver = receiver; events->childSpell = db;
	events->childAbility = new PFAbilityInstance(data, Target(receiver.GetPtr()), false);
	events->first = CreateDispatch(first, nullptr, Target(sender.GetPtr()), Target(receiver.GetPtr()), db.GetPtr(), 0, true, 0);
	events->second = CreateDispatch(second, nullptr, Target(sender.GetPtr()), Target(receiver.GetPtr()), db.GetPtr(), 0, true, 0);
	sender->AddEventListener(events.GetPtr());
	world->Step(.5f, .5f);
	check(events->fired && events->child && receiver->GetHealth() == 975,
		"B11 snapshot survives real callback cancellation and registration changes without stepping newborn work");
	world->Step(.5f, .5f);
	check(events->child && events->child->IsObjectDead() && receiver->GetHealth() == 950,
		"B11 newborn detached child receives exactly its first tick on the next world phase");
	sender->RemoveEventListener(events.GetPtr());
	// A real delayed periodic payload also creates its child during unit stepping, after phase capture.
	CObj<B8BehaviourCreep> moving = B8BehaviourMoving(world, NDb::FACTION_FREEZE);
	moving->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
	CObj<PFAbilityData> periodic = new PFAbilityData(moving.GetPtr(), B11PeriodicPayload(B11TimedPayload("0.5"), true),
		NDb::ABILITYTYPEID_SPECIAL);
	CObj<PFAbilityInstance> outer = moving->CreateAbilityInstance(periodic, Target(receiver.GetPtr()));
	world->Step(.5f, .5f);
	check(outer && outer->GetActiveApplicatorsCount() == 1 && receiver->GetHealth() == 950,
		"B11 periodic child born during moving-unit stepping is not advanced in its birth tick");
	world->Step(.5f, .5f);
	check(receiver->GetHealth() == 925, "B11 production periodic child advances in the next detached world phase");
	if (outer) outer->Cancel();
}
