/** Observe the real nested FSM without replacing the production follow policy. */
struct B9FollowInspect : PFHeroFollowUnitState
{
	B9FollowInspect(PFBaseHero* hero, PFBaseUnit* leader, float range, float forceRange)
		: PFHeroFollowUnitState(hero, leader, range, forceRange) {}
	const IPFState* Child() const { return GetCurrentState(); }
};

/** Following shares a leader's strong enemy; forced distance takes priority over combat. */
static void HeroFollowCombatB9(Checks& check, PFWorld* world)
{
	B8BehaviourReveal reveal(world, NDb::FACTION_FREEZE);
	for (bool selectForbidden : {false, true})
	{
		CObj<B8BehaviourHero> hero = B8BehaviourHeroUnit(world, NDb::FACTION_FREEZE);
		CObj<B8BehaviourCreep> leader = B8BehaviourMoving(world, NDb::FACTION_FREEZE);
		CObj<B8BehaviourCreep> enemy = B8BehaviourMoving(world, NDb::FACTION_BURN);
		hero->AddFlag(NDb::UNITFLAG_FORBIDATTACK);
		if (selectForbidden) hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		leader->AssignTarget(enemy.GetPtr(), true);
		CObj<B9FollowInspect> follow = new B9FollowInspect(hero.GetPtr(), leader.GetPtr(), 2, 4);
		CPtr<IPFState> order = follow.GetPtr();
		check(!order->DoStep(0), "follow remains active while sharing a leader target");
		const PFBaseUnitAttackState* attack = dynamic_cast<const PFBaseUnitAttackState*>(follow->Child());
		check((attack != nullptr) == !selectForbidden,
			"leader enemy enters an existing nested attack unless target selection is forbidden");
		check(!attack || attack->GetTarget() == enemy.GetPtr(), "shared-target attack retains the actual enemy");
		if (!selectForbidden)
		{
			order->DoStep(0);
			check(hero->GetCurrentTarget() == enemy.GetPtr() && hero->IsCurrentTargetStrong(),
				"leader target is strong after production attack entry");
			leader->TeleportTo(CVec2(20, 5), true, false);
			order->DoStep(0);
			check(!follow->Child() && hero->IsMoving() && !IsValid(hero->GetCurrentTarget()),
				"forced follow distance clears owned attack before resuming movement");
		}
		order->DoLeave(true);
		check(!follow->Child(), "follow cleanup releases its nested combat state");
	}
	CObj<B8BehaviourHero> hero = B8BehaviourHeroUnit(world, NDb::FACTION_FREEZE);
	CObj<B8BehaviourCreep> leader = B8BehaviourMoving(world, NDb::FACTION_FREEZE);
	CObj<B8BehaviourCreep> ally = B8BehaviourMoving(world, NDb::FACTION_FREEZE);
	leader->AssignTarget(ally.GetPtr(), true);
	hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
	CObj<B9FollowInspect> follow = new B9FollowInspect(hero.GetPtr(), leader.GetPtr(), 2, 0);
	CPtr<IPFState> order = follow.GetPtr();
	check(!order->DoStep(0) && !follow->Child(), "allied leader target never becomes an attack child");
	order->DoLeave(true);
	{
		B8BehaviourForeignWorld isolated(check);
		if (!isolated.world) return;
		B8BehaviourReveal reveal(isolated.world, NDb::FACTION_FREEZE);
		CObj<B8BehaviourHero> follower = B8BehaviourHeroUnit(isolated.world, NDb::FACTION_FREEZE);
		CObj<B8BehaviourCreep> idleLeader = B8BehaviourMoving(isolated.world, NDb::FACTION_FREEZE);
		CObj<B8BehaviourCreep> candidate = B8BehaviourMoving(isolated.world, NDb::FACTION_BURN);
		follower->AddFlag(NDb::UNITFLAG_FORBIDATTACK);
		CObj<B9FollowInspect> weak = new B9FollowInspect(follower.GetPtr(), idleLeader.GetPtr(), 2, 0);
		CPtr<IPFState> escort = weak.GetPtr(); escort->DoStep(0);
		CObj<IPFState> original = const_cast<IPFState*>(weak->Child());
		check(original && dynamic_cast<const PFBaseUnitAttackState*>(original.GetPtr()),
			"idle leader permits the existing engine to select a weak attack target");
		escort->DoStep(0);
		check(original && weak->Child() == original.GetPtr() && IsValid(follower->GetCurrentTarget()) &&
			!follower->IsCurrentTargetStrong(), "unchanged weak follow target retains one attack state across steps");
		escort->DoLeave(true);
	}
}
