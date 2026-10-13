/** Add an actual receiver-owned cast so sent and applied death policies can be tested independently. */
static int DeathCleanupCastB13(ApplicatorQueryFixtureB12& f)
{
	f.data.push_back(new PFAbilityData(f.receiver.GetPtr(), f.db, NDb::ABILITYTYPEID_SPECIAL, false, false));
	f.casts.push_back(new PFAbilityInstance(f.data.back(), Target(f.elsewhere.GetPtr()), false));
	return static_cast<int>(f.casts.size()) - 1;
}

static NDb::Ptr<NDb::StatModApplicator> DeathCleanupStatB13(const char* name, int flags)
{
	auto db = ApplicatorQueryFixtureB12::Stat(name);
	const_cast<NDb::StatModApplicator*>(db.GetPtr())->behaviorFlags = static_cast<NDb::EBuffBehavior>(flags);
	return db;
}

/** Death reaches production world cleanup, retaining only the independently authored endpoint exemptions. */
static void DeathCleanupPoliciesB13(Checks& check)
{
	for (bool keepReceived : {false, true}) for (bool keepSent : {false, true})
	{
		B8BehaviourForeignWorld world(check); if (!world.world) continue;
		ApplicatorHistoryFixtureB13 f(check, world.world);
		const int outgoingCast = DeathCleanupCastB13(f);
		auto incoming = f.Apply(DeathCleanupStatB13("Incoming", keepReceived ? NDb::BUFFBEHAVIOR_DONTSTOPONDEATH : 0));
		auto outgoing = f.Apply(DeathCleanupStatB13("Outgoing", NDb::BUFFBEHAVIOR_ENABLEDONSENDERDEATH |
			(keepSent ? NDb::BUFFBEHAVIOR_DONTSTOPONSENDERDEATH : 0)), outgoingCast, nullptr, f.elsewhere);
		auto unrelated = f.Apply(f.Stat("Unrelated"), 2, nullptr, f.other);
		f.receiver->KillUnit(f.other.GetPtr(), PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
		world.world->Step(.1f, .1f);
		check(f.receiver->IsDead() && world.world->GetLinuxPendingDeadUnitsCount() == 0,
			"real unit death drains through the native world queue");
		check(incoming->IsAlive() == keepReceived && outgoing->IsAlive() == keepSent,
			"world cleanup applies independent receiver-death and sender-death survival flags");
		check(f.receiver->GetStatValue(NDb::STAT_STRENGTH) == 20 + 3 * keepReceived &&
			f.elsewhere->GetStatValue(NDb::STAT_STRENGTH) == 20 + 3 * keepSent,
			"death policy removes actual stat contributions only for stopped effects");
		check(incoming->GetStopReason() == (keepReceived ? APPL_STOP_REASON_NONE : APPL_STOP_REASON_ONDEATH)
			&& outgoing->GetStopReason() == APPL_STOP_REASON_NONE,
			"only receiver-death removal records the original death reason");
		check(ApplicatorHistoryNamedCountB13(f.receiver) == int(keepReceived) &&
			ApplicatorHistoryNamedCountB13(f.elsewhere) == int(keepSent),
			"death cleanup removes stopped names without unlinking surviving effects");
		check(ApplicatorHistoryListB13(f.receiver, false).empty() && ApplicatorHistoryListB13(f.receiver, true).empty(),
			"direct death removal does not retain discarded effects in history");
		check(f.casts[0]->GetActiveApplicatorsCount() == int(keepReceived) &&
			f.casts[outgoingCast]->GetActiveApplicatorsCount() == int(keepSent),
			"death cleanup balances independent ability ownership");
		check(unrelated->IsAlive() && f.other->GetStatValue(NDb::STAT_STRENGTH) == 43,
			"cleanup leaves an unrelated live effect and its receiver unchanged");
		f.receiver->CleanupAfterDeath();
		check(incoming->IsAlive() == keepReceived && outgoing->IsAlive() == keepSent,
			"repeated partial cleanup preserves surviving effects");
		f.receiver->CleanupAfterDeath(true);
		check(!incoming->IsAlive() && !outgoing->IsAlive() && unrelated->IsAlive(),
			"full cleanup also retires effects exempt from ordinary death");
	}
}

/** Both full and partial cleanup release local histories, taunt stack and occupied attack sectors. */
static void DeathCleanupStateB13(Checks& check)
{
	for (bool full : {false, true})
	{
		B8BehaviourForeignWorld world(check); if (!world.world) continue;
		ApplicatorHistoryFixtureB13 f(check, world.world);
		const int outgoingCast = DeathCleanupCastB13(f);
		auto received = f.Apply(f.Stat("HistoryIn")); received->Stop(); MemorizeApplicator(received);
		auto sent = f.Apply(f.Stat("HistoryOut"), outgoingCast, nullptr, f.elsewhere); sent->Stop(); MemorizeApplicator(sent);
		auto* raw = new ApplicatorQueryDbB12<NDb::TauntApplicator>("/Mock/DeathTaunt.xdb");
		raw->enabled.sString = "true"; raw->startCondition.sString = "true"; raw->lifeTime.sString = "-1";
		raw->tauntTarget = NDb::APPLICATORAPPLYTARGET_ABILITYOWNER;
		raw->behaviorFlags = NDb::BUFFBEHAVIOR_DONTSTOPONDEATH;
		NDb::Ptr<NDb::TauntApplicator> tauntDb = raw;
		auto taunt = f.Apply(tauntDb);
		auto attacker = B8BehaviourMoving(world.world, NDb::FACTION_BURN);
		f.receiver->InitAttackSectors(4);
		const int sector = f.receiver->OccupyAttackSector(attacker.GetPtr());
		check(sector >= 0 && f.receiver->GetAttackSectorOccupies(sector) == 1 &&
			f.receiver->GetAppliedTaunts().GetCount() == 1, "cleanup fixture owns a real taunt and attack sector");
		f.receiver->CleanupAfterDeath(full);
		check(!received->IsLinkedToAppliedRing() && received->IsLinkedToSentRing() &&
			!sent->IsLinkedToSentRing() && sent->IsLinkedToAppliedRing(),
			"cleanup removes only local history ownership, preserving each other endpoint");
		check(f.receiver->GetAppliedTaunts().GetCount() == 0 &&
			f.receiver->GetAttackSectorOccupies(sector) == 0, "both cleanup modes clear taunt stack and attack sectors");
		check(taunt->IsAlive() == !full, "clearing taunt bookkeeping does not override authored partial-death survival");
		f.receiver->CleanupAfterDeath(full);
		check(f.receiver->GetAppliedTaunts().GetCount() == 0 && f.receiver->GetAttackSectorOccupies(sector) == 0,
			"repeated cleanup remains empty and does not release peer-owned history");
	}
}

/** Deferred death does not prematurely enter ordinary death cleanup. */
static void DeathCleanupDeferredB13(Checks& check)
{
	B8BehaviourForeignWorld world(check); if (!world.world) return;
	ApplicatorHistoryFixtureB13 f(check, world.world);
	auto app = f.Apply(f.Stat("Deferred"));
	f.receiver->KillUnit(f.other.GetPtr(), PFBaseUnit::UNITDIEFLAGS_DEFERREDDEATH | PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
	world.world->Step(.1f, .1f);
	check(!f.receiver->IsDead() && app->IsAlive() && world.world->GetLinuxPendingDeadUnitsCount() == 0,
		"deferred-death unit retains its live effect without entering the dead queue");
	f.receiver->KillUnit(f.other.GetPtr(), PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
	world.world->Step(.1f, .1f);
	check(f.receiver->IsDead() && !app->IsAlive() && app->GetStopReason() == APPL_STOP_REASON_ONDEATH,
		"later ordinary death performs the previously deferred effect cleanup");
}
