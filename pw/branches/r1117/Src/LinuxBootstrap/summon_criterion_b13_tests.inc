/** Two actual summoned descriptors expose candidate-versus-applicator receiver mistakes and staged replacement. */
struct SummonCriterionFixtureB13 : NaftaFixtureB13
{
	PFWorld* world;
	NDb::Ptr<NDb::SummonApplicator> db;
	std::vector<CObj<PFBaseSummonedUnit>> units;
	bool ready = false;
	SummonCriterionFixtureB13(Checks& check, PFWorld* world, NDb::SummonType type = NDb::SUMMONTYPE_PRIMARY)
		: NaftaFixtureB13(world), world(world), db(SummonLifetimeDbB13(world, type, "0")), units(2)
	{
		auto* raw = const_cast<NDb::SummonApplicator*>(db.GetPtr());
		raw->summonedUnits[0] = new SummonCountDbB13(world, "/Mock/CriterionFirst.xdb");
		raw->summonedUnits.push_back(new SummonCountDbB13(world, "/Mock/CriterionSecond.xdb"));
		raw->summonedUnitIndex.sString = "f2l(sender.vChoice)";
		const_cast<NDb::SummonBehaviourCommon*>(static_cast<const NDb::SummonBehaviourCommon*>(raw->behaviour.GetPtr()))->maxCount.sString = "2";
		for (int i = 0; i < 2; ++i)
		{
			sender->GetVariableVWM("Choice")->SetBaseValue(i); auto app = Create(db);
			check(app && Activate(app), "B13 criterion fixture creates each actual summoned descriptor");
		}
		const auto live = SummonCountUnitsB13(world, sender);
		for (const auto& unit : live) units[unit->DbUnitDesc() == raw->summonedUnits[0].GetPtr() ? 0 : 1] = unit;
		ready = live.size() == 2 && units[0] && units[1]; check(ready, "B13 criterion fixture owns both distinct creature identities");
		if (!ready) return;
		for (int i = 0; i < 2; ++i)
		{
			units[i]->GetStat(NDb::STAT_LIFE)->SetCoreValue(i ? 1000 : 100);
			units[i]->GetStat(NDb::STAT_ENERGY)->SetCoreValue(100);
		}
		// Resource maximum caches update on a positive simulation tick, not registration-only Step(0).
		world->Step(.125f, .125f);
		check(units[0]->GetMaxLife() == 100 && units[1]->GetMaxLife() == 1000
			&& units[0]->GetMaxEnergy() == 100 && units[1]->GetMaxEnergy() == 100,
			"B13 criterion fixture observes the real updated resource maxima before assigning unequal ratios");
		for (int i = 0; i < 2; ++i)
		{
			units[i]->SetHealth(i ? 200 : 40); units[i]->ChangeMana((i ? 25 : 75) - units[i]->GetEnergy());
			units[i]->GetVariableVWM("Priority")->SetBaseValue(i);
			check(units[i]->TeleportTo(CVec2(i ? 20 : 12, 5)), "B13 criterion candidates have distinct real in-map positions");
		}
		sender->GetVariableVWM("Choice")->SetBaseValue(0);
	}
	PFSummonBehaviour* Behaviour(int index) const
	{
		return const_cast<PFSummonBehaviour*>(dynamic_cast<const PFSummonBehaviour*>(units[index]->GetBehaviour()));
	}
	CObj<PFBaseApplicator> Recast(const char* expression, bool perDescriptor = false)
	{
		auto* raw = new NDb::SummonApplicator; raw->Assign(*db); raw->unsummonSortCriterion.sString = expression;
		raw->unsummonSortCriterion.compiledString.clear();
		if (std::strcmp(expression, "compiled-only") == 0) { raw->unsummonSortCriterion.sString.clear(); raw->unsummonSortCriterion.compiledString = "unsupported"; }
		auto* behaviour = new NDb::SummonBehaviourCommon; behaviour->Assign(*static_cast<const NDb::SummonBehaviourCommon*>(db->behaviour.GetPtr()));
		behaviour->maxThisCount.sString = perDescriptor ? "1" : "0"; raw->behaviour = behaviour;
		NDb::Ptr<NDb::SummonApplicator> authored = raw; auto app = Create(authored); Activate(app); return app;
	}
	CObj<PFBaseApplicator> Mark(int index, const char* name)
	{
		auto* raw = WrapperFactoryFixtureB12::Stat("0"); raw->formulaName = name;
		NDb::Ptr<NDb::BaseApplicator> markerDb = raw;
		const Target target(units[index].GetPtr()); PFApplCreatePars pars(instance, target); pars.pDBAppl = markerDb;
		auto app = CreateApplicator(pars); Activate(app); return app;
	}
};

