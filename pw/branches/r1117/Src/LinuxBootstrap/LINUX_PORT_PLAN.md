# Linux Native Port Plan

Updated: 2026-04-25

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
- `PrimeWorldLinuxClientRuntimeProbe` builds successfully.
- `PrimeWorldLinuxRenderProbe` builds successfully.
- The native Linux client shell runs, opens a native window, mounts real game content, loads config, input, localization, session data, DB roots, and real map/hero/loading assets.
- `PrimeWorldLinuxClient` now exercises a narrow real client-runtime seam in the executable itself: `PW_Client/LoadingFlashInterface.cpp` and `PW_Client/LoadingStatusHandler.cpp` are linked into the Linux client target, and the Linux `Game.cpp` shell now drives loading-status events through the real runtime handler instead of only using duplicated shell-side status mapping.
- `PrimeWorldLinuxClient` now also links `PW_Client/LoadingHeroes.cpp` directly into the Linux executable. The bootstrap `LoadingFlashInterface` now captures the real loading-lineup state produced by `LoadingHeroes`, so the Linux shell can log and display runtime hero slots, player names, hero titles, and loading progress instead of only the loading-status text path.
- The Linux `Game.cpp` shell now injects synthetic human metadata into the real `MapStartInfo` loading path before `LoadingHeroes` consumes it. The current verified Linux-local-match path carries the human locale through to both `engineStartSlot[...]` and the real loading runtime hero list (`Loading hero metadata: Linux Player / en-US`, `Loading runtime heroes: ... 1 locales ...`) without changing the Windows client path.
- The Linux shell now presents through a real native OpenGL path instead of only X11 pixmap blitting. `System/MainFrame_linux.cpp` now creates a GLX-capable window/context, and `PW_Client/Game.cpp` uploads the existing loading artwork as an OpenGL texture and draws the shell header/panels/text/artwork through OpenGL. The verified smoke run now reports `Overlay backend: OpenGL` in stdout and `overlayBackend=OpenGL` in `linux-client-shell.log`.
- `Render/RenderInterface.cpp` now also has a Linux executable-only OpenGL bootstrap branch behind `PW_LINUX_OPENGL_BOOTSTRAP`. On the Linux client target, the engine-facing render interface can now bind the GLX context through `NMainFrame`, apply the viewport from `RenderMode`, clear through OpenGL, and swap buffers without touching the non-UTF8 legacy `renderer.cpp`. The maintained render probe still uses the pure null-render path, so probe coverage and the Windows renderer path stay isolated from this executable-only bootstrap behavior.
- The Linux shell now also owns a real `Render::DeviceLostWrapper<PF_Render::Interface>` bootstrap object in the executable path. `PW_Client/Game.cpp` now starts that render interface with a Linux `RenderMode`, keeps it sized to the shell window, and routes the frame begin/end path through `PF_Render::Interface::Render()` / `Present()` on top of the OpenGL backend instead of doing shell-side clear/present calls directly. The current verified smoke run reports `Render bootstrap: PF_Render::Interface via OpenGL` plus `Render frame path: PF_Render::Interface::Render/Present` in stdout and records `renderBootstrapReady=yes`, `renderBootstrapFramePath=PF_Render::Interface::Render/Present`, and the active render mode in `linux-client-shell.log`.
- The Linux shell UI now also renders through the canonical `PF_Render::Interface::RenderUI()` path in `PF_GameLogic/PFRenderInterface.cpp` instead of drawing beside the render interface. `PW_Client/Game.cpp` registers a Linux-only UI callback on the bootstrap render interface, the shell frame now reports `Render UI path: PF_Render::Interface::RenderUI callback` in stdout, and the startup log records `renderBootstrapUiPath=PF_Render::Interface::RenderUI callback`.
- The Linux OpenGL bootstrap now also initializes the canonical `Render::IUIRenderer` surface through `PF_Render::Interface::Start()`, releases it through `Stop()`, and keeps the canonical UI resolution state in sync through `UI::UpdateScreenResolution()`. The current verified shell run reports `Render UI resolution: 1820x1024 on 1280x720`, and the startup log records `renderBootstrapUiResolution=1820x1024` plus `renderBootstrapScreenResolution=1280x720`.
- The Linux executable now adopts a narrow canonical `UI::Initialize(...)` / `UI::Release()` lifecycle after the OpenGL render bootstrap is live. `PW_Client/Game.cpp` resolves the real `UI/UIRoot`, calls `UI::Initialize(...)`, and logs `UI runtime: init=yes path=UI::Initialize`. On Linux bootstrap, `UI/Root.cpp` intentionally registers root/screens/content/constants/substitutes while keeping Flash, scripts, cursor creation, font renderer startup, debug UI, and `UI::User` creation out of the executable path until the real client loop needs them. The latest startup log records `uiRootRuntimeInit=yes` and `uiRootRuntimePath=UI::Initialize`.
- The Linux executable now also drives the canonical UI frame lifecycle each frame through `UI::ApplyNewParams(...)`, `UI::NewFrame(...)`, and `UI::PresentFrame(...)` in the OpenGL shell render loop. The verified runtime path now resolves canonical screen and constant lookups through `UI::GetScreenLayout(...)` and `UI::GetConstant(...)`, and the latest smoke run reports `UI frame loop: yes path=UI::ApplyNewParams/NewFrame/PresentFrame`, `UI runtime screen: ClinicResultsScreen -> <unnamed>`, `UI runtime content: test/<empty-group> [empty-group]`, and `UI runtime constant: test = 123`. The same run also proves that the remaining content seam is data-driven rather than a Linux registration bug: `Data/UI/UIRoot.xdb` currently defines one content group, `test`, with an empty `<resources />` list, so the Linux runtime now reports that state explicitly as `empty-group` instead of treating it as a failed content lookup.
- The Linux executable now also exercises the canonical `UI::User` runtime path. `UI/Root.cpp` creates a real `UI::User` on the Linux bootstrap path, `PW_Client/Game.cpp` now drives `UI::User::StartEvent(...)`, `EndEvent(...)`, and `Step(...)`, and the verified runtime now reports `UI user: yes events=0 path=UI::User::StartEvent/EndEvent/Step`.
- The Linux executable now also exercises the canonical cursor bootstrap path without pulling the full Windows texture/material stack into the runnable target. `UI/Root.cpp` now calls `NCursor::Init()`, registers real cursor DB entries on Linux bootstrap, and no longer suppresses `UI::SetCursor(...)` or the frame-end cursor update in `UI::PresentFrame(...)` on Linux bootstrap. `runtime_stubs.cpp` provides the narrow bootstrap `TextureManager` and `ImageComponent` surface needed by that path. The current verified runtime reports `UI cursor: ready=yes registered=yes id=ui_normal size=29x48 hotspot=1,1 path=UI::SetCursor/NCursor::Update`.
- The Linux executable now also takes the first real screen-layout step instead of stopping before layout initialization. `PW_Client/SelectGameModeScreen.cpp` now lets the Linux bootstrap call `LoadScreenLayout("Lobby_SelectGameMode")`, and `PW_Client/Game.cpp` now reports the resulting runtime window state clearly. The current verified runtime reports `UI screen runtime: ready=yes window=<unnamed:15-children> events=0 path=NGameX::SelectGameModeScreen::Init/LoadScreenLayout/Step/Draw`, which confirms that the canonical `SelectGameModeScreen` now owns a live main window on Linux even though the layout still has no explicit window name.
- The Linux executable now also reaches the first live custom-lobby hero-selection screen on the canonical UI path. `PW_Client/Game.cpp` now accepts `--bootstrap-create-game`, requests the narrow custom-lobby flow through the existing Linux bootstrap context, and then drives `NGameX::SelectHeroScreen::Init/CommonStep/Draw` directly. The current verified runtime reports `UI hero runtime: ready=yes window=<unnamed:13-children> players=6 path=NGameX::SelectHeroScreen::Init/CommonStep/Draw`, which confirms that Linux now gets past the game-mode list and into the first live hero-selection screen without touching the Windows transport path.
- The Linux executable now also keeps the live custom-lobby hero-selection state synchronized with the canonical bootstrap game context instead of only drawing the screen. `PW_Client/Game.cpp` now pushes the selected team/hero from the Linux local-match preview into `LinuxBootstrapGameContextUi`, keeps faction aligned on the bootstrap path when appropriate, and reports the resulting state from the live `SelectHeroScreen` runtime. The current verified custom-lobby run reports `UI hero state: team=2 faction=2 ready=no hero=shadow`, which proves that the live hero-selection screen is now reading the canonical game-context state on Linux instead of only showing a static preview.
- The Linux executable now also crosses the first real custom-lobby game-start boundary and instantiates a canonical `Game::LoadingScreen` on Linux through the existing `MapStartInfo` / `LoadingGameContext` / `LoadingScreenLogic` path. `PW_Client/Game.cpp` now auto-readies the bootstrap custom-lobby flow, builds a real loading context from the selected map/lineup, and drives `Game::LoadingScreen::Init/Step/Draw` directly in the executable path. The current verified runtime reports `UI loading runtime: ready=yes window=<unnamed:2-children> players=7 status=Загрузка path=Game::LoadingScreen::Init/Step/Draw`, which confirms that Linux now moves past the live `SelectHeroScreen` path and into the first real loading-screen transition without touching the Windows transport path.
- The Linux loading runtime seam now also resolves the localized status text correctly on the canonical loading-screen path instead of surfacing a Linux-specific fallback key. `PW_Client/LoadingStatusHandler.cpp` now uses explicit Unicode conversion for the non-localized fallback path, and `PW_Client/Game.cpp` normalizes brace-wrapped loading-status ids back through the already-decoded loading-status table before reporting them. The latest verified log at `Bin/logs/2026.04.25-00.46.11/linux-client-shell.log` records `uiRootRuntimeLoadingScreenReady=yes`, `uiRootRuntimeLoadingScreenPath=Game::LoadingScreen::Init/Step/Draw`, `uiRootRuntimeLoadingPlayerEntries=7`, `uiRootRuntimeLoadingStatusReady=yes`, and `uiRootRuntimeLoadingStatusText=Загрузка`.
- The Linux loading runtime seam now also drives real `LoadingProgress` and per-player progress through the canonical `Game::LoadingScreen` path instead of holding the Linux bootstrap branch at `0.0f`. `PW_Client/Game.cpp` now owns a bootstrap `LoadingProgress`, advances it through the live loading-screen runtime, seeds per-player loading progress for the real lineup, and reports the resulting progress/disconnect state in the Linux overlay/stdout/log. `PW_Client/LoadingScreen.cpp` now uses `progress->GetTotalProgress()` on the Linux bootstrap path too, so the canonical screen logic consumes the same progress source on both Windows and Linux. The latest verified log at `Bin/logs/2026.04.25-03.05.05/linux-client-shell.log` records `uiRootRuntimeLoadingScreenReady=yes`, `uiRootRuntimeLoadingScreenPath=Game::LoadingScreen::Init/Step/Draw`, `uiRootRuntimeLoadingPlayerEntries=7`, `uiRootRuntimeLoadingProgress=0.14`, `uiRootRuntimeLoadingDisconnected=0`, and `uiRootRuntimeLoadingStatusText=Загрузка`.
- The Linux executable and the maintained runtime probe now both compile against the canonical `PF_GameLogic/PFRenderInterface.cpp`. The temporary Linux-only sidecar bootstrap translation unit is gone, and the Linux bootstrap implementation now lives directly in the real source file behind the Linux render guards while the original Windows Direct3D body remains intact under `#else`.
- The maintained Linux gameplay probe currently compiles 70 gameplay `.cpp` files.
- The maintained Linux client runtime probe currently compiles 48 client/runtime/UI/render-bridge `.cpp` files: `Client/DefaultScreenBase.cpp`, `Client/ScreenCommands.cpp`, `Client/MainLoop.cpp`, `PF_GameLogic/PFRenderInterface.cpp`, `PW_Client/CpuTopology.cpp`, `PW_Client/DebugVarsSender.cpp`, `PW_Client/GameContext.cpp`, `PW_Client/GameStatistics.cpp`, `PW_Client/GameStatisticsWrapper.cpp`, `PW_Client/LoadingFlashInterface.cpp`, `PW_Client/LoadingHeroes.cpp`, `PW_Client/LobbyConnection.cpp`, `PW_Client/LocalCmdScheduler.cpp`, `PW_Client/LoadingScreen.cpp`, `PW_Client/LoadingScreenLogic.cpp`, `PW_Client/LoadingStatusHandler.cpp`, `PW_Client/LocalGameContext.cpp`, `PW_Client/NewLobbyClientPW.cpp`, `PW_Client/NewLobbyGameClientPW.cpp`, `PW_Client/NetworkStatusScreen.cpp`, `PW_Client/ReplayRunner.cpp`, `PW_Client/ReplayTransceiver.cpp`, `PW_Client/SelectGameModeLogic.cpp`, `PW_Client/SelectGameModeScreen.cpp`, `PW_Client/SelectHeroScreen.cpp`, `PW_Client/SelectHeroScreenLogic.cpp`, `UI/DebugVarsRender.cpp`, `UI/Cursor.cpp`, `UI/FrameTimeRender.cpp`, `UI/FontRender.cpp`, `UI/FontStyle.cpp`, `UI/ImageComponent.cpp`, `UI/ImageLabel.cpp`, `UI/LuaEventResult.cpp`, `UI/NameMappedWindow.cpp`, `UI/PreferencesProcessor.cpp`, `UI/Resolution.cpp`, `UI/Root.cpp`, `UI/ScreenLogicBase.cpp`, `UI/SkinStyles.cpp`, `UI/Scripts.cpp`, `UI/TextComponent.cpp`, `UI/TextComponentBasic.cpp`, `UI/TextMarkupLexems.cpp`, `UI/TextMarkupParser.cpp`, `UI/User.cpp`, `UI/View.cpp`, and `UI/WindowPointJob.cpp`.
- The Linux UI bootstrap seam is narrower now. `UI/Cursor.cpp`, `UI/FontRender.cpp`, and `UI/FontStyle.cpp` all build under the maintained Linux runtime path, `UI/Root.cpp` now lets the executable adopt `UI::Initialize(...)`, and the Linux executable now also reaches the canonical `UI::User` and cursor bootstrap paths without pulling the full Flash/script/window/material stack into the runnable client too early.
- The maintained Linux render probe now compiles 165 render/scene/terrain `.cpp` files, including the real render-runtime path pieces `batch.cpp`, `DXManager.cpp`, `OcclusionQueries.cpp`, `DBRender.cpp`, `DBRenderResources.cpp`, `shaderdefinestable.cpp`, `DeviceLost.cpp`, `ReadDDS.cpp`, `SHCoeffs.cpp`, `VertexColorStream.cpp`, `shaderconstantsbinder.cpp`, `Blur.cpp`, `Bloom.cpp`, `ConstantProtection.cpp`, `FullScreenFX.cpp`, `AOERenderer.cpp`, `WaterConvexes.cpp`, `CleanGeometry.cpp`, `fxresource.cpp`, `skeletonanimation.cpp`, `SkeletalAnimationBlender.cpp`, `SkeletalAnimationSampler.cpp`, `SkeletonWrapper.cpp`, `Stretcher.cpp`, `dipdescriptor.cpp`, `rendermode.cpp`, `DebugMaterial.cpp`, `GrassMaterial.cpp`, `MaterialResourceInterface.cpp`, `facefxsystem.cpp`, `ModelLoader.cpp`, `TimeCtrlSceneComponent.cpp`, and the now-maintained full `DiAnGr` / terrain-runtime/core slice on top of the earlier null-render mesh, scene, UI, and utility work.
- Recent additions to the maintained client runtime probe now cover the deeper lobby/runtime slice and the first real client screen/UI layer. Linux bootstrap now keeps `GameContext.cpp` inside the maintained runtime target by cutting the transport/ACE initialization path behind `PW_LINUX_DB_BOOTSTRAP`, sharing `EGameStatStatus` through `GameStatStatus.h`, adding a Linux `__time32_t` alias in `System/ported/types.h`, fixing the lobby status include-order seam around `LobbyClientBase.h`, and providing the missing `LobbyUserProxy` / `Peered` declarations for the bootstrap path. On top of that, the lobby layer itself is no longer outside the maintained target: `PW_Game/server_ip.h` now exists in the repo, `Shared/WebRequests.h` has a Linux bootstrap stub path, `LobbyConnection.cpp` has a Linux dummy castle-link path instead of pulling WinSock, `NewLobbyClientPW.cpp` was converted to UTF-8 and now uses `StringExecutorBootstrap.h`, `NewLobbyGameClientPW.cpp` no longer drags the generated `HybridServer/RPeered.auto.h` wrapper into the bootstrap path, `DebugVarsSender.cpp` and `LocalCmdScheduler.cpp` are maintained, and `CpuTopology.cpp` now has a Linux bootstrap implementation instead of depending on `<intrin.h>`. The maintained target now also includes `Client/DefaultScreenBase.cpp`, `PW_Client/SelectGameModeScreen.cpp`, `PW_Client/SelectHeroScreen.cpp`, and the bootstrap-safe UI stack around `UI/Root.cpp`, `UI/User.cpp`, `UI/Cursor.cpp`, `UI/FontRender.cpp`, `UI/FontStyle.cpp`, `UI/ImageComponent.cpp`, `UI/ImageLabel.cpp`, `UI/NameMappedWindow.cpp`, `UI/PreferencesProcessor.cpp`, `UI/ScreenLogicBase.cpp`, `UI/SkinStyles.cpp`, `UI/Scripts.cpp`, `UI/TextComponent.cpp`, `UI/TextComponentBasic.cpp`, `UI/TextMarkupLexems.cpp`, `UI/TextMarkupParser.cpp`, `UI/View.cpp`, and `UI/WindowPointJob.cpp`. The intentionally excluded UI slice is narrower now: it is primarily the heavier Flash/container/vendor path and the old encoding-heavy `UI/Window.cpp` / `UI/TextMarkup.cpp` seam rather than fonts or cursors.
- The Linux executable no longer tried to link the entire `PrimeWorldLinuxClientRuntimeProbe` object library. That experiment was intentionally backed out because it exploded into unrelated transport/UI/link dependencies. Instead, the executable now links only the narrow runtime pieces it needs for the first real runtime seam: `PW_Client/LoadingFlashInterface.cpp` and `PW_Client/LoadingStatusHandler.cpp`. The Linux `Game.cpp` shell now builds a bootstrap-safe `LoadingFlashInterface`, dispatches real loading events through `LoadingStatusHandler`, records the real runtime status key, and resolves the displayed localized status text through the already-loaded `DBUIData` preview table.
- Recent additions to the maintained render probe are the animation-graph/time-control slice (`AnimGraphApplicator.cpp`, `AnimGraphBlender.cpp`, `AnimGraphController.cpp`, `Animators.cpp`, `BitMap.cpp`, `HeightsController.cpp`, `TimeMutator.cpp`), the next scene/terrain runtime slice (`CollisionHull.cpp`, `DBSceneBase.cpp`, `DiAnGrAPI.cpp`, `DiAnGrCl.cpp`, `DiAnGrNLinker.cpp`, `DiAnGrSStorage.cpp`, `DiAnGrUp.cpp`, `DiAnGrUtils.cpp`, `DiGraph.cpp`, `LandPlacementMutator.cpp`, `DBTerrain.cpp`, `GrassLayersManager.cpp`, `GrassRegion.cpp`, `GrassRenderManager.cpp`, `NatureMap.cpp`, `SpeedGrass.cpp`, `TerrainElementManager.cpp`), the terrain cache/runtime layer (`TerrainHeightManager.cpp`, `TerrainLayerManager.cpp`, `TerrainMaterialCache.cpp`, `TerrainTextureCache.cpp`), the remaining maintained terrain core (`NatureMapVisual.cpp`, `Terrain.cpp`, `TerrainCollision.cpp`, `TerrainElement.cpp`, `TerrainGeometryManager.cpp`, `BezierSurface.cpp`, `NatureAttackSpace.cpp`), and now the rest of the legacy animation-graph core (`DiAnGr.cpp`, `DiAnGrDb.cpp`, `DiAnGrEditor.cpp`, `DiAnGrExtPars.cpp`, `DiAnGrExtParsAd.cpp`, `DiAnGrG.cpp`, `DiAnGrIo2.cpp`, `DiAnGrMarker.cpp`). Those files now build in the maintained Linux render probe instead of only passing ad-hoc syntax checks.
- Standalone Linux null-render syntax probes for `AnimatedSplitSceneComponent.cpp`, `AttacherSceneComponent.cpp`, `Camera.cpp`, `CameraControllersContainer.cpp`, `CameraInputModifier.cpp`, and `FreeCameraController.cpp` are now green or superseded by maintained render-probe builds.
- Latest verified runtime command:

