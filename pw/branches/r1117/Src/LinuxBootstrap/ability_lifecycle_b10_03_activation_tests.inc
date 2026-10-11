#include "../PF_GameLogic/PFBaseUnitEvent.h"

/** Observe engine cast events without replacing dispatch, payment, or applicators. */
struct B10AbilityEvents : CObjectBase, PFBaseUnitEventListener
{
	OBJECT_BASIC_METHODS(B10AbilityEvents);
public:
	int casts = 0;
	const PFAbilityInstance* last = nullptr;
	unsigned int OnEvent(const PFBaseUnitEvent* event) override
	{
		if (event->GetType() == NDb::BASEUNITEVENT_CASTMAGIC)
		{
			++casts;
			last = event->GetAbility();
		}
		return 0;
	}
};

/** One instance owns one dispatch and one activation transaction. */
static void AbilityActivationOnceB10(Checks& check, PFWorld* world)
{
	CObj<PFBaseUnit> sender = Unit(world, NDb::FACTION_FREEZE, 40);
	CObj<PFBaseUnit> receiver = Unit(world, NDb::FACTION_BURN, 20);
	NDb::Ability* raw = new NDb::Ability;
	raw->manaCost.sString = "70";
	raw->cooldownTime.sString = "5";
	NDb::DamageApplicator* damage = new NDb::DamageApplicator;
	damage->damage.sString = "25";
	damage->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
	raw->applicators.push_back(damage);
	NDb::Ptr<NDb::Ability> db = raw;
	CObj<PFAbilityData> data = new PFAbilityData(sender.GetPtr(), db, NDb::ABILITYTYPEID_ABILITY1);
	CObj<B10AbilityEvents> events = new B10AbilityEvents;
	sender->AddEventListener(events.GetPtr());
	CObj<PFAbilityInstance> cast = sender->CreateAbilityInstance(data, Target(receiver.GetPtr()));
	check(cast && receiver->GetHealth() == 975 && sender->GetMana() == 130,
		"immediate activation applies real damage and pays once");
	check(cast && events->casts == 1 && events->last == cast.GetPtr() && data->GetCurrentCooldown() == 5,
		"activation emits the matching cast event and starts authored cooldown");
	check(cast && !cast->ApplyToTarget(), "an activated instance cannot create a second dispatch");
	check(receiver->GetHealth() == 975 && sender->GetMana() == 130 && events->casts == 1,
		"repeated admission cannot duplicate effects, payment, or cast events");
	if (cast) { cast->Update(0); cast->Update(1); }
	check(events->casts == 1 && receiver->GetHealth() == 975,
		"later instance updates cannot activate the same cast again");
	CObj<PFAbilityInstance> passive = new PFAbilityInstance(data, Target(sender.GetPtr()), true);
	check(!passive->ApplyToTarget() && sender->GetMana() == 130,
		"passive instance cannot be submitted as paid active work");
	sender->RemoveEventListener(events.GetPtr());
}
