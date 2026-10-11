/** Original projectile algorithms run from real unit steps, including retargeted payload receivers. */
static void DispatchLinearB11(Checks& check, PFWorld* world)
{
	for (bool linked : {false, true})
	for (int travel = 0; travel < 6; ++travel)
	{
		CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
		sender->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		CObj<B8BehaviourCreep> receiver = B8BehaviourMoving(world, NDb::FACTION_BURN);
		check(receiver->TeleportTo(CVec2(15, 5)), "B11 linear fixture uses an actual positioned engine unit");
		NDb::Ability* raw = new NDb::Ability;
		raw->manaCost.sString = "70";
		NDb::DispatchLinear* linear = linked ? new NDb::DispatchLinearWithLink : new NDb::DispatchLinear;
		linear->speed = 5;
		linear->aiming = NDb::DISPATCHAIMING_PURSUE;
		linear->suspendTime = travel == 1 ? .5f : 0;
		linear->maxDistance = travel == 2 ? 3 : -1;
		raw->dispatch = linear;
		for (const char* expression : {"tStrength", "5"})
		{
			NDb::DamageApplicator* damage = new NDb::DamageApplicator;
			damage->damage.sString = expression;
			damage->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
			raw->applicators.push_back(damage);
		}
		CObj<PFAbilityData> data = new PFAbilityData(sender.GetPtr(), raw, NDb::ABILITYTYPEID_SPECIAL);
		CObj<PFAbilityInstance> cast = sender->CreateAbilityInstance(data, Target(receiver.GetPtr()));
		CObj<PFDispatch> dispatch = cast ? B11FindDispatch(world, cast.GetPtr()) : CObj<PFDispatch>();
		PFDispatchUniformLinearMove* projectile = dynamic_cast<PFDispatchUniformLinearMove*>(dispatch.GetPtr());
		check(projectile && (dynamic_cast<PFDispatchWithLink*>(projectile) != nullptr) == linked,
			"B11 linear and linked descriptors retain their authored engine subtype");
		check(cast && !cast->IsFinished() && receiver->GetHealth() == 1000 && sender->GetMana() == 130,
			"B11 projectile activation pays once without early payload");
		if (!projectile) { if (cast) cast->Cancel(); continue; }
		sender->Step(.5f);
		check(projectile->GetCurPos() == CVec2(travel == 1 ? 5 : 7.5f, 5)
			&& receiver->GetHealth() == 1000, "B11 authored speed and suspension control actual displacement");
		CObj<PFBaseUnit> replacement = Unit(world, NDb::FACTION_BURN, 60);
		if (travel == 3)
		{
			dispatch->SetNewTarget(Target(replacement.GetPtr()));
			receiver->StopAttackingMe(true);
			check(dispatch->GetTarget().IsUnit() && dispatch->GetTarget().GetUnit() == replacement.GetPtr(),
				"B11 retarget transfers inbound registration away from the old receiver");
		}
		if (travel == 4) receiver->StopAttackingMe(true);
		if (travel == 5) receiver->ChangeHealth(-1000);
		for (int tick = 0; tick < 5; ++tick) sender->Step(.5f);
		const float expected = travel == 5 ? 0 : (travel == 0 || travel == 1 ? 975 : 1000);
		check(cast->IsFinished() && dispatch->IsObjectDead() && receiver->GetHealth() == expected,
			"B11 arrival, distance exhaustion and lost targets terminate without synthetic damage");
		check(replacement->GetHealth() == (travel == 3 ? 935 : 1000),
			"B11 every pending applicator reinitializes against the new real receiver");
		check(travel != 2 || projectile->GetCurPos() == CVec2(8, 5),
			"B11 maximum-distance expiry uses the original clamped travel position");
		sender->Step(1);
		check(receiver->GetHealth() == expected && sender->GetMana() == 130,
			"B11 completed projectile does not replay its payload or payment");
	}
}