```bash
/tmp/primeworld-linux-bootstrap/LinuxBootstrap/PrimeWorldLinuxClient --locale en-US --map Aram --hero shadow --seconds 0.2
```

The current deeper custom-lobby verification command is:

```bash
/tmp/primeworld-linux-bootstrap/LinuxBootstrap/PrimeWorldLinuxClient --root /home/vitaly/p/Prime-World/pw/branches/r1117 --locale en-US --map Aram --hero shadow --bootstrap-create-game --seconds 0.2
```

- Latest verified runtime log:

```text
/home/vitaly/p/Prime-World/pw/branches/r1117/Bin/logs/2026.04.25-00.03.39/linux-client-shell.log
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
- A maintained Linux client runtime compile target exists: `PrimeWorldLinuxClientRuntimeProbe`.
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
- `Scene/ModelLoader.cpp` is now inside the maintained Linux render probe. The Linux path no longer depends on the stale primitive/material API there: it now uses the current `CreateVB` / `CreateIB` helpers, current `SubMesh` / `Primitive` setup, current `CreateRenderMaterial` path, and 32-bit packed vertex colors so the legacy AIGeometry loader compiles correctly on 64-bit Linux.
- `Scene/TimeCtrlSceneComponent.cpp` is now also inside the maintained Linux render probe through a Linux null-render/bootstrap stub. The Windows code path is unchanged, but the Linux probe no longer treats this excluded legacy component as an unresolved scene portability hole.
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
- The Linux client/runtime slice is now well past shell-only code. `MainLoop.cpp`, `GameContext.cpp`, `GameStatistics.cpp`, `GameStatisticsWrapper.cpp`, `LoadingFlashInterface.cpp`, `LoadingHeroes.cpp`, `LoadingScreen.cpp`, `LoadingScreenLogic.cpp`, `LoadingStatusHandler.cpp`, `LocalGameContext.cpp`, `NetworkStatusScreen.cpp`, `ReplayRunner.cpp`, `ReplayTransceiver.cpp`, `SelectGameModeLogic.cpp`, `SelectGameModeScreen.cpp`, `SelectHeroScreen.cpp`, and `SelectHeroScreenLogic.cpp` all compile under `PW_LINUX_NULL_RENDER`, `PW_LINUX_DB_BOOTSTRAP`, and `VISUAL_CUTTED`, and are maintained by the `PrimeWorldLinuxClientRuntimeProbe` target instead of ad-hoc compiler invocations.

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

### Current client runtime frontier

The maintained client runtime probe is now past the earlier UI/status/replay wall, past the deeper lobby layer, past the remaining lightweight runtime helpers (`CpuTopology.cpp`, `DebugVarsSender.cpp`, `LocalCmdScheduler.cpp`), and into the real client screen implementation layer through `Client/DefaultScreenBase.cpp`, `PW_Client/SelectGameModeScreen.cpp`, `PW_Client/SelectHeroScreen.cpp`, and the wider UI class stack. Replay is still intentionally stubbed on Linux bootstrap, but it is no longer a compile blocker.

The next meaningful client-runtime work is no longer those old lobby blockers, and it is no longer only the loading-status seam either. The executable now exercises a real runtime status path through `LoadingStatusHandler` and a bootstrap `LoadingFlashInterface`, with localized runtime text resolving correctly in both the Linux shell (`login: connecting -> Подключение`) and the real `Game::LoadingScreen` transition (`status=Загрузка`), it exercises the real `LoadingHeroes` lineup path so the shell captures runtime hero slots, resolved hero titles, load percentages, and the first synthetic human metadata path into the real loading seam (`Linux Player / en-US`, `engineStartSlot[2].locale=en-US`, `Loading runtime heroes: ... 1 locales ...`), it now also drives real `LoadingProgress` and per-player loading updates through the canonical `Game::LoadingScreen` path (`uiRootRuntimeLoadingProgress=0.14`, `uiRootRuntimeLoadingDisconnected=0`), it adopts the canonical UI lifecycle after render bootstrap startup through `UI::Initialize(...)`, `UI::ApplyNewParams(...)`, `UI::NewFrame(...)`, and `UI::PresentFrame(...)`, and it now moves from the live custom-lobby hero-selection screen through the first real `Game::LoadingScreen::Init/Step/Draw` transition. The current runtime proof also reaches the real UI root lookup APIs: `UI::GetScreenLayout(...)` and `UI::GetConstant(...)` both resolve in the Linux executable, while `UI::GetContentResource(...)` is now known to be blocked by data shape rather than Linux registration, because `Data/UI/UIRoot.xdb` currently exposes a single empty content group (`test`) with no resources. The next client-runtime seam is therefore deeper real game-start/runtime adoption above the first live `Game::LoadingScreen` transition, not re-debugging content registration or the already-working loading-status text path. The non-maintained client files left in `PW_Client/` are:

- `PW_Client/FullScreenTest.cpp`, which is a Windows-only test utility and still stops at `windows.h`
- `PW_Client/RegistryToolbox.cpp`, which is still tied to Win32 registry types through `RegistryStorage.h`
- `PW_Client/Game.cpp`, which is already part of the Linux client executable, but is still the next real integration seam because moving toward a playable client now means pushing the Linux path deeper into the actual game loop instead of only widening the maintained bootstrap/runtime slice

For first playable Linux work, `FullScreenTest.cpp` and `RegistryToolbox.cpp` are not on the critical path. The next practical frontier is deeper `Game.cpp` runtime integration on top of the maintained lobby/runtime slice.

The intentionally unmaintained UI/runtime files are now mostly the Flash/vendor and encoding-heavy seam rather than basic runtime code:

- `UI/ActionContext.cpp`, which still depends on the stale missing `UIBase.h`
- `UI/FlashContainer2.cpp`, `UI/FlashFontsRender.cpp`, and `UI/FlashInterface.cpp`, which still pull the Tamarin/Flash vendor path and `windows.h`
- `UI/Window.cpp` and `UI/TextMarkup.cpp`, which are still non-UTF8 legacy sources and should stay out until they are worth converting rather than patching around

### Current gameplay frontier

The maintained gameplay probe is now past the old include/case-sensitivity wall and also past the previous heavy ability timing slice. The next frontier is no longer `PFAbilityData.cpp` or `PFBaseAttackData.cpp`, but the render-facing gameplay/client runtime seam, especially units that still depend on the real client render path and scene-side rendering infrastructure.

### Current render frontier

The maintained render probe is now past the old mesh implementation wall, the first core scene-object layer, the camera/runtime control slice above full-scene orchestration, the scene DB/object utility slice above that, the render/runtime utility layer around config, immediate rendering, DX resource bookkeeping, the batch-adjacent bounding-volume slice, the generated render DB/resource layer, the first screen-space post-processing slice, the first AOE/water utility slice, the remaining non-test scene slice, and a broader shadow/water/runtime layer than the older roadmap reflected.

The next deeper practical blockers are no longer `DiAnGr`, `ModelLoader.cpp`, `TimeCtrlSceneComponent.cpp`, or basic scene compile coverage. The next frontier is replacing the current null-render/bootstrap maintenance surface with real Linux renderer/runtime integration so the client can move beyond the shell and into the actual client loop.

Current exact blockers:

- The generated material/runtime slice (`Render/DebugMaterial.cpp`, `Render/GrassMaterial.cpp`, `Render/MaterialResourceInterface.cpp`) is no longer outside the maintained probe. On Linux it now stays behind null-render/bootstrap-safe wrappers because the generated API in those files has drifted from the current DB/material surface.
- `Render/facefxsystem.cpp` is also now inside the maintained probe. The Linux path uses a no-op bootstrap implementation because this repository does not ship the FaceFX SDK headers (`FxSDK.h`, `FxActor.h`, `FxActorInstance.h`), so the next work should not assume those vendor headers exist locally.
- `Render/mesh.cpp` is no longer outside the maintained probe. It now builds on Linux through the current compatibility layer, so it is no longer an active compile blocker even though it remains legacy render code.
- Outside the probe, the remaining obvious Windows-only render helpers are `Render/VidMemViaDDraw.cpp` and `Render/VidMemViaWMI.cpp`. Those are not meaningful Linux runtime wins; they should stay deprioritized behind the real renderer/runtime path unless a build-system reason forces them earlier.
- `Render/batch_linux_null.cpp` is now legacy scaffolding rather than the active seam. The maintained render probe already compiles the real `batch.cpp`, so future work should prioritize the remaining real runtime files still outside the probe instead of extending the older null batch shim.
- `Render/RenderInterface.cpp` is no longer only a no-op on the Linux executable path, and the Linux shell now constructs and runs a real `Render::DeviceLostWrapper<PF_Render::Interface>` through the canonical `PF_GameLogic/PFRenderInterface.cpp` file. The Linux bootstrap branch there now owns constrained frame setup plus `Render()`, `RenderUI()`, and `Present()` for the shell, but it is still a narrow OpenGL bootstrap implementation. The next real render/backend seam is deeper inside that same file, where the full Windows `PF_Render::Interface` body still assumes the broader Direct3D 9 renderer/runtime stack.
- `Scene/DiAnGr.cpp`, `Scene/DiAnGrDb.cpp`, `Scene/DiAnGrEditor.cpp`, `Scene/DiAnGrExtPars.cpp`, `Scene/DiAnGrExtParsAd.cpp`, `Scene/DiAnGrG.cpp`, `Scene/DiAnGrIo2.cpp`, and `Scene/DiAnGrMarker.cpp` are no longer outside the maintained probe. The Linux path now has the missing `DiMath` include alias, safe non-MSVC `strcpy_s` / `sprintf_s` compatibility shims, direct DB type visibility for `DiAnGrExtPars`, and a manual marker-ring teardown in `DiAnGrG.cpp` that avoids `ring::Clear()` ambiguity for refcounted ring elements.
- `Scene/ModelLoader.cpp` is no longer an active blocker. The local `AIGeometry.h` compatibility definition remains in-tree, but the loader now builds through the current mesh/material/runtime API and is maintained as part of the render probe.
- The next scene/runtime frontier is no longer `DiAnGr`, `ModelLoader.cpp`, or `TimeCtrlSceneComponent.cpp`. The maintained probe now covers every non-test, non-`stdafx`, non-deprecated scene translation unit. The remaining work is no longer scene compile coverage; it is moving from the null-render/bootstrap path toward real client runtime integration and a real Linux renderer backend.
- `Terrain/TerrainMaterialCache.cpp`, `Terrain/TerrainHeightManager.cpp`, `Terrain/TerrainLayerManager.cpp`, `Terrain/TerrainTextureCache.cpp`, `Terrain/NatureMapVisual.cpp`, `Terrain/TerrainElement.cpp`, `Terrain/Terrain.cpp`, `Terrain/TerrainCollision.cpp`, `Terrain/TerrainGeometryManager.cpp`, `Terrain/BezierSurface.cpp`, and `Terrain/NatureAttackSpace.cpp` are no longer outside the maintained probe. The render-probe-specific `PW_LINUX_TERRAIN_RUNTIME_PROBE` split in `Terrain.h` now lets the real terrain runtime compile on Linux while gameplay bootstrap keeps the lightweight terrain surface.
- `Terrain/UberElement.cpp` is still outside the maintained probe, but it is explicitly deprecated in-tree (`#error "This file was deprecated 28.05.2008"`) and depends on missing retired headers (`DummyElement.h`), so it should stay out unless some content unexpectedly revives it.
- `Scene/TimeCtrlSceneComponent.cpp` still targets removed DB/runtime APIs on Windows, which is why it remains excluded from the normal source list. For Linux maintenance it now has a probe-only null-render stub, so it is no longer an open portability blocker.

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

Build the client runtime portability slice:

```bash
cmake --build /tmp/primeworld-linux-bootstrap --target PrimeWorldLinuxClientRuntimeProbe -j4
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
