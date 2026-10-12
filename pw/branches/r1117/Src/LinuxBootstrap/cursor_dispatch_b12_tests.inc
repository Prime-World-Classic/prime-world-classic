/** The existing command class must keep exact actor/dispatch identity through execution. */
static void CursorCommandB12(Checks& check)
{
	B8BehaviourForeignWorld fixture(check);
	if (!fixture.world) return;
	PFWorld* world = fixture.world;
	for (int loss = 0; loss < 9; ++loss)
	{
		CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
		CObj<PFPlayer> player = new PFPlayer(world, 88, NCore::ETeam::Team1, NCore::ETeam::Team1,
			1988, NCore::ESex::Male, false, 0, false, false);
		hero->AttachToPlayer(player.GetPtr());
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		NDb::Ability* raw = new NDb::Ability;
		NDb::DispatchCursor* cursor = new NDb::DispatchCursor;
		cursor->lifeTime.sString = "10"; cursor->speed.sString = "3"; cursor->maxSpeed.sString = "4";
		raw->dispatch = cursor;
		CObj<PFAbilityData> data = new PFAbilityData(hero.GetPtr(), raw, NDb::ABILITYTYPEID_ABILITY1);
		CObj<PFAbilityInstance> cast = hero->CreateAbilityInstance(data, Target(CVec3(15, 5, 0)));
		CObj<PFDispatch> base = B11FindDispatch(world, cast.GetPtr());
		PFDispatchCursor* dispatch = dynamic_cast<PFDispatchCursor*>(base.GetPtr());
		check(dispatch != nullptr, "cursor command fixture has the actual authored subtype");
		if (!dispatch) continue;
		CObj<NCore::WorldCommand> command = CreateCmdUpdateCursorPos(dispatch, CVec2(15, 10));
		command->SetId(1988);
		check(command->CanExecute(), "live owner can command its own cursor dispatch");
		if (loss == 1) command->SetId(1989);
		if (loss == 2) cast->Cancel();
		if (loss == 3) hero->ChangeHealth(-hero->GetHealth());
		if (loss == 4)
		{
			world->UnregisterObject(hero.GetPtr(), false);
			world->ProcessAddRemove();
		}
		if (loss == 6) player->DetachHero();
		if (loss == 7) world->UnsubscribeObject(dispatch);
		if (loss == 8)
		{
			command = CreateCmdUpdateCursorPos(dispatch, CVec2(std::numeric_limits<float>::quiet_NaN(), 10));
			command->SetId(1988);
		}
		check(command->CanExecute() == (loss == 0 || loss == 5), "cursor admission rechecks ownership and liveness");
		command->Execute(loss == 5 ? nullptr : world);
		check(dispatch->GetTarget().GetPosition() == CVec3(15, loss == 0 ? 10 : 5, 0),
			"queued cursor execution rejects stale actors and a mismatched execution world");
		cast->Cancel(); hero->DoStop();
		if (loss != 4) world->UnregisterObject(hero.GetPtr(), false);
	}
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
	NDb::Ability* raw = new NDb::Ability;
	NDb::DispatchCursor* cursor = new NDb::DispatchCursor;
	cursor->lifeTime.sString = "10"; cursor->speed.sString = "3"; cursor->maxSpeed.sString = "4";
	raw->dispatch = cursor;
	CObj<PFAbilityData> data = new PFAbilityData(sender.GetPtr(), raw, NDb::ABILITYTYPEID_SPECIAL);
	CObj<PFAbilityInstance> cast = sender->CreateAbilityInstance(data, Target(CVec3(15, 5, 0)));
	CObj<PFDispatch> base = B11FindDispatch(world, cast.GetPtr());
	PFDispatchCursor* dispatch = dynamic_cast<PFDispatchCursor*>(base.GetPtr());
	check(dispatch != nullptr, "nonhero cursor still supports authored internal movement");
	if (!dispatch) return;
	CObj<NCore::WorldCommand> nonhero = CreateCmdUpdateCursorPos(dispatch, CVec2(15, 10));
	nonhero->SetId(-1);
	check(!nonhero->CanExecute(), "nonhero cursor is not controllable through a hero command");
	nonhero->Execute(world);
	const CVec3 target = dispatch->GetTarget().GetPosition();
	for (float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
	{
		dispatch->UpdateCursorPos(CVec2(invalid, 5));
		check(dispatch->GetTarget().GetPosition() == target, "invalid cursor updates never poison the target");
	}
	world->Step(.5f, .5f);
	check(dispatch->GetCurrentPosition() == CVec3(7, 5, 0),
		"world scheduler advances the unit-owned cursor once, not again as a detached dispatch");
	cast->Cancel();
}

/** Negative cursor duration follows the original persistent-ring exhaustion policy. */
static void CursorPersistentB12(Checks& check, PFWorld* world)
{
	for (bool cancelled : {false, true})
	{
		CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
		CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 20);
		NDb::Ability* raw = new NDb::Ability;
		NDb::DispatchCursor* cursor = new NDb::DispatchCursor;
		cursor->lifeTime.sString = "-1"; cursor->speed.sString = "3"; cursor->maxSpeed.sString = "4";
		NDb::StatModApplicator* modifier = new NDb::StatModApplicator;
		modifier->lifeTime.sString = "1"; modifier->modifier.stat = NDb::STAT_STRENGTH;
		modifier->modifier.multValue.sString = "1"; modifier->modifier.addValue.sString = "5";
		cursor->persistentApplicators.push_back(modifier); raw->dispatch = cursor;
		CObj<PFAbilityData> data = new PFAbilityData(sender.GetPtr(), raw, NDb::ABILITYTYPEID_SPECIAL);
		CObj<PFAbilityInstance> cast = sender->CreateAbilityInstance(data, Target(receiver.GetPtr()));
		CObj<PFDispatch> dispatch = B11FindDispatch(world, cast.GetPtr());
		check(dispatch && cast->GetActiveApplicatorsCount() == 1 && receiver->GetStatValue(NDb::STAT_STRENGTH) == 25,
			"cursor starts the real persistent buff and its ability ownership");
		if (!dispatch) continue;
		sender->Step(.5f);
		check(!cast->IsFinished() && receiver->GetStatValue(NDb::STAT_STRENGTH) == 25,
			"negative cursor lifetime remains active while persistent buff is alive");
		if (cancelled) cast->Cancel(); else sender->Step(.5f);
		check(receiver->GetStatValue(NDb::STAT_STRENGTH) == 20 && cast->GetActiveApplicatorsCount() == 0,
			"cursor expiry or cancellation removes the persistent stat effect and counter");
		sender->Step(.1f);
		check(dispatch->IsObjectDead() && cast->IsFinished(), "empty persistent cursor finishes on the original next step");
	}
}

