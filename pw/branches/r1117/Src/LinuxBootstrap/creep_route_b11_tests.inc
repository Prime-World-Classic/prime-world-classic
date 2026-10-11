/** Lane movement must use the existing route state and survive combat interruption. */
static void CommonCreepRouteB11(Checks& check, PFWorld* world)
{
	vector<CVec2> route;
	route.push_back(CVec2(5, 5));
	route.push_back(CVec2(25, 5));
	route.push_back(CVec2(25, 25));
	CObj<PFCommonCreep> creep = CommonCreepB11(world, route);
	creep->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
	creep->AddFlag(NDb::UNITFLAG_FORBIDMOVE);
	creep->Step(0);
	check(creep->IsIdle() && !creep->IsMoving(), "forbidden lane movement does not create an order");
	creep->RemoveFlag(NDb::UNITFLAG_FORBIDMOVE);
	creep->Step(0);
	const IPFState* moving = creep->GetCurrentState();
	check(dynamic_cast<const PFBaseUnitPathMovingState*>(moving) != nullptr,
		"lane behavior installs the production route state");
	for (int i = 0; i < 10; ++i) creep->Step(0.1f);
	check(creep->GetCurrentState() == moving && creep->GetPosition().x > 5,
		"lane ticks retain and advance one route state");
	CObj<B8BehaviourCreep> target = B8BehaviourMoving(world, NDb::FACTION_BURN);
	target->EnemyVisible(true);
	creep->OnTarget(target.GetPtr(), true);
	check(B8BehaviourAttack(*creep) != nullptr, "creep combat replaces the route state");
	creep->AddFlag(NDb::UNITFLAG_FORBIDATTACK);
	creep->Step(0);
	check(B8BehaviourAttack(*creep) && !creep->IsMoving(), "paused combat does not resume lane movement");
	target->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
	creep->Step(0);
	creep->RemoveFlag(NDb::UNITFLAG_FORBIDATTACK);
	creep->Step(0);
	check(dynamic_cast<const PFBaseUnitPathMovingState*>(creep->GetCurrentState()) != nullptr,
		"creep resumes its authored route after combat target death");
	for (int i = 0; i < 120; ++i) creep->Step(0.1f);
	check(creep->IsPositionInRange(route.back(), 7.5f), "resumed creep reaches the authored route endpoint");
	creep->DropStates();
	check(!creep->IsMoving(), "dropping lane route stops its movement");
}
