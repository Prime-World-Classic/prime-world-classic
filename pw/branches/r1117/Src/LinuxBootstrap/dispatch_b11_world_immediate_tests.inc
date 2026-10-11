/** Detached immediate payloads advance on real world ticks, with terminal cancel/death handling. */
static void DispatchWorldImmediateB11(Checks& check)
{
	for (int mode = 0; mode < 4; ++mode)
	{
		B8BehaviourForeignWorld isolated(check);
		PFWorld* world = isolated.world;
		if (!world) continue;
		CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
		CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 20);
		NDb::Ptr<NDb::Ability> db = B11AttackDescriptor(0);
		CObj<PFAbilityData> data = new PFAbilityData(sender.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL);
		CObj<PFAbilityInstance> cast = new PFAbilityInstance(data, Target(receiver.GetPtr()), false);
		CObj<PFDispatch> dispatch = CreateDispatch(cast, nullptr, Target(sender.GetPtr()),
			Target(receiver.GetPtr()), db.GetPtr(), 0, mode != 1, 0);
		check(dispatch && receiver->GetHealth() == 1000, "B11 detached immediate awaits its first world tick");
		if (!dispatch) continue;
		if (mode == 1)
		{
			world->Step(1, 1);
			check(!dispatch->Started() && receiver->GetHealth() == 1000,
				"B11 world cannot start an unstarted detached payload");
			dispatch->Start();
		}
		if (mode == 2) { dispatch->Cancel(); dispatch->Cancel(); }
		if (mode == 3) receiver->ChangeHealth(-1000);
		world->Step(.1f, .1f);
		const float expected = mode == 3 ? 0 : mode == 2 ? 1000 : 975;
		check(dispatch->IsObjectDead() && receiver->GetHealth() == expected,
			"B11 detached world phase delivers or cancels the actual immediate payload");
		world->Step(.1f, .1f);
		check(receiver->GetHealth() == expected, "B11 detached world completion cannot replay damage");
	}
}
