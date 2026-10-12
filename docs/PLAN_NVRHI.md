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

### 1.2 Фактическое потребление — посчитать нельзя, счётчики сломаны

`smartrenderer.cpp` задумывал давать ровно нужную метрику:
`numVSUniq/numPSUniq` (уникальных VS/PS за кадр), `numVSTotal/numPSTotal`
(за сессию). Но:

```cpp
#ifndef _SHIPPING
  NDebug::DebugVar<int> g_numVS( "numVS", "Statistics", true );   // строка 35-36
  hash_set<void*>  psMap, vsMap, psMapTotal, vsMapTotal;          // строка 37
#endif
```

`g_numVSu/g_numPSu/g_numVSt/g_numPSt` (строки 31–34) **никогда не инкрементируются
и не читаются** — `SetValue` в `OnFrameStart()` пишет в них `psMap.size()`, но
`psMap/vsMap` заполняются только внутри `#ifndef _SHIPPING` в
`BindVertexShader/BindPixelShader`. В **shipping-сборке `psMap` пуст навсегда →
все четыре счётчика = 0**. Т.е. штатной метрики нет, и в релизном конфиге, на
котором идёт E2E/tiny10, она не соберётся.

**Вывод по п.3:** готовых данных нет; верхняя граница — 5×10⁴ пар
(потенциал контента). Для NVRHI это **не проблема**: 5×10⁴ `IGraphicsPipeline`
— это нормально (NVRHI-проекты живут с десятками тысяч), при условии что ключ
пайплайна конечен и кэш не растёт бесконечно. Проблема не в количестве, а в
том, что к ключу надо домножить состояние (§1.3).

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

**Обязательная работа перед этапом 3:** починить счётчики (вынести из-под
`#ifndef _SHIPPING` или добавить shipping-вариант) и снять реальные
`numVSUniq/numPSUniq` + число уникальных `sortId` за бой. Без этой цифры
нельзя спроектировать кэш пайплайнов (размер, eviction).

---

## 2. Поверхность D3D9, которую надо убрать

### 2.1 Утечки за пределы `Src/Render` (правятся в первую очередь)

| Место | Что |
|---|---|
| `Scene/SHGridCalculator.cpp` | `#include <d3d9.h>`, `D3DXVECTOR2/3`, `D3DXVec3Normalize`, `D3DXSHEvalDirection` (16 ссылок) |
| `PF_GameLogic/PFRenderInterface.cpp` | 41 `GetDevice()`: `SetRenderState`, `SetSamplerState(D3DSAMP_BORDERCOLOR/MIPMAPLODBIAS)`, `SetPixelShaderConstantF`, `StretchRect`, `D3DXVECTOR3`, `D3DSURFACE_DESC`, `EvictManagedResources`, `Clear` |
| `PF_GameLogic/PFClientVisibilityMap.cpp` | `GetDevice()` |
| `PF_GameLogic/SocialConnection.cpp` | скриншот: `CreateOffscreenPlainSurface`, `D3DXLoadSurfaceFromSurface`, `D3DXSaveSurfaceToFile` |
| `PF_Core/{AnimatedTexture,RecolorableTexture}.cpp` | `D3DPOOL_MANAGED` assert, `EndScene` |
| `Terrain/{Terrain,UberElement,ArrayInVRAM}.cpp` | `GetDevice()`, `GetDirect3D`, мёртвый `d3d9texture2D.h` |
| `EditorNative/*` | `SetViewport`, `D3DVIEWPORT9`, `SetFVF` |
| `Src/UI`, `Src/Game` | **0** — уже чисто |

