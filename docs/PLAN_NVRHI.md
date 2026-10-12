# NVRHI в client-modern: план (DX12/Vulkan, D3D9 удаляется)

Статус: **план, кода нет.** Ветка `client-modern` (worktree
`prime-world-classic-client-modern/`), рендер — `pw/branches/r1117/Src/Render/`.

## Решение (принято)

- **Цель:** выбор рендерера **Vulkan или DX12** + упрощение переноса клиента на
  Linux. **D3D9-путь удаляется**, параллельной поддержки двух бэкендов нет.
- **Визуальный дрейф допустим** (в т.ч. в тенях/PostFX) — это снимает главный
  стопор для vendor-форматов глубины.
- NVRHI берётся как слой над DX12/Vulkan. Обоснование: два GAPI + Linux из
  коробки, state tracking и binding-модель готовые, дальше DLSS/RTXSDK.

### Порядок и границы (уточнено)

- **Vulkan — первый и дефолтный бэкенд.** DX12 — опция позже.
- **DXVK не рассматривается.** На Windows Vulkan идёт через нативный
  Vulkan-драйвер; Wine — только нативный Vulkan (wined3d Vulkan-бэкенд /
  `winevulkan`), не DXVK-трансляция D3D.
- **Кросс-платформенности пока нет:** клиент остаётся Windows-сборкой
  (Wine/native x64, тот же граф `build_client_x64_*`), тесты — **на tiny10**
  (`vm10/`, скил `pw-client`). Linux-контур — отдельная задача после этапа 4,
  здесь только требования к коду (не тащить `HWND` в `IRenderBackend`).
- Ветка — **`nvrhi`** (от `client-modern`).

### Гейт качества (замена «золотых unitstats»)

Рендер-этапы ломают байтовый паритет по определению, поэтому:

| Тип правки | Гейт |
|---|---|
| вне рендер-пути (этап 1 — зачистка мёртвого, ликвидация утечек) | обычный: x86 VS2008 + x64 Wine + x64 native + E2E tiny10 + **золотые `unitstats` байт-в-байт** |
| рендер-этапы (2, 3, 4) | сборка x64 (Wine + native) + **E2E на tiny10: вход → лобби → бой** + скриншот-дифф (новый эталон фиксируется в ветке) + `unitstats` как smoke (бой/физика живы, значения не эталон) |

Из «удаление D3D9» следует смена стратегии: не «шов + два бэкенда», а
**перезапись `Src/Render` поверх NVRHI с сохранением верхнего контракта**
(`Render::Interface`, `SmartRenderer`, `ImmRenderer`, `Material`, `Batch`,
`RenderResourceManager`, шейдерные `.hlsl`/`.shd`/`MaterialSpec.xml`).
Верхние проекты (`Scene`, `UI`, `Game`, `PF_GameLogic`, `Terrain`) правятся
только в местах утечек D3D9 (см. §2).

---

## 1. Замер п.3: сколько реально пайплайнов

### 1.1 Верхняя граница (то, что заявлено в контенте)

Посчитал заголовки `.shd` (прекомпилированные uber-shader-вариации,
`Data/Shaders/UberShaders/`, формат: `uint32 count` + массив `{vsOffset,
psOffset}`):

| Файл | пар VS+PS |
|---|---|
| `BasicMaterial.shd` | **36 864** |
| `DropMaterial.shd` | 6 720 |
| `UIFlashMaterial.shd` | 4 096 |
| `DecalMaterial/DecalTerrainMaterial.shd` | 384 + 384 |
| `BasicWeatherMaterial.shd` | 64 |
| остальные 30 файлов | 2…256 каждый |
| **Итого, 36 файлов** | **49 966 пар** |

Это **потенциал**, не фактическое потребление: `MultiShader::OpenFile` читает
таблицу целиком, а `GetShaderPairByIndex(index)` компилирует бинарник
**лениво** — только затребованные индексы.

### 1.2 Фактическое потребление — счётчики были нерабочими (исправлено)

`smartrenderer.cpp` задумывал давать ровно нужную метрику:
`numVSUniq/numPSUniq` (уникальных VS/PS за кадр), `numVSTotal/numPSTotal`
(за сессию). Но весь блок (объявления счётчиков, `hash_set psMap/vsMap`,
`insert` в `BindVertexShader/BindPixelShader`, `SetValue` в `OnFrameStart()`)
стоял под `#ifndef _SHIPPING`. E2E/tiny10 гоняют **ShippingSingleExe**, т.е.
в прогоняемой сборке счётчиков не было вовсе.

Исправлено: блок переведён на `USE_RENDER_SHADER_STATS`, который включается
либо в не-shipping, либо по `-DPW_RENDER_SHADER_STATS` в shipping:

```sh
PW_EXTRA_DEFS=-DPW_RENDER_SHADER_STATS ./build_client_x64_wine.sh
```

Добавлена метрика **`numShaderPairUniq` / `numShaderPairTotal`** — число
уникальных пар (VS, PS). Это ядро ключа пайплайна; именно оно, а не число
вариаций в `.shd`, определяет размер кэша `IGraphicsPipeline`.

**Оставшееся действие:** собрать shipping с этим дефайном и снять цифры в бою
на tiny10 (см. §6, пункт «замеры»).

### 1.3 Что домножает число пайплайнов

Ключ пайплайна в NVRHI = (VS, PS, InputLayout, `RenderState`, форматы RT).
- `RenderState` в клиенте уже **bitmask** (`states.h`: blendMode:3,
  alphaTest:1, alphaTestRef:8, culling:1, emissive:1) → ≤ 2⁵=32 реальных
  комбинаций (alphaTestRef в ключ не входит, он в константах).
- Форматы RT: 2 цвета (A8R8G8B8 + A16B16G16R16F) + несколько depth → ~6.
- InputLayout: `VertexFormatDescriptor`-кэш уже есть (`VertexDeclaraionsCash`).

Оценка: **49 966 × ~32 × ~6 ≈ верхняя граница 10⁷**, но фактически
`MaterialManager::GetShadersID` уже сейчас дедуплицирует по
`(multiShaderIndex, staticShaderIndex)`, а `sortId` (CRC материала)
дедуплицирует состояние+сэмплеры — т.е. реальное число **разных**
(шейдер-пара × состояние) в каду ограничено числом отрисованных батчей.
`GetSortId()`/`EqualIDs()` — готовый хеш-ключ, его и надо использовать как
ключ кэша пайплайнов.

