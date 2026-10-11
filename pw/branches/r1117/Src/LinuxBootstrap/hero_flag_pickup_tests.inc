#include "../PF_GameLogic/PFScriptedFlagpole.h"
#include "../PF_GameLogic/PFPickupable.h"

/** A real pickup object exposes only its authored ability and completion observation. */
struct B9Pickup : PFPickupableObjectBase
{
	NDb::Ptr<NDb::Ability> ability;
	int picked = 0;
	B9Pickup(PFWorld* world, const NDb::GameObject* db, NDb::Ability* ability)
		: PFPickupableObjectBase(world, CVec3(5, 5, 0), db), ability(ability) {}
	const NDb::Ability* GetPickupAbility() const override { return ability.GetPtr(); }
	void OnPickedUp(PFBaseUnit*) override { ++picked; }
};

struct B9PickupOrder : PFHeroPickupObjectState
{
	B9PickupOrder(PFBaseHero* hero, PFPickupableObjectBase* object)
		: PFHeroPickupObjectState(hero, object) {}
	float Distance() const { return GetRange(); }
	PFAbilityInstance* Instance() const { return pActionInstance.GetPtr(); }
};

struct B9FlagOrder : PFCreatureRaiseFlagState
{
	B9FlagOrder(PFBaseHero* hero, PFFlagpole* object)
		: PFCreatureRaiseFlagState(hero, object) {}
	float Distance() const { return GetRange(); }
	PFAbilityInstance* Instance() const { return pActionInstance.GetPtr(); }
};

/** Authored actions delegate mutations to ability applicators instead of instant state shortcuts. */
static void HeroFlagPickupB9(Checks& check, PFWorld* world, NDb::AILogicParameters* ai)
{
	const NDb::Ptr<NDb::Ability> savedRaise = ai->raiseFlagAbility;
	const float savedPickupRange = ai->pickupItemRange;
	ai->pickupItemRange = 4.f;
	for (bool authored : {false, true})
	for (bool forbidden : {false, true})
	{
		CObj<B8BehaviourHero> hero = B8BehaviourHeroUnit(world, NDb::FACTION_FREEZE);
		NDb::Ability* ability = nullptr;
		if (authored)
		{
			ability = new NDb::Ability;
			ability->useRange.sString = "6+tStrength";
			ability->manaCost.sString = "15";
			ability->dispatch = new NDb::DispatchImmediate;
		}
		NDb::Ptr<NDb::GameObject> db = new NDb::GameObject;
		CObj<B9Pickup> object = new B9Pickup(world, db, ability);
		CObj<B9InteractionEvents> events = new B9InteractionEvents;
		hero->AddEventListener(events.GetPtr());
		if (forbidden) hero->AddFlag(NDb::UNITFLAG_FORBIDINTERACT);
		CObj<B9PickupOrder> pickup = new B9PickupOrder(hero.GetPtr(), object.GetPtr());
		CPtr<IPFState> order = pickup.GetPtr();
		check(pickup->Distance() == (authored ? 6.f : 4.f),
			"pickup evaluates actual object target formula or authored AI fallback range");
		order->DoStep(0);
		check(object->picked == (!authored && !forbidden ? 1 : 0),
			"plain pickup uses engine PickUp while authored or forbidden pickup cannot take an instant shortcut");
		check((pickup->Instance() != nullptr) == (authored && !forbidden) &&
			events->started == (authored && !forbidden ? 1 : 0),
			"authored pickup dispatches its actual ability and start event only when allowed");
		check(hero->GetMana() == (authored && !forbidden ? 185.f : 200.f),
			"authored pickup spends real mana without direct object mutation");
		order->DoLeave(true); hero->RemoveEventListener(events.GetPtr());
	}
	{
		CObj<B8BehaviourHero> hero = B8BehaviourHeroUnit(world, NDb::FACTION_FREEZE);
		NDb::Ability* invalid = new NDb::Ability;
		invalid->useRange.sString = "missing"; invalid->manaCost.sString = "15";
		invalid->dispatch = new NDb::DispatchImmediate;
		NDb::Ptr<NDb::GameObject> db = new NDb::GameObject;
		CObj<B9Pickup> object = new B9Pickup(world, db, invalid);
		CObj<B9PickupOrder> pickup = new B9PickupOrder(hero.GetPtr(), object.GetPtr());
		CPtr<IPFState> order = pickup.GetPtr();
		check(!std::isfinite(pickup->Distance()), "unsupported pickup range remains invalid rather than becoming zero");
		check(order->DoStep(0) && !pickup->Instance() && object->picked == 0 && hero->GetMana() == 200,
			"unsupported pickup formula fails closed without movement, pickup or mana spend");
	}
	for (bool forbidden : {false, true})
	{
		CObj<B8BehaviourHero> hero = B8BehaviourHeroUnit(world, NDb::FACTION_FREEZE);
		NDb::Ability* ability = new NDb::Ability;
		ability->useRange.sString = "7"; ability->manaCost.sString = "12";
		ability->dispatch = new NDb::DispatchImmediate;
		ai->raiseFlagAbility = ability;
		CObj<PFBaseUnit> seed = Unit(world, NDb::FACTION_NEUTRAL, 10);
		NDb::Flagpole* rawFlag = new NDb::Flagpole; rawFlag->stats = seed->DbUnitDesc()->stats;
		NDb::AdvMapObject desc; desc.gameObject = rawFlag;
		desc.offset = CPlacement(CVec3(5, 5, 0), QNULL, CVec3(1, 1, 1));
		CObj<PFScriptedFlagpole> flag = new PFScriptedFlagpole(world, desc);
		CObj<B9InteractionEvents> events = new B9InteractionEvents;
		hero->AddEventListener(events.GetPtr());
		if (forbidden) hero->AddFlag(NDb::UNITFLAG_FORBIDINTERACT);
		CObj<B9FlagOrder> raise = new B9FlagOrder(hero.GetPtr(), flag.GetPtr());
		CPtr<IPFState> order = raise.GetPtr();
		check(raise->Distance() == 7.f, "flag interaction obtains its authored ability range");
		order->DoStep(0);
		check(flag->IsNeutral() && !flag->IsRising(),
			"flag order no longer claims a raise before its authored applicator fires");
		check((raise->Instance() != nullptr) == !forbidden && events->started == (forbidden ? 0 : 1),
			"flag interaction invokes real authored ability and start event when allowed");
		check(hero->GetMana() == (forbidden ? 200.f : 188.f), "flag interaction spends authored mana only when allowed");
		order->DoLeave(true); hero->RemoveEventListener(events.GetPtr());
		ai->raiseFlagAbility = nullptr;
		CObj<B9FlagOrder> absent = new B9FlagOrder(hero.GetPtr(), flag.GetPtr());
		order = absent.GetPtr();
		check(order->DoStep(0) && !absent->Instance() && flag->IsNeutral(),
			"missing authored raise ability cannot fall back to instant flag ownership");
	}
	ai->raiseFlagAbility = savedRaise; ai->pickupItemRange = savedPickupRange;
}
