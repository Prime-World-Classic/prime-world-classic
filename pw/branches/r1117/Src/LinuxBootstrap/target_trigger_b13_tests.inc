/** Mock candidate input only; retain the original multiple-selector mode and dispatch/count machinery. */
struct TriggerTargetsDbB13 : NDb::MultipleTargetSelector
{
	std::vector<Target> targets;
	mutable int enumerations = 0;
	CPtr<PFBaseApplicator> stopOnEnumeration;
	PFTargetSelector* Create(PFWorld* world) const override;
};
struct TriggerTargetsB13 : PFMultipleTargetSelector
{
	OBJECT_METHODS(PFMultipleTargetSelector::typeId, TriggerTargetsB13);
public:
	TriggerTargetsB13() = default;
	TriggerTargetsB13(const TriggerTargetsDbB13& db, PFWorld* world) : PFMultipleTargetSelector(db, world) {}
	void ForAllTargets(ITargetAction& action, const RequestParams&) override
	{
		const auto* db = static_cast<const TriggerTargetsDbB13*>(GetDBBase().GetPtr());
		++db->enumerations;
		if (IsValid(db->stopOnEnumeration)) db->stopOnEnumeration->Stop();
		for (const auto& target : db->targets) action(target);
	}
};
PFTargetSelector* TriggerTargetsDbB13::Create(PFWorld* world) const { return new TriggerTargetsB13(*this, world); }

/** Build original OnTarget descriptors; permanent response effects expose real dispatches. */
struct TargetTriggerFixtureB13 : TriggerConditionFixtureB13
{
	explicit TargetTriggerFixtureB13(PFWorld* world) : TriggerConditionFixtureB13(world)
	{
		// A real world-owned ability supplies the selector's seeded random context.
		ability = new PFAbilityData(sender.GetPtr(), abilityDb, NDb::ABILITYTYPEID_SPECIAL, true, false);
		instance = new PFAbilityInstance(ability, Target(receiver.GetPtr()), true);
	}
	static NDb::EventProcessorOnTarget* Processor(const char* minimum = "1", const NDb::TargetSelector* selector = nullptr)
	{
		auto* db = new NDb::EventProcessorOnTarget;
		db->minTargets.sString = minimum; db->targetSelector = selector; db->spell = Spell(); return db;
	}
};

