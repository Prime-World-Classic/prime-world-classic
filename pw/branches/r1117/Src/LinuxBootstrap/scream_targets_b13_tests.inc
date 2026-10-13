/** Real target selection consumes help requests without reapplying ordinary scan range/fog. */
static void ScreamSelectionB13(Checks& check)
{
	B8BehaviourForeignWorld f(check); if (!f.world) return;
	auto params = WeightedParamsB12(); auto* p = const_cast<NDb::UnitTargetingParameters*>(params.GetPtr());
	p->unitTypeTableWeight = 1; p->unitTargetWeight[NDb::UNITTYPE_CREEP] = 1; p->unitTargetWeight[NDb::UNITTYPE_BUILDING] = 5;
	auto owner = WeightedUnitCreateB12(f.world, p);
	auto near = WeightedUnitCreateB12(f.world, p, NDb::FACTION_BURN);
	auto far = WeightedUnitCreateB12(f.world, p, NDb::FACTION_BURN, NDb::UNITTYPE_BUILDING);
	near->Place(CVec3(9,5,0)); far->Place(CVec3(35,5,0));
	B8BehaviourReveal reveal(f.world, NDb::FACTION_FREEZE);
	check(!far->IsVisibleForFaction(NDb::FACTION_FREEZE) && !owner->IsTargetInRange(far.GetPtr(),20),
		"help fixture target is outside ordinary range and current faction fog");
	for (auto type : {ScreamTarget::ScreamForHelp, ScreamTarget::ScreamAlert})
	{
		owner->OnScream(far.GetPtr(), type);
		check(owner->FindTarget(20,false) == near.GetPtr(), "ordinary scan neither considers nor consumes pending help targets");
		check(owner->FindTarget(20,true) == far.GetPtr(), "queued help target uses original no-range no-fog additional selection");
		check(owner->FindTarget(20,true) == near.GetPtr(), "successful help scan consumes its old queue exactly once");
	}
	for (float score : {-100.f, -1.f, -.5f, 1.f, 2.f})
	{
		p->unitTargetWeight[NDb::UNITTYPE_BUILDING] = score;
		owner->OnScream(far.GetPtr(), ScreamTarget::ScreamForHelp);
		check(owner->FindTarget(20,true) == (score > 1 ? far.GetPtr() : near.GetPtr()),
			"help candidates share signed authored weights and preserve earlier scan ties");
	}
	p->unitTargetWeight[NDb::UNITTYPE_BUILDING] = -.5f;
	near->AddFlag(NDb::UNITFLAG_FORBIDAUTOTARGETME);
	owner->OnScream(far.GetPtr(), ScreamTarget::ScreamForHelp);
	check(owner->FindTarget(20,true) == far.GetPtr(), "negative help score above minus one remains selectable");
	far->AddFlag(NDb::UNITFLAG_FORBIDAUTOTARGETME);
	owner->OnScream(far.GetPtr(), ScreamTarget::ScreamForHelp);
	check(!owner->FindTarget(20,true), "help cannot bypass automatic-target exclusion");
	far->RemoveFlag(NDb::UNITFLAG_FORBIDAUTOTARGETME);
	owner->OnScream(far.GetPtr(), ScreamTarget::ScreamForHelp);
	owner->ClearScreamTargets();
	check(!owner->FindTarget(20,true), "explicit help cleanup cancels queued candidates");
	owner->OnScream(far.GetPtr(), ScreamTarget::ScreamForHelp);
	owner->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
	check(!owner->FindTarget(20,true), "forbidden selection cannot consume help as a bypass");
	owner->RemoveFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
	check(owner->FindTarget(20,true) == far.GetPtr(), "unprocessed help survives the existing early forbid return");
}

/** Weak queued identities must not authorize foreign, dead, removed or corrupted targets. */
static void ScreamInvalidB13(Checks& check)
{
	for (int reason = 0; reason != 6; ++reason)
	{
		B8BehaviourForeignWorld f(check), foreign(check); if (!f.world || !foreign.world) continue;
		auto params = WeightedParamsB12();
		auto owner = WeightedUnitCreateB12(f.world, params);
		auto target = WeightedUnitCreateB12(reason == 0 ? foreign.world : f.world, params, NDb::FACTION_BURN);
		target->Place(CVec3(35,5,0));
		owner->OnScream(target.GetPtr(), ScreamTarget::ScreamForHelp);
		if (reason == 1) f.world->UnregisterObject(target.GetPtr(),false);
		if (reason == 2) { target->ChangeHealth(-target->GetHealth()); check(target->IsDead(), "help fixture uses real death health"); }
		if (reason == 3) target->AddFlag(NDb::UNITFLAG_FORBIDAUTOTARGETME);
		if (reason == 4) target->Place(CVec3(std::numeric_limits<float>::quiet_NaN(),5,0));
		if (reason == 5) const_cast<NDb::UnitTargetingParameters*>(params.GetPtr())->rangeWeight = std::numeric_limits<float>::infinity();
		const auto result = owner->FindTarget(20,true);
		if (result) std::printf("Scream invalid fixture reason=%d selected=%d\n", reason, result->GetObjectId());
		check(!result, "queued help validates exact world identity life eligibility and checked score");
	}
}

