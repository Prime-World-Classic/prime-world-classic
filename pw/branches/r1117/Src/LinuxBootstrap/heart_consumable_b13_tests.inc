/** Construct original heroes with explicit authored gender, without replacing identity getters. */
static CObj<B9CastHero> HeartHeroB13(PFWorld* world, NDb::EGender gender)
{
	const auto source = B8BehaviourCreatureDb(world, NDb::FACTION_FREEZE);
	NDb::Hero* raw = new NDb::Hero; raw->stats = source->stats;
	raw->targetingParams = source->targetingParams; raw->gender = gender;
	NDb::Ptr<NDb::Hero> db = raw; PFBaseHero::SpawnInfo spawn; spawn.pHero = db;
	spawn.placement = Placement(CVec3(5, 5, 0), QNULL, CVec3(1, 1, 1));
	CObj<B9CastHero> hero = new B9CastHero(world, spawn, NDb::FACTION_FREEZE);
	hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); return hero;
}

/** Real attachment distinguishes human player identity from authored hero identity and bot fallback. */
static CObj<PFPlayer> HeartPlayerB13(PFWorld* world, PFBaseHero* hero, NCore::ESex::Enum sex, int userId = 1984)
{
	CObj<PFPlayer> player = new PFPlayer(world, hero->GetObjectId(), NCore::ETeam::Team1,
		NCore::ETeam::Team1, userId, sex, false, 0, false, false);
	hero->AttachToPlayer(player.GetPtr()); return player;
}

