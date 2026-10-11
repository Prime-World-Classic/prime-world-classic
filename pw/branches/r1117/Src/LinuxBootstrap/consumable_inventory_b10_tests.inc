/** Inventory mutations retain fixed slots, bounded stacks and cached ability identity. */
static void ConsumableInventoryB10(Checks& check, PFWorld* world)
{
	CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
	NDb::Consumable* raw = new NDb::Consumable;
	raw->maxQuantity = 5; raw->manaCost.sString = "0"; raw->cooldownTime.sString = "8";
	NDb::Ptr<NDb::Consumable> db = raw;
	NDb::Consumable* other = new NDb::Consumable; other->maxQuantity = 5;
	NDb::Ptr<NDb::Consumable> otherDb = other;
	check(hero->GetSlotCount() == 32, "hero retains the original bounded inventory size");
	check(!hero->TakeConsumable(db, 6) && !hero->CanTakeConsumable(db, 6),
		"oversized stack fails admission without inventory mutation");
	check(hero->TakeConsumable(db, 2, NDb::CONSUMABLEORIGIN_DEFAULT, 0), "inventory admits its first bounded stack");
	check(hero->TakeConsumable(db, 1, NDb::CONSUMABLEORIGIN_DEFAULT, 0)
		&& hero->GetConsumable(0)->GetQuantity() == 3, "same descriptor adds to the existing stack");
	const PFConsumable* retained = hero->GetConsumable(0);
	check(!hero->TakeConsumable(otherDb, 1, NDb::CONSUMABLEORIGIN_DEFAULT, 0)
		&& hero->GetConsumable(0) == retained, "occupied slot cannot silently replace another item");
	check(!hero->TakeConsumable(db, 3, NDb::CONSUMABLEORIGIN_DEFAULT, 0), "stack overflow is not silently clamped");
	check(!hero->TakeConsumable(db, 1, NDb::CONSUMABLEORIGIN_DEFAULT, 99), "explicit invalid slot is not an append request");
	check(hero->TakeConsumable(db, 2, NDb::CONSUMABLEORIGIN_DEFAULT, 1), "second slot holds the same descriptor");
	CObj<PFConsumableAbilityData> ability = hero->GetConsumable(0)->GetAbility();
	check(ability == hero->GetConsumable(1)->GetAbility(), "same consumables share cooldown and passive ability identity");
	check(!hero->CanStackConsumables(0, 0), "self stacking cannot remove its own item");
	check(hero->CanStackConsumables(1, 0) && hero->StackConsumables(1, 0)
		&& !hero->GetConsumable(1) && hero->GetConsumable(0)->GetQuantity() == 5,
		"stack transfer uses admission before removing the source");
	ability->RestartCooldown(-1); hero->RemoveConsumable(0);
	check(hero->TakeConsumable(db, 1, NDb::CONSUMABLEORIGIN_DEFAULT, 0)
		&& hero->GetConsumable(0)->GetAbility() == ability && !hero->CanUseConsumable(0),
		"reacquiring a removed item cannot reset its cached cooldown");
	hero->RemoveConsumable(0);
	for (int slot = 0; slot < 32; ++slot)
		check(hero->TakeConsumable(db, 5, NDb::CONSUMABLEORIGIN_DEFAULT, slot), "fixed inventory slot can be filled");
	check(!hero->CanTakeConsumable(otherDb, 1) && !hero->TakeConsumable(otherDb, 1)
		&& hero->GetSlotCount() == 32, "full inventory rejects insertion instead of growing");
	hero->RemoveConsumable(0);
	NDb::Consumable* permanent = new NDb::Consumable; permanent->maxQuantity = 0;
	NDb::Ptr<NDb::Consumable> permanentDb = permanent;
	check(hero->TakeConsumable(permanentDb, 1, NDb::CONSUMABLEORIGIN_DEFAULT, 0)
		&& !hero->TakeConsumable(permanentDb, 1, NDb::CONSUMABLEORIGIN_DEFAULT, 0),
		"unspendable items occupy one slot without stacking");
	check(!hero->CanPutConsumableIntoSlot(db, std::numeric_limits<int>::max(), 1),
		"large quantities cannot overflow stack admission arithmetic");
}
