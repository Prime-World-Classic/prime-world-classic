#include "../behaviour_test_fixtures.inc"

/** Observe dispatch without assigning targets or substituting an attack-state model. */
struct B8TargetObserver : PFBaseBehaviour
{
	int calls = 0;
	CPtr<PFBaseUnit> received;
	bool strong = false;
	explicit B8TargetObserver(PFBaseMovingUnit* owner) : PFBaseBehaviour(owner) {}
	void OnTarget(const CPtr<PFBaseUnit>& target, bool requestedStrong) override
	{
		++calls; received = target; strong = requestedStrong;
	}
};

/** Unit admission belongs to the enabled top behavior, not an unconditional assignment. */
static void BehaviourTargetDispatchB8(Checks& check, PFWorld* world)
{
	for (int policy = 0; policy < 3; ++policy)
	for (bool enabled : {false, true})
	for (bool strong : {false, true})
	{
		NDb::Ptr<NDb::Creature> db = B8BehaviourCreatureDb(world, NDb::FACTION_FREEZE);
		if (policy == 0)
			const_cast<NDb::Creature*>(db.GetPtr())->targetingParams = nullptr;
		CObj<B8BehaviourCreep> owner = new B8BehaviourCreep(world, *db, NDb::FACTION_FREEZE);
		CObj<PFBaseUnit> previous = Unit(world, NDb::FACTION_BURN, 20);
		CObj<PFBaseUnit> requested = Unit(world, NDb::FACTION_BURN, 20);
		if (policy != 0)
		{
			NDb::UnitTargetingParameters* params = new NDb::UnitTargetingParameters;
			params->onlyAutoTargeting = policy == 2;
			owner->OverrideTargetingParams(params);
		}
		check((owner->GetTargetingParamsPtr() == nullptr) == (policy == 0),
			"dispatch fixture distinguishes missing and authored targeting configuration");
		CObj<B8TargetObserver> lower = new B8TargetObserver(owner.GetPtr());
		CObj<B8TargetObserver> top = new B8TargetObserver(owner.GetPtr());
		owner->AddBehaviourOnTop(lower.GetPtr()); owner->AddBehaviourOnTop(top.GetPtr());
		if (!enabled) top->Disable();
		owner->AssignTarget(previous.GetPtr(), true);
		owner->OnTarget(requested.GetPtr(), strong);
		const bool delivered = enabled && policy != 2;
		check(top->calls == (delivered ? 1 : 0) && lower->calls == 0,
			"only the enabled nonautomatic top behavior receives a target callback");
		check(!delivered || (top->received == requested.GetPtr() && top->strong == strong),
			"target dispatch retains identity and requested strength");
		check(owner->GetCurrentTarget() == previous.GetPtr() && owner->IsCurrentTargetStrong(),
			"unit target dispatch never bypasses behavior with direct assignment");
		owner->OnTarget(CPtr<PFBaseUnit>(), strong);
		check(top->calls == (delivered ? 2 : 0) && (!delivered || !IsValid(top->received)),
			"target callback argument validation remains behavior-owned");
		check(owner->GetCurrentTarget() == previous.GetPtr(), "null request cannot silently drop current target");
	}
	CObj<PFBaseUnit> bare = Unit(world, NDb::FACTION_FREEZE, 20);
	CObj<PFBaseUnit> target = Unit(world, NDb::FACTION_BURN, 20);
	bare->OnTarget(target.GetPtr(), true);
	check(!bare->HasBehaviour() && !IsValid(bare->GetCurrentTarget()),
		"absent behavior does not create an implicit targeting fallback");
}

