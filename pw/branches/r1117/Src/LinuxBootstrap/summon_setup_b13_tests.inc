/** Set authored source explicitly, including the unsupported compiled-only case. */
static void SummonSetupSourceB13(ExecutableFloatString& value, const char* text)
{
	value.sString = text; value.compiledString.clear();
	if (std::strcmp(text, "compiled-only") == 0) { value.sString.clear(); value.compiledString = "unsupported"; }
}

/** Resource formulas see the newly created candidate and both reads precede either resource write. */
static void SummonSetupResourcesB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	struct Values { const char* life; const char* energy; int kind; };
	for (int type : {-1, int(NDb::SUMMONTYPE_PRIMARY), int(NDb::SUMMONTYPE_SECONDARY), int(NDb::SUMMONTYPE_PET)})
	for (const auto& value : {Values{"", "", 0}, Values{"-1", "-1", 0}, Values{"0", "0", 1},
		Values{"rLife/4", "rLife/10", 2}, Values{"sLife+cWrapperBonus*10", "cWrapperBonus*10", 3},
		Values{"2*rMaxLife", "2*rMaxEnergy", 0}, Values{"false ? cMissing : 70", "false ? random(1,9) : 30", 4}})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		NaftaFixtureB13 f(isolated.world); auto db = SummonLifetimeDbB13(isolated.world, type, "0");
		auto* raw = const_cast<NDb::SummonApplicator*>(db.GetPtr());
		SummonSetupSourceB13(raw->spawnStats.life, value.life); SummonSetupSourceB13(raw->spawnStats.energy, value.energy);
		const NRandom::RandomGenerator saved(*isolated.world->GetRndGen()); auto app = f.Create(db);
		check(app && f.Activate(app), "B13 summon resource setup uses actual factory admission");
		const auto units = SummonCountUnitsB13(isolated.world, f.sender);
		check(units.size() == 1, "B13 accepted spawn resources retain one actual owned creature"); if (units.size() != 1) continue;
		const auto& unit = units[0];
		const float life = value.kind == 2 ? unit->GetMaxLife()/4 : value.kind == 3 ? 420 : value.kind == 4 ? 70 : unit->GetMaxLife();
		const float energy = value.kind == 1 ? 0 : value.kind == 2 ? unit->GetMaxLife()/10 : value.kind == 3 ? 20 : value.kind == 4 ? 30 : unit->GetMaxEnergy();
		check(unit->GetLife() == life && unit->GetEnergy() == energy,
			"B13 spawn resources read the candidate and original owner/constants before writing either resource, retaining original sentinels and clamps");
		check(f.instance->GetActiveApplicatorsCount() == 0 && GlyphRngEqualB13(*isolated.world->GetRndGen(), saved),
			"B13 resource setup retires the instant cast without evaluating lazy random branches");
	}
	std::printf("B13 summon resource setup checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Damage multipliers are captured from the actual fresh candidate, before its spawn-resource changes. */
static void SummonSetupDamageB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	struct Value { const char* text; float expected; };
	for (int type : {-1, int(NDb::SUMMONTYPE_PRIMARY)})
	for (const auto& value : {Value{"", 1}, Value{"0", 0}, Value{"cWrapperBonus", 2}, Value{"rLife/rMaxLife+cWrapperBonus", 3},
		Value{"sLife/100", 4}, Value{"false ? cMissing : 0.5", .5f}})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		NaftaFixtureB13 f(isolated.world); auto db = SummonLifetimeDbB13(isolated.world, type, "0");
		auto* raw = const_cast<NDb::SummonApplicator*>(db.GetPtr()); SummonSetupSourceB13(raw->takeModDmg, value.text);
		raw->takeTypeUnit = static_cast<NDb::ESpellTarget>(NDb::SPELLTARGET_HEROMALE | NDb::SPELLTARGET_ALLY);
		raw->spawnStats.life.sString = "100"; auto app = f.Create(db); check(app && f.Activate(app), "B13 summon damage fixture creates its actual creature");
		const auto units = SummonCountUnitsB13(isolated.world, f.sender); check(units.size() == 1, "B13 valid multiplier retains the summon");
		if (units.size() != 1) continue;
		auto damage = DirectDamage(); damage.pSender = f.sender; damage.amount = 10; damage.damageType = NDb::APPLICATORDAMAGETYPE_PURE;
		check(units[0]->OnBeforeDamage(damage) == 10*value.expected,
			"B13 original target mask applies the formula multiplier captured before spawn-resource mutation");
		const float life = units[0]->GetLife(); units[0]->OnDamage(damage);
		check(units[0]->GetLife() == life - 10*value.expected, "B13 actual incoming damage applies the accepted summon multiplier exactly once");
		damage.pSender = nullptr; check(units[0]->OnBeforeDamage(damage) == 10, "B13 unmatched damage retains the original unmodified amount");
	}
	std::printf("B13 summon damage setup checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Leash refresh reads live master/candidate stats; a rejected refresh retains range without freezing lifetime. */
static void SummonSetupLashB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (auto type : {NDb::SUMMONTYPE_PRIMARY, NDb::SUMMONTYPE_SECONDARY, NDb::SUMMONTYPE_PET})
	for (const char* expression : {"", "sRange+rRange", "sRange+rRange+cWrapperBonus", "false ? random(1,9) : 9", "0", "-2"})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		NaftaFixtureB13 f(isolated.world); auto db = SummonLifetimeDbB13(isolated.world, type, "1");
		auto* common = const_cast<NDb::SummonBehaviourCommon*>(static_cast<const NDb::SummonBehaviourCommon*>(db->behaviour.GetPtr()));
		SummonSetupSourceB13(common->lashRange, expression); auto app = f.Create(db);
		check(app && f.Activate(app), "B13 leash fixture creates a real timer-controlled AI summon");
		const auto units = SummonCountUnitsB13(isolated.world, f.sender); check(units.size() == 1, "B13 valid initial leash retains the summon");
		if (units.size() != 1) continue;
		CObj<PFSummonBehaviour> behaviour = const_cast<PFSummonBehaviour*>(dynamic_cast<const PFSummonBehaviour*>(units[0]->GetBehaviour()));
		const bool stats = std::strstr(expression, "sRange") != nullptr, constant = std::strstr(expression, "cWrapperBonus") != nullptr;
		const float initial = !*expression ? 15 : stats ? f.sender->GetRange()+units[0]->GetRange()+(constant ? 2 : 0)
			: std::strcmp(expression, "0") == 0 || std::strcmp(expression, "-2") == 0 ? 0 : 9;
		check(behaviour && behaviour->GetLashRange() == initial, "B13 initial leash uses actual candidate context and applicator constants"); if (!behaviour) continue;
		f.sender->GetStat(NDb::STAT_RANGE)->SetCoreValue(20); units[0]->GetStat(NDb::STAT_RANGE)->SetCoreValue(3);
		const NRandom::RandomGenerator saved(*isolated.world->GetRndGen()); isolated.world->Step(.25f, .25f);
		check(behaviour->GetLashRange() == (stats && !constant ? 23 : initial) && behaviour->GetLifeTime() == .25f,
			"B13 live leash refresh has original null-misc context; invalid constants retain the last valid range while the timer advances");
		SummonSetupSourceB13(common->lashRange, "random(1,9)+cMissing"); isolated.world->Step(.75f, .75f);
		check(!units[0]->IsDead() && behaviour->GetLifeTime() == 1 && GlyphRngEqualB13(*isolated.world->GetRndGen(), saved),
			"B13 rejected leash cannot consume RNG or bypass the exact controlled lifetime boundary");
		isolated.world->Step(.125f, .125f);
		check(units[0]->IsDead() && !units[0]->HasBehaviour() && f.sender->GetSummonsCount(type, "FactoryLifetime") == 0,
			"B13 failed repeated leash reads do not make a controlled summon immortal");
	}
	std::printf("B13 summon leash setup checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Bad initial candidate data retires the new creature before child effects; prior replacement is not rolled back. */
static void SummonSetupRejectionB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (int field = 0; field < 4; ++field)
	for (const char* expression : {"cMissing", "1/0", "1e39f", "random(2,4)+cMissing", "compiled-only"})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		NaftaFixtureB13 f(isolated.world); auto db = SummonLifetimeDbB13(isolated.world, NDb::SUMMONTYPE_PRIMARY, "0");
		auto* raw = const_cast<NDb::SummonApplicator*>(db.GetPtr());
		auto* common = const_cast<NDb::SummonBehaviourCommon*>(static_cast<const NDb::SummonBehaviourCommon*>(db->behaviour.GetPtr()));
		auto* child = WrapperFactoryFixtureB12::Stat("777"); child->formulaName = "RejectedSpawnChild"; raw->applicators.push_back(child);
		if (field == 3) raw->spawnStats.life.sString = "random(20,40)";
		SummonSetupSourceB13(field == 0 ? raw->takeModDmg : field == 1 ? common->lashRange : field == 2 ? raw->spawnStats.life : raw->spawnStats.energy, expression);
		const NRandom::RandomGenerator saved(*isolated.world->GetRndGen()); auto app = f.Create(db);
		check(app && f.Activate(app) && !app->IsAlive() && f.instance->GetActiveApplicatorsCount() == 0,
			"B13 rejected candidate setup balances instant ownership and never activates child effects");
		const auto live = SummonCountUnitsB13(isolated.world, f.sender);
		check(live.empty() && f.sender->GetSummonsCount(NDb::SUMMONTYPE_PRIMARY, "FactoryLifetime") == 0,
			"B13 malformed selected candidate formula does not leave a live default-configured summon");
		check(GlyphRngEqualB13(*isolated.world->GetRndGen(), saved),
			"B13 failed initial reads preserve RNG, including the atomic life/energy read pair");
		std::vector<CObj<PFBaseSummonedUnit>> corpses;
		auto collect = [&](PF_Core::WorldObjectBase& object) {
			if (auto* unit = dynamic_cast<PFBaseSummonedUnit*>(&object)) if (unit->GetMasterUnit() == f.sender) corpses.emplace_back(unit);
		};
		isolated.world->ForAllObjectsBeingStepped(collect);
		check(corpses.size() == 1 && corpses[0]->IsDead() && !corpses[0]->HasBehaviour(),
			"B13 rejected fresh candidate follows original unsummon death and behavior cleanup");
		isolated.world->Step(.625f, .625f);
		check(corpses.size() == 1 && corpses[0]->IsObjectDead(), "B13 rejected candidate retires its world identity after the original corpse delay");
	}
	std::printf("B13 summon setup rejection checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Preserve setup order per candidate, with per-expression staging and paired resource rollback. */
static void SummonSetupRandomB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (int failure : {-1, 0, 1, 2, 3})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		NaftaFixtureB13 f(isolated.world); auto db = SummonLifetimeDbB13(isolated.world, NDb::SUMMONTYPE_PRIMARY, "0");
		auto* raw = const_cast<NDb::SummonApplicator*>(db.GetPtr()); raw->summonedUnitsCount.sString = "2";
		raw->takeTypeUnit = static_cast<NDb::ESpellTarget>(NDb::SPELLTARGET_HEROMALE | NDb::SPELLTARGET_ALLY);
		auto* common = const_cast<NDb::SummonBehaviourCommon*>(static_cast<const NDb::SummonBehaviourCommon*>(db->behaviour.GetPtr()));
		common->maxCount.sString = "2";
		ExecutableFloatString* sources[] = {&raw->takeModDmg, &common->lashRange, &raw->spawnStats.life, &raw->spawnStats.energy};
		const char* expressions[] = {"random(2,4)", "random(5,8)", "random(20,40)", "random(50,60)"};
		for (int field = 0; field < 4; ++field) SummonSetupSourceB13(*sources[field], field == failure ? "random(1,9)+cMissing" : expressions[field]);
		NRandom::RandomGenerator expected(*isolated.world->GetRndGen());
		struct Values { float damage, lash, life, energy; }; std::vector<Values> values;
		for (int candidate = 0; candidate < 2; ++candidate)
		{
			Values value{};
			if (failure != 0) value.damage = expected.Next(2, 4);
			if (failure < 0 || failure > 1) value.lash = expected.Next(5, 8);
			if (failure < 0) { value.life = expected.Next(20, 40); value.energy = expected.Next(50, 60); }
			values.push_back(value);
		}
		auto app = f.Create(db); check(app && f.Activate(app), "B13 random setup runs through the actual multi-summon loop");
		auto units = SummonCountUnitsB13(isolated.world, f.sender);
		check(units.size() == (failure < 0 ? 2u : 0u) && GlyphRngEqualB13(*isolated.world->GetRndGen(), expected),
			"B13 per-candidate setup keeps accepted earlier draws, rolls back failed resource pairs and skips later reads");
		std::sort(units.begin(), units.end(), [](const CObj<PFBaseSummonedUnit>& a, const CObj<PFBaseSummonedUnit>& b) { return a->GetObjectId() < b->GetObjectId(); });
		if (failure < 0) for (size_t i = 0; i < units.size(); ++i)
		{
			const auto* behaviour = dynamic_cast<const PFSummonBehaviour*>(units[i]->GetBehaviour());
			auto hit = DirectDamage(); hit.pSender = f.sender; hit.amount = 10;
			check(units[i]->GetLife() == values[i].life && units[i]->GetEnergy() == values[i].energy
				&& behaviour && behaviour->GetLashRange() == values[i].lash && units[i]->OnBeforeDamage(hit) == 10*values[i].damage,
				"B13 damage leash life and energy consume one seeded draw each in original per-candidate order");
		}
		check(f.instance->GetActiveApplicatorsCount() == 0, "B13 multi-summon setup balances cast ownership on success and failure");
	}
	std::printf("B13 summon setup randomness checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Candidate-dependent rejection cannot restore an old summon already replaced, but leaves no alpha/group leak. */
static void SummonSetupRecastB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (int field = 0; field < 4; ++field)
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		NaftaFixtureB13 f(isolated.world); auto db = SummonLifetimeDbB13(isolated.world, NDb::SUMMONTYPE_PRIMARY, "0");
		auto* raw = const_cast<NDb::SummonApplicator*>(db.GetPtr());
		raw->flags = static_cast<NDb::ESummonFlags>(raw->flags | NDb::SUMMONFLAGS_ALPHASUMMON);
		auto first = f.Create(db); check(first && f.Activate(first), "B13 rejected recast starts with an actual alpha summon");
		const auto old = SummonCountUnitsB13(isolated.world, f.sender);
		check(old.size() == 1 && f.sender->GetAlphaSummon() == old[0], "B13 first summon owns the real alpha link"); if (old.size() != 1) continue;
		auto* common = const_cast<NDb::SummonBehaviourCommon*>(static_cast<const NDb::SummonBehaviourCommon*>(db->behaviour.GetPtr()));
		SummonSetupSourceB13(field == 0 ? raw->takeModDmg : field == 1 ? common->lashRange : field == 2 ? raw->spawnStats.life : raw->spawnStats.energy, "rMaxLife+cMissing");
		auto second = f.Create(db); check(second && f.Activate(second), "B13 malformed candidate recast follows actual replacement then setup");
		const auto live = SummonCountUnitsB13(isolated.world, f.sender);
		check(old[0]->IsDead() && live.empty() && !IsValid(f.sender->GetAlphaSummon())
			&& f.sender->GetSummonsCount(NDb::SUMMONTYPE_PRIMARY, "FactoryLifetime") == 0,
			"B13 failed replacement candidate retires group and alpha ownership without claiming rollback of prior unsummon callbacks");
	}
	std::printf("B13 summon setup recasts checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Execute shipped spawn-resource and leash expressions on real summons; this does not claim hero cloning. */
static void SummonSetupAuthoredB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		const auto fox = NDb::Get<NDb::CloneHeroApplicator>(NDb::DBID("/Items/Talents/Class/FireFox/A3Clone.APLR.xdb"));
		const auto panther = NDb::Get<NDb::SummonBehaviourCommon>(NDb::DBID("/Items/Talents/Class/Night/PantherBehaviour.xdb"));
		const auto demon = NDb::Get<NDb::SummonApplicator>(NDb::DBID("/Items/Talents/Class/Demonolog/SummonDemons.APLR.xdb"));
		const auto* demonBehaviour = demon ? dynamic_cast<const NDb::SummonBehaviourCommon*>(demon->behaviour.GetPtr()) : nullptr;
		check(fox && panther && demonBehaviour && fox->spawnStats.life.sString == "sLife / sMaxLife * rMaxLife"
			&& fox->spawnStats.energy.sString == "sEnergy / sMaxEnergy * rMaxEnergy", "B13 original spawn-resource and leash descriptor sources load unchanged");
		if (fox && panther && demonBehaviour) for (bool ranged : {false, true}) for (bool demonRange : {false, true})
		{
			B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
			NaftaFixtureB13 f(isolated.world); f.sender->GetStat(NDb::STAT_RANGE)->SetCoreValue(20);
			f.sender->ChangeMana(f.sender->GetMaxEnergy()/4 - f.sender->GetEnergy());
			auto db = SummonLifetimeDbB13(isolated.world, NDb::SUMMONTYPE_PRIMARY, "0");
			auto* raw = const_cast<NDb::SummonApplicator*>(db.GetPtr()); raw->spawnStats = fox->spawnStats;
			auto* common = const_cast<NDb::SummonBehaviourCommon*>(static_cast<const NDb::SummonBehaviourCommon*>(db->behaviour.GetPtr()));
			common->lashRange = demonRange ? demonBehaviour->lashRange : panther->lashRange;
			auto* stats = new NDb::StatsContainer; stats->Assign(*raw->summonedUnits[0]->stats);
			NDb::UnitStat range; range.statId = NDb::STAT_RANGE; range.value.sString = ranged ? "14" : "3"; range.increment.sString = "0";
			stats->stats.push_back(range); const_cast<NDb::Summoned*>(raw->summonedUnits[0].GetPtr())->stats = stats;
			auto app = f.Create(db); check(app && f.Activate(app), "B13 shipped setup expressions execute on the real summon factory");
			const auto units = SummonCountUnitsB13(isolated.world, f.sender); check(units.size() == 1, "B13 shipped setup expressions retain their actual candidate");
			if (units.size() != 1) continue;
			check(units[0]->GetLife() == f.sender->GetLife()/f.sender->GetMaxLife()*units[0]->GetMaxLife()
				&& units[0]->GetEnergy() == units[0]->GetMaxEnergy()/4, "B13 original FireFox resource fractions use owner and fresh summon maxima independently");
			const auto* behaviour = dynamic_cast<const PFSummonBehaviour*>(units[0]->GetBehaviour());
			const float expected = demonRange ? (ranged ? 11 : 18) : 18;
			check(behaviour && behaviour->GetLashRange() == expected, "B13 original Demonologist/Panther leash respects owner range and candidate melee/ranged branches");
			f.sender->GetStat(NDb::STAT_RANGE)->SetCoreValue(24); isolated.world->Step(.125f, .125f);
			check(behaviour && behaviour->GetLashRange() == expected+4, "B13 shipped leash recalculates from live owner range on actual behavior stepping");
		}
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 authored summon setup checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
