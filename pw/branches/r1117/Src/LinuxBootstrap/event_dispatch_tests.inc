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
// Chunk 6 tests: share the chunk 5 fixture and preserve the existing probe tests.
// Call EventReentrancyB7Regression(check, world) inside the loaded mock-world branch.

/** Case 5: self-unregistering callback stays valid with only list/snapshot ownership. */
static void EventDispatchB7SelfRemoval(Checks& check, PFWorld* world)
{
	int released = 0;
	std::vector<int> trace;
	CObj<PFBaseUnit> unit = Unit(world, NDb::FACTION_FREEZE, 40);
	auto a = EventDispatchB7Listen(trace, 1);
	auto b = EventDispatchB7Listen(trace, 2);
	a->released = &released;
	a->action = [&](EventDispatchB7Listener& self, const PFBaseUnitEvent*) {
		unit->RemoveEventListener(GetAddress(self));
		check(released == 0 && IsValid(static_cast<CObjectBase*>(GetAddress(self))), "B7.5 snapshot owns self after unregister inside callback");
		return static_cast<unsigned int>(PFBaseUnitEventListener::FLAGS_REMOVE);
	};
	CPtr<EventDispatchB7Listener> weak = a.GetPtr();
	unit->AddEventListener(a.GetPtr()); unit->AddEventListener(b.GetPtr());
	a = nullptr;
	check(IsValid(weak), "B7.5 unit CObj registration owns listener without external CObj");
	unit->EventHappened(EventDispatchB7Event());
	EventDispatchB7Trace(check, trace, {101, 102}, "B7.5 self removal does not skip successor");
	check(released == 1 && !IsValid(weak), "B7.5 removed contents released when snapshot returns");
	trace.clear(); unit->EventHappened(EventDispatchB7Event(2));
	EventDispatchB7Trace(check, trace, {202}, "B7.5 no repeated self call");
}

/** Case 6: removal of future, previous, or all listeners never keeps stale iterators. */
static void EventDispatchB7OtherRemoval(Checks& check, PFWorld* world)
{
	int released = 0;
	std::vector<int> trace;
	CObj<PFBaseUnit> unit = Unit(world, NDb::FACTION_FREEZE, 40);
	auto a = EventDispatchB7Listen(trace, 1);
	auto b = EventDispatchB7Listen(trace, 2);
	auto c = EventDispatchB7Listen(trace, 3);
	a->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent*) { unit->RemoveEventListener(b.GetPtr()); return 0u; };
	c->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent*) { unit->RemoveEventListener(a.GetPtr()); return 0u; };
	unit->AddEventListener(a.GetPtr()); unit->AddEventListener(b.GetPtr()); unit->AddEventListener(c.GetPtr());
	unit->EventHappened(EventDispatchB7Event()); unit->EventHappened(EventDispatchB7Event(2));
	EventDispatchB7Trace(check, trace, {101, 103, 203}, "B7.6 removed future skipped/previous safe");
	unit->RemoveEventListener(c.GetPtr()); trace.clear();
	a->action = [&](EventDispatchB7Listener& self, const PFBaseUnitEvent*) {
		unit->RemoveEventListener(GetAddress(self)); unit->RemoveEventListener(b.GetPtr()); unit->RemoveEventListener(c.GetPtr());
		return 0u;
	};
	unit->AddEventListener(a.GetPtr()); unit->AddEventListener(b.GetPtr()); unit->AddEventListener(c.GetPtr());
	unit->EventHappened(EventDispatchB7Event()); unit->EventHappened(EventDispatchB7Event(2));
	EventDispatchB7Trace(check, trace, {101}, "B7.6 callback may empty the registration list");
	trace.clear();
	b->released = &released;
	CPtr<EventDispatchB7Listener> weak = b.GetPtr();
	a->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent*) {
		unit->RemoveEventListener(weak.GetPtr());
		check(released == 0 && IsValid(weak), "B7.6 removed future contents are pinned by outer snapshot");
		return 0u;
	};
	c->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent*) {
		check(released == 0, "B7.6 removed candidate stays owned until entire dispatch returns");
		return 0u;
	};
	unit->AddEventListener(a.GetPtr()); unit->AddEventListener(b.GetPtr()); unit->AddEventListener(c.GetPtr());
	b = nullptr;
	unit->EventHappened(EventDispatchB7Event());
	EventDispatchB7Trace(check, trace, {101, 103}, "B7.6 removed sole-owned future listener is not delivered");
	check(released == 1 && !IsValid(weak), "B7.6 removed future contents release at dispatch exit");
}

