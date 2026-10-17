/**
 * Non-instant immediate responses retain their sender-relative subscription order.
 * This is a partial-order regression, not total Windows scheduler parity: timed,
 * travel and Mace physics retain their separate existing tests and native ordering.
 * All descriptors below are mocks; CreateDispatch and world/unit stepping are real.
 * Include after dispatch_b11_09_world_tests.inc and moving_world_b12_tests.inc
 * (which follows stationary_world_b12_tests.inc in the existing probe).
 */
namespace dispatch_order_b13
{
/** An explicit deferred-immediate descriptor avoids relying on generated defaults. */
static NDb::Ability* Payload()
{
	auto* raw = new NDb::Ability;
	auto* immediate = new NDb::DispatchImmediate;
	immediate->instantApply = false;
	raw->dispatch = immediate;
	auto* damage = new NDb::DamageApplicator;
	damage->damage.sString = "25";
	damage->damageType = NDb::APPLICATORDAMAGETYPE_PURE;
	raw->applicators.push_back(damage);
	return raw;
}

/** Existing observers record entry into real owner ticks, before production ability updates. */
struct Fixture
{
	B8BehaviourForeignWorld map;
	CObj<PFBaseUnit> sender, receiver;
	CObj<StationaryTickB12> stationary;
	CObj<MovingTickB12> moving;
	std::vector<float> healthAtOwnerTick;

	Fixture(Checks& check, bool movingOwner) : map(check)
	{
		if (!map.world) return;
		if (movingOwner)
		{
			const auto db = B8BehaviourCreatureDb(nullptr, NDb::FACTION_FREEZE);
			moving = new MovingTickB12(map.world, *db);
			sender = moving.GetPtr();
		}
		else
		{
			const auto seed = Unit(nullptr, NDb::FACTION_FREEZE, 40);
			stationary = new StationaryTickB12(map.world, seed->DbUnitDesc());
			sender = stationary.GetPtr();
		}
		receiver = Unit(map.world, NDb::FACTION_BURN, 20);
		sender->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		sender->AddFlag(NDb::UNITFLAG_FORBIDATTACK);
		receiver->AddFlag(NDb::UNITFLAG_FORBIDSELECTTARGET);
		receiver->AddFlag(NDb::UNITFLAG_FORBIDATTACK);
		const auto observe = [this]() { healthAtOwnerTick.push_back(receiver->GetHealth()); };
		if (moving) moving->callback = observe;
		if (stationary) stationary->callback = observe;
	}

	/** World ownership may outlast this fixture's borrowed observation callback. */
	~Fixture()
	{
		if (moving) moving->callback = nullptr;
		if (stationary) stationary->callback = nullptr;
		if (sender) sender->Cleanup();
		if (receiver) receiver->Cleanup();
	}

