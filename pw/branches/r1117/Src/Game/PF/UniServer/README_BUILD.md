# UniServerApp Linux Build Guide

## Overview

This CMake build system compiles UniServerApp (Prime World Classic game server) for Linux.
The original project was built with Visual Studio 2008 on Windows.

## System Requirements

- GCC 15+ (tested on Ubuntu 25.04)
- CMake 3.15+
- System packages:
  - `libace-dev` (ACE 8.x - NOT used directly, vendored ACE 5.x is built)
  - `libcurl4-openssl-dev`
  - `libssl-dev`
  - `libz-dev`
  - `libthrift-dev`
  - `libtinyxml-dev` (optional fallback)
  - `libjsoncpp-dev` (optional fallback)
  - `make` or `ninja`

## Build Instructions

```bash
cd Src/Game/PF/UniServer
mkdir -p build_linux && cd build_linux
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

The output binary will be `UniServerApp` in the build directory.

## Architecture

### Libraries Built from Vendored Sources

| Library | Version | Notes |
|---------|---------|-------|
| ACE | 5.x | Patched for Linux/GCC 15 compatibility |
| Terabit (TProactor) | vendored | Depends on ACE 5.x |
| mongoose | vendored | HTTP server |
| wsdlpull | vendored | WSDL parsing |
| JsonCpp | vendored | JSON library |
| tinyxml | vendored | XML library |
| MD4 | vendored | Hash function |

### Libraries from System Packages

| Library | Notes |
|---------|-------|
| curl | HTTP client |
| OpenSSL | SSL/TLS |
| zlib | Compression |
| Thrift | Uses system Thrift 0.22.0 + compatibility shim |
| pthreads | Threading |

### Project Libraries Built from Source

| Library | Source Location | Notes |
|---------|-----------------|-------|
| PF_GameLogic | `Src/PF_GameLogic/` | Game logic (253 files) |
| UniServerApp | `Src/Game/PF/UniServer/` | Main executable (1125 files) |

Source files are collected automatically from `.cmake` files in `UniServerApp.auto/`.

## Patches Applied to Vendored Libraries

### ACE 5.x (Vendor/ACE_wrappers/)

1. **os_include/os_sched.h**: Fixed `cpu_set_t` redefinition conflict with glibc
2. **OS_NS_stropts.h**: Added `ACE_LACKS_STROPTS_H` guards, added stub `ACE_Str_Buf` struct for non-STREAMS platforms
3. **OS_NS_stropts.inl**: Rewrote to guard STREAMS functions, added `isastream` stub
4. **OS_NS_stropts.cpp**: Added `ACE_LACKS_STROPTS_H` guards around QoS ioctl functions
5. **os_stropts.h**: Already patched (stub)
6. **config-linux-common.h**: Added unconditional `ACE_LACKS_STROPTS_H` and `ACE_LACKS_SYS_SYSCTL_H`
7. **ARGV.cpp**: Added missing `#include "ace/ARGV.h"`
8. **Message_Block_T.cpp**: Added missing `#include "ace/Message_Block_T.h"`
9. **Tokenizer_T.cpp**: Added missing `#include "ace/Tokenizer_T.h"`
10. **SPIPE_Stream.h**: Replaced with stub (STREAMS not available on Linux)
11. **SPIPE_Stream.inl**: Removed (use stub header)

### Terabit (Vendor/Terabit/)

No patches needed. Build excludes: SSL files, examples, tests.

### Thrift

Uses system Thrift 0.22.0 instead of vendored 0.9.1.
Compatibility shim: `thrift_compat/cxxfunctional.h`

## Known Issues / Remaining Work

### PF_GameLogic Rendering Headers

**Status: BLOCKING**

PF_GameLogic includes client-side rendering headers through `stdafx.h` → `specific.h`:
```cpp
#include "../Render/DBRender.h"
#include "../Render/renderer.h"
#include "../Render/material.h"
#include "../Render/smartrenderer.h"
#include "../Render/ImmediateRenderer.h"
```

These headers reference DirectX/D3D rendering code that doesn't exist on Linux.
The server doesn't need rendering, but the headers are included transitively.

**Solution needed**: Either:
1. Create stub render headers
2. Modify `specific.h` to conditionally exclude render headers when `NV_LINUX_PLATFORM` is defined
3. Create a server-only variant of `stdafx.h`

### Windows-Specific Code

Files with `win`, `Win`, `windows` in the filename are automatically excluded.
Some code may have `#ifdef WIN32` blocks that need manual review.

### Boost Compatibility

The vendored Boost version is old (~1.39) and has incompatibilities with GCC 15.
The build uses `-fpermissive` to work around some issues.

## Build Output

```
build_linux/
├── libace_vendor.a          # ~30MB, ACE 5.x
├── libterabit.a             # ~5MB, Terabit/TProactor
├── libmongoose.a            # ~100KB
├── libwsdlpull.a            # ~1.5MB
├── libjsoncpp_vendor.a      # ~100KB
├── libtinyxml_vendor.a      # ~50KB
├── libmd4_vendor.a          # ~10KB
├── libpf_gamelogic.a        # ~20MB, PF_GameLogic
└── UniServerApp             # ~30MB, final executable
```

## Troubleshooting

### "Cannot find header X"

Check if the include path is in `COMMON_INCLUDES` in CMakeLists.txt.

### "Undefined reference to ACE::..."

Make sure `ace_vendor` is linked. Check that the ACE build succeeded.

### "ACE_RCSID macro error"

The macro is defined as no-op in config-macros.h when `ACE_USE_RCSID=0`.

### Thrift compatibility errors

The `thrift_compat/cxxfunctional.h` shim provides `apache::thrift::stdcxx::function` etc.
Make sure the include path includes `thrift_compat/`.
