#pragma once

// Single definition point for the Linux port of Windows sprintf_s.
// Included by System/systemStdAfx.h (PCH chain) and by the
// UniServer thrift_compat/ace_compat.h force-include. Do NOT redefine
// sprintf_s in individual .cpp files.
#if defined( NV_LINUX_PLATFORM )

#include <cstdio>
#include <cstdarg>

// Windows-style safe sprintf without size (maps to sprintf)
inline int sprintf_s(char *buf, const char *fmt, ...) {
  va_list args; va_start(args, fmt); int r = vsprintf(buf, fmt, args); va_end(args); return r;
}

// Windows-style safe sprintf with size (maps to snprintf)
inline int sprintf_s(char *buf, size_t sz, const char *fmt, ...) {
  va_list args; va_start(args, fmt); int r = vsnprintf(buf, sz, fmt, args); va_end(args); return r;
}

// Windows-style vsprintf_s (size is explicit, unlike the naive sizeof-based macro)
inline int vsprintf_s(char *buf, size_t sz, const char *fmt, va_list ap) {
  return vsnprintf(buf, sz, fmt, ap);
}

#endif // NV_LINUX_PLATFORM
