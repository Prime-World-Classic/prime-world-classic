/** Authored creep data and real engine objects, with no replacement combat model. */
static CObj<PFCommonCreep> CommonCreepB11(PFWorld* world, const vector<CVec2>& route = vector<CVec2>(), PFCreepSpawner* spawner = nullptr)
{
	NDb::Ptr<NDb::Creature> seed = B8BehaviourCreatureDb(world, NDb::FACTION_FREEZE);
	NDb::AdvMapCreep* raw = new NDb::AdvMapCreep;
	raw->stats = seed->stats;
	raw->targetingParams = seed->targetingParams;
	raw->creepType = NDb::CREEPTYPE_FACTIONALNORMAL;
	NDb::Ptr<NDb::AdvMapCreep> db = raw;
	CObj<PFCommonCreep> creep = new PFCommonCreep(world, *db, NDb::FACTION_FREEZE,
		Placement(CVec3(5, 5, 0), QNULL, CVec3(1, 1, 1)), route, 0, 0, spawner);
	NDb::DamageApplicator* damage = new NDb::DamageApplicator;
	damage->damage.sString = "13";
	damage->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
	NDb::BaseAttack* attack = new NDb::BaseAttack;
	attack->useRange.sString = "4";
	attack->manaCost.sString = "0";
	attack->applicators.push_back(damage);
	creep->ReplaceBaseAttack(new PFBaseAttackData(creep.GetPtr(), attack), false);
	creep->SetMoveFlags(MOVE_FLAG_NO_COLLIDE | MOVE_FLAG_OVERRIDE_SPEED);
	creep->SetUnitSpeed(4);
	return creep;
}

/** Creep callbacks install production attack orders; behavior ticks never deal damage. */
static void CommonCreepCombatB11(Checks& check, PFWorld* world)
{
	B8BehaviourReveal reveal(world, NDb::FACTION_FREEZE);
	for (bool strong : {false, true})
	{
		CObj<PFCommonCreep> creep = CommonCreepB11(world);
		CObj<B8BehaviourCreep> target = B8BehaviourMoving(world, NDb::FACTION_BURN);
		target->SetMoveFlags(MOVE_FLAG_NO_COLLIDE);
		target->TeleportTo(CVec2(7, 5), false, false);
		target->EnemyVisible(true);
		creep->OnTarget(target.GetPtr(), strong);
		const PFBaseUnitAttackState* state = B8BehaviourAttack(*creep);
		check(state && state->GetTarget() == target.GetPtr(), "creep target callback installs production attack state");
		creep->AddFlag(NDb::UNITFLAG_FORBIDATTACK);
		creep->FSMStep(0);
		check(creep->IsCurrentTargetStrong() == strong, "creep attack state retains requested target strength");
		const float life = target->GetHealth();
		PFCommonCreepBehaviour* behavior = const_cast<PFCommonCreepBehaviour*>(
			dynamic_cast<const PFCommonCreepBehaviour*>(creep->GetBehaviour()));
		check(behavior != nullptr, "common creep constructor installs its real behavior");
		if (!behavior) continue;
		behavior->OnStep(0.1f);
		check(target->GetHealth() == life, "creep behavior does not bypass FSM with direct damage");
		check(creep->GetCurrentState() == state, "creep behavior retains its active attack order");
		creep->RemoveFlag(NDb::UNITFLAG_FORBIDATTACK);
		creep->Step(0.1f);
		check(target->GetHealth() == life - 13, "unit simulation performs the authored creep attack exactly once");
		creep->DropStates();
		check(!IsValid(creep->GetCurrentTarget()) && !creep->IsMoving(), "dropping creep attack cleans target and movement");
	}
	for (int rejected = 0; rejected < 4; ++rejected)
	{
		CObj<PFCommonCreep> creep = CommonCreepB11(world);
		CObj<B8BehaviourCreep> target = B8BehaviourMoving(world, NDb::FACTION_BURN);
		if (rejected == 1) target->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
		if (rejected == 2) target->SetVulnerable(false);
		if (rejected == 3) creep->AddFlag(NDb::UNITFLAG_FORBIDATTACK);
		creep->OnTarget(rejected == 0 ? CPtr<PFBaseUnit>() : CPtr<PFBaseUnit>(target.GetPtr()), true);
		check(creep->IsIdle() && !IsValid(creep->GetCurrentTarget()), "invalid or forbidden creep attack is not admitted");
	}
}
