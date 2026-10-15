#include "../PF_GameLogic/PFGlyphManager.h"

/** Compare subsequent generator outputs without consuming either caller's state. */
static bool GlyphRngEqualB13(NRandom::RandomGenerator actual, NRandom::RandomGenerator expected)
{
	for (int i = 0; i < 4; ++i) if (actual.Next() != expected.Next()) return false;
	return true;
}

/** Real map loading must initialize shared glyph settings before constructing authored spawners. */
struct GlyphSpawnWorldB13
{
	CObj<NCore::IWorldBase> owner;
	PFWorld* world = nullptr;
	GlyphSpawnWorldB13(Checks& check, const NDb::GlyphManagerParams* params, const NDb::GlyphSpawner* spawner = nullptr,
		bool inherited = false, int spawners = 1)
	{
		auto* terrain = new NDb::Terrain; terrain->elemXCount = 4; terrain->elemYCount = 3; terrain->tilesPerElement = 4;
		auto* settings = new NDb::AdvMapSettings; settings->overrideGlyphSettings = params;
		auto* map = new NDb::AdvMap; map->terrain = terrain;
		if (spawner) for (int i = 0; i < spawners; ++i)
		{
			NDb::AdvMapObject object; object.gameObject = spawner;
			object.offset = CPlacement(CVec3(5 + 5 * i, 5, 0), QNULL, CVec3(1, 1, 1)); map->objects.push_back(object);
		}
		auto* description = new NDb::AdvMapDescription; description->map = map;
		if (inherited) map->mapSettings = settings; else description->mapSettings = settings;
		NDb::Ptr<NDb::AdvMapDescription> db = description;
		owner = PFWorld::CreatePFWorld(); world = dynamic_cast<PFWorld*>(owner.GetPtr());
		const bool loaded = world && world->LoadMap(db, 0, NCore::TPlayersStartInfo(), 0, false, PFResourcesCollection::TalentMap());
		check(loaded, "B13 glyph world loads actual map descriptors and spawner objects"); if (!loaded) world = nullptr;
	}
	/** Inspect actual scheduled objects, retaining identities safely across pickup and removal. */
	template<class T> std::vector<CObj<T>> Objects()
	{
		std::vector<CObj<T>> result; if (!world) return result;
		world->ProcessAddRemove();
		auto collect = [&](PF_Core::WorldObjectBase& object) {
			if (auto* typed = dynamic_cast<T*>(&object)) if (!typed->IsObjectDead()) result.emplace_back(typed);
		};
		world->ForAllObjectsBeingStepped(collect); return result;
	}
};

/** Finite deterministic delays make constructor timing and post-pickup scheduling observable. */
static NDb::GlyphManagerParams* GlyphSpawnSettingsB13(const NDb::GlyphsDB* table)
{
	auto* params = new NDb::GlyphManagerParams; params->glyphs = table;
	params->settings.minNormalSpawnDelay = params->settings.maxNormalSpawnDelay = 1;
	params->settings.minAfterPickupSpawnDelay = params->settings.maxAfterPickupSpawnDelay = 2;
	params->settings.spawnAttemptDelayDecrease = .5f;
	params->settings.maxInstances = 2; params->settings.spawnMode = NDb::GLYPHSPAWNMODE_ORDERED;
	return params;
}

