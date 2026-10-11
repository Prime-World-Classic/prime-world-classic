// Engine-linked listener fixtures included by applicator_formula_probe.cpp.
#ifndef PW_B7_EVENT_DISPATCH_PROBE_COMMON_INCLUDED
#define PW_B7_EVENT_DISPATCH_PROBE_COMMON_INCLUDED
#include <functional>
#include <initializer_list>
#include <vector>

/** A dispatch-only payload: no hero, damage, death, or applicator execution. */
struct EventDispatchB7Event : PFBaseUnitEvent
{
	explicit EventDispatchB7Event(int tag_ = 1)
		: PFBaseUnitEvent(NDb::BASEUNITEVENT_MOVE), tag(tag_), value(0) {}
	int tag;
	mutable int value;
};

/** Mock callback using the real engine listener interface and ownership macros. */
struct EventDispatchB7Listener : CObjectBase, PFBaseUnitEventListener
{
	OBJECT_BASIC_METHODS(EventDispatchB7Listener);
public:
	int id = 0;
	int calls = 0;
	float priority = 0;
	EListenerType type = LISTENER_DEFAULT;
	std::vector<int>* trace = nullptr;
	int* released = nullptr;
	const PFBaseUnitEvent* observed = nullptr;
	std::function<unsigned int(EventDispatchB7Listener&, const PFBaseUnitEvent*)> action;

	/** Record identity, order, and shared payload before running a mock mutation. */
	unsigned int OnEvent(const PFBaseUnitEvent* event) override
	{
		++calls;
		observed = event;
		const auto* payload = dynamic_cast<const EventDispatchB7Event*>(event);
		if (trace && payload)
		{
			trace->push_back(payload->tag * 100 + id);
			payload->value = payload->value * 10 + id;
		}
		return action ? action(*this, event) : 0;
	}

	float GetListenerPriority() const override { return priority; }
	EListenerType GetListenerType() const override { return type; }

	/** Count logical contents release, not shell destruction/reconstruction. */
	void OnDestroyContents() override
	{
		if (released) ++*released;
	}
};

/** Keep mock data outside listeners so reentrant contents release is observable. */
static CObj<EventDispatchB7Listener> EventDispatchB7Listen(std::vector<int>& trace, int id,
	PFBaseUnitEventListener::EListenerType type = PFBaseUnitEventListener::LISTENER_DEFAULT,
	float priority = 0)
{
	CObj<EventDispatchB7Listener> listener = new EventDispatchB7Listener;
	listener->trace = &trace;
	listener->id = id;
	listener->type = type;
	listener->priority = priority;
	return listener;
}

/** Compare complete traces; counts alone miss priority inversions and double calls. */
static void EventDispatchB7Trace(Checks& check, const std::vector<int>& trace,
	std::initializer_list<int> expected, const char* label)
{
	check(trace == std::vector<int>(expected), label);
	if (trace != std::vector<int>(expected))
	{
		std::printf("B7 observed trace:");
		for (int entry : trace) std::printf(" %d", entry);
		std::printf("\n");
	}
}

#endif

// Chunk 5 tests: paste the shared fixture once, then these functions into the existing probe.
// Call EventOrderingB7Regression(check, world) inside the loaded mock-world branch.

