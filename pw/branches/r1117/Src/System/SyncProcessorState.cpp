#include "stdafx.h"

#include "SyncProcessorState.h"
#if defined( NI_PLATF_LINUX )

#include <dirent.h>
#include <unistd.h>
#include <cstdio>

void SyncProcessorState()
{
}

unsigned int GetProcessorState()
{
  return LOGIC_PROCESSOR_STATE;
}

void SetProcessorState( unsigned int /*newState*/, unsigned int /*mask*/ )
{
}

bool IsProcessorStateForLogic()
{
  return true;
}

bool IsProcessorStateForUI()
{
  return true;
}

namespace utils
{

bool GetMemoryStatus( size_t & virtualSize )
{
  virtualSize = 0;

  FILE* statm = fopen("/proc/self/statm", "r");
  if ( !statm )
    return false;

  unsigned long pages = 0;
  const bool ok = fscanf( statm, "%lu", &pages ) == 1;
  fclose( statm );

  if ( !ok )
    return false;

  const long pageSize = sysconf( _SC_PAGESIZE );
  if ( pageSize <= 0 )
    return false;

  virtualSize = static_cast<size_t>( pages ) * static_cast<size_t>( pageSize );
  return true;
}

int GetThreadCount()
{
  DIR* taskDir = opendir( "/proc/self/task" );
  if ( !taskDir )
    return -1;

  int count = 0;
  while ( dirent* entry = readdir( taskDir ) )
  {
    if ( entry->d_name[0] != '.' )
      ++count;
  }

  closedir( taskDir );
  return count;
}

} //namespace utils

#else

#include <float.h>
#include <psapi.h>
#include <tlhelp32.h>

void SyncProcessorState()
{
	SetProcessorState( LOGIC_PROCESSOR_STATE, 0xffffffff );
}

unsigned int GetProcessorState()
{
  return _control87( 0, 0 );
}

void SetProcessorState( unsigned int newState, unsigned int mask /*= 0xffffffff*/ )
{
  _control87( newState, mask );
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
  PROCESS_MEMORY_COUNTERS pmc;
  ZeroMemory( &pmc, sizeof( pmc ) );
  pmc.cb = sizeof( pmc );
  if ( !GetProcessMemoryInfo( GetCurrentProcess(), &pmc, sizeof( pmc ) ) )
    return false;

  virtualSize = (size_t)pmc.PagefileUsage;
  return true;
}

int GetThreadCount()
{
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
}

} //namespace utils

#endif
