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

**Замер по импортам собранного x64 `PW_Game.exe` (2026-10-12) — утверждение
про «единственные места» было неверным.** Всего в exe импортируется 17 функций
`d3dx9_43.dll`:

| группа | функции | чем заменяется |
|---|---|---|
| матрицы/векторы | `MatrixMultiply`(14), `MatrixLookAtLH`(4), `MatrixInverse`(3), `MatrixTranspose`, `MatrixTranslation`, `MatrixPerspectiveLH`, `MatrixOrthoLH`, `MatrixOrthoOffCenterLH`, `Vec3TransformCoord`(4), `Vec3TransformCoordArray`(9), `Vec3TransformNormal`(2), `Vec2TransformNormalArray`, `Vec3Normalize`(3) | своя математика; **осторожно с `MatrixMultiply`**: порядок операций влияет на биты, а матрицы уходят и в игровые трансформации — менять только с проверкой |
| загрузка текстур | `CreateTextureFromFileInMemoryEx`(3), `CreateCubeTextureFromFileInMemoryEx`, константы `D3DX_DEFAULT*`, `D3DX_FILTER_*`, `D3DXIFF_DDS`, `D3DX_SKIP_DDS_MIP_LEVELS`, `D3DXBUFFER` | нужен собственный DDS/TGA/PNG-загрузчик с фильтрацией и мипами — это отдельная работа этапа 3/4, не «мелочь» |
| поверхность | `LoadSurfaceFromSurface`, `SaveSurfaceToFile`(5), `SaveSurfaceToFileInMemory`(3) | свёрнуто в `Renderer::SaveScreenshotToFile` (этап 1) |
| типы | `D3DXVECTOR3`(105), `D3DXMATRIX`(26), `D3DXPLANE`(7), `D3DXMACRO`, `D3DX_PI` | только заголовок `d3dx9math.h`; типы меняются на `CVec3`/свою матрицу |
| SH | `SHEvalDirectionalLight` | **заменено своей реализацией** (этап 1, см. ниже) |

Разбор по вызовам (старый, дополнен таблицей выше):

| D3DX-вызов | Статус |
|---|---|
| `D3DXSHEvalDirectionalLight(order=3, dir, r,g,b, R,G,B)` | **ЖИВО.** `Scene/Scene.cpp:706,710` (lighting environment, два источника) и `Scene/SHGridCalculator.cpp:47` (запекание SH-сетки). Уже имеет Wine/PerfHUD-фолбэк (`Compatibility::IsRunnedUnderWine()` → только DC-член, `coeffs[0]`), причем фолбэк меняет каналы местами (`RCoeffs()[0]=R; GCoeffs()[0]=B; BCoeffs()[0]=G`) — выглядит как баг, проверить при портировании. На tiny10 (нативный Windows) идёт **настоящий** D3DX-путь. |
| `D3DXVec3Normalize`, `D3DXVECTOR2/3` | **ЖИВО** (`SHGridCalculator::CubeCoord` и обход граней куба). Тривиально заменяется на `CVec3`/свою нормализацию. |
   **Снято экспериментально (2026-10-12, `tools/sh_math_test/`). Базис —
   ортонормированные вещественные СГ, порядок индексов D3DX, знак у x и y
   отрицательный (левая система), l=2,m=0 = (3z²−1)/2. Константы битами:
   c0=0x3F558751, c1=0x3FB8EBD2, c2=0x404EBF89, c3=0x3FEEBB70, c4=0x3FCEBF88.
   Расхождение кандидата с D3DX по 400 случайным направлениям:
   maxAbs=9.5e-7 (~1 ULP), maxRel=4.3e-6 (только на вычитании в члене
   (3z²−1)/2). Т.е. замена безопасна по точности; дополнительно уходит
   Wine-фолбэк с перестановкой каналов. Детали и способ получить эталон
   (x64 d3dx9_43.dll + WINEDLLOVERRIDES=n) — README там же.
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
- [x] **`tools/nvrhi_probe/` — сделано (2026-10-12).** NVRHI `6b96fb03`
      собирается cl 14.44 под Wine, C++17, статика, **`/MT`** — без ошибок,
      линкуется с приложением. Найденные при этом факты API (важнее самого
      запуска): swapchain-абстракции **нет вообще** (бэк-буфер оборачивается
      через `createHandleForNativeTexture`); при статике NVRHI даёт **четыре**
      либы (`nvrhi`, `nvrhi_d3d11`, `nvrhi_d3d12`, `nvrhi_vk`); при статике же
      NVRHI **не** инициализирует диспетчер Vulkan-Hpp — это обязанность хоста;
      `d3d11::DeviceDesc` принимает только `context`; ни VkInstance, ни VkDevice,
      ни surface NVRHI не создаёт. Итоговые размеры статики и список «что
      осталось на tiny10» — `tools/nvrhi_probe/README.md`.
