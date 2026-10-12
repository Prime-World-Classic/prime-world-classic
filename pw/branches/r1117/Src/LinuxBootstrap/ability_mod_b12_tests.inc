#include "../PF_GameLogic/PFApplMod.h"
#include "../PF_GameLogic/PFPredefinedUnitVariables.h"

namespace
{
/** Expose lifecycle observations only; formulas, ownership and payment are engine code. */
struct AbilityModProbeB12 : PFApplAbilityMod
{
	int enables = 0;
	explicit AbilityModProbeB12(const PFApplCreatePars& pars) : PFApplAbilityMod(pars) {}
	bool Initialize() { return Init(); }
	bool Run() { return Start(); }
	bool Tick(float dt) { return Step(dt); }
	void Finish() { Stop(); }
	using PFBaseApplicator::Block;
	using PFBaseApplicator::Unblock;
	void Enable() override { ++enables; PFApplAbilityMod::Enable(); }
	void Toggle(bool value) { SetEnabled(value); if (value) Enable(); else Disable(); }
};

/** Literal DB lifetime/enabled fields isolate coefficients from the parent's buff policy. */
struct AbilityModFixtureB12
{
	CObj<B9CastHero> hero;
	CObj<PFBaseUnit> sender;
	NDb::Ptr<NDb::Ability> sourceDb;
	CObj<PFAbilityData> source;
	NDb::Ptr<NDb::AbilityModApplicator> db;
	CObj<AbilityModProbeB12> app;
	const char* addName;
	const char* mulName;
	AbilityModFixtureB12(PFWorld* world, NDb::EAbilityModMode mode = NDb::ABILITYMODMODE_MANACOST,
		const char* mul = "sLife/2000", const char* add = "cAbilityAdd")
	{
		hero = B9MaleHero(world, NDb::FACTION_FREEZE);
		sender = Unit(world, NDb::FACTION_FREEZE, 40);
		NDb::Ability* ability = new NDb::Ability;
		NDb::UnitConstantsContainer* constants = new NDb::UnitConstantsContainer;
		NDb::UnitConstant* constant = new NDb::UnitConstant;
		constant->name = "AbilityAdd"; constant->var.sString = "rLife/200";
		constants->vars.push_back(constant); ability->constants = constants;
		sourceDb = ability;
		source = new PFAbilityData(sender.GetPtr(), sourceDb, NDb::ABILITYTYPEID_SPECIAL, false, false);
		NDb::AbilityModApplicator* raw = new NDb::AbilityModApplicator;
		raw->mode = mode; raw->targetAbilities = NDb::ABILITYIDFLAGS_TALENTS;
		raw->multValue.sString = mul; raw->addValue.sString = add;
		raw->enabled.sString = "true"; raw->startCondition.sString = "true"; raw->lifeTime.sString = "-1";
		db = raw;
		PFApplCreatePars pars(CObj<PFAbilityInstance>(), Target(hero.GetPtr()));
		pars.pWorld = world; pars.pOwner = sender; pars.pUpgraderAbilityData = source; pars.pDBAppl = db;
		app = new AbilityModProbeB12(pars);
		addName = mode == NDb::ABILITYMODMODE_COOLDOWN ? UnitVariables::szTalentsCdAdd : UnitVariables::szTalentsMcAdd;
		mulName = mode == NDb::ABILITYMODMODE_COOLDOWN ? UnitVariables::szTalentsCdMul : UnitVariables::szTalentsMcMul;
	}
	NDb::AbilityModApplicator& Edit() { return *const_cast<NDb::AbilityModApplicator*>(db.GetPtr()); }
	ValueWithModifiers* Add() const { return hero->SearchVariableVWM(addName); }
	ValueWithModifiers* Mul() const { return hero->SearchVariableVWM(mulName); }
	int Count() const { return Add() ? Add()->GetModifiers().size() : 0; }
	float Modified(float base = 40) const
	{
		return hero->GetModifiedAbilityValue(base, db->mode, NDb::ABILITYTYPEID_TALENT, sourceDb);
	}
	CObj<PFTalent> Talent() const
	{
		NDb::Talent* raw = new NDb::Talent;
		raw->manaCost.sString = "40"; raw->cooldownTime.sString = "20";
		raw->cooldownTimeSecondState.sString = "20"; raw->naftaCost = 0;
		raw->useRange.sString = "0"; raw->requireLineOfSight = false;
		NDb::Ptr<NDb::Talent> dbTalent = raw;
		CObj<PFTalent> talent = new PFTalent(hero.GetPtr(), dbTalent, 1, 0, 0, false);
		talent->Activate();
		return talent;
	}
	~AbilityModFixtureB12() { app->Finish(); hero->Cleanup(); }
};

static void AbilityModB12(Checks& check, PFWorld* world)
{
	for (auto mode : {NDb::ABILITYMODMODE_MANACOST, NDb::ABILITYMODMODE_COOLDOWN})
	{
		AbilityModFixtureB12 f(world, mode);
		check(f.app->Initialize() && !f.app->Run(), "ability modifier starts on a real male hero");
		check(f.Count() == 1 && f.Modified() == 25, "ability coefficients use live sender receiver and named constant");
		check(f.Add() && f.Mul() && f.Add()->GetValue() == 5 && f.Mul()->GetValue() == .5f,
			"talent coefficient variables match the hero modifier ring");
		CObj<PFTalent> talent = f.Talent();
		check(talent->GetManaCost() == (mode == NDb::ABILITYMODMODE_MANACOST ? 25 : 40), "real talent caches checked mana modifiers");
		check(talent->GetCooldown() == (mode == NDb::ABILITYMODMODE_COOLDOWN ? 15 : 20), "real talent caches checked cooldown modifiers");
		CObj<PFAbilityInstance> cast = f.hero->UseTalent(talent, Target(f.hero.GetPtr()));
		check(cast && f.hero->GetMana() == (mode == NDb::ABILITYMODMODE_MANACOST ? 175 : 160),
			"actual paid talent cast consumes its checked modified cost");
		const int addId = f.Add() ? f.Add()->GetModifiers().back().id : INVALID_MODIFIER_ID;
		const int mulId = f.Mul() ? f.Mul()->GetModifiers().back().id : INVALID_MODIFIER_ID;
		f.sender->SetHealth(500);
		check(f.Modified() == 25, "reading modified values does not reevaluate coefficients between steps");
		check(!f.app->Tick(.1f) && f.Modified() == 15, "dynamic ability modifier samples live coefficients on step");
		check(f.Add()->Retrieve(addId) && f.Mul()->Retrieve(mulId), "dynamic update retains both modifier identifiers");
		f.Edit().multValue.sString = "cMissing";
		f.app->Reset(); f.app->Reset();
		check(f.Modified() == 15 && f.Count() == 1 && f.Mul()->Retrieve(mulId), "Reset preserves cached values and serialized modifier ownership");
		check(!f.app->Tick(.1f) && f.Modified() == 15 && f.Mul()->GetValue() == .25f,
			"invalid dynamic formula after Reset retains the last good pair");
		f.app->Finish(); f.app->Finish();
		check(f.Count() == 0 && f.Mul()->GetModifiers().empty() && f.Modified() == 40,
			"Stop after Reset removes both owned modifiers exactly once");
	}
	for (bool badMul : {false, true})
		for (const char* bad : {"", "cMissing", "1/0", "sqrt(-1)", "1e39", "sLife+", "unknown(sLife)"})
	{
		AbilityModFixtureB12 f(world, NDb::ABILITYMODMODE_MANACOST, badMul ? bad : ".5", badMul ? "5" : bad);
		check(f.app->Initialize() && f.app->Run(), "invalid initial coefficients complete rather than create a fallback buff");
		check(!f.app->IsEnabled() && f.app->enables == 0 && f.Count() == 0 && f.Modified() == 40,
			"invalid initial pair leaves target and hero ring untouched");
		check(!f.Add() && !f.Mul(), "invalid initial formula does not create talent variables");
	}
	{
		AbilityModFixtureB12 f(world, NDb::ABILITYMODMODE_MANACOST, "", "5");
		f.Edit().multValue.compiledString = "unsupported-native-bytecode";
		check(f.app->Initialize() && f.app->Run() && f.Count() == 0, "compiled-only coefficient cannot become a neutral fallback");
	}
	for (bool badMul : {false, true}) for (const char* bad : {"cMissing", "1/0", "1e39"})
	{
		AbilityModFixtureB12 f(world);
		check(f.app->Initialize() && !f.app->Run(), "last-good dynamic fixture starts");
		if (badMul) { f.Edit().multValue.sString = bad; f.Edit().addValue.sString = "8"; }
		else { f.Edit().multValue.sString = ".25"; f.Edit().addValue.sString = bad; }
		check(!f.app->Tick(.1f) && f.Modified() == 25 && f.Add()->GetValue() == 5 && f.Mul()->GetValue() == .5f,
			"invalid dynamic half cannot publish either coefficient");
		f.Edit().multValue.sString = ".25"; f.Edit().addValue.sString = "8";
		check(!f.app->Tick(.1f) && f.Modified() == 18, "valid dynamic pair recovers after rejection");
	}
	{
		AbilityModFixtureB12 f(world, NDb::ABILITYMODMODE_MANACOST, ".5", "5");
		CObj<PFAbilityInstance> cast = new PFAbilityInstance(f.source, Target(f.hero.GetPtr()), true);
		PFApplCreatePars pars(cast, Target(f.hero.GetPtr())); pars.pDBAppl = f.db;
		CObj<AbilityModProbeB12> sibling = new AbilityModProbeB12(pars);
		check(sibling->Initialize() && ActivateApplicator(CObj<PFBaseApplicator>(sibling.GetPtr()), cast), "real activation owns a persistent sibling");
		f.Edit().multValue.sString = "cMissing";
		CObj<AbilityModProbeB12> invalid = new AbilityModProbeB12(pars);
		check(invalid->Initialize() && ActivateApplicator(CObj<PFBaseApplicator>(invalid.GetPtr()), cast), "invalid coefficient reaches actual activation cleanup");
		check(!invalid->IsAlive() && !invalid->IsEnabled() && invalid->enables == 0,
			"invalid initial activation completes without calling buff Enable");
		check(cast->GetActiveApplicatorsCount() == 1 && sibling->IsAlive() && f.Count() == 1 && f.Modified() == 25,
			"rejected activation cannot stop or decrement its live sibling");
		invalid->Finish();
		check(cast->GetActiveApplicatorsCount() == 1 && sibling->IsAlive(), "parent terminal Stop guard preserves a live sibling on repeated completion");
		sibling->Finish();
		check(cast->GetActiveApplicatorsCount() == 0 && f.Count() == 0, "sibling owns its final stop count and modifier cleanup");
	}
	{
		AbilityModFixtureB12 f(world, NDb::ABILITYMODMODE_MANACOST, ".5", "5");
		CObj<PFAbilityInstance> cast = new PFAbilityInstance(f.source, Target(f.sender.GetPtr()), true);
		PFApplCreatePars pars(cast, Target(f.sender.GetPtr())); pars.pDBAppl = f.db;
		CObj<AbilityModProbeB12> app = new AbilityModProbeB12(pars);
		check(app->Initialize() && ActivateApplicator(CObj<PFBaseApplicator>(app.GetPtr()), cast), "nonhero is admitted to the original completion path");
		check(!app->IsAlive() && !app->IsEnabled() && app->enables == 0 && cast->GetActiveApplicatorsCount() == 0,
			"nonhero completes without an ongoing buff or unmatched ability count");
		app->Finish();
	}
	{
		AbilityModFixtureB12 f(world, NDb::ABILITYMODMODE_MANACOST, ".5", "5");
		check(f.app->Initialize() && !f.app->Run(), "blocked lifecycle fixture starts");
		f.app->Toggle(true); f.app->Toggle(true);
		check(f.Count() == 1 && f.Mul()->GetModifiers().size() == 1, "repeated Enable cannot orphan coefficient modifiers");
		f.app->Block(); f.app->Reset();
		f.Edit().multValue.sString = "1/0";
		check(!f.app->Tick(.1f) && f.Count() == 0 && f.Modified() == 40, "blocked Step never reenables a modifier");
		f.app->Unblock();
		check(f.Count() == 0 && f.Modified() == 40, "invalid resumption does not reinstall stale coefficients");
		f.Edit().multValue.sString = ".25";
		check(!f.app->Tick(.1f) && f.Count() == 1 && f.Modified() == 15, "valid enabled step retries rejected resumption once");
		f.Edit().enabled.sString = "false";
		check(!f.app->Tick(.1f) && f.Count() == 0, "authored dynamic false removes ability and variable modifiers");
		f.Edit().enabled.sString = "true";
		check(!f.app->Tick(.1f) && f.Count() == 1 && f.Modified() == 15, "authored true restores one owned pair");
		for (float dt : {-1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
		{
			f.Edit().addValue.sString = "9";
			check(!f.app->Tick(dt) && f.Modified() == 15, "invalid delta cannot sample or mutate ability coefficients");
		}
	}
	for (bool overflowMul : {false, true})
	{
		AbilityModFixtureB12 f(world, NDb::ABILITYMODMODE_MANACOST, "1", "0");
		ValueWithModifiers* add = f.hero->GetVariableVWM(f.addName);
		ValueWithModifiers* mul = f.hero->GetVariableVWM(f.mulName); mul->SetBaseValue(1);
		const int foreign = overflowMul ? mul->AddModifier(2e38f, 0, 91) : add->AddModifier(1, 2e38f, 91);
		check(f.app->Initialize() && !f.app->Run(), "finite foreign coefficient fixture starts");
		const float oldAdd = add->GetValue(), oldMul = mul->GetValue();
		const int addId = add->GetModifiers().back().id, mulId = mul->GetModifiers().back().id;
		f.Edit().multValue.sString = overflowMul ? "2" : ".5";
		f.Edit().addValue.sString = overflowMul ? "5" : "2e38";
		check(!f.app->Tick(.1f) && f.Modified() == 40, "overflow in either staged variable rejects both cached coefficients");
		check(add->GetValue() == oldAdd && mul->GetValue() == oldMul
			&& add->Retrieve(addId)->addValue == 0 && mul->Retrieve(mulId)->mulValue == 1,
			"checked variable arithmetic cannot overflow a live value");
		f.app->Reset(); f.app->Finish();
		check((overflowMul ? mul : add)->Retrieve(foreign) && add->GetModifiers().size() == (overflowMul ? 0 : 1)
			&& mul->GetModifiers().size() == (overflowMul ? 1 : 0), "Reset and Stop preserve unrelated variable ownership");
	}
	for (bool overflowMul : {false, true})
	{
		AbilityModFixtureB12 f(world, NDb::ABILITYMODMODE_MANACOST, overflowMul ? "2" : "1", overflowMul ? "0" : "2e38");
		ValueWithModifiers* add = f.hero->GetVariableVWM(f.addName);
		ValueWithModifiers* mul = f.hero->GetVariableVWM(f.mulName); mul->SetBaseValue(7);
		const int foreign = overflowMul ? mul->AddModifier(2e38f, 0, 92) : add->AddModifier(1, 2e38f, 92);
		check(f.app->Initialize() && f.app->Run() && !f.app->IsEnabled() && f.app->enables == 0,
			"initial variable overflow completes before enabling the buff");
		check(mul->GetBaseValue() == 7 && (overflowMul ? mul : add)->Retrieve(foreign)
			&& add->GetModifiers().size() == (overflowMul ? 0 : 1) && mul->GetModifiers().size() == (overflowMul ? 1 : 0),
			"invalid initial arithmetic preserves foreign IDs and the authored variable base");
	}
	{
		AbilityModFixtureB12 f(world, NDb::ABILITYMODMODE_MANACOST, "2", "5");
		check(f.app->Initialize() && !f.app->Run(), "aggregate overflow fixture starts");
		float add = 3, mul = 2e38f;
		f.app->AddModifier(add, mul, NDb::ABILITYMODMODE_MANACOST, NDb::ABILITYTYPEID_TALENT, f.sourceDb);
		check(add == 3 && mul == 2e38f, "overflowing aggregate multiplication leaves both caller accumulators unchanged");
	}
	{
		AbilityModFixtureB12 f(world, NDb::ABILITYMODMODE_MANACOST, "1", "2e38");
		check(f.app->Initialize() && !f.app->Run(), "aggregate addition fixture starts");
		float add = 2e38f, mul = 3;
		f.app->AddModifier(add, mul, NDb::ABILITYMODMODE_MANACOST, NDb::ABILITYTYPEID_TALENT, f.sourceDb);
		check(add == 2e38f && mul == 3, "overflowing aggregate addition leaves both caller accumulators unchanged");
	}
	for (int policy = 0; policy != 4; ++policy)
	{
		AbilityModFixtureB12 f(world, NDb::ABILITYMODMODE_MANACOST, ".5", "5");
		if (policy == 0) f.Edit().enabled.sString = "false";
		if (policy == 1) f.Edit().startCondition.sString = "false";
		if (policy == 2) f.Edit().lifeTime.sString = ".2";
		if (policy == 3) f.Edit().behaviorFlags = NDb::BUFFBEHAVIOR_STOPONDISABLE;
		check(f.app->Initialize() && f.app->Run() == (policy == 1), "ability modifier retains base enabled and startCondition completion policy");
		if (policy <= 1)
			check(f.Count() == 0 && f.Modified() == 40, "disabled or denied start does not attach modifiers");
		if (policy == 0)
		{
			f.Edit().enabled.sString = "true";
			check(!f.app->Tick(.1f) && f.Modified() == 25 && f.Count() == 1, "authored enable resumes a disabled start");
		}
		if (policy == 2)
			check(!f.app->Tick(.1f) && f.app->Tick(.1f), "finite ability modifier expires on the stock timer boundary");
		if (policy == 3)
		{
			f.app->Block();
			check(f.app->Tick(.1f) && f.Count() == 0, "stock stop-on-disable policy remains terminal while blocked");
		}
	}
	for (int policy = 0; policy != 4; ++policy)
	{
		AbilityModFixtureB12 f(world, NDb::ABILITYMODMODE_MANACOST, ".5", "5");
		if (policy == 0) f.sender->ChangeFaction(NDb::FACTION_BURN);
		if (policy == 1) f.Edit().targetAbilities = NDb::ABILITYIDFLAGS_ABILITY1;
		if (policy == 2) { f.Edit().talents.push_back(f.sourceDb); f.Edit().useListAs = NDb::USEMODE_LISTASRULE; }
		if (policy == 3) { f.Edit().targetAbilities = NDb::ABILITYIDFLAGS_SPECIFIC; f.Edit().specificAbility = f.sourceDb; }
		check(f.app->Initialize() && !f.app->Run() && !f.Add() && !f.Mul(), "faction and ability selection retain stock talent-variable restrictions");
		check(f.Modified() == (policy == 1 ? 40 : 25), "ability selection remains independent from allied talent-variable exposure");
		check(!f.app->IsApplicable(NDb::ABILITYMODMODE_COOLDOWN, NDb::ABILITYTYPEID_TALENT, f.sourceDb), "mode mismatch is never applicable");
	}
	{
		B8BehaviourForeignWorld loaded(check);
		if (loaded.world)
		{
			AbilityModFixtureB12 f(loaded.world, NDb::ABILITYMODMODE_MANACOST, ".5", "5");
			CObj<PFAbilityInstance> cast = new PFAbilityInstance(f.source, Target(f.hero.GetPtr()), true);
			PFApplCreatePars pars(cast, Target(f.hero.GetPtr())); pars.pDBAppl = f.db;
			f.app = new AbilityModProbeB12(pars);
			check(f.app->Initialize() && ActivateApplicator(CObj<PFBaseApplicator>(f.app.GetPtr()), cast), "world-step fixture uses real ability activation");
			CObj<PFTalent> existing = f.Talent();
			loaded.world->Step(.1f, .1f);
			f.Edit().addValue.sString = "8";
			loaded.world->Step(.1f, .1f); existing->Update(0, true);
			check(f.Modified() == 28 && existing->GetManaCost() == 28 && cast->GetActiveApplicatorsCount() == 1,
				"world stepping updates live ability modifiers and an existing talent cache at a later timestamp");
			f.Edit().enabled.sString = "false";
			loaded.world->Step(.1f, .1f); existing->Update(0, true);
			check(f.Count() == 0 && existing->GetManaCost() == 40, "world stepping disables the ring and invalidates existing talent cost");
			f.app->Finish();
			check(cast->GetActiveApplicatorsCount() == 0, "world-step ability ownership balances on completion");
		}
	}
}

/** Separate dependency reproducer: same-tick cache invalidation is not owned by this patch. */
static void AbilityModCacheDependencyB12(Checks& check, PFWorld* world)
{
	AbilityModFixtureB12 f(world, NDb::ABILITYMODMODE_MANACOST, ".5", "5");
	f.app->Initialize(); f.app->Run();
	CObj<PFTalent> existing = f.Talent();
	check(existing->GetManaCost() == 25, "existing talent caches the first modified price");
	f.Edit().addValue.sString = "10"; f.app->Tick(0); existing->Update(0, true);
	check(f.Modified() == 30, "same-tick dynamic modifier publishes the next checked price");
	check(existing->GetManaCost() == 30, "DEPENDENCY existing talent sees same-tick modifier changes");
	CObj<PFAbilityInstance> cast = f.hero->UseTalent(existing, Target(f.hero.GetPtr()));
	check(cast && f.hero->GetMana() == 170, "DEPENDENCY paid cast uses the same-tick modified price");
	std::printf("AbilityMod cache dependency: live=%g cached=%g mana-after-cast=%g cast=%d\n",
		f.Modified(), existing->GetManaCost(), f.hero->GetMana(), cast ? 1 : 0);
	f.app->Block(); existing->Update(0, true);
	check(existing->GetManaCost() == 40, "DEPENDENCY existing talent sees same-tick modifier removal");
	std::printf("AbilityMod cache dependency: removed-live=%g removed-cache=%g\n", f.Modified(), existing->GetManaCost());
}
}
