#include "abi.hpp"
#include <cstdio>
#include <initializer_list>

/** Standalone assertions remain active in Release and fail before touching an unsafe interface. */
struct Checks
{
	int count = 0, failed = 0;
	void operator()(bool ok, const char* label)
	{
		++count;
		if (!ok) { ++failed; std::printf("Notifier ABI FAIL: %s\n", label); }
	}
};

int main()
{
	using namespace notifierabi;
	const Endpoint endpoints[] = {plain::GetEndpoint(), bootstrap::GetEndpoint()};
	Checks check;
	check(endpoints[0].bootstrapFlags() == 0, "plain TU has no bootstrap definitions");
	check(endpoints[1].bootstrapFlags() == 3, "bootstrap TU has both bootstrap definitions");
	for (const Endpoint& endpoint : endpoints)
	{
		const bool safe = endpoint.hasVirtualDestructor();
		std::printf("Notifier ABI: %s flags=%u virtual-destructor=%d\n",
			endpoint.name, endpoint.bootstrapFlags(), int(safe));
		check(safe, "every Linux TU must see the virtual callback destructor");
	}
	if (check.failed)
	{
		std::printf("Notifier ABI rejected before allocation: %d checks, %d failures\n", check.count, check.failed);
		return 1;
	}
	for (int source = 0; source != 2; ++source)
	{
		const Endpoint& producer = endpoints[source];
		const Endpoint& consumer = endpoints[1 - source];
		for (bool owned : {false, true}) for (bool secondary : {false, true}) for (bool tracked : {false, true})
		{
			Receiver receiver, unrelated;
			Counters counters;
			Callback* callback = producer.create(&receiver, tracked ? &counters : nullptr, owned, secondary);
			check(consumer.owned(callback) == owned, "ownership query crosses TU boundary");
			consumer.call(callback, 7);
			consumer.call(callback, -2);
			check(receiver.calls == 2 && receiver.sum == 5 && receiver.secondary == (secondary ? 2 : 0),
				"real MethodNotifyCallback invokes the selected receiver method across TUs");
			check(consumer.matches(callback, &receiver, owned, secondary), "real RTTI equality is symmetric across TUs");
			check(!consumer.matches(callback, &unrelated, owned, secondary), "another receiver is not equal");
			check(!consumer.matches(callback, &receiver, owned, !secondary), "another method is not equal");
			check(!consumer.matches(callback, &receiver, !owned, secondary), "another ownership specialization is not equal");
			consumer.destroy(callback);
			check(counters.derived == int(tracked) && counters.member == int(tracked),
				"opposite TU deletion reaches derived and member destructors exactly once");
			check(receiver.calls == 2 && receiver.sum == 5, "deletion does not invoke the receiver");
		}
	}
	std::printf("Notifier ABI: %d checks, %d failures\n", check.count, check.failed);
	return check.failed ? 1 : 0;
}
