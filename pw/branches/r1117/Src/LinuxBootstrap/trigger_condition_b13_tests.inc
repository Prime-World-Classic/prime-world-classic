/** Real immediate dispatch increments a permanent stat, making every trigger action observable. */
struct TriggerConditionFixtureB13 : WrapperFactoryFixtureB12
{
	explicit TriggerConditionFixtureB13(PFWorld* world) : WrapperFactoryFixtureB12(world) {}
	static NDb::Spell* Spell()
	{
		auto* spell = new NDb::Spell; auto* dispatch = new NDb::DispatchImmediate;
		dispatch->instantApply = true; spell->dispatch = dispatch;
		auto* effect = new NDb::PermanentStatModApplicator;
		effect->enabled.sString = "true"; effect->modifier.stat = NDb::STAT_STRENGTH;
		effect->modifier.multValue.sString = "1"; effect->modifier.addValue.sString = "1";
		spell->applicators.push_back(effect); return spell;
	}
	static NDb::EventProcessorCondition* Condition(int mask = NDb::BASEUNITEVENTFLAGS_MOVE, const char* condition = "true")
	{
		auto* db = new NDb::EventProcessorCondition;
		db->events = static_cast<NDb::EBaseUnitEventFlags>(mask); db->condition.sString = condition;
		db->spell = Spell(); return db;
	}
	static NDb::TriggerApplicator* Descriptor(const NDb::EventProcessorBase* processor, const char* count = "0")
	{
		auto* db = new NDb::TriggerApplicator;
		db->enabled.sString = "true"; db->startCondition.sString = "true"; db->lifeTime.sString = "10";
		db->actionsCount.sString = count; db->processor = processor; return db;
	}
	CObj<PFBaseApplicator> Apply(Checks& check, const NDb::BaseApplicator* db)
	{
		auto app = Create(db);
		check(app && Activate(app), "B13 trigger factory admits the real original applicator"); return app;
	}
	void Event(NDb::EBaseUnitEvent type = NDb::BASEUNITEVENT_MOVE)
	{
		const PFBaseUnitEvent event(type); receiver->EventHappened(event);
	}
	void Payment(float mana, const char* cost = "10")
	{
		auto* db = const_cast<NDb::Ability*>(abilityDb.GetPtr()); db->manaCost.sString = cost;
		db->cooldownTime.sString = "2"; ability->Reset(); sender->ChangeMana(mana - sender->GetMana());
	}
};

/** Distinct loaded DB identities distinguish descriptor matching from pointer/instance matching. */
struct TriggerAbilityDbB13 : NDb::Ability
{
	explicit TriggerAbilityDbB13(const char* id)
	{
		SetDbId(NDb::DBID(id)); ProbeResourceCache::MarkLoaded(this); manaCost.sString = "0";
	}
};

