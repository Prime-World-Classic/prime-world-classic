#include <functional>

/** Mock a payment hook only; balance changes and inventory remain production engine code. */
struct B12PaymentHero : B9CastHero
{
	std::function<void()> payment;
	bool partial = false;
	B12PaymentHero(PFWorld* world, const SpawnInfo& spawn)
		: B9CastHero(world, spawn, NDb::FACTION_FREEZE) {}
	float OnRemoveGold(CPtr<PFBaseUnit> sender, float amount) override
	{
		const float paid = PFBaseUnit::OnRemoveGold(sender, partial ? amount / 2 : amount);
		if (payment) payment();
		return paid;
	}
};

/** Observe the real obtained event, including callback mutations and nested commands. */
struct B12ShopEvents : CObjectBase, PFBaseUnitEventListener
{
	OBJECT_BASIC_METHODS(B12ShopEvents);
public:
	std::function<void()> obtained;
	int calls = 0;
	unsigned int OnEvent(const PFBaseUnitEvent* event) override
	{
		if (event->GetType() == NDb::BASEUNITEVENT_CONSUMABLEOBTAINED)
		{
			++calls;
			if (calls == 1 && obtained) obtained();
		}
		return 0;
	}
};

/** Bound all authored indices and retain explicit-slot/inventory/funds eligibility. */
static void ShopPurchaseBoundsB12(Checks& check, PFWorld* world)
{
	for (int index : {-1, 1, std::numeric_limits<int>::max()})
	{
		B12ShopFixture f(world); CObj<NCore::WorldCommand> command = f.Command(index);
		check(!command->CanExecute(), "shop index is bounded before descriptor lookup");
		command->Execute(world); f.hero->Simulate(0);
		check(!f.hero->GetConsumable(0) && f.hero->GetGold() == 100, "invalid shop index cannot spend or grant");
	}
	for (int slot : {-2, std::numeric_limits<int>::min(), 32, std::numeric_limits<int>::max()})
	{
		B12ShopFixture f(world); CObj<NCore::WorldCommand> command = f.Command(0, slot);
		check(!command->CanExecute(), "only minus one or an existing inventory slot is admitted");
		command->Execute(world); f.hero->Simulate(0);
		check(!f.hero->GetConsumable(0) && f.hero->GetGold() == 100, "malformed slot cannot encode an actor or auto-select a slot");
	}
	for (int loss = 0; loss < 11; ++loss)
	{
		B12ShopFixture f(world); CObj<NCore::WorldCommand> command = f.Command(0, 0);
		check(command->CanExecute(), "authored shop transaction starts eligible");
		if (loss == 0) f.hero->TakeGold(100);
		if (loss == 1) const_cast<NDb::Consumable*>(f.item.GetPtr())->naftaCost = -1;
		if (loss == 2) const_cast<NDb::Consumable*>(f.item.GetPtr())->naftaCost = 101;
		if (loss == 3) const_cast<NDb::ConsumablesShop*>(f.stock.GetPtr())->items[0] = nullptr;
		if (loss == 4) f.hero->TakeConsumable(f.item, 5, NDb::CONSUMABLEORIGIN_DEFAULT, 0);
		if (loss == 5)
		{
			NDb::Ptr<NDb::Consumable> other = new NDb::Consumable;
			f.hero->TakeConsumable(other, 1, NDb::CONSUMABLEORIGIN_DEFAULT, 0);
		}
		if (loss == 6) f.hero->AddFlag(NDb::UNITFLAG_FORBIDINTERACT);
		if (loss == 7) f.shop->Hide(true);
		if (loss == 8) const_cast<NDb::ConsumablesShop*>(f.stock.GetPtr())->interactionRadius = -1;
		if (loss == 9) const_cast<NDb::ConsumablesShop*>(f.stock.GetPtr())->interactionRadius = std::numeric_limits<float>::quiet_NaN();
		if (loss == 10) const_cast<NDb::ConsumablesShop*>(f.stock.GetPtr())->interactionRadius = std::numeric_limits<float>::infinity();
		const PFConsumable* before = f.hero->GetConsumable(0);
		const int quantity = before ? before->GetQuantity() : 0;
		check(!command->CanExecute(), "shop admission rechecks authored inventory, funds and interaction eligibility");
		command->Execute(world); f.hero->Simulate(0);
		check(f.hero->GetConsumable(0) == before && (!before || before->GetQuantity() == quantity)
			&& f.hero->GetGold() == (loss == 0 ? 0 : 100), "late shop rejection preserves inventory and remaining funds");
	}
	for (int slot : {-1, 0, 31})
	{
		B12ShopFixture f(world); CObj<NCore::WorldCommand> command = f.Command(0, slot);
		check(command->CanExecute(), "valid explicit and automatic slots remain supported");
		command->Execute(world); f.hero->Simulate(0);
		check(f.hero->GetConsumable(slot < 0 ? 0 : slot) && f.hero->GetGold() == 65,
			"successful purchase uses exactly the requested slot and authored price");
	}
	B12ShopFixture distant(world, 15);
	distant.Command()->Execute(world); distant.hero->Simulate(0);
	check(!distant.hero->GetConsumable(0) && distant.hero->GetGold() == 100,
		"native always-near shop stub cannot grant outside the authored interaction radius");
	B12ShopFixture full(world);
	for (int slot = 0; slot < full.hero->GetSlotCount(); ++slot)
		full.hero->TakeConsumable(full.item, 5, NDb::CONSUMABLEORIGIN_DEFAULT, slot);
	check(!full.Command()->CanExecute(), "full inventory fails automatic-slot purchase admission");
}

