# Linux Native Port Plan

Updated: 2026-04-18

## Goal

Deliver a native Linux client for Prime World without Wine, while keeping the Windows client buildable and functionally intact.

## Non-Negotiable Constraints

- Do not remove or regress the Windows client path.
- Keep Linux-only behavior behind `__linux__`, Linux-specific targets, or bootstrap-only compile definitions.
- Prefer cross-platform fixes when they are low-risk and do not change Windows behavior.
- Batch work into larger chunks and keep this file current so progress does not depend on chat history.

## Current Verified State

- `PrimeWorldLinuxClient` builds successfully.
- `PrimeWorldLinuxGameplayProbe` builds successfully.
- `PrimeWorldLinuxRenderProbe` builds successfully.
- The native Linux client shell runs, opens a native window, mounts real game content, loads config, input, localization, session data, DB roots, and real map/hero/loading assets.
- The maintained Linux gameplay probe currently compiles 70 gameplay `.cpp` files.
- The maintained Linux render probe now compiles 155 render/scene/terrain `.cpp` files, including the real render-runtime path pieces `batch.cpp`, `DXManager.cpp`, `OcclusionQueries.cpp`, `DBRender.cpp`, `DBRenderResources.cpp`, `shaderdefinestable.cpp`, `DeviceLost.cpp`, `ReadDDS.cpp`, `SHCoeffs.cpp`, `VertexColorStream.cpp`, `shaderconstantsbinder.cpp`, `Blur.cpp`, `Bloom.cpp`, `ConstantProtection.cpp`, `FullScreenFX.cpp`, `AOERenderer.cpp`, `WaterConvexes.cpp`, `CleanGeometry.cpp`, `fxresource.cpp`, `skeletonanimation.cpp`, `SkeletalAnimationBlender.cpp`, `SkeletalAnimationSampler.cpp`, `SkeletonWrapper.cpp`, `Stretcher.cpp`, `dipdescriptor.cpp`, `rendermode.cpp`, `DebugMaterial.cpp`, `GrassMaterial.cpp`, `MaterialResourceInterface.cpp`, `facefxsystem.cpp`, and the newly maintained animation-graph / terrain-runtime/core slice on top of the earlier null-render mesh, scene, UI, and utility work.
- Recent additions to the maintained render probe are the animation-graph/time-control slice (`AnimGraphApplicator.cpp`, `AnimGraphBlender.cpp`, `AnimGraphController.cpp`, `Animators.cpp`, `BitMap.cpp`, `HeightsController.cpp`, `TimeMutator.cpp`), the next scene/terrain runtime slice (`CollisionHull.cpp`, `DBSceneBase.cpp`, `DiAnGrAPI.cpp`, `DiAnGrCl.cpp`, `DiAnGrNLinker.cpp`, `DiAnGrSStorage.cpp`, `DiAnGrUp.cpp`, `DiAnGrUtils.cpp`, `DiGraph.cpp`, `LandPlacementMutator.cpp`, `DBTerrain.cpp`, `GrassLayersManager.cpp`, `GrassRegion.cpp`, `GrassRenderManager.cpp`, `NatureMap.cpp`, `SpeedGrass.cpp`, `TerrainElementManager.cpp`), the terrain cache/runtime layer (`TerrainHeightManager.cpp`, `TerrainLayerManager.cpp`, `TerrainMaterialCache.cpp`, `TerrainTextureCache.cpp`), and now the remaining maintained terrain core (`NatureMapVisual.cpp`, `Terrain.cpp`, `TerrainCollision.cpp`, `TerrainElement.cpp`, `TerrainGeometryManager.cpp`, `BezierSurface.cpp`, `NatureAttackSpace.cpp`). Those files now build in the maintained Linux render probe instead of only passing ad-hoc syntax checks.
- Standalone Linux null-render syntax probes for `AnimatedSplitSceneComponent.cpp`, `AttacherSceneComponent.cpp`, `Camera.cpp`, `CameraControllersContainer.cpp`, `CameraInputModifier.cpp`, and `FreeCameraController.cpp` are now green or superseded by maintained render-probe builds.
- Latest verified runtime command:

