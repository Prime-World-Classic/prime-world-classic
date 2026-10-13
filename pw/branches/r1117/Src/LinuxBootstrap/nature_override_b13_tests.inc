/** Real world, original authored cell bytes, actual hero and terrain applicator factories. */
struct NatureOverrideFixtureB13
{
	Checks& check;
	B8BehaviourForeignWorld loaded;
	CObj<TestFileSystem> files;
	NDb::Ptr<NDb::Terrain> terrain;
	CObj<B9CastHero> hero, observer;
	NDb::Ptr<NDb::Ability> abilityDb;
	CObj<PFAbilityData> ability;
	CObj<PFAbilityInstance> cast;
	std::vector<CObj<PFBaseApplicator>> effects;
	PFWorld* World() const { return loaded.world; }
	PFWorldNatureMap* Map() const { return World()->GetNatureMap(); }
	NatureOverrideFixtureB13(Checks& check, float speed = 0, float gridStep = 10) : check(check), loaded(check)
	{
		files = new TestFileSystem("", false); RootFileSystem::RegisterFileSystem(files);
		auto* raw = new NDb::Terrain; raw->elemXCount = 4; raw->elemYCount = 3; raw->tilesPerElement = 4;
		raw->natureMap.gridStep = gridStep; raw->natureMap.segmentCount = 3;
		raw->natureMap.attackSpeed = 1; raw->natureMap.overrideSpeed = speed;
		raw->natureMap.natureMapFile = "/Mock/NatureOverrideB13.naturemap"; terrain = raw;
		files->SetFileWithContents(raw->natureMap.natureMapFile, NatureQueryImageB12(raw->natureMap));
		Map()->OnLoaded(terrain);
		hero = B9MaleHero(World(), NDb::FACTION_FREEZE);
		observer = B9MaleHero(World(), NDb::FACTION_BURN);
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); observer->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		check(observer->TeleportTo(CVec2(25, 10)), "terrain observer uses original movement positioning");
		observer->UpdateNatureType();
		auto* db = new NDb::Ability; db->manaCost.sString = "0";
		auto* constants = new NDb::UnitConstantsContainer;
		for (const auto& value : {std::pair<const char*, const char*>("Time_Terrain", "sLife / 500"),
			std::pair<const char*, const char*>("Radius_Terrain", "sLife / 1000")})
		{
			auto* constant = new NDb::UnitConstant; constant->name = value.first; constant->var.sString = value.second;
			constants->vars.push_back(constant);
		}
		db->constants = constants; abilityDb = db;
		ability = new PFAbilityData(hero.GetPtr(), abilityDb, NDb::ABILITYTYPEID_SPECIAL, true, false);
		cast = new PFAbilityInstance(ability, Target(observer.GetPtr()), false);
		check(Map()->GetNatureInPoint(25.f, 10.f) == TERR_B && observer->GetTerrainType() == TERR_B,
			"terrain fixture begins on an authored enemy cell, not synthesized terrain");
	}
	NDb::Ptr<NDb::ModifyTerrainApplicator> Descriptor(const char* faction = "sFctn", const char* radius = "cRadius_Terrain", const char* duration = "cTime_Terrain")
	{
		auto* db = new NDb::ModifyTerrainApplicator;
		db->enabled.sString = "true"; db->faction.sString = faction;
		db->modifRadius.sString = radius; db->durationTime.sString = duration;
		return db;
	}
	CObj<PFBaseApplicator> Apply(const NDb::BaseApplicator* db, bool position = false)
	{
		const Target target = position ? Target(CVec3(25, 10, 0)) : Target(observer.GetPtr());
		PFApplCreatePars pars(cast, target);
		pars.pWorld = World(); pars.pDBAppl = db;
		CObj<PFBaseApplicator> app = CreateApplicator(pars);
		check(app && ActivateApplicator(app, cast), "actual terrain factory initializes and activates through the engine");
		if (app) effects.push_back(app);
		return app;
	}
	~NatureOverrideFixtureB13()
	{
		for (auto& effect : effects) if (effect->IsAlive()) effect->Stop();
		hero->Cleanup(); observer->Cleanup(); RootFileSystem::UnregisterFileSystem(files);
	}
};

