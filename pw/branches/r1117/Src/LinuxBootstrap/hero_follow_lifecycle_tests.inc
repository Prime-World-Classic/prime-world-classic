#include "../PF_GameLogic/PFHeroStates.h"

/** Run production follow orders through their existing IPFState entry/leave hooks. */
static void HeroFollowLifecycleB9(Checks& check, PFWorld* world)
{
	for (float radius : {1.f, 3.f})
	{
		CObj<B8BehaviourHero> hero = B8BehaviourHeroUnit(world, NDb::FACTION_FREEZE);
		CObj<B8BehaviourCreep> leader = B8BehaviourMoving(world, NDb::FACTION_FREEZE);
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		hero->TeleportTo(CVec2(5, 5), true, false);
		leader->TeleportTo(CVec2(12, 5), true, false);
		CObj<IPFState> order = new PFHeroFollowUnitState(hero.GetPtr(), leader.GetPtr(), radius);
		check(!order->DoStep(0) && hero->IsMoving(), "follow order persists and starts real movement");
		hero->Stop();
		check(!order->DoStep(0) && hero->IsMoving(), "follow order restarts interrupted ordinary movement");
		hero->TeleportTo(CVec2(12, 5), true, false);
		check(!order->DoStep(0) && !hero->IsMoving(), "follow order holds inside requested distance");
		leader->TeleportTo(CVec2(20, 5), true, false);
		check(!order->DoStep(0) && hero->IsMoving(), "follow order tracks a relocated live leader");
		order->DoLeave(true);
		check(!hero->IsMoving(), "follow order cleanup stops owned ordinary movement");
	}
	CObj<B8BehaviourHero> hero = B8BehaviourHeroUnit(world, NDb::FACTION_FREEZE);
	CObj<B8BehaviourCreep> leader = B8BehaviourMoving(world, NDb::FACTION_FREEZE);
	CObj<IPFState> self = new PFHeroFollowUnitState(hero.GetPtr(), hero.GetPtr(), 2);
	check(self->DoStep(0), "self follow is rejected without persistent movement");
	CObj<IPFState> dead = new PFHeroFollowUnitState(hero.GetPtr(), leader.GetPtr(), 2);
	leader->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
	check(dead->DoStep(0), "dead leader completes follow without accessing stale movement");
	CObj<B8BehaviourCreep> mount = B8BehaviourMoving(world, NDb::FACTION_FREEZE);
	mount->AttachUnit(hero.GetPtr());
	CObj<IPFState> mounted = new PFHeroFollowUnitState(hero.GetPtr(), mount.GetPtr(), 2);
	check(mounted->DoStep(0) && hero->IsMounted(), "mounted hero exits follow without dismounting");
	mount->DetachUnit();
}
