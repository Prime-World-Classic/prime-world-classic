/** An attached real hero receives original faction-wide death events; no synthetic broadcast substitutes. */
struct NearbyDeathFixtureB13 : DeathTriggerFixtureB13
{
	DeathEventsFixtureB13 registry;
	NearbyDeathFixtureB13(Checks& check, PFWorld* world) : DeathTriggerFixtureB13(world), registry(check, world)
	{
		receiver->AttachToPlayer(registry.Player(NCore::ETeam::Team1).GetPtr());
	}
	~NearbyDeathFixtureB13() { receiver->DetachFromPlayer(); }
	static NDb::EventProcessorUnitDieNearMe* Near(const char* range = "0")
	{
		auto* db = new NDb::EventProcessorUnitDieNearMe; db->range.sString = range;
		db->applyTarget = NDb::APPLICATORAPPLYTARGET_ABILITYOWNER; db->spell = Spell(); return db;
	}
	CObj<PFBaseUnit> Victim(NDb::EFaction faction = NDb::FACTION_BURN, bool hero = false)
	{
		return registry.Unit(hero, faction);
	}
	void Kill(PFBaseUnit* victim, PFBaseUnit* killer)
	{
		victim->KillUnit(CPtr<PFBaseUnit>(killer), PFBaseUnit::UNITDIEFLAGS_NONE);
	}
};

