/** Real hero stepping advances cached data once, even when no item occupies a slot. */
static void ConsumableSimulationB10(Checks& check, PFWorld* world)
{
	CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
	hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
	NDb::Consumable* raw = new NDb::Consumable;
	raw->maxQuantity = 5; raw->manaCost.sString = "15"; raw->cooldownTime.sString = "8";
	NDb::Ptr<NDb::Consumable> db = raw;
	hero->TakeConsumable(db, 2, NDb::CONSUMABLEORIGIN_DEFAULT, 0);
	hero->TakeConsumable(db, 2, NDb::CONSUMABLEORIGIN_DEFAULT, 1);
	CObj<PFConsumableAbilityData> data = hero->GetConsumable(0)->GetAbility();
	CObj<B9InteractionEvents> events = new B9InteractionEvents;
	hero->AddEventListener(events.GetPtr());
	CObj<PFAbilityInstance> cast = hero->UseConsumable(0, Target(hero.GetPtr()), false);
	check(cast && events->started == 1 && events->lastStarted == cast.GetPtr(),
		"consumable use delivers its original matching ability-start event");
	check(cast && data->GetCurrentCooldown() == 8 && hero->GetMana() == 185,
		"consumable simulation begins after real once-only payment");
	check(!hero->UseConsumable(1, Target(hero.GetPtr()), false)
		&& events->started == 1 && hero->GetMana() == 185,
		"direct slot use cannot bypass shared cooldown admission");
	hero->Simulate(1);
	check(data->GetCurrentCooldown() == 7, "shared data ticks once rather than once per inventory slot");
	hero->RemoveConsumable(0); hero->RemoveConsumable(1); hero->Simulate(2);
	check(data->GetCurrentCooldown() == 5, "cached cooldown keeps advancing after inventory removal");
	hero->TakeConsumable(db, 1); hero->Simulate(5);
	check(data->GetCurrentCooldown() == 0 && hero->CanUseConsumable(0),
		"reacquired item becomes usable at the actual cooldown deadline");
	hero->TakeConsumable(db, 1, NDb::CONSUMABLEORIGIN_DEFAULT, 1);
	data->RestartCooldown(8);
	const nstl::vector<NDb::Ptr<NDb::Ability>> list;
	DropCooldownParams except(0, list, NDb::USEMODE_LISTASRULE, data, .5f, true);
	hero->DropConsumablesCooldowns(except);
	check(data->GetCurrentCooldown() == 8, "cooldown reset honors excluded ability identity");
	DropCooldownParams halve(0, list, NDb::USEMODE_LISTASRULE, nullptr, .5f, true);
	hero->DropConsumablesCooldowns(halve);
	check(data->GetCurrentCooldown() == 4, "shared consumable cooldown reset applies once rather than per slot");
	hero->RemoveEventListener(events.GetPtr());
}
