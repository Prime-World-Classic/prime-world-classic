#include "../behaviour_test_fixtures.inc"

/** Hero retaliation has its own idle/queue/auto-target policy and uses a strong state. */
static void BehaviourHeroDamageB8C(Checks& check, PFWorld* world)
{
	B8BehaviourForeignWorld foreign(check);
	if (!foreign.world)
		return;
	B8BehaviourReveal reveal(world, NDb::FACTION_FREEZE);
	for (int scenario = 0; scenario != 18; ++scenario)
	{
		CObj<B8BehaviourHero> owner = B8BehaviourHeroUnit(world, NDb::FACTION_FREEZE);
		CObj<B8BehaviourHero> sender = B8BehaviourHeroUnit(
			scenario == 16 ? foreign.world : world, scenario == 2 ? NDb::FACTION_FREEZE : NDb::FACTION_BURN);
		CObj<PFBaseUnit> current = Unit(world, NDb::FACTION_BURN, 20);
		CObj<B8HeroScreamObserver> behavior = new B8HeroScreamObserver(owner.GetPtr());
		owner->AddBehaviourOnTop(behavior.GetPtr());
		CObj<SchedulingState> order = new SchedulingState(owner.GetPtr());
		PFBaseUnitDamageDesc desc = DirectDamage();
		desc.pSender = sender.GetPtr();
		desc.dontAttackBack = false;
		switch (scenario)
		{
		case 1: desc.pSender = nullptr; break;
		case 3: sender->ChangeHealth(-1000); break;
		case 4: sender->SetVulnerable(false); break;
		case 5: sender->EnemyVisible(false); break;
		case 6: desc.dontAttackBack = true; break;
		case 7: sender->AddFlag(NDb::UNITFLAG_FORBIDAUTOTARGETME); break;
		case 8: owner->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); break;
		case 9: owner->AddFlag(NDb::UNITFLAG_FORBIDAUTOATTACK); break;
		case 10: owner->PushState(order.GetPtr()); break;
		case 11: owner->EnqueueState(order.GetPtr(), true); break;
		case 12: owner->AssignTarget(current.GetPtr(), false); break;
		case 13: owner->AssignTarget(current.GetPtr(), true); break;
		case 14: owner->AddFlag(NDb::UNITFLAG_FORBIDATTACK); break;
		case 15: owner->ChangeHealth(-1000); break;
		case 17: desc.pSender = owner.GetPtr(); break;
		}
		if (scenario == 0)
			check(sender->IsVisibleForFaction(NDb::FACTION_FREEZE), "C real fog reveals the hero sender");
		behavior->OnDamage(desc);
		const PFBaseUnitAttackState* attack = B8BehaviourAttack(*owner);
		const bool accepted = scenario == 0 || (scenario >= 12 && scenario <= 14);
		check((attack != nullptr) == accepted, "C hero retaliation follows its separate Windows gate matrix");
		check(behavior->screams == 0, "C initial zero cooldown does not scream before a tick");
		if (accepted)
		{
			check(attack && attack->GetTarget() == sender.GetPtr(), "C hero attack state keeps the direct sender");
			if (scenario != 14) owner->AddFlag(NDb::UNITFLAG_FORBIDATTACK);
			owner->FSMStep(0);
			check(owner->GetCurrentTarget() == sender.GetPtr() && owner->IsCurrentTargetStrong(),
				"C hero retaliation assigns a strong target even with an idle preexisting target");
		}
		if (scenario == 10)
			check(owner->GetCurrentState() == order.GetPtr(), "C busy hero retains its existing order");
		if (scenario == 11)
			check(owner->IsIdle() && owner->HasQueuedStates(), "C idle hero with a queued order does not retaliate");
		owner->DropStates();
	}
	CObj<B8BehaviourCreep> notHero = B8BehaviourMoving(world, NDb::FACTION_FREEZE);
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_BURN, 20);
	CObj<PFHeroBehaviour> misplaced = new PFHeroBehaviour(notHero.GetPtr());
	PFBaseUnitDamageDesc desc = DirectDamage();
	desc.pSender = sender.GetPtr();
	desc.dontAttackBack = false;
	misplaced->OnDamage(desc);
	misplaced->OnTarget(sender.GetPtr(), true);
	check(notHero->IsIdle() && !notHero->HasQueuedStates(), "C nonhero owners cannot enter hero behavior states");
	CObj<PFHeroBehaviour> detached = new PFHeroBehaviour(nullptr);
	detached->OnDamage(desc);
	detached->OnTarget(sender.GetPtr(), true);
	check(detached->OnStep(0.25f), "C null owner remains safe and cooldown step returns true");
}

