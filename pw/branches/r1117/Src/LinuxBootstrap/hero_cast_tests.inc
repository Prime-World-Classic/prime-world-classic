#include "../PF_GameLogic/PFHeroStates.h"
#include "../PF_GameLogic/PFTalent.h"

/** A real male hero with literal movement/vision stats and no alternate gameplay implementation. */
struct B9CastHero : PFBaseMaleHero
{
	B9CastHero(PFWorld* world, const SpawnInfo& spawn, NDb::EFaction faction)
		: PFBaseMaleHero(world, spawn, faction, faction) {}
	void EnemyVisible(bool value) { visibleForEnemy = value; }
	void Orders(float dt) { FsmStep(dt); }
	bool Simulate(float dt) { return PFBaseMaleHero::Step(dt); }
};

static CObj<B9CastHero> B9MaleHero(PFWorld* world, NDb::EFaction faction)
{
	NDb::Ptr<NDb::Creature> source = B8BehaviourCreatureDb(world, faction);
	NDb::StatsContainer* stats = const_cast<NDb::StatsContainer*>(source->stats.GetPtr());
	for (const auto entry : {nstl::pair<NDb::EStat, const char*>(NDb::STAT_VISIBILITYRANGE, "12"),
		nstl::pair<NDb::EStat, const char*>(NDb::STAT_RANGE, "1"),
		nstl::pair<NDb::EStat, const char*>(NDb::STAT_MOVESPEED, "4")})
	{
		NDb::UnitStat stat; stat.statId = entry.first; stat.value.sString = entry.second;
		stat.increment.sString = "0"; stats->stats.push_back(stat);
	}
	NDb::Hero* raw = new NDb::Hero; raw->stats = source->stats; raw->targetingParams = source->targetingParams;
	NDb::Ptr<NDb::Hero> db = raw;
	PFBaseHero::SpawnInfo spawn; spawn.pHero = db;
	spawn.placement = Placement(CVec3(5, 5, 0), QNULL, CVec3(1, 1, 1));
	return new B9CastHero(world, spawn, faction);
}

/** Expose stepping/instance observations, without replacing talent-use implementation. */
struct B9TalentState : PFHeroUseTalentState
{
	B9TalentState(PFBaseMaleHero* hero, PFTalent* talent, const Target& target)
		: PFHeroUseTalentState(hero, talent, target) {}
	void Enter() { OnEnter(); }
	void Leave() { OnLeave(); }
	bool Step(float dt) { return OnStep(dt); }
	PFAbilityInstance* Instance() const { return pAbilityInstance; }
};

/** Only mock work duration; activation, payment, ownership and stepping remain engine code. */
struct B9TimedTalent : PFTalent
{
	B9TimedTalent(PFBaseMaleHero* hero, const NDb::Talent* db) : PFTalent(hero, db, 1, 0, 0, false) {}
	float GetWorkTime() const override { return 3; }
};

/** Talent orders approach before real payment, then observe (never advance) owned cast work. */
static void HeroTalentLifecycleB9(Checks& check, PFWorld* world)
{
	{
		CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
		NDb::Talent* raw = new NDb::Talent; raw->manaCost.sString = "0";
		raw->isUnitFreeAfterCast = true;
		NDb::Ptr<NDb::Talent> db = raw;
		CObj<PFTalent> talent = new PFTalent(hero.GetPtr(), db, 1, 0, 0, false);
		check(!hero->UseTalent(talent, Target(hero.GetPtr())), "unactivated talent cannot bypass purchase admission");
		for (int mode = 0; mode < 4; ++mode)
		{
			raw->flags = mode == 0 ? static_cast<NDb::EAbilityFlags>(0) : NDb::ABILITYFLAGS_MOMENTARY;
			if (mode == 2) raw->flags = static_cast<NDb::EAbilityFlags>(raw->flags | NDb::ABILITYFLAGS_WAITFORCHANNELING);
			if (mode == 3) raw->flags = static_cast<NDb::EAbilityFlags>(raw->flags | NDb::ABILITYFLAGS_FORCEDONOTSTOPUNIT);
			check(talent->IsAbilitySupposedToStopUnit() == (mode == 0 || mode == 2),
				"talent stop metadata preserves momentary/channel/forced-motion policy");
		}
	}
	for (int policy = 0; policy < 4; ++policy)
	{
		CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); hero->SetMoveFlags(MOVE_FLAG_NO_COLLIDE);
		hero->SetUnitSpeed(4);
		NDb::Talent* raw = new NDb::Talent;
		raw->useRange.sString = policy == 2 ? "unknownRange" : "4";
		raw->manaCost.sString = "70"; raw->naftaCost = 0; raw->cooldownTime.sString = "0";
		raw->flags = static_cast<NDb::EAbilityFlags>(0); raw->isUnitFreeAfterCast = true;
		raw->requireLineOfSight = false;
		NDb::Ptr<NDb::Talent> db = raw;
		CObj<PFTalent> talent = new PFTalent(hero.GetPtr(), db, 1, 0, 0, false);
		check(talent->Activate(), "mock authored talent activates through real purchase policy");
		Target point(CVec3(15, 5, 0));
		CObj<B9TalentState> state = new B9TalentState(hero.GetPtr(), talent, point);
		if (policy == 1) hero->AddFlag(NDb::UNITFLAG_FORBIDMOVE);
		if (policy == 3) talent->AddForbid();
		state->Enter(); const bool finished = state->Step(0);
		if (policy == 0)
		{
			check(!finished && hero->IsMoving() && hero->GetMana() == 200,
				"distant talent starts movement without early resource payment");
			for (int i = 0; i < 100 && !state->Instance(); ++i) { hero->Simulate(0.1f); state->Step(0.1f); }
			check(state->Instance() && hero->GetMana() == 130 && hero->IsPositionInRange(CVec2(15, 5), 4),
				"talent casts once after actual authored-range arrival");
		}
		else
		{
			for (int i = 0; i < 100 && !state->Step(0.1f); ++i) hero->Simulate(0.1f);
			check(!state->Instance() && hero->GetMana() == 200, "forbidden or malformed talent cannot spend resources");
		}
		state->Leave(); hero->Cleanup(); hero->Stop(false);
	}
	CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
	NDb::Talent* raw = new NDb::Talent; raw->useRange.sString = "0";
	raw->manaCost.sString = "0"; raw->naftaCost = 0; raw->cooldownTime.sString = "0";
	raw->flags = NDb::ABILITYFLAGS_WAITFORCHANNELING; raw->isUnitFreeAfterCast = true;
	raw->requireLineOfSight = false;
	NDb::Ptr<NDb::Talent> db = raw;
	CObj<PFTalent> talent = new B9TimedTalent(hero.GetPtr(), db); talent->Activate();
	CObj<B9TalentState> state = new B9TalentState(hero.GetPtr(), talent, Target(hero.GetPtr()));
	state->Enter(); check(!state->Step(0) && state->Instance(), "timed talent begins a real unfinished cast");
	check(!state->Step(2) && state->Instance() && !state->Instance()->IsCastFinished(),
		"talent state does not double-step simulation-owned cast duration");
	hero->Simulate(3); hero->SetChannellingProgress(0);
	check(!state->Step(0), "channel-wait state retains its positive-step completion boundary");
	check(state->Step(0.1f), "simulation completion and positive step release channel-wait state");
	state->Leave(); hero->Cleanup();
}

