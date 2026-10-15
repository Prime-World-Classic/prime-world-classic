/** Checked quantities still enter the original fixed-slot inventory and acquisition event dispatcher. */
static void ConsumableGrantB13(Checks& check, PFWorld* world)
{
	const int before = check.count, failed = check.failed;
	struct Amount { const char* expression; int quantity; bool random = false; };
	for (const char* enabled : {"true", "false", "sLife < rLife", "sLife > rLife", "cMissing", "compiled-only"})
	for (const auto& amount : {Amount{"", 1}, Amount{"1", 1}, Amount{"2.9", 2}, Amount{"0", 0}, Amount{"-2", 0},
		Amount{"cWrapperBonus", 2}, Amount{"sLife/100", 4}, Amount{"rLife/200", 4}, Amount{"random(1,3)", 0, true},
		Amount{"cMissing", 0}, Amount{"1/0", 0}, Amount{"1e39", 0}, Amount{"2147483648", 0},
		Amount{"random(1,3) + 2147483904", 0}, Amount{"compiled-only", 0}})
	{
		ConsumableTriggerFixtureB13 f(world); NDb::Ptr<NDb::Consumable> item = new ConsumableTriggerDbB13;
		auto* raw = new NDb::GiveConsumable; raw->enabled.sString = enabled; raw->amount.sString = amount.expression; raw->consumable = item;
		if (strcmp(enabled, "compiled-only") == 0) { raw->enabled.sString = ""; raw->enabled.compiledString = "unsupported"; }
		if (strcmp(amount.expression, "compiled-only") == 0) { raw->amount.sString = ""; raw->amount.compiledString = "unsupported"; }
		NDb::Ptr<NDb::BaseApplicator> db = raw;
		int events = 0; CObj<EventDispatchB7Listener> observer = new EventDispatchB7Listener;
		observer->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent* event) {
			if (const auto* acquired = dynamic_cast<const PFHeroEventConsumableObtained*>(event))
			{
				++events; check(acquired->GetOrigin() == NDb::CONSUMABLEORIGIN_APPLICATOR && acquired->GetDBConsumable() == item.GetPtr(),
					"B13 grant event retains original applicator origin and actual inventory resource");
			}
			return 0u;
		};
		f.receiver->AddEventListener(observer.GetPtr());
		const bool admits = strcmp(enabled, "true") == 0 || strcmp(enabled, "sLife < rLife") == 0;
		NRandom::RandomGenerator expected(*world->GetRndGen());
		const int quantity = admits ? amount.random ? expected.Next(1, 3) : amount.quantity : 0;
		auto app = f.Apply(check, db); const auto* acquired = f.receiver->GetConsumable(0);
		check((acquired ? acquired->GetQuantity() : 0) == quantity && events == int(quantity > 0),
			"B13 disabled or malformed grants never invent a default item; accepted finite quantities use original integer truncation");
		check(world->GetRndGen()->Next(0, 1000) == expected.Next(0, 1000),
			"B13 unselected or invalid grant formulas preserve RNG; accepted quantities consume only their original random expression");
		check(f.instance->GetActiveApplicatorsCount() == 0, "B13 immediate grants balance ability ownership on success and every rejection");
		f.receiver->RemoveEventListener(observer.GetPtr()); observer->action = nullptr;
	}
	for (int maximum : {0, 1, 5}) for (int existing : {0, 1}) for (int amount : {1, 2, 5, 6})
	{
		ConsumableTriggerFixtureB13 f(world); auto* itemRaw = new ConsumableTriggerDbB13; itemRaw->maxQuantity = maximum;
		NDb::Ptr<NDb::Consumable> item = itemRaw;
		if (existing) check(f.receiver->TakeConsumable(item, 1, NDb::CONSUMABLEORIGIN_DEFAULT, 0), "B13 grant fixture owns an existing real inventory stack");
		auto* raw = new NDb::GiveConsumable; raw->enabled.sString = "true"; raw->consumable = item;
		char expression[16]; std::snprintf(expression, sizeof(expression), "%d", amount); raw->amount.sString = expression;
		NDb::Ptr<NDb::BaseApplicator> db = raw; auto app = f.Apply(check, db);
		const bool accepted = maximum > 0 ? amount <= maximum : amount == 1;
		const int first = existing && (maximum <= 0 || amount > maximum - existing) ? existing : existing + (accepted ? amount : 0);
		const auto* slot0 = f.receiver->GetConsumable(0); const auto* slot1 = f.receiver->GetConsumable(1);
		check((slot0 ? slot0->GetQuantity() : 0) == first
			&& (slot1 ? slot1->GetQuantity() : 0) == (accepted && first == existing ? amount : 0),
			"B13 original inventory prefers a compatible stack, otherwise an empty slot, without splitting oversized grants");
	}
	{
		ConsumableTriggerFixtureB13 f(world); NDb::Ptr<NDb::Consumable> item = new ConsumableTriggerDbB13;
		const int slots = f.receiver->GetSlotCount();
		check(slots > 0 && slots <= 128, "B13 bounded fill uses the real hero inventory capacity");
		if (slots <= 0 || slots > 128) return;
		for (int i = 0; i < slots; ++i) check(f.receiver->TakeConsumable(item, 5, NDb::CONSUMABLEORIGIN_DEFAULT, i),
			"B13 every actual inventory slot is full before testing rejected reward delivery");
		auto* raw = new NDb::GiveConsumable; raw->enabled.sString = "true"; raw->consumable = item; raw->amount.sString = "random(1,3)";
		NDb::Ptr<NDb::BaseApplicator> db = raw;
		NRandom::RandomGenerator expected(*world->GetRndGen()); expected.Next(1, 3);
		auto app = f.Apply(check, db);
		for (int i = 0; i < slots; ++i) check(f.receiver->GetConsumable(i)->GetQuantity() == 5, "B13 a full inventory cannot receive or partially overwrite a grant");
		check(world->GetRndGen()->Next(0, 1000) == expected.Next(0, 1000),
			"B13 original amount evaluation precedes inventory-capacity rejection");
	}
	std::printf("B13 consumable grants checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Exercise both original switch delivery modes with real inventory effects and deterministic selection. */
static void ConsumableSwitchB13(Checks& check, PFWorld* world)
{
	const int before = check.count, failed = check.failed;
	struct Index { const char* text; int value; bool random = false; };
	for (bool direct : {false, true}) for (bool enabled : {false, true})
	for (const auto& index : {Index{"", 0}, Index{"0", 0}, Index{"1", 1}, Index{"1.9", 1}, Index{"-0.9", 0},
		Index{"-1", -1}, Index{"2", 2}, Index{"cWrapperBonus-1", 1}, Index{"random(0,1)", 0, true},
		Index{"cMissing", -1}, Index{"1/0", -1}, Index{"1e39", -1}, Index{"2147483648", -1},
		Index{"random(0,1)+2147483904", -1}, Index{"compiled-only", -1}})
	{
		ConsumableTriggerFixtureB13 f(world); NDb::Ptr<NDb::Consumable> item = new ConsumableTriggerDbB13;
		auto* raw = new NDb::SpellSwitchApplicator; raw->enabled.sString = enabled ? "true" : "false";
		raw->activeSpellIndex.sString = index.text; raw->dontUseDispatches = direct;
		if (strcmp(index.text, "compiled-only") == 0) { raw->activeSpellIndex.sString = ""; raw->activeSpellIndex.compiledString = "unsupported"; }
		for (const char* amount : {"1", "2"})
		{
			auto* spell = new NDb::Spell; auto* dispatch = new NDb::DispatchImmediate; dispatch->instantApply = true; spell->dispatch = dispatch;
			auto* grant = new NDb::GiveConsumable; grant->enabled.sString = "true"; grant->consumable = item; grant->amount.sString = amount;
			spell->applicators.push_back(grant); raw->spells.push_back(spell);
		}
		NDb::Ptr<NDb::BaseApplicator> db = raw;
		NRandom::RandomGenerator expected(*world->GetRndGen()); const int selected = enabled && index.random ? expected.Next(0, 1) : index.value;
		const int quantity = enabled && selected >= 0 && selected < 2 ? selected + 1 : 0;
		auto app = f.Apply(check, db); const auto* acquired = f.receiver->GetConsumable(0);
		check((acquired ? acquired->GetQuantity() : 0) == quantity,
			"B13 checked switch selection preserves integer truncation, list bounds, default zero and both original delivery modes");
		check(world->GetRndGen()->Next(0, 1000) == expected.Next(0, 1000)
			&& f.instance->GetActiveApplicatorsCount() == 0,
			"B13 malformed/unselected switch indices preserve RNG and ownership instead of silently casting spell zero");
	}
	{
		ConsumableTriggerFixtureB13 f(world); NDb::Ptr<NDb::Consumable> item = new ConsumableTriggerDbB13;
		auto* raw = new NDb::SpellSwitchApplicator; raw->enabled.sString = "true"; raw->activeSpellIndex.sString = "0"; raw->dontUseDispatches = true;
		auto* spell = new NDb::Spell;
		for (int i = 0; i < 2; ++i)
		{
			auto* grant = new NDb::GiveConsumable; grant->enabled.sString = "true"; grant->amount.sString = "1"; grant->consumable = item;
			spell->applicators.push_back(grant);
		}
		raw->spells.push_back(spell); NDb::Ptr<NDb::BaseApplicator> db = raw; auto app = f.Create(db);
		CObj<EventDispatchB7Listener> observer = new EventDispatchB7Listener; int events = 0;
		observer->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent* event) {
			if (event->GetType() == NDb::BASEUNITEVENT_CONSUMABLEOBTAINED) { ++events; app->Stop(); }
			return 0u;
		};
		f.receiver->AddEventListener(observer.GetPtr()); check(app && f.Activate(app), "B13 callback-cancelled switch activates through the actual factory");
		check(events == 1 && f.receiver->GetConsumable(0) && f.receiver->GetConsumable(0)->GetQuantity() == 1
			&& f.instance->GetActiveApplicatorsCount() == 0,
			"B13 direct switch stops producing siblings after an inventory callback retires its parent");
		f.receiver->RemoveEventListener(observer.GetPtr()); observer->action = nullptr;
	}
	std::printf("B13 consumable switches checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Original compensation leaf and complete shop-trigger response exercise authored conditional/seeded rewards. */
static void ConsumableGrantAuthoredB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		const char* compensationPath = "/Items/Talents/TalentKits/Redset9_6_DeathCompensation/Programm_Redset9_6_CompensationTP.APLR.xdb";
		const auto parent = NDb::Get<NDb::ProgramApplicator>(NDb::DBID(compensationPath));
		const auto grant = NDb::Get<NDb::GiveConsumable>(NDb::DBID(compensationPath, "21233DE8-B5D0-405C-8686-E1C3B12A6344"));
		check(parent && grant && grant->consumable && grant->enabled.sString == "sender.vRedset9_6Counter_Damage == 1"
			&& grant->applyTarget == NDb::APPLICATORAPPLYTARGET_ABILITYOWNER,
			"B13 retained original compensation program resolves its exact conditional teleport-scroll grant");
		if (grant)
		for (int counter : {-1, 0, 1, 2})
		{
			B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
			ConsumableTriggerFixtureB13 f(isolated.world); f.sender->GetVariableVWM("Redset9_6Counter_Damage")->SetBaseValue(counter);
			auto app = f.Apply(check, grant);
			check(bool(f.sender->GetConsumable(0)) == (counter == 1) && !f.receiver->GetConsumable(0),
				"B13 original compensation leaf grants only at the authored damage-counter value and only to the ability owner");
			if (f.sender->GetConsumable(0)) check(f.sender->GetConsumable(0)->GetDBDesc() == grant->consumable.GetPtr(),
				"B13 compensation reward is the authored teleport scroll, not a substituted inventory descriptor");
		}
		const auto talent = NDb::Get<NDb::Talent>(NDb::DBID("/Items/Talents/Level1/G520_StrOrInt.TALENT.xdb"));
		NDb::Ptr<NDb::TriggerApplicator> trigger;
		if (talent) for (const auto& item : talent->passiveApplicators)
			if (item && item->GetDBID().GetId() == "80FB021E-E532-4E67-ACEA-A29D3DB9833D") trigger = dynamic_cast<const NDb::TriggerApplicator*>(item.GetPtr());
		const auto* processor = trigger ? dynamic_cast<const NDb::EventProcessorOnConsumableObtained*>(trigger->processor.GetPtr()) : nullptr;
		const auto* choice = processor && processor->spell && processor->spell->applicators.size() == 1
			? dynamic_cast<const NDb::SpellSwitchApplicator*>(processor->spell->applicators[0].GetPtr()) : nullptr;
		check(choice && choice->spells.size() == 4 && choice->activeSpellIndex.sString == "random(0,3)"
			&& int(processor->origin) == 2 && !processor->consumable,
			"B13 original G520 acquisition trigger retains its shop-only random choice among four authored rewards");
		if (choice && choice->spells.size() == 4)
		for (unsigned seed = 0; seed < 8; ++seed) for (int origin = 0; origin < 6; ++origin)
		{
			B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
			ConsumableTriggerFixtureB13 f(isolated.world); auto app = f.Apply(check, trigger); if (!app) continue;
			NDb::Ptr<NDb::Consumable> item = new ConsumableTriggerDbB13;
			isolated.world->GetRndGen()->SetSeed(seed); NRandom::RandomGenerator expected(seed);
			const int index = origin == NDb::CONSUMABLEORIGIN_SHOP ? expected.Next(0, 3) : 0;
			const auto* reward = choice->spells[index] && choice->spells[index]->applicators.size() == 1
				? dynamic_cast<const NDb::GiveConsumable*>(choice->spells[index]->applicators[0].GetPtr()) : nullptr;
			check(reward && reward->consumable, "B13 selected G520 spell contains its original single consumable reward");
			check(f.sender->TakeConsumable(item, 1, static_cast<NDb::EConsumableOrigin>(origin), 0),
				"B13 G520 receives a real acquisition on the ability owner selected by the original trigger");
			for (int i = 0; i < 4; ++i) isolated.world->Step(.01f, .01f);
			const auto* bonus = f.sender->GetConsumable(1);
			check(origin == NDb::CONSUMABLEORIGIN_SHOP ? bonus && reward && bonus->GetDBDesc() == reward->consumable.GetPtr()
				&& bonus->GetQuantity() == 1 : !bonus, "B13 original G520 shop reward chooses exactly one authored item and ignores all other acquisition origins");
			check(isolated.world->GetRndGen()->Next(0, 1000) == expected.Next(0, 1000)
				&& !f.sender->GetConsumable(2) && f.instance->GetActiveApplicatorsCount() == 1,
				"B13 bonus acquisition cannot recurse into another G520 reward or advance an extra selection roll");
		}
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 authored consumable grants checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
