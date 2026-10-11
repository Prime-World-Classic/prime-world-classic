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

/** The live unit queue approaches before payment and can cancel an uncommitted cast. */
static void ConsumableCommandQueueB10(Checks& check, PFWorld* world)
{
	for (int policy = 0; policy < 3; ++policy)
	{
		CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		hero->SetMoveFlags(MOVE_FLAG_NO_COLLIDE); hero->SetUnitSpeed(4);
		NDb::Consumable* raw = new NDb::Consumable;
		raw->useRange.sString = "4"; raw->manaCost.sString = "70";
		raw->cooldownTime.sString = "0"; raw->maxQuantity = 5;
		raw->requireLineOfSight = false; raw->isUnitFreeAfterCast = true;
		NDb::Ptr<NDb::Consumable> db = raw;
		hero->TakeConsumable(db, 2);
		CObj<PFConsumable> item = const_cast<PFConsumable*>(hero->GetConsumable(0));
		CObj<NCore::WorldCommand> source = CreateCmdUseConsumable(hero.GetPtr(), 0, Target(CVec3(15, 5, 0)));
		CObj<NCore::PackedWorldCommand> packet = new NCore::PackedWorldCommand(source,
			world->GetPointerSerialization(), 1984, 0);
		CObj<NCore::WorldCommand> command = packet->GetWorldCommand(world->GetPointerSerialization());
		check(command && command->CanExecute(), "packed consumable command retains its exact live actor");
		if (command) command->Execute(world);
		check(hero->HasQueuedStates() && hero->GetMana() == 200 && item->GetQuantity() == 2,
			"consumable command queues approach without instant payment");
		hero->Simulate(0);
		check(hero->IsMoving() && hero->GetMana() == 200 && item->GetQuantity() == 2,
			"real unit simulation enters consumable approach before payment");
		if (policy == 1) hero->DoStop();
		if (policy == 2) hero->RemoveConsumable(0);
		for (int i = 0; i < 100 && item->GetQuantity() == 2; ++i) hero->Simulate(0.1f);
		check(hero->GetMana() == (policy == 0 ? 130.f : 200.f) && item->GetQuantity() == (policy == 0 ? 1 : 2),
			"queued consumable pays once at arrival and never after stop or inventory removal");
		check(policy != 0 || hero->IsPositionInRange(CVec2(15, 5), 4),
			"queued consumable casts only inside the authored range");
		hero->DoStop();
	}
}

/** Restored missing targets and nonfinite positions must never become a self cast. */
static void ConsumableCommandTargetsB10(Checks& check, PFWorld* world)
{
	CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
	NDb::Consumable* raw = new NDb::Consumable;
	raw->manaCost.sString = "15"; raw->cooldownTime.sString = "0";
	raw->maxQuantity = 5; raw->requireLineOfSight = false;
	NDb::Ptr<NDb::Consumable> db = raw; hero->TakeConsumable(db, 2);
	CObj<PFBaseUnit> unit = Unit(world, NDb::FACTION_FREEZE, 20);
	CObj<NCore::WorldCommand> source = CreateCmdUseConsumable(hero.GetPtr(), 0, Target(unit.GetPtr()));
	check(source->CanExecute(), "consumable command admits an exact registered object target");
	CObj<NCore::PackedWorldCommand> packet = new NCore::PackedWorldCommand(source,
		world->GetPointerSerialization(), 1984, 0);
	CObj<PointersHolder> missingPointers = new PointersHolder(world, 0);
	missingPointers->Add(hero.GetPtr(), world->GetPointerSerialization()->GetObjectID(hero.GetPtr()));
	CObj<NCore::WorldCommand> missing = packet->GetWorldCommand(missingPointers);
	check(missing && !missing->CanExecute(), "consumable command rejects a missing restored target");
	if (missing) missing->Execute(world);
	hero->Simulate(0);
	check(hero->GetMana() == 200 && hero->GetConsumable(0)->GetQuantity() == 2 && !hero->HasQueuedStates(),
		"missing consumable target cannot queue or pay for a fallback self cast");
	hero->DoStop();
	for (CVec3 point : {CVec3(std::numeric_limits<float>::quiet_NaN(), 5, 0),
		CVec3(5, std::numeric_limits<float>::infinity(), 0),
		CVec3(5, 5, std::numeric_limits<float>::infinity()), CVec3(-1, 5, 0),
		CVec3(world->GetMapSize().x, 5, 0)})
	{
		CObj<NCore::WorldCommand> invalid = CreateCmdUseConsumable(hero.GetPtr(), 0, Target(point));
		check(!invalid->CanExecute(), "consumable command rejects malformed or off-map positions");
		invalid->Execute(world);
		check(!hero->HasQueuedStates() && !hero->GetCurrentState() && hero->GetMana() == 200,
			"invalid position cannot enqueue an order even if execution is called directly");
		hero->DoStop();
	}
	CObj<NCore::IWorldBase> foreignOwner = PFWorld::CreatePFWorld();
	PFWorld* foreignWorld = dynamic_cast<PFWorld*>(foreignOwner.GetPtr());
	const bool loaded = foreignWorld && foreignWorld->LoadMap(world->GetMapDescription(), 0,
		NCore::TPlayersStartInfo(), 0, false, PFResourcesCollection::TalentMap());
	check(loaded, "foreign target fixture has a real independently loaded world");
	if (loaded)
	{
		CObj<PFBaseUnit> foreign = Unit(foreignWorld, NDb::FACTION_FREEZE, 20);
		CObj<NCore::WorldCommand> command = CreateCmdUseConsumable(hero.GetPtr(), 0, Target(foreign.GetPtr()));
		check(!command->CanExecute(), "foreign target identity is not replaced by a matching local object id");
		command->Execute(world);
		check(!hero->HasQueuedStates() && hero->GetMana() == 200,
			"direct execution cannot queue a consumable for another world's object");
	}
}
