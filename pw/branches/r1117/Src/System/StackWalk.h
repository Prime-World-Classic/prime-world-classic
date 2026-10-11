#ifndef _STACKWALK_H_
#define _STACKWALK_H_

#if defined( NV_WIN_PLATFORM )
#include "../MemoryLib/SymAccess.h"
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CollectCallStack( vector<SCallStackEntry> *pCallStack );
void CollectCallStack( vector<SCallStackEntry> *pCallStack, const EXCEPTION_POINTERS *pExPtrs );
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#elif defined( NV_LINUX_PLATFORM )
inline void CollectCallStack( void * /*pCallStack*/ ) {}
#endif

#endif

