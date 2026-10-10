# muParser Numeric Sidecar Dependency

- Upstream: https://github.com/beltoforion/muparser
- Official release: https://github.com/beltoforion/muparser/releases/tag/v2.3.5
- Tag: `v2.3.5`
- Commit: `fbafd7f8774af2b53f4d2de07c57353fcfc09216`
- Tag verification: https://api.github.com/repos/beltoforion/muparser/git/ref/tags/v2.3.5
- Archive: https://codeload.github.com/beltoforion/muparser/tar.gz/refs/tags/v2.3.5
- Archive SHA-256: `20b43cc68c655665db83711906f01b20c51909368973116dfc8d7b3c4ddb5dd4`
- Retrieved: 2026-10-11
- License: BSD-2-Clause; the upstream `LICENSE` and file notices are retained.

The ten headers in `include/`, six translation units in `src/`, and `LICENSE`
are byte-for-byte upstream files extracted from that archive. No patches,
formatting changes, build-time downloads, installed muParser dependency or
upstream build scripts are used. DLL bindings, integer parser, upstream test
runner, samples and documentation are omitted. All six vendored `.cpp` files
are required; the include directory must be on the compiler search path.

This dependency is used only by `../../formula_numeric.cpp`. The Linux CMake
target `PrimeWorldLinuxNumericFormula` isolates it from engine headers/flags;
`LinuxNumericFormula` runs its headless tests. Build with C++17, exceptions and
`MUPARSER_STATIC`, without OpenMP or fast-math. Upstream sources remain in their
original encoding/formatting; locally authored files use ASCII and tabs.

## Standalone Verification

Run from `pw/branches/r1117/Src/LinuxBootstrap`. These commands do not configure
or build the parent client; all generated artifacts go to `/tmp`.

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -DMUPARSER_STATIC -pthread \
	-Ithird_party/muparser/include formula_numeric.cpp formula_numeric_probe.cpp \
	third_party/muparser/src/*.cpp -o /tmp/pw-formula-numeric-probe
/tmp/pw-formula-numeric-probe

clang++ -std=c++17 -O1 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=all -DMUPARSER_STATIC -pthread \
	-Ithird_party/muparser/include formula_numeric.cpp formula_numeric_probe.cpp \
	third_party/muparser/src/*.cpp -o /tmp/pw-formula-numeric-probe-sanitize
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 \
	/tmp/pw-formula-numeric-probe-sanitize
```

The public header documents the deliberately limited numeric semantics. In
particular, arithmetic is double-backed floating point, not C++ integer
arithmetic or guaranteed bit-identical Windows formula execution. Decimal
`f/F` literals round to float before evaluation. Resolver values are lazy and
local to one evaluation. The adapter disables default muParser functions,
constants and binary operators and registers only the documented operator set.
It does not translate the full game formula language.

API references: [muParser interface](https://beltoforion.de/en/muparser/interface.php),
[custom callbacks](https://beltoforion.de/en/muparser/customizing_muparser.php).
The vendored v2.3.5 source, rather than the latest online documentation, is the
authority for the API actually compiled here.

## Recorded Verification

On 2026-10-11, the standalone probe passed 8,721 checks with GCC 15.3.0 and
Clang 23.1.1 ASan/UBSan. Leak detection passed when run outside the sandbox;
the sandbox's ptrace restriction prevents LeakSanitizer from completing.
The probe includes the actual native Plane A1 expression with mock stats,
4,000 deterministic token-corpus cases, lazy branches, failures and concurrent
and recursive calls. GCC coverage for `formula_numeric.cpp` was 98.09% of 157
executable lines. All 17 vendored upstream files were compared byte-for-byte
against the archive with `cmp` and matched. No client or parent CMake build
was run.