**Обязательная работа перед этапом 3:** снять реальные
`numShaderPairTotal` + число уникальных `sortId` за бой. Без этой цифры
нельзя спроектировать кэш пайплайнов (размер, eviction). Счётчик пар —
см. §1.2, он готов; счётчик уникальных `sortId` — не добавлен.

---

## 2. Поверхность D3D9, которую надо убрать

### 2.1 Утечки D3D9 за пределы `Src/Render` — полный список (пересчитан)

**Текущие цифры (после этапа 1):** `GetDevice()` — 190, из них 145 в `Src/Render`
и **45 снаружи, в 8 файлах**; `IDirect3D*` — 200 ссылок. (Первичная оценка
«344» считала все упоминания `IDirect3D`/`D3DX` по дереву, а не вызовы.)

| Файл | Кол-во | Что именно | На что менять |
|---|---|---|---|
| `PF_GameLogic/PFRenderInterface.cpp` | 33 | 25 «сырых» `SetRenderState` (SRCBLEND/DESTBLEND/BLENDOP, SEPARATEALPHABLEND*, BLENDOPALPHA=MAX/REVSUBTRACT, COLORWRITEENABLE, ALPHATEST*/ALPHAREF/ALPHAFUNC, ZENABLE/ZWRITEENABLE), 2 `SetSamplerState(0, D3DSAMP_BORDERCOLOR, 0)`, 2 `SetPixelShaderConstantF(96, …)`, 2 `SetScissorRect`, `StretchRect`, `Clear`, `GetDepthStencilSurface`, 2 `EvictManagedResources` | **главная проблема не в количестве, а в том, что это обход `RenderStatesManager`**: состояния не попадают в `RenderState`-ключ и не восстанавливаются после `SetState()` → порядок вызовов критичен. Правка = перевести на `RenderStatesManager::SetState`/`SetStateDirect` (одна точка истины), `StretchRect` → `copyTexture`/blit, `Clear` → `clearRenderTargetView`, `GetDepthStencilSurface` → запрос текущего depth у бэкенда, `EvictManagedResources` → no-op |
| `PF_GameLogic/PFRenderWater.cpp` | 3 | `GetViewport(&vp)` (перевод экранных координат в NDC) | кэшированный viewport в бэкенде (`getViewport()`), либо передавать размеры экрана извне |
| `PF_GameLogic/PFClientVisibilityMap.cpp` | 3 | `SetTexture(WARFOGSAMPLERINDEX, …)` ×2 (s10), `IDirect3DDevice9*` локальная | биндинг через `SmartRenderer::BindTexture` (уже есть) → binding set |
| `PF_GameLogic/SocialConnection.cpp` | 1 | скриншот: `GetRenderTargetData` + `D3DXLoadSurfaceFromSurface` + `D3DXSaveSurfaceToFile(PNG)` | readback через staging + свой PNG-энкодер (или `ICommandList::writeTexture` + stb/`WIC`) |
| `PF_GameLogic/PFPostFXParams.cpp` | 1 | `ColorFill(surface, NULL, D3DCOLOR_XRGB(0,255,0))` | `clearSurfaceView(color)` |
| `Terrain/Terrain.cpp` | 1 | `GetDevice()` → `GetDirect3D()` → `IDirect3D9` (395–398) | перечисление адаптера → DXGI adapter LUID / Vulkan device UUID |
| `Terrain/TerrainTextureCache.cpp` | 1 | `EvictManagedResources()` перед аллокацией тайлов | no-op (у кэша тайлов свой LRU, пула D3D9 больше нет) |
| `EditorNative/EditorRender.cpp` | 2 | `SetViewport`, `Present(0,0,hEditorWindow,0)` — редактор рендерит в своё окно | **решение:** редактор в новый бэкенд не переносим (внутренний инструмент), выключаем из сборки на этапе 4 |

Итого для контракта `IRenderBackend` нужно ровно: state-object (blend/depth/
stencil/alpha-test/color-write/scissor), viewport, clear (color+depth),
copy/blit, bind-texture, set-constants, readback, enumerate-adapters.
Ничего из перечисленного не требует D3D9-специфики.

### 2.1.1 D3DX-математика: что реально живо (уточнено)

`Render/SHCoeffs.cpp` + `Scene/SHGridCalculator.cpp` — единственные места
D3DX-математики. Разбор по вызовам:

| D3DX-вызов | Статус |
|---|---|
| `D3DXSHEvalDirectionalLight(order=3, dir, r,g,b, R,G,B)` | **ЖИВО.** `Scene/Scene.cpp:706,710` (lighting environment, два источника) и `Scene/SHGridCalculator.cpp:47` (запекание SH-сетки). Уже имеет Wine/PerfHUD-фолбэк (`Compatibility::IsRunnedUnderWine()` → только DC-член, `coeffs[0]`), причем фолбэк меняет каналы местами (`RCoeffs()[0]=R; GCoeffs()[0]=B; BCoeffs()[0]=G`) — выглядит как баг, проверить при портировании. На tiny10 (нативный Windows) идёт **настоящий** D3DX-путь. |
| `D3DXVec3Normalize`, `D3DXVECTOR2/3` | **ЖИВО** (`SHGridCalculator::CubeCoord` и обход граней куба). Тривиально заменяется на `CVec3`/свою нормализацию. |
| `D3DXSHProjectCubeMap` (`EvaluateSHCoeffsForCubeMap`) | **МЁРТВО** — ни одного вызова. Удалено. |
| GPU-проекция в SH (`EvaluateSHCoeffsFor2DTexture` + `ImmRenderer` проходы `SH`/`SH2`/`SH3`) | **МЁРТВО** — ни одного вызова. Удалено; осиротели `OnlineShaders/{SH,SH2,SH3}.psb`. |
| `D3DXSaveTextureToFile` (`#ifdef DEBUG_SH`) | **МЁРТВО** — `DEBUG_SH` закомментирован в обоих файлах; путь `“Z:/tst_tex/project.dds”` хардкод. |
| `D3DXMatrixInverse`, `D3DXMATRIX` (`ShadowManager.cpp`) | **ЖИВО** — построение матриц света/фрустума теней. Заменяется на `SHMatrix`/`System/Geom`. |

Вывод: объём D3DX-математики **меньше**, чем казалось, но `SHEvalDirectionalLight`
требует **побитово** тех же констант базиса, что и D3DX9 — иначе поедет
запечённое освещение (и `unitstats`-смоук по бою). Брать константы надо из
исходников D3DX/DirectXMath (`SHEvalDirection`, таблицы `g_SHCoeff{1..5}`),
не по памяти; проверить диффом на малом наборе направлений до переключения.


### 2.2 Реальный срез состояний (много меньше, чем кажется)