/** Use actual identity getters for formulas, including player replacement and candidate selection. */
static void HeartIdentityB13(Checks& check, PFWorld* world)
{
	const int before = check.count, failed = check.failed;
	for (NDb::EGender gender : {NDb::GENDER_MALE, NDb::GENDER_FEMALE})
	{
		auto hero = HeartHeroB13(world, gender);
		const int authored = gender == NDb::GENDER_MALE ? ZZSEX_MALE : ZZSEX_FEMALE;
		check(hero->GetZZimaSex() == authored, "B13 unattached hero initializes player sex from its original descriptor");
		for (bool bot : {false, true}) for (auto sex : {NCore::ESex::Undefined, NCore::ESex::Male, NCore::ESex::Female})
		{
			auto player = HeartPlayerB13(world, hero, sex, bot ? -1 : 1984);
			const int expected = bot ? authored : int(sex);
			check(hero->GetZZimaSex() == expected && hero->IsMale() == (gender == NDb::GENDER_MALE),
				"B13 original player attachment distinguishes player sex from hero gender and uses authored gender for bots");
			for (const char* expression : {"sZZimaSex", "rZZimaSex", "tZZimaSex", "sender.GetZZimaSex()",
				"receiver.GetZZimaSex()", "pFirst->GetZZimaSex()", "pSecond->GetZZimaSex()"})
			{
				const auto result = LinuxBootstrap::EvaluateUnitNumericFormula(expression, hero, hero);
				check(result.Succeeded() && result.value == expected, "B13 identity formulas use the original live integer getter");
			}
			hero->DetachFromPlayer();
			check(hero->GetZZimaSex() == authored, "B13 detachment restores authored identity without retaining a former player's sex");
		}
		hero->Cleanup();
	}
	for (const auto& value : {std::pair<const char*, int>{"ZZSEX_UNDEFINED", ZZSEX_UNDEFINED},
		{"ZZSEX_MALE", ZZSEX_MALE}, {"ZZSEX_FEMALE", ZZSEX_FEMALE}})
	{
		const auto result = LinuxBootstrap::EvaluateUnitNumericFormula(value.first, nullptr, nullptr);
		check(result.Succeeded() && result.value == value.second, "B13 exact FormulaPars identity constants require no unit context");
	}
	auto ordinary = Unit(world, NDb::FACTION_FREEZE, 10);
	const auto undefined = LinuxBootstrap::EvaluateUnitNumericFormula("sZZimaSex == ZZSEX_UNDEFINED", ordinary, nullptr);
	check(undefined.Succeeded() && undefined.value == 1, "B13 nonhero identity retains original undefined rather than invented male identity");
	for (const char* expression : {"sZZimaSex", "rZZimaSex", "receiver.GetZZimaSex()"})
	{
		check(!LinuxBootstrap::EvaluateUnitNumericFormula(expression, nullptr, nullptr).Succeeded(), "B13 selected identity needs its corresponding unit");
		const auto lazy = LinuxBootstrap::EvaluateUnitNumericFormula(std::string("true ? 7 : ") + expression, nullptr, nullptr);
		check(lazy.Succeeded() && lazy.value == 7, "B13 lazy identity access does not require an unselected unit");
	}
	for (const char* expression : {"receiver.GetZZimaSex(1)", "pFirst->SetZZimaSex()", "receiver.GetZZimaSex()()", "ZZSEX_OTHER"})
		check(!LinuxBootstrap::EvaluateUnitNumericFormula(expression, ordinary, ordinary).Succeeded(), "B13 identity binding does not expose arbitrary methods or invented constants");
	auto sender = HeartHeroB13(world, NDb::GENDER_MALE), receiver = HeartHeroB13(world, NDb::GENDER_FEMALE);
	const auto candidate = LinuxBootstrap::EvaluateSelectorConditionFormula("sZZimaSex != rZZimaSex", sender, receiver, nullptr);
	check(candidate.Succeeded() && candidate.value == 1, "B13 selector identity retains the actual candidate rather than substituting sender");
	sender->Cleanup(); receiver->Cleanup(); ordinary->Cleanup();
	std::printf("B13 heart identity checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Shipped heart items retain their original cast restrictions, projectile, cooldown and full healing lifetime. */
static void HeartConsumablesB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		for (const char* path : {"/Items/Consumables/HealHeart.ARCT.xdb", "/Items/Consumables/OffHealHeart.ARCT.xdb"})
		{
			const auto db = NDb::Get<NDb::Consumable>(NDb::DBID(path));
			check(db && db->castLimitations.size() == 1, "B13 shipped heart item retains its actual cast predicate");
			if (!db) continue;
			const bool femaleOnly = std::string(path).find("OffHealHeart") != std::string::npos;
			for (auto senderSex : {NCore::ESex::Undefined, NCore::ESex::Male, NCore::ESex::Female})
			for (auto targetSex : {NCore::ESex::Undefined, NCore::ESex::Male, NCore::ESex::Female})
			{
				B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
				auto sender = HeartHeroB13(isolated.world, NDb::GENDER_FEMALE);
				auto receiver = HeartHeroB13(isolated.world, NDb::GENDER_MALE);
				auto senderPlayer = HeartPlayerB13(isolated.world, sender, senderSex);
				auto targetPlayer = HeartPlayerB13(isolated.world, receiver, targetSex, 1985);
				check(receiver->TeleportTo(CVec2(6, 5)), "B13 heart item target uses a real one-meter projectile path");
				receiver->SetHealth(100);
				check(receiver->GetLifeRegen() == 0 && receiver->GetLifeRegenAbs() == 0, "B13 heart fixture excludes passive regeneration");
				check(sender->TakeConsumable(db, 2, NDb::CONSUMABLEORIGIN_DEFAULT, 0), "B13 original heart item enters actual inventory");
				CObj<PFConsumable> item = sender->GetConsumable(0); if (!item) continue;
				const bool allow = femaleOnly ? targetSex == NCore::ESex::Female : targetSex != senderSex;
				const Target target(receiver.GetPtr());
				check((item->GetAbility()->CheckCastLimitations(target) == nullptr) == allow,
					"B13 authored heart restriction samples player sex, not hero gender or an unconditional permit");
				const float mana = sender->GetMana();
				CObj<PFAbilityInstance> cast = sender->UseConsumable(0, target, false);
				check(bool(cast) == allow && item->GetQuantity() == (allow ? 1 : 2) && sender->GetMana() == mana,
					"B13 heart admission consumes exactly one allowed charge without changing its authored zero mana cost");
				check(item->GetAbility()->GetCurrentCooldown() == (allow ? 60 : 0), "B13 denied heart cast starts no cooldown while allowed cast retains authored sixty seconds");
				check(receiver->GetHealth() == 100, "B13 heart projectile does not heal before its original world travel");
				for (int i = 0; i < 40; ++i) isolated.world->Step(.01f, .01f);
				check(receiver->GetHealth() == (allow ? 130 : 100), "B13 arrived heart projectile applies its first authored thirty-point heal");
				for (int i = 0; i < 10; ++i) isolated.world->Step(.1f, .1f);
				check(receiver->GetHealth() == (allow ? 130 : 100), "B13 heart healing preserves its authored two-second period");
				for (int i = 0; i < 12; ++i) isolated.world->Step(.1f, .1f);
				check(receiver->GetHealth() == (allow ? 160 : 100), "B13 heart periodic counter does not incorrectly scale the second heal");
				for (int i = 0; i < 100; ++i) isolated.world->Step(.1f, .1f);
				check(receiver->GetHealth() == (allow ? 250 : 100) && (!cast || cast->GetActiveApplicatorsCount() == 0),
					"B13 original heart status delivers five authored heals and retires after ten seconds");
				check(!sender->UseConsumable(0, target, false) && item->GetQuantity() == (allow ? 1 : 2),
					"B13 active cooldown or unchanged cast restriction cannot spend another heart charge");
				sender->DetachFromPlayer(); receiver->DetachFromPlayer(); sender->Cleanup(); receiver->Cleanup();
			}
			B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
			auto sender = HeartHeroB13(isolated.world, NDb::GENDER_MALE);
			auto receiver = HeartHeroB13(isolated.world, NDb::GENDER_MALE);
			check(receiver->TeleportTo(CVec2(6, 5)), "B13 identity transition keeps a real projectile target");
			receiver->SetHealth(100);
			check(sender->TakeConsumable(db, 2, NDb::CONSUMABLEORIGIN_DEFAULT, 0), "B13 transition fixture acquires the unchanged heart item");
			CObj<PFConsumable> item = sender->GetConsumable(0); if (!item) continue;
			const Target target(receiver.GetPtr());
			check(!sender->UseConsumable(0, target, false) && item->GetQuantity() == 2 && item->GetAbility()->IsReady(),
				"B13 rejected heart cast retains both charges and readiness for a future valid target");
			auto player = HeartPlayerB13(isolated.world, receiver, NCore::ESex::Female);
			CObj<PFAbilityInstance> cast = sender->UseConsumable(0, target, false);
			check(cast && item->GetQuantity() == 1, "B13 same heart ability reevaluates changed player identity after a rejected cast");
			receiver->DetachFromPlayer();
			check(receiver->GetZZimaSex() == ZZSEX_MALE && item->GetAbility()->CheckCastLimitations(target) != nullptr,
				"B13 later casts see restored authored identity immediately after detachment");
			for (int i = 0; i < 40; ++i) isolated.world->Step(.01f, .01f);
			check(receiver->GetHealth() == 130, "B13 already accepted projectile is not retroactively canceled by a later identity change");
			for (int i = 0; i < 110; ++i) isolated.world->Step(.1f, .1f);
			check(receiver->GetHealth() == 250 && cast && cast->GetActiveApplicatorsCount() == 0,
				"B13 accepted heart lifetime remains owned through subsequent player detachment");
			sender->Cleanup(); receiver->Cleanup();
		}
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 authored heart consumables checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
