/** Inspect the engine's stored records and seed malformed serialized state only. */
struct PriorityProbeB12 : ProbeUnit
{
	PriorityProbeB12(PFWorld* world, const NDb::Unit* db) : ProbeUnit(world, db, NDb::FACTION_FREEZE) {}
	int Count() const { return priorityTargets.size(); }
	float Remaining(int id) const
	{
		const auto it = priorityTargets.find(id);
		return it == priorityTargets.end() ? -999.0f : it->second.timeOut;
	}
	void Seed(int key, PFBaseUnit* target, int storedId, float time)
	{
		auto& record = priorityTargets[key];
		record.pTarget = target; record.objectId = storedId; record.timeOut = time;
	}
};

/** Use authored targeting overrides without replacing any production lookup or clock logic. */
static void PriorityDurationB12(PFBaseUnit* unit, float duration)
{
	NDb::UnitTargetingParameters* params = new NDb::UnitTargetingParameters;
	params->maxTimeFromAttack = duration;
	params->targetingRange = 20;
	unit->OverrideTargetingParams(params);
}

static CObj<PriorityProbeB12> PriorityUnitB12(PFWorld* world, float duration = 2)
{
	CObj<PFBaseUnit> seed = Unit(world, NDb::FACTION_FREEZE, 20);
	CObj<PriorityProbeB12> unit = new PriorityProbeB12(world, seed->DbUnitDesc());
	PriorityDurationB12(unit.GetPtr(), duration);
	unit->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
	unit->AddFlag(NDb::UNITFLAG_FORBIDATTACK);
	return unit;
}

/** Positive clocks refresh rather than accumulate; nonpositive authored clocks expire on update. */
static void PriorityRetentionB12(Checks& check)
{
	B8BehaviourForeignWorld fixture(check);
	if (!fixture.world) return;
	CObj<PriorityProbeB12> owner = PriorityUnitB12(fixture.world);
	CObj<PFBaseUnit> target = Unit(fixture.world, NDb::FACTION_BURN, 20);
	const int id = target->GetObjectId();
	owner->AddToPriorityTargets(target.GetPtr());
	check(owner->IsPriorityTarget(target.GetPtr()) && owner->Count() == 1 && owner->Remaining(id) == 2,
		"authored priority duration is stored for the exact target identity");
	owner->UpdatePriorityTargets(.5f);
	check(owner->IsPriorityTarget(target.GetPtr()) && owner->Remaining(id) == 1.5f, "finite elapsed time reduces the retained deadline");
	PriorityDurationB12(owner.GetPtr(), 3);
	check(owner->Remaining(id) == 1.5f, "changing authored parameters does not retroactively reset an existing record");
	owner->AddToPriorityTargets(target.GetPtr());
	check(owner->Count() == 1 && owner->Remaining(id) == 3, "repeat insertion refreshes from current authored duration instead of accumulating");
	owner->UpdatePriorityTargets(2.75f);
	check(owner->IsPriorityTarget(target.GetPtr()) && owner->Remaining(id) == .25f, "priority survives until its exact positive boundary");
	owner->UpdatePriorityTargets(.25f);
	check(!owner->IsPriorityTarget(target.GetPtr()) && owner->Count() == 0, "priority expires exactly at its deadline");
	for (float duration : {0.0f, -1.0f, -100.0f})
	{
		PriorityDurationB12(owner.GetPtr(), duration);
		owner->AddToPriorityTargets(target.GetPtr());
		check(owner->IsPriorityTarget(target.GetPtr()) && owner->Remaining(id) == duration,
			"original finite zero or negative duration is observable before the next update");
		owner->UpdatePriorityTargets(0);
		check(!owner->IsPriorityTarget(target.GetPtr()) && owner->Count() == 0,
			"zero-time maintenance removes nonpositive clocks rather than treating minus one as permanent");
	}
	PriorityDurationB12(owner.GetPtr(), 1);
	owner->AddToPriorityTargets(target.GetPtr());
	owner->UpdatePriorityTargets(100);
	check(owner->Count() == 0, "a large valid step expires the priority without a residual record");
	owner->AddToPriorityTargets(owner.GetPtr());
	check(owner->IsPriorityTarget(owner.GetPtr()), "retention does not invent a self-target selection policy");
	owner->UpdatePriorityTargets(1);
	owner->AddToPriorityTargets(target.GetPtr());
	target->ChangeHealth(-target->GetHealth());
	check(target->IsDead() && owner->IsPriorityTarget(target.GetPtr()), "valid zero-health target retains its original data lifetime");
	owner->UpdatePriorityTargets(1);
	check(!owner->IsPriorityTarget(target.GetPtr()), "zero-health target still expires by the retained clock");
}

