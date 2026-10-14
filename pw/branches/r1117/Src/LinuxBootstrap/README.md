# Native Linux Client

This is the native Linux/OpenGL client port, not a Linux server or Wine wrapper.
Windows implementation branches and projects remain in place. A complete playable
match and Windows build validation are still outstanding.

See [the port plan](LINUX_PORT_PLAN.md) for verified checkpoints, evidence and
remaining work. [VERSION](VERSION) identifies this port checkpoint independently
of the shared network/replay version.

## Default Build

Requirements: a C++17 toolchain, CMake, Python 3, pthreads, and development files for X11,
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
The client CTests include actual engine formula, resource, cooldown, world-grid,
fog/stealth, bounded voxel traversal, packed-command policy and scheduler-draining fixtures. Passing them
does not establish a complete match.

`LinuxDispatchFactoryAbi` compiles an owning-return assertion using the generated
flags for all 27 factory/caller builds in the client, runtime and gameplay probes.
It includes the compile-only gameplay target: its declarations must agree with
the factory even though no executable links that object library. The check uses
the standard [CMake compilation database](https://cmake.org/cmake/help/latest/variable/CMAKE_EXPORT_COMPILE_COMMANDS.html)
and Python's standard library; no additional Python package is required.

Native dispatch factories return an owning handle across synchronous callbacks.
Instance holders remain weak: delayed work without another owner is cancelled
and rejected before activation. Registered-world and payload-owned casts retain
their existing behavior; this does not add a worldless scheduler. Native attack
admission reports failure to `DoAttack`, avoiding queued work and cooldown on
factory rejection while preserving successful immediate completion.

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

The finite automatic proof closes command admission at its simulation limit,
drains accepted commands through the final replay step, then freezes the world.
Use interactive mode above for manual testing without that artificial step cap.
At a finite interactive deadline, command admission closes and normal wall-paced
ticks drain accepted input through the replay boundary. A tail that has not drained
after one second reports a nonzero exit status; live indefinite sessions are unchanged.

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
The driver restores only its PID's window before scripted purchase and again
before delayed controls. A focus change still fails closed. Shared desktops can
use an isolated Xvfb display with an EWMH window manager; software GLX evidence
does not replace a desktop/GPU performance check.

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

The local native view now shares the immutable authored baked terrain between
drawing, ground picking, camera focus and placement sampling. Ray queries adapt
the original HeightsController triangle algorithm, reject tiles by their bounds,
ignore hidden triangle indices and select the nearest bounded hit. Height queries
select the upper surface at folded/overlapping geometry. No per-query allocations
or GL readback are required. An available baked mesh never falls through to an
invisible heightfield when a pick misses; maps without it retain sampled terrain,
and the inspection overview retains its planar selection contract. Visual height
sampling outside covered geometry still uses the old fallback. These are two-sided
geometry queries, not alpha-mask, prop, road or complete Windows collision parity;
they do not change simulation coordinates, navigation or the interim camera type.

Native BasicMaterial previews preserve authored diffuse addressing independently
of texture identity, including negative exporter UVs and shared-texture material
slots. Private GL3.3 sampler objects are capped at sixteen address combinations;
each scope restores the borrowed sampler, texture binding and active unit, and
the owning context releases the cache during shutdown. The renderer probe covers
original Plane payloads, immediate renderer pixels, cold/warm hero pose caches,
malformed inputs, nested scopes and context/thread/teardown boundaries. Filtering
remains level-zero linear: this does not add original mip/anisotropic filtering,
and UI texture settings are unchanged.

Ordinary opaque BasicMaterial previews disable output-zero blending and use the
authored GEQUAL cutout threshold on texture alpha without preview attenuation.
The scope restores host blend enable and alpha-test state, leaving depth, culling,
blend factors and other outputs untouched. Both original immediate draws and
cached live hero poses preserve this policy per material slot. Transparent,
emissive, runtime-opacity and overridden-state materials keep the legacy preview;
this is not complete material, culling, sorting or original lighting parity.

Native artwork and TextureManager decode DDS BC1/2/3 base images through the
repository's MIT-licensed Squish decoder. This restores full 565 endpoint values,
BC2/3 four-colour blocks, alpha indices and cropped tiny/odd-sized blocks. Base
inspection is bounded separately from strict GPU mip-chain admission: shipped
textures with missing unused mip tails remain readable without uploading an
incomplete chain. RGB16/24/32 and aligned premultiplied DXT2/4 retain guarded legacy
conversion. The shared Windows decoder, cursor/splash callers, texture filtering
and original compressed GPU upload are unchanged. Portable tests cover malformed
inputs and independent pixel fixtures; the engine probe checks both consumers
and original hero textures against driver-decompressed pixels where available.
Driver comparison allows one RGB unit and two units only for interpolated BC3
alpha; alpha endpoints and explicit alpha remain exact. Independent CPU fixtures
and every alpha pixel in the shipped BC3 fixtures verify exact integer division
from the [BC3 definition](https://learn.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-block-compression#bc3).
The 2D loader rejects cubemaps/volumes; luminance-only DDS support remains absent.

Native skeletal previews decode the original exporter `Render::Color` bone
indices from BGRA storage into RGBA weight order before fragment palette lookup.
Plain `UBYTE4` declarations retain their byte order. The
`--bootstrap-flash-renderer-probe` includes asymmetric mock influence tests,
complete Plane skin decoding and original-mesh framebuffer comparisons with an
independently pre-skinned reference. This verifies index association, not original
skeletal material, lighting, animation-state or GPU-skinning parity.

Eligible single-skeleton live hero/creep previews now use the original
`DiAnimGraph`, `AnimGraphBlender` and `AnimGraphApplicator`, including authored
default nodes, dispatchers and transition blending. Idle/move aliases come from
the session's Hero/Creep animation sets, not the animation list order. Each
visible unit owns its graph; the bounded cache retires unseen units and resets
on map/world replacement or clock rewind. Pose-cache keys include graph identity,
so units sharing a mesh can display different states at the same timestamp.
Linux graph IDs/booleans retain Windows' 32-bit widths across the `int*` blender
boundary; the Windows definitions are unchanged. This is presentation-only:
cast events, attack-speed scaling, upper-body graphs, dynamic external
parameters and gameplay animation callbacks remain unconnected.
Live playback uses world time (including pauses), restarts after a gap over one second, and
recreates graphs after offscreen eviction. Unadmitted assets and the separate
character-inspection view retain the first-clip preview. The engine probe covers
original graph transitions, malformed metadata, ownership and actual pose pixels.

Living graph owners now consume the original world unit speed, including movement
scaling. As in `PFAnimController`, only nodes with authored surface velocity scale
their animation and surface rates by actual speed divided by the move alias's
authored velocity length. Immutable per-owner base coefficients prevent compounding
and allow zero speed followed by resume. Idle, attack and death nodes with zero
surface velocity are unchanged. Zero authored move velocity keeps authored rates;
invalid inputs reject before mutation. Normalized playback is bounded at 100 cycles
per second to limit graph work. This does not alter simulation speed, shared DB data,
network state, attack timing or the Windows controller.

Ordinary animated deaths expose a nonserialized corpse snapshot from the original
creature lifecycle. Already-visible supported graphs request the session death
alias and retain their last heading; the authored graph owns its final corpse pose.
Sinking waits for both the original strict fall-timer crossing and the graph's
original 99% death-completion callback, then advances at authored speed capped
at three mesh half-heights. Completion is recorded on the rendered world-clock
frame; it never catches up sinking time from before the death animation finished.
Corpse removal and resurrection invalidate
presentation without changing world timers, object retirement or respawn behavior.
Death notification identity is separate from health, which lethal damage clears
before the hook, and cannot restart on repeated kills. Only fresh deaths observed
within 250 ms are admitted. Offscreen or replaced owners and gaps over one second
hide corpses rather than replay death. Unsupported graphs, unsummon, deferred death,
no-animation and forced-removal paths keep the previous no-corpse rendering;
separate skeleton/effect objects and complete corpse/visibility parity remain open.

Accepted native base attacks publish a nonserialized presentation cue independent
of their dispatch lifetime. Live markers carry the ability identity, sequence,
world timestamp, cooldown period and cancellation. The original session attack
alias drives whole-body playback for the remaining accepted cooldown window;
movement, cancellation or expiry returns to locomotion. Bounded short-lived cue
receipts prevent duplicate draws and quick offscreen reentry from restarting an
attack. Unseen cues older than 250 ms, invalid intervals and missing attack aliases
are skipped, not queued. This is visual state only: native `GetAttackTimeOffset()`
still returns zero, so authored windup, damage-marker synchronization, attack-speed
clip scaling and critical/special attacks remain open.

Accepted attacks also snapshot their victim position before synchronous callbacks.
Eligible live units face that position once per consumed cue, retaining their own
visual heading through idle, cancellation and attack expiry. Subsequent movement
uses the original world direction. Checked double-precision normalization rejects
nonfinite and near-zero directions; no simulation position/direction is changed.
Map/world/asset replacement or offscreen eviction discards visual heading and
reentry starts from current world direction (stable forward if unavailable).
Turns are immediate; original smooth turning and offscreen facing continuity are
not yet ported. Unsupported graph assets retain the previous mesh-yaw policy.

- Rendering uses real assets and selected original BasicFX/baked BasicMaterial
	shaders, but most materials and presentation remain simplified. The
	[native static shader contract](native_shader/README.md) documents exact admission,
	per-placement color ownership, tests and preview-coverage limits.
- Numeric formulas support bounded arithmetic/conditionals, checked helpers,
	abilityScale/damageScale contexts and 47 exact unit properties through s/t/r
	prefixes. Four exact combat accessors support the shipped defence constants;
	general member access and strings remain unsupported. Damage, healing and
	cooldown-refresh applicators use checked live contexts; applicator-only random
	uses the seeded world stream with failed
	evaluation rollback. Unsupported expressions fail closed. Generic RetrieveParam,
	full formula-language coverage and atomic downstream cast refunds remain unfinished.
- Fog observers, visibility queries and stealth/true-sight caches use simulation
	state. True sight uses bounded voxel queries and AI target selection cannot retry
	hidden targets through the registry. Rendered fog, complete stealth events/effects
	and measured whole-world performance parity remain incomplete.
- Manual attacks check visibility and legality before replacing an order, then
	use the existing simulation state queue. Script orders bypass visibility explicitly;
	neither path teleports targets, resets cooldowns or synthesizes damage. Attack states
	approach/stop through native movement and revalidate target legality. Authored
	targeting/chase/aggro distances resolve unit overrides and AI defaults; attack
	range and allowChase use checked live formula contexts. Point/chase/route states
	restart interrupted ordinary motion, stop on exit and retain last-seen chase positions.
	Full pathfinding/LOS presentation, attack sectors, mounted routing and player
	authorization remain incomplete.
- Combat now includes seeded critical rolls, outgoing/pre-damage hooks, authored
	physical/energy defence, hero damage masks/isolation, attack/cancel notifications,
	healing modifiers, life/energy drains, damage throttling and deferred-death transitions.
	Listener ordering follows Windows; callback-safe snapshots defer new identities
	from the current event, unlike Windows live insertion. Direct callbacks reject
	nonfinite resource amounts and incoming damage results. These guards do not
	roll back prior callback effects.
- Enabled top behaviors receive target orders without unconditional assignment;
	ordinary heroes install their production behavior and restore missing ownership
	on resurrection. Base retaliation uses weak orders, while hero retaliation uses
	strong orders with its own idle/queue policy and help-scream cooldown. Behavior
	disable clears orders, targets, screams and attack-memory victims. Tower help,
	complete mounted endpoints and allied-recipient behavior remain unfinished.
- Clone construction, full death rewards/projectile timing,
	complete casting/movement, remaining HUD actions, online integration
	and a complete local match are still outstanding.
- Consumable/talent/portal commands now queue production cast states, approach
	before payment and revalidate live actor/control/target policy. Inventory keeps
	32 bounded slots, stacks compatible descriptors and retains shared cooldown data
	after removal. Real unit simulation advances timed applicators and cached consumable
	data. Ability instances reject repeated admission, recheck delayed activation and
	retain deferred payloads; creation channels defer payment until real completion.
	Full consumable groups, authored animation timing, projectile subtypes and saved-world
	restoration of bootstrap command observers remain unproven or unported.
- Linux source guards preserve the Windows path, but an actual Windows build and
	runtime regression run are still required.

## Regression Coverage

The focused checks run without a graphical session through CTest:

```sh
env LC_ALL=C MALLOC_PERTURB_=165 ctest --test-dir "$BUILD" \
	-R 'Linux(NumericFormula|FormulaRange|ApplicatorFormula|VoxelRange|Visibility.*|SchedulerDrain)$' \
	--output-on-failure
```

- [Numeric parser](formula_numeric_probe.cpp): checked helpers, lazy callbacks,
	malformed syntax, finite arithmetic, budgets and independent parser ownership.
- [Formula context](formula_range_probe.cpp): real base/modified stats, resource
	pools, constants, scaling parameters, range, costs and cooldowns; original
	defence formulas/constants loaded through the game DB and checked against a scalar oracle.
- [Applicators](applicator_formula_probe.cpp): actual damage/heal/refresh consumers,
	seeded random/critical hits, posthumous senders, outgoing/incoming events,
	pre-damage and delegation, defence routing, hero/creep healing modifiers,
	drains, fatal healing cleanup, hero masking/isolation, damage throttling,
	behavior notification, last-hit rescue/deferred death, actual base attacks and
	direct numeric resource boundaries. [Listener fixtures](event_dispatch_tests.inc)
	cover priority, registration, nested callbacks and retained contents lifetime.
- [Behavior cleanup](behaviour_tests/cleanup_tests.inc),
	[base retaliation](behaviour_tests/retaliation_tests.inc),
	[hero policy](behaviour_tests/hero_tests.inc) and
	[unit dispatch](behaviour_tests/dispatch_tests.inc): actual FSM and combat-memory
	cleanup, current/original faction metadata, visibility/flag/queue admission,
	weak/strong targeting, mount boundaries, cooldown rearming, same-target deduplication,
	enabled top-stack routing and fresh/resurrected hero integration.
- [Queued cast commands](cast_command_tests.inc) and
	[target ranges](cast_target_range_tests.inc): real packed actor/target restoration,
	control loss during approach, authored masks, actual arrival and payment diagnostics.
- [Consumable inventory](consumable_inventory_b10_tests.inc) and
	[simulation](consumable_simulation_b10_tests.inc): fixed slots, stacking, bounded
	quantities, retained ability identity, once-per-descriptor cooldown updates/reset
	and original ability-start event identity.
- [Applicator simulation](applicator_simulation_b10_tests.inc),
	[deferred dispatch](ability_lifecycle_b10_06_dispatch_tests.inc) and
	[channel completion](ability_lifecycle_b10_07_channel_tests.inc): actual timed stat
	restoration and effect ownership through unit stepping, blocked receiver payloads,
	creation-channel payment/cancellation and synchronous ability-end callbacks.
	[Delayed admission](ability_lifecycle_b10_05_delayed_tests.inc) mocks serialized
	timers only; dispatch, damage, resource payment and cancellation use engine code.
- [Voxel queries](voxel_range_probe.cpp): bounded numeric admission, rectangular
	geometry, inclusive circles, ordering and retained live callback semantics.
- [Visibility queries](visibility_query_probe.cpp) and
	[stealth state](visibility_stealth_probe.cpp): rectangular maps, faction fog,
	true sight, target loss, movement, death and one scheduled refresh per unit.
- [Unit lifecycle](visibility_lifecycle_probe.cpp): observer lifecycle and exact
	serialized command actors/targets, policy changes after queueing, and preservation
	of prior orders on rejection, including missing and foreign-world objects;
	packed attack orders preserve resources/positions/cooldowns, stop/move interrupt
	them, and target loss or forbidden chase ends movement. Ordinary world movement
	refreshes live speed stats; explicit overrides remain separate, and zero/tiny
	speed never becomes a full-distance jump.
- [Movement orders](movement_order_tests.inc): authored unit/AI distances, live
	attack range/chase expressions, point/chase lifecycle and nearest-forward waypoint
	selection across wide/portrait maps and three voxel densities. Malformed goals
	and prohibited motion fail before movement; ordinary interruption cleanup is
	separate from special/mounted movement policy.
- [Building orders](building_order_tests.inc), [combat movement](combat_move_tests.inc),
	[attack sectors](attack_sector_tests.inc) and [attachments](attachment_tests.inc):
	production region checks, nested combat/approach cleanup, angular slot ownership,
	actual formation arrival and bounded acyclic attachment admission. These do not
	replace full pathfinding or prove complete mounted gameplay.
- [Base ability use](base_use_tests.inc), [hero casting](hero_cast_tests.inc) and
	[consumables](consumable_state_tests.inc): authored range/flag policies, approach
	before payment, real inventory and unit-owned cast/channel completion without
	double stepping. Mock only descriptor inputs and nonzero work duration;
	bootstrap talent/consumable commands still need production state routing and
	authored animation timing.
- Hero orders cover [follow movement](hero_follow_lifecycle_tests.inc),
	[assisted combat](hero_follow_combat_tests.inc), [hold](hero_hold_tests.inc),
	[suspend](hero_suspend_tests.inc), [interaction approaches](hero_interaction_approach_tests.inc),
	[action lifetime](hero_interaction_action_tests.inc), [unit use](hero_use_unit_tests.inc)
	and [authored pickup/flag actions](hero_flag_pickup_tests.inc). Real engine states,
	personal fog, mana and event identities are exercised; empty-dispatch fixtures
	do not establish complete live-map channel/applicator effects.
- [Scheduler drain](scheduler_drain_probe.cpp): preserve admitted commands and
	statuses while closing finite smoke input and draining the replay boundary,
	including closed barriers with unbounded interactive step budgets.

Use the graphical acceptance driver above as well; these checks do not prove
complete stealth abilities, the full combat pipeline or Windows visual parity.
