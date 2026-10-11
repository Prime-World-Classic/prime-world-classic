/** Empty payload rings do not mean timed, travelling, or persistent dispatch work has ended. */
static void DispatchPersistentB11(Checks& check, PFWorld* world)
{
	for (int kind = 0; kind < 6; ++kind)
	{
		CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
		CObj<B8BehaviourCreep> receiver = B8BehaviourMoving(world, NDb::FACTION_BURN);
		sender->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		receiver->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		check(receiver->TeleportTo(CVec2(15, 5)), "B11 persistent fixture has a positioned real receiver");
		NDb::Ability* raw = new NDb::Ability;
		NDb::DispatchContinuous* continuous = nullptr;
		if (kind == 4)
			continuous = new NDb::DispatchContinuous;
		else if (kind == 5)
		{
			NDb::DispatchLinear* linear = new NDb::DispatchLinear;
			linear->speed = 5;
			linear->aiming = NDb::DISPATCHAIMING_PURSUE;
			continuous = linear;
		}
		else
		{
			NDb::DispatchByTime* timed = new NDb::DispatchByTime;
			timed->lifeTime.sString = kind == 3 ? "-1" : "2";
			timed->dieAfterApply = kind != 2;
			continuous = timed;
		}
		const bool persistent = kind >= 1 && kind <= 4;
		if (persistent)
		{
			NDb::StatModApplicator* modifier = new NDb::StatModApplicator;
			modifier->lifeTime.sString = kind == 3 ? "1" : "10";
			modifier->modifier.stat = NDb::STAT_STRENGTH;
			modifier->modifier.multValue.sString = "1";
			modifier->modifier.addValue.sString = "5";
			continuous->persistentApplicators.push_back(modifier);
		}
		raw->dispatch = continuous;
		CObj<PFAbilityData> data = new PFAbilityData(sender.GetPtr(), raw, NDb::ABILITYTYPEID_SPECIAL);
		CObj<PFAbilityInstance> cast = sender->CreateAbilityInstance(data, Target(receiver.GetPtr()));
		CObj<PFDispatch> dispatch = cast ? B11FindDispatch(world, cast.GetPtr()) : CObj<PFDispatch>();
		check(cast && dispatch && !cast->IsFinished() && dispatch->GetApplicators().empty(),
			"B11 empty payload keeps unfinished authored dispatch ownership");
		check(cast && cast->GetActiveApplicatorsCount() == (persistent ? 1 : 0)
			&& receiver->GetStatValue(NDb::STAT_STRENGTH) == (persistent ? 25 : 20),
			"B11 persistent applicators start their actual stat effect and ability counter");
		if (!cast || !dispatch) continue;
		receiver->Step(.5f);
		check(receiver->GetStatValue(NDb::STAT_STRENGTH) == (persistent ? 25 : 20),
			"B11 receiver stepping does not take over dispatch-owned persistent lifetime");
		sender->Step(.5f);
		check(!cast->IsFinished() && !dispatch->IsObjectDead(),
			"B11 unit keeps payload-free work before the travel or persistence deadline");
		sender->Step(.5f);
		if (kind == 3)
		{
			check(receiver->GetStatValue(NDb::STAT_STRENGTH) == 20 && cast->GetActiveApplicatorsCount() == 0
				&& !dispatch->IsObjectDead(), "B11 negative lifetime waits for actual persistent-ring expiry");
			sender->Step(.125f);
			check(dispatch->IsObjectDead() && cast->IsFinished(),
				"B11 original next-step empty-ring policy completes negative timed lifetime");
		}
		else
		{
			sender->Step(1);
			const bool remains = kind == 2 || kind == 4;
			check(dispatch->IsObjectDead() != remains && cast->IsFinished() != remains,
				"B11 travel completion respects authored dieAfterApply and continuous ownership");
			if (remains)
			{
				check(receiver->GetStatValue(NDb::STAT_STRENGTH) == 25 && cast->GetActiveApplicatorsCount() == 1,
					"B11 persistent effect remains owned until explicit cancellation");
				cast->Cancel();
			}
		}
		check(cast->IsFinished() && cast->GetActiveApplicatorsCount() == 0
			&& receiver->GetStatValue(NDb::STAT_STRENGTH) == 20,
			"B11 terminal dispatch stops persistent effects and releases counters exactly once");
		// Cleanup failed baseline ownership without substituting for the assertions above.
		if (!dispatch->IsObjectDead()) dispatch->Cancel();
	}
}
