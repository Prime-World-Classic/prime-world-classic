/** Compose real processor descriptors; only leaf spell payloads use explicit test data. */
struct TriggerCompositionB13 : TriggerConditionFixtureB13
{
	explicit TriggerCompositionB13(PFWorld* world) : TriggerConditionFixtureB13(world) {}
	static NDb::EventProcessorGroup* Group(const NDb::EventProcessorBase* child = nullptr)
	{
		auto* db = new NDb::EventProcessorGroup; db->spell = Spell();
		if (child) db->processors.push_back(child); return db;
	}
	static NDb::EventProcessorCancelDispatch* Cancel()
	{
		auto* db = new NDb::EventProcessorCancelDispatch;
		db->filter.abilityCondition.sString = "true"; db->spell = Spell(); return db;
	}
};

/** Original groups execute every matching child in order and one group spell after any success. */
static void TriggerGroupsB13(Checks& check, PFWorld* world)
{
	const int before = check.count, failed = check.failed;
	using F = TriggerCompositionB13;
	for (bool first : {false, true}) for (bool second : {false, true}) for (bool sharedMask : {false, true})
	{
		F f(world); auto* group = F::Group(F::Condition(NDb::BASEUNITEVENTFLAGS_MOVE, first ? "true" : "false"));
		group->processors.push_back(F::Condition(sharedMask ? NDb::BASEUNITEVENTFLAGS_MOVE : NDb::BASEUNITEVENTFLAGS_ATTACK,
			second ? "true" : "false"));
		NDb::Ptr<NDb::EventProcessorGroup> proc = group;
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(proc, "1"); auto app = f.Apply(check, db); if (!app) continue;
		f.Event(); const int children = int(first) + int(sharedMask && second);
		check(f.Delta() == children + int(children > 0), "B13 group executes all matching successful children then one group spell");
		f.Event(); check(f.Delta() == children + int(children > 0), "B13 complete group consumes exactly one parent action");
		check(app->Step(0) == (children > 0), "B13 unsuccessful group leaves its action budget available");
	}
	for (int depth : {1, 2, 4, 127, 128})
	{
		F f(world); NDb::Ptr<NDb::EventProcessorBase> tree = F::Condition();
		for (int i = 0; i < depth; ++i) tree = F::Group(tree);
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(tree); auto app = f.Apply(check, db); if (!app) continue;
		f.Event(); const bool valid = depth < 128;
		check(app->IsAlive() == valid && f.Delta() == (valid ? depth + 1 : 0),
			"B13 nested groups retain leaf-to-root spell order within the checked 128-node construction bound");
	}
	for (int width : {0, 2, 127, 128})
	{
		F f(world); auto* group = F::Group(); NDb::Ptr<NDb::EventProcessorGroup> proc = group;
		NDb::Ptr<NDb::EventProcessorCondition> shared = F::Condition();
		for (int i = 0; i < width; ++i) group->processors.push_back(shared.GetPtr());
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(proc); auto app = f.Apply(check, db); if (!app) continue;
		f.Event(); const bool valid = width < 128;
		check(app->IsAlive() == valid && f.Delta() == (valid && width ? width + 1 : 0),
			"B13 repeated leaf descriptors are independent processors, not cycles, and obey total construction bounds");
	}
	for (int invalid = 0; invalid < 4; ++invalid)
	{
		F f(world); auto* a = F::Group(F::Condition()); auto* b = F::Group();
		NDb::Ptr<NDb::EventProcessorGroup> aDb = a, bDb = b;
		if (invalid == 0) a->processors.push_back(NDb::Ptr<NDb::EventProcessorBase>());
		if (invalid == 1) a->processors.push_back(new NDb::EventProcessorDamageTransformer);
		if (invalid == 2) a->processors.push_back(a);
		if (invalid == 3) { a->processors.push_back(b); b->processors.push_back(a); }
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(aDb, "random(1,4)");
		NRandom::RandomGenerator expected(*world->GetRndGen()); auto app = f.Apply(check, db);
		check(world->GetRndGen()->Next(0, 1000) == expected.Next(0, 1000),
			"B13 malformed processor tree rejects before evaluating a random action limit");
		if (app)
		{
			f.Event(); check(!app->IsAlive() && f.Delta() == 0 && f.instance->GetActiveApplicatorsCount() == 0,
				"B13 missing unsupported or cyclic group trees reject before any sibling effect can execute");
		}
		a->processors.clear(); b->processors.clear();
	}
	{
		F f(world); auto* group = F::Group(F::Condition());
		group->processors.push_back(F::Condition(NDb::BASEUNITEVENTFLAGS_APPLICATORSTEP));
		NDb::Ptr<NDb::EventProcessorGroup> proc = group; NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(proc);
		auto app = f.Apply(check, db); if (app)
		{
			f.Event(); check(f.Delta() == 2, "B13 group mask union admits its ordinary event child");
			app->Step(0); check(f.Delta() == 4, "B13 mixed group also receives its original synthetic step event");
		}
	}
	{
		F f(world); auto* group = F::Group(F::Condition()); group->processors.push_back(F::Condition());
		NDb::Ptr<NDb::EventProcessorGroup> proc = group; NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(proc);
		auto app = f.Apply(check, db); if (app)
		{
			CObj<EventDispatchB7Listener> observer = new EventDispatchB7Listener; int applied = 0;
			observer->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent* event) {
				if (event->GetType() == NDb::BASEUNITEVENT_APPLICATORAPPLIED) { ++applied; app->Stop(); }
				return 0u;
			};
			f.receiver->AddEventListener(observer.GetPtr()); f.Event();
			check(f.Delta() == 1 && applied == 1 && !app->IsAlive() && f.instance->GetActiveApplicatorsCount() == 0,
				"B13 callback retirement stops remaining group children and its group spell without undoing the delivered child");
			f.receiver->RemoveEventListener(observer.GetPtr()); observer->action = nullptr;
		}
	}
	std::printf("B13 trigger groups checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Cancellation must stop actual damage delivery and consume only an eligible trigger action. */
static void TriggerCancellationB13(Checks& check, PFWorld* world)
{
	const int before = check.count, failed = check.failed;
	using F = TriggerCompositionB13;
	for (bool allied : {false, true}) for (bool base : {false, true}) for (bool matchingType : {false, true})
	for (int sides = 0; sides < 4; ++sides)
	{
		F f(world); auto attacker = Unit(world, allied ? NDb::FACTION_FREEZE : NDb::FACTION_BURN, 40);
		auto* cancel = F::Cancel(); NDb::Ptr<NDb::EventProcessorCancelDispatch> proc = cancel;
		const int type = (base == matchingType) ? NDb::TRIGGERABILITIESABILITIESFLAGS_BASEATTACK : NDb::TRIGGERABILITIESABILITIESFLAGS_SPECIAL;
		cancel->filter.flags = static_cast<NDb::ETriggerAbilitiesAbilitiesFlags>(type
			| ((sides & 1) ? NDb::TRIGGERABILITIESABILITIESFLAGS_FROMALLY : 0)
			| ((sides & 2) ? NDb::TRIGGERABILITIESABILITIESFLAGS_FROMENEMY : 0));
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(proc, "1"); auto app = f.Apply(check, db); if (!app) continue;
		DispatchAdmissionFixtureB13 incoming(attacker.GetPtr(), f.receiver.GetPtr(), base, false);
		check(incoming.dispatch != nullptr, "B13 cancellation fixture creates an actual incoming damage dispatch"); if (!incoming.dispatch) continue;
		const bool accepts = matchingType && (sides & (allied ? 1 : 2)); incoming.dispatch->Start();
		check(f.receiver->GetHealth() == (accepts ? 800 : 775) && incoming.parent->misses == int(accepts) && f.Delta() == int(accepts),
			"B13 faction and ability-type filters decide actual cancellation, parent miss and response spell before damage");
		DispatchAdmissionFixtureB13 next(attacker.GetPtr(), f.receiver.GetPtr(), base, false);
		if (next.dispatch) next.dispatch->Start();
		check(next.dispatch && f.receiver->GetHealth() == (accepts ? 775 : 750) && f.Delta() == int(accepts),
			"B13 one successful cancellation spends one action and lets the next real payload through");
	}
	for (const char* expression : {"", "true", "false", "sLife == 400 && rLife == 800", "sLife == 1000", "cWrapperBonus == 2", "cMissing", "1/0", "compiled-only"})
	{
		F f(world); auto attacker = Unit(world, NDb::FACTION_BURN, 40);
		auto* cancel = F::Cancel(); NDb::Ptr<NDb::EventProcessorCancelDispatch> proc = cancel;
		cancel->filter.abilityCondition.sString = expression;
		if (strcmp(expression, "compiled-only") == 0) { cancel->filter.abilityCondition.sString = ""; cancel->filter.abilityCondition.compiledString = "unsupported"; }
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(proc); auto app = f.Apply(check, db); if (!app) continue;
		DispatchAdmissionFixtureB13 incoming(attacker.GetPtr(), f.receiver.GetPtr(), true, false);
		if (!incoming.dispatch) { check(false, "B13 condition fixture creates incoming dispatch"); continue; }
		incoming.dispatch->Start(); const bool accepts = !*expression || strcmp(expression, "true") == 0 || strcmp(expression, "sLife == 400 && rLife == 800") == 0;
		check(f.receiver->GetHealth() == (accepts ? 800 : 775) && f.Delta() == int(accepts),
			"B13 cancellation formula uses trigger unit endpoints but never substitutes trigger constants for missing incoming upgrader context");
	}
	for (bool upgraded : {false, true})
	{
		F f(world); auto attacker = Unit(world, NDb::FACTION_BURN, 40);
		auto* upgradeDb = new NDb::Ability; NDb::Ptr<NDb::Ability> owned = upgradeDb;
		auto* constants = new NDb::UnitConstantsContainer; auto* value = new NDb::UnitConstant;
		value->name = "WrapperBonus"; value->var.sString = "7"; constants->vars.push_back(value); upgradeDb->constants = constants;
		CObj<PFAbilityData> upgrade = new PFAbilityData(attacker.GetPtr(), owned, NDb::ABILITYTYPEID_SPECIAL);
		auto* cancel = F::Cancel(); NDb::Ptr<NDb::EventProcessorCancelDispatch> proc = cancel;
		cancel->filter.flags = static_cast<NDb::ETriggerAbilitiesAbilitiesFlags>(NDb::TRIGGERABILITIESABILITIESFLAGS_FROMENEMY | NDb::TRIGGERABILITIESABILITIESFLAGS_SPECIAL);
		cancel->filter.abilityCondition.sString = "cWrapperBonus == 7";
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(proc); auto app = f.Apply(check, db); if (!app) continue;
		DispatchAdmissionFixtureB13 incoming(attacker.GetPtr(), f.receiver.GetPtr(), true, false, false, upgraded ? upgrade.GetPtr() : nullptr);
		if (!incoming.dispatch) { check(false, "B13 upgrade fixture creates incoming dispatch"); continue; }
		incoming.dispatch->Start();
		check(f.receiver->GetHealth() == (upgraded ? 800 : 775) && f.Delta() == int(upgraded),
			"B13 actual dispatch upgrader owns both filter ability type and formula constants");
	}
	for (bool base : {false, true}) for (bool single : {false, true})
	{
		F f(world); auto attacker = Unit(world, NDb::FACTION_BURN, 40);
		auto* cancel = F::Cancel(); cancel->filter.onlySingleTarget = true;
		NDb::Ptr<NDb::EventProcessorCancelDispatch> proc = cancel;
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(proc); auto app = f.Apply(check, db); if (!app) continue;
		DispatchAdmissionFixtureB13 incoming(attacker.GetPtr(), f.receiver.GetPtr(), base, false);
		auto* ability = const_cast<NDb::BaseAttack*>(incoming.db.GetPtr()); ability->type = NDb::ABILITYTYPE_ACTIVE;
		ability->targetType = single ? NDb::SPELLTARGET_ALLENEMIES : static_cast<NDb::ESpellTarget>(0);
		if (incoming.dispatch) incoming.dispatch->Start();
		check(incoming.dispatch && f.receiver->GetHealth() == (base || single ? 800 : 775),
			"B13 single-target filter retains the original base-attack exemption and actual ability target shape");
	}
	for (bool match : {false, true}) for (bool fallback : {false, true})
	{
		F f(world); auto attacker = Unit(world, NDb::FACTION_BURN, 40);
		auto* cancel = F::Cancel(); NDb::Ptr<NDb::EventProcessorCancelDispatch> proc = cancel;
		cancel->filter.flags = static_cast<NDb::ETriggerAbilitiesAbilitiesFlags>(NDb::TRIGGERABILITIESABILITIESFLAGS_FROMENEMY
			| NDb::TRIGGERABILITIESABILITIESFLAGS_FROMLIST | (fallback ? NDb::TRIGGERABILITIESABILITIESFLAGS_BASEATTACK : 0));
		cancel->filter.abilitiesList.push_back(match ? new NDb::Ability : new TriggerAbilityDbB13("/Mock/OtherCancellationAbility.xdb"));
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(proc); auto app = f.Apply(check, db); if (!app) continue;
		DispatchAdmissionFixtureB13 incoming(attacker.GetPtr(), f.receiver.GetPtr(), true, false);
		if (incoming.dispatch) incoming.dispatch->Start();
		check(incoming.dispatch && f.receiver->GetHealth() == (match || fallback ? 800 : 775),
			"B13 explicit list compares DB identities then falls through to the original type mask on a miss");
	}
	for (bool classTalent : {false, true}) for (bool listed : {false, true})
	{
		F f(world); auto attacker = Unit(world, NDb::FACTION_BURN, 40);
		auto* talent = new NDb::Talent; NDb::Ptr<NDb::Talent> talentDb = talent;
		talent->rarity = classTalent ? NDb::TALENTRARITY_CLASS : NDb::TALENTRARITY_ORDINARY;
		talent->manaCost.sString = "0"; talent->naftaCost = 0;
		CObj<PFTalent> upgrader = new PFTalent(CPtr<PFBaseMaleHero>(f.sender.GetPtr()), talentDb, 1, 0, 0, false);
		auto* cancel = F::Cancel(); NDb::Ptr<NDb::EventProcessorCancelDispatch> proc = cancel;
		cancel->filter.flags = static_cast<NDb::ETriggerAbilitiesAbilitiesFlags>(NDb::TRIGGERABILITIESABILITIESFLAGS_FROMENEMY
			| NDb::TRIGGERABILITIESABILITIESFLAGS_ONLYCLASSTALENT | (listed ? NDb::TRIGGERABILITIESABILITIESFLAGS_FROMLIST : 0));
		cancel->filter.abilitiesList.push_back(talentDb.GetPtr());
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(proc); auto app = f.Apply(check, db); if (!app) continue;
		DispatchAdmissionFixtureB13 incoming(attacker.GetPtr(), f.receiver.GetPtr(), true, false, false, upgrader.GetPtr());
		if (incoming.dispatch) incoming.dispatch->Start();
		check(incoming.dispatch && f.receiver->GetHealth() == (classTalent || listed ? 800 : 775),
			"B13 actual talent rarity gates class-only cancellation after the explicit-list override");
	}
	for (bool except : {false, true}) for (bool same : {false, true})
	{
		F f(world); auto attacker = Unit(world, NDb::FACTION_BURN, 40);
		auto* cancel = F::Cancel(); cancel->filter.exceptThis = except;
		NDb::Ptr<NDb::EventProcessorCancelDispatch> proc = cancel;
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(proc); auto app = f.Apply(check, db); if (!app) continue;
		DispatchAdmissionFixtureB13 incoming(attacker.GetPtr(), f.receiver.GetPtr(), true, false, false, same ? f.ability.GetPtr() : nullptr);
		if (incoming.dispatch) incoming.dispatch->Start();
		check(incoming.dispatch && f.receiver->GetHealth() == (except && same ? 775 : 800),
			"B13 exceptThis compares the selected incoming ability object, including an actual matching upgrader");
	}
	for (bool sourceOwner : {false, true}) for (bool requireMatch : {false, true})
	{
		F f(world); CObj<PFBaseUnit> attacker = sourceOwner ? CObj<PFBaseUnit>(f.sender.GetPtr()) : Unit(world, NDb::FACTION_BURN, 40);
		const float strength = attacker->GetStatValue(NDb::STAT_STRENGTH);
		auto* cancel = F::Cancel(); NDb::Ptr<NDb::EventProcessorCancelDispatch> proc = cancel;
		cancel->checkSender = requireMatch ? NDb::TRIGGEREVENTCHECKSENDER_MATCHTARGET : NDb::TRIGGEREVENTCHECKSENDER_NONE;
		cancel->senderMatchTarget = NDb::APPLICATORAPPLYTARGET_ABILITYOWNER;
		cancel->applyTarget = NDb::APPLICATORAPPLYTARGET_APPLICATORSELECTEDTARGET;
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(proc); auto app = f.Apply(check, db); if (!app) continue;
		DispatchAdmissionFixtureB13 incoming(attacker.GetPtr(), f.receiver.GetPtr(), true, false);
		if (incoming.dispatch) incoming.dispatch->Start(); const bool accepts = !requireMatch || sourceOwner;
		check(incoming.dispatch && f.receiver->GetHealth() == (accepts ? 800 : 775)
			&& attacker->GetStatValue(NDb::STAT_STRENGTH) == strength + int(accepts) && f.Delta() == 0,
			"B13 sender matching and selected-target response route to the actual dispatch sender");
	}
	{
		F f(world); auto attacker = Unit(world, NDb::FACTION_BURN, 40);
		f.instance = new PFAbilityInstance(f.ability, Target(CVec2(3,4)), true);
		auto* cancel = F::Cancel(); NDb::Ptr<NDb::EventProcessorCancelDispatch> proc = cancel;
		cancel->checkSender = NDb::TRIGGEREVENTCHECKSENDER_MATCHTARGET; cancel->senderMatchTarget = NDb::APPLICATORAPPLYTARGET_ABILITYTARGET;
		auto* raw = F::Descriptor(proc); raw->applyTarget = NDb::APPLICATORAPPLYTARGET_ABILITYOWNER;
		NDb::Ptr<NDb::TriggerApplicator> db = raw; auto app = f.Apply(check, db); if (app)
		{
			DispatchAdmissionFixtureB13 incoming(attacker.GetPtr(), f.sender.GetPtr(), true, false);
			if (incoming.dispatch) incoming.dispatch->Start();
			check(incoming.dispatch && f.sender->GetHealth() == 375 && incoming.parent->misses == 0,
				"B13 position-valued sender matching rejects without calling the unit-only target accessor");
		}
	}
	{
		F f(world); auto attacker = Unit(world, NDb::FACTION_BURN, 40);
		auto* group = F::Group(F::Condition(NDb::BASEUNITEVENTFLAGS_DISPATCHAPPLIED));
		group->processors.push_back(F::Cancel());
		NDb::Ptr<NDb::EventProcessorGroup> proc = group;
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(proc, "1"); auto app = f.Apply(check, db); if (app)
		{
			DispatchAdmissionFixtureB13 incoming(attacker.GetPtr(), f.receiver.GetPtr(), true, false);
			if (incoming.dispatch) incoming.dispatch->Start();
			check(incoming.dispatch && f.receiver->GetHealth() == 800 && incoming.parent->misses == 1 && f.Delta() == 3,
				"B13 real arrival runs grouped condition and cancellation children then exactly one group response");
			DispatchAdmissionFixtureB13 next(attacker.GetPtr(), f.receiver.GetPtr(), true, false);
			if (next.dispatch) next.dispatch->Start();
			check(next.dispatch && f.receiver->GetHealth() == 775 && f.Delta() == 3,
				"B13 mixed cancellation group spends one root action for the entire original processor tree");
		}
	}
	{
		F f(world); NDb::Ptr<NDb::EventProcessorCancelDispatch> proc = F::Cancel();
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(proc, "1"); auto app = f.Apply(check, db); if (app)
		{
			const PFBaseUnitEvent missing(NDb::BASEUNITEVENT_DISPATCHAPPLIED);
			const PFBaseUnitDispatchEvent empty(NDb::BASEUNITEVENT_DISPATCHAPPLIED, nullptr);
			f.receiver->EventHappened(missing); f.receiver->EventHappened(empty);
			check(f.Delta() == 0 && !empty.IsDispatchCancelled() && !app->Step(0),
				"B13 missing or wrong arrival payload cannot cancel a dispatch or consume a trigger action");
			auto attacker = Unit(world, NDb::FACTION_BURN, 40);
			DispatchAdmissionFixtureB13 incoming(attacker.GetPtr(), f.receiver.GetPtr(), true, false);
			if (incoming.dispatch) incoming.dispatch->Start();
			check(incoming.dispatch && f.receiver->GetHealth() == 800 && f.Delta() == 1,
				"B13 malformed event leaves the original subscription and valid cancellation action available");
		}
	}
	std::printf("B13 trigger cancellation checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Decode only the selected cancellation branch through the original XML reader. */
struct TriggerCancellationFieldsB13
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
			auto* processor = new NDb::EventProcessorCancelDispatch;
			saver.Add("EventProcessorCancelDispatch", processor); db->processor = processor; saver.FinishChunk();
		}
		return 0;
	}
};

