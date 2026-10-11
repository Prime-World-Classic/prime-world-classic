/** Hold uses the movement engine flag and owner stack, not a parallel attack model. */
static void HeroHoldB9(Checks& check, PFWorld* world)
{
	for (bool stopOnEnter : {false, true})
	{
		CObj<B8BehaviourHero> hero = B8BehaviourHeroUnit(world, NDb::FACTION_FREEZE);
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		hero->MoveTo(CVec2(12, 5));
		CObj<IPFState> hold = new PFHeroHoldState(world, hero.GetPtr(), stopOnEnter);
		check(hold->DoStep(0) == !stopOnEnter && hero->IsHoldingPosition(),
			"hold marks position and respects the stop-on-enter option");
		check(hero->IsMoving() == !stopOnEnter, "hold stop-on-enter owns ordinary movement only");
		hold->DoLeave(true);
		check(!hero->IsHoldingPosition(), "hold cleanup clears the engine hold flag");
		hero->Stop();
	}
	for (unsigned forbidden : {0U, (unsigned)NDb::UNITFLAG_FORBIDATTACK,
		(unsigned)NDb::UNITFLAG_FORBIDAUTOATTACK})
	{
		B8BehaviourForeignWorld isolated(check);
		if (!isolated.world) continue;
		B8BehaviourReveal reveal(isolated.world, NDb::FACTION_FREEZE);
		CObj<B8BehaviourHero> hero = B8BehaviourHeroUnit(isolated.world, NDb::FACTION_FREEZE);
		CObj<B8BehaviourCreep> enemy = B8BehaviourMoving(isolated.world, NDb::FACTION_BURN);
		hero->GetStat(NDb::STAT_RANGE)->SetCoreValue(5.f);
		if (forbidden) hero->AddFlag(forbidden);
		CObj<IPFState> hold = new PFHeroHoldState(isolated.world, hero.GetPtr());
		check(!hold->DoStep(0), "stationary hold remains active");
		const PFBaseUnitAttackState* attack = B8BehaviourAttack(*hero);
		check((attack != nullptr) == (forbidden == 0),
			"hold acquires an in-range attack only when both attack policies permit it");
		check(!attack || (IsTargetValid(attack->GetTarget()) &&
			attack->GetTarget()->GetWorld() == isolated.world &&
			attack->GetTarget()->GetFaction() != hero->GetFaction() &&
			hero->CanSelectTarget(attack->GetTarget(), true) &&
			hero->IsTargetInRange(attack->GetTarget(), hero->GetAttackRange())),
			"hold attack selects a real visible in-range enemy from the isolated world");
		hold->DoLeave(true); hero->DropStates();
	}
	CObj<B8BehaviourHero> hero = B8BehaviourHeroUnit(world, NDb::FACTION_FREEZE);
	CObj<B8BehaviourCreep> mount = B8BehaviourMoving(world, NDb::FACTION_FREEZE);
	mount->AttachUnit(hero.GetPtr());
	CObj<IPFState> mounted = new PFHeroHoldState(world, hero.GetPtr());
	check(mounted->DoStep(0) && !hero->IsHoldingPosition() && hero->IsMounted(),
		"mounted hero cannot enter hold or lose its mounted movement state");
	mount->DetachUnit();
}
