/** Mutable parent data models a later pickup without replacing the production upgrade or dispatch. */
struct GlyphSourceB13 : PFApplBuff
{
	OBJECT_METHODS(PFApplBuff::typeId, GlyphSourceB13);
public:
	using PFApplBuff::Init;
	using PFApplBuff::Stop;
	float number = -1;
	GlyphSourceB13() = default;
	explicit GlyphSourceB13(const PFApplCreatePars& pars) : PFApplBuff(pars) {}
	float GetVariable(const char* name) const override
	{
		return name && std::strcmp(name, "glyphNumber") == 0 ? number : PFApplBuff::GetVariable(name);
	}
};

/** Loaded glyph identities carry observable original permanent-stat payloads. */
struct GlyphDescriptorB13 : NDb::Glyph
{
	GlyphDescriptorB13(const char* path, const char* amount)
	{
		SetDbId(NDb::DBID(path)); ProbeResourceCache::MarkLoaded(this);
		flags = static_cast<NDb::EAbilityFlags>(0); manaCost.sString = "0";
		auto* immediate = new NDb::DispatchImmediate; immediate->instantApply = true; dispatch = immediate;
		auto* effect = new NDb::PermanentStatModApplicator;
		effect->enabled.sString = "true"; effect->modifier.stat = NDb::STAT_STRENGTH;
		effect->modifier.multValue.sString = "1"; effect->modifier.addValue.sString = amount;
		applicators.push_back(effect);
	}
};

/** Preserve the process session root while exercising real map-versus-session glyph selection. */
struct GlyphRootB13
{
	NDb::Ptr<NDb::SessionRoot> previous;
	NDb::Ptr<NDb::GlyphsDB> glyphs;
	GlyphRootB13() : previous(NDb::SessionRoot::GetRoot())
	{
		auto* table = new NDb::GlyphsDB; table->glyphs.resize(2);
		table->glyphs[0].glyph = new GlyphDescriptorB13("/Mock/GlyphSeven.xdb", "7");
		table->glyphs[1].glyph = new GlyphDescriptorB13("/Mock/GlyphThirteen.xdb", "13");
		glyphs = table;
		auto* logic = new NDb::SessionLogicRoot;
		if (previous && previous->logicRoot) logic->Assign(*previous->logicRoot);
		logic->glyphsDB = table;
		auto* root = new NDb::SessionRoot;
		if (previous) root->Assign(*previous);
		root->logicRoot = logic; NDb::SessionRoot::InitRoot(root);
	}
	~GlyphRootB13() { NDb::SessionRoot::InitRoot(previous.GetPtr()); }
};

/** Retain the real upgrade's owning ability and target across delayed release and teardown. */
struct GlyphUpgradeFixtureB13
{
	CObj<PFBaseUnit> hero;
	NDb::Ptr<NDb::Ability> db;
	CObj<PFAbilityData> data;
	CObj<PFAbilityInstance> cast;
	CObj<GlyphSourceB13> source;
	CObj<PFBaseApplicator> app;
	float strength;
	explicit GlyphUpgradeFixtureB13(PFWorld* world)
	{
		hero = Unit(world, NDb::FACTION_FREEZE, 40).GetPtr(); hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		auto* raw = new NDb::Ability; raw->manaCost.sString = "10"; raw->cooldownTime.sString = "2";
		db = raw; data = new PFAbilityData(hero.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL);
		const Target target(hero.GetPtr()); cast = new PFAbilityInstance(data, target, true);
		PFApplCreatePars pars(cast, target); pars.pDBAppl = new NDb::FlagsApplicator;
		source = new GlyphSourceB13(pars); source->Init(); strength = hero->GetStatValue(NDb::STAT_STRENGTH);
	}
	void Install(float number, const char* count = "1", const char* lifetime = "10")
	{
		source->number = number;
		auto* raw = new NDb::AbilityUpgradeApplicator; raw->enabled.sString = "true";
		raw->startCondition.sString = "true"; raw->lifeTime.sString = lifetime; raw->applyCount.sString = count;
		raw->targetAbility = NDb::ABILITYTYPEID_SPECIAL; raw->flags = NDb::ABILITYUPGRADEMODE_USEGLYPH;
		const Target target(hero.GetPtr()); PFApplCreatePars pars(cast, target, source.GetPtr()); pars.pDBAppl = raw;
		app = CreateApplicator(pars); if (app) ActivateApplicator(app, cast);
	}
	void Release()
	{
		auto* upgrade = dynamic_cast<PFApplAbilityUpgrade*>(app.GetPtr());
		if (!upgrade) return;
		vector<NDb::Ptr<NDb::BaseApplicator>> normal, persistent; bool original = true;
		upgrade->UpgradeAbilityApplicators(data.GetPtr(), normal, persistent, original);
	}
	/** Use the real dispatch upgrade traversal so stopped/expired buffs cannot participate. */
	void DispatchRelease()
	{
		auto* immediate = new NDb::DispatchImmediate;
		immediate->instantApply = true; immediate->flags = NDb::DISPATCHFLAGS_UPGRADABLE;
		auto* raw = new NDb::Spell; raw->dispatch = immediate; NDb::Ptr<NDb::Spell> spell = raw;
		CreateDispatch(cast, nullptr, Target(hero.GetPtr()), Target(hero.GetPtr()), spell);
	}
	float Delta() const { return hero->GetStatValue(NDb::STAT_STRENGTH) - strength; }
	~GlyphUpgradeFixtureB13() { if (app) app->Stop(); hero->Cleanup(); }
};

