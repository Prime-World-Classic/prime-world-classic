/** Supply authored spawner parameters; constructor and behavior selection remain real. */
static CObj<PFCreepSpawner> CreepSpawnerB11(PFWorld* world, bool evade, float walkLimit = 5)
{
	NDb::AdvMapCreepSpawner* db = new NDb::AdvMapCreepSpawner;
	db->faction = NDb::FACTION_FREEZE;
	db->mustRegisterInAIWorld = false;
	db->limitWalkDistance = walkLimit;
	db->useEvade = evade;
	db->evadeParams.waitForEvadeTime = 0.5f;
	db->evadeParams.waitForNextTargetTime = 0.5f;
	db->evadeParams.evadeTimeout = 5;
	db->evadeParams.restoreHealthPercent = 0.1f;
	NDb::AdvMapObject object;
	object.gameObject = db;
	object.offset = CPlacement(CVec3(5, 5, 0), QNULL, CVec3(1, 1, 1));
	return new PFCreepSpawner(world, object);
}

/** Leash geometry, default-state queries and target callbacks use original contracts. */
static void CreepEvadeAdmissionB11(Checks& check, PFWorld* world)
{
	CObj<PFCreepSpawner> spawner = CreepSpawnerB11(world, false);
	CObj<PFCommonCreep> creep = CommonCreepB11(world, vector<CVec2>(), spawner.GetPtr());
	const PFEvadeCreepBehaviour* behavior = dynamic_cast<const PFEvadeCreepBehaviour*>(creep->GetBehaviour());
	check(behavior != nullptr, "empty-route spawner installs the original evade behavior");
	if (!behavior) return;
	CObj<B8BehaviourCreep> target = B8BehaviourMoving(world, NDb::FACTION_BURN);
	target->SetMoveFlags(MOVE_FLAG_NO_COLLIDE);
	target->EnemyVisible(true);
	const float boundary = 5 + creep->GetWalkLimit() + creep->GetAttackRange();
	target->TeleportTo(CVec2(boundary, 5), false, false);
	check(behavior->CanSelectTarget(target.GetPtr()), "spawner leash includes its attack-range boundary");
	target->TeleportTo(CVec2(boundary + 0.25f, 5), false, false);
	check(!behavior->CanSelectTarget(target.GetPtr()), "spawner leash excludes targets beyond its boundary");
	check(!behavior->CanSelectTarget(nullptr), "missing leash endpoint is rejected safely");
	check(behavior->IsInDefaultState(), "idle creep at home reports its default state");
	creep->TeleportTo(CVec2(5 + world->GetTileMap()->GetTileSize() * 5, 5), false, false);
	check(!behavior->IsCreepAtSpawner(4) && !behavior->IsInDefaultState(), "home checks use authored placement and tile-scaled distance");
	creep->OnTarget(target.GetPtr(), true);
	check(B8BehaviourAttack(*creep) != nullptr, "evade behavior explicit target installs the production attack state");
	creep->AddFlag(NDb::UNITFLAG_FORBIDATTACK);
	creep->FSMStep(0);
	check(creep->IsCurrentTargetStrong(), "evade attack state retains explicit target strength");
	creep->DropStates();
	creep->OnTarget(target.GetPtr(), true);
	check(creep->IsIdle(), "forbidden evade attack is not admitted");
	creep->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
}