- [x] **Runtime-смоук — сделан частично (2026-10-12).** Три среды: Wine (обе
      ветки падают: нет адаптера / нет surface-расширений), **tiny10** (D3D11
      через **WARP**, FL 11_0, смоук проходит; **Vulkan в госте нет вообще** —
      `Microsoft Basic Display Adapter`, `vulkan-1.dll` отсутствует) и
      **нативный Linux-хост** (Vulkan: instance → device →
      `nvrhi::vulkan::createDevice` → `IEventQuery` → смоук
      «createTexture → clear → copy → readback» с эталонным пикселем
      `(0,188,255,255)`; устройство — **llvmpipe**, модули nvidia на хосте не
      загружены). Итог: **Vulkan-путь NVRHI работоспособен**, но реального
      Vulkan-железа ни в одной доступной среде нет (см. риски, п.5).
- [x] **Ещё два структурных факта об API (меняют план):**
      (а) **render pass'ов в NVRHI `6b96fb03` нет** — вместо
      `beginRenderPass`/`endRenderPass` один `setGraphicsState(GraphicsState)` +
      `draw()`, очистка явная (`clearTextureFloat/UInt` +
      `clearDepthStencilTexture`). Для клиента это скорее плюс: его модель
      «RT + SetRenderState + Draw» ложится один-в-один, перестраивать под render
      pass не надо; но и tile-based-оптимизации из кода не получить.
      (б) приложение **обязано** включить Vulkan 1.2+ (`timelineSemaphore`) и
      `VK_KHR_synchronization2` (или 1.3) на `VkDevice`, а каждой текстуре —
      `initialState` + `keepInitialState`; без этого NVRHI падает в assert /
      SIGSEGV. В доках NVRHI об этом нет ни строки — детали в
      `tools/nvrhi_probe/README.md`.
- [x] **`VkSurfaceKHR`/`VkSwapchainKHR` + present — сделано (2026-10-12).**
      В tiny10 поднят software-Vulkan: `vulkan-1.dll` (свой, из
      `vendor/vulkan-loader`, `/MT`) + ICD Mesa **lavapipe** 26.2.4
      (`vendor/mesa-win/vulkan_lvp.dll`); ставится одной командой —
      `tools/nvrhi_probe/setup_tiny10_vulkan.sh`. Проверено на госте: окно →
      `VkSurfaceKHR` → surface supported → `VkSwapchainKHR` (120x89, 2 картинки,
      B8G8R8A8 sRGB) → `createHandleForNativeTexture` → clear →
      `vkQueuePresentKHR` — всё PASS. Два обязательных условия (реестр вместо
      `VK_ICD_FILENAMES` из-за elevated-режима загрузчика; запуск только в
      интерактивной сессии, иначе present = `VK_ERROR_MEMORY_MAP_FAILED`) —
      в `tools/nvrhi_probe/README.md`.
      **Оговорка:** растер CPU → на этом стенде проверяется только
      *корректность*, но не производительность.
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
- [x] Удалены мёртвые `Render/rendertarget.h`, `Terrain/UberElement.{h,cpp}`;
      `d3d9texture2D.h` в дереве **нет**, `D3DUSAGE_QUERY` не встречается —
      пункт закрыт сам собой (проверено grep 2026-10-12).
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
- [x] Утечки D3D9 снаружи ликвидированы (2026-10-12, `668c76f7d`): было
      45 `GetDevice()` в 8 файлах → **осталось 2, оба в `EditorNative`**
      (внутренний инструмент, снимается с сборки на этапе 4). Новая обвязка:
      `Renderer::{GetViewport,SetScissorRect,ColorFillSurface,StretchSurfaceRect,
      EvictManagedResources,GetAvailableVidMem,SaveScreenshotToFile}` +
      `RenderStatesManager::SetStateDirect/SetSamplerStateDirect`.
      Попутно закрыт дефект: 21 `SetRenderState` + 18 `SetSamplerState` в
      `PFRenderInterface` шли мимо `RenderStatesManager`, т.е. не
      инвалидировали кэш — следующий `SetState()` с тем же пресетом не
      переписывал состояние.
- [x] `Render/SHCoeffs.cpp`: `D3DXSHEvalDirectionalLight` заменена своей
      реализацией (константы сняты битами с настоящей `d3dx9_43.dll` x64 —
      `tools/sh_math_test/`, расхождение maxAbs 9.5e-7 ≈ 1 ULP); убран
      Wine/PerfHUD-фолбэк, который писал только DC-член и путал каналы G↔B.
      `D3DX_PI` → литерал.
