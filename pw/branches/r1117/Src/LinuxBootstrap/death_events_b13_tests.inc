/** Original player/hero ownership plus observable real unit events. */
struct DeathEventsFixtureB13 : KillAttributionFixtureB13
{
	std::vector<CObj<PFPlayer>> players;
	std::vector<nstl::pair<PFBaseUnit*, CObj<EventDispatchB7Listener>>> listeners;
	std::vector<int> order;
	PFBaseUnit* observedVictim = nullptr;
	PFBaseUnit* observedActual = nullptr;
	PFBaseUnit* observedQualified = nullptr;
	NDb::EBaseUnitEvent observedType = NDb::BASEUNITEVENT_MOVE;
	using KillAttributionFixtureB13::KillAttributionFixtureB13;
	CObj<PFPlayer> Player(NCore::ETeam::Enum team)
	{
		CObj<PFPlayer> player = new PFPlayer(world, players.size() + 80, team, team,
			players.size() + 1980, NCore::ESex::Male, false, 0, false, false);
		players.push_back(player); return player;
	}
	PFBaseHero* Hero(NCore::ETeam::Enum team)
	{
		auto unit = Unit(true); auto* hero = static_cast<PFBaseHero*>(unit.GetPtr());
		hero->AttachToPlayer(Player(team).GetPtr()); return hero;
	}
	std::vector<PFBaseHero*> Heroes(NDb::EFaction faction)
	{
		std::vector<PFBaseHero*> result;
		auto collect = [&](PFBaseHero& hero) { result.push_back(&hero); };
		world->GetAIWorld()->ForAllHeroes(collect, faction); return result;
	}
	EventDispatchB7Listener* Listen(PFBaseUnit* unit, int id)
	{
		CObj<EventDispatchB7Listener> listener = new EventDispatchB7Listener;
		listener->action = [&, id](EventDispatchB7Listener&, const PFBaseUnitEvent* event) {
			if (const auto* death = dynamic_cast<const PFBaseUnitEventUnitDeath*>(event))
			{
				order.push_back(id); observedVictim = death->GetVictim(); observedActual = death->GetKiller();
				observedQualified = death->GetQualifiedKiller();
				observedType = death->GetType();
			}
			return 0u;
		};
		unit->AddEventListener(listener.GetPtr()); listeners.push_back(nstl::make_pair(unit, listener)); return listener;
	}
	~DeathEventsFixtureB13()
	{
		for (auto& entry : listeners) { entry.first->RemoveEventListener(entry.second.GetPtr()); entry.second->action = nullptr; }
		for (auto& unit : units) if (auto* hero = dynamic_cast<PFBaseHero*>(unit.GetPtr())) hero->DetachFromPlayer();
	}
};

