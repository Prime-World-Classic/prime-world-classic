/** Observe scheduling while retaining production unit effects, abilities and state stepping. */
struct StationaryTickB12 : ProbeUnit
{
	int ticks = 0;
	float elapsed = 0;
	std::function<void()> callback;
	StationaryTickB12(PFWorld* world, const NDb::Unit* db) : ProbeUnit(world, db, NDb::FACTION_FREEZE) {}
	bool Step(float dt) override
	{
		++ticks; elapsed += dt;
		if (callback) callback();
		return PFBaseUnit::Step(dt);
	}
};

/** Stationary lifecycle follows the real step registry, not the global unit cache. */
static void StationaryWorldB12(Checks& check)
{
	B8BehaviourForeignWorld fixture(check), foreign(check);
	if (!fixture.world || !foreign.world) return;
	PFWorld* world = fixture.world;
	CObj<PFBaseUnit> seed = Unit(world, NDb::FACTION_FREEZE, 20);
	CObj<StationaryTickB12> first = new StationaryTickB12(world, seed->DbUnitDesc());
	CObj<StationaryTickB12> removed = new StationaryTickB12(world, seed->DbUnitDesc());
	CObj<StationaryTickB12> unsubscribed = new StationaryTickB12(world, seed->DbUnitDesc());
	CObj<StationaryTickB12> elsewhere = new StationaryTickB12(foreign.world, seed->DbUnitDesc());
	CObj<StationaryTickB12> child;
	world->Step(0, 0);
	world->UnsubscribeObject(unsubscribed.GetPtr());
	first->callback = [&]() {
		if (child) return;
		world->UnregisterObject(removed.GetPtr(), false);
		child = new StationaryTickB12(world, seed->DbUnitDesc());
	};
	for (float dt : {0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
		world->Step(dt, dt);
	check(first->ticks == 0 && !child, "B12 invalid and zero ticks do not run stationary gameplay");
	world->Step(.5f, .5f);
	check(first->ticks == 1 && first->elapsed == .5f, "B12 stationary unit receives exactly one world tick");
	check(removed->ticks == 0 && unsubscribed->ticks == 0 && elsewhere->ticks == 0,
		"B12 removed, unsubscribed and foreign stationary units never receive this world tick");
	check(child && child->ticks == 0, "B12 stationary objects created during a tick wait until the next tick");
	world->Step(.5f, .5f);
	check(first->ticks == 2 && child && child->ticks == 1, "B12 retained and newborn stationary objects advance once on their eligible tick");
	first->callback = nullptr;
	first->ChangeHealth(-first->GetHealth());
	world->Step(.5f, .5f);
	check(first->ticks == 2, "B12 dead stationary units do not resume gameplay");
}

/** Stationary casts and real tower guards deliver through existing ability/dispatch ownership. */
static void StationaryCombatB12(Checks& check)
{
	B8BehaviourForeignWorld fixture(check);
	if (!fixture.world) return;
	PFWorld* world = fixture.world;
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 20);
	CObj<PFBaseUnit> target = Unit(world, NDb::FACTION_BURN, 20);
	CObj<PFAbilityData> ability = new PFAbilityData(sender.GetPtr(), B11TimedPayload("1"), NDb::ABILITYTYPEID_SPECIAL);
	CObj<PFAbilityInstance> cast = sender->CreateAbilityInstance(ability, Target(target.GetPtr()));
	check(cast != nullptr, "B12 stationary caster admits an authored timed ability");
	world->Step(.5f, .5f);
	check(target->GetHealth() == 1000, "B12 stationary primary dispatch does not double advance through the detached phase");
	world->Step(.5f, .5f);
	check(target->GetHealth() == 975 && cast && cast->IsFinished(), "B12 stationary primary dispatch delivers at its authored deadline");
	world->Step(.5f, .5f);
	check(target->GetHealth() == 975, "B12 stationary primary payload does not replay after completion");
	NDb::Tower* towerDb = new NDb::Tower;
	towerDb->stats = sender->DbUnitDesc()->stats; towerDb->faction = NDb::FACTION_FREEZE;
	NDb::AdvMapObject object; object.gameObject = towerDb;
	// Face the target at (5,5); the empty mock AI root supplies no turret rotation speed.
	object.offset = CPlacement(CVec3(5, 7, 0), QNULL, CVec3(1, 1, 1));
	CObj<PFTower> tower = new PFTower(world, object);
	tower->GetStat(NDb::STAT_RANGE)->SetCoreValue(10);
	tower->GetStat(NDb::STAT_VISIBILITYRANGE)->SetCoreValue(20); tower->OpenWarFog();
	tower->ReplaceBaseAttack(new PFBaseAttackData(tower.GetPtr(), B11AttackDescriptor(0)), false);
	world->Step(0, 0);
	world->Step(.1f, .1f);
	check(tower->GetCurrentTarget() == target.GetPtr(), "B12 existing tower guard acquires its target during world simulation");
	world->Step(.1f, .1f);
	check(target->GetHealth() == 950, "B12 existing tower attack payload damages exactly once through world scheduling");
}
