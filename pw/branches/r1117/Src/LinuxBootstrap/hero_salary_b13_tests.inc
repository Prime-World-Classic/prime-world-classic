/** Inspect real salary state; inject getter results only for malformed numeric-boundary tests. */
struct SalaryHeroB13 : PFBaseHero
{
	OBJECT_METHODS(PFBaseHero::typeId, SalaryHeroB13);
public:
	SalaryHeroB13() = default;
	SalaryHeroB13(PFWorld* world, const SpawnInfo& spawn, NDb::EUnitType type)
		: PFBaseHero(world, spawn, type, NDb::FACTION_FREEZE, NDb::FACTION_FREEZE) {}
	bool injectBonus = false;
	float bonusResult = 0;
	float GetVariable(const char* name) const override
	{
		return injectBonus && std::strcmp(name, UnitVariables::szNaftaForTimeBonus) == 0
			? bonusResult : PFBaseHero::GetVariable(name);
	}
	float Balance() const { return gold; }
	float Offset() const { return giveWorldGoldOffset; }
	void Offset(float value) { giveWorldGoldOffset = value; }
	void Isolate(bool value) { isolated = value; }
};

/** Install explicit mock map rules before hero construction, through the production AI API. */
static void SalarySettingsB13(PFWorld* world, int delay, int amount, bool dead = true, float resurrection = 0)
{
	NDb::Ptr<NDb::AdvMapSettings> settings = new NDb::AdvMapSettings;
	auto* raw = const_cast<NDb::AdvMapSettings*>(settings.GetPtr());
	raw->battleStartDelay = delay; raw->primeSettings.giveWorldPrimeAmount = amount;
	raw->primeSettings.giveWorldPrimeToDead = dead; raw->primeSettings.giveWorldPrimeRessurectDelay = resurrection;
	world->GetAIWorld()->SetMapData(nullptr, settings);
}

/** Create real male/female/clone heroes with deterministic mock stats and no automatic attacks. */
static CObj<SalaryHeroB13> SalaryCreateB13(PFWorld* world, bool clone = false, bool female = false)
{
	auto source = B8BehaviourCreatureDb(world, NDb::FACTION_FREEZE);
	auto* raw = new NDb::Hero; raw->stats = source->stats; raw->targetingParams = source->targetingParams;
	PFBaseHero::SpawnInfo spawn; spawn.pHero = raw; spawn.isClone = clone;
	spawn.placement = Placement(CVec3(5, 5, 0), QNULL, CVec3(1, 1, 1));
	CObj<SalaryHeroB13> hero = new SalaryHeroB13(world, spawn, female ? NDb::UNITTYPE_HEROFEMALE : NDb::UNITTYPE_HEROMALE);
	hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET | NDb::UNITFLAG_FORBIDATTACK);
	return hero;
}

