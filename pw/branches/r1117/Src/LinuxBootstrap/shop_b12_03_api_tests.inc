/** Expose geometry injection only; direct queries still use the real hero/shop implementations. */
struct B12ShopQueryHero : B9CastHero
{
	B12ShopQueryHero(PFWorld* world, const SpawnInfo& spawn)
		: B9CastHero(world, spawn, NDb::FACTION_FREEZE) {}
	void Position(const CVec3& value) { position = value; }
};

struct B12ShopQueryEndpoint : PFShop
{
	B12ShopQueryEndpoint(PFWorld* world, const NDb::AdvMapObject& object) : PFShop(world, object) {}
	void Position(const CVec3& value) { position = value; }
};

/** Reuse B10/B12 authored data; never send malformed coordinates through pathfinding or war fog. */
struct B12ShopApiFixture : B12ShopFixture
{
	CObj<B12ShopQueryHero> actor;
	CObj<B12ShopQueryEndpoint> endpoint;
	explicit B12ShopApiFixture(PFWorld* world) : B12ShopFixture(world)
	{
		PFBaseHero::SpawnInfo spawn; spawn.pHero = dynamic_cast<const NDb::Hero*>(hero->GetDbHero());
		spawn.placement = Placement(CVec3(5, 5, 0), QNULL, CVec3(1, 1, 1));
		actor = new B12ShopQueryHero(world, spawn); hero = actor;
		hero->AttachToPlayer(player.GetPtr()); hero->AddGold(100);
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		NDb::Shop* db = new NDb::Shop;
		db->shop = stock; db->faction = NDb::FACTION_FREEZE;
		db->stats = hero->GetDbHero()->stats; db->targetingParams = hero->GetDbHero()->targetingParams;
		NDb::AdvMapObject object; object.gameObject = db;
		object.offset = CPlacement(CVec3(5, 5, 0), QNULL, CVec3(1, 1, 1));
		endpoint = new B12ShopQueryEndpoint(world, object); shop = endpoint;
	}
	~B12ShopApiFixture()
	{
		actor->Position(CVec3(5, 5, 0)); endpoint->Position(CVec3(5, 5, 0));
	}
};

/** Live proximity is inclusive 3D center distance, independent of building/hero collision size. */
static void ShopApiGeometryB12(Checks& check, PFWorld* world)
{
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();
	const float high = std::numeric_limits<float>::max();
	struct Example { CVec3 actor; CVec3 shop; float radius; bool near; const char* label; };
	const Example examples[] = {
		{CVec3(5, 5, 0), CVec3(5, 5, 0), 0, true, "zero radius admits equal centers"},
		{CVec3(5.125f, 5, 0), CVec3(5, 5, 0), 0, false, "zero radius rejects separated centers"},
		{CVec3(8, 5, 0), CVec3(5, 5, 0), 3, true, "horizontal boundary is inclusive"},
		{CVec3(std::nextafter(8.f, inf), 5, 0), CVec3(5, 5, 0), 3, false, "one float step outside radius is rejected"},
		{CVec3(std::nextafter(8.f, 5.f), 5, 0), CVec3(5, 5, 0), 3, true, "one float step inside radius is accepted"},
		{CVec3(5, 8, 0), CVec3(5, 5, 0), 3, true, "both map-plane axes use the same boundary"},
		{CVec3(5, 5, 3), CVec3(5, 5, 0), 3, true, "vertical boundary is inclusive"},
		{CVec3(5, 5, 3.125f), CVec3(5, 5, 0), 3, false, "vertical separation participates in shop proximity"},
		{CVec3(8, 5, 1), CVec3(5, 5, 0), 3, false, "planar reach alone cannot authorize a shop query"},
		{CVec3(7, 7, 1), CVec3(5, 5, 0), 3, true, "combined XYZ distance reaches the exact boundary"},
		{CVec3(5, 5, 0), CVec3(8, 5, 0), 3, true, "center distance is symmetric"},
		{CVec3(5, 5, 0), CVec3(5, 5, 0), -1, false, "negative authored radius fails closed"},
		{CVec3(5, 5, 0), CVec3(5, 5, 0), nan, false, "NaN authored radius fails closed"},
		{CVec3(5, 5, 0), CVec3(5, 5, 0), inf, false, "infinite authored radius fails closed"},
		{CVec3(nan, 5, 0), CVec3(5, 5, 0), 3, false, "NaN actor X fails closed"},
		{CVec3(5, nan, 0), CVec3(5, 5, 0), 3, false, "NaN actor Y fails closed"},
		{CVec3(5, 5, nan), CVec3(5, 5, 0), 3, false, "NaN actor Z fails closed"},
		{CVec3(5, 5, 0), CVec3(nan, 5, 0), 3, false, "NaN shop X fails closed"},
		{CVec3(5, 5, 0), CVec3(5, nan, 0), 3, false, "NaN shop Y fails closed"},
		{CVec3(5, 5, 0), CVec3(5, 5, nan), 3, false, "NaN shop Z fails closed"},
		{CVec3(inf, 5, 0), CVec3(5, 5, 0), 3, false, "infinite actor position fails closed"},
		{CVec3(5, 5, 0), CVec3(5, 5, -inf), 3, false, "infinite shop position fails closed"},
		{CVec3(high, high, high), CVec3(-high, -high, -high), high, false, "large finite distance cannot overflow into false reach"},
		{CVec3(high, 0, 0), CVec3(0, 0, 0), high, true, "large finite boundary remains representable"}
	};
	for (const auto& example : examples)
	{
		B12ShopApiFixture f(world);
		f.actor->Position(example.actor); f.endpoint->Position(example.shop);
		const_cast<NDb::ConsumablesShop*>(f.stock.GetPtr())->interactionRadius = example.radius;
		f.hero->SetObjectSize(100); f.shop->SetObjectSize(100);
		check(f.shop->IsHeroNear(f.hero.GetPtr()) == example.near, example.label);
		check(f.shop->CanBuyConsumable(f.hero.GetPtr(), 0) == example.near,
			"direct UI affordability query uses the same live shop geometry");
		check(f.Command()->CanExecute() == example.near,
			"live command admission agrees with the authoritative shop radius API");
		check(f.hero->GetGold() == 100 && !f.hero->GetConsumable(0) && !f.hero->HasQueuedStates(),
			"direct eligibility queries are read-only and never invent a movement order");
	}
}

