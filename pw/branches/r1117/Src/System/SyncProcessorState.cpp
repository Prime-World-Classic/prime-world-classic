#include "stdafx.h"

#include "SyncProcessorState.h"
#include <float.h>
#ifdef _WIN32
#include <psapi.h>
#endif
#ifdef _WIN32
#include <tlhelp32.h>
#endif


void SyncProcessorState()
{
	SetProcessorState( LOGIC_PROCESSOR_STATE, 0xffffffff );
}


unsigned int GetProcessorState()
{
#ifdef _WIN32
  return _control87( 0, 0 );
#else
  return 0;
#endif
}


void SetProcessorState( unsigned int newState, unsigned int mask /*= 0xffffffff*/ )
{
#ifdef _WIN32
  _control87( newState, mask );
#endif
}

bool IsProcessorStateForLogic()
{
  return (LOGIC_PROCESSOR_STATE == GetProcessorState());
}

bool IsProcessorStateForUI()
{
  return (UI_PROCESSOR_STATE == GetProcessorState());
}

namespace utils
{

bool GetMemoryStatus( size_t & virtualSize )
{
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS pmc;
  ZeroMemory( &pmc, sizeof( pmc ) );
  pmc.cb = sizeof( pmc );
  if ( !GetProcessMemoryInfo( GetCurrentProcess(), &pmc, sizeof( pmc ) ) )
    return false;

  virtualSize = (size_t)pmc.PagefileUsage;
  return true;
#else
  virtualSize = 0;
  return false;
#endif
}




int GetThreadCount()
{
#ifdef _WIN32
  DWORD pid = GetCurrentProcessId();

  HANDLE snapshot = CreateToolhelp32Snapshot( TH32CS_SNAPALL, 0 );
  NI_ASSERT( snapshot, "CreateToolhelp32Snapshot() failed" );

  PROCESSENTRY32 entry = { 0 };
  entry.dwSize = sizeof( entry );

  BOOL ret = Process32First( snapshot, &entry );
  NI_ASSERT( ret, "Process32First() failed" );
  while ( ret && entry.th32ProcessID != pid ) {
    ret = Process32Next( snapshot, &entry );
    NI_ASSERT( ret, "Process32Next() failed" );
  }

  CloseHandle( snapshot );

  return ret ? entry.cntThreads : -1;
#else
  return 1;
#endif
}

} //namespace utils
