/** Isolate developer switches while testing the original controller rather than a replacement clock. */
struct MapClockSwitchScopeB13
{
	NGlobal::VariantValue cycle = NGlobal::GetVar("day_night_cycle");
	NGlobal::VariantValue twilight = NGlobal::GetVar("day_night_twilight");
	MapClockSwitchScopeB13()
	{
		NGlobal::SetVar("day_night_cycle", NGlobal::VariantValue(true), STORAGE_NONE);
		NGlobal::SetVar("day_night_twilight", NGlobal::VariantValue(false), STORAGE_NONE);
	}
	~MapClockSwitchScopeB13()
	{
		NGlobal::SetVar("day_night_cycle", cycle, STORAGE_NONE);
		NGlobal::SetVar("day_night_twilight", twilight, STORAGE_NONE);
	}
};

/** Supply explicit mock lighting availability; all time/state logic remains in DayNightController. */
static void MapClockConfigureB13(PFWorld* world, const NDb::DayNightSettings& settings, int lights = 3)
{
	auto* map = const_cast<NDb::AdvMap*>(world->GetMapDescription()->map.GetPtr());
	map->dayNightSettings = settings;
	map->lightEnvironment = (lights & 1) ? new NDb::LightEnvironment : nullptr;
	map->nightLightEnvironment = (lights & 2) ? new NDb::LightEnvironment : nullptr;
	world->GetDayNightController()->Initialize();
}

