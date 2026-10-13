/** Count real creature/corpse hooks without substituting timer or registration behavior. */
template<class Base>
struct DeadCreatureB13 : Base
{
	int ticks = 0, removals = 0;
	std::function<void()> callback;
	using Base::Base;
	bool Step(float dt) override
	{
		++ticks;
		if (callback) callback();
		return Base::Step(dt);
	}
	void RemoveCorpse() override { ++removals; Base::RemoveCorpse(); }
	float CorpseTime() const { return this->deadUnitLifeTime; }
	float FallTime() const { return this->unitFallUndergroundOffset; }
	float ObserveTime() const { return this->observingTimeOffset; }
	int MovementRandom() const { return this->random; }
};

/** Author independent ordinary, unsummon, observation and fall deadlines. */
static NDb::Ptr<NDb::Creature> DeadCreatureDbB13(PFWorld* world)
{
	auto db = B8BehaviourCreatureDb(world, NDb::FACTION_FREEZE);
	auto* death = new NDb::UnitDeathParameters;
	death->deathTime = 2; death->unsummonTime = .5f;
	death->observeOffset = .75f; death->fallOffset = 1;
	const_cast<NDb::Creature*>(db.GetPtr())->deathParameters = death;
	return db;
}

/** Surviving effects and history expire on actual dead stationary-unit world ticks. */
static void DeadEffectsB13(Checks& check)
{
	B8BehaviourForeignWorld world(check); if (!world.world) return;
	ApplicatorHistoryFixtureB13 f(check, world.world);
	auto db = DeathCleanupStatB13("Survivor", NDb::BUFFBEHAVIOR_DONTSTOPONDEATH);
	const_cast<NDb::StatModApplicator*>(db.GetPtr())->lifeTime.sString = "1";
	auto app = f.Apply(db);
	f.receiver->GetStat(NDb::STAT_LIFEREGENERATION)->SetCoreValue(20);
	f.receiver->GetStat(NDb::STAT_ENERGYREGENERATION)->SetCoreValue(20);
	f.receiver->TakeMana(50);
	const float energy = f.receiver->GetEnergy();
	f.receiver->KillUnit(f.other.GetPtr(), PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
	world.world->Step(.25f, .25f);
	check(app->IsAlive() && f.receiver->IsDead(), "dead unit keeps its authored surviving effect before expiry");
	world.world->Step(.75f, .75f);
	check(!app->IsAlive() && f.receiver->GetStatValue(NDb::STAT_STRENGTH) == 20,
		"dead stationary unit expires its real timed stat contribution through world Step");
	check(f.casts[0]->GetActiveApplicatorsCount() == 0 && ApplicatorHistoryNamedCountB13(f.receiver) == 0,
		"dead effect expiry releases ability ownership and weak names exactly once");
	check(app->GetMarkerTime() == 1 && app->IsLinkedToAppliedRing() && app->IsLinkedToSentRing(),
		"dead effect completion records the same shared world clock at both history endpoints");
	world.world->Step(35, 35);
	check(app->IsLinkedToAppliedRing() && app->IsLinkedToSentRing(), "dead history retains exact 35-second boundary");
	world.world->Step(.125f, .125f);
	check(!app->IsLinkedToAppliedRing() && !app->IsLinkedToSentRing(), "dead history expires after 35 seconds without manual ring calls");
	check(f.receiver->GetHealth() == 0 && f.receiver->GetEnergy() == energy && !f.receiver->GetCurrentTarget(),
		"dead lifecycle does not regenerate resources or acquire a combat target");
}

/** Authored corpse timers retain strict boundaries and the original base/creep ownership split. */
static void DeadCorpseB13(Checks& check)
{
	for (int flags : {0, int(PFBaseUnit::UNITDIEFLAGS_UNSUMMON), int(PFBaseUnit::UNITDIEFLAGS_FORCEREMOVECORPSE),
		int(PFBaseUnit::UNITDIEFLAGS_DONTPLAYDEATHANIMATION)})
	{
		B8BehaviourForeignWorld world(check); if (!world.world) continue;
		auto db = DeadCreatureDbB13(world.world);
		CObj<DeadCreatureB13<B8BehaviourCreep>> creep = new DeadCreatureB13<B8BehaviourCreep>(world.world, *db, NDb::FACTION_FREEZE);
		world.world->Step(0, 0);
		creep->KillUnit(nullptr, flags | PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
		const bool force = (flags & PFBaseUnit::UNITDIEFLAGS_FORCEREMOVECORPSE) == PFBaseUnit::UNITDIEFLAGS_FORCEREMOVECORPSE;
		check(creep->ObserveTime() == .75f, "creature death uses authored observation offset");
		if (force)
		{
			world.world->Step(0, 0);
			check(creep->removals == 1 && creep->IsObjectDead() && !world.world->FindLinuxUnitByObjectId(creep->GetObjectId()),
				"forced creep corpse removal retires the real world identity immediately");
			world.world->Step(.125f, .125f);
			check(creep->ticks == 0 && creep->removals == 1, "removed corpse never reenters a world tick");
			continue;
		}
		const float duration = flags & PFBaseUnit::UNITDIEFLAGS_UNSUMMON ? .5f : 2;
		const int random = creep->MovementRandom();
		check(creep->CorpseTime() == duration, "creature chooses authored ordinary or unsummon lifetime");
		check(creep->FallTime() == (flags == 0 ? 1 : -1), "only ordinary animated death arms the fall timer");
		for (float invalid : {-1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
			world.world->Step(invalid, invalid);
		check(creep->ticks == 0 && creep->CorpseTime() == duration, "invalid world ticks leave corpse timers untouched");
		world.world->Step(duration, duration);
		check(creep->ticks == 1 && creep->removals == 0 && creep->CorpseTime() == 0 && !creep->IsObjectDead(),
			"dead moving creature receives one tick and retains exact authored corpse boundary");
		check(creep->ObserveTime() == .75f - duration && creep->CanObserveNature() == (duration <= .75f),
			"dead creature observation expires with the original inclusive zero policy");
		check(creep->MovementRandom() == random && creep->GetTimeStanding() == 0,
			"dead lifecycle does not advance movement random or movement standing time");
		world.world->Step(.125f, .125f);
		check(creep->ticks == 2 && creep->removals == 1 && creep->IsObjectDead(), "creep corpse retires strictly after its authored deadline");
		world.world->Step(.125f, .125f);
		check(creep->ticks == 2 && creep->removals == 1 && !world.world->FindLinuxUnitByObjectId(creep->GetObjectId()),
			"retired creep cannot be stepped again through a stale unit cache");
	}
	B8BehaviourForeignWorld world(check); if (!world.world) return;
	auto db = DeadCreatureDbB13(world.world);
	CObj<DeadCreatureB13<PFCreature>> creature = new DeadCreatureB13<PFCreature>(world.world, CVec3(5,5,0), CVec2(1,0), *db);
	creature->ChangeHealth(-creature->GetHealth());
	creature->RemoveCorpse();
	world.world->Step(.25f, .25f);
	check(!creature->IsObjectDead() && creature->CorpseTime() == -1 && creature->removals == 1,
		"base creature corpse removal disables the timer without destroying the world object");
}

/** Snapshot membership is rechecked after callbacks; revived moving units are never ticked twice. */
static void DeadSchedulingB13(Checks& check)
{
	B8BehaviourForeignWorld world(check), foreign(check); if (!world.world || !foreign.world) return;
	auto db = DeadCreatureDbB13(world.world);
	using Creep = DeadCreatureB13<B8BehaviourCreep>;
	CObj<Creep> first = new Creep(world.world, *db, NDb::FACTION_FREEZE);
	CObj<Creep> removed = new Creep(world.world, *db, NDb::FACTION_FREEZE);
	CObj<Creep> unsubscribed = new Creep(world.world, *db, NDb::FACTION_FREEZE);
	CObj<Creep> elsewhere = new Creep(foreign.world, *db, NDb::FACTION_FREEZE);
	CObj<Creep> child;
	for (auto* unit : {first.GetPtr(), removed.GetPtr(), unsubscribed.GetPtr(), elsewhere.GetPtr()})
		unit->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
	world.world->Step(0, 0);
	world.world->UnsubscribeObject(unsubscribed.GetPtr());
	first->callback = [&]() {
		if (child) return;
		world.world->UnregisterObject(removed.GetPtr(), false);
		child = new Creep(world.world, *db, NDb::FACTION_FREEZE);
		child->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
	};
	world.world->Step(.25f, .25f);
	check(first->ticks == 1 && removed->ticks == 0 && unsubscribed->ticks == 0 && elsewhere->ticks == 0,
		"dead snapshot excludes callback-removed, unsubscribed and foreign units");
	check(child && child->ticks == 0, "dead units created during callbacks wait until the next snapshot");
	world.world->Step(.25f, .25f);
	check(first->ticks == 2 && child && child->ticks == 1, "eligible old and newborn corpses tick exactly once");
	first->callback = nullptr;

	CObj<MovingTickB12> living = new MovingTickB12(world.world, *db);
	CObj<MovingTickB12> revived = new MovingTickB12(world.world, *db);
	revived->ChangeHealth(-revived->GetHealth());
	living->callback = [&]() { revived->ChangeHealth(100); };
	world.world->Step(.25f, .25f);
	check(!revived->IsDead() && living->ticks == 1 && revived->ticks == 1,
		"moving unit revived before movement reaches it advances only through its living owner");
	living->callback = nullptr;
}

/** Missing/malformed timers do not fabricate a removal deadline or poison simulation state. */
static void DeadTimerGuardsB13(Checks& check)
{
	for (float timer : {-2.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		B8BehaviourForeignWorld world(check); if (!world.world) continue;
		auto db = DeadCreatureDbB13(world.world);
		auto* death = const_cast<NDb::UnitDeathParameters*>(db->deathParameters.GetPtr());
		death->deathTime = death->observeOffset = death->fallOffset = timer;
		CObj<DeadCreatureB13<B8BehaviourCreep>> creep = new DeadCreatureB13<B8BehaviourCreep>(world.world, *db, NDb::FACTION_FREEZE);
		creep->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
		const float expected = std::isfinite(timer) ? timer : -1;
		check(creep->CorpseTime() == expected && creep->FallTime() == expected && creep->ObserveTime() == expected,
			"finite negative authored timers survive and nonfinite timers become disabled");
		for (float invalid : {-1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
		{
			creep->Step(invalid);
			check(creep->CorpseTime() == expected && creep->FallTime() == expected && creep->ObserveTime() == expected,
				"direct creature Step rejects invalid time before mutating base or corpse state");
		}
		world.world->Step(10, 10);
		check(!creep->IsObjectDead() && creep->removals == 0 && !creep->CanObserveNature(),
			"disabled negative corpse timers do not create a synthetic deadline");
	}
	B8BehaviourForeignWorld world(check); if (!world.world) return;
	auto db = B8BehaviourCreatureDb(world.world, NDb::FACTION_FREEZE);
	CObj<DeadCreatureB13<B8BehaviourCreep>> creep = new DeadCreatureB13<B8BehaviourCreep>(world.world, *db, NDb::FACTION_FREEZE);
	creep->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
	world.world->Step(10, 10);
	check(creep->ticks == 1 && creep->CorpseTime() == -1 && creep->removals == 0 && !creep->IsObjectDead(),
		"missing death descriptor retains base lifecycle without invented one-second removal");
}

/** Dead hero effects advance even with respawn forbidden; enabled respawn has one tick owner. */
static void DeadHeroesB13(Checks& check)
{
	for (bool forbid : {false, true})
	{
		B8BehaviourForeignWorld world(check); if (!world.world) continue;
		ApplicatorHistoryFixtureB13 f(check, world.world);
		auto source = DeadCreatureDbB13(world.world);
		CObj<MovingTickB12> reviver = new MovingTickB12(world.world, *source);
		auto* raw = new NDb::Hero;
		raw->stats = source->stats; raw->targetingParams = source->targetingParams; raw->deathParameters = source->deathParameters;
		PFBaseHero::SpawnInfo spawn; spawn.pHero = raw;
		spawn.placement = Placement(CVec3(5,5,0), QNULL, CVec3(1,1,1));
		CObj<DeadCreatureB13<B8BehaviourHero>> hero = new DeadCreatureB13<B8BehaviourHero>(world.world, spawn, NDb::FACTION_FREEZE);
		CObj<MovingTickB12> killer = new MovingTickB12(world.world, *source);
		hero->SetForbidRespawn(forbid);
		auto db = DeathCleanupStatB13("DeadHero", NDb::BUFFBEHAVIOR_DONTSTOPONDEATH);
		const_cast<NDb::StatModApplicator*>(db.GetPtr())->lifeTime.sString = ".5";
		auto app = f.Apply(db, 0, nullptr, hero.GetPtr());
		hero->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
		const float delay = hero->GetTimeToRespawn();
		world.world->Step(.25f, .25f);
		check(hero->ticks == 1 && hero->IsDead() && app->IsAlive(), "dead hero receives one effect/lifecycle tick regardless of respawn policy");
		check(hero->GetTimeToRespawn() == (forbid ? delay : delay - .25f), "hero respawn timer has exactly one simulation owner");
		world.world->Step(.25f, .25f);
		check(hero->ticks == 2 && !app->IsAlive() && f.casts[0]->GetActiveApplicatorsCount() == 0,
			"dead hero effect expires at the authored world deadline");
		if (!forbid)
		{
			world.world->Step(delay, delay);
			check(!hero->IsDead() && hero->ticks == 3, "dead phase performs resurrection without same-tick double stepping");
			world.world->Step(.25f, .25f);
			check(hero->ticks == 4, "resurrected hero transfers to living movement on the next tick");
		}
		hero->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
		int count = hero->ticks;
		reviver->callback = [&]() { hero->Resurrect(); };
		killer->callback = [&]() { hero->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS); };
		world.world->Step(.25f, .25f);
		check(hero->IsDead() && hero->ticks == count + 1,
			"hero revived, fully stepped and killed again during movement cannot also receive the dead phase");
		reviver->callback = nullptr; killer->callback = nullptr;
		count = hero->ticks;
		world.world->UnsubscribeObject(hero.GetPtr());
		world.world->Step(.25f, .25f);
		check(hero->ticks == count, "dead hero cannot bypass subscription through a separate respawn scan");
		hero->CleanupHistoryApplicators(); hero->Cleanup();
	}
}

/** Direct hero calls must reject invalid time before touching respawn and corpse state. */
static void DeadHeroTickGuardsB13(Checks& check)
{
	for (float dt : {0.f, -1.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
	{
		B8BehaviourForeignWorld fixture(check); if (!fixture.world) continue;
		auto source = DeadCreatureDbB13(fixture.world);
		auto* raw = new NDb::Hero;
		raw->stats = source->stats; raw->targetingParams = source->targetingParams; raw->deathParameters = source->deathParameters;
		PFBaseHero::SpawnInfo spawn; spawn.pHero = raw;
		spawn.placement = Placement(CVec3(5,5,0), QNULL, CVec3(1,1,1));
		CObj<DeadCreatureB13<B8BehaviourHero>> hero = new DeadCreatureB13<B8BehaviourHero>(fixture.world, spawn, NDb::FACTION_FREEZE);
		hero->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
		fixture.world->Step(.125f, .125f);
		const float respawn = hero->GetTimeToRespawn(), corpse = hero->CorpseTime();
		const int hidden = hero->GetHiddenCounter();
		hero->PFBaseHero::Step(dt);
		check(hero->IsDead() && hero->GetTimeToRespawn() == respawn && hero->CorpseTime() == corpse
			&& hero->GetHiddenCounter() == hidden, "nonadvancing direct hero tick preserves active respawn corpse and hide state");
		fixture.world->Step(.125f, .125f);
		check(hero->IsDead() && hero->GetTimeToRespawn() == respawn - .125f && hero->CorpseTime() == corpse - .125f,
			"valid world tick after rejected hero time remains usable");
	}
}

static void DeadWorldB13(Checks& check)
{
	DeadEffectsB13(check);
	DeadCorpseB13(check);
	DeadSchedulingB13(check);
	DeadTimerGuardsB13(check);
	DeadHeroesB13(check);
	DeadHeroTickGuardsB13(check);
}
