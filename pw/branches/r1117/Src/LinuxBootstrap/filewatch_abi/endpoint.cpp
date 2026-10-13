#include "fixture.hpp"

namespace filewatch
{
namespace PW_FILEWATCH_ENDPOINT
{
namespace
{
/** Owned heap storage detects omitted member cleanup independently of sized delete. */
struct Payload
{
	Counters* counts;
	std::unique_ptr<char[]> bytes;
	explicit Payload(Counters* value) : counts(value), bytes(new char[64]()) {}
	~Payload() { ++counts->payload; }
};

struct OwnedMethod : FileWatchMethodFunctor<Receiver>
{
	Payload payload;
	OwnedMethod(Receiver* receiver, Counters* counts)
		: FileWatchMethodFunctor<Receiver>(receiver, &Receiver::OnChanged), payload(counts) {}
	~OwnedMethod() { ++payload.counts->derived; }
};

bool HasVirtualDestructor() { return std::has_virtual_destructor<IFileWatchFunctor>::value; }
unsigned Flags()
{
	unsigned flags = 0;
#ifdef PW_LINUX_DB_BOOTSTRAP
	flags |= 1;
#endif
#ifdef PW_LINUX_NULL_RENDER
	flags |= 2;
#endif
	return flags;
}
IFileWatchFunctor* Create(Receiver* receiver, Counters* counts)
{
	if (counts) return new OwnedMethod(receiver, counts);
	return new FileWatchMethodFunctor<Receiver>(receiver, &Receiver::OnChanged);
}
void Destroy(IFileWatchFunctor* callback) { delete callback; }
}
Endpoint Get() { return {&HasVirtualDestructor, &Flags, &Create, &Destroy}; }
}
}
