#include "../behaviour_test_fixtures.inc"

/** Cover Windows base-retaliation gates without stepping movement or synthesizing attacks. */
static void BehaviourRetaliationB8B(Checks& check, PFWorld* world)
{
	B8BehaviourForeignWorld foreign(check);
	if (!foreign.world)
		return;
	B8BehaviourReveal reveal(world, NDb::FACTION_FREEZE);
	for (int scenario = 0; scenario != 19; ++scenario)
	{
		CObj<B8BehaviourCreep> owner = B8BehaviourMoving(world, NDb::FACTION_FREEZE);
		CObj<B8BehaviourCreep> sender = B8BehaviourMoving(
			scenario == 13 ? foreign.world : world, scenario == 2 ? NDb::FACTION_FREEZE : NDb::FACTION_BURN);
		CObj<PFBaseUnit> current = Unit(world, NDb::FACTION_BURN, 20);
		CObj<B8BaseScreamObserver> behavior = new B8BaseScreamObserver(owner.GetPtr());
		owner->AddBehaviourOnTop(behavior.GetPtr());
		PFBaseUnitDamageDesc desc = DirectDamage();
		desc.pSender = sender.GetPtr();
		desc.amount = 1;
		desc.dontAttackBack = false;
		const bool accepted = scenario == 0 || scenario == 11 || (scenario >= 14 && scenario <= 17);
		switch (scenario)
		{
		case 1: desc.pSender = nullptr; break;
		case 3: sender->ChangeHealth(-1000); break;
		case 4: sender->SetVulnerable(false); break;
		case 5: sender->EnemyVisible(false); break;
		case 6: desc.dontAttackBack = true; break;
		case 7: owner->AddFlag(NDb::UNITFLAG_FORBIDATTACK); break;
		case 8: owner->AssignTarget(current.GetPtr(), false); break;
		case 9: owner->AssignTarget(current.GetPtr(), true); break;
		case 10:
			owner->AssignTarget(current.GetPtr(), false);
			current->SetVulnerable(false);
			current->Hide(true);
			break;
		case 11:
			owner->AssignTarget(current.GetPtr(), true);
			current->ChangeHealth(-1000);
			break;
		case 12: owner->ChangeHealth(-1000); break;
		case 14: owner->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); break;
		case 15: owner->AddFlag(NDb::UNITFLAG_FORBIDAUTOATTACK); break;
		case 16: sender->AddFlag(NDb::UNITFLAG_FORBIDAUTOTARGETME); break;
		case 17:
			owner->PushState(new SchedulingState(owner.GetPtr()));
			owner->EnqueueState(new SchedulingState(owner.GetPtr()), true);
			break;
		case 18: desc.pSender = owner.GetPtr(); break;
		}
		if (scenario == 0)
			check(sender->IsVisibleForFaction(NDb::FACTION_FREEZE), "B real fog reveals the damage sender");
		if (scenario == 5)
			check(!sender->IsVisibleForFaction(NDb::FACTION_FREEZE), "B cached stealth fixture hides the sender");
		behavior->OnDamage(desc);
		const PFBaseUnitAttackState* attack = B8BehaviourAttack(*owner);
		check((attack != nullptr) == accepted, "B base retaliation follows the Windows gate matrix");
		if (accepted)
		{
			check(attack && attack->GetTarget() == sender.GetPtr(), "B PushAttackState retains the admitted sender");
			check(behavior->screams == (scenario == 16 ? 0 : 1),
				"B helper requests an alert subject to the existing unit scream gate");
			if (behavior->screams)
				check(behavior->screamedTarget == sender.GetPtr()
					&& behavior->screamedType == ScreamTarget::ScreamAlert, "B retaliation requests ScreamAlert");
			if (scenario == 17)
				check(owner->GetStackSize() == 2 && owner->HasQueuedStates(),
					"B base retaliation pushes over a current order without flushing its queue");
			// Hold the admitted production state before its first combat step.
			owner->AddFlag(NDb::UNITFLAG_FORBIDATTACK);
			owner->FSMStep(0);
			check(owner->GetCurrentTarget() == sender.GetPtr() && !owner->IsCurrentTargetStrong(),
				"B real attack-state entry assigns a weak retaliation target");
		}
		else
			check(behavior->screams == 0, "B rejected damage does not request an alert");
		if (scenario >= 8 && scenario <= 10)
			check(owner->GetCurrentTarget() == current.GetPtr() && owner->IsCurrentTargetStrong() == (scenario == 9),
				"B any live current target wins even if weak hidden or invulnerable");
		owner->DropStates();
	}
	CObj<PFBaseBehaviour> detached = new PFBaseBehaviour(nullptr);
	PFBaseUnitDamageDesc desc = DirectDamage();
	detached->OnDamage(desc);
	check(detached->GetUnit().GetPtr() == nullptr, "B null owner damage callback is harmless");
}

