/** Expose only original clone/isolation metadata; all combat and state transitions stay engine-owned. */
struct IdleHeroB13 : B9CastHero
{
	IdleHeroB13(PFWorld* world, const SpawnInfo& spawn) : B9CastHero(world, spawn, NDb::FACTION_FREEZE) {}
	void Isolate(bool value) { isolated = value; }
};

/** Build an ordinary hero with bounded literal attack and original targeting/suspend parameters. */
static CObj<IdleHeroB13> MakeIdleHeroB13(PFWorld* world, bool clone = false)
{
	auto seed = B9MaleHero(world, NDb::FACTION_FREEZE);
	PFBaseHero::SpawnInfo spawn; spawn.pHero = dynamic_cast<const NDb::Hero*>(seed->GetDbHero());
	spawn.isClone = clone;
	spawn.placement = Placement(CVec3(10, 5, 0), QNULL, CVec3(1, 1, 1));
	CObj<IdleHeroB13> hero = new IdleHeroB13(world, spawn);
	NDb::UnitTargetingParameters* params = new NDb::UnitTargetingParameters;
	params->targetingRange = params->chaseRange = 20;
	params->sortSuspendTimeout = .5f; params->longSuspendTimeout = 2;
	hero->OverrideTargetingParams(params);
	hero->SetObjectSize(1);
	hero->GetStat(NDb::STAT_RANGE)->SetCoreValue(3);
	auto* db = B11AttackDescriptor(0); db->useRange.sString = "3";
	hero->ReplaceBaseAttack(new PFBaseAttackData(hero.GetPtr(), db), false);
	return hero;
}

