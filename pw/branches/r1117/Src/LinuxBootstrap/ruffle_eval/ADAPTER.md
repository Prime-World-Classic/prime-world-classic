# Native Adapter Checkpoint

The prototype now provides a native C++-callable library and an **opt-in Linux
combat inspection path**. It is not the default Flash backend. The default
Tamarin/OpenGL client and Windows/DirectX projects remain unchanged. No Wine or
browser is involved. Windows was not built on this host.

This feature checkpoint is tagged `linux-native-v0.16.0`. Both Linux CMake projects
read the port release version from [VERSION](../VERSION). This does not change
the game's network/replay version, the pinned Ruffle revision, or C ABI v1.

## Implemented Boundary

### Native Action Controls (0.16.0)

The opt-in client routes top-row `1` through `0` to the authored action bar's ten
talent slots. It queries `GetTalentActionBarIndex` before `mainInterface.UseSlot`;
empty, inventory and portal slots are not guessed. The resulting original
callback still passes live gameplay validation. Held/repeated/blocked keys cannot
become fresh actions until release; captured edges never fall through to legacy
world controls. Shift/Ctrl/Alt/Super chords, lost focus, stale viewport, authored
modal/chat state and focused text fields are inert. Chat text/IME, rebinding,
inventory and portal hotkeys remain outside this path.

The additive `focus_state` JSON request returns `{"text":bool}` from Ruffle's
actual focus tracker without retaining an object handle. Rebuild the explicit
DSO with this checkpoint before using shortcuts. Its C ABI version is unchanged.
Only opt-in builds normalize X11 release/press autorepeat pairs and preserve
modifier metadata; the default Linux input behavior is unchanged. Native Escape
(`27`) and synthetic X11 Escape both cancel targeting. Pending talents receive
the original SWF's `Chosen` status, cleared after cast, cancellation or invalidation.

### Surface Transport (0.3.0)

The additive ABI v1 export `pw_ruffle_render` returns owned top-down RGBA8 with
straight alpha and no row padding. Free `frame.rgba` with the existing buffer
function on all return paths; a successful all-transparent frame is valid.
Rendering does not advance time. The `surface` JSON action accepts integer
`width`, `height`, and a `transparent` boolean, capped at 4096 per dimension and
8,388,608 pixels (32 MiB). Invalid requests leave the surface unchanged.
The default remains opaque 1280x720. This transport uses native GPU readback,
not shared GL textures or a zero-copy performance claim. Older ABI v1 libraries
can lack the new symbol; an embedding consumer must check its presence.

The C++ probe checks three sizes, including an odd-width frame, alpha coverage,
buffer ownership, invalid resize recovery, and the unchanged loading/combat
contract. Before implementation the first resize failed with Unknown host action.

### Input And Time (0.4.0)

`{"action":"tick","delta_ms":16.0}` advances the autoplay Player through Ruffle's
existing timer/frame scheduler; finite slices must be between 0 and 250 ms.
Rendering remains independent. `{"action":"input","event":{...}}` fully validates
an event before dispatch. [input.rs](input.rs) documents exact mouse, wheel,
key, Unicode-text, and focus schemas. Coordinates are viewport pixels.
The host tracks at most 128 held keys and releases held controls outside the
movie on focus loss, then clears focus. Unknown/oversized events fail explicitly.
The real combat chat field receives supplementary Unicode and stops accepting
text after blur. This does not yet provide IME, clipboard, text-edit commands,
layout-specific physical-key mapping, or a native game-window event adapter.

### Dynamic Bitmaps (0.5.0)

`bitmap_create` with integer width/height returns a rooted transparent BitmapData
handle. The additive `pw_ruffle_bitmap_upload` export accepts a complete, borrowed,
tightly packed straight-alpha RGBA image; dimensions must match and be 1..2048.
Pixels are copied/premultiplied once and Ruffle's CPU/GPU/display caches are marked
dirty without replacing the BitmapData identity. Pass the handle through the
existing object-argument API and release it normally. Display objects can retain
their own references after the host releases a handle. Bounds are per bitmap,
not a complete VM/GPU memory budget. There is no partial-region API yet.

The C++ loading test attaches an 8x8 host bitmap to the actual SWF, checks
getPixel32 channel/alpha results and changed rendered pixels after a second
upload, then restores the original asset. Malformed byte counts, mismatched
dimensions, non-bitmap, disposed and released handles fail. This enables texture
transport; the game's live minimap producer is not connected yet.