/** Compare actual world scheduling with the original strict, non-catch-up salary timer. */
static void HeroSalaryTimingB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (bool female : {false, true}) for (bool isolated : {false, true})
	{
		B8BehaviourForeignWorld f(check); if (!f.world) continue;
		SalarySettingsB13(f.world, 2, 4);
		auto hero = SalaryCreateB13(f.world, false, female); hero->Isolate(isolated);
		auto clone = SalaryCreateB13(f.world, true, female);
		hero->GetVariableVWM(UnitVariables::szNaftaForTimeBonus)->SetBaseValue(.75f);
		check(hero->Offset() == 2 && clone->Offset() == 2, "B13 salary starts at the map's battle delay for every hero identity");
		float offset = 2, balance = 0;
		for (float dt : {0.f, 1.f, 1.f, .125f, 1.f, 0.f, .25f, 8.f, 0.f, .5f})
		{
			offset -= dt;
			if (offset < 0) { balance += 4.75f; offset = 1; }
			f.world->Step(dt, dt);
			check(hero->Balance() == balance && hero->Offset() == offset,
				"B13 scheduled salary preserves strict zero boundary, fractional bonus and one payout per large tick");
			check(clone->Balance() == 0 && clone->Offset() == 2,
				"B13 clones neither earn ordinary world salary nor advance its timer");
		}
		hero->Cleanup(); clone->Cleanup();
	}
	{
		B8BehaviourForeignWorld f(check); if (f.world)
		{
			SalarySettingsB13(f.world, 3, 4); f.world->Step(1.25f, 1.25f);
			auto hero = SalaryCreateB13(f.world);
			check(hero->Offset() == 1.75f, "B13 late-created hero uses the remaining AI battle delay rather than original map delay");
			f.world->Step(1.75f, 1.75f); check(hero->Balance() == 0, "B13 late-created salary still waits beyond exact zero");
			f.world->Step(.25f, .25f); check(hero->Balance() == 4, "B13 late-created salary begins on the next eligible world tick");
			hero->Cleanup();
		}
	}
	std::printf("B13 hero salary timing checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Death eligibility freezes the original timer; resurrection applies max(delay, remaining). */
static void HeroSalaryDeathB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (bool deadIncome : {false, true}) for (float resurrection : {0.f, 3.f})
	{
		B8BehaviourForeignWorld f(check); if (!f.world) continue;
		SalarySettingsB13(f.world, 2, 4, deadIncome, resurrection);
		auto hero = SalaryCreateB13(f.world); hero->SetForbidRespawn(true);
		f.world->Step(.5f, .5f);
		HeroCorpseKillB13(f.world, hero);
		check(hero->IsDead() && !hero->IsObjectDead(), "B13 salary fixture uses real persistent hero death");
		float offset = 1.5f, balance = 0;
		for (float dt : {.5f, 1.f, .25f, 2.f})
		{
			if (deadIncome) { offset -= dt; if (offset < 0) { balance += 4; offset = 1; } }
			f.world->Step(dt, dt);
			check(hero->IsDead() && hero->Balance() == balance && hero->Offset() == offset,
				"B13 real dead-world scheduling follows authored dead-income policy without advancing excluded timers");
		}
		hero->Resurrect(); offset = Max(resurrection, offset);
		check(!hero->IsDead() && hero->Offset() == offset && hero->Balance() == balance,
			"B13 resurrection delays salary without erasing a larger existing battle delay or paying immediately");
		f.world->Step(offset, offset);
		check(hero->Balance() == balance && hero->Offset() == 0, "B13 resurrected salary retains exact-zero boundary");
		f.world->Step(.25f, .25f);
		check(hero->Balance() == balance + 4 && hero->Offset() == 1, "B13 actual resurrected hero resumes salary once");
		hero->Cleanup();
	}
	std::printf("B13 hero salary death checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Invalid arithmetic cannot poison serialized timers or int-facing balances. */
static void HeroSalaryNumericB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	B8BehaviourForeignWorld f(check); if (!f.world) return;
	SalarySettingsB13(f.world, 2, 4); auto hero = SalaryCreateB13(f.world); hero->AddGold(100);
	const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
	for (float bad : {-1.f, nan, inf, -inf})
	{
		hero->Step(bad);
		check(hero->Balance() == 100 && hero->Offset() == 2, "B13 invalid hero dt cannot advance or pay salary");
	}
	for (float bonus : {nan, inf, -inf, std::numeric_limits<float>::max(), 2147483648.f})
	{
		// ValueWithModifiers clamps raw values; inject the actual getter result to test this boundary.
		hero->Offset(0); hero->injectBonus = true; hero->bonusResult = bonus;
		hero->Step(.25f);
		check(hero->Balance() == 100 && hero->Offset() == 1, "B13 invalid or overflowing due salary rejects balance mutation but retains the normal cadence");
	}
	hero->injectBonus = false;
	hero->GetVariableVWM(UnitVariables::szNaftaForTimeBonus)->SetBaseValue(-4.5f);
	hero->Offset(0); hero->Step(.25f);
	check(hero->Balance() == 99.5f, "B13 finite signed salary bonus retains existing native balance-adjustment semantics");
	hero->GetVariableVWM(UnitVariables::szNaftaForTimeBonus)->SetBaseValue(0);
	for (float bad : {nan, inf, -inf})
	{
		hero->Offset(bad); hero->Step(.25f);
		check(hero->Balance() == 99.5f && (std::isnan(bad) ? std::isnan(hero->Offset()) : hero->Offset() == bad),
			"B13 corrupt serialized salary timers are rejected without an accidental award or silent repair");
	}
	hero->Offset(-std::numeric_limits<float>::max()); hero->Step(std::numeric_limits<float>::max());
	check(hero->Balance() == 99.5f && hero->Offset() == -std::numeric_limits<float>::max(),
		"B13 overflowing timer subtraction leaves salary state unchanged");
	struct DelayScope
	{
		NGlobal::VariantValue saved = NGlobal::GetVar("hero_give_world_gold_delay");
		~DelayScope() { NGlobal::SetVar("hero_give_world_gold_delay", saved, STORAGE_NONE); }
	} delayScope;
	for (float delay : {-1.f, 0.f, .5f, 3.f, nan, inf})
	{
		NGlobal::SetVar("hero_give_world_gold_delay", NGlobal::VariantValue(delay), STORAGE_NONE);
		hero->Offset(0); const float balance = hero->Balance(); hero->Step(.25f);
		check(hero->Balance() == balance + (std::isfinite(delay) ? 4 : 0)
			&& hero->Offset() == (std::isfinite(delay) ? delay : 0),
			"B13 developer salary interval keeps finite original values and rejects nonfinite timers atomically");
	}
	hero->Cleanup();
	std::printf("B13 hero salary numeric checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Use unchanged MOBA/ARAM settings and exact float countdown in actual small engine worlds. */
static void HeroSalaryAuthoredB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		for (bool aram : {false, true})
		{
			const auto settings = NDb::Get<NDb::AdvMapSettings>(NDb::DBID(aram ? "/Maps/Multiplayer/ARAM/_.ADMPSTTN.xdb" : "/Maps/Multiplayer/MOBA/_.ADMPSTTN.xdb"));
			check(settings && settings->battleStartDelay == (aram ? 30 : 60)
				&& settings->primeSettings.giveWorldPrimeAmount == (aram ? 15 : 4), "B13 original map salary values load without modifying assets");
			if (!settings) continue;
			B8BehaviourForeignWorld f(check); if (!f.world) continue;
			f.world->GetAIWorld()->SetMapData(nullptr, settings); auto hero = SalaryCreateB13(f.world);
			float offset = float(settings->battleStartDelay), balance = 0; int payments = 0;
			for (int tick = 0; tick < (settings->battleStartDelay + 10) * 4; ++tick)
			{
				offset -= .25f;
				if (offset < 0) { balance += settings->primeSettings.giveWorldPrimeAmount; offset = 1; ++payments; }
				f.world->Step(.25f, .25f);
				check(hero->Balance() == balance && hero->Offset() == offset, "B13 every original-map salary tick matches Windows float countdown and exact payout");
			}
			check(payments == 8 && hero->GetGold() == (aram ? 120 : 32), "B13 original cadence pays eight times in ten seconds at quarter-second world ticks");
			hero->Cleanup();
		}
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 authored hero salary checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
