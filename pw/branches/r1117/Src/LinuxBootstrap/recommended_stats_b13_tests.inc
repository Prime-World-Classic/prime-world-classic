/** Exercise original profile flags and live getters without replacing the hero's recommendation implementation. */
static void RecommendedStatsB13(Checks& check, PFWorld* world)
{
	const int before = check.count, failed = check.failed;
	auto sender = B9MaleHero(world, NDb::FACTION_FREEZE), receiver = B9MaleHero(world, NDb::FACTION_FREEZE);
	const NDb::EStat mapping[] = {NDb::STAT_LIFE, NDb::STAT_ENERGY, NDb::STAT_MOVESPEED, NDb::STAT_STRENGTH,
		NDb::STAT_INTELLECT, NDb::STAT_ATTACKSPEED, NDb::STAT_DEXTERITY, NDb::STAT_STAMINA, NDb::STAT_WILL};
	for (int index = 0; index < 9; ++index)
	{
		int flags[9] = {}; flags[index] = index % 2 ? -3 : 2;
		sender->SetRecommendedStats(flags);
		for (int other = 0; other < 9; ++other)
			check(sender->IsRecommended(mapping[other]) == (other == index), "B13 original nine profile slots map to exactly their intended recommended stats");
		check(sender->profileStats.size() == 1 && sender->profileStats[0] == mapping[index], "B13 recommendation replacement preserves original order without stale or duplicate entries");
		for (const char* root : {"sender.", "receiver.", "pFirst->", "pSecond->"})
		{
			const std::string expression = std::string(root) + "IsRecommended(" + std::to_string(int(mapping[index])) + ")";
			const auto value = LinuxBootstrap::EvaluateUnitNumericFormula(expression, sender, sender);
			check(value.Succeeded() && value.value == 1, "B13 exact recommendation methods use the live original hero getter");
		}
	}
	int flags[9] = {}; flags[3] = flags[4] = 1; sender->SetRecommendedStats(flags);
	sender->SetRecommendedStats(nullptr);
	check(sender->IsRecommended(NDb::STAT_STRENGTH) && sender->IsRecommended(NDb::STAT_INTELLECT),
		"B13 missing profile data does not erase an already supplied recommendation set");
	flags[3] = 0; receiver->SetRecommendedStats(flags);
	const auto candidate = LinuxBootstrap::EvaluateSelectorConditionFormula("pFirst->IsRecommended(15) && !pSecond->IsRecommended(15)", sender, receiver, nullptr);
	check(candidate.Succeeded() && candidate.value == 1, "B13 selector recommendations distinguish sender from actual candidate");
	for (const auto& entry : {std::pair<const char*, float>{"sender.IsRecommended(15.9)", 1},
		{"sender.IsRecommended(-15.9)", 0}, {"sender.IsRecommended(2147483647)", 0},
		{"sender.IsRecommended(-2147483648)", 0}, {"sender.IsRecommended ( 15 )", 1}})
	{
		const auto value = LinuxBootstrap::EvaluateUnitNumericFormula(entry.first, sender, receiver);
		check(value.Succeeded() && value.value == entry.second, "B13 checked integer recommendation arguments retain original truncation and unknown-stat false results");
	}
	for (const char* expression : {"sender.IsRecommended()", "sender.IsRecommended(1,2)", "pFirst->IsRecommended(,1)",
		"receiver.SetRecommendedStats(1)", "__recommend_s(15)"})
		for (bool lazy : {false, true})
			check(!LinuxBootstrap::EvaluateUnitNumericFormula(std::string(lazy ? "true ? 7 : " : "") + expression, sender, receiver).Succeeded(),
				"B13 recommendation support exposes neither invalid method syntax nor internal transport names");
	for (const char* expression : {"sender.IsRecommended(1e100)", "sender.IsRecommended(2147483648)",
		"sender.IsRecommended(-2147483649)", "sender.IsRecommended(sqrt(-1))"})
		check(!LinuxBootstrap::EvaluateUnitNumericFormula(expression, sender, receiver).Succeeded(), "B13 unsafe recommendation arguments cannot reach original integer conversion");
	check(!LinuxBootstrap::EvaluateUnitNumericFormula("sender.IsRecommended(15)", nullptr, receiver).Succeeded(), "B13 a missing sender is not replaced with the receiver for recommendation queries");
	const auto lazy = LinuxBootstrap::EvaluateUnitNumericFormula("true ? 7 : sender.IsRecommended(1e100)", nullptr, nullptr);
	check(lazy.Succeeded() && lazy.value == 7, "B13 valid unselected recommendation calls require neither units nor valid evaluated arguments");
	auto unit = Unit(world, NDb::FACTION_FREEZE, 10);
	const auto nonhero = LinuxBootstrap::EvaluateUnitNumericFormula("sender.IsRecommended(15)", unit, sender);
	check(nonhero.Succeeded() && nonhero.value == 0, "B13 ordinary units retain the original false recommendation default");
	for (int& flag : flags) flag = 0;
	sender->SetRecommendedStats(flags);
	check(sender->profileStats.empty() && !sender->IsRecommended(NDb::STAT_STRENGTH), "B13 a supplied empty profile clears recommendations rather than retaining an old preference");
	const auto cleared = LinuxBootstrap::EvaluateUnitNumericFormula("sender.IsRecommended(15)", sender, receiver);
	check(cleared.Succeeded() && cleared.value == 0, "B13 recommendation formulas do not retain values across profile replacement");
	NDb::Ptr<NDb::Ability> db = new NDb::Ability;
	CObj<PFAbilityData> ability = new PFAbilityData(sender.GetPtr(), db, NDb::ABILITYTYPEID_ABILITY1, true, false);
	const Target target(receiver.GetPtr()); CObj<PFAbilityInstance> instance = new PFAbilityInstance(ability.GetPtr(), target, false);
	NDb::Ptr<NDb::BaseApplicator> leaf = new NDb::HealApplicator;
	PFApplCreatePars pars(instance, target); pars.pDBAppl = leaf;
	CObj<PFBaseApplicator> app = CreateApplicator(pars);
	check(app && app->Init(), "B13 recommendation RNG fixture owns actual applicator sender and receiver context");
	if (app) for (bool invalid : {false, true})
	{
		NRandom::RandomGenerator expectedRng(*world->GetRndGen()); const int roll = invalid ? 0 : expectedRng.Next(1, 3);
		const auto value = LinuxBootstrap::EvaluateApplicatorNumericFormula(invalid
			? "random(1,3)+receiver.IsRecommended(2147483648)" : "random(1,3)+receiver.IsRecommended(16)", *app);
		check(value.Succeeded() == !invalid && (invalid || value.value == roll + 1), "B13 applicator recommendation binds its real receiver after seeded argument evaluation");
		check(world->GetRndGen()->Next(0, 1000) == expectedRng.Next(0, 1000), "B13 failed integer admission rolls back preceding formula randomness");
	}
	sender->Cleanup(); receiver->Cleanup(); unit->Cleanup();
	std::printf("B13 recommended stats checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** The shipped adaptive talent chooses one stat from profile preference, then original base-stat tie breaking. */
static void AuthoredRecommendedStatsB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		const auto db = NDb::Get<NDb::Talent>(NDb::DBID("/Items/Talents/Level1/G250_Int_Str.TALENT.xdb"));
		check(db && db->passiveApplicators.size() == 3, "B13 original adaptive talent keeps all three authored passive applicators");
		if (db) for (int recommendations : {0, 1, 2, 3}) for (int difference : {-1, 0, 1}) for (int rate : {1, 6})
		{
			B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
			auto hero = CommandHeroB10(isolated.world); hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
			hero->GetStat(NDb::STAT_STRENGTH)->SetCoreValue(40 + difference);
			hero->GetStat(NDb::STAT_INTELLECT)->SetCoreValue(40);
			int flags[9] = {}; flags[3] = recommendations & 1; flags[4] = recommendations & 2;
			hero->SetRecommendedStats(flags); hero->InstallTalent(db, false);
			CObj<PFTalent> talent = hero->GetTalent(0, 0); talent->SetRefineRate(rate); hero->AddGold(10000);
			const float gold = hero->GetGold(), strength = hero->GetStatValue(NDb::STAT_STRENGTH), intellect = hero->GetStatValue(NDb::STAT_INTELLECT);
			const int cost = talent->GetNaftaCost();
			const bool chooseStrength = recommendations == 1 || (recommendations != 2 && difference >= 0);
			const float bonus = 4.2f + .3f * float(rate - 1);
			check(hero->ActivateTalent(0, 0) && hero->GetGold() == gold - cost,
				"B13 actual adaptive talent purchase preserves its original cost and activation path");
			check(std::fabs(hero->GetStatValue(NDb::STAT_STRENGTH) - (strength + (chooseStrength ? bonus : 0))) < .0001f
				&& std::fabs(hero->GetStatValue(NDb::STAT_INTELLECT) - (intellect + (chooseStrength ? 0 : bonus))) < .0001f,
				"B13 adaptive talent uses profile preference first and base-stat tie breaking only when recommendations tie");
			check(hero->GetVariable("G250_StrOrInt") == (chooseStrength ? 1 : 0), "B13 actual adaptive talent records its authored chosen-stat variable");
			check(!hero->ActivateTalent(0, 0) && hero->GetGold() == gold - cost, "B13 repeated adaptive purchase does not reapply the permanent effect");
			hero->GetTalentsSet()->CleanSet(); hero->Cleanup();
		}
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 authored recommendations checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
