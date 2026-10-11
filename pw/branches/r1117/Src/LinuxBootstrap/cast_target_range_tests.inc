/** Observe production range admission without replacing its formula or movement logic. */
struct B10ConsumableRangeState : B9ConsumableState
{
	using B9ConsumableState::B9ConsumableState;
	void Enter() { OnEnter(); }
	void Leave() { OnLeave(); }
	bool Close() const { return NeedToGetClose(); }
};

/** Expose the same distance predicate for ordinary and alternative talent activities. */
struct B10TalentRangeState : B9TalentState
{
	using B9TalentState::B9TalentState;
	bool Close() const { return NeedToGetClose(); }
};

/** Target-strength formulas must not silently use the caster's strength instead. */
static void CastTargetRangesB10(Checks& check, PFWorld* world)
{
	B8BehaviourReveal reveal(world, NDb::FACTION_FREEZE);
	for (bool consumable : {false, true}) for (bool alternative : {false, true})
	{
		CObj<B9CastHero> hero = B9MaleHero(world, NDb::FACTION_FREEZE);
		hero->GetStat(NDb::STAT_STRENGTH)->SetCoreValue(0);
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		hero->SetMoveFlags(MOVE_FLAG_NO_COLLIDE);
		check(hero->TeleportTo(CVec2(15, 5)), "range fixture relocates its actual caster");
		CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_FREEZE, 20);
		AbilityTarget target(Target(receiver.GetPtr()));
		NDb::AlternativeTarget* alt = new NDb::AlternativeTarget;
		NDb::Ptr<NDb::AlternativeTarget> altDb = alt;
		NDb::AlternativeActivity* activity = new NDb::AlternativeActivity;
		activity->useRange.sString = "2+tStrength";
		alt->alternativeActivity = activity;
		if (alternative) target.SetDBAlternativeTarget(altDb);
		if (consumable)
		{
			NDb::Consumable* raw = new NDb::Consumable;
			raw->useRange.sString = "1+tStrength"; raw->manaCost.sString = "15";
			raw->cooldownTime.sString = "0"; raw->maxQuantity = 5;
			raw->requireLineOfSight = false;
			NDb::Ptr<NDb::Consumable> db = raw; hero->TakeConsumable(db, 2);
			CObj<B10ConsumableRangeState> state = new B10ConsumableRangeState(hero.GetPtr(), 0, target);
			check(!state->Close(), "consumable authored range uses actual target stats");
			state->Enter(); state->Step(0);
			check(state->Instance() && hero->GetMana() == 185 && !hero->IsMoving(),
				"in-range target-dependent consumable casts without an invented approach");
			state->Leave();
		}
		else
		{
			NDb::Talent* raw = new NDb::Talent;
			raw->useRange.sString = "1+tStrength"; raw->manaCost.sString = "15";
			raw->cooldownTime.sString = "0"; raw->naftaCost = 0;
			raw->requireLineOfSight = false;
			NDb::Ptr<NDb::Talent> db = raw;
			CObj<PFTalent> talent = new PFTalent(hero.GetPtr(), db, 1, 0, 0, false);
			check(talent->Activate(), "range fixture purchases its real talent");
			CObj<B10TalentRangeState> state = new B10TalentRangeState(hero.GetPtr(), talent, target);
			check(!state->Close(), "talent authored range uses actual target stats");
			state->Enter(); state->Step(0);
			check(state->Instance() && hero->GetMana() == 185 && !hero->IsMoving(),
				"in-range alternative talent casts without an invented approach");
			state->Leave();
		}
		hero->DoStop();
	}
}
