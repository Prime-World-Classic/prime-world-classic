/** Direct authored FindApplicator calls must be aliases of the same bounded original lookup. */
static void DirectNamedFormulaB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	ApplicatorQueryFixtureB12 f(check, isolated.world);
	auto first = f.Apply(f.Stat("Shared", "false", "20"));
	auto foreign = f.Apply(f.Stat("Shared", "true", "30"), 2);
	if (!first || !foreign) return;
	struct Scope { const char* name; ApplicatorSearchType type; };
	for (const IMiscFormulaPars* misc : {static_cast<const IMiscFormulaPars*>(first.GetPtr()),
		static_cast<const IMiscFormulaPars*>(foreign.GetPtr()), static_cast<const IMiscFormulaPars*>(f.data[0].GetPtr())})
	for (const Scope scope : {Scope{"GLOBAL", GLOBAL}, {"UNIT", UNIT}, {"ABILITY", ABILITY}, {"DISPATCH", DISPATCH}, {"UNIT_AND_SUMMONS", UNIT_AND_SUMMONS}})
	for (const char* root : {"sender.", "receiver.", "pFirst->", "pSecond->"})
	{
		const bool sender = std::strcmp(root, "sender.") == 0 || std::strcmp(root, "pFirst->") == 0;
		const auto* found = (sender ? f.sender : f.receiver)->FindApplicator("Shared", misc, scope.type);
		const std::string lookup = std::string(root) + "FindApplicator(\"Shared\", pMisc, " + scope.name + ")";
		for (bool equal : {false, true})
		{
			const auto value = LinuxBootstrap::EvaluateSelectorConditionFormula(lookup + (equal ? " == pMisc" : " != pMisc"), f.sender, f.receiver, misc);
			check(value.Succeeded() && value.value == float(equal ? found == misc : found != misc),
				"B13 direct named calls preserve actual identities across original scopes and sender/candidate spellings");
		}
		const auto value = LinuxBootstrap::EvaluateSelectorConditionFormula(lookup + "->GetVariable(\"Duration\")", f.sender, f.receiver, misc);
		check(value.Succeeded() && value.value == found->GetVariable("Duration"), "B13 direct named calls read the same reviewed field and original missing-match fallback");
	}
	for (const char* expression : {"pFirst->FindApplicator(\"Shared\",pMisc,UNIT)",
		"pFirst->FindApplicator(\"Shared\",pMisc,UNIT)+1", "pFirst->FindApplicator(\"Shared\",pMisc,UNIT)==pMisc+1",
		"2*pFirst->FindApplicator(\"Shared\",pMisc,UNIT)==pMisc", "!pFirst->FindApplicator(\"Shared\",pMisc,UNIT)==pMisc",
		"pFirst->FindApplicator(\"Shared\",pMisc,UNIT,0)==pMisc", "pFirst->FindApplicator(\"Shared\",pFirst,UNIT)==pMisc",
		"pFirst->FindApplicator(\"Shared\",pMisc,1)==pMisc", "pFirst->FindApplicator(\"Shared\",pMisc,UNKNOWN)==pMisc",
		"pFirst->FindApplicator(name,pMisc,UNIT)==pMisc", "pFirst->FindApplicator(\"Sh\" \"ared\",pMisc,UNIT)==pMisc",
		"pFirst->FindApplicator(\"Sh\\x61red\",pMisc,UNIT)==pMisc", "pFirst->FindApplicator(\"Shared\",pMisc,UNIT)->GetVariable(\"statMod\")"})
		for (bool lazy : {false, true})
			check(!LinuxBootstrap::EvaluateApplicatorNumericFormula(std::string(lazy ? "true ? 7 : " : "") + expression, *first).Succeeded(),
				"B13 direct named calls reject pointers, alternate contexts, indices, unreviewed fields and nonliteral arguments before evaluation");
	const auto spacing = LinuxBootstrap::EvaluateApplicatorNumericFormula("pSecond->FindApplicator ( \"shared\" , pMisc , UNIT ) == pMisc", *first);
	check(spacing.Succeeded() && spacing.value == 1, "B13 direct literal names retain case sensitivity and bounded argument whitespace");
	const auto lazy = LinuxBootstrap::EvaluateUnitNumericFormula("true ? 7 : pSecond->FindApplicator(\"Shared\",pMisc,UNIT)==pMisc", nullptr, nullptr);
	check(lazy.Succeeded() && lazy.value == 7, "B13 an unselected valid direct lookup requires no unit or miscellaneous context");
	check(!LinuxBootstrap::EvaluateUnitNumericFormula("pFirst->FindApplicator(\"Shared\",pMisc,UNIT)==pMisc", f.sender, f.receiver).Succeeded(),
		"B13 selected direct lookup cannot invent a missing miscellaneous identity");
	std::printf("B13 direct named formula checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Completed use states release command ownership without cancelling their still-scheduled payload. */
static void CompletedUseDispatchB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (int mode = 0; mode < 6; ++mode)
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		auto sender = Unit(isolated.world, NDb::FACTION_FREEZE, 40), receiver = Unit(isolated.world, NDb::FACTION_BURN, 20);
		sender->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); receiver->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		auto* raw = new NDb::Ability; raw->isUnitFreeAfterCast = mode != 2;
		raw->manaCost.sString = "70"; raw->flags = static_cast<NDb::EAbilityFlags>(0);
		if (mode == 1) { auto* timed = new NDb::DispatchByTime; timed->lifeTime.sString = ".5"; raw->dispatch = timed; }
		else { auto* immediate = new NDb::DispatchImmediate; immediate->instantApply = false; raw->dispatch = immediate; }
		auto* damage = new NDb::DamageApplicator; damage->damage.sString = "25"; damage->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
		raw->applicators.push_back(damage); NDb::Ptr<NDb::Ability> db = raw;
		CObj<PFAbilityData> data = mode == 5 ? new B9TimedAbility(sender.GetPtr(), db)
			: new PFAbilityData(sender.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL);
		CObj<PFAbilityInstance> cast = sender->CreateAbilityInstance(data, Target(receiver.GetPtr()));
		check(cast && sender->GetMana() == 130 && receiver->GetLife() == 1000 && !cast->IsFinished(),
			"B13 completed-state fixture pays once while its real dispatch still owns a deferred payload");
		if (!cast) continue;
		CObj<B9UseState> state = new B9UseState(sender.GetPtr(), Target(receiver.GetPtr()), db); state->Instance(cast.GetPtr());
		if (mode == 3) sender->SetChannellingProgress(.5f);
		if (mode == 4) cast->Cancel();
		state->OnLeave(); state->OnLeave();
		check(state->Instance() == cast.GetPtr(), "B13 completed use-state leave retains original instance metadata without releasing simulation ownership");
		for (int i = 0; i < 4; ++i) isolated.world->Step(.25f, .25f);
		check(receiver->GetLife() == (mode < 2 ? 975 : 1000) && cast->IsFinished() && sender->GetMana() == 130,
			"B13 completed immediate/timed payload survives state leave, while blocking, channeling, explicit cancellation and unfinished cast work still cancel");
		isolated.world->Step(.5f, .5f);
		check(receiver->GetLife() == (mode < 2 ? 975 : 1000) && sender->GetMana() == 130,
			"B13 repeated completion and later simulation cannot deliver or pay twice");
		sender->Cleanup(); receiver->Cleanup();
	}
	std::printf("B13 completed use dispatch checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Buy and command the unchanged Plane A2, then observe original payments, statuses and terrain healing. */
static void AuthoredNamedCastB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		const auto db = NDb::Get<NDb::Talent>(NDb::DBID("/Items/Talents/Class/Plane/Ability_A2.TALENT.xdb"));
		check(db && db->applicators.size() == 3 && db->castLimitations.size() == 1,
			"B13 Plane A2 cast uses its complete original descriptor and original named-status restriction");
		if (db) for (bool native : {false, true}) for (bool reset : {false, true})
		{
			NatureOverrideFixtureB13 f(check); if (!f.World()) continue;
			auto hero = CommandHeroB10(f.World()), control = CommandHeroB10(f.World());
			for (const auto& unit : {hero, control})
			{
				unit->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
				unit->GetStat(NDb::STAT_LIFE)->SetCoreValue(3000); unit->GetStat(NDb::STAT_ENERGY)->SetCoreValue(1000);
				unit->GetStat(NDb::STAT_STRENGTH)->SetCoreValue(50); unit->GetStat(NDb::STAT_INTELLECT)->SetCoreValue(40);
				check(unit->TeleportTo(native ? CVec2(10, 0) : CVec2(25, 10)), "B13 Plane cast fixture uses real native/enemy terrain positioning");
				unit->UpdateNatureType(); unit->InstallTalent(db, false); unit->AddGold(10000);
			}
			f.World()->Step(.1f, .1f);
			for (const auto& unit : {hero, control}) { unit->SetHealth(100); unit->ChangeMana(500 - unit->GetMana()); }
			CObj<NCore::WorldCommand> command = CreateCmdUseTalent(hero.GetPtr(), 0, 0, Target(hero.GetPtr()), true);
			check(command && !command->CanExecute(), "B13 unbought original A2 cannot enter the production command queue");
			for (const auto& unit : {hero, control})
			{
				const float gold = unit->GetGold(); const int cost = unit->GetTalent(0, 0)->GetNaftaCost();
				check(unit->ActivateTalent(0, 0) && unit->GetGold() == gold - cost, "B13 original Plane A2 purchase retains the actual talent cost");
			}
			CObj<PFTalent> talent = hero->GetTalent(0, 0);
			const auto status = [&]() { return hero->FindApplicator("A2PlaneStatus", talent.GetPtr(), UNIT) != talent.GetPtr(); };
			check(!talent->CheckCastLimitations(Target(hero.GetPtr())) && !status(), "B13 shipped direct FindApplicator restriction admits an absent same-owner A2 status");
			check(command && command->CanExecute(), "B13 purchased original A2 enters the real command path");
			if (command) command->Execute(f.World());
			check(hero->HasQueuedStates() && hero->GetMana() == 500 && talent->GetLastUseStep() == -1,
				"B13 queued A2 has not paid or produced a cast before world simulation");
			for (int i = 0; i < 10; ++i) f.World()->Step(.1f, .1f);
			check(status() && talent->GetLastUseStep() >= 0 && std::fabs(control->GetMana() - hero->GetMana() - 80) < .001f
				&& talent->GetCurrentCooldown() > 12 && talent->GetCurrentCooldown() <= 14,
				"B13 simulation casts original A2 once, spends 80 mana and starts its 14-second cooldown");
			check(std::fabs(hero->GetMoveSpeed() - control->GetMoveSpeed() * 1.5f) < .001f,
				"B13 original A2 also applies its complete shared 50-percent haste status");
			const float mana = hero->GetMana();
			if (reset) talent->DropCooldown(true, 0, false);
			const float remaining = talent->GetCurrentCooldown();
			check(talent->CheckCastLimitations(Target(hero.GetPtr())) && !hero->UseTalent(talent.GetPtr(), Target(hero.GetPtr()))
				&& hero->GetMana() == mana && talent->GetCurrentCooldown() == remaining,
				"B13 same-owner named status prevents a duplicate cast even when cooldown is explicitly cleared, without another payment");
			for (int i = 0; i < 55; ++i) f.World()->Step(.1f, .1f);
			const auto total = LinuxBootstrap::EvaluateUnitNumericFormula("cA2_HealTotal", hero, hero, talent);
			const float healed = hero->GetLife() - control->GetLife();
			std::printf("B13 Plane A2 cycle: native=%d reset=%d status=%d healed=%g total=%g speed=%g/%g mana=%g/%g\n",
				native, reset, status(), healed, total.value, hero->GetMoveSpeed(), control->GetMoveSpeed(), hero->GetMana(), control->GetMana());
			check(total.Succeeded() && std::fabs(healed - (native ? total.value : 0.f)) < .01f,
				"B13 all six original half-second-offset healing ticks apply only on native terrain, beyond matched passive regeneration");
			check(!status() && std::fabs(hero->GetMoveSpeed() - control->GetMoveSpeed()) < .001f
				&& !talent->CheckCastLimitations(Target(hero.GetPtr())), "B13 natural status expiry restores speed and clears the original named cast restriction");
			if (!reset)
			{
				const float before = hero->GetMana();
				check(talent->GetCurrentCooldown() > 6 && !hero->UseTalent(talent.GetPtr(), Target(hero.GetPtr())) && hero->GetMana() == before,
					"B13 A2 cooldown independently prevents another payment after its shorter named status has expired");
				for (int i = 0; i < 80; ++i) f.World()->Step(.1f, .1f);
				check(talent->GetCurrentCooldown() == 0 && talent->GetPreparedness() == 1 && !status(),
					"B13 natural 14-second cooldown expiry restores readiness without restarting the expired effects");
			}
			const float beforeMana = hero->GetMana();
			CObj<PFAbilityInstance> replacement = hero->UseTalent(talent.GetPtr(), Target(hero.GetPtr()));
			for (int i = 0; i < 3; ++i) f.World()->Step(.1f, .1f);
			check(replacement && status() && talent->GetCurrentCooldown() > 13 && hero->GetMana() < beforeMana - 79,
				"B13 expired named status permits a fresh original A2 cast and a new payment/cooldown cycle");
			hero->GetTalentsSet()->CleanSet(); control->GetTalentsSet()->CleanSet(); hero->Cleanup(); control->Cleanup();
		}
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 authored named cast checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