/** Case 1: shield > damage trigger > default, irrespective of cross-type priority. */
static void EventDispatchB7Types(Checks& check, PFWorld* world)
{
	std::vector<int> trace;
	CObj<PFBaseUnit> unit = Unit(world, NDb::FACTION_FREEZE, 40);
	auto a = EventDispatchB7Listen(trace, 1, PFBaseUnitEventListener::LISTENER_DEFAULT, 1000000);
	auto b = EventDispatchB7Listen(trace, 2, PFBaseUnitEventListener::LISTENER_DAMAGETRIGGER, 999);
	auto c = EventDispatchB7Listen(trace, 3, PFBaseUnitEventListener::LISTENER_SHIELD, -999);
	auto d = EventDispatchB7Listen(trace, 4, PFBaseUnitEventListener::LISTENER_DEFAULT,
		std::numeric_limits<float>::infinity());
	for (PFBaseUnitEventListener* listener : {a.GetPtr(), b.GetPtr(), c.GetPtr(), d.GetPtr()})
		unit->AddEventListener(listener);
	EventDispatchB7Event event;
	unit->EventHappened(event);
	EventDispatchB7Trace(check, trace, {103, 102, 101, 104}, "B7.1 Windows type order/default FIFO");
	check(event.value == 3214, "B7.1 ordered listeners mutate one shared event");
	check(a->observed == &event && b->observed == &event && c->observed == &event && d->observed == &event,
		"B7.1 dispatch borrows the original event without copies");
	for (PFBaseUnitEventListener* listener : {a.GetPtr(), b.GetPtr(), c.GetPtr(), d.GetPtr()})
		unit->RemoveEventListener(listener);
	trace.clear();
	for (PFBaseUnitEventListener* listener : {c.GetPtr(), b.GetPtr(), d.GetPtr(), a.GetPtr()})
		unit->AddEventListener(listener);
	unit->EventHappened(EventDispatchB7Event());
	EventDispatchB7Trace(check, trace, {103, 102, 104, 101}, "B7.1 reverse type registration still ignores default priorities");
}

/** Case 2: descending non-default priority, stable ties, registration-time ordering. */
static void EventDispatchB7Priorities(Checks& check, PFWorld* world)
{
	for (auto type : {PFBaseUnitEventListener::LISTENER_SHIELD, PFBaseUnitEventListener::LISTENER_DAMAGETRIGGER})
	{
		std::vector<int> trace;
		CObj<PFBaseUnit> unit = Unit(world, NDb::FACTION_FREEZE, 40);
		auto a = EventDispatchB7Listen(trace, 1, type, -2);
		auto b = EventDispatchB7Listen(trace, 2, type, 7);
		auto c = EventDispatchB7Listen(trace, 3, type, 7);
		auto d = EventDispatchB7Listen(trace, 4, type, 0);
		for (PFBaseUnitEventListener* listener : {a.GetPtr(), b.GetPtr(), c.GetPtr(), d.GetPtr()})
			unit->AddEventListener(listener);
		unit->EventHappened(EventDispatchB7Event());
		EventDispatchB7Trace(check, trace, {102, 103, 104, 101}, "B7.2 higher first/stable equal priorities");
		trace.clear();
		a->priority = 99;
		unit->AddEventListener(a.GetPtr());
		unit->EventHappened(EventDispatchB7Event());
		EventDispatchB7Trace(check, trace, {102, 103, 104, 101}, "B7.2 duplicate does not resort mutable priority");
		trace.clear();
		auto e = EventDispatchB7Listen(trace, 5, type, 8);
		unit->AddEventListener(e.GetPtr());
		unit->EventHappened(EventDispatchB7Event());
		EventDispatchB7Trace(check, trace, {105, 102, 103, 104, 101}, "B7.2 insertion is not a global sort");
	}
	std::vector<int> trace;
	CObj<PFBaseUnit> unit = Unit(world, NDb::FACTION_FREEZE, 40);
	auto a = EventDispatchB7Listen(trace, 1, PFBaseUnitEventListener::LISTENER_SHIELD,
		std::numeric_limits<float>::quiet_NaN());
	auto b = EventDispatchB7Listen(trace, 2, PFBaseUnitEventListener::LISTENER_SHIELD,
		std::numeric_limits<float>::infinity());
	auto c = EventDispatchB7Listen(trace, 3, PFBaseUnitEventListener::LISTENER_SHIELD,
		-std::numeric_limits<float>::infinity());
	unit->AddEventListener(a.GetPtr()); unit->AddEventListener(c.GetPtr()); unit->AddEventListener(b.GetPtr());
	unit->EventHappened(EventDispatchB7Event());
	EventDispatchB7Trace(check, trace, {101, 102, 103}, "B7.2 exact Windows strict greater-than including NaN");
}

