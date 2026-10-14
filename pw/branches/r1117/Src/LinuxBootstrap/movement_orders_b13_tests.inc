/** MOVE callbacks exercise the original event dispatch and real movement cancellation paths. */
struct MovementOrderEventsB13 : CObjectBase, PFBaseUnitEventListener
{
	OBJECT_BASIC_METHODS(MovementOrderEventsB13);
public:
	PFBaseMovingUnit* mover=nullptr;
	int moves=0, action=0;
	CVec2 redirected=CVec2(30.5f,20.5f);
	unsigned int OnEvent(const PFBaseUnitEvent* event) override
	{
		if (event->GetType()!=NDb::BASEUNITEVENT_MOVE) return 0;
		++moves;
		if (moves!=1 || !mover) return 0;
		if (action==1) mover->Stop(false);
		if (action==2) mover->MoveTo(redirected,0);
		if (action==3) mover->GetWorld()->UnsubscribeObject(mover);
		if (action==4) mover->AddFlag(NDb::UNITFLAG_FORBIDMOVE);
		if (action==5) mover->KillUnit();
		if (action==6) mover->Step(.1f);
		if (action==7) mover->GetStat(NDb::STAT_MOVESPEED)->SetCoreValue(1);
		return 0;
	}
};

/** Deferred native route admission must report original start/failure semantics to point orders. */
static void MovementOrdersB13(Checks& check)
{
	const int before=check.count, failed=check.failed;
	{
		RoutingFixtureB13 f(check); if (!f.world) return;
		auto hero=B9MaleHero(f.world,NDb::FACTION_FREEZE);
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		CObj<MovementOrderEventsB13> events=new MovementOrderEventsB13;
		hero->AddEventListener(events.GetPtr());
		hero->Move(CVec2(30,5)); f.Tick(); f.Tick();
		check(events->moves==1,"B13 ordinary hero path emits its original start event");
		for (int tick=0;tick<5;++tick) f.Tick();
		check(events->moves==1,"B13 continued movement does not duplicate the start event");
		hero->Move(CVec2(30,5)); f.Tick(); f.Tick();
		check(events->moves==1,"B13 identical active destination does not restart movement notification");
		hero->Move(CVec2(30,20)); f.Tick(); f.Tick();
		check(events->moves==2,"B13 redirected hero path emits a new start event");
		hero->Cleanup();
	}
	for (const CVec2 start : {CVec2(65.5f,60.5f),CVec2(20.5f,60.5f)})
	{
		RoutingFixtureB13 f(check,20,12,10,true); if (!f.world) return;
		auto hero=B9MaleHero(f.world,NDb::FACTION_FREEZE);
		hero->TeleportTo(start); hero->SetObjectSize(1);
		hero->GetStat(NDb::STAT_MOVESPEED)->SetCoreValue(50);
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		CObj<MovementOrderEventsB13> events=new MovementOrderEventsB13;
		hero->AddEventListener(events.GetPtr());
		const CVec2 blocked(72.5f,60.5f);
		hero->Move(blocked); f.Tick();
		for (int tick=0;tick<200 && (hero->IsMoving() || hero->HasQueuedStates() || hero->GetCurrentState());++tick) f.Tick();
		check(!hero->IsMoving() && !hero->HasQueuedStates() && !hero->GetCurrentState(),
			"B13 unreachable point order retires instead of reissuing the failed route forever");
		check(fabs(hero->GetPos()-blocked)>1 && events->moves==1,
			"B13 unreachable order preserves collision and emits one original movement event");
		hero->Move(CVec2(30.5f,20.5f)); f.Tick();
		for (int tick=0;tick<200 && (hero->IsMoving() || hero->GetCurrentState());++tick) f.Tick();
		check(!hero->IsMoving() && !hero->GetCurrentState() && fabs(hero->GetPos()-CVec2(30.5f,20.5f))<=1.001f,
			"B13 a fresh reachable order is not poisoned by the previous route failure");
		hero->Cleanup();
	}
	{
		RoutingFixtureB13 f(check); if (!f.world) return;
		auto mover=f.Mover(CVec2(20.5f,20.5f)), blocker=f.Mover(CVec2(24.5f,20.5f));
		CObj<MovementOrderEventsB13> events=new MovementOrderEventsB13; mover->AddEventListener(events.GetPtr());
		mover->MoveTo(CVec2(30.5f,20.5f),0);
		for (int tick=0;tick<10;++tick) f.Tick();
		check(events->moves==1 && mover->IsMoving() && mover->GetPos().x<24,
			"B13 temporary dynamic blockage retains one admitted order and movement event");
		blocker->TeleportTo(CVec2(24.5f,25.5f));
		for (int tick=0;tick<20 && mover->IsMoving();++tick) f.Tick();
		check(!mover->IsMoving() && mover->GetPos()==CVec2(30.5f,20.5f) && events->moves==1,
			"B13 movement resumes when a dynamic blocker clears without another player command");
	}
	for (int action=1;action<=7;++action)
	{
		RoutingFixtureB13 f(check); if (!f.world) return;
		auto mover=f.Mover(CVec2(20.5f,20.5f));
		if (action==7) mover->SetMoveFlags(0);
		CObj<MovementOrderEventsB13> events=new MovementOrderEventsB13;
		events->mover=mover.GetPtr(); events->action=action; mover->AddEventListener(events.GetPtr());
		const CVec2 original=mover->GetPos();
		mover->MoveTo(CVec2(20.5f,35.5f),0); f.Tick();
		check(events->moves==1 && mover->GetPos()==original,
			"B13 start callback cancels stale displacement before stop, redirection or removal");
		if (action==1) check(!mover->IsMoving(),"B13 callback stop remains effective after notification");
		if (action==2)
		{
			f.Tick();
			check(events->moves==2 && mover->GetPos().x>original.x && mover->GetPos().y==original.y,
				"B13 callback redirection begins on a later tick with its own event");
		}
		if (action==3) check(!f.world->IsSubscribed(mover.GetPtr()),"B13 callback removal remains unsubscribed");
		if (action==4)
		{
			f.Tick();
			check(mover->GetPos()==original && mover->IsMoving(),"B13 rooted ordinary movement retains its order without translation");
			mover->RemoveFlag(NDb::UNITFLAG_FORBIDMOVE); f.Tick();
			check(mover->GetPos().y>original.y && events->moves==1,"B13 removing a root resumes the same admitted route");
		}
		if (action==5) check(mover->IsDead() && !mover->IsMoving(),"B13 callback death cannot resume movement");
		if (action==6)
		{
			f.Tick();
			check(mover->GetPos().y>original.y && events->moves==1,"B13 recursive callback cannot spend distance twice or repeat the event");
		}
		if (action==7)
		{
			f.Tick();
			check(mover->GetPos().y>original.y && mover->GetPos().y-original.y<.02f && events->moves==1,
				"B13 callback speed changes apply before any translated distance");
		}
		mover->RemoveEventListener(events.GetPtr()); mover->Stop(false);
	}
	std::printf("Movement orders B13: checks=%d failed=%d\n",check.count-before,check.failed-failed);
}
