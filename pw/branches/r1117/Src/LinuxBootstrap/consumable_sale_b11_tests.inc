/** Selling refunds the original half-stack value once and preserves invalid transactions. */
static void ConsumableSaleB11(Checks& check, PFWorld* world)
{
	for (int cost : {0, 1, 35, -1, std::numeric_limits<int>::max()})
	{
		CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
		hero->AddGold(100);
		NDb::Consumable* raw = new NDb::Consumable;
		raw->maxQuantity = 5; raw->naftaCost = cost;
		NDb::Ptr<NDb::Consumable> db = raw;
		hero->TakeConsumable(db, 3, NDb::CONSUMABLEORIGIN_DEFAULT, 0);
		const bool valid = cost >= 0 && cost < 100;
		hero->SellConsumable(0);
		check((hero->GetConsumable(0) == nullptr) == valid,
			"only a valid bounded sale removes its inventory stack");
		check(hero->GetGold() == 100 + (valid ? cost * 3 / 2 : 0),
			"sale returns half of the stack value with original integer rounding");
		hero->SellConsumable(-1); hero->SellConsumable(99);
		if (valid) hero->SellConsumable(0);
		check(hero->GetGold() == 100 + (valid ? cost * 3 / 2 : 0),
			"missing or already-sold stacks cannot mint additional gold");
	}
}
