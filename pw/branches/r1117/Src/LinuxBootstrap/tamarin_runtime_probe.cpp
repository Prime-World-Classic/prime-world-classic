#include "avmplus.h"

#include <cstdlib>
#include <cstdio>

namespace
{
  class ProbeCore : public avmplus::AvmCore
  {
  public:
    explicit ProbeCore(MMgc::GC* gc)
      : AvmCore(gc)
    {
      static const char* uris[] = { "" };
      static const int32_t apiCompatibility[] = { 1 };
      setAPIInfo(0, 1, 0, uris, apiCompatibility);
    }

    virtual int32_t getDefaultAPI()
    {
      return 0;
    }

    virtual void interrupt(avmplus::Toplevel*, InterruptReason)
    {
      std::abort();
    }

    virtual void stackOverflow(avmplus::Toplevel*)
    {
      std::abort();
    }
  };
}

int main()
{
  MMgc::GCHeap::EnterLockInit();
  MMgc::GCHeapConfig config;
  MMgc::GCHeap::Init(config);

  MMgc::GCHeap* heap = MMgc::GCHeap::GetGCHeap();
  bool allocated = false;
  bool builtinPool = false;
  bool topLevel = false;
  if (heap)
  {
    MMgc::GC gc(heap, MMgc::GC::kIncrementalGC);
    MMGC_GCENTER(&gc);
    allocated = gc.Alloc(64) != 0;
    {
      ProbeCore* core = new ProbeCore(&gc);
      core->setActiveAPI(0);
      avmplus::AvmCore::CacheSizes cacheSizes;
      core->setCacheSizes(cacheSizes);
      core->initBuiltinPool();
      builtinPool = core->builtinPool != 0;
      topLevel = core->initTopLevel() != 0;
      delete core;
    }
    gc.Collect(false);
  }

  const size_t leakedBytes = MMgc::GCHeap::Destroy();
  MMgc::GCHeap::EnterLockDestroy();
  std::printf(
    "Tamarin runtime probe: heap=%s allocation=%s builtins=%s topLevel=%s leakedBytes=%zu\n",
    heap ? "yes" : "no",
    allocated ? "yes" : "no",
    builtinPool ? "yes" : "no",
    topLevel ? "yes" : "no",
    leakedBytes);

  return heap && allocated && builtinPool && topLevel && leakedBytes == 0 ? 0 : 1;
}
