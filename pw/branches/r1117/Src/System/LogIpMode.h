#ifndef LOGIPMODE_H_INCLUDED
#define LOGIPMODE_H_INCLUDED

#include "config.h"

// Rendering mode for IPv4 addresses in log output.
//
// It is a compile-time constant on purpose: a player controls Profiles/*.cfg and
// user.cfg, so a runtime switch could just be turned off by whoever wants to read
// the addresses back. There is no "log_ip_mode" config variable.
//
//   0 - dotted quad, as before.
//   1 - an address is left out of log lines (ports and muxes are still printed),
//       and the single address of the server selected for the connection is logged
//       once, at connect time, in hex (same byte order as the launcher protocol:
//       0a4e0001 == 10.78.0.1).
//
// Client builds mask; the Linux server build keeps the historical output
// byte-for-byte - pool logs must not change. Debug override for our own builds:
// compile with -DPW_LOG_IP_MODE=0 (never reachable from a config file).
//
// NOTE: keyed on the platform because every shipped client is a Windows build. If a
// Linux client build ever ships to players, it must set PW_LOG_IP_MODE=1 explicitly.
#if !defined( PW_LOG_IP_MODE )
  #if defined( NV_WIN_PLATFORM )
    #define PW_LOG_IP_MODE 1
  #else
    #define PW_LOG_IP_MODE 0
  #endif
#endif

namespace NGlobal
{
  inline int GetLogIpMode() { return PW_LOG_IP_MODE; }
}

#endif //LOGIPMODE_H_INCLUDED