/** Case 7: additions before/after the cursor defer to the next event, nested sees live list. */
static void EventDispatchB7Additions(Checks& check, PFWorld* world)
{
	std::vector<int> trace;
	CObj<PFBaseUnit> unit = Unit(world, NDb::FACTION_FREEZE, 40);
	auto a = EventDispatchB7Listen(trace, 1);
	auto b = EventDispatchB7Listen(trace, 2);
	auto c = EventDispatchB7Listen(trace, 3, PFBaseUnitEventListener::LISTENER_SHIELD);
	auto d = EventDispatchB7Listen(trace, 4);
	a->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent* event) {
		if (static_cast<const EventDispatchB7Event*>(event)->tag == 1)
		{
			unit->AddEventListener(c.GetPtr()); unit->AddEventListener(d.GetPtr());
			unit->EventHappened(EventDispatchB7Event(2));
		}
		return 0u;
	};
	unit->AddEventListener(a.GetPtr()); unit->AddEventListener(b.GetPtr());
	unit->EventHappened(EventDispatchB7Event());
	EventDispatchB7Trace(check, trace, {101, 203, 201, 202, 204, 102}, "B7.7 additions absent from outer snapshot/present in nested");
	trace.clear(); unit->EventHappened(EventDispatchB7Event(3));
	EventDispatchB7Trace(check, trace, {303, 301, 302, 304}, "B7.7 next event gets current Windows registration order");
}

/** Case 8: identity-based remove/readd, including FLAGS_REMOVE winning over self readd. */
static void EventDispatchB7Readd(Checks& check, PFWorld* world)
{
	std::vector<int> trace;
	CObj<PFBaseUnit> unit = Unit(world, NDb::FACTION_FREEZE, 40);
	auto a = EventDispatchB7Listen(trace, 1);
	auto b = EventDispatchB7Listen(trace, 2);
	auto c = EventDispatchB7Listen(trace, 3);
	a->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent*) {
		unit->RemoveEventListener(b.GetPtr()); unit->AddEventListener(b.GetPtr()); return 0u;
	};
	unit->AddEventListener(a.GetPtr()); unit->AddEventListener(b.GetPtr()); unit->AddEventListener(c.GetPtr());
	unit->EventHappened(EventDispatchB7Event());
	EventDispatchB7Trace(check, trace, {101, 102, 103}, "B7.8 pending same-object readd remains at outer snapshot position");
	a->action = nullptr; trace.clear(); unit->EventHappened(EventDispatchB7Event(2));
	EventDispatchB7Trace(check, trace, {201, 203, 202}, "B7.8 next event reflects re-registration order");
	for (bool remove : {false, true})
	{
		unit->RemoveEventListener(a.GetPtr()); unit->RemoveEventListener(b.GetPtr()); unit->RemoveEventListener(c.GetPtr());
		trace.clear();
		a->action = [&](EventDispatchB7Listener& self, const PFBaseUnitEvent*) {
			unit->RemoveEventListener(GetAddress(self)); unit->AddEventListener(GetAddress(self));
			return remove ? static_cast<unsigned int>(PFBaseUnitEventListener::FLAGS_REMOVE) : 0u;
		};
		unit->AddEventListener(a.GetPtr()); unit->AddEventListener(b.GetPtr());
		unit->EventHappened(EventDispatchB7Event());
		EventDispatchB7Trace(check, trace, {101, 102}, "B7.8 self readd calls at most once per dispatch");
		a->action = nullptr; trace.clear(); unit->EventHappened(EventDispatchB7Event(2));
		if (remove) EventDispatchB7Trace(check, trace, {202}, "B7.8 return remove flag wins over self readd");
		else EventDispatchB7Trace(check, trace, {202, 201}, "B7.8 zero flag preserves self readd for next event");
	}
}