/** Dead allies retain the original remote query policy; commands still require a live actor. */
static void ShopApiEligibilityB12(Checks& check, PFWorld* world)
{
	for (bool dead : {false, true})
	for (bool allied : {false, true})
	for (bool distant : {false, true})
	{
		B12ShopApiFixture f(world);
		if (!allied) f.shop->ChangeFaction(NDb::FACTION_BURN);
		if (distant) f.endpoint->Position(CVec3(100, 100, 10));
		if (dead) f.hero->ChangeHealth(-f.hero->GetHealth());
		const bool eligible = dead ? allied : !distant;
		check(f.shop->IsHeroNear(f.hero.GetPtr()) == eligible,
			"live distance and dead same-faction policy follow separate original branches");
		check(f.shop->CanBuyConsumable(f.hero.GetPtr(), 0) == eligible,
			"direct shop query preserves remote dead-ally eligibility without admitting dead enemies");
		check(f.Command()->CanExecute() == (!dead && eligible),
			"command ownership/liveness policy is stricter than the read-only shop API");
		if (dead)
		{
			f.Command()->Execute(world);
			check(f.hero->GetGold() == 100 && !f.hero->GetConsumable(0),
				"dead allied UI eligibility cannot bypass the command's live actor requirement");
		}
	}
	B12ShopFixture f(world);
	check(!f.shop->IsHeroNear(nullptr) && !f.shop->CanBuyConsumable(nullptr, 0), "null hero fails both direct queries");
	CObj<PFShop> missing = new PFShop;
	check(!missing->IsHeroNear(f.hero.GetPtr()) && !missing->CanBuyConsumable(f.hero.GetPtr(), 0),
		"missing shop descriptor/world cannot authorize direct queries");
	for (int index : {std::numeric_limits<int>::min(), -1, 1, std::numeric_limits<int>::max()})
		check(!f.shop->CanBuyConsumable(f.hero.GetPtr(), index), "direct stock lookup bounds malformed item indices");
	for (int cost : {-1, std::numeric_limits<int>::min(), 0, 35, 100, 101, std::numeric_limits<int>::max()})
	{
		const_cast<NDb::Consumable*>(f.item.GetPtr())->naftaCost = cost;
		check(f.shop->CanBuyConsumable(f.hero.GetPtr(), 0) == (cost >= 0 && cost <= 100),
			"direct shop query requires a nonnegative affordable current price");
	}
	const_cast<NDb::Consumable*>(f.item.GetPtr())->naftaCost = 35;
	f.hero->TakeGold(100);
	check(!f.shop->CanBuyConsumable(f.hero.GetPtr(), 0), "direct query rechecks depleted funds");
	f.hero->AddGold(100);
	check(f.shop->CanBuyConsumable(f.hero.GetPtr(), 0), "direct query observes restored funds without a cached answer");
	for (int slot = 0; slot < f.hero->GetSlotCount(); ++slot)
		f.hero->TakeConsumable(f.item, 5, NDb::CONSUMABLEORIGIN_DEFAULT, slot);
	check(f.shop->CanBuyConsumable(f.hero.GetPtr(), 0) && !f.hero->CanTakeConsumable(f.item, 1),
		"full inventory does not mislabel original UI affordability as insufficient prime");
	check(!f.Command()->CanExecute(), "command inventory admission remains bounded despite UI affordability");
	f.Command()->Execute(world);
	check(f.hero->GetGold() == 100 && f.hero->GetConsumable(0)->GetQuantity() == 5,
		"full-inventory execution remains a no-op after direct API parity");
	const_cast<NDb::ConsumablesShop*>(f.stock.GetPtr())->items[0] = nullptr;
	check(!f.shop->CanBuyConsumable(f.hero.GetPtr(), 0), "missing authored item fails direct query");
	const_cast<NDb::ConsumablesShop*>(f.stock.GetPtr())->items.clear();
	check(!f.shop->CanBuyConsumable(f.hero.GetPtr(), 0), "removed authored stock fails direct query");
	CObj<NCore::IWorldBase> foreignOwner = PFWorld::CreatePFWorld();
	PFWorld* foreign = dynamic_cast<PFWorld*>(foreignOwner.GetPtr());
	const bool loaded = foreign && foreign->LoadMap(world->GetMapDescription(), 0, NCore::TPlayersStartInfo(), 0,
		false, PFResourcesCollection::TalentMap());
	check(loaded, "shop API foreign-world fixture loads independently");
	if (loaded)
	{
		B12ShopFixture local(world); B12ShopFixture other(foreign);
		check(!local.shop->IsHeroNear(other.hero.GetPtr()) && !local.shop->CanBuyConsumable(other.hero.GetPtr(), 0),
			"equal coordinates and faction never authorize a hero from another world");
		other.hero->ChangeHealth(-other.hero->GetHealth());
		check(!local.shop->IsHeroNear(other.hero.GetPtr()) && !local.shop->CanBuyConsumable(other.hero.GetPtr(), 0),
			"dead allied policy does not cross world ownership boundaries");
	}
}