/** Original comparator reads each candidate, not the summon applicator's receiver or a literal default. */
static void SummonCriterionValuesB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	SummonCriterionFixtureB13 f(check, isolated.world); if (!f.ready) return;
	auto context = f.Create(f.db); struct Rule { const char* text; bool first; };
	for (const auto& rule : {Rule{"rLife", true}, Rule{"-rLife", false}, Rule{"rLife/rMaxLife", false}, Rule{"-d(rPos,sPos)", false},
		Rule{"cWrapperBonus+rLife", true}, Rule{"sLife-rLife", false}, Rule{"receiver.vPriority", true},
		Rule{"rEnergy/rMaxEnergy", false}, Rule{"false ? random(0,9) : rLife", true}})
	{
		ExecutableFloatString expression; expression.sString = rule.text;
		UnsummonCriterion criterion(&expression, f.sender, context);
		const NRandom::RandomGenerator saved(*isolated.world->GetRndGen());
		const bool first = criterion(f.Behaviour(0), f.Behaviour(1)), second = criterion(f.Behaviour(1), f.Behaviour(0));
		if (first != rule.first || second == rule.first) std::printf("B13 criterion '%s': comparison=%d/%d expected=%d life=%g/%g energy=%g/%g positions=%g,%g/%g,%g owner=%g,%g\n",
			rule.text, first, second, rule.first, f.units[0]->GetLife(), f.units[1]->GetLife(), f.units[0]->GetEnergy(), f.units[1]->GetEnergy(),
			f.units[0]->GetPos().x, f.units[0]->GetPos().y, f.units[1]->GetPos().x, f.units[1]->GetPos().y, f.sender->GetPos().x, f.sender->GetPos().y);
		check(first == rule.first && second != rule.first,
			"B13 custom criterion compares actual candidate resources positions variables and ability constants");
		check(!criterion(f.Behaviour(0), f.Behaviour(0)) && GlyphRngEqualB13(*isolated.world->GetRndGen(), saved),
			"B13 read-only criterion remains irreflexive and does not consume unselected random branches");
	}
	std::printf("B13 summon criterion values checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Unsupported comparisons return a checked failure before any actual removal; inactive requests stay lazy. */
static void SummonCriterionFailureB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (const char* text : {"cMissing", "rLife/0", "1e39f", "random(2,4)", "random(2,4)+cMissing", "compiled-only"})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		SummonCriterionFixtureB13 f(check, isolated.world); if (!f.ready) continue;
		auto context = f.Create(f.db); ExecutableFloatString expression; expression.sString = text;
		if (std::strcmp(text, "compiled-only") == 0) { expression.sString.clear(); expression.compiledString = "unsupported"; }
		UnsummonCriterion criterion(&expression, f.sender, context); const NRandom::RandomGenerator saved(*isolated.world->GetRndGen());
		check(f.sender->RemoveSummons(0, NDb::SUMMONTYPE_PRIMARY, "FactoryLifetime", nullptr, 2, &criterion) == 0
			&& f.sender->RemoveSummons(1, NDb::SUMMONTYPE_PRIMARY, "Absent", nullptr, 2, &criterion) == 0
			&& f.sender->RemoveSummons(3, NDb::SUMMONTYPE_PRIMARY, "FactoryLifetime", nullptr, 0, &criterion) == 0,
			"B13 no-op absent and infeasible removals do not evaluate their unused custom criteria");
		check(f.sender->RemoveSummons(1, NDb::SUMMONTYPE_PRIMARY, "FactoryLifetime", nullptr, 2, &criterion) == -1,
			"B13 selected malformed or nondeterministic criterion reports failure distinctly from no removal");
		check(!f.units[0]->IsDead() && !f.units[1]->IsDead() && f.sender->GetSummonsCount(NDb::SUMMONTYPE_PRIMARY, "FactoryLifetime") == 2
			&& GlyphRngEqualB13(*isolated.world->GetRndGen(), saved),
			"B13 rejected sort cannot destroy a candidate or spend world randomness");
	}
	for (bool perDescriptor : {false, true})
	for (const char* text : {"cMissing", "rLife/0", "1e39f", "random(2,4)", "compiled-only", "receiver.vPriority == 1 ? cMissing : rLife"})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		SummonCriterionFixtureB13 f(check, isolated.world); if (!f.ready) continue;
		const NRandom::RandomGenerator saved(*isolated.world->GetRndGen()); auto app = f.Recast(text, perDescriptor);
		const auto live = SummonCountUnitsB13(isolated.world, f.sender);
		check(app && !app->IsAlive() && f.instance->GetActiveApplicatorsCount() == 0,
			"B13 failed custom-priority recast balances its instant applicator ownership");
		check(live.size() == 2 && !f.units[0]->IsDead() && !f.units[1]->IsDead()
			&& f.sender->GetMaxAllowedSummons(NDb::SUMMONTYPE_PRIMARY, "FactoryLifetime") == 2
			&& GlyphRngEqualB13(*isolated.world->GetRndGen(), saved),
			"B13 invalid candidate priority rejects the recast before either descriptor-specific or group replacement");
	}
	std::printf("B13 summon criterion rejection checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Real recasts honor custom priority while retaining original zero-prefix bypass and dependent corpse cleanup. */
static void SummonCriterionRecastB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	struct Rule { const char* text; int removed; };
	for (auto type : {NDb::SUMMONTYPE_PRIMARY, NDb::SUMMONTYPE_SECONDARY})
	for (const auto& rule : {Rule{"rLife", 0}, Rule{"-rLife", 1}, Rule{"-d(rPos,sPos)", 1}, Rule{"cWrapperBonus+rLife", 0},
		Rule{"rEnergy/rMaxEnergy", 1}, Rule{"false ? random(0,9) : rLife", 0}, Rule{"0.0f", 1}, Rule{"0.f+cMissing", 1}})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		SummonCriterionFixtureB13 f(check, isolated.world, type); if (!f.ready) continue;
		const NRandom::RandomGenerator saved(*isolated.world->GetRndGen()); auto app = f.Recast(rule.text);
		const auto live = SummonCountUnitsB13(isolated.world, f.sender);
		if (!f.units[rule.removed]->IsDead() || f.units[1 - rule.removed]->IsDead())
			std::printf("B13 criterion recast '%s': dead=%d/%d expected=%d live=%zu\n", rule.text, f.units[0]->IsDead(), f.units[1]->IsDead(), rule.removed, live.size());
		check(app && !app->IsAlive() && live.size() == 2 && f.units[rule.removed]->IsDead() && !f.units[1 - rule.removed]->IsDead(),
			"B13 actual summon recast removes its formula-ranked candidate and preserves original zero-prefix default-priority selection");
		check(f.instance->GetActiveApplicatorsCount() == 0 && GlyphRngEqualB13(*isolated.world->GetRndGen(), saved),
			"B13 accepted custom-priority recast balances instant ownership without consuming comparison randomness");
		isolated.world->Step(.625f, .625f);
		check(f.units[rule.removed]->IsObjectDead() && !f.units[1 - rule.removed]->IsObjectDead(),
			"B13 formula-selected replaced corpse retires through the actual unsummon lifecycle");
	}
	std::printf("B13 summon criterion recasts checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Preserve exact shipped comparison formulas, including original missing named-effect identity semantics. */
static void SummonCriterionAuthoredB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		const auto boss = NDb::Get<NDb::SummonApplicator>(NDb::DBID("/Creeps/Bosses/Boss1/PvE_Coop1_DoTSummon.APLR.xdb"));
		const auto demon = NDb::Get<NDb::SummonApplicator>(NDb::DBID("/Items/Talents/Class/Demonolog/Ability3.TALENT.xdb", "B73351FE-A6B5-4822-B2ED-D43EE6580ABC"));
		const auto tree = NDb::Get<NDb::SummonApplicator>(NDb::DBID("/Items/Talents/Class/WitchDoctor/SummonEvilTreeAppl.APLR.xdb"));
		check(boss && demon && tree && boss->unsummonSortCriterion.sString == "-d(rPos,sPos)",
			"B13 actual boss Demonologist and WitchDoctor descriptors retain their original nonzero unsummon criteria");
		for (const auto& authored : {boss, demon, tree}) if (authored)
		for (int marked : {-1, 0, 1})
		{
			B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
			SummonCriterionFixtureB13 f(check, isolated.world); if (!f.ready) continue;
			CObj<PFBaseApplicator> marker;
			if (marked >= 0) marker = f.Mark(marked, authored == tree ? "HasCatchedTarget" : "A1u1_BurnDemons");
			const int selected = authored == boss || marked < 0 ? 1 : 1 - marked;
			auto app = f.Recast(authored->unsummonSortCriterion.sString.c_str());
			isolated.world->Step(.125f, .125f);
			const auto live = SummonCountUnitsB13(isolated.world, f.sender);
			std::printf("B13 authored criterion '%s': marked=%d dead=%d/%d expected=%d marker=%d\n",
				authored->unsummonSortCriterion.sString.c_str(), marked, f.units[0]->IsDead(), f.units[1]->IsDead(), selected, marker ? marker->IsAlive() : false);
			check(app && live.size() == 2 && f.units[selected]->IsDead() && !f.units[1 - selected]->IsDead(),
				"B13 unchanged shipped criterion selects by farthest distance or actual candidate resources plus live named-effect membership");
			if (marker) check(marker->IsAlive() == !f.units[marked]->IsDead(),
				"B13 formula ranking leaves a survivor's real named effect active and cleans up only effects on the removed creature");
		}
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 authored summon criteria checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Check every selected-group candidate before mutation, including late failures and partitioned sorts. */
static void SummonCriterionLargeB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (int size : {2, 17, 33}) for (int invalidIndex : {-1, 0, size - 1})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		SummonLifetimeFixtureB13 f(isolated.world); NaftaFixtureB13 context(isolated.world);
		NDb::Ptr<NDb::BaseApplicator> contextDb = WrapperFactoryFixtureB12::Stat(); auto app = context.Create(contextDb);
		for (int i = 0; i < size; ++i)
		{
			auto entry = f.Add(NDb::SUMMONTYPE_PRIMARY, 0);
			entry.unit->GetVariableVWM("Priority")->SetBaseValue(size - 1 - i);
		}
		const bool invalid = invalidIndex >= 0;
		const std::string text = invalid ? "receiver.vPriority == " + std::to_string(invalidIndex) + " ? cMissing : receiver.vPriority" : "receiver.vPriority";
		ExecutableFloatString expression; expression.sString = text.c_str(); UnsummonCriterion criterion(&expression, f.master, app);
		const NRandom::RandomGenerator saved(*isolated.world->GetRndGen());
		check(f.master->RemoveSummons(size / 2, NDb::SUMMONTYPE_PRIMARY, "Lifetime", nullptr, 0, &criterion) == (invalid ? -1 : size / 2),
			"B13 larger custom-priority sort returns checked failure or the exact feasible removal count");
		for (int i = 0; i < size; ++i) check(f.units[i].unit->IsDead() == (!invalid && size - 1 - i < size / 2),
			"B13 custom ordering selects the lowest real candidate scores and cannot partially remove a malformed group");
		check(GlyphRngEqualB13(*isolated.world->GetRndGen(), saved), "B13 complete custom-score validation and sorting remain read-only");
	}
	std::printf("B13 large summon criteria checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