```bash
/tmp/primeworld-linux-bootstrap/LinuxBootstrap/PrimeWorldLinuxClient --locale en-US --map Aram --hero shadow --seconds 0.2
```

- Latest verified runtime log:

```text
 /home/vitaly/p/Prime-World/pw/branches/r1117/Bin/logs/2026.04.18-09.02.15/linux-client-shell.log
```

## What Is Already Done

### Linux bootstrap and shell

- Native X11 window/bootstrap path exists.
- Linux build path is wired through `Src/CMakeLists.txt` and `Src/LinuxBootstrap/CMakeLists.txt`.
- Native shell handles command line, filesystem root detection, profile/log setup, content mounting, localization fallback, launcher/session import, and input diagnostics.

### Content and data bootstrap

- Real `RootFileSystem` mounting works.
- Real DB cache bootstrap works for the Linux shell.
- Real map catalog, map settings inheritance, loading screens, map art, hero portraits, and localized strings are resolved from game data.
- Real `SessionRoot`, `SessionLogicRoot`, `SessionVisualRoot`, `UIRoot`, and related DB roots are probed from the Linux shell.

### Gameplay portability groundwork

- A maintained Linux gameplay compile target exists: `PrimeWorldLinuxGameplayProbe`.
- A maintained Linux render compile target exists: `PrimeWorldLinuxRenderProbe`.
- It now covers a broad slice of `PF_GameLogic`, including world, AI, applicators, statistics, base unit state, target selectors, buildings, glyphs, towers, and related gameplay objects.
- The Linux gameplay probe now also compiles the heavier ability timing/attack slice in `PFAbilityData.cpp` and `PFBaseAttackData.cpp` by cutting visual-only scene animation paths behind `VISUAL_CUTTED`.
- The Linux render probe now compiles `renderer.cpp`, `smartrenderer.cpp`, `dxutils.cpp`, `rendersurface.cpp`, `texture.cpp`, `TextureManager.cpp`, and `renderflagsconverter.cpp` through Linux/bootstrap-only null-render branches instead of failing immediately at the top-level Direct3D include wall.
- `System/Geom.h` now exposes a narrow Linux null-render D3DX math shim for `D3DXVECTOR3`, `D3DXMATRIX`, `D3DXPLANE`, `D3DXVec3Length`, `D3DXPlaneDotNormal`, `D3DXPlaneDotCoord`, and `D3DXVec3TransformCoord`, which is enough to compile the bounding-volume slice without pulling the Windows D3DX headers into the Linux path.
- The Linux render probe now also compiles `StaticMesh.cpp`, `SkeletalMesh.cpp`, `InstancedMesh.cpp`, and `WaterMesh.cpp` through Linux/bootstrap-only null-render branches, so the maintained render slice is past the old mesh implementation wall and through the first water mesh implementation seam as well.
- The Linux render probe now also compiles the real `batch.cpp` together with `BoundingPrimitives.cpp`, `ConvexVolume.cpp`, `FrustumCuller.cpp`, and `InstancedMeshResource.cpp`, so the maintained render slice is now past the old batch-adjacent bounding-volume seam without relying on `batch_linux_null.cpp`.
- The Linux render probe now also compiles `AnimatedSceneComponent.cpp`, `StaticSceneComponent.cpp`, and `WaterSceneComponent.cpp`, so the maintained scene slice is now past the first mesh-consuming scene component layer.
- The Linux render probe now also compiles `SceneComponent.cpp`, `SceneObject.cpp`, and `LightingScene.cpp`, so the maintained scene slice is now past the first core scene-object and lighting layer rather than stopping at mesh-consuming scene components.
- The Linux render probe now also compiles `Scene.cpp`, so the maintained scene slice is past the old full-scene `ShadowManager -> d3dx9.h -> d3d9.h` blocker and into the next runtime layer above core scene-object management.
- The Linux render probe now also compiles the lightning and trace effect families: `LightningFX.cpp`, `LightningSceneComponent.cpp`, `TraceFX.cpp`, `TraceFXGeo.cpp`, and `TraceSceneComponent.cpp`.
- The Linux render probe now also compiles the camera/runtime control slice: `Camera.cpp`, `CameraControllersContainer.cpp`, `CameraInputModifier.cpp`, `FreeCameraController.cpp`, `AnimatedSplitSceneComponent.cpp`, and `AttacherSceneComponent.cpp`.
- The Linux render probe now also compiles the scene DB/object utility slice above the old core scene wall: `DBScene.cpp`, `SceneObjectCreation.cpp`, `SceneObjectsPool.cpp`, `SceneObjectTrack.cpp`, `CollisionGeometry.cpp`, `CollisionGeometryManager.cpp`, `ScenePassabilityMask.cpp`, and `SoundItem.cpp`.
- The Linux render probe now also compiles the render/runtime utility slice above the old core scene wall: `ConfigManager.cpp`, `DxResourcesControl.cpp`, `ImmediateRenderer.cpp`, `MaterialSpec.cpp`, `RenderComponent.cpp`, `sceneconstants.cpp`, `SceneUtils.cpp`, `GeometryTracer.cpp`, `TerrainGrid.cpp`, `TimeController.cpp`, and `VertexColorManager.cpp`.
- The Linux render probe now also compiles `Material.cpp` and `LightsManager.cpp`: the Linux null-render path now sees the texture helper declarations used by materials, the old MSVC-only material registration block no longer stops GCC, and the null-render `LightsManager` header split is unified enough for the maintained probe to build the real translation unit instead of a duplicate class.
- The Linux render probe now also compiles `SHGrid.cpp`: the Linux null-render path now exposes the legacy Z-state enums used by the debug path and `SHGrid.cpp` now pulls the actual `RenderResourceManager` declaration it already relies on.
- The Linux render probe now also compiles the render-foundation slice `aabb.cpp`, `primitive.cpp`, `sampler.cpp`, and `vertexformatdescriptor.cpp`. The Linux path now pulls the real `NDb::AABB` definition into `aabb.cpp`, sampler binding sees the null-render `renderer.h` helpers, the vertex-format hash specialization is GCC-safe inside `nstl`, and the shared render headers have cleaner Linux/GCC behavior through fixes in `material.h` and `Scene/VoxelGrid.h`.
- The Linux render probe now also compiles `configdatabase.cpp`: the Linux null-render path now gets past the old `<tchar.h>` and Win32 adapter/file-version seam through `System/tchar_compat.h`, the `configdatabase.h -> dxutils.h` Linux include split, and the extra adapter/caps/file-version declarations in the Linux null-render `dxutils.h` surface.
- The Linux render probe now also compiles the debug/runtime utility slice `debugrenderer.cpp`, `debugmesh.cpp`, `debugstaticmesh.cpp`, and `renderstatesmanager.cpp`: Linux now has a no-op debug renderer translation unit under `PW_LINUX_NULL_RENDER`, and `renderstatesmanager.cpp` no longer pulls the real D3D state-manager body on the Linux null-render path because the maintained probe uses the existing header-level stub instead.
- The Linux render probe now also compiles `shadercompiler.cpp` and `multishader.cpp`: the Linux null-render path no longer pulls `DxIntrusivePtr.h` into `shadercompiler.h`, `shadercompiler.cpp` now provides a no-op bootstrap implementation under `PW_LINUX_NULL_RENDER`, and `multishader.cpp` now uses the proper renderer/aligned-allocation declarations and a cross-platform `snprintf` path.
- The Linux render probe now also compiles the UI renderer/cache slice: `uirenderer.cpp`, `FlashRenderer.cpp`, `UITextureAtlasItem.cpp`, and `UITextureCache.cpp`. The Linux null-render path now has no-op but linkable implementations for the UI renderer, Flash renderer, and bitmap/texture cache layer, and the shared UI texture-cache headers now pull the real `Texture2DRef` definition plus the texture-manager helpers they already rely on.
- The Linux render probe now also compiles the generated render DB glue slice: `DBRender.cpp`, `DBRenderResources.cpp`, and `shaderdefinestable.cpp`. That moves the maintained Linux path through the real render enum/resource registration layer instead of treating it as an offline-only generated dependency.
- The Linux render probe now also compiles the stale generated material/runtime slice `DebugMaterial.cpp`, `GrassMaterial.cpp`, and `MaterialResourceInterface.cpp` through Linux null-render/bootstrap-safe wrappers. On Linux, those files no longer try to instantiate the obsolete generated material API; they stay inert while the maintained probe keeps the current Windows-generated path intact under the existing `#else` branches.
- The Linux render probe now also compiles `facefxsystem.cpp` through a Linux bootstrap no-op path. That keeps the maintained render slice honest for this repo state, where the FaceFX vendor SDK headers are not present.
- The Linux render probe now also compiles the shader/runtime utility slice `DeviceLost.cpp`, `ReadDDS.cpp`, `SHCoeffs.cpp`, `VertexColorStream.cpp`, and `shaderconstantsbinder.cpp` through Linux null-render/bootstrap-safe translation-unit branches.
- The Linux render probe now also compiles the first screen-space post-processing slice `Blur.cpp`, `Bloom.cpp`, `ConstantProtection.cpp`, and `FullScreenFX.cpp` through Linux null-render branches. That moves the maintained slice beyond pure renderer infrastructure and into real screen-space runtime code.
- The Linux render probe now also compiles `AOERenderer.cpp` and `WaterConvexes.cpp`. `AOERenderer.cpp` now gates one Windows-only material specialization call on the Linux null-render path, and `WaterConvexes.cpp` builds unchanged as a real water geometry utility translation unit.
- The Linux render probe now also compiles the first maintained animation-graph slice above the older scene/runtime wall: `AnimGraphApplicator.cpp`, `AnimGraphBlender.cpp`, `AnimGraphController.cpp`, `Animators.cpp`, `BitMap.cpp`, `HeightsController.cpp`, and `TimeMutator.cpp`. Linux now has a cross-platform `_alloca` path in `AnimGraphApplicator.cpp`, GCC-safe callback declarations and string-copy usage in `AnimGraphController.cpp`, and a bootstrap terrain height-version shim in `Terrain.h` so these files build inside the real maintained probe rather than staying as one-off probes.
- The Linux render probe now also compiles the next scene/terrain runtime slice: `CollisionHull.cpp`, `DBSceneBase.cpp`, `DiAnGrAPI.cpp`, `DiAnGrCl.cpp`, `DiAnGrNLinker.cpp`, `DiAnGrSStorage.cpp`, `DiAnGrUp.cpp`, `DiAnGrUtils.cpp`, `DiGraph.cpp`, `LandPlacementMutator.cpp`, `DBTerrain.cpp`, `GrassLayersManager.cpp`, `GrassRegion.cpp`, `GrassRenderManager.cpp`, `NatureMap.cpp`, `SpeedGrass.cpp`, and `TerrainElementManager.cpp`. That moves the maintained Linux path through more of the legacy animation-graph support layer and into the first wider terrain runtime/cache layer without reopening the old top-level Direct3D include wall.
- The Linux render probe now also compiles the next terrain cache/runtime layer: `TerrainHeightManager.cpp`, `TerrainLayerManager.cpp`, `TerrainMaterialCache.cpp`, and `TerrainTextureCache.cpp`. The Linux null-render material surface now exposes the terrain pins and frozen/burned texture samplers those files expect, `ArrayInVRAM.h` now sees the null-render texture/renderer declarations it uses, and `TerrainTextureCache.cpp` no longer calls the Windows-only `EvictManagedResources()` path on Linux.
- `dxutils.h` now exposes the extra Win/D3D stub surface needed by that slice on Linux, including `IUnknown`, `IDirect3DDevice9`, `IDirect3DResource9`, `HMONITOR`, `WCHAR`, and `S_OK`/`E_FAIL`.
- `System/tchar_compat.h` now provides a narrow Linux-safe `_tcscat_s`, `_stprintf_s`, and `sprintf_s` compatibility layer for legacy render code that still includes `<tchar.h>` and MSVC CRT helpers on the Linux null-render path.
- `TimedChannel.h` now has the GCC `typename` fixes needed for deeper animation/runtime probes, and the Linux null-render `RenderStatesManager` stub now lives in `renderstatesmanager.h` instead of colliding with `renderer.h`.
- `MemoryLib/UserMessage.h` no longer hard-depends on `Windows.h` for Linux bootstrap probes.
- The Linux null-render `dxutils` path is now self-contained and no longer depends on `d3d9.h`, `objbase.h`, or `DeviceLost.h` support glue during bootstrap probes.
- Linux null-render texture management now has a maintained bootstrap path in `TextureManager.cpp`, so the render probe no longer stops at the old texture/surface D3D smart-pointer seam.
- Linux case-sensitivity aliases were added for `materialspec.h`, `Material.h`, and `RenderResourceManager.h` so deeper render resource probes can get past Windows-only include casing.
- Linux case-sensitivity aliases now also cover `ConfigDatabase.h`, `Renderer.h`, `MultiShader.h`, and `System/FixedVector.h`, which moves the maintained render slice past more Windows-only include casing.
- Linux case-sensitivity aliases now also cover `DebugRenderer.h`, which removed one more Windows-only include-case seam in scene/runtime code.
- Linux case-sensitivity aliases now also cover legacy Windows PCH spellings via `Scene/StdAfx.h`, `Render/StdAfx.h`, and `Terrain/StdAfx.h`, which unblocks deeper scene/render/terrain files that still include `StdAfx.h`.
- `RenderComponent.h` now has a GCC-safe `DECLARE_INSTANCE_COUNTER`, and the shared mesh declaration layer in `MeshResource.h` is Linux-safe enough for the widened mesh probes.
- `Terrain.h` now splits its Linux bootstrap path correctly: scene/null-render probes use the real terrain manager headers, while gameplay bootstrap keeps lightweight terrain manager shims, so the maintained render and gameplay probes stay green at the same time.
- `AnimatedPlacement.h` and `SceneObjectTrack.cpp` now include `DBScene.h` directly, which removes one more generated-DB type visibility seam on Linux.
- `SceneObjectsPool.h` now forward-declares `NDb::DBSceneObject`, and `SceneObjectsPool.cpp` now includes `DBScene.h` plus `DxResourcesControl.h`, which gets the scene object pool layer past the old DB/resource-control split.
- The Linux bootstrap terrain shim now exposes `GetHeightsAsFloat()` for `ScenePassabilityMask.cpp`, and `ScenePassabilityMask.h` accessor returns were fixed.
- `Render/debugRenderer.h` was added as a case-safe alias for the legacy lowercase include used by deeper scene/runtime files.
- `PrimeWorldLinuxClient` linkability now depends on `runtime_stubs.cpp` being compiled under the Linux null-render/bootstrap defines, where it now provides the narrow `CastToObjectBaseImpl<NScene::SceneComponent>` and `CastToObjectBaseImpl<Render::Texture>` specializations plus bootstrap-safe `LoadTexture2D` and `LoadTexture2DIntoPool` stubs needed by the generated render DB/resource layer without dragging the full Windows renderer path into the Linux client target.
- Several Windows-path, case-sensitivity, GCC, and DB/bootstrap blockers were already removed.

