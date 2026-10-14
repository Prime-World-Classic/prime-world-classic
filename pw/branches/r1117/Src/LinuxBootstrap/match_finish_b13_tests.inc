/** Count real finish notifications without replacing the production behaviour/state cleanup. */
struct B13FinishUnit : ProbeUnit
{
	B13FinishUnit(PFWorld* world,const NDb::Unit* db,NDb::EFaction faction) : ProbeUnit(world,db,faction) {}
	int finishes=0;
	NDb::EFaction loser=NDb::FACTION_NEUTRAL;
	std::function<void()> duringFinish;
	void OnGameFinished(NDb::EFaction failed) override
	{
		++finishes; loser=failed;
		if (duringFinish) duringFinish();
		PFBaseUnit::OnGameFinished(failed);
	}
};

/** Dead heroes are not in the voxel map, but still receive the original match-end notification. */
struct B13FinishHero : B9CastHero
{
	B13FinishHero(PFWorld* world,const SpawnInfo& spawn) : B9CastHero(world,spawn,NDb::FACTION_FREEZE) {}
	int finishes=0;
	void OnGameFinished(NDb::EFaction failed) override { ++finishes; PFBaseHero::OnGameFinished(failed); }
};

static CObj<B13FinishHero> MatchFinishHeroB13(PFWorld* world)
{
	auto seed=B9MaleHero(world,NDb::FACTION_FREEZE);
	PFBaseHero::SpawnInfo spawn; spawn.pHero=seed->GetDbHero();
	spawn.placement=Placement(CVec3(5,5,0),QNULL,CVec3(1,1,1));
	return new B13FinishHero(world,spawn);
}

