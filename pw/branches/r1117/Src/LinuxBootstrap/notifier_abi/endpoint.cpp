#include "abi.hpp"
#include <cstdlib>
#include <memory>
#include <type_traits>
#include "System/DefaultTypes.h"
#include "System/nlist.h"
#include "System/nhash_map.h"
using nstl::list;
using nstl::hash_map;
#include "System/Notifier.h"

#if !defined(__linux__)
#error This fixture checks the native Linux interface.
#endif
#if PW_NOTIFIER_ABI_BOOTSTRAP
#if !defined(PW_LINUX_DB_BOOTSTRAP) || !defined(PW_LINUX_NULL_RENDER)
#error The bootstrap endpoint must see both bootstrap definitions.
#endif
#else
#if defined(PW_LINUX_DB_BOOTSTRAP) || defined(PW_LINUX_NULL_RENDER)
#error The plain endpoint must not see bootstrap definitions.
#endif
#endif

namespace notifierabi
{
#if PW_NOTIFIER_ABI_BOOTSTRAP
namespace bootstrap
#else
namespace plain
#endif
{
namespace
{
/** An actual member allocation makes missing derived cleanup visible to LeakSanitizer. */
struct Payload
{
	Counters* counters;
	std::unique_ptr<char[]> storage;
	explicit Payload(Counters* value) : counters(value), storage(new char[64]) {}
	~Payload() { ++counters->member; }
};

/** Retain the engine's Call/Equals/OwnerByNotifier implementations unchanged. */
template<bool Owned>
struct Tracked : MethodNotifyCallback<Receiver, int, Owned>
{
	Payload payload;
	Tracked(Receiver* receiver, Counters* counters, bool secondary)
		: MethodNotifyCallback<Receiver, int, Owned>(receiver,
			secondary ? &Receiver::Secondary : &Receiver::Primary), payload(counters) {}
	~Tracked() { ++payload.counters->derived; }
};

bool HasVirtualDestructor() { return std::has_virtual_destructor<Callback>::value; }
unsigned BootstrapFlags()
{
	unsigned result = 0;
#ifdef PW_LINUX_DB_BOOTSTRAP
	result |= 1;
#endif
#ifdef PW_LINUX_NULL_RENDER
	result |= 2;
#endif
	return result;
}

template<bool Owned>
Callback* CreateKind(Receiver* receiver, Counters* counters, bool secondary)
{
	if (counters) return new Tracked<Owned>(receiver, counters, secondary);
	return new MethodNotifyCallback<Receiver, int, Owned>(receiver,
		secondary ? &Receiver::Secondary : &Receiver::Primary);
}

Callback* Create(Receiver* receiver, Counters* counters, bool owned, bool secondary)
{
	return owned ? CreateKind<true>(receiver, counters, secondary)
		: CreateKind<false>(receiver, counters, secondary);
}

void Call(Callback* callback, int value) { callback->Call(value); }
bool Owned(Callback* callback) { return callback->OwnerByNotifier(); }

template<bool Owned>
bool MatchesKind(Callback* callback, Receiver* receiver, bool secondary)
{
	MethodNotifyCallback<Receiver, int, Owned> other(receiver,
		secondary ? &Receiver::Secondary : &Receiver::Primary);
	return callback->Equals(&other) && other.Equals(callback);
}

bool Matches(Callback* callback, Receiver* receiver, bool owned, bool secondary)
{
	return owned ? MatchesKind<true>(callback, receiver, secondary)
		: MatchesKind<false>(callback, receiver, secondary);
}

/** The unfixed interface is rejected before allocation; never instantiate its unsafe delete. */
template<class Base>
void SafeDelete(Base* callback)
{
	if constexpr (std::has_virtual_destructor<Base>::value) delete callback;
	else std::abort();
}
void Destroy(Callback* callback) { SafeDelete(callback); }
}

Endpoint GetEndpoint()
{
#if PW_NOTIFIER_ABI_BOOTSTRAP
	const char* name = "bootstrap";
#else
	const char* name = "plain";
#endif
	return {name, &HasVirtualDestructor, &BootstrapFlags, &Create, &Call, &Matches, &Owned, &Destroy};
}
}
}
