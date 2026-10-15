/** Minimal real storage isolates admission from the authored status/program chain. */
struct GlyphCaptureFixtureB13 : GlyphUpgradeFixtureB13
{
	NDb::Ptr<NDb::AbilityUpgradeApplicator> upgrade;
	NDb::Ptr<NDb::Spell> response;
	NDb::Ptr<NDb::EventProcessorPickupGlyph> processor;
	NDb::Ptr<NDb::TriggerApplicator> trigger;
	explicit GlyphCaptureFixtureB13(PFWorld* world) : GlyphUpgradeFixtureB13(world)
	{
		auto* buff = new NDb::AbilityUpgradeApplicator;
		buff->enabled.sString = "true"; buff->startCondition.sString = "true"; buff->lifeTime.sString = "10";
		buff->applyCount.sString = "1"; buff->targetAbility = NDb::ABILITYTYPEID_SPECIAL;
		buff->flags = NDb::ABILITYUPGRADEMODE_USEGLYPH; upgrade = buff;
		auto* spell = new NDb::Spell; auto* immediate = new NDb::DispatchImmediate;
		immediate->instantApply = true; spell->dispatch = immediate; spell->applicators.push_back(buff); response = spell;
		auto* proc = new NDb::EventProcessorPickupGlyph; proc->spell = spell; processor = proc;
		auto* raw = TriggerConditionFixtureB13::Descriptor(proc, "1");
		raw->useAbilityOwnerMana = true; raw->useAbilityCooldown = true; trigger = raw;
		hero->ChangeMana(25 - hero->GetMana());
	}
	void Install()
	{
		const Target target(hero.GetPtr()); PFApplCreatePars pars(cast, target); pars.pDBAppl = trigger;
		app = CreateApplicator(pars); if (app) ActivateApplicator(app, cast);
	}
	void Event(const PFBaseUnitEvent& event) { hero->EventHappened(event); }
};

