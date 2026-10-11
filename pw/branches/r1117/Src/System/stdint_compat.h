#pragma once
// Fixed-width integer types.
//
// <stdint.h> does not exist in the VS2008 (MSVC 15.xx) CRT, which is still the
// reference toolchain for the shipping Windows client (ShippingSingleExe|Win32),
// while the Linux build and every MSVC >= 2012 have it. Include this header
// instead of <stdint.h> from any shared (client+server) code.
//
// NOTE: the typedefs below must stay byte-identical to the real stdint.h ones -
// they are used in on-wire structures (Network/RUDP/RdpProto.h).

#if defined(_MSC_VER) && _MSC_VER < 1700

typedef signed char         int8_t;
typedef unsigned char       uint8_t;
typedef short               int16_t;
typedef unsigned short      uint16_t;
typedef int                 int32_t;
typedef unsigned int        uint32_t;
typedef __int64             int64_t;
typedef unsigned __int64    uint64_t;

#else

#include <stdint.h>

#endif
