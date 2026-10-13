# Native Static Shader Path

The Linux renderer translates shipped `BasicFXMaterial.shd` pairs 0, 12 and 24
and baked `BasicMaterial.shd` pair 24 with
[MojoShader](https://github.com/icculus/mojoshader), then draws raw engine vertex
and index buffers through `StaticMesh -> Batch -> SmartRenderer`. Windows still
uses its existing DirectX renderer. No Wine or replacement shader is involved.
See [vendor provenance](../third_party/mojoshader/README.md) for the exact pin,
license and two allocation-failure fixes.

## Ordinary Terrain Backend

`LoadSceneFamily` atomically adds the eight shipped `TerrainElementMaterial.shd`
pairs `0,30,60,90,120,150,180,210` to the four static programs in the same context
owner. The slot is `2 * (layerCount - 1) + localUV`. This is a tested backend
contract; the map renderer still uses its preview terrain material until the
authored layer/nature inputs are connected. No terrain appearance parity is
claimed by the backend alone.

Terrain positions are world-space. Required vertex registers are `c8..11`
(view-projection), `c20.x` (global tiling), `c21` (nature UV mapping), and `c234.x`
(night fraction). Pixel `c20.x` is overburn and `c21` selects the nature channels.
The original shader owns `c0`, decodes day/night BGRA lighting, swizzles COLOR2
weights, and samples one to four diffuse textures plus nature sampler 4. Local
UVs affect only layer zero. Its alpha rule is intentional: layer-zero alpha is
unweighted; subsequent layer alphas are weighted. Overburn multiplies the
already nature-weighted RGB, not an independent additive light.

`IndexedDraw` borrows independent day/night/weight streams and live level-zero
2D textures. Global variants ignore unused UV layout/data. Off writes RGBA/depth;
AddColor supports later nature passes while preserving destination alpha/depth.
Other terrain blend modes, cache, shadow and depth-output permutations are not
admitted. Every referenced sampler uses repeat/linear level-zero sampling; the
Windows diffuse mipmapping/anisotropic modes are not implemented here. All five unit
bindings/samplers and modified texture parameters are restored, including aliases
and failures partway through borrowing. Tickets remain CPU-only after shutdown.

CPU and isolated pixel gates accept a third archive argument:

```bash
DATA=pw/branches/r1117/Data/Shaders/UberShaders
/tmp/primeworld-linux-bootstrap/native_shader/PrimeWorldLinuxShaderFamilyCpuProbe \
	"$DATA/BasicFXMaterial.shd" "$DATA/BasicMaterial.shd" "$DATA/TerrainElementMaterial.shd"
prlimit --core=0 --as=8589934592 xvfb-run -a \
	-s '-screen 0 1280x720x24 +extension GLX -nolisten tcp' \
	env PW_BACKEND_APPROVED_XVFB=1 \
	/tmp/primeworld-linux-bootstrap/native_shader/PrimeWorldLinuxShaderPixelProbe \
	"$DATA/BasicFXMaterial.shd" "$DATA/BasicMaterial.shd" "$DATA/TerrainElementMaterial.shd"
```

Run GUI gates sequentially. `--terrain-only` after the three paths runs the
focused terrain suite. The tests use the original bytecode with mock geometry,
textures and a byte-domain color oracle, including shader compile/link failure
injection and hostile caller state. ASan builds require their shadow address
space, so use an RSS cap instead of `prlimit --as` for them.

## Supported Contract

- OpenGL 3.3 compatibility or newer, one render thread and one active host context.
  The host begins a monotonically identified context lifetime and releases the
  shader backend before GLX destruction. A cleanup failure is not reported as
  successful shutdown.
- BasicFX Simple, AdditiveOpacity and BlendOpacity with optional alpha testing
  and backface culling; skin, vertex-color, distortion and runtime depth-output
  pins at zero; constant diffuse/UV/opacity values and linear repeat sampling.
  AdditiveOpacity scales final source RGB and BlendOpacity scales final source
  alpha by `opacity*masterOpacity`; neither chooses the fixed-function blend mode.
  Both operands must be finite, and opacity-program products must fit a float.
  Finite values outside 0..1 are not clamped. Simple ignores the product, including
  a finite zero opacity: authored values do not imply a runtime `ModifyOpacity`
  call, a different pin or a blend-state override. Alpha testing compares
  final shader alpha using `>= alphaTestRef / 255`, after texture/multiply/add;
  comparison precision follows the compatibility alpha test, including permitted
  fixed-point conversion ([OpenGL 3.3, section 4.1.4](https://registry.khronos.org/OpenGL/specs/gl/glspec33.compatibility.pdf)).
  Discarded fragments write neither color nor depth. Culling-off is two-sided,
  without automatic winding changes for reflected WORLD transforms.
  Unsupported materials keep the existing preview path. Invalid draws of an
  admitted material are skipped, never replaced with stale shader constants.
  Malformed opacity/alpha/culling/blend enums and references outside 0..255 also fail
  closed, even when alpha testing is off.
- For BasicFX: original Off, LerpByAlpha, PremultipliedLerp, AddColor, AddColorMulAlpha,
  MulColor and MulInvColor factors. Non-Off modes preserve destination alpha and
  depth, with RGB-only writes, LEQUAL testing and FUNC_ADD. PremultipliedLerp does
  not multiply source RGB by alpha again. Blending requires GL 4.0 or
  [ARB_draw_buffers_blend](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_draw_buffers_blend.txt)
  plus its indexed entrypoints; only draw-buffer zero is changed and restored.
  Without this optional capability, blended draws are rejected while the GL 3.3
  opaque/cutout path remains available. No global blend-state fallback is used.
- Original POSITION0 float3 and TEXCOORD0 float2, triangle lists, 16/32-bit
  indices, stream offsets, strides and signed base vertices. WORLD rows stay
  separate from preview/camera registers; vertices are not transformed twice.
- VS c0-3 WORLD, c8-11 ViewProjection and c20 UV; PS c20 diffuse multiplier and
  c21 addition, plus c6 for the two opacity programs. UV includes the original
  `vOffset + 1`. Depth conversion retains
  the native camera's clip range and perspective w.
- Private VAO/buffers/program with borrowed texture ownership and restored host
  state. Only draw-buffer zero may be active: GLSL 1.20 `gl_FragColor` otherwise
  broadcasts to additional outputs
  ([OpenGL specification, section 4.2](https://registry.khronos.org/OpenGL/specs/gl/glspec20.pdf)).

Preview materials initialize their static shader identity and use the existing
`SkipIDEvaluation` policy because their texture/constants are not in the shared
material CRC. This forces preparation for each queued batch even when sort IDs
match. Sorted and unsorted queue tests cover distinct colours and a transition
from an unsupported material back to a supported one.

The renderer loads a fixed four-program family once per context. Both original
archives are required and their combined size is capped at 4 MiB. Ticket switches
do not compile or link shaders. A family reload either publishes all four tickets
or none; failed owning-context reloads invalidate the old tickets and register
initialization. The standalone `LoadBasicFxPair0` API still compiles only pair zero
and retains its original reflection/constant contract. The BasicFX-only family
API remains available. Combined loads validate the complete reflected interfaces
and archive directories of both original files.

## Authored Baked Lighting

Eligible BasicMaterial meshes use the original pair 24: unskinned, non-instanced,
normal render mode, no depth output, opacity/emissive/reflectivity off, no normal
or specular texture, no derived specular pin, and BlendMode Off. Overridden render
state, special transparency and non-wrap/linear sampling remain unsupported.
Original byte alpha testing and culling are retained. Malformed admitted buffers,
constants, runtime pins and raster state fail closed, without preview fallback.

The [bounded reader](../baked_vertex_colors.h) decodes the original map `.vtxcolor`
file once per map, with a 64 MiB source cap and independent metadata/count budgets.
Lookup uses the original AdvMap object index, scene DBID hash and component index,
then validates the complete common vertex-buffer CRC and vertex count. The current
map integration admits only one childless static root per scene, where traversal
index zero is proven. Complex hierarchies retain the earlier preview. Filtered
payload indices and geometry-only cache keys are not valid lighting identities.

Day/night streams remain BGRA8. The backend stages referenced colors as RGBA8;
the original shader performs HDR decoding and day/night interpolation. VS c0..2
and c8..11 carry WORLD/VP, c234.x carries the current world night fraction, and
c3 is shader-defined. PS c20/c21 modify RGB only; texture alpha remains unchanged.
The day/night value refreshes per view, not only when a cached material is built.
Vertex/fragment color clamping and two-sided vertex-color state are restored.

Native DXPtr does not own allocations. An aliasing shared mesh owner retains both
color buffers and destroys the mesh descriptor before freeing them. Retained draws
therefore survive map cache eviction. Failed binding clears previous descriptors
and buffers. Missing colors use the explicit unsupported preview path; fake/editor
colors are not presented as authored lighting. Preview caps still restrict coverage.
`finalBakedMapColors` reports cumulative lookup/binding counts, selected/drawn
payload instances, and successful draw calls separately.

## Transparent Scene Pass

Eligible static batches share one retained-owner queue per 3D view. Map terrain
elements and map static objects are collected together and flushed after opaque
animated/dynamic unit meshes, before debug/health/selection overlays. The
character view owns a separate pass after its skeletal and opaque attachment
draws. Placement matrices are resolved before the submitting helper returns;
no temporary queue or skeleton pointer is retained.

The original queue sorts by descending `-eye.z` of the transformed authored DB
bounds center, then SubPriority. Flat bounds are accepted; nonfinite, inverted,
missing all-point bounds and unrepresentable keys are rejected. Capacity is
reserved before publishing batch links. Viewport, framebuffer and camera changes
discard pending draws, and explicit flushing is once-only. Destruction discards
without GL calls. Deferred counters count successful shader draws, not attempts.

Static terrain and object meshes/materials are cached separately by payload index
using the existing scene-resource cache. Payloads are immutable during a map's
lifetime; selecting another map explicitly clears both groups, even when payload
counts match. Failed loads stay cached until invalidation. A deferred view retains
shared mesh ownership independently of cache eviction, then releases it on flush,
view rejection or destruction. Both caches are cleared before render-resource
shutdown. This does not add asset hot reload or change the existing preview limits.

This is view-local ordering of the admitted **static** subset, not all scene
transparency or per-triangle/intersection sorting. Water/road visualization stays
an early underlay; unsupported materials, particles and skeletal transparency do
not join this pass.

This is not a general permutation renderer. Skinning, animated
material constants, runtime opacity/blend overrides, emissive, distortion,
general vertex colors, SH/dynamic lighting, shadows, instancing and full texture mip chains are
not implemented here. Texture sampling deliberately uses the existing native
level-zero upload. Source reflection/bounds checks are not a signature or a
sandbox for arbitrary replacement shader archives.

## Verification

Build with the normal LinuxBootstrap CMake configuration. CPU transform and family
directory/reflection tests are part of CTest; isolated GL tests are explicit so
they cannot race shared client/HUD captures. The CPU probe rejects malformed or
different-layout archives before parsing or applying fixture-specific mutations:

```sh
cmake --build /tmp/primeworld-linux-bootstrap --target PrimeWorldLinuxShaderTransformProbe PrimeWorldLinuxShaderFamilyCpuProbe PrimeWorldLinuxShaderPixelProbe
/tmp/primeworld-linux-bootstrap/native_shader/PrimeWorldLinuxShaderTransformProbe
ctest --test-dir /tmp/primeworld-linux-bootstrap -R 'linux_shader_.*cpu|LinuxBakedColors' --output-on-failure
xvfb-run -a env PW_BACKEND_APPROVED_XVFB=1 LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 \
  /tmp/primeworld-linux-bootstrap/native_shader/PrimeWorldLinuxShaderPixelProbe \
  pw/branches/r1117/Data/Shaders/UberShaders/BasicFXMaterial.shd \
  pw/branches/r1117/Data/Shaders/UberShaders/BasicMaterial.shd
```

From `pw/branches/r1117/Bin`, the existing explicit renderer probe now also loads
the original rubble mesh, material and Stone02 texture. It checks authored
pixels, a private material colour change, rotated coverage, invalid-constant
rejection and null-batch selection clearing. Raster probes use private copies of
the material to test byte thresholds, culling, malformed states, and the runtime
depth-output permutation guard. One-sided engine-buffer mock quads distinguish
DB culling on/off under normal and reflected WORLD transforms; disabling the
culling-off mapping must fail that probe. A separate test combines the original Stones
geometry with the shipped TrapFlower alpha texture; this fixture is not the
authored animated TrapFlower scene or evidence of skeletal support:

```sh
xvfb-run -a -s '-screen 0 1280x720x24 +extension GLX -nolisten tcp' \
  /tmp/primeworld-linux-bootstrap/PrimeWorldLinuxClient \
  --bootstrap-flash-renderer-probe --width 1280 --height 720 \
  --bootstrap-frame-capture /tmp/basicfx-stones.png
```

The capture is a 256-square actual engine draw; a second `-rotated.png` file
shows the controlled colour/rotation test. Additional `-cutout.png` and
`-opaque.png` captures show the same geometry/texture with alpha testing enabled
and disabled. These are not full-game parity images.

The same engine probe draws original Rough01N geometry/material/texture with MOBA
placements 378 and 1293 under the same WORLD transform. `-baked-day.png` and
`-baked-night.png` show its original lighting. Tests distinguish both placements,
fresh day/night constants, production identity discovery, invalid CRC/count/fragment
binding, invalid materials, missing-color fallback, shader switching and retained
buffer ownership across cache eviction. The full backend pixel suite additionally
checks exact synthetic HDR/alpha output using the shipped shader bytecode.
The independent pixel suite also tests allocation refusal at each selected
program-load allocation, context recreation, state restoration and malformed
draw inputs, all blend equations, preserved depth/alpha and missing indexed
blend entrypoints. Real-engine transparent probes exercise local-queue/owner
lifetimes, both payload-list orders, later opaque occluders, overlays, 40 retained
batches, view changes, invalid bounds/modes, all DB blend mappings, an empty-world
flush and the actual map/character payload hooks. Defining `BACKEND_NEGATIVE_DRAW` while compiling
`shader_backend.cpp` is a test-only no-draw control that must fail the pixel
suite; never enable it in a deliverable client. Sanitizer evidence and exact
checkpoint results are recorded in [the port plan](../LINUX_PORT_PLAN.md).

Opacity regressions alternate all three programs and separate source RGB scaling
from alpha scaling, final-alpha thresholds, independently authored blend modes,
non-unit master opacity, invalid products/pins, runtime depth-output rejection
and same-family queued materials with distinct constants. Animated opacity and
emissive remain explicitly unsupported. The Flat03 fixture resolves the shipped
mesh's material-slot map and draws only its original BlendOpacity 1.1 slot with
its own texture; it does not claim support for the sibling BasicMaterial slot or
attached effects. When a capture path is supplied, `-opacity-flat03.png` records
that slot alone. Family tests also cover partial-load cleanup, legacy/family
replacement, missing registers and actual load-time versus draw-time GL calls.

The engine cache probe draws opaque and transparent geometry through both map
hooks for repeated frames, compares nonblank pixels, checks independent cache
groups, evicts resources with pending batches, replaces a same-sized map and
rejects a changed view. Missing geometry must not retry or reuse a stale mesh.