/** Failed interception preserves ordinary pickup and the trigger's action/payment budget. */
static void GlyphCaptureAdmissionB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (int mode = 0; mode < 20; ++mode)
	{
		GlyphRootB13 root; B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		GlyphCaptureFixtureB13 f(isolated.world);
		auto* response = const_cast<NDb::Spell*>(f.response.GetPtr());
		NDb::Ptr<NDb::DispatchImmediate> immediateOwner = dynamic_cast<const NDb::DispatchImmediate*>(response->dispatch.GetPtr());
		auto* immediate = const_cast<NDb::DispatchImmediate*>(immediateOwner.GetPtr());
		auto* upgrade = const_cast<NDb::AbilityUpgradeApplicator*>(f.upgrade.GetPtr());
		auto* processor = const_cast<NDb::EventProcessorPickupGlyph*>(f.processor.GetPtr());
		if (mode == 5) processor->spell = nullptr;
		if (mode == 6) response->applicators.clear();
		if (mode == 7) response->applicators.push_back(new NDb::LockTilesApplicator);
		if (mode == 8) immediate->instantApply = false;
		if (mode == 9) response->dispatch = nullptr;
		if (mode == 10) immediate->flags = NDb::DISPATCHFLAGS_UPGRADABLE;
		if (mode == 11) upgrade->enabled.sString = "false";
		if (mode == 12) upgrade->startCondition.sString = "false";
		if (mode == 13) upgrade->lifeTime.sString = "0";
		if (mode == 14) upgrade->lifeTime.sString = "cMissing";
		if (mode == 15)
		{
			NDb::Ptr<NDb::Spell> instant = TriggerConditionFixtureB13::Spell();
			response->applicators.push_back(instant->applicators[0]);
		}
		f.Install(); check(f.app && f.app->IsAlive(), "B13 capture installs its real trigger before testing response admission");
		const PFBaseUnitPickupEvent event(NDb::BASEUNITEVENT_PICKUP, mode != 1, mode == 2 ? -1 : mode == 3 ? 2 : 0, "");
		if (mode == 4) event.SetNeedActivate(false);
		int callbacks = 0; CObj<EventDispatchB7Listener> listener = new EventDispatchB7Listener;
		listener->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent* current) {
			if (mode == 16) if (const auto* dispatch = dynamic_cast<const PFBaseUnitDispatchEvent*>(current)) dispatch->CancelDispatch();
			if (const auto* applied = dynamic_cast<const PFBaseUnitApplicatorAppliedEvent*>(current))
			{
				++callbacks;
				if (mode == 17) applied->CancelApplicator();
				if (mode == 18 && f.app) f.app->Stop();
				if (mode == 19 && callbacks == 1) f.Event(event);
			}
			return 0u;
		};
		f.hero->AddEventListener(listener.GetPtr()); f.Event(event);
		f.hero->RemoveEventListener(listener.GetPtr()); listener->action = {};
		const bool accepted = mode == 0 || mode == 19;
		check(event.NeedActivate() == !(accepted || mode == 4)
			&& f.hero->GetMana() == (accepted ? 15 : 25) && f.data->IsReady() == !accepted,
			"B13 only live synchronous storage consumes activation, mana and cooldown; recursive delivery cannot charge twice");
		check(f.Delta() == 0 && f.cast->GetActiveApplicatorsCount() == (accepted ? 2 : mode == 18 ? 0 : 1),
			"B13 incomplete or rejected storage leaves no owned release buff or immediate payload");
		check(!f.app || f.app->GetVariable("glyphNumber") == (accepted ? 0 : -1),
			"B13 failed pickup preserves the processor's previous number");
		if (accepted)
		{
			f.DispatchRelease(); f.DispatchRelease();
			check(f.Delta() == 7, "B13 accepted storage releases its glyph only once through the original upgrade ring");
		}
		else if (mode != 18)
		{
			processor->spell = response; response->dispatch = immediate; immediate->instantApply = true;
			immediate->flags = static_cast<NDb::EDispatchFlags>(0);
			response->applicators.clear(); response->applicators.push_back(upgrade);
			upgrade->enabled.sString = "true"; upgrade->startCondition.sString = "true"; upgrade->lifeTime.sString = "10";
			const PFBaseUnitPickupEvent retry(NDb::BASEUNITEVENT_PICKUP, true, 1, ""); f.Event(retry);
			check(!retry.NeedActivate() && f.hero->GetMana() == 15 && !f.data->IsReady()
				&& f.app->GetVariable("glyphNumber") == 1,
				"B13 failed response leaves its single action available for a later valid glyph");
			f.DispatchRelease(); check(f.Delta() == 13, "B13 retry retains the new glyph instead of a rejected earlier identity");
		}
	}
	for (bool stopped : {false, true})
	{
		GlyphRootB13 root; B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		GlyphCaptureFixtureB13 f(isolated.world);
		if (!stopped)
		{
			auto* group = new NDb::EventProcessorGroup; group->processors.push_back(f.processor.GetPtr());
			const_cast<NDb::TriggerApplicator*>(f.trigger.GetPtr())->processor = group;
		}
		f.Install(); if (stopped && f.app) f.app->Stop();
		const PFBaseUnitPickupEvent event(NDb::BASEUNITEVENT_PICKUP, true, 0, ""); f.Event(event);
		check(event.NeedActivate() && f.hero->GetMana() == 25 && f.cast->GetActiveApplicatorsCount() == 0,
			"B13 stopped triggers and groups without glyph-variable delegation cannot swallow pickups");
	}
	{
		GlyphRootB13 root; B8BehaviourForeignWorld isolated(check);
		if (isolated.world)
		{
			GlyphCaptureFixtureB13 f(isolated.world); f.Install(); CObj<PFBaseApplicator> first = f.app; f.Install();
			const PFBaseUnitPickupEvent event(NDb::BASEUNITEVENT_PICKUP, true, 0, "");
			int callbacks = 0; CObj<EventDispatchB7Listener> listener = new EventDispatchB7Listener;
			listener->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent* current) {
				if (dynamic_cast<const PFBaseUnitApplicatorAppliedEvent*>(current) && ++callbacks == 1) f.Event(event);
				return 0u;
			};
			f.hero->AddEventListener(listener.GetPtr()); f.Event(event);
			f.hero->RemoveEventListener(listener.GetPtr()); listener->action = {};
			check(callbacks == 1 && !event.NeedActivate() && f.hero->GetMana() == 15 && f.cast->GetActiveApplicatorsCount() == 3,
				"B13 same pickup reentered through a second live trigger is captured and charged only once");
			f.DispatchRelease(); check(f.Delta() == 7, "B13 shared-event reentry cannot install a duplicate release");
			if (first) first->Stop();
		}
	}
	std::printf("B13 glyph capture admission checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Purchase the original G290, including its real passive trigger rather than a stand-in source. */
static CObj<B10CommandHero> GlyphCaptureHeroB13(Checks& check, PFWorld* world, const NDb::Talent* db)
{
	auto hero = CommandHeroB10(world); hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
	hero->InstallTalent(db, false); hero->AddGold(10000);
	const float gold = hero->GetGold(); const int cost = hero->GetTalent(0, 0)->GetNaftaCost();
	check(hero->ActivateTalent(0, 0) && hero->GetGold() == gold - cost,
		"B13 original G290 purchase installs its actual passive effects and preserves authored pricing");
	check(hero->FindApplicator("G290GlyphUp2", hero->GetTalent(0, 0), UNIT) != hero->GetTalent(0, 0),
		"B13 original G290 pickup trigger remains live after purchase");
	return hero;
}

/** Drive an actual pickupable object, original pickup event and original talent release command. */
static void GlyphCaptureAuthoredB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		const auto db = NDb::Get<NDb::Talent>(NDb::DBID("/Items/Talents/Level2/G290_GuildGlyphSave.TALENT.xdb"));
		check(db && !db->passiveApplicators.empty(), "B13 capture opens the shipped G290 talent read-only");
		if (db) for (int number : {0, 1}) for (bool automatic : {false, true})
		{
			GlyphRootB13 root; B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
			auto hero = GlyphCaptureHeroB13(check, isolated.world, db);
			CObj<PFTalent> talent = hero->GetTalent(0, 0); const Target target(hero.GetPtr());
			const float strength = hero->GetStatValue(NDb::STAT_STRENGTH);
			CObj<PFGlyph> pickup = new PFGlyph(isolated.world, root.glyphs->glyphs[number].glyph, CVec3(5, 5, 0));
			check(pickup->PickUp(hero.GetPtr()) && pickup->IsObjectDead(), "B13 G290 consumes the actual pickup object once");
			isolated.world->Step(.1f, .1f);
			check(hero->GetVariable("GlyphCatched") == 1 && hero->GetVariable("NumberOfGlyph") == number
				&& hero->GetStatValue(NDb::STAT_STRENGTH) == strength && !talent->CheckCastLimitations(target),
				"B13 actual pickup is stored by the real processor instead of immediately applying or losing its glyph");
			if (!automatic)
			{
				CObj<NCore::WorldCommand> command = CreateCmdUseTalent(hero.GetPtr(), 0, 0, target, true);
				check(command && command->CanExecute(), "B13 actually captured glyph enables the original release command");
				if (command) command->Execute(isolated.world);
			}
			for (int i = 0; i < (automatic ? 620 : 10); ++i) isolated.world->Step(.1f, .1f);
			check(hero->GetStatValue(NDb::STAT_STRENGTH) - strength == (number ? 13 : 7)
				&& hero->GetVariable("GlyphCatched") == 0 && talent->CheckCastLimitations(target),
				"B13 actual capture reaches exactly one manual or timed release and clears storage");
			hero->GetTalentsSet()->CleanSet(); hero->Cleanup();
		}
		if (db) for (bool nested : {false, true})
		{
			GlyphRootB13 root; B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
			auto first = GlyphCaptureHeroB13(check, isolated.world, db), second = GlyphCaptureHeroB13(check, isolated.world, db);
			const float firstStrength = first->GetStatValue(NDb::STAT_STRENGTH), secondStrength = second->GetStatValue(NDb::STAT_STRENGTH);
			CObj<PFGlyph> firstPickup = new PFGlyph(isolated.world, root.glyphs->glyphs[0].glyph, CVec3(5, 5, 0));
			CObj<PFGlyph> secondPickup = new PFGlyph(isolated.world, root.glyphs->glyphs[1].glyph, CVec3(5, 5, 0));
			int callbacks = 0; CObj<EventDispatchB7Listener> listener = new EventDispatchB7Listener;
			listener->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent* event) {
				const auto* applied = dynamic_cast<const PFBaseUnitApplicatorAppliedEvent*>(event);
				if (nested && !callbacks && applied && dynamic_cast<const PFApplStatus*>(applied->GetApplicator()))
				{
					++callbacks; check(secondPickup->PickUp(second.GetPtr()), "B13 nested response performs another hero's real pickup");
				}
				return 0u;
			};
			first->AddEventListener(listener.GetPtr());
			check(firstPickup->PickUp(first.GetPtr()), "B13 first hero picks up its own glyph");
			if (!nested) check(secondPickup->PickUp(second.GetPtr()), "B13 second hero picks up its own glyph");
			first->RemoveEventListener(listener.GetPtr()); listener->action = {};
			isolated.world->Step(.1f, .1f);
			check((!nested || callbacks == 1) && first->GetVariable("GlyphCatched") == 1 && second->GetVariable("GlyphCatched") == 1
				&& first->GetVariable("NumberOfGlyph") == 0 && second->GetVariable("NumberOfGlyph") == 1,
				"B13 independent and nested hero pickups keep their own live stored numbers");
			check(first->UseTalent(first->GetTalent(0, 0), Target(first.GetPtr()))
				&& second->UseTalent(second->GetTalent(0, 0), Target(second.GetPtr())), "B13 both independent captures can be released");
			for (int i = 0; i < 10; ++i) isolated.world->Step(.1f, .1f);
			check(first->GetStatValue(NDb::STAT_STRENGTH) - firstStrength == 7
				&& second->GetStatValue(NDb::STAT_STRENGTH) - secondStrength == 13,
				"B13 nested callbacks cannot replace another hero's saved descriptor or deliver it twice");
			for (auto hero : {first, second}) { hero->GetTalentsSet()->CleanSet(); hero->Cleanup(); }
		}
		if (db) for (int firstNumber : {0, 1}) for (bool automatic : {false, true})
		{
			GlyphRootB13 root; B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
			auto hero = GlyphCaptureHeroB13(check, isolated.world, db);
			const float strength = hero->GetStatValue(NDb::STAT_STRENGTH);
			CObj<PFGlyph> first = new PFGlyph(isolated.world, root.glyphs->glyphs[firstNumber].glyph, CVec3(5, 5, 0));
			CObj<PFGlyph> second = new PFGlyph(isolated.world, root.glyphs->glyphs[1 - firstNumber].glyph, CVec3(5, 5, 0));
			check(first->PickUp(hero.GetPtr()), "B13 replacement stores the first real glyph");
			for (int i = 0; i < 20; ++i) isolated.world->Step(.25f, .25f);
			check(second->PickUp(hero.GetPtr()), "B13 replacement accepts the later real glyph");
			isolated.world->Step(.25f, .25f);
			check(hero->GetStatValue(NDb::STAT_STRENGTH) == strength && hero->GetVariable("NumberOfGlyph") == 1 - firstNumber,
				"B13 same-hero replacement stores the latest number without prematurely delivering either payload");
			if (!automatic) check(bool(hero->UseTalent(hero->GetTalent(0, 0), Target(hero.GetPtr()))), "B13 replacement can be released manually");
			for (int i = 0; i < 260; ++i) isolated.world->Step(.25f, .25f);
			std::printf("B13 replacement first=%d automatic=%d delivered=%g\n", firstNumber, automatic, hero->GetStatValue(NDb::STAT_STRENGTH) - strength);
			check(hero->GetStatValue(NDb::STAT_STRENGTH) - strength == (firstNumber ? 7 : 13) && hero->GetVariable("GlyphCatched") == 0,
				"B13 shipped latest-glyph-only policy retires the superseded release upgrade");
			hero->GetTalentsSet()->CleanSet(); hero->Cleanup();
		}
		if (db) for (bool priorCapture : {false, true}) for (int cancel = 0; cancel < 3; ++cancel)
		{
			GlyphRootB13 root; B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
			auto hero = GlyphCaptureHeroB13(check, isolated.world, db);
			CObj<PFBaseApplicator> trigger = const_cast<PFBaseApplicator*>(dynamic_cast<const PFBaseApplicator*>(
				hero->FindApplicator("G290GlyphUp2", hero->GetTalent(0, 0), UNIT)));
			const float strength = hero->GetStatValue(NDb::STAT_STRENGTH);
			if (priorCapture)
			{
				CObj<PFGlyph> prior = new PFGlyph(isolated.world, root.glyphs->glyphs[1].glyph, CVec3(5, 5, 0));
				check(prior->PickUp(hero.GetPtr()), "B13 canceled replacement starts with an earlier live capture");
				isolated.world->Step(.25f, .25f);
			}
			int callbacks = 0; CObj<EventDispatchB7Listener> listener = new EventDispatchB7Listener;
			listener->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent* event) {
				const auto* applied = dynamic_cast<const PFBaseUnitApplicatorAppliedEvent*>(event);
				if (applied && applied->GetApplicator()->GetParentAppl() == trigger.GetPtr())
				{
					if ((cancel == 0 && dynamic_cast<PFApplAbilityUpgrade*>(applied->GetApplicator()))
						|| (cancel == 1 && dynamic_cast<PFApplStatus*>(applied->GetApplicator())))
					{ ++callbacks; applied->CancelApplicator(); }
					if (cancel == 2 && !callbacks++) trigger->Stop();
				}
				return 0u;
			};
			hero->AddEventListener(listener.GetPtr());
			CObj<PFGlyph> pickup = new PFGlyph(isolated.world, root.glyphs->glyphs[0].glyph, CVec3(5, 5, 0));
			check(pickup->PickUp(hero.GetPtr()), "B13 rejected original storage still consumes the physical pickup normally");
			hero->RemoveEventListener(listener.GetPtr()); listener->action = {};
			for (int i = 0; i < 620; ++i) isolated.world->Step(.1f, .1f);
			const float expected = priorCapture && cancel == 1 ? 20 : 7;
			check(callbacks > 0 && hero->GetStatValue(NDb::STAT_STRENGTH) - strength == expected
				&& hero->GetVariable("GlyphCatched") == 0 && hero->GetTalent(0, 0)->CheckCastLimitations(Target(hero.GetPtr())),
				"B13 failed storage permits ordinary activation, preserves an untouched older capture and retires displaced release roots");
			hero->GetTalentsSet()->CleanSet(); hero->Cleanup();
		}
		if (db) for (float dt : {.1f, .25f}) for (int release = 0; release < 3; ++release)
		{
			GlyphRootB13 root;
			const auto table = NDb::Get<NDb::GlyphsDB>(NDb::DBID("/Glyphs/_.GLDB.xdb"));
			check(table && table->glyphs.size() > 6 && table->glyphs[6].glyph
				&& table->glyphs[6].glyph->GetDBID() == NDb::DBID("/Glyphs/LifeRestorer.GLPH.xdb"),
				"B13 original session table selects the shipped LifeRestorer glyph");
			if (!table || table->glyphs.size() <= 6 || !table->glyphs[6].glyph) continue;
			const_cast<NDb::SessionLogicRoot*>(NDb::SessionRoot::GetRoot()->logicRoot.GetPtr())->glyphsDB = table;
			B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
			auto hero = release ? GlyphCaptureHeroB13(check, isolated.world, db) : CommandHeroB10(isolated.world);
			hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); hero->SetHealth(100); hero->ChangeMana(10 - hero->GetMana());
			check(hero->GetLifeRegen() == 0 && hero->GetLifeRegenAbs() == 0,
				"B13 original glyph fixture excludes ordinary health regeneration");
			CObj<PFGlyph> pickup = new PFGlyph(isolated.world, table->glyphs[6].glyph, CVec3(5, 5, 0));
			check(pickup->PickUp(hero.GetPtr()), "B13 real LifeRestorer pickup enters the original event path");
			if (release)
			{
				for (int i = 0; i < (dt == .1f ? 10 : 4); ++i) isolated.world->Step(dt, dt);
				check(hero->GetHealth() == 100 && hero->GetMana() == 10 && hero->GetVariable("NumberOfGlyph") == 6,
					"B13 stored original LifeRestorer does not heal before release");
				if (release == 1)
				{
					CObj<NCore::WorldCommand> command = CreateCmdUseTalent(hero.GetPtr(), 0, 0, Target(hero.GetPtr()), true);
					check(command && command->CanExecute(), "B13 original healing glyph enables the queued talent release");
					if (command) command->Execute(isolated.world);
				}
				else for (int i = 0; i < (dt == .1f ? 600 : 240); ++i) isolated.world->Step(dt, dt);
			}
			for (int i = 0; i < (dt == .1f ? 80 : 32); ++i) isolated.world->Step(dt, dt);
			// Original Start strikes immediately; the child precedes its parent in the applied ring.
			// Keep float timer/modulo and strict > period, then expire the parent's six-second lifetime.
			int pulses = 1; float remaining = 6, timer = 0;
			do { timer += dt; if (timer > 2) { ++pulses; timer = fmodf(timer, 2); } remaining -= dt; } while (remaining >= EPS_VALUE);
			check(pulses == (dt == .1f ? 4 : 3), "B13 independent original timer oracle retains fractional and exact-step expiry boundaries");
			const float health = Min(hero->GetMaxHealth(), 100 + pulses * (hero->GetMaxHealth() * .12f + 50));
			const float mana = Min(hero->GetMaxMana(), 10 + pulses * (hero->GetMaxMana() * .10f + 15));
			std::printf("B13 original glyph release=%d dt=%g pulses=%d health=%g expected=%g mana=%g expected=%g\n", release, dt, pulses, hero->GetHealth(), health, hero->GetMana(), mana);
			check(std::fabs(hero->GetHealth() - health) < .01f && std::fabs(hero->GetMana() - mana) < .01f,
				"B13 ordinary, manual and timed original glyph releases preserve authored healing and original float timing");
			if (release) hero->GetTalentsSet()->CleanSet(); hero->Cleanup();
		}
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 authored glyph capture checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
