/** Give mock creatures distinct DB identities so selection and per-descriptor limits are observable. */
struct SummonCountDbB13 : NDb::Summoned
{
	SummonCountDbB13(PFWorld* world, const char* path)
	{
		NDb::Creature::Assign(*DeadCreatureDbB13(world)); SetDbId(NDb::DBID(path)); ProbeResourceCache::MarkLoaded(this);
	}
};

/** Collect real registered summons, disabling unrelated combat only after their actual creation. */
static std::vector<CObj<PFBaseSummonedUnit>> SummonCountUnitsB13(PFWorld* world, PFBaseUnit* master)
{
	world->Step(0, 0);
	std::vector<CObj<PFBaseSummonedUnit>> units;
	auto collect = [&](PF_Core::WorldObjectBase& object) {
		if (auto* unit = dynamic_cast<PFBaseSummonedUnit*>(&object))
			if (!unit->IsDead() && unit->GetMasterUnit() == master)
			{
				unit->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); unit->AddFlag(NDb::UNITFLAG_FORBIDMOVE);
				unit->EnableAbilityAI(false); units.emplace_back(unit);
			}
	};
	world->ForAllObjectsBeingStepped(collect); return units;
}

/** Empty values retain original defaults; accepted numeric fractions truncate before legacy fallbacks. */
static void SummonCountsB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	struct Count { const char* text; int count; };
	for (int type : {-1, int(NDb::SUMMONTYPE_PRIMARY), int(NDb::SUMMONTYPE_SECONDARY), int(NDb::SUMMONTYPE_PET)})
	for (const auto& value : {Count{"", 1}, Count{"2.9", 2}, Count{"cWrapperBonus", 2}, Count{"sLife/100", 4},
		Count{"rLife/200", 4}, Count{"f2l(max(sender.vSaveDemons,1))", 3}, Count{"0", 1}, Count{"-2", 1},
		Count{"random(2,4)", -1}})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		NaftaFixtureB13 f(isolated.world); f.sender->GetVariableVWM("SaveDemons")->SetBaseValue(3);
		auto db = SummonLifetimeDbB13(isolated.world, type, "0");
		auto* raw = const_cast<NDb::SummonApplicator*>(db.GetPtr()); raw->summonedUnitsCount.sString = value.text;
		if (type >= 0) const_cast<NDb::SummonBehaviourCommon*>(static_cast<const NDb::SummonBehaviourCommon*>(raw->behaviour.GetPtr()))->maxCount.sString = "10";
		NRandom::RandomGenerator expected(*isolated.world->GetRndGen()); const int count = value.count < 0 ? expected.Next(2, 4) : value.count;
		auto app = f.Create(db); check(app && f.Activate(app), "B13 summon count uses actual factory and activation");
		const auto units = SummonCountUnitsB13(isolated.world, f.sender);
		check(int(units.size()) == count, "B13 summoned count evaluates live endpoints constants variables and original numeric truncation");
		check(GlyphRngEqualB13(*isolated.world->GetRndGen(), expected) && f.instance->GetActiveApplicatorsCount() == 0,
			"B13 summon count evaluates once and retires the instant cast independently of its creatures");
		if (type >= 0) check(f.sender->GetSummonsCount(static_cast<NDb::SummonType>(type), "FactoryLifetime") == count,
			"B13 every produced creature enters the original named owner group");
	}
	struct Index { const char* text; int index; };
	for (const auto& value : {Index{"", 0}, Index{"1.9", 1}, Index{"-0.9", 0}, Index{"cWrapperBonus-1", 1},
		Index{"sLife < rLife", 1}, Index{"-1", 0}, Index{"2", 0}, Index{"2147483520", 0}, Index{"random(0,1)", -1}})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		NaftaFixtureB13 f(isolated.world); auto db = SummonLifetimeDbB13(isolated.world, NDb::SUMMONTYPE_PRIMARY, "0");
		auto* raw = const_cast<NDb::SummonApplicator*>(db.GetPtr());
		raw->summonedUnits[0] = new SummonCountDbB13(isolated.world, "/Mock/SummonFirst.xdb");
		raw->summonedUnits.push_back(new SummonCountDbB13(isolated.world, "/Mock/SummonSecond.xdb")); raw->summonedUnitIndex.sString = value.text;
		NRandom::RandomGenerator expected(*isolated.world->GetRndGen()); const int selected = value.index < 0 ? expected.Next(0, 1) : value.index;
		auto app = f.Create(db); check(app && f.Activate(app), "B13 checked summon selection reaches the original factory");
		const auto units = SummonCountUnitsB13(isolated.world, f.sender);
		check(units.size() == 1 && units[0]->DbUnitDesc() == raw->summonedUnits[selected].GetPtr(),
			"B13 numeric summon selection retains original out-of-list fallback without replacing valid formula indices by zero");
		check(GlyphRngEqualB13(*isolated.world->GetRndGen(), expected), "B13 summon selection consumes exactly its authored seeded draw");
	}
	std::printf("B13 summon counts and selection checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Invalid integer formulas cannot replace/toggle a pet, change limits or advance failed-expression randomness. */
static void SummonCountRejectionB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (int type : {int(NDb::SUMMONTYPE_PRIMARY), int(NDb::SUMMONTYPE_SECONDARY), int(NDb::SUMMONTYPE_PET)})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		NaftaFixtureB13 f(isolated.world); auto db = SummonLifetimeDbB13(isolated.world, type, "0");
		auto first = f.Create(db); check(first && f.Activate(first), "B13 integer rejection fixture creates its actual existing summon");
		const auto old = SummonCountUnitsB13(isolated.world, f.sender); if (old.size() != 1) continue;
		for (int field = 0; field < 4; ++field)
		for (const char* expression : {"cMissing", "1/0", "1e39f", "2147483648", "(-2147483904)", "random(2,4)+2147483904", "compiled-only"})
		{
			auto* raw = new NDb::SummonApplicator; raw->Assign(*db);
			auto* behaviour = new NDb::SummonBehaviourCommon; behaviour->Assign(*static_cast<const NDb::SummonBehaviourCommon*>(db->behaviour.GetPtr()));
			raw->behaviour = behaviour;
			auto& authored = field == 0 ? raw->summonedUnitIndex : field == 1 ? raw->summonedUnitsCount : field == 2 ? behaviour->maxCount : behaviour->maxThisCount;
			authored.sString = expression; authored.compiledString.clear();
			if (std::strcmp(expression, "compiled-only") == 0) { authored.sString.clear(); authored.compiledString = "unsupported"; }
			NDb::Ptr<NDb::SummonApplicator> invalid = raw;
			const NRandom::RandomGenerator saved(*isolated.world->GetRndGen()); auto app = f.Create(invalid);
			check(app && f.Activate(app) && !app->IsAlive() && f.instance->GetActiveApplicatorsCount() == 0,
				"B13 malformed summon integer retires the instant cast with balanced ownership");
			const auto live = SummonCountUnitsB13(isolated.world, f.sender);
			check(live.size() == 1 && live[0] == old[0] && !old[0]->IsDead()
				&& f.sender->GetMaxAllowedSummons(static_cast<NDb::SummonType>(type), "FactoryLifetime") == 1,
				"B13 malformed summon integer cannot replace a creature, toggle a pet or change the owner's advertised limit");
			check(GlyphRngEqualB13(*isolated.world->GetRndGen(), saved), "B13 invalid summon integer range rolls back its own random expression");
		}
	}
	std::printf("B13 summon integer rejection checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Formula-backed group and per-descriptor limits retain the original replacement algorithm. */
static void SummonCountLimitsB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (int type : {int(NDb::SUMMONTYPE_PRIMARY), int(NDb::SUMMONTYPE_SECONDARY)})
	for (bool perDescriptor : {false, true})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		NaftaFixtureB13 f(isolated.world); auto db = SummonLifetimeDbB13(isolated.world, type, "0");
		auto* raw = const_cast<NDb::SummonApplicator*>(db.GetPtr());
		raw->summonedUnits[0] = new SummonCountDbB13(isolated.world, "/Mock/SummonFirst.xdb");
		raw->summonedUnits.push_back(new SummonCountDbB13(isolated.world, "/Mock/SummonSecond.xdb"));
		raw->summonedUnitIndex.sString = "f2l(sender.vChoice)";
		auto* behaviour = const_cast<NDb::SummonBehaviourCommon*>(static_cast<const NDb::SummonBehaviourCommon*>(raw->behaviour.GetPtr()));
		behaviour->maxCount.sString = "f2l(cWrapperBonus)"; behaviour->maxThisCount.sString = perDescriptor ? "cWrapperBonus-1" : "";
		std::vector<CObj<PFBaseSummonedUnit>> old;
		for (int cast = 0; cast < 3; ++cast)
		{
			f.sender->GetVariableVWM("Choice")->SetBaseValue(cast == 2 ? 1 : 0);
			auto app = f.Create(db); check(app && f.Activate(app), "B13 formula limits reach actual repeated summon admission");
			const auto live = SummonCountUnitsB13(isolated.world, f.sender);
			const int expected = cast == 0 || (cast == 1 && perDescriptor) ? 1 : 2;
			check(int(live.size()) == expected && f.sender->GetSummonsCount(static_cast<NDb::SummonType>(type), "FactoryLifetime") == expected
				&& f.sender->GetMaxAllowedSummons(static_cast<NDb::SummonType>(type), "FactoryLifetime") == 2,
				"B13 authored owner and per-descriptor limits govern actual membership and advertised maximum");
			if (cast == 1 && !old.empty()) check(old[0]->IsDead() == perDescriptor,
				"B13 per-descriptor limit replaces only that descriptor before the overall group limit is reached");
			if (cast == 2)
			{
				int selected = 0; for (const auto& unit : live) if (unit->DbUnitDesc() == raw->summonedUnits[1].GetPtr()) ++selected;
				check(selected == 1, "B13 descriptor-specific replacement retains other creature types in the same group");
			}
			old = live;
		}
	}
	std::printf("B13 summon formula limits checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Integer admission remains ordered and lazy; only a failed expression's own draws roll back. */
static void SummonCountOrderB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (int failure = 0; failure <= 5; ++failure)
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		NaftaFixtureB13 f(isolated.world); auto db = SummonLifetimeDbB13(isolated.world, NDb::SUMMONTYPE_PRIMARY, "random(10,20),0");
		auto* raw = const_cast<NDb::SummonApplicator*>(db.GetPtr());
		raw->summonedUnits[0] = new SummonCountDbB13(isolated.world, "/Mock/SummonFirst.xdb");
		raw->summonedUnits.push_back(new SummonCountDbB13(isolated.world, "/Mock/SummonSecond.xdb"));
		raw->summonedUnitIndex.sString = "random(10,20),1"; raw->summonedUnitsCount.sString = "random(10,20),2";
		auto* behaviour = const_cast<NDb::SummonBehaviourCommon*>(static_cast<const NDb::SummonBehaviourCommon*>(raw->behaviour.GetPtr()));
		behaviour->maxCount.sString = "random(10,20),4"; behaviour->maxThisCount.sString = "random(10,20),3";
		if (failure == 1) raw->lifeTime.sString = "random(10,20),cMissing";
		else if (failure < 5)
		{
			auto& bad = failure == 0 ? raw->summonedUnitIndex : failure == 2 ? raw->summonedUnitsCount
				: failure == 3 ? behaviour->maxCount : behaviour->maxThisCount;
			bad.sString = "random(10,20)+2147483904";
		}
		NRandom::RandomGenerator expected(*isolated.world->GetRndGen()); for (int i = 0; i < failure; ++i) expected.Next(10, 20);
		auto app = f.Create(db); check(app && f.Activate(app), "B13 staged summon formulas reach original admission order");
		const auto units = SummonCountUnitsB13(isolated.world, f.sender);
		check(units.size() == (failure == 5 ? 2u : 0u) && GlyphRngEqualB13(*isolated.world->GetRndGen(), expected),
			"B13 summon evaluates selection duration count group limit and descriptor limit once in Windows order before creation");
		for (const auto& unit : units) check(unit->DbUnitDesc() == raw->summonedUnits[1].GetPtr(),
			"B13 seeded admission keeps selected descriptor identity for every new creature");
	}
	for (bool clone : {false, true})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		NaftaFixtureB13 f(isolated.world); auto db = SummonLifetimeDbB13(isolated.world, -1, "0");
		auto* raw = const_cast<NDb::SummonApplicator*>(db.GetPtr()); raw->summonedUnits.clear(); raw->summonedUnitIndex.sString = "cMissing";
		raw->summonSource = clone ? NDb::SUMMONSOURCE_TARGETCLONE : NDb::SUMMONSOURCE_BYAPPLICATOR;
		raw->summonedUnitsCount.sString = "random(1,3)";
		NRandom::RandomGenerator expected(*isolated.world->GetRndGen()); const int count = clone ? expected.Next(1, 3) : 0;
		auto app = f.Create(db); check(app && f.Activate(app), "B13 original source selection admits clones without a descriptor list");
		const auto units = SummonCountUnitsB13(isolated.world, f.sender);
		check(int(units.size()) == count && GlyphRngEqualB13(*isolated.world->GetRndGen(), expected),
			"B13 clone source skips the unused invalid index while missing ordinary descriptors skip later random formulas");
		for (const auto& unit : units) check(unit->DbUnitDesc() == f.receiver->DbUnitDesc(),
			"B13 target clone retains its actual receiver creature descriptor");
	}
	std::printf("B13 summon integer order checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Load the shipped Demonologist summon leaf, its creature and ability constants without substituting its graph. */
static void SummonCountsAuthoredB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		const auto db = NDb::Get<NDb::SummonApplicator>(NDb::DBID("/Items/Talents/Class/Demonolog/SummonDemons.APLR.xdb"));
		const auto talent = NDb::Get<NDb::Talent>(NDb::DBID("/Items/Talents/Class/Demonolog/Ability0.TALENT.xdb"));
		const auto rules = NDb::Get<NDb::AILogicParameters>(NDb::DBID("/GameLogic/AILogic.AILP.xdb"));
		auto* ai = const_cast<NDb::AILogicParameters*>(NDb::SessionRoot::GetRoot()->logicRoot->aiLogic.GetPtr());
		const auto savedConstants = ai->globalConstants;
		if (rules) ai->globalConstants = rules->globalConstants;
		check(rules && rules->globalConstants, "B13 shipped summons use original global damage constants, not literal fallbacks");
		const auto* behaviour = db ? dynamic_cast<const NDb::SummonBehaviourCommon*>(db->behaviour.GetPtr()) : nullptr;
		check(db && talent && behaviour && db->summonedUnits.size() == 1 && db->summonedUnits[0]
			&& db->summonedUnitsCount.sString == "f2l(max(sender.vSaveDemons , 1))" && behaviour->maxCount.sString == "f2l(cA0_MaxDemons)",
			"B13 shipped Demonologist leaf retains its original variable count constant limit and creature");
		if (db && talent && behaviour) for (int saved : {-1, 0, 1, 2, 3})
		{
			B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
			NaftaFixtureB13 f(isolated.world); f.abilityDb = talent.GetPtr();
			f.ability = new PFAbilityData(f.sender.GetPtr(), f.abilityDb, NDb::ABILITYTYPEID_SPECIAL, true, false);
			f.instance = new PFAbilityInstance(f.ability, Target(f.receiver.GetPtr()), true);
			f.sender->GetVariableVWM("SaveDemons")->SetBaseValue(saved);
			auto app = f.Create(db); check(app && f.Activate(app), "B13 original Demonologist summon leaf activates through the real production graph");
			const auto units = SummonCountUnitsB13(isolated.world, f.sender); const int expected = std::max(saved, 1);
			check(int(units.size()) == expected && f.sender->GetSummonsCount(behaviour->summonType, db->summonGroupName) == expected
				&& f.sender->GetMaxAllowedSummons(behaviour->summonType, db->summonGroupName) == 3,
				"B13 original Demonologist leaf produces saved demon count with its authored three-creature owner limit");
			const auto neutral = B8BehaviourMoving(isolated.world, NDb::FACTION_NEUTRAL);
			neutral->SetUnitType(NDb::UNITTYPE_NEUTRALCREEP);
			for (const auto& unit : units)
			{
				check(unit->DbUnitDesc() == db->summonedUnits[0].GetPtr() && unit->HasBehaviour(),
					"B13 authored summon is the original Demon2 creature with its real owned behavior, not a mock substitute");
				auto hit = DirectDamage(); hit.amount = 10; hit.pSender = neutral;
				check(unit->OnBeforeDamage(hit) == 20, "B13 original Demon2 summon applies the shipped global neutral-damage multiplier and target mask");
			}
		}
		ai->globalConstants = savedConstants;
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 authored summon counts checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
