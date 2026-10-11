/** Expose authored inputs and owned instances while keeping base-use policy engine-owned. */
struct B9UseState : PFBaseUseState
{
	NDb::Ptr<NDb::Ability> db;
	float useRange = 4;
	B9UseState(PFBaseUnit* owner, const Target& target, const NDb::Ability* descriptor)
		: PFBaseUseState(owner, target), db(descriptor) {}
	const NDb::Ability* GetDBDesc() const override { return db; }
	float GetUseRange() const override { return useRange; }
	void Alternative(const NDb::AlternativeTarget* alternative) { target.SetDBAlternativeTarget(alternative); }
	void Instance(PFAbilityInstance* instance) { pAbilityInstance = instance; }
	PFAbilityInstance* Instance() const { return pAbilityInstance; }
};

/** Base-use state resolves authored/alternative flags and distinguishes self, point and unit range. */
static void BaseUseRangeB9(Checks& check, PFWorld* world)
{
	CObj<PFBaseUnit> owner = Unit(world, NDb::FACTION_FREEZE, 20);
	NDb::Ability* raw = new NDb::Ability;
	raw->flags = NDb::ABILITYFLAGS_MOMENTARY;
	NDb::Ptr<NDb::Ability> db = raw;
	Target point(CVec3(15, 5, 0));
	CObj<B9UseState> state = new B9UseState(owner.GetPtr(), point, db);
	check(state->GetAbilityFlags() == NDb::ABILITYFLAGS_MOMENTARY && !state->IsEjecting(),
		"momentary base-use state keeps authored queue retention");
	check(state->NeedToGetClose(), "distant point requires approach before cast");
	state->useRange = 12;
	check(!state->NeedToGetClose(), "in-range point does not require approach");
	state->useRange = 0;
	check(!state->NeedToGetClose(), "finite zero range preserves no-range-limit semantics");
	state->useRange = std::numeric_limits<float>::quiet_NaN();
	check(state->NeedToGetClose(), "malformed use range cannot admit a distant cast");
	state->useRange = 4;
	raw->flags = NDb::ABILITYFLAGS_CANUSEOUTOFRANGE;
	check(!state->NeedToGetClose() && state->IsEjecting(), "out-of-range flag bypasses approach but not queue ejection");
	NDb::AlternativeActivity* activity = new NDb::AlternativeActivity;
	activity->flags = NDb::ABILITYFLAGS_MOMENTARY;
	NDb::AlternativeTarget* alternative = new NDb::AlternativeTarget;
	alternative->alternativeActivity = activity;
	NDb::Ptr<NDb::AlternativeTarget> alt = alternative;
	state->Alternative(alt);
	check(state->GetAbilityFlags() == NDb::ABILITYFLAGS_MOMENTARY && state->NeedToGetClose(),
		"alternative activity flags take precedence over base ability");
	state->Alternative(nullptr); raw->flags = static_cast<NDb::EAbilityFlags>(0);
	CObj<B9UseState> self = new B9UseState(owner.GetPtr(), Target(owner.GetPtr()), db);
	check(!self->NeedToGetClose(), "self cast never requires approach");
	CObj<B9UseState> missing = new B9UseState(nullptr, point, nullptr);
	check(missing->GetAbilityFlags() == 0 && !missing->NeedToGetClose(), "missing owner/descriptor are safe metadata queries");
}

/** Mock only the authored work duration; activation, applicators and cancellation stay real. */
struct B9TimedAbility : PFAbilityData
{
	B9TimedAbility(PFBaseUnit* owner, const NDb::Ability* db)
		: PFAbilityData(owner, db, NDb::ABILITYTYPEID_SPECIAL) {}
	float GetWorkTime() const override { return 3; }
};

/** Cast states observe simulation-owned instances and retain completion/cancel boundaries. */
static void BaseUseLifetimeB9(Checks& check, PFWorld* world)
{
	CObj<PFBaseUnit> owner = Unit(world, NDb::FACTION_FREEZE, 20);
	NDb::Ability* raw = new NDb::Ability;
	raw->flags = static_cast<NDb::EAbilityFlags>(0); raw->isUnitFreeAfterCast = false;
	raw->useRange.sString = "0"; raw->manaCost.sString = "0";
	NDb::Ptr<NDb::Ability> db = raw;
	Target self(owner.GetPtr());
	CObj<B9UseState> state = new B9UseState(owner.GetPtr(), self, db);
	check(!state->IsCastFinished() && !state->IsAbilityFinished() && !state->IsBlocking(),
		"unstarted cast is unfinished without blocking queued orders");
	CObj<PFAbilityData> data = new B9TimedAbility(owner.GetPtr(), db);
	CObj<PFAbilityInstance> instance = new PFAbilityInstance(data, self, false);
	state->Instance(instance);
	check(!state->IsCastFinished() && !state->IsAbilityFinished() && state->IsBlocking(),
		"live unfinished cast blocks through the actual engine instance");
	instance->Update(1); instance->NotifyApplicatorStarted(owner.GetPtr());
	check(!state->IsCastFinished() && state->IsBlocking(), "cast work duration is not bypassed by activation");
	instance->Update(2);
	check(state->IsCastFinished() && !state->IsAbilityFinished() && state->IsBlocking(),
		"nonfree-after-cast waits for active applicators after cast work ends");
	raw->isUnitFreeAfterCast = true;
	check(!state->IsBlocking() && !state->IsAbilityFinished(), "free-after-cast releases orders without claiming effect completion");
	owner->SetChannellingProgress(0.5f);
	check(!state->IsCastFinished() && !state->IsAbilityFinished(), "channeling delays state completion after cast work");
	owner->SetChannellingProgress(0);
	instance->NotifyApplicatorStopped(owner.GetPtr());
	check(state->IsAbilityFinished(), "last real applicator completion releases the use state");
	CObj<PFAbilityInstance> pending = new PFAbilityInstance(data, self, false);
	CObj<SchedulingState> approach = new SchedulingState(owner.GetPtr());
	state->PushState(approach.GetPtr()); state->FSMStep(0);
	state->Instance(pending); owner->SetChannellingProgress(0.5f); state->OnLeave();
	check(approach->leaves == 1 && state->GetStackSize() == 0, "use-state leave releases a still-owned nested approach");
	check(pending->IsCastFinished() && !owner->IsInChannelling(), "state leave cancels unfinished cast and channeling");
	check(state->Instance() == pending.GetPtr(), "leave retains completed-instance metadata for momentary after-leave policy");
	state->OnLeave();
	check(pending->IsCastFinished(), "repeated leave is safe and does not reactivate the cast");
}