/** Unlike base retaliation, hero retaliation never resolves GetMountedOn(). */
static void BehaviourHeroMountedB8C(Checks& check, PFWorld* world)
{
	B8BehaviourReveal reveal(world, NDb::FACTION_FREEZE);
	for (bool visible : {false, true})
	{
		CObj<B8BehaviourHero> owner = B8BehaviourHeroUnit(world, NDb::FACTION_FREEZE);
		CObj<B8BehaviourHero> rider = B8BehaviourHeroUnit(world, NDb::FACTION_BURN);
		CObj<B8BehaviourCreep> mount = B8BehaviourMoving(world, NDb::FACTION_BURN);
		mount->AttachUnit(rider.GetPtr());
		rider->EnemyVisible(visible);
		CObj<PFHeroBehaviour> behavior = new PFHeroBehaviour(owner.GetPtr());
		PFBaseUnitDamageDesc desc = DirectDamage();
		desc.pSender = rider.GetPtr();
		desc.dontAttackBack = false;
		check(rider->IsMounted() && mount->IsVisibleForFaction(NDb::FACTION_FREEZE),
			"C mounted hero fixture uses the production mount lifecycle");
		behavior->OnDamage(desc);
		const PFBaseUnitAttackState* attack = B8BehaviourAttack(*owner);
		check((attack != nullptr) == visible, "C hero retaliation gates visibility on the rider");
		if (visible)
			check(attack && attack->GetTarget() == rider.GetPtr(), "C hero retaliation retains the mounted rider target");
		owner->DropStates();
		mount->DetachUnit();
	}
}

/** Scream eligibility is independent of idle/queue/owner-auto flags but shares sender gates. */
static void BehaviourHeroScreamsB8C(Checks& check, PFWorld* world)
{
	B8BehaviourReveal reveal(world, NDb::FACTION_FREEZE);
	for (int scenario = 0; scenario != 10; ++scenario)
	{
		CObj<B8BehaviourHero> owner = B8BehaviourHeroUnit(world, NDb::FACTION_FREEZE);
		CObj<PFBaseUnit> sender;
		if (scenario == 5 || scenario == 6)
		{
			CObj<B8BehaviourCreep> creep = B8BehaviourMoving(world, NDb::FACTION_BURN);
			sender = creep.GetPtr();
		}
		else
		{
			CObj<B8BehaviourHero> hero = B8BehaviourHeroUnit(world, NDb::FACTION_BURN);
			sender = hero.GetPtr();
		}
		CObj<B8HeroScreamObserver> behavior = new B8HeroScreamObserver(owner.GetPtr());
		owner->AddBehaviourOnTop(behavior.GetPtr());
		CObj<PFBaseUnit> master = DrainParticipant(world, NDb::FACTION_BURN, true);
		if (scenario == 1) owner->PushState(new SchedulingState(owner.GetPtr()));
		if (scenario == 2) owner->EnqueueState(new SchedulingState(owner.GetPtr()), true);
		if (scenario == 3) owner->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		if (scenario == 4) owner->AddFlag(NDb::UNITFLAG_FORBIDAUTOATTACK);
		if (scenario == 6)
		{
			sender->SetMaster(master.GetPtr());
			master->SetAlphaSummon(sender.GetPtr());
			check(sender->IsAlphaSummon() && !sender->IsTrueHero(), "C actual alpha-summon metadata fixture");
		}
		if (scenario == 8) sender->AddFlag(NDb::UNITFLAG_FORBIDAUTOTARGETME);
		if (scenario == 9) static_cast<B8BehaviourHero*>(sender.GetPtr())->EnemyVisible(false);
		PFBaseUnitDamageDesc desc = DirectDamage();
		desc.pSender = sender.GetPtr();
		desc.dontAttackBack = scenario == 7;
		check(behavior->OnStep(0.25f), "C hero cooldown step remains enabled");
		behavior->OnDamage(desc);
		const bool screams = scenario < 5;
		check(behavior->screams == (screams ? 1 : 0), "C only eligible true heroes trigger a help scream");
		if (screams)
		{
			check(behavior->screamedTarget == sender.GetPtr()
				&& behavior->screamedType == ScreamTarget::ScreamForHelp,
				"C hero damage requests ScreamForHelp rather than ScreamAlert");
			behavior->OnDamage(desc);
			check(behavior->screams == 1, "C nonnegative cooldown suppresses a repeated scream");
			const float delay = world->GetAIWorld()->GetAIParameters().heroScreamDelay;
			check(std::isfinite(delay) && delay >= 0, "C mock hero scream delay is finite and nonnegative");
			behavior->OnStep(delay);
			behavior->OnDamage(desc);
			check(behavior->screams == 1, "C exact zero cooldown is not yet scream eligible");
			behavior->OnStep(0.25f);
			behavior->OnDamage(desc);
			check(behavior->screams == 2, "C cooldown crossing below zero rearms the help scream");
		}
		owner->DropStates();
		if (scenario == 6)
		{
			master->SetAlphaSummon(nullptr);
			sender->SetMaster(nullptr);
		}
	}
}