/** Formula state belongs to the actual miscellaneous context, not the receiver or a global world. */
static void MapClockNightQueriesB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	B8BehaviourForeignWorld isolated(check), foreign(check); if (!isolated.world || !foreign.world) return;
	NaftaFixtureB13 f(isolated.world), other(foreign.world);
	NDb::Ptr<NDb::BaseApplicator> db = new NDb::HealApplicator; auto app = f.Create(db);
	for (bool night : {false, true, false})
	{
		isolated.world->GetDayNightController()->Switch(night ? NDb::DAYNIGHTSTATE_NIGHT : NDb::DAYNIGHTSTATE_DAY);
		foreign.world->GetDayNightController()->Switch(night ? NDb::DAYNIGHTSTATE_DAY : NDb::DAYNIGHTSTATE_NIGHT);
		for (const char* expression : {"mIsNight", "pMisc->IsNight()", "appl.IsNight()"})
		{
			const auto appl = LinuxBootstrap::EvaluateApplicatorNumericFormula(expression, *app);
			const auto ability = LinuxBootstrap::EvaluateUnitNumericFormula(expression, other.sender, other.receiver, f.ability);
			check(appl.Succeeded() && appl.value == int(night) && ability.Succeeded() && ability.value == int(night),
				"B13 night query reads its actual applicator/ability world even with foreign unit endpoints and a cached formula");
		}
		const auto condition = LinuxBootstrap::EvaluateApplicatorNumericFormula("!(mIsNight)", *app);
		check(condition.Succeeded() && condition.value == int(!night), "B13 original day condition negates the live night state");
	}
	for (const char* expression : {"mIsNight", "pMisc->IsNight()", "appl.IsNight()"})
	{
		const auto absent = LinuxBootstrap::EvaluateUnitNumericFormula(expression, f.sender, f.receiver);
		const auto lazy = LinuxBootstrap::EvaluateUnitNumericFormula(std::string("true ? 7 : ") + expression, f.sender, f.receiver);
		check(!absent.Succeeded() && lazy.Succeeded() && lazy.value == 7,
			"B13 absent miscellaneous context cannot silently become daytime, while an inactive valid query stays lazy");
	}
	check(!LinuxBootstrap::EvaluateApplicatorNumericFormula("mIsNightExtra", *app).Succeeded(),
		"B13 night symbol matching cannot resolve an identifier prefix");
	for (const char* expression : {"pMisc->IsNight(1)", "pMisc->IsNight()()", "appl.IsNight(0)",
		"pMisc->SetNight(true)", "receiver.IsNight()", "mObjectParent->IsNight()", "pMisc->GetIsNight()"})
	{
		check(!LinuxBootstrap::EvaluateApplicatorNumericFormula(expression, *app).Succeeded(),
			"B13 night query admits no arbitrary methods, arguments, aliases or parent traversal");
		check(!LinuxBootstrap::EvaluateApplicatorNumericFormula(std::string("true ? 1 : ") + expression, *app).Succeeded(),
			"B13 invalid night syntax cannot hide in an inactive expression");
	}
	NRandom::RandomGenerator expected(*isolated.world->GetRndGen()); const int draw = expected.Next(1, 9);
	const auto accepted = LinuxBootstrap::EvaluateApplicatorNumericFormula("mIsNight ? cMissing : random(1,9)", *app);
	check(accepted.Succeeded() && accepted.value == draw && GlyphRngEqualB13(*isolated.world->GetRndGen(), expected),
		"B13 valid night condition commits only the selected random branch");
	expected = *isolated.world->GetRndGen();
	const auto rejected = LinuxBootstrap::EvaluateApplicatorNumericFormula("random(1,9)+pMisc->IsNight(1)", *app);
	check(!rejected.Succeeded() && GlyphRngEqualB13(*isolated.world->GetRndGen(), expected),
		"B13 invalid night query cannot commit a preceding random draw");
	std::printf("B13 night formula queries checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Preserve original lighting eligibility, initial fraction, developer pause and world tick ordering. */
static void MapClockCycleB13(Checks& check)
{
	const int before = check.count, failed = check.failed; MapClockSwitchScopeB13 switches;
	for (int lights : {0, 1, 2, 3}) for (bool night : {false, true})
	{
		B8BehaviourForeignWorld f(check); if (!f.world) continue;
		NDb::DayNightSettings settings; settings.dayDuration = 2; settings.nightDuration = 1;
		settings.initialState = night ? NDb::DAYNIGHTSTATE_NIGHT : NDb::DAYNIGHTSTATE_DAY;
		settings.initialStateFraction = .25f; settings.transitionDuration = 1;
		MapClockConfigureB13(f.world, settings, lights);
		auto* clock = f.world->GetDayNightController().GetPtr();
		check(f.world->IsNight() == night && clock->GetCurrentStateFraction() == .25f,
			"B13 authored initial state/fraction is preserved independently of rendering availability");
		const float elapsed = f.world->GetTimeElapsed();
		for (float dt : {0.f, -1.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
		{
			f.world->Step(dt, dt);
			check(f.world->GetTimeElapsed() == elapsed && f.world->IsNight() == night && clock->GetCurrentStateFraction() == .25f,
				"B13 zero/invalid world steps cannot advance the day/night controller");
		}
		const float dt = (night ? settings.nightDuration : settings.dayDuration) * .25f;
		for (int i = 0; i < 3; ++i) f.world->Step(dt, dt);
		check(f.world->IsNight() == (lights == 3 ? !night : night),
			"B13 automatic native cycle crosses the authored boundary only when both light environments exist");
		check(clock->GetCurrentStateFraction() == (lights == 3 ? 0.f : .25f),
			"B13 native automatic cycle preserves the original boundary remainder and disabled-cycle fraction");
		if (lights == 3)
		{
			const bool stoppedNight = f.world->IsNight();
			NGlobal::SetVar("day_night_cycle", NGlobal::VariantValue(false), STORAGE_NONE);
			f.world->Step(3, 3);
			check(f.world->IsNight() == stoppedNight && clock->GetCurrentStateFraction() == 0,
				"B13 original developer cycle pause freezes simulation phase without stopping world ticks");
			NGlobal::SetVar("day_night_cycle", NGlobal::VariantValue(true), STORAGE_NONE);
			const float duration = stoppedNight ? settings.nightDuration : settings.dayDuration;
			f.world->Step(duration, duration);
			check(f.world->IsNight() == night && clock->GetCurrentStateFraction() == 0,
				"B13 resuming the original cycle completes the opposite phase without resetting its clock");
		}
	}
	{
		B8BehaviourForeignWorld f(check); if (f.world)
		{
			NDb::DayNightSettings settings; settings.dayDuration = settings.nightDuration = 1;
			MapClockConfigureB13(f.world, settings); NaftaFixtureB13 fixture(f.world);
			auto* processor = TriggerConditionFixtureB13::Condition(1 << NDb::BASEUNITEVENT_APPLICATORSTEP);
			auto* effect = const_cast<NDb::PermanentStatModApplicator*>(
				dynamic_cast<const NDb::PermanentStatModApplicator*>(processor->spell->applicators[0].GetPtr()));
			effect->modifier.variable = "ObservedNight";
			effect->modifier.multValue.sString = "0"; effect->modifier.addValue.sString = "mIsNight";
			NDb::Ptr<NDb::TriggerApplicator> db = TriggerConditionFixtureB13::Descriptor(processor);
			auto app = fixture.Create(db); check(app && fixture.Activate(app), "B13 actual step-event trigger observes simulation phase ordering");
			for (float dt : {.5f, .5f, .25f, .75f, .25f})
			{
				const bool previousNight = f.world->IsNight(); f.world->Step(dt, dt);
				check(fixture.receiver->GetVariable("ObservedNight") == int(previousNight),
					"B13 original Windows order updates night state after gameplay objects, so boundary effects observe the preceding phase");
			}
		}
	}
	{
		B8BehaviourForeignWorld f(check); if (f.world)
		{
			NGlobal::SetVar("day_night_twilight", NGlobal::VariantValue(true), STORAGE_NONE);
			NDb::DayNightSettings settings; settings.dayDuration = settings.nightDuration = 2;
			settings.twilightDuration = 1; settings.twilightFraction = .5f;
			settings.transitionDuration = 1; settings.transitionThreshold = .7f; settings.initialStateFraction = .9375f;
			MapClockConfigureB13(f.world, settings);
			NaftaFixtureB13 fixture(f.world); NDb::Ptr<NDb::BaseApplicator> db = new NDb::HealApplicator;
			auto app = fixture.Create(db); auto* clock = f.world->GetDayNightController().GetPtr();
			check(!f.world->IsNight() && clock->GetNightFraction() == .5f,
				"B13 original dusk keeps daytime gameplay state despite blended night lighting");
			f.world->Step(.125f, .125f);
			const auto midnight = LinuxBootstrap::EvaluateApplicatorNumericFormula("mIsNight", *app);
			check(f.world->IsNight() && clock->GetNightFraction() == .625f && midnight.Succeeded() && midnight.value == 1,
				"B13 night formulas use logical midnight before lighting reaches the transition threshold");
			settings.initialState = NDb::DAYNIGHTSTATE_NIGHT; settings.initialStateFraction = .75f;
			MapClockConfigureB13(f.world, settings);
			const auto dawn = LinuxBootstrap::EvaluateApplicatorNumericFormula("mIsNight", *app);
			check(f.world->IsNight() && clock->GetNightFraction() == .5f && dawn.Succeeded() && dawn.value == 1,
				"B13 original dawn remains logically night independently of the blended lighting fraction");
		}
	}
	std::printf("B13 native day night cycle checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Original main-building passives drive the clock used by actual rune pickups, without injected values. */
static void MapClockAuthoredB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed; MapClockSwitchScopeB13 switches;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		for (bool aram : {false, true}) for (bool burn : {false, true})
		{
			const std::string directory = std::string("/Maps/Multiplayer/") + (aram ? "ARAM" : "MOBA");
			const std::string path = directory + "/Mains/_" + (burn ? "B" : "A") + ".MBLD.xdb";
			std::printf("B13 natural map clock loading %s\n", path.c_str());
			const auto db = NDb::Get<NDb::MainBuilding>(NDb::DBID(path.c_str()));
			const auto glyphDb = NDb::Get<NDb::Glyph>(NDb::DBID(aram ? "/Glyphs/PrimeRune_ARAM.GLPH.xdb" : "/Glyphs/PrimeRune.GLPH.xdb"));
			check(db && db->abilities.size() >= 3 && glyphDb, "B13 unchanged original main building and prime rune load with their passive graphs");
			if (!db || !glyphDb) continue;
			B8BehaviourForeignWorld f(check); if (!f.world) continue;
			SalarySettingsB13(f.world, 60, 0);
			CObj<Stream> xml = files->OpenFile((directory + "/_.ADMP.xdb").c_str(), FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
			check(xml && xml->IsOk(), "B13 original map clock settings open read-only"); if (!xml || !xml->IsOk()) continue;
			CObj<IXmlSaver> saver = CreateXmlSaver(xml, true); NDb::DayNightSettings settings;
			const bool opened = saver->StartChunk("AdvMap", 0);
			check(opened, "B13 typed serializer enters the original map settings"); if (!opened) continue;
			saver->Add("dayNightSettings", &settings); saver->FinishChunk();
			MapClockConfigureB13(f.world, settings);
			const auto faction = burn ? NDb::FACTION_BURN : NDb::FACTION_FREEZE;
			auto hero = B9MaleHero(f.world, faction), ally = B9MaleHero(f.world, faction);
			auto enemy = B9MaleHero(f.world, burn ? NDb::FACTION_FREEZE : NDb::FACTION_BURN);
			for (auto* unit : {hero.GetPtr(), ally.GetPtr(), enemy.GetPtr()})
			{
				unit->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
				unit->GetStat(NDb::STAT_VISIBILITYRANGE)->SetCoreValue(15);
			}
			NDb::AdvMapObject object; object.gameObject = db;
			object.offset = CPlacement(CVec3(20, 15, 0), QNULL, CVec3(1, 1, 1));
			CObj<PFMainBuilding> base = new PFMainBuilding(f.world, object);
			base->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET | NDb::UNITFLAG_FORBIDATTACK); base->DropStates();
			check(base->GetVariable("GameDuration") == 0, "B13 main-building constructors do not advance or duplicate the authored clock");
			float expectedClock = 0; bool pendingClockDispatch = false;
			const auto step = [&](int count, float dt) {
				for (int i = 0; i < count; ++i)
				{
					f.world->Step(dt, dt);
					// The shipped counter uses instantApply=false, so the first pulse arrives next tick.
					if (pendingClockDispatch) expectedClock += .1f;
					pendingClockDispatch = true;
				}
			};
			step(10, .25f);
			std::printf("B13 natural map clock first aram=%d burn=%d clock=%g expected=%g\n", aram, burn, base->GetVariable("GameDuration"), expectedClock);
			check(base->GetVariable("GameDuration") == expectedClock && f.world->GetTimeElapsed() == 2.5f,
				"B13 original counter advances once per applicator step, not by variable world delta or constructor count");
			check(f.world->IsNight() == aram && (aram || base->GetVariable("DayDuration") > 0),
				"B13 original MOBA starts its first-day counter while ARAM preserves its authored initial night");
			for (int period = 0; period < 2; ++period)
			{
				if (period) step((aram ? 900 : 3000) + 5, .1f);
				const float oldClock = base->GetVariable("GameDuration"); const int oldGold = hero->GetGold();
				CObj<PFGlyph> glyph = new PFGlyph(f.world, glyphDb, CVec3(5, 5, 0));
				check(glyph->PickUp(hero.GetPtr()), "B13 real rune pickup admits against the naturally clocked allied main building");
				step(8, .1f);
				const float copied = hero->GetVariable("GameDuration");
				const int award = (aram ? 100 : 70) + period * (aram ? 20 : 30);
				std::printf("B13 natural rune aram=%d burn=%d period=%d clock=%g expected=%g copied=%g gold=%d/%d/%d\n",
					aram, burn, period, base->GetVariable("GameDuration"), expectedClock, copied, hero->GetGold(), ally->GetGold(), enemy->GetGold());
				check(base->GetVariable("GameDuration") == expectedClock && copied >= oldClock && copied <= expectedClock,
					"B13 original rune copies the live building clock through its real selector and deferred graph");
				check(hero->GetGold() == oldGold + award && ally->GetGold() == hero->GetGold() && enemy->GetGold() == 0,
					"B13 naturally scaled MOBA/ARAM rune awards reach only the allied heroes at each tested period");
			}
			if (!aram)
			{
				const auto vision = [&](float range) {
					std::printf("B13 natural vision burn=%d night=%d ranges=%g/%g/%g expected=%g\n", burn, f.world->IsNight(),
						hero->GetVisibilityRange(), ally->GetVisibilityRange(), enemy->GetVisibilityRange(), range);
					check(hero->GetVisibilityRange() == range && ally->GetVisibilityRange() == range && enemy->GetVisibilityRange() == range,
						"B13 natural map transitions broadcast original sight-range changes to both factions without an injected vision cast");
				};
				vision(burn ? 15 : 12);
				std::printf("B13 first natural night burn=%d state=%d day=%g night=%g\n", burn,
					f.world->IsNight(), base->GetVariable("DayDuration"), base->GetVariable("NightDuration"));
				// The first transition resets immediately; the last deferred first-day pulse arrives afterward.
				check(f.world->IsNight() && base->GetVariable("DayDuration") == .1f && base->GetVariable("NightDuration") > 0,
					"B13 original first-night graph retains its final deferred day pulse and starts the authored night counter");
				step(1805, .1f);
				// The original dawn program increases range gradually over two one-second delays.
				step(25, .1f); vision(15);
				check(!f.world->IsNight() && base->GetVariable("NightDuration") == 0 && base->GetVariable("DayDuration") > 0,
					"B13 original MOBA night-to-day graph replaces the counter through deferred child dispatch");
				step(3005, .1f);
				vision(12);
				check(f.world->IsNight() && base->GetVariable("DayDuration") == 0 && base->GetVariable("NightDuration") > 0,
					"B13 original recurring transition graph reaches its second night without injected timer values");
				const float oldNight = base->GetVariable("NightDuration"); step(10, .1f);
				check(std::fabs(base->GetVariable("NightDuration") - oldNight - 1.f) < .0001f
					&& base->GetVariable("GameDuration") == expectedClock,
					"B13 repeated authored transition graphs retain one counter pulse per tick without accumulating duplicates");
			}
			hero->Cleanup(); ally->Cleanup(); enemy->Cleanup(); base->Cleanup();
		}
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 natural authored map clocks checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