## What Is Not Done Yet

- The real playable client path is still blocked by the Direct3D 9 renderer and scene/render coupling.
- The current Linux executable is still a native bootstrap shell, not the real in-game client loop.
- Real Linux gameplay rendering, production UI, audio, and final client runtime integration are not running yet.

## Main Remaining Milestones

### 1. Renderer escape hatch

Create a Linux-safe null-render or minimal renderer path that lets the real client startup move past the current Direct3D 9 wall.

Success condition:
- real client startup goes further than the current bootstrap shell without including `d3d9.h` on the Linux path

### 2. First real client startup on Linux

Move from shell/bootstrap behavior into more of the actual client runtime, even if visual output is incomplete.

Success condition:
- Linux client enters the real client loop with native startup, real runtime objects, and no D3D dependency on the Linux path

### 3. First in-game render

Bring up the first native Linux-rendered scene, even if minimal or visually wrong.

Success condition:
- map scene renders on Linux with enough runtime integration to observe live world state

### 4. First playable local match

Reach a playable local game on Linux with input, world simulation, basic UI visibility, and stable rendering.

Success condition:
- launch a local match and control a hero natively on Linux

### 5. Stabilization and parity

Fix correctness, UI gaps, audio, launcher integration, and wider gameplay/client regressions.

Success condition:
- Linux client is stable enough to ship publicly without breaking the Windows client

