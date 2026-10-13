/** Real casts supply source identity; damage still goes through the production factory/history. */
struct DamageExtendedFixtureB13 : DamageHistoryFixtureB13
{
	using DamageHistoryFixtureB13::DamageHistoryFixtureB13;
	int Cast(PFAbilityData* ability)
	{
		casts.push_back(new PFAbilityInstance(ability, Target(receiver.GetPtr()), false));
		return static_cast<int>(casts.size()) - 1;
	}
	CObj<PFAbilityData> Ability(NDb::EAbilityTypeId type)
	{
		return new PFAbilityData(sender.GetPtr(), db, type, false, false);
	}
};

/** Both extended getters intentionally query SENT history, as in original Windows source. */
static void DamageExtendedPairB13(Checks& check, PFBaseUnit* unit, unsigned mask,
	const IMiscFormulaPars* context, float expected, float dt = 1)
{
	const IUnitFormulaPars* pars = unit;
	check(pars->GetDamageTakenEx(dt, mask, context) == expected, "extended Taken preserves original sent-history routing");
	check(pars->GetDamageDealtEx(dt, mask, context) == expected, "extended Dealt selects actual classified damage");
}

/** Ability slots, any-bit matching and same-data filtering are independent of cast identity. */
static void DamageExtendedAbilitiesB13(Checks& check)
{
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	DamageExtendedFixtureB13 f(check, isolated.world); f.Tick(10);
	const unsigned masks[] = {DMG_ABILITY_ATTACK, DMG_ABILITY1, DMG_ABILITY2, DMG_ABILITY3,
		DMG_ABILITY4, DMG_ABILITY5, 0, 0, 0, 0, DMG_ABILITY_SPECIAL, 0};
	for (int type = 0; type <= NDb::ABILITYTYPEID_PORTAL; ++type)
	{
		if (type == NDb::ABILITYTYPEID_TALENT) continue;
		f.Clear(); auto data = f.Ability(static_cast<NDb::EAbilityTypeId>(type));
		auto a = f.Hit("7.75", f.Cast(data));
		f.Hit("2.75", f.Cast(data));
		for (unsigned mask : {DMG_ABILITY_ATTACK, DMG_ABILITY1, DMG_ABILITY2, DMG_ABILITY3,
			DMG_ABILITY4, DMG_ABILITY5, DMG_ABILITY_SPECIAL, DMG_TALENT})
			DamageExtendedPairB13(check, f.sender, mask, nullptr, masks[type] & mask ? 9 : 0);
		DamageExtendedPairB13(check, f.sender, DMG_UNIT, nullptr, 9);
		DamageExtendedPairB13(check, f.sender, DMG_APPLICATOR_ABILITY, a, 9);
		DamageExtendedPairB13(check, f.sender, DMG_APPLICATOR_ABILITY | DMG_TALENT, a, 9);
		DamageExtendedPairB13(check, f.sender, 0, nullptr, 0);
		DamageExtendedPairB13(check, f.sender, 1u << 31, nullptr, 0);
		DamageExtendedPairB13(check, f.sender, DMG_UNIT | (1u << 31), nullptr, 9);
		DamageExtendedPairB13(check, f.receiver, DMG_UNIT, a, 0);
		check(f.receiver->GetDamageTaken(1) == 9, "simple received history remains distinct from both extended getters");
		// Same descriptor and owner, but a different PFAbilityData, must not match.
		auto other = f.Ability(static_cast<NDb::EAbilityTypeId>(type));
		auto b = f.Hit("5.75", f.Cast(other));
		DamageExtendedPairB13(check, f.sender, DMG_APPLICATOR_ABILITY, a, 9);
		DamageExtendedPairB13(check, f.sender, DMG_APPLICATOR_ABILITY, b, 5);
		DamageExtendedPairB13(check, f.sender, DMG_UNIT | DMG_APPLICATOR_ABILITY, a, 14);
		DamageExtendedPairB13(check, f.sender, DMG_APPLICATOR_ABILITY, data, 0);
		DamageExtendedPairB13(check, f.sender, DMG_APPLICATOR_ABILITY, nullptr, 0);
	}
}

