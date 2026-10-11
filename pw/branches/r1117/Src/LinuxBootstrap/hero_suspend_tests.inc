/** A hook observer records suspend completion while retaining the engine callback. */
struct B9SuspendHero : B8BehaviourHero
{
	int callbacks = 0;
	bool longCallback = false;
	B9SuspendHero(PFWorld* world, const SpawnInfo& spawn)
		: B8BehaviourHero(world, spawn, NDb::FACTION_FREEZE) {}
	void OnAfterSuspend(bool isLong) override
	{
		++callbacks; longCallback = isLong;
		PFBaseHero::OnAfterSuspend(isLong);
	}
};

/** Authored short/long durations persist in the real order state until expiration. */
static void HeroSuspendB9(Checks& check, PFWorld* world)
{
	for (bool longSuspend : {false, true})
	{
		CObj<B8BehaviourHero> seed = B8BehaviourHeroUnit(world, NDb::FACTION_FREEZE);
		PFBaseHero::SpawnInfo spawn;
		spawn.pHero = dynamic_cast<const NDb::Hero*>(seed->DbUnitDesc());
		spawn.placement = Placement(CVec3(5, 5, 0), QNULL, CVec3(1, 1, 1));
		CObj<B9SuspendHero> hero = new B9SuspendHero(world, spawn);
		NDb::UnitTargetingParameters* params = new NDb::UnitTargetingParameters;
		params->sortSuspendTimeout = .5f; params->longSuspendTimeout = 2.f;
		hero->OverrideTargetingParams(params);
		CObj<IPFState> order = new PFHeroSuspendState(hero.GetPtr(), longSuspend);
		check(order->IsEscaping(), "suspend retains the engine's escaping-order contract");
		check(!order->DoStep(0) && hero->callbacks == 0, "suspend does not expire at entry");
		const float duration = longSuspend ? 2.f : .5f;
		check(!order->DoStep(duration * .5f), "suspend remains active before authored deadline");
		check(order->DoStep(duration * .5f), "suspend completes exactly at authored deadline");
		order->DoLeave();
		check(hero->callbacks == 1 && hero->longCallback == longSuspend,
			"suspend leave calls the existing completion hook with duration kind");
		CObj<IPFState> escaping = new PFHeroSuspendState(hero.GetPtr(), longSuspend);
		hero->PushState(escaping.GetPtr()); hero->AdvanceOrders(0);
		check(hero->GetCurrentState() == escaping.GetPtr(), "suspend occupies the production owner stack");
		CObj<SchedulingState> replacement = new SchedulingState(hero.GetPtr());
		hero->PushState(replacement.GetPtr());
		check(hero->GetCurrentState() == replacement.GetPtr() && hero->callbacks == 2,
			"a later order replaces escaping suspend through the existing leave hook");
		hero->DropStates();
		CObj<IPFState> invalid = new PFHeroSuspendState(hero.GetPtr(), longSuspend);
		check(invalid->DoStep(std::numeric_limits<float>::quiet_NaN()),
			"nonfinite suspend timestep exits without poisoning elapsed time");
	}
	CObj<B8BehaviourHero> hero = B8BehaviourHeroUnit(world, NDb::FACTION_FREEZE);
	CObj<B8BehaviourCreep> mount = B8BehaviourMoving(world, NDb::FACTION_FREEZE);
	mount->AttachUnit(hero.GetPtr());
	CObj<IPFState> mounted = new PFHeroSuspendState(hero.GetPtr(), false);
	check(mounted->DoStep(.1f) && hero->IsMounted(), "mounted hero cannot enter suspend");
	mount->DetachUnit();
}
