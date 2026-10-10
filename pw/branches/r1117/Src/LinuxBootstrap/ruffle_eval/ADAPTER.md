# Native Adapter Checkpoint

The second five-chunk batch advances the isolated Ruffle prototype into a native
C++-callable library. **It is not linked into PrimeWorldLinuxClient.** The default
Tamarin/OpenGL client and Windows/DirectX projects remain unchanged. No Wine or
browser is involved. Windows was not built on this host.

This feature checkpoint is tagged `linux-native-v0.2.0`. Both Linux CMake projects
read the port release version from [VERSION](../VERSION). This does not change
the game's network/replay version, the pinned Ruffle revision, or C ABI v1.

## Implemented Boundary

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
`invoke` (default), `release`, `clear`, `step`, `events`, `stats`, and `capture`.
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
- The integrated core suite passes all 152 tests, along with two standalone-host
  unit tests, one ABI unit test, and 13 Python preparation/fixture/harness tests.

The [port plan](../LINUX_PORT_PLAN.md) records final regression results and
temporary log/capture locations. Temporary files under `/tmp` are not durable.
The source pin remains `1b24dd3a6925eecdd1d7fa49e165814e7ed2163d`
(`nightly-2026-02-13`); these results do not claim compatibility with newer Ruffle APIs.

## Reproduce From A Clean Checkout

Use the requirements in [README](README.md), plus a C++17 compiler, CMake, and
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
cargo test --manifest-path "$RUFFLE/Cargo.toml" --locked -p ruffle_core --lib --features default_font -j4
cargo test --manifest-path "$RUFFLE/Cargo.toml" --locked -p ruffle_core --example primeworld --features default_font -j4
cargo test --manifest-path "$RUFFLE/Cargo.toml" --locked -p ruffle_core --example pw_bridge --features default_font -j4
cargo build --manifest-path "$RUFFLE/Cargo.toml" --locked -p ruffle_core --example primeworld --example handles_probe --example pw_bridge --features default_font -j4
timeout 60 "$CARGO_TARGET_DIR/debug/examples/primeworld" "$DATA/UI/Screens/Loading/Flash/pwl.swf" "$EVAL/native_loading_calls.json" /tmp/pw-native-loading.png "$DATA"
timeout 60 "$CARGO_TARGET_DIR/debug/examples/handles_probe" "$DATA" "$DATA/UI/Screens/Combat/Flash/main.swf"
cmake -S "$EVAL" -B /tmp/pw-ruffle-cpp -DPW_RUFFLE_LIBRARY="$CARGO_TARGET_DIR/debug/examples/libpw_bridge.so"
cmake --build /tmp/pw-ruffle-cpp --parallel 2
timeout 90 /tmp/pw-ruffle-cpp/PrimeWorldRuffleBridgeProbe "$DATA" "$DATA/UI/Screens/Loading/Flash/pwl.swf" /tmp/pw-cpp-loading.png loading
timeout 90 /tmp/pw-ruffle-cpp/PrimeWorldRuffleBridgeProbe "$DATA" "$DATA/UI/Screens/Combat/Flash/main.swf" /tmp/pw-cpp-combat.png combat
```

These commands need native GPU/display access. C++ probes are deliberately not
part of default headless CTest discovery. The Python preparation/fixture tests
in README remain headless. No server is needed for any of these probes.

## Not Yet Production

The `userInput` shim remains incomplete: standard `condenseWhite` is not PW's
ordinary-text markup, punctuation handling, or reflow. Bundled fallback fonts
also do not prove Windows font-metric parity. The Rust host renders only an
isolated fixed-size offscreen framebuffer; live engine compositing, renderer
state restoration across both engines, dynamic minimap texture upload, input,
focus, audio events, resize, and live HUD/game-state binding are not implemented
by ABI v1. Callback polling proves transport, not execution of gameplay commands.

The next client integration should be opt-in and Linux-only, after these gates
have evidence. Keep the original Windows runtime available and run a Windows
build gate before changing shared client interfaces. This batch does not claim
a playable game, a stable upstream C ABI, or complete Windows visual parity.