/** Exercise shared pool limits, original weighted draws and released slots through real pickups. */
static void GlyphManagerB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (bool inherited : {false, true}) for (int seed = 0; seed < 8; ++seed)
	{
		GlyphRootB13 root; auto* table = const_cast<NDb::GlyphsDB*>(root.glyphs.GetPtr());
		table->glyphs[0].weight = 1; table->glyphs[1].weight = 3;
		auto* raw = GlyphSpawnSettingsB13(table); NDb::Ptr<NDb::GlyphManagerParams> params = raw;
		raw->settings.spawnMode = seed & 1 ? NDb::GLYPHSPAWNMODE_RANDOMWEIGHT : NDb::GLYPHSPAWNMODE_ORDERED;
		GlyphSpawnWorldB13 f(check, params, nullptr, inherited); if (!f.world) continue;
		auto manager = f.world->GetAIWorld()->GetGlyphsManager();
		check(IsValid(manager) && manager->GetGlyphsDB() == table,
			"B13 world owns the shared manager initialized from direct or inherited map glyph settings");
		if (!IsValid(manager)) continue;
		f.world->GetRndGen()->SetSeed(seed); NRandom::RandomGenerator expectedRng(*f.world->GetRndGen());
		const int index = seed & 1 ? (expectedRng.Next(0, 3) < 1 ? 0 : 1) : 0;
		CObj<PFGlyph> first = manager->CreateGlyph(CVec3(5, 5, 0)).GetPtr();
		check(first && first->GetDBDesc() == table->glyphs[index].glyph && GlyphRngEqualB13(*f.world->GetRndGen(), expectedRng),
			"B13 manager preserves original weighted or ordered selection and exact RNG consumption");
		if (seed & 1) expectedRng.Next(0, table->glyphs[1 - index].weight - 1);
		CObj<PFGlyph> second = manager->CreateGlyph(CVec3(10, 5, 0)).GetPtr();
		check(second && second->GetDBDesc() == table->glyphs[1 - index].glyph && GlyphRngEqualB13(*f.world->GetRndGen(), expectedRng),
			"B13 shared weighted candidates exclude occupied slots and ordered cursor advances");
		check(!manager->CreateGlyph(CVec3(15, 5, 0)) && GlyphRngEqualB13(*f.world->GetRndGen(), expectedRng),
			"B13 full shared pool rejects another spawn without advancing its random stream");
		auto hero = CommandHeroB10(f.world); hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		check(first && first->PickUp(hero.GetPtr()) && first->IsObjectDead(), "B13 actual pickup releases a shared slot even while a test retains its dead object");
		if (seed & 1) expectedRng.Next(0, table->glyphs[index].weight - 1);
		CObj<PFGlyph> replacement = manager->CreateGlyph(CVec3(5, 5, 0)).GetPtr();
		check(replacement && replacement.GetPtr() != first.GetPtr() && replacement->GetDBDesc() == table->glyphs[index].glyph
			&& GlyphRngEqualB13(*f.world->GetRndGen(), expectedRng), "B13 released slot gets a new real glyph with original random accounting");
		if (!(seed & 1) && replacement && second)
		{
			replacement->PickUp(hero.GetPtr());
			check(!manager->CreateGlyph(CVec3(5, 5, 0)) && !second->IsObjectDead(),
				"B13 ordered cursor cannot overwrite a still-occupied slot while a different slot is empty");
			second->PickUp(hero.GetPtr()); CObj<PFGlyph> resumed = manager->CreateGlyph(CVec3(5, 5, 0)).GetPtr();
			check(resumed && resumed->GetDBDesc() == table->glyphs[1].glyph && GlyphRngEqualB13(*f.world->GetRndGen(), expectedRng),
				"B13 freeing the pending ordered slot resumes the same cursor without RNG or duplicate accounting");
		}
		hero->Cleanup();
	}
	for (int invalid = 0; invalid < 8; ++invalid)
	{
		GlyphRootB13 root; auto* table = const_cast<NDb::GlyphsDB*>(root.glyphs.GetPtr());
		auto* raw = GlyphSpawnSettingsB13(table); NDb::Ptr<NDb::GlyphManagerParams> params = raw;
		raw->settings.spawnMode = NDb::GLYPHSPAWNMODE_RANDOMWEIGHT;
		table->glyphs[0].weight = 1; table->glyphs[1].weight = 1;
		if (invalid == 0) raw->glyphs = nullptr;
		if (invalid == 1) table->glyphs.clear();
		if (invalid == 2) table->glyphs[0].glyph = nullptr;
		if (invalid == 3) table->glyphs[0].weight = -1;
		if (invalid == 4) table->glyphs[0].weight = table->glyphs[1].weight = 0;
		if (invalid == 5) table->glyphs[0].weight = table->glyphs[1].weight = std::numeric_limits<int>::max();
		if (invalid == 6) raw->settings.maxInstances = 0;
		if (invalid == 7) raw->settings.spawnMode = static_cast<NDb::EGlyphSpawnMode>(7);
		GlyphSpawnWorldB13 f(check, params); if (!f.world) continue;
		auto manager = f.world->GetAIWorld()->GetGlyphsManager();
		check(IsValid(manager), "B13 malformed glyph data still has a safely owned world manager"); if (!IsValid(manager)) continue;
		const NRandom::RandomGenerator beforeRng(*f.world->GetRndGen());
		check(!manager->CreateGlyph(CVec3(5, 5, 0)) && f.Objects<PFGlyph>().empty() && GlyphRngEqualB13(*f.world->GetRndGen(), beforeRng),
			"B13 invalid authoritative tables, weights, sums, capacity and modes cannot spawn or consume RNG");
	}
	{
		GlyphRootB13 root; GlyphSpawnWorldB13 f(check, nullptr);
		if (f.world)
		{
			auto manager = f.world->GetAIWorld()->GetGlyphsManager();
			check(IsValid(manager) && manager->GetGlyphsDB() == root.glyphs.GetPtr(), "B13 absent map override retains the session glyph table");
			if (IsValid(manager)) for (const CVec3& position : {CVec3(-1, 5, 0), CVec3(40, 5, 0), CVec3(5, 30, 0),
				CVec3(std::numeric_limits<float>::quiet_NaN(), 5, 0), CVec3(5, std::numeric_limits<float>::infinity(), 0),
				CVec3(5, 5, std::numeric_limits<float>::infinity()), CVec3(1e30f, 5, 0)})
			{
				const NRandom::RandomGenerator expectedRng(*f.world->GetRndGen());
				check(!manager->CreateGlyph(position) && f.Objects<PFGlyph>().empty() && GlyphRngEqualB13(*f.world->GetRndGen(), expectedRng),
					"B13 invalid spawn geometry cannot reach tile/voxel conversions or consume a selection draw");
			}
		}
	}
	std::printf("B13 glyph manager checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Actual world ticks own initial delay, post-pickup delay and spawner visibility inheritance. */
static void GlyphSpawnerB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (bool managed : {false, true}) for (bool inherited : {false, true}) for (bool hidden : {false, true})
	{
		GlyphRootB13 root; NDb::Ptr<NDb::GlyphManagerParams> params = GlyphSpawnSettingsB13(root.glyphs);
		auto* raw = new NDb::GlyphSpawner; raw->useGlyphManager = managed;
		raw->glyphs = managed || !inherited ? root.glyphs.GetPtr() : nullptr; raw->settings = params->settings;
		NDb::Ptr<NDb::GlyphSpawner> spawnerDb = raw;
		GlyphSpawnWorldB13 f(check, params, spawnerDb, inherited); if (!f.world) continue;
		auto spawners = f.Objects<PFGlyphSpawner>(); check(spawners.size() == 1, "B13 original map load creates the real glyph spawner");
		if (spawners.empty()) continue;
		spawners[0]->Hide(hidden);
		check(f.Objects<PFGlyph>().empty(), "B13 spawner constructor schedules the authored initial delay rather than spawning immediately");
		f.world->Step(.5f, .5f); check(f.Objects<PFGlyph>().empty(), "B13 no glyph appears before its initial deadline");
		f.world->Step(.5f, .5f); auto glyphs = f.Objects<PFGlyph>();
		check(glyphs.size() == 1 && glyphs[0]->GetDBDesc() == root.glyphs->glyphs[0].glyph,
			"B13 local and managed spawners create the first authored glyph at the initial deadline");
		if (glyphs.empty()) continue;
		auto hero = CommandHeroB10(f.world); hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		check(glyphs[0]->GetHiddenCounter() == int(hidden), "B13 newly spawned glyph inherits its spawner's hidden state");
		spawners[0]->Hide(false); check(glyphs[0]->PickUp(hero.GetPtr()), "B13 visible spawned glyph follows the real pickup path");
		f.world->Step(.5f, .5f); f.world->Step(1.5f, 1.5f);
		check(f.Objects<PFGlyph>().empty(), "B13 first post-pickup tick schedules a fresh delay without decrementing it");
		f.world->Step(.5f, .5f); auto next = f.Objects<PFGlyph>();
		check(next.size() == 1 && next[0].GetPtr() != glyphs[0].GetPtr() && next[0]->GetDBDesc() == root.glyphs->glyphs[1].glyph,
			"B13 actual pickup is followed by the next authored glyph at the post-pickup deadline");
		hero->Cleanup();
	}
	std::printf("B13 glyph spawner checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Fractional authored delays retain the original integer RNG, while retry clamps stay float. */
static void GlyphSpawnerTimersB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (int seed = 0; seed < 8; ++seed)
	{
		GlyphRootB13 root; auto* raw = GlyphSpawnSettingsB13(root.glyphs); NDb::Ptr<NDb::GlyphManagerParams> params = raw;
		raw->settings.minNormalSpawnDelay = 1.75f; raw->settings.maxNormalSpawnDelay = 3.75f;
		raw->settings.minAfterPickupSpawnDelay = 2.75f; raw->settings.maxAfterPickupSpawnDelay = 4.75f;
		GlyphSpawnWorldB13 f(check, params); if (!f.world) continue;
		f.world->GetRndGen()->SetSeed(seed); NRandom::RandomGenerator expectedRng(*f.world->GetRndGen());
		const float initial = expectedRng.Next(1, 3);
		auto* db = new NDb::GlyphSpawner; db->useGlyphManager = true;
		NDb::AdvMapObject object; object.gameObject = db; object.offset = CPlacement(CVec3(5, 5, 0), QNULL, CVec3(1, 1, 1));
		CObj<PFGlyphSpawner> spawner = new PFGlyphSpawner(f.world, object);
		auto* scheduled = static_cast<PFWorldObjectBase*>(spawner.GetPtr());
		check(GlyphRngEqualB13(*f.world->GetRndGen(), expectedRng), "B13 initial fractional delay consumes exactly the original truncated integer draw");
		for (float bad : {-1.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) scheduled->Step(bad);
		scheduled->Step(initial - .25f); check(f.Objects<PFGlyph>().empty(), "B13 invalid elapsed time leaves the pending initial deadline unchanged");
		scheduled->Step(.25f); auto glyphs = f.Objects<PFGlyph>();
		check(glyphs.size() == 1 && !glyphs[0]->IsMapObject(), "B13 initial integer deadline creates a runtime pickup, not a baked map object");
		if (glyphs.empty()) continue;
		auto hero = CommandHeroB10(f.world); hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		check(glyphs[0]->PickUp(hero.GetPtr()), "B13 timer fixture consumes the actual spawned object");
		const float afterPickup = expectedRng.Next(2, 4); scheduled->Step(.5f);
		check(GlyphRngEqualB13(*f.world->GetRndGen(), expectedRng), "B13 post-pickup delay uses its separate authored integer interval");
		scheduled->Step(afterPickup - .25f); check(f.Objects<PFGlyph>().empty(), "B13 pickup scheduling tick does not shorten the new delay");
		scheduled->Step(.25f); check(f.Objects<PFGlyph>().size() == 1, "B13 respawn occurs at the independently sampled post-pickup deadline");
		hero->Cleanup();
	}
	for (int invalid = 0; invalid < 8; ++invalid)
	{
		GlyphRootB13 root; auto* raw = GlyphSpawnSettingsB13(root.glyphs); NDb::Ptr<NDb::GlyphManagerParams> params = raw;
		if (invalid == 0) raw->settings.minNormalSpawnDelay = -1;
		if (invalid == 1) raw->settings.maxNormalSpawnDelay = std::numeric_limits<float>::quiet_NaN();
		if (invalid == 2) raw->settings.maxNormalSpawnDelay = 2147483648.f;
		if (invalid == 3) raw->settings.minNormalSpawnDelay = 3;
		if (invalid == 4) raw->settings.minAfterPickupSpawnDelay = -1;
		if (invalid == 5) raw->settings.maxAfterPickupSpawnDelay = std::numeric_limits<float>::infinity();
		if (invalid == 6) raw->settings.spawnAttemptDelayDecrease = -1;
		if (invalid == 7) raw->settings.spawnMode = static_cast<NDb::EGlyphSpawnMode>(7);
		GlyphSpawnWorldB13 f(check, params); if (!f.world) continue;
		const NRandom::RandomGenerator beforeRng(*f.world->GetRndGen());
		auto* db = new NDb::GlyphSpawner; db->useGlyphManager = true;
		NDb::AdvMapObject object; object.gameObject = db; object.offset = CPlacement(CVec3(5, 5, 0), QNULL, CVec3(1, 1, 1));
		CObj<PFGlyphSpawner> spawner = new PFGlyphSpawner(f.world, object);
		static_cast<PFWorldObjectBase*>(spawner.GetPtr())->Step(10);
		check(f.Objects<PFGlyph>().empty() && GlyphRngEqualB13(*f.world->GetRndGen(), beforeRng),
			"B13 invalid delay ranges, nonfinite/overflow conversions and invalid modes cannot mutate RNG or spawn");
	}
	for (int seed = 0; seed < 8; ++seed)
	{
		GlyphRootB13 root; auto* raw = GlyphSpawnSettingsB13(root.glyphs); NDb::Ptr<NDb::GlyphManagerParams> params = raw;
		raw->settings.minNormalSpawnDelay = 3.75f; raw->settings.maxNormalSpawnDelay = 5.75f;
		raw->settings.maxInstances = 0;
		GlyphSpawnWorldB13 f(check, params); if (!f.world) continue;
		f.world->GetRndGen()->SetSeed(seed); NRandom::RandomGenerator expectedRng(*f.world->GetRndGen());
		const float initial = expectedRng.Next(3, 5);
		auto* db = new NDb::GlyphSpawner; NDb::AdvMapObject object; object.gameObject = db;
		object.offset = CPlacement(CVec3(5, 5, 0), QNULL, CVec3(1, 1, 1));
		CObj<PFGlyphSpawner> spawner = new PFGlyphSpawner(f.world, object);
		auto* scheduled = static_cast<PFWorldObjectBase*>(spawner.GetPtr()); scheduled->Step(initial);
		const float retry = Max(3.75f, expectedRng.Next(3, 5) - .5f);
		check(f.Objects<PFGlyph>().empty() && GlyphRngEqualB13(*f.world->GetRndGen(), expectedRng),
			"B13 rejected shared spawn draws the original retry interval and applies its float decrease/clamp");
		raw->settings.maxInstances = 2;
		auto manager = f.world->GetAIWorld()->GetGlyphsManager(); if (IsValid(manager)) manager->Init(params);
		scheduled->Step(retry - .25f); check(f.Objects<PFGlyph>().empty(), "B13 restored capacity does not bypass the pending retry delay");
		scheduled->Step(.25f); check(f.Objects<PFGlyph>().size() == 1 && GlyphRngEqualB13(*f.world->GetRndGen(), expectedRng),
			"B13 retry spawns at the float-clamped deadline without an extra scheduling draw");
	}
	{
		GlyphRootB13 root; auto* raw = GlyphSpawnSettingsB13(root.glyphs); raw->settings.maxInstances = 1;
		NDb::Ptr<NDb::GlyphManagerParams> params = raw; NDb::Ptr<NDb::GlyphSpawner> db = new NDb::GlyphSpawner;
		GlyphSpawnWorldB13 f(check, params, db, false, 2);
		if (f.world)
		{
			f.world->Step(1, 1); auto first = f.Objects<PFGlyph>();
			check(first.size() == 1, "B13 two actual map spawners obey one shared capacity limit");
			auto hero = CommandHeroB10(f.world); hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
			if (!first.empty()) first[0]->PickUp(hero.GetPtr());
			f.world->Step(1, 1); auto second = f.Objects<PFGlyph>();
			check(second.size() == 1 && second[0]->GetPosition().x == 10 && second[0]->GetDBDesc() == root.glyphs->glyphs[1].glyph,
				"B13 a waiting peer spawner can reuse released shared capacity on its own retry deadline");
			hero->Cleanup();
		}
	}
	std::printf("B13 glyph spawn timing checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Preserve original MOBA's 60-second first spawn and 300-second post-pickup delay without editing assets. */
static void GlyphSpawnerAuthoredB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		GlyphRootB13 root;
		const auto params = NDb::Get<NDb::GlyphManagerParams>(NDb::DBID("/Maps/Multiplayer/MOBA/_GlyphSettings.xdb"));
		check(params && params->glyphs && params->glyphs->glyphs.size() == 5, "B13 original MOBA glyph table loads read-only");
		if (params && params->glyphs && !params->glyphs->glyphs.empty())
		{
			NDb::Ptr<NDb::GlyphSpawner> db = new NDb::GlyphSpawner; GlyphSpawnWorldB13 f(check, params, db);
			if (f.world)
			{
				for (int i = 0; i < 239; ++i) f.world->Step(.25f, .25f);
				check(f.Objects<PFGlyph>().empty(), "B13 shipped MOBA settings do not spawn before sixty seconds");
				f.world->Step(.25f, .25f); auto glyphs = f.Objects<PFGlyph>();
				check(glyphs.size() == 1 && glyphs[0]->GetDBDesc() == params->glyphs->glyphs[0].glyph,
					"B13 original weighted table spawns its positive-weight PrimeRune entry");
				auto manager = f.world->GetAIWorld()->GetGlyphsManager();
				const NRandom::RandomGenerator beforeRng(*f.world->GetRndGen());
				check(IsValid(manager) && !manager->CreateGlyph(CVec3(10, 5, 0))
					&& GlyphRngEqualB13(*f.world->GetRndGen(), beforeRng) && f.Objects<PFGlyph>().size() == 1,
					"B13 occupied positive-weight entry with only zero-weight peers never enters the original helper's dangling default return");
				auto hero = CommandHeroB10(f.world); hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
				check(!glyphs.empty() && glyphs[0]->PickUp(hero.GetPtr()), "B13 actual authored rune is picked up through the original ability path");
				f.world->Step(.25f, .25f);
				for (int i = 0; i < 1199; ++i) f.world->Step(.25f, .25f);
				check(f.Objects<PFGlyph>().empty(), "B13 original post-pickup delay remains three hundred seconds");
				f.world->Step(.25f, .25f); auto replacement = f.Objects<PFGlyph>();
				check(replacement.size() == 1 && replacement[0]->GetDBDesc() == params->glyphs->glyphs[0].glyph,
					"B13 original MOBA rune respawns with zero-weight alternatives still excluded");
				hero->Cleanup();
			}
		}
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 authored glyph spawn checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