/** Real hero unit hooks reach production behavior, queue and damage-retaliation paths. */
static void BehaviourHeroDispatchB8(Checks& check, PFWorld* world)
{
	B8BehaviourReveal reveal(world, NDb::FACTION_FREEZE);
	for (bool enabled : {false, true})
	for (bool automaticOnly : {false, true})
	for (bool strong : {false, true})
	{
		CObj<B8BehaviourHero> hero = B8BehaviourHeroUnit(world, NDb::FACTION_FREEZE);
		CObj<B8BehaviourCreep> target = B8BehaviourMoving(world, NDb::FACTION_BURN);
		NDb::UnitTargetingParameters* params = new NDb::UnitTargetingParameters;
		params->onlyAutoTargeting = automaticOnly;
		hero->OverrideTargetingParams(params);
		CObj<PFHeroBehaviour> behavior = new PFHeroBehaviour(hero.GetPtr());
		hero->AddBehaviourOnTop(behavior.GetPtr());
		if (!enabled) behavior->Disable();
		const bool admitted = enabled && !automaticOnly;
		hero->OnTarget(target.GetPtr(), strong);
		check(hero->IsIdle() && hero->HasQueuedStates() == admitted,
			"hero unit target hook admits through enabled behavior into the real queue");
		check(!IsValid(hero->GetCurrentTarget()), "target admission does not assign before state entry");
		hero->AddFlag(NDb::UNITFLAG_FORBIDATTACK);
		hero->AdvanceOrders(0);
		const PFBaseUnitAttackState* attack = B8BehaviourAttack(*hero);
		check((attack != nullptr) == admitted && !hero->HasQueuedStates(),
			"dispatched hero order enters the existing attack state");
		check(!admitted || (attack && attack->GetTarget() == target.GetPtr()
			&& hero->IsCurrentTargetStrong() == strong), "dispatched hero attack retains target strength");
		hero->DropStates(); hero->DropTarget(); hero->RemoveFlag(NDb::UNITFLAG_FORBIDATTACK);
		PFBaseUnitDamageDesc damage = DirectDamage();
		damage.pSender = target.GetPtr(); damage.amount = 1;
		damage.damageType = NDb::APPLICATORDAMAGETYPE_PURE; damage.dontAttackBack = false;
		check(hero->OnDamage(damage) == 1 && hero->GetHealth() == 999,
			"hero actual damage notification preserves the health delta");
		attack = B8BehaviourAttack(*hero);
		check((attack != nullptr) == admitted && (!admitted || attack->GetTarget() == target.GetPtr()),
			"hero actual damage reaches enabled behavior and production strong retaliation");
		hero->DropStates();
	}
}

/** Ordinary hero construction installs the behavior used by production target hooks. */
static void BehaviourHeroConstructionB8(Checks& check, PFWorld* world)
{
	B8BehaviourReveal reveal(world, NDb::FACTION_FREEZE);
	CObj<B8BehaviourHero> hero = B8BehaviourHeroUnit(world, NDb::FACTION_FREEZE);
	CObj<B8BehaviourCreep> target = B8BehaviourMoving(world, NDb::FACTION_BURN);
	check(dynamic_cast<const PFHeroBehaviour*>(hero->GetBehaviour()) != nullptr,
		"ordinary hero constructor installs its production behavior");
	hero->OnTarget(target.GetPtr(), true);
	check(hero->IsIdle() && hero->HasQueuedStates() && !IsValid(hero->GetCurrentTarget()),
		"fresh hero target hook queues an order without fixture behavior injection");
	hero->AddFlag(NDb::UNITFLAG_FORBIDATTACK); hero->AdvanceOrders(0);
	const PFBaseUnitAttackState* attack = B8BehaviourAttack(*hero);
	check(attack && attack->GetTarget() == target.GetPtr() && hero->IsCurrentTargetStrong(),
		"fresh hero behavior enters the actual strong attack order");
	hero->DropStates(); hero->DropTarget(); hero->RemoveFlag(NDb::UNITFLAG_FORBIDATTACK);
	PFBaseUnitDamageDesc damage = DirectDamage(); damage.pSender = target.GetPtr();
	damage.amount = 1; damage.damageType = NDb::APPLICATORDAMAGETYPE_PURE; damage.dontAttackBack = false;
	check(hero->OnDamage(damage) == 1 && B8BehaviourAttack(*hero),
		"fresh hero damage hook retaliates without fixture behavior injection");
	hero->DropStates();
	hero->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
	check(hero->IsDead() && !hero->HasBehaviour(), "final hero death removes its behavior");
	hero->Resurrect();
	check(!hero->IsDead() && dynamic_cast<const PFHeroBehaviour*>(hero->GetBehaviour()),
		"hero resurrection reinstalls a missing production behavior");
	hero->OnTarget(target.GetPtr(), true);
	check(hero->HasQueuedStates(), "resurrected hero receives target orders again");
	hero->DropStates();
	const PFBaseBehaviour* before = hero->GetBehaviour();
	hero->Resurrect();
	check(hero->GetBehaviour() == before, "resurrection does not duplicate an existing behavior");
	CObj<B8BehaviourHero> clone = B8BehaviourHeroUnit(world, NDb::FACTION_FREEZE, true);
	check(clone->IsClone() && !clone->HasBehaviour(), "clone construction retains separate behavior ownership");
}