- **`D3DRS_*`, используемые вне `renderstatesmanager.cpp`: 32 уникальных,
  290 вызовов.** Остальные **71 из 103** в `renderstatesmanager.cpp` — только в
  таблице `DumpStates` (тесселяция, fog, point-scale, `WRAP0..15`,
  material sources, `TWOSIDEDSTENCILMODE`/`CCW_STENCIL*`) → **мёртвый код,
  удаляется**.
- Итоговый набор, который надо выразить через NVRHI `RenderState`:
  z (enable/func/write), stencil (enable/ref/mask/write-mask/fail/zfail/pass/func),
  blend (enable/src/dest/op + separate-alpha), color-write (0 и 1),
  cull, scissor, fillmode, depth-bias + slope-scale, clip-plane,
  `D3DRS_POINTSIZE` (только как R2VB-переключатель — удаляется вместе с R2VB),
  `D3DRS_LIGHTING` (1 место, fixed-function — удаляется).
  Всё это **полностью покрывается** `BlendState`/`DepthStencilState`/
  `RasterState` NVRHI. Отдельно: `D3DRS_SEPARATEALPHABLENDENABLE` +
  `SRC/DESTBLENDALPHA` + `BLENDOPALPHA` → `RenderTarget::…Alpha` (есть).
- **Sampler:** реально используются только `ADDRESSU/V`, `MIN/MAG/MIPFILTER`,
  `BORDERCOLOR`, `MIPMAPLODBIAS` (в т.ч. vendor-`FOURCC_GET4/GET1` = ATI
  Fetch4). `ADDRESSW`, `MAXANISOTROPY`, `DMAPOFFSET` — только в dump-таблице.
  NVRHI `SamplerDesc` покрывает всё, кроме Fetch4 (→ явный 4-тап).
- **FVF / fixed-function:** 4 места (`SetFVF(D3DFVF_XYZ)` в `DXHelpers.h`,
  `ShadowManager.cpp`; `SetFVF(D3DFVF_XYZRHW|DIFFUSE)` +
  `SetVertexShader(NULL)` в `RenderDeferred2D`). **Удаляются** — нужен явный
  `VertexFormatDescriptor` + простой VS.
- **Vertex-форматы:** `D3DDECLTYPE_*` 18, `D3DDECLUSAGE_*` 14. Из них
  `UDEC3/DEC3N/FLOAT16_2/FLOAT16_4/UNUSED` — только в конвертере, в контенте не
  встречаются (проверить до удаления). NVRHI `InputLayout` покрывает
  используемое.
- **Примитивы:** point list, line list/strip, tri list/strip/fan.
  NVRHI `PrimitiveType` есть, но **TriangleFan в Vulkan/DX12 есть, а
  `DrawPrimitiveUP` — нет**: UP-вызовы (4 места) → временные буферы.
- **Объёмных текстур в коде нет** (только ветки в dump) — упрощает.

### 2.3 Пулы и device-lost — удаляются, а не переносятся

- `D3DPOOL_MANAGED` 21 место, `DEFAULT` 35, `SYSTEMMEM` 5, `SCRATCH` 0
  (только в строке dump).
- `DXManager` (managed-priority/eviction по кадрам) — **удалён целиком**
  (`DXManager.{h,cpp}`, `BatchQueue::resourceManager`, cvar `DXHints`;
  по умолчанию был выключен — `s_enableDXHints = false`). Вместе с ним
  осиротел `IRenderablePrimitive::GetBuffers()` (7 реализаций) — убирать на
  этапе 4.
- `DXResourcesControl` в дереве **нет** (первичная оценка ошибочна).
- `DeviceLostHandler` + `DeviceLostWrapper` (~35 классов, `NotifyDeviceLost/
  NotifyDeviceReset`, `RecoverFromDeviceLost`) — **интерфейс сохранить,
  семантику свести к «ресайз/ре-аллокация»**: в DX12/Vulkan device-lost нет.
  Это самый большой по числу файлов, но самый дешёвый по риску кусок.
- `SharedD3DBufferST` / `DXVertexBufferDynamicRef` / `DXIndexBufferDynamicRef_`
  (`dxutils.h`) → NVRHI upload-heap / `writeBuffer`.

### 2.4 Vendor-форматы — удаляем, а не переносим (решение «дрейф допустим»)

`Features.h` + `ConfigManager::CheckFormats` пробуют 12 vendor-фич:
`DF16, DF24, RAWZ, INTZ, RESZ, R2VB, NLRT(FOURCC_NULL), D16T, D24X, D24S,
VT16, VT32`.

Решение по удалению:

| Фича | Где | Что делать |
|---|---|---|
| **R2VB / `D3DUSAGE_DMAP` / vertex textures** | `atir2vb.h`, `TextureVtx` (`texture.{h,cpp}`), `FullScreenFX.cpp`, `PFPostFXParams.cpp` | **Удалить.** На современном железе `hasR2VB` (`CheckDeviceFormat(0, D3DRTYPE_SURFACE, FOURCC_R2VB)`) == false, т.е. R2VB-ветка мёртвая и её удаление на картинку не влияет. Рабочей была vertex-texture-ветка (`HasVT16/32`). Значения SH/PostFX-коэффициент в Vulkan/DX12 поедут в structured/typed buffer. |
| **Fetch4** (`D3DSAMP_MIPMAPLODBIAS = FOURCC_GET4`) | `ShadowMapESM_Fetch4` | **Удалить класс**, оставить `ShadowMapESM`. Класс выбирался только при `HasDF24()` (vendor-формат) → на современном железе не создавался. |
| **DF16/DF24/RAWZ/RESZ** (vendor depth formats) | `ShadowMap.cpp` (`FOURCC_DF16/DF24`), `renderformat.h`, `renderflagsconverter.cpp`, `ConfigManager` | **Удалить.** Тени → `D24S8`/`D32F` + depth SRV (`R24_UNORM_X8_TYPELESS` / `R32F`). |
| **INTZ** | `renderformat.h FORMAT_INTZ`, `ShadowMap` | **Удалить** → depth SRV. |
| **NULL-RT** (`FOURCC_NULL`) | `ShadowMap.cpp` (`s_disableNLRT`), `ConfigManager` | **Удалить** → color-write-mask=0 (уже используется рядом мест). |
| `D3DUSAGE_QUERY_VERTEXTEXTURE` | `ConfigManager::CheckFormats` | Удалить вместе с VT. |

