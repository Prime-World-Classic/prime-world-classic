static const char* ParentTargetFactionB13 = "mObjectParent->GetObject(\"Target\")->GetFctn()";

/** A redirected receiver must not replace the original parent target in authored object queries. */
static void ParentTargetQueriesB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	NaftaFixtureB13 f(isolated.world); f.sender->ChangeFaction(NDb::FACTION_NEUTRAL);
	auto other = B9MaleHero(isolated.world, NDb::FACTION_BURN); other->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
	auto* raw = WrapperFactoryFixtureB12::Stat(); raw->applyTarget = NDb::APPLICATORAPPLYTARGET_ABILITYOWNER;
	NDb::Ptr<NDb::StatModApplicator> db = raw;
	const Target rootTarget(other.GetPtr()); PFApplCreatePars rootPars(f.instance, rootTarget); rootPars.pDBAppl = db;
	auto root = CreateApplicator(rootPars), middle = f.Create(db, root.GetPtr());
	PFApplCreatePars leafPars(f.instance, rootTarget, middle.GetPtr()); leafPars.pDBAppl = new NDb::HealApplicator;
	auto leaf = CreateApplicator(leafPars); isolated.world->Step(0, 0);
	check(root && middle && leaf && middle->GetReceiver() == f.sender.GetPtr(),
		"B13 parent target fixture has distinct owner, redirected receiver and original target");
	if (!root || !middle || !leaf) return;
	for (const auto& entry : {std::pair<const char*, int>{ParentTargetFactionB13, NDb::FACTION_FREEZE},
		{"pMisc->GetObjectParent()->GetObject(\"Target\")->GetFctn()", NDb::FACTION_FREEZE},
		{"appl.GetObjectParent()->GetObject(\"Target\")->GetFctn()", NDb::FACTION_FREEZE},
		{"mObjectParent->GetObjectParent()->GetObject(\"Target\")->GetFctn()", NDb::FACTION_BURN},
		{"pMisc->GetObject(\"Target\")->GetFctn()", NDb::FACTION_BURN},
		{"appl.GetObject(\"Target\")->GetFctn()", NDb::FACTION_BURN}})
	{
		const auto value = LinuxBootstrap::EvaluateApplicatorNumericFormula(entry.first, *leaf);
		check(value.Succeeded() && value.value == entry.second, "B13 exact target-faction queries preserve original parent depth and raw target identity");
	}
	const auto self = LinuxBootstrap::EvaluateApplicatorNumericFormula(ParentTargetFactionB13, *root);
	check(self.Succeeded() && self.value == NDb::FACTION_BURN, "B13 root parent target lookup preserves original self fallback");
	for (NDb::EFaction faction : {NDb::FACTION_NEUTRAL, NDb::FACTION_FREEZE, NDb::FACTION_BURN})
	{
		f.receiver->ChangeFaction(faction);
		const auto value = LinuxBootstrap::EvaluateApplicatorNumericFormula(std::string("rFctn != ") + ParentTargetFactionB13, *leaf);
		check(value.Succeeded() && value.value == (faction != NDb::FACTION_BURN),
			"B13 original Boss1 condition compares the current receiver with the live parent-target faction");
	}
	f.receiver->SetForbidRespawn(true); HeroCorpseKillB13(isolated.world, f.receiver);
	check(f.Activate(middle) && middle->IsAlive(), "B13 redirected parent activates on its living receiver despite a dead original target");
	middle->Stop();
	const auto stopped = LinuxBootstrap::EvaluateApplicatorNumericFormula(ParentTargetFactionB13, *leaf);
	check(stopped.Succeeded() && stopped.value == NDb::FACTION_BURN,
		"B13 stopped parent and dead but registered target retain original read-only faction context");
	NRandom::RandomGenerator expected(*isolated.world->GetRndGen()); const int draw = expected.Next(1, 9);
	const auto rng = LinuxBootstrap::EvaluateApplicatorNumericFormula(std::string("random(1,9)+") + ParentTargetFactionB13, *leaf);
	check(rng.Succeeded() && rng.value == draw + NDb::FACTION_BURN && GlyphRngEqualB13(*isolated.world->GetRndGen(), expected),
		"B13 valid parent-target reads commit exactly the original random expression");
	isolated.world->UnregisterObject(f.receiver.GetPtr(), false); isolated.world->ProcessAddRemove();
	expected = *isolated.world->GetRndGen();
	const auto removed = LinuxBootstrap::EvaluateApplicatorNumericFormula(std::string("random(1,9)+") + ParentTargetFactionB13, *leaf);
	check(!removed.Succeeded() && GlyphRngEqualB13(*isolated.world->GetRndGen(), expected),
		"B13 world-unregistered parent target rejects without committing earlier randomness");
	other->Cleanup();
	std::printf("B13 parent target queries checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Exact syntax, bounded ancestry and actual world identity guard the single read-only object query. */
static void ParentTargetSafetyB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	B8BehaviourForeignWorld isolated(check), foreign(check); if (!isolated.world || !foreign.world) return;
	NaftaFixtureB13 f(isolated.world); NDb::Ptr<NDb::BaseApplicator> db = new NDb::HealApplicator;
	auto root = f.Create(db); auto leaf = f.Create(db, root.GetPtr()); isolated.world->Step(0, 0);
	for (const char* expression : {"mObjectParent->GetObject(\"Other\")->GetFctn()", "mObjectParent->GetObject(\"Target\")->GetLife()",
		"mObjectParent->GetObject(\"Target\")->SetFaction(1)", "mObjectParent->GetObject(\"Target\")->GetFctn(1)",
		"mObjectParent->GetObject(\"Target\",1)->GetFctn()", "mObjectParent->GetObject(\"Target\")->GetFctn()()",
		"__misc_target_faction_1", "mObjectParent->GetObject(\"Target\").GetFctn()"})
	{
		check(!LinuxBootstrap::EvaluateApplicatorNumericFormula(expression, *leaf).Succeeded(),
			"B13 parent target query exposes neither arbitrary names/methods nor injected reserved symbols");
		check(!LinuxBootstrap::EvaluateApplicatorNumericFormula(std::string("true ? 1 : ") + expression, *leaf).Succeeded(),
			"B13 invalid parent target syntax remains invalid in an unselected branch");
	}
	for (bool withAbility : {false, true})
	{
		const auto invalid = LinuxBootstrap::EvaluateUnitNumericFormula(ParentTargetFactionB13, f.sender, f.receiver,
			withAbility ? f.ability.GetPtr() : nullptr);
		const auto lazy = LinuxBootstrap::EvaluateUnitNumericFormula(std::string("true ? 7 : ") + ParentTargetFactionB13,
			f.sender, f.receiver, withAbility ? f.ability.GetPtr() : nullptr);
		check(!invalid.Succeeded() && lazy.Succeeded() && lazy.value == 7,
			"B13 ability-only or absent object context cannot substitute an owner but remains lazy when unselected");
	}
	auto alien = B9MaleHero(foreign.world, NDb::FACTION_BURN); foreign.world->Step(0, 0);
	for (const Target target : {Target(CVec3(5, 5, 0)), Target(alien.GetPtr())})
	{
		PFApplCreatePars pars(f.instance, target); pars.pDBAppl = db;
		CObj<ParentScaleProbeB13> parent = new ParentScaleProbeB13(pars);
		auto child = f.Create(db, parent.GetPtr());
		check(!LinuxBootstrap::EvaluateApplicatorNumericFormula(ParentTargetFactionB13, *child).Succeeded(),
			"B13 position-only and foreign-world parent targets cannot provide a unit faction");
	}
	const Target target(f.receiver.GetPtr()); PFApplCreatePars pars(f.instance, target); pars.pDBAppl = db;
	for (int length : {1, 2, 128, 129})
	{
		std::vector<CObj<ParentScaleProbeB13>> chain;
		for (int i = 0; i < length; ++i)
		{
			CObj<ParentScaleProbeB13> item = new ParentScaleProbeB13(pars);
			if (!chain.empty()) item->Parent(chain.back().GetPtr()); chain.push_back(item);
		}
		for (bool cycle : {false, true})
		{
			chain.front()->Parent(cycle ? chain.back().GetPtr() : nullptr);
			const auto result = LinuxBootstrap::EvaluateApplicatorNumericFormula(ParentTargetFactionB13, *chain.back());
			check(result.Succeeded() == (!cycle && length <= 128), "B13 malformed cyclic or over-budget parent graph cannot enter original target getters");
		}
		chain.front()->Parent(nullptr);
	}
	for (int depth : {16, 17})
	{
		std::string expression = "pMisc";
		for (int i = 0; i < depth; ++i) expression += "->GetObjectParent()";
		expression += "->GetObject(\"Target\")->GetFctn()";
		const auto result = LinuxBootstrap::EvaluateApplicatorNumericFormula(expression, *leaf);
		check(result.Succeeded() == (depth == 16) && (depth != 16 || result.value == NDb::FACTION_FREEZE),
			"B13 explicit target traversal preserves original root fallback within the existing sixteen-parent syntax budget");
	}
	alien->Cleanup();
	std::printf("B13 parent target safety checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Execute the unchanged Boss1 selector, deferred dispatch and resurrection payload as one original graph. */
static void ParentTargetBossB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		const auto db = NDb::Get<NDb::SpellApplicator>(NDb::DBID("/Creeps/Bosses/Boss1/Resurrect.APLR.xdb"));
		check(db && db->spell && db->targetSelector, "B13 unchanged Boss1 resurrection graph loads read-only with its original selector and dispatch");
		if (db) for (NDb::EFaction faction : {NDb::FACTION_FREEZE, NDb::FACTION_BURN}) for (bool removeTarget : {false, true})
		{
			B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
			SalarySettingsB13(isolated.world, 60, 0); NaftaFixtureB13 f(isolated.world);
			f.sender->ChangeFaction(NDb::FACTION_NEUTRAL); f.receiver->ChangeFaction(faction);
			const_cast<NDb::Ability*>(f.abilityDb.GetPtr())->flags = NDb::ABILITYFLAGS_APPLYTODEAD;
			auto ally = B9MaleHero(isolated.world, faction);
			const auto opponent = faction == NDb::FACTION_FREEZE ? NDb::FACTION_BURN : NDb::FACTION_FREEZE;
			auto enemy = B9MaleHero(isolated.world, opponent), living = B9MaleHero(isolated.world, opponent);
			for (auto* hero : {ally.GetPtr(), enemy.GetPtr(), living.GetPtr(), f.receiver.GetPtr(), f.sender.GetPtr()})
			{
				hero->SetForbidRespawn(true); hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
			}
			living->SetHealth(37); HeroCorpseKillB13(isolated.world, ally); HeroCorpseKillB13(isolated.world, enemy);
			int events = 0; CObj<EventDispatchB7Listener> listener = new EventDispatchB7Listener;
			listener->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent* event) {
				if (event->GetType() == NDb::BASEUNITEVENT_RESURRECT) ++events; return 0u;
			};
			enemy->AddEventListener(listener.GetPtr());
			auto app = f.Create(db); check(app && ActivateApplicator(app, f.instance) && !app->IsAlive(),
				"B13 original Boss1 spell completes its parent before the authored deferred dispatches apply");
			check(ally->IsDead() && enemy->IsDead() && events == 0, "B13 original Boss1 dispatch does not resurrect synchronously");
			CPtr<PFBaseApplicator> parent(app.GetPtr()); app = nullptr;
			if (removeTarget) { isolated.world->UnregisterObject(f.receiver.GetPtr(), false); isolated.world->ProcessAddRemove(); }
			isolated.world->Step(.1f, .1f); isolated.world->Step(.1f, .1f);
			check(ally->IsDead() && enemy->IsDead() == removeTarget && events == int(!removeTarget),
				"B13 original Boss1 revives only the parent target's opposing dead team, and cannot use a removed target");
			check(living->GetHealth() == 37 && (removeTarget || (enemy->GetHealth() == enemy->GetMaxHealth()
				&& enemy->GetEnergy() == enemy->GetMaxEnergy())), "B13 Boss1 restores authored resources only to an eligible dead hero");
			check(IsValid(parent) && !parent->IsAlive() && f.instance->GetActiveApplicatorsCount() == 0,
				"B13 original history retains the stopped parent after external release and balances completed child ownership");
			isolated.world->Step(.1f, .1f); check(events == int(!removeTarget), "B13 finished Boss1 dispatch cannot repeat resurrection on later ticks");
			enemy->RemoveEventListener(listener.GetPtr()); listener->action = nullptr;
			ally->Cleanup(); enemy->Cleanup(); living->Cleanup();
		}
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 original Boss1 resurrection checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