/** Exercise the actual trigger factory, event dispatcher, effect delivery and bounded action ownership. */
static void TriggerConditionB13(Checks& check, PFWorld* world)
{
	const int before = check.count, failed = check.failed;
	using F = TriggerConditionFixtureB13;
	for (int wanted : {0, 13, 15, 29, 31})
	{
		F f(world); const int mask = static_cast<int>(1u << wanted);
		NDb::Ptr<NDb::EventProcessorCondition> proc = F::Condition(mask);
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(proc); auto app = f.Apply(check, db); if (!app) continue;
		float actions = 0;
		for (int type = 0; type < NDb::KnownEnum<NDb::EBaseUnitEvent>::SizeOf(); ++type)
		{
			f.Event(static_cast<NDb::EBaseUnitEvent>(type));
			// Preserve the original signed-int mask extension and step-only listener optimization.
			if (wanted != 13 && ((int64(1) << type) & int64(mask))) ++actions;
			check(f.Delta() == actions, "B13 trigger uses original event-mask membership without shift wrap or wrong-event delivery");
		}
		app->Step(0); if (wanted == 13) ++actions;
		check(f.Delta() == actions, "B13 step-only trigger is ticked once without an external event subscription");
		for (int invalid : {-1, 36, 63, 64}) f.Event(static_cast<NDb::EBaseUnitEvent>(invalid));
		check(f.Delta() == actions, "B13 out-of-enum events never shift or invoke a trigger processor");
		app->Stop(); f.Event(static_cast<NDb::EBaseUnitEvent>(wanted)); app->Step(0);
		check(f.Delta() == actions && f.instance->GetActiveApplicatorsCount() == 0,
			"B13 stopped trigger releases subscription and ability ownership without another action");
	}
	for (const char* count : {"", "0", "-1", "2", "2.9", "cWrapperBonus", "cMissing", "1/0", "1e39", "2147483648", "compiled-only"})
	{
		F f(world); NDb::Ptr<NDb::EventProcessorCondition> proc = F::Condition();
		auto* raw = F::Descriptor(proc, count); NDb::Ptr<NDb::TriggerApplicator> db = raw;
		if (strcmp(count, "compiled-only") == 0) { raw->actionsCount.sString = ""; raw->actionsCount.compiledString = "unsupported"; }
		auto app = f.Apply(check, db); if (!app) continue;
		const bool unlimited = !*count || strcmp(count, "0") == 0 || strcmp(count, "-1") == 0;
		const bool bounded = strcmp(count, "2") == 0 || strcmp(count, "2.9") == 0 || strcmp(count, "cWrapperBonus") == 0;
		for (int i = 0; i < 4; ++i) f.Event();
		check(f.Delta() == (unlimited ? 4 : bounded ? 2 : 0),
			"B13 checked count preserves unlimited nonpositive and finite truncation but rejects malformed or overflowing limits");
		check(app->Step(0) == !unlimited, "B13 consumed or malformed trigger action budget requests retirement");
	}
	for (const char* condition : {"true", "false", "sLife > rLife", "sLife < rLife", "cMissing", "1/0", "compiled-only"})
	{
		F f(world); auto* raw = F::Condition(NDb::BASEUNITEVENTFLAGS_MOVE, condition);
		if (strcmp(condition, "compiled-only") == 0) { raw->condition.sString = ""; raw->condition.compiledString = "unsupported"; }
		NDb::Ptr<NDb::EventProcessorCondition> proc = raw;
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(proc, "1"); auto app = f.Apply(check, db); if (!app) continue;
		f.Event(); const bool matches = strcmp(condition, "true") == 0 || strcmp(condition, "sLife < rLife") == 0;
		check(f.Delta() == int(matches) && app->Step(0) == matches,
			"B13 selected condition uses live endpoints and rejects invalid expressions without consuming its action");
		if (!matches)
		{
			raw->condition.sString = "true"; raw->condition.compiledString = ""; f.Event();
			check(f.Delta() == 1 && app->Step(0), "B13 false or failed condition leaves budget available for a later valid event");
		}
	}
	for (bool cooldown : {false, true}) for (bool pay : {false, true}) for (float mana : {5.f, 25.f})
	{
		F f(world); f.Payment(mana); NDb::Ptr<NDb::EventProcessorCondition> proc = F::Condition();
		auto* raw = F::Descriptor(proc); raw->useAbilityCooldown = cooldown; raw->useAbilityOwnerMana = pay;
		NDb::Ptr<NDb::TriggerApplicator> db = raw; auto app = f.Apply(check, db); if (!app) continue;
		f.Event(); f.Event(); const int actions = pay && mana < 10 ? 0 : cooldown ? 1 : 2;
		check(f.Delta() == actions && f.sender->GetMana() == mana - (pay ? 10 * actions : 0),
			"B13 trigger observes independent cooldown and mana gates and pays once per successful action");
		check(f.ability->IsReady() == !(cooldown && actions), "B13 successful trigger alone restarts the authored ability cooldown");
	}
	{
		F f(world); NDb::Ptr<NDb::EventProcessorCondition> proc = F::Condition();
		auto* raw = F::Descriptor(proc); NDb::Ptr<NDb::TriggerApplicator> db = raw;
		auto app = f.Apply(check, db); if (app)
		{
			f.Event(); raw->enabled.sString = "false"; app->Step(0); f.Event();
			check(f.Delta() == 1 && !app->IsEnabled(), "B13 authored disable removes the actual event subscription");
			raw->enabled.sString = "true"; app->Step(0); app->Step(0); f.Event();
			check(f.Delta() == 2, "B13 reenable installs one listener and retains the action history");
			check(app->Step(11), "B13 trigger expires through its original buff timer"); app->Stop(); f.Event();
			check(f.Delta() == 2 && f.instance->GetActiveApplicatorsCount() == 0, "B13 expired trigger cannot fire again");
		}
	}
	for (bool onlyParent : {false, true})
	{
		F f(world); auto* raw = F::Condition(); raw->onlyParent = onlyParent;
		NDb::Ptr<NDb::EventProcessorCondition> proc = raw;
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(proc); auto app = f.Apply(check, db); if (!app) continue;
		f.Event(); check(f.Delta() == int(!onlyParent), "B13 onlyParent condition requires an actual event ability");
		CObj<PFAbilityInstance> second = new PFAbilityInstance(f.ability, Target(f.receiver.GetPtr()), true);
		const PFBaseUnitUseAbilityEvent same(NDb::BASEUNITEVENT_MOVE, second); f.receiver->EventHappened(same);
		check(f.Delta() == (onlyParent ? 1 : 2), "B13 onlyParent accepts the original ability descriptor identity");
		NDb::Ptr<NDb::Ability> otherDb = new TriggerAbilityDbB13("/Mock/UnrelatedTriggerAbility.xdb");
		CObj<PFAbilityData> otherData = new PFAbilityData(f.sender.GetPtr(), otherDb, NDb::ABILITYTYPEID_SPECIAL);
		CObj<PFAbilityInstance> other = new PFAbilityInstance(otherData, Target(f.receiver.GetPtr()), true);
		const PFBaseUnitUseAbilityEvent mismatch(NDb::BASEUNITEVENT_MOVE, other); f.receiver->EventHappened(mismatch);
		check(f.Delta() == (onlyParent ? 1 : 3), "B13 onlyParent compares descriptors rather than merely accepting any event ability");
	}
	for (bool cancel : {false, true})
	{
		F f(world); f.Payment(25); NDb::Ptr<NDb::EventProcessorCondition> proc = F::Condition();
		auto* raw = F::Descriptor(proc, "1"); raw->useAbilityOwnerMana = !cancel;
		NDb::Ptr<NDb::TriggerApplicator> db = raw; auto app = f.Apply(check, db); if (!app) continue;
		CObj<EventDispatchB7Listener> observer = new EventDispatchB7Listener; int delivered = 0;
		observer->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent* event) {
			if (event->GetType() == NDb::BASEUNITEVENT_APPLICATORAPPLIED && ++delivered == 1)
			{
				if (cancel) app->Stop(); else f.Event();
			}
			return 0u;
		};
		f.receiver->AddEventListener(observer.GetPtr()); f.Event(); f.Event();
		check(delivered == 1 && f.Delta() == 1 && f.sender->GetMana() == (cancel ? 25 : 15),
			"B13 synchronous child callbacks cannot recursively consume the same action or charge mana twice");
		check(app->Step(0), "B13 callback-cancelled or exhausted trigger reports terminal"); app->Stop();
		check(f.instance->GetActiveApplicatorsCount() == 0, "B13 callback teardown balances parent and instant child ownership");
		f.receiver->RemoveEventListener(observer.GetPtr()); observer->action = nullptr;
	}
	for (bool cooldown : {false, true}) for (const char* invalid : {"cMissing", "1/0", "1e39"})
	{
		F f(world); f.Payment(25, cooldown ? "10" : invalid);
		if (cooldown) const_cast<NDb::Ability*>(f.abilityDb.GetPtr())->cooldownTime.sString = invalid;
		NDb::Ptr<NDb::EventProcessorCondition> proc = F::Condition();
		auto* raw = F::Descriptor(proc, "1"); raw->useAbilityCooldown = cooldown; raw->useAbilityOwnerMana = !cooldown;
		NDb::Ptr<NDb::TriggerApplicator> db = raw; auto app = f.Apply(check, db); if (!app) continue;
		f.Event(); check(f.Delta() == 0 && f.sender->GetMana() == 25 && !app->Step(0),
			"B13 malformed selected cooldown or mana quote cannot invoke a trigger or consume its action");
		f.Payment(25); f.Event();
		check(f.Delta() == 1 && app->Step(0), "B13 corrected payment formula recovers without a lost trigger action");
	}
	{
		F f(world); NDb::Ptr<NDb::EventProcessorCondition> proc = F::Condition(NDb::BASEUNITEVENTFLAGS_APPLICATORSTEP);
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(proc); auto app = f.Apply(check, db); if (app)
		{
			for (float dt : {-1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
				check(!app->Step(dt) && f.Delta() == 0, "B13 invalid elapsed time cannot fire a step-only trigger");
			app->Step(0); check(f.Delta() == 1, "B13 zero elapsed time retains the original step event");
		}
	}
	for (bool absent : {false, true})
	{
		F f(world); NDb::Ptr<NDb::EventProcessorBase> proc = absent ? nullptr : new NDb::EventProcessorCancelDispatch;
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(proc); auto app = f.Apply(check, db); if (!app) continue;
		f.Event(); check(!app->IsAlive() && f.Delta() == 0 && f.instance->GetActiveApplicatorsCount() == 0,
			"B13 missing or still-unsupported processors terminate without leaving a subscribed inert buff");
	}
	for (bool sourceTarget : {false, true})
	{
		F f(world); auto* raw = F::Condition();
		raw->applyTarget = sourceTarget ? NDb::APPLICATORAPPLYTARGET_ABILITYOWNER : NDb::APPLICATORAPPLYTARGET_APPLICATORTARGET;
		NDb::Ptr<NDb::EventProcessorCondition> proc = raw;
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(proc); auto app = f.Apply(check, db); if (!app) continue;
		f.Event(); check(f.Delta() == int(!sourceTarget)
			&& f.sender->GetStatValue(NDb::STAT_STRENGTH) == f.senderStrength + int(sourceTarget),
			"B13 processor spell applies to the authored owner or trigger target without substituting endpoints");
	}
	for (bool processor : {false, true})
	{
		F f(world); NDb::Ptr<NDb::EventProcessorCondition> proc = processor ? F::Condition() : nullptr;
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(proc);
		const Target target(f.receiver.GetPtr()); PFApplCreatePars cp(f.instance, target); cp.pDBAppl = db;
		CObj<AbsorbShieldParentB13> parent = new AbsorbShieldParentB13(cp);
		auto app = f.Create(db, parent.GetPtr()); check(app && f.Activate(app), "B13 parent callback receives actual trigger start");
		if (!app) continue;
		f.Event(); check(!app->IsAlive() && f.Delta() == 0 && parent->starts == 1 && parent->stops == 1
			&& f.instance->GetActiveApplicatorsCount() == 0, "B13 parent start cancellation installs no trigger subscription");
	}
	{
		B8BehaviourForeignWorld owning(check); if (owning.world)
		{
			F f(owning.world); NDb::Ptr<NDb::EventProcessorCondition> proc = F::Condition(NDb::BASEUNITEVENTFLAGS_APPLICATORSTEP);
			NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(proc, "1"); auto app = f.Apply(check, db); if (app)
			{
				owning.world->Step(.1f, .1f);
				check(f.Delta() == 1 && !app->IsAlive() && f.instance->GetActiveApplicatorsCount() == 0,
					"B13 actual world tick dispatches one step action and retires its exhausted trigger");
				owning.world->Step(.1f, .1f); check(f.Delta() == 1, "B13 retired world-owned trigger cannot tick again");
			}
		}
	}
	for (bool sourceDies : {false, true})
	{
		B8BehaviourForeignWorld owning(check); if (!owning.world) continue;
		F f(owning.world); NDb::Ptr<NDb::EventProcessorCondition> proc = F::Condition();
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(proc); auto app = f.Apply(check, db); if (!app) continue;
		(sourceDies ? f.sender : f.receiver)->KillUnit(nullptr, PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS);
		owning.world->Step(.1f, .1f); f.Event();
		check(!app->IsAlive() && f.Delta() == 0 && f.instance->GetActiveApplicatorsCount() == 0,
			"B13 world endpoint death cleanup removes the trigger event subscription");
	}
	std::printf("B13 trigger conditions checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Load just the actual cooldown-trigger branch, without pulling in the entire authored program. */
struct TriggerAuthoredFieldsB13
{
	NDb::TriggerApplicator* db;
	int operator&(IXmlSaver& saver)
	{
		saver.Add("actionsCount", &db->actionsCount); saver.Add("applyTarget", &db->applyTarget);
		saver.Add("behaviorFlags", &db->behaviorFlags); saver.Add("enabled", &db->enabled);
		saver.Add("lifeTime", &db->lifeTime); saver.Add("startCondition", &db->startCondition);
		saver.Add("useAbilityCooldown", &db->useAbilityCooldown); saver.Add("useAbilityOwnerMana", &db->useAbilityOwnerMana);
		saver.Add("playAck", &db->playAck);
		if (saver.StartChunk("processor", 0))
		{
			auto* processor = new NDb::EventProcessorCondition;
			saver.Add("EventProcessorCondition", processor); db->processor = processor; saver.FinishChunk();
		}
		return 0;
	}
};

/** Plane's original mixed step/death trigger restarts its owning ability cooldown without a spell. */
static void TriggerConditionAuthoredB13(Checks& check, PFWorld* world, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	{
		CObj<Stream> xml = files->OpenFile("Items/Talents/Class/Plane/A1_program.APLR.xdb", FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
		check(xml && xml->IsOk(), "B13 shipped Plane A1 program XML is readable");
		if (xml && xml->IsOk())
		{
			CObj<IXmlSaver> saver = CreateXmlSaver(xml, true); bool found = true;
			for (int item : {1, 1, 3, 2})
				found = found && saver->StartChunk("ProgramApplicator", 0) && saver->StartChunk("applicators", 0)
					&& saver->StartChunk("Item", item) && saver->StartChunk("applicator", 0);
			check(found, "B13 original XML reader reaches the exact nested Plane cooldown trigger");
			if (found)
			{
				auto* raw = new NDb::TriggerApplicator; NDb::Ptr<NDb::TriggerApplicator> db = raw;
				TriggerAuthoredFieldsB13 fields{raw}; saver->Add("TriggerApplicator", &fields);
				const auto* processor = dynamic_cast<const NDb::EventProcessorCondition*>(raw->processor.GetPtr());
				check(raw->useAbilityCooldown && !raw->useAbilityOwnerMana && raw->lifeTime.sString == "0.7"
					&& processor && processor->events == 40960 && processor->condition.sString == "true" && !processor->spell,
					"B13 actual Plane cooldown flags duration event mask and empty spell are preserved");
				for (bool deathEvent : {false, true})
				{
					TriggerConditionFixtureB13 f(world); f.Payment(25); auto app = f.Apply(check, db); if (!app) continue;
					check(app->GetReceiver() == f.sender && f.ability->IsReady(), "B13 authored trigger targets the actual ability owner");
					if (deathEvent) { const PFBaseUnitEvent death(NDb::BASEUNITEVENT_DEATH); f.sender->EventHappened(death); }
					else app->Step(.1f);
					check(f.ability->GetCurrentCooldown() == 2 && f.sender->GetMana() == 25,
						"B13 shipped Plane trigger starts cooldown from its original death or step event without spending mana");
					f.ability->Update(.5f, true); app->Step(.1f);
					check(f.ability->GetCurrentCooldown() == 1.5f, "B13 cooldown gate prevents repeated authored trigger resets");
					check(app->Step(.7f), "B13 shipped trigger expires after its actual authored lifetime"); app->Stop();
					check(f.instance->GetActiveApplicatorsCount() == 0, "B13 authored cooldown trigger releases original ability ownership");
				}
			}
		}
	}
	RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 authored Plane trigger checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
