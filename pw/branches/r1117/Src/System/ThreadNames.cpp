#include "stdafx.h"

#include "ThreadNames.h"
#include "SyncPrimitives.h"

#if defined( NV_LINUX_PLATFORM )
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace threading
{

#ifndef _SHIPPING
struct SThreadNames
{
  typedef map<DWORD, string> TNames;
  Mutex     mutex;
  TNames    names;
};

// Fix NUM_TASK
static SThreadNames threadNames;
#endif

//Nice MSVS hack
#define MS_VC_EXCEPTION 0x406D1388

#pragma pack(push,8)
  typedef struct tagTHREADNAME_INFO
  {
    DWORD dwType; // Must be 0x1000.
    LPCSTR szName; // Pointer to name (in user addr space).
    DWORD dwThreadID; // Thread ID (-1=caller thread).
    DWORD dwFlags; // Reserved for future use, must be zero.
  } THREADNAME_INFO;
#pragma pack(pop)


static void ThrowVcException( const char* threadName )
{
#if defined( NV_WIN_PLATFORM )
  if ( !IsDebuggerPresent() )
    return;
  //moved to separete function for error workaround:
  //error C2712: Cannot use __try in functions that require object unwinding

#if !defined( _SHIPPING )
  THREADNAME_INFO info;
  info.dwType = 0x1000;
  info.szName = threadName;
  info.dwThreadID = ::GetCurrentThreadId();
  info.dwFlags = 0;

  __try
  {
    RaiseException( MS_VC_EXCEPTION, 0, sizeof(info)/sizeof(ULONG_PTR), (ULONG_PTR*)&info );
  }
  __except(EXCEPTION_EXECUTE_HANDLER)
  {
  }
#endif
#endif // defined( NV_WIN_PLATFORM )
}

void SetDebugThreadName( const char* threadName )
{
  if ( !threadName )
    return;

#if defined( NV_LINUX_PLATFORM )
  // Make the name visible in /proc/PID/task/TID/comm (top, htop, perf, gdb):
  // on Linux the old implementation was a no-op there, which hid thread roles
  // during server profiling (REPORT_server_profiling.md, round 2).
  // pthread_setname_np limits the name to 15 chars + NUL; long names are
  // truncated (EPERM is ignored on purpose). The main thread is skipped on
  // purpose: renaming it changes the process name shown by ps/top, which
  // breaks operational tooling (pgrep -x UniServerApp).
  if ( (int)syscall( SYS_gettid ) != getpid() )
    pthread_setname_np( pthread_self(), threadName );
#endif

#ifndef _SHIPPING
  {
    SThreadNames & names = threadNames;
    MutexLock lock( names.mutex );
#if defined( NV_WIN_PLATFORM )
    names.names[ ::GetCurrentThreadId() ] = threadName;
#elif defined( NV_WIN_PLATFORM )
    names.names[ ::pthread_self() ] = threadName;
#endif
  }

  ThrowVcException( threadName );
#endif
}


const char * GetDebugThreadName( DWORD threadId )
{
#ifndef _SHIPPING
  SThreadNames & names = threadNames;

  MutexLock lock( names.mutex );

  SThreadNames::TNames::iterator it = names.names.find( threadId );
  if ( it != names.names.end() )
    return it->second.c_str();
#endif
  return "<Unknown>";
}

}; //namespace threading