/** Thresholds are floats, and the original count pass is a real selector invocation, not a cached preview. */
static void TargetTriggerB13(Checks& check, PFWorld* world)
{
	const int before = check.count, failed = check.failed;
	using F = TargetTriggerFixtureB13;
	struct Threshold { const char* text; float value; bool valid = true; };
	for (const auto& threshold : {Threshold{"", 1}, Threshold{"-1", -1}, Threshold{"0", 0}, Threshold{"0.5", .5f},
		Threshold{"1", 1}, Threshold{"1.0001", 1.0001f}, Threshold{"2", 2}, Threshold{"2.5", 2.5f},
		Threshold{"cWrapperBonus", 2}, Threshold{"sLife/200", 2}, Threshold{"cMissing", 0, false},
		Threshold{"1/0", 0, false}, Threshold{"1e39", 0, false}, Threshold{"compiled-only", 0, false}})
	for (int count = 0; count <= 2; ++count)
	{
		F f(world); auto* list = new TriggerTargetsDbB13; NDb::Ptr<TriggerTargetsDbB13> listDb = list;
		for (int i = 0; i < count; ++i) list->targets.push_back(Target(f.receiver.GetPtr()));
		auto* raw = F::Processor(threshold.text, list);
		if (strcmp(threshold.text, "compiled-only") == 0) { raw->minTargets.sString = ""; raw->minTargets.compiledString = "unsupported"; }
		NDb::Ptr<NDb::EventProcessorBase> processor = raw;
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(processor, "1"); auto app = f.Apply(check, db); if (!app) continue;
		f.Event(); check(f.Delta() == 0 && list->enumerations == 0, "B13 target triggers do not subscribe to unrelated unit events");
		const bool matches = threshold.valid && count >= threshold.value;
		check(app->Step(0) == matches && f.Delta() == (matches ? count : 0),
			"B13 original target threshold compares a checked float count before sending one response per selected target");
		check(list->enumerations == (threshold.valid ? matches ? 2 : 1 : 0),
			"B13 malformed thresholds never enumerate, failed thresholds count once, accepted thresholds enumerate again");
	}
	for (auto mode : {NDb::TARGETSELECTORMODE_NORMAL, NDb::TARGETSELECTORMODE_CAPTURETARGETS, NDb::TARGETSELECTORMODE_SENDONCE})
	{
		F f(world); auto* list = new TriggerTargetsDbB13; NDb::Ptr<TriggerTargetsDbB13> listDb = list;
		list->mode = mode; list->targets.push_back(Target(f.receiver.GetPtr()));
		NDb::Ptr<NDb::EventProcessorBase> processor = F::Processor("1", list);
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(processor, "1"); auto app = f.Apply(check, db); if (!app) continue;
		check(app->Step(0) && f.Delta() == (mode == NDb::TARGETSELECTORMODE_SENDONCE ? 0 : 1),
			"B13 original selector instance spans both passes, including SendOnce consumption during counting");
		check(list->enumerations == (mode == NDb::TARGETSELECTORMODE_CAPTURETARGETS ? 1 : 2),
			"B13 CaptureTargets reuses its own original stored selection rather than constructing another selector");
		app->Step(0); check(list->enumerations == (mode == NDb::TARGETSELECTORMODE_CAPTURETARGETS ? 1 : 2),
			"B13 exhausted target triggers cannot restart selector state");
	}
	for (bool withSpell : {false, true}) for (bool pay : {false, true})
	{
		F f(world); f.Payment(25); auto* raw = F::Processor();
		// OnTarget uses the trigger's application target, not EventProcessorBase::DoSpell's applyTarget.
		raw->applyTarget = NDb::APPLICATORAPPLYTARGET_ABILITYOWNER;
		if (!withSpell) raw->spell = nullptr;
		NDb::Ptr<NDb::EventProcessorBase> processor = raw;
		auto* trigger = F::Descriptor(processor, "1"); trigger->useAbilityOwnerMana = pay; trigger->useAbilityCooldown = pay;
		NDb::Ptr<NDb::TriggerApplicator> db = trigger; auto app = f.Apply(check, db); if (!app) continue;
		check(app->Step(0) && f.Delta() == int(withSpell),
			"B13 no-selector target trigger uses the original application target and admits a spell-less successful predicate");
		check(f.sender->GetMana() == (pay ? 15 : 25) && (f.ability->GetCurrentCooldown() > 0) == pay,
			"B13 successful target condition owns one original payment and cooldown even without a response spell");
	}
	for (bool interaction : {false, true}) for (bool ally : {false, true}) for (bool vulnerable : {false, true})
	{
		F f(world); f.receiver->ChangeFaction(ally ? NDb::FACTION_FREEZE : NDb::FACTION_BURN); f.receiver->SetVulnerable(vulnerable);
		f.ability = new PFAbilityData(f.sender.GetPtr(), f.abilityDb, NDb::ABILITYTYPEID_SPECIAL, false, interaction);
		f.instance = new PFAbilityInstance(f.ability, Target(f.receiver.GetPtr()), true);
		auto* list = new TriggerTargetsDbB13; NDb::Ptr<TriggerTargetsDbB13> listDb = list; list->targets.push_back(Target(f.receiver.GetPtr()));
		auto* raw = F::Processor("1", list); raw->spell = nullptr; NDb::Ptr<NDb::EventProcessorBase> processor = raw;
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(processor, "1"); auto app = f.Apply(check, db); if (!app) continue;
		const bool matches = interaction || ally || vulnerable;
		check(app->Step(0) == matches && list->enumerations == 1,
			"B13 original count helper rejects invulnerable enemies only for noninteraction abilities, with no second pass for a null spell");
	}
	{
		struct MissingSelector : NDb::MultipleTargetSelector { PFTargetSelector* Create(PFWorld*) const override { return nullptr; } };
		F f(world); NDb::Ptr<NDb::TargetSelector> missing = new MissingSelector;
		NDb::Ptr<NDb::EventProcessorBase> processor = F::Processor("0", missing);
		NDb::Ptr<NDb::TriggerApplicator> db = F::Descriptor(processor, "1"); auto app = f.Apply(check, db); if (app)
		{
			check(!app->Step(0) && f.Delta() == 0, "B13 a failed authored selector factory never substitutes the unselected application target or spends an action");
		}
	}
	{
		F f(world); f.Payment(25); auto* list = new TriggerTargetsDbB13; NDb::Ptr<TriggerTargetsDbB13> listDb = list;
		list->targets.push_back(Target(f.receiver.GetPtr())); NDb::Ptr<NDb::EventProcessorBase> processor = F::Processor("1", list);
		auto* raw = F::Descriptor(processor, "1"); raw->useAbilityOwnerMana = true;
		NDb::Ptr<NDb::TriggerApplicator> db = raw; auto app = f.Apply(check, db); if (app)
		{
			list->stopOnEnumeration = app.GetPtr(); app->Step(0);
			check(!app->IsAlive() && f.Delta() == 0 && list->enumerations == 1 && f.sender->GetMana() == 25,
				"B13 selector callback stopping its trigger cancels the effect pass and payment without releasing borrowed context early");
		}
	}
	std::printf("B13 target triggers checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Condition selectors keep the candidate receiver and actual miscellaneous formula context, not literal fallbacks. */
static void TargetTriggerConditionB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	struct Case { const char* expression; bool matches; bool random = false; };
	for (const auto& item : {Case{"", true}, Case{"true", true}, Case{"false", false}, Case{"sLife < rLife", true},
		Case{"sLife > rLife", false}, Case{"receiver.vIgnorableTarget == 0", false}, Case{"cWrapperBonus == 2", true},
		Case{"pMisc->GetVariable(\"isAutoAttack\") == 0", true}, Case{"random(0,10) >= 0", true, true},
		Case{"random(0,10) + cMissing", false}, Case{"true || cMissing", true}, Case{"false && cMissing", false},
		Case{"cMissing", false}, Case{"1/0", false}, Case{"1e39", false}, Case{"compiled-only", false}})
	{
		TargetTriggerFixtureB13 f(isolated.world); f.receiver->GetVariableVWM("IgnorableTarget")->SetBaseValue(1);
		auto* list = new TriggerTargetsDbB13; list->targets.push_back(Target(f.receiver.GetPtr()));
		auto* condition = new NDb::ConditionTargetSelector; condition->targetSelector = list; condition->condition.sString = item.expression;
		if (strcmp(item.expression, "compiled-only") == 0) { condition->condition.sString = ""; condition->condition.compiledString = "unsupported"; }
		auto* processorRaw = TargetTriggerFixtureB13::Processor("1", condition); processorRaw->spell = nullptr;
		NDb::Ptr<NDb::EventProcessorBase> processor = processorRaw;
		NDb::Ptr<NDb::TriggerApplicator> db = TargetTriggerFixtureB13::Descriptor(processor, "1"); auto app = f.Apply(check, db); if (!app) continue;
		NRandom::RandomGenerator expected(*isolated.world->GetRndGen()); if (item.random) expected.Next(0, 10);
		check(app->Step(0) == item.matches, "B13 original condition selector evaluates live candidate properties, variables and applicator constants");
		check(isolated.world->GetRndGen()->Next(0, 1000) == expected.Next(0, 1000),
			"B13 selected successful selector randomness commits once; malformed and lazy branches preserve the world sequence");
	}
	{
		TargetTriggerFixtureB13 f(isolated.world); auto candidate = Unit(isolated.world, NDb::FACTION_BURN, 20);
		candidate->SetHealth(20);
		candidate->GetVariableVWM("IgnorableTarget")->SetBaseValue(1);
		auto* list = new TriggerTargetsDbB13; list->targets.push_back(Target(candidate.GetPtr()));
		auto* condition = new NDb::ConditionTargetSelector; condition->targetSelector = list;
		condition->condition.sString = "rLife == 20 && receiver.vIgnorableTarget == 1 && cWrapperBonus == 2";
		auto* raw = TargetTriggerFixtureB13::Processor("1", condition); raw->spell = nullptr;
		NDb::Ptr<NDb::EventProcessorBase> processor = raw;
		NDb::Ptr<NDb::TriggerApplicator> db = TargetTriggerFixtureB13::Descriptor(processor, "1"); auto app = f.Apply(check, db);
		check(app && app->Step(0), "B13 condition receiver is the selected candidate, not the trigger receiver or its caster");
		for (const IMiscFormulaPars* misc : {static_cast<const IMiscFormulaPars*>(nullptr),
			static_cast<const IMiscFormulaPars*>(f.ability.GetPtr()), static_cast<const IMiscFormulaPars*>(app.GetPtr())})
		{
			const auto value = LinuxBootstrap::EvaluateSelectorConditionFormula("rLife + sLife", f.sender, candidate, misc);
			check(value.Succeeded() && value.value == 420, "B13 pure candidate properties require no substituted miscellaneous context");
			NRandom::RandomGenerator expected(*isolated.world->GetRndGen()); const int random = misc ? expected.Next(0, 10) : 0;
			const auto result = LinuxBootstrap::EvaluateSelectorConditionFormula("random(0,10) + cWrapperBonus", f.sender, candidate, misc);
			check(result.Succeeded() == bool(misc) && (!misc || result.value == random + 2)
				&& isolated.world->GetRndGen()->Next(0, 1000) == expected.Next(0, 1000),
				"B13 original ability and applicator contexts supply their own constants and committed RNG; null context does not");
		}
		candidate->Cleanup();
	}
	std::printf("B13 target condition formulas checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Load the exact bomb-proximity trigger through the original cache, retaining its nested selectors and formulas. */
static void TargetTriggerAuthoredB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		const auto status = NDb::Get<NDb::StatusApplicator>(NDb::DBID("/Items/Talents/Class/Inventor/A3_BombInvis.APLR.xdb"));
		NDb::Ptr<NDb::TriggerApplicator> trigger = status && status->applicators.size() == 1
			? dynamic_cast<const NDb::TriggerApplicator*>(status->applicators[0].GetPtr()) : nullptr;
		check(trigger && trigger->GetDBID().GetId() == "A9EC5F55-8261-4D36-ABE1-E4BBD05C4B24",
			"B13 the retained authored status owns the expected inline proximity trigger");
		const auto* processor = trigger ? dynamic_cast<const NDb::EventProcessorOnTarget*>(trigger->processor.GetPtr()) : nullptr;
		check(processor && processor->minTargets.sString == "1.0f" && !processor->spell && processor->targetSelector,
			"B13 original DB cache resolves the exact Inventor bomb trigger and its external proximity processor");
		NDb::Ptr<NDb::UnitConstantsContainer> constants;
		CObj<Stream> xml = files->OpenFile("Items/Talents/Class/Inventor/Ability_A3.TALENT.xdb", FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
		check(xml && xml->IsOk(), "B13 original Inventor A3 constants XML is readable");
		if (xml && xml->IsOk())
		{
			CObj<IXmlSaver> saver = CreateXmlSaver(xml, true);
			if (saver->StartChunk("Talent", 0)) constants = NearbyConstantsB13(*saver, "constants");
		}
		if (processor && constants)
		for (bool parented : {false, true})
		for (bool allied : {false, true}) for (bool ignored : {false, true})
		for (bool distant : {false, true}) for (bool vulnerable : {false, true})
		{
			B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
			TargetTriggerFixtureB13 f(isolated.world);
			const_cast<NDb::Ability*>(f.abilityDb.GetPtr())->constants = constants; f.ability->Reset();
			check(f.ability->GetConstant("A3_TriggerRange") && f.ability->GetConstant("A3_TriggerRange")->var.sString == "1",
				"B13 proximity radius is the original A3 constant, not a copied test substitution");
			auto enemy = B8BehaviourMoving(isolated.world, allied ? NDb::FACTION_FREEZE : NDb::FACTION_BURN);
			const int ghost = enemy->SetGhostMode(NDb::GHOSTMOVEMODE_IGNOREDYNAMIC, false);
			check(f.sender->TeleportTo(CVec2(20, 10)) && f.receiver->TeleportTo(CVec2(10, 10))
				&& enemy->TeleportTo(CVec2(distant ? 20 : 10.25f, 10)) && enemy->GetPosition().x == (distant ? 20 : 10.25f),
				"B13 actual trap and candidate occupy exact valid map positions without dynamic collision relocation");
			enemy->SetVulnerable(vulnerable); enemy->GetVariableVWM("IgnorableTarget")->SetBaseValue(ignored ? 1 : 0);
			auto app = f.Apply(check, parented ? static_cast<const NDb::BaseApplicator*>(status.GetPtr()) : trigger.GetPtr()); if (!app) continue;
			isolated.world->Step(.01f, .01f);
			isolated.world->Step(.01f, .01f);
			const bool matches = !allied && !ignored && !distant && vulnerable;
			check(app->IsAlive() == !matches && f.instance->GetActiveApplicatorsCount() == (matches ? 0 : parented ? 2 : 1),
				"B13 real bomb trigger and original StopByChild parent retire only for nearby nonignored vulnerable enemies");
			enemy->ResetGhostMode(ghost); enemy->Cleanup();
		}
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 authored target triggers checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