static void NatureOverrideB13(Checks& check)
{
	{
		NatureOverrideFixtureB13 f(check);
		CObj<PFLogicObject> object = new PFLogicObject(f.World(), CVec3(25, 10, 0), nullptr);
		f.World()->GetAIWorld()->RegisterObject(object);
		object->UpdateNatureType();
		auto app = f.Apply(f.Descriptor());
		check(app && app->IsAlive() && f.cast->GetActiveApplicatorsCount() == 1
			&& f.Map()->IsOverrideActive(app->GetObjectId()), "terrain applicator owns its authored temporary override");
		check(f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_A && f.observer->GetTerrainType() == TERR_B,
			"immediate cell change precedes the original next-world-step unit notification");
		f.World()->Step(.5f, .5f);
		check(object->GetTerrainType() == TERR_A, "original non-unit AI object ring receives the world nature callback");
		check(f.observer->GetTerrainType() == TERR_A && f.ability->GetNatureTypeInPos(CVec2(25, 10)) == TERR_A,
			"world stepping publishes live nature to cached units and real formula getters");
		check(app->IsAlive() && f.cast->GetActiveApplicatorsCount() == 1, "partial duration keeps actual applicator ownership");
		f.World()->Step(1.5f, 1.5f);
		check(!app->IsAlive() && !f.Map()->IsOverrideActive(app->GetObjectId()) && f.cast->GetActiveApplicatorsCount() == 0,
			"world expiry finishes the applicator and ability count on the original owner path");
		check(f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_B && f.observer->GetTerrainType() == TERR_B,
			"expiry restores authored base nature and notifies real units");
		check(object->GetTerrainType() == TERR_B, "non-unit callback also receives authored nature restoration");
		object->OnDie();
	}
	{
		NatureOverrideFixtureB13 f(check, 10);
		f.Map()->Override(NDb::FACTION_FREEZE, CCircle(CVec2(15, 10), 12), 3, 101);
		check(f.Map()->IsOverrideActive(101) && f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_B,
			"original override speed schedules distance-delayed cells");
		f.World()->Step(.5f, .5f);
		check(f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_B, "delayed cell does not activate early");
		f.World()->Step(.5f, .5f);
		check(f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_A && f.observer->GetTerrainType() == TERR_A,
			"wave reaches the cell at its authored distance/speed delay");
		f.World()->Step(1, 1); f.World()->Step(1, 1);
		check(!f.Map()->IsOverrideActive(101) && f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_B,
			"delayed wave expires at the original duration when stepped on its delay boundaries");
	}
	for (bool neutral : {false, true})
	{
		NatureOverrideFixtureB13 f(check);
		// Exercise the underlying original code too, so overlap regressions are
		// reproduced even while the baseline world wrapper is still disabled.
		NatureQueryCodecB12 original;
		f.files->SetFileWithContents(f.terrain->natureMap.natureMapFile, NatureQueryImageB12(f.terrain->natureMap));
		const Terrain::NatureMapParams params = {&f.terrain->natureMap, CVec3(40, 30, 50)};
		check(original.Initialize(params, &original.modified), "original override ownership regression loads real grid storage");
		const CCircle sample(CVec2(25, 10), 2);
		original.Override(NDb::NATURETYPE_A, sample, 4, 201); original.OnStep(.5f);
		original.Override(neutral ? NDb::NATURETYPE_N : NDb::NATURETYPE_A, sample, 1, 202);
		original.OnStep(.5f);
		check(original.GetNature(25.f, 10.f) == (neutral ? TERR_N : TERR_A),
			"original-code regression: a superseded immediate owner cannot erase its replacement");
		original.OnStep(.5f);
		check(original.GetNature(25.f, 10.f) == (neutral ? TERR_B : TERR_A)
			&& original.GetElementInfo(7).ownerData == (neutral ? 0u : 201u),
			"original-code regression: surviving same-faction lifetime restores its actual owner");
		const CCircle circle(CVec2(25, 10), 2);
		f.Map()->Override(NDb::FACTION_FREEZE, circle, 4, 201);
		f.World()->Step(.5f, .5f);
		f.Map()->Override(neutral ? NDb::FACTION_NEUTRAL : NDb::FACTION_FREEZE, circle, 1, 202);
		f.World()->Step(.5f, .5f);
		check(f.Map()->GetNatureInPoint(25.f, 10.f) == (neutral ? TERR_N : TERR_A),
			"older zero-delay ownership cannot expire over a newer circle");
		f.World()->Step(.5f, .5f);
		check(f.Map()->GetNatureInPoint(25.f, 10.f) == (neutral ? TERR_B : TERR_A),
			"same-faction older lifetime resumes; opposing circles cancel the old owner");
		check(f.Map()->IsOverrideActive(201) == !neutral && !f.Map()->IsOverrideActive(202),
			"override registry reflects stock same-faction versus opposing-faction policy");
		f.World()->Step(3, 3);
		check(f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_B && !f.Map()->IsOverrideActive(201),
			"all remaining ownership eventually expires back to authored nature");
	}
	{
		NatureOverrideFixtureB13 f(check, 10);
		f.Map()->Override(NDb::FACTION_FREEZE, CCircle(CVec2(25, 10), 2), 4, 211);
		f.Map()->Override(NDb::FACTION_FREEZE, CCircle(CVec2(15, 10), 12), 2, 212);
		f.World()->Step(1, 1);
		check(f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_A, "same-faction delayed owner merges into the active cell");
		f.World()->Step(1, 1);
		check(f.Map()->IsOverrideActive(211) && !f.Map()->IsOverrideActive(212)
			&& f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_A, "delayed owner expiry resumes the live previous owner");
		f.World()->Step(2, 2);
		check(f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_B, "all delayed and immediate ownership restores the authored cell");
	}
	{
		NatureOverrideFixtureB13 f(check, 10);
		f.Map()->Override(NDb::FACTION_FREEZE, CCircle(CVec2(25, 10), 2), 2, 221);
		f.Map()->Override(NDb::FACTION_FREEZE, CCircle(CVec2(15, 10), 12), 4, 222);
		f.World()->Step(1, 1); f.World()->Step(1, 1);
		check(!f.Map()->IsOverrideActive(221) && f.Map()->IsOverrideActive(222)
			&& f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_A, "longer delayed owner survives expiry of the initial owner");
		f.World()->Step(2, 2);
		check(f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_B, "delayed same-value owner updates ownership needed for final restoration");
	}
	{
		NatureOverrideFixtureB13 f(check, 10);
		f.Map()->Override(NDb::FACTION_FREEZE, CCircle(CVec2(25, 10), 2), 4, 231);
		f.Map()->Override(NDb::FACTION_NEUTRAL, CCircle(CVec2(15, 10), 12), 2, 232);
		f.World()->Step(2, 2); f.World()->Step(2, 2);
		check(!f.Map()->IsOverrideActive(231) && !f.Map()->IsOverrideActive(232)
			&& f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_B,
			"a delayed conflict activated and expired in one tick cannot leave ownerless permanent terrain");
	}
	for (const char* bad : {"missing", "1/0", "1e39", "sLife+", "sqrt(-1)"})
		for (int field = 0; field < 3; ++field)
		{
			NatureOverrideFixtureB13 f(check);
			auto good = f.Apply(f.Descriptor());
			auto badDb = f.Descriptor(field == 0 ? bad : "0", field == 1 ? bad : "1", field == 2 ? bad : "2");
			auto rejected = f.Apply(badDb);
			check(rejected && !rejected->IsAlive() && !f.Map()->IsOverrideActive(rejected->GetObjectId()),
				"invalid terrain formula terminates without a fallback override");
			check(good->IsAlive() && f.cast->GetActiveApplicatorsCount() == 1 && f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_A,
				"invalid activation balances notifications without stopping its live sibling or terrain");
		}
	for (int field = 0; field < 3; ++field)
	{
		NatureOverrideFixtureB13 f(check);
		auto db = f.Descriptor(); auto* edit = const_cast<NDb::ModifyTerrainApplicator*>(db.GetPtr());
		if (field == 0) { edit->faction.sString = ""; edit->faction.compiledString = "bytecode-only"; }
		if (field == 1) { edit->modifRadius.sString = ""; edit->modifRadius.compiledString = "bytecode-only"; }
		if (field == 2) { edit->durationTime.sString = ""; edit->durationTime.compiledString = "bytecode-only"; }
		auto app = f.Apply(db);
		check(app && !app->IsAlive() && f.cast->GetActiveApplicatorsCount() == 0
			&& f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_B, "compiled-only terrain fields fail closed without neutral defaults");
	}
	{
		NatureOverrideFixtureB13 f(check);
		auto app = f.Apply(f.Descriptor("0", "", "1"), true);
		check(app && !app->IsAlive() && f.cast->GetActiveApplicatorsCount() == 0
			&& f.Map()->IsOverrideActive(app->GetObjectId()) && f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_N,
			"position activation retains original instant applicator completion and world-owned lifetime");
		f.World()->Step(1, 1);
		check(f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_B && !f.Map()->IsOverrideActive(app->GetObjectId()),
			"world independently expires terrain after the position applicator has stopped");
	}
	for (int bad = 0; bad < 12; ++bad)
	{
		NatureOverrideFixtureB13 f(check);
		auto db = f.Descriptor("1", "1", "2"); auto* edit = const_cast<NDb::ModifyTerrainApplicator*>(db.GetPtr());
		if (bad < 4) edit->faction.sString = bad == 0 ? "-1" : bad == 1 ? "3" : bad == 2 ? ".5" : "1e30";
		else if (bad < 8) edit->modifRadius.sString = bad == 4 ? "0" : bad == 5 ? "-1" : bad == 6 ? "1e30" : "1e39";
		else edit->durationTime.sString = bad == 8 ? "0" : bad == 9 ? "-1" : bad == 10 ? "" : "1e39";
		auto app = f.Apply(db);
		check(app && !app->IsAlive() && f.cast->GetActiveApplicatorsCount() == 0
			&& !f.Map()->IsOverrideActive(app->GetObjectId()) && f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_B,
			"invalid enum, radius or duration completes before any terrain mutation");
	}
	{
		NatureOverrideFixtureB13 f(check);
		const CCircle circle(CVec2(25, 10), 2);
		f.Map()->Override(NDb::FACTION_FREEZE, circle, 2, 301);
		for (float dt : {-1.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::max()})
		{
			f.Map()->OnStep(dt);
			check(f.Map()->IsOverrideActive(301) && f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_A,
				"invalid or overflowing tick arithmetic retains the last good override and timer");
		}
		f.Map()->Override(NDb::FACTION_NEUTRAL, circle, 10, 301);
		check(f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_A, "duplicate owner cannot replace or orphan its existing lifetime");
		for (int bad = 0; bad < 5; ++bad)
		{
			CCircle invalid = circle;
			if (bad == 0) invalid.center.x = std::numeric_limits<float>::quiet_NaN();
			if (bad == 1) invalid.center.y = std::numeric_limits<float>::max();
			if (bad == 2) invalid.r = std::numeric_limits<float>::infinity();
			if (bad == 3) invalid.r = 1e30f;
			const unsigned owner = bad == 4 ? 0 : 302 + bad;
			f.Map()->Override(NDb::FACTION_NEUTRAL, invalid, 2, owner);
			check(!f.Map()->IsOverrideActive(owner) && f.Map()->IsOverrideActive(301)
				&& f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_A, "invalid circle cannot cancel an intersecting valid owner");
		}
		f.Map()->OnStep(1);
		check(f.Map()->IsOverrideActive(301), "rejected steps consume no valid duration");
		f.Map()->OnStep(1);
		check(!f.Map()->IsOverrideActive(301) && f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_B,
			"valid timer recovers and expires after rejected operations");
	}
	{
		NatureOverrideFixtureB13 f(check, 0, 8.5f);
		f.Map()->Override(NDb::FACTION_FREEZE, CCircle(CVec2(20, 15), 100), 1, 401);
		bool all = true; for (int id = 0; id < 20; ++id) all = all && f.Map()->GetNatureInPoint(id) == TERR_A;
		check(all && f.Map()->GetNatureInPoint(20) == TERR_N, "fractional grid boundaries clip hex iteration to actual storage");
		f.World()->Step(1, 1);
		bool restored = true; for (int id = 0; id < 20; ++id) restored = restored && f.Map()->GetNatureInPoint(id) == (id % 5) % 3;
		check(restored, "whole-map override restores every authored boundary cell");
		f.Map()->Override(NDb::FACTION_NEUTRAL, CCircle(CVec2(100, 100), 1), 1, 402);
		check(f.Map()->IsOverrideActive(402) && f.Map()->GetNatureInPoint(7) == TERR_B,
			"fully outside finite circles retain the original registry lifetime without visiting a cell");
		f.World()->Step(1, 1);
		check(!f.Map()->IsOverrideActive(402), "outside-circle lifetime still expires normally");
	}
	for (bool enabled : {false, true})
	{
		NatureOverrideFixtureB13 f(check);
		auto db = f.Descriptor(); auto* edit = const_cast<NDb::ModifyTerrainApplicator*>(db.GetPtr());
		edit->enabled.sString = enabled ? "true" : "false";
		auto app = f.Apply(db);
		check(app && app->IsAlive() == enabled && f.cast->GetActiveApplicatorsCount() == int(enabled),
			"terrain start respects checked outer enabled and balanced activation notifications");
		if (!enabled) continue;
		app->SetBlocked(true);
		edit->faction.sString = "0"; edit->durationTime.sString = "missing";
		f.World()->Step(.5f, .5f);
		check(f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_A && app->IsAlive(),
			"blocking does not retroactively cancel the original start-only terrain operation");
		app->SetBlocked(false); app->Stop();
		check(f.cast->GetActiveApplicatorsCount() == 0 && f.Map()->IsOverrideActive(app->GetObjectId()),
			"explicit applicator Stop leaves the independently world-owned terrain duration intact");
		f.World()->Step(1.5f, 1.5f);
		check(!f.Map()->IsOverrideActive(app->GetObjectId()) && f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_B,
			"stopped or blocked applicators do not leak their independently expiring terrain");
	}
	{
		NatureOverrideFixtureB13 f(check);
		auto* raw = new NDb::Talent; raw->manaCost.sString = "20"; raw->cooldownTime.sString = "5";
		raw->naftaCost = 0; raw->isUnitFreeAfterCast = true; raw->constants = f.abilityDb->constants;
		auto* dispatchDb = new NDb::DispatchByTime; dispatchDb->lifeTime.sString = "-1";
		dispatchDb->persistentApplicators.push_back(f.Descriptor()); raw->dispatch = dispatchDb;
		NDb::Ptr<NDb::Talent> talentDb = raw;
		CObj<PFTalent> talent = new PFTalent(f.hero.GetPtr(), talentDb, 1, 0, 0, false);
		check(talent->Activate(), "terrain talent purchases through real hero activation");
		CObj<PFAbilityInstance> cast = f.hero->UseTalent(talent, Target(CVec3(25, 10, 0)));
		CObj<PFDispatch> dispatch = cast ? B11FindDispatch(f.World(), cast.GetPtr()) : CObj<PFDispatch>();
		check(cast && dispatch && f.hero->GetMana() == 180 && talent->GetCurrentCooldown() == 5
			&& cast->GetActiveApplicatorsCount() == 1 && f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_A,
			"paid cast creates an actual timed dispatch and persistent terrain applicator");
		if (cast && dispatch)
		{
			f.World()->Step(1, 1);
			check(!cast->IsFinished() && cast->GetActiveApplicatorsCount() == 1, "actual unit-owned dispatch keeps a live terrain lifetime");
			f.World()->Step(1, 1);
			check(cast->GetActiveApplicatorsCount() == 0 && f.Map()->GetNatureInPoint(25.f, 10.f) == TERR_B,
				"world timer expiry reaches persistent-applicator cleanup through the unit's actual Step");
			f.World()->Step(.125f, .125f);
			check(cast->IsFinished() && dispatch->IsObjectDead(), "original next-step empty-ring policy completes the paid timed dispatch");
			if (!dispatch->IsObjectDead()) dispatch->Cancel();
		}
	}
}

