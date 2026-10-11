/** Authored evade delays enter original wait/recovery states with real healing. */
static void CreepRecoveryB11(Checks& check, PFWorld* world)
{
	B8BehaviourForeignWorld isolated(check);
	if (!isolated.world) return;
	world = isolated.world;
	CObj<PFCreepSpawner> spawner = CreepSpawnerB11(world, true, -1);
	CObj<PFCommonCreep> creep = CommonCreepB11(world, vector<CVec2>(), spawner.GetPtr());
	creep->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
	PFBaseUnitDamageDesc damage = DirectDamage();
	damage.amount = 250; damage.damageType = NDb::APPLICATORDAMAGETYPE_PURE;
	damage.dontAttackBack = true;
	creep->OnDamage(damage);
	check(dynamic_cast<const PFNeutralCreepWaitState*>(creep->GetCurrentState()) != nullptr,
		"damaged idle evade creep enters the original wait state");
	const auto* wait = dynamic_cast<const PFNeutralCreepWaitState*>(creep->GetCurrentState());
	check(wait && wait->GetWorld() == world, "moving-unit states retain world context for nested transitions");
	creep->Step(0.25f);
	check(dynamic_cast<const PFNeutralCreepWaitState*>(creep->GetCurrentState()) && creep->GetHealth() == 750,
		"evade wait retains its authored delay without early healing");
	creep->Step(0.25f);
	check(dynamic_cast<const PFNeutralCreepEvadeState*>(creep->GetCurrentState()) != nullptr,
		"expired evade wait enters the production recovery state");
	creep->Step(1);
	check(creep->GetHealth() == 850, "evade recovery applies authored health percentage through real healing");
	creep->Step(1); creep->Step(1);
	check(creep->GetHealth() == 1000 && creep->IsIdle(), "fully healed creep at home completes recovery");
	creep->TakeMana(40);
	creep->Step(0);
	check(dynamic_cast<const PFNeutralCreepWaitState*>(creep->GetCurrentState()) != nullptr,
		"idle energy deficit also schedules authored recovery");
	creep->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
}
