/** Real talent rates and upgrader contexts exercise the original refinement getter, including its zero-rate fallback. */
static void RefineScaleB13(Checks& check, PFWorld* world)
{
	const int before = check.count, failed = check.failed;
	auto hero = B9MaleHero(world, NDb::FACTION_FREEZE);
	NDb::Ptr<NDb::Talent> db = new NDb::Talent;
	CObj<PFTalent> talent = new PFTalent(hero.GetPtr(), db, 1, 0, 0, false);
	const Target target(hero.GetPtr()); CObj<PFAbilityInstance> instance = new PFAbilityInstance(talent.GetPtr(), target, false);
	NDb::Ptr<NDb::BaseApplicator> leaf = new NDb::HealApplicator;
	PFApplCreatePars pars(instance, target); pars.pDBAppl = leaf;
	CObj<PFBaseApplicator> app = CreateApplicator(pars);
	check(app && app->Init(), "B13 refinement fixture initializes a real talent-owned applicator");
	if (!app) return;
	for (int rate : {-2, 0, 1, 2, 6, 11, std::numeric_limits<int>::max(), std::numeric_limits<int>::min()})
	{
		talent->SetRefineRate(rate);
		const bool valid = rate != std::numeric_limits<int>::min();
		const float expected = valid ? 10.f + 2.f * float(rate - 1) : 0.f;
		const auto direct = LinuxBootstrap::EvaluateUnitNumericFormula("refineScale(10,2)", hero, hero, talent);
		const auto applied = LinuxBootstrap::EvaluateApplicatorNumericFormula("refineScale(10,2)", *app);
		const auto selected = LinuxBootstrap::EvaluateSelectorConditionFormula("refineScale(10,2)", hero, hero, app);
		for (const auto& value : {direct, applied, selected})
			check(value.Succeeded() == valid && (!valid || value.value == expected),
				"B13 refinement preserves original signed rate-minus-one arithmetic and rejects unsafe integer subtraction");
		const auto raw = LinuxBootstrap::EvaluateUnitNumericFormula("mRefineRate", hero, hero, talent);
		check(raw.Succeeded() && raw.value == float(rate), "B13 raw refinement reads retain the actual signed integer rate without clamping");
		NRandom::RandomGenerator expectedRng(*world->GetRndGen()); const int roll = valid ? expectedRng.Next(0, 10) : 0;
		const auto staged = LinuxBootstrap::EvaluateApplicatorNumericFormula("random(0,10)+refineScale(10,2)", *app);
		check(staged.Succeeded() == valid && (!valid || staged.value == float(double(roll) + expected)),
			"B13 applicator refinement retains the existing seeded formula transaction");
		check(world->GetRndGen()->Next(0, 1000) == expectedRng.Next(0, 1000), "B13 unsafe refinement cannot consume an earlier random draw");
	}
	talent->SetRefineRate(6);
	struct Value { const char* expression; float expected; };
	for (const auto& entry : {Value{"refineScale(10,-2)", 0}, Value{"refineScale(-10,2)", 0},
		Value{"refineScale(refineScale(1,2),0.5)", 13.5f}, Value{"refineScale(16777217,0)", 16777216.f},
		Value{"refineScale(0,1e-45f)", std::numeric_limits<float>::denorm_min() * 5},
		Value{"true ? 7 : refineScale(1e39,0)", 7}})
	{
		const auto value = LinuxBootstrap::EvaluateApplicatorNumericFormula(entry.expression, *app);
		check(value.Succeeded() && value.value == entry.expected, "B13 refinement preserves float arguments, signed increments, nested calls and unselected invalid context");
	}
	for (const char* expression : {"refineScale()", "refineScale(1)", "refineScale(1,2,3)", "true ? 2 : refineScale(1)",
		"refineScale(1e39,1)", "refineScale(1,1e39)", "refineScale(3.4e38,3.4e38)", "refineScale(1,sqrt(-1))"})
	{
		NRandom::RandomGenerator expectedRng(*world->GetRndGen());
		check(!LinuxBootstrap::EvaluateApplicatorNumericFormula(std::string("random(0,10)+") + expression, *app).Succeeded(),
			"B13 malformed, nonfinite and overflowing refinement cannot produce an invented bonus");
		check(world->GetRndGen()->Next(0, 1000) == expectedRng.Next(0, 1000), "B13 rejected refinement rolls back prior random evaluation");
	}
	for (const char* expression : {"refineScale(1,2)", "mRefineRate"})
	{
		check(!LinuxBootstrap::EvaluateUnitNumericFormula(expression, hero, hero).Succeeded(), "B13 refinement needs the real miscellaneous ability context");
		const auto lazy = LinuxBootstrap::EvaluateUnitNumericFormula(std::string("true ? 7 : ") + expression, nullptr, nullptr);
		check(lazy.Succeeded() && lazy.value == 7, "B13 unselected refinement needs no ability or owner");
	}
	NDb::Ptr<NDb::Ability> ordinaryDb = new NDb::Ability;
	CObj<PFAbilityData> ordinary = new PFAbilityData(hero.GetPtr(), ordinaryDb, NDb::ABILITYTYPEID_ABILITY1, false, false);
	const auto ordinaryResult = LinuxBootstrap::EvaluateUnitNumericFormula("refineScale(10,2)", hero, hero, ordinary);
	check(ordinaryResult.Succeeded() && ordinaryResult.value == 8, "B13 ordinary ability preserves original rate-zero behavior instead of assuming talent level one");
	CObj<PFTalent> upgrader = new PFTalent(hero.GetPtr(), db, 8, 0, 0, false);
	pars.pUpgraderAbilityData = upgrader; CObj<PFBaseApplicator> upgraded = CreateApplicator(pars);
	check(upgraded && upgraded->Init(), "B13 refinement fixture retains distinct source and upgrader abilities");
	if (upgraded)
	{
		const auto value = LinuxBootstrap::EvaluateApplicatorNumericFormula("refineScale(10,2)", *upgraded);
		check(value.Succeeded() && value.value == 24 && talent->GetRefineRate() == 6,
			"B13 refinement follows original applicator upgrader precedence rather than its source talent");
	}
	PFApplCreatePars orphanPars(nullptr, target); orphanPars.pWorld = world; orphanPars.pOwner = hero; orphanPars.pDBAppl = leaf;
	CObj<PFBaseApplicator> orphan = CreateApplicator(orphanPars);
	check(orphan && orphan->Init(), "B13 missing-ability refinement fixture retains a real owner and world");
	if (orphan) for (const char* expression : {"refineScale(1,2)", "mRefineRate"})
	{
		NRandom::RandomGenerator expectedRng(*world->GetRndGen());
		check(!LinuxBootstrap::EvaluateApplicatorNumericFormula(std::string("random(0,10)+") + expression, *orphan).Succeeded(),
			"B13 owner and world cannot substitute for a missing refinement ability");
		check(world->GetRndGen()->Next(0, 1000) == expectedRng.Next(0, 1000), "B13 missing refinement ability preserves staged randomness");
	}
	hero->Cleanup();
	std::printf("B13 refinement scales checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Purchase unchanged shipped passive talents at different refinement rates and observe original permanent stat effects. */
static void AuthoredRefinementB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		struct Entry { const char* path; NDb::EStat stat; float base, increment; bool floorTenths; };
		for (const auto& entry : {
			Entry{"/Items/Talents/Level1/G129_Str.TALENT.xdb", NDb::STAT_STRENGTH, 4.2f, .3f, false},
			Entry{"/Items/Talents/Level1/G132_RegLife.TALENT.xdb", NDb::STAT_LIFEREGENERATIONABSOLUTE, 2.3f, .15f, true},
			Entry{"/Items/Talents/Level1/G295_LifeDrain.TALENT.xdb", NDb::STAT_LIFEDRAIN, 7.2f, .6f, false},
			Entry{"/Items/Talents/Level1/G318_Energy.TALENT.xdb", NDb::STAT_ENERGY, 130.f, 10.f, false}})
		{
			const auto db = NDb::Get<NDb::Talent>(NDb::DBID(entry.path));
			check(db && db->passiveApplicators.size() == 1, "B13 shipped refinement talent retains its complete authored passive list");
			if (!db) continue;
			for (int rate : {1, 6, 11})
			{
				B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
				auto hero = CommandHeroB10(isolated.world); hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
				hero->InstallTalent(db, false); CObj<PFTalent> talent = hero->GetTalent(0, 0);
				talent->SetRefineRate(rate); hero->AddGold(10000);
				const float initial = hero->GetStatValue(entry.stat), gold = hero->GetGold();
				const int cost = talent->GetNaftaCost();
				check(!talent->IsTalentBought(), "B13 installing a refined talent does not prematurely purchase its passive");
				check(cost >= 0 && hero->ActivateTalent(0, 0) && hero->GetGold() == gold - cost,
					"B13 original hero purchase charges the actual talent cost without replacing the authored descriptor");
				float bonus = entry.base + entry.increment * float(rate - 1);
				if (entry.floorTenths)
				{
					// The existing scalar evaluator uses double intermediates; do not claim C++ float-expression parity.
					const float cppBonus = std::floor(bonus * 10.f) / 10.f;
					bonus = float(std::floor(double(bonus) * 10) / 10);
					if (cppBonus != bonus) std::printf("B13 refinement rounding boundary: %s rate=%d native=%g cpp-float=%g\n",
						entry.path, rate, bonus, cppBonus);
				}
				check(std::fabs(hero->GetStatValue(entry.stat) - (initial + bonus)) < .0001f,
					"B13 shipped talent applies its refined permanent stat bonus through original constants and the documented native arithmetic policy");
				check(!hero->ActivateTalent(0, 0) && hero->GetGold() == gold - cost
					&& std::fabs(hero->GetStatValue(entry.stat) - (initial + bonus)) < .0001f,
					"B13 repeated purchase cannot duplicate a permanent refined bonus or charge gold twice");
				hero->GetTalentsSet()->CleanSet(); hero->Cleanup();
			}
		}
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 authored refinement checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
