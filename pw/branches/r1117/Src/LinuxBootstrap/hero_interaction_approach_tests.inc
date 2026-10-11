#include "../PF_GameLogic/RegionPointChecker.h"

/** Count action admissions while movement/range checks run in the real interaction state. */
struct B9Interaction : PFInteractObjectState
{
	int actions = 0;
	B9Interaction(PFBaseHero* hero, const Target& target, float distance)
		: PFInteractObjectState(hero, target) { range = distance; }
	void DoAction() override { ++actions; }
	const IPFState* Child() const { return GetCurrentState(); }
	void Checker(IPointChecking* checker) { pointChecking = checker; }
};

/** A distant object must be approached before an action can execute. */
static void HeroInteractionApproachB9(Checks& check, PFWorld* world)
{
	for (bool unitTarget : {false, true})
	{
		CObj<B8BehaviourHero> hero = B8BehaviourHeroUnit(world, NDb::FACTION_FREEZE);
		hero->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		hero->GetStat(NDb::STAT_VISIBILITYRANGE)->SetCoreValue(12.f);
		hero->OpenWarFog();
		CObj<B8BehaviourCreep> object = B8BehaviourMoving(world, NDb::FACTION_FREEZE);
		object->TeleportTo(CVec2(15, 5), true, false);
		Target target = unitTarget ? Target(object.GetPtr()) : Target(CVec2(15, 5));
		CObj<B9Interaction> interaction = new B9Interaction(hero.GetPtr(), target, 2.f);
		CPtr<IPFState> order = interaction.GetPtr();
		check(!order->DoStep(0) && interaction->actions == 0 && interaction->Child() && hero->IsMoving(),
			"distant interaction creates a production approach state without executing the action");
		hero->TeleportTo(CVec2(15, 5), true, false);
		hero->UpdateWarFog();
		world->GetFogOfWar()->StepVisibility(0);
		check(!order->DoStep(0) && interaction->actions == 1 && !interaction->Child(),
			"interaction executes once after the production approach reaches range");
		check(order->DoStep(0) && interaction->actions == 1, "completed interaction cannot execute twice");
		order->DoLeave(true);
	}
	CObj<B8BehaviourHero> hero = B8BehaviourHeroUnit(world, NDb::FACTION_FREEZE);
	CObj<B8BehaviourCreep> object = B8BehaviourMoving(world, NDb::FACTION_FREEZE);
	object->TeleportTo(CVec2(15, 5), true, false);
	CObj<B9Interaction> cancelled = new B9Interaction(hero.GetPtr(), Target(object.GetPtr()), 2);
	CPtr<IPFState> order = cancelled.GetPtr(); order->DoStep(0);
	order->DoLeave(true);
	check(!cancelled->Child() && !hero->IsMoving() && cancelled->actions == 0,
		"interaction cleanup releases and stops its owned approach without executing");
	CObj<B9Interaction> invalid = new B9Interaction(hero.GetPtr(), Target(CVec2(15, 5)),
		std::numeric_limits<float>::quiet_NaN());
	order = invalid.GetPtr();
	check(order->DoStep(0) && invalid->actions == 0 && !hero->IsMoving(),
		"invalid interaction range does not trigger movement or an action");
	object->TeleportTo(CVec2(20, 5), true, false);
	hero->TeleportTo(CVec2(5, 5), true, false);
	hero->GetStat(NDb::STAT_VISIBILITYRANGE)->SetCoreValue(12.f);
	hero->OpenWarFog();
	CObj<B9Interaction> regional = new B9Interaction(hero.GetPtr(), Target(object.GetPtr()), 0);
	vector<SVector> occupied;
	occupied.push_back(world->GetTileMap()->GetTile(10.f, 5.f));
	CObj<RegionPointChecker> checker = new RegionPointChecker(world, occupied, 1);
	regional->Checker(checker.GetPtr()); order = regional.GetPtr();
	check(!order->DoStep(0) && regional->actions == 0 &&
		dynamic_cast<const PFBaseUnitMoveToBuildingState*>(regional->Child()),
		"occupied-region interaction uses the existing building movement state");
	hero->TeleportTo(CVec2(10, 5), true, false);
	hero->UpdateWarFog(); world->GetFogOfWar()->StepVisibility(0);
	check(!hero->IsTargetInRange(Target(object.GetPtr()), 0), "region mock arrival is outside geometric center range");
	check(checker->IsGoodTile(world->GetTileMap()->GetTile(hero->GetPos())), "region mock arrival is inside the actual region checker");
	check(!order->DoStep(0) && regional->actions == 1 && !regional->Child(),
		"occupied-region arrival admits action outside the object's center radius");
	order->DoLeave(true);
}