- [ ] `Render/ShadowManager.cpp`: 3 × `D3DXMatrixInverse` → своя реализация.
      Отложено: обратная матрица уходит в матрицу теней, разница в последнем
      бите видна как дрог границ — сначала сверка с эталоном из
      `tools/sh_math_test` (4 случая уже печатаются).
- [ ] Остальная D3DX-математика (§2.1.1, таблица импортов): `MatrixMultiply`
      (14), `MatrixLookAtLH` (4), `MatrixTranspose/Translation/PerspectiveLH/
      OrthoLH/OrthoOffCenterLH`, `Vec3Transform*`/`Vec2TransformNormalArray`
      (15), `Vec3Normalize` (3) + типы `D3DXVECTOR3`(105)/`D3DXMATRIX`(26)/
      `D3DXPLANE`(7). Матричные перемножения участвуют и в игровых
      трансформациях — менять только с проверкой битов (инвариант FP).
- [ ] **Удалить GPU-окклюзию целиком** (§3.y — решение принято 2026-10-12):
      `Render/OcclusionQueries.{h,cpp}`, `RenderComponent::GetQueries()` +
      реализации, `RenderBatch<mode>`/`switch(GetUseMode())` в `batch.cpp`,
      `IssueQueriesInvisible`, `g_useQueries`/cvar `waterUseQueries`,
      `OcclusionQueriesBank::OnFrameStart()`. В дефолте механизм выключен
      (`waterZPrepass=0`), так что это зачистка мёртвого, а не смена поведения;
      заодно уходит `IDirect3DQuery9` и `#include <d3d9.h>` из `OcclusionQueries.h`.
- [ ] Загрузка текстур через D3DX (`CreateTextureFromFileInMemoryEx`,
      `CreateCubeTextureFromFileInMemoryEx`, `D3DX_FILTER_*`, `D3DXIFF_DDS`) —
      нужен собственный DDS/TGA/PNG-загрузчик; это работа этапа 3/4, не этап 1.
- Проверка (2026-10-12): x64-сборка без ошибок (`build/PW_Game.exe`,
      15 190 016 Б, PE32+, LAA, FileVersion 2.16.1.0) + **полный E2E на tiny10
      через `PwE2E64` ПРОЙДЕН**: login `Success(5) uid=131` → lobby →
      `gamesvc_00` → `mapload: all map objects loaded` → бой 3,5 мин →
      `Switch to EGameState::Finished by 3` → `finish <token>` в dev-бэкенде;
      дампов нет, `runtime error`/`shader`/`D3D`/`assert` в логе нет, набор
      `Unknown command or variable` = эталонный. Грабель про гейт версии
      (`PW_VERSION=2.16.1` для `client-modern`) — в скиле `pw-client`.
- **Проверка критерия (2026-10-12): в КОМПИЛИРУЕМОМ клиенте утечек не
  осталось — 0.** Оставшиеся 2 `GetDevice()` в `EditorNative/EditorRender.cpp`
  в сборку `PW_Game.exe` **не входят**: граф `Tools/ClientCMake` = 25
  объектных библиотек (`Client Core Foundation GameChatClient MemoryLib
  Network NivalInput PF_Core PF_GameLogic PF_Minigames PW_Client RemoteObjects
  Render RpcBase Scene Scripts Shared Sound Terrain UI libdb mballocator
  nio transport`), `EditorNative` и `PF_EditorNative` среди них **нет**
  (`.obj`-ов в `build/x64-wine` — 0). Эти проекты живут только в VS-решении
  `Src/PF.sln` (внутренний редактор) и для клиента не собираются вовсе —
  «снимать с сборки на этапе 4» не нужно, нужно только **удалить из дерева**
  вместе с их `.vcproj`, когда решим, что редактор нам больше не нужен
  (решение отложено; в плане NVRHI он не участвует).
  Формальный grep по `Src/EditorNative` поэтому в критерий не входит.
- в `Src/Render` — только
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
- [x] Occlusion-запросы — **решение принято: отказываемся** (2026-10-12,
      §3.y). Реализация удаляется целиком на этапе 1 (остаток), в NVRHI-бэкенде
      GPU-окклюзии не будет; при необходимости вернёмся отдельной задачей
      (CPU-culling / HZB), вне рамок миграции.
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

### 3.w Бюджет дескрипторов (замерено этапом 0, определяет форму бэкенда)

