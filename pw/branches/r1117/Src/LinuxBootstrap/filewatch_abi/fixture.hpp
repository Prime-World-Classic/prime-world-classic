#pragma once

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <type_traits>
#include "System/DefaultTypes.h"
#include "System/nlist.h"
#include "System/nstring.h"
#include "System/nhash_map.h"
using nstl::list;
using nstl::string;
using nstl::hash_map;
#include "System/Basic.h"
#include "FileSystemChangesProcessor.h"

namespace filewatch
{
/** Receivers are borrowed, exactly as in DbResourceCache::SetFileSystemChangesProcessor. */
struct Receiver
{
	int calls = 0;
	void OnChanged(const string&) { ++calls; }
	void Other(const string&) { calls += 10; }
};

struct Counters
{
	int derived = 0;
	int payload = 0;
};

/** Both endpoints include the actual interface with different bootstrap definitions. */
struct Endpoint
{
	bool (*hasVirtualDestructor)();
	unsigned (*flags)();
	IFileWatchFunctor* (*create)(Receiver*, Counters*);
	void (*destroy)(IFileWatchFunctor*);
};
namespace plain { Endpoint Get(); }
namespace bootstrap { Endpoint Get(); }
}