/** Payment hooks can invalidate the quote or endpoints before any inventory callback. */
static void ShopPaymentCallbacksB12(Checks& check, PFWorld* world)
{
	for (int mutation = 0; mutation < 7; ++mutation)
	{
		B12ShopFixture f(world);
		PFBaseHero::SpawnInfo spawn; spawn.pHero = dynamic_cast<const NDb::Hero*>(f.hero->GetDbHero());
		spawn.placement = Placement(CVec3(5, 5, 0), QNULL, CVec3(1, 1, 1));
		CObj<B12PaymentHero> hero = new B12PaymentHero(world, spawn);
		f.hero = hero; hero->AttachToPlayer(f.player.GetPtr()); hero->AddGold(100);
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		hero->partial = mutation == 0;
		hero->payment = [&]() {
			if (mutation == 1) hero->AddFlag(NDb::UNITFLAG_FORBIDPLAYERCONTROL);
			if (mutation == 2) const_cast<NDb::Consumable*>(f.item.GetPtr())->naftaCost = 80;
			if (mutation == 3) const_cast<NDb::ConsumablesShop*>(f.stock.GetPtr())->items[0] = new NDb::Consumable;
			if (mutation == 4) { world->UnregisterObject(f.shop.GetPtr(), false); world->ProcessAddRemove(); }
			if (mutation == 5) hero->TakeConsumable(f.item, 5, NDb::CONSUMABLEORIGIN_DEFAULT, 0);
			if (mutation == 6) hero->TeleportTo(CVec2(20, 5), true, false);
		};
		CObj<B12ShopEvents> events = new B12ShopEvents; hero->AddEventListener(events.GetPtr());
		CObj<NCore::WorldCommand> command = f.Command(0, 0);
		check(command->CanExecute(), "callback shop fixture admits before payment mutation");
		command->Execute(world); hero->Simulate(0);
		check(hero->GetGold() == 100 && events->calls == 0,
			"partial payment or callback-invalidated purchase refunds only its debit without obtained event");
		check(mutation == 5 ? hero->GetConsumable(0) && hero->GetConsumable(0)->GetQuantity() == 5 : !hero->GetConsumable(0),
			"payment callback changes are retained without an extra item grant");
		hero->RemoveEventListener(events.GetPtr()); hero->payment = nullptr;
	}
	for (int mutation = 0; mutation < 3; ++mutation)
	{
		B12ShopFixture f(world);
		if (mutation == 2) f.hero->TakeGold(65);
		CObj<B12ShopEvents> events = new B12ShopEvents;
		int balanceAtEvent = -1;
		events->obtained = [&]() {
			balanceAtEvent = f.hero->GetGold();
			if (mutation == 0) f.hero->TakeGold(f.hero->GetGold());
			if (mutation == 1) const_cast<NDb::Consumable*>(f.item.GetPtr())->naftaCost = 80;
			if (mutation == 2) { CObj<NCore::WorldCommand> nested = f.Command(); nested->Execute(world); }
		};
		f.hero->AddEventListener(events.GetPtr()); f.Command()->Execute(world); f.hero->Simulate(0); f.hero->Simulate(0);
		check(balanceAtEvent == (mutation == 2 ? 0 : 65), "obtained callbacks see the already-paid quoted price");
		check(events->calls == 1 && f.hero->GetConsumable(0) && f.hero->GetConsumable(0)->GetQuantity() == 1,
			"reentrant obtained callback cannot reuse unpaid funds for a second item");
		check(f.hero->GetGold() == (mutation == 1 ? 65 : 0), "obtained callback cannot change the committed purchase price");
		f.hero->RemoveEventListener(events.GetPtr());
	}
}