/** Case 9: nested flags can erase both current and future outer registrations. */
static void EventDispatchB7NestedRemoval(Checks& check, PFWorld* world)
{
	int released = 0;
	std::vector<int> trace;
	CObj<PFBaseUnit> unit = Unit(world, NDb::FACTION_FREEZE, 40);
	auto a = EventDispatchB7Listen(trace, 1);
	auto b = EventDispatchB7Listen(trace, 2);
	auto c = EventDispatchB7Listen(trace, 3);
	a->released = &released;
	a->action = [&](EventDispatchB7Listener& self, const PFBaseUnitEvent* event) {
		if (static_cast<const EventDispatchB7Event*>(event)->tag == 1)
		{
			unit->EventHappened(EventDispatchB7Event(2));
			check(released == 0 && IsValid(static_cast<CObjectBase*>(GetAddress(self))), "B7.9 outer owner pins self after nested unregister");
			return 0u;
		}
		return static_cast<unsigned int>(PFBaseUnitEventListener::FLAGS_REMOVE);
	};
	b->action = [](EventDispatchB7Listener&, const PFBaseUnitEvent* event) {
		return static_cast<const EventDispatchB7Event*>(event)->tag == 2
			? static_cast<unsigned int>(PFBaseUnitEventListener::FLAGS_REMOVE) : 0u;
	};
	CPtr<EventDispatchB7Listener> weak = a.GetPtr();
	unit->AddEventListener(a.GetPtr()); unit->AddEventListener(b.GetPtr()); unit->AddEventListener(c.GetPtr());
	a = nullptr;
	unit->EventHappened(EventDispatchB7Event());
	EventDispatchB7Trace(check, trace, {101, 201, 202, 203, 103}, "B7.9 nested removal safely suppresses future outer listener");
	check(released == 1 && !IsValid(weak), "B7.9 self contents release only after outer dispatch exits");
	trace.clear(); unit->EventHappened(EventDispatchB7Event(3));
	EventDispatchB7Trace(check, trace, {303}, "B7.9 only surviving registration remains");
}

/** Case 10: tombstones prune at the candidate turn; self death without flag waits. */
static void EventDispatchB7Tombstones(Checks& check, PFWorld* world)
{
	std::vector<int> trace;
	CObj<PFBaseUnit> unit = Unit(world, NDb::FACTION_FREEZE, 40);
	auto a = EventDispatchB7Listen(trace, 1);
	auto b = EventDispatchB7Listen(trace, 2);
	auto c = EventDispatchB7Listen(trace, 3);
	a->action = [&](EventDispatchB7Listener&, const PFBaseUnitEvent*) {
		auto predicate = [&](const CObj<PFBaseUnitEventListener>& candidate) { return candidate.GetPtr() != a.GetPtr(); };
		unit->RemoveEventListenerIf(predicate); return 0u;
	};
	unit->AddEventListener(a.GetPtr()); unit->AddEventListener(b.GetPtr()); unit->AddEventListener(c.GetPtr());
	unit->EventHappened(EventDispatchB7Event());
	EventDispatchB7Trace(check, trace, {101}, "B7.10 RemoveEventListenerIf suppresses pending callback");
	check(!b->IsAlive() && !c->IsAlive(), "B7.10 predicate marks every match dead instead of removing directly");
	a->action = nullptr; b->SetAlive(true); c->SetAlive(true); trace.clear(); unit->EventHappened(EventDispatchB7Event(2));
	EventDispatchB7Trace(check, trace, {201}, "B7.10 dead candidate was pruned at its turn");
	unit->RemoveEventListener(a.GetPtr()); trace.clear();
	a->action = [](EventDispatchB7Listener& self, const PFBaseUnitEvent*) { self.SetAlive(false); return 0u; };
	unit->AddEventListener(a.GetPtr()); unit->EventHappened(EventDispatchB7Event());
	a->SetAlive(true); a->action = nullptr; unit->EventHappened(EventDispatchB7Event(2));
	EventDispatchB7Trace(check, trace, {101, 201}, "B7.10 Windows samples alive before callback, not after");
	a->SetAlive(false); trace.clear(); unit->EventHappened(EventDispatchB7Event());
	a->SetAlive(true); unit->EventHappened(EventDispatchB7Event(2));
	check(trace.empty(), "B7.10 remaining dead listener prunes on subsequent dispatch");
	a->action = [&](EventDispatchB7Listener& self, const PFBaseUnitEvent*) {
		self.SetAlive(false); unit->EventHappened(EventDispatchB7Event(2)); return 0u;
	};
	unit->AddEventListener(a.GetPtr()); unit->EventHappened(EventDispatchB7Event());
	a->SetAlive(true); a->action = nullptr; unit->EventHappened(EventDispatchB7Event(3));
	EventDispatchB7Trace(check, trace, {101}, "B7.10 nested dispatch prunes an outer self tombstone safely");
}

/** Chunk 6 entry: callback mutations, contents lifetime, nested dispatch, and tombstones. */
static void EventReentrancyB7Regression(Checks& check, PFWorld* world)
{
	EventDispatchB7SelfRemoval(check, world);
	EventDispatchB7OtherRemoval(check, world);
	EventDispatchB7Additions(check, world);
	EventDispatchB7Readd(check, world);
	EventDispatchB7NestedRemoval(check, world);
	EventDispatchB7Tombstones(check, world);
}
