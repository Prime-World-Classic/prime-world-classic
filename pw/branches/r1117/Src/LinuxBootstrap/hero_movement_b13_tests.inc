/** Observe the original movement notification, without implementing any movement policy. */
struct HeroMoveEventsB13 : CObjectBase, PFBaseUnitEventListener
{
	OBJECT_BASIC_METHODS(HeroMoveEventsB13);
public:
	int moves = 0;
	unsigned int OnEvent(const PFBaseUnitEvent* event) override
	{
		if (event->GetType() == NDb::BASEUNITEVENT_MOVE) ++moves;
		return 0;
	}
};

/** Real hero orders must use the same authored speed, FSM and route planner as other movers. */
static void HeroMovementB13(Checks& check)
{
	const int before=check.count, failed=check.failed;
	{
		RoutingFixtureB13 f(check); if (!f.world) return;
		auto mover=f.Mover(CVec2(20.5f,20.5f));
		for (const CVec2 goal : {CVec2(20,21),CVec2(20,19),CVec2(21,20),CVec2(19,20),CVec2(19,19),CVec2(21,21)})
		{
			mover->TeleportTo(CVec2(20.5f,20.5f));
			const float distance=fabs(goal-mover->GetPos());
			check(mover->CheckStraightMove(goal,MAP_MODE_ALL_STATICS)>=distance-1e-5f,
				"B13 static segment ends in the requested tile without stepping past a negative-axis boundary");
			mover->MoveTo(goal,0);
			for (int tick=0;tick<10 && mover->IsMoving();++tick) f.Tick();
			check(!mover->IsMoving() && mover->GetPos()==goal,"B13 original movement reaches exact integer tile-edge destinations");
		}
	}
	for (float offset : {0.f,.25f})
	{
		RoutingFixtureB13 f(check); if (!f.world) return;
		auto hero=B9MaleHero(f.world,NDb::FACTION_FREEZE);
		auto target=B8BehaviourMoving(f.world,NDb::FACTION_BURN);
		// Exact float endpoint observed from the redirected ordinary path above.
		hero->TeleportTo(CVec2(5.5f+offset,24.1339741f+offset)); target->TeleportTo(CVec2(5+offset,25+offset));
		hero->SetObjectSize(1); target->SetObjectSize(1);
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		target->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); target->AddFlag(NDb::UNITFLAG_FORBIDATTACK);
		auto* raw=B11AttackDescriptor(0); raw->useRange.sString="0.5";
		CObj<PFBaseAttackData> attack=new PFBaseAttackData(hero.GetPtr(),raw); hero->ReplaceBaseAttack(attack,false);
		const float health=target->GetHealth();
		check(!hero->IsTargetInAttackRange(target.GetPtr(),true),"B13 rounded fixture starts strictly outside authored attack reach");
		hero->EnqueueState(new PFBaseUnitAttackState(f.world,hero.GetPtr(),target.GetPtr(),true,true,true),true);
		for (int tick=0;tick<20 && target->GetHealth()==health;++tick) f.Tick();
		if (target->GetHealth()!=health-25)
			std::printf("Hero rounded chase: hp=%.9g/%.9g position=%.9g,%.9g target=%.9g,%.9g distance=%.9g size=%.9g range=%.9g inRange=%d state=%p moving=%d\n",
				target->GetHealth(),health,hero->GetPos().x,hero->GetPos().y,target->GetPos().x,target->GetPos().y,
				fabs(hero->GetPos()-target->GetPos()),target->GetObjectSize(),attack->GetUseRange(target.GetPtr()),
				hero->IsTargetInAttackRange(target.GetPtr(),true),hero->GetCurrentState(),hero->IsMoving());
		// The real victim can move during its own later tick; damage proves range at launch.
		check(target->GetHealth()==health-25 && attack->GetUseRange(target.GetPtr())==.5f,
			"B13 rounded path endpoint cannot strand a hero outside the exact authored attack range");
		hero->Cleanup();
	}
	for (const CVec2 goal : {CVec2(80.25f,60.75f),CVec2(5,25),CVec2(150.25f,80.75f),CVec2(100,110),CVec2(65.125f,30.5f)})
	{
		RoutingFixtureB13 f(check); if (!f.world) return;
		auto hero=B9MaleHero(f.world,NDb::FACTION_FREEZE);
		auto target=B8BehaviourMoving(f.world,NDb::FACTION_BURN);
		hero->TeleportTo(CVec2(20.5f,20.5f)); target->TeleportTo(goal);
		hero->SetObjectSize(2); target->SetObjectSize(2);
		hero->GetStat(NDb::STAT_MOVESPEED)->SetCoreValue(50);
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		target->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); target->AddFlag(NDb::UNITFLAG_FORBIDATTACK);
		auto* raw=B11AttackDescriptor(0); raw->useRange.sString="2";
		CObj<PFBaseAttackData> attack=new PFBaseAttackData(hero.GetPtr(),raw); hero->ReplaceBaseAttack(attack,false);
		const float health=target->GetHealth();
		hero->EnqueueState(new PFBaseUnitAttackState(f.world,hero.GetPtr(),target.GetPtr(),true,true,true),true);
		for (int tick=0;tick<500 && target->GetHealth()==health;++tick) f.Tick();
		if (target->GetHealth()==health)
			std::printf("Hero chase diagnostic: position=%.9g,%.9g target=%.9g,%.9g distance=%.9g range=%.9g moving=%d\n",
				hero->GetPos().x,hero->GetPos().y,target->GetPos().x,target->GetPos().y,
				fabs(hero->GetPos()-target->GetPos()),attack->GetUseRange(target.GetPtr()),hero->IsMoving());
		check(target->GetHealth()==health-25 && hero->IsTargetInAttackRange(target.GetPtr(),true),
			"B13 ordinary hero chase crosses the exact attack boundary and delivers authored damage");
		hero->Cleanup();
	}
	{
		RoutingFixtureB13 f(check); if (!f.world) return;
		f.world->SetLinuxAbilityAnimationTiming(std::make_shared<B13TimingSource>());
		auto hero=B9MaleHero(f.world,NDb::FACTION_FREEZE);
		auto target=B8BehaviourMoving(f.world,NDb::FACTION_BURN); target->TeleportTo(CVec2(10,5));
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		CObj<PFBaseAttackData> attack=new PFBaseAttackData(hero.GetPtr(),B11AttackDescriptor(0));
		hero->ReplaceBaseAttack(attack,false);
		hero->EnqueueState(new PFBaseUnitAttackState(f.world,hero.GetPtr(),target.GetPtr(),true,true,true),true);
		hero->Simulate(0);
		check(!hero->IsAttackFinished() && hero->GetCurrentState(),"B13 move interruption fixture has an actual unfinished windup");
		hero->AddFlag(NDb::UNITFLAG_FORBIDATTACK);
		hero->Move(CVec2(20,20)); hero->Simulate(.1f);
		check(hero->HasQueuedStates() && dynamic_cast<const PFBaseUnitAttackState*>(hero->GetCurrentState()),
			"B13 pending movement preserves the original unfinished-attack block");
		for (int tick=0;tick<4;++tick) hero->Simulate(.1f);
		check(hero->IsAttackFinished() && !hero->HasQueuedStates()
			&& dynamic_cast<const PFBaseUnitMoveToState*>(hero->GetCurrentState()),
			"B13 completed windup releases queued movement through the original FSM");
		hero->Cleanup();
	}
	{
		RoutingFixtureB13 f(check); if (!f.world) return;
		auto hero=B9MaleHero(f.world,NDb::FACTION_FREEZE);
		const NDb::EUnitFlag flags[]={NDb::UNITFLAG_FORBIDPLAYERCONTROL,NDb::UNITFLAG_INMINIGAME,
			NDb::UNITFLAG_FORBIDMOVE,NDb::UNITFLAG_CANTURNWHILEFORBIDMOVE};
		for (int mask=0;mask<16;++mask)
		{
			for (int bit=0;bit<4;++bit) if (mask & (1<<bit)) hero->AddFlag(flags[bit]);
			const bool allowed=!(mask&1) && !(mask&2) && (!(mask&4) || (mask&8));
			check(hero->CanMove()==allowed,"B13 hero CanMove matches original control/turn-only policy");
			CObj<NCore::WorldCommand> move=CreateCmdMoveHero(hero.GetPtr(),CVec2(20,20),false);
			check(move->CanExecute()==allowed,"B13 move command uses original hero control admission");
			for (int bit=0;bit<4;++bit) if (mask & (1<<bit)) hero->RemoveFlag(flags[bit]);
		}
		CObj<NCore::WorldCommand> queued=CreateCmdMoveHero(hero.GetPtr(),CVec2(20,20),false);
		check(queued->CanExecute(),"B13 late-control fixture first admits movement");
		hero->AddFlag(NDb::UNITFLAG_FORBIDPLAYERCONTROL); queued->Execute(f.world); f.Tick();
		check(!hero->IsMoving() && !hero->HasQueuedStates() && !hero->GetCurrentState(),
			"B13 queued movement rechecks controls before changing the hero state");
		hero->Cleanup();
	}
	{
		RoutingFixtureB13 f(check,20,12,10,true); if (!f.world) return;
		auto hero=B9MaleHero(f.world,NDb::FACTION_FREEZE);
		hero->SetObjectSize(1); hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		hero->GetStat(NDb::STAT_MOVESPEED)->SetCoreValue(12);
		hero->TeleportTo(CVec2(20.5f,60.5f));
		const CVec2 goal(150.25f,60.75f);
		const unsigned flags=hero->GetMoveFlags();
		CObj<NCore::WorldCommand> move=CreateCmdMoveHero(hero.GetPtr(),goal,false);
		check(move->CanExecute(),"B13 real hero route command is admitted"); move->Execute(f.world);
		check(hero->HasQueuedStates() && !hero->IsMovingSpecial(),"B13 hero movement enters the original order queue, not special movement");
		check(hero->GetMoveFlags()==flags,"B13 hero movement does not invent no-collide/speed-override flags");
		f.Tick();
		check(dynamic_cast<const PFBaseUnitMoveToState*>(hero->GetCurrentState()) && hero->IsMoving(),
			"B13 world tick installs the original hero move state and path");
		check(fabs(hero->GetUnitSpeed()-hero->GetMoveSpeedMpS())<.001f,"B13 hero path uses authored live movement speed");
		bool admissible=true, detoured=false;
		for (int tick=0;tick<900 && (hero->IsMoving() || hero->HasQueuedStates() || hero->GetCurrentState());++tick)
		{
			f.Tick(); const CVec2 at=hero->GetPos();
			detoured |= at.y<30 || at.y>=91;
			MapModeChanger mode(MAP_MODE_ALL_STATICS,f.world->GetTileMap());
			admissible &= f.world->GetTileMap()->CanUnitGo(hero->GetUnitRadius(),f.world->GetTileMap()->GetTile(at));
		}
		check(admissible && detoured,"B13 player movement detours around loaded authored collision geometry");
		check(!hero->IsMoving() && fabs(hero->GetPos()-goal)<=1.05f,"B13 original hero move state reaches its authored one-unit arrival range");
		hero->Cleanup();
	}
	{
		RoutingFixtureB13 f(check); if (!f.world) return;
		auto hero=B9MaleHero(f.world,NDb::FACTION_FREEZE);
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		hero->Move(CVec2(50,5)); f.Tick(); f.Tick();
		CObj<IPFState> first=hero->GetCurrentState();
		check(dynamic_cast<PFBaseUnitMoveToState*>(first.GetPtr())!=nullptr,"B13 reissue fixture has an active original move state");
		hero->GetStat(NDb::STAT_MOVESPEED)->SetCoreValue(9); f.Tick();
		check(fabs(hero->GetUnitSpeed()-hero->GetMoveSpeedMpS())<.001f,"B13 movement reacts to a live speed-stat change");
		const CVec2 goal(5,25); hero->Move(goal);
		check(first && hero->GetCurrentState()==first && !hero->HasQueuedStates(),"B13 reissued movement updates the current state instead of replacing it");
		f.Tick();
		for (int tick=0;tick<300 && (hero->IsMoving() || hero->GetCurrentState());++tick) f.Tick();
		if (fabs(hero->GetPos()-goal)>1.05f || hero->IsMoving())
			std::printf("Hero reissue diagnostic: position=%.9g,%.9g goal=%.9g,%.9g distance=%.9g moving=%d state=%p\n",
				hero->GetPos().x,hero->GetPos().y,goal.x,goal.y,fabs(hero->GetPos()-goal),hero->IsMoving(),hero->GetCurrentState());
		check(fabs(hero->GetPos()-goal)<=1.05f && !hero->IsMoving(),"B13 reissued path reaches the new endpoint");
		hero->Move(CVec2(50,25)); f.Tick();
		CObj<NCore::WorldCommand> stop=CreateCmdStopHero(hero.GetPtr()); stop->Execute(f.world);
		const CVec2 stopped=hero->GetPos(); f.Tick();
		check(!hero->IsMoving() && !hero->GetCurrentState() && !hero->HasQueuedStates() && hero->GetPos()==stopped,
			"B13 ordinary stop drops the hero route and queued/current move state");
		hero->Cleanup();
	}
	{
		RoutingFixtureB13 f(check); if (!f.world) return;
		auto hero=B9MaleHero(f.world,NDb::FACTION_FREEZE);
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); hero->SetObjectSize(1);
		hero->GetStat(NDb::STAT_MOVESPEED)->SetCoreValue(12);
		for (const CVec2 goal : {CVec2(25.25f,40.75f),CVec2(5,25),CVec2(48.25f,5.125f),CVec2(12.5f,8.5f)})
		{
			hero->Move(goal); f.Tick();
			for (int tick=0;tick<400 && (hero->IsMoving() || hero->GetCurrentState());++tick) f.Tick();
			if (fabs(hero->GetPos()-goal)>1.0001f || hero->IsMoving() || hero->GetCurrentState())
				std::printf("Hero arrival diagnostic: position=%.9g,%.9g goal=%.9g,%.9g distance=%.9g moving=%d state=%p speed=%.9g\n",
					hero->GetPos().x,hero->GetPos().y,goal.x,goal.y,fabs(hero->GetPos()-goal),hero->IsMoving(),hero->GetCurrentState(),hero->GetUnitSpeed());
			check(fabs(hero->GetPos()-goal)<=1.0001f && !hero->IsMoving() && !hero->GetCurrentState(),
				"B13 rounded arrival retires the move state in every direction");
		}
		const CVec2 goal(60,60); hero->Move(goal); f.Tick();
		hero->TeleportTo(goal-CVec2(1.001f,0)); f.Tick();
		check(hero->IsMoving() && hero->GetCurrentState(),"B13 stopped position outside rounding error still resumes its route");
		hero->Cleanup();
	}
	{
		RoutingFixtureB13 f(check); if (!f.world) return;
		auto hero=B9MaleHero(f.world,NDb::FACTION_FREEZE);
		CObj<HeroMoveEventsB13> events=new HeroMoveEventsB13; hero->AddEventListener(events.GetPtr());
		hero->Move(hero->GetPos()); f.Tick();
		check(events->moves==1 && !hero->IsMoving(),"B13 nearby move emits one original move notification without starting a path");
		const int previous=events->moves;
		hero->Move(CVec2(-1,10)); f.Tick();
		check(events->moves==previous+1 && !hero->IsMoving(),"B13 initial path failure still emits the original move notification");
		hero->AddFlag(NDb::UNITFLAG_FORBIDMOVE); hero->AddFlag(NDb::UNITFLAG_CANTURNWHILEFORBIDMOVE);
		const CVec2 position=hero->GetPos(); hero->Move(position+CVec2(0,10)); f.Tick();
		check(hero->GetPos()==position && !hero->IsMoving() && hero->GetMoveDirection().y>.99f,
			"B13 allowed turn-only order changes facing without translating the rooted hero");
		hero->RemoveEventListener(events.GetPtr()); hero->Cleanup();
	}
	{
		RoutingFixtureB13 f(check); if (!f.world) return;
		auto hero=B9MaleHero(f.world,NDb::FACTION_FREEZE);
		auto mount=f.Mover(CVec2(20,20)); mount->AttachUnit(hero.GetPtr()); hero->OnMountSpecial(mount.GetPtr(),true);
		hero->AddFlag(NDb::UNITFLAG_FORBIDMOVE);
		check(hero->ControlsMount() && hero->CanMove(),"B13 mount controller can move its mount while passenger movement is forbidden");
		const unsigned flags=mount->GetMoveFlags(); const float speed=mount->GetUnitSpeed();
		hero->Move(CVec2(35,20)); f.Tick();
		check(dynamic_cast<const PFBaseUnitMoveToState*>(mount->GetCurrentState()) && !hero->GetCurrentState(),
			"B13 controlled mount owns the move state, not its passenger");
		check(mount->GetMoveFlags()==flags && mount->GetUnitSpeed()==speed && !mount->IsMovingSpecial(),
			"B13 mount movement retains caller flags/speed and uses ordinary routing");
		mount->Cleanup(); mount->Stop(false); mount->DetachUnit(); hero->DismountSpecial(); hero->Cleanup();
	}
	std::printf("Hero movement gameplay: checks=%d failures=%d\n",check.count-before,check.failed-failed);
}
