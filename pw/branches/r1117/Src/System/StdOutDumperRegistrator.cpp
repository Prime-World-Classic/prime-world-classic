#include "stdafx.h"
#include "StdOutDumper.h"

namespace NLogg
{
#ifndef NV_LINUX_PLATFORM
  static NLogg::CStdOutDumper g_stdoutDumper( &GetSystemLog() );
#endif
}