/** Attack-move owns alert search, movement and cleanup through the production HFSM. */
static void CombatMoveLifecycle(Checks& checks)
{
	Fixture fixture(checks); if (!fixture.loaded) return;
	for (int policy = 0; policy < 4; ++policy)
	{
		NDb::Unit* raw = new NDb::Unit; Configure(*raw);
		AddStat(*const_cast<NDb::StatsContainer*>(raw->stats.GetPtr()), NDb::STAT_RANGE, "4");
		NDb::Ptr<NDb::Unit> db = raw;
		CObj<ProbeMover> owner = new ProbeMover(fixture.world, *db, CVec3(20, 20, 0));
		CObj<ProbeMover> target = new ProbeMover(fixture.world, *db, CVec3(24, 20, 0));
		target->ChangeFaction(Enemy); target->SetObjectSize(2); fixture.Tick();
		owner->RemoveFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		owner->SetMoveFlags(MOVE_FLAG_NO_COLLIDE); owner->SetUnitSpeed(4);
		if (policy == 1) owner->AddFlag(NDb::UNITFLAG_INVISIBLE);
		if (policy == 2) owner->AddFlag(NDb::UNITFLAG_FORBIDMOVE);
		if (policy == 3) owner->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		owner->EnqueueState(new PFBaseUnitCombatMoveState(fixture.world, owner.GetPtr(), CVec2(40, 20), 1), true);
		for (int i = 0; i < 3; ++i) owner->Step(0);
		if (policy == 0)
		{
			checks.Check(owner->GetCurrentTarget() == target.GetPtr() && !owner->IsMoving(), "alert move acquires a visible enemy before plain movement");
			target->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
			for (int i = 0; i < 4; ++i) owner->Step(0);
			checks.Check(!owner->IsIdle() && owner->IsMoving() && !owner->GetCurrentTarget(),
				"creep alert move resumes destination after enemy loss");
		}
		else if (policy == 2)
			checks.Check(owner->IsIdle() && !owner->IsMoving(), "forbidden attack-move ends without displacement");
		else
			checks.Check(!owner->GetCurrentTarget() && owner->IsMoving(), "invisible/selection-forbidden alert movement does not auto-acquire");
		owner->Cleanup();
		checks.Check(!owner->GetCurrentTarget() && !owner->IsMoving(), "combat movement cleanup drops nested combat and motion");
		owner->Stop(false);
	}
	NDb::Unit* raw = new NDb::Unit; Configure(*raw); NDb::Ptr<NDb::Unit> db = raw;
	CObj<ProbeMover> owner = new ProbeMover(fixture.world, *db, CVec3(20, 20, 0));
	owner->SetMoveFlags(MOVE_FLAG_NO_COLLIDE); owner->SetUnitSpeed(4);
	owner->EnqueueState(new PFBaseUnitCombatMoveState(fixture.world, owner.GetPtr(), CVec2(24, 20), 1), true);
	for (int i = 0; i < 100 && (!owner->IsIdle() || owner->HasQueuedStates()); ++i) owner->Step(0.1f);
	checks.Check(owner->IsIdle() && !owner->IsMoving() && owner->IsPositionInRange(CVec2(24, 20), 1),
		"unopposed alert move reaches its requested destination");
	NDb::Hero* heroRaw = new NDb::Hero; Configure(*heroRaw);
	AddStat(*const_cast<NDb::StatsContainer*>(heroRaw->stats.GetPtr()), NDb::STAT_RANGE, "4");
	NDb::Ptr<NDb::Hero> heroDb = heroRaw;
	PFBaseHero::SpawnInfo spawn; spawn.pHero = heroDb;
	spawn.placement = Placement(CVec3(20, 30, 0), QNULL, CVec3(1, 1, 1));
	CObj<PFBaseHero> hero = new PFBaseHero(fixture.world, spawn, NDb::UNITTYPE_HEROMALE, Ally, Ally);
	CObj<ProbeMover> enemy = new ProbeMover(fixture.world, *db, CVec3(24, 30, 0));
	enemy->ChangeFaction(Enemy); enemy->SetObjectSize(2); fixture.Tick();
	hero->SetMoveFlags(MOVE_FLAG_NO_COLLIDE); hero->SetUnitSpeed(4);
	hero->EnqueueState(new PFBaseUnitCombatMoveState(fixture.world, hero.GetPtr(), CVec2(60, 30), 1), true);
	for (int i = 0; i < 3; ++i) hero->Step(0);
	checks.Check(hero->GetCurrentTarget() == enemy.GetPtr(), "hero alert move also acquires its enemy");
	enemy->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
	for (int i = 0; i < 4; ++i) hero->Step(0);
	checks.Check(hero->IsIdle() && !hero->IsMoving(), "hero alert move ends after its combat state finishes");
}

/** Building combat may briefly engage nearby enemies but never chase away from its order. */
static void BuildingCombatLifecycle(Checks& checks)
{
	Fixture fixture(checks); if (!fixture.loaded) return;
	for (int policy = 0; policy < 3; ++policy)
	{
		NDb::Unit* raw = new NDb::Unit; Configure(*raw);
		AddStat(*const_cast<NDb::StatsContainer*>(raw->stats.GetPtr()), NDb::STAT_RANGE, "4");
		NDb::Ptr<NDb::Unit> db = raw;
		CObj<ProbeMover> owner = new ProbeMover(fixture.world, *db, CVec3(20, 20, 0));
		CObj<ProbeMover> enemy = new ProbeMover(fixture.world, *db, CVec3(24, 20, 0));
		CObj<ProbeUnit> building = new ProbeUnit(fixture.world, db);
		building->Relocate(CVec3(40, 20, 0)); building->ChangeFaction(Enemy);
		enemy->ChangeFaction(Enemy); enemy->SetObjectSize(2); fixture.Tick();
		owner->RemoveFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		owner->SetMoveFlags(MOVE_FLAG_NO_COLLIDE); owner->SetUnitSpeed(4);
		CObj<B9Region> region = new B9Region;
		if (policy == 1) owner->AddFlag(NDb::UNITFLAG_INVISIBLE);
		if (policy == 2) building->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
		owner->EnqueueState(new PFBaseUnitCombatMoveToBuildingState(fixture.world, owner.GetPtr(), building.GetPtr(), region, false), true);
		for (int i = 0; i < 3; ++i) owner->Step(0);
		if (policy == 0)
		{
			checks.Check(owner->GetCurrentTarget() == enemy.GetPtr() && !owner->IsMoving(), "building move engages nearby enemy through a nested attack");
			enemy->TeleportTo(CVec2(30, 20));
			for (int i = 0; i < 4; ++i) owner->Step(0);
			checks.Check(!owner->GetCurrentTarget() && owner->IsMoving(), "distant enemy cannot turn building interruption into chase");
			enemy->TeleportTo(CVec2(24, 20));
			owner->Step(0); owner->Step(0);
			checks.Check(!owner->GetCurrentTarget(), "building search observes its cooldown between interruptions");
			owner->Step(0.21f); owner->Step(0); owner->Step(0);
			checks.Check(owner->GetCurrentTarget() == enemy.GetPtr(), "elapsed search cooldown permits a new interruption");
		}
		else if (policy == 1)
			checks.Check(!owner->GetCurrentTarget() && owner->IsMoving(), "invisible building mover skips automatic selection");
		else
			checks.Check(owner->IsIdle() && !owner->IsMoving(), "destroyed building terminates combat approach");
		owner->Cleanup();
		checks.Check(!owner->GetCurrentTarget() && !owner->IsMoving(), "building combat cleanup releases nested attack and motion");
		owner->Stop(false);
	}
}