/** Player attachment owns exactly one faction-ring entry and detaches both sides on replacement. */
static void HeroRegistryB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	using F = DeathEventsFixtureB13;
	{
		F f(check, isolated.world); auto* a = f.Hero(NCore::ETeam::Team1); auto* b = f.Hero(NCore::ETeam::Team2);
		check(f.Heroes(NDb::FACTION_FREEZE) == std::vector<PFBaseHero*>{a} && f.Heroes(NDb::FACTION_BURN) == std::vector<PFBaseHero*>{b},
			"B13 actual player attachment registers each hero in its original faction ring");
		CObj<PFPlayer> player = f.players[0]; a->AttachToPlayer(player.GetPtr());
		check(f.Heroes(NDb::FACTION_FREEZE) == std::vector<PFBaseHero*>{a}, "B13 repeated same-player attachment does not duplicate or reorder membership");
		b->AttachToPlayer(player.GetPtr());
		check(!a->GetPlayer() && a->GetPlayerId() == -1 && a->GetFaction() == NDb::FACTION_NEUTRAL
			&& player->GetHero() == b && !f.players[1]->GetHero() && b->GetPlayer() == player,
			"B13 replacing an attached hero retires both old player links and the old hero identity");
		check(f.Heroes(NDb::FACTION_FREEZE) == std::vector<PFBaseHero*>{b} && f.Heroes(NDb::FACTION_BURN).empty(),
			"B13 replacement removes old faction memberships before registering the replacement");
		b->DetachFromPlayer(); b->DetachFromPlayer();
		check(!player->GetHero() && !b->GetPlayer() && b->GetPlayerId() == -1 && b->GetFaction() == NDb::FACTION_NEUTRAL
			&& f.Heroes(NDb::FACTION_FREEZE).empty(), "B13 repeated detach clears original identity and ring ownership once");
	}
	{
		F f(check, isolated.world); auto* hero = f.Hero(NCore::ETeam::Team1);
		f.world->UnregisterObject(f.players[0].GetPtr(), false); f.world->ProcessAddRemove();
		f.players.clear();
		check(!hero->GetPlayer() && hero->GetPlayerId() == -1 && hero->GetFaction() == NDb::FACTION_NEUTRAL
			&& f.Heroes(NDb::FACTION_FREEZE).empty(), "B13 original player destruction detaches its surviving hero and owning faction ring");
	}
	{
		B8BehaviourForeignWorld other(check); if (!other.world) return;
		F local(check, isolated.world), foreign(check, other.world);
		auto* hero = local.Hero(NCore::ETeam::Team1); auto player = foreign.Player(NCore::ETeam::Team2);
		hero->AttachToPlayer(player.GetPtr()); foreign.world->GetAIWorld()->RegisterHero(hero);
		foreign.world->GetAIWorld()->UnregisterHero(hero);
		check(hero->GetPlayer() == local.players[0] && !player->GetHero()
			&& local.Heroes(NDb::FACTION_FREEZE) == std::vector<PFBaseHero*>{hero}
			&& foreign.Heroes(NDb::FACTION_FREEZE).empty() && foreign.Heroes(NDb::FACTION_BURN).empty(),
			"B13 foreign-world attachment and registry operations preserve the original owning player and faction ring");
	}
	std::printf("B13 hero registry checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Actual death notifications retain faction order, payload identities, and original reward/unsummon policy. */
static void DeathEventsB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	using F = DeathEventsFixtureB13;
	for (bool heroVictim : {false, true}) for (int killerKind = 0; killerKind < 4; ++killerKind)
	{
		F f(check, isolated.world); auto* a = f.Hero(NCore::ETeam::Team1); auto* b = f.Hero(NCore::ETeam::Team2);
		auto unit = f.Unit(false, NDb::FACTION_BURN); PFBaseUnit* victim = heroVictim ? b : unit.GetPtr();
		auto actual = f.Unit(false); PFBaseUnit* killer = killerKind == 0 ? nullptr : killerKind == 1 ? a : actual.GetPtr();
		if (killerKind == 3) actual->SetMaster(a);
		f.Listen(a, 1); f.Listen(b, 2); f.Listen(unit, 3); f.Listen(actual, 4);
		victim->KillUnit(CPtr<PFBaseUnit>(killer), PFBaseUnit::UNITDIEFLAGS_NONE);
		std::vector<int> expected = heroVictim ? std::vector<int>{1, 2} : std::vector<int>{1, 2, 3};
		if (killerKind >= 2) expected.push_back(4);
		check(f.order == expected && f.observedVictim == victim && f.observedActual == killer
			&& f.observedQualified == (killerKind == 3 ? a : killer),
			"B13 actual KillUnit delivers faction heroes then victim then nonhero killer with distinct qualified attribution");
	}
	for (bool forbidden : {false, true}) for (bool unsummon : {false, true}) for (bool deferred : {false, true})
	{
		F f(check, isolated.world); auto* hero = f.Hero(NCore::ETeam::Team1);
		auto victim = f.Unit(false, NDb::FACTION_BURN), killer = f.Unit(false);
		f.Listen(hero, 1); f.Listen(victim, 2); f.Listen(killer, 3);
		const int flags = (forbidden ? PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS : 0)
			| (unsummon ? PFBaseUnit::UNITDIEFLAGS_UNSUMMON : 0) | (deferred ? PFBaseUnit::UNITDIEFLAGS_DEFERREDDEATH : 0);
		victim->KillUnit(CPtr<PFBaseUnit>(killer.GetPtr()), flags);
		const std::vector<int> expected = forbidden ? std::vector<int>() : unsummon ? std::vector<int>{2} : std::vector<int>{1, 2, 3};
		check(f.order == expected && (forbidden || !unsummon || (!f.observedActual && !f.observedQualified)),
			"B13 forbidden rewards suppress death events and unsummon notifies only its victim with null killer identities");
		check(forbidden || f.observedType == (unsummon ? NDb::BASEUNITEVENT_UNSUMMON : NDb::BASEUNITEVENT_DEATH),
			"B13 unsummon is its original distinct event rather than a public death broadcast");
	}
	for (int mutation = 0; mutation < 4; ++mutation)
	{
		F f(check, isolated.world); auto* a = f.Hero(NCore::ETeam::Team1); auto* b = f.Hero(NCore::ETeam::Team1);
		auto victim = f.Unit(false, NDb::FACTION_BURN), killer = f.Unit(false);
		auto* observer = f.Listen(a, 1); f.Listen(b, 2); f.Listen(victim, 3); f.Listen(killer, 4);
		auto original = observer->action; int calls = 0;
		observer->action = [&, original](EventDispatchB7Listener& self, const PFBaseUnitEvent* event) {
			original(self, event);
			if (event->GetType() != NDb::BASEUNITEVENT_DEATH || ++calls > 1) return 0u;
			if (mutation == 0) b->DetachFromPlayer();
			if (mutation == 1) { auto* newcomer = f.Hero(NCore::ETeam::Team2); f.Listen(newcomer, 5); }
			if (mutation == 2) f.world->GetAIWorld()->SendUnitDieNotification(CPtr<PFBaseUnit>(victim.GetPtr()), CPtr<PFBaseUnit>(killer.GetPtr()), nullptr);
			if (mutation == 3) { f.world->UnregisterObject(b, false); f.world->ProcessAddRemove(); }
			return 0u;
		};
		victim->KillUnit(CPtr<PFBaseUnit>(killer.GetPtr()), PFBaseUnit::UNITDIEFLAGS_NONE);
		check(f.order == (mutation == 0 || mutation == 3 ? std::vector<int>{1, 3, 4} : std::vector<int>{1, 2, 3, 4}),
			"B13 death fanout skips detached recipients, defers newborn subscriptions and rejects recursive same-victim broadcast");
		observer->action = original;
	}
	{
		F f(check, isolated.world); auto* a = f.Hero(NCore::ETeam::Team1); auto* b = f.Hero(NCore::ETeam::Team2);
		auto victim = f.Unit(false, NDb::FACTION_BURN), killer = f.Unit(false);
		b->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
		f.Listen(a, 1); f.Listen(b, 2); f.Listen(victim, 3); f.Listen(killer, 4);
		victim->KillUnit(CPtr<PFBaseUnit>(killer.GetPtr()), PFBaseUnit::UNITDIEFLAGS_NONE);
		check(b->IsDead() && f.order == std::vector<int>{1, 2, 3, 4},
			"B13 registered dead heroes remain original notification recipients until detached, not merely until health reaches zero");
	}
	for (bool forbidden : {false, true})
	{
		TriggerConditionFixtureB13 f(isolated.world);
		auto* condition = f.Condition(NDb::BASEUNITEVENTFLAGS_DEATH); condition->applyTarget = NDb::APPLICATORAPPLYTARGET_ABILITYOWNER;
		NDb::Ptr<NDb::EventProcessorCondition> proc = condition;
		NDb::Ptr<NDb::TriggerApplicator> db = f.Descriptor(proc, "1"); auto app = f.Apply(check, db); if (!app) continue;
		f.receiver->KillUnit(nullptr, forbidden ? PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS : PFBaseUnit::UNITDIEFLAGS_NONE);
		check(f.sender->GetStatValue(NDb::STAT_STRENGTH) == f.senderStrength + int(!forbidden),
			"B13 actual death notification drives the original condition trigger and real spell delivery before deferred cleanup");
	}
	{
		F f(check, isolated.world); auto* hero = f.Hero(NCore::ETeam::Team1);
		auto outer = f.Unit(false, NDb::FACTION_BURN), inner = f.Unit(false, NDb::FACTION_BURN), killer = f.Unit(false);
		auto* observer = f.Listen(hero, 1); f.Listen(outer, 2); f.Listen(inner, 3); f.Listen(killer, 4);
		auto original = observer->action;
		observer->action = [&, original](EventDispatchB7Listener& self, const PFBaseUnitEvent* event) {
			original(self, event);
			const auto* death = dynamic_cast<const PFBaseUnitEventUnitDeath*>(event);
			if (death && death->GetVictim() == outer.GetPtr())
				inner->KillUnit(CPtr<PFBaseUnit>(killer.GetPtr()), PFBaseUnit::UNITDIEFLAGS_NONE);
			return 0u;
		};
		outer->KillUnit(CPtr<PFBaseUnit>(killer.GetPtr()), PFBaseUnit::UNITDIEFLAGS_NONE);
		check(f.order == std::vector<int>({1, 1, 3, 4, 2, 4}) && inner->IsDead()
			&& f.observedVictim == outer.GetPtr(), "B13 a nested different-victim death completes before the original broadcast resumes");
		observer->action = original;
	}
	{
		F f(check, isolated.world); auto unit = f.Unit(false); f.Listen(unit, 1);
		unit->KillUnit(CPtr<PFBaseUnit>(unit.GetPtr()), PFBaseUnit::UNITDIEFLAGS_NONE);
		check(f.order == std::vector<int>({1, 1}) && f.observedVictim == unit.GetPtr() && f.observedActual == unit.GetPtr(),
			"B13 a nonhero self-kill preserves original victim and nonhero-killer delivery roles");
	}
	std::printf("B13 death events checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