/** Use real PFTalent descriptors across every rarity/tier; do not cast ordinary ability data. */
static void DamageExtendedTalentsB13(Checks& check)
{
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	DamageExtendedFixtureB13 f(check, isolated.world); f.Tick(10);
	CObj<B9CastHero> hero = B9MaleHero(isolated.world, NDb::FACTION_FREEZE);
	const unsigned rarities[] = {DMG_TALENT_CLASS, DMG_TALENT_ORDINARY, DMG_TALENT_GOOD,
		DMG_TALENT_EXCELLENT, DMG_TALENT_MAGNIFICENT, DMG_TALENT_EXCLUSIVE, DMG_TALENT_OUTSTANDING};
	const unsigned tiers[] = {DMG_TALENT_TIER1, DMG_TALENT_TIER2, DMG_TALENT_TIER3,
		DMG_TALENT_TIER4, DMG_TALENT_TIER5, DMG_TALENT_TIER6};
	for (int rarity = 0; rarity < 7; ++rarity)
		for (int tier = 0; tier < 6; ++tier)
		{
			hero->CleanupHistoryApplicators(); f.Clear();
			auto* raw = new ApplicatorQueryDbB12<NDb::Talent>("/Mock/ExtendedTalent.xdb");
			raw->rarity = static_cast<NDb::ETalentRarity>(rarity); raw->minLevel = tier;
			NDb::Ptr<NDb::Talent> db = raw;
			CObj<PFTalent> talent = new PFTalent(hero.GetPtr(), db, 1, 0, 0, false);
			auto hit = f.Hit("3.75", f.Cast(talent));
			DamageExtendedPairB13(check, hero, DMG_TALENT, nullptr, 3);
			for (int i = 0; i < 7; ++i) DamageExtendedPairB13(check, hero, rarities[i], nullptr, i == rarity ? 3 : 0);
			for (int i = 0; i < 6; ++i) DamageExtendedPairB13(check, hero, tiers[i], nullptr, i == tier ? 3 : 0);
			DamageExtendedPairB13(check, hero, tiers[tier] | rarities[(rarity + 1) % 7], nullptr, 3);
			DamageExtendedPairB13(check, hero, DMG_ABILITY_ATTACK, nullptr, 0);
			DamageExtendedPairB13(check, hero, DMG_APPLICATOR_ABILITY | DMG_ABILITY_ATTACK, hit, 3);
		}
	// The all-damage fast path does not need classification, even for a corrupt talent subtype.
	f.Clear(); auto invalid = f.Ability(NDb::ABILITYTYPEID_TALENT); f.Hit("4", f.Cast(invalid));
	DamageExtendedPairB13(check, f.sender, DMG_UNIT, nullptr, 4);
	DamageExtendedPairB13(check, f.sender, DMG_TALENT, nullptr, 0);
	hero->CleanupHistoryApplicators(); hero->Cleanup();
}

