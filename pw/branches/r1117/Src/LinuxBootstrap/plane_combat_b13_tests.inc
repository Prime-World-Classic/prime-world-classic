/** Exercise the real talent/portal routing with distinct loaded mock descriptor identities. */
static void TalentCooldownRoutingB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	auto hero = CommandHeroB10(isolated.world);
	NDb::Ptr<NDb::Talent> a = FlagTalentB12("/Mock/C95/First.xdb"), b = FlagTalentB12("/Mock/C95/Second.xdb");
	NDb::Ptr<NDb::Talent> ready = FlagTalentB12("/Mock/C95/Ready.xdb"), portal = FlagTalentB12("/Mock/C95/Portal.xdb");
	for (const auto& db : {a, b, ready, portal}) const_cast<NDb::Talent*>(db.GetPtr())->cooldownTime.sString = "10";
	hero->InstallTalent(a, false); hero->InstallPortal(portal, true);
	PFTalentsSet::SetInfo info;
	info[0][0].pTalentDesc = a; info[0][1].pTalentDesc = b; info[0][2].pTalentDesc = ready;
	for (int i = 0; i < 3; ++i) info[0][i].refineRate = 1;
	hero->GetTalentsSet()->LoadSet(hero.GetPtr(), info);
	for (int i = 0; i < 3; ++i) hero->GetTalent(0, i)->Activate();
	auto* first = hero->GetTalent(0, 0); auto* second = hero->GetTalent(0, 1);
	auto* portalTalent = hero->GetPortal();
	for (int policy = 0; policy < 6; ++policy) for (float amount : {0.f, 3.f, -.5f, .25f,
		std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
	{
		first->RestartCooldown(10); second->RestartCooldown(10); portalTalent->RestartCooldown(10);
		const bool percent = amount == .25f;
		vector<NDb::Ptr<NDb::Ability>> list;
		if (policy >= 2) list.push_back(a.GetPtr());
		if (policy == 5) { list.push_back(nullptr); list.push_back(a.GetPtr()); }
		const auto mode = policy == 3 ? NDb::USEMODE_LISTASEXCEPTION : policy == 4
			? static_cast<NDb::EUseMode>(77) : NDb::USEMODE_LISTASRULE;
		const DropCooldownParams params(policy ? NDb::ABILITYIDFLAGS_TALENTS : 0, list, mode, first, amount, percent);
		hero->DropCooldowns(params);
		const float changed = !std::isfinite(amount) ? 10 : amount == 0 ? 0 : percent ? 7.5f : 10 - amount;
		check(first->GetCurrentCooldown() == ((policy == 2 || policy == 5) ? changed : 10),
			"B13 explicit talent list overrides exceptAbility, with unique application and invalid-mode rejection");
		check(second->GetCurrentCooldown() == ((policy == 1 || policy == 3) ? changed : 10),
			"B13 empty-list exception and nonempty exclusion select the original sibling set");
		check(portalTalent->GetCurrentCooldown() == ((policy == 1 || policy == 3) ? changed : 10),
			"B13 portal participates in the same original refresh rules as ordinary talents");
		check(hero->GetTalent(0, 2)->IsReady(), "B13 cooldown extension never starts an already-ready talent");
	}
	hero->Cleanup();
	std::printf("B13 talent cooldown routing checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Run the complete shipped A1 graph with real purchase/casting, not a rewritten damage leaf. */
static void PlaneRocketAuthoredB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	TalentCooldownRoutingB13(check);
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		const auto db = NDb::Get<NDb::Talent>(NDb::DBID("/Items/Talents/Class/Plane/Ability_A1.TALENT.xdb"));
		const auto upgrade = NDb::Get<NDb::Talent>(NDb::DBID("/Items/Talents/Class/Plane/Ability_A1u.TALENT.xdb"));
		NDb::Ptr<NDb::UnitConstantsContainer> globals = new NDb::UnitConstantsContainer;
		check(db && db->applicators.size() == 2 && db->persistentId == "Plane_A1",
			"B13 full original Plane A1 talent, program and passive status load unchanged");
		auto* ai = const_cast<NDb::AILogicParameters*>(NDb::SessionRoot::GetRoot()->logicRoot->aiLogic.GetPtr());
		const auto savedConstants = ai->globalConstants; const auto savedScaling = ai->abilityAndDamageScalingParams;
		CObj<Stream> xml = files->OpenFile("GameLogic/AILogic.AILP.xdb", FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
		check(xml && xml->IsOk(), "B13 original ability damage-scaling rules are available");
		if (xml && xml->IsOk())
		{
			CObj<IXmlSaver> saver = CreateXmlSaver(xml, true);
			const bool opened = saver->StartChunk("AILogicParameters", 0);
			check(opened, "B13 original typed serializer enters ability-scaling settings");
			if (opened)
			{
				saver->Add("abilityAndDamageScalingParams", &ai->abilityAndDamageScalingParams);
				// Read original literal constant descriptors without force-precaching the entire AI/talent graph.
				if (saver->StartChunk("globalConstants", 0))
				{
					if (saver->StartChunk("UnitConstantsContainer", 0))
					{
						if (saver->StartChunk("vars", 0))
						{
							const int count = saver->CountChunks();
							for (int i = 1; i <= count; ++i) if (saver->StartChunk("Item", i))
							{
								auto* value = new NDb::UnitConstant; NDb::Ptr<NDb::UnitConstant> held = value;
								saver->Add("UnitConstant", value); saver->FinishChunk();
								if (!value->name.empty()) const_cast<NDb::UnitConstantsContainer*>(globals.GetPtr())->vars.push_back(held);
							}
							saver->FinishChunk();
						}
						saver->FinishChunk();
					}
					saver->FinishChunk();
				}
				saver->FinishChunk();
			}
		}
		check(!globals->vars.empty(), "B13 original global constant expressions load without replacement values or forced root precaching");
		ai->globalConstants = globals;
		check(upgrade && upgrade->persistentId == "Plane_A1u", "B13 full original repeat-launch upgrade loads unchanged");
		if (db && upgrade && !globals->vars.empty()) for (bool burn : {false, true}) for (bool strength : {false, true})
		for (int mode : {0, 1, 2})
		{
			B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
			auto hero = CommandHeroB10(isolated.world);
			const auto faction = burn ? NDb::FACTION_BURN : NDb::FACTION_FREEZE;
			const auto other = burn ? NDb::FACTION_FREEZE : NDb::FACTION_BURN;
			hero->ChangeFaction(faction);
			auto victim = B9MaleHero(isolated.world, other), outside = B9MaleHero(isolated.world, other), ally = B9MaleHero(isolated.world, faction);
			PFBaseUnit* units[] = {hero.GetPtr(), victim.GetPtr(), outside.GetPtr(), ally.GetPtr()};
			for (auto* unit : units)
			{
				unit->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
				unit->GetStat(NDb::STAT_LIFE)->SetCoreValue(10000); unit->SetHealth(10000);
				for (auto stat : {NDb::STAT_LIFEREGENERATION, NDb::STAT_LIFEREGENERATIONABSOLUTE,
					NDb::STAT_ENERGYREGENERATION, NDb::STAT_ENERGYREGENERATIONABSOLUTE, NDb::STAT_WILL, NDb::STAT_STAMINA})
					unit->GetStat(stat)->SetCoreValue(0);
			}
			hero->GetStat(NDb::STAT_RANGE)->SetCoreValue(20);
			hero->GetStat(NDb::STAT_STRENGTH)->SetCoreValue(strength ? 120 : 50);
			hero->GetStat(NDb::STAT_INTELLECT)->SetCoreValue(strength ? 50 : 120);
			check(victim->TeleportTo(CVec2(15, 5)) && ally->TeleportTo(CVec2(15, 6)) && outside->TeleportTo(CVec2(35, 25)),
				"B13 original hero targets occupy in-area, allied and out-of-area positions through real movement registration");
			hero->InstallTalent(db, false); hero->AddGold(400);
			if (mode)
			{
				PFTalentsSet::SetInfo info;
				info[0][0].pTalentDesc = db; info[0][0].refineRate = 1;
				info[0][1].pTalentDesc = upgrade; info[0][1].refineRate = 1;
				hero->GetTalentsSet()->LoadSet(hero.GetPtr(), info);
			}
			CObj<NCore::WorldCommand> purchase = CreateCmdActivateTalent(hero.GetPtr(), 0, 0);
			check(purchase && purchase->CanExecute(), "B13 original A1 purchase enters the real command admission path");
			if (purchase) purchase->Execute(isolated.world);
			CObj<PFTalent> talent = hero->GetTalent(0, 0);
			check(talent && talent->IsActivated() && hero->GetGold() == 100, "B13 original A1 purchase pays exactly its authored 300 prime");
			if (mode)
			{
				hero->AddGold(290);
				CObj<NCore::WorldCommand> buyUpgrade = CreateCmdActivateTalent(hero.GetPtr(), 0, 1);
				check(buyUpgrade && buyUpgrade->CanExecute(), "B13 original repeat-launch purchase follows its acquired prerequisite");
				if (buyUpgrade) buyUpgrade->Execute(isolated.world);
				check(hero->GetTalent(0, 1)->IsActivated() && hero->GetGold() == 100,
					"B13 original repeat-launch upgrade pays exactly 290 prime");
			}
			const auto damage = LinuxBootstrap::EvaluateUnitNumericFormula("cA1_Damage", hero, victim, talent);
			const auto dot = LinuxBootstrap::EvaluateUnitNumericFormula("cA1_VoidZoneDoT", hero, victim, talent);
			const auto radius = LinuxBootstrap::EvaluateUnitNumericFormula("cA1_Radius", hero, victim, talent);
			check(damage.Succeeded() && damage.value > 0 && dot.Succeeded() && dot.value > 0 && radius.Succeeded() && radius.value > 1,
				"B13 original A1 constants resolve through actual talent and global rules without injected formula values");
			const Target point(CVec3(15, 5, 0));
			const float manaBefore = hero->GetMana();
			check(talent->GetManaCost() == 70, "B13 original A1 retains the authored 70-mana price");
			std::printf("B13 Plane pre-cast mana=%g cost=%g modifier=%g maximum=%g\n", manaBefore,
				talent->GetManaCost(), hero->GetManaCostModifier(), hero->GetMaxEnergy());
			CObj<PFAbilityInstance> cast = hero->UseTalent(talent, point);
			check(cast && hero->GetMana() == manaBefore - 70, "B13 full original A1 cast pays authored mana once through real talent use");
			const auto step = [&](int count) { for (int i = 0; i < count; ++i) isolated.world->Step(.1f, .1f); };
			step(8);
			std::printf("B13 full Plane A1 burn=%d strength=%d mode=%d initial hp=%g mana=%g cooldown=%g counter=%g active=%d damage=%g dot=%g radius=%g\n",
				burn, strength, mode, victim->GetLife(), hero->GetMana(), talent->GetCurrentCooldown(), hero->GetVariable("A1_PlaneRocket"),
				cast ? cast->GetActiveApplicatorsCount() : -1, damage.value, dot.value, radius.value);
			check(damage.Succeeded() && std::fabs(10000 - victim->GetLife() - damage.value) < .01f,
				"B13 full original A1 program delivers exactly its initial area hit before the authored periodic offset");
			check(ally->GetLife() == 10000 && outside->GetLife() == 10000 && hero->GetLife() == 10000,
				"B13 original A1 area selector excludes allies, the caster and out-of-radius enemies");
			CObj<PFAbilityInstance> second;
			if (mode)
			{
				check(hero->GetVariable("A1_PlaneRocket") == 1 && talent->IsReady(),
					"B13 original upgrade retains one fired rocket and refreshes the repeat-launch cooldown");
				step(4);
				check(victim->GetLife() <= 10000 - damage.value,
					"B13 first upgraded rocket retains its own original periodic damage zone");
				if (mode == 2)
				{
					second = hero->UseTalent(talent, point);
					check(second && hero->GetMana() == manaBefore - 140,
						"B13 second original rocket casts inside the wait window and pays once");
				}
				step(58);
			}
			else step(42);
			std::printf("B13 full Plane A1 final hp=%g mana=%g cooldown=%g counter=%g active=%d finished=%d\n",
				victim->GetLife(), hero->GetMana(), talent->GetCurrentCooldown(), hero->GetVariable("A1_PlaneRocket"),
				cast ? cast->GetActiveApplicatorsCount() : -1, cast && cast->IsFinished());
			const int shots = mode == 2 ? 2 : 1;
			const float zoneDamage = shots * 3 * dot.value;
			check(damage.Succeeded() && dot.Succeeded() && std::fabs(10000 - victim->GetLife() - shots * damage.value - zoneDamage) < .05f,
				"B13 each fired original A1 rocket owns one three-tick zone, without damage from an unfired rocket");
			check(hero->GetMana() == manaBefore - shots * 70 && talent->GetCurrentCooldown() > 0 && talent->GetCurrentCooldown() < 10,
				"B13 original A1 keeps one mana charge and its live authored cooldown during effect retirement");
			check(cast && cast->GetActiveApplicatorsCount() == 0 && cast->IsFinished(),
				"B13 complete original A1 program and delayed zone release the cast rather than leaving a live ability behind");
			check(hero->GetVariable("A1_PlaneRocket") == 0 && (!second || second->IsFinished()),
				"B13 original A1 resets its rocket counter and retires any second cast");
			const float life = victim->GetLife(); step(110);
			check(victim->GetLife() == life && talent->IsReady(), "B13 finished A1 cannot repeat damage and becomes usable after its natural cooldown");
			for (auto* unit : units) unit->Cleanup();
		}
		ai->globalConstants = savedConstants; ai->abilityAndDamageScalingParams = savedScaling;
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 full authored Plane A1 checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