/** A saved upgrade must release the admitted glyph, not a different later parent or database value. */
static void GlyphUpgradeB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (bool laterParent : {false, true}) for (bool laterDatabase : {false, true})
	{
		GlyphRootB13 root; B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		GlyphUpgradeFixtureB13 f(isolated.world); f.Install(0);
		check(f.app && f.cast->GetActiveApplicatorsCount() == 1 && f.Delta() == 0,
			"B13 saved glyph upgrade owns one original buff and does not release its payload on admission");
		if (laterParent) f.source->number = 1;
		if (laterDatabase) const_cast<NDb::GlyphsDB*>(root.glyphs.GetPtr())->glyphs[0].glyph = root.glyphs->glyphs[1].glyph;
		isolated.world->Step(.1f, .1f); f.Release();
		check(f.Delta() == 7, "B13 delayed glyph upgrade retains the admitted descriptor across later pickups and database changes");
		f.Release();
		check(f.Delta() == 7, "B13 bounded saved glyph upgrade releases only once");
	}
	for (float number : {-1.f, -.5f, .5f, 1.5f, 2.f, 2147483648.f,
		std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
	{
		GlyphRootB13 root; B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		GlyphUpgradeFixtureB13 f(isolated.world); f.Install(number); f.Release();
		check(f.Delta() == 0 && f.cast->GetActiveApplicatorsCount() == 0,
			"B13 invalid glyph indices cannot truncate to another glyph or install a release upgrade");
	}
	for (bool mapFallback : {false, true}) for (bool outOfRange : {false, true})
	{
		GlyphRootB13 root; B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		auto* table = new NDb::GlyphsDB; table->glyphs.resize(1);
		table->glyphs[0].glyph = new GlyphDescriptorB13("/Mock/MapGlyphNineteen.xdb", "19");
		auto* manager = new NDb::GlyphManagerParams; manager->glyphs = table;
		auto* settings = new NDb::AdvMapSettings; settings->overrideGlyphSettings = manager;
		auto* description = const_cast<NDb::AdvMapDescription*>(isolated.world->GetMapDescription());
		if (mapFallback) const_cast<NDb::AdvMap*>(description->map.GetPtr())->mapSettings = settings;
		else description->mapSettings = settings;
		GlyphUpgradeFixtureB13 f(isolated.world); f.Install(outOfRange ? 1 : 0); f.Release();
		check(f.Delta() == (outOfRange ? 0 : 19),
			"B13 selected map glyph tables are authoritative and never fall through to a different session glyph");
	}
	for (int missing = 0; missing < 6; ++missing)
	{
		GlyphRootB13 root; B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		GlyphUpgradeFixtureB13 f(isolated.world);
		if (missing < 3)
		{
			auto* manager = new NDb::GlyphManagerParams;
			if (missing != 0)
			{
				auto* table = new NDb::GlyphsDB;
				if (missing == 2) table->glyphs.resize(1);
				manager->glyphs = table;
			}
			auto* settings = new NDb::AdvMapSettings; settings->overrideGlyphSettings = manager;
			const_cast<NDb::AdvMapDescription*>(isolated.world->GetMapDescription())->mapSettings = settings;
		}
		else if (missing == 3) const_cast<NDb::SessionLogicRoot*>(NDb::SessionRoot::GetRoot()->logicRoot.GetPtr())->glyphsDB = nullptr;
		else if (missing == 4) const_cast<NDb::SessionRoot*>(NDb::SessionRoot::GetRoot().GetPtr())->logicRoot = nullptr;
		else NDb::SessionRoot::InitRoot(static_cast<const NDb::SessionRoot*>(nullptr));
		f.Install(0); f.DispatchRelease();
		check(f.Delta() == 0 && f.cast->GetActiveApplicatorsCount() == 0,
			"B13 absent override table, empty table, empty entry and absent session data never install or deliver a glyph upgrade");
	}
	for (int retire = 0; retire < 3; ++retire) for (bool unlimited : {false, true})
	{
		GlyphRootB13 root; B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		GlyphUpgradeFixtureB13 f(isolated.world); f.Install(0, unlimited ? "0" : "1", "1");
		if (retire == 1) f.app->Stop();
		if (retire == 2) isolated.world->Step(1.1f, 1.1f);
		f.DispatchRelease(); f.DispatchRelease();
		check(f.Delta() == (retire ? 0 : unlimited ? 14 : 7),
			"B13 real dispatch traversal preserves bounded/unlimited release and excludes stopped or naturally expired storage");
		f.app->Stop(); f.DispatchRelease();
		check(f.Delta() == (retire ? 0 : unlimited ? 14 : 7), "B13 retired storage cannot replay its retained glyph descriptor");
	}
	for (bool overrideTable : {false, true}) for (bool included : {false, true})
	{
		GlyphRootB13 root; B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		GlyphUpgradeFixtureB13 f(isolated.world);
		NDb::Ptr<NDb::Glyph> chosen = included ? root.glyphs->glyphs[0].glyph
			: new GlyphDescriptorB13("/Mock/UnlistedGlyph.xdb", "19");
		if (overrideTable)
		{
			auto* table = new NDb::GlyphsDB; table->glyphs.resize(1);
			table->glyphs[0].glyph = root.glyphs->glyphs[1].glyph;
			auto* manager = new NDb::GlyphManagerParams; manager->glyphs = table;
			auto* settings = new NDb::AdvMapSettings; settings->overrideGlyphSettings = manager;
			const_cast<NDb::AdvMapDescription*>(isolated.world->GetMapDescription())->mapSettings = settings;
		}
		int observed = -2; CObj<EventDispatchB7Listener> listener = new EventDispatchB7Listener;
		listener->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent* event) {
			if (const auto* pickup = dynamic_cast<const PFBaseUnitPickupEvent*>(event)) observed = pickup->GetGlyphNumber();
			return 0u;
		};
		f.hero->AddEventListener(listener.GetPtr());
		CObj<PFGlyph> pickup = new PFGlyph(isolated.world, chosen, CVec3(5, 5, 0));
		check(pickup->PickUp(f.hero.GetPtr()) && pickup->IsObjectDead(), "B13 ordinary glyph pickup still completes through the actual pickupable object");
		check(observed == (!overrideTable && included ? 0 : -1)
			&& f.Delta() == (included ? 7 : 19),
			"B13 pickup event and saved release share authoritative index space while unlisted ordinary glyphs still activate normally");
		f.hero->RemoveEventListener(listener.GetPtr()); listener->action = {};
	}
	std::printf("B13 glyph upgrade checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** A disabled spell must not dispatch its payload, including invalid selected formula branches. */
static void GlyphReleaseSpellGuardB13(Checks& check, PFWorld* world)
{
	const int before = check.count, failed = check.failed;
	struct Case { const char* condition; bool expected; };
	for (const Case test : {Case{"true", true}, {"", true}, {"false", false}, {"sLife < rLife", true},
		{"sLife > rLife", false}, {"cMissing", false}, {"1/0", false}, {"1e39", false},
		{"true ? 1 : cMissing", true}, {"false ? 1 : cMissing", false}, {"compiled-only", false}})
	{
		TriggerConditionFixtureB13 f(world);
		auto* raw = new NDb::SpellApplicator; raw->spell = TriggerConditionFixtureB13::Spell();
		raw->enabled.sString = test.condition;
		if (std::strcmp(test.condition, "compiled-only") == 0) { raw->enabled.sString = ""; raw->enabled.compiledString = "unsupported"; }
		NDb::Ptr<NDb::SpellApplicator> db = raw; auto app = f.Create(db);
		check(app && f.Activate(app), "B13 guarded spell uses original creation and immediate retirement");
		check(f.Delta() == (test.expected ? 1 : 0) && f.instance->GetActiveApplicatorsCount() == 0,
			"B13 original base-start result prevents disabled or invalid spell dispatch without unbalanced ownership");
	}
	std::printf("B13 glyph release spell guard checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Exercise original G290 storage/release resources; only the not-yet-enabled pickup source is mocked. */
static void AuthoredGlyphUpgradeB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		const auto db = NDb::Get<NDb::Talent>(NDb::DBID("/Items/Talents/Level2/G290_GuildGlyphSave.TALENT.xdb"));
		const auto* trigger = db && !db->passiveApplicators.empty()
			? dynamic_cast<const NDb::TriggerApplicator*>(db->passiveApplicators[0].GetPtr()) : nullptr;
		const auto* processor = trigger ? dynamic_cast<const NDb::EventProcessorPickupGlyph*>(trigger->processor.GetPtr()) : nullptr;
		check(processor && processor->spell && processor->spell->applicators.size() == 2,
			"B13 original G290 response owns its complete storage program and saved glyph upgrade");
		if (processor && processor->spell) for (int number : {0, 1}) for (bool automatic : {false, true})
		{
			GlyphRootB13 root; B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
			auto hero = CommandHeroB10(isolated.world); hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
			hero->InstallTalent(db, true); CObj<PFTalent> talent = hero->GetTalent(0, 0);
			const Target target(hero.GetPtr()); CObj<PFAbilityInstance> passive = new PFAbilityInstance(talent.GetPtr(), target, true);
			auto* sourceDb = new NDb::FlagsApplicator; sourceDb->formulaName = "G290GlyphUp2";
			sourceDb->enabled.sString = "true"; sourceDb->startCondition.sString = "true"; sourceDb->lifeTime.sString = "-1";
			PFApplCreatePars pars(passive, target); pars.pDBAppl = sourceDb;
			CObj<GlyphSourceB13> source = new GlyphSourceB13(pars); source->number = number;
			check(source->Init() && ActivateApplicator(source.GetPtr(), passive), "B13 G290 fixture supplies only a named pickup value on the real passive ability");
			const float strength = hero->GetStatValue(NDb::STAT_STRENGTH);
			check(talent->CheckCastLimitations(target), "B13 original G290 cannot be cast before a glyph is stored");
			auto storage = CreateDispatch(passive, source.GetPtr(), target, target, processor->spell);
			isolated.world->Step(.1f, .1f);
			check(storage && hero->GetVariable("GlyphCatched") == 1 && hero->GetVariable("NumberOfGlyph") == number
				&& hero->GetStatValue(NDb::STAT_STRENGTH) == strength && !talent->CheckCastLimitations(target),
				"B13 original G290 response stores its glyph markers and cast eligibility without prematurely applying the glyph");
			if (!automatic)
			{
				CObj<NCore::WorldCommand> command = CreateCmdUseTalent(hero.GetPtr(), 0, 0, target, true);
				check(command && command->CanExecute(), "B13 stored G290 is admitted by the actual talent command");
				if (command) command->Execute(isolated.world);
			}
			for (int i = 0; i < (automatic ? 620 : 10); ++i) isolated.world->Step(.1f, .1f);
			const float delivered = hero->GetStatValue(NDb::STAT_STRENGTH) - strength;
			std::printf("B13 G290 stored release: glyph=%d automatic=%d delivered=%g caught=%g number=%g\n",
				number, automatic, delivered, hero->GetVariable("GlyphCatched"), hero->GetVariable("NumberOfGlyph"));
			check(delivered == (number ? 13 : 7) && hero->GetVariable("GlyphCatched") == 0
				&& talent->CheckCastLimitations(target), "B13 original G290 manual or sixty-second automatic release applies the saved glyph once and clears cast eligibility");
			for (int i = 0; i < 20; ++i) isolated.world->Step(.1f, .1f);
			check(hero->GetStatValue(NDb::STAT_STRENGTH) - strength == delivered,
				"B13 completed G290 program cannot apply a second glyph during later simulation");
			source->Stop(); hero->GetTalentsSet()->CleanSet(); hero->Cleanup();
		}
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 authored glyph upgrade checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