/** The shipped G455 branch blocks hostile spells, not base attacks, while receiver life exceeds one. */
static void TriggerCancellationAuthoredB13(Checks& check, PFWorld* world, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	{
		CObj<Stream> xml = files->OpenFile("Items/Talents/Level6/Status_G455Dragon.APLR.xdb", FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
		check(xml && xml->IsOk(), "B13 shipped G455 transformation XML is readable");
		if (xml && xml->IsOk())
		{
			CObj<IXmlSaver> saver = CreateXmlSaver(xml, true);
			const bool root = saver->StartChunk("StatusApplicator", 0) && saver->StartChunk("applicators", 0);
			NDb::Ptr<NDb::TriggerApplicator> db;
			if (root) for (int i = 1, count = saver->CountChunks(); i <= count && i <= 128; ++i)
			{
				if (!saver->StartChunk("Item", i)) continue;
				if (saver->GetIdAttrValue() == "4024E18C-B141-496D-9334-145779BD8DE1")
				{
					auto* raw = new NDb::TriggerApplicator; db = raw;
					TriggerCancellationFieldsB13 fields{raw}; saver->Add("TriggerApplicator", &fields);
				}
				saver->FinishChunk();
			}
			check(db != nullptr, "B13 original XML reader finds the exact shipped cancellation trigger identity");
			if (db)
			{
				const auto* processor = dynamic_cast<const NDb::EventProcessorCancelDispatch*>(db->processor.GetPtr());
				check(db->actionsCount.sString == "0" && db->enabled.sString == "rLife > 1"
					&& db->lifeTime.sString == "-1.0" && !db->useAbilityCooldown && !db->useAbilityOwnerMana
					&& processor && processor->filter.flags == 1342178558 && processor->filter.abilitiesList.empty()
					&& processor->filter.abilityCondition.sString == "true" && !processor->spell,
					"B13 actual G455 lifetime enabled predicate unlimited actions and hostile non-base filter are preserved");
				for (bool base : {false, true}) for (bool allied : {false, true})
				{
					TriggerCompositionB13 f(world); auto app = f.Apply(check, db); if (!app) continue;
					auto attacker = Unit(world, allied ? NDb::FACTION_FREEZE : NDb::FACTION_BURN, 40);
					for (int i = 1; i <= 2; ++i)
					{
						DispatchAdmissionFixtureB13 incoming(attacker.GetPtr(), f.receiver.GetPtr(), base, false);
						if (incoming.dispatch) incoming.dispatch->Start(); const bool cancelled = !base && !allied;
						check(incoming.dispatch && f.receiver->GetHealth() == (cancelled ? 800 : 800 - 25 * i)
							&& incoming.parent->misses == int(cancelled) && app->IsAlive(),
							"B13 shipped trigger repeatedly cancels hostile non-base payloads and admits allies or base attacks");
					}
				}
				TriggerCompositionB13 f(world); auto app = f.Apply(check, db);
				if (app)
				{
					auto attacker = Unit(world, NDb::FACTION_BURN, 40);
					f.receiver->ChangeHealth(1 - f.receiver->GetHealth()); app->Step(0);
					DispatchAdmissionFixtureB13 low(attacker.GetPtr(), f.receiver.GetPtr(), false, false);
					check(low.dispatch && !app->IsEnabled() && f.receiver->OnDispatchApply(*low.dispatch),
						"B13 authored life threshold removes cancellation without delivering lethal fixture damage");
					f.receiver->ChangeHealth(1); app->Step(0);
					DispatchAdmissionFixtureB13 recovered(attacker.GetPtr(), f.receiver.GetPtr(), false, false);
					if (recovered.dispatch) recovered.dispatch->Start();
					check(recovered.dispatch && app->IsEnabled() && f.receiver->GetHealth() == 2 && recovered.parent->misses == 1,
						"B13 original buff reenable restores authored cancellation at two life before lethal damage admission");
				}
			}
		}
	}
	RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 authored G455 cancellation checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
