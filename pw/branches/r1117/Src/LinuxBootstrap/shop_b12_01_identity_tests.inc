/** Exact registered actor/shop pointers survive packing; missing references never substitute. */
static void ShopIdentityB12(Checks& check, PFWorld* world)
{
	for (int loss = 0; loss < 10; ++loss)
	{
		B12ShopFixture f(world);
		CObj<NCore::WorldCommand> command = f.Command();
		check(command->CanExecute(), "shop identity fixture admits the controlled live actor");
		if (loss == 1) f.hero->AddFlag(NDb::UNITFLAG_FORBIDPLAYERCONTROL);
		if (loss == 2) f.hero->AddFlag(NDb::UNITFLAG_INMINIGAME);
		if (loss == 3) f.hero->ChangeHealth(-f.hero->GetHealth());
		if (loss == 4) f.player->DetachHero();
		if (loss == 5) command->SetId(1985);
		if (loss == 6) world->UnregisterObject(f.hero.GetPtr(), false);
		if (loss == 7) world->UnregisterObject(f.shop.GetPtr(), false);
		if (loss == 6 || loss == 7) world->ProcessAddRemove();
		if (loss == 8) f.hero->DetachFromPlayer();
		if (loss == 9) f.shop->ChangeHealth(-f.shop->GetHealth());
		check(command->CanExecute() == (loss == 0), "shop admission rechecks exact live controlled identities");
		command->Execute(world); f.hero->Simulate(0);
		check((f.hero->GetConsumable(0) != nullptr) == (loss == 0) && f.hero->GetGold() == (loss == 0 ? 65 : 100),
			"shop execution cannot pay or grant after identity/control loss");
	}
	for (int missing = 0; missing < 4; ++missing)
	{
		B12ShopFixture f(world);
		CObj<NCore::WorldCommand> source = f.Command();
		CObj<NCore::PackedWorldCommand> packet = new NCore::PackedWorldCommand(source,
			world->GetPointerSerialization(), 1984, 0);
		CObj<PointersHolder> pointers = new PointersHolder(world, 0);
		if (!(missing & 1)) pointers->Add(f.hero.GetPtr(), world->GetPointerSerialization()->GetObjectID(f.hero.GetPtr()));
		if (!(missing & 2)) pointers->Add(f.shop.GetPtr(), world->GetPointerSerialization()->GetObjectID(f.shop.GetPtr()));
		CObj<NCore::WorldCommand> command = packet->GetWorldCommand(pointers);
		check(command && command->CanExecute() == (missing == 0), "packed shop order requires both exact restored identities");
		if (command) command->Execute(world);
		f.hero->Simulate(0);
		check((f.hero->GetConsumable(0) != nullptr) == (missing == 0) && f.hero->GetGold() == (missing == 0 ? 65 : 100),
			"missing packed actor/shop never substitutes a live hero or first shop");
	}
	B12ShopFixture f(world);
	CObj<NCore::IWorldBase> foreignOwner = PFWorld::CreatePFWorld();
	PFWorld* foreign = dynamic_cast<PFWorld*>(foreignOwner.GetPtr());
	check(foreign && foreign->LoadMap(world->GetMapDescription(), 0, NCore::TPlayersStartInfo(), 0,
		false, PFResourcesCollection::TalentMap()), "foreign shop fixture loads a real world");
	if (foreign)
	{
		B12ShopFixture other(foreign);
		CObj<NCore::WorldCommand> command = CreateCmdBuyConsumable(f.hero.GetPtr(), other.shop.GetPtr(), 0, -1);
		command->SetId(1984);
		check(!command->CanExecute(), "shop admission rejects cross-world endpoints");
		command->Execute(world); f.hero->Simulate(0);
		check(!f.hero->GetConsumable(0) && f.hero->GetGold() == 100, "foreign shop cannot mutate a local inventory");
		f.Command()->Execute(foreign); f.hero->Simulate(0);
		check(!f.hero->GetConsumable(0) && f.hero->GetGold() == 100, "execution world must match both serialized endpoints");
	}
	for (int uid : {-1, 1984})
	{
		B12ShopFixture bot(world); bot.player->SetUserID(uid);
		if (uid != -1) bot.player->SetIsPlaying(false);
		CObj<NCore::WorldCommand> command = bot.Command(); command->SetId(999);
		check(command->CanExecute(), "Windows bot/leaver scheduler ownership remains supported without actor substitution");
	}
}
