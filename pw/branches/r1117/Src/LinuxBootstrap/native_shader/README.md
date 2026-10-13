# Native BasicFX Shader Path

The Linux renderer translates the shipped `BasicFXMaterial.shd` pair zero with
[MojoShader](https://github.com/icculus/mojoshader), then draws raw engine vertex
and index buffers through `StaticMesh -> Batch -> SmartRenderer`. Windows still
uses its existing DirectX renderer. No Wine or replacement shader is involved.
See [vendor provenance](../third_party/mojoshader/README.md) for the exact pin,
license and two allocation-failure fixes.

## Supported Contract

- OpenGL 3.3 compatibility or newer, one render thread and one active host context.
  The host begins a monotonically identified context lifetime and releases the
  shader backend before GLX destruction. A cleanup failure is not reported as
  successful shutdown.
- BasicFX with optional alpha testing and backface culling, all four
  authored pins and runtime depth-output pin at zero, constant diffuse/UV values,
  opacity/masterOpacity one, and linear repeat sampling. Alpha testing compares
  final shader alpha using `>= alphaTestRef / 255`, after texture/multiply/add;
  discarded fragments write neither color nor depth. Culling-off is two-sided,
  without automatic winding changes for reflected WORLD transforms.
  Unsupported materials keep the existing preview path. Invalid draws of an
  admitted material are skipped, never replaced with stale shader constants.
  Malformed alpha/culling/blend enums and references outside 0..255 also fail
  closed, even when alpha testing is off.
- Original Off, LerpByAlpha, PremultipliedLerp, AddColor, AddColorMulAlpha,
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
  c21 addition. UV includes the original `vOffset + 1`. Depth conversion retains
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

This is view-local ordering of the admitted **static** subset, not all scene
transparency or per-triangle/intersection sorting. Water/road visualization stays
an early underlay; unsupported materials, particles and skeletal transparency do
not join this pass.

This is not a general permutation renderer. Skinning, animated
material constants, lighting/shadows, instancing and full texture mip chains are
not implemented here. Texture sampling deliberately uses the existing native
level-zero upload. Source reflection/bounds checks are not a signature or a
sandbox for arbitrary replacement shader archives.

## Verification

Build with the normal LinuxBootstrap CMake configuration. CPU transform tests
are part of CTest; isolated GL tests are explicit so they cannot race shared
client/HUD captures:

```sh
cmake --build /tmp/primeworld-linux-bootstrap --target PrimeWorldLinuxShaderTransformProbe PrimeWorldLinuxShaderPixelProbe
/tmp/primeworld-linux-bootstrap/native_shader/PrimeWorldLinuxShaderTransformProbe
xvfb-run -a env PW_BACKEND_APPROVED_XVFB=1 LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 \
  /tmp/primeworld-linux-bootstrap/native_shader/PrimeWorldLinuxShaderPixelProbe \
  pw/branches/r1117/Data/Shaders/UberShaders/BasicFXMaterial.shd
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