## Active Blockers

### Primary blocker

The real renderer/backend path is still centered on Direct3D 9 and Windows-specific render infrastructure, especially:

- `Src/Render/renderer.h`
- `Src/Render/smartrenderer.h`
- `Src/Render/DXManager.cpp`
- `Src/Render/RenderInterface.cpp`
- `Src/Render/DxIntrusivePtr.h`
- `Src/Render/dxutils.cpp`
- `Src/Render/texture.h`
- `Src/Render/rendersurface.h`

### Secondary blocker

Scene and client runtime code still pull render-side types too early. The Linux path needs a cleaner seam between gameplay/runtime code and scene/render code.

### Current gameplay frontier

The maintained gameplay probe is now past the old include/case-sensitivity wall and also past the previous heavy ability timing slice. The next frontier is no longer `PFAbilityData.cpp` or `PFBaseAttackData.cpp`, but the render-facing gameplay/client runtime seam, especially units that still depend on the real client render path and scene-side rendering infrastructure.

### Current render frontier

The maintained render probe is now past the old mesh implementation wall, the first core scene-object layer, the camera/runtime control slice above full-scene orchestration, the scene DB/object utility slice above that, the render/runtime utility layer around config, immediate rendering, DX resource bookkeeping, the batch-adjacent bounding-volume slice, the generated render DB/resource layer, the first screen-space post-processing slice, and the first AOE/water utility slice.

