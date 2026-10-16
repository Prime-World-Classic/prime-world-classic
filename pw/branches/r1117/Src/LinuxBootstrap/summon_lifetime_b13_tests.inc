#include "../PF_GameLogic/PFSummoned.h"

/** Observe actual summoned creatures and owner groups without replacing their lifecycle. */
struct SummonLifetimeFixtureB13
{
	using Creature = DeadCreatureB13<PFBaseSummonedUnit>;
	struct Entry { CObj<Creature> unit; CObj<PFSummonedUnitBehaviour> behaviour; };
	PFWorld* world;
	CObj<B9CastHero> master;
	CObj<PFPlayer> player;
	NDb::Ptr<NDb::Creature> creatureDb;
	std::vector<Entry> units;
	std::vector<std::pair<PFBaseUnit*, CObj<EventDispatchB7Listener>>> listeners;
	std::vector<std::pair<PFBaseUnit*, NDb::EBaseUnitEvent>> events;
	std::function<void(const PFBaseUnitEventUnitDeath&)> callback;
	explicit SummonLifetimeFixtureB13(PFWorld* world) : world(world), master(B9MaleHero(world, NDb::FACTION_FREEZE)), creatureDb(DeadCreatureDbB13(world))
	{
		player = new PFPlayer(world, 80, NCore::ETeam::Team1, NCore::ETeam::Team1, 1980, NCore::ESex::Male, false, 0, false, false);
		master->AttachToPlayer(player.GetPtr()); master->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); Listen(master);
	}
	void Listen(PFBaseUnit* unit)
	{
		CObj<EventDispatchB7Listener> listener = new EventDispatchB7Listener;
		listener->action = [this, unit](EventDispatchB7Listener&, const PFBaseUnitEvent* event) {
			if (const auto* death = dynamic_cast<const PFBaseUnitEventUnitDeath*>(event))
			{
				events.emplace_back(unit, death->GetType());
				if (callback) callback(*death);
			}
			return 0u;
		};
		unit->AddEventListener(listener.GetPtr()); listeners.emplace_back(unit, listener);
	}
	Entry Add(int type, float lifetime, int flags = PFSummonBehaviour::BEHAVIOURFLAGS_CONTROLLEDLIFETIME)
	{
		Entry entry;
		entry.unit = new Creature(CPtr<PFWorld>(world), creatureDb, type < 0 ? NDb::UNITTYPE_DUMMYUNIT
			: type == NDb::SUMMONTYPE_PET ? NDb::UNITTYPE_PET : NDb::UNITTYPE_SUMMON,
			CPtr<PFBaseUnit>(master.GetPtr()), Placement(CVec3(12 + units.size(), 12, 0), QNULL, CVec3(1, 1, 1)), true, false, true);
		entry.unit->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); entry.unit->AddFlag(NDb::UNITFLAG_FORBIDMOVE);
		entry.unit->EnableAbilityAI(false);
		if (type < 0) entry.behaviour = PFSummonedUnitBehaviour::Create(entry.unit, CPtr<PFBaseUnit>(master.GetPtr()), nullptr, lifetime, flags);
		else
		{
			auto* raw = new NDb::SummonBehaviourCommon; raw->summonType = static_cast<NDb::SummonType>(type); raw->lashRange.sString = "10";
			NDb::Ptr<NDb::SummonBehaviourCommon> db = raw;
			entry.behaviour = PFSummonedUnitAIBehaviour::Create(entry.unit, CPtr<PFBaseUnit>(master.GetPtr()), db, raw->summonType, lifetime, flags);
		}
		entry.behaviour->SetGroupName("Lifetime"); entry.unit->AddBehaviourOnTop(entry.behaviour);
		Listen(entry.unit); units.push_back(entry); return entry;
	}
	int Count(PFBaseUnit* unit, NDb::EBaseUnitEvent type) const
	{
		int count = 0; for (const auto& event : events) if (event.first == unit && event.second == type) ++count; return count;
	}
	~SummonLifetimeFixtureB13()
	{
		callback = nullptr;
		for (auto& item : listeners) { item.first->RemoveEventListener(item.second.GetPtr()); item.second->action = nullptr; }
		for (auto& entry : units) entry.unit->Cleanup();
		master->DetachFromPlayer(); master->Cleanup();
	}
};