После этого `ERenderFormat` (`renderformat.h`) сокращается до
`A8, L8, R32F, G32R32F, A8R8G8B8, X8R8G8B8, A16B16G16R16F, A32B32G32R32F,
L16, R16F, R5G6B5, D24S8` — все есть в NVRHI `Format` (кроме `L8/L16` →
`R8_UNORM/R16_UNORM`, `R5G6B5` → `B5G6R5_UNORM`).

### 2.5 `configdatabase.cpp` — 2483 строки, завязаны на D3D9-caps

`CConfigDatabase::Load(file, soundDevice, D3DADAPTER_IDENTIFIER9, D3DCAPS9, …)`
матчит **42 поля `D3DCAPS9`** (`Caps/Caps2/Caps3/DevCaps/RasterCaps/ZCmpCaps/
SrcBlendCaps/DestBlendCaps/AlphaCmpCaps/ShadeCaps/TextureCaps/
TextureFilterCaps/CubeTextureFilterCaps/VolumeTextureFilterCaps/
TextureAddressCaps/LineCaps/MaxTextureWidth/MaxVolumeExtent/MaxTextureRepeat/
MaxTextureAspectRatio/MaxAnisotropy/StencilCaps/FVFCaps/TextureOpCaps/
MaxTextureBlendStages/MaxSimultaneousTextures/PresentationIntervals/CursorCaps/
…`) + 14 полей `D3DADAPTER_IDENTIFIER9`, и выдаёт пары «команда → значение» в
`NGlobal::RunCommand` (`ConfigManager.cpp:137`).

Это **не только рендер**: тот же `config.txt` задаёт и звуковые устройства.
Решение: **не удалять `configdatabase`**, а заменить источник капсов —
синтезировать `D3DCAPS9`-совместимую структуру из DX12/Vulkan
(`D3D11_FEATURE_DATA_*`/DXGI-адаптер; для Vulkan — device props + limits).
Поля, которых больше нет (`FVFCaps`, `TextureOpCaps`, `MaxTextureBlendStages`,
`LineCaps`, `CursorCaps`, `PresentationIntervals`) — заполнить «как у
современной карты» и **прогнать `config.txt` через новый матчер**, чтобы
пользовательские конфиги не начали давать другие пины. Отдельно проверить:
`Session.ROOT_*_shader3compatibilityError.txt` — текст про нес-support SM3.0,
тоже уходит.

### 2.6 Шейдеры

- Рантайм-компиляция: `D3DXCompileShader` + `ID3DXInclude`
  (`shadercompiler.cpp`) → `D3DCompile` (`d3dcompiler_47`, есть в
  `Vendor/DirectX/{Include/D3Dcompiler.h,Lib/*/d3dcompiler.lib}`) +
  `ID3DInclude`. Профили `vs_3_0/ps_3_0` → `vs_5_0/ps_5_0` (DX12) и
  HLSL→SPIR-V через DXC (Vulkan).
- **SM2.0-фолбэк удалить**: `FORCE_SM20`, `ps_2_0/vs_2_0`, `.shd2`-файлы
  (`MultiShader::OpenFile` выбирает `%s.shd%s` по `bSupportSM30`),
  `Session.ROOT_*_shader3compatibilityError.txt`, `forceSM20Usage` dev-var.
- **`.shd`-контейнер переформатировать**: сейчас это `{uint32 codesCount,
  [ {vsOffset, psOffset} × count ]}` + DXBC-под-SM3.0. Нужен формат под
  DXBC-SM5 (+ SPIR-V для Vulkan, либо компиляция HLSL→SPIR-V на клиенте
  через DXC — дороже).
- **Генератор `.shd` в дереве ЕСТЬ** (первичная оценка была неверна):
  проект **`Src/ShaderCompiler/`** (`ShaderCompiler.cpp`,
  `UberShaderDesc.{h,cpp}`, `parse.cpp`) — читает `MaterialSpec.xml`
  (`<shaderName>` + `<pinName>/<maxPinValue>/<pinMultiplier>`), для каждой
  из `GetTotalCount()` комбинаций пинов компилирует **тот же `.hlsl`** с
  `D3DXCompileShader` и `D3DXMACRO`-определениями пинов, entry points
  `MainPass_VSMain` / `MainPass_PSMain`, и пишет `.shd`.
  Вызов — `Vendor/BuildUtils/CompileShaders.cmd` →
  `Bin/ShaderCompiler.exe ../Src/Render/Shaders/MaterialSpec.xml
  --out../Data/Shaders/UberShaders/ [--sm20] [--clean] [--name <shader>]`.
  Проект есть в `Src/PF.sln`, но **не входит в CMake-граф клиента**
  (25 проектов) — собирается отдельно, под Win32/D3DX9.
  Что в нём надо менять: `D3DXCompileShader` → `D3DCompile`, профили
  `vs_3_0/ps_3_0` → `vs_5_0/ps_5_0` (+ DXC→SPIR-V), убрать `--sm20`/`.shd2`,
  и переносить на x64/вне Wine-D3DX.
  Инкрементальный кэш `.codes` **фактически выключён**: в
  `UberShaderFileInfo::Read()` ранний `return` после `Reset()`, т.е. всегда
  полная компиляция всех пинов (вот откуда 49 966 пар в `BasicMaterial.shd`).
  `MaterialSpec.{h,cpp}` этим генератором **не** создаются — их генератор
  (XML → C++-классы материала) в дереве действительно отсутствует;
  для смены набора пинов (например, убрать `R2VBPinValue`) он нужен.
- **Второй контейнер шейдеров, не учтённый в первичной оценке:**
  `Data/Shaders/OnlineShaders/` — **44 файла** `.vsb`/`.psb` (готовые DXBC,
  без обёртки `.shd`), грузятся `ImmRenderer::Get{V,P}Shader(name)` по имени
  из кэша (`ImmediateRenderer.cpp`). Это путь всех «нематериальных» проходов:
  `Z_Only`, `ConstantColor`, `ShadowPass*`, `SA*`, `SH*`, `Upscale{H,V}`,
  `DecodeZ`, `default_shader`, `WarfogBlend`, `NormalMap`, … Имена либо
  хардкод в C++ (`ShadowMap.cpp`, `Stretcher.cpp`, `DXHelpers.h`), либо из БД
  (ресурсы материала). При переносе на NVRHI этот контейнер надо завести
  **параллельно** `.shd` (тот же DXBC-SM5/SPIR-V, другой способ выбора).
  Уже осиротели после этапа 1: `DecodeRAWZ.psb` (RESZ-путь удалён) и
  `SAPrePixelShaderF4.psb` (Fetch4) — ни одного упоминания в коде.
  Живой 4-таповый PCF — `ShadowPassPCFDST4*` (dev-var `shadowPCF4_enable`,
  по умолчанию `true`), переносим.
