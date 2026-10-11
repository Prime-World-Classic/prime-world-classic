#include "../PF_GameLogic/PFBaseUnitEvent.h"

/** Record only engine event delivery; do not implement cancellation or casting. */
struct B9InteractionEvents : CObjectBase, PFBaseUnitEventListener
{
	OBJECT_BASIC_METHODS(B9InteractionEvents);
public:
	int cancelled = 0, started = 0;
	const PFAbilityInstance* lastStarted = nullptr;
	unsigned int OnEvent(const PFBaseUnitEvent* event) override
	{
		if (event->GetType() == NDb::BASEUNITEVENT_CHANNELINGCANCELED) ++cancelled;
		if (event->GetType() == NDb::BASEUNITEVENT_ABILITYSTART)
		{
			++started; lastStarted = event->GetAbility();
		}
		return 0;
	}
};

/** Inject an actual ability instance only, keeping its activation and state timing engine-owned. */
struct B9InteractionAction : B9Interaction
{
	B9InteractionAction(PFBaseHero* hero, const Target& target)
		: B9Interaction(hero, target, 2) {}
	void Instance(PFAbilityInstance* instance) { pActionInstance = instance; }
	bool Interruptible() const { return CanBeInterrupted(); }
};

static void HeroInteractionActionB9(Checks& check, PFWorld* world)
{
	for (bool cancelBeforeActivation : {false, true})
	{
		CObj<B8BehaviourHero> hero = B8BehaviourHeroUnit(world, NDb::FACTION_FREEZE);
		NDb::Ability* raw = new NDb::Ability; raw->manaCost.sString = "0";
		CObj<PFAbilityData> data = new PFAbilityData(hero.GetPtr(), raw, NDb::ABILITYTYPEID_SPECIAL, false, false);
		CObj<PFAbilityInstance> instance = new PFAbilityInstance(data, Target(CVec2(5, 5)), false);
		CObj<B9InteractionEvents> events = new B9InteractionEvents;
		hero->AddEventListener(events.GetPtr());
		CObj<B9InteractionAction> interaction = new B9InteractionAction(hero.GetPtr(), Target(CVec2(5, 5)));
		interaction->Instance(instance.GetPtr());
		CPtr<IPFState> order = interaction.GetPtr();
		check(!order->DoStep(0) && interaction->actions == 1, "interaction admits its action once");
		check(!order->DoStep(.25f) && !instance->IsActivated() && interaction->Interruptible(),
			"interaction waits for engine activation without updating the instance twice");
		if (!cancelBeforeActivation)
		{
			hero->SetChannellingProgress(.5f); instance->Update(.25f);
			check(instance->IsActivated() && !order->DoStep(0),
				"activated interaction stays live while the owner is channeling");
			hero->SetChannellingProgress(0);
			check(order->DoStep(0) && !interaction->Interruptible(),
				"interaction completes after activation and channeling with Windows interruption policy");
		}
		order->DoLeave(true);
		check(events->cancelled == 1 && interaction->actions == 1,
			"interaction leave notifies engine channel cancellation once without replaying action");
		hero->RemoveEventListener(events.GetPtr());
	}
	CObj<B8BehaviourHero> hero = B8BehaviourHeroUnit(world, NDb::FACTION_FREEZE);
	CObj<B9InteractionEvents> events = new B9InteractionEvents;
	hero->AddEventListener(events.GetPtr());
	CObj<IPFState> empty = new PFInteractObjectState(hero.GetPtr(), Target(CVec2(5, 5)));
	empty->DoStep(0); empty->DoLeave(true);
	check(events->cancelled == 0, "interaction without an ability instance emits no cancellation event");
	hero->RemoveEventListener(events.GetPtr());
}