/** Exact authoring syntax uses bounded masks and the existing lazy numeric evaluator. */
static void DamageExtendedFormulasB13(Checks& check)
{
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	DamageExtendedFixtureB13 f(check, isolated.world); f.Tick(10);
	auto attack = f.Ability(NDb::ABILITYTYPEID_BASEATTACK);
	auto hit = f.Hit("7.75", f.Cast(attack)); f.Hit("2.75");
	const auto run = [&](const char* expression, float expected) {
		const auto result = LinuxBootstrap::EvaluateApplicatorNumericFormula(expression, *hit);
		check(result.Succeeded() && result.value == expected, expression);
	};
	for (const char* expression : {"sender.GetDamageDealtEx(1, DMG_ABILITY_ATTACK, pMisc)",
		"pFirst->GetDamageTakenEx(min(1,2), DMG_ATTACK)",
		"sender.GetDamageDealtEx(1, DMG_ABILITY_ATTACK | DMG_TALENT_CLASS, pMisc)",
		"sender.GetDamageDealtEx(1, DMG_ATTACK | DMG_ATTACK)",
		"sender.GetDamageDealtEx(1, DMG_APPLICATOR_ABILITY, pMisc)",
		"sender.GetDamageTakenEx(1, DMG_APPL_ABLT, pMisc)",
		"sender.GetDamageDealtEx(receiver.GetDamageTaken(1), DMG_ATTACK)"}) run(expression, 7);
	run("sender.GetDamageDealtEx(1, DMG_UNIT)", 9);
	run("sender.GetDamageTakenEx(1, DMG_SPECIAL)", 2);
	run("receiver.GetDamageTakenEx(1, DMG_UNIT, pMisc)", 0);
	run("pSecond->GetDamageDealtEx(1, DMG_UNIT)", 0);
	run("sender.GetDamageDealtEx(0, DMG_UNIT)", 0);
	run("sender.GetDamageDealtEx(-1, DMG_UNIT)", 0);
	run("sender.GetDamageDealtEx(1, DMG_ATTACK) > 0 && sender.GetDamageDealtEx(1, DMG_TALENT) == 0", 1);
	for (const char* good : {"1 ? 8 : sender.GetDamageDealtEx(1/0,DMG_UNIT,pMisc)",
		"1 ? 8 : sender.GetDamageTakenEx(missing,DMG_APPL_ABLT)",
		"1 ? 8 : pSecond->GetDamageDealtEx(1,DMG_UNIT)"})
	{
		const auto result = LinuxBootstrap::EvaluateUnitNumericFormula(good, nullptr, nullptr);
		check(result.Succeeded() && result.value == 8, "unselected extended queries need no unit/applicator context");
	}
	for (const char* bad : {"sender.GetDamageDealtEx(1)", "sender.GetDamageDealtEx(1,DMG_UNIT,pMisc,1)",
		"sender.GetDamageDealtEx(1,DMG_UNIT,pFirst)", "sender.GetDamageDealtEx(1,DMG_UNKNOWN)",
		"sender.GetDamageDealtEx(1,DMG_UNIT||DMG_ATTACK)", "sender.GetDamageDealtEx(1,DMG_UNIT+DMG_ATTACK)",
		"sender.GetDamageDealtEx(1,DMG_UNIT|)", "sender.GetDamageDealtEx(1,|DMG_UNIT)",
		"sender.GetDamageDealtEx(1,DMG_UNIT DMG_ATTACK)", "sender.GetDamageDealtEx(1,0,pMisc)",
		"sender.GetDamageDealtEx(1e39,DMG_UNIT)", "sender.GetDamageDealtEx(1/0,DMG_UNIT)",
		"sender.GetDamageDealtEx(1,DMG_APPL_ABLT)", "__history_s_dealt_ex(1,1)",
		"0 ? __history_s_dealt_ex(1,1) : 8", "1 ? 8 : sender.GetDamageDealtEx(1,DMG_UNIT,pFirst)"})
		check(!LinuxBootstrap::EvaluateApplicatorNumericFormula(bad, *hit).Succeeded(),
			"extended formula rejects unsupported masks, pointers, arity and reserved-name injection");
	// A production damage consumer sees preceding history but cannot count itself in flight.
	auto actual = f.Hit("sender.GetDamageDealtEx(1, DMG_ATTACK | DMG_TALENT_CLASS, pMisc) * 2");
	check(actual->GetDamageDealed() == 14, "actual damage factory consumes original filtered history through authored formula");
	float& amount = hit.GetPtr()->*DamageHistoryAmountMemberB13(DamageHistoryAmountTagB13());
	amount = std::numeric_limits<float>::quiet_NaN();
	check(!LinuxBootstrap::EvaluateApplicatorNumericFormula("sender.GetDamageDealtEx(1,DMG_ATTACK) <= 0", *actual).Succeeded(),
		"malformed selected history never becomes a successful zero-damage predicate");
	run("sender.GetDamageDealtEx(1,DMG_SPECIAL)", 16);
	amount = 7.75f;
	f.Tick(1);
	run("sender.GetDamageDealtEx(1,DMG_UNIT)", 0);
}

/** Shared clock, suffix and integer guards remain checked after classification. */
static void DamageExtendedGuardsB13(Checks& check)
{
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	DamageExtendedFixtureB13 f(check, isolated.world); f.Tick(10);
	auto old = f.Hit("7.75"), newest = f.Hit("2.75");
	old->SetMarkerTime(9); newest->SetMarkerTime(9.5f);
	DamageExtendedPairB13(check, f.sender, DMG_SPECIAL, nullptr, 2);
	DamageExtendedPairB13(check, f.sender, DMG_SPECIAL, nullptr, 0, .5f);
	DamageExtendedPairB13(check, f.sender, DMG_SPECIAL, nullptr, 9, std::nextafter(1.f, 2.f));
	auto stat = f.Apply(f.Stat("ExtendedBoundary")); stat->Stop(); MemorizeApplicator(stat); stat->SetMarkerTime(0);
	DamageExtendedPairB13(check, f.sender, DMG_UNIT, nullptr, 0);
	stat->SetMarkerTime(10);
	const auto history = ApplicatorHistoryListB13(f.sender, true);
	const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
	const auto rejected = [&](float dt) {
		float value = 123;
		check(!f.sender->TryGetDamageTakenEx(dt, DMG_SPECIAL, nullptr, value) && value == 123,
			"checked extended Taken failure preserves caller output");
		check(!f.sender->TryGetDamageDealtEx(dt, DMG_SPECIAL, nullptr, value) && value == 123,
			"checked extended Dealt failure preserves caller output");
	};
	for (float bad : {nan, inf, -inf}) rejected(bad);
	float& clock = isolated.world->*ApplicatorHistoryMemberB13(ApplicatorHistoryClockTagB13());
	for (float bad : {nan, inf, -1.f}) { clock = bad; rejected(1); }
	clock = 10;
	for (float bad : {nan, inf, -1.f, 11.f}) { stat->SetMarkerTime(bad); rejected(1); }
	stat->SetMarkerTime(10);
	float& amount = newest.GetPtr()->*DamageHistoryAmountMemberB13(DamageHistoryAmountTagB13());
	for (float bad : {nan, inf, -inf, 2147483648.f}) { amount = bad; rejected(1); }
	amount = -2.75f;
	DamageExtendedPairB13(check, f.sender, DMG_SPECIAL, nullptr, -2);
	amount = 2.75f;
	float result = -1;
	check(f.sender->TryGetDamageDealtEx(0, DMG_UNIT, nullptr, result) && result == 0,
		"valid empty extended interval is a successful zero, not an error sentinel");
	result = 123;
	check(!f.sender->TryGetDamageTakenEx(1, DMG_APPL_ABLT, f.data[0], result) && result == 123,
		"ability data cannot impersonate an applicator identity context");
	check(ApplicatorHistoryListB13(f.sender, true) == history, "failed and successful classified traversals preserve ring order and owners");
	std::string mask = "DMG_UNIT";
	for (std::size_t i = 1; i < LinuxBootstrap::NumericFormulaMaxSymbols; ++i) mask += "|DMG_UNIT";
	const auto evaluate = [&](const std::string& bits) {
		return LinuxBootstrap::EvaluateUnitNumericFormula("sender.GetDamageDealtEx(1," + bits + ")", f.sender, f.receiver);
	};
	check(evaluate(mask).value == 2, "mask union preserves duplicate bits at its bounded term limit");
	check(!evaluate(mask + "|DMG_UNIT").Succeeded(), "oversized mask union is rejected rather than bypassing parser limits");
	std::string nested = "1";
	for (std::size_t i = 0; i <= LinuxBootstrap::NumericFormulaMaxNesting; ++i)
		nested = "sender.GetDamageDealtEx(" + nested + ",DMG_UNIT)";
	check(!LinuxBootstrap::EvaluateUnitNumericFormula(nested, f.sender, f.receiver).Succeeded(),
		"nested extended calls retain bounded structural translation");
}

