#pragma once

// Only a declaration crosses the harness boundary; endpoint.cpp includes the real interface.
template<class TParam> struct INotifyCallback;

namespace notifierabi
{
using Callback = INotifyCallback<int>;

/** Receiver lifetime belongs to the test, never to its MethodNotifyCallback. */
struct Receiver
{
	int calls = 0, sum = 0, secondary = 0;
	void Primary(const int& value) { ++calls; sum += value; }
	void Secondary(const int& value) { ++calls; sum += value; ++secondary; }
};

/** Separate derived/member counters detect incomplete polymorphic destruction. */
struct Counters
{
	int derived = 0, member = 0;
};

/** Non-inline entry points ensure each operation is compiled in its designated TU. */
struct Endpoint
{
	const char* name;
	bool (*hasVirtualDestructor)();
	unsigned (*bootstrapFlags)();
	Callback* (*create)(Receiver*, Counters*, bool, bool);
	void (*call)(Callback*, int);
	bool (*matches)(Callback*, Receiver*, bool, bool);
	bool (*owned)(Callback*);
	void (*destroy)(Callback*);
};

namespace plain
{
	Endpoint GetEndpoint();
}
namespace bootstrap
{
	Endpoint GetEndpoint();
}
}