/** Original killer flag combinations distinguish direct summon ownership from qualified hero attribution. */
static void NearbyDeathB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	using F = NearbyDeathFixtureB13;
	for (int flags = 0; flags < 16; ++flags) for (int killerKind = 0; killerKind < 7; ++killerKind)
	{
		F f(check, isolated.world); auto* raw = F::Near(); raw->killerFlags = static_cast<NDb::UnitDieKillersFlags>(flags);
		NDb::Ptr<NDb::EventProcessorBase> processor = raw;
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(processor, "1"); auto app = f.Apply(check, db); if (!app) continue;
		auto victim = f.Victim(); auto ally = f.Victim(NDb::FACTION_FREEZE), enemy = f.Victim(), neutral = f.Victim(NDb::FACTION_NEUTRAL);
		auto summon = f.Victim(NDb::FACTION_FREEZE), indirect = f.Victim(NDb::FACTION_FREEZE);
		summon->SetMaster(f.receiver.GetPtr()); indirect->SetMaster(summon.GetPtr());
		PFBaseUnit* killer = killerKind == 0 ? nullptr : killerKind == 1 ? f.receiver.GetPtr() : killerKind == 2 ? ally.GetPtr()
			: killerKind == 3 ? enemy.GetPtr() : killerKind == 4 ? summon.GetPtr() : killerKind == 5 ? indirect.GetPtr() : neutral.GetPtr();
		f.Kill(victim.GetPtr(), killer);
		const bool allied = killerKind == 1 || killerKind == 2 || killerKind == 4 || killerKind == 5;
		const bool matched = killer && (flags == NDb::UNITDIEKILLERSFLAGS_KILLEDBYALL
			|| ((flags & NDb::UNITDIEKILLERSFLAGS_KILLEDBYME) && killerKind == 1)
			|| ((flags & NDb::UNITDIEKILLERSFLAGS_KILLEDBYMYSUMMONS) && killerKind == 4)
			|| ((flags & NDb::UNITDIEKILLERSFLAGS_KILLEDBYALLIES) && allied)
			|| ((flags & NDb::UNITDIEKILLERSFLAGS_KILLEDBYENEMY) && !allied));
		check(f.Responses() == int(matched),
			"B13 nearby death uses original killer bits, direct master ownership, neutral-enemy relation and null rejection");
	}
	for (int relation : {0, int(NDb::SPELLTARGET_ALLY), int(NDb::SPELLTARGET_ENEMY), int(NDb::SPELLTARGET_ALLY | NDb::SPELLTARGET_ENEMY)})
	for (NDb::EFaction faction : {NDb::FACTION_FREEZE, NDb::FACTION_BURN, NDb::FACTION_NEUTRAL})
	for (bool hero : {false, true})
	{
		F f(check, isolated.world); auto* raw = F::Near();
		raw->targets = static_cast<NDb::ESpellTarget>(relation | NDb::SPELLTARGET_CREEP);
		NDb::Ptr<NDb::EventProcessorBase> processor = raw;
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(processor, "1"); auto app = f.Apply(check, db); if (!app) continue;
		auto victim = f.Victim(faction, hero); f.Kill(victim.GetPtr(), f.sender.GetPtr());
		const bool relationMatches = relation & (faction == NDb::FACTION_FREEZE ? NDb::SPELLTARGET_ALLY : NDb::SPELLTARGET_ENEMY);
		check(f.Responses() == int(!hero && relationMatches),
			"B13 nearby-death victim filter requires its own explicit faction and type bits, including neutral victims");
	}
	for (const char* range : {"", "-1", "0", "0.00000001", "4.999", "5", "5.001", "sLife/80", "cMissing", "1/0", "1e39", "compiled-only"})
	{
		F f(check, isolated.world); auto* raw = F::Near(range);
		if (strcmp(range, "compiled-only") == 0) { raw->range.sString = ""; raw->range.compiledString = "unsupported"; }
		NDb::Ptr<NDb::EventProcessorBase> processor = raw;
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(processor, "1"); auto app = f.Apply(check, db); if (!app) continue;
		auto victim = f.Victim(); auto* moving = dynamic_cast<PFBaseMovingUnit*>(victim.GetPtr());
		check(moving && f.receiver->TeleportTo(CVec2(10, 10)) && moving->TeleportTo(CVec2(13, 14)),
			"B13 real nearby-death endpoints occupy an exact five-unit separation");
		f.Kill(victim.GetPtr(), f.sender.GetPtr());
		const bool matches = !*range || strcmp(range, "-1") == 0 || strcmp(range, "0") == 0 || strcmp(range, "0.00000001") == 0
			|| strcmp(range, "5") == 0 || strcmp(range, "5.001") == 0 || strcmp(range, "sLife/80") == 0;
		check(f.Responses() == int(matches),
			"B13 nearby-death range is receiver-centered, inclusive, unbounded below epsilon and checked before effects");
	}
	for (int rejected = 0; rejected < 3; ++rejected)
	{
		F f(check, isolated.world); auto* raw = F::Near("random(0,10)");
		if (rejected == 0) raw->killerFlags = static_cast<NDb::UnitDieKillersFlags>(0);
		if (rejected == 1) raw->targets = static_cast<NDb::ESpellTarget>(0);
		if (rejected == 2) { raw->checkSender = NDb::TRIGGEREVENTCHECKSENDER_MATCHTARGET; raw->senderMatchTarget = NDb::APPLICATORAPPLYTARGET_ABILITYOWNER; }
		NDb::Ptr<NDb::EventProcessorBase> processor = raw;
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(processor, "1"); auto app = f.Apply(check, db); if (!app) continue;
		auto victim = f.Victim(); NRandom::RandomGenerator expected(*isolated.world->GetRndGen());
		f.Kill(victim.GetPtr(), f.sender.GetPtr());
		check(f.Responses() == 0 && isolated.world->GetRndGen()->Next(0, 1000) == expected.Next(0, 1000),
			"B13 rejected killer victim or sender-match predicates never evaluate the later random range formula");
	}
	{
		F f(check, isolated.world); NDb::Ptr<NDb::EventProcessorBase> processor = F::Near();
		auto* group = new NDb::EventProcessorGroup; group->processors.push_back(processor);
		NDb::Ptr<NDb::EventProcessorBase> root = group;
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(root, "1"); auto app = f.Apply(check, db); if (app)
		{
			f.Event(NDb::BASEUNITEVENT_DEATH); f.Event(NDb::BASEUNITEVENT_UNSUMMON);
			check(f.Responses() == 0 && app->IsAlive(), "B13 malformed death and unsummon payloads cannot consume a nearby-kill action");
			auto victim = f.Victim(); f.Kill(victim.GetPtr(), f.sender.GetPtr());
			check(f.Responses() == 1 && app->Step(0), "B13 nearby-kill processors compose through the original group action budget");
		}
	}
	for (int type : {-1, NDb::KnownEnum<NDb::EUnitType>::SizeOf(), 32, 63})
	{
		F f(check, isolated.world); NDb::Ptr<NDb::EventProcessorBase> processor = F::Near();
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(processor, "1"); auto app = f.Apply(check, db); if (!app) continue;
		auto victim = f.Victim(); const auto originalType = victim->GetUnitType(); victim->SetUnitType(static_cast<NDb::EUnitType>(type));
		const PFBaseUnitEventUnitDeath malformed(CPtr<PFBaseUnit>(victim.GetPtr()), CPtr<PFBaseUnit>(f.sender.GetPtr()), nullptr);
		f.receiver->EventHappened(malformed); victim->SetUnitType(originalType);
		check(f.Responses() == 0 && !app->Step(0), "B13 malformed victim types reject before a mask shift and preserve the action budget");
	}
	{
		F f(check, isolated.world); auto* raw = F::Near(); raw->applyTarget = NDb::APPLICATORAPPLYTARGET_APPLICATORSELECTEDTARGET;
		const_cast<NDb::Ability*>(f.abilityDb.GetPtr())->flags = NDb::ABILITYFLAGS_APPLYTODEAD;
		NDb::Ptr<NDb::EventProcessorBase> processor = raw;
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(processor, "1"); auto app = f.Apply(check, db); if (app)
		{
			auto victim = f.Victim(); const float strength = victim->GetStatValue(NDb::STAT_STRENGTH);
			f.Kill(victim.GetPtr(), f.sender.GetPtr());
			check(f.Responses() == 0 && victim->IsDead() && victim->GetStatValue(NDb::STAT_STRENGTH) == strength + 1,
				"B13 selected-target response receives the actual dead victim, not the killer or ability owner");
		}
	}
	{
		F f(check, isolated.world); auto* raw = F::Near();
		raw->checkSender = NDb::TRIGGEREVENTCHECKSENDER_OBJECTFILTER;
		raw->targets = static_cast<NDb::ESpellTarget>(NDb::SPELLTARGET_ALL | NDb::SPELLTARGET_ALLY);
		NDb::Ptr<NDb::EventProcessorBase> processor = raw;
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(processor, "1"); auto app = f.Apply(check, db); if (app)
		{
			f.Kill(f.receiver.GetPtr(), f.sender.GetPtr());
			check(f.Responses() == 1, "B13 nearby-death uses its original faction/type predicate: no SELF-bit gate or extra sender-object filtering");
		}
	}
	std::printf("B13 nearby-death triggers checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Read the actual inline constants with the original XML serializer, keeping all formulas intact. */
static NDb::Ptr<NDb::UnitConstantsContainer> NearbyConstantsB13(IXmlSaver& saver, const char* field)
{
	auto* constants = new NDb::UnitConstantsContainer; NDb::Ptr<NDb::UnitConstantsContainer> result = constants;
	if (!saver.StartChunk(field, 0)) return result;
	if (saver.StartChunk("UnitConstantsContainer", 0))
	{
		if (saver.StartChunk("vars", 0))
		{
			for (int i = 1, count = saver.CountChunks(); i <= count && i <= 128; ++i)
			{
				if (!saver.StartChunk("Item", i)) continue;
				auto* constant = new NDb::UnitConstant; saver.Add("UnitConstant", constant);
				constants->vars.push_back(constant); saver.FinishChunk();
			}
			saver.FinishChunk();
		}
		saver.FinishChunk();
	}
	saver.FinishChunk(); return result;
}

/** Decode only G011's original nearby-death trigger and its single healing response. */
struct NearbyDeathFieldsB13
{
	NDb::TriggerApplicator* db;
	int operator&(IXmlSaver& saver)
	{
		saver.Add("actionsCount", &db->actionsCount); saver.Add("applyTarget", &db->applyTarget);
		saver.Add("behaviorFlags", &db->behaviorFlags); saver.Add("enabled", &db->enabled);
		saver.Add("lifeTime", &db->lifeTime); saver.Add("startCondition", &db->startCondition);
		saver.Add("useAbilityCooldown", &db->useAbilityCooldown); saver.Add("useAbilityOwnerMana", &db->useAbilityOwnerMana);
		saver.Add("playAck", &db->playAck);
		if (!saver.StartChunk("processor", 0) || !saver.StartChunk("EventProcessorUnitDieNearMe", 0)) return 0;
		auto* processor = new NDb::EventProcessorUnitDieNearMe; db->processor = processor;
		saver.Add("applyTarget", &processor->applyTarget); saver.Add("range", &processor->range);
		saver.Add("checkSender", &processor->checkSender); saver.Add("senderMatchTarget", &processor->senderMatchTarget);
		saver.Add("targets", &processor->targets); saver.Add("killerFlags", &processor->killerFlags);
		if (saver.StartChunk("spell", 0) && saver.StartChunk("Spell", 0))
		{
			auto* spell = new NDb::Spell; processor->spell = spell;
			if (saver.StartChunk("applicators", 0))
			{
				if (saver.CountChunks() == 1 && saver.StartChunk("Item", 1))
				{
					auto* effect = new NDb::HealApplicator;
					saver.Add("HealApplicator", effect); spell->applicators.push_back(effect); saver.FinishChunk();
				}
				saver.FinishChunk();
			}
			if (saver.StartChunk("dispatch", 0))
			{
				auto* dispatch = new NDb::DispatchImmediate; saver.Add("DispatchImmediate", dispatch);
				spell->dispatch = dispatch; saver.FinishChunk();
			}
			saver.FinishChunk(); saver.FinishChunk();
		}
		saver.FinishChunk(); saver.FinishChunk(); return 0;
	}
};

/** G011 heals its registered passive owner by the shipped amount for nearby hostile deaths. */
static void NearbyDeathAuthoredB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	auto& ai = const_cast<NDb::AILogicParameters&>(isolated.world->GetAIWorld()->GetAIParameters());
	const NDb::Ptr<NDb::UnitConstantsContainer> savedConstants = ai.globalConstants;
	{
		CObj<Stream> xml = files->OpenFile("GameLogic/AILogic.AILP.xdb", FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
		check(xml && xml->IsOk(), "B13 authored global constant XML is readable");
		if (xml && xml->IsOk())
		{
			CObj<IXmlSaver> saver = CreateXmlSaver(xml, true);
			if (saver->StartChunk("AILogicParameters", 0)) ai.globalConstants = NearbyConstantsB13(*saver, "globalConstants");
		}
	}
	{
		CObj<Stream> xml = files->OpenFile("Items/Talents/Level1/G011-HealByNearbyDeath-Life.TALENT.xdb", FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
		check(xml && xml->IsOk(), "B13 shipped G011 talent XML is readable");
		if (xml && xml->IsOk())
		{
			CObj<IXmlSaver> saver = CreateXmlSaver(xml, true);
			const bool root = saver->StartChunk("Talent", 0);
			auto* abilityRaw = new NDb::Ability; NDb::Ptr<NDb::Ability> abilityDb = abilityRaw;
			if (root) { saver->Add("flags", &abilityRaw->flags); abilityRaw->constants = NearbyConstantsB13(*saver, "constants"); }
			NDb::Ptr<NDb::TriggerApplicator> trigger;
			if (root && saver->StartChunk("passiveApplicators", 0))
			{
				for (int i = 1, count = saver->CountChunks(); i <= count && i <= 128; ++i)
				{
					if (!saver->StartChunk("Item", i)) continue;
					if (saver->GetIdAttrValue() == "682992D4-2E57-4DF0-8506-139801186BFE")
					{
						auto* raw = new NDb::TriggerApplicator; trigger = raw;
						NearbyDeathFieldsB13 fields{raw}; saver->Add("TriggerApplicator", &fields);
					}
					saver->FinishChunk();
				}
			}
			check(trigger && abilityRaw->constants && abilityRaw->constants->vars.size() == 3,
				"B13 original XML reader finds G011's exact healing trigger and unchanged local constants");
			if (trigger)
			{
				const auto* processor = dynamic_cast<const NDb::EventProcessorUnitDieNearMe*>(trigger->processor.GetPtr());
				const auto* heal = processor && processor->spell && processor->spell->applicators.size() == 1
					? dynamic_cast<const NDb::HealApplicator*>(processor->spell->applicators[0].GetPtr()) : nullptr;
				check(heal && heal->uiEvent && heal->uiEvent->GetDBID().GetFileName() == "UI/Events/CommonEvents/EventHeal.UIEV.xdb",
					"B13 original healing serializer retains its external UI-event resource through the real DB cache");
				abilityRaw->passiveApplicators.push_back(trigger);
				for (NDb::EFaction faction : {NDb::FACTION_FREEZE, NDb::FACTION_BURN, NDb::FACTION_NEUTRAL})
				for (float distance : {13.f, 14.f, 15.f})
				{
					NearbyDeathFixtureB13 f(check, isolated.world); f.receiver->TeleportTo(CVec2(10, 10));
					f.receiver->SetHealth(400);
					CObj<PFAbilityData> passive = new PFAbilityData(f.receiver.GetPtr(), abilityDb, NDb::ABILITYTYPEID_SPECIAL);
					check(passive->GetConstant("Heal") && passive->GetConstant("Heal")->var.sString == "6"
						&& passive->GetConstant("Range_Long") && passive->GetConstant("Range_Long")->var.sString == "14",
						"B13 authored G011 heal and global range come from original local/global constant lookup");
					passive->ApplyPassivePart(true); auto victim = f.Victim(faction);
					auto* moving = dynamic_cast<PFBaseMovingUnit*>(victim.GetPtr());
					check(moving && moving->TeleportTo(CVec2(10 + distance, 10)), "B13 authored healing fixture places the real victim at the selected range");
					f.Kill(victim.GetPtr(), f.sender.GetPtr());
					const float beforeStep = f.receiver->GetHealth(); isolated.world->Step(.01f, .01f);
					check(beforeStep == 400 && f.receiver->GetHealth() == (faction != NDb::FACTION_FREEZE && distance <= 14 ? 406 : 400),
						"B13 real G011 passive heals six through its authored deferred dispatch only for hostile deaths within inclusive range14");
					passive->ApplyPassivePart(false);
				}
			}
		}
	}
	ai.globalConstants = savedConstants;
	// Release queued spells and their DB references before the cache and filesystem.
	isolated.owner = nullptr; isolated.world = nullptr;
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 authored nearby-death healing checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