/** Resolve only alpha summons on insertion; direct membership queries do not resolve aliases. */
static void PriorityMasterB12(Checks& check)
{
	B8BehaviourForeignWorld fixture(check), foreign(check);
	if (!fixture.world || !foreign.world) return;
	CObj<PriorityProbeB12> owner = PriorityUnitB12(fixture.world);
	CObj<PFBaseUnit> master = Unit(fixture.world, NDb::FACTION_BURN, 20);
	CObj<PFBaseUnit> alpha = Unit(fixture.world, NDb::FACTION_BURN, 20);
	CObj<PFBaseUnit> ordinary = Unit(fixture.world, NDb::FACTION_BURN, 20);
	alpha->SetMaster(master.GetPtr()); master->SetAlphaSummon(alpha.GetPtr());
	ordinary->SetMaster(master.GetPtr());
	check(alpha->IsAlphaSummon() && !ordinary->IsAlphaSummon(), "real master bindings distinguish alpha and ordinary summons");
	owner->AddToPriorityTargets(alpha.GetPtr());
	check(owner->Count() == 1 && owner->IsPriorityTarget(master.GetPtr()) && !owner->IsPriorityTarget(alpha.GetPtr()),
		"alpha insertion retains the master while membership lookup remains exact");
	owner->UpdatePriorityTargets(.5f);
	PriorityDurationB12(owner.GetPtr(), 4);
	owner->AddToPriorityTargets(alpha.GetPtr());
	check(owner->Count() == 1 && owner->Remaining(master->GetObjectId()) == 4, "alpha refresh updates the existing master's timer");
	owner->AddToPriorityTargets(ordinary.GetPtr());
	check(owner->Count() == 2 && owner->IsPriorityTarget(ordinary.GetPtr()), "ordinary summon keeps its own identity instead of substituting its master");
	CObj<PFBaseUnit> nextMaster = Unit(fixture.world, NDb::FACTION_BURN, 20);
	alpha->SetMaster(nextMaster.GetPtr()); nextMaster->SetAlphaSummon(alpha.GetPtr());
	owner->AddToPriorityTargets(alpha.GetPtr());
	check(owner->Count() == 3 && owner->IsPriorityTarget(master.GetPtr()) && owner->IsPriorityTarget(nextMaster.GetPtr()),
		"a changed master creates a new retention entry without erasing the previous master's independent clock");
	CObj<PFBaseUnit> otherMaster = Unit(foreign.world, NDb::FACTION_BURN, 20);
	alpha->SetMaster(otherMaster.GetPtr()); otherMaster->SetAlphaSummon(alpha.GetPtr());
	owner->AddToPriorityTargets(alpha.GetPtr());
	check(owner->Count() == 3 && !owner->IsPriorityTarget(otherMaster.GetPtr()), "resolved foreign master cannot enter the local retention table");
	owner->UpdatePriorityTargets(4);
	check(owner->Count() == 0, "one traversal expires multiple independent records safely");
}