/** Policy callback mutation is test-only; production owns snapshots, scoring and lifetime. */
struct ScreamPolicyB13 : WeightedPolicyB12
{
	mutable std::function<void(const PFBaseUnit*,bool)> callback;
	bool accepts = true;
	bool CanSelectTarget(const PFBaseUnit* target, bool mustSee) const override
	{
		if (callback) callback(target,mustSee);
		return accepts;
	}
};

static void ScreamCallbacksB13(Checks& check)
{
	B8BehaviourForeignWorld f(check); if (!f.world) return;
	auto params = WeightedParamsB12();
	auto owner = WeightedUnitCreateB12(f.world, params);
	auto first = WeightedUnitCreateB12(f.world, params, NDb::FACTION_BURN);
	auto second = WeightedUnitCreateB12(f.world, params, NDb::FACTION_BURN);
	first->Place(CVec3(35,5,0)); second->Place(CVec3(36,5,0));
	CObj<ScreamPolicyB13> policy = new ScreamPolicyB13; owner->AddBehaviourOnTop(policy);
	int callbacks = 0;
	policy->callback = [&](const PFBaseUnit* target, bool mustSee) {
		if (mustSee || target != first.GetPtr()) return;
		++callbacks;
		f.world->UnregisterObject(first.GetPtr(),false);
		owner->ClearScreamTargets();
		owner->OnScream(second.GetPtr(), ScreamTarget::ScreamForHelp);
	};
	owner->OnScream(first.GetPtr(), ScreamTarget::ScreamForHelp);
	check(!owner->FindTarget(20,true) && callbacks == 1, "callback-unregistered help candidate is not selected");
	policy->callback = nullptr;
	check(owner->FindTarget(20,true) == second.GetPtr(), "help enqueued during evaluation waits for a later stable snapshot");
	check(!owner->FindTarget(20,true), "later help snapshot is consumed without stale replay");
	first = WeightedUnitCreateB12(f.world, params, NDb::FACTION_BURN);
	first->Place(CVec3(35,5,0));
	policy->callback = [&](const PFBaseUnit* target, bool mustSee) {
		if (mustSee || target != second.GetPtr()) return;
		f.world->UnregisterObject(first.GetPtr(),false);
		policy->accepts = false;
	};
	owner->OnScream(first.GetPtr(), ScreamTarget::ScreamForHelp);
	owner->OnScream(second.GetPtr(), ScreamTarget::ScreamForHelp);
	check(!owner->FindTarget(20,true), "denying callback cannot leave an earlier removed winner selectable");
	policy->accepts = true;
	first = WeightedUnitCreateB12(f.world, params, NDb::FACTION_BURN);
	first->Place(CVec3(35,5,0));
	CPtr<PFBaseUnit> weak = first.GetPtr();
	policy->callback = [&](const PFBaseUnit* target, bool mustSee) {
		if (mustSee || target != first.GetPtr()) return;
		f.world->GetAIWorld()->UnregisterObjectOrUnit(first.GetPtr());
		f.world->UnregisterObject(first.GetPtr(),false);
		first = nullptr;
	};
	owner->OnScream(first.GetPtr(), ScreamTarget::ScreamForHelp);
	const auto removed = owner->FindTarget(20,true);
	if (removed || IsValid(weak)) std::printf("Scream removal selected=%d refs=%d objs=%d registered=%d\n", bool(removed),
		IsValid(weak) ? weak->GetRefCount() : -1, IsValid(weak) ? weak->GetObjCount() : -1,
		IsValid(weak) ? f.world->FindLinuxUnitByObjectId(weak->GetObjectId()) == weak.GetPtr() : -1);
	check(!removed && !IsValid(weak), "queued snapshot safely releases the last strong candidate owner after callback removal");
	policy->callback = [&](const PFBaseUnit*, bool mustSee) { if (!mustSee) f.world->UnregisterObject(owner.GetPtr(),false); };
	owner->OnScream(second.GetPtr(), ScreamTarget::ScreamForHelp);
	check(!owner->FindTarget(20,true), "callback-removed requester cannot finish selecting a target");
	policy->callback = nullptr;
	owner->RemoveBehaviour(policy);
}