/** Case 3: identity deduplication, no-op removal, Linux null/invalid guards. */
static void EventDispatchB7Registration(Checks& check, PFWorld* world)
{
	std::vector<int> trace;
	CObj<PFBaseUnit> unit = Unit(world, NDb::FACTION_FREEZE, 40);
	auto a = EventDispatchB7Listen(trace, 1);
	auto absent = EventDispatchB7Listen(trace, 2);
	unit->AddEventListener(a.GetPtr()); unit->AddEventListener(a.GetPtr());
	unit->RemoveEventListener(absent.GetPtr());
	unit->RemoveEventListener(CPtr<PFBaseUnitEventListener>());
	unit->EventHappened(EventDispatchB7Event());
	EventDispatchB7Trace(check, trace, {101}, "B7.3 repeated registration calls once");
	unit->RemoveEventListener(a.GetPtr()); unit->RemoveEventListener(a.GetPtr());
	trace.clear(); unit->EventHappened(EventDispatchB7Event());
	check(trace.empty(), "B7.3 repeated removal and empty dispatch are no-ops");
	// Windows AddEventListener has no null/invalid guard; do not invoke that UB in the oracle run.
#ifndef PW_B7_WINDOWS_ORACLE
	unit->AddEventListener(CPtr<PFBaseUnitEventListener>());
	CPtr<PFBaseUnitEventListener> weak = absent.GetPtr();
	absent = nullptr;
	check(!IsValid(weak), "B7.3 real CPtr shell becomes invalid when the final CObj drops");
	unit->AddEventListener(weak); unit->RemoveEventListener(weak);
	unit->EventHappened(EventDispatchB7Event());
	check(trace.empty(), "B7.3 invalid and null listeners are not registered");
#endif
}

/** Case 4: only bit 1 removes; pre-call liveness matches Windows. */
static void EventDispatchB7Flags(Checks& check, PFWorld* world)
{
	std::vector<int> trace;
	CObj<PFBaseUnit> unit = Unit(world, NDb::FACTION_FREEZE, 40);
	auto a = EventDispatchB7Listen(trace, 1);
	auto b = EventDispatchB7Listen(trace, 2);
	auto c = EventDispatchB7Listen(trace, 3);
	a->action = [](EventDispatchB7Listener&, const PFBaseUnitEvent*) { return 1u; };
	b->action = [](EventDispatchB7Listener&, const PFBaseUnitEvent*) { return 2u | 8u; };
	c->SetAlive(false);
	unit->AddEventListener(a.GetPtr()); unit->AddEventListener(b.GetPtr()); unit->AddEventListener(c.GetPtr());
	unit->EventHappened(EventDispatchB7Event()); unit->EventHappened(EventDispatchB7Event(2));
	EventDispatchB7Trace(check, trace, {101, 102, 201}, "B7.4 dead skipped/FLAGS_REMOVE only/remaining order");
	check(c->calls == 0, "B7.4 dead-before-call listener is pruned without callback");
	c->SetAlive(true); unit->EventHappened(EventDispatchB7Event(3));
	check(c->calls == 0, "B7.4 pruned listener must register again after revival");
	unit->AddEventListener(c.GetPtr()); unit->EventHappened(EventDispatchB7Event(4));
	check(c->calls == 1, "B7.4 live listener can explicitly register after pruning");
}

/** Chunk 5 entry: ordering and registration contracts need no reentrant dispatcher yet. */
static void EventOrderingB7Regression(Checks& check, PFWorld* world)
{
	EventDispatchB7Types(check, world);
	EventDispatchB7Priorities(check, world);
	EventDispatchB7Registration(check, world);
	EventDispatchB7Flags(check, world);
}
