/** Actual teleports preserve authored pursuit policy and notify all inbound projectiles. */
static void DispatchTeleportB11(Checks& check, PFWorld* world)
{
	for (auto aiming : {NDb::DISPATCHAIMING_STRAIGHT, NDb::DISPATCHAIMING_HOMING, NDb::DISPATCHAIMING_PURSUE})
	for (bool notify : {false, true})
	{
		CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
		sender->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		CObj<B8BehaviourCreep> receiver = B8BehaviourMoving(world, NDb::FACTION_BURN);
		receiver->TeleportTo(CVec2(15, 5));
		NDb::BaseAttack* raw = B11AttackDescriptor(2);
		NDb::DispatchLinear* linear = const_cast<NDb::DispatchLinear*>(
			dynamic_cast<const NDb::DispatchLinear*>(raw->dispatch.GetPtr()));
		linear->aiming = aiming;
		linear->maxDeviation = 180;
		CObj<PFAbilityData> data = new PFAbilityData(sender.GetPtr(), raw, NDb::ABILITYTYPEID_SPECIAL);
		vector<CObj<PFAbilityInstance> > casts;
		vector<CObj<PFDispatch> > dispatches;
		for (int index = 0; index < 2; ++index)
		{
			CObj<PFAbilityInstance> cast = sender->CreateAbilityInstance(data, Target(receiver.GetPtr()));
			casts.push_back(cast);
			dispatches.push_back(cast ? B11FindDispatch(world, cast.GetPtr()) : CObj<PFDispatch>());
			check(cast && dispatches.back(), "B11 teleport fixture launches real inbound projectiles");
		}
		sender->Step(.25f);
		check(receiver->TeleportTo(CVec2(15, 15), true, notify)
			&& receiver->GetPos() == CVec2(15, 15), "B11 production teleport updates actual unit placement");
		const bool retained = !notify || aiming == NDb::DISPATCHAIMING_PURSUE;
		for (const auto& dispatch : dispatches)
			check(dispatch && dispatch->GetTarget().IsUnit() == retained
				&& (retained || dispatch->GetTarget().AcquirePosition().AsVec2D() == CVec2(15, 5)),
				"B11 teleport applies authored target-loss policy to each inbound dispatch");
		for (int tick = 0; tick < 12; ++tick) sender->Step(.25f);
		check(receiver->GetHealth() == (retained ? 950 : 1000),
			"B11 only retained projectiles deliver damage after a real target teleport");
		for (const auto& cast : casts)
			check(cast && cast->IsFinished(), "B11 teleported projectile completes its actual ability lifetime");
	}
}
