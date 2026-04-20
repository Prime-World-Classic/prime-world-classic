#include "stdafx.h"
#include "AppInstancesLimit.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
AppInstancesLimit::AppInstancesLimit( const char * appKeyName ) :
key( appKeyName ),
winMutex( NULL ),
index( 0 )
{
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef _WIN32
AppInstancesLimit::~AppInstancesLimit()
{
  if ( winMutex )
    CloseHandle( winMutex );
}
#else
AppInstancesLimit::~AppInstancesLimit() {}
#endif


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef _WIN32
bool AppInstancesLimit::Lock( int limit )
{
  for( int i = 0; i < limit; ++i )
  {
    winMutex = CreateMutex( 0, TRUE, NStr::StrFmt( "%s_instance_%02x", key.c_str(), i ) );

    if ( !winMutex )
      continue;
    if ( GetLastError() != ERROR_SUCCESS )
    {
      CloseHandle( winMutex );
      winMutex = NULL;
      continue;
    }

    index = i + 1;
    return true;
  }
  return false;
}
#else
bool AppInstancesLimit::Lock( int limit ) { return true; }
#endif