/** Validate world/identity context and reject malformed retained records without owning targets. */
static void PriorityContextB12(Checks& check)
{
	B8BehaviourForeignWorld fixture(check), foreign(check);
	if (!fixture.world || !foreign.world) return;
	CObj<PriorityProbeB12> owner = PriorityUnitB12(fixture.world);
	CObj<PriorityProbeB12> mirror = PriorityUnitB12(foreign.world);
	CObj<PFBaseUnit> target = Unit(fixture.world, NDb::FACTION_BURN, 20);
	CObj<PFBaseUnit> foreignTarget = Unit(foreign.world, NDb::FACTION_BURN, 20);
	check(target->GetObjectId() == foreignTarget->GetObjectId() && target.GetPtr() != foreignTarget.GetPtr(),
		"isolated worlds provide distinct actual units with colliding object IDs");
	owner->AddToPriorityTargets(target.GetPtr());
	check(!owner->IsPriorityTarget(foreignTarget.GetPtr()), "foreign same-ID query cannot borrow a local priority");
	owner->AddToPriorityTargets(foreignTarget.GetPtr());
	check(owner->Count() == 1 && owner->IsPriorityTarget(target.GetPtr()), "foreign same-ID insertion cannot replace the retained local target");
	owner->AddToPriorityTargets(nullptr);
	check(!owner->IsPriorityTarget(nullptr) && owner->Count() == 1, "null priority input is a no-op");
	CObj<PriorityProbeB12> detached = PriorityUnitB12(nullptr);
	CObj<PFBaseUnit> unregistered = Unit(nullptr, NDb::FACTION_BURN, 20);
	detached->AddToPriorityTargets(unregistered.GetPtr());
	owner->AddToPriorityTargets(unregistered.GetPtr());
	check(detached->Count() == 0 && owner->Count() == 1 && !owner->IsPriorityTarget(unregistered.GetPtr()),
		"missing world and invalid object-ID contexts do not create shared sentinel entries");
	CPtr<PFBaseUnit> expired = unregistered.GetPtr(); unregistered = nullptr;
	check(!IsValid(expired), "actual engine weak reference becomes invalid when its last owner releases it");
	owner->AddToPriorityTargets(expired);
	check(owner->Count() == 1 && !owner->IsPriorityTarget(expired), "invalid engine references are rejected by insertion and query");
	const int id = target->GetObjectId();
	owner->Seed(id, foreignTarget.GetPtr(), id, 2);
	check(!owner->IsPriorityTarget(target.GetPtr()), "same-ID record with the wrong target pointer cannot satisfy a query");
	owner->UpdatePriorityTargets(0);
	check(owner->Count() == 0, "zero-time maintenance removes a foreign serialized record");
	owner->Seed(id + 10000, target.GetPtr(), id, 2);
	owner->Seed(id + 10001, expired.GetPtr(), id + 10001, 2);
	owner->Seed(id, target.GetPtr(), id + 10, 2);
	check(!owner->IsPriorityTarget(target.GetPtr()), "mismatched stored identity is not a valid priority");
	owner->UpdatePriorityTargets(0);
	check(owner->Count() == 0, "malformed keys and invalid targets are erased by actual map key without iterator invalidation");
	owner->OverrideTargetingParams(nullptr);
	check(owner->GetTargetingParamsPtr() == nullptr, "isolated mock AI provides no fallback targeting parameters");
	owner->AddToPriorityTargets(target.GetPtr());
	check(owner->Count() == 0, "missing authored targeting context is rejected without dereferencing a null fallback");
}