/** Mount endpoint access is a fixture seam, not a duplicate retaliation decision. */
static void BehaviourMountedRetaliationB8B(Checks& check, PFWorld* world)
{
	B8BehaviourForeignWorld foreign(check);
	if (!foreign.world)
		return;
	B8BehaviourReveal reveal(world, NDb::FACTION_FREEZE);
	for (int scenario = 0; scenario != 10; ++scenario)
	{
		CObj<B8BehaviourCreep> owner = B8BehaviourMoving(world, NDb::FACTION_FREEZE);
		CObj<B8BehaviourCreep> mount = B8BehaviourMoving(
			scenario == 2 ? foreign.world : world, scenario == 4 ? NDb::FACTION_FREEZE : NDb::FACTION_BURN);
		PFWorld* riderWorld = scenario == 9 ? foreign.world : world;
		NDb::Ptr<NDb::Creature> db = B8BehaviourCreatureDb(riderWorld, NDb::FACTION_BURN);
		CObj<B8BehaviourRider> rider = new B8BehaviourRider(riderWorld, *db, NDb::FACTION_BURN);
		// Physical attachment is local; resolved endpoint metadata can still be foreign.
		CObj<B8BehaviourCreep> carrier = mount;
		if (mount->GetWorld() != riderWorld) carrier = B8BehaviourMoving(riderWorld, NDb::FACTION_BURN);
		carrier->AttachUnit(rider.GetPtr());
		rider->mountedOn = scenario == 1 ? nullptr : static_cast<PFBaseUnit*>(mount.GetPtr());
		if (scenario == 3) mount->ChangeHealth(-1000);
		if (scenario == 5) mount->EnemyVisible(false);
		if (scenario == 6) mount->SetVulnerable(false);
		if (scenario == 7) rider->EnemyVisible(false);
		if (scenario == 8) rider->ChangeHealth(-1000);
		CObj<PFBaseBehaviour> behavior = new PFBaseBehaviour(owner.GetPtr());
		owner->AddBehaviourOnTop(behavior.GetPtr());
		PFBaseUnitDamageDesc desc = DirectDamage();
		desc.pSender = rider.GetPtr();
		desc.dontAttackBack = false;
		check(rider->IsMounted(), "B rider enters the real mounted move state");
		behavior->OnDamage(desc);
		const PFBaseUnitAttackState* attack = B8BehaviourAttack(*owner);
		const bool accepted = scenario == 0 || scenario == 7 || scenario == 8;
		check((attack != nullptr) == accepted, "B mounted retaliation validates the resolved endpoint");
		if (accepted)
			check(attack && attack->GetTarget() == mount.GetPtr(),
				"B retaliation uses mount visibility and liveness rather than rider visibility or health");
		owner->DropStates();
		carrier->DetachUnit();
	}
	CObj<B8BehaviourCreep> owner = B8BehaviourMoving(world, NDb::FACTION_FREEZE);
	CObj<B8BehaviourCreep> mount = B8BehaviourMoving(world, NDb::FACTION_BURN);
	CObj<B8BehaviourCreep> rider = B8BehaviourMoving(world, NDb::FACTION_BURN);
	mount->AttachUnit(rider.GetPtr());
	CObj<PFBaseBehaviour> behavior = new PFBaseBehaviour(owner.GetPtr());
	PFBaseUnitDamageDesc desc = DirectDamage();
	desc.pSender = rider.GetPtr();
	desc.dontAttackBack = false;
	check(rider->IsMounted() && !IsValid(rider->GetMountedOn()), "B baseline mounted endpoint remains unimplemented");
	behavior->OnDamage(desc);
	check(owner->IsIdle(), "B a real null mounted endpoint does not fall back to the rider");
	mount->DetachUnit();
}

/** Exercise real post-health damage dispatch and its existing caller-owned enable gate. */
static void BehaviourDamageDispatchB8B(Checks& check, PFWorld* world)
{
	B8BehaviourReveal reveal(world, NDb::FACTION_FREEZE);
	for (bool enabled : {false, true})
	for (bool automaticOnly : {false, true})
	{
		CObj<B8BehaviourCreep> owner = B8BehaviourMoving(world, NDb::FACTION_FREEZE);
		CObj<B8BehaviourCreep> sender = B8BehaviourMoving(world, NDb::FACTION_BURN);
		NDb::UnitTargetingParameters* targeting = new NDb::UnitTargetingParameters;
		targeting->onlyAutoTargeting = automaticOnly;
		owner->OverrideTargetingParams(targeting);
		CObj<PFBaseBehaviour> behavior = new PFBaseBehaviour(owner.GetPtr());
		owner->AddBehaviourOnTop(behavior.GetPtr());
		if (!enabled) behavior->Disable();
		PFBaseUnitDamageDesc desc = DirectDamage();
		desc.pSender = sender.GetPtr();
		desc.damageType = NDb::APPLICATORDAMAGETYPE_PURE;
		desc.amount = 1;
		desc.dontAttackBack = false;
		check(owner->OnDamage(desc) == 1 && owner->GetHealth() == 999,
			"B actual damage callback preserves the health delta");
		check((B8BehaviourAttack(*owner) != nullptr) == (enabled && !automaticOnly),
			"B actual damage dispatch admits only an enabled nonautomatic behavior");
		owner->DropStates();
	}
}
