#ifndef __CWFN_H_INCLUDED__781309__
#define __CWFN_H_INCLUDED__781309__

#include "System/config.h"

#if defined( NV_LINUX_PLATFORM )
// Handled by Win32_linux.h or system headers
#include <errno.h>
typedef int errno_t;
#endif

#endif // __CWFN_H_INCLUDED__781309__
