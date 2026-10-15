/** Distinct resource instances with stable identities exercise the original DBID filter. */
struct ConsumableTriggerDbB13 : NDb::Consumable
{
	explicit ConsumableTriggerDbB13(const char* id = "/Mock/TriggerConsumable.xdb")
	{
		SetDbId(NDb::DBID(id)); ProbeResourceCache::MarkLoaded(this);
		maxQuantity = 5; manaCost.sString = "0";
	}
};

/** Real hero inventory and world-owned ability context, with immediate observable spell responses. */
struct ConsumableTriggerFixtureB13 : TargetTriggerFixtureB13
{
	explicit ConsumableTriggerFixtureB13(PFWorld* world) : TargetTriggerFixtureB13(world) {}
	static NDb::EventProcessorOnConsumableObtained* Processor(int mask, const NDb::Consumable* item = nullptr)
	{
		auto* db = new NDb::EventProcessorOnConsumableObtained;
		db->origin = static_cast<NDb::EConsumableOriginFlags>(mask); db->consumable = item; db->spell = Spell(); return db;
	}
	void Obtained(const NDb::Consumable* item, int origin)
	{
		const PFHeroEventConsumableObtained event(item, static_cast<NDb::EConsumableOrigin>(origin));
		receiver->EventHappened(event);
	}
};

