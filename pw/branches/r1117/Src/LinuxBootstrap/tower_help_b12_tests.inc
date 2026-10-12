/** Real AI-world towers consume the existing hero help producer and authored priority clocks. */
static void TowerHelpB12(Checks& check)
{
	B8BehaviourForeignWorld local(check), foreign(check); if (!local.world || !foreign.world) return;
	CObj<B9CastHero> caller = B9MaleHero(local.world, NDb::FACTION_FREEZE);
	CObj<B9CastHero> enemy = B9MaleHero(local.world, NDb::FACTION_BURN);
	auto params = WeightedParamsB12(); auto* raw = const_cast<NDb::UnitTargetingParameters*>(params.GetPtr());
	raw->nearbyRange = 12; raw->maxTimeFromAttack = 2; raw->attackerNearbyHeroWeight = 100;
	auto makeTower = [&](PFWorld* world, NDb::EFaction faction, float x) {
		auto* db = new NDb::Tower; db->stats = caller->DbUnitDesc()->stats; db->faction = faction; db->targetingParams = params;
		NDb::AdvMapObject object; object.gameObject = db; object.offset = CPlacement(CVec3(x, 7, 0), QNULL, CVec3(1, 1, 1));
		CObj<PFTower> tower = new PFTower(world, object); tower->GetStat(NDb::STAT_RANGE)->SetCoreValue(6);
		return tower;
	};
	auto near = makeTower(local.world, NDb::FACTION_FREEZE, 5), middle = makeTower(local.world, NDb::FACTION_FREEZE, 15);
	auto far = makeTower(local.world, NDb::FACTION_FREEZE, 30), hostile = makeTower(local.world, NDb::FACTION_BURN, 5);
	auto elsewhere = makeTower(foreign.world, NDb::FACTION_FREEZE, 5);
	caller->CallForTowerHelp(enemy.GetPtr());
	check(near->GetCurrentTarget() == enemy.GetPtr() && near->IsPriorityTarget(enemy.GetPtr()), "tower in attack range immediately targets and retains the attacking hero");
	check(!middle->GetCurrentTarget() && middle->IsPriorityTarget(enemy.GetPtr()), "nearby out-of-attack-range tower remembers attacker without immediate retarget");
	check(!far->IsPriorityTarget(enemy.GetPtr()) && !hostile->IsPriorityTarget(enemy.GetPtr()) && !elsewhere->IsPriorityTarget(enemy.GetPtr()),
		"distant hostile and foreign-world towers do not receive help priority");
	near->UpdatePriorityTargets(2); middle->UpdatePriorityTargets(2); near->DropTarget();
	enemy->AddFlag(NDb::UNITFLAG_FORBIDAUTOTARGETME); caller->CallForTowerHelp(enemy.GetPtr());
	check(!near->GetCurrentTarget() && !near->IsPriorityTarget(enemy.GetPtr()), "forbid-auto-target attacker cannot provoke tower help");
	enemy->RemoveFlag(NDb::UNITFLAG_FORBIDAUTOTARGETME);
	CObj<B9CastHero> current = B9MaleHero(local.world, NDb::FACTION_BURN); near->AssignTarget(current.GetPtr(), false);
	caller->CallForTowerHelp(enemy.GetPtr());
	check(near->GetCurrentTarget() == current.GetPtr() && !near->IsPriorityTarget(enemy.GetPtr()), "tower already attacking an unpaired true hero preserves its target and priority");
	near->DropTarget();
	auto alpha = WeightedUnitCreateB12(local.world, params, NDb::FACTION_BURN, NDb::UNITTYPE_SUMMON);
	alpha->SetMaster(enemy.GetPtr()); enemy->SetAlphaSummon(alpha.GetPtr());
	caller->CallForTowerHelp(enemy.GetPtr());
	check(near->GetCurrentTarget() == alpha.GetPtr() && near->IsPriorityTarget(enemy.GetPtr()), "tower prefers the attacker alpha while priority retains original master identity");
	near->DropTarget(); near->UpdatePriorityTargets(2); enemy->SetAlphaSummon(nullptr); alpha->SetMaster(nullptr);
	if (!caller->GetBehaviour()) caller->AddBehaviourOnTop(new PFHeroBehaviour(caller.GetPtr()));
	PFBaseUnit::DamageDesc hit;
	hit.pSender = enemy.GetPtr(); hit.amount = 10; hit.damageType = NDb::APPLICATORDAMAGETYPE_PURE;
	hit.flags = 0; hit.damageMode = NDb::DAMAGEMODE_ZERO; hit.dontAttackBack = true; hit.pDealerApplicator = nullptr;
	check(caller->OnDamage(hit) == 10 && caller->GetHealth() == 990 && near->GetCurrentTarget() == enemy.GetPtr()
		&& near->IsPriorityTarget(enemy.GetPtr()), "real committed hero damage triggers tower help even when personal retaliation is disabled");
	near->DropTarget(); near->UpdatePriorityTargets(1);
	check(near->IsPriorityTarget(enemy.GetPtr()), "help priority survives before its authored expiry boundary");
	near->UpdatePriorityTargets(1);
	check(!near->IsPriorityTarget(enemy.GetPtr()), "help priority expires through the existing combat clock");
	caller->CallForTowerHelp(caller.GetPtr()); caller->CallForTowerHelp(nullptr);
	check(!near->GetCurrentTarget() && !near->IsPriorityTarget(caller.GetPtr()), "invalid or allied attacker cannot provoke help");
	local.world->UnregisterObject(enemy.GetPtr(), false); local.world->ProcessAddRemove();
	caller->CallForTowerHelp(enemy.GetPtr());
	check(!near->GetCurrentTarget() && !near->IsPriorityTarget(enemy.GetPtr()), "unregistered attackers cannot provoke help through stale pointers");
	caller->Cleanup(); enemy->Cleanup(); current->Cleanup();
}
