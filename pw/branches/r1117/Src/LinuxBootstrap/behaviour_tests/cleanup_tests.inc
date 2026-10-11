#include "../behaviour_test_fixtures.inc"

/** Verify cleanup against real orders and both protected combat-memory slots. */
static void BehaviourCleanupB8A(Checks& check, PFWorld* world)
{
	CObj<B8BehaviourCreep> owner = B8BehaviourMoving(world, NDb::FACTION_FREEZE);
	CObj<PFBaseUnit> target = Unit(world, NDb::FACTION_BURN, 20);
	CObj<PFBaseUnit> alarm = Unit(world, NDb::FACTION_BURN, 20);
	CObj<PFBaseBehaviour> behavior = new PFBaseBehaviour(owner.GetPtr());
	owner->AddBehaviourOnTop(behavior.GetPtr());
	CObj<SchedulingState> active = new SchedulingState(owner.GetPtr());
	CObj<SchedulingState> queued = new SchedulingState(owner.GetPtr());
	owner->PushState(active.GetPtr());
	owner->FSMStep(0);
	owner->EnqueueState(queued.GetPtr(), true);
	owner->AssignTarget(target.GetPtr(), true);
	owner->OnScream(alarm.GetPtr(), ScreamTarget::ScreamForHelp);
	// A strong target rejects screams; seed one through the public hook before strengthening.
	owner->DropTarget();
	owner->OnScream(alarm.GetPtr(), ScreamTarget::ScreamForHelp);
	owner->AssignTarget(target.GetPtr(), true);
	owner->SetLastAttackData(target.GetPtr(), true, NDb::FACTION_BURN);
	owner->SeedHeroAttack(alarm.GetPtr());
	check(active->entries == 1 && !owner->IsIdle() && owner->HasQueuedStates(),
		"A cleanup fixture has a running and queued order");
	check(owner->GetCurrentTarget() == target.GetPtr() && owner->IsCurrentTargetStrong()
		&& owner->ScreamCount() == 1, "A cleanup fixture contains target and scream memory");
	check(owner->GetLastAttackData()->pVictim == target.GetPtr()
		&& owner->GetLastHeroAttackData()->pVictim == alarm.GetPtr(),
		"A cleanup fixture contains ordinary and hero attack memory");
	behavior->Disable();
	check(!behavior->IsEnabled() && owner->IsIdle() && !owner->HasQueuedStates(),
		"A Disable clears active and queued states");
	check(active->leaves == 1 && queued->leaves == 1, "A Disable leaves both real state objects once");
	check(!IsValid(owner->GetCurrentTarget()) && !owner->IsCurrentTargetStrong()
		&& owner->ScreamCount() == 0, "A Disable clears target strength and scream memory");
	check(!IsValid(owner->GetLastAttackData()->pVictim)
		&& !IsValid(owner->GetLastHeroAttackData()->pVictim), "A Disable clears both attack-memory victims");
	behavior->Disable();
	check(active->leaves == 1 && queued->leaves == 1, "A repeated Disable is idempotent");
	behavior->Enable();
	check(behavior->IsEnabled() && owner->IsIdle() && !IsValid(owner->GetCurrentTarget()),
		"A Enable does not resurrect dropped combat orders");
	owner->ChangeHealth(-1000);
	owner->SetLastAttackData(target.GetPtr(), true, NDb::FACTION_BURN);
	owner->SeedHeroAttack(alarm.GetPtr());
	behavior->Disable();
	check(owner->IsDead() && !IsValid(owner->GetLastAttackData()->pVictim)
		&& !IsValid(owner->GetLastHeroAttackData()->pVictim), "A valid dead owner still receives cleanup");
	CObj<PFBaseBehaviour> detached = new PFBaseBehaviour(nullptr);
	detached->Disable();
	check(!detached->IsEnabled(), "A null owner can be disabled");
}

/** Ally classification is faction metadata, including changed and dead victims. */
static void BehaviourAllyB8A(Checks& check, PFWorld* world)
{
	CObj<B8BehaviourCreep> owner = B8BehaviourMoving(world, NDb::FACTION_FREEZE);
	CObj<PFBaseBehaviour> behavior = new PFBaseBehaviour(owner.GetPtr());
	for (NDb::EFaction current : {NDb::FACTION_FREEZE, NDb::FACTION_BURN, NDb::FACTION_NEUTRAL})
	for (NDb::EFaction original : {NDb::FACTION_FREEZE, NDb::FACTION_BURN, NDb::FACTION_NEUTRAL})
	{
		CObj<PFBaseUnit> victim = Unit(world, current, 20);
		check(behavior->IsAttackedUnitAlly(victim.GetPtr(), original)
			== (current == NDb::FACTION_FREEZE || original == NDb::FACTION_FREEZE),
			"A forwards current and original faction to the engine classifier");
		victim->ChangeHealth(-1000);
		check(behavior->IsAttackedUnitAlly(victim.GetPtr(), original)
			== owner->IsAttackedUnitAlly(victim.GetPtr(), original), "A dead victim retains ally metadata");
	}
	CObj<PFBaseBehaviour> detached = new PFBaseBehaviour(nullptr);
	CObj<PFBaseUnit> ally = Unit(world, NDb::FACTION_FREEZE, 20);
	check(!behavior->IsAttackedUnitAlly(CPtr<PFBaseUnit>(), NDb::FACTION_FREEZE)
		&& !detached->IsAttackedUnitAlly(ally.GetPtr(), NDb::FACTION_FREEZE),
		"A absent owner or victim fails closed");
	B8BehaviourForeignWorld foreign(check);
	if (foreign.world)
	{
		CObj<PFBaseUnit> victim = Unit(foreign.world, NDb::FACTION_BURN, 20);
		check(behavior->IsAttackedUnitAlly(victim.GetPtr(), NDb::FACTION_FREEZE),
			"A foreign metadata retains Windows original-faction classification");
	}
}
