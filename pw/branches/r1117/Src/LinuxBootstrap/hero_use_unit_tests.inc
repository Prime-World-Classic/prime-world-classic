/** Inspect the real unit-use state, not a replacement interaction/casting implementation. */
struct B9UseUnit : PFHeroUseUnitState
{
	B9UseUnit(PFBaseHero* hero, PFBaseUnit* unit) : PFHeroUseUnitState(hero, unit) {}
	float Distance() const { return GetRange(); }
	PFAbilityInstance* Instance() const { return pActionInstance.GetPtr(); }
};

/** Target-dependent authored ranges, mana and start events use the production unit API. */
static void HeroUseUnitB9(Checks& check, PFWorld* world)
{
	for (bool forbidden : {false, true})
	{
		CObj<B8BehaviourHero> hero = B8BehaviourHeroUnit(world, NDb::FACTION_FREEZE);
		CObj<PFBaseUnit> unit = Unit(world, NDb::FACTION_FREEZE, 7);
		NDb::Ability* ability = new NDb::Ability;
		ability->useRange.sString = "2+tStrength/7";
		ability->manaCost.sString = "10";
		ability->dispatch = new NDb::DispatchImmediate;
		NDb::Interaction* interaction = new NDb::Interaction;
		interaction->externalAbility = ability;
		const_cast<NDb::Unit*>(unit->DbUnitDesc())->interaction = interaction;
		CObj<B9InteractionEvents> events = new B9InteractionEvents;
		hero->AddEventListener(events.GetPtr());
		if (forbidden) hero->AddFlag(NDb::UNITFLAG_FORBIDINTERACT);
		CObj<B9UseUnit> use = new B9UseUnit(hero.GetPtr(), unit.GetPtr());
		CPtr<IPFState> order = use.GetPtr();
		check(use->Distance() == 3.f, "unit-use range evaluates the authored formula against the actual target");
		check(!order->DoStep(0), "unit-use admits at most one action at entry");
		check((use->Instance() != nullptr) == !forbidden && events->started == (forbidden ? 0 : 1),
			"unit-use calls production Use and reports its ability instance only when interaction is allowed");
		check(hero->GetMana() == (forbidden ? 200.f : 190.f), "unit-use spends authored mana once, never while forbidden");
		check(!use->Instance() || events->lastStarted == use->Instance(), "unit-use start event identifies the actual instance");
		order->DoStep(0);
		check(events->started == (forbidden ? 0 : 1) && hero->GetMana() == (forbidden ? 200.f : 190.f),
			"unit-use subsequent steps do not dispatch or spend twice");
		order->DoLeave(true); hero->RemoveEventListener(events.GetPtr());
	}
	CObj<B8BehaviourHero> hero = B8BehaviourHeroUnit(world, NDb::FACTION_FREEZE);
	CObj<B9UseUnit> absent = new B9UseUnit(hero.GetPtr(), nullptr);
	CPtr<IPFState> order = absent.GetPtr();
	check(order->DoStep(0) && !absent->Instance(), "missing usable unit exits without a cast or movement");
}
