/** World population counts follow registered common creeps, not optional spawner rings. */
static void CommonCreepRegistrationB11(Checks& check, PFWorld* world)
{
	const int before = world->GetRegisteredCreepsCount();
	CObj<PFCommonCreep> first = CommonCreepB11(world);
	CObj<PFCommonCreep> second = CommonCreepB11(world);
	check(world->GetRegisteredCreepsCount() == before + 2, "world registers creeps without a spawner ring");
	first->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
	check(first->IsDead() && world->GetRegisteredCreepsCount() == before + 1,
		"death removes a nonspawner creep from the live population");
	first->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
	check(world->GetRegisteredCreepsCount() == before + 1, "repeated death cannot decrement the live population twice");
	second->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
	check(world->GetRegisteredCreepsCount() == before, "all creep deaths restore the original population count");
	CObj<PFCommonCreep> deferred = CommonCreepB11(world);
	deferred->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS | PFBaseUnit::UNITDIEFLAGS_DEFERREDDEATH);
	check(!deferred->IsDead() && world->GetRegisteredCreepsCount() == before + 1,
		"deferred death retains live creep population registration");
	PFBaseUnitDamageDesc damage = DirectDamage();
	damage.amount = deferred->GetHealth() + 1;
	damage.damageType = NDb::APPLICATORDAMAGETYPE_PURE;
	deferred->AddFlag(NDb::UNITFLAG_FORBIDREWARDS);
	deferred->OnDamage(damage);
	check(deferred->IsDead() && world->GetRegisteredCreepsCount() == before,
		"final damage-driven death unregisters even when health was zeroed before the callback");
}