/** Mutate only the in-memory copy of shipped MOBA cells through an actual applicator. */
static void NatureOverrideAuthoredB13(Checks& check, const char* dataRoot)
{
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false);
	RootFileSystem::RegisterFileSystem(files);
	{
		CObj<Stream> xml = files->OpenFile("Maps/Multiplayer/MOBA/_.TERN.xdb", FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
		check(xml && xml->IsOk(), "shipped terrain override opens its descriptor read-only");
		if (xml && xml->IsOk())
		{
			auto* terrain = new NDb::Terrain; NDb::Ptr<NDb::Terrain> terrainDb = terrain;
			CObj<IXmlSaver> saver = CreateXmlSaver(xml, true);
			NatureQueryDescriptorB12 descriptor = {terrain}; saver->Add("Terrain", &descriptor);
			auto* adv = new NDb::AdvMap; adv->terrain = terrain;
			auto* desc = new NDb::AdvMapDescription; desc->map = adv;
			NDb::Ptr<NDb::AdvMapDescription> description = desc;
			CObj<NCore::IWorldBase> owner = PFWorld::CreatePFWorld();
			auto* world = dynamic_cast<PFWorld*>(owner.GetPtr());
			const bool loaded = world && world->LoadMap(description, 0, NCore::TPlayersStartInfo(), 0, false, PFResourcesCollection::TalentMap());
			check(loaded && !world->GetScene(), "shipped override world uses original authored storage without terrain visuals");
			if (loaded)
			{
				auto* map = world->GetNatureMap(); CVec2 position(0, 0); bool found = false;
				for (int y = 1; y < 160 && !found; ++y) for (int x = 1; x < 160 && !found; ++x)
				{
					position = CVec2((x + (y & 1) * .5f) * 2.5f, y * 2.5f);
					found = map->GetNatureInPoint(position.x, position.y) == TERR_B;
				}
				check(found, "shipped terrain contains an actual authored enemy cell for temporary mutation");
				if (found)
				{
					CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
					hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
					NDb::Ptr<NDb::Ability> abilityDb = new NDb::Ability;
					CObj<PFAbilityData> ability = new PFAbilityData(hero.GetPtr(), abilityDb, NDb::ABILITYTYPEID_SPECIAL, true, false);
					CObj<PFAbilityInstance> cast = new PFAbilityInstance(ability, Target(CVec3(position, 0)), false);
					auto* raw = new NDb::ModifyTerrainApplicator; raw->enabled.sString = "true";
					raw->faction.sString = "sFctn"; raw->durationTime.sString = ".5"; raw->modifRadius.sString = "1";
					NDb::Ptr<NDb::ModifyTerrainApplicator> db = raw;
					const Target target(CVec3(position, 0));
					PFApplCreatePars pars(cast, target); pars.pWorld = world; pars.pDBAppl = db;
					CObj<PFBaseApplicator> app = CreateApplicator(pars);
					check(app && ActivateApplicator(app, cast) && !app->IsAlive() && cast->GetActiveApplicatorsCount() == 0,
						"shipped-grid position applicator follows actual balanced instantaneous activation");
					check(ability->GetNatureTypeInPos(position) == TERR_A && map->IsOverrideActive(app->GetObjectId()),
						"checked source faction modifies the loaded shipped cell without replacing the grid");
					world->Step(.25f, .25f);
					check(ability->GetNatureTypeInPos(position) == TERR_A, "shipped override remains visible through real world advancement");
					world->Step(.25f, .25f);
					check(ability->GetNatureTypeInPos(position) == TERR_B && !map->IsOverrideActive(app->GetObjectId()),
						"shipped override restores its original authored enemy cell at expiry");
					hero->Cleanup();
				}
			}
		}
	}
	RootFileSystem::UnregisterFileSystem(files);
}