/** Exercise authored cursor dispatches through the existing factory and engine scheduler. */
static void CursorDispatchB12(Checks& check, PFWorld* world)
{
	for (int ending = 0; ending < 4; ++ending)
	{
		CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
		CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 20);
		sender->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		NDb::Ability* raw = new NDb::Ability;
		raw->manaCost.sString = "70"; raw->cooldownTime.sString = "5";
		NDb::DispatchCursor* cursor = new NDb::DispatchCursor;
		cursor->lifeTime.sString = "sStrength / 20";
		cursor->speed.sString = "3"; cursor->maxSpeed.sString = "4";
		raw->dispatch = cursor;
		NDb::DamageApplicator* damage = new NDb::DamageApplicator;
		damage->damage.sString = "25"; damage->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
		raw->applicators.push_back(damage);
		CObj<PFAbilityData> data = new PFAbilityData(sender.GetPtr(), raw, NDb::ABILITYTYPEID_ABILITY1);
		CObj<PFAbilityInstance> cast = sender->CreateAbilityInstance(data, Target(receiver.GetPtr()));
		check(cast && cast->IsActivated() && !cast->IsFinished(), "cursor cast creates a travelling engine dispatch");
		check(sender->GetMana() == 130 && data->GetCurrentCooldown() == 5,
			"cursor activation owns exactly one original mana and cooldown payment");
		CObj<PFDispatch> dispatch = B11FindDispatch(world, cast.GetPtr());
		check(dispatch && !dispatch->HasArrived(), "cursor factory does not fall back to instant damage");
		if (!dispatch) continue;
		sender->Step(.5f);
		check(receiver->GetHealth() == 1000 && !cast->IsFinished(), "cursor payload waits for authored duration");
		dispatch->Reset();
		if (ending == 1) cast->Cancel();
		if (ending == 2) sender->ChangeHealth(-1000);
		if (ending == 3) receiver->EnableDispatch(false);
		sender->Step(.5f);
		check(receiver->GetHealth() == (ending == 2 ? 975 : 1000),
			"cursor preserves original apply-on-sender-death and explicit cancellation");
		sender->Step(1);
		check(receiver->GetHealth() == (ending == 0 || ending == 2 ? 975 : 1000),
			"cursor reset does not restart lifetime and blocked admission retains payload");
		receiver->EnableDispatch(true);
		sender->Step(.1f); sender->Step(.1f);
		check(receiver->GetHealth() == (ending == 1 ? 1000 : 975) && cast->IsFinished()
			&& sender->GetMana() == 130, "cursor finishes or drains deferred payload exactly once");
	}
	for (float link : {0.0f, 3.0f})
	{
		CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
		NDb::Ability* raw = new NDb::Ability;
		NDb::DispatchCursor* cursor = new NDb::DispatchCursor;
		cursor->lifeTime.sString = "10"; cursor->speed.sString = "3";
		cursor->maxSpeed.sString = "4"; cursor->linkRange = link; raw->dispatch = cursor;
		NDb::Ptr<NDb::Spell> spell = raw;
		CObj<PFAbilityData> data = new PFAbilityData(sender.GetPtr(), raw, NDb::ABILITYTYPEID_SPECIAL);
		Target source(CVec3(5, 5, 0)), target(CVec3(15, 5, 0));
		CObj<PFAbilityInstance> cast = new PFAbilityInstance(data, target, false);
		CObj<PFDispatch> dispatch = CreateDispatch(cast, nullptr, source, target, spell, 0, true, 0);
		check(dispatch && dispatch->GetCurrentPosition() == source.GetPosition(), "cursor starts at the authored source");
		if (!dispatch) continue;
		for (float dt : {-1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
		{
			dispatch->Step(dt);
			check(dispatch->GetCurrentPosition() == source.GetPosition() && !dispatch->HasArrived(),
				"invalid cursor clock cannot move, expire or corrupt travel");
		}
		dispatch->Step(0);
		check(dispatch->GetCurrentPosition() == source.GetPosition(), "zero cursor clock cannot move");
		dispatch->Step(.5f);
		check(dispatch->GetCurrentPosition() == CVec3(7, 5, 0), "cursor uses original max-speed travel");
		dispatch->Step(.5f);
		check(dispatch->GetCurrentPosition() == CVec3(link > 0 ? 8 : 9, 5, 0),
			"cursor clamps to the authored sender link range");
		if (link > 0) dispatch->SetNewTarget(Target(CVec3(1e30f, 5, 0)));
		dispatch->Step(link > 0 ? 20 : std::numeric_limits<float>::max());
		check(dispatch->IsObjectDead(), "cursor lifetime still completes when unused travel arithmetic would overflow");
		dispatch->Cancel();
		const CVec3 stopped = dispatch->GetCurrentPosition();
		dispatch->Step(1);
		check(dispatch->GetCurrentPosition() == stopped, "cancelled cursor never resumes movement");
	}
	for (int invalid = 0; invalid < 9; ++invalid)
	{
		CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
		NDb::Ability* raw = new NDb::Ability;
		raw->manaCost.sString = "70"; raw->cooldownTime.sString = "5";
		NDb::DispatchCursor* cursor = new NDb::DispatchCursor;
		cursor->lifeTime.sString = "2"; cursor->speed.sString = "3"; cursor->maxSpeed.sString = "4";
		if (invalid == 0) cursor->lifeTime.sString = "bad(";
		if (invalid == 1) cursor->speed.sString = "bad(";
		if (invalid == 2) cursor->maxSpeed.sString = "bad(";
		if (invalid == 3) cursor->speed.sString = "-1";
		if (invalid == 4) cursor->maxSpeed.sString = "-1";
		if (invalid == 5) cursor->linkRange = std::numeric_limits<float>::infinity();
		if (invalid == 6) cursor->linkRange = -1;
		if (invalid == 7) { cursor->lifeTime.sString.clear(); cursor->lifeTime.compiledString = "opaque"; }
		Target target(CVec3(invalid == 8 ? std::numeric_limits<float>::quiet_NaN() : 15, 5, 0));
		raw->dispatch = cursor;
		CObj<PFAbilityData> data = new PFAbilityData(sender.GetPtr(), raw, NDb::ABILITYTYPEID_ABILITY1);
		CObj<PFAbilityInstance> cast = sender->CreateAbilityInstance(data, target);
		check(!cast || !cast->IsActivated(), "malformed cursor data fails before activation");
		check(sender->GetMana() == 200 && data->GetCurrentCooldown() == 0,
			"rejected cursor dispatch does not consume mana or start cooldown");
	}
}
