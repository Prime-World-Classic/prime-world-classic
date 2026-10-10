# Native Ruffle Evaluation

This is a compatibility experiment with opt-in native client inspection,
**not the default Flash backend**. The Linux client normally uses Tamarin/OpenGL. Windows project
files and the DirectX path are unchanged. No Wine, browser, or shipped-SWF edits
are involved. The Windows build was not run on this Linux host.

## Decision

The follow-up adapter now provides synchronous native DDS loading, rooted object
handles, frame/input/bitmap transport, and optional GLX composition through an
experimental C ABI. See [adapter status and
reproduction](ADAPTER.md) for the current implementation. The original evaluation
results below are retained as before/after evidence, not current blockers for the
explicit native-image mode.

Continue with a bounded Ruffle compatibility adapter prototype, but **do not
switch the game backend yet**. The native host can render the actual movies,
make typed calls, and receive FSCommands. Stock Ruffle is not a drop-in
replacement for Prime World's customized Flash runtime.

Read [the source-backed compatibility audit](COMPATIBILITY.md) and the current
[port plan](../LINUX_PORT_PLAN.md). Ruffle APIs in this experiment are pinned
internals, not a stable C ABI.

## Verified Results

Tested source: official tag `nightly-2026-02-13`, commit
`1b24dd3a6925eecdd1d7fa49e165814e7ed2163d`. These are results for that revision,
not claims about all later nightlies. Local toolchain: rustc 1.98.1, native wgpu
OpenGL on Radeon RX 6800 XT, Pillow 12.3.0 for offline DDS fixtures.

| Check | Result |
| --- | --- |
| Installed native desktop player, both shipped SWFs | Partial rendering; both fail the missing `TextField.userInput` trait. |
| Direct host bridge | Eleven loading and six combat assertions pass: localization String round-trip, arrays/colors, Unicode, scalar returns, and explicit failure paths. |
| Runtime error reporting | Counted separately from direct-call assertions; caught event errors fail the process. |
| Temporary `userInput` shim | Six assertions: five fail before patch, all pass after. Observed construction/localization errors disappear. This does **not** implement PW text/markup semantics. |
| Combat callbacks | Five FSCommands captured; only recorded, not routed into live gameplay. |
| Native rendering | Fixed 1280x720 GL framebuffers captured and checked for non-background pixels. Authored combat chrome is visible; no gameplay-state binding. |
| Raw DDS background/logo | Four bitmap-dimension checks fail. |
| Temporary PNG conversions | Sixteen loading/resource checks pass, zero runtime errors. Original DDS assets remain untouched. |
| Asset positioning | **Four expected failures:** both images remain at `(640,360)` rather than their centered positions after asynchronous loading. |
| Safety/regression tests | Four desktop-harness Python tests, nine preparation/asset Python tests, two Rust host tests. Client regressions are recorded in the port plan. |

The DDS background is 1920x1080 and logo is 690x320. Expected centered positions
at this viewport are `(-320,-180)` and `(295,200)`. PW's original Loader fires
INIT/COMPLETE synchronously, then `LoaderMain.SetMapBack` positions the loaded
images. Ruffle completes later, after that calculation used zero dimensions.
This is a measured lifecycle incompatibility, not simply a scaling preference.

`user_input.patch` only stores the property and maps its setter to standard
`condenseWhite`. It does not implement PW's ordinary-text markup, punctuation
spacing, default whitespace policy, or reflow. Do not upstream or ship it as a
complete fix. Ruffle's bundled fallback font is enabled for the host; that is
also not font-metric parity with Windows.

## Reproduce

