#include "../PF_GameLogic/PFHeroStates.h"

/** A real male hero with literal movement/vision stats and no alternate gameplay implementation. */
struct B9CastHero : PFBaseMaleHero
{
	B9CastHero(PFWorld* world, const SpawnInfo& spawn, NDb::EFaction faction)
		: PFBaseMaleHero(world, spawn, faction, faction) {}
	void EnemyVisible(bool value) { visibleForEnemy = value; }
	void Orders(float dt) { FsmStep(dt); }
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
