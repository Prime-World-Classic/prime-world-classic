#include "stdafx.h"
#include "Reallocate.h"
#include "smartrenderer.h"
#include "System/MultiReaderLock.h"

namespace Render {

static threading::Mutex* mutexes = NULL;

static bool s_threaded_loading = true;

static volatile bool s_reset_requested = false;
static threading::MultiReaderLock s_reset_mutex;
static const int WAIT_TICKS = 10000;


inline ReallocateHandler::Ring& ReallocateHandler::GetHandlers(int _priority)
{
  static Ring theHandlers[ReallocateHandler::HANDLERPRIORITY_COUNT];
  return theHandlers[_priority];
}


bool ReallocateHandler::InitReallocateHandler()
{
  static threading::Mutex theMutexes[ReallocateHandler::HANDLERPRIORITY_COUNT];

  mutexes = theMutexes;
  s_threaded_loading = NGlobal::GetVar( "render_threadsafe", 1 ).GetInt64();
  return true;
}

// This class do locks only when threaded loading is enabled
class LoadingMutexLock : public NonCopyable
{
  const threading::Mutex * mutex;

public:
  LoadingMutexLock(const threading::Mutex & _mutex)
  {
    if(s_threaded_loading)
    {
      mutex = &_mutex;
      mutex->Lock();
    }
    else
      mutex = 0;
  }

  ~LoadingMutexLock()
  {
    if(mutex)
      mutex->Unlock();
  }
};


void ReallocateHandler::SelfRegister()
{
  static bool inited = InitReallocateHandler();

#ifdef _DEBUG
  static DWORD threadId = GetCurrentThreadId();
  if( threadId != GetCurrentThreadId() )
    threadId = threadId; // set breakpoint here if desirable
#endif // _DEBUG

  LoadingMutexLock lock(mutexes[priority]);
  GetHandlers(priority).addLast(this);
}

void ReallocateHandler::UnRegister()
{
  LoadingMutexLock lock(mutexes[priority]);
  ReallocateHandler::Ring::remove(this);
}


ReallocateHandlerLoadable::LoaderCS::LoaderCS(volatile bool& _loadFinished)
: loadFinished(_loadFinished)
{
  //int i = WAIT_TICKS;
  //while(s_reset_requested) {
  //  if(--i < 0)
  //    break; // don't wait infinitely
  //  Sleep(0);
  //}

  s_reset_mutex.LockForRead();
}


ReallocateHandlerLoadable::LoaderCS::~LoaderCS()
{
  s_reset_mutex.UnlockAfterRead();
  loadFinished = true;
}


void NotifyResourcesRelease()
{
  DebugTrace("NotifyResourcesRelease");
  
  s_reset_requested = true;
  threading::WriteLockGuard guard( s_reset_mutex );

  SmartRenderer::NullThePointers();

  for (int i = 0; i < ReallocateHandler::HANDLERPRIORITY_COUNT; i++)
  {
    LoadingMutexLock lock(mutexes[i]);
    for(ring::Range<ReallocateHandler::Ring> it(ReallocateHandler::GetHandlers(i)); it; ++it)
      it->OnResourcesRelease();
  }
}

void NotifyResourcesReallocate()
{
  {
    DebugTrace("NotifyResourcesReallocate");
    
    threading::WriteLockGuard guard( s_reset_mutex );
  
    for (int i = 0; i < ReallocateHandler::HANDLERPRIORITY_COUNT; i++)
    {
      LoadingMutexLock lock(mutexes[i]);
      for(ring::Range<ReallocateHandler::Ring> it(ReallocateHandler::GetHandlers(i)); it; ++it)
      {
        ASSERT(it.operator->());
        it->OnResourcesReallocate();
      }
    }
  }
  s_reset_requested = false;
}

//////////////////////////////////////////////////////////////////////////

static DeviceDeleteHandler::Ring* ddHandlers = NULL;
static threading::Mutex* ddMutexes = NULL;

bool InitDeviceDeleteHandler()
{
  static DeviceDeleteHandler::Ring theHandlers[ReallocateHandler::HANDLERPRIORITY_COUNT];
  static threading::Mutex theMutexes[ReallocateHandler::HANDLERPRIORITY_COUNT];

  ddHandlers = theHandlers;
  ddMutexes = theMutexes;
  s_threaded_loading = NGlobal::GetVar( "render_threadsafe", 1 ).GetInt64();
  return true;
}

DeviceDeleteHandler::DeviceDeleteHandler(HandlerPriority p /*= HANDLERPRIORITY_NORMAL*/)
: priority(p)
{
  static bool inited = InitDeviceDeleteHandler();

  LoadingMutexLock lock(ddMutexes[p]);
  ddHandlers[p].addLast(this);
}

DeviceDeleteHandler::~DeviceDeleteHandler()
{
  LoadingMutexLock lock(ddMutexes[priority]);
  DeviceDeleteHandler::Ring::remove(this);
}

void DeviceDeleteHandler::NotifyDeviceCreate()
{
  DebugTrace("NotifyDeviceCreate");
  
  for(int i = 0; i < DeviceDeleteHandler::HANDLERPRIORITY_COUNT; i++)
  {
    LoadingMutexLock lock(ddMutexes[i]);
    for(ring::Range<DeviceDeleteHandler::Ring> it( ddHandlers[i] ); it; ++it)
      it->OnDeviceCreate();
  }
}

void DeviceDeleteHandler::NotifyDeviceDelete()
{
  DebugTrace("NotifyDeviceDelete");
  
  for(int i = DeviceDeleteHandler::HANDLERPRIORITY_COUNT; --i >= 0;)
  {
    LoadingMutexLock lock(ddMutexes[i]);
    for(ring::Range<DeviceDeleteHandler::Ring> it( ddHandlers[i] ); it; ++it)
      it->OnDeviceDelete();
  }
}

} // namespace Render

// end of Reallocate.cpp
