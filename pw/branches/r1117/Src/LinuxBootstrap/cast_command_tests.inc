#include "../Core/WorldCommand.h"
#include "../Core/GameCommand.h"
#include "../PF_GameLogic/HeroActions.h"
#include "../PF_GameLogic/PointersHolder.h"

/** Packed commands exercise real actor restoration, inventory and permission gates. */
static void ConsumableCommandAdmissionB10(Checks& check, PFWorld* world)
{
	CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
	NDb::Consumable* raw = new NDb::Consumable;
	raw->manaCost.sString = "15"; raw->cooldownTime.sString = "0";
	raw->maxQuantity = 5; raw->requireLineOfSight = false;
	NDb::Ptr<NDb::Consumable> db = raw;
	check(hero->TakeConsumable(db, 2), "command fixture has actual consumable inventory");
	CObj<NCore::WorldCommand> command = CreateCmdUseConsumable(hero.GetPtr(), 0, Target(hero.GetPtr()));
	check(command->CanExecute(), "live consumable command admits its exact actor and occupied slot");
	for (int slot : {-1, 1, 100})
	{
		CObj<NCore::WorldCommand> invalid = CreateCmdUseConsumable(hero.GetPtr(), slot, Target(hero.GetPtr()));
		check(!invalid->CanExecute(), "invalid or empty consumable slot fails command admission");
	}
	for (NDb::EUnitFlag flag : {NDb::UNITFLAG_FORBIDPLAYERCONTROL, NDb::UNITFLAG_INMINIGAME,
		NDb::UNITFLAG_FORBIDUSECONSUMABLES})
	{
		hero->AddFlag(flag);
		check(!command->CanExecute(), "consumable command observes live control and inventory permission");
		command->Execute(world);
		check(hero->GetMana() == 200 && hero->GetConsumable(0)->GetQuantity() == 2,
			"permission loss before execution cannot spend mana or inventory");
		hero->RemoveFlag(flag);
	}
	CObj<NCore::PackedWorldCommand> packet = new NCore::PackedWorldCommand(command,
		world->GetPointerSerialization(), 1984, 0);
	CObj<PointersHolder> missingPointers = new PointersHolder(world, 0);
	CObj<NCore::WorldCommand> missing = packet->GetWorldCommand(missingPointers);
	check(missing && !missing->CanExecute(), "restored consumable command rejects a missing actor");
	if (missing) missing->Execute(world);
	check(hero->GetMana() == 200 && hero->GetConsumable(0)->GetQuantity() == 2,
		"missing restored actor cannot substitute a world hero");
	hero->Cleanup();
}
