/** Actual original trigger factories expose permanent response effects on the surviving caster. */
struct DeathTriggerFixtureB13 : TriggerConditionFixtureB13
{
	using TriggerConditionFixtureB13::TriggerConditionFixtureB13;
	static NDb::EventProcessorOnDeath* Death(const char* delay = "0")
	{
		auto* db = new NDb::EventProcessorOnDeath; db->castSpellDelay.sString = delay;
		db->applyTarget = NDb::APPLICATORAPPLYTARGET_ABILITYOWNER; db->spell = Spell(); return db;
	}
	static NDb::EventProcessorLastHit* LastHit()
	{
		auto* db = new NDb::EventProcessorLastHit;
		db->applyTarget = NDb::APPLICATORAPPLYTARGET_ABILITYOWNER; db->spell = Spell(); return db;
	}
	float Responses() const { return sender->GetStatValue(NDb::STAT_STRENGTH) - senderStrength; }
	void Damage(float amount)
	{
		auto hit = DirectDamage(); hit.pSender = sender.GetPtr(); hit.amount = amount;
		hit.damageType = NDb::APPLICATORDAMAGETYPE_PURE; hit.ignoreDefences = true; receiver->OnDamage(hit);
	}
};

/** Lethal damage emits last-hit before death, even when original death-prevention flags save the victim. */
static void LastHitTriggerB13(Checks& check, PFWorld* world)
{
	const int before = check.count, failed = check.failed;
	using F = DeathTriggerFixtureB13;
	for (bool prevent : {false, true}) for (bool forbidRewards : {false, true}) for (bool grouped : {false, true})
	{
		F f(world); NDb::Ptr<NDb::EventProcessorBase> processor = F::LastHit();
		if (grouped) { auto* group = new NDb::EventProcessorGroup; group->processors.push_back(processor); processor = group; }
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(processor, "1"); auto app = f.Apply(check, db); if (!app) continue;
		if (prevent) f.receiver->AddFlag(NDb::UNITFLAG_FORBIDDEATH);
		if (forbidRewards) f.receiver->AddFlag(NDb::UNITFLAG_FORBIDREWARDS);
		f.Damage(1); check(f.Responses() == 0 && f.receiver->GetHealth() == 799,
			"B13 last-hit processor ignores ordinary nonlethal damage");
		f.Damage(800);
		check(f.Responses() == 1 && f.receiver->IsDead() == !prevent,
			"B13 original lethal damage fires last-hit even when death or kill rewards are forbidden");
		check(app->Step(0) && f.Responses() == 1, "B13 a completed last-hit action cannot be spent twice");
	}
	{
		F f(world); NDb::Ptr<NDb::EventProcessorBase> processor = F::LastHit();
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(processor, "1"); auto app = f.Apply(check, db); if (app)
		{
			f.Event(NDb::BASEUNITEVENT_LASTHIT); f.Event(NDb::BASEUNITEVENT_DEATH);
			check(f.Responses() == 0 && !app->Step(0), "B13 malformed last-hit payloads do not consume an action");
			f.Damage(801); check(f.Responses() == 1, "B13 rejected malformed events leave the real lethal action available");
		}
	}
	std::printf("B13 last-hit triggers checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Death timing uses original event payloads, selected sender filters and the buff's real world-step ownership. */
static void OnDeathTriggerB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	using F = DeathTriggerFixtureB13;
	for (int flags : {int(PFBaseUnit::UNITDIEFLAGS_NONE), int(PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS), int(PFBaseUnit::UNITDIEFLAGS_UNSUMMON)})
	{
		F f(isolated.world); NDb::Ptr<NDb::EventProcessorBase> processor = F::Death();
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(processor, "1"); auto app = f.Apply(check, db); if (!app) continue;
		f.Event(NDb::BASEUNITEVENT_DEATH);
		check(f.Responses() == 0 && !app->Step(0), "B13 death processor requires the original typed victim and killer event");
		f.receiver->KillUnit(f.sender.GetPtr(), flags);
		check(f.Responses() == int(flags == PFBaseUnit::UNITDIEFLAGS_NONE),
			"B13 death trigger fires on actual ordinary death, not forbidden rewards or unsummon");
	}
	for (int filter = 0; filter < 4; ++filter) for (int killerKind = 0; killerKind < 4; ++killerKind)
	{
		F f(isolated.world); auto* raw = F::Death(); raw->checkSender = static_cast<NDb::ETriggerEventCheckSender>(filter);
		raw->senderMatchTarget = NDb::APPLICATORAPPLYTARGET_ABILITYOWNER;
		raw->objectFilter = static_cast<NDb::ESpellTarget>(NDb::SPELLTARGET_ALL | NDb::SPELLTARGET_ALLY);
		NDb::Ptr<NDb::EventProcessorBase> processor = raw;
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(processor, "1"); auto app = f.Apply(check, db); if (!app) continue;
		auto enemy = Unit(isolated.world, NDb::FACTION_BURN, 40); auto summon = Unit(isolated.world, NDb::FACTION_FREEZE, 40);
		summon->SetMaster(f.sender.GetPtr());
		PFBaseUnit* killer = killerKind == 0 ? nullptr : killerKind == 1 ? f.sender.GetPtr() : killerKind == 2 ? enemy.GetPtr() : summon.GetPtr();
		f.receiver->KillUnit(killer, PFBaseUnit::UNITDIEFLAGS_NONE);
		const bool matches = (!(filter & NDb::TRIGGEREVENTCHECKSENDER_MATCHTARGET) || killerKind == 1)
			&& (!(filter & NDb::TRIGGEREVENTCHECKSENDER_OBJECTFILTER) || killerKind != 2);
		check(f.Responses() == int(matches),
			"B13 original death filters use the actual killer, skip null sender masking, and never substitute a qualified master");
		summon->SetMaster(nullptr); enemy->Cleanup(); summon->Cleanup();
	}
	for (const char* delay : {"", "-1", "0.0f", "0.5", "cWrapperBonus", "cMissing", "1/0", "1e39", "compiled-only"})
	{
		F f(isolated.world); auto* raw = F::Death(delay);
		if (strcmp(delay, "compiled-only") == 0) { raw->castSpellDelay.sString = ""; raw->castSpellDelay.compiledString = "unsupported"; }
		NDb::Ptr<NDb::EventProcessorBase> processor = raw;
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(processor, "1"); auto app = f.Apply(check, db); if (!app) continue;
		f.receiver->KillUnit(f.sender.GetPtr(), PFBaseUnit::UNITDIEFLAGS_NONE);
		const bool immediate = !*delay || strcmp(delay, "-1") == 0 || strcmp(delay, "0.0f") == 0;
		const bool delayed = strcmp(delay, "0.5") == 0 || strcmp(delay, "cWrapperBonus") == 0;
		check(f.Responses() == int(immediate), "B13 empty and nonpositive death delays are immediate; invalid selected formulas never fire");
		for (float dt : {-1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) app->Step(dt);
		app->Step(.25f); check(f.Responses() == int(immediate), "B13 invalid or insufficient elapsed time cannot complete a pending death spell");
		app->Step(2.f); check(f.Responses() == int(immediate || delayed), "B13 checked death delay uses original ability constants and actual elapsed time");
		app->Step(0); app->Step(1); check(f.Responses() == int(immediate || delayed), "B13 a completed delayed death casts only once");
	}
	for (bool survive : {false, true})
	{
		B8BehaviourForeignWorld owning(check); if (!owning.world) continue;
		F f(owning.world); NDb::Ptr<NDb::EventProcessorBase> processor = F::Death("0.5");
		auto* raw = F::Descriptor(processor, "1");
		if (survive) raw->behaviorFlags = NDb::BUFFBEHAVIOR_DONTSTOPONDEATH;
		NDb::Ptr<NDb::TriggerApplicator> db = raw; auto app = f.Apply(check, db); if (!app) continue;
		f.receiver->KillUnit(f.sender.GetPtr(), PFBaseUnit::UNITDIEFLAGS_NONE);
		for (int i = 0; i < 3; ++i) owning.world->Step(.25f, .25f);
		check(f.Responses() == int(survive) && !app->IsAlive() && f.instance->GetActiveApplicatorsCount() == 0,
			"B13 actual world death cleanup retains only authored surviving triggers until their delayed action retires");
	}
	for (bool allowDead : {false, true}) for (bool marked : {false, true}) for (bool dead : {false, true})
	{
		F f(isolated.world); auto* abilityDb = const_cast<NDb::Ability*>(f.abilityDb.GetPtr());
		abilityDb->flags = marked ? NDb::ABILITYFLAGS_APPLYTODEAD : static_cast<NDb::EAbilityFlags>(0);
		if (dead) f.receiver->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
		// Windows XOR promotes the flag value (4) to int, so a set bit also permits living units.
		const bool original = (!f.receiver->IsDead() ^ (abilityDb->flags & NDb::ABILITYFLAGS_APPLYTODEAD)) || allowDead;
		check(f.ability->IsTargetValid(Target(f.receiver.GetPtr()), allowDead) == original,
			"B13 death-aware ability admission matches the exact original Windows flag expression for living and dead targets");
	}
	{
		F f(isolated.world); NDb::Ptr<NDb::EventProcessorBase> processor = F::Death("0.0000002384185792");
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(processor); auto app = f.Apply(check, db); if (app)
		{
			f.receiver->KillUnit(f.sender.GetPtr(), PFBaseUnit::UNITDIEFLAGS_NONE);
			app->Step(EPS_VALUE * 1.5f); app->Step(0); app->Step(0);
			check(f.Responses() == 1 && app->IsAlive(),
				"B13 unlimited delayed triggers clear a positive sub-epsilon remainder before callbacks and never repeat on zero-time ticks");
		}
	}
	{
		F f(isolated.world); f.Payment(25); NDb::Ptr<NDb::EventProcessorBase> processor = F::Death("0.5");
		auto* raw = F::Descriptor(processor, "1"); raw->useAbilityCooldown = true; raw->useAbilityOwnerMana = true;
		NDb::Ptr<NDb::TriggerApplicator> db = raw; auto app = f.Apply(check, db); if (app)
		{
			f.receiver->KillUnit(f.sender.GetPtr(), PFBaseUnit::UNITDIEFLAGS_NONE);
			check(f.Responses() == 0 && f.sender->GetMana() == 25 && f.ability->IsReady(),
				"B13 arming a delayed death action spends neither mana nor cooldown");
			f.sender->ChangeMana(-20); app->Step(.5f); f.sender->ChangeMana(20);
			f.Event(NDb::BASEUNITEVENT_APPLICATORSTEP); app->Step(.25f);
			check(f.Responses() == 0 && f.ability->IsReady(),
				"B13 insufficient payment or an untyped step leaves the pending death countdown unchanged");
			app->Step(.25f);
			check(f.Responses() == 1 && f.sender->GetMana() == 15 && !f.ability->IsReady(),
				"B13 delayed completion pays and restarts cooldown exactly once");
		}
	}
	for (bool expire : {false, true})
	{
		F f(isolated.world); NDb::Ptr<NDb::EventProcessorBase> processor = F::Death("0.5");
		auto* raw = F::Descriptor(processor, "1"); if (expire) raw->lifeTime.sString = "0.25";
		NDb::Ptr<NDb::TriggerApplicator> db = raw; auto app = f.Apply(check, db); if (!app) continue;
		f.receiver->KillUnit(f.sender.GetPtr(), PFBaseUnit::UNITDIEFLAGS_NONE);
		if (expire)
		{
			check(app->Step(.5f) && f.Responses() == 0, "B13 original buff expiry wins over a pending death action on the same tick");
			app->Stop(); app->Step(1); check(f.Responses() == 0, "B13 retired pending actions cannot execute later");
		}
		else
		{
			raw->enabled.sString = "false"; app->Step(1); raw->enabled.sString = "true"; app->Step(.25f);
			check(f.Responses() == 0, "B13 authored disable pauses the original event-driven death countdown");
			app->Step(.25f); check(f.Responses() == 1, "B13 reenabled death trigger resumes its retained countdown");
		}
	}
	{
		F f(isolated.world); auto* raw = F::Death("0.5"); NDb::Ptr<NDb::EventProcessorBase> processor = raw;
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(processor, "1"); auto app = f.Apply(check, db); if (app)
		{
			f.receiver->KillUnit(f.sender.GetPtr(), PFBaseUnit::UNITDIEFLAGS_NONE); app->Step(.25f);
			raw->castSpellDelay.sString = "cMissing"; f.receiver->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_NONE);
			app->Step(.25f); check(f.Responses() == 1, "B13 a malformed later death delay cannot discard an already armed valid action");
		}
	}
	std::printf("B13 on-death triggers checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Decode the shipped Brewer branch with the original XML serializers, without loading unrelated talent logic. */
struct BrewerDeathFieldsB13
{
	NDb::TriggerApplicator* db;
	int operator&(IXmlSaver& saver)
	{
		saver.Add("actionsCount", &db->actionsCount); saver.Add("applyTarget", &db->applyTarget);
		saver.Add("behaviorFlags", &db->behaviorFlags); saver.Add("enabled", &db->enabled);
		saver.Add("lifeTime", &db->lifeTime); saver.Add("startCondition", &db->startCondition);
		saver.Add("useAbilityCooldown", &db->useAbilityCooldown); saver.Add("useAbilityOwnerMana", &db->useAbilityOwnerMana);
		saver.Add("playAck", &db->playAck);
		if (!saver.StartChunk("processor", 0) || !saver.StartChunk("EventProcessorOnDeath", 0)) return 0;
		auto* processor = new NDb::EventProcessorOnDeath; db->processor = processor;
		saver.Add("applyTarget", &processor->applyTarget); saver.Add("castSpellDelay", &processor->castSpellDelay);
		saver.Add("checkSender", &processor->checkSender); saver.Add("senderMatchTarget", &processor->senderMatchTarget);
		saver.Add("objectFilter", &processor->objectFilter);
		if (saver.StartChunk("spell", 0) && saver.StartChunk("Spell", 0))
		{
			auto* spell = new NDb::Spell; processor->spell = spell;
			if (saver.StartChunk("applicators", 0))
			{
				if (saver.CountChunks() == 1 && saver.StartChunk("Item", 1))
				{
					auto* effect = new NDb::PermanentStatModApplicator;
					saver.Add("PermanentStatModApplicator", effect); spell->applicators.push_back(effect); saver.FinishChunk();
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

/** The real Brewer passive clears its dash counter through an immediate spell after receiver death. */
static void BrewerDeathAuthoredB13(Checks& check, PFWorld* world, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	{
		CObj<Stream> xml = files->OpenFile("Items/Talents/Class/Brewer/Ability_A1.TALENT.xdb", FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
		check(xml && xml->IsOk(), "B13 shipped Brewer passive XML is readable");
		if (xml && xml->IsOk())
		{
			CObj<IXmlSaver> saver = CreateXmlSaver(xml, true);
			const bool root = saver->StartChunk("Talent", 0); NDb::EAbilityFlags abilityFlags = static_cast<NDb::EAbilityFlags>(0);
			if (root) saver->Add("flags", &abilityFlags);
			const bool found = root && saver->StartChunk("passiveApplicators", 0)
				&& saver->StartChunk("Item", 2) && saver->GetIdAttrValue() == "D9819597-CFB5-4A36-9084-17A4D4BB5E9D";
			check(found, "B13 original XML reader reaches the exact Brewer death-trigger identity");
			if (found)
			{
				auto* raw = new NDb::TriggerApplicator; NDb::Ptr<NDb::TriggerApplicator> db = raw;
				BrewerDeathFieldsB13 fields{raw}; saver->Add("TriggerApplicator", &fields);
				const auto* processor = dynamic_cast<const NDb::EventProcessorOnDeath*>(db->processor.GetPtr());
				const auto* effect = processor && processor->spell && processor->spell->applicators.size() == 1
					? dynamic_cast<const NDb::PermanentStatModApplicator*>(processor->spell->applicators[0].GetPtr()) : nullptr;
				check(db->lifeTime.sString == "-1.0" && db->actionsCount.sString == "0" && processor
					&& processor->castSpellDelay.sString == "0.0f" && effect && effect->modifier.variable == "A1_BrewerDash"
					&& effect->modifier.addValue.sString == "0.0f" && effect->modifier.multValue.sString == "0.0f",
					"B13 shipped trigger and original counter-reset effect are decoded without replacement formulas");
				for (bool death : {false, true})
				{
					DeathTriggerFixtureB13 f(world); f.sender->GetVariableVWM("A1_BrewerDash")->SetBaseValue(3);
					f.receiver->GetVariableVWM("A1_BrewerDash")->SetBaseValue(7);
					auto app = f.Apply(check, db); if (!app) continue;
					if (death) f.receiver->KillUnit(f.sender.GetPtr(), PFBaseUnit::UNITDIEFLAGS_NONE);
					else f.Event(NDb::BASEUNITEVENT_LASTHIT);
					check(f.sender->GetVariable("A1_BrewerDash") == (death ? 0 : 3) && f.receiver->GetVariable("A1_BrewerDash") == 7,
						"B13 actual Brewer death branch resets only its ability owner's dash counter through the shipped spell");
				}
				DeathTriggerFixtureB13 self(world);
				auto* passiveDb = const_cast<NDb::Ability*>(self.abilityDb.GetPtr());
				passiveDb->flags = abilityFlags; passiveDb->passiveApplicators.push_back(db);
				self.sender->GetVariableVWM("A1_BrewerDash")->SetBaseValue(3);
				self.ability->ApplyPassivePart(true);
				check(abilityFlags == 599 && self.sender->GetVariable("A1_BrewerDash") == 3,
					"B13 shipped Brewer ability flags attach its original passive without prematurely clearing the live counter");
				self.sender->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_NONE);
				check(self.sender->IsDead() && self.sender->GetVariable("A1_BrewerDash") == 0,
					"B13 actual Brewer self-death passive resets the dying ability owner's dash counter with original ability flags");
				self.ability->ApplyPassivePart(false);
			}
		}
	}
	RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 authored Brewer death checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
