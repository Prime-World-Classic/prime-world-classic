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
