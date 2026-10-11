#include "../PF_GameLogic/PFConsumable.h"
#include "../PF_GameLogic/DBConsumable.h"

/** Expose observations only; production consumable state owns approach and activation. */
struct B9ConsumableState : PFHeroUseConsumableState
{
	B9ConsumableState(PFBaseMaleHero* hero, int slot, const Target& target)
		: PFHeroUseConsumableState(hero, slot, target) {}
	bool Step(float dt) { return OnStep(dt); }
	PFAbilityInstance* Instance() const { return pAbilityInstance; }
};

/** Supply only nonzero work time to detect an accidental second simulation tick. */
struct B9TimedConsumable : PFConsumableAbilityData
{
	B9TimedConsumable(PFBaseUnit* hero, const NDb::Consumable* db)
		: PFConsumableAbilityData(hero, db, NDb::ABILITYTYPEID_CONSUMABLE) {}
	float GetWorkTime() const override { return 3; }
};

/** Real inventory, approach, resource payment and unit-owned instance stepping. */
static void HeroConsumableLifecycleB9(Checks& check, PFWorld* world)
{
	for (int policy = 0; policy < 5; ++policy)
	{
		CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); hero->SetMoveFlags(MOVE_FLAG_NO_COLLIDE);
		hero->SetUnitSpeed(4);
		NDb::Consumable* raw = new NDb::Consumable;
		raw->useRange.sString = policy == 2 ? "unknownRange" : "4";
		raw->manaCost.sString = "70"; raw->cooldownTime.sString = "0";
		raw->flags = static_cast<NDb::EAbilityFlags>(0); raw->requireLineOfSight = false;
		raw->maxQuantity = 5; raw->isUnitFreeAfterCast = true;
		NDb::Ptr<NDb::Consumable> db = raw;
		check(hero->TakeConsumable(db, 2), "real hero inventory admits mock authored consumable");
		CObj<PFConsumable> item = const_cast<PFConsumable*>(hero->GetConsumable(0));
		Target point(CVec3(15, 5, 0));
		CObj<B9ConsumableState> state = new B9ConsumableState(hero.GetPtr(), 0, point);
		if (policy == 1) hero->AddFlag(NDb::UNITFLAG_FORBIDMOVE);
		if (policy == 3) hero->AddFlag(NDb::UNITFLAG_FORBIDUSECONSUMABLES);
		state->OnEnter(); const bool finished = state->Step(0);
		if (policy == 0)
		{
			check(!finished && hero->IsMoving() && hero->GetMana() == 200 && item->GetQuantity() == 2,
				"consumable approach spends neither mana nor inventory early");
			for (int i = 0; i < 100 && !state->Instance(); ++i) { hero->Simulate(0.1f); state->Step(0.1f); }
			check(state->Instance() && hero->GetMana() == 130 && item->GetQuantity() == 1,
				"arrival invokes actual consumable activation once");
		}
		else
		{
			if (policy == 4) hero->RemoveConsumable(0);
			for (int i = 0; i < 100 && !state->Step(0.1f); ++i) hero->Simulate(0.1f);
			check(!state->Instance() && hero->GetMana() == 200 && item->GetQuantity() == 2,
				"forbidden malformed or removed consumable cannot activate");
		}
		state->OnLeave(); hero->Cleanup(); hero->Stop(false);
	}
	CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
	hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
	NDb::Consumable* raw = new NDb::Consumable; raw->useRange.sString = "0";
	raw->manaCost.sString = "0"; raw->flags = NDb::ABILITYFLAGS_WAITFORCHANNELING;
	raw->requireLineOfSight = false; raw->maxQuantity = 5; raw->isUnitFreeAfterCast = true;
	NDb::Ptr<NDb::Consumable> db = raw; hero->TakeConsumable(db, 2);
	PFConsumable* item = const_cast<PFConsumable*>(hero->GetConsumable(0));
	item->GetAbility() = new B9TimedConsumable(hero.GetPtr(), db);
	CObj<B9ConsumableState> state = new B9ConsumableState(hero.GetPtr(), 0, Target(hero.GetPtr()));
	state->OnEnter(); check(!state->Step(0) && state->Instance(), "consumable starts a real timed instance");
	check(!state->Step(4) && state->Instance() && !state->Instance()->IsCastFinished(),
		"consumable state cannot advance simulation-owned work");
	hero->Simulate(3); hero->SetChannellingProgress(0);
	check(!state->Step(0) && state->Step(0.1f), "consumable channel wait requires simulation completion and positive step");
	state->OnLeave(); hero->Cleanup();
}
