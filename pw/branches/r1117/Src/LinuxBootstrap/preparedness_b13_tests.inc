/** Read original live cooldown state through direct, applicator and candidate formula contexts. */
static void PreparednessB13(Checks& check, PFWorld* world)
{
	const int before = check.count, failed = check.failed;
	auto hero = B9MaleHero(world, NDb::FACTION_FREEZE);
	auto* raw = new NDb::Ability; raw->cooldownTime.sString = "10"; raw->cooldownTimeSecondState.sString = "20";
	NDb::Ptr<NDb::Ability> db = raw;
	CObj<PFAbilityData> ability = new PFAbilityData(hero.GetPtr(), db, NDb::ABILITYTYPEID_ABILITY1, true, false);
	const Target target(hero.GetPtr()); CObj<PFAbilityInstance> instance = new PFAbilityInstance(ability.GetPtr(), target, false);
	NDb::Ptr<NDb::BaseApplicator> leaf = new NDb::HealApplicator;
	PFApplCreatePars pars(instance, target); pars.pDBAppl = leaf;
	CObj<PFBaseApplicator> app = CreateApplicator(pars);
	check(app && app->Init(), "B13 preparedness fixture uses a real ability-owned applicator");
	if (!app) return;
	const auto verify = [&]() {
		for (const char* expression : {"mPreparedness", "pMisc->GetPreparedness()", "appl.GetPreparedness()",
			"mCooldown", "pMisc->GetCooldown()", "appl.GetCooldown()"})
		{
			const float expected = std::strstr(expression, "Cooldown") ? ability->GetCooldown() : ability->GetPreparedness();
			for (const auto& value : {LinuxBootstrap::EvaluateUnitNumericFormula(expression, hero, hero, ability),
				LinuxBootstrap::EvaluateApplicatorNumericFormula(expression, *app),
				LinuxBootstrap::EvaluateSelectorConditionFormula(expression, hero, nullptr, app)})
				check(value.Succeeded() && value.value == expected, "B13 miscellaneous cooldown queries use the actual current state without substituting a candidate");
		}
	};
	for (const auto state : {EAbilityState::First, EAbilityState::Second})
	{
		ability->SetState(state); verify();
		ability->RestartCooldown(); verify();
		ability->Update(2.5f, false); verify();
		ability->DropCooldown(false, 2.5f, false); verify();
		ability->Update(20, false); verify();
		ability->RestartCooldown(0); verify();
	}
	ability->SetState(EAbilityState::First); ability->RestartCooldown(12); ability->Update(3, false);
	NRandom::RandomGenerator expectedRng(*world->GetRndGen()); const int roll = expectedRng.Next(1, 3);
	const auto valid = LinuxBootstrap::EvaluateApplicatorNumericFormula("random(1,3)+mPreparedness", *app);
	check(valid.Succeeded() && valid.value == roll + .25f, "B13 readiness preserves the original elapsed-over-total ratio");
	check(world->GetRndGen()->Next(0, 1000) == expectedRng.Next(0, 1000), "B13 successful readiness evaluation commits exactly its seeded random draws");
	for (const char* expression : {"pMisc->GetPreparedness(1)", "appl.GetCooldown(1)", "appl.GetPreparedness( )",
		"mObjectParent->GetPreparedness()", "pMisc->GetCurrentCooldown()"})
		check(!LinuxBootstrap::EvaluateApplicatorNumericFormula(std::string("true ? 7 : ") + expression, *app).Succeeded(),
			"B13 readiness aliases do not expose generic methods, parent traversal or relaxed signatures");
	CObj<PFAbilityData> upgrader = new PFAbilityData(hero.GetPtr(), db, NDb::ABILITYTYPEID_ABILITY2, true, false);
	upgrader->RestartCooldown(30); upgrader->Update(15, false);
	pars.pUpgraderAbilityData = upgrader; CObj<PFBaseApplicator> upgraded = CreateApplicator(pars);
	check(upgraded && upgraded->Init(), "B13 readiness fixture retains a distinct original upgrader");
	if (upgraded)
	{
		const auto value = LinuxBootstrap::EvaluateApplicatorNumericFormula("mPreparedness * mCooldown", *upgraded);
		check(value.Succeeded() && value.value == 15, "B13 miscellaneous cooldown reads honor actual applicator upgrader precedence");
	}
	for (bool invalidState : {false, true})
	{
		if (invalidState) ability->SetState(EAbilityState::_Count);
		else ability->RestartCooldown(std::numeric_limits<float>::quiet_NaN());
		for (const char* expression : {"mPreparedness", "mCooldown", "pMisc->GetPreparedness()", "appl.GetCooldown()"})
		{
			NRandom::RandomGenerator expected(*world->GetRndGen());
			check(!LinuxBootstrap::EvaluateApplicatorNumericFormula(std::string("random(1,3)+") + expression, *app).Succeeded(),
				"B13 malformed timers and invalid state indices cannot become ready predicates or index arrays");
			check(world->GetRndGen()->Next(0, 1000) == expected.Next(0, 1000), "B13 failed readiness preserves earlier staged randomness");
		}
		ability->SetState(EAbilityState::First); ability->RestartCooldown(10);
	}
	for (const char* expression : {"mPreparedness", "mCooldown", "pMisc->GetPreparedness()"})
	{
		check(!LinuxBootstrap::EvaluateUnitNumericFormula(expression, hero, hero).Succeeded(), "B13 missing miscellaneous context cannot imply cooldown readiness");
		const auto lazy = LinuxBootstrap::EvaluateUnitNumericFormula(std::string("true ? 7 : ") + expression, nullptr, nullptr);
		check(lazy.Succeeded() && lazy.value == 7, "B13 unselected cooldown reads require no ability or units");
	}
	PFApplCreatePars orphanPars(nullptr, target); orphanPars.pWorld = world; orphanPars.pOwner = hero; orphanPars.pDBAppl = leaf;
	CObj<PFBaseApplicator> orphan = CreateApplicator(orphanPars);
	check(orphan && orphan->Init(), "B13 missing-ability cooldown fixture still owns a real unit and world");
	if (orphan) for (const char* expression : {"mPreparedness", "mCooldown"})
	{
		NRandom::RandomGenerator expected(*world->GetRndGen());
		check(!LinuxBootstrap::EvaluateApplicatorNumericFormula(std::string("random(1,3)+") + expression, *orphan).Succeeded(),
			"B13 owner and world cannot substitute for missing cooldown ability state");
		check(world->GetRndGen()->Next(0, 1000) == expected.Next(0, 1000), "B13 missing cooldown context rolls back earlier randomness");
	}
	hero->Cleanup();
	std::printf("B13 preparedness checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Buy complete shipped alchemy passives, consume their real gifts and wait through the authored cooldown. */
static void AuthoredPreparednessB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		for (bool energy : {false, true})
		{
			const auto db = NDb::Get<NDb::Talent>(NDb::DBID(energy
				? "/Items/Talents/Level1/G722_RegEnergy.TALENT.xdb" : "/Items/Talents/Level1/G723_StrOrInt.TALENT.xdb"));
			const auto potion = NDb::Get<NDb::Consumable>(NDb::DBID(energy
				? "/Items/Consumables/EnergyPotion.ARCT.xdb" : "/Items/Consumables/HealingPotion.ARCT.xdb"));
			check(db && potion && db->passiveApplicators.size() == (energy ? 4 : 6), "B13 alchemy fixture loads the complete unchanged talent and original potion");
			if (!db || !potion) continue;
			const auto* trigger = db->passiveApplicators.empty() ? nullptr
				: dynamic_cast<const NDb::TriggerApplicator*>(db->passiveApplicators.back().GetPtr());
			const auto* processor = trigger ? dynamic_cast<const NDb::EventProcessorCondition*>(trigger->processor.GetPtr()) : nullptr;
			const auto* reset = processor && processor->spell ? dynamic_cast<const NDb::DispatchImmediate*>(processor->spell->dispatch.GetPtr()) : nullptr;
			check(reset && !reset->instantApply, "B13 authored gift-marker reset explicitly uses a deferred original dispatch");
			for (int rate : {1, 6})
			{
				B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
				auto hero = CommandHeroB10(isolated.world), control = CommandHeroB10(isolated.world);
				for (const auto& unit : {hero, control})
				{
					unit->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
					unit->GetStat(NDb::STAT_LIFE)->SetCoreValue(1000); unit->GetStat(NDb::STAT_ENERGY)->SetCoreValue(1000);
					unit->GetStat(NDb::STAT_STRENGTH)->SetCoreValue(50); unit->GetStat(NDb::STAT_INTELLECT)->SetCoreValue(40);
					int flags[9] = {}; flags[4] = 1; unit->SetRecommendedStats(flags);
					unit->GetVariableVWM("Tossed")->SetBaseValue(1);
					unit->InstallTalent(db, false); unit->GetTalent(0, 0)->SetRefineRate(rate); unit->AddGold(10000);
					const float gold = unit->GetGold(); const int cost = unit->GetTalent(0, 0)->GetNaftaCost();
					check(unit->ActivateTalent(0, 0) && unit->GetGold() == gold - cost, "B13 original alchemy purchase activates its full passive list with the actual cost");
					const float expected = energy ? 2.7f + .2f * (rate - 1) : 40 + 5.f + .4f * (rate - 1);
					check(std::fabs(unit->GetStatValue(energy ? NDb::STAT_ENERGYREGENERATIONABSOLUTE : NDb::STAT_INTELLECT) - expected) < .0001f,
						"B13 alchemy retains its refined passive bonus and original recommended-stat selection");
				}
				int gifts = 0;
				CObj<EventDispatchB7Listener> observer = new EventDispatchB7Listener;
				observer->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent* event) {
					if (const auto* obtained = dynamic_cast<const PFHeroEventConsumableObtained*>(event))
					{
						++gifts;
						check(obtained->GetOrigin() == NDb::CONSUMABLEORIGIN_APPLICATOR && obtained->GetDBConsumable() == potion.GetPtr(),
							"B13 alchemy gift keeps its authored resource and applicator acquisition origin");
					}
					return 0u;
				};
				hero->AddEventListener(observer.GetPtr());
				for (int i = 0; i < 4; ++i) isolated.world->Step(.1f, .1f);
				check(gifts == 0 && !hero->GetConsumable(0) && hero->GetTalent(0, 0)->GetPreparedness() == 1,
					"B13 authored tossed restriction blocks gifts without spending the talent cooldown");
				for (const auto& unit : {hero, control}) unit->GetVariableVWM("Tossed")->SetBaseValue(0);
				for (int i = 0; i < 4; ++i) isolated.world->Step(.1f, .1f);
				check(gifts == 1 && hero->GetConsumable(0) && hero->GetConsumable(0)->GetQuantity() == 1,
					"B13 live readiness and tossed release produce exactly one original potion");
				CObj<PFTalent> talent = hero->GetTalent(0, 0);
				std::printf("B13 alchemy first gift: energy=%d rate=%d cooldown=%g remaining=%g ready=%g marker=%g gifts=%d\n",
					energy, rate, talent->GetCooldown(), talent->GetCurrentCooldown(), talent->GetPreparedness(),
					hero->GetVariable(energy ? "RegEnergyConsumableGived" : "HealingPotionConsumableGived"), gifts);
				check(talent->GetCooldown() == 240 && talent->GetCurrentCooldown() > 239 && talent->GetPreparedness() < .01f
					&& hero->GetVariable(energy ? "RegEnergyConsumableGived" : "HealingPotionConsumableGived") == 1,
					"B13 original step trigger starts four-minute cooldown before its deferred reset spell applies");
				isolated.world->Step(.1f, .1f);
				check(hero->GetVariable(energy ? "RegEnergyConsumableGived" : "HealingPotionConsumableGived") == 0
					&& talent->GetCurrentCooldown() > 239 && talent->GetCurrentCooldown() < 240 && gifts == 1,
					"B13 the next real world step applies the original deferred marker reset without minting another gift");
				for (const auto& unit : {hero, control}) { unit->SetHealth(100); unit->ChangeMana(100 - unit->GetMana()); }
				CObj<PFAbilityInstance> cast = hero->UseConsumable(0, Target(hero.GetPtr()), false);
				check(cast != nullptr, "B13 generated potion uses the real inventory cast and acquired talent branch");
				for (int i = 0; i < 206; ++i) isolated.world->Step(.1f, .1f);
				const float restored = energy ? hero->GetMana() - control->GetMana() : hero->GetLife() - control->GetLife();
				check(std::fabs(restored - 500) < .05f && cast && cast->GetActiveApplicatorsCount() == 0,
					"B13 enhanced potion restores original 400 plus ten percent of the actual 1000-point pool, beyond matched passive regeneration");
				check(gifts == 1 && !hero->GetConsumable(0), "B13 consumed gift cannot regenerate on every periodic tick while its talent cools down");
				for (int i = 0; i < 870; ++i) isolated.world->Step(.25f, .25f);
				check(gifts == 1 && talent->GetCurrentCooldown() > 0, "B13 original long cooldown remains active before its authored boundary");
				hero->GetVariableVWM("Tossed")->SetBaseValue(1);
				for (int i = 0; i < 16; ++i) isolated.world->Step(.25f, .25f);
				check(gifts == 1 && talent->GetPreparedness() == 1 && talent->GetCurrentCooldown() == 0,
					"B13 cooldown expiry while tossed neither grants a potion nor restarts cooldown from a stale gift marker");
				hero->GetVariableVWM("Tossed")->SetBaseValue(0);
				for (int i = 0; i < 5; ++i) isolated.world->Step(.1f, .1f);
				check(gifts == 2 && hero->GetConsumable(0) && hero->GetConsumable(0)->GetQuantity() == 1
					&& talent->GetCurrentCooldown() > 235,
					"B13 natural cooldown expiry grants one replacement potion and restarts the same original cycle");
				hero->RemoveEventListener(observer.GetPtr()); observer->action = nullptr;
				hero->GetTalentsSet()->CleanSet(); control->GetTalentsSet()->CleanSet(); hero->Cleanup(); control->Cleanup();
			}
		}
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 authored preparedness checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