The next deeper practical blockers are no longer the material/light/runtime seam, `SHGrid.cpp`, the basic render-foundation slice, the first bounding-volume seam, the generated render DB glue, the first screen-space effects slice, the first AOE/water utility slice, or the previous terrain runtime/cache seam. Those slices are now in the maintained render probe, so the next frontier is the remaining legacy `DiAnGr` core plus the stale/retired scene-loader edge cases that still sit outside the maintained null-render surface.

Current exact blockers:

- The generated material/runtime slice (`Render/DebugMaterial.cpp`, `Render/GrassMaterial.cpp`, `Render/MaterialResourceInterface.cpp`) is no longer outside the maintained probe. On Linux it now stays behind null-render/bootstrap-safe wrappers because the generated API in those files has drifted from the current DB/material surface.
- `Render/facefxsystem.cpp` is also now inside the maintained probe. The Linux path uses a no-op bootstrap implementation because this repository does not ship the FaceFX SDK headers (`FxSDK.h`, `FxActor.h`, `FxActorInstance.h`), so the next work should not assume those vendor headers exist locally.
- `Render/mesh.cpp` is still a legacy render portability blocker identified by standalone Linux probing, but it is clearly outside the maintained render path. The Linux path stops immediately because `mesh.h` includes a nonexistent `submesh.h`, and the file still uses the older `SubMesh`/legacy `DipDescriptor` API rather than the current `MeshGeometry` path, so this remains a compatibility or retirement decision, not just a case-fix.
- Outside the probe, the remaining obvious Windows-only render helpers are `Render/VidMemViaDDraw.cpp` and `Render/VidMemViaWMI.cpp`. Those are not meaningful Linux runtime wins; they should stay deprioritized behind the real renderer/runtime path unless a build-system reason forces them earlier.
- `Render/batch_linux_null.cpp` is now legacy scaffolding rather than the active seam. The maintained render probe already compiles the real `batch.cpp`, so future work should prioritize the remaining real runtime files still outside the probe instead of extending the older null batch shim.
- `Scene/DiAnGr.cpp` and the remaining deeper `Scene/DiAnGr*.cpp` core files are now the legacy animation-graph seam. The maintained probe is already through `DiAnGrAPI.cpp`, `DiAnGrCl.cpp`, `DiAnGrNLinker.cpp`, `DiAnGrSStorage.cpp`, `DiAnGrUp.cpp`, `DiAnGrUtils.cpp`, and `DiGraph.cpp`, so the remaining failures are now inside the heavier `DiMath` / `DiAnGr` core rather than at top-level render includes.
- `Scene/ModelLoader.cpp` is blocked immediately by a missing `AIGeometry.h` include, which looks like either a stale include path or a retired dependency rather than a Linux renderer problem.
- `Terrain/TerrainMaterialCache.cpp`, `Terrain/TerrainHeightManager.cpp`, `Terrain/TerrainLayerManager.cpp`, `Terrain/TerrainTextureCache.cpp`, `Terrain/NatureMapVisual.cpp`, `Terrain/TerrainElement.cpp`, `Terrain/Terrain.cpp`, `Terrain/TerrainCollision.cpp`, `Terrain/TerrainGeometryManager.cpp`, `Terrain/BezierSurface.cpp`, and `Terrain/NatureAttackSpace.cpp` are no longer outside the maintained probe. The render-probe-specific `PW_LINUX_TERRAIN_RUNTIME_PROBE` split in `Terrain.h` now lets the real terrain runtime compile on Linux while gameplay bootstrap keeps the lightweight terrain surface.
- `Terrain/UberElement.cpp` is still outside the maintained probe, but it is explicitly deprecated in-tree (`#error "This file was deprecated 28.05.2008"`) and depends on missing retired headers (`DummyElement.h`), so it should stay out unless some content unexpectedly revives it.
- `Scene/TimeCtrlSceneComponent.cpp` is still stale legacy code with major DB/runtime API drift, but it is excluded from the normal source list and is not on the critical path right now.