	int Ticks() const { return moving ? moving->ticks : stationary ? stationary->ticks : 0; }
};

/** Inspect the actual step ring, not object IDs or IsSubscribed's pending-ring membership. */
static bool CheckRing(Checks& check, Fixture& f, PFDispatch* dispatch, bool beforeOwner)
{
	int index = 0, ownerIndex = -1, dispatchIndex = -1;
	auto collect = [&](PF_Core::WorldObjectBase& object) {
		if (&object == f.sender.GetPtr()) ownerIndex = index;
		if (&object == dispatch) dispatchIndex = index;
		++index;
	};
	f.map.world->ForAllObjectsBeingStepped(collect);
	const bool ordered = ownerIndex >= 0 && dispatchIndex >= 0
		&& (dispatchIndex < ownerIndex) == beforeOwner;
	check(ordered, beforeOwner
		? "B13 deferred immediate subscribes before its sender in the actual step ring"
		: "B13 ordinary deferred immediate subscribes after its sender in the actual step ring");
	check(dispatch && dispatch->GetObjectId() > f.sender->GetObjectId(),
		"B13 later dispatch creation IDs do not define sender-relative subscription order");
	return ordered;
}

/** Damage callbacks must observe the AI clock update that precedes original object stepping. */
struct PhaseObserver : CObjectBase, PFBaseUnitEventListener
{
	OBJECT_BASIC_METHODS(PhaseObserver);
public:
	CPtr<PFWorld> world;
	float battleDelay = -1;
	int calls = 0;
	unsigned int OnEvent(const PFBaseUnitEvent* event) override
	{
		if (dynamic_cast<const PFOutgoingDamageEvent*>(event))
		{
			++calls;
			battleDelay = world->GetAIWorld()->GetBattleStartDelay();
		}
		return 0;
	}
};

/** Detached responses obey the ring; ordinary ability holders remain owned by the unit tick. */
static void Delivery(Checks& check, bool movingOwner, bool beforeOwner, bool unitOwned)
{
	Fixture f(check, movingOwner);
	if (!f.map.world) return;
	PFWorld* world = f.map.world;
	if (!beforeOwner) world->ProcessAddRemove();
	NDb::Ptr<NDb::Ability> db = Payload();
	CObj<PFAbilityData> data = new PFAbilityData(f.sender.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL);
	CObj<PFAbilityInstance> cast;
	CObj<PFDispatch> dispatch;
	if (unitOwned)
	{
		cast = f.sender->CreateAbilityInstance(data, Target(f.receiver.GetPtr()));
		if (cast) dispatch = B11FindDispatch(world, cast.GetPtr());
	}
	else
	{
		cast = new PFAbilityInstance(data, Target(f.receiver.GetPtr()), false);
		dispatch = CreateDispatch(cast, nullptr, Target(f.sender.GetPtr()), Target(f.receiver.GetPtr()),
			db.GetPtr(), 0, true, 0);
	}
	const bool ready = dispatch && dynamic_cast<PFDispatchImmediate*>(dispatch.GetPtr())
		&& dispatch->Started() && !dispatch->IsObjectDead() && cast
		&& cast->OwnsNativeDispatch(dispatch.GetPtr()) == unitOwned;
	check(ready, "B13 ordering fixture creates the real deferred immediate with the requested holder ownership");
	if (!ready) return;
	world->ProcessAddRemove();
	if (!CheckRing(check, f, dispatch.GetPtr(), beforeOwner)) return;
	NDb::Ptr<NDb::AdvMapSettings> settings = new NDb::AdvMapSettings;
	const_cast<NDb::AdvMapSettings*>(settings.GetPtr())->battleStartDelay = 2;
	world->GetAIWorld()->SetMapData(nullptr, settings);
	CObj<PhaseObserver> observer = new PhaseObserver;
	observer->world = world;
	f.sender->AddEventListener(observer.GetPtr());
	world->Step(0, 0);
	check(f.Ticks() == 0 && f.healthAtOwnerTick.empty() && f.receiver->GetHealth() == 1000
		&& !dispatch->IsObjectDead(), "B13 registration and zero ticks cannot deliver a deferred immediate response");
	world->Step(.5f, .5f);
	const float seen = beforeOwner && !unitOwned ? 975.f : 1000.f;
	check(f.Ticks() == 1 && f.healthAtOwnerTick.size() == 1 && f.healthAtOwnerTick[0] == seen,
		unitOwned ? "B13 unit-owned immediate is still pending at owner entry despite its ring position"
		: beforeOwner ? "B13 startup immediate response is visible before the sender's first real tick"
		: "B13 post-owner immediate remains pending until after the sender's first real tick");
	check(dispatch->IsObjectDead() && f.receiver->GetHealth() == 975,
		"B13 eligible immediate delivers exactly one payload during its first positive world tick");
	check(observer->calls == 1 && observer->battleDelay == 1.5f,
		"B13 immediate payload callbacks observe the AI clock update before either dispatch phase");
	world->Step(.5f, .5f);
	check(f.Ticks() == 2 && f.healthAtOwnerTick.size() == 2 && f.healthAtOwnerTick[1] == 975
		&& f.receiver->GetHealth() == 975 && dispatch->IsObjectDead(),
		"B13 completed immediate neither replays damage nor double-steps its moving or stationary owner");
	f.sender->RemoveEventListener(observer.GetPtr());
}

/** A real damage callback cancels a post-owner sibling and creates a new deferred immediate. */
static void Mutation(Checks& check, bool movingOwner, bool beforeOwner)
{
	Fixture f(check, movingOwner);
	if (!f.map.world) return;
	PFWorld* world = f.map.world;
	if (!beforeOwner) world->ProcessAddRemove();
	NDb::Ptr<NDb::Ability> db = Payload();
	CObj<PFAbilityData> data = new PFAbilityData(f.sender.GetPtr(), db, NDb::ABILITYTYPEID_SPECIAL);
	CObj<PFAbilityInstance> first = new PFAbilityInstance(data, Target(f.receiver.GetPtr()), false);
	CObj<PFAbilityInstance> second = new PFAbilityInstance(data, Target(f.receiver.GetPtr()), false);
	CObj<B11WorldDispatchMutation> events = new B11WorldDispatchMutation;
	events->sender = f.sender;
	events->receiver = f.receiver;
	events->childSpell = db;
	events->childAbility = new PFAbilityInstance(data, Target(f.receiver.GetPtr()), false);
	events->first = CreateDispatch(first, nullptr, Target(f.sender.GetPtr()), Target(f.receiver.GetPtr()), db.GetPtr(), 0, true, 0);
	// The sibling is deliberately post-owner even when the triggering response precedes the owner.
	world->ProcessAddRemove();
	events->second = CreateDispatch(second, nullptr, Target(f.sender.GetPtr()), Target(f.receiver.GetPtr()), db.GetPtr(), 0, true, 0);
	world->ProcessAddRemove();
	check(events->first && events->second, "B13 cross-phase mutation owns two real pending immediate dispatches");
	if (!events->first || !events->second) return;
	if (!CheckRing(check, f, events->first.GetPtr(), beforeOwner)
		|| !CheckRing(check, f, events->second.GetPtr(), false)) return;
	f.sender->AddEventListener(events.GetPtr());
	world->Step(.5f, .5f);
	check(events->fired && events->first->IsObjectDead() && events->second->IsObjectDead()
		&& f.receiver->GetHealth() == 975,
		"B13 immediate callback cancellation suppresses its later sibling across the captured phases");
	check(f.Ticks() == 1 && f.healthAtOwnerTick.size() == 1
		&& f.healthAtOwnerTick[0] == (beforeOwner ? 975.f : 1000.f),
		"B13 callback registration mutations preserve the original response-to-owner partial order");
	check(events->child && dynamic_cast<PFDispatchImmediate*>(events->child.GetPtr())
		&& events->child->Started() && !events->child->IsObjectDead()
		&& !events->childAbility->OwnsNativeDispatch(events->child.GetPtr()),
		"B13 callback-born immediate stays detached and unstepped for its entire birth tick");
	if (events->child) CheckRing(check, f, events->child.GetPtr(), false);
	world->Step(.5f, .5f);
	check(events->child && events->child->IsObjectDead() && f.receiver->GetHealth() == 950
		&& f.Ticks() == 2 && f.healthAtOwnerTick.size() == 2 && f.healthAtOwnerTick[1] == 975,
		"B13 newborn immediate first delivers after its owner on the next world tick, not in the pre-owner phase");
	world->Step(.5f, .5f);
	check(f.receiver->GetHealth() == 950 && f.Ticks() == 3 && f.healthAtOwnerTick.size() == 3
		&& f.healthAtOwnerTick[2] == 950,
		"B13 cancelled siblings and completed newborns cannot replay damage or duplicate unit ticks");
	f.sender->RemoveEventListener(events.GetPtr());
}
}

/** Run after the existing B11 mixed/lifecycle/mutation cases; no private holder mutation is used. */
static void DispatchOrderB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	for (bool movingOwner : {false, true})
	for (bool beforeOwner : {false, true})
	{
		for (bool unitOwned : {false, true})
			dispatch_order_b13::Delivery(check, movingOwner, beforeOwner, unitOwned);
		dispatch_order_b13::Mutation(check, movingOwner, beforeOwner);
	}
	std::printf("B13 immediate dispatch partial order: checks=%d failures=%d scope=sender-relative-immediate-only\n",
		check.count - before, check.failed - failed);
}
