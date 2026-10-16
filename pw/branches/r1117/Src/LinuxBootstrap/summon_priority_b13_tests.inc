/** Real health ratios and elapsed behavior time make original removal priority independent of insertion order. */
static void SummonPriorityB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (int type : {int(NDb::SUMMONTYPE_PRIMARY), int(NDb::SUMMONTYPE_SECONDARY), int(NDb::SUMMONTYPE_PET)})
	for (bool noHealth : {false, true}) for (bool equalHealth : {false, true}) for (int count : {0, 1, 2, 3, 4})
	{
		int order[] = {0, 1, 2};
		do
		{
			B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
			SummonLifetimeFixtureB13 f(isolated.world); SummonLifetimeFixtureB13::Entry units[3];
			for (int index : order)
			{
				units[index] = f.Add(type, 0, noHealth ? PFSummonBehaviour::BEHAVIOURFLAGS_NOHEALTHPRIORITY : 0);
				units[index].unit->GetStat(NDb::STAT_LIFE)->SetCoreValue(index == 1 ? 1000 : 100);
			}
			isolated.world->Step(0, 0);
			const float age[] = {3, 1, 2}, ratio[] = {.8f, .2f, .5f};
			for (int index = 0; index < 3; ++index)
			{
				units[index].unit->SetHealth(units[index].unit->GetMaxHealth() * (equalHealth ? .5f : ratio[index]));
				units[index].behaviour->PFSummonBehaviour::OnStep(age[index]);
			}
			const int removed = f.master->RemoveSummons(count, static_cast<NDb::SummonType>(type), "Lifetime", nullptr, 3);
			const int expected = count <= 3 ? count : 0;
			check(removed == expected && f.master->GetSummonsCount(static_cast<NDb::SummonType>(type), "Lifetime") == 3 - expected,
				"B13 original summon removal count rejects oversized requests instead of clamping them to all live units");
			const int rankByHealth[] = {2, 0, 1}, rankByAge[] = {0, 2, 1};
			for (int index = 0; index < 3; ++index)
			{
				const bool dead = (noHealth || equalHealth ? rankByAge[index] : rankByHealth[index]) < expected;
				check(units[index].unit->IsDead() == dead && f.Count(units[index].unit, NDb::BASEUNITEVENT_UNSUMMON) == int(dead),
					"B13 default replacement uses ascending health percentage then descending age unless health priority is disabled");
			}
		}
		while (std::next_permutation(std::begin(order), std::end(order)));
	}
	std::printf("B13 summon removal priority checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Either operand's NOHEALTHPRIORITY flag makes a two-unit comparison use elapsed age. */
static void SummonMixedPriorityB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (int flags = 0; flags < 4; ++flags) for (bool reversed : {false, true})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		SummonLifetimeFixtureB13 f(isolated.world); SummonLifetimeFixtureB13::Entry units[2];
		for (int i = 0; i < 2; ++i)
		{
			const int index = reversed ? 1 - i : i;
			units[index] = f.Add(NDb::SUMMONTYPE_PRIMARY, 0, flags & (1 << index) ? PFSummonBehaviour::BEHAVIOURFLAGS_NOHEALTHPRIORITY : 0);
			units[index].unit->SetHealth(units[index].unit->GetMaxHealth() * (index ? .25f : .75f));
			units[index].behaviour->PFSummonBehaviour::OnStep(index ? 1 : 2);
		}
		check(f.master->RemoveSummons(1, NDb::SUMMONTYPE_PRIMARY, "Lifetime") == 1,
			"B13 mixed health-priority removal accepts one actual group member");
		check(units[flags ? 0 : 1].unit->IsDead() && !units[flags ? 1 : 0].unit->IsDead(),
			"B13 either disabled health priority preserves original age-only pair comparison");
	}
	std::printf("B13 mixed summon priority checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Original capacity arithmetic is checked before narrowing and cannot turn malformed counts into mass removal. */
static void SummonRemovalCountsB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (int existing : {0, 1, 3}) for (int requested : {-2, 0, 1, 2, 3, 4, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
	for (int maximum : {-1, 0, 1, 2, 3, 5, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		SummonLifetimeFixtureB13 f(isolated.world); for (int i = 0; i < existing; ++i) f.Add(NDb::SUMMONTYPE_PRIMARY, 0);
		const long long amount = maximum > 0 ? std::max(0LL, static_cast<long long>(existing) - maximum + requested) : requested;
		const int expected = requested >= 0 && amount > 0 && amount <= existing ? static_cast<int>(amount) : 0;
		const int removed = f.master->RemoveSummons(requested, NDb::SUMMONTYPE_PRIMARY, "Lifetime", nullptr, maximum);
		check(removed == expected && f.master->GetSummonsCount(NDb::SUMMONTYPE_PRIMARY, "Lifetime") == existing - expected,
			"B13 original capacity policy removes only a positive feasible count with overflow-safe signed arithmetic");
	}
	std::printf("B13 summon removal arithmetic checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Named and descriptor filters isolate independent owners, groups, creature identities and summon categories. */
static void SummonRemovalFiltersB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (bool descriptor : {false, true}) for (bool sameIdentity : {false, true})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		SummonLifetimeFixtureB13 f(isolated.world), other(isolated.world);
		NDb::Ptr<NDb::Summoned> firstDb = new SummonCountDbB13(isolated.world, "/Mock/PriorityFirst.xdb");
		NDb::Ptr<NDb::Summoned> secondDb = new SummonCountDbB13(isolated.world, sameIdentity ? "/Mock/PriorityFirst.xdb" : "/Mock/PrioritySecond.xdb");
		f.creatureDb = firstDb.GetPtr(); auto first = f.Add(NDb::SUMMONTYPE_PRIMARY, 0);
		f.creatureDb = secondDb.GetPtr(); auto second = f.Add(NDb::SUMMONTYPE_PRIMARY, 0);
		auto named = f.Add(NDb::SUMMONTYPE_PRIMARY, 0); named.behaviour->SetGroupName("Other");
		auto secondary = f.Add(NDb::SUMMONTYPE_SECONDARY, 0), foreign = other.Add(NDb::SUMMONTYPE_PRIMARY, 0);
		first.unit->SetHealth(first.unit->GetMaxHealth() * .75f); second.unit->SetHealth(second.unit->GetMaxHealth() * .25f);
		for (auto entry : {named, secondary, foreign}) entry.unit->SetHealth(1);
		check(f.master->RemoveSummons(1, NDb::SUMMONTYPE_PRIMARY, "Lifetime", descriptor ? firstDb.GetPtr() : nullptr) == 1,
			"B13 replacement filters admit an existing creature with exact named owner membership");
		const bool selectFirst = descriptor && !sameIdentity;
		check(first.unit->IsDead() == selectFirst && second.unit->IsDead() != selectFirst,
			"B13 descriptor filtering compares original DB identity rather than allocation address before applying health priority");
		check(!named.unit->IsDead() && !secondary.unit->IsDead() && !foreign.unit->IsDead(),
			"B13 lower-health creatures outside selected group category descriptor or owner remain untouched");
	}
	std::printf("B13 summon removal filters checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Death callbacks may retire other selected units; actual behaviors must survive the removal traversal. */
static void SummonRemovalReentryB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (int type : {int(NDb::SUMMONTYPE_PRIMARY), int(NDb::SUMMONTYPE_SECONDARY), int(NDb::SUMMONTYPE_PET)})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		SummonLifetimeFixtureB13 f(isolated.world); for (int i = 0; i < 3; ++i) f.Add(type, 0);
		for (auto& entry : f.units) entry.behaviour = nullptr;
		bool nested = false;
		f.callback = [&](const PFBaseUnitEventUnitDeath&) {
			if (nested) return;
			nested = true;
			f.master->RemoveSummons(3, static_cast<NDb::SummonType>(type), "Lifetime");
		};
		check(f.master->RemoveSummons(3, static_cast<NDb::SummonType>(type), "Lifetime") == 3,
			"B13 removal retains original accepted count while nested callbacks can drain selected members");
		f.callback = nullptr;
		for (const auto& entry : f.units) check(entry.unit->IsDead() && f.Count(entry.unit, NDb::BASEUNITEVENT_UNSUMMON) == 1,
			"B13 nested summon removal emits exactly one local unsummon for every selected creature");
		check(f.master->GetSummonsCount(static_cast<NDb::SummonType>(type), "Lifetime") == 0,
			"B13 callback-driven removal drains the original group without retaining stale behavior ownership");
	}
	std::printf("B13 summon removal reentry checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Exercise both small and partitioned paths of the existing nstl sort with owning behavior snapshots. */
static void SummonLargePriorityB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (int size : {13, 16, 17, 32, 64}) for (int remove : {1, 5, size / 2, size})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		SummonLifetimeFixtureB13 f(isolated.world); std::vector<CObj<PFBaseSummonedUnit>> units(size);
		for (int i = 0; i < size; ++i)
		{
			const int index = (i * 7) % size; auto entry = f.Add(NDb::SUMMONTYPE_SECONDARY, 0);
			entry.unit->SetHealth(entry.unit->GetMaxHealth() * float(index + 1) / float(size + 1)); units[index] = entry.unit.GetPtr();
		}
		check(f.master->RemoveSummons(remove, NDb::SUMMONTYPE_SECONDARY, "Lifetime") == remove,
			"B13 original sorting selects the requested feasible prefix in larger summon groups");
		for (int index = 0; index < size; ++index) check(units[index]->IsDead() == (index < remove),
			"B13 partitioned sort keeps health priority and owning snapshot integrity across larger groups");
	}
	std::printf("B13 large summon priority checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Real recasts use the restored priority before inserting a replacement and naturally retire its old corpse. */
static void SummonPriorityApplicatorB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (int type : {int(NDb::SUMMONTYPE_PRIMARY), int(NDb::SUMMONTYPE_SECONDARY)}) for (bool noHealth : {false, true})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		NaftaFixtureB13 f(isolated.world); auto db = SummonLifetimeDbB13(isolated.world, type, "0");
		auto* raw = const_cast<NDb::SummonApplicator*>(db.GetPtr()); raw->summonedUnitsCount.sString = "f2l(sender.vReplacementCount)";
		if (noHealth) raw->flags = static_cast<NDb::ESummonFlags>(raw->flags | NDb::SUMMONFLAGS_NOHEALTHPRIORITY);
		auto* behaviour = const_cast<NDb::SummonBehaviourCommon*>(static_cast<const NDb::SummonBehaviourCommon*>(raw->behaviour.GetPtr()));
		behaviour->maxCount.sString = "2";
		f.sender->GetVariableVWM("ReplacementCount")->SetBaseValue(2); auto first = f.Create(db);
		check(first && f.Activate(first), "B13 priority fixture spawns a full group through its real applicator");
		const auto old = SummonCountUnitsB13(isolated.world, f.sender);
		check(old.size() == 2, "B13 real replacement starts with two owned summons"); if (old.size() != 2) continue;
		for (int i = 0; i < 2; ++i)
		{
			old[i]->SetHealth(old[i]->GetMaxHealth() * (i ? .25f : .75f));
			auto* timer = const_cast<PFSummonBehaviour*>(dynamic_cast<const PFSummonBehaviour*>(old[i]->GetBehaviour()));
			check(timer != nullptr, "B13 actual summoned creature retains its original timed behavior");
			if (timer) timer->PFSummonBehaviour::OnStep(i ? 1 : 2);
		}
		f.sender->GetVariableVWM("ReplacementCount")->SetBaseValue(1); auto again = f.Create(db);
		check(again && f.Activate(again), "B13 real recast reaches original capacity replacement");
		const auto live = SummonCountUnitsB13(isolated.world, f.sender); const int removed = noHealth ? 0 : 1;
		check(old[removed]->IsDead() && !old[1 - removed]->IsDead() && live.size() == 2
			&& f.sender->GetSummonsCount(static_cast<NDb::SummonType>(type), "FactoryLifetime") == 2
			&& f.instance->GetActiveApplicatorsCount() == 0,
			"B13 actual summon recast removes the authored health/age priority without losing the surviving creature or cast balance");
		isolated.world->Step(.625f, .625f);
		check(old[removed]->IsObjectDead() && !old[1 - removed]->IsObjectDead(),
			"B13 priority replacement retires only the selected original summoned corpse");
	}
	std::printf("B13 summon applicator priority checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