## Current Effort Estimate

Roughly `65-75%` of the hard work still remains before a truly playable native Linux client exists.

Reason:
- bootstrap/startup/content/input groundwork is largely in place
- the renderer/scene/client runtime conversion is still the hardest remaining block

## Maintained Verification Commands

Build the gameplay portability slice:

```bash
cmake --build /tmp/primeworld-linux-bootstrap --target PrimeWorldLinuxGameplayProbe -j4
```

Build the native Linux client shell:

```bash
cmake --build /tmp/primeworld-linux-bootstrap --target PrimeWorldLinuxClient -j4
```

Build the maintained render portability slice:

```bash
cmake --build /tmp/primeworld-linux-bootstrap --target PrimeWorldLinuxRenderProbe -j4
```

Run the latest verified Linux shell smoke test:

```bash
/tmp/primeworld-linux-bootstrap/LinuxBootstrap/PrimeWorldLinuxClient --locale en-US --map Aram --hero shadow --seconds 0.2
```

## Working Rules For Future Updates

- Update this file after each substantial chunk, not after every tiny edit.
- Record the latest verified build/run commands and latest verified log path.
- When a blocker changes, move it here immediately so the next session does not need to reconstruct context from chat.
- Prefer moving the maintained Linux gameplay probe deeper before touching risky shared code blindly.
- Use this document as the source of truth for port status, not chat memory.