/** A paid class talent and live stat consumer exercise the shipped G567 expression. */
static void DamageExtendedConsumersB13(Checks& check)
{
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	DamageExtendedFixtureB13 f(check, isolated.world); f.Tick(10);
	CObj<B9CastHero> hero = B9MaleHero(isolated.world, NDb::FACTION_FREEZE);
	f.receiver->ChangeFaction(NDb::FACTION_BURN);
	auto* raw = new NDb::Talent;
	raw->rarity = NDb::TALENTRARITY_CLASS; raw->minLevel = 2; raw->naftaCost = 0;
	raw->manaCost.sString = "20"; raw->cooldownTime.sString = "1"; raw->useRange.sString = "0";
	raw->applicators.push_back(f.DamageDb("7.75"));
	auto* constants = new NDb::UnitConstantsContainer;
	auto* step = new NDb::UnitConstant; step->name = "STEP"; step->var.sString = "1";
	constants->vars.push_back(step); raw->constants = constants;
	NDb::Ptr<NDb::Talent> db = raw;
	CObj<PFTalent> talent = new PFTalent(hero.GetPtr(), db, 1, 0, 0, false); talent->Activate();
	CObj<PFAbilityInstance> paid = hero->UseTalent(talent, Target(f.receiver.GetPtr()));
	check(paid && hero->GetMana() == 180 && f.receiver->GetHealth() == 992.25f,
		"actual class-talent cast pays mana and memorizes real fractional damage");
	if (paid)
	{
		f.casts.push_back(paid);
		auto statDb = f.Stat("ExtendedLive");
		const_cast<NDb::StatModApplicator*>(statDb.GetPtr())->modifier.addValue.sString =
			"sender.GetDamageDealtEx(cSTEP, DMG_TALENT_CLASS, pMisc )";
		auto app = f.Apply(statDb, static_cast<int>(f.casts.size()) - 1);
		check(app && app->IsAlive() && f.receiver->GetStatValue(NDb::STAT_STRENGTH) == 27,
			"shipped G567 class-damage expression drives a real modifier through constants and pMisc");
		f.Tick(1);
		check(app->IsAlive() && f.receiver->GetStatValue(NDb::STAT_STRENGTH) == 20,
			"world stepping updates the same modifier at the strict history interval boundary");
		auto hit = f.Hit("5.75", f.Cast(talent)); f.Tick(.125f);
		check(f.receiver->GetStatValue(NDb::STAT_STRENGTH) == 25,
			"new class damage updates the existing modifier without stale formula results");
	}
	hero->CleanupHistoryApplicators(); hero->Cleanup();
}

static void DamageExtendedB13(Checks& check)
{
	DamageExtendedAbilitiesB13(check);
	DamageExtendedTalentsB13(check);
	DamageExtendedFormulasB13(check);
	DamageExtendedGuardsB13(check);
	DamageExtendedConsumersB13(check);
}