Из пробы (lavapipe Mesa 26 в tiny10; на хостовом llvmpipe — то же «почти
безлимитно»): `maxPerStageDescriptorSampledImages` / `...Samplers` /
`...UniformBuffers` ≈ 1 015 808, `maxImageDimension2D` = 16384,
`maxColorAttachments` = 8, `maxVertexInputAttributes` = 32. Т.е. количество
текатур/сэмплеров/CB на стадию ограничением не является.

Единственное реальное ограничение — **`maxBoundDescriptorSets = 8`**. Для
клиента это проектирующее число: его модель — 16 PS-сэмплеров
(`Sampler::MIN/MAX_PS_SAMPLER_INDEX`, §1.x) + массив из 261 указателя текстур
(`lastTexturesPtrs`) + константные буферы под виртуальные регистры `c0..c255`
(`GlobalMasks.h`). В Vulkan всё это раскладывается по set'ам, и набор должен
влезть в 8 (у NVIDIA типично 7-8, у AMD 8, у D3D12 своя модель — 32 root
параметра + таблицы). Значит в `NvrhiBackend` нужен **фиксированный макет
сетов** (например: 0 = общие CB/«константы кадра», 1 = материал, 2 = текстуры,
3 = сэмплеры, 4..7 = постэффекты/компьют), а не «по сету на каждый ресурс», и
это надо решить до написания бэкенда — иначе `IBindingLayout` придётся
пересобирать. Проверить на реальном железе (там `maxBoundDescriptorSets` может
быть 7).

### 3.x GAP: swapchain

В NVRHI `6b96fb03` **нет ни `ISwapchain`, ни `createSwapchain`** (проверено
по `include/` и по `doc/Tutorial.md`). Приложение само создаёт DXGI swapchain /
`VkSwapchainKHR`, пересоздаёт при ресайзе, и оборачивает бэк-буфер через
`IDevice::createHandleForNativeTexture`. Т.е. в плане это не «бэкенд сам всё
сделает», а отдельный кусок работы: перечисление адаптеров, создание
странического представления, получение кадров, обработка
`VK_ERROR_OUT_OF_DATE_KHR` / `DXGI_STATUS_OC_RESIZED`. Это же — единственная
часть, где придётся держать нативный Vulkan/DXGI код в обход NVRHI.

### 3.y Решение: GPU-окклюзии не будет (принято 2026-10-12)

**Факт по NVRHI** (`6b96fb03`): в `nvrhi.h` есть только `IEventQuery` и
`ITimerQuery` (`beginTimerQuery/endTimerQuery/getTimerQueryTime`);
`IQuery`/occlusion-типа нет нигде — ни в `d3d12.h`, ни в `vulkan.h`.

**Факт по клиенту (разбор кода, 2026-10-12) — механизм выключен по умолчанию
и даже включённый работает в одном запросе на камеру за кадр:**

1. `OcclusionQueries::s_useMode` инициализирован `QUM_NONE`. Единственные места,
   где ставится `QUM_ISSUE` / `QUM_CHECK_AND_ISSUE`, — `PFRenderWater.cpp:93`
   (внутри `IssueInvisibleQueries()`) и `:220`, и оба под гейтом
   `if (g_doTerrainZPrepass)`, а `g_doTerrainZPrepass = false`
   (cvar `waterZPrepass`, `STORAGE_NONE`). Значит **в дефолтной конфигурации
   `RenderBatchesPtrArray()` всегда идёт по ветке `QUM_NONE`**, а
   `Batch::IssueQuery*` не вызывается вовсе.
2. `OcclusionQueriesBank` содержит 2 слота (`StaticArray<OcclusionQuery, 2>`),
   но `EndNextQuery()` фиксирует `currentFrame = s_currentFrame`, после чего
   `BeginNextQuery()` до конца кадра возвращает `E_FAIL`. Итого — **один
   occlusion-запрос на камеру за кадр**, а `GetLatestResults()` отдаёт этот
   единственный результат всем батчам прогона; `IssueQueriesInvisible()`
   перепроверяет только первый из помеченных `dontDraw`. Т.е. это не
   поштучный occlusion-culling геометрии, а одна OBB-проверка на камеру.
3. Не-нулевой `GetQueries()` дают только `StaticMesh` и `SkeletalMesh`
   (`Render/StaticMesh.cpp:247`, `Render/SkeletalMesh.h:112`); у
   `InstancedMesh`, `ParticleFX`, `TerrainElement`, `GrassRegion`, `RoadSceneObject`,
   `TraceFX`, `LightningFX` — `return 0`.
4. Реализация — чистая D3D9-зависимость: `#include <d3d9.h>`,
   `CreateQuery(D3DQUERYTYPE_OCCLUSION)`, `D3DISSUE_BEGIN/END`,
   `D3DGETDATA_FLUSH`, `DXQueryRef`.

