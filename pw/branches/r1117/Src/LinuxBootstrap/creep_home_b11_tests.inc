/** Exercise authored non-evading home movement and automatic attack admission. */
static void CreepHomeB11(Checks& check, PFWorld* world)
{
	B8BehaviourForeignWorld isolated(check);
	if (!isolated.world) return;
	world = isolated.world;
	B8BehaviourReveal reveal(world, NDb::FACTION_FREEZE);
	CObj<PFCreepSpawner> spawner = CreepSpawnerB11(world, false);
	CObj<PFCommonCreep> creep = CommonCreepB11(world, vector<CVec2>(), spawner.GetPtr());
	creep->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
	const float tileSize = world->GetTileMap()->GetTileSize();
	creep->TeleportTo(CVec2(5 + tileSize * 6, 5), false, false);
	creep->AddFlag(NDb::UNITFLAG_FORBIDMOVE);
	creep->Step(0);
	check(creep->IsIdle(), "immobilized stationary creep does not enqueue home movement");
	creep->RemoveFlag(NDb::UNITFLAG_FORBIDMOVE);
	creep->Step(0);
	check(dynamic_cast<const PFBaseUnitCombatMoveState*>(creep->GetCurrentState()) != nullptr,
		"idle displaced stationary creep uses the production combat-move state");
	for (int i = 0; i < 100; ++i) creep->Step(0.1f);
	const PFEvadeCreepBehaviour* behavior = dynamic_cast<const PFEvadeCreepBehaviour*>(creep->GetBehaviour());
	check(behavior && behavior->IsCreepAtSpawner(4), "stationary creep returns to its authored home radius");
	creep->DropStates();
	creep->TeleportTo(CVec2(5, 5), false, false);
	creep->RemoveFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
	CObj<PFBaseUnit> target = Unit(world, NDb::FACTION_BURN, 20);
	check(creep->GetTargetingRange() > 0 && creep->CanSelectTarget(target.GetPtr(), true),
		"stationary fixture target passes range and visible behavior admission");
	check(creep->IsTargetInRange(target.GetPtr(), creep->GetTargetingRange()),
		"stationary fixture target lies within acquisition radius");
	check(target->ringField.isLinked() && UnitMaskingPredicate(creep->GetOppositeFactionFlags(), NDb::SPELLTARGET_ALL, creep.GetPtr())(*target),
		"stationary fixture target is indexed and matches automatic target masks");
	check(creep->FindTarget(creep->GetTargetingRange()) == target.GetPtr(),
		"isolated stationary-creep fixture exposes exactly its intended automatic target");
	creep->Step(0);
	check(B8BehaviourAttack(*creep) && creep->GetCurrentTarget() == target.GetPtr(),
		"idle stationary creep automatically enters a real attack order");
	creep->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
}