Requirements: Git, Cargo/Rust, Java for Ruffle's builtin ActionScript build,
native OpenGL/EGL libraries, and Python with Pillow for the asset fixtures.
See [Ruffle's build documentation](https://github.com/ruffle-rs/ruffle/wiki/Building-From-Source)
and [Pillow DDS support](https://pillow.readthedocs.io/en/stable/handbook/image-file-formats.html#dds).
Use only trusted local game SWFs. The example uses Ruffle's local-file null
navigator; it is not an untrusted-content sandbox or a production game VFS.

From the Prime World repository root, using a new temporary checkout:

```sh
EVAL="$PWD/pw/branches/r1117/Src/LinuxBootstrap/ruffle_eval"
DATA="$PWD/pw/branches/r1117/Data"
RUFFLE=/tmp/pw-ruffle-eval
git clone --depth 1 --branch nightly-2026-02-13 https://github.com/ruffle-rs/ruffle.git "$RUFFLE"
python3 "$EVAL/prepare.py" "$RUFFLE"
export CARGO_HOME=/tmp/pw-ruffle-cargo
export CARGO_TARGET_DIR=/tmp/pw-ruffle-target
export CARGO_PROFILE_DEV_DEBUG=0
cargo build --manifest-path "$RUFFLE/Cargo.toml" --locked -p ruffle_core --example primeworld --features default_font -j4
HOST="$CARGO_TARGET_DIR/debug/examples/primeworld"
timeout 60 "$HOST" "$DATA/UI/Screens/Loading/Flash/pwl.swf" "$EVAL/user_input_calls.json" /tmp/pw-ruffle-trait-before.png
```

The final baseline command should exit nonzero: it reproduces the missing
trait. Source staging checks the exact SHA, refuses conflicting files/patches,
and is idempotent. The host/dependency patches include the native adapter and
pinned DDS decoder dependencies; existing versions remain pinned. It does not fetch or build
anything itself. Never use it to overwrite a separately edited Ruffle checkout.

Then apply the incomplete compatibility experiment:

```sh
python3 "$EVAL/prepare.py" "$RUFFLE" --user-input
cargo build --manifest-path "$RUFFLE/Cargo.toml" --locked -p ruffle_core --example primeworld --features default_font -j4
cargo test --manifest-path "$RUFFLE/Cargo.toml" --locked -p ruffle_core --example primeworld --features default_font -j4
timeout 60 "$HOST" "$DATA/UI/Screens/Loading/Flash/pwl.swf" "$EVAL/user_input_calls.json" /tmp/pw-ruffle-trait-after.png
timeout 60 "$HOST" "$DATA/UI/Screens/Combat/Flash/main.swf" "$EVAL/combat_calls.json" /tmp/pw-ruffle-combat.png
python3 "$EVAL/assets.py" "$DATA" /tmp/pw-ruffle-resource-fixture
timeout 60 "$HOST" "$DATA/UI/Screens/Loading/Flash/pwl.swf" /tmp/pw-ruffle-resource-fixture/dds_calls.json /tmp/pw-ruffle-dds.png
timeout 60 "$HOST" "$DATA/UI/Screens/Loading/Flash/pwl.swf" /tmp/pw-ruffle-resource-fixture/png_calls.json /tmp/pw-ruffle-png.png
timeout 60 "$HOST" "$DATA/UI/Screens/Loading/Flash/pwl.swf" /tmp/pw-ruffle-resource-fixture/layout_calls.json /tmp/pw-ruffle-layout.png
```

The DDS and layout commands are intentionally failing compatibility gates.
Trait, combat, and PNG resource checks should pass. `assets.py` requires a new
output directory and records source, decoded-pixel, and PNG hashes. Runtime
fixtures use local file URLs, not production colon-prefixed resource lookup.
Asynchronous work is pumped outside the Player mutex to avoid deadlock.

JSON lines on stdout report each assertion plus runtime errors, callback data,
and frame statistics. Stderr preserves Ruffle diagnostics. A zero exit means
only these bounded assertions passed; every report sets
`game_integration_verified` to false. Stub warnings, input, visual parity, audio,
and unvisited UI paths are not covered by that exit code.

Headless harness tests and optional 45-second standalone desktop capture:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s "$EVAL" -p 'test_*.py' -v
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s "$EVAL/.." -p test_ruffle_probe.py -v
python3 "$EVAL/../ruffle_probe.py" loading --seconds 45
python3 "$EVAL/../ruffle_probe.py" combat --seconds 45
```

Desktop capture additionally requires installed `ruffle`, `xdotool`, `maim`, and
an accessible X11 session. The window manager may override the requested size;
use the embedded host for fixed-size evidence. Desktop harness success means
capture completion only, not movie compatibility.

## Next Acceptance Gates

1. Implement exact PW text semantics and verify styled text, whitespace,
   punctuation, localization, and fonts against Windows reference captures.
2. Add an engine asset-provider adapter with real game paths, image decoding,
   errors, and a tested solution to synchronous-loader layout assumptions.
3. Design rooted opaque object handles and a narrow C ABI, including teardown,
   reentrancy, callbacks, and failure propagation; never pass Tamarin Atoms into Ruffle.
4. Verify interactive mouse/keyboard/focus, engine GL compositing/state restoration,
   dynamic minimap textures, and the game's sound-event bridge.
5. Only then make a Linux-only opt-in client backend and run both platform gates
   before considering any default change. This batch did not start that migration.