**Что это значит для миграции:** GPU-окклюзия не является ни feature-wymoganiem,
ни механизмом, влияющим на картинку (пропускается только то, что и так
перекрыто, и сейчас — с однокадровой задержкой). Цена отказа — возможная
просадка FPS в отражениях воды **только при `waterZPrepass=1`** (нестандартный
режим). Поэтому вариант «обход через `getNativeDevice()` + свой query-пул» и
вариант «патчить/форкать NVRHI» **отклонены**: оба дороже того, что дают.

**Что удаляется (этап 1, остаток — это зачистка, а не переделка):**
- `Render/OcclusionQueries.{h,cpp}` целиком;
- `RenderComponent::GetQueries()` (чисто виртуальный) + все реализации
  (12+, включая `StaticMesh::GetQueries` и поле `SkeletalMesh::queries`);
- `Render/batch.cpp`: шаблон `RenderBatch<UINT mode>` сворачивается в одну
  реализацию без `mode`, `switch (OcclusionQueries::GetUseMode())` в
  `RenderBatchesPtrArray()` → тело `QUM_NONE`; `Batch::IssueQuery`,
  `Batch::IssueQueryUP`, `IssueQueriesInvisible()`, `s_queryUP`,
  `Batch::dontDraw` (после проверки других потребителей);
- `PFRenderWater.cpp`: `IssueInvisibleQueries()`, `g_useQueries` + cvar
  `waterUseQueries`, вызовы `SetUseMode/SetCurrentCameraID`. **`waterZPrepass`
  остаётся** — он про Z-препасс террейна в отражениях, к окклюзии отношения
  не имеет;
- `Render/smartrenderer.cpp:360` `OcclusionQueriesBank::OnFrameStart()`.

**Чем заменяется (если понадобится):** усиление CPU-culling (он уже есть:
`FrustumCuller`, AABB/OBB в `Scene`, `PFClientVisibilityMap`) и, отдельно от
миграции, программный occlusion по иерархии глубины (HZB) — это шейдерная
техника, она ложится на `IRenderBackend` без query-примитивов. Решения по ней
в рамках NVRHI не принимается.


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
5. **Где запускать Vulkan (уточнено этапом 0, 2026-10-12).** Прежнее
   предположение «tiny10 = нативный Windows + Vulkan-драйвер» **ошибочно**:
   tiny10 — это QEMU со стандартным VGA, в госте `Microsoft Basic Display
   Adapter` (драйвер 10.0.17763.1), `vulkan-1.dll` в системе **нет**
   (`[FAIL] vulkan: LoadLibrary(vulkan-1.dll)`). D3D11 там при этом работает
   через **WARP** (`D3D_DRIVER_TYPE_WARP`, feature level 11_0) и смоук
   NVRHI-D3D11 проходит.winevulkan на домашней машине есть, но surface-расширений
   не даёт (`VK_ERROR_EXTENSION_NOT_PRESENT`), т.е. под Wine Vulkan-окно не
   поднять.
   **Решение (2026-10-12): выбран вариант (б)** — Mesa lavapipe в tiny10
   (`tools/nvrhi_probe/setup_tiny10_vulkan.sh`), т.е. Vulkan-путь проверяется на
   том же стенде, что и E2E клиента. Ограничение остаётся: растер CPU, поэтому
   **замеры производительности (этап 3) на этом стенде невозможны** — для них
   понадобится реальное GPU-железо (варианты (a)/(c)).
   Рабочая среда Vulkan-смоука на сегодня — **нативный Linux домашней машины**
   (`tools/nvrhi_probe/build-linux`, g++): NVRHI-Vulkan поднимается, смоук
   «createTexture → clear → copy → readback» даёт эталонный пиксель
   `(0,188,255,255)`. Минус: доступен только **llvmpipe** (модули nvidia на
   хосте не загружены, RTX 3060 в перечне устройств не появляется), поэтому
   `ComputeQueue/Meshlets/RayTracing/VRS` = 0 и производительность не измеряется.
   Варианты на выбор (решение нужно до этапа 3): (а) загрузить nvidia-модули на
   хосте и гонять Vulkan-смоук на RTX 3060 из-под Linux; (б) поставить в tiny10
   Mesa lavapipe (`vulkan-1.dll` + lvp ICD) — software-Vulkan в госте, медленно,
   но картинка/детерминизм проверяемы на том же стенде, что и E2E; (в) физическая
   Windows-машина с NVIDIA. Wine остаётся **только тулчейном сборки**.
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
