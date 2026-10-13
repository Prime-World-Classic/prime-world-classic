#include "fixture.hpp"

namespace
{
int checks = 0;
int failures = 0;
void Check(bool condition, const char* label)
{
	++checks;
	if (!condition)
	{
		++failures;
		std::fprintf(stderr, "FAIL: %s\n", label);
	}
}

void StaticChanged(const string&) {}
}

int main()
{
	using namespace filewatch;
	const Endpoint endpoints[] = {plain::Get(), bootstrap::Get()};
	Check(endpoints[0].flags() == 0, "plain TU has no bootstrap flags");
	Check(endpoints[1].flags() == 3, "bootstrap TU has both flags");
	Check(endpoints[0].hasVirtualDestructor(), "plain interface has virtual destructor");
	Check(endpoints[1].hasVirtualDestructor(), "bootstrap interface has virtual destructor");
	// Keep an unfixed interface a clean rejection, not an intentional unsafe delete.
	if (failures) return 1;
	for (unsigned direction = 0; direction != 2; ++direction)
	{
		Receiver receiver, differentReceiver;
		Counters counts;
		IFileWatchFunctor* callback = endpoints[direction].create(&receiver, &counts);
		FileWatchMethodFunctor<Receiver> key(&receiver, &Receiver::OnChanged);
		FileWatchMethodFunctor<Receiver> otherMethod(&receiver, &Receiver::Other);
		FileWatchMethodFunctor<Receiver> otherReceiver(&differentReceiver, &Receiver::OnChanged);
		FileWatchFunctor staticKey(&StaticChanged);
		Check(callback->Equals(&key) && key.Equals(callback), "real inherited equality matches stack key");
		Check(!callback->Equals(&otherMethod), "different receiver method does not match");
		Check(!callback->Equals(&otherReceiver), "different receiver does not match");
		Check(!callback->Equals(&staticKey) && !staticKey.Equals(callback), "static and method callback kinds differ");
		callback->Call(string("UI/Root.xdb"));
		Check(receiver.calls == 1 && differentReceiver.calls == 0, "real inherited Call reaches borrowed receiver");
		Check(counts.derived == 0 && counts.payload == 0, "owned payload alive until callback deletion");
		endpoints[1 - direction].destroy(callback);
		Check(counts.derived == 1, "other TU calls concrete destructor exactly once");
		Check(counts.payload == 1, "other TU releases owned member exactly once");
		key.Call(string("still-alive.xdb"));
		Check(receiver.calls == 2, "deleting callback does not own receiver or stack key");
		callback = endpoints[direction].create(&receiver, nullptr);
		Check(callback->Equals(&key), "unextended engine method wrapper preserves equality");
		endpoints[1 - direction].destroy(callback);
		Check(receiver.calls == 2, "unextended engine method wrapper deletes without invoking receiver");
		// The other built-in implementation also crosses the interface delete boundary.
		endpoints[direction].destroy(new FileWatchFunctor(&StaticChanged));
	}
	std::printf("filewatch ABI: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
