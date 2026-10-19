#pragma once
// <typeinfo.h> is an ancient MSVC CRT header. It exists in the VS2008 CRT
// (MSVC 15.xx), which is still the reference toolchain for the shipping
// Windows client (ShippingSingleExe|Win32), but it was removed in MSVC 14.x
// (VS2017+). The Linux port never had it and always used <typeinfo>.
//
// Include this header instead of <typeinfo.h> / <typeinfo> from shared code.
// It must not change what the reference (VS2008/x86) build sees.

#if defined(__has_include)
#  if __has_include(<typeinfo.h>)
#    include <typeinfo.h>
#  else
#    include <typeinfo>
#  endif
#elif defined(_MSC_VER) && _MSC_VER < 1700
#  include <typeinfo.h>
#else
#  include <typeinfo>
#endif
