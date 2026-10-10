# Native Linux Client

This is the native Linux/OpenGL client port, not a Linux server or Wine wrapper.
Windows implementation branches and projects remain in place. A complete playable
match and Windows build validation are still outstanding.

See [the port plan](LINUX_PORT_PLAN.md) for verified checkpoints, evidence and
remaining work. [VERSION](VERSION) identifies this port checkpoint independently
of the shared network/replay version.

## Default Build

Requirements: a C++17 toolchain, CMake, pthreads, and development files for X11,
OpenGL, FreeType, JPEG, PNG and zlib. Runtime requires an accessible X11 display,
an OpenGL driver and the repository's game Data directory. The checked formula
parser is vendored; see its [provenance and license](third_party/muparser/PROVENANCE.md).

From the repository root:

```sh
ROOT="$(git rev-parse --show-toplevel)"
PORT="$ROOT/pw/branches/r1117/Src/LinuxBootstrap"
BUILD=/tmp/primeworld-linux-bootstrap
cmake -S "$PORT" -B "$BUILD" -DPW_LINUX_RUFFLE_INSPECTION=OFF
cmake --build "$BUILD" --target PrimeWorldLinuxClient PrimeWorldLinuxSwfProbe \
	PrimeWorldLinuxRenderProbe PrimeWorldLinuxClientRuntimeProbe \
	PrimeWorldLinuxGameplayProbe PrimeWorldLinuxPresentationProbe -j4
env LC_ALL=C MALLOC_PERTURB_=165 ctest --test-dir "$BUILD" --output-on-failure
```

Several maintained probes are compile-only object targets, not runnable programs.
The client CTests include actual engine formula, resource, cooldown, world-grid
and fog-observer fixtures. Passing them does not establish a complete match.

Run from the game Bin directory so asset paths resolve:

```sh
cd "$ROOT/pw/branches/r1117/Bin"
"$BUILD/PrimeWorldLinuxClient" --seconds 300 --width 1280 --height 720 \
	--bootstrap-create-game --bootstrap-interactive-world
```

The default HUD is a compatibility presentation. Omitting
`--bootstrap-interactive-world` enables the automatic gameplay proof sequence;
it is not a manually played session. Do not run full clients concurrently because
they share replay output paths.

Known automatic-proof limitation at this checkpoint: the finite smoke run can
freeze its world-step consumer while the scheduler continues recording commands.
The replay evidence gate correctly rejects that mismatch. Use the interactive
command above for manual testing; this does not claim the capped smoke is fixed.

## Original Combat HUD

The authored SWF HUD uses the opt-in native Ruffle adapter. It is not a browser
overlay and does not require Wine. Keep a separate build directory to avoid
repeatedly rebuilding the default client when changing the option:

```sh
RUFFLE_BUILD=/tmp/primeworld-linux-ruffle
cmake -S "$PORT" -B "$RUFFLE_BUILD" -DPW_LINUX_RUFFLE_INSPECTION=ON
cmake --build "$RUFFLE_BUILD" --target PrimeWorldLinuxClient -j4
```

Build the pinned adapter with [build_native.sh](ruffle_eval/build_native.sh),
using its prepared Ruffle checkout and cached Cargo dependencies. That script
enables the required mouse-event compatibility feature and Rust FFI unwind
guards. The [Ruffle evaluation notes](ruffle_eval/README.md) document source
preparation and historical compatibility experiments; an arbitrary upstream
Ruffle library is not a substitute for this adapter.

Set `RUFFLE_DSO` to the absolute path of the resulting `libpw_bridge.so`, then:

```sh
cd "$ROOT/pw/branches/r1117/Bin"
"$RUFFLE_BUILD/PrimeWorldLinuxClient" --seconds 300 --width 1280 --height 720 \
	--bootstrap-create-game --bootstrap-interactive-world \
	--bootstrap-ruffle-library "$RUFFLE_DSO"
```

## Native Acceptance

The [native controls driver](ruffle_eval/native_controls_probe.py) requires
`xdotool`, the foreground 1280x720 client window and fresh output paths. Do not
interact with the window during this automated test. It sends window-addressed
events, checks focus and never installs a global held key.

```sh
python3 -B "$PORT/ruffle_eval/native_controls_probe.py" \
	--binary "$RUFFLE_BUILD/PrimeWorldLinuxClient" --library "$RUFFLE_DSO" \
	--bin-dir "$ROOT/pw/branches/r1117/Bin" --output /tmp/pw-native-check.log \
	--seconds 90 --control-delay 50 --min-cast-step 650
```

The driver requires successful startup effect fixtures, authored talent purchase,
off-map and out-of-range rejection, cancellation, an executed ground cast,
range 14, a 70-mana deduction, continued world stepping and replay consistency.
The [offline evidence validator](ruffle_eval/gameplay_gate.py) rejects incomplete
or duplicated final records; wait for the process to exit before validating logs.

## Remaining Boundaries

- Rendering uses real assets but still has simplified materials and presentation.
- Numeric formulas support a bounded arithmetic/conditional subset and four live
	unit properties. Unsupported abilities remain unavailable, not silently free.
- Fog observers now maintain simulation visibility data. Unit visibility queries,
	stealth/true-sight and rendered fog are not yet restored together.
- Production damage formulas, complete casting/movement behavior, remaining HUD
	actions, online integration and a complete local match are not acceptance claims.
- Linux source guards preserve the Windows path, but an actual Windows build and
	runtime regression run are still required.
