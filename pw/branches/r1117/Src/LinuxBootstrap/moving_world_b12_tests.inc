/** Tick observer preserves the production movement and unit simulation implementations. */
struct MovingTickB12 : B8BehaviourCreep
{
	int ticks = 0;
	std::function<void()> callback;
	MovingTickB12(PFWorld* world, const NDb::Creature& db) : B8BehaviourCreep(world, db, NDb::FACTION_FREEZE)
	{
		AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		SetMoveFlags(MOVE_FLAG_NO_COLLIDE | MOVE_FLAG_OVERRIDE_SPEED); SetUnitSpeed(2);
	}
	bool Step(float dt) override
	{
		++ticks;
		if (callback) callback();
		return PFBaseMovingUnit::Step(dt);
	}
};

/** Removal callbacks must not tick a stale global-cache entry or invalidate the movement snapshot. */
static void MovingWorldB12(Checks& check, bool invalidTicks)
{
	B8BehaviourForeignWorld fixture(check), foreign(check);
	if (!fixture.world || !foreign.world) return;
	PFWorld* world = fixture.world;
	NDb::Ptr<NDb::Creature> db = B8BehaviourCreatureDb(world, NDb::FACTION_FREEZE);
	CObj<MovingTickB12> first = new MovingTickB12(world, *db);
	CObj<MovingTickB12> removed = new MovingTickB12(world, *db);
	CObj<MovingTickB12> unsubscribed = new MovingTickB12(world, *db);
	CObj<MovingTickB12> elsewhere = new MovingTickB12(foreign.world, *db);
	CObj<MovingTickB12> child;
	world->Step(0, 0);
	world->UnsubscribeObject(unsubscribed.GetPtr());
	first->callback = [&]() {
		if (child) return;
		world->UnregisterObject(removed.GetPtr(), false);
		child = new MovingTickB12(world, *db);
	};
	check(first->MoveTo(CVec2(15, 5)), "B12 moving lifecycle fixture uses a production movement request");
	if (invalidTicks)
	{
		for (float dt : {0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
			MovingUnit::UpdateMovements(world->GetAIWorld(), nullptr, world->GetTileMap(), dt);
		check(first->ticks == 0 && !child && first->GetPos().x == 5,
			"B12 direct movement scheduler rejects invalid ticks before callbacks or movement");
		first->callback = nullptr;
		return;
	}
	world->Step(.5f, .5f);
	check(first->ticks == 1 && first->GetPos().x == 6, "B12 live moving unit and position advance exactly once");
	check(removed->ticks == 0 && unsubscribed->ticks == 0 && elsewhere->ticks == 0,
		"B12 movement phase excludes removed, unsubscribed and foreign units");
	check(child && child->ticks == 0, "B12 units born during movement wait for the next snapshot");
	world->Step(.5f, .5f);
	check(first->ticks == 2 && first->GetPos().x == 7 && child && child->ticks == 1,
		"B12 retained and newborn moving units receive one eligible tick");
	first->callback = nullptr;
	first->ChangeHealth(-first->GetHealth());
	world->Step(.5f, .5f);
	check(first->ticks == 2 && removed->ticks == 0, "B12 death or unregistration cannot resume moving gameplay");
}
