/** Actual resurrection must notify original listeners/triggers before clearing ability progress. */
static void ResurrectionEventsB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (bool automatic : {false, true})
	{
		B8BehaviourForeignWorld f(check); if (!f.world) continue;
		SalarySettingsB13(f.world, 60, 0);
		auto hero = HeroCorpseCreateB13(f.world); hero->SetForbidRespawn(!automatic);
		for (int life = 0; life < 2; ++life)
		{
			HeroCorpseKillB13(f.world, hero);
			int events = 0; bool payload = false, alive = false, inProgress = false;
			CObj<EventDispatchB7Listener> listener = new EventDispatchB7Listener;
			listener->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent* event) {
				if (event->GetType() == NDb::BASEUNITEVENT_RESURRECT)
				{
					++events; payload = dynamic_cast<const PFCreatureResurrectEvent*>(event);
					alive = !hero->IsDead() && !hero->GetLinuxDeathPresentation().active && hero->InAIWorld();
					inProgress = hero->HaveAbilityInProgress() && hero->GetChannellingProgress() == .75f;
				}
				return 0u;
			};
			hero->AddEventListener(listener.GetPtr());
			auto* abilityRaw = new NDb::Ability; abilityRaw->flags = NDb::ABILITYFLAGS_APPLYTODEAD;
			NDb::Ptr<NDb::Ability> abilityDb = abilityRaw;
			CObj<PFAbilityData> ability = new PFAbilityData(hero.GetPtr(), abilityDb, NDb::ABILITYTYPEID_SPECIAL);
			const Target target(hero.GetPtr()); CObj<PFAbilityInstance> instance = new PFAbilityInstance(ability, target, true);
			using F = TriggerConditionFixtureB13;
			auto* raw = F::Descriptor(F::Condition(1 << NDb::BASEUNITEVENT_RESURRECT)); raw->lifeTime.sString = "-1";
			raw->behaviorFlags = static_cast<NDb::EBuffBehavior>(NDb::BUFFBEHAVIOR_DONTSTOPONDEATH
				| NDb::BUFFBEHAVIOR_DONTSTOPONSENDERDEATH | NDb::BUFFBEHAVIOR_ENABLEDONSENDERDEATH);
			NDb::Ptr<NDb::TriggerApplicator> db = raw; PFApplCreatePars pars(instance, target); pars.pDBAppl = db;
			auto trigger = CreateApplicator(pars);
			check(trigger && ActivateApplicator(trigger, instance), "B13 actual resurrection trigger attaches to the dead hero");
			const float strength = hero->GetStatValue(NDb::STAT_STRENGTH);
			hero->SetAbilityInProgress(0, true); hero->SetChannellingProgress(.75f);
			if (automatic) for (int tick = 0; tick < 40 && hero->IsDead(); ++tick) f.world->Step(.25f, .25f);
			else hero->Resurrect();
			check(events == 1 && payload && alive, "B13 explicit and scheduled resurrection publish the original typed event after life/presentation restoration");
			check(inProgress && !hero->HaveAbilityInProgress() && hero->GetChannellingProgress() == 0,
				"B13 resurrection listeners observe original progress before the original post-event reset");
			check(hero->GetStatValue(NDb::STAT_STRENGTH) == strength + 1,
				"B13 real resurrection event dispatches its original condition-trigger spell exactly once");
			f.world->Step(.25f, .25f); f.world->Step(.25f, .25f);
			check(events == 1 && hero->GetStatValue(NDb::STAT_STRENGTH) == strength + 1, "B13 living ticks cannot repeat resurrection notifications");
			hero->RemoveEventListener(listener.GetPtr()); listener->action = nullptr;
			if (trigger) { trigger->Stop(); trigger->RemoveFromRingsSafe(); }
		}
		hero->Cleanup();
	}
	std::printf("B13 resurrection event checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Actual paid talent activation queues only dead purchases; APPLYTODEAD passives are not repeated. */
static void ResurrectionTalentsB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (bool deadPurchase : {false, true}) for (bool applyDead : {false, true}) for (bool automatic : {false, true})
	{
		B8BehaviourForeignWorld f(check); if (!f.world) continue;
		SalarySettingsB13(f.world, 60, 0);
		auto hero = CommandHeroB10(f.world); hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); hero->SetForbidRespawn(!automatic);
		auto* raw = new NDb::Talent; raw->naftaCost = 30; raw->manaCost.sString = "0";
		raw->flags = applyDead ? NDb::ABILITYFLAGS_APPLYTODEAD : NDb::EAbilityFlags(0);
		auto* mod = new NDb::PermanentStatModApplicator; mod->enabled.sString = "true";
		mod->modifier.stat = NDb::STAT_STRENGTH; mod->modifier.addValue.sString = "7"; mod->modifier.multValue.sString = "1";
		raw->passiveApplicators.push_back(mod); NDb::Ptr<NDb::Talent> db = raw;
		hero->InstallTalent(db, false); hero->AddGold(100);
		const float strength = hero->GetStatValue(NDb::STAT_STRENGTH);
		if (deadPurchase) HeroCorpseKillB13(f.world, hero);
		check(hero->ActivateTalent(0, 0) && hero->GetGold() == 70 && hero->GetTalent(0, 0)->IsActivated(),
			"B13 real talent-set purchase pays once whether the hero is alive or dead");
		const float purchased = strength + (!deadPurchase || applyDead ? 7 : 0);
		check(hero->GetStatValue(NDb::STAT_STRENGTH) == purchased,
			"B13 talent purchase preserves original APPLYTODEAD admission before resurrection");
		if (!deadPurchase) HeroCorpseKillB13(f.world, hero);
		if (automatic) for (int tick = 0; tick < 40 && hero->IsDead(); ++tick) f.world->Step(.25f, .25f);
		else hero->Resurrect();
		check(!hero->IsDead() && hero->GetStatValue(NDb::STAT_STRENGTH) == strength + 7 && hero->GetGold() == 70,
			"B13 resurrection applies only the deferred passive without buying again or duplicating an already applied permanent modifier");
		hero->SetForbidRespawn(true); HeroCorpseKillB13(f.world, hero); hero->Resurrect();
		check(hero->GetStatValue(NDb::STAT_STRENGTH) == strength + 7 && hero->GetGold() == 70,
			"B13 a second lifetime cannot replay the previous death's completed purchase queue");
		hero->Cleanup();
	}
	std::printf("B13 resurrection talent checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Callback-induced death/removal owns the new lifecycle; no stale resurrection tail may undo it. */
static void ResurrectionReentryB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (int action : {0, 1, 2})
	{
		B8BehaviourForeignWorld f(check); if (!f.world) continue;
		SalarySettingsB13(f.world, 60, 0); auto hero = HeroCorpseCreateB13(f.world);
		HeroCorpseKillB13(f.world, hero);
		int events = 0; float newDelay = -1;
		CObj<EventDispatchB7Listener> listener = new EventDispatchB7Listener;
		listener->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent* event) {
			if (event->GetType() != NDb::BASEUNITEVENT_RESURRECT) return 0u;
			++events;
			if (events > 1) return 0u;
			if (action == 0) { hero->SetHealth(37); hero->Resurrect(); }
			if (action == 1) { HeroCorpseKillB13(f.world, hero); newDelay = hero->GetTimeToRespawn(); }
			if (action == 2) { f.world->UnregisterObject(hero.GetPtr(), false); f.world->ProcessAddRemove(); }
			return 0u;
		};
		hero->AddEventListener(listener.GetPtr()); hero->Resurrect();
		check(events == 1, "B13 reentrant resurrection cannot recursively revive an already-living hero");
		if (action == 0) check(hero->GetHealth() == 37, "B13 rejected living resurrection cannot refill resources during a listener");
		if (action == 1) check(hero->IsDead() && newDelay > 0 && hero->GetTimeToRespawn() == newDelay
			&& !hero->HasBehaviour() && hero->GetLinuxDeathPresentation().active,
			"B13 death during resurrection preserves its new timer, corpse presentation and behavior cleanup");
		if (action == 2) check(f.world->FindLinuxUnitByObjectId(hero->GetObjectId()) != hero.GetPtr() && !hero->HasBehaviour(),
			"B13 removed hero cannot regain behavior from a returning resurrection callback");
		hero->RemoveEventListener(listener.GetPtr()); listener->action = nullptr; hero->Cleanup();
	}
	{
		B8BehaviourForeignWorld f(check); if (f.world)
		{
			SalarySettingsB13(f.world, 60, 0); auto hero = SalaryCreateB13(f.world); hero->SetForbidRespawn(true);
			HeroCorpseKillB13(f.world, hero); CPtr<SalaryHeroB13> weak(hero.GetPtr());
			CObj<EventDispatchB7Listener> listener = new EventDispatchB7Listener; int events = 0;
			listener->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent* event) {
				if (event->GetType() == NDb::BASEUNITEVENT_RESURRECT)
				{
					++events; f.world->GetAIWorld()->UnregisterObjectOrUnit(hero.GetPtr());
					f.world->UnregisterObject(hero.GetPtr(), false); f.world->ProcessAddRemove(); hero = nullptr;
				}
				return 0u;
			};
			hero->AddEventListener(listener.GetPtr()); hero->Resurrect();
			check(events == 1 && !IsValid(weak), "B13 resurrection retains itself across callback-driven final-owner release then retires normally");
			listener->action = nullptr;
		}
	}
	std::printf("B13 resurrection reentry checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** A passive can cause another death before the old queue is exhausted, with a new purchase during death. */
static void ResurrectionInterruptedQueueB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	B8BehaviourForeignWorld f(check); if (!f.world) return;
	SalarySettingsB13(f.world, 60, 0); auto hero = CommandHeroB10(f.world);
	hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET | NDb::UNITFLAG_FORBIDREWARDS); hero->SetForbidRespawn(true);
	NDb::Ptr<NDb::Talent> descriptors[3];
	for (int i = 0; i < 3; ++i)
	{
		auto* db = new NDb::Talent; db->naftaCost = 10; db->manaCost.sString = "0";
		if (i == 0)
		{
			auto* kill = new NDb::KillApplicator; kill->enabled.sString = "true"; kill->dontGiveRewards = false;
			db->passiveApplicators.push_back(kill);
		}
		else
		{
			auto* mod = new NDb::PermanentStatModApplicator; mod->enabled.sString = "true";
			mod->modifier.stat = NDb::STAT_STRENGTH; mod->modifier.multValue.sString = "1";
			mod->modifier.addValue.sString = i == 1 ? "7" : "13"; db->passiveApplicators.push_back(mod);
		}
		descriptors[i] = db;
	}
	hero->InstallTalent(descriptors[0], false);
	PFTalentsSet::SetInfo info;
	for (int i = 0; i < 3; ++i) { info[0][i].pTalentDesc = descriptors[i]; info[0][i].refineRate = 1; }
	hero->GetTalentsSet()->LoadSet(hero.GetPtr(), info); hero->AddGold(100);
	const float strength = hero->GetStatValue(NDb::STAT_STRENGTH);
	HeroCorpseKillB13(f.world, hero);
	check(hero->ActivateTalent(0, 0) && hero->ActivateTalent(0, 1) && hero->GetGold() == 80,
		"B13 interrupted resurrection starts with two actual paid dead purchases");
	int deaths = 0; bool bought = false;
	CObj<EventDispatchB7Listener> listener = new EventDispatchB7Listener;
	listener->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent* event) {
		if (event->GetType() == NDb::BASEUNITEVENT_DEATH && ++deaths == 1) bought = hero->ActivateTalent(0, 2);
		return 0u;
	};
	hero->AddEventListener(listener.GetPtr()); hero->Resurrect();
	check(hero->IsDead() && deaths == 1 && bought && hero->GetGold() == 70,
		"B13 applying a queued passive can kill the hero and record a new real purchase during that death");
	(f.world->*HeroCorpseDrainMemberB13(HeroCorpseDrainTagB13()))(false); f.world->ProcessAddRemove();
	hero->RemoveEventListener(listener.GetPtr()); listener->action = nullptr;
	hero->Resurrect();
	check(!hero->IsDead() && hero->GetStatValue(NDb::STAT_STRENGTH) == strength + 20 && hero->GetGold() == 70,
		"B13 next resurrection retains the unvisited old passive and new purchase without repeating the completed killing passive");
	HeroCorpseKillB13(f.world, hero); hero->Resurrect();
	check(hero->GetStatValue(NDb::STAT_STRENGTH) == strength + 20, "B13 interrupted passive queue drains exactly once across later lifetimes");
	hero->Cleanup();
	std::printf("B13 interrupted resurrection checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Purchase unchanged G037 and exercise its original prime reward and five-action life-growth cap. */
static void ResurrectionAuthoredB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		const auto db = NDb::Get<NDb::Talent>(NDb::DBID("/Items/Talents/Level2/G037_IncrLife-perDeaths.TALENT.xdb"));
		// Reuse the bounded field reader; resolving an inline DBID eagerly precaches unrelated AI resources.
		NDb::Ptr<NDb::UnitConstantsContainer> constants;
		vector<int> costs;
		CObj<Stream> xml = files->OpenFile("GameLogic/AILogic.AILP.xdb", FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
		if (xml && xml->IsOk())
		{
			CObj<IXmlSaver> saver = CreateXmlSaver(xml, true);
			if (saver->StartChunk("AILogicParameters", 0))
			{
				constants = NearbyConstantsB13(*saver, "globalConstants");
				saver->Add("talentNaftaCostByLevel", &costs);
			}
		}
		check(db && db->passiveApplicators.size() == 3 && constants && costs.size() == 6,
			"B13 original resurrection talent, prices and global constants load read-only");
		if (db && constants && costs.size() == 6) for (bool deadPurchase : {false, true}) for (int rate : {1, 6})
		{
			B8BehaviourForeignWorld f(check); if (!f.world) continue; SalarySettingsB13(f.world, 60, 0);
			auto& ai = const_cast<NDb::AILogicParameters&>(f.world->GetAIWorld()->GetAIParameters());
			const auto savedConstants = ai.globalConstants; ai.globalConstants = constants;
			const auto savedCosts = ai.talentNaftaCostByLevel; ai.talentNaftaCostByLevel = costs;
			auto hero = CommandHeroB10(f.world); hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); hero->SetForbidRespawn(true);
			hero->InstallTalent(db, false); hero->GetTalent(0, 0)->SetRefineRate(rate); hero->AddGold(10000);
			const float life = hero->GetStatValue(NDb::STAT_LIFE);
			const int cost = hero->GetTalent(0, 0)->GetNaftaCost();
			if (deadPurchase) HeroCorpseKillB13(f.world, hero);
			check(cost == 500 && hero->ActivateTalent(0, 0) && hero->GetGold() == 10000 - cost,
				"B13 original G037 is purchased through its real talent set without replacing the authored passive list");
			const float initial = (2.f + .2f * (rate - 1)) * 30.f, perLife = 40.f + 5.f * (rate - 1);
			for (int revival = 1; revival <= 7; ++revival)
			{
				if (!hero->IsDead()) HeroCorpseKillB13(f.world, hero);
				f.world->Step(.25f, .25f); hero->Resurrect();
				// Both authored response dispatches defer application until the next world tick.
				check(hero->GetGold() == 10000 - cost + 50 * (revival - 1),
					"B13 original G037 resurrection dispatch retains its authored noninstant timing");
				f.world->Step(.25f, .25f);
				check(!hero->IsDead() && hero->GetGold() == 10000 - cost + 50 * revival,
					"B13 original G037 resurrection event awards fifty prime on every actual revival");
				check(std::fabs(hero->GetStatValue(NDb::STAT_LIFE) - (life + initial + perLife * Min(revival, 5))) < .001f,
					"B13 original refined G037 grows life only on its first five resurrection events");
				const float actual = hero->GetStatValue(NDb::STAT_LIFE); const int gold = hero->GetGold();
				hero->Resurrect(); f.world->Step(.25f, .25f);
				check(hero->GetGold() == gold && hero->GetStatValue(NDb::STAT_LIFE) == actual,
					"B13 repeated living resurrection cannot farm the original talent's rewards");
			}
			hero->GetTalentsSet()->CleanSet(); hero->Cleanup(); ai.globalConstants = savedConstants;
			ai.talentNaftaCostByLevel = savedCosts;
		}
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 authored resurrection checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
