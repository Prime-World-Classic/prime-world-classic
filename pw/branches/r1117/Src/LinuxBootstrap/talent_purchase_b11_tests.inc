/** Purchase admission and execution retain one live actor, authored price and exact slot. */
static void TalentPurchaseB11(Checks& check, PFWorld* world)
{
	for (int loss = 0; loss < 5; ++loss)
	{
		CObj<B10CommandHero> hero = CommandHeroB10(world);
		NDb::Talent* raw = new NDb::Talent;
		raw->naftaCost = 35; raw->minLevel = 0; raw->devPoints = 1;
		NDb::Ptr<NDb::Talent> db = raw;
		hero->InstallTalent(db, false); hero->AddGold(100);
		CObj<NCore::WorldCommand> command = CreateCmdActivateTalent(hero.GetPtr(), 0, 0);
		check(hero->CanActivateTalent(0, 0) == ETalentActivation::Ok && command->CanExecute(),
			"purchase fixture admits an unbought authored talent");
		if (loss == 1) hero->AddFlag(NDb::UNITFLAG_FORBIDPLAYERCONTROL);
		if (loss == 2) hero->AddFlag(NDb::UNITFLAG_INMINIGAME);
		if (loss == 3) hero->TakeGold(100);
		if (loss == 4) hero->ChangeHealth(-hero->GetHealth());
		check(command->CanExecute() == (loss == 0), "purchase admission rechecks live control and authored affordability");
		command->Execute(world);
		check(hero->GetTalent(0, 0)->IsActivated() == (loss == 0), "purchase execution rejects late actor or resource loss");
		check(hero->GetGold() == (loss == 0 ? 65 : loss == 3 ? 0 : 100), "only successful talent purchase pays its authored price");
		if (loss == 0)
		{
			check(!command->CanExecute(), "already bought talent is not admitted for another purchase");
			command->Execute(world);
			check(hero->GetGold() == 65, "duplicate purchase cannot pay twice");
		}
	}
	CObj<B10CommandHero> hero = CommandHeroB10(world);
	NDb::Talent* raw = new NDb::Talent; raw->naftaCost = 35;
	NDb::Ptr<NDb::Talent> db = raw; hero->InstallTalent(db, false); hero->AddGold(100);
	for (const auto& slot : {nstl::pair<int, int>(-1, 0), {0, -1}, {999, 0}, {0, 999}, {0, 1}})
	{
		CObj<NCore::WorldCommand> invalid = CreateCmdActivateTalent(hero.GetPtr(), slot.first, slot.second);
		check(!invalid->CanExecute(), "invalid or empty talent coordinates fail purchase admission");
		invalid->Execute(world);
		check(hero->GetGold() == 100, "invalid purchase coordinates cannot mutate gold");
	}
	CObj<NCore::WorldCommand> source = CreateCmdActivateTalent(hero.GetPtr(), 0, 0);
	CObj<NCore::PackedWorldCommand> packet = new NCore::PackedWorldCommand(source, world->GetPointerSerialization(), 1984, 0);
	CObj<PointersHolder> missingPointers = new PointersHolder(world, 0);
	CObj<NCore::WorldCommand> missing = packet->GetWorldCommand(missingPointers);
	check(missing && !missing->CanExecute(), "packed purchase rejects a missing restored actor");
	if (missing) missing->Execute(world);
	check(!hero->GetTalent(0, 0)->IsActivated() && hero->GetGold() == 100, "missing purchase actor is never substituted");
}
