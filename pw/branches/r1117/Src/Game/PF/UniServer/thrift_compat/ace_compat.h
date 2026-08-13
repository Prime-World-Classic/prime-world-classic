#ifndef ACE_COMPAT_H_INCLUDED
#define ACE_COMPAT_H_INCLUDED

// ============================================================
// Pre-include system headers
// ============================================================
#include <sys/time.h>     // gettimeofday
#include <sys/resource.h> // struct rusage
#include <fcntl.h>        // fcntl
#include <arpa/inet.h>    // htons, htonl, ntohs, ntohl, TCP_NODELAY
#include <netdb.h>        // gai_strerror, getaddrinfo, freeaddrinfo, addrinfo, AI_PASSIVE, AI_ADDRCONFIG
#include <sys/socket.h>   // sockaddr_un, socket types
#include <sys/un.h>       // sockaddr_un
#include <netinet/in.h>   // sockaddr_in
#include <algorithm>      // std::min, std::max (needed by old Thrift)
#include <unistd.h>       // close

// TCP_NODELAY might not be exposed if _BSD_SOURCE/_DEFAULT_SOURCE not set
#ifndef TCP_NODELAY
  #define TCP_NODELAY 1
#endif

// ============================================================
// Critical workaround: glibc declares `struct rusage` but ACE
// 8.x expects a bare `typedef rusage` (not `struct rusage`).
// Create the typedef so ACE's `typedef rusage ACE_Rusage` works.
// ============================================================
#ifndef __rusage_defined
  typedef struct rusage rusage;
  #define __rusage_defined
#endif

// ============================================================
// ACE 8.x compatibility macros for old Terabit code
// ============================================================
#ifndef ACE_static_cast
  #define ACE_static_cast(T, expr) static_cast<T>(expr)
#endif

#ifndef ACE_LIB_TEXT
  #define ACE_LIB_TEXT(x) x
#endif

#ifndef ACE_RCSID
  #define ACE_RCSID(n, m, v)
#endif

// ============================================================
// Workaround: ACE::get_flags namespace issue with GCC 15.
// Define ACE_LACKS_FCNTL to skip problematic fcntl code.
// ============================================================
#ifndef ACE_LACKS_FCNTL
  #define ACE_LACKS_FCNTL 1
#endif

// ============================================================
// sprintf_s compatibility: Windows-only safe sprintf.
// On Linux, map to snprintf which is the POSIX safe version.
// ============================================================
#ifndef sprintf_s
  #define sprintf_s(buf, sz, fmt, ...) snprintf(buf, sz, fmt, __VA_ARGS__)
#endif

#endif // ACE_COMPAT_H_INCLUDED