/** Original range-filtered broadcasts now feed actual unit world-step targeting. */
static void ScreamBroadcastB13(Checks& check)
{
	B8BehaviourForeignWorld f(check); if (!f.world) return;
	auto db = B8BehaviourCreatureDb(f.world, NDb::FACTION_FREEZE);
	auto* p = const_cast<NDb::UnitTargetingParameters*>(db->targetingParams.GetPtr());
	p->chaseRange = 50; p->targetingRange = 20;
	CObj<B8BehaviourCreep> source = new B8BehaviourCreep(f.world,*db,NDb::FACTION_FREEZE);
	auto listener = CommonCreepB11(f.world);
	listener->OverrideTargetingParams(p);
	CObj<PFBaseBehaviour> broadcast = new PFBaseBehaviour(source.GetPtr()); source->AddBehaviourOnTop(broadcast);
	auto target = WeightedUnitCreateB12(f.world,p,NDb::FACTION_BURN);
	target->Place(CVec3(35,5,0));
	check(source->GetScreamRange() > 0 && listener->GetChaseRange() == 50, "broadcast fixture has authored nonzero delivery and pursuit ranges");
	source->DoScream(target.GetPtr(),ScreamTarget::ScreamAlert);
	check(!listener->FindTarget(20,true), "ordinary alert broadcast retains original recipient targeting-range filter");
	source->DoScream(target.GetPtr(),ScreamTarget::ScreamForHelp);
	check(listener->FindTarget(20,true) == target.GetPtr(), "original help broadcast reaches a nearby ally for an out-of-scan target");
	// Selection can accept fog-hidden help, but the attack order still requires current sight.
	const int observer = f.world->GetFogOfWar()->AddObject(f.world->GetTileMap()->GetTile(35.f,5.f),NDb::FACTION_FREEZE,5);
	f.world->GetFogOfWar()->StepVisibility(0);
	check(target->IsVisibleForFaction(NDb::FACTION_FREEZE), "broadcast combat fixture reveals its target through real war fog");
	source->DoScream(target.GetPtr(),ScreamTarget::ScreamForHelp);
	f.world->Step(.1f,.1f);
	const auto* state = B8BehaviourAttack(*listener);
	check(state && state->GetTarget() == target.GetPtr(),
		"real unit world tick consumes broadcast help and assigns its authored target");
	const CVec2 before = listener->GetPosition().AsVec2D();
	f.world->Step(.1f,.1f);
	check(listener->GetCurrentTarget() == target.GetPtr() && fabs2(listener->GetPosition().AsVec2D()-before) > 0,
		"broadcast-created attack order advances real chase movement on the next world tick");
	listener->DropStates(); source->DropTarget();
	source->RemoveBehaviour(broadcast);
	f.world->GetFogOfWar()->RemoveObject(observer);
}

static void ScreamTargetsB13(Checks& check)
{
	ScreamSelectionB13(check);
	ScreamInvalidB13(check);
	ScreamCallbacksB13(check);
	ScreamBroadcastB13(check);
	B8BehaviourForeignWorld f(check); if (!f.world) return;
	auto params = WeightedParamsB12();
	auto owner = WeightedUnitCreateB12(f.world,params);
	auto master = WeightedUnitCreateB12(f.world,params,NDb::FACTION_BURN);
	auto alpha = WeightedUnitCreateB12(f.world,params,NDb::FACTION_BURN,NDb::UNITTYPE_SUMMON);
	master->Place(CVec3(35,5,0)); alpha->Place(CVec3(36,5,0));
	alpha->SetMaster(master.GetPtr()); master->SetAlphaSummon(alpha.GetPtr());
	for (bool excluded : {false,true})
	{
		if (excluded) alpha->AddFlag(NDb::UNITFLAG_FORBIDAUTOTARGETME);
		owner->OnScream(master.GetPtr(),ScreamTarget::ScreamForHelp);
		owner->OnScream(alpha.GetPtr(),ScreamTarget::ScreamForHelp);
		check(owner->FindTarget(20,true) == (excluded ? master.GetPtr() : alpha.GetPtr()),
			"explicit help candidates retain eligible alpha preference without reviving excluded candidates");
	}
}