/** Reject malformed payloads before shifting; preserve every original origin bit and DB identity policy. */
static void ConsumableTriggerB13(Checks& check, PFWorld* world)
{
	const int before = check.count, failed = check.failed;
	using F = ConsumableTriggerFixtureB13;
	NDb::Ptr<NDb::Consumable> item = new ConsumableTriggerDbB13;
	NDb::Ptr<NDb::Consumable> same = new ConsumableTriggerDbB13;
	NDb::Ptr<NDb::Consumable> other = new ConsumableTriggerDbB13("/Mock/OtherTriggerConsumable.xdb");
	for (int mask : {0, 1, 2, 4, 6, 8, 16, 32, 63, 64, -1}) for (bool filtered : {false, true})
	{
		F f(world); NDb::Ptr<NDb::EventProcessorBase> processor = F::Processor(mask, filtered ? item.GetPtr() : nullptr);
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(processor); auto app = f.Apply(check, db); if (!app) continue;
		float expected = 0;
		for (int origin = 0; origin < NDb::KnownEnum<NDb::EConsumableOrigin>::SizeOf(); ++origin)
		for (const auto* acquired : {item.GetPtr(), same.GetPtr(), other.GetPtr()})
		{
			f.Obtained(acquired, origin);
			if ((static_cast<unsigned>(mask) & (1u << origin)) && (!filtered || acquired != other.GetPtr())) ++expected;
			check(f.Delta() == expected, "B13 consumable trigger uses origin membership and DBID, not descriptor pointer identity");
		}
		for (int origin : {-1, 6, 31, 32, 63, 64}) f.Obtained(item, origin);
		f.Obtained(nullptr, NDb::CONSUMABLEORIGIN_SHOP); f.Event(NDb::BASEUNITEVENT_CONSUMABLEOBTAINED); f.Event();
		check(f.Delta() == expected, "B13 malformed consumable payload, invalid origin and unrelated event never invoke a response");
		app->Stop(); f.Obtained(item, NDb::CONSUMABLEORIGIN_SHOP);
		check(f.Delta() == expected && f.instance->GetActiveApplicatorsCount() == 0,
			"B13 stopped acquisition trigger releases subscription and active ownership");
	}
	for (int origin = 0; origin < NDb::KnownEnum<NDb::EConsumableOrigin>::SizeOf(); ++origin)
	{
		F f(world); NDb::Ptr<NDb::EventProcessorBase> processor = F::Processor(63);
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(processor, "2"); auto app = f.Apply(check, db); if (!app) continue;
		const auto source = static_cast<NDb::EConsumableOrigin>(origin); const bool notified = origin != 0;
		check(f.receiver->TakeConsumable(item, 2, source, 0) && f.receiver->GetConsumable(0)->GetQuantity() == 2
			&& f.Delta() == int(notified), "B13 successful real inventory creation emits one action per acquisition, not per item");
		check(!f.receiver->TakeConsumable(item, 4, source, 0) && !f.receiver->TakeConsumable(other, 1, source, 0)
			&& !f.receiver->TakeConsumable(item, 0, source, 0) && !f.receiver->TakeConsumable(item, 1, source, 99)
			&& f.Delta() == int(notified), "B13 full, incompatible, empty and invalid-slot inventory transactions emit no acquisition event");
		check(f.receiver->TakeConsumable(item, 1, source, 0) && f.receiver->GetConsumable(0)->GetQuantity() == 3
			&& f.Delta() == (notified ? 2 : 0), "B13 successful stack addition emits once while original default-origin additions stay silent");
		f.receiver->TakeConsumable(item, 1, source, 0);
		check(f.Delta() == (notified ? 2 : 0) && app->Step(0) == notified,
			"B13 acquisition action budget is spent only by successful filtered events");
	}
	for (bool withSpell : {false, true}) for (bool pay : {false, true}) for (bool ownerTarget : {false, true})
	{
		F f(world); f.Payment(25); auto* raw = F::Processor(2);
		raw->applyTarget = ownerTarget ? NDb::APPLICATORAPPLYTARGET_ABILITYOWNER : NDb::APPLICATORAPPLYTARGET_APPLICATORTARGET;
		if (!withSpell) raw->spell = nullptr;
		NDb::Ptr<NDb::EventProcessorBase> processor = raw; auto* trigger = F::Descriptor(processor, "1");
		trigger->useAbilityOwnerMana = pay; trigger->useAbilityCooldown = pay;
		NDb::Ptr<NDb::TriggerApplicator> db = trigger; auto app = f.Apply(check, db); if (!app) continue;
		f.Obtained(other, NDb::CONSUMABLEORIGIN_SCRIPT);
		check(f.sender->GetMana() == 25 && !app->Step(0), "B13 rejected acquisition does not pay or consume its action");
		f.Obtained(item, NDb::CONSUMABLEORIGIN_SHOP); f.Obtained(item, NDb::CONSUMABLEORIGIN_SHOP);
		check(f.Delta() == int(withSpell && !ownerTarget)
			&& f.sender->GetStatValue(NDb::STAT_STRENGTH) - f.senderStrength == int(withSpell && ownerTarget)
			&& f.sender->GetMana() == (pay ? 15 : 25) && app->Step(0),
			"B13 accepted acquisition preserves application target and one payment, including a spell-less successful predicate");
	}
	for (bool cancel : {false, true})
	{
		F f(world); f.Payment(25); auto* raw = F::Processor(63);
		auto* response = new NDb::Spell; auto* dispatch = new NDb::DispatchImmediate;
		dispatch->instantApply = true; response->dispatch = dispatch;
		auto* grant = new NDb::GiveConsumable; grant->enabled.sString = "true"; grant->amount.sString = "1"; grant->consumable = item;
		response->applicators.push_back(grant); raw->spell = response;
		NDb::Ptr<NDb::EventProcessorBase> processor = raw; auto* trigger = F::Descriptor(processor, "2"); trigger->useAbilityOwnerMana = true;
		NDb::Ptr<NDb::TriggerApplicator> db = trigger; auto app = f.Apply(check, db); if (!app) continue;
		int nested = 0; CObj<EventDispatchB7Listener> observer = new EventDispatchB7Listener;
		observer->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent* event) {
			const auto* acquired = dynamic_cast<const PFHeroEventConsumableObtained*>(event);
			if (acquired && acquired->GetOrigin() == NDb::CONSUMABLEORIGIN_APPLICATOR) { ++nested; if (cancel) app->Stop(); }
			return 0u;
		};
		f.receiver->AddEventListener(observer.GetPtr());
		check(f.receiver->TakeConsumable(item, 1, NDb::CONSUMABLEORIGIN_SHOP, 0), "B13 nested response starts with an actual successful inventory acquisition");
		check(nested == 1 && f.receiver->GetConsumable(0)->GetQuantity() == 2 && f.sender->GetMana() == 15,
			"B13 synchronous GiveConsumable callback cannot recursively spend the in-flight acquisition action");
		f.receiver->TakeConsumable(item, 1, NDb::CONSUMABLEORIGIN_SHOP, 0);
		check(nested == (cancel ? 1 : 2) && f.receiver->GetConsumable(0)->GetQuantity() == (cancel ? 3 : 4)
			&& f.sender->GetMana() == (cancel ? 15 : 5), "B13 callback stop cancels later responses while a live trigger can accept the next acquisition");
		f.receiver->RemoveEventListener(observer.GetPtr()); observer->action = nullptr;
	}
	std::printf("B13 consumable triggers checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Original probability dispatch truncates to an integer and consumes one Next(100), even at zero/one hundred. */
static void ConsumableProbabilityB13(Checks& check, PFWorld* world)
{
	const int before = check.count, failed = check.failed;
	struct Case { const char* text; float value; bool valid = true; };
	for (bool enabled : {false, true})
	for (const auto& item : {Case{"", 0}, Case{"-5", -5}, Case{"-0.9", -.9f}, Case{"0", 0}, Case{"0.9", .9f},
		Case{"1.9", 1.9f}, Case{"30.9", 30.9f}, Case{"99.9", 99.9f}, Case{"100", 100}, Case{"150", 150},
		Case{"cWrapperBonus * 50", 100}, Case{"-2147483648", -2147483648.f}, Case{"2147483520", 2147483520.f},
		Case{"2147483648", 0, false}, Case{"-2147483904", 0, false}, Case{"1e39", 0, false},
		Case{"random(0,10) + 2147483904", 0, false}, Case{"cMissing", 0, false}, Case{"1/0", 0, false}, Case{"compiled-only", 0, false}})
	{
		ConsumableTriggerFixtureB13 f(world); auto* raw = new NDb::SpellProbabilityApplicator;
		raw->enabled.sString = enabled ? "true" : "false"; raw->probability.sString = item.text;
		if (strcmp(item.text, "compiled-only") == 0) { raw->probability.sString = ""; raw->probability.compiledString = "unsupported"; }
		raw->spell = f.Spell(); auto* failure = f.Spell(); raw->spellIfFailed = failure;
		auto* effect = const_cast<NDb::PermanentStatModApplicator*>(static_cast<const NDb::PermanentStatModApplicator*>(failure->applicators[0].GetPtr()));
		effect->modifier.addValue.sString = "-1";
		NDb::Ptr<NDb::BaseApplicator> db = raw;
		NRandom::RandomGenerator expected(*world->GetRndGen()); float delta = 0;
		if (enabled && item.valid) delta = expected.Next(100) < static_cast<int>(item.value) ? 1 : -1;
		auto app = f.Apply(check, db);
		check(f.Delta() == delta, "B13 probability dispatch uses checked original constants, integer truncation and distinct success/failure spells");
		check(world->GetRndGen()->Next(0, 1000) == expected.Next(0, 1000),
			"B13 original integer roll advances once; disabled, malformed and out-of-int-range probabilities preserve RNG and cast neither branch");
	}
	for (int boundary : {0, 30, 99}) for (bool above : {false, true})
	{
		ConsumableTriggerFixtureB13 f(world); unsigned seed = 0;
		for (; seed < 10000; ++seed) { NRandom::RandomGenerator candidate(seed); if (candidate.Next(100) == boundary) break; }
		check(seed < 10000, "B13 deterministic seed reaches the exact original integer probability boundary");
		const NRandom::RandomGenerator saved(*world->GetRndGen()); world->GetRndGen()->SetSeed(seed);
		auto* raw = new NDb::SpellProbabilityApplicator; raw->enabled.sString = "true";
		char expression[32]; std::snprintf(expression, sizeof(expression), "%d.%d", boundary + int(above), above ? 0 : 9);
		raw->probability.sString = expression; raw->spell = f.Spell(); NDb::Ptr<NDb::BaseApplicator> db = raw;
		auto app = f.Apply(check, db);
		check(f.Delta() == int(above), "B13 fractional probability cannot admit an equal integer roll, while the next integer threshold does");
		*world->GetRndGen() = saved;
	}
	for (bool enabled : {false, true}) for (const char* probability : {"0", "100", "cWrapperBonus * 50"})
	{
		ConsumableTriggerFixtureB13 f(world); auto* raw = new NDb::Ability;
		raw->type = NDb::ABILITYTYPE_ACTIVE; raw->targetType = NDb::SPELLTARGET_ALL;
		raw->manaCost.sString = "0"; raw->constants = f.abilityDb->constants;
		auto* effect = new NDb::SpellProbabilityApplicator; effect->enabled.sString = enabled ? "true" : "false";
		effect->probability.sString = probability; effect->spell = f.Spell(); raw->applicators.push_back(effect);
		NDb::Ptr<NDb::Ability> db = raw;
		const auto external = f.sender->UseExternalAbility(db, Target(f.receiver.GetPtr()));
		check(external && external->GetWorld() == world && external->GetData()->GetWorld() == world,
			"B13 external abilities retain original registered world ownership for probability and deferred effects");
		check(f.Delta() == int(enabled && strcmp(probability, "0") != 0),
			"B13 real UseExternalAbility executes checked probability responses with its actual world context");
		if (external) external->Cancel();
	}
	std::printf("B13 consumable probability checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Execute the shipped Artist scroll triggers, probability branches and real bonus inventory events. */
static void ConsumableTriggerAuthoredB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		const auto trigger = NDb::Get<NDb::TriggerApplicator>(NDb::DBID("/Items/Talents/Class/Artist/trigger_scrollRoll.APLR.xdb"));
		const auto status = NDb::Get<NDb::StatusApplicator>(NDb::DBID("/Items/Talents/Class/Artist/status_A3_MasterColors.APLR.xdb"));
		check(status && status->applicators.size() == 5, "B13 original Artist parent status owns five separate scroll acquisition triggers");
		const auto* processor = trigger ? dynamic_cast<const NDb::EventProcessorOnConsumableObtained*>(trigger->processor.GetPtr()) : nullptr;
		check(processor && processor->GetDBID().GetId() == "34A7C75B-B850-4335-85B9-1577EE34F7A2"
			&& int(processor->origin) == 6 && processor->consumable && processor->spell,
			"B13 original Artist trigger retains the authored scroll identity and shop/minigame origin filter");
		NDb::Ptr<NDb::UnitConstantsContainer> constants;
		CObj<Stream> xml = files->OpenFile("Items/Talents/Class/Artist/Ability_A3.TALENT.xdb", FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
		check(xml && xml->IsOk(), "B13 original Artist A3 constants XML is readable");
		if (xml && xml->IsOk())
		{
			CObj<IXmlSaver> saver = CreateXmlSaver(xml, true);
			if (saver->StartChunk("Talent", 0)) constants = NearbyConstantsB13(*saver, "constants");
		}
		if (processor && constants && status && status->applicators.size() == 5)
		for (int branch = -1; branch < 5; ++branch)
		for (int origin = 0; origin < NDb::KnownEnum<NDb::EConsumableOrigin>::SizeOf(); ++origin)
		for (bool success : {false, true}) for (bool matching : {false, true})
		{
			B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
			ConsumableTriggerFixtureB13 f(isolated.world);
			const_cast<NDb::Ability*>(f.abilityDb.GetPtr())->constants = constants; f.ability->Reset();
			check(f.ability->GetConstant("A3_ScrollProc") && f.ability->GetConstant("A3_ScrollProc")->var.sString == "30",
				"B13 Artist probability uses the original thirty-percent constant");
			const char* variable = "ArtistA2_scrollGainProbability_cumulative";
			const float initial = success ? 70.f : -30.f; f.receiver->GetVariableVWM(variable)->SetBaseValue(initial);
			const auto* child = branch < 0 ? trigger.GetPtr() : dynamic_cast<const NDb::TriggerApplicator*>(status->applicators[branch].GetPtr());
			const auto* selected = child ? dynamic_cast<const NDb::EventProcessorOnConsumableObtained*>(child->processor.GetPtr()) : nullptr;
			check(selected && selected->consumable && int(selected->origin) == 6, "B13 each original Artist branch retains its own authored scroll filter");
			if (!selected || !selected->consumable) continue;
			auto app = f.Apply(check, branch < 0 ? static_cast<const NDb::BaseApplicator*>(trigger.GetPtr()) : status.GetPtr()); if (!app) continue;
			NDb::Ptr<NDb::Consumable> item = matching ? selected->consumable.GetPtr() : new ConsumableTriggerDbB13;
			int acquired = 0, bonus = 0; CObj<EventDispatchB7Listener> observer = new EventDispatchB7Listener;
			observer->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent* event) {
				if (const auto* inventory = dynamic_cast<const PFHeroEventConsumableObtained*>(event))
				{
					++acquired; if (inventory->GetOrigin() == NDb::CONSUMABLEORIGIN_APPLICATOR) ++bonus;
				}
				return 0u;
			};
			f.receiver->AddEventListener(observer.GetPtr());
			check(f.receiver->TakeConsumable(item, 1, static_cast<NDb::EConsumableOrigin>(origin), 0),
				"B13 actual Artist scroll acquisition enters the original hero inventory");
			for (int i = 0; i < 6; ++i) isolated.world->Step(.01f, .01f);
			const bool matches = matching && (origin == 1 || origin == 2);
			check(f.receiver->GetConsumable(0) && f.receiver->GetConsumable(0)->GetQuantity() == (matches && success ? 2 : 1)
				&& f.receiver->GetVariableVWM(variable)->GetValue() == (matches ? 0.f : initial),
				"B13 authored Artist success grants a real scroll and resets accumulation; failure adds the original thirty points");
			check(acquired == int(origin != 0) + int(matches && success) && bonus == int(origin == 4) + int(matches && success)
				&& app->IsAlive() && f.instance->GetActiveApplicatorsCount() == (branch < 0 ? 1 : 6),
				"B13 Artist applicator-origin bonus never retriggers probability and the original parent retains exactly five live children");
			f.receiver->RemoveEventListener(observer.GetPtr()); observer->action = nullptr;
		}
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 authored consumable triggers checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
