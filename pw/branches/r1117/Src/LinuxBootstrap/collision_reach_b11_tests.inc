/** Actual placement/resolution must agree with the engine's diameter-based attack reach. */
static void CollisionReachB11(Checks& check, PFWorld* world)
{
	B8BehaviourForeignWorld isolated(check);
	if (!isolated.world) return;
	world = isolated.world;
	CObj<PFCommonCreep> first = CommonCreepB11(world);
	CObj<PFCommonCreep> second = CommonCreepB11(world);
	first->SetObjectSize(4); second->SetObjectSize(4);
	first->TeleportTo(CVec2(10, 10), false, false);
	second->TeleportTo(CVec2(15, 10), false, false);
	first->SetMoveFlags(0); second->SetMoveFlags(0);
	check(first->CanOccupyPosition(CVec2(10, 10)), "two four-meter-diameter units fit five meters apart");
	check(first->CanOccupyPosition(CVec2(11, 10)), "tangent unit footprints are not overlapping");
	check(!first->CanOccupyPosition(CVec2(12, 10)), "actual overlapping footprints remain blocked");
	vector<PFBaseMovingUnit*> units;
	units.push_back(first.GetPtr()); units.push_back(second.GetPtr());
	CObj<CollisionResolver> resolver = new CollisionResolver(world);
	resolver->Resolve(units, .1f);
	check(first->GetPosition().AsVec2D() == CVec2(10, 10) && second->GetPosition().AsVec2D() == CVec2(15, 10),
		"collision resolution does not separate nonoverlapping units using doubled radii");
	check(first->IsTargetInAttackRange(second.GetPtr(), true), "resolved melee units remain within authored center-to-edge reach");
	second->ChangeFaction(NDb::FACTION_BURN);
	first->AssignTarget(second.GetPtr(), true);
	const float health = second->GetHealth();
	check(first->DoAttack(false) && second->GetHealth() == health - 13,
		"correctly separated melee units deliver real authored attack damage");
	first->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
	second->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
}
