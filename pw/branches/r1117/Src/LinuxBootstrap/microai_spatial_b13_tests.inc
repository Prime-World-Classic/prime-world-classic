/** Real world, voxel rings and ability factory; only authored data and unit placement are synthetic. */
struct MicroSpatialB13
{
	B8BehaviourForeignWorld fixture;
	NDb::Ptr<NDb::UnitTargetingParameters> params = WeightedParamsB12();
	NDb::Ptr<NDb::BasicMicroAI> microDb;
	NDb::Ptr<NDb::Ability> db;
	CObj<WeightedUnitB12> owner, enemy;
	CObj<PFAbilityData> ability;
	explicit MicroSpatialB13(Checks& check) : fixture(check)
	{
		if (!fixture.world) return;
		owner = WeightedUnitCreateB12(fixture.world, params);
		enemy = WeightedUnitCreateB12(fixture.world, params, NDb::FACTION_BURN);
		owner->AddBehaviourOnTop(new MicroBehaviourB12);
		auto* m = new NDb::BasicMicroAI; m->condition.sString = "true"; microDb = m;
		auto* a = new NDb::Ability; a->microAI = m; a->type = NDb::ABILITYTYPE_ACTIVE;
		a->targetType = NDb::ESpellTarget(NDb::SPELLTARGET_ENEMY | NDb::SPELLTARGET_CREEP);
		a->useRange.sString = "10"; a->manaCost.sString = "7"; db = a;
		ability = new PFAbilityData(owner.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL);
	}
	NDb::Ability& Data() { return *const_cast<NDb::Ability*>(db.GetPtr()); }
	bool Finds(PFBaseUnit* expected)
	{
		Target result;
		return ability->FindMicroAITarget(result) && result.IsUnit() && result.GetUnit() == expected;
	}
};

/** Windows basic targeting uses positive authored range and inclusive planar center distance. */
static void MicroSpatialRangeB13(Checks& check)
{
	MicroSpatialB13 f(check); if (!f.ability) return;
	check(f.Finds(f.enemy), "basic MicroAI finds an indexed enemy in its own world");
	for (const char* expression : {"0", "-1", "1/0", "1e39", "1e38", "unknown(3)"})
	{
		f.Data().useRange.sString = expression;
		Target result(f.owner);
		check(!f.ability->FindMicroAITarget(result) && result.GetUnit() == f.owner,
			"invalid or nonpositive authored range cannot become a visibility fallback or change output");
	}
	f.Data().useRange.sString = "10";
	f.enemy->Place(CVec3(15, 5, 30));
	check(f.Finds(f.enemy), "original voxel range includes equality and ignores height");
	f.enemy->SetObjectSize(20); f.enemy->Place(CVec3(15.25f, 5, 0));
	Target result;
	check(!f.ability->FindMicroAITarget(result), "large target radius does not extend the original center-based MicroAI range");
	f.enemy->Place(CVec3(14.75f, 5, 0));
	check(f.Finds(f.enemy), "moving back inside the radius updates spatial eligibility");
	f.Data().targetType = NDb::SPELLTARGET_CREEP;
	check(!f.ability->FindMicroAITarget(result), "missing ally/enemy mask is not silently treated as enemy");
	f.Data().targetType = NDb::ESpellTarget(NDb::SPELLTARGET_ALLY | NDb::SPELLTARGET_CREEP);
	check(f.Finds(f.owner), "original allied unit query may select its own eligible owner");
	f.Data().targetType = NDb::ESpellTarget(NDb::SPELLTARGET_ENEMY | NDb::SPELLTARGET_HEROMALE);
	check(!f.ability->FindMicroAITarget(result), "original type predicate rejects a different unit category");
	f.enemy->SetUnitType(NDb::UNITTYPE_HEROMALE);
	check(f.Finds(f.enemy), "live unit-type changes are observed by the original predicate");
	f.owner->Place(CVec3(std::numeric_limits<float>::quiet_NaN(), 5, 0));
	check(!f.ability->FindMicroAITarget(result), "malformed query center is rejected before voxel conversion");
	f.owner->Place(CVec3(5, 5, 0));
	f.Data().targetType = NDb::SPELLTARGET_SELF; f.Data().useRange.sString = "1/0";
	check(f.Finds(f.owner), "self route does not evaluate the unused spatial range");
	check(f.owner->GetMana() == 200 && f.ability->GetActiveInstancesCount() == 0,
		"spatial queries do not spend mana or instantiate casts");
}