/** Idle auto-attacks must use original hold/suspend ownership, not the native direct-attack fallback. */
static void HeroIdleB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (bool heroKind : {false, true})
	{
		RoutingFixtureB13 f(check); if (!f.world) return;
		CObj<PFBaseUnit> owner = heroKind ? CObj<PFBaseUnit>(MakeIdleHeroB13(f.world).GetPtr())
			: Unit(f.world, NDb::FACTION_FREEZE, 20);
		auto target = Unit(f.world, NDb::FACTION_BURN, 20);
		owner->AssignTarget(target.GetPtr(), true);
		check(owner->GetCurrentTarget() == target.GetPtr(), "B13 idle presentation fixture assigns a real target");
		owner->Idle();
		check(owner->GetCurrentTarget() == target.GetPtr() && !owner->GetCurrentState(),
			"B13 Idle presentation cannot drop a target or create a gameplay suspension");
		owner->ForceIdle();
		check(owner->GetCurrentTarget() == target.GetPtr() && !owner->GetCurrentState(),
			"B13 ForceIdle presentation preserves target and FSM ownership");
	}
	for (int mode = 0; mode < 6; ++mode)
	{
		RoutingFixtureB13 f(check); if (!f.world) return;
		auto hero = MakeIdleHeroB13(f.world, mode == 3);
		if (mode == 1) hero->AddFlag(NDb::UNITFLAG_FORBIDATTACK);
		if (mode == 2) hero->AddFlag(NDb::UNITFLAG_FORBIDAUTOATTACK);
		if (mode == 4) hero->Isolate(true);
		if (mode == 5) { hero->SetForbidRespawn(true); hero->KillUnit(); }
		hero->Simulate(0);
		const bool held = dynamic_cast<const PFHeroHoldState*>(hero->GetCurrentState()) != nullptr;
		check(held == (mode == 0) && hero->IsHoldingPosition() == (mode == 0),
			"B13 idle transition honors attack, autoattack, clone, isolation and death policy");
		if (mode == 0) check(hero->IsIdle(), "B13 original hero hold counts as idle for behavior and order admission");
		const IPFState* state = hero->GetCurrentState();
		hero->Simulate(0);
		check(hero->GetCurrentState() == state, "B13 idle polling does not stack duplicate hold states");
		hero->Cleanup();
	}
	{
		RoutingFixtureB13 f(check); if (!f.world) return;
		B8BehaviourReveal reveal(f.world, NDb::FACTION_FREEZE);
		auto hero = MakeIdleHeroB13(f.world);
		auto enemy = B8BehaviourMoving(f.world, NDb::FACTION_BURN);
		enemy->TeleportTo(CVec2(17, 5)); enemy->SetObjectSize(1);
		enemy->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); enemy->AddFlag(NDb::UNITFLAG_FORBIDATTACK);
		const float health = enemy->GetHealth();
		const CVec2 origin = hero->GetPos();
		for (int tick = 0; tick < 5; ++tick) f.Tick();
		check(hero->IsHoldingPosition() && hero->GetPos() == origin && enemy->GetHealth() == health,
			"B13 idle hero holds position instead of chasing a nearby out-of-attack-range enemy");
		enemy->TeleportTo(CVec2(12, 5));
		for (int tick = 0; tick < 10 && enemy->GetHealth() == health; ++tick) f.Tick();
		check(hero->IsHoldingPosition() && hero->GetPos() == origin && enemy->GetHealth() == health - 25,
			"B13 world-stepped idle hold delivers the real authored attack without a player attack command");
		hero->Move(CVec2(10, 15));
		for (int tick = 0; tick < 8 && hero->IsHoldingPosition(); ++tick) f.Tick();
		check(!hero->IsHoldingPosition() && dynamic_cast<const PFBaseUnitMoveToState*>(hero->GetCurrentState()),
			"B13 a new player move leaves idle hold through the original owner FSM");
		hero->Cleanup();
	}
	{
		RoutingFixtureB13 f(check); if (!f.world) return;
		B8BehaviourReveal reveal(f.world, NDb::FACTION_FREEZE);
		auto hero = MakeIdleHeroB13(f.world);
		auto enemy = B8BehaviourMoving(f.world, NDb::FACTION_BURN);
		enemy->TeleportTo(CVec2(17, 5));
		hero->Simulate(0);
		if (!hero->GetBehaviour()) hero->AddBehaviourOnTop(new PFHeroBehaviour(hero.GetPtr()));
		PFBaseUnitDamageDesc damage;
		damage.pSender = enemy.GetPtr(); damage.amount = 1; damage.dontAttackBack = false;
		hero->GetBehaviour()->OnDamage(damage);
		const auto* attack = dynamic_cast<const PFBaseUnitAttackState*>(hero->GetCurrentState());
		check(attack && attack->GetTarget() == enemy.GetPtr() && !hero->IsIdle() && hero->IsHoldingPosition(),
			"B13 original damage retaliation stacks the exact attacker above passive hold");
		hero->Cleanup();
	}
	for (bool isolated : {false, true})
	{
		RoutingFixtureB13 f(check); if (!f.world) return;
		auto hero = MakeIdleHeroB13(f.world);
		auto target = B8BehaviourMoving(f.world, NDb::FACTION_BURN);
		target->TeleportTo(CVec2(12, 5));
		if (isolated) hero->Isolate(true); else hero->AddFlag(NDb::UNITFLAG_FORBIDAUTOATTACK);
		hero->AssignTarget(target.GetPtr(), true);
		const float health = target->GetHealth();
		hero->Simulate(.1f); hero->Simulate(.1f);
		check(!hero->GetCurrentState() && target->GetHealth() == health,
			"B13 generic direct-attack fallback cannot bypass hero idle admission policy");
	}
	for (bool stopped : {false, true})
	{
		RoutingFixtureB13 f(check); if (!f.world) return;
		auto hero = MakeIdleHeroB13(f.world);
		if (stopped) hero->Stop(false);
		else
		{
			auto target = Unit(f.world, NDb::FACTION_BURN, 20);
			hero->AssignTarget(target.GetPtr(), true); hero->DropTarget();
		}
		check(dynamic_cast<const PFHeroSuspendState*>(hero->GetCurrentState()) != nullptr,
			"B13 stop and target loss install the original long/short suspend state");
		hero->Simulate(0);
		const float half = stopped ? 1.f : .25f;
		hero->Simulate(half);
		check(dynamic_cast<const PFHeroSuspendState*>(hero->GetCurrentState()) != nullptr,
			"B13 idle autoattack cannot bypass the authored reacquisition delay");
		hero->Simulate(half);
		check(!hero->GetCurrentState(), "B13 original suspend expires exactly at its authored duration");
		hero->Simulate(0);
		check(dynamic_cast<const PFHeroHoldState*>(hero->GetCurrentState()) && hero->IsHoldingPosition(),
			"B13 expired suspend returns to idle hold on the next hero tick");
		hero->Cleanup();
	}
	{
		RoutingFixtureB13 f(check); if (!f.world) return;
		auto hero = MakeIdleHeroB13(f.world);
		hero->Move(CVec2(10, 15)); hero->Simulate(0);
		check(dynamic_cast<const PFBaseUnitMoveToState*>(hero->GetCurrentState()) && !hero->IsHoldingPosition(),
			"B13 idle transition cannot supersede a queued player order");
		const auto* move = hero->GetCurrentState(); hero->Stop(false);
		check(hero->GetCurrentState() == move, "B13 stopping active movement does not push an idle-only suspend");
		hero->Cleanup();
	}
	{
		RoutingFixtureB13 f(check); if (!f.world) return;
		B8BehaviourReveal reveal(f.world, NDb::FACTION_FREEZE);
		auto hero = MakeIdleHeroB13(f.world);
		auto enemy = B8BehaviourMoving(f.world, NDb::FACTION_BURN);
		enemy->TeleportTo(CVec2(12, 5)); enemy->SetObjectSize(1);
		enemy->AddFlag(NDb::UNITFLAG_FORBIDATTACK);
		f.world->GameFinish(NDb::FACTION_BURN);
		const float health = enemy->GetHealth();
		for (int tick = 0; tick < 30; ++tick) f.Tick();
		check(!hero->IsHoldingPosition() && enemy->GetHealth() == health,
			"B13 restored idle transition cannot restart autonomous combat after match completion");
		hero->Cleanup();
	}
	for (bool clone : {false, true})
	{
		RoutingFixtureB13 f(check); if (!f.world) return;
		auto hero = MakeIdleHeroB13(f.world, clone);
		if (!clone) { hero->SetForbidRespawn(true); hero->KillUnit(); }
		hero->OnStopped();
		check(!hero->GetCurrentState(), "B13 dead and clone heroes cannot start a stop suspension");
		hero->OnTargetDropped();
		check(!hero->GetCurrentState(), "B13 dead and clone heroes cannot start a target-loss suspension");
	}
	for (bool longSuspend : {false, true})
	{
		// Orphan diagnostics have no AI fallback; absence of authored timing means no pause.
		auto seed = B9MaleHero(nullptr, NDb::FACTION_FREEZE);
		NDb::Hero* raw = new NDb::Hero; raw->stats = seed->GetDbHero()->stats;
		PFBaseHero::SpawnInfo spawn; spawn.pHero = raw;
		spawn.placement = Placement(CVec3(5, 5, 0), QNULL, CVec3(1, 1, 1));
		CObj<IdleHeroB13> hero = new IdleHeroB13(nullptr, spawn);
		check(!hero->GetTargetingParamsPtr(), "B13 orphan suspension fixture has no targeting defaults");
		CObj<IPFState> state = new PFHeroSuspendState(hero.GetPtr(), longSuspend);
		check(state->DoStep(0), "B13 missing targeting data retires suspension without inventing a delay");
		state->DoLeave();
	}
	std::printf("Hero idle B13: checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