/** Explicit target orders use the engine queue and do not inherit automatic damage gates. */
static void BehaviourHeroTargetB8C(Checks& check, PFWorld* world)
{
	B8BehaviourForeignWorld foreign(check);
	if (!foreign.world)
		return;
	B8BehaviourReveal reveal(world, NDb::FACTION_FREEZE);
	for (bool strong : {false, true})
	for (int scenario = 0; scenario != 12; ++scenario)
	{
		CObj<B8BehaviourHero> owner = B8BehaviourHeroUnit(world, NDb::FACTION_FREEZE);
		CObj<B8BehaviourCreep> target = B8BehaviourMoving(
			scenario == 5 ? foreign.world : world, scenario == 2 ? NDb::FACTION_FREEZE : NDb::FACTION_BURN);
		CObj<PFHeroBehaviour> behavior = new PFHeroBehaviour(owner.GetPtr());
		CPtr<PFBaseUnit> requested = target.GetPtr();
		if (scenario == 1) requested = nullptr;
		if (scenario == 3) target->ChangeHealth(-1000);
		if (scenario == 4) target->SetVulnerable(false);
		if (scenario == 6) owner->ChangeHealth(-1000);
		if (scenario == 7) owner->AddFlag(NDb::UNITFLAG_FORBIDATTACK);
		if (scenario == 8) target->EnemyVisible(false);
		if (scenario == 9) owner->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET | NDb::UNITFLAG_FORBIDAUTOATTACK);
		if (scenario == 10) target->AddFlag(NDb::UNITFLAG_FORBIDAUTOTARGETME);
		if (scenario == 11) requested = owner.GetPtr();
		behavior->OnTarget(requested, strong);
		const bool accepted = scenario == 0 || (scenario >= 8 && scenario <= 10);
		check(owner->IsIdle() && owner->HasQueuedStates() == accepted,
			"C target callback enqueues valid explicit orders without automatic-selection gates");
		if (accepted && scenario != 8)
		{
			owner->AddFlag(NDb::UNITFLAG_FORBIDATTACK);
			owner->AdvanceOrders(0);
			const PFBaseUnitAttackState* attack = B8BehaviourAttack(*owner);
			check(attack && attack->GetTarget() == target.GetPtr() && !owner->HasQueuedStates(),
				"C queued order enters the existing attack state at the real FSM boundary");
			check(owner->GetCurrentTarget() == target.GetPtr() && owner->IsCurrentTargetStrong() == strong,
				"C explicit order preserves the requested strength");
		}
		owner->DropStates();
	}
	CObj<B8BehaviourHero> owner = B8BehaviourHeroUnit(world, NDb::FACTION_FREEZE);
	CObj<B8BehaviourCreep> first = B8BehaviourMoving(world, NDb::FACTION_BURN);
	CObj<B8BehaviourCreep> second = B8BehaviourMoving(world, NDb::FACTION_BURN);
	CObj<PFHeroBehaviour> behavior = new PFHeroBehaviour(owner.GetPtr());
	behavior->OnTarget(first.GetPtr(), false);
	owner->AddFlag(NDb::UNITFLAG_FORBIDATTACK);
	owner->AdvanceOrders(0);
	const IPFState* initial = owner->GetCurrentState();
	check(initial && !owner->IsCurrentTargetStrong(), "C same-target fixture has an active weak attack");
	owner->RemoveFlag(NDb::UNITFLAG_FORBIDATTACK);
	behavior->OnTarget(first.GetPtr(), true);
	check(owner->GetCurrentState() == initial && !owner->HasQueuedStates() && !owner->IsCurrentTargetStrong(),
		"C same-target callback neither queues a duplicate nor upgrades the existing strength");
	CObj<SchedulingState> discarded = new SchedulingState(owner.GetPtr());
	owner->EnqueueState(discarded.GetPtr(), true);
	behavior->OnTarget(first.GetPtr(), true);
	check(owner->HasQueuedStates() && discarded->leaves == 0,
		"C same-target deduplication preserves already queued orders");
	behavior->OnTarget(second.GetPtr(), true);
	check(owner->GetCurrentState() == initial && owner->HasQueuedStates() && discarded->leaves == 1,
		"C a new target keeps the active order until the queue boundary and flushes ejecting queued orders");
	owner->AddFlag(NDb::UNITFLAG_FORBIDATTACK);
	CObj<IPFState> removed = owner->PopState();
	if (IsValid(removed)) removed->DoLeave(true);
	owner->AdvanceOrders(0);
	const PFBaseUnitAttackState* changed = B8BehaviourAttack(*owner);
	check(changed && changed->GetTarget() == second.GetPtr() && owner->IsCurrentTargetStrong(),
		"C replacement queued order targets the newly requested unit");
	owner->DropStates();
}