/** A foreign world and stale global cache must never substitute for the owning world's spatial index. */
static void MicroSpatialWorldB13(Checks& check)
{
	MicroSpatialB13 f(check); if (!f.ability) return;
	B8BehaviourForeignWorld other(check); if (!other.world) return;
	auto foreign = WeightedUnitCreateB12(other.world, f.params, NDb::FACTION_BURN);
	f.enemy->Place(CVec3(35, 25, 0));
	Target result(f.owner);
	check(!f.ability->FindMicroAITarget(result) && result.GetUnit() == f.owner,
		"same-position enemy in another loaded world is not a candidate");
	f.enemy->Place(CVec3(6, 5, 0));
	check(f.Finds(f.enemy), "local indexed candidate wins independently of the foreign global cache");
	f.fixture.world->GetAIWorld()->UnregisterObjectOrUnit(f.enemy);
	check(!f.ability->FindMicroAITarget(result), "unindexed live units are not recovered from the global cache");
	f.owner->AssignTarget(f.enemy.GetPtr(), false);
	check(!f.ability->FindMicroAITarget(result), "current attack target cannot bypass the original spatial index");
	f.owner->DropTarget();
	f.fixture.world->GetAIWorld()->RegisterUnit(f.enemy);
	check(f.Finds(f.enemy), "restoring the spatial registration restores selection");
	f.fixture.world->UnregisterObject(f.enemy, false); f.fixture.world->ProcessAddRemove();
	check(!f.ability->FindMicroAITarget(result), "stale indexed but world-unregistered unit is rejected");
	f.Data().targetType = NDb::SPELLTARGET_SELF;
	f.fixture.world->UnregisterObject(f.owner, false); f.fixture.world->ProcessAddRemove();
	check(!f.ability->FindMicroAITarget(result), "unregistered owner cannot perform even a self MicroAI query");
}

/** Seed a nonunit object into the original object ring, without inventing a second targeting implementation. */
struct MicroObjectB13 : PFLogicObject
{
	explicit MicroObjectB13(PFWorld* world, const CVec3& pos, NDb::EUnitType type)
		: PFLogicObject(world, pos, nullptr)
	{
		InitData data; data.type = type; data.faction = NDb::FACTION_NEUTRAL;
		Initialize(data); health = 1;
	}
};

/** The original nonunit branch traverses objects, not the unit cache or a combined query. */
static void MicroSpatialObjectsB13(Checks& check)
{
	MicroSpatialB13 f(check); if (!f.ability) return;
	CObj<MicroObjectB13> tree = new MicroObjectB13(f.fixture.world, CVec3(6, 5, 0), NDb::UNITTYPE_TREE);
	f.Data().targetType = NDb::ESpellTarget(NDb::SPELLTARGET_ENEMY | NDb::SPELLTARGET_TREE);
	Target result;
	check(f.ability->FindMicroAITarget(result) && result.IsObject() && result.GetObject() == tree,
		"basic MicroAI reaches eligible nonunit objects through the original object ring");
	f.fixture.world->GetAIWorld()->UnregisterObjectOrUnit(tree);
	check(!f.ability->FindMicroAITarget(result), "removed nonunit object no longer participates in the spatial query");
	f.fixture.world->GetAIWorld()->RegisterObject(tree);
	check(f.ability->FindMicroAITarget(result) && result.GetObject() == tree,
		"object ring restoration is immediately visible to MicroAI");
	f.Data().targetType = NDb::ESpellTarget(NDb::SPELLTARGET_ENEMY | NDb::SPELLTARGET_CREEP | NDb::SPELLTARGET_TREE);
	check(f.Finds(f.enemy), "mixed unit/object masks retain Windows' unit-branch precedence");
}

/** Simulate caller-owned removal while the real MicroAI retains and checks its spatial snapshot. */
struct MicroRemovingConditionB13 : ITargetCondition
{
	PFWorld* world;
	PFBaseUnit* removed;
	mutable int calls = 0;
	MicroRemovingConditionB13(PFWorld* world, PFBaseUnit* removed) : world(world), removed(removed) {}
	bool operator()(const Target&, const PFAbilityData*) const override
	{
		++calls;
		world->UnregisterObject(removed, false); world->ProcessAddRemove();
		return true;
	}
};

static void MicroSpatialCallbacksB13(Checks& check)
{
	for (bool removeOwner : {false, true})
	{
		MicroSpatialB13 f(check); if (!f.ability) continue;
		MicroRemovingConditionB13 condition(f.fixture.world, removeOwner ? f.owner.GetPtr() : f.enemy.GetPtr());
		Target result(f.owner);
		check(!f.ability->FindMicroAITargetTemp(result, condition) && result.GetUnit() == f.owner && condition.calls == 1,
			"caller removal cannot publish a stale selected unit or continue with an unregistered owner");
	}
	MicroSpatialB13 f(check); if (!f.ability) return;
	f.Data().targetType = NDb::SPELLTARGET_SELF;
	MicroCallerConditionB12 condition;
	Target result(f.enemy);
	check(!f.ability->FindMicroAITargetTemp(result, condition) && result.GetUnit() == f.enemy,
		"rejected basic self target preserves caller output too");
	condition.allow = true;
	check(f.ability->FindMicroAITargetTemp(result, condition) && result.GetUnit() == f.owner,
		"accepted basic self target retains the original result");
	MicroRemovingConditionB13 removal(f.fixture.world, f.owner);
	result = Target(f.enemy);
	check(!f.ability->FindMicroAITargetTemp(result, removal) && result.GetUnit() == f.enemy,
		"self-query callback removal also fails without changing caller output");
}