/** Mock descriptor/range only; the production base hero-use state owns approach and cleanup. */
struct B9HeroUseState : PFBaseUseHeroState
{
	NDb::Ptr<NDb::Ability> db;
	float useRange = 4;
	B9HeroUseState(PFBaseMaleHero* hero, const Target& target, const NDb::Ability* db)
		: PFBaseUseHeroState(hero, target), db(db) {}
	const NDb::Ability* GetDBDesc() const override { return db; }
	float GetUseRange() const override { return useRange; }
	bool Close() const { return NeedToGetClose(); }
	bool Sight() const { return IsRequireLineOfSight(); }
	bool IgnoreVisibility() const { return CanIgnoreVisibility(); }
	void Instance(PFAbilityInstance* value) { pAbilityInstance = value; }
};

/** Authored hero-use policy drives real point/chase children and preserves momentary continuation. */
static void HeroUseApproachB9(Checks& check, PFWorld* world)
{
	CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
	hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); hero->SetUnitSpeed(4);
	hero->SetMoveFlags(MOVE_FLAG_NO_COLLIDE); hero->EnemyVisible(true);
	NDb::Ability* raw = new NDb::Ability; raw->requireLineOfSight = false;
	raw->flags = NDb::ABILITYFLAGS_MOMENTARY;
	NDb::Ptr<NDb::Ability> db = raw;
	Target point(CVec3(15, 5, 0));
	CObj<B9HeroUseState> state = new B9HeroUseState(hero.GetPtr(), point, db);
	check(state->Close(), "hero use needs approach for a distant point");
	state->OnEnter(); state->FSMStep(0);
	check(state->GetStackSize() == 1 && hero->IsMoving(), "hero use enters the existing point movement state");
	state->OnLeave();
	check(state->GetStackSize() == 0 && !hero->IsMoving(), "hero use leave cleans approach while state is retained");
	hero->AddFlag(NDb::UNITFLAG_FORBIDMOVE);
	state = new B9HeroUseState(hero.GetPtr(), point, db); state->OnEnter();
	check(state->GetStackSize() == 0, "forbidden approach creates no movement child");
	hero->RemoveFlag(NDb::UNITFLAG_FORBIDMOVE);
	state->useRange = std::numeric_limits<float>::quiet_NaN(); state->OnEnter();
	check(state->GetStackSize() == 0, "malformed hero use range cannot create an approach");
	state = new B9HeroUseState(hero.GetPtr(), Target(CVec3(5, 5, 0)), db);
	state->useRange = 100; raw->requireLineOfSight = true; hero->CloseWarFog(true);
	check(state->Sight() && state->Close(), "personal sight can require approach despite geometric range");
	raw->requireLineOfSight = false;
	raw->targetType = NDb::SPELLTARGET_VISIBLETARGETSONLY;
	check(!state->IgnoreVisibility(), "visible-only authored target forbids blind use");
	raw->targetType = NDb::SPELLTARGET_ALL;
	check(state->IgnoreVisibility(), "ordinary target policy permits blind-use evaluation");
	state->db = nullptr;
	check(!state->IgnoreVisibility(), "missing ability does not authorize blind use");
	CObj<PFBaseUnit> enemy = Unit(world, NDb::FACTION_BURN, 20);
	B8BehaviourReveal reveal(world, NDb::FACTION_FREEZE);
	raw->flags = static_cast<NDb::EAbilityFlags>(NDb::ABILITYFLAGS_MOMENTARY | NDb::ABILITYFLAGS_MOVING);
	state = new B9HeroUseState(hero.GetPtr(), Target(enemy.GetPtr()), db); state->OnEnter();
	CObj<PFAbilityData> data = new PFAbilityData(hero.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL);
	CObj<PFAbilityInstance> instance = new PFAbilityInstance(data, Target(enemy.GetPtr()), false);
	state->Instance(instance); state->OnLeave(); state->OnAfterLeave();
	check(hero->HasQueuedStates(), "successful moving momentary use restores enemy targeting through hero behavior");
	hero->Cleanup();
}
