/** Client reconstruction must preserve live dispatch phase, progress and persistent cancellation. */
static void DispatchResetB11(Checks& check, PFWorld* world)
{
	for (int kind = 0; kind < 4; ++kind)
	{
		CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
		CObj<B8BehaviourCreep> receiver = B8BehaviourMoving(world, NDb::FACTION_BURN);
		sender->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		receiver->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		check(receiver->TeleportTo(CVec2(15, 5)), "B11 reset fixture initializes a real unit endpoint");
		NDb::Ability* raw = new NDb::Ability;
		if (kind == 0)
		{
			NDb::DispatchByTime* timed = new NDb::DispatchByTime;
			timed->lifeTime.sString = "2";
			raw->dispatch = timed;
		}
		if (kind == 1)
		{
			NDb::DispatchLinear* linear = new NDb::DispatchLinear;
			linear->speed = 5;
			linear->aiming = NDb::DISPATCHAIMING_PURSUE;
			raw->dispatch = linear;
		}
		if (kind == 2) raw->dispatch = new NDb::DispatchImmediate;
		if (kind == 3)
		{
			NDb::DispatchContinuous* continuous = new NDb::DispatchContinuous;
			NDb::StatModApplicator* modifier = new NDb::StatModApplicator;
			modifier->lifeTime.sString = "10";
			modifier->modifier.stat = NDb::STAT_STRENGTH;
			modifier->modifier.multValue.sString = "1";
			modifier->modifier.addValue.sString = "5";
			continuous->persistentApplicators.push_back(modifier);
			raw->dispatch = continuous;
		}
		else
		{
			NDb::DamageApplicator* damage = new NDb::DamageApplicator;
			damage->damage.sString = "25";
			damage->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
			raw->applicators.push_back(damage);
		}
		if (kind == 2) receiver->EnableDispatch(false);
		CObj<PFAbilityData> data = new PFAbilityData(sender.GetPtr(), raw, NDb::ABILITYTYPEID_SPECIAL);
		CObj<PFAbilityInstance> cast = sender->CreateAbilityInstance(data, Target(receiver.GetPtr()));
		CObj<PFDispatch> dispatch = cast ? B11FindDispatch(world, cast.GetPtr()) : CObj<PFDispatch>();
		check(cast && dispatch, "B11 reset observes the registered instance-owned dispatch");
		if (!cast || !dispatch) continue;
		sender->Step(.5f);
		const CVec3 position = dispatch->GetCurrentPosition();
		const bool arrived = dispatch->HasArrived();
		dispatch->Reset();
		check(dispatch->Started() && dispatch->HasArrived() == arrived
			&& dispatch->GetCurrentPosition() == position && !cast->IsFinished(),
			"B11 reset preserves dispatch phase and current progress instead of replaying initialization");
		if (kind == 3)
		{
			cast->Cancel();
			check(cast->IsFinished() && dispatch->IsObjectDead() && cast->GetActiveApplicatorsCount() == 0
				&& receiver->GetStatValue(NDb::STAT_STRENGTH) == 20,
				"B11 reset continuous dispatch still stops persistent effects on cancellation");
		}
		else
		{
			receiver->EnableDispatch(true);
			sender->Step(.5f);
			check(receiver->GetHealth() == (kind == 2 ? 975 : 1000),
				"B11 reset preserves remaining travel and ready receiver admission");
			sender->Step(1);
			check(cast->IsFinished() && dispatch->IsObjectDead() && receiver->GetHealth() == 975,
				"B11 reset dispatch completes at the original deadline without restarting travel");
			sender->Step(1);
			check(receiver->GetHealth() == 975, "B11 reset cannot duplicate a completed payload");
		}
		if (!dispatch->IsObjectDead()) dispatch->Cancel();
	}
}