### Native GL Composition (0.6.0)

[native_host.h](native_host.h) loads an explicit absolute DSO path and checks ABI
and required exports. Ruffle uses its own EGL context while the caller's GLX
context is detached; context/draw/read drawable and EGL API selection are
restored before compositing or returning. Caller EGL contexts are rejected.
All calls and teardown stay on one thread; reset before destroying the original
compatibility context. A reported Rust panic poisons the host until reset.
Optional builds discover system EGL through `pkg-config egl`, avoiding unrelated
application-bundled implementations. A teardown failure retains the loader
reference instead of unloading code potentially still in use.

[gl_compositor.h](gl_compositor.h) documents supported state and caller limits.
It uploads top-down straight-alpha RGBA into one reusable texture and composites
with source-over alpha while restoring legacy GL state. No GPU object is shared
with Ruffle. It does not swap, interpret FSCommands, or feed live game data.
The lifecycle rules follow the [GLX specification](https://registry.khronos.org/OpenGL/specs/gl/glx1.4.pdf)
and [EGL specification](https://registry.khronos.org/EGL/specs/eglspec.1.4.withchanges.pdf).

The mock compositor probe checks orientation, alpha, updates/resizing, malformed
inputs, full matrix stacks, shaders, multitexture state, matrices, masks, and PBO/
pixel-store restoration. The real combat-SWF probe checks three viewport sizes
across two complete load/draw/close cycles, transparent background preservation,
nonblank authored UI, GLX/EGL restoration, invalid-request recovery, and zero
runtime errors. Combat startup is advanced three frames before first presentation.

### Authored Callback Transport (0.12.0)

`gameplay_events.h` decodes the shipped talent and minimap FSCommands with the
existing nlohmann JSON parser and strict bounded scalar parsing. Coordinates and
row/column identity are preserved. Unknown and informational callbacks are inert;
malformed batches and queue overflow fail atomically. The actionable queue is
capped at 1024 and drained exactly once outside rendering. Blur, teardown and
failure clear pending requests. This checkpoint counts requests but does not yet
execute gameplay commands. The real-SWF probe clicks an authored shortcut and
verifies its exact talent coordinates and empty second drain.

### Local Talent Commands (0.13.0)

Add `--bootstrap-interactive-world` to disable automated gameplay proof commands
and permit manual native input. With both Ruffle opt-ins, exact authored talent
clicks now submit normal purchase or immediate-use commands through the client
scheduler and replay writer. Live ownership, session readiness, focus, control
flags and talent availability are rechecked; spectator/replay requests are inert.
Duplicate requests for the same talent in one world step are suppressed. Uses
carry `issuedByScript=false`. Targetless/toggle/current attack-target casts add
client-side visibility/type/faction/range/limitation checks; talents needing manual targeting
are rejected and counted until the two-step targeting flow is implemented.
The native command implementation still differs from Windows (including immediate
casts rather than the full approach/cast state machine); do not infer command parity.

Linux map loading now allocates authored starting prime as on Windows. Live prime
and talent states update after execution, without proof-only currency grants.
Paid currency is zero in this offline bootstrap, which has no account/payment
service; that is not a live-account balance binding. Hidden compatibility HUD and
minimap hit regions no longer intercept world clicks while Ruffle is active.

### Local Minimap Commands (0.14.0)

Interactive sessions project the original minimap's normalized coordinates using
the last composed bitmap's bounds and north-up orientation. Right press submits
one ground move; left press/drag repositions the native camera. Outside-circle,
invalid, repeated or canceled gestures cannot acquire another destination. Focus,
resize and teardown publish an input epoch so native gesture state resets with
the callback queue. Pointer leave preserves completed prior-frame callbacks, then
cancels held gestures; focus loss still discards pending requests. Signals,
camera-lock toggle and minimap ability targeting are
not connected. Interactive mode retains manual rotation/zoom but disables the
automatic rotating-map preview. Scripted native checks accept `x,y:right`.

The shipped ActionScript expects Tamarin's `mouseRightDown`, `mouseRightUp` and
`clickRight`, not standard Flash's right-button names. `mouse_events.patch` adds
the **default-OFF** Cargo feature `primeworld_mouse_events` to the pinned Ruffle
checkout. Build the Prime World DSO with it enabled; standard builds retain their
original event names. The client checks the explicit capability in `stats` and
falls back with a diagnostic when an older/noncompatible DSO is supplied. No game
SWF or Windows event implementation is rewritten; this is not double-right-click
support or a general Ruffle event renaming change.

### Ground Talent Targeting (0.15.0)

An eligible authored talent click can arm its exact hero/row/column. A fresh world
left press selects a ground position and submits through the existing transceiver;
invalid or out-of-range points keep the selection. Right press or Escape cancels
without issuing a move. Captured releases remain consumed after casting or canceling.
Pending ownership is revalidated on each frame; hero, focus, viewport or session
changes invalidate it. Pointer leave alone does not erase a selected talent.

This path only supports standard `SPELLTARGET_LAND` talents. Unit picking, line of
sight and alternative-target semantics remain unsupported; Linux visibility methods
still contain stubs and must not be treated as proof of those rules. Projection uses
the actual window dimensions and rejects off-map points instead of clamping them.
Terrain-height intersection and automatic approach-to-cast remain unimplemented.
The manual human ground command rechecks live ownership, activation/resources,
target bounds and range at execution; scripted/bot commands retain their legacy path.

### Profiling (0.14.1)

`PrimeWorldRuffleNativeHostProbe LIBRARY DATA COMBAT_SWF --benchmark` measures
60 original-SWF draws at 1280x720 after five warmup frames. JSON output reports
average wall milliseconds for VM tick, render/readback, composition and alpha
coverage, plus the actual GL renderer. It verifies context restoration, runtime
errors and counter behavior without hardware-specific performance assertions.
This isolated HUD does not populate the full match's talents, heroes or minimap;
its draw time is not a whole-game FPS claim.

The client also records cumulative stages in `finalRuffleTimingMs`, plus total
request and bitmap-upload costs. `requestInclusive` includes tick and resize
requests, so it must not be added to the draw-stage sum without subtracting that
overlap. Failed calls do not count as successful work; counters survive teardown.
Setup, world rendering, frame pacing and other client work are not draw timings.
`finalClientTimingMs` separately measures disjoint input, update/UI, dynamic asset
discovery and drawing/swap stages across the complete native main loop. Its draw
stage includes the Ruffle work above; explicit frame sleep and pre-loop startup
are excluded. Compare the same map, viewport, script and build configuration.

### Optimized Native Builds (0.14.2)

Use the existing compiler optimization paths before changing GPU transport:

```sh
export CARGO_HOME=/tmp/pw-ruffle-cargo
export CARGO_TARGET_DIR=/tmp/pw-ruffle-target
bash pw/branches/r1117/Src/LinuxBootstrap/ruffle_eval/build_native.sh /tmp/pw-ruffle-surface-final 4
```

The script validates the pinned checkout using `prepare.py`, then builds offline
with locked dependencies and the Prime World mouse feature. It explicitly uses
release optimization with `panic=unwind` and overflow checks. Upstream's default
release `panic=abort` bypasses the C ABI panic boundary and is not this preset.
The resulting library is `$CARGO_TARGET_DIR/release/examples/libpw_bridge.so`.
Existing debug libraries remain usable for comparison; the script never downloads
sources or changes the default client backend. Mock-tool tests pin its arguments,
environment overrides, invalid-job rejection and preparation-failure behavior.

Linux CMake's `PW_LINUX_OPTIMIZE_PRESENTATION` defaults ON and applies `-O2` only
to the native bootstrap presentation and isolated C++ Ruffle host. It does not
define `NDEBUG`, change Windows projects, or optimize the legacy world closure.
Set it OFF for unoptimized debugging/comparisons. Ruffle inspection itself remains
default-OFF. Use the release DSO explicitly when launching an inspection client.

### Client Inspection (0.14.0)

The Linux CMake option `PW_LINUX_RUFFLE_INSPECTION` defaults to OFF. When enabled,
`--bootstrap-ruffle-library /absolute/path/libpw_bridge.so` requests the original
combat SWF over the existing native 3D world. Both opt-ins are required. The DSO
is loaded at runtime, not linked or downloaded by the client build. Without the
argument, the existing Linux path remains active and no Ruffle host is opened.

The inspection initializes localization/window visibility, advances the startup
timeline, follows viewport size, clamps frame time, and reports frames/errors in
`finalRuffleInspection`. Initialization/render/runtime failure disables inspection
and retains the existing HUD fallback. Only the allowlisted talent/minimap commands
above execute in interactive mode. Mouse motion/buttons/wheel and focus reach
Ruffle before native world controls; keyboard/text input remains unbound.
Live local-hero identity, the original portrait and level/health/energy/regen values
are bound through the authored methods. Plain engine-independent snapshots are
validated and only changed values are sent. A different hero identity disables
this session's inspection instead of mixing players or appending force tables.
Rank/premium/flag decorations and custom-energy colors remain unbound. Audio and
most gameplay action bindings are not yet connected. Actual talent icons,
purchase state, resource restrictions and cooldowns now populate the authored grid
and action bar. Initial purchased shortcuts use the authored prerequisite state
transition; updates do not recreate existing shortcuts. Loadout replacement/respec
and inventory/portal/global cooldown remain unbound. This flag remains
for integration inspection, not playing a match.

Pointer capture uses the last composed frame's nonzero alpha coverage and the
authored invisible escape-menu shield, not a replacement layout or a Flash-object
hit test. Each button keeps its initial HUD/world owner until release, so dragging
between them cannot turn into a second command. Focus loss releases held VM input.
Resize discards stale coverage and consumes queued clicks/wheel until a matching
frame exists. Failure disables inspection and retains the existing HUD. Transparent
interactive regions other than the known modal shield and same-frame layout changes
still need object-level hit testing; keyboard/text/IME and remaining gameplay
FSCommands need separate integration. The pure capture probe covers all three buttons and mixed
ownership. The real-SWF probe opens/closes the original talent window and checks
HUD/world routing, focus reset and repeated resize events. A
60-second client run also opens that window through its normal raw-input queue.

The original minimap now receives a retained 270x270 BitmapData with native map
artwork, visible living world markers and the simulation clock. Uploads are skipped
when pixels are unchanged. The shipped SWF lacks the circular mask present in the
available ActionScript source, so the adapter applies that documented circle to
the bitmap alpha before upload. Background and markers use the existing terrain
preview's meter dimensions, now consistent with the corrected Linux world grid.
This is a north-up inspection map: primitive markers, no nature blending, explored
fog texture, last-seen state, authored map offset/rotation, or camera footprint.

The recurring engine GL error was fixed in 0.7.1: hero materials now upload before
opening a triangle batch. Cold/warm two-material pixel regressions cover it.
Inspection still counts any prior engine flags separately as `priorGlErrors`;
errors during drawing or state restoration fail inspection. The native probe
injects a prior error and checks reporting/recovery and missing-library fallback.

From the repository root, after building the adapter above:

```sh
ROOT="$PWD"
cmake -S "$ROOT/pw/branches/r1117/Src/LinuxBootstrap" -B /tmp/primeworld-linux-bootstrap -DPW_LINUX_RUFFLE_INSPECTION=ON
cmake --build /tmp/primeworld-linux-bootstrap --target PrimeWorldLinuxClient --parallel 4
cd "$ROOT/pw/branches/r1117/Bin"
/tmp/primeworld-linux-bootstrap/PrimeWorldLinuxClient --seconds 60 --width 1280 --height 720 --bootstrap-create-game --bootstrap-interactive-world --bootstrap-ruffle-library /tmp/pw-ruffle-target/debug/examples/libpw_bridge.so
```

Omit the library argument for ordinary startup, or configure the option OFF to
remove the adapter and its EGL/nlohmann build requirements altogether. A missing
library produces a diagnostic and the existing HUD, not a silent backend switch.

### Core Adapter Contract

- Native `:/...` game-root and movie-relative image paths, normalized and confined
  to the canonical Data tree. Symlink/parent escapes, URLs, directories, malformed
  files, unsupported formats, and oversized images fail explicitly.
- Original DDS and PNG decoding without Python, subprocesses, file conversion,
  or SWF changes. `dds = 0.2.0` handles compressed and uncompressed legacy DDS;
  `image` handles PNG. DX10 alpha modes other than Unknown/Straight are rejected
  rather than silently double-premultiplied. Legacy DXT2/4 normalization belongs
  to the decoder.
- At most 32 MiB encoded input, 8192 per dimension, 64 MiB RGBA per image, and
  128 MiB/512 entries in the decoded cache. These bounds do not include every
  AVM2 object or GPU texture allocation. This is a trusted game-data provider,
  not a race-proof filesystem sandbox.
- Opt-in synchronous `Loader.load` bitmap construction, native pixel ownership,
  and Ruffle INIT/COMPLETE dispatch. Other navigators keep stock behavior. Other
  fetches, navigation, and sockets are disabled in the game-image navigator.
- Application domains are registered before callbacks, including an explicit
  LoaderContext domain. Temporary rooted operation guards prevent stale loads
  and recursive unloads during callbacks. Missing, rejected, or corrupt images
  dispatch IO_ERROR instead of leaking an uncatchable Rust error into ActionScript.
- GC-rooted object handles with bounded capacity, permanent Player/root-domain
  ownership, independent retains, release/clear/drop, and never-reused u64 IDs.
  Only integers/owned data cross the native boundary, not Tamarin Atoms or GC pointers.
- Experimental C ABI v1: create, execute JSON requests, poll callbacks, close,
  and free owned output buffers. Hosts are thread-confined; cross-thread/stale
  IDs fail without dereferencing caller-provided object addresses. Rust panics
  are contained; callers must close a host after PANIC.

The [ABI header](bridge.h) is the pointer/ownership contract. JSON actions are
`invoke` (default), `release`, `clear`, `step`, `tick`, `input`, `surface`,
`bitmap_create`, `events`, `stats`, and `capture`.
Invocation `op` is `call`, `get`, or `set`. A root `path` or a retained `receiver`
selects the object. Arguments tagged `{"$handle":"123"}` pass a retained object;
other arguments are ordinary JSON data. Handle IDs in JSON must be decimal
strings, never floating point. Each request executes exactly once and returns
one fresh buffer; there is no sizing retry that could duplicate side effects.

## Measured Evidence

- The original DDS background/logo now load before positioning. All 19 loading
  checks pass without intermediate frames, including dimensions, all four
  centered positions, repeat load, unload, and reload. The background is at
  `(-320,-180)` and logo at `(295,200)` for a 1280x720 viewport. The previous
  asynchronous experiment left both at `(640,360)`.
- Eight handle tests force collection, confirm retained objects survive and are
  collectible after release, and cover stale IDs, capacity, exhaustion,
  cross-store/root/Player access, and replacement Players.
- A live combat-SWF test retains an action-bar item across 120 frames, changes
  and reads its visibility, passes it as a `contains()` argument, releases it,
  rejects subsequent access, and verifies drained callbacks do not repeat.
- A standalone C++17 consumer built with warnings treated as errors calls the
  library for both shipped movies, checks scalar/object returns and lifecycle,
  and captures nonblank native GL framebuffers. The C ABI has a separate
  buffer/input/panic-boundary unit test.
- Seven native asset tests cover PNG alpha/cache/path behavior, DXT1,
  uncompressed BGRA, malicious dimensions, unsupported premultiplied DX10, and
  many-small-image cache growth. The last three tests reproduced failures before
  their fixes; the original BGRA test also reproduced the image decoder gap.
- Ten headless Loader tests generate mock SWF/ABC listeners through Ruffle's own
  writer and execute real AVM2 callbacks. They cover applicationDomain inside
  INIT, LoaderContext, IO_ERROR recovery, nested load/unload during added,
  removed, and INIT, independent Loaders, and temporary root cleanup. Pre-fix
  runs reproduced a domain panic and five failing lifecycle/error tests.
- The clean integrated core suite passes all 153 tests, along with two standalone-
  host tests, 20 ABI/runtime/input tests, and 13 Python preparation/fixture/harness
  tests. Both prepare runs are idempotent; all three native examples build offline
  with the lockfile. The client and its maintained probes build; all seven
  headless client CTests pass with allocator perturbation and the C locale.

The [port plan](../LINUX_PORT_PLAN.md) records final regression results and
temporary log/capture locations. Temporary files under `/tmp` are not durable.
The source pin remains `1b24dd3a6925eecdd1d7fa49e165814e7ed2163d`
(`nightly-2026-02-13`); these results do not claim compatibility with newer Ruffle APIs.

## Reproduce From A Clean Checkout

Use the requirements in [README](README.md), plus a C++17 compiler, CMake,
pkg-config, X11/OpenGL/EGL development libraries, and
[nlohmann/json](https://github.com/nlohmann/json) headers for the C++ probe.
The decoder API is documented at [dds 0.2.0](https://docs.rs/dds/0.2.0/dds/).
The Rust source, exact dependency checksums, and integration changes are staged
by `prepare.py`; local conflicting edits are refused, not overwritten. Use a
fresh checkout when updating from the older experiment.

```sh
EVAL="$PWD/pw/branches/r1117/Src/LinuxBootstrap/ruffle_eval"
DATA="$PWD/pw/branches/r1117/Data"
RUFFLE=/tmp/pw-ruffle-native-checkout
git clone --depth 1 --branch nightly-2026-02-13 https://github.com/ruffle-rs/ruffle.git "$RUFFLE"
python3 "$EVAL/prepare.py" "$RUFFLE" --user-input
export CARGO_HOME=/tmp/pw-ruffle-cargo
export CARGO_TARGET_DIR=/tmp/pw-ruffle-target
export CARGO_PROFILE_DEV_DEBUG=0
cargo test --manifest-path "$RUFFLE/Cargo.toml" --locked -p ruffle_core --lib --features default_font,primeworld_mouse_events -j4
cargo test --manifest-path "$RUFFLE/Cargo.toml" --locked -p ruffle_core --example primeworld --features default_font,primeworld_mouse_events -j4
cargo test --manifest-path "$RUFFLE/Cargo.toml" --locked -p ruffle_core --example pw_bridge --features default_font,primeworld_mouse_events -j4
cargo build --manifest-path "$RUFFLE/Cargo.toml" --locked -p ruffle_core --example primeworld --example handles_probe --example pw_bridge --features default_font,primeworld_mouse_events -j4
timeout 60 "$CARGO_TARGET_DIR/debug/examples/primeworld" "$DATA/UI/Screens/Loading/Flash/pwl.swf" "$EVAL/native_loading_calls.json" /tmp/pw-native-loading.png "$DATA"
timeout 60 "$CARGO_TARGET_DIR/debug/examples/handles_probe" "$DATA" "$DATA/UI/Screens/Combat/Flash/main.swf"
cmake -S "$EVAL" -B /tmp/pw-ruffle-cpp -DPW_RUFFLE_LIBRARY="$CARGO_TARGET_DIR/debug/examples/libpw_bridge.so"
cmake --build /tmp/pw-ruffle-cpp --parallel 2
timeout 90 /tmp/pw-ruffle-cpp/PrimeWorldRuffleBridgeProbe "$DATA" "$DATA/UI/Screens/Loading/Flash/pwl.swf" /tmp/pw-cpp-loading.png loading
timeout 90 /tmp/pw-ruffle-cpp/PrimeWorldRuffleBridgeProbe "$DATA" "$DATA/UI/Screens/Combat/Flash/main.swf" /tmp/pw-cpp-combat.png combat
timeout 60 /tmp/pw-ruffle-cpp/PrimeWorldRuffleGlCompositorProbe
timeout 90 /tmp/pw-ruffle-cpp/PrimeWorldRuffleNativeHostProbe "$CARGO_TARGET_DIR/debug/examples/libpw_bridge.so" "$DATA" "$DATA/UI/Screens/Combat/Flash/main.swf"
```

These commands need native GPU/display access. C++ probes are deliberately not
part of default headless CTest discovery. The Python preparation/fixture tests
in README remain headless. No server is needed for any of these probes.

## Not Yet Production

The `userInput` shim remains incomplete: standard `condenseWhite` is not PW's
ordinary-text markup, punctuation handling, or reflow. Bundled fallback fonts
also do not prove Windows font-metric parity. The Rust host renders an
isolated resizable offscreen framebuffer. GLX composition/state restoration and
opt-in client inspection now bind hero/talent/prime snapshots, native pointer input,
the minimap bitmap and selected gameplay commands. Keyboard/text/IME, audio,
inventory/portal/global cooldown and two-step ability targeting remain unbound.
Purchases and minimap moves use the local transceiver; this is not full Windows
command parity, online-match integration, or a shared-texture performance path.

Keep further client integration opt-in and Linux-only until fidelity and live
bindings have evidence. Keep the original Windows runtime and run a Windows
build gate before changing shared client interfaces. This batch does not claim
a playable game, a stable upstream C ABI, or complete Windows visual parity.