Всего прямых `GetDevice()` — **344** (Render 153, PF_GameLogic 41, Terrain 2,
EditorNative 2).

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
- `DXManager` (managed-priority/eviction по кадрам) — **удалить целиком**.
- `DXResourcesControl.cpp` (50 D3D9-ссылок, учёт по пулам) — **уже выключен**
  (`//#define DX_RESOURCE_CONTROL`); либо удалить, либо переписать на
  NVRHI-refcount-статистику.
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
| **R2VB / `D3DUSAGE_DMAP` / vertex textures** | `atir2vb.h`, `TextureVtx` (`texture.{h,cpp}`), `FullScreenFX.cpp`, `PFPostFXParams.cpp`, `SHCoeffs.cpp`, `SHGrid*` | **Удалить.** Заменить на structured/typed buffer (SH-коэффициенты, «vertex texture»). Это же снимает 261-слотовую таблицу сэмплеров (`Sampler::MAX_VS_SAMPLER_INDEX = 260`, VS-слоты 257–260) — она существует **только** ради R2VB и упирается в лимит DX12/Vulkan (16 VS-sampler). |
| **Fetch4** (`D3DSAMP_MIPMAPLODBIAS = FOURCC_GET4`) | `ShadowMapESM_Fetch4` | **Удалить класс**, оставить `ShadowMapESM`. |
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
- **`.shd`-контейнер переформатировать**: сейчас это `{count, [vsOffset,
  psOffset]}` + DXBC-под-SM3.0. Нужен формат под DXBC-SM5 (+ SPIR-V для
  Vulkan, либо компиляция HLSL→SPIR-V на клиенте через DXC — дороже).
  Генератор `.shd` в дереве **отсутствует** (`MaterialSpec.h/.cpp` —
  «GENERATED AUTOMATICALLY! DON'T CHANGE», генератор не в дереве) → **это
  блокер: без генератора пересобрать 36 uber-shader-наборов нельзя.**
  Первый практический шаг — найти/написать генератор
  (`.hlsl` + `MaterialSpec.xml` pins → `.shd` + `MaterialSpec.{h,cpp}`).
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

Отдельно: `Sampler::MAX_VS_SAMPLER_INDEX = 260` и `lastTexturesPtrs[261]`
(`smartrenderer.cpp`) — после удаления R2VB сужается до 16.

---

## 3. Этапы

Гейт каждого этапа: сборка x64 (Wine + native) + E2E на tiny10.
**Гейт «золотые `unitstats` байт-в-байт» остаётся только для правок вне
рендер-пути**; для самих рендер-этапов он неприменим (меняется картинка) —
заменить на скриншот-дифф + `unitstats` как smoke (бой/физика не сломаны).

### Этап 0. Блокеры и разведка (1–2 смены)

- [ ] **Генератор `.shd` + `MaterialSpec.{h,cpp}`.** Найти в истории/архивах
      или написать (`Src/Render/Shaders/*.hlsl` + `MaterialSpec.xml` pins →
      `.shd` + сгенерированные классы). Без этого этапа дальше идти нельзя.
- [ ] `tools/nvrhi_probe/` — вне графа клиента: NVRHI статикой под MSVC 14.44
      `/MT` + C++17; **смоук Vulkan** на `HWND` клиента; прогон под Wine
      (нативный `winevulkan`) и на tiny10. DX12 — только проверить, что
      компилируется (запуск позже).
- [ ] Зафиксировать: `d3dcompiler_47` vs DXC для SPIR-V; размер прироста;
      какие DXGI/Vulkan-зависимости появляются.
- [ ] Починить счётчики `numVSUniq/numPSUniq/numVSTotal/numPSTotal`
      (§1.2) и **снять реальные цифры** в бою + число уникальных `sortId`.

### Этап 1. Зачистка: удалить мёртвое и D3D9-утечки (2–3 смены)

Ничего не меняет в картинке — и потому идёт первым.

- [ ] Удалить: `rendertarget.h` (уже `#error`), `d3d9texture2D.h`,
      `atir2vb.h` + `TextureVtx` + R2VB/VT-ветки, `ShadowMapESM_Fetch4`,
      `D3DUSAGE_DMAP`, vendor-FOURCC (`DF16/DF24/RAWZ/RESZ/INTZ/NULL/R2VB`) и
      `Features.h`-фичи под них, SM2.0-путь (`FORCE_SM20`, `.shd2`,
      `forceSM20Usage`, `shader3compatibilityError`), 71 `D3DRS_*` из
      dump-таблицы, `D3DUSAGE_QUERY`, `D3DQUERYTYPE_EVENT`-механику
      (`Renderer::Synchronize`), `DXManager` целиком, `DXResourcesControl`
      (уже выключен).
- [ ] Убрать FVF/fixed-function (4 места, §2.2).
- [ ] Ликвидировать утечки D3D9 из `PF_GameLogic`/`Scene`/`Terrain`/
      `PF_Core`/`EditorNative` (§2.1) — 41+ `GetDevice()` снаружи.
- [ ] `Scene/SHGridCalculator.cpp`: D3DX-математика → своя/`System/Geom`.
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
- [ ] Occlusion → `IQuery`; UP-вызовы → временные буферы; `StretchRect` →
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
