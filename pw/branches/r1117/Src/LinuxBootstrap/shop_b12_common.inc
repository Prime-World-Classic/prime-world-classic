#include "PF_GameLogic/PFPlayer.h"

/** Mock authored shop data around the existing B10 real hero/inventory fixture. */
struct B12ShopFixture
{
	CObj<B9CastHero> hero;
	CObj<PFPlayer> player;
	NDb::Ptr<NDb::Consumable> item;
	NDb::Ptr<NDb::ConsumablesShop> stock;
	CObj<PFShop> shop;

	explicit B12ShopFixture(PFWorld* world, float x = 5)
		: hero(B9MaleHero(world, NDb::FACTION_FREEZE))
	{
		player = new PFPlayer(world, 77, NCore::ETeam::Team1, NCore::ETeam::Team1,
			1984, NCore::ESex::Male, false, 0, false, false);
		hero->AttachToPlayer(player.GetPtr());
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		hero->SetMoveFlags(MOVE_FLAG_NO_COLLIDE); hero->SetUnitSpeed(4); hero->AddGold(100);
		NDb::Consumable* consumable = new NDb::Consumable;
		consumable->maxQuantity = 5; consumable->naftaCost = 35;
		consumable->manaCost.sString = "0"; consumable->cooldownTime.sString = "0";
		item = consumable;
		NDb::ConsumablesShop* inventory = new NDb::ConsumablesShop;
		inventory->interactionRadius = 3; inventory->items.push_back(item); stock = inventory;
		NDb::Shop* db = new NDb::Shop;
		db->shop = stock; db->faction = NDb::FACTION_FREEZE;
		db->stats = hero->GetDbHero()->stats; db->targetingParams = hero->GetDbHero()->targetingParams;
		NDb::AdvMapObject object; object.gameObject = db;
		object.offset = CPlacement(CVec3(x, 5, 0), QNULL, CVec3(1, 1, 1));
		shop = new PFShop(world, object);
	}

	~B12ShopFixture() { hero->DoStop(); }
	CObj<NCore::WorldCommand> Command(int index = 0, int slot = -1) const
	{
		CObj<NCore::WorldCommand> command = CreateCmdBuyConsumable(hero.GetPtr(), shop.GetPtr(), index, slot);
		command->SetId(1984);
		return command;
	}
};