/** Invalid elapsed clocks never extend or poison retained time; invalid stored clocks are cleaned. */
static void PriorityClocksB12(Checks& check)
{
	B8BehaviourForeignWorld fixture(check);
	if (!fixture.world) return;
	CObj<PriorityProbeB12> owner = PriorityUnitB12(fixture.world);
	CObj<PFBaseUnit> target = Unit(fixture.world, NDb::FACTION_BURN, 20);
	const int id = target->GetObjectId();
	owner->AddToPriorityTargets(target.GetPtr());
	for (float dt : {-1.0f, std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
	{
		owner->UpdatePriorityTargets(dt);
		check(owner->IsPriorityTarget(target.GetPtr()) && owner->Remaining(id) == 2,
			"negative or non-finite elapsed time is a no-op for retained clocks");
	}
	for (float duration : {std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
	{
		PriorityDurationB12(owner.GetPtr(), duration);
		owner->AddToPriorityTargets(target.GetPtr());
		check(owner->Remaining(id) == 2, "invalid authored refresh cannot replace a valid retained clock");
		owner->Seed(id, target.GetPtr(), id, duration);
		check(!owner->IsPriorityTarget(target.GetPtr()), "non-finite serialized timeout is never exposed as valid priority");
		owner->UpdatePriorityTargets(0);
		check(owner->Count() == 0, "valid maintenance removes non-finite serialized timeouts");
		owner->AddToPriorityTargets(target.GetPtr());
		check(owner->Count() == 0, "non-finite authored duration cannot create an immortal priority");
		PriorityDurationB12(owner.GetPtr(), 2); owner->AddToPriorityTargets(target.GetPtr());
	}
	PriorityDurationB12(owner.GetPtr(), std::numeric_limits<float>::max());
	owner->AddToPriorityTargets(target.GetPtr());
	owner->UpdatePriorityTargets(std::numeric_limits<float>::max());
	check(owner->Count() == 0, "largest finite duration expires at an equally large finite delta");
	PriorityDurationB12(owner.GetPtr(), -std::numeric_limits<float>::max());
	owner->AddToPriorityTargets(target.GetPtr());
	owner->UpdatePriorityTargets(std::numeric_limits<float>::max());
	check(owner->Count() == 0, "finite subtraction overflow cannot leave a retained non-finite clock");
}

/** Observe the actual behavior phase without replacing base-unit stepping or timer operations. */
struct PriorityBehaviorB12 : PFBaseBehaviour
{
	std::function<void()> action;
	PriorityBehaviorB12() { SetFaction(NDb::FACTION_FREEZE); SetUnitType(NDb::UNITTYPE_BUILDING); }
	bool OnStep(float) override { if (action) action(); return true; }
};

/** Advance a fresh world only; priority expiry runs once and precedes behavior callbacks. */
static void PrioritySteppingB12(Checks& check)
{
	B8BehaviourForeignWorld fixture(check), foreign(check);
	if (!fixture.world || !foreign.world) return;
	CObj<PriorityProbeB12> owner = PriorityUnitB12(fixture.world, 1);
	CObj<PFBaseUnit> target = Unit(fixture.world, NDb::FACTION_BURN, 20);
	CObj<PriorityProbeB12> elsewhere = PriorityUnitB12(foreign.world, 1);
	CObj<PFBaseUnit> foreignTarget = Unit(foreign.world, NDb::FACTION_BURN, 20);
	owner->AddToPriorityTargets(target.GetPtr()); elsewhere->AddToPriorityTargets(foreignTarget.GetPtr());
	fixture.world->Step(0, 0);
	for (float dt : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
		fixture.world->Step(dt, dt);
	check(owner->Remaining(target->GetObjectId()) == 1, "invalid and zero world ticks do not advance priority time");
	fixture.world->Step(.25f, .25f);
	check(owner->Remaining(target->GetObjectId()) == .75f, "stationary real-world step advances priority time exactly once");
	check(elsewhere->Remaining(foreignTarget->GetObjectId()) == 1, "stepping one world leaves another world's priority clocks unchanged");
	fixture.world->Step(.75f, .75f);
	check(!owner->IsPriorityTarget(target.GetPtr()), "world stepping expires the retained priority at the exact deadline");
	CObj<PriorityBehaviorB12> observer = new PriorityBehaviorB12;
	owner->AddBehaviourOnTop(observer.GetPtr());
	bool expiredBeforeBehavior = false;
	observer->action = [&] {
		expiredBeforeBehavior = !owner->IsPriorityTarget(target.GetPtr());
		owner->AddToPriorityTargets(target.GetPtr());
	};
	owner->AddToPriorityTargets(target.GetPtr());
	fixture.world->Step(1, 1);
	check(expiredBeforeBehavior && owner->Remaining(target->GetObjectId()) == 1,
		"expiry precedes behavior and a callback refresh is not aged again during the same step");
	observer->action = {};
	owner->RemoveBehaviour(observer.GetPtr());
	CObj<B8BehaviourCreep> moving = B8BehaviourMoving(fixture.world, NDb::FACTION_FREEZE);
	moving->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET); moving->AddFlag(NDb::UNITFLAG_FORBIDATTACK);
	PriorityDurationB12(moving.GetPtr(), 1);
	moving->AddToPriorityTargets(target.GetPtr());
	fixture.world->Step(.5f, .5f);
	check(moving->IsPriorityTarget(target.GetPtr()), "moving real-world unit does not double-age the base-unit priority clock");
	fixture.world->Step(.5f, .5f);
	check(!moving->IsPriorityTarget(target.GetPtr()), "moving real-world unit reaches expiry through the same base Step hook");
}

/** Pin the explicit unported scorer boundary instead of inventing a priority weight. */
static void PrioritySelectionBoundaryB12(Checks& check)
{
	B8BehaviourForeignWorld fixture(check);
	if (!fixture.world) return;
	CObj<PriorityProbeB12> owner = PriorityUnitB12(fixture.world);
	NDb::Creature* raw = new NDb::Creature;
	raw->stats = owner->DbUnitDesc()->stats; raw->targetingParams = owner->GetTargetingParamsPtr();
	NDb::Ptr<NDb::Creature> db = raw;
	CObj<B8BehaviourCreep> near = new B8BehaviourCreep(fixture.world, *db, NDb::FACTION_BURN);
	CObj<B8BehaviourCreep> far = new B8BehaviourCreep(fixture.world, *db, NDb::FACTION_BURN);
	check(near->TeleportTo(CVec2(8, 5), true, false) && far->TeleportTo(CVec2(15, 5), true, false),
		"selection boundary fixture positions real indexed targets");
	B8BehaviourReveal reveal(fixture.world, NDb::FACTION_FREEZE);
	owner->AddToPriorityTargets(far.GetPtr());
	NDb::UnitTargetingParameters* params = new NDb::UnitTargetingParameters;
	params->maxTimeFromAttack = 2; params->targetingRange = 20; params->attackerNearbyHeroWeight = 10000;
	params->unitTypeTableWeight = 1; params->unitTargetWeight[NDb::UNITTYPE_BUILDING] = -10000;
	owner->OverrideTargetingParams(params);
	check(owner->GetTargetWeight(far.GetPtr(), *params) == 0,
		"known boundary: native GetTargetWeight remains a stub despite authored priority weight");
	check(owner->IsPriorityTarget(far.GetPtr()) && owner->FindTarget(20, false, 0, false) == near.GetPtr(),
		"known boundary: retained priority does not replace the native nearest-target scan");
	far->AddFlag(NDb::UNITFLAG_FORBIDAUTOTARGETME);
	check(!owner->CanSelectTarget(far.GetPtr(), true) && owner->IsPriorityTarget(far.GetPtr()),
		"retention does not bypass an authored target-selection exclusion");
	check(!owner->GetCurrentTarget() && params->unitTargetWeight[NDb::UNITTYPE_BUILDING] == -10000,
		"priority bookkeeping neither assigns a target nor rewrites authored building weights");
}

/** Isolate both session context and each stepped world from the accumulated parent fixtures. */
static void PriorityTargetB12(Checks& check)
{
	const NDb::Ptr<NDb::SessionRoot> saved = NDb::SessionRoot::GetRoot();
	NDb::SessionLogicRoot* logic = new NDb::SessionLogicRoot; logic->aiLogic = new NDb::AILogicParameters;
	NDb::SessionRoot* root = new NDb::SessionRoot; root->logicRoot = logic;
	NDb::SessionRoot::InitRoot(root);
	PriorityRetentionB12(check);
	PriorityMasterB12(check);
	PriorityContextB12(check);
	PriorityClocksB12(check);
	PrioritySteppingB12(check);
	PrioritySelectionBoundaryB12(check);
	NDb::SessionRoot::InitRoot(saved);
}