- **Лимиты SM5, которые надо проверить по контенту:**
  `VSHADER_NIGHT_PARAMS c234`, `PSHADER_NIGHT_PARAMS c220` (OK, 4096),
  `BONES c24` + `MAX_BONES 70` (comment: «can't be much higher due to Wine»),
  `MAX_INSTANCES_COUNT 19`, `PFX_MAX_INSTANCES 47`,
  `SetVertexShaderConstants(VSHADER_LOCALCONST9, 3*numInstances, …)` —
  **запись произвольного числа регистров в mid-shader**, в DXBC-SM5 это
  возможно, но в Vulkan-константном буфере требует другой раскладки.
  См. §3.

### 2.7 `GlobalMasks.h` — главный источник трения

Контракт HLSL↔C++ на абсолютных регистрах: `WORLD c0`, `VIEW c4`,
`VIEWPROJECTION c8`, `PROJECTION c12`, `EYEPOSITION c16`, `CAMERADIRECTION c17`,
`RENDERTIME c18`, `WARFOGMAPPING c19`, `VSHADER_LOCALCONST0..11 c20..c31`,
`BONES c24`, `INSTANCE_DATA c20`, `PSHADER_NIGHT_PARAMS c220`, сэмплеры
`WARFOGSAMPLERINDEX s10`, `GLOBALENVIROMENTSAMPLERINDEX s12`,
`DISTORTIONBUFFERSAMPLERINDEX s13`, `TERRAIN_DIFFUSESAMPLER0..3 s0..s3`,
`SAMPLER_CONSTANTS_BEGIN + samplerIndex*2` (модификаторы сэмплера пишутся в
константы из `Sampler::Bind`).

В NVRHI это `IBindingLayout` + `IBindingSet`. План:
**один `cbuffer` на стадию с «виртуальными регистраами» c0..c255**
(≈4 KB), раскладка генерируется из `GlobalMasks.h` скриптом;
`Set{Vertex,Pixel}ShaderConstants(reg, n, data)` пишет в staging-копию,
на `setGraphicsState` — один upload. Это сохраняет и HLSL (макросы `cN`
работают как `cbuffer`-массив), и C++ (API не меняется), и позволяет
`SAMPLER_CONSTANTS_BEGIN` остаться константой.

Отдельно — **двойная трактовка имён `sN` в `GlobalMasks.h`** (подводный
камень, снят на этапе 1): в C++-секции (`#ifndef HLSL`) было
`static const unsigned int s0 = 257;`, т.е. одно и то же имя `s0` в HLSL —
это слот 0, а в C++ — слот **257**. Расширенный диапазон 257..260 —
D3D9-пространство SM3.0 vertex-texture-адресации; под него и была
размерена таблица `lastTexturesPtrs[MAX_VS_SAMPLER_INDEX+1]` (261).
Единственный реальный потребитель — PostFX-коэффициенты:
`FullScreenFX::Apply` биндил `TextureVtx` в `Sampler::MIN_VS_SAMPLER_INDEX`
(257), тогда как HLSL читает их из `coeffMap : register(s0)`.
`IDirect3DDevice9::SetTexture` по индексу 257 вне
`MaxSimultaneousTextures` возвращает `D3DERR_INVALIDCALL` (ошибка только
печаталась при `s_DXWarnLevel > 1`), т.е. **coeffMap фактически не
биндился** — VS читал то, что осталось в слоте 0 (обычно DepthMap war fog).
Плюс `coeffMap : register(s0)` конфликтовал с `DepthMap : register(s0)`
(тот же `#if WarFogPinValue == PRESENT`).

Снято так: `coeffMap` перенесён в свободный слот
(`GlobalMasks.h: POSTFX_COEFFSAMPLERINDEX = s3`; у `FullScreenMaterial`
заняты s0 DepthMap, s1 diffuse, s2 bloom, s10 warFog), C++ `s0..s3`
стали обычными индексами, `MIN/MAX_VS_SAMPLER_INDEX` совмещены с PS-диапазоном
(16 слотов вместо 261). Это **изменение картинки** (PostFX sharp /
color-correct начинают получать настоящие веса) — сознательное, класс
«исправление мёртвого пути».

---

## 3. Этапы

Гейт каждого этапа: сборка x64 (Wine + native) + E2E на tiny10.
**Гейт «золотые `unitstats` байт-в-байт» остаётся только для правок вне
рендер-пути**; для самих рендер-этапов он неприменим (меняется картинка) —
заменить на скриншот-дифф + `unitstats` как smoke (бой/физика не сломаны).

### Этап 0. Блокеры и разведка (1–2 смены)

- [x] **Генератор `.shd` — найден, блокера нет:** `Src/ShaderCompiler/` +
      `Vendor/BuildUtils/CompileShaders.cmd` (см. §2.6). Не входит в
      CMake-граф клиента, Win32/D3DX9 — переносить на `D3DCompile`/SM5/DXC
      нужно в рамках этапа 3.
- [ ] **Генератор `MaterialSpec.{h,cpp}` из `MaterialSpec.xml` — отсутствует.**
      Нужен, только если меняется набор пинов материала (например, убрать
      `R2VBPinValue`). Пока пины не меняются — не блокер.
- [ ] `tools/nvrhi_probe/` — вне графа клиента: NVRHI статикой под MSVC 14.44
      `/MT` + C++17; **смоук Vulkan** на `HWND` клиента; прогон под Wine
      (нативный `winevulkan`) и на tiny10. DX12 — только проверить, что
      компилируется (запуск позже).
- [ ] Зафиксировать: `d3dcompiler_47` vs DXC для SPIR-V; размер прироста;
      какие DXGI/Vulkan-зависимости появляются.
- [x] Счётчики уникальных VS/PS/пар переведены на `USE_RENDER_SHADER_STATS`
      и доступны в shipping по `-DPW_RENDER_SHADER_STATS` (§1.2).
- [ ] **Снять реальные цифры** (`numShaderPairTotal`, `numVSUniq`) в бою на
      tiny10 + число уникальных `sortId`.

### Этап 1. Зачистка: удалить мёртвое и D3D9-утечки (2–3 смены)

Ничего не меняет в картинке — и потому идёт первым. Единственные
сознательные отклонения от «картинка не меняется» отмечены в §6
(PostFX-коэффициенты, §2.7).

- [x] Удалено: `atir2vb.h`, `VendorFOURCC.h`, R2VB-ветка `TextureVtx`,
      `ShadowMapESM_Fetch4`, `ShadowMapPCF_DF` (никогда не создавался),
      `D3DUSAGE_DMAP`, vendor-FOURCC (`DF16/DF24/RAWZ/RESZ/INTZ/NULL/R2VB`)
      и `Features.h`-фичи под них, SM2.0-путь (`FORCE_SM20`, `.shd2`,
      `forceSM20Usage`), RESZ-путь (`CopyDepthRESZ`, `enableRESZ`),
      71 `D3DRS_*` + 5 `D3DSAMP_*` из dump-таблицы.
- [x] `ERenderFormat` сокращён до 12 форматов (§2.4).
- [ ] Удалить: `rendertarget.h` (уже `#error`), `d3d9texture2D.h`,
      `D3DUSAGE_QUERY`.
      `D3DQUERYTYPE_EVENT` (`Renderer::Synchronize`) — **не удаляется, а
      переносится**: это троттлинг CPU относительно GPU при present без
      тройной буферизации (`g_synchronizeRender`, `!bTripleBufferUsed`).
      В NVRHI есть `createEventQuery/setEventQuery/pollEventQuery/
      waitEventQuery/resetEventQuery` — ложится 1:1.
      `DXManager` удалён целиком (+ cvar `DXHints`, `resourceManager` в
      `BatchQueue`; по умолчанию был выключен: `s_enableDXHints = false`).
      `DXResourcesControl` в дереве **нет** (первичная оценка ошибочна).
      Удалены мёртвые файлы: `Render/rendertarget.h` (`#error` с 2008,
      ни одного включения), `Terrain/UberElement.{h,cpp}` (`#error`
      «deprecated 28.05.2008», ни одного включения; единственный
      `D3DXSaveTextureToFile`-хак в дереве).
- [x] FVF/fixed-function: `SmartRenderer::SetFVF` удалён, три места
      (`DXHelpers.h` ×2, `ShadowManager.cpp`) переведены на
      `SetVertexFormatXYZ()` (POSITION-only declaration).
      **Осталось одно:** `RenderDeferred2D()` (`smartrenderer.cpp`) —
      `SetVertexShader(NULL)` + `SetFVF(XYZRHW|DIFFUSE)` +
      `DrawPrimitiveUP`. Практически вызывается только debug-оверлеями
      (`DebugRenderer::Draw2D`, график кадров `UI/FrameTimeRender`).
      Для NVRHI нужен собственный VS/PS (экранизация XYZRHW + diffuse);
      образец раскладки — `Data/deprecated_shaders/debug_shader.hlsl`
      (`float3 POSITION @0`, `float TEXCOORD0 @12`, `int4 COLOR0 @16`
      — байт-в-байт совместим с `Vertex2D`).
- [ ] Ликвидировать утечки D3D9 из `PF_GameLogic`/`Scene`/`Terrain`/
      `PF_Core`/`EditorNative` (§2.1) — 45 `GetDevice()` снаружи в 8 файлах.
- [ ] `Scene/SHGridCalculator.cpp` + `Render/SHCoeffs.cpp`: D3DX-математика
      (`D3DXSHEvalDirection`, `D3DXSHEvalDirectionalLight`) → своя/`System/Geom`.
- Критерий: `grep -c "IDirect3D\|D3DX\|d3d9" Src/{Scene,UI,Game,Terrain,
  PF_GameLogic,PF_Core,EditorNative}` → 0; в `Src/Render` — только
  `renderer.*`, `dxutils.*`, `texture*`, `smartrenderer.*`, `shadercompiler.*`,
  `Dx*`, `configdatabase.*`.

### Этап 2. Контракт вместо D3D9 (3–5 смен)

Цель — чтобы `Src/Render` перестал светить D3D9-типы наружу и внутри себя
ходил через один внутренний интерфейс. Это **тот же рефакторинг, что и при
своём D3D11-слое**, т.е. не «лишняя» работа.

- [ ] `Src/Render/Backend/` — внутренний интерфейс: `DrawIndexed(DipDescriptor,
      InputLayoutKey, ShaderPairKey, RenderStateKey, samplers[], consts)`,
      `SetViewport/Scissor`, `BindRT/Depth`, `Clear`, `CopyRegion`,
      `CreateTexture/CreateBuffer/CreateShader`, `Present`, `Resize`.
- [ ] `D3D9Backend` — перенос `SmartRenderer`/`Renderer` **как есть**
      (временно; удаляется на этапе 4).
- [ ] Кэш пайплайнов по ключу `sortId`-подобного хеша (§1.3) — размер и
      eviction по замерам этапа 0.
- [ ] `ConstBlock` — «виртуальные регистры» c0..c255 одним буфером (§2.7),
      генерация раскладки из `GlobalMasks.h`.
- [ ] `DeviceLostHandler` → `OnReallocate` (интерфейс тот же, семантика —
      ресайз).
- [ ] Пулы: `RENDER_POOL_MANAGED` → staging + default + upload-ring
      (`dxutils.h::PoolType` сохраняется как enum).
- Критерий: картинка не меняется, E2E зелёный.

### Этап 3. `NvrhiBackend`: DX12 + Vulkan (6–10 смен)

- [ ] `Vendor/NVRHI/` + отдельный CMake-таргет (`cxx_std_17`, свои флаги,
      статика или `nvrhi.dll` при проблемах с `/MT`); в TU клиента — только
      обёртки, без `<nvrhi.h>`. В `gen_cmake.py` — новый вендор + правила
      `X64_LIB_*`/`X64_LINKDIR_DROP` (vcproj не правим: он остаётся источником
      графа).
- [ ] Один бэкенд за `IRenderBackend` — **Vulkan**, дефолт. Интерфейс
      рассчитан на последующее добавление DX12 (никаких Vulkan-типов в
      заголовках вне реализации); dev-var `rndr_backend=vulkan` закладывается
      сразу, DX12 появится в нём позже.
- [ ] Swapchain: **WSI (Vulkan)**; DXGI-часть — только для будущего DX12.
      `isBorderless`/`vsyncCount` из
      `RenderMode` → `IDXGISwapChain::Present(0|1)` / `vkQueuePresentKHR`;
      `EnumDisplaySettings`-валидация ресов — только для нативного
      fullscreen, на Linux — не нужна.
- [ ] Шейдеры: `D3DCompile` → DXBC `vs_5_0/ps_5_0` (DX12) и DXC → SPIR-V
      (Vulkan); новый `.shd`-формат (этап 0); `ID3DXInclude` → `ID3DInclude`.
- [ ] Тени: `D24S8`/`D32F` + depth SRV вместо DF16/DF24/INTZ; ESM без Fetch4;
      NULL-RT → color-write 0. **Скриншот-дифф с ожиданием дрейфа** —
      зафиксировать новые эталоны.
- [ ] SH/PostFX: vertex-texture/R2VB → structured buffer.
- [ ] Occlusion — **не `IQuery`** (в NVRHI его нет, см. §3.x): выбрать
      обход через нативный query-пул или патч NVRHI.
      UP-вызовы → временные буферы; `StretchRect` →
      `copyTexture`/fullscreen-blit; `GetRenderTargetData` → `copyTextureRegion`
      в staging.
- [ ] `configdatabase`: синтез `D3DCAPS9`-совместимой структуры (§2.5) +
      прогон `config.txt` через новый матчер (регресс: пины не должны
      «уехать»).
- [ ] Linux-контур — **вне рамок этой ветки** (клиент пока Windows-only).
      Но `IRenderBackend` обязана не содержать `HWND`: сейчас
      `Renderer(HWND)` — окно вынести в platform-слой заранее, на этапе 2.

### Этап 4. Удаление D3D9 (1–2 смены)

- [ ] Удалить `D3D9Backend`, `dxutils.*`-D3D9-часть, `DxIntrusivePtr.h`,
      `DXHelpers.h`, `DXWarnSignal.h`, `DeviceLost.cpp`, `ReadDDS`-D3D9-часть
      (DDS-парсер оставить), `dxerr`, `d3d9.lib/d3dx9.lib/dxguid.lib` из
      линковки (vcproj + `gen_cmake.py`).
- [ ] Обновить `Vendor/DirectX` (D3D11/DXGI-заголовки из вендора June 2010 для
      NVRHI **не годятся** — брать Windows SDK).
- [ ] Обновить `BUILD_CLIENT_X64.md` / `HOWTO_BUILD_CLIENT_WINDOWS.md` /
      скил `pw-client`.

---

### 3.x GAP: occlusion-запросов в NVRHI нет

Проверено по исходникам NVRHI (`6b96fb03`, 2026-10-05): в `nvrhi.h` есть
только `IEventQuery` и `ITimerQuery` (`beginTimerQuery/endTimerQuery/
getTimerQueryTime`); **`IQuery`/occlusion-типа нет нигде** (ни в `d3d12.h`,
ни в `vulkan.h`).

При этом occlusion в клиенте **живой**: `OcclusionQueriesBank`
(`Render/OcclusionQueries.cpp`, `D3DQUERYTYPE_OCCLUSION`, пул запросов с
кольцевым буфером `head/currentFrame/lastFinishedQuery`) используется для
подводного/верхнего cullinga из `PF_GameLogic/PFRenderWater.cpp`
(`SetUseMode(QUM_ISSUE / QUM_CHECK_AND_ISSUE / QUM_NONE)`), потребление —
`Render/batch.cpp` (`QUM_CHECK`/`QUM_ISSUE` в обходе батчей).

Варианты (решение принимать на этапе 3, не сейчас):
1. **Обход NVRHI**: `IDevice::getNativeDevice()` → нативный
   `ID3D12QueryHeap`+`ID3D12GraphicsCommandList` / `vkCmdBeginQuery`+
   `VkQueryPool`. NVRHI это разрешает, но ломает переносимость бэкенда и
   требует своей обвязки над command-list'ом NVRHI.
2. **Запатчить/завендорить NVRHI** (`IQuery` + реализация DX12/Vulkan) —
   самый чистый, но это поддержка форка.
3. **Убрать GPU-occlusion** и усилить CPU-culling — риск по производительности
   на подводных/крупных сценах и по поведению (активные cvar'ы).

## 4. Риски

1. **Нет генератора `.shd` и `MaterialSpec.{h,cpp}`** — блокер этапа 0.
   36 uber-файлов, 49 966 вариаций, «GENERATED AUTOMATICALLY» в заголовках.
2. **Vulkan: константы.** `GlobalMasks.h`-раскладка c0..c255 +
   `SetVertexShaderConstants(reg, n, data)` с произвольным `n` в середине
   материала (инстансинг, SH) — в Vulkan это один `VkBuffer`; нужна
   динамическая раскладка или `push constants`/несколько CBV. Решается на
   этапе 2 (`ConstBlock`), но проверять надо сразу.
3. **Vulkan: Y-flip и глубина.** D3D9 → Vulkan: ориентация
   viewport/UV, `gl_Position.y`, range глубины (0..1 vs -1..1). D3D9-шейдеры
   уже «D3D-семантики»; при DXC-пути часть решается автоматически, но
   `StretchRect`/`GetRenderTargetData`/PostFX-после-чтения-экрана — ручные.
4. **Vulkan: `PrimitiveType::TriangleFan` + `LineStrip`** — есть, но
   `PatchList`/тесселяция не нужны (уже удалили).
5. **Wine + Vulkan.** DXVK исключён, значит под Wine нужен нативный
   Vulkan-путь Wine (`winevulkan` + Vulkan-драйвер хоста). Проверить на этапе 0:
   есть ли в `~/.wine-vs`-тулчейне/окружении Vulkan (`vulkan-1.dll`,
   `winevulkan.dll`), и что покажет смоук. Если под Wine Vulkan недоступен —
   тестирование только на tiny10 (нативный Windows + Vulkan-драйвер), а Wine
   остаётся тулчейном сборки, не средой запуска.
6. **Производительность.** Текущий рендер заточен под D3D9-дешевизну
   `SetRenderState`. После этапа 2 (кэш пайплайнов) узкое место — число
   `setGraphicsState`. Замерить на этапе 3 (`numDIP`, `numVS/numPS`).
7. **`unitstats` как гейт.** Рендер-этапы его ломают по определению;
   зафиксировать замену гейта заранее, иначе «регресс/не регресс» станет
   неразличимы.

---

## 5. Документация по итогам

При принятии — раздел «Рендер-бэкенды» в `.pi/skills/pw-client/`:
`rndr_backend`, ограничения по слотам/профилям, статус vendor-форматов,
формат `.shd`, как регенерировать `MaterialSpec`. Здесь — только механизм и
подводные камни, не текущее состояние.

---

## 6. Статус в ветке `nvrhi`

Ветка от `client-modern` (`84ca6630e`). Гейт: `./build_client_x64_wine.sh`
(x64 shipping, Wine+MSVC 14.44) + E2E на tiny10.

### Сделано (этап 1, часть)

| Область | Изменение |
|---|---|
| `Render/Features.h`, `IConfigManager.h` | из перечня аппаратных фич убраны `hasDF16/DF24/RAWZ/INTZ/RESZ/R2VB/NLRT/VT16/VT32` (+ обёртки `HasDF16()…HasVT()`); остались `hasD16T/D24X/D24S` — единый гейт «depth читается как текстура» (`HasDSM()`). `TechniqueDesc::r2vbFormat` убран, `FindAcceptableTechnique` упрощён. `featuresVersion` → 2. |
| `Render/VendorFOURCC.h`, `Render/atir2vb.h` | **удалены** (+ записи из `Render.vcproj`, `specific.h`). |
| `Render/renderformat.h`, `renderflagsconverter.cpp` | `ERenderFormat`: удалены `FORMAT_INTZ/DF24/RAWZ/DF16`; `D3DFormat2Str` без vendor-FOURCC. |
| `Render/ConfigManager.cpp` | `CheckFormats()` проверяет только D16T/D24X/D24S; удалён прощуп R2VB-форматов (`g_R2VBFormats`, `r2vbFormats`), `AllowDF24`. `GetDepthFormatPreffered()` → всегда `FORMAT_D24S8`; `ReadableDepthSupported()` → `HasDSM()`; `StencilSupported()` — по D24S8. Убран винный гейт `ReadableDepthEnabled()` (читаемость глубины больше не зависит от vendor-форматов). |
| `Render/ShadowMap.cpp` | удалены классы `ShadowMapPCF_DF` (никогда не создавался — единственная ветка выбора закомментирована) и `ShadowMapESM_Fetch4`; `FOURCC_NULL` → обычный RT + color-write маска; vendor-формат глубины теней → `D24S8`; dev-var'ы `shadowFat`, `shadowNoNLRT` удалены. |
| `Render/ShadowManager.cpp` | убраны два `!HasDSM() && HasDF24() → SHADOWMODE_SAESM` фолбэка. |
| `PF_GameLogic/PFRenderInterface.{h,cpp}` | удалён RESZ-путь целиком: `CopyDepthRESZ()`, `s_useRESZ`, `s_EnableRESZ` + dev-var `enableRESZ`, `DECALMODEPIN_ZDECODE`-ветки в decal-проходах, `depthCopyShaderName` всегда `DecodeZ`. `DropAllSamplerStates()` — один проход (VS/PS-диапазоны совмещены). |
| `Render/renderstatesmanager.cpp` | таблицы `DumpStates` сокращены до реально выставляемых состояний: 32 `D3DRS_*` (было 103) и 8 `D3DSAMP_*` (было 13). |
| `Render/texture.{h,cpp}` | `TextureVtx` без R2VB: один vertex-texture режим, `Sizes` без r2vb-пары, `Bind(sampler)` без стрима, удалены `UsingR2VB()`, `stream`, `D3DUSAGE_DMAP`, `BindVertexBufferRaw`/`R2VB_*`. |
| `Render/GlobalMasks.h` | C++ `s0` перестал быть 257; добавлены `s1..s3` и `POSTFX_COEFFSAMPLERINDEX = s3`. |
| `Render/sampler.h` | `MIN/MAX_VS_SAMPLER_INDEX` = PS-диапазон (было 257/260) → таблица биндингов 16 вместо 261. |
| `Render/smartrenderer.cpp` | `lastTexturesPtrs` обход по `capacity()` (был off-by-one: 259 из 261); статистика шейдеров на `USE_RENDER_SHADER_STATS` + метрика уникальных пар (VS,PS). |
| `Render/FullScreenFX.{h,cpp}` | R2VB-ветка удалена (`r2vbGlbEnable_Set`, `SetR2VBPin` вызывается с «есть ли coeff-текстура»), мёртвый `pVDecl` (он был под R2VB-стрим 15) удалён; coeff-текстура биндится в `POSTFX_COEFFSAMPLERINDEX`. |
| `Render/Shaders/FullScreenMaterial.hlsl` | `coeffMap: register(s0)` → `register(s3)` (см. §2.7). **Пока не пересобран `FullScreenMaterial.shd`, правка инертна:** в готовом DXBC всё ещё `s0`, а C++ пишет коэффициенты в s3 → шейдер читает s0 (= DepthMap), как и раньше. Регресса нет, но и исправления нет до пересборки шейдеров. |
| `PF_GameLogic/PFPostFXParams.cpp` | гейт `!HasVT() && !HasR2VB()` убран (чтение текстур в VS гарантировано SM3.0); `TextureVtx::Sizes(1,1)`. |
| `Render/renderer.cpp`, `shadercompiler.cpp`, `multishader.cpp` | SM2.0-путь удалён: `forceSM20Usage` dev-var, `#ifdef FORCE_SM20` клампы, `FORCE_SM20`/`ps_2_0`/`vs_2_0` в рантайм-компиляторе, суффикс `.shd2` в `MultiShader::OpenFile`. `bSupportSM30` оставлен — это гейт запуска (`PW_Client/Game.cpp::CheckHardwareCompatibility`), не путь рендера. |

### Сознательные отклонения от «картинка не меняется»

1. **PostFX sharp / color-correct.** `coeffMap` был не привязан (бинд в
   слот 257 + конфликт с `DepthMap` в s0). После переноса в s3 веса
   начинают читаться — картинка PostFX поменяется (в лучшую сторону).
2. **Тени.** `ShadowMapPCF_DS` вместо `D16`/`D24X8` теперь всегда `D24S8`
   (было `shadowFat && HasD24X() ? D24X8 : D16`, `shadowFat` — выключенный
   dev-var → фактически был `D16`). Разрядность глубины выросла →
   возможны сдвиги в bias-чувствительных местах.

### Не сделано / под вопросом

- `.shd2`-файлы в `Data/Shaders/UberShaders/` (17 шт.) клиентом больше не
  читаются, но из контента не удалены — удаление трогает `files.list`
  (манифест обновлений), делать в контентном контуре.
- `Session.ROOT_*.shader3compatibilityError` (текст про SM3.0) — поле БД,
  оставлено: гейт `CheckHardwareCompatibility` жив, а менять xdb-раскладку
  ради мёртвого текста не нужно.
- `ShaderCompiler` (`--sm20`, `.shd2`, D3DX9) не правился: он вне CMake-графа
  клиента, переносится целиком на этапе 3.
- `Render/SHCoeffs.cpp` → `D3DXSHEvalDirectionalLight`, `Scene/SHGridCalculator.cpp`
  → `D3DXSHEvalDirection`/`D3DXVec3Normalize` — D3DX пока жив, это отдельная
  задача этапа 1.
