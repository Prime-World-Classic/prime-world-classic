/** Distances use the original two-dimensional positions, not the redirected receiver or render height. */
static void ManeuverDistanceB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	NaftaFixtureB13 f(isolated.world);
	check(f.sender->TeleportTo(CVec2(5, 5)) && f.receiver->TeleportTo(CVec2(8, 9)),
		"B13 distance fixture places real registered moving units");
	auto* raw = WrapperFactoryFixtureB12::Stat(); raw->applyTarget = NDb::APPLICATORAPPLYTARGET_ABILITYOWNER;
	NDb::Ptr<NDb::BaseApplicator> db = raw;
	const Target point(CVec3(17, 10, 90)); PFApplCreatePars pars(f.instance, point); pars.pDBAppl = db;
	auto app = CreateApplicator(pars);
	check(app && app->GetReceiver() == f.sender.GetPtr(), "B13 distance fixture redirects its receiver without changing the raw point target");
	if (!app) return;
	for (const char* expression : {"d(sPos,pMisc->GetTargetPos())", "d( pMisc->GetTargetPos(), sPos )",
		"d(sPos,appl.GetTargetPos())", "d(rPos,pMisc->GetTargetPos())"})
	{
		const auto value = LinuxBootstrap::EvaluateApplicatorNumericFormula(expression, *app);
		check(value.Succeeded() && value.value == 13, "B13 distance reads original XY endpoints and ignores visual altitude");
	}
	for (const char* expression : {"d(sPos,rPos)", "d(tPos,sPos)", "d(rPos,rPos)+5"})
	{
		const auto value = LinuxBootstrap::EvaluateUnitNumericFormula(expression, f.sender, f.receiver, f.ability);
		check(value.Succeeded() && value.value == 5, "B13 distance keeps sender receiver and compatibility target identities");
	}
	check(f.sender->TeleportTo(CVec2(17, 10)), "B13 distance owner moves through the original teleport API");
	const auto moved = LinuxBootstrap::EvaluateApplicatorNumericFormula("d(sPos,pMisc->GetTargetPos())", *app);
	check(moved.Succeeded() && moved.value == 0, "B13 distance rereads positions on each evaluation");
	for (const char* expression : {"d(sPos)", "d(sPos,rPos,sPos)", "d(sPos,1)", "d(sPos,rPos+1)",
		"d(s Pos,rPos)", "d(sPos,pMisc->GetTargetPos(1))", "d(sPos,pMisc->GetTargetPos().x)",
		"d(sPos,pMisc->GetObjectParent()->GetTargetPos())", "__distance_sm"})
	{
		check(!LinuxBootstrap::EvaluateApplicatorNumericFormula(expression, *app).Succeeded(),
			"B13 distance rejects arbitrary vector syntax methods and transport injection");
		check(!LinuxBootstrap::EvaluateApplicatorNumericFormula(std::string("true ? 1 : ") + expression, *app).Succeeded(),
			"B13 malformed distance syntax remains rejected in unselected branches");
	}
	const auto absent = LinuxBootstrap::EvaluateUnitNumericFormula("d(sPos,pMisc->GetTargetPos())", f.sender, f.receiver, f.ability);
	const auto lazy = LinuxBootstrap::EvaluateUnitNumericFormula("true ? 7 : d(sPos,pMisc->GetTargetPos())", nullptr, nullptr);
	check(!absent.Succeeded() && lazy.Succeeded() && lazy.value == 7,
		"B13 selected missing misc context fails without invoking the ability assertion while unselected geometry stays lazy");
	const Target unitTarget(f.receiver.GetPtr()); PFApplCreatePars unitPars(f.instance, unitTarget); unitPars.pDBAppl = db;
	auto unitApp = CreateApplicator(unitPars);
	check(unitApp && f.receiver->TeleportTo(CVec2(17, 22)), "B13 distance raw object target moves independently of its redirected receiver");
	if (unitApp)
	{
		const auto value = LinuxBootstrap::EvaluateApplicatorNumericFormula("d(sPos,pMisc->GetTargetPos())", *unitApp);
		check(value.Succeeded() && value.value == 12, "B13 distance object target uses its live engine position");
	}
	NRandom::RandomGenerator expected(*isolated.world->GetRndGen()); const int draw = expected.Next(1, 9);
	const auto random = LinuxBootstrap::EvaluateApplicatorNumericFormula("random(1,9)+d(sPos,pMisc->GetTargetPos())", *app);
	check(random.Succeeded() && random.value == draw && GlyphRngEqualB13(*isolated.world->GetRndGen(), expected),
		"B13 valid distance commits exactly the original seeded random expression");
	for (const CVec3& bad : {CVec3(std::numeric_limits<float>::quiet_NaN(), 1, 0), CVec3(1, std::numeric_limits<float>::infinity(), 0),
		CVec3(std::numeric_limits<float>::max(), 1, 0), CVec3(1.e20f, -1.e20f, 0)})
	{
		const Target badPoint(bad); PFApplCreatePars badPars(f.instance, badPoint); badPars.pDBAppl = db;
		CObj<ParentScaleProbeB13> unsafe = new ParentScaleProbeB13(badPars);
		check(unsafe->Init(), "B13 numeric geometry fixture retains an original applicator context");
		expected = *isolated.world->GetRndGen();
		const auto value = LinuxBootstrap::EvaluateApplicatorNumericFormula("random(1,9)+d(sPos,pMisc->GetTargetPos())", *unsafe);
		check(!value.Succeeded() && GlyphRngEqualB13(*isolated.world->GetRndGen(), expected),
			"B13 nonfinite or overflow distance fails without committing earlier random draws");
	}
	std::printf("B13 maneuver distances checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** FormulaBuilder's switch is eager, while flags retain the real per-unit bit mask. */
static void ManeuverHelpersB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	WrapperFactoryFixtureB12 f(isolated.world);
	for (const auto& sample : {std::pair<const char*, float>{"s(true,7,2)", 7}, {"s(false,7,2)", 2},
		{"s(-2,16777217,0)", 16777216}, {"s(0,7.5f,-2.5f)", -2.5f}})
	{
		const auto value = LinuxBootstrap::EvaluateUnitNumericFormula(sample.first, nullptr, nullptr);
		check(value.Succeeded() && value.value == sample.second, "B13 original eager boolean switch retains float argument narrowing and nonzero truth");
	}
	for (const char* invalid : {"s(true,7,cMissing)", "s(false,1/0,7)", "s(true,7,1e39)", "s(false,1e39,7)"})
		check(!LinuxBootstrap::EvaluateUnitNumericFormula(invalid, f.sender, f.receiver, f.ability).Succeeded(),
			"B13 eager switch evaluates and validates both original float arguments");
	f.receiver->AddFlag(NDb::UNITFLAG_FORBIDMOVE);
	for (unsigned mask : {0u, unsigned(NDb::UNITFLAG_FORBIDMOVE), unsigned(NDb::UNITFLAG_FORBIDMOVE | NDb::UNITFLAG_FORBIDATTACK), 0x80000000u})
	{
		for (bool receiver : {false, true})
		{
			const std::string expression = std::string(receiver ? "rFlag(" : "sFlag(") + std::to_string(mask) + ")";
			const auto value = LinuxBootstrap::EvaluateUnitNumericFormula(expression, f.sender, f.receiver, f.ability);
			check(value.Succeeded() && value.value == (receiver ? f.receiver->GetFlag(mask) : f.sender->GetFlag(mask)),
				"B13 original flag getter preserves sender receiver zero and combined-mask semantics");
		}
	}
	for (const char* invalid : {"s(true,1)", "s(true,1,2,3)", "rFlag()", "sFlag(1,2)", "rFlag(-1)", "rFlag(4294967296)"})
		check(!LinuxBootstrap::EvaluateUnitNumericFormula(invalid, f.sender, f.receiver, f.ability).Succeeded(),
			"B13 malformed switch arity and unsafe unsigned flag conversions fail closed");
	const auto lazy = LinuxBootstrap::EvaluateUnitNumericFormula("true ? 7 : rFlag(1)", nullptr, nullptr);
	check(lazy.Succeeded() && lazy.value == 7 && !LinuxBootstrap::EvaluateUnitNumericFormula("rFlag(1)", nullptr, nullptr).Succeeded(),
		"B13 unselected flag access needs no unit while selected missing receivers fail");
	std::printf("B13 maneuver helpers checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Drive the unmodified A3 graph through real purchases, creation channel, flight and retirement. */
static void PlaneManeuverAuthoredB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		const auto db = NDb::Get<NDb::Talent>(NDb::DBID("/Items/Talents/Class/Plane/Ability_A3.TALENT.xdb"));
		const auto upgrade = NDb::Get<NDb::Talent>(NDb::DBID("/Items/Talents/Class/Plane/Ability_A3u.TALENT.xdb"));
		const auto superior = NDb::Get<NDb::Talent>(NDb::DBID("/Items/Talents/Class/Plane/Ability_A3_SUP.TALENT.xdb"));
		const auto rules = NDb::Get<NDb::AILogicParameters>(NDb::DBID("/GameLogic/AILogic.AILP.xdb"));
		check(db && upgrade && superior && rules && db->persistentId == "Plane_A3" && upgrade->persistentId == "Plane_A3u"
			&& superior->persistentId == "Plane_A3_SUP",
			"B13 complete original A3 shield upgrade and global rules load through the owning resource cache");
		auto* ai = const_cast<NDb::AILogicParameters*>(NDb::SessionRoot::GetRoot()->logicRoot->aiLogic.GetPtr());
		const auto savedConstants = ai->globalConstants; const auto savedScaling = ai->abilityAndDamageScalingParams;
		if (rules) { ai->globalConstants = rules->globalConstants; ai->abilityAndDamageScalingParams = rules->abilityAndDamageScalingParams; }
		if (db && upgrade && superior && rules) for (bool burn : {false, true}) for (int mode : {0, 1, 2})
		for (bool cancel : {false, true}) for (float distance : {12.f, 24.f, 90.f})
		{
			RoutingFixtureB13 isolated(check); if (!isolated.world) continue;
			const bool shield = mode != 0;
			auto hero = CommandHeroB10(isolated.world); hero->ChangeFaction(burn ? NDb::FACTION_BURN : NDb::FACTION_FREEZE);
			auto attacker = B9MaleHero(isolated.world, burn ? NDb::FACTION_FREEZE : NDb::FACTION_BURN);
			attacker->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
			check(attacker->TeleportTo(CVec2(150, 100)), "B13 shield damage sender stays outside the maneuver path");
			hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
			for (auto stat : {NDb::STAT_ENERGYREGENERATION, NDb::STAT_ENERGYREGENERATIONABSOLUTE}) hero->GetStat(stat)->SetCoreValue(0);
			hero->GetStat(NDb::STAT_STRENGTH)->SetCoreValue(burn ? 50 : 160);
			hero->GetStat(NDb::STAT_INTELLECT)->SetCoreValue(burn ? 160 : 50);
			hero->GetVariableVWM("TalentsCdMul")->SetBaseValue(burn ? .5f : 0);
			hero->InstallTalent(db, false);
			PFTalentsSet::SetInfo info; info[0][0].pTalentDesc = db; info[0][0].refineRate = 1;
			if (shield) { info[0][1].pTalentDesc = upgrade; info[0][1].refineRate = 1; }
			if (mode == 2) { info[0][2].pTalentDesc = superior; info[0][2].refineRate = 1; }
			hero->GetTalentsSet()->LoadSet(hero.GetPtr(), info); hero->AddGold(2000);
			for (int i = 0; i <= mode; ++i)
			{
				CObj<NCore::WorldCommand> buy = CreateCmdActivateTalent(hero.GetPtr(), 0, i);
				check(buy && buy->CanExecute(), "B13 original maneuver talent purchase passes real command admission");
				if (buy) buy->Execute(isolated.world);
				check(hero->GetTalent(0, i)->IsActivated(), "B13 original maneuver purchase activates its real talent and passives");
			}
			check(hero->GetGold() == 2000 - 390 - (shield ? 440 : 0) - (mode == 2 ? 800 : 0),
				"B13 maneuver purchases pay the original 390, 440 and 800 prime prices");
			CObj<PFTalent> talent = hero->GetTalent(0, 0);
			const auto capacity = LinuxBootstrap::EvaluateUnitNumericFormula("cA3_DamageCapacity", hero, hero, talent);
			check(capacity.Succeeded() && capacity.value == (mode == 2 ? 466 : 311),
				"B13 maneuver shield scales its larger stat and purchased upper upgrade with original global endpoints");
			check(hero->TeleportTo(CVec2(5, 15)), "B13 maneuver caster is separated from the descriptor seed hero");
			const Target point(CVec3(5 + distance, 15, 0)); const CVec2 origin = hero->GetPos();
			const float mana = hero->GetMana(); CObj<PFAbilityInstance> cast = hero->UseTalent(talent, point);
			check(cast && hero->IsInChannelling() && hero->GetMana() == mana && hero->GetPos() == origin,
				"B13 original maneuver begins a stationary creation channel without premature mana payment");
			bool flying = false, trapProtected = true, stationaryChannel = true; float maxShield = 0;
			int paymentStep = 0, firstFlight = 0, lastFlight = 0, shieldHitStep = -1;
			for (int i = 0; i < 150; ++i)
			{
				if (i == 1 && cancel) hero->CancelChannelling();
				isolated.world->Step(.1f, .1f);
				flying = flying || hero->IsMovingSpecial(); maxShield = Max(maxShield, hero->GetShield());
				if (shieldHitStep >= 0 && i == shieldHitStep + 1)
					check(hero->GetShield() == capacity.value - 50, "B13 full maneuver shield publishes consumed damage on its next owning tick");
				if (shieldHitStep < 0 && hero->GetShield() > 0)
				{
					shieldHitStep = i; auto hit = DirectDamage(); hit.pSender = attacker.GetPtr();
					hit.amount = 50; hit.damageType = NDb::APPLICATORDAMAGETYPE_PURE;
					const float health = hero->GetLife();
					check(hero->OnDamage(hit) == 0 && hero->GetLife() == health,
						"B13 complete original maneuver shield absorbs an actual enemy damage event before health");
				}
				if (!paymentStep && hero->GetMana() < mana) paymentStep = i + 1;
				if (hero->IsInChannelling()) stationaryChannel = stationaryChannel && hero->GetPos() == origin;
				if (hero->IsMovingSpecial())
				{
					if (!firstFlight) firstFlight = i + 1;
					lastFlight = i + 1; trapProtected = trapProtected && hero->GetVariable("NoTraps") == 1;
				}
			}
			std::printf("B13 A3 burn=%d mode=%d cancel=%d distance=%g pos=%g,%g payStep=%d flight=%d..%d mana=%g cd=%g maxShield=%g active=%d\n",
				burn, mode, cancel, distance, hero->GetPos().x, hero->GetPos().y, paymentStep, firstFlight, lastFlight,
				hero->GetMana(), talent->GetCurrentCooldown(), maxShield, cast ? cast->GetActiveApplicatorsCount() : -1);
			const float channel = Max(distance / 30 - (mode == 2 ? 2 : 0), .1f);
			check(stationaryChannel && (cancel ? paymentStep == 0 : paymentStep * .1f >= channel && paymentStep * .1f <= channel + .21f),
				"B13 original channel uses distance divided by 30 and the upper upgrade's two-second reduction with a tenth-second floor");
			check(trapProtected && (cancel || (firstFlight > paymentStep && std::fabs((lastFlight - firstFlight + 1) * .1f - distance / 30) <= .11f)),
				"B13 original movement retains 30-unit speed and trap protection throughout flight");
			check(hero->GetMana() == mana - (cancel ? 0 : 100), "B13 maneuver completion pays exactly once and cancellation pays nothing");
			check(cancel ? hero->GetPos() == origin && !flying : fabs(hero->GetPos() - point.GetPosition().AsVec2D()) < .01f && flying,
				"B13 original maneuver flies to its selected endpoint only after successful channel completion");
			check(maxShield == (!cancel && shield ? capacity.value : 0) && hero->GetShield() == 0,
				"B13 complete maneuver applies only its purchased shield and retires it at the authored deadline");
			check(cast && cast->IsFinished() && cast->GetActiveApplicatorsCount() == 0 && !hero->IsInChannelling()
				&& !hero->IsMovingSpecial() && hero->GetVariable("NoTraps") == 0,
				"B13 complete maneuver clears channel flight trap immunity and all owning effects");
			check(!hero->CheckFlag(NDb::UNITFLAG_FORBIDMOVE) && !hero->CheckFlag(NDb::UNITFLAG_FORBIDATTACK)
				&& !hero->CheckFlag(NDb::UNITFLAG_FORBIDUSETALENTS) && !talent->IsForbidded(),
				"B13 maneuver retirement restores the original movement attack and talent permissions");
			const float extension = Max(distance / 4 - 4, 0.f) * (burn ? .5f : 1.f);
			check(cancel ? talent->IsReady() : std::fabs(talent->GetCurrentCooldown() - extension) < .01f,
				"B13 original cancellation refunds cooldown while successful distance extension honors the live talent multiplier");
			for (int i = 0; i < 200; ++i) isolated.world->Step(.1f, .1f);
			check(talent->IsReady() && hero->GetMana() == mana - (cancel ? 0 : 100) && hero->GetShield() == 0,
				"B13 finished maneuver returns to readiness without repeat payment or shield recreation");
			hero->Cleanup(); attacker->Cleanup();
		}
		ai->globalConstants = savedConstants; ai->abilityAndDamageScalingParams = savedScaling;
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 full authored Plane A3 checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
