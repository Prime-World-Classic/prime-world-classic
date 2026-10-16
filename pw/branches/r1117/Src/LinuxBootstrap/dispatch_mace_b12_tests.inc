#include "../PF_GameLogic/TileMap.h"

/** Authored mock data uses real ability ownership, collision, formulas and dispatch factories. */
namespace
{
struct MaceFixtureB12
{
	B8BehaviourForeignWorld fixture;
	CObj<PFBaseUnit> sender;
	CObj<B8BehaviourCreep> receiver;
	NDb::Ptr<NDb::Ability> db;
	NDb::DispatchRockmanMace* mace = nullptr;
	CObj<PFAbilityData> data;
	CObj<PFAbilityInstance> cast;
	CObj<PFDispatch> dispatch;
	explicit MaceFixtureB12(Checks& check, bool moving = false, bool persistent = false) : fixture(check)
	{
		if (!fixture.world) return;
		sender = moving ? CObj<PFBaseUnit>(B8BehaviourMoving(fixture.world, NDb::FACTION_FREEZE).GetPtr())
			: Unit(fixture.world, NDb::FACTION_FREEZE, 40);
		receiver = B8BehaviourMoving(fixture.world, NDb::FACTION_BURN);
		sender->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		receiver->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		sender->GetStat(NDb::STAT_STRENGTH)->SetCoreValue(40);
		receiver->GetStat(NDb::STAT_STRENGTH)->SetCoreValue(20);
		receiver->TeleportTo(CVec2(15, 5));
		NDb::Ability* raw = new NDb::Ability;
		mace = new NDb::DispatchRockmanMace;
		mace->speed = 5; mace->aiming = NDb::DISPATCHAIMING_PURSUE;
		mace->maxDistance = -1; raw->dispatch = mace; raw->manaCost.sString = "10";
		NDb::UnitConstantsContainer* constants = new NDb::UnitConstantsContainer;
		NDb::UnitConstant* constant = new NDb::UnitConstant;
		constant->name = "MaceBonus"; constant->var.sString = "sLife/1000+rLife/1000";
		constants->vars.push_back(constant); raw->constants = constants;
		NDb::DamageApplicator* damage = new NDb::DamageApplicator;
		damage->enabled.sString = "true"; damage->damage.sString = "sStrength/2+rStrength+cMaceBonus";
		damage->damageType = NDb::APPLICATORDAMAGETYPE_PURE; raw->applicators.push_back(damage);
		if (persistent)
		{
			NDb::StatModApplicator* modifier = new NDb::StatModApplicator;
			modifier->enabled.sString = "true"; modifier->startCondition.sString = "true";
			modifier->lifeTime.sString = "10"; modifier->dontUpdate = true;
			modifier->modifier.stat = NDb::STAT_STRENGTH;
			modifier->modifier.multValue.sString = "1"; modifier->modifier.addValue.sString = "cMaceBonus";
			mace->persistentApplicators.push_back(modifier);
		}
		db = raw;
		data = new PFAbilityData(sender.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL);
	}
	void Launch(bool owned = false, bool start = true)
	{
		if (owned)
		{
			cast = sender->CreateAbilityInstance(data, Target(receiver.GetPtr()));
			dispatch = cast ? B11FindDispatch(fixture.world, cast.GetPtr()) : CObj<PFDispatch>();
		}
		else
		{
			cast = new PFAbilityInstance(data, Target(receiver.GetPtr()), false);
			dispatch = CreateDispatch(cast, nullptr, Target(sender.GetPtr()), Target(receiver.GetPtr()), db.GetPtr(), 0, start, 0);
		}
	}
	~MaceFixtureB12() { if (dispatch) dispatch->Cancel(); if (cast) cast->Cancel(); }
};

/** Keep original ready-but-unapplied arrival and explicit cancellation, including persistent ownership. */
static void DispatchMaceB12(Checks& check)
{
	for (bool owned : {false, true})
	{
		// Ordinary casts are stepped by real moving units; detached casts use the world dispatch phase.
		MaceFixtureB12 f(check, owned, true);
		if (!f.fixture.world) continue;
		f.Launch(owned, owned);
		PFDispatchRockmanMace* mace = dynamic_cast<PFDispatchRockmanMace*>(f.dispatch.GetPtr());
		check(mace != nullptr, "mace factory preserves the actual Rockman subtype");
		if (!mace) continue;
		check(!mace->HasArrived() && !mace->HasApplied() && !mace->GetApplicators().empty(),
			"mace retains authored payload until an explicit apply owner acts");
		if (!owned)
		{
			f.fixture.world->Step(1, 1);
			check(!mace->Started() && mace->GetCurPos() == CVec2(5, 5) && f.cast->GetActiveApplicatorsCount() == 0,
				"unstarted detached mace does not move or start persistent effects");
			mace->Start();
		}
		check(f.cast->GetActiveApplicatorsCount() == 1 && f.receiver->GetStatValue(NDb::STAT_STRENGTH) == 22,
			"mace persistent buff uses real checked sender receiver and ability constant context");
		for (int tick = 1; tick <= 4; ++tick)
		{
			f.fixture.world->Step(.5f, .5f);
			check(mace->GetCurPos() == CVec2(5 + tick * 2.5f, 5) && f.receiver->GetHealth() == 1000,
				"real world advances owned or detached mace once without premature damage");
			if (tick == 1)
			{
				mace->Reset();
				check(mace->GetCurPos() == CVec2(7.5f, 5) && mace->Started() && !mace->HasArrived(),
					"mace reset preserves travel progress without renderer work");
			}
		}
		check(mace->HasArrived() && !mace->HasApplied() && !mace->IsObjectDead() && f.cast->GetActiveApplicatorsCount() == 1,
			"original mace arrival remains ready and alive with persistent ownership");
		f.fixture.world->Step(20, 20);
		check(!mace->HasApplied() && f.receiver->GetHealth() == 1000 && f.receiver->GetStatValue(NDb::STAT_STRENGTH) == 22,
			"ready mace neither applies its payload nor advances proceeded-only persistent timers");
		mace->Cancel(); mace->Cancel();
		f.fixture.world->Step(.5f, .5f);
		check(mace->IsObjectDead() && f.cast->GetActiveApplicatorsCount() == 0 && f.receiver->GetStatValue(NDb::STAT_STRENGTH) == 20,
			"mace cancellation balances persistent ability ownership and cleans the target once");
		check(f.receiver->GetHealth() == 1000 && (!owned || f.cast->IsFinished()),
			"mace cancellation cannot replay payload and releases an ordinary cast holder");
	}
	for (int mode = 0; mode < 8; ++mode)
	{
		MaceFixtureB12 f(check, mode != 7);
		if (!f.fixture.world) continue;
		TileMap* map = f.fixture.world->GetTileMap();
		const int layer = mode == 2 ? MAP_MODE_STATIC : mode == 3 ? MAP_MODE_DYNAMIC : MAP_MODE_BUILDING;
		vector<SVector> wall;
		if (mode >= 1 && mode <= 4)
		{
			for (int y = 0; y < map->GetSizeY(); ++y) wall.push_back(SVector(map->GetTile(CVec2(9, 5)).x, y));
			map->MarkObject(wall, true, layer);
		}
		if (mode == 4) f.mace->minDistance = 6;
		if (mode == 5 || mode == 7) f.mace->maxDistance = 3;
		if (mode == 5) f.mace->minDistance = 6;
		if (mode == 6) f.receiver->TeleportTo(CVec2(5, 5));
		PFBaseMovingUnit* moving = dynamic_cast<PFBaseMovingUnit*>(f.sender.GetPtr());
		const float distance = moving ? moving->CheckStraightMove(f.receiver->GetPos(), MAP_MODE_BUILDING) : 10;
		const float limited = min(distance, f.mace->maxDistance > 0 ? f.mace->maxDistance : 1e6f);
		const bool clipped = moving && limited < (mode == 6 ? 0 : 10);
		const float destinationX = mode == 6 ? 5 : clipped ? 5 + max(limited, f.mace->minDistance) : 15;
		f.Launch();
		PFDispatchRockmanMace* mace = dynamic_cast<PFDispatchRockmanMace*>(f.dispatch.GetPtr());
		check(mace != nullptr, "mace collision fixture constructs the real dispatch");
		if (!mace) { if (!wall.empty()) map->MarkObject(wall, false, layer); continue; }
		check(mace->GetTarget().IsPosition() == clipped && fabs(mace->GetTarget().AcquirePosition().x - destinationX) < .001f,
			"mace clips only moving senders using the original min trace max-distance then min-distance order");
		check((mode != 1 && mode != 4) || distance < 10, "building-layer obstruction participates in the real straight-move query");
		check((mode != 2 && mode != 3) || distance == 10, "mace building-only collision ignores static and dynamic map layers");
		for (int tick = 0; tick != 5; ++tick) f.fixture.world->Step(.5f, .5f);
		// Initially coincident units separate through real collision resolution before the pursuing dispatch arrives.
		const float finalX = mode == 6 ? f.receiver->GetPos().x : mode == 5 || mode == 7 ? 8 : destinationX;
		if (!mace->HasArrived() || mace->HasApplied() || mace->IsObjectDead() || fabs(mace->GetCurPos().x - finalX) >= .001f)
			std::printf("Mace landing context: mode=%d arrived=%d applied=%d dead=%d actual=%g,%g expectedX=%g receiver=%g,%g\n",
				mode, mace->HasArrived(), mace->HasApplied(), mace->IsObjectDead(), mace->GetCurPos().x, mace->GetCurPos().y,
				finalX, f.receiver->GetPos().x, f.receiver->GetPos().y);
		check(mace->HasArrived() && !mace->HasApplied() && !mace->IsObjectDead() && fabs(mace->GetCurPos().x - finalX) < .001f,
			"collision zero-distance and distance-limit arrivals preserve original deferred mace policy");
		if (mode == 6) check(mace->GetCurPos() == f.receiver->GetPos(), "initially coincident mace follows its collision-corrected live target in both coordinates");
		check(f.receiver->GetHealth() == 1000 && mace->GetCurrentPosition().IsFinite(),
			"mace clipping and arrival cannot create implicit damage or nonfinite coordinates");
		if (!wall.empty()) map->MarkObject(wall, false, layer);
	}
	{
		MaceFixtureB12 f(check);
		if (f.fixture.world)
		{
			f.mace->suspendTime = .5f; f.Launch();
			PFDispatchRockmanMace* mace = dynamic_cast<PFDispatchRockmanMace*>(f.dispatch.GetPtr());
			check(mace != nullptr, "mace suspension fixture constructs");
			if (mace)
			{
				for (float dt : {0.f, -1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::max()})
				{
					f.dispatch->Step(dt);
					check(mace->GetCurPos() == CVec2(5, 5) && mace->IsSuspended() && !mace->HasArrived(),
						"invalid or overflowing mace tick cannot change movement suspension or arrival");
				}
				f.fixture.world->Step(.5f, .5f);
				check(mace->GetCurPos() == CVec2(5, 5) && !mace->IsSuspended(), "valid mace tick consumes suspension without travel");
				f.fixture.world->Step(.5f, .5f);
				check(mace->GetCurPos() == CVec2(7.5f, 5), "mace resumes original speed after rejected ticks and suspension");
			}
		}
	}
	for (int field = 0; field != 7; ++field) for (float bad : {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		MaceFixtureB12 f(check, true);
		if (!f.fixture.world) continue;
		CVec3 source(5, 5, 0), target(15, 5, 0);
		if (field == 0) f.mace->speed = bad;
		if (field == 1) f.mace->maxDistance = bad;
		if (field == 2) f.mace->minDistance = bad;
		if (field == 3) f.mace->suspendTime = bad;
		if (field == 4) f.mace->maxDeviation = bad;
		if (field == 5) source.x = bad;
		if (field == 6) target.y = bad;
		f.cast = new PFAbilityInstance(f.data, Target(f.receiver.GetPtr()), false);
		f.dispatch = CreateDispatch(f.cast, nullptr, Target(source), Target(target), f.db.GetPtr(), 0, true, 0);
		check(!f.dispatch && f.cast->GetActiveApplicatorsCount() == 0 && f.receiver->GetHealth() == 1000,
			"nonfinite mace fields and endpoints reject before registration or effect mutation");
	}
	for (bool overflow : {false, true})
	{
		MaceFixtureB12 f(check, true);
		if (!f.fixture.world) continue;
		f.mace->maxDistance = 1;
		if (overflow) f.mace->minDistance = 3e38f;
		f.cast = new PFAbilityInstance(f.data, Target(f.receiver.GetPtr()), false);
		const Target target = overflow ? Target(f.receiver.GetPtr()) : Target(CVec3(1e18f, 5, 0));
		f.dispatch = CreateDispatch(f.cast, nullptr, Target(f.sender.GetPtr()), target, f.db.GetPtr(), 0, true, 0);
		check(!f.dispatch, "finite but unrepresentable tile conversion or clipping arithmetic fails closed");
	}
	{
		MaceFixtureB12 f(check, true, true);
		if (f.fixture.world)
		{
			NDb::Ability* outer = new NDb::Ability;
			outer->constants = f.db->constants;
			NDb::DispatchImmediate* immediate = new NDb::DispatchImmediate;
			immediate->instantApply = true; outer->dispatch = immediate;
			NDb::WaitForSpellApplicator* wait = new NDb::WaitForSpellApplicator;
			wait->enabled.sString = "true"; wait->startCondition.sString = "true"; wait->lifeTime.sString = "-1";
			wait->applyTarget = NDb::APPLICATORAPPLYTARGET_ABILITYOWNER;
			wait->spellTarget = NDb::APPLICATORAPPLYTARGET_APPLICATORTARGET;
			wait->behaviour = NDb::WAITFORSPELLBEHAVIOUR_STOPWHENSPELLFINISHED;
			wait->spell = f.db; outer->applicators.push_back(wait);
			f.data = new PFAbilityData(f.sender.GetPtr(), outer, NDb::ABILITYTYPEID_SPECIAL);
			f.Launch(true);
			PFDispatchRockmanMace* mace = dynamic_cast<PFDispatchRockmanMace*>(f.dispatch.GetPtr());
			check(mace && f.cast->GetActiveApplicatorsCount() == 2,
				"real wait-for-spell ability owns its parent buff and nested mace persistent effect");
			if (mace)
			{
				for (int tick = 0; tick != 4; ++tick) f.fixture.world->Step(.5f, .5f);
				check(mace->HasArrived() && !mace->HasApplied() && !mace->IsObjectDead(),
					"nested mace reaches ready state through the detached world phase");
				f.fixture.world->Step(.5f, .5f); f.fixture.world->Step(.5f, .5f);
				check(mace->IsObjectDead() && f.cast->IsFinished() && f.cast->GetActiveApplicatorsCount() == 0,
					"stock wait-for-spell arrival policy cancels mace and finishes the real outer ability");
				check(f.receiver->GetHealth() == 1000 && f.receiver->GetStatValue(NDb::STAT_STRENGTH) == 20,
					"parent-driven mace completion cleans persistent effects without applying a hidden payload");
			}
		}
	}
	{
		MaceFixtureB12 f(check);
		if (f.fixture.world)
		{
			f.mace->speed = 0; f.Launch();
			PFDispatchRockmanMace* mace = dynamic_cast<PFDispatchRockmanMace*>(f.dispatch.GetPtr());
			check(mace != nullptr, "authored zero mace speed remains a supported stationary dispatch");
			if (mace)
			{
				f.fixture.world->Step(1, 1);
				check(mace->GetCurPos() == CVec2(5, 5) && !mace->HasArrived(), "zero-speed mace does not synthesize travel or arrival");
				mace->ForceArrive(Target(f.receiver.GetPtr()), false);
				check(mace->HasArrived() && !mace->HasApplied() && f.receiver->GetHealth() == 1000,
					"forced mace arrival retains its deferred-apply override");
				mace->Reset();
				check(mace->HasArrived() && !mace->IsObjectDead() && mace->GetCurPos() == CVec2(15, 5),
					"ready mace reset cannot rewind or apply retained payload");
			}
		}
	}
}
}
