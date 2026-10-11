/** Observe the real nested follow state without overriding its production state transitions. */
struct B11StateWorldFollow : PFHeroFollowUnitState
{
	B11StateWorldFollow(PFBaseHero* hero, PFBaseUnit* leader) : PFHeroFollowUnitState(hero, leader, 2, 0) {}
	const IPFState* Child() const { return GetCurrentState(); }
};

/** All five state bases remain weak, non-stepped world objects, not independently scheduled units. */
template<class State>
static void B11CheckStateWorld(Checks& check, State* state, PFWorld* world)
{
	check(state && state->GetWorld() == world, "B11 state retains its owner's exact world");
	check(state && !state->IsRealWorldObject() && !state->NeedToBeStepped(),
		"B11 state world retention does not add a general-world simulation owner");
	check(state && (!world || world->GetObjectById(state->GetObjectId()) == state),
		"B11 owned state is discoverable through the actual weak world registry");
}

/** Call after the existing B8 fixtures; owners, registration and nested transitions are actual engine objects. */
static void StateWorldB11(Checks& check, PFWorld* world)
{
	CObj<PFBaseUnit> unit = Unit(world, NDb::FACTION_FREEZE, 20);
	CObj<B8BehaviourCreep> moving = B8BehaviourMoving(world, NDb::FACTION_FREEZE);
	CObj<B8BehaviourHero> hero = B8BehaviourHeroUnit(world, NDb::FACTION_FREEZE);
	NDb::MainBuilding* mainDb = new NDb::MainBuilding;
	mainDb->stats = unit->DbUnitDesc()->stats;
	mainDb->faction = NDb::FACTION_FREEZE;
	NDb::AdvMapObject mainObject;
	mainObject.gameObject = mainDb;
	CObj<PFMainBuilding> building = new PFMainBuilding(world, mainObject);
	NDb::BasePet* petRaw = new NDb::BasePet;
	petRaw->stats = unit->DbUnitDesc()->stats;
	NDb::Ptr<NDb::BasePet> petDb = petRaw;
	CObj<PFBasePetUnit> pet = new PFBasePetUnit(world, *petDb, hero.GetPtr());
	check(unit->GetWorld() == world && hero->GetWorld() == world && building->GetWorld() == world
		&& pet->GetWorld() == world, "B11 state fixture uses real initialized owners in the requested world");
	CObj<PFBaseUnitState> unitState = new PFBaseUnitState(unit.GetPtr());
	CObj<PFBaseMovingUnitState> movingState = new PFBaseMovingUnitState(moving.GetPtr());
	CObj<PFBaseHeroState> heroState = new PFBaseHeroState(hero.GetPtr());
	CObj<PFMainBuildingState> buildingState = new PFMainBuildingState(building.GetPtr());
	CObj<PFBasePetUnitState> petState = new PFBasePetUnitState(pet.GetPtr());
	B11CheckStateWorld(check, unitState.GetPtr(), world);
	B11CheckStateWorld(check, movingState.GetPtr(), world);
	B11CheckStateWorld(check, heroState.GetPtr(), world);
	B11CheckStateWorld(check, buildingState.GetPtr(), world);
	B11CheckStateWorld(check, petState.GetPtr(), world);
	CObj<PFBaseUnitState> nullUnit = new PFBaseUnitState(nullptr);
	CObj<PFBaseMovingUnitState> nullMoving = new PFBaseMovingUnitState(nullptr);
	CObj<PFBaseHeroState> nullHero = new PFBaseHeroState(nullptr);
	CObj<PFMainBuildingState> nullBuilding = new PFMainBuildingState(nullptr);
	CObj<PFBasePetUnitState> nullPet = new PFBasePetUnitState(nullptr);
	B11CheckStateWorld(check, nullUnit.GetPtr(), nullptr);
	B11CheckStateWorld(check, nullMoving.GetPtr(), nullptr);
	B11CheckStateWorld(check, nullHero.GetPtr(), nullptr);
	B11CheckStateWorld(check, nullBuilding.GetPtr(), nullptr);
	B11CheckStateWorld(check, nullPet.GetPtr(), nullptr);
	const int transientId = unitState->GetObjectId();
	unitState = nullptr;
	check(world->GetObjectById(transientId) == nullptr, "B11 releasing a state removes its weak world registration");
	// Inspect states created by production constructors rather than recreating their guard/escort logic.
	const PFMainBuildingState* guard = dynamic_cast<const PFMainBuildingState*>(building->GetCurrentState());
	check(guard && guard->GetWorld() == world, "B11 production main-building guard inherits its owner's world");
	pet->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
	static_cast<PFBaseUnit*>(pet.GetPtr())->Step(0);
	const PFPetAIBaseState* escort = dynamic_cast<const PFPetAIBaseState*>(pet->GetCurrentState());
	check(escort && escort->GetWorld() == world, "B11 production pet escort inherits its owner's world");
	CObj<PFBaseUnitUseAbilityState> use = new PFBaseUnitUseAbilityState(world, unit.GetPtr(), 0, Target(unit.GetPtr()));
	B11CheckStateWorld(check, use.GetPtr(), world);
	B8BehaviourReveal reveal(world, NDb::FACTION_FREEZE);
	CObj<B8BehaviourCreep> leader = B8BehaviourMoving(world, NDb::FACTION_FREEZE);
	CObj<B8BehaviourCreep> enemy = B8BehaviourMoving(world, NDb::FACTION_BURN);
	leader->AssignTarget(enemy.GetPtr(), true);
	CObj<B11StateWorldFollow> follow = new B11StateWorldFollow(hero.GetPtr(), leader.GetPtr());
	CPtr<IPFState> order = follow.GetPtr();
	check(!order->DoStep(0), "B11 real follow state enters its production nested attack path");
	const PFWorldObjectBase* child = dynamic_cast<const PFWorldObjectBase*>(follow->Child());
	check(follow->GetWorld() == world && child && child->GetWorld() == world,
		"B11 hero parent and existing moving-unit child agree on world context");
	order->DoLeave(true);
	check(!follow->Child(), "B11 follow cleanup retains the existing nested-state lifecycle");
}