/** Original controlled lifetimes use strict greater-than; nonpositive and epsilon limits are unlimited. */
static void SummonLifetimeB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (int type : {-1, int(NDb::SUMMONTYPE_PRIMARY), int(NDb::SUMMONTYPE_SECONDARY), int(NDb::SUMMONTYPE_PET)})
		for (float duration : {-1.f, 0.f, EPS_VALUE, 1.f}) for (bool controlled : {false, true})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		SummonLifetimeFixtureB13 f(isolated.world); auto entry = f.Add(type, duration, controlled ? PFSummonBehaviour::BEHAVIOURFLAGS_CONTROLLEDLIFETIME : 0);
		check(!entry.unit->IsDead() && entry.unit->GetBehaviour() == entry.behaviour && entry.behaviour->GetLifeTime() == 0,
			"B13 summoned creature installs its actual behavior with an unelapsed timer");
		isolated.world->Step(.75f, .75f);
		check(!entry.unit->IsDead() && entry.behaviour->GetLifeTime() == .75f, "B13 live summon advances its real behavior timer once per world tick");
		isolated.world->Step(.25f, .25f);
		check(!entry.unit->IsDead() && entry.behaviour->GetLifeTime() == 1, "B13 summon remains alive at the exact authored lifetime boundary");
		isolated.world->Step(.125f, .125f); const bool expired = controlled && duration > EPS_VALUE;
		check(entry.unit->IsDead() == expired && entry.behaviour->GetLifeTime() == (expired ? -1 : 1.125f)
			&& entry.unit->HasBehaviour() != expired,
			"B13 only controlled positive summon lifetime crossing performs original unsummon and removes its behavior");
		check(f.Count(entry.unit, NDb::BASEUNITEVENT_UNSUMMON) == int(expired) && f.Count(f.master, NDb::BASEUNITEVENT_UNSUMMON) == 0
			&& f.Count(f.master, NDb::BASEUNITEVENT_DEATH) == 0,
			"B13 timed unsummon sends one local event without broadcasting an ordinary kill to the owner hero");
		if (type >= 0) check(f.master->GetSummonsCount(static_cast<NDb::SummonType>(type), "Lifetime") == int(!expired),
			"B13 timed unsummon releases exactly its original named owner-group membership");
		if (expired)
		{
			check(entry.unit->CorpseTime() == .375f && entry.unit->removals == 0, "B13 expiry selects original unsummon corpse delay including the current tick");
			isolated.world->Step(.375f, .375f);
			check(!entry.unit->IsObjectDead() && entry.unit->CorpseTime() == 0, "B13 unsummon corpse persists at its exact deadline");
			isolated.world->Step(.125f, .125f);
			check(entry.unit->IsObjectDead() && entry.unit->removals == 1 && !isolated.world->FindLinuxUnitByObjectId(entry.unit->GetObjectId()),
				"B13 expired summon retires its real world identity strictly after its corpse deadline");
		}
	}
	std::printf("B13 summon lifetime checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Explicit removal shares the same death path and cannot reenter through its own local callback. */
static void SummonExplicitRemovalB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (int type : {-1, int(NDb::SUMMONTYPE_PRIMARY), int(NDb::SUMMONTYPE_SECONDARY), int(NDb::SUMMONTYPE_PET)})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		SummonLifetimeFixtureB13 f(isolated.world); auto entry = f.Add(type, 0);
		const int gold = f.master->GetGold();
		f.callback = [&](const PFBaseUnitEventUnitDeath& event) {
			if (event.GetVictim() == entry.unit && f.Count(entry.unit, NDb::BASEUNITEVENT_UNSUMMON) == 1) entry.behaviour->Unsummon();
		};
		entry.behaviour->Unsummon(); entry.behaviour->Unsummon(); f.callback = nullptr;
		check(entry.unit->IsDead() && !entry.unit->HasBehaviour() && entry.behaviour->GetLifeTime() == -1,
			"B13 explicit unsummon kills the actual creature and releases its behavior");
		check(f.Count(entry.unit, NDb::BASEUNITEVENT_UNSUMMON) == 1 && f.Count(f.master, NDb::BASEUNITEVENT_DEATH) == 0
			&& f.master->GetGold() == gold, "B13 repeated and callback-recursive unsummon cannot emit another death or award owner gold");
		check(entry.unit->CorpseTime() == .5f, "B13 explicit unsummon uses authored unsummon rather than ordinary death time");
		if (type >= 0) check(f.master->GetSummonsCount(static_cast<NDb::SummonType>(type), "Lifetime") == 0,
			"B13 explicit removal releases the owner group once");
	}
	std::printf("B13 explicit summon removal checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Owner death removes primary summons and pets while retaining independent secondary summons. */
static void SummonOwnerDeathB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (int flags : {0, int(PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS), int(PFBaseUnit::UNITDIEFLAGS_DEFERREDDEATH),
		int(PFBaseUnit::UNITDIEFLAGS_DEFERREDDEATH | PFBaseUnit::UNITDIEFLAGS_FORBIDREWARDS)})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		SummonLifetimeFixtureB13 f(isolated.world);
		for (int type : {int(NDb::SUMMONTYPE_PRIMARY), int(NDb::SUMMONTYPE_PET), int(NDb::SUMMONTYPE_SECONDARY)})
			for (int i = 0; i < 2; ++i) f.Add(type, 0);
		f.master->KillUnit(nullptr, flags);
		check(f.master->IsDead() == !(flags & PFBaseUnit::UNITDIEFLAGS_DEFERREDDEATH),
			"B13 dependent summon cleanup preserves the owner's original deferred-death health policy");
		for (int i = 0; i < int(f.units.size()); ++i)
		{
			const auto& entry = f.units[i]; const bool removed = i < 4;
			check(entry.unit->IsDead() == removed && entry.unit->HasBehaviour() != removed
				&& f.Count(entry.unit, NDb::BASEUNITEVENT_UNSUMMON) == int(removed),
				"B13 owner death uses original primary/pet removal policy and leaves secondary summons alive");
		}
		check(f.master->GetSummonedGroup(NDb::SUMMONTYPE_PRIMARY)->GetSize() == 0
			&& f.master->GetSummonedGroup(NDb::SUMMONTYPE_PET)->GetSize() == 0
			&& f.master->GetSummonedGroup(NDb::SUMMONTYPE_SECONDARY)->GetSize() == 2,
			"B13 mutation-safe owner death traversal drains only the original dependent summon groups");
	}
	for (bool transferred : {false, true})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		SummonLifetimeFixtureB13 f(isolated.world); auto entry = f.Add(NDb::SUMMONTYPE_PRIMARY, 0);
		auto other = B9MaleHero(isolated.world, NDb::FACTION_FREEZE);
		if (transferred) entry.unit->SetMaster(CPtr<PFBaseUnit>(other.GetPtr()));
		PFSummonBehaviour::UnsummonFunc remove(f.master); remove(entry.behaviour);
		check(entry.unit->IsDead() != transferred && f.master->GetSummonedGroup(NDb::SUMMONTYPE_PRIMARY)->GetSize() == 0,
			"B13 owner removal kills only units still owned by that master, otherwise stopping the old group behavior");
		other->Cleanup();
	}
	std::printf("B13 summon owner death checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Disabled behavior does not age; resumed alpha expiry releases its exact owner reference. */
static void SummonPausedLifetimeB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	B8BehaviourForeignWorld isolated(check); if (!isolated.world) return;
	SummonLifetimeFixtureB13 f(isolated.world);
	auto entry = f.Add(NDb::SUMMONTYPE_SECONDARY, 1,
		PFSummonBehaviour::BEHAVIOURFLAGS_CONTROLLEDLIFETIME | PFSummonBehaviour::BEHAVIOURFLAGS_ALPHASUMMON);
	check(f.master->GetAlphaSummon() == entry.unit && entry.unit->IsAlphaSummon(), "B13 real alpha summon factory records its owner identity");
	entry.behaviour->Disable(); isolated.world->Step(2, 2);
	check(!entry.unit->IsDead() && entry.behaviour->GetLifeTime() == 0, "B13 disabled behavior does not spend its summon lifetime");
	entry.behaviour->Enable(); isolated.world->Step(1, 1);
	check(!entry.unit->IsDead() && entry.behaviour->GetLifeTime() == 1 && f.master->GetAlphaSummon() == entry.unit,
		"B13 reenabled alpha summon resumes its unspent timer and retains the exact deadline");
	isolated.world->Step(.125f, .125f);
	check(entry.unit->IsDead() && !f.master->GetAlphaSummon() && !entry.unit->IsAlphaSummon()
		&& f.master->GetSummonsCount(NDb::SUMMONTYPE_SECONDARY, "Lifetime") == 0,
		"B13 timed alpha expiry releases both group and owner alpha identity");
	std::printf("B13 paused summon lifetime checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Mock only authored data; production factories own every summoned unit and behavior. */
static NDb::Ptr<NDb::SummonApplicator> SummonLifetimeDbB13(PFWorld* world, int type, const char* lifetime)
{
	auto* creature = new NDb::Summoned; creature->NDb::Creature::Assign(*DeadCreatureDbB13(world));
	auto* raw = new NDb::SummonApplicator; raw->summonedUnits.push_back(creature);
	raw->enabled.sString = "true"; raw->lifeTime.sString = lifetime; raw->summonedUnitsCount.sString = "1";
	raw->summonedUnitIndex.sString = "0"; raw->placeMode = NDb::SUMMONPLACEMODE_BYAPPLICATOR;
	raw->flags = NDb::SUMMONFLAGS_NOSUMMONANIMATION; raw->summonGroupName = "FactoryLifetime";
	if (type >= 0)
	{
		auto* behaviour = new NDb::SummonBehaviourCommon; behaviour->summonType = static_cast<NDb::SummonType>(type);
		behaviour->maxCount.sString = "1"; behaviour->lashRange.sString = "10"; raw->behaviour = behaviour;
	}
	return raw;
}

/** Actual SummonApplicator admission creates, owns and retires its timer-driven creature. */
static void SummonApplicatorLifetimeB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (int type : {-1, int(NDb::SUMMONTYPE_PRIMARY), int(NDb::SUMMONTYPE_SECONDARY), int(NDb::SUMMONTYPE_PET)})
		for (const char* duration : {"1", "cWrapperBonus/2", "0,1", "random(2,4),1", "", "0", "-1"})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		NaftaFixtureB13 f(isolated.world);
		auto db = SummonLifetimeDbB13(isolated.world, type, duration); auto app = f.Create(db);
		NRandom::RandomGenerator draws(*isolated.world->GetRndGen());
		if (std::strcmp(duration, "random(2,4),1") == 0) draws.Next(2, 4);
		check(app && f.Activate(app) && !app->IsAlive() && f.instance->GetActiveApplicatorsCount() == 0,
			"B13 actual instant summon applicator balances cast ownership independently of summoned lifetime");
		check(GlyphRngEqualB13(*isolated.world->GetRndGen(), draws), "B13 summon lifetime evaluates its original formula once before creating the unit");
		isolated.world->Step(0, 0);
		CObj<PFBaseSummonedUnit> unit; int count = 0;
		auto observe = [&](PF_Core::WorldObjectBase& object) {
			if (auto* candidate = dynamic_cast<PFBaseSummonedUnit*>(&object))
				if (candidate->GetMasterUnit() == f.sender) { unit = candidate; ++count; }
		};
		isolated.world->ForAllObjectsBeingStepped(observe);
		check(count == 1 && unit && !unit->IsDead() && unit->HasBehaviour(), "B13 actual summon applicator creates one registered creature with its timer behavior");
		if (!unit) continue;
		unit->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); unit->AddFlag(NDb::UNITFLAG_FORBIDMOVE); unit->EnableAbilityAI(false);
		const bool limited = *duration && std::strcmp(duration, "0") != 0 && std::strcmp(duration, "-1") != 0;
		isolated.world->Step(1, 1);
		check(!unit->IsDead(), "B13 applicator-created summon remains alive at its original duration boundary");
		isolated.world->Step(.125f, .125f);
		check(unit->IsDead() == limited && unit->HasBehaviour() != limited,
			"B13 applicator-created duration expires through real stepping while empty zero and negative durations remain unlimited");
		if (type >= 0) check(f.sender->GetSummonsCount(static_cast<NDb::SummonType>(type), "FactoryLifetime") == int(!limited),
			"B13 applicator-created expiry releases the exact authored owner group");
		isolated.world->Step(.5f, .5f);
		check(unit->IsObjectDead() == limited && bool(isolated.world->FindLinuxUnitByObjectId(unit->GetObjectId())) != limited,
			"B13 only expired applicator-created summons retire their corpse and world identity naturally");
	}
	std::printf("B13 summon applicator lifetime checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Invalid durations cannot replace a live summon; accepted recasts retain original pet-toggle policy. */
static void SummonReplacementB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (int type : {int(NDb::SUMMONTYPE_PRIMARY), int(NDb::SUMMONTYPE_SECONDARY), int(NDb::SUMMONTYPE_PET)})
	{
		B8BehaviourForeignWorld isolated(check); if (!isolated.world) continue;
		NaftaFixtureB13 f(isolated.world); auto db = SummonLifetimeDbB13(isolated.world, type, "10");
		auto cast = f.Create(db); check(cast && f.Activate(cast), "B13 replacement fixture spawns through the actual summon applicator");
		isolated.world->Step(0, 0);
		std::vector<CObj<PFBaseSummonedUnit>> live;
		auto collect = [&](PF_Core::WorldObjectBase& object) {
			if (auto* unit = dynamic_cast<PFBaseSummonedUnit*>(&object))
				if (!unit->IsDead() && unit->GetMasterUnit() == f.sender) live.emplace_back(unit);
		};
		isolated.world->ForAllObjectsBeingStepped(collect);
		check(live.size() == 1, "B13 actual summon replacement starts with one owned creature");
		if (live.size() != 1) continue;
		CObj<PFBaseSummonedUnit> first = live[0];
		for (const char* expression : {"cMissing", "1/0", "1e39f", "0,", "random(2,4),cMissing", "compiled-only"})
		{
			auto* raw = new NDb::SummonApplicator; raw->Assign(*db); raw->lifeTime.sString = expression;
			if (std::strcmp(expression, "compiled-only") == 0) { raw->lifeTime.sString.clear(); raw->lifeTime.compiledString = "unsupported"; }
			NDb::Ptr<NDb::SummonApplicator> invalid = raw; auto rejected = f.Create(invalid);
			const NRandom::RandomGenerator saved(*isolated.world->GetRndGen());
			check(rejected && f.Activate(rejected) && !rejected->IsAlive() && f.instance->GetActiveApplicatorsCount() == 0,
				"B13 failed summon duration retires its instant applicator with balanced cast ownership");
			isolated.world->Step(0, 0); live.clear(); isolated.world->ForAllObjectsBeingStepped(collect);
			check(live.size() == 1 && live[0] == first && !first->IsDead()
				&& f.sender->GetSummonsCount(static_cast<NDb::SummonType>(type), "FactoryLifetime") == 1
				&& GlyphRngEqualB13(*isolated.world->GetRndGen(), saved),
				"B13 rejected duration cannot replace or toggle the old summon, create another unit, or consume a staged random draw");
		}
		auto again = f.Create(db); check(again && f.Activate(again), "B13 valid recast reaches the original summon replacement policy");
		isolated.world->Step(0, 0); live.clear(); isolated.world->ForAllObjectsBeingStepped(collect);
		const bool pet = type == NDb::SUMMONTYPE_PET;
		check(first->IsDead() && (pet ? live.empty() : live.size() == 1 && live[0] != first)
			&& f.sender->GetSummonsCount(static_cast<NDb::SummonType>(type), "FactoryLifetime") == int(!pet),
			"B13 valid pet recast toggles off while primary and secondary recasts replace the old max-count summon");
		isolated.world->Step(.625f, .625f);
		check(first->IsObjectDead(), "B13 replacement retires the old summoned corpse through original unsummon timing");
	}
	std::printf("B13 summon replacement checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