/** Admission, first-defeat ownership, main-building death and callback-safe finish use real worlds. */
static void MatchFinishB13(Checks& check)
{
	const auto before=check.count,failed=check.failed;
	{
		B8BehaviourForeignWorld f(check); if (!f.world) return;
		f.world->OnGameFinished(NDb::FACTION_FREEZE);
		check(!f.world->IsFactionDefeated(NDb::FACTION_FREEZE) && !f.world->GetAIWorld()->WasGameFinished(),
			"B13 automatic finish requires an actually recorded defeated faction");
		f.world->SetDefeatedFaction(NDb::FACTION_FREEZE); f.world->SetDefeatedFaction(NDb::FACTION_BURN);
		check(f.world->IsFactionDefeated(NDb::FACTION_FREEZE) && !f.world->IsFactionDefeated(NDb::FACTION_BURN),
			"B13 first main-building defeat cannot be replaced by a later death");
		f.world->SetManualGameFinish(true); f.world->OnGameFinished(NDb::FACTION_FREEZE);
		check(!f.world->GetAIWorld()->WasGameFinished(),"B13 manual-finish maps defer automatic main-building victory");
		f.world->GameFinish(NDb::FACTION_BURN);
		check(!f.world->GetAIWorld()->WasGameFinished(),"B13 conflicting scripted finish cannot reverse the first defeat");
		f.world->GameFinish(NDb::FACTION_FREEZE);
		check(f.world->GetAIWorld()->WasGameFinished() && f.world->IsFactionDefeated(NDb::FACTION_FREEZE),
			"B13 explicit scripted finish reaches real AI match completion");
	}
	{
		B8BehaviourForeignWorld f(check), other(check); if (!f.world || !other.world) return;
		auto seed=Unit(f.world,NDb::FACTION_FREEZE,40);
		CObj<B13FinishUnit> unit=new B13FinishUnit(f.world,seed->DbUnitDesc(),NDb::FACTION_BURN);
		CObj<B13FinishUnit> dead=new B13FinishUnit(f.world,seed->DbUnitDesc(),NDb::FACTION_BURN);
		CObj<B13FinishUnit> foreign=new B13FinishUnit(other.world,seed->DbUnitDesc(),NDb::FACTION_BURN);
		auto hero=MatchFinishHeroB13(f.world), deadHero=MatchFinishHeroB13(f.world);
		CObj<NCore::WorldCommand> move=CreateCmdMoveHero(hero.GetPtr(),CVec2(15,15),false);
		check(move->CanExecute(),"B13 moving hero command is admitted before match end");
		move->Execute(f.world);
		check(hero->IsMoving(),"B13 match finish fixture has an active movement cursor");
		dead->AddFlag(NDb::UNITFLAG_FORBIDREWARDS); dead->ChangeHealth(-1000);
		deadHero->AddFlag(NDb::UNITFLAG_FORBIDREWARDS); deadHero->ChangeHealth(-1000);
		check(dead->IsDead() && deadHero->IsDead(),"B13 finish fixture includes an actual dead unit and dead hero");
		unit->EnqueueState(new PFBaseUnitState(unit.GetPtr()),true);
		hero->EnqueueState(new PFBaseUnitState(hero.GetPtr()),true);
		check(unit->HasQueuedStates() && hero->HasQueuedStates(),"B13 finish fixture retains real pending orders");
		unit->duringFinish=[&] {
			check(f.world->GetAIWorld()->WasGameFinished(),"B13 completion is latched before callbacks");
			f.world->GameFinish(NDb::FACTION_FREEZE);
		};
		f.world->GameFinish(NDb::FACTION_BURN);
		check(unit->finishes==1 && hero->finishes==1 && deadHero->finishes==1 && dead->finishes==0 && foreign->finishes==0,
			"B13 finish notifies live local units and both live/dead heroes exactly once");
		check(!unit->HasQueuedStates() && !hero->HasQueuedStates() && !unit->GetBehaviour() && !hero->GetBehaviour() && !hero->IsMoving(),
			"B13 original match-end handlers remove behaviours and pending orders");
		check(f.world->IsFactionDefeated(NDb::FACTION_BURN),"B13 reentrant opposing finish cannot replace the accepted result");
		f.world->GameFinish(NDb::FACTION_BURN); f.world->OnGameFinished(NDb::FACTION_BURN);
		check(unit->finishes==1 && hero->finishes==1 && deadHero->finishes==1,"B13 repeated finish does not replay lifecycle callbacks");
		const CVec2 position=hero->GetPos();
		CObj<NCore::WorldCommand> command=CreateCmdMoveHero(hero.GetPtr(),CVec2(15,15),false);
		check(!command->CanExecute(),"B13 completed match rejects new movement admission");
		command->Execute(f.world);
		check(!hero->HasQueuedStates() && !hero->IsMoving() && hero->GetPos()==position,"B13 queued player commands cannot restart a completed match");
		unit->duringFinish={}; hero->Cleanup(); deadHero->Cleanup();
	}
	{
		B8BehaviourForeignWorld f(check); if (!f.world) return;
		auto seed=Unit(f.world,NDb::FACTION_FREEZE,40);
		CObj<B13FinishUnit> unit=new B13FinishUnit(f.world,seed->DbUnitDesc(),NDb::FACTION_BURN);
		auto hero=MatchFinishHeroB13(f.world);
		unit->duringFinish=[&] { f.world->UnregisterObject(hero.GetPtr(),false); f.world->ProcessAddRemove(); };
		f.world->GameFinish(NDb::FACTION_BURN);
		check(unit->finishes==1 && hero->finishes==0,"B13 callback-unregistered hero is not notified through a stale finish snapshot");
		unit->duringFinish={}; hero->Cleanup();
	}
	{
		B8BehaviourForeignWorld f(check); if (!f.world) return;
		auto attacker=Unit(f.world,NDb::FACTION_BURN,40);
		auto* db=new NDb::MainBuilding; db->stats=attacker->DbUnitDesc()->stats; db->faction=NDb::FACTION_FREEZE;
		NDb::AdvMapObject object; object.gameObject=db;
		CObj<PFMainBuilding> base=new PFMainBuilding(f.world,object);
		base->SetVulnerable(true); base->AddFlag(NDb::UNITFLAG_FORBIDREWARDS);
		PFBaseUnit::DamageDesc damage; damage.pSender=attacker; damage.amount=2000;
		damage.flags=0; damage.damageMode=static_cast<NDb::EDamageMode>(0); damage.pDealerApplicator=nullptr;
		damage.damageType=NDb::APPLICATORDAMAGETYPE_PURE; damage.dontAttackBack=true; damage.ignoreDefences=true;
		base->OnDamage(damage);
		check(base->IsDead() && f.world->IsFactionDefeated(NDb::FACTION_FREEZE) && f.world->GetAIWorld()->WasGameFinished(),
			"B13 real lethal main-building damage reaches native match completion");
	}
	std::printf("Match completion gameplay: checks=%d failures=%d\n",check.count-before,check.failed-failed);
}
