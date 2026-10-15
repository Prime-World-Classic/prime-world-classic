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

### Очередь работ (зафиксирована 2026-10-14)

Шаги 1–4 не зависят от этапа 3 и закрывают хвосты этапа 2/0; 5–8 готовят
этап 3. Каждый — отдельный коммит со своим гейтом (сборка + E2E + скриншот-дифф).

| # | шаг | почему здесь |
|---|---|---|
| 1 | ✅ вендоринг `Vendor/NVRHI` + `Vendor/SDL3` (закрыто 2026-10-14, §3.0) | согласовано; в рантайме ничего не меняет, развязывает этап 3 |
| 2 | ✅ счётчики шейдеров/`sortId` в бою (закрыто 2026-10-14, §1.2.1) | закрывает хвост этапа 0, даёт настоящие числа для кэша пайплайнов (§1.2.1) |
| 3 | ✅ инстансинг, шаг 1: механизм D3D9 в бэкенде (закрыто 2026-10-14, §2.10) | без этого этап 3 начнётся с переделки draw-пути (§2.10) |
| 4 | ✅ caps: `BackendSystemInfo` в seam (закрыто 2026-10-15, `3b01b92c4`, §2.5); эталонный дамп пинов перепроверен по новой цепочке; форс DSM-флагов сделан механизмом (`force_dsm`), на D3D9-стенде не включается (замер `tools/dsm_probe`, §2.15 п.9) — покрытие DSM переносится на этап 3 | матчер `config.txt` остался D3D9-ным осознанно (§2.5 п.1) |
| 5 | ✅ генератор `MaterialSpec` + 3 DXC-провала (70/70) — закрыто 2026-10-14; преконпиляция `deprecated_shaders` перенесена на этап 3 (в shipping рантайм-компиляции нет, §2.6 п.6) | этап 3 добавляет пины — перегенерация воспроизводит дерево побайтово (§2.6) |
| 6 | ✅ механизм блокировки буферов переведён в seam (закрыто 2026-10-15, `d8e3f45eb`, §2.8 «Восьмой шаг»); staging-кольцо — этап 3 | остаток пункта «пулы» в этапе 2 |
| 7 | ✅ `ConstBlock` (c0..c255 → один буфер) — закрыто 2026-10-15, §2.9: тень + `GetConstBlock`/`TakeDirty` в seam, `D3D9Backend` читает состояние из устройства (`verify_const_block` — 0 расхождений), счётчики тронутых/изменившихся регистров, `tools/const_layout` | трение §2.7; полностью проверено на живом D3D9; числа поправили вывод о дизайне (см. §2.9) |
| 8 | ✅ ключ пайплайна + счётчик уникальных ключей (закрыто 2026-10-15, §1.4): ключ собирается в `CSmartRenderer` перед рисованием (пары VS/PS + декларация вершин + семантический `RenderState` + сэмплеры + stencil + тип примитива + инстансинг + форматы RT/depth), считается семантический и raw-хеш, счётчики по сессии и по кадру | размер кэша измерен: **206** ключей на бой (85 в кадре), raw-состояние добавляет только **+4** → в ключ его брать не нужно (§1.4) |

### Гейт качества (замена «золотых unitstats»)

Рендер-этапы ломают байтовый паритет по определению, поэтому:

| Тип правки | Гейт |
|---|---|
| вне рендер-пути (этап 1 — зачистка мёртвого, ликвидация утечек) | обычный: x86 VS2008 + x64 Wine + x64 native + E2E tiny10 + **золотые `unitstats` байт-в-байт** |
| рендер-этапы (2, 3, 4) | сборка x64 (Wine + native) + **E2E на tiny10: вход → лобби → бой** + скриншот-дифф (новый эталон фиксируется в ветке: `tools/render_shot/refs/`, сравнение — `tools/render_shot/shot_diff.py`) + `unitstats` как smoke (бой/физика живы, значения не эталон) |

**Поправка к первой строке (2026-10-15, ветка `client-modern`):** x86-контур
(`./build_client_wine.sh` = VS2008, `ShippingSingleExe|Win32`) в этой ветке
**не собирается** и как гейт не используется: seam-заголовки с NSDMI давали
34 × `error C2864` (чинится конструктором — сделано), но дальше идут
`error C2039: 'data' : is not a member of 'std::vector<_Ty>'`
(`Render/screenshot.cpp:52`), `error C2552: non-aggregates cannot be
initialized with initializer list`, `error C2275: 'Render::HTexture' : illegal
use of this type as an expression` и `error C4018` (в x86-rsp стоит `/WX`,
предупреждения становятся ошибками). Плюс `Render.vcproj` ссылается на удалённый
на этапе 1 `OcclusionQueries.cpp`. Для этой ветки гейт — x64 Wine + x64 native +
E2E tiny10; x86 остаётся только там, где он жив: он единственный собирает
`Src/ShaderCompiler` (`cl ShaderCompiler: 12 objs` в x86-прогоне), т.е. правки
тула ими проверяются. Seam-заголовки при этом держим C++03-совместимыми
(конструкторы вместо NSDMI, `= {}` только у агрегатов) — иначе x86-прогон
перестаёт доходить до ShaderCompiler.

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

### 1.2.1 Замер (2026-10-13, tiny10, бой, контент из 35 перегнанных шейдеров)

```
ShaderStats: pairTotal=185 pairFrame=146 vsTotal=59 vsFrame=55 psTotal=68 psFrame=61 sortTotal=117 sortFrame=99
```

`sortId` — CRC материального состояния (техника + пины + сэмплеры +
рендер-состояния), считается `Material::GetSortId()`; учитывается в
`Batch::Prepare()` (`Render/renderstats.h`). Это второй член полного ключа
пайплайна, и он **меньше** числа пар: 117 против 185. Разница (~68) — проходы,
которые не идут через батчи: PostFX/полноэкранные проходы и UI биндят шейдеры
напрямую, их состояние в `sortId` не попадает. То есть для батчевого пути
полный ключ пайплайна укладывается в ~117 состояний при ~146 парах в кадре,
а не в декартово произведение.

Цифры стабильны на всём протяжении боя (первый снимок через ~8 с после mapload:
183/144/59/68; последний перед `Finished`: 185/146/59/68; sortId — 117/99).

* **185 уникальных пар (VS,PS) за весь бой** — это ядро ключа пайплайна.
* **146 пар связывается внутри одного кадра** — то есть за кадр меняется почти
  вся сессия: рабочий набор велик, но потолок мал.
* 59 уникальных VS и 68 уникальных PS.

Выводы для этапа 3:
* Верхняя граница из контента (§1.1, 49 966 вариаций в одном `BasicMaterial.shd`)
  к реальности отношения не имеет — фактическое число пар меньше примерно в 270
  раз. Кэш пайплайнов нужен на сотни записей, а не на миллионы; eviction-политика
  почти не важна, кэш прогревается за первые кадры.
* Домножение на `RenderState`/форматы RT (§1.3) остаётся, но его потолок —
  число уникальных `sortId`, а не число вариаций в `.shd`. Счётчик `sortId`
  по-прежнему не добавлен.

Как снималось: `PW_EXTRA_DEFS=PW_RENDER_SHADER_STATS ./build_client_x64_wine.sh`;
в `OnFrameStart()` добавлен `DebugTrace("ShaderStats: ...")` раз в 300 кадров —
`DebugVar`-ы из лога shipping не читаются (игровая консоль на E2E-стенде
недоступна). Побочная находка: блок статистики в обычных сборках **не
компилируется** (shipping без дефайна), поэтому в нём прятался битый каст
`UINT64(size_t)pLastVertexShader` (C2275/C2146) — всплыл только в этом билде,
поправлено на каскад указателей с хешированием (коллизии при ~10^4 ключей
~5e-12).

**Подтверждение 2026-10-14** (тот же стенд, сборка `PW_Game_bb93f2d0.exe`,
лог `2026.10.14-14.59.29`, прогон чистый: login → mapload → `Finished`):
`pairTotal=185 pairFrame=142..148 vsTotal=59 psTotal=68 psFrame=60..64
sortTotal=117 sortFrame=98..99` — цифры совпали с замером 2026-10-13 в
пределах шума прогона, то есть потолок устойчив и на него можно опираться при
размере кэша.

Попутно тот же прогон даёт числа для `ConstBlock` (§2.7), их раньше не было:

```
ConstStore: VS c0..c234 (235 reg) | PS c0..c220 (221 reg)
```

за окно в 300 кадров: ~3,5·10^5 записей констант на каждый стад (≈1,2 тыс./кадр,
всего ≈2,4 тыс./кадр), из них **изменились** значения лишь ~270 (VS) и ~35 (PS)
за окно. То есть подавляющая часть записей — повторная запись того же значения:
для Vulkan-пути решающей будет не перепись буфера, а dirty-отслеживание
(`ChangedSinceTurn` уже есть в `ConstStore`), а диапазон реально используемых
регистров (c0..c234 / c0..c220) укладывается в один uniform-буфер на стад.

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

Число уникальных `sortId` снято — см. §1.2.1: 117 за сессию (99 в кадре).
Не измеренным оставалось состояние проходов вне батчей (PostFX, полноэкранные
проходы, UI): они биндят шейдеры мимо `Batch::Prepare()`, их `RenderState` в
статистику не попадает. Закрыто шагом 8: ключ снимается не в `Batch::Prepare()`,
a в `CSmartRenderer` непосредственно перед рисованием — в статистику попадают и
проходы вне батчей. Полное число и вывод — §1.4.

### 1.4 Замер полного ключа пайплайна (шаг 8, 2026-10-15)

Ключ собирается в `CSmartRenderer::NotePipeline()` (`Src/Render/smartrenderer.cpp`)
перед каждым рисованием — `BeginBatch`, `DrawIndexedPrimitive`, `DrawPrimitive`,
`RenderDeferred2D`. Состав (ровно то, что станет объектом пайплайна в NVRHI):

```
(пара pLastVertexShader/pLastPixelShader, pLastVertexDeclaration,
 RenderState::GetBitMask(), хеш сэмплеров, ключ stencil,
 ERenderPrimitiveType, instancingEnabled, формат RT[0], формат depth)
```

Сэмплеры и stencil берутся из теней `RenderStatesManager`
(`GetSamplerShadowHash()`, `GetStencilShadowKey()`), `RenderState` — из
семантического состояния. Параллельно считается **raw**-ключ: поверх
семантического домешивается `GetRenderStateShadowHash()` — хеш всех записей
`m_state`, т.е. ровно то, что `D3D9Backend::ApplyState` отправляет в устройство.
Смысл пары — проверить, нужен ли raw-уровень в ключе Vulkan-пайплайна.

Числа (бой MOBA, 1 игрок, tiny10 = WARP, инструментальная сборка
`-DPW_RENDER_SHADER_STATS`, сессия ~5 мин, лог `2026.10.15-02.49.38`):

| Метрика | Значение |
|---|---|
| уникальных ключей за сессию, семантический | **206** |
| уникальных ключей за сессию, raw | **210** (разница **+4**) |
| уникальных ключей в одном кадре | **84–86** |
| рисований в кадре | **457–458** |
| рисований за сессию | 2,32 млн |
| для сверки: пар VS/PS и `sortId` за бой | 185 и 117 (§1.2.1) |

Выводы:

1. **Размер кэша пайплайнов — сотни, не тысячи.** 206 объектов на бой, из них
   ~85 активных в кадре. Оценка §1.3 («верхняя граница 10⁷») не подтвердилась:
   домножение на `RenderState` и форматы RT почти не работает, потому что
   материал (`sortId`) уже фиксирует состояние, а форматов RT в бою два.
   Для этапа 3 это значит: кэш пайплайнов можно держать открытым хеш-таблицей без
   LRU и без «ленивой» компиляции вариантов в отдельном потоке.
2. **Raw-состояние в ключ не входит.** +4 ключа на 206 — обходные записи
   `SetStateDirect` почти не плодят варианты. Значит нормализовать D3DRS-биты
   (главный источник трения §2.7) под ключ не нужно: достаточно семантического
   `RenderState` + сэмплеров + stencil. Это снимает целый пласт работы этапа 3.
3. **85 переключений пайплайна на 458 рисований** — средний пробег 5,4 рисования
   на пайплайн. Переключение пайплайна в Vulkan дешёвое, а вот смена
   `const offset` / descriptor set происходит ~458 раз за кадр. Кольцо
   uniform-буферов (§2.9) надо считать по **рисованиям**, а не по пайплайнам.
4. Шаги 7 и 8 смыкаются в один бюджет: 458 рисований × 3760 Б (VS 235 + PS 221
   регистров) = **1,72 МБ/кадр** при наивной загрузке всего блока констант на
   каждое рисование (≈36 МБ/с при 21 к/с). Отсюда обязательность `TakeDirty`
   (§2.9) и маленького грязного диапазона.

Побочный результат (найдено счётчиком, закрыто в том же коммите):
`BindRenderTargetColor(i, NULL)` — сброс MRT с ненулевым индексом — в бою
встречается. Попытка читать формат RT при `pColorSurface == nullptr` падала
(`0xC0000005`, `mov rax,[rdi]` при `rdi = 0`; разборка RVA `0x615FE6` показала
`call *0x60(%rax)` — `GetDesc`). Прежний код читал поверхность только для
индекса 0, поэтому путь был безопасен; закрыто ранним выходом с обнулением
форматов. Эталонный кадр не меняется.

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


### 2.1.2 Загрузка текстур: D3DX-фолбэки убраны (2026-10-13)

Свой загрузчик DDS в клиенте **уже есть** — `Src/Render/ReadDDS.cpp`
(порт DDS-лоадера Microsoft: `TextureHolder`/`CubeTextureHolder`, DXT1/3/5,
L8/L16, A8R8G8B8 и пр., пропуск mipmap-уровней, объёмные текстуры отвергает).
`TextureManager` сначала звал его, а `D3DXCreateTextureFromFileInMemoryEx` /
`D3DXCreateCubeTextureFromFileInMemoryEx` — только как фолбэк для не-DDS.

Инвентаризация контента (`tools/dds_inventory`, 8031 `.dds` в `Data/`):
3715 A8R8G8B8, 3001 DXT5, 1294 DXT1, 5 L8, 16 кубов (13 DXT1 + 3 A8R8G8B8);
mipmap-цепочки до 12 уровней; DX10-заголовков, volume и прочих форматов нет;
в манифесте обновлений (`files.list`) из картинок только `.dds` (4318 записей).
Размер битовых данных сходится с тем, что ожидает загрузчик, у 8030 из 8031
файлов; единственный расходится — `Data/Heroes/Gunslinger/S2/_.dds` (объявлено
11 mipmap, данных на один), в `files.list` его нет, то есть в контент он не
уходит.

Вывод: фолбэки мёртвы и удалены — 2D, кубы и отладочный `ProcessMipMaps`
(`show_mipmap`) идут через свой загрузчик; вместо тихого отката на D3DX теперь
явная ошибка «not a loadable DDS». Импорты `d3dx9_43.dll`: 17 → 16
(исчез `D3DXCreateCubeTextureFromFileInMemoryEx`).

**Эмблемы гильдий — точечно не чиним, переделывать заново (решение
пользователя, 2026-10-13; задача в `PENDING.md` §9).** Это единственный
оставшийся загрузчик текстур, зависящий от устройства: гейтвей отдаёт JSON с
`guild_icon_url` (`Game/PF/Server/HttpGateway/GatewayJSonHandler.cpp:816` →
`GuildData::icon_url`), клиент качает байты (`PF_GameLogic/GuildEmblem.cpp`) и
зовёт `CreateARGBTextureFromFileInMemory` → `D3DXCreateTextureFromFileInMemoryEx`.
Формат картинки нигде не зафиксирован (URL приходит с веб-бэкенда), так что
писать декодер не под что. Следствия, которые надо помнить: `d3dx9_43.dll`
полностью из клиента снимается только вместе с этой переделкой (импортов сейчас
16), и `IRenderBackend::CreateTexture` до неё не считается закрытым — фасад
этапа 2 закрывает остальной путь загрузки (DDS). До этого `IRenderBackend::CreateTexture` нельзя
считать закрытым.

### 2.8 Seam бэкенда: что уже перенесено (2026-10-13)

`IRenderBackend` (`Src/Render/renderbackend.h`) + `D3D9Backend`
(`d3d9backend.{h,cpp}`). Через бэкенд из `renderer.cpp` идут: `BeginScene`,
`EndScene`, `Present`, `GetViewport`, `SetScissorRect`, `Clear` (три места,
свои флаги `bcTarget/bcZBuffer/bcStencil` вместо `D3DCLEAR_*`),
`SetVertexShaderConstants`, `SetPixelShaderConstants`, `SetClipPlane`
(пара `D3DRS_CLIPPLANEENABLE` + `SetClipPlane(0,..)` спрятана внутри бэкенда),
`EvictManagedResources`. Публичный API `Renderer` не менялся — вызывающий код
не правился.

Осознанные течи интерфейса: `HRESULT` (такой же контракт у NVRHI), `RECT` в
`Present`/`SetScissorRect` (Win32, нужен и Vulkan-свопчейну), семантика
`Present` (прямого swapchain-интерфейса у NVRHI нет — §3.1).

**Второй шаг (2026-10-13, тот же день) — `smartrenderer.cpp`: 27 → 9.**
Через бэкенд теперь идут биндинги (`SetStreamSource`, `SetIndices`,
`SetInputLayout`, `SetVertexShader`, `SetPixelShader`, `SetTexture`,
`SetRenderTarget`, `SetDepthStencil`, `SetViewport`) и вывод
(`DrawIndexed`, `Draw`, `DrawIndexedUP`, `DrawUP`), а также `GetViewport`.
Тип примитива в интерфейсе — проектный `Render::ERenderPrimitiveType`
(`renderprimitivetype.h`), перевод в `D3DPRIMITIVETYPE` делает бэкенд
(`ConvertPrimitiveType`).

Ручки ресурсов: `HBuffer`/`HTexture`/`HSurface`/`HShader`/`HInputLayout` —
обёртки над указателем (`MakeBackendHandle<H>(ptr)`). На этапе 2 внутри —
указатель на объект D3D9, но тип на каждый вид ресурса свой, чтобы этап 3
подставил `nvrhi::BufferHandle` и т.п., не трогая вызывающий код, и чтобы
`HTexture` нельзя было передать вместо `HBuffer`.

Биндинги возвращают `HRESULT`, хотя у D3D9-бэкенда это проброс кода вызова:
вызывающий код `SmartRenderer` логирует их по `s_DXWarnLevel` — терять эту
диагностику нельзя, поэтому бэкенду этапа 3 (у NVRHI методы `void`) возвращать
`S_OK`.

Что осталось в `smartrenderer.cpp` (9 `GetDevice()`, всё осознанно):
`CreateVertexDeclaration` (создание ресурса — этап 2, §чек-лист, ещё не закрыт);
4 вызова в `RenderDeferred2D` (единственный фиксированный блок: `SetFVF`,
`DrawPrimitiveUP` — план §1.4); `SetStreamSourceFreq` в `ApplyStates` —
механизм D3D9-инстансинга по частоте потока, аналога нет ни в NVRHI, ни в
Vulkan (там instanced draw + per-instance атрибуты), схему меняем на этапе 3;
2 вызова внутри `#ifdef _DEBUG_VIEWPORT` (не компилируются); 1 — внутри
закомментированного блока.

*(уточнение 2026-10-14: `SetStreamSourceFreq` и `CreateVertexDeclaration` из
этого списка ушли в бэкенд — §2.10 шаг 1; в `smartrenderer.cpp` остались только
`RenderDeferred2D` и некомпилируемый `_DEBUG_VIEWPORT`)*

Итог по `Render/`: `renderer.cpp` — 12 прямых вызовов устройства,
`smartrenderer.cpp` — 9.

**Гейт для рендер-правок (2026-10-13).** Логи и дампы не показывают, что
картинка поехала: неверный порядок параметров draw, неверный вьюпорт или
подставленный шейдер молча рисуют мусор. Поэтому кадр снимается с ВМС
(`python3 vm7/vncshot.py 127.0.0.1 5918 /tmp/shot.ppm`), дальше
`tools/render_shot/ppm_tools.py` (`diff` — доля отличающихся пикселей,
`png` — чтобы посмотреть глазами). Первый прогон после переноса draw-пути:
сцена боя отрисована целиком (ландшафт, юнит с полосой HP, партиклы, UI,
миникарта), два кадра с интервалом 25 с отличаются на 4,9% пикселей — сцена
анимируется, не залипла. Порог `--limit 0` уместен только для сравнения
«до/после» одного и того же момента; сам по себе кадр боя не детерминирован
(таймер, позиция юнита), так что это smoke, а не пиксельный эталон.

**Третий шаг (2026-10-13) — создание ресурсов и поверхности: `renderer.cpp`
12 → 5.** В интерфейс добавлены `CreateVertexShader`/`CreatePixelShader`
(код — DXBC из `.shd`, размер пока не спрашивается: D3D9 читает свой размер из
самого блоба; на этапе 3 в этот же слот ляжет SPIR-V — §1.3),
`CreateInputLayout(const VertexFormatDescriptor&)` и операции над поверхностями
`GetRenderTargetData`/`ColorFill`/`StretchRect`.

Важные детали, которые надо помнить при переносе:
* перевод `VertexFormatDescriptor` → `D3DVERTEXELEMENT9` переехал из
  `Renderer::CreateVertexFormatDeclaration` внутрь `D3D9Backend` — это и есть
  часть бэкенда (в Vulkan на этом месте `VkVertexInputState`);
* create-методы возвращают `HRESULT` наружу, а не «ручку или 0»: вызывающий код
  логирует код ошибки через `NI_DX_THROW`, терять его нельзя;
* фильтр копирования поверхности — свой `IRenderBackend::EBackendFilter`
  (`bfPoint`/`bfLinear`): замер вызовов показал, что `D3DTEXF_ANISOTROPIC` и
  прочие значения в клиенте не встречаются;
* семантика владения не менялась: `DX*Ref(..., false)` (adopt) — бэкенд отдаёт
  уже созданный объект с счётчиком 1.

Осталось в `renderer.cpp` 5 прямых вызовов устройства: `GetDeviceCaps`
(`FillCaps`), `GetDirect3D` и `GetCreationParameters` (интроспекция адаптера и
параметров создания — это слой создания устройства, его этап 2 не закрывал),
`GetBackBuffer` + `CreateOffscreenPlainSurface` в `SaveScreenshotToFile` (там же
остались `D3DXLoadSurfaceFromSurface` и `D3DXSaveSurfaceToFile(PNG)` — см. §2.13)
и одна закомментированная строка `Present`.

**Четвёртый шаг (2026-10-14) — окно и отвязка ресурсов.** Считать вызовы только
по `GetDevice()->` было неверно: в `renderer.cpp` они идут и через член
`pDevice`. Честный замер `pDevice->` + `GetDevice()->` в `renderer.cpp`:
**19 → 10**.

* **Окно уходит из `Renderer` и из seam'а.** Новый `Render/renderwindow.h`:
  `struct NativeWindow { void* p; }` + `MakeNativeWindow(void*)`. `Renderer`
  хранит `NativeWindow window` вместо `HWND hWnd`; `Renderer(HWND)` →
  `Renderer(NativeWindow)`, `SetHWND` → `SetWindow`, `Renderer::Present(HWND,…)`
  → `Present(NativeWindow,…)`; в `IRenderBackend::Present` HWND заменён на
  `NativeWindow`. Граница платформы — `Render/RenderInterface.cpp`
  (`MakeNativeWindow(nWnd)`, `nWnd` приходит из `NMainFrame::GetWnd()`).
  `HWND` в `renderer.{h,cpp}` остался только как приведение в D3D9-местах
  (`CreateDevice`, `pp.hDeviceWindow`) — это код создания устройства, он уходит
  на этапе 4. Отдельный заголовок, а не место в `renderbackend.h`, нужен потому,
  что `renderbackend.h` в `renderer.h` не подключается намеренно (renderer.h
  включается почти везде).
* **Владеющие getters поверхностей.** В seam добавлены
  `GetRenderTarget(index, HSurface*)` и `GetDepthStencil(HSurface*)` — в отличие
  от `GetBackBuffer` (не владеющая ручка, действительна до конца вызова)
  они отдают владеющую: `OnDeviceChanged()` хранит поверхности кадра и глубины
  между кадрами через `DXSurfacePtr::Attach`. Путаницу владения здесь легко
  сделать — `GetBackBuffer` в снимке кадра (§2.13) намеренно не владеющая.
* **`ClearDeviceResources()` — через seam** (нулевая ручка = unbind):
  `SetInputLayout/SetVertexShader/SetPixelShader/SetTexture×16/
  SetRenderTarget×N/SetStreamSource×M`. Границы перебора (`NumSimultaneousRTs`,
  `MaxStreams`) кэшируются в `Renderer::Caps` при `FillCaps()` — раньше ради них
  каждый раз дёргался `GetDeviceCaps`; в ветке неудачного `GetDeviceCaps`
  поставлены консервативные значения (1 RT / 256 потоков).
* **`InvalidateResources()` → `pBackend->EvictManagedResources()`.**

Остаток в `renderer.cpp` (10) — это слой создания/жизни устройства и caps:
`Release`, `Reset`, `TestCooperativeLevel` (D3D9-восстановление, §2.3),
`CreateQuery(D3DQUERYTYPE_EVENT)` ×2 (вертикальная синхронизация; у NVRHI —
`EventQuery`, в seam ещё не положено), `GetDisplayMode` (перечисление режимов —
заменит SDL, §3.x GAP), `GetDeviceCaps`, `GetDirect3D`, `GetCreationParameters`
(§2.5 — синтез caps-структуры на этапе 3).

**Грабли include-списков.** `renderer.h` отдаёт наружу кучу типов по
транзитивности; удаление из него `#include "vertexformatdescriptor.h"`
сломало `AOERenderer.cpp` (тот не объявляет тип сам). Правя списки include в
общих заголовках `Render/`, надо пересобирать полный граф, а не только свой TU.

**Пятый шаг (2026-10-14) — `RECT` уходит из seam'а.** В `renderbackend.h`
добавлен `BackendRect { long left, top, right, bottom; }` + `MakeBackendRect()`;
`Present`, `SetScissorRect`, `StretchRect`, `ReadSurface` принимают его, а не
Win32 `RECT`; конвертация `BackendRect` → `RECT` — только в `d3d9backend.cpp`
(`ToD3DRect`). В `Renderer::Present` исчез `reinterpret_cast` `Render::Rect` →
`RECT` (типы совпадали по размеру — `Rect.h` держит это `NI_STATIC_ASSERT`'ом,
но seam не должен был этого знать).

* **Гравля: `NULL`-прямоугольник в D3D9 означает «вся поверхность»**
  (`StretchRect(src, 0, dst, 0, …)` — копирование целиком; `SetScissorRect(NULL)`
  тоже отличается от пустого). Подмена null на `BackendRect{0,0,0,0}` молча
  превращает копирование в пустое — поэтому null проходит сквозь seam как null
  (`ToD3DRect` возвращает false, бэкенд передаёт `NULL`/`0`).
* Публичный API `Renderer` пока **принимает `RECT` и `D3DTEXTUREFILTERTYPE`**
  (`SetScissorRect`, `StretchSurfaceRect`, `SaveFrameToFile`): его зовут из
  `PF_GameLogic` (3 места scissor) и из `Render/*` (surface-обёртки). Это
  следующий слой типизации, к seam отношения не имеет.
* Scissor закрыт полностью (тот же день): в seam добавлен
  `GetScissorRect(BackendRect*)` (чтение нужно для save/restore; на этапе 3 на
  этом месте — тень в бэкенде, динамического состояния у NVRHI нет),
  `batch.cpp::ZeroScissor` переведён на `GetBackend()` + `BackendRect`
  (`zeroRect` остался 1×1: пустой scissor D3D9 отвергает). Путь обёрнут в
  `#ifndef _SHIPPING` — в shipping-сборке он вырезан, поэтому проверен компиляцией
  и E2E-smoke, а не исполнением. В `ShadowManager.cpp` scissor/viewport — под
  `#ifdef _DEBUG_VIEWPORT` (не компилируются).

**Шестой шаг (2026-10-14) — текстуры и поверхности создаются через seam.**
Живые обращения к устройству внутри `Render/` (кроме `d3d9backend.cpp` и
`renderstatesmanager.cpp`): **66 → 48**, из них к самому устройству — 50 → 32
(ещё 16 — это `pD3D->`, слой создания устройства, этап 4). Закрыто создание
текстур/кубов/поверхностей, `UpdateTexture`, все `StretchRect`, `Clear` в
`WaterManager`, unbind-`SetTexture` в `FullScreenFX`.

* **Словари.** `EBackendFormat` — набор замкнут по фактическому контенту
  (замер `tools/dds_inventory`: 8031 DDS — A8R8G8B8 3715, DXT5 3001, DXT1 1294,
  L8 5; кубы DXT1 13 / A8R8G8B8 3; плюс проектный `ERenderFormat` и глубина
  D24S8), а не по всему пространству `D3DFMT_*` (полный список `D3DFMT_*` в
  `Render/` — это `D3DFormat2Str` и проверки адаптера, такие ресурсы не
  создаются). `bfUnknown` = отказ создания (`D3DERR_INVALIDCALL`), а не подмена
  формата: молча поехавший альфа-канал в логах не виден. DXT1..5 **не
  сливать** в один: у DXT2/3 явный альфа-канал, у DXT4/5 интерполируемый,
  DXT2/DXT4 — premultiplied.
* **`EBackendUsage` — только «назначение»** (RENDERTARGET / DEPTHSTENCIL /
  AUTOGENMIPMAP). Биты памяти (DYNAMIC, WRITEONLY) в него не кладутся: их
  выражает пул, иначе одно значение задавалось бы дважды и разошлось бы с
  `EBackendPool`.
* **`D3DPoolToBackend(pool, usage)` — перевод парой.** В D3D9 «динамичность» —
  флаг usage при `D3DPOOL_DEFAULT`, в seam'е — отдельное значение пула. Без
  WRITEONLY round-trip ломается: `RENDER_POOL_DYNAMIC` даёт WRITEONLY|DYNAMIC,
  `RENDER_POOL_TEX_DYNAMIC` — только DYNAMIC, и recreate текстуры
  (`Texture2D::CreateInternal`) тихо менял бы флаги.
* **`EBackendSurfaceKind`** (`ssPlain`/`ssRenderTarget`/`ssDepthStencil`): у D3D9
  под каждую — своя фабрика, у Vulkan/DX12 — одна текстура с usage-флагами.
  Multisample в сигнатуру не положен: в клиенте везде `D3DMULTISAMPLE_NONE`
  (замер), качество 0.
* **Куда легли конвертеры:** D3D→нейтральное — в `renderflagsconverter.{h,cpp}`
  (это и есть D3D9-слой словарей, его уже включали все затронутые TU),
  нейтральное→D3D — внутрь `d3d9backend.cpp` (`D3DFormatFromBackend`,
  `D3DUsageFromBackend`, `D3DResPoolUsageFromBackend`).
* **Что осталось D3D9-структурой:** `Texture2D`/`RenderSurface` хранят
  `D3DSURFACE_DESC` и recreate делают из него, поэтому параметры в seam
  переводятся обратно из D3D9 (`D3DFormatToBackend(desc.Format)` и т.п.).
  Проектное описание ресурса (и отказ от `GetLevelDesc`/`GetSurfaceLevel`) —
  отдельная работа этапа 3.
* **Попутно зафиксировано:** в E2E-логе каждого прогона (и до правки, и после)
  есть 2–3 строки `LoadTextureFromFileImpl(); Failed ... D3DERR_INVALIDCALL`.
  Это не регрессия seam'а: форматы всех DDS контента покрыты словарём, а
  `tools/dds_inventory` показывает битый файл `Data/Heroes/Gunslinger/S2/_.dds`
  (объявлено 11 mipmap, данных на один уровень); счётчик 2↔3 — шум прогона.

**Седьмой шаг (2026-10-14) — шейдеры, Clear в тенях, viewport из immediate-слоя.**
Счётчик живых обращений к устройству/Direct3D в `Render/` (кроме бэкенда и
менеджера состояний): **48 → 40**, из них к устройству — **32 → 24**.

* `shadercompiler.cpp`: `CreatePixelShader`/`CreateVertexShader` через seam
  (раньше — напрямую устройство). В seam передаётся **размер** DXBC-блоба
  (`info.code->GetBufferSize()`): D3D9 его игнорирует (читает из заголовка
  блоба), а Vulkan/DX12-бэкенду он нужен — иначе слот под SPIR-V (§1.3)
  пришлось бы переделывать.
* `ShadowMap.cpp`: три `Clear` через seam (`bcTarget|bcZBuffer`, `bcZBuffer`).
* `ImmediateRenderer.cpp`: `GetViewport` через seam, `D3DVIEWPORT9` →
  `BackendViewport` (это не косметика: на размерах вьюпорта держится математика
  экранного квада — `1/vp.width`, перевод координат в NDC).
* `ShadowManager.cpp`: `EvictManagedResources` через seam.

Что осталось внутри `Render/` — и почему это не «доделать механически»:
* слой создания/перечисления устройства: `pD3D->` ×16 (`GetAdapterIdentifier`,
  `GetAdapterCount/Monitor/ModeCount`, `EnumAdapterModes`,
  `CheckDeviceFormat`) + `CreateDevice`/`Reset`/`Release`/`TestCooperativeLevel`
  — этап 3/4 (режимы перечисления заменит SDL, §3.x GAP);
* caps/свойства: `GetDeviceCaps`, `GetProperties` ×4 (`configdatabase`),
  `GetDirect3D`, `GetCreationParameters`, `GetDisplayMode` — синтез caps-структуры
  (§2.5);
* `CreateQuery(D3DQUERYTYPE_EVENT)` ×2 — вертикальная синхронизация, у NVRHI это
  `EventQuery`, в seam ещё не положено;
* фиксированный блок `RenderDeferred2D` (`SetFVF`, `DrawPrimitiveUP`,
  `SetVertexShader`) — план §1.4; `CreateVertexDeclaration` в `smartrenderer.cpp`
  — часть инстансинг-хака (достроить декларацию элементом на потоке 1 под
  `SetStreamSourceFreq`): переносится только вместе со **схемой инстансинга**
  (§2.10), а не как отдельный вызов — **закрыто 2026-10-14** (шаг 1 §2.10:
  построение инстансной декларации — в `D3D9Backend::CreateInstancedInputLayout`);
* мёртвый код: `GetViewport` ×3 и `GetScissorRect` ×1 под `#ifdef
  _DEBUG_VIEWPORT` (не компилируются).

Замер гейта: `gross(>64)` 1,76% (выше обычного шума 0,1–1,0%) — проверено кропом:
разница сосредоточена в верхнем левом углу и у портала, это сдвиг камеры между
прогонами (на старом кадре строение левее, кольцо арены уже); центр кадра — земля и
тени — 0,0% грубых отличий, UI-зоны чистые. То есть «gross выше шума» само по себе
не регрессия: надо смотреть, где именно различия (тени/земля vs анимация/камера).

**Восьмой шаг (2026-10-14) — блокировка буферов уходит в seam.** Состояние:
**закрыто 2026-10-15, `d8e3f45eb`.** Гейты: сборка 120/120 (0 ошибок),
`check_exe` ИТОГ: OK (15 199 232 B), E2E `2026.10.15-00.28.42` —
`Switch to EGameState::Finished by 3`, дампов нет, asserts 0, `FAILED` 0,
`LoadTextureFromFileImpl` 2 (известный битый DDS), на сервере
`Game result delivered … attempts=0`, `finish_journal.log` 0 байт, в логе
dev-бэкенда `finish <token> no db session` (норма для mode 99).
Визуальный гейт (`setvar auto_screenshot 3000`, `C:\pw64\Game\Bin\pw_auto_*`):
`bb_3000` vs `bb_3100` = 4,65% отличий / 1,06% грубых — кадр не залипший;
`bb_3000` против эталона шага инстансинга (`/tmp/autoshots_inst`) = 4,74% /
**0,27% грубых** — в пределах зафиксированного шума между прогонами
(0,13–0,33%); правый край UI 0,07%, нижняя полоса 0,45%; кадр глазами —
база, герой, UI, миникарта на месте.

* `locktype.h`: `ERenderLockType` становится словарём режимов блокировки;
  добавлен `LOCK_NOSYSLOCK` — D3D9-подсказка, которую запрашивали конкретные
  вызовы (`RoadPrimitive`); молча терять её нельзя, остальные бэкенды игнорируют.
* `IRenderBackend`: `LockVertexBuffer`/`UnlockVertexBuffer`/`LockIndexBuffer`/
  `UnlockIndexBuffer`. Раздельно по типу намеренно: в D3D9
  `IDirect3DVertexBuffer9` и `IDirect3DIndexBuffer9` — разные COM-интерфейсы, у
  `Lock`/`Unlock` разные слоты vtable, поэтому один метод с `HBuffer` и кастом
  внутри был бы UB.
* карта режимов одна — `ConvertRenderLockType()` (`renderflagsconverter.cpp`),
  она уже использовалась для поверхностей; отдельную карту в `D3D9Backend`
  заводить не стали (фактически завели бы второй источник истины).
* `size == 0` сохранено как «весь буфер» (семантика D3D9; ср.
  NULL-прямоугольник у `StretchRect`, «Пятый шаг»).
* кольцо `SharedD3DBufferST` (`dxutils.{h,cpp}`) остаётся **клиентской
  политикой**: курсор записи, `timeStamp`, рост через `QuerySize`/`Resize`,
  `SpaceHolder`, `UnlockAll`/`ResetAll`. В seam уехал только механизм: режимы
  `LOCK_NOOVERWRITE` (append) и `LOCK_DISCARD` (flash) приходят параметром, а не
  константами D3D9 (внутренний enum `LOCK_FLAGS` удалён). Для Vulkan/DX12
  кольцом записи становится тот же курсор + барьеры по фенсам — политику
  переписывать не придётся.
* переведены все живые точки: `dxutils` (`LockVB`/`LockIB`/`FillVB`/`FillIB`),
  `Render` (debugrenderer, smartrenderer, ParticleFX, ShadowMap, FlashRenderer,
  ImmediateRenderer, StaticMesh, renderresourcemanager, InstancedMeshResource,
  VertexColorStream), `PF_GameLogic/Minimap`, `Scene` (RoadGeometryBuilder,
  RoadSceneObject, TraceFXGeo, VertexColorManager), `Terrain` (NatureMapVisual,
  RoadPrimitive, TerrainGeometryManager). Аудит `\->Lock(` / `\->Unlock()` по
  всему `Src/`: **0** обращений к буферам устройства вне бэкенда; остались
  блокировки поверхностей (`RenderSurface::Lock` — это шаг текстур/поверхностей)
  и не-буферные `Lock` (mutex, умные указатели).

Находка при аудите: `Render/mesh.cpp` и `Scene/ModelLoader.cpp` лежат в дереве,
но **не собираются** (нет в `Render.vcproj`/component-списках и в CMake-графе;
проверка — список `.obj` в
`Tools/ClientCMake/build/x64-wine/CMakeFiles/<Target>.dir/`). Они ссылаются на
тип `IVertexBuffer`, который в дереве не объявлен. При аудитах обращений к
устройству их надо исключать, иначе счётчик и правки уходят в мёртвый код
(в частности, прежний счётчик «40 обращений» включал `mesh.cpp`).

**Удалены (зачистка, 2026-10-15):** `Src/Render/mesh.{cpp,h}` и
`Src/Scene/ModelLoader.{cpp,h}` — четырёх доказательств мёртвости достаточно
(`model.json`, `Src/CMakeLists.txt:167` с `list_remove_regexp( ALL_SRCS
".+ModelLoader\\.cpp$" )`, отсутствие включателей, а `mesh.h:3` включает
несуществующий `submesh.h`, т.е. mesh.cpp уже не компилировался).

Чего в этом шаге **нет**: самого staging-кольца. `D3D9Backend` по-прежнему
прокидывает `Lock`/`Unlock` устройству — кольцо появится как реализация этих
четырёх методов в `NvrhiBackend` (курсор + фенсы + пересоздание буфера на
`Resize`), и проверяется только вместе с Vulkan-бэкендом (этап 3).

### 2.9 Тень константных регистров (`conststore`) — 2026-10-13

`Src/Render/conststore.{h,cpp}`. D3D9 применяет каждую запись сразу
(`SetVertexShaderConstantF` по индексу регистра), Vulkan/D3D11 — один
uniform-буфер на стадию, который надо собрать из записей. Мест записи констант
в клиенте больше сотни (`FlashRenderer` 19, `PFRenderUtils` 14,
`PFRenderInterface` 13, `ShadowMap` 8, `FullScreenFX` 7, `Minimap` 7,
`Terrain` 6, `WaterMesh`/`WaterManager`/`AOERenderer` 6, …), но все они идут
через две функции — `Renderer::Set{Vertex,Pixel}ShaderConstants` (проверено:
прямых вызовов `Set*ShaderConstantF` в дереве больше нет, все convenience
обёртки — `...Vector4`, `...Matrix`, `...Matrix43`, `...Float`, `...HDRColor` —
зовут их). Поэтому полное состояние регистров держится в тени: вызов идёт
«сквозь» в устройство как раньше (порядок и поведение не меняются), тень
обновляется рядом, `NvrhiBackend` на биндинге берёт из неё один блок.
Переписывать 100+ мест не придётся.

Второй смысл — измеримая раскладка вместо догадки по `GlobalMasks.h`: какие
диапазоны `c0..c255` реально пишутся за бой и сколько регистров живые
(`FirstUsed`/`LastUsed`/`ChangedSinceTurn`/`Writes`). Это вход для выбора
размера и числа uniform-буферов при бюджете `maxBoundDescriptorSets = 8`
(§2.6). Замер включается в тот же периодический `DebugTrace`-снимок, что и
счётчики пар/`sortId` (`PW_RENDER_SHADER_STATS`).

Замер получен (2026-10-14, §1.2.1): **VS c0..c234 (235 регистра), PS c0..c220
(221)** — один `cbuffer` на стад покрывает всё используемое пространство, дробить
его из-за диапазона не нужно.

Переснято 2026-10-15 с раздельным учётом «тронутых» и «изменившихся по значению»
регистров (инструментальная сборка `-DPW_RENDER_SHADER_STATS`, E2E-бой на tiny10,
лог `2026.10.15-01.51.43`): блок VS 3760 Б, PS 3536 Б; за окно в 300 кадров
VS — ~3,4·10^5 вызовов записи, ~8,3·10^5 тронутых регистров и **~5,1·10^5
регистров с реально изменившимся значением** (62% тронутых), PS — ~4,1·10^5
тронутых и ~1,2·10^5 изменившихся (31%). Итоговый счётчик изменений за бой:
VS `rev=9,59·10^6`, PS `rev=2,14·10^6`.

**Ранее стоявшая в этом разделе цифра «изменившихся значений за окно ~270 (VS)
и ~35 (PS)» не подтверждается** — по новому замеру их на два порядка больше
(VS ≈1,7 тыс. на кадр). Прежняя цифра относилась к другому срезу (происхождение
не восстановлено) и снята как основание для дизайна.

Следствие для Vulkan-пути (оно противоположно прежнему выводу): ключение
перекачки по «менялся ли блок с прошлого рисования» в бою почти не экономит —
VS-блок меняется каждый кадр. Экономия — в размере пересылаемого диапазона:
между двумя соседними рисованиями меняются единицы регистров, поэтому
`TakeDirty` (диапазон + маска изменённых) и кольцевой буфер со слотом на
рисование — основной механизм. Верхняя граница наивной схемы — полный блок
3760 Б на рисование; при ~500 рисованиях/кадр это ~1,9 МБ/кадр, и относиться к
этому надо как к цене, а не как норме. Число рисований на кадр не измерено —
замер нужен при написании бэкенда (он же определит размер кольца).

Проверка тени об устройство (`verify_const_block 1`, та же инструментальная
сборка): 18 снимков × 2 стадии, **mismatch=0** (VS 235 регистров, PS 221,
`hr=0x0`) — обходных путей записи констант в дереве нет, тень годится как
единственный источник констант для `NvrhiBackend`.

Раскладка из `GlobalMasks.h` — инструмент `tools/const_layout/const_layout.py`
(2026-10-15): VS — 28 названных диапазонов, PS — 43, но **явными размерами
покрыто только 8/256 регистров**, а остальные размеры выводятся из расстояния до
следующего имени (в отчёте помечены `~`). Плюс пересечения имён:
`WORLD` × `INSTANCING_*` (c0..c3), `VSHADER_LOCALCONST0` × `INSTANCE_DATA` (c20),
`VSHADER_LOCALCONST4` × `BONES` (c24); на PS — `SCREEN_TO_TEXTURE` /
`TERRAIN_AMBIENT` / `WATER_PARAMS0` / `POINTLIGHTSDATA` все в c10,
`PSHADER_SHCONSTS` / `TERRAIN_DIFFUSE2` / `WATER_REFLECTION_MTX` в c12..c15,
`SAMPLER{0..4}_MUL/ADD` × `PSHADER_LOCALCONST{0..9}` в c20..c27. То есть один и
тот же регистр значит разное для разных семейств материалов, и **генерировать
описание раскладки по `GlobalMasks.h` нельзя**: источник — reflection из DXC
(SPIR-V/DXBC), а заголовк даёт только имена для человекочитаемого отчёта и
пересечения, которые надо разрешить до написания бэкенда.

Реализация — свободная функция `Render::GetConstStore()`, а не член `Renderer`:
`renderer.h` включается почти везде, и любое его касание стоит полной
пересборки графа (см. §2.11).

### 2.14 D3DX-математика: замер показал, что механическая замена невозможна (2026-10-13)

11 из 16 оставшихся импортов `d3dx9_43.dll` — чистая математика
(`MatrixMultiply/Inverse/LookAtLH/OrthoLH/OrthoOffCenterLH/PerspectiveLH/
Translation/Transpose`, `Vec3Normalize/Vec3TransformCoord/
Vec3TransformCoordArray`, `Vec2TransformNormalArray`), ~55 мест вызова.
Оракул `tools/sh_math_test` расширен разделом математики (1000 случайных входов
на функцию + разделы с фиксированными входами для снятия формул).

Результат (полная таблица — README оракула):

* **побитово** совпадают только `MatrixTranspose` и `MatrixTranslation`;
* остальные — расхождение 1-2 ULP в 2-50% компонентов: DLL собрана с SSE и
  другой ассоциацией операций, повторить это скалярным кодом не вышло
  (пробовались прямая и попарная ассоциация для умножения, `x*inv(sqrt)` и
  `x/sqrt` для нормализации);
* **формулы DLL не совпадают с «документированными»**: `OrthoLH` — `2/w`, `2/h`;
  `PerspectiveLH` — `_33 = zf/(zf-zn)`, `_43 = -zf*zn/(zf-zn)`;
  `OrthoOffCenterLH` — порядок параметров `(l, r, b, t)`;
  `Vec3TransformCoord` — **с делением на `w`** (без него расхождение ~1e4).
  Кандидат, написанный «по памяти», даёт правдоподобную, но неверную матрицу.

Решение: математику НЕ менять «потому что это просто». Варианты:
(a) не трогать до этапа 4 и жить с `d3dx9_43.dll` (x64-дистрибутив уже решает
проблему отсутствия DLL у игроков);
(b) принять однократное изменение чисел — своя реализация детерминирована на
любом CPU (`/fp:strict`, SSE2 без FMA), но отличается от DLL, значит все клиенты
должны обновиться атомарно одной версией, а визуальный гейт обязателен;
(c) повторить последовательность операций DLL по дизассемблеру — дорого.

Матрицы view/proj/shadow и picking в клиенте — клиентские, в CRC мира не входят
(состояние игры считается по игровой логике), так что (b) не ломает консенсус —
но ломает сравнимость картинки между старой и новой версиями клиента.

### 2.15 Решения, подтверждённые пользователем (2026-10-13, пополнено 2026-10-15)

1. **Ветка `nvrhi` запушена в `origin`** (`prime-world-classic`, не прод-ветка).
   До этого 44 коммита существовали в одной копии на домашней машине.
2. **Контент `.shd` (расхождение репо ↔ прод) — не трогать.** Ни откатывать
   перегенерированные контейнеры в репо, ни выкладывать `zpatch_shaders.pile`
   в прод. Следствие надо помнить: dev-сборки отличаются от прода по байтам
   шейдеров (визуально — нет), поэтому «сверить как у игроков» по контенту
   нельзя; когда перенос всё же понадобится — точечно по файлам (§инвариант
   про pile).
3. **D3DX-математика остаётся как есть до этапа 4** (вариант (a) из §2.14).
   `d3dx9_43.dll` возим сами (`pwc-gitupdates/Bin/d3dx9_43.dll`, 2.4 МБ),
   системный редист не нужен, риска для игроков нет. Тема закрыта; возвращается
   только если появится требование не возить D3D9-хелпер в Vulkan-клиенте.
4. **Реальный GPU доступен с 2026-10-15** (хост, Linux: RTX 3060 12 ГБ, драйвер
   595.99.02; как поднят и что именно снимает — §4 п.5). Этап 3 проверяется
   корректно и на железе (спайк B0 проходит целиком, `b0cull` подтверждён),
   метрики производительности снимаются там же. В tiny10 GPU по-прежнему нет
   (`Microsoft Basic Display Adapter`, D3D11 через WARP, Vulkan — Mesa lavapipe)
   → E2E-стенд остаётся software-растером. DX12-путь проверять негде
   (Windows+NVIDIA нет, tiny10 = WARP).
5. **Эмблемы гильдий** — не трогать, переделываются заново (`PENDING.md` §9).
6. **`Bin/D3DCompiler_43.dll` в прод-обновлениях** не импортируется
   `PW_Game.exe` (импорты exe — только `d3d9.dll` и `d3dx9_43.dll`), т.е. либо
   грузится другим модулем, либо мёртвый груз в доставке. Запомнено, по решению
   пользователя игнорируется (`PENDING.md` §10).
7. **Swapchain / окно — через SDL, версия — SDL3 (3.2.x)** (решение от
   2026-10-14). SDL3, а не SDL2: surface для Vulkan (`SDL_Vulkan_CreateSurface`,
   `SDL_Vulkan_GetInstanceExtensions`), HWND для будущего DX12
   (`SDL_GetWindowProperties` → `SDL_PROP_WINDOW_WIN32_HWND_POINTER`),
   корректные события ресайза/минимизации/окклюзии/поворота — на них решается
   `VK_ERROR_OUT_OF_DATE_KHR`, и полноэкранные режимы
   (`SDL_GetFullscreenDisplayModes`/`SDL_SetWindowFullscreenMode`) вместо
   `EnumDisplaySettings`-валидации. Статика, MIT, C ABI; `SDL_main` не
   использовать (`SDL_SetMainReady()` / `SDL_MAIN_HANDLED`) — `WinMain` остаётся
   наш. XP не поддерживается (для x64-клиента VS2022 не ограничение).
   Запасной вариант — SDL2 2.32.x (`SDL_Vulkan_CreateSurface` есть с 2.0.7),
   если упрёмся в Win7 или в поведение SDL3-окон; ресайз/fullscreen там хуже.
   Подробности и границы — §3.x GAP: swapchain.
8. **Порядок очереди: `ConstBlock` раньше кэша пайплайнов** (2026-10-15, шаги 7↔8
   поменяны местами). Обоснование: на D3D9 кэшировать пайплайн нечего — объекта
   пайплайна нет, шаг «кэш по ключу» сегодня даёт только тип ключа и счётчик
   уникальных ключей. `ConstBlock` же проверяем на живом D3D9-пути (тень в
   `conststore` уже есть, вызовы идут сквозь устройство) и именно он определяет
   макет Vulkan-дескрипторов, который надо зафиксировать до написания бэкенда.
9. **Форс флагов depth-текстур на E2E-стенде НЕВОЗМОЖЕН — решение пересмотрено
   замером (2026-10-15, было разрешено ранее в этот же день).** Замер
   `tools/dsm_probe` на самом стенде (Microsoft Basic Render Driver, HAL):
   `CheckDeviceFormat(DEPTHSTENCIL, D3DRTYPE_TEXTURE)` = `no` для D16/D24X8/D24S8
   (поверхности — OK), а реальная попытка `CreateTexture(512×512, USAGE_DEPTHSTENCIL)`
   даёт `0x8876086C` (D3DERR_INVALIDCALL) для всех трёх форматов. Значит форс
   битов `hasD16T/hasD24X/hasD24S` превратил бы «путь карт теней не исполняется»
   в `NI_DX_THROW` на создании ресурса — проверяемого покрытия это не даёт.
   `D3DDEVTYPE_REF` формально поддерживает D24S8/D32 как текстуры, но
   `CreateDevice(REF)` = `0x8876086A` (NOTAVAILABLE): `C:\Windows\System32\d3dref9.dll`
   на tiny10 вырезан. Итог: покрытие DSM-пути переносится на этап 3 — там он
   исполняется на Vulkan-бэкенде (host: RTX 3060 / lavapipe, depth-текстуры
   есть). Механизм dev-var'а (`force_dsm` + ключ `-forceDsm`,
   `ConfigManager.cpp`) сделан (2026-10-15, `3b01b92c4`) — он нужен для
   машин с реальным D3D9-железом и для прогонов поверх `NvrhiBackend`, — но в
   E2E на tiny10 не включается и новый визуальный эталон не создаётся.
10. **`.pile`/`.shd` хранятся и обновляются в основном репо; в
    `pwc-gitupdates-test` и `pwc-gitupdates` в рамках этого плана не лить**
    (2026-10-15). Уточняет п.2 от 2026-10-13: перегенерированные контейнеры
    коммитятся в `Data/Shaders/UberShaders/` (репо `prime-world-classic`), прод
    не трогается. Перенос шейдерного контента в контентный контур — отдельная
    задача с выпуском готовой ветки на тест, с этим планом не пересекается.

### 2.13 Скриншот: последние два D3DX-импорта в клиенте (2026-10-13)

`Renderer::SaveScreenshotToFile` (`Src/Render/renderer.cpp`) — единственное место,
где остались `D3DXLoadSurfaceFromSurface` и `D3DXSaveSurfaceToFileA` (2 из 16
импортов `d3dx9_43.dll`). Путь: `GetBackBuffer` → `CreateOffscreenPlainSurface`
(SYSTEMMEM) → `D3DXLoadSurfaceFromSurface` (кроп + перенос) →
`D3DXSaveSurfaceToFile(D3DXIFF_PNG)`.

Что нужно, чтобы убрать: `GetBackBuffer`/`CreateOffscreenPlainSurface` в
интерфейс бэкенда (поверхности уже там), кроп — `StretchRect` или копирование по
строкам, и **свой PNG-энкодер** (zlib уже подключён: IHDR + IDAT из
отфильтрованных строк + IEND — около 60 строк). Проверять — офлайн-тестом
(`tools/`-паттерн, как `sh_math_test`): закодировать известный кадр и сверить с
эталонным PNG, потому что в E2E скриншот никто не вызывает.

**Сделано (2026-10-14).**

* В `IRenderBackend` добавлены `ReadSurface(HSurface, pCropRect, pDst, rowPitch,
  pOutLayout)` и `GetBackBuffer(HSurface*)`. D3D9-реализация сама решает про
  staging: поверхности пула DEFAULT (RT, бэк-буфер) CPU недоступны →
  `GetRenderTargetData` в SYSTEMMEM, затем `LockRect` + копирование по строкам.
  Формат пикселя — явный `Render::EBackendPixelLayout` (`renderpixellayout.h`):
  A8R8G8B8/X8R8G8B8 у D3D9 лежит в памяти как B,G,R,A, у Vulkan-бэкенда
  естественный ответ — R,G,B,A; перестановку делает пишущий код, иначе на этапе
  3 каналы поменяются молча. Другой формат у источника — отказ (`D3DERR_INVALIDCALL`),
  а не тихая подмена.
* `Renderer::SaveFrameToFile(fileName, keepAlpha, pCropRect, useBackBuffer)` —
  общий путь. `SaveScreenshotToFile` (её зовёт `SocialConnection`) — обёртка над
  ней (бэк-буфер + кроп, альфа отбрасывалась и раньше через X8R8G8B8);
  `SmartRenderer::DumpScreenshot` — тот же путь с поверхностью сцены.
  `DumpScreenshotToMemory` **удалена**: читал её только `EditorNative` (вне
  CMake-графа), а сама она была обёрткой над `D3DXSaveSurfaceToFileInMemory`;
  вместо неё — `BuildDdsImage` (заголовок те же 128 байт, смещение 128 у
  вызывающего сохраняется).
* Писатель — `Render/screenshot.{h,cpp}`: png (IHDR + один IDAT + IEND, фильтр
  None, `compress2`, CRC32 из zlib), bmp (32 bpp BI_RGB, строки снизу вверх —
  отрицательная высота в DIB запрещена), dds (магия `DDS ` + `DDSURFACEDESC2`).
  jpg/hdr умел только D3DX: дефолт `screenshot_ext` переведён `jpg` → `png`,
  `jpg`/`hdr` в запросе сворачиваются в png (имя файла не переписывается).
  Из `PFRenderUtils.cpp` убран Wine-гейт (`!Compatibility::IsRunnedUnderWine()` —
  обход битого `D3DXSaveSurfaceToFile` в Wine <1.5.6): теперь снимок работает и
  под Wine.
* Модуль намеренно **без** `stdafx.h` (только C-stdlib, `std::vector`, zlib) —
  компилируется нативно, вне графа клиента; в графе TU получает stdafx через
  pch-обёртку (`Tools/ClientCMake/pch_rules.py`), поэтому её отсутствие в TU
  сборки не ломает.
* Смоук `tools/screenshot_test/` (g++ + zlib; дальше `check.py`: свой декодер +
  **независимая сверка через PIL**) поймал три бага, которые по логам E2E не
  проявились бы вообще:
  1. CRC PNG-чанка считался **от поля длины** (`typePos` брался до вставки
     длины) — декодер браковал каждый чанк;
  2. DDS-заголовок писался **без магии `DDS `**, со всеми полями, сдвинутыми на 4
     (`dwSize` в байт 0 вместо 4, маски в 88..100 вместо 92..104), и без
     `DDSD_WIDTH` в `dwFlags`;
  3. у **собственного декодера** теста смещения BMP были неверны
     (`biPlanes`/`biBitCount`/`biCompression` читались с 24/26 вместо 26/30) —
     писатель и декодер ошибались по-разному; без независимого декодера такую
     пару легко «починить» под общую ошибку. Отсюда правило: у офлайн-смоука
     должен быть второй декодер (PIL/`file`), а не только свой.
  Итог: png/bmp/dds сверяются пиксель в пиксель (кадр 37×13 — нечётные размеры
  ловят ошибку шага строки; узоры таковы, что переворот строк и подмена каналов
  дают разные расхождения) и своим декодером, и PIL.
* Гейт: x64-сборка под Wine (`check_exe.py` ИТОГ: OK, 15 191 552 Б) + **E2E на
  tiny10 ПРОЙДЕН** (финальный артефакт, лог `2026.10.13-22.42.41`): login
  `Success(5) uid=131` → lobby → `gamesvc_00` → `mapload: all map objects
  loaded` → бой → `Switch to EGameState::Finished by 3` → `finish 7e447f97…` в
  dev-бэкенде; дампов нет, `assert`/`shader`-ошибок нет. Предыдущий прогон того
  же пути (до удаления мёртвого `GetFrameSize` и до ограничения кропа —
  `2026.10.13-22.16.50`, `finish d21ab423…`) — тоже зелёный. Визуальный гейт
  (`tools/render_shot`): кадр боя отрисован целиком (ландшафт, юнит с HP-полосой,
  партиклы, UI, миникарта), два кадра через 26 с отличаются на 9,6% (второй
  прогон — 5,3%) — рендер не залип.
* **Readback проверен на устройстве (2026-10-14, хук `auto_screenshot`).** Точки
  запуска снимка в shipping по-прежнему нет: команда есть
  (`REGISTER_CMD( screenshot, PF_Render::CommandScreenshot )`,
  `PF_GameLogic/PFRenderUtils.cpp`; опции `asis` и имя, файл в
  `NProfile::FOLDER_SCREENSHOTS`, cvar `screenshot_ext`), но вводить её нечем —
  `command_line` (`System/Commands.cpp`) никем не вызывается,
  `PreferencesProcessor::Apply` гоняет только `applyCommandName` из
  БД-предпочтений, игровой консоли на стенде нет.
  Поэтому в `SmartRenderer::OnFrameStart()` добавлен хук по переменной
  `auto_screenshot` (`REGISTER_VAR`, `STORAGE_NONE`, по умолчанию 0 = выключено):
  на кадре N и на N+100 снимаются три кадра — `pw_auto_scene_*` (поверхность
  кадра, с альфой — путь `DumpScreenshot`), `pw_auto_bb_*` (бэк-буфер целиком —
  путь `SocialConnection`) и `pw_auto_crop_*` (кроп, заведомо выходящий за кадр).
  Файлы — в cwd процесса (на стенде `C:\pw64\Game\Bin`). Включается НЕ сборкой, а
  `autoexec.cfg`, который клиент исполняет сам: `NGlobal::ExecuteConfig(
  "autoexec.cfg", NProfile::FOLDER_GLOBAL )` (`PW_Client/Game.cpp:1413`),
  `FOLDER_GLOBAL` = `<каталог exe>\..\Profiles\` (`ProfileManager.cpp:165`),
  строка `setvar auto_screenshot 3000`. Формат конфига: построчно, комментарии
  `;` и `//` (`System/ConfigFiles.cpp`, `LoadConfig`).
* Замеры прогона (`2026.10.13-23.06.47`, кадр 3000 ≈ 166 с после старта — на
  стенде ~18 к/с, бой к этому моменту идёт):
  * `bb` 1024×768 RGB, `scene` 1024×768 RGBA (149 разных значений альфы —
    альфа не обнулена), `crop` **200×200** при запросе 400×400 в `(W-200, H-200)`
    — ограничение прямоугольника сработало;
  * `crop` == `bb[W-200:H-200]` **пиксель в пиксель (0/40000)** — значит
    смещения, шаг строки (`Pitch` ≠ `w*4` не бывает, но проверяется) и порядок
    строк в `ReadSurface` верны;
  * `scene` == `bb` пиксель в пиксель (0/786432): обе поверхности пула DEFAULT,
    обе идут через `GetRenderTargetData` → одинаковый результат;
  * `bb_3000` против `bb_3100` — 11,6% отличающихся пикселей: читается живой
    кадр, а не залипший буфер;
  * на клиентском кадре **нет** системной надписи «Activate Windows», которая
    есть на VNC-снимке того же стенда → читается бэк-буфер, а не экран.
  Гейт этого прогона: E2E зелёный (`finish 4cd0a613…` в dev-бэкенде), дампов нет.

### 2.12 Замер раскладки констант (2026-10-13, `conststore`)

Снимок из боя на tiny10 (shipping + `PW_RENDER_SHADER_STATS`, лог
`2026.10.13-19.06.25`, последние кадры):

```
ConstStore: VS c0..c234 (235 reg, 6342864 writes, 835944 changed)
         | PS c0..c220 (221 reg, 6479460 writes, 412048 changed)
```

Что это значит в пересчёте на кадр (снимок раз в 300 кадров):

| стадия | затронутых регистров за сессию | вызовов записи на кадр | регистров записи на кадр |
|---|---|---|---|
| VS | c0..c234 (235 из 256) | ~1140 | ~2790 |
| PS | c0..c220 (221 из 256) | ~1220 | ~1370 |

Выводы для этапа 3 (это вход в раскладку uniform-буферов, §2.6/§2.7):

1. **Узкого «материального» блока на 20–30 регистров не будет** — за сессию
   трогается почти всё пространство `c0..c255`. Значит раскладка обязана быть
   **иерархической** (frame / material / object), иначе на каждый биндинг
   состояния придётся лить ~4 КБ на стадию.
2. При ~117 состояниях и ~146 парах шейдеров в кадре наивная полная
   перезапись блока дала бы порядка **1 МБ загрузок констант на кадр**. Отсюда
   два требования: загружать **подблок по диапазону** (`FirstUsed`/`LastUsed`
   для этого и сняты) и **дедуплицировать записи** — 1140 вызовов записи на
   кадр при 117 состояниях означают, что одно и то же значение пишется много
   раз подряд.
3. `ChangedSinceTurn` — готовый критерий «перезаливать ли буфер»: счётчик
   сбрасывается на снимке, на этапе 3 его достаточно привязать к кадру.

Побочное наблюдение: `LoadTextureFromFileImpl`-ошибок в этом прогоне снова 2
(в предыдущем было 3) — разброс есть, имена файлов в лог не печатаются;
отдельная диагностика.

### 2.10 Инстансинг: что решать до этапа 3 (2026-10-13)

Схема D3D9 (`SmartRenderer::EnableHardwareInstancing`, `ApplyStates`,
`batch.cpp`): геометрийным потокам ставится
`D3DSTREAMSOURCE_INDEXEDDATA | numInstances`, отдельному instance-потоку —
`D3DSTREAMSOURCE_INSTANCEDATA | 1`; в вершинную декларацию добавляется элемент
`SHORT2 BLENDINDICES` из потока 1 (`s_instancedVDecl`), сам поток —
`s_pInstanceIndexVB` (stride 4, `pBuff[i] = i`). То есть один draw проигрывает
геометрию `numInstances` раз, а instance-поток даёт индекс чанка геометрии.
`SetStreamSourceFreq` — D3D9-специфичный механизм, аналога нет ни в NVRHI, ни
в Vulkan, поэтому он и остался единственным прямым вызовом устройства в
`ApplyStates`.

Хорошая новость: HLSL уже читает индекс инстанса явно —
`int InstanceId : BLENDINDICES0;` (`BasicMaterial.hlsl:61`), плюс
`INSTANCE_DATA c20` и `MAX_INSTANCES_COUNT 19` в `GlobalMasks.h`. Значит
переносимый путь существует: `drawIndexedInstanced` + индекс инстанса из
системной семантики (`SV_InstanceID` / `gl_InstanceIndex`), а генератор шейдеров
должна уметь отображать `BLENDINDICES0` instance-потока на эту семантику.

Что это меняет (и почему решение нужно до того, как ручки станут настоящими):
1. `IRenderBackend` должен уметь рисовать N экземпляров — **сделано, но не в
   форме `DrawIndexedInstanced`** (почему — см. «Решение» ниже);
2. трюк с индексированием геометрийного потока (`INDEXEDDATA`) не переносится:
   смещение чанка на инстанс обязано считаться в шейдере из индекса инстанса,
   а это значит, что доступ к геометрии должен быть **по индексу** (index
   buffer), а не «сырым» потоком — влияет на `BatchChunk`/`StaticMesh` и на
   генератор;
3. `s_instancedVDecl` (подмена декларации на лету) тогда не нужна вовсе.

Вариант «развернуть инстансы в N отдельных draw» отклоняем: именно он
съедает выигрыш батчинга, ради которого всё и делалось.

**Замер (2026-10-14): геометрия в батч-пути уже индексная.** `Primitive::Bind()`
биндит `pIndexBuffer` + декларацию + поток 0, рисуется через `DrawIndexed`
(`DipDescriptor`: `startIndex/primitiveCount/baseVertexIndex`). Значит
D3D9-трюк `INDEXEDDATA|N` = «повторить тот же index buffer N раз» = **1:1**
ложится на `drawIndexedInstanced` с тем же IB: пункт 2 выше про
`BatchChunk`/`StaticMesh` **уже выполнен, формат данных менять не нужно.**

**Решение (2026-10-14) — два шага, не пересекающихся по времени.**
- **Шаг 1 — сделано (2026-10-14).** Форма seam'а оказалась **другой**, чем
  планировалось, и вот почему (это надо помнить на шаге 2):
  * `EnableHardwareInstancing(1)` — рабочий режим, а не «инстансинг выключен»:
    `batch.cpp:468` так рисит одиночный примитив, записанный в инстанс-батч
    (шейдер идёт веткой `NewInstancing`, `InstanceId` приходит из потока).
    Значит инстансинг меняет **раскладку** (декларация получает атрибут
    экземпляра), а не только число экземпляров в draw-вызове;
  * биндинги происходят **между** включением инстансинга и рисованием
    (`EnableHardwareInstancing` → `BindInstanceVB` → `Draw` → внутри биндятся
    VB/декларация), поэтому бэкенд обязан знать режим на моменте бинда.
    Параметр `instanceCount` в draw стал бы вторым источником истины.

  Реализация: в seam — `SetInstanceCount(unsigned)` (0 — выключено) и
  `CreateInstancedInputLayout(base, pOut)` («вариант раскладки с атрибутом
  номера экземпляра»: D3D9 сдвигает потоки и добавляет SHORT2/BLENDINDICES в
  поток 1, Vulkan вернёт раскладку без изменений, там индекс — встроенный
  `SV_InstanceID`). Внутри `D3D9Backend` теперь живут массив частот потоков и
  ленивая их переустановка перед каждым `Draw*` (бывший `ApplyStates`),
  построение инстансной декларации и проверка диапазона счётчика. Из
  `SmartRenderer` удалены `ApplyStates`, `lastStreamSourceFreqs`,
  `StateValueType`, мёртвая ветка `_SUPRESS_HWI` (нигде не определялась) и
  неиспользуемая `InstancingEnabled()`; `DeclMap` теперь только кэширует
  вариант, построенный бэкендом. Из `Render` исчезли последние два вызова
  устройства этого семейства — `SetStreamSourceFreq` и `CreateVertexDeclaration`
  (всего живых вызовов устройства вне бэкенда/менеджера состояний — 41, из них
  16 — `pD3D->` перечисления адаптеров).
  *Третья seam-ручка, появившаяся по ходу:* `InvalidateBoundState()` — вызывается
  из `Renderer::ClearDeviceResources()`. Без неё кэш частот переживает `Reset()`
  как «уже выставлено», а устройство после сброса возвращает частоты к 1 →
  инстансинг молча ломался бы после смены разрешения.
  **Что НЕ сделано из плана:** сдвиг потоков в `BindVertexBuffer()`
  (`if(instancingEnabled && streamNumber) ++streamNumber`) остался в клиенте, и
  это не мелочность: `ParticleFX` биндит свои per-instance данные в **физический**
  поток 1 через `BindVertexBufferRaw` и берёт декларацию без подмены
  (`BindVertexDeclarationRaw`). То есть «raw = физический слот, не raw =
  логический» — соглашение клиента о раскладке, а не свойство устройства: если
  сдвиг перенести в бэкенд, поток данных партиклов уедет в слот 2 и инстансы
  партиклов перестанут обновляться. Убрать его можно только вместе с
  полноценной seam-лексикой «слот атрибута экземпляра» (`SetInstanceSource`
  с встроенным счётчиком вместо VB `0..N-1`) — это делается на шаге 2 вместе с
  `SV_InstanceID`.
- **Шаг 2 (вместе с шейдерным пайплайном этапа 3):** HLSL берёт индекс из
  `SV_InstanceID` вместо `int InstanceId : BLENDINDICES0`; ветка `NewInstancing`
  (`(InstancingValue == PRESENT) && (SkeletalMeshPinValue == NONE)`) для этого
  уже есть в `BasicMaterial.hlsl`/`ShadowMaterial.hlsl`. Туда же переезжает
  лексика «атрибут экземпляра» (вместо `BindInstanceVB` + сдвига потоков).

⚠️ **Ограничение, которого в первоначальной оценке не было:**
`instanceData[MAX_INSTANCES_COUNT] : register(INSTANCE_DATA /*c20*/)` — данные
инстансов лежат в **константных регистрах**, и шейдер индексирует их значением
индекса инстанса (`instanceData[In.InstanceId]`). Vulkan запрещает динамически
индексовать uniform-буфер не-униформным индексом (валидация это ловит), поэтому
на этапе 3 `instanceData[]` обязан переехать в **storage/structured buffer**
(DX12 — SRV, Vulkan — SSBO). На D3D9 это недоступно (SM3 не знает structured
buffer) → форма `instanceData` становится зависящей от бэкенда; то же касается
`PFX_MAX_INSTANCES 47` у партиклов. `MAX_INSTANCES_COUNT = 19` пока сохраняем
(батчинг не меняется), лимит поднимаем позже отдельной задачей.

### 2.11 Стоимость итерации (2026-10-13)

Две правки, которые надо держать, иначе каждый шаг этапа 2 стоит 30–40 минут:

1. **`renderbackend.h`/`d3d9backend.h` не должны попадать в `renderer.h`.**
   В `renderer.h` — только forward-объявление `class IRenderBackend;` и поле
   `IRenderBackend* pBackend`; реализацию (`D3D9Backend`) подключает
   `renderer.cpp`, а `smartrenderer.cpp` подключает `d3d9backend.h` сам (ему
   нужны `MakeBackendHandle`/`MakeBackendViewport`). Замерено: касание
   `renderbackend.h` — **3 TU за 8 с**; до развязки любое касание интерфейса
   тянуло за собой `renderer.h`, а его правка в этом же заходе давала полную
   пересборку (~30 мин, 1160 TU).
2. **`build_client_x64_wine.sh` разводит build/gen-каталоги по флагам.**
   `PW_EXTRA_DEFS`/`PW_EXTRA_OPTS` попадают в командную строку каждой TU,
   поэтому инструментальная сборка (например `-DPW_RENDER_SHADER_STATS`) в
   общем каталоге сносила весь кэш и затем требовала полной пересборки
   обычного артефакта. Теперь суффикс каталога берётся из `PW_BUILD_SUFFIX`, а
   если он не задан — из хэша этих переменных (`x64-wine_a0a46f96`), и артефакт
   пишется в `build/PW_Game<суффикс>.exe`, не затирая стендовый
   `build/PW_Game.exe`.

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
- `DXResourcesControl` — **в дереве ЕСТЬ и он компилируется** (первичная
  оценка «нет» была ошибочна, проверено 2026-10-14): `Render/DxResourcesControl.{h,cpp}`
  в `Render.vcproj`, есть в unity-графе `_ShippingSingleExe`, подключается шестью
  файлами — `Render/DxIntrusivePtr.h`, `dxutils.cpp`, `renderer.cpp`,
  `TextureManager.cpp`, `shadercompiler.cpp` и **снаружи** `Render` —
  `PF_Core/EffectsPool.cpp`. В этап 2 он не попал (это счётчик ресурсов, не
  вызовы устройства), но этап 4 обязан его убрать/переписать — в списке удаления
  его раньше не было.
- `DeviceLostHandler` + `DeviceLostWrapper` (~35 классов, `NotifyDeviceLost/
  NotifyDeviceReset`, `RecoverFromDeviceLost`) — **интерфейс сохранить,
  семантику свести к «ресайз/ре-аллокация»**: в DX12/Vulkan device-lost нет.
  Это самый большой по числу файлов, но самый дешёвый по риску кусок.
  **Сделано 2026-10-14** — см. §6 «Сделано (этап 2, ре-аллокация)».
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

**Замер (2026-10-14), меняющий объём работы.** Боевой `Bin/config.txt`
(2036 строк) **не использует ни одного ключа `D3DCAPS9`**: ноль вхождений
`Caps/Caps2/DevCaps/RasterCaps/Max*/PixelShaderVersion/…`. Из «требований» в
файле живут только `OS`, `CpuSpeed`, `Memory`, `VideoMemory`, `DirectX`,
`DiskSpace`, `DisplayVendor` + блоки по ID устройства (`0xac0c = …`). При этом
caps нужен **вне** матчера, в двух местах: `sysInfo.MaxVertexShaderConst`
(`ConfigManager` → `ParticleFX.cpp:498`, из него считается лимит партиклов) и
`HasD16T/HasD24X/HasDSM` (гейт аппаратных карт теней). Плюс `ConfigManager`
подаёт в матчер **зашитые** `1024 / 64 / 1700` (RAM / VRAM / CPU) — «требования
по железу» сейчас и так фиктивные.

**Решение (2026-10-14): `D3DCAPS9` целиком не синтезировать.**
1. матчер и словарь его ключей не трогать: мёртвые ключи остаются мёртвыми,
   правка матчера = риск по пинам без выигрыша;
2. в seam — маленький `BackendSystemInfo`: `vendorID`/`deviceID`/`description`
   адаптера, `MaxVertexShaderConst`, флаг поддержки depth/stencil-текстур,
   версии шейдерных моделей. Всё это Vulkan отдаёт напрямую
   (`VkPhysicalDeviceProperties/Features`); PCI-ID в Vulkan совпадают с
   D3D9-`VendorID`, поэтому строки вида `DisplayVendor = 0x10de` работают без
   словаря-переводчика;
3. `MaxVertexShaderConst` синтезировать **256** (наш ConstBlock и есть
   c0..c255) — иначе партиклы молча поменяют лимит;
4. зашитые `1024/64/1700` на время миграции не трогать: их «исправление» само
   по себе меняет результат матчинга = лишний источник дрейфа пинов;
5. регресс: дамп разрешённого набора пинов — сделан (см. замер ниже);
   после замера 2026-10-14 он же стал **главным аргументом** против правки
   матчера: пинов в боевом конфиге почти нет.

**Замер 2026-10-14 (дамп пинов, стенд tiny10, лог `2026.10.14-16.08.57`).**
Дамп включается ключом командной строки `-dumpConfigPins` (в обёртке E2E-задачи)
или dev-переменной `dump_config_pins`. Порядок запуска — отдельная находка:
`CheckDeviceCaps()` работает **до** `NGlobal::ExecuteConfig("autoexec.cfg")`
(autoexec исполняется в `PseudoWinMain` позже инициализации рендера), поэтому
`setvar dump_config_pins 1` из autoexec.cfg до матчера не доходит — проверено
прогоном, нужен именно ключ командной строки.

```
CheckDeviceCaps: args=5 dumpConfigPins=1
ConfigPins: adapter="Microsoft Basic Render Driver" vendor=0x1414 device=0x008c
            mem=1024 vid=64 cpu=1700 MaxVertexShaderConst=256 IsNullRef=0
            caps=0x0 pins=0
```

Три следствия:
1. **`pins=0`.** Матчер на стенде не разрешает ни одного пина: `Requirements`
   требует `VideoMemory=128`, а клиент жёстко подаёт 64 (плюс `OS=WinXP`), и
   `CConfigDatabase::Load` при невыполнении требований возвращает `false` →
   `config_params` пуст. Дифф «пины не уехали» на этом стенде вакуумный.
2. **Весь реальный surface пинов в боевом `config.txt` — одна переменная**
   `rnd_PFX_new = 0` в секции nVidia (2000 строк из 2036 — имена устройств,
   оба `ApplyToAll` пустые). Значит «не сломать пользовательские пины» — это
   одна строчка на nVidia, а не 42 поля caps.
3. **`caps=0x0` → `HasD16T/HasD24X/HasDSM` ложны: аппаратные карты теней на
   E2E-стенде ВЫКЛЮЧЕНЫ** (стенный адаптер — базовый драйвер без depth/stencil
   текстур). Это дыра в покрытии этапа 3: путь shadow maps через NVRHI стендом
   не проверяется вообще, нужны либо реальная GPU-машина, либо временный
   форс этих флагов для теста.
   **Замер исчерпывающий, форс на D3D9 бесполезен** (`tools/dsm_probe`,
   2026-10-15, запуск задачей `DsmProbe` — в SSH-сессии `adapters=0`, D3D9
   видит адаптеры только в интерактивной сессии): HAL — `CheckDeviceFormat
   (DEPTHSTENCIL, TEXTURE)` = `no` для D16/D24X8/D24S8 (`surface=OK`),
   `CreateTexture(512×512, USAGE_DEPTHSTENCIL, D24S8|D24X8|D16)` = `0x8876086C`,
   `DEPTHSTENCIL|DYNAMIC` тоже `0x8876086C`; REF — текстуры D24S8/D32
   поддерживаются, но `CreateDevice(REF)` = `0x8876086A` (`d3dref9.dll`
   на tiny10 нет). Заодно подтверждён потребитель `MaxVertexShaderConst=256`
   на HAL (тот же, что и на реальном железе → лимит `ParticleFX.cpp:498`
   стендом проверяется честно).

`MaxVertexShaderConst=256` подтверждает п.3 решения (синтез 256 совпадает с
D3D9-результатом). Регресс после синтеза caps: сверять не «список пинов»
(он пуст), а три прямых потребителя — `MaxVertexShaderConst`,
`HasD16T/HasD24X/HasDSM`, `IsNullRef` — плюс `rnd_PFX_new` на nVidia.

**Шаг 4 закрыт (2026-10-15, `3b01b92c4`).** В seam добавлены
`Render::BackendSystemInfo` (`renderbackend.h`: `vendorId`, `deviceId`,
`driverVersion`, `description`, `maxVertexShaderConst`, `isNullRef`,
`features`) и `virtual HRESULT IRenderBackend::GetSystemInfo(BackendSystemInfo&)`.
`D3D9Backend::GetSystemInfo` (`d3d9backend.cpp`) собирает их из
`GetDeviceCaps` + `GetDirect3D`/`GetAdapterIdentifier` +
`GetAdapterDisplayMode`/`CheckDeviceFormat`; `description` копируется в
собственный буфер бэкенда (`D3DADAPTER_IDENTIFIER9` живёт на стеке вызова).
`ConfigManager::CheckDeviceCaps` больше caps не читает: `sysInfo.
MaxVertexShaderConst`, `sysInfo.IsNullRef` и маска `caps` берутся из структуры;
`CheckFormats`/`SetFlag` удалены (проверка depth-формат переехала в бэкенд без
изменений, маску приносят целиком), у `CheckDeviceCaps` убран параметр
`_checkFormats` — потребителей не было. `enum Features` + `featuresVersion`
перенесены из тела `IConfigManager` в `namespace Render` (`Features.h`,
`#pragma once`): второму владельцу словаря — бэкенду — их было не взять, а
дублировать enum в двух областях нельзя.

Контракт для `NvrhiBackend` (этап 3): те же поля, синтез из
`VkPhysicalDeviceProperties/Limits/Features` (`vendorId`/`deviceId` из
`vendorID`/`deviceID` — они совпадают с D3D9, `maxVertexShaderConst` = 256 по
п.3, `features` = наличие `VK_KHR_maintenance*`/depth-текстур).

Регресс проверен по той же цепочке: строка `ConfigPins` печатается теперь из
`BackendSystemInfo` и совпадает с эталоном 2026-10-14 дословно —
`vendor=0x1414 device=0x008c driver=655360.1164117400 MaxVertexShaderConst=256
IsNullRef=0 caps=0x0 pins=0` (логи `2026.10.15-00.55.16` и `2026.10.15-01.07.24`).
Осталось D3D9-ным осознанно: `CConfigDatabase::Load(…, D3DADAPTER_IDENTIFIER9,
D3DCAPS9, …)` — 42 поля caps + 14 полей адаптера без нейтрального словаря
(п.1 решения).

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
  `vs_3_0/ps_3_0` → `vs_5_0/ps_5_0` (+ DXC→SPIR-V), убрать `--sm20`/`.shd2`
  (**сделано 2026-10-15**: `--sm20`, `isV20`, `macroV20`, `.shd2` и мёртвый
  блок чтения `.codes` из тула удалены — клиентский SM2.0-фолбэк удалён раньше,
  см. `Render/renderer.cpp:543`), и переносить на x64/вне Wine-D3DX.
  Инкрементальный кэш `.codes` **фактически выключён**: в
  `UberShaderFileInfo::Read()` ранний `return` после `Reset()`, т.е. всегда
  полная компиляция всех пинов (вот откуда 49 966 пар в `BasicMaterial.shd`).
  **Как вставить его в наш граф (снято 2026-10-12, конкретика для шага):**
  проект в `Src/PF.sln` есть (строка 237), но `projects_from_sln()` из
  `Tools/WineClientBuild/parse_vcproj.py` его не отдаёт: для
  `ShippingSingleExe|Win32` у него **нет `Build.0`** (в `EXCLUDE` он тоже не
  числится) — поэтому в `model.json` ровно 25 проектов. Конфигурации в
  `ShaderCompiler.vcproj` только `Debug|Win32` / `Release|Win32` (+ третья),
  x64 нет. Параметры `Release|Win32`: include `../..`,
  `../../vendor/zlib/include`, `../../vendor/DirectX/Include`, `../`;
  дефайны `WIN32;_WINDOWS;NDEBUG;_SHIPPING;_DO_ASSERT`; линкует
  `zdll dxguid d3d9 d3dx9 dxerr`; **`ProjectReference` в vcproj нет** —
  символы libdb (`XmlSaver.h`) в VS-сборке приходят из либы проекта libdb,
  в нашем графе это `$<TARGET_OBJECTS:libdb>` + `Foundation` + `MemoryLib`
  (+ `mballocator`). Исходники: `ShaderCompiler.cpp`, `UberShaderDesc.cpp`,
  `crc_math.cpp`, `parse.cpp`, `multythread/mtworkerlite.cpp`, `stdafx.cpp`.
  Порядок шагов: (1) в `parse_vcproj.py` — явный список «extra tools»
  (`ShaderCompiler` из `Release|Win32`), в `gen_cmake.py` —
  `add_executable(ShaderCompiler …)` с этими объектами (x64-подстановки
  `X64_LIB_*` уже есть); (2) смоук паритета — перегенерировать
  `Data/Shaders/UberShaders/*.shd` **тем же D3DX** и сверить побайтово с
  текущими: это доказывает, что перенос тула в граф ничего не меняет;
  (3) только потом `D3DXCompileShader` → `D3DCompile` (сначала с теми же
  `vs_3_0/ps_3_0` — D3D9 не умеет SM5, так что SM3-выход обязан остаться),
  затем SM5.0 + DXC→SPIR-V и новый `.shd` с несколькими блобами.
  Для шага (1) под Wine нужен **x64** `d3dx9_43.dll` (в дереве только x86) —
  он уже доставлен ради `tools/sh_math_test`.
  **Статус переноса в CMake-граф (2026-10-13): выполнен.** `ShaderCompiler`
  — 26-й проект графа: `EXTRA_TOOLS` в `parse_vcproj.py` + `TOOL_OBJS` и
  цель `<t>_tool` в `gen_cmake.py` (имя цели с суффиксом, потому что
  OBJECT-цель `<t>` уже занята из `projects/<t>.cmake` — CMP0002). Собран
  `cl 14.44` x64 под Wine: `ShaderCompiler.exe` 649 728 Б. В линковку
  `PW_Game.exe` не попадает (проверено: в `add_executable(PW_Game …)` 24
  объектные либы, `ShaderCompiler` среди них нет), клиент после этого
  собирается и проходит `check_exe.py` (ИТОГ: OK).
  Потребовалась одна правка исходника: `multythread/mtworkerlite.cpp` —
  `_asm mfence` → `MemoryBarrier()` (x64 не поддерживает inline `__asm`,
  C4235; на x86 `MemoryBarrier()` — тот же полный барьер).
  Запуск: `wine ShaderCompiler.exe <abs>/Src/Render/Shaders/MaterialSpec.xml
  --out<каталог>/ --name<Shader>`; у парсера (`parse.cpp`) значения
  **приклеены к флагу без пробела** (`--nameSkyMaterial`, `--out<path>`);
  XML лежит в `Src/Render/Shaders/` (не в `Data/Shaders/UberShaders/`, там
  только результат), `.hlsl` ищутся рядом с XML; рядом с exe нужен **x64**
  `d3dx9_43.dll`.
  **Две находки, меняющие план работ с контентом:**
  1. **Поставленные в контент `.shd` не воспроизводятся текущими
     исходниками.** Прогон `SkyMaterial` (компилируется без ошибок) дал
     2156 Б против 772 Б в `Data/Shaders/UberShaders/SkyMaterial.shd`:
     заголовок тот же (версия 2, `CTAB`), размеры и смещения таблиц разные.
     Значит критерий «перегенерировать и сверить побайтово с тем, что в
     репозитории» **невалиден** — контейнеры в контенте старше текущих
     `.hlsl`. Это же объясняет, почему фикс `coeffMap: s0→s3` (§2.7)
     инертен: DXBC в контенте просто не пересобирался.
  2. **Часть материалов не компилируется текущим `d3dx9_43`:**
     `E5017: Aborting due to not yet implemented feature: Resource load
     type 4` — у `FullScreenMaterial` на каждой вариации, у `UIBaseMaterial`
     3 из N; `SkyMaterial` компилируется. Версия D3DX та же, что у
     импортной либы в вендоре (`Vendor/DirectX/Lib/*/d3dx9.lib` →
     `d3dx9_43.dll`), т.е. это не «тулчейн другой» — либо `.hlsl` ушли вперёд
     относительно компилятора, либо тул не запускался очень давно.
     Это **отдельный блокер этапа 3**: без рабочей перегенерации нельзя
     активировать никакие правки `.hlsl` (в том числе перевод vertex-texture
     и PostFX-коэффициентов на structured buffer).
  **E5017 разъяснён (2026-10-13, вечер).** Ошибка стоит на
  `tex2Dlod(coeffMap, …)` в вершинной стадии (`Shaders/FullScreenMaterial.hlsl:77`,
  `MainPass_VSMain`); профили задаёт сам тул (`vs_3_0/ps_3_0`, `vs_2_0/ps_2_0`
  только с `--sm20`), в XML профилей нет. Версия «это Wine-встроенный d3dx9»
  **отклонена экспериментально**: проба `LoadLibraryA("d3dx9_43.dll")` +
  `GetModuleFileNameA` под тем же префиксом даёт `Z:\tmp\d3dx9_43.dll`, т.е.
  при `WINEDLLOVERRIDES=d3dx9_43=n` грузится настоящая MS-библиотека (в прогоне
  генератора она стояла рядом с exe и оверрайд был выставлен). Значит
  **настоящий `d3dx9_43` не компилирует текущие исходники**: `tex2Dlod` в
  `vs_3_0` он отвергает как «not yet implemented feature». Объявление
  `sampler2D coeffMap` вместо бесцветного `sampler` (внесено) само по себе не
  помогает.
  **Решение найдено: `D3DCompile` эти исходники компилирует (2026-10-13,
  tiny10).** Для проверки сделан `tools/hlsl_compile_test` (динамическая
  загрузка `d3dcompiler_XX`, свой `ID3DInclude`, pin-дефайны как у генератора,
  профили/точки входа `vs_3_0|ps_3_0` + `MainPass_VSMain|MainPass_PSMain`,
  флаги `OPTIMIZATION_LEVEL3`). Прогон на tiny10 с настоящим
  `C:\windows\SYSTEM32\d3dcompiler_47.dll`:

  | материал | стадия | `d3dx9_43` | `D3DCompile` (MS 47) |
  |---|---|---|---|
  | FullScreenMaterial | `vs_3_0` VS | E5017 на всех вариациях | **OK**, DXBC 1568 Б |
  | FullScreenMaterial | `ps_3_0` PS | E5017 | **OK** |
  | UIBaseMaterial | `vs_3_0` / `ps_3_0` | 3× E5017 | **OK**, 768/1612 Б |
  | SkyMaterial | — | OK | — |

  В Wine проверять нельзя: `d3dcompiler_46/42/40` в префиксе — заглушки
  (`D3DCompile` → `E_NOTIMPL`, 0x80004001), а встроенный в Wine `d3dcompiler_47`
  расходится с MS `fxc` (`MainPass_PSMain` у `FullScreenMaterial` он отвергает с
  `E5017: SM1 non-float expression`, MS-компилятор принимает). Выводы принимает
  только прогон на настоящей Windows (стенд `C:\tools\hlsl\` на tiny10).
  Вывод: поставленные в контент `.shd` собраны **не** этим компилятором и (или)
  из других исходников — находка 1 (SkyMaterial 2156 Б против 772 Б) и находка
  2 согласуются. Для этапа 3 это означает: путь «перегенерировать `.shd` тем же
  D3DX» закрыт; генератор переносится на `D3DCompile` **на том же профиле
  `vs_3_0/ps_3_0`** (D3D9 June 2010 и новее ест DXBC от `fxc`, так что контейнер
  и рантайм-загрузка не меняются), с проверкой эквивалентности картинкой на
  D3D9-бэкенде: разный компилятор — разная регистрация и, возможно, разный
  порядок инструкций, поэтому E2E + скриншоты обязательны. Дальше — SM5/DXBC и
  DXC→SPIR-V для Vulkan.
  **Перенос сделан (2026-10-13, коммит `006e8d356`):** `Src/ShaderCompiler`
  зовёт `D3DCompile` вместо `D3DXCompileShader` (`D3D_SHADER_MACRO`,
  `ID3DInclude`, `ShaderBlob` = `ID3DBlob*`, `D3DCOMPILE_OPTIMIZATION_LEVEL3`),
  профили прежние `vs_3_0/ps_3_0`. `D3DCompile` грузится динамически
  (`LoadLibrary` 47/48/50/43 + `GetProcAddress`): в SDK единственная импортная
  либа `d3dcompiler.lib`, и она привязывает exe к `D3DCOMPILER_43.dll`, которой
  нет на урезанных образах (на tiny10 есть только 47) — с импортом тул там не
  стартовал. В импортах собранного `ShaderCompiler.exe` нет ни `d3dx9_43.dll`,
  ни `d3dcompiler_*.dll`.
  Прогон на tiny10: `SkyMaterial.shd` и `FullScreenMaterial.shd`
  сгенерированы (46 076 Б против 46 528 Б в репозитории), ни одной E5017,
  `Sharing: 20% left (44.0Kb vs 216.3Kb)`. Клиент после переноса собирается как
  раньше (15 175 680 Б, ИТОГ: OK).
  **Проба на клиенте (2026-10-13): новый DXBC клиент принимает.**
  Перегнано 34 материала из 35 (нет только `BasicMaskMaterial` — тул делает для
  него 0 вариаций, вопрос открытый) и поставлено в `C:\pw64\Game\Packs\
  zzzresult.pile`. Порядок выбран не случайно: `PileFileSystem::ScanForPiles()`
  сортирует `Packs\*.pile` по убыванию (`NStr::ICmp(...) > 0`), поиск идёт по
  этому списку до первого совпадения, поэтому `zzzresult.pile` перекрывает и
  `result.pile`, и `zpatch_fev.pile`; в логе это видно первым:
  `Pile file '...zzzresult.pile' found`. Регистрация файло систем в
  `PW_Client/Game.cpp` — pile **до** `WinFileSystem(Data/)`, так что положить
  шейдеры отдельными файлами в `Data/` нельзя: они перекроются pile.
  E2E на tiny10 (логи `2026.10.13-03.21.11` и `2026.10.13-03.39.03`): login →
  lobby → game service → `mapload: all map objects loaded` →
  `Switch to EGameState::Finished by 3`, дампов нет, ни одной строки про
  повреждённый pile/CRC/шейдеры; на бэкенде `[session] finish …`.
  Чем паковали: готовый `Tools/FilePileCompiler/FilePileCompiler.exe` на
  урезанной Windows бесполезен (x86, требует MSVCR90/MSVCP90/CrashRpt/zlib1 —
  на tiny10 их нет, старт молча не удаётся). Написан `tools/shader_pile/mkpile.py`
  — формат простой (`FilePileFormat.h` + `FilePileCompiler.cpp`): метка, данные
  (zlib level 9 + флаг `FILEPILE_COMPRESSED`), таблица имён и индекс, забитые
  XOR-потоком `SimpleCrypt` (state+83 на байт, без сброса внутри блока), в конце
  незашифрованный `SFilePileTail`. Понимание формата проверено чтением боевого
  `zpatch_fev.pile`.
  **Контрольный A/B-прогон — приоритет pile доказан.** Тот же стенд, та же
  процедура, свежая сессия в каждом прогоне:
  - хороший pile (34 шейдера, zlib) → `Got login reply` → лобби через 1.5 с →
    game service → mapload → `Finished` (два таких прогона);
  - тот же pile, но один байт инвертирован в теле `BasicMaterial.shd` (контейнер
    собран без сжатия, чтобы CRC считалась по хранимым байтам) → клиент
    **зависает сразу после `Got login reply`**, ещё до лобби; дампов нет.
  Итог: запись из `zzzresult.pile` читается и используется (иначе битый CRC-байт
  не проявил бы себя), перегенерированный DXBC клиент принимает.
  Зависание вместо строки в логе объяснимо: при несовпадении CRC
  `FilePileLoader` зовёт `UserMessage::ShowMessageAndTerminate` — модалку из
  SSH-сессии не снять, в лог она не пишется. Учитывать при будущих проверках. A/B-прогон x86/VS2008-версии тула отменён: причина E5017
  установлена (компилятор), различать тулчейны больше не нужно.
  `MaterialSpec.{h,cpp}` этим генератором **не** создаются — их создаёт
  `Src/DBCodeGen/` (этап 0): вход — скомпилированная сборка DB-типов
  (`PF_Types.csproj`), а `Shaders/MaterialSpec.xml` — **выход** генератора
  (таблица пинов для `ShaderCompiler`). Для смены набора пинов (например, убрать
  `R2VBPinValue`) теперь достаточно перегенерации — она воспроизводит дерево.
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
- **Исходников у этих 44 блобов в репозитории нет** (замер 2026-10-15): файл
  `.psb`/`.vsb` = чистый D3D9-микротекст (заголовок `0x03ff 0xfffe` + таблица
  комментариев `CTAB` с именем шейдера), размеры 156–560 Б; ни `.hlsl`, ни
  `.fx` с этими именами в репо не ищется (по имени встречаются только
  `Src/Render/Bloom.cpp` и `Src/Terrain/NatureMapVisual.cpp`). Значит
  предкомпилировать их в DXBC-SM5/SPIR-V нельзя — только **переписать руками**
  (PostFX-трюки: `ConstantColor`, `ApplyAlpha`, `Convolve7/9`, `BlurDistort`,
  `AddDiffuseMap`). Оценка — 1 смена; без этого PostFX на Vulkan не работает
  вовсе, откладывать нельзя. Решение по объёму работ — §3.z (открытые вопросы).
- **Лимиты SM5, которые надо проверить по контенту:**
  `VSHADER_NIGHT_PARAMS c234`, `PSHADER_NIGHT_PARAMS c220` (OK, 4096),
  `BONES c24` + `MAX_BONES 70` (comment: «can't be much higher due to Wine»),
  `MAX_INSTANCES_COUNT 19`, `PFX_MAX_INSTANCES 47`,
  `SetVertexShaderConstants(VSHADER_LOCALCONST9, 3*numInstances, …)` —
  **запись произвольного числа регистров в mid-shader**, в DXBC-SM5 это
  возможно, но в Vulkan-константном буфере требует другой раскладки.
  См. §3.

**Компилятор: DXC — единый для обоих новых бэкендов (решение 2026-10-14).**
Замер на `dxc 1.9.2609` (`tools/spirv_probe/vendor/dxc`, качается
`fetch_dxc.sh`):

| профиль | вывод |
|---|---|
| `-spirv` | SPIR-V (magic `0x07230203`) |
| `-T vs_5_0` / `ps_5_0` | **DXBC** (magic `DXBC`) — DX12 принимает SM5.x DXBC |
| `-T vs_6_0` | DXIL-контейнер (тот же magic `DXBC`) |
| `-T vs_3_0` | **ошибка `invalid profile vs_3_0`** |

Следствия:
1. Vulkan — `-spirv`; DX12 — `-T vs_5_0/ps_5_0` (DXBC SM5.1). DXIL (`vs_6_0`)
   не берём: он требует валидации/подписи `dxil.dll` в рантайме, тогда как
   SM5.x DXBC DX12 принимает без дополнительных зависимостей; SM6-фичи
   (wave-операции, bind arrays) клиенту не нужны.
2. **D3D9-путь DXC не покрывается** (SM3 недостижим): он остаётся на
   `D3DCompile` (`vs_3_0/ps_3_0`), куда генератор уже перенесён (`006e8d356`),
   и живёт до этапа 4. В генераторе значит два компилятора — легаси (SM3 DXBC
   для D3D9) и DXC (SPIR-V + DXBC SM5 для новых бэкендов), **из одного и того
   же `.hlsl`**.
3. Прослойка SM3→SM6 из одноразового `shim.py` (спайк A) — **не заголовок, а
   шаг подготовки исходника перед DXC**. Макросами в HLSL это не выражается:
   `: register(...)` нельзя ни снять, ни переопределить (остаётся висячее `:`),
   `POSITION`/`COLOR` в одной TU встречаются и на входе VS, и на выходе VS
   (макром не различить), а `sampler NAME[4]` — ещё и массив ресурса. Значит
   это шаг драйвера компиляции (того же генератора, который уже раскрывает
   PIN'ы); `shim.py` — её прототип, на этапе 3 переезжает в тул. В
   `.hlsl`-заголовке остаётся только то, что макросом выражается (общий
   `SamplerState`, обёртки выборок), и под guard'ом, чтобы fxc/SM3-путь его не
   видел, иначе легаси-контейнер перестанет собираться.
4. Три DXC-провала спайка A **исправлены в исходниках (2026-10-14)**:
   `UIFlashMaterial::PrepareUV` — запасной `return defaultUV` (при части
   перестановок PIN'ов ни один `return` не оставался: `MorphShapes` без `UseUV`,
   либо `UseDiffuse != PRESENT`), `TerrainElementMaterial` PS — цикл, индексом
   которого подставлялся сэмплер из массива, развёрнут по значению пина
   `TextureCountPinValue` (порядок сложения сохранён). После этого спайк A —
   **70/70**; легаси-путь прогнан регрессом: 272 компиляции `D3DCompile`
   (`vs_3_0`/`ps_3_0`, перестановки затрагиваемых PIN'ов) — 0 провалов.
5. `.shd`-контейнер — один, с несколькими блобами (SM3 DXBC / DXBC SM5 /
   SPIR-V) и выбором по бэкенду; `OnlineShaders/` (`.vsb`/`.psb`) получает тот
   же набор, но выбор по имени.
6. **Рантайм-компиляции шейдеров в shipping нет (первичная оценка неверна),
   но путь всё равно убираем — по другой причине.**
   `RenderData::Initialize()` (`debugrenderer.cpp:193`, вызов безусловный из
   `PFRenderInterface.cpp:470`) компилирует **8 шейдеров**
   (`Data/deprecated_shaders/{debug,debug3d,debug_points2d,debug_points3d}_shader.hlsl`
   × VS/PS) через `D3DXCompileShader`/`vs_3_0` (`shadercompiler.cpp:135`) — но
   блок закрыт `#ifndef _SHIPPING`, а под Wine ранний `return` делает
   `Compatibility::IsRunnedUnderWine()`. Второй рантайм-компилятор —
   `MakeModelFromAIGeometryInstances(..., Render::StaticModel*, ...)`
   (`ModelLoader.cpp:115`, `../data/dummy_shader.hlsl`) — мёртв: объявление
   закомментировано (`ModelLoader.h:16`), вызывается только перегрузка с
   `StaticMesh*`. Третий (`WaterManager.cpp:92`) закомментирован.
   Убирать надо не из-за shipping, а потому что `vs_3_0` DXC не умеет, и
   отладочным линиям/точкам на новом бэкенде нужен свой блоб:
   предкомпиляция в `OnlineShaders/` (`ImmRenderer::Get{V,P}Shader(name)` уже
   есть) — этап 3, удаление рантайм-`ShaderCompiler` — этап 4 вместе с
   `d3dx9_43`.

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
- [x] **Генератор `MaterialSpec.{h,cpp}` — найден, своего писать не нужно
      (2026-10-14, `d7c2ac0c8`).** Это `Src/DBCodeGen/` (C#; предсобранный
      `Vendor/BuildUtils/DBCodeGen.exe`), вызывается post-build'ом
      `Types.csproj` / `PF_Types.csproj` через `Vendor/BuildUtils/DBCodeGen.cmd`.
      Вход — **не** XML: классы материалов строятся по типам из скомпилированной
      сборки DB-типов (`PF_Types.csproj`), а `Shaders/MaterialSpec.xml` — выход
      генератора (пины для `ShaderCompiler`).
      Проверено под Wine: генератор собирается (`MSBuild.exe` из
      `C:\windows\Microsoft.NET\Framework64\v4.0.30319` в префиксе VS2022) и
      перегенерирует `Render/MaterialSpec.{h,cpp,inl}` **побайтово идентично**
      закоммиченному. Команда:
      `DBCodeGen.exe PF_Types --solution=<abs>/Src/PF.sln --project=PF_Types.DBTypes --materials`
      (именно без финального проекта: `Types.csproj` передаёт `--materials=Render`
      и пишет в `Render/MaterialSpec.h` финальный вариант с неподставленным
      `$(ProjectsWithSpecs)` — в сборке редактора это отдельный баг).
      Ручные правки перенесены в генератор: `NI_DB_LAYOUT_ASSERT` в эмиттере
      (`MaterialCode.cs`), блок макроса и фактический регистр include'ов
      (`material.h`, `renderresourcemanager.h`) в шаблонах
      `Patterns/MaterialSpec{,Final}Header.txt` (`.txt` — ресурсы, нужен
      пересбор `DBCodeGen.exe`). Починен разбор `.sln`: `GetFileNameInProject`
      сравнивал путь как строку, из-за чего у `Client\Client.vcproj` имя
      обрезалось до `.vcproj` и падал весь `PF.sln` (то же для любого проекта,
      где имя файла начинается с имени каталога).
- [ ] **Остальной generated-слой перегенерацией НЕ включать:** из 82 затронутых
      файлов 72 меняются только по EOL, 10 — по содержимому
      (`[FloatMinMax(0, 1)]` → `[FloatMinMax(0f, 1f)]` и т.п.: закоммиченное
      сделано более старой версией тула; то же описано в `docs/x64/EDITOR.md`
      §DBCodeGen, там же культур-зависимый `String.Format("{0}f")`). К этапу 3
      отношения нет — нам нужен только `MaterialSpec*`.
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
- [x] **Компилятор зафиксирован (2026-10-14): DXC — единый для обоих новых
      бэкендов** (`-spirv` для Vulkan, `-T vs_5_0/ps_5_0` → DXBC SM5.1 для
      DX12; DXIL не берём из-за `dxil.dll`). `d3dcompiler_47` остаётся только
      за легаси-D3D9-генерацией (SM3) до этапа 4. DXC — **build-time**
      инструмент, в `Bin/` не кладётся. Замер профилей и следствия — §2.6.
- [x] Счётчики уникальных VS/PS/пар переведены на `USE_RENDER_SHADER_STATS`
      и доступны в shipping по `-DPW_RENDER_SHADER_STATS` (§1.2).
- [x] **Реальные цифры сняты (2026-10-13, подтверждено 2026-10-14, §1.2.1):**
      185 уникальных пар (VS,PS) за бой, 146–148 пар внутри окна в 300 кадров,
      59 VS / 68 PS, 117 уникальных `sortId` (98–99 в окне). Дамп группы в лог
      уже был (`OnFrameStart`, раз в 300 кадров, вместе с `ConstStore:`) —
      отдельный дамп при выходе не понадобился: `DebugVar` в shipping не читаются,
      а лог читается. Цифры воспроизводимы между прогонами, потолок мал — кэш
      пайплайнов проектируется на сотни записей без eviction-политики.

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
      `DXResourcesControl` в дереве ЕСТЬ и компилируется (см. §2.3 — ушёл в
      этап 4).
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
- [x] **Удалить GPU-окклюзию целиком** (§3.y — решение принято 2026-10-12,
      удалено `a49b7d2b5`, проверено 2026-10-14: `Render/OcclusionQueries.*` в
      дереве нет, `OcclusionQuer`/`g_useQueries` в исходниках не встречаются):
      `Render/OcclusionQueries.{h,cpp}`, `RenderComponent::GetQueries()` +
      реализации, `RenderBatch<mode>`/`switch(GetUseMode())` в `batch.cpp`,
      `IssueQueriesInvisible`, `g_useQueries`/cvar `waterUseQueries`,
      `OcclusionQueriesBank::OnFrameStart()`. В дефолте механизм выключен
      (`waterZPrepass=0`), так что это зачистка мёртвого, а не смена поведения;
      заодно уходит `IDirect3DQuery9` и `#include <d3d9.h>` из `OcclusionQueries.h`.
- [ ] Загрузка текстур через D3DX (`CreateTextureFromFileInMemoryEx`,
      `CreateCubeTextureFromFileInMemoryEx`, `D3DX_FILTER_*`, `D3DXIFF_DDS`) —
      нужен собственный DDS/TGA/PNG-загрузчик; это работа этапа 3/4, не этап 1.
- Проверка (2026-10-12, зачистка D3D9-утечек и SH-математики): x64-сборка без
      ошибок (`build/PW_Game.exe`, 15 190 016 Б, PE32+, LAA, FileVersion
      2.16.1.0) + **полный E2E на tiny10
      через `PwE2E64` ПРОЙДЕН**: login `Success(5) uid=131` → lobby →
      `gamesvc_00` → `mapload: all map objects loaded` → бой 3,5 мин →
      `Switch to EGameState::Finished by 3` → `finish <token>` в dev-бэкенде;
      дампов нет, `runtime error`/`shader`/`D3D`/`assert` в логе нет, набор
      `Unknown command or variable` = эталонный. Грабель про гейт версии
      (`PW_VERSION=2.16.1` для `client-modern`) — в скиле `pw-client`.
- Проверка (2026-10-12, `a49b7d2b5` — удаление GPU-окклюзии, −616/+26):
      x64-сборка 0 ошибок (`PW_Game.exe` 15 175 680 Б) + **E2E на tiny10
      ПРОЙДЕН** (лог-папка `2026.10.12-22.19.19`): login `Success(5) uid=131`
      → lobby → `gamesvc_00` → `mapload: all map objects loaded` → бой 3 мин →
      `Switch to EGameState::Finished by 3` → `finish 2de87775…` в dev-бэкенде;
      новых дампов нет, `assert`/`shader`/`query`-ошибок в логе нет.
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
      **Начато 2026-10-13:** интерфейс положен, но файлы лежат плоско
      (`Src/Render/renderbackend.h`), а не в каталоге `Backend/` — список
      файлов `Render.vcproj` плоский, подкаталогов в нём нет, а из него
      строится CMake-граф. Пока закрыта только ядро-часть (см. §2.8).
      **Состояние на 2026-10-15 (шаги 2–8 очереди):** в seam переведены
      окно/`Present`, `RECT`-вызовы, рендер-состояния и сэмплеры
      (`RenderStatesManager`), блокировка буферов, вершинные потоки и
      индексы, input layout, создание ресурсов и шейдеров, константы стадии.
      Прямых `GetDevice()->…` внутри `Src/Render` осталось **20** (было 96),
      из них `SetRenderState` — 3, `GetViewport` — 3; в `FlashRenderer.cpp`
      вместо 51 вызова — 1. Что не закрыто: `StretchSurfaceRect` /
      `GetRenderTargetData` / `ColorFillSurface` (нужна нейтральная ручка
      поверхности), `GetViewport(D3DVIEWPORT9&)`, `SetFVF` +
      `DrawPrimitiveUP` у `RenderDeferred2D` (fixed-function, §3.z).
- [ ] `D3D9Backend` — перенос `SmartRenderer`/`Renderer` **как есть**
      (временно; удаляется на этапе 4). **Начато 2026-10-13:**
      `Src/Render/d3d9backend.{h,cpp}`, у `Renderer` появился `pBackend`
      (пересоздаётся вместе с `CreateDevice`, `Reset()` объекта устройства не
      меняет). Перенесено 10 вызовов из `renderer.cpp`, осталось 12 (§2.8).
      **Состояние на 2026-10-15:** счётчик устарел — `D3D9Backend` единственный
      реализатор `IRenderBackend`, в `renderer.cpp` осталось 4 прямых
      `GetDevice()->`, в `smartrenderer.cpp` — 5.
- [ ] Кэш пайплайнов по ключу `sortId`-подобного хеша (§1.3) — размер и
      eviction по замерам этапа 0. **Ключ и замер сделаны 2026-10-15 (шаг 8
      очереди, `634bb2894`):** семантический ключ (VS+PS+input layout+маска
      состояний+тени сэмплеров+форматы RT+тип примитива+инстансинг) —
      206 уникальных на бой, 85 активных в кадре; raw-хеш состояния добавляет
      ещё 4. Сам кэш строится на этапе 3 (§3.z п.7: открытая таблица без LRU,
      синхронная компиляция).
- [ ] `ConstBlock` — «виртуальные регистры» c0..c255 одним буфером (§2.7),
      генерация раскладки из `GlobalMasks.h`. **Закрыто 2026-10-15 (шаг 7
      очереди, `ef9cfaa4e`):** `IRenderBackend::GetConstBlock` +
      `ShaderConstStore::TakeDirty` (диапазон грязных регистров + revision),
      сверка тени с устройством (`verify_const_block`) — 36/36 снимков,
      mismatch=0. Формулировка «генерация раскладки из `GlobalMasks.h`»
      признана недостаточной: явными размерами покрыто 8 регистров из 256
      (`tools/const_layout`), раскладку надо строить по живому диапазону +
      рефлексии DXC/SPIR-V.
- [x] **Состояние внутри `Src/Render`, обходящее `RenderStatesManager`**
      (находка 2026-10-14; в §2.1 считались только утечки *снаружи* `Render`;
      закрыто в тот же день).
      Прямых вызовов устройства внутри `Render` было **96** (`GetDevice()->…`),
      из них **55 `SetRenderState`**; `FlashRenderer.cpp` — 51 вызов устройства
      (33 «сырых» `SetRenderState`: SRCBLEND/DESTBLEND/BLENDOP, STENCIL*,
      COLORWRITEENABLE) и `uirenderer.cpp` — тот же обход менеджера состояний,
      что чинился в `PFRenderInterface`. Критерий «0 `GetDevice()` вне `Render`»
      выполнен, но цель этапа 2 («`Render` ходит через один интерфейс») по
      `FlashRenderer` не закрыта: на Vulkan-бэкенде эти состояния не попадут ни
      в `RenderState`-ключ, ни в `IRenderBackend`. Плюс чтение состояния обратно
      (`GetRenderState`/`GetSamplerState`/`GetVertexShaderConstantF`/
      `GetPixelShaderConstantF`) — `conststore` (§2.9) его не покрывает, на
      uniform-буферах ответить будет нечем.
      **Сделано:** все записи состояния вне менеджера переведены на
      `SetStateDirect`/`SetSamplerStateDirect` — `FlashRenderer.cpp` (50 вызовов),
      `uirenderer.cpp` (2, ALPHATESTENABLE), `smartrenderer.cpp` (1,
      ALPHABLENDENABLE в `RenderDeferred2D`), `ShadowMap.cpp` (1,
      `D3DSAMP_BORDERCOLOR` → `SetSamplerStateDirect`, BORDERCOLOR не входит в
      пресет). Вызовы устройства те же и в том же порядке, но кэш менеджера теперь
      инвалидируется → следующий `SetState()` с тем же пресетом обязан
      перепримениться (это и есть чинившийся дефект, см. §2.1 про
      `PFRenderInterface`).
      **Второй заход (тот же день) — то, что первый замер пропустил.** Считать
      «сырые» записи только по `GetDevice()->` недостаточно: в `Render` есть
      обращения к устройству через локальные переменные (`pDev`, `pDevice`,
      `device`). Так нашлись ещё 6 записей и 3 чтения состояния:
      `ShadowManager.cpp` (save/restore `DEPTHBIAS`/`SLOPESCALEDEPTHBIAS` — 4
      записи + 2 чтения) и `batch.cpp` (`ZeroScissor`, `SCISSORTESTENABLE` — 2
      записи + 1 чтение). Всё переведено на менеджер.
      **Чтение состояния обратно закрыто тенью состояний.** `RenderStatesManager`
      держит `stateShadow[256]` (индекс — числовое значение `D3DRS_*`, максимум
      209; тот же размер, что у `g_cachedStates`) + `stateShadowKnown[]`; все
      записи менеджера (25 мест в `SetState`/`SetStencilState`/`SetStateDirect`)
      идут через приватную `ApplyState()`, которая пишет в устройство и обновляет
      тень; `GetStateDirectValue()` отвечает из тени, а неизвестное значение один
      раз подтягивает с устройства. Тень обнуляется в `Init()` и в
      `OnDeviceLost()` (значения относятся к теряемому устройству). `DumpStates()`
      (dev-команда `dump_states`) читает из тени.
      Оговорки: путь state-block (`USE_STATEBLOCK_CACHE`, `cached->Apply()`) тень
      не наполняет — макрос выключен (`//#define` в начале файла), при включении
      тень придётся синхронизировать с блоком; сэмплерные чтения (`GetSamplerState`
      в `DumpStates()` и в `#ifdef _DEBUG`-проверках того же выключенного кэша)
      остались обращены к устройству — функциональных чтений сэмплеров в коде нет.
      Итог по прямым обращениям к устройству внутри `Render` (кроме менеджера и
      `d3d9backend.cpp`, только живые строки): **60**, из них **0 записей и чтений
      состояния**. Остаток — ресурсы и поверхности (`CreateTexture`/
      `CreateRenderTarget`/`CreateCubeTexture`/`CreatePixelShader`/
      `CreateVertexShader`/`CreateVertexDeclaration`/`CreateQuery`, `StretchRect`,
      `Clear`, `SetScissorRect`/`GetScissorRect`, `EvictManagedResources`),
      фиксированный блок `RenderDeferred2D` (`SetFVF`/`DrawPrimitiveUP`/
      `SetVertexShader`/`SetTexture`/`SetStreamSource*`, §1.4), запросы
      возможностей (`GetDeviceCaps`, `GetProperties`, `TestCooperativeLevel`) и
      `GetViewport` (5 мест; в интерфейсе бэкенда он есть, но кэш viewport'а ещё
      D3D9-типизирован — §2.8).
- [x] **`DeviceLostHandler` → ре-аллокация** (закрыто 2026-10-14).
      Реестр переименован в `ReallocateHandler` (`DeviceLost.{h,cpp}` →
      `Reallocate.{h,cpp}`), методы — `OnResourcesRelease()` /
      `OnResourcesReallocate()`, уведомления — `NotifyResourcesRelease()` /
      `NotifyResourcesReallocate()`, обёртка — `ReallocateWrapper`.
      `DeviceDeleteHandler` (`OnDeviceDelete`/`OnDeviceCreate`) назван не тронут:
      это про сам объект устройства (CreateDevice/DeleteDevice), а не про размер
      кадра.
      **Находка по ходу (важнее самого переименования):** путь `Renderer::Start()`
      → `ResetDevice()` (create-or-reset) **не уведомлял обработчики вообще** —
      `NotifyDeviceLost/Reset` были только в `RecoverFromDeviceLost()` и
      `InvalidateResources()`. Безопасно это лишь потому, что `Start()` в клиенте
      вызывается один раз на старте (`Game.cpp:1441`, `EditorNative`, тест) и
      смены разрешения на лету нет; с SDL-ресайзом (этап 3) этот путь станет
      живым, и DEFAULT-пул/RT-поверхности без уведомлений остались бы висячими.
      Поэтому `ResetDevice()` теперь делает release → `pDevice->Reset()` →
      reallocate, а при неудачном `Reset()` остаётся в состоянии «отпущено»
      (`bDeviceLost = true`), которое разберёт повторный `Start()` или
      `RecoverFromDeviceLost()` на ближайшем `Present()`.
- [~] Пулы: `RENDER_POOL_MANAGED` → staging + default + upload-ring
      (`dxutils.h::PoolType` сохраняется как enum).
      **Первый шаг сделан 2026-10-14:** создание вершинных/индексных буферов
      переведено через `IRenderBackend` — в seam положен нейтральный enum
      `EBackendPool` (порядок значений совпадает с `PoolType`, паритет
      проверяется `NI_STATIC_ASSERT`-ами в `D3DPoolFromBackend`,
      `d3d9backend.cpp`), появились `CreateVertexBuffer(size, pool, HBuffer*)` и
      `CreateIndexBuffer(size, index32, pool, HBuffer*)`; `CreateVBInternal` /
      `CreateIBInternal` в `dxutils.cpp` больше не зовут устройство напрямую
      (прямых `CreateVertexBuffer`/`CreateIndexBuffer` в `dxutils.cpp` — 0),
      34 места-потребителя (`CreateVB`/`CreateIB`/`CreateIB16`,
      `DXVertexBufferDynamicRef` и т.п.) не менялись. Начальных данных в
      сигнатуре нет намеренно: путь записи — отдельный.
      **Остаток:** (а) **закрыто 2026-10-15 (`d8e3f45eb`): механизм записи
      переведён в seam — §2.8 «Восьмой шаг»:** `Lock`/`Unlock`
      буферов идут через `IRenderBackend`, режим — `ERenderLockType`;
      прямых обращений к буферам устройства вне бэкенда по всему `Src/` — 0.
      Само staging-кольцо — реализация этих методов в `NvrhiBackend` (этап 3);
      (б) **закрыто
      2026-10-14:** текстуры и поверхности тоже создаются через seam —
      `CreateTexture2D`/`CreateTextureCube`/`CreateSurface`/`UpdateTexture` и
      словари `EBackendFormat`/`EBackendUsage`/`EBackendSurfaceKind`
      (подробности и замер форматов — §2.8 «Шестой шаг»); реально устройство
      создавало ресурсы в 10 местах (`texture.cpp`, `TextureManager.cpp`,
      `rendersurface.cpp`, `ReadDDS.cpp`), а не в 14: `ShadowMap.cpp` и
      `WaterManager.cpp` зовут проектную фабрику `CreateDepthStencilSurface()`,
      `mesh.cpp`/`ModelLoader.cpp` ресурсов не создают; (в) само разделение
      managed → staging+default живёт в NvrhiBackend (этап 3), здесь только
      словарь пулов за seam'ом.
- Критерий: картинка не меняется, E2E зелёный.

### 3.0 Вендоринг зависимостей рендера (закрыто 2026-10-14)

Деревья в `pw/branches/r1117/Vendor/`, версии зафиксированы в
`Vendor/fetch_render_deps.sh` (+ `SOURCE_COMMIT.txt` в каждом дереве):

| дерево | версия | размер | что нужно |
|---|---|---|---|
| `NVRHI/` | `6b96fb03e075` | 2,0 МБ | сам бэкенд-слой (examples/doc вырезаны) |
| `Vulkan-Headers/` | `v1.4.352` | 21 МБ | только `include/`: NVRHI работает через Vulkan-Hpp |
| `DirectX-Headers/` | `v1.717.0-preview` | 4,0 МБ | `include/` + `src/` (CMake компилирует `DirectX-Headers`/`DirectX-Guids`) |
| `SDL3/` | `release-3.2.30` | 32 МБ | `src/`+`include/`+`cmake/`+`build-scripts/` (test/ 21 МБ и examples/ 8 МБ вырезаны) |

Версии хэдеров совпадают с теми, что подтягивает CMake NVRHI по умолчанию
(`NVRHI_VULKAN_HEADERS_GIT_TAG`/`NVRHI_DIRECTX_HEADERS_GIT_TAG`) — сверять при
апгрейде NVRHI. Свои, а не FetchContent: сборка клиента идёт без сети.

Встраивание — `gen_cmake.py` (блок «вендорские библиотеки рендера», только при
`PW_MACHINE=X64`): `Vulkan-Headers` (INTERFACE) → `add_subdirectory`
DirectX-Headers → NVRHI (`NVRHI_WITH_VULKAN=ON`, `DX12=ON`, `DX11=OFF`,
`BUILD_SHARED=OFF`, FETCH выключены) → SDL3 (`SDL_STATIC=ON`, `SDL_SHARED=OFF`).
Все `EXCLUDE_FROM_ALL`, в линковку `PW_Game` не входят; проверяются отдельной
целью `render_vendor` — она собирается `./build_client_x64_wine.sh --vendor`.

Найденные при внедрении грабли (все — не очевидные, при апгрейде повторятся):
1. **`add_library(Vulkan::Headers INTERFACE)` CMake запрещает** («reserved»).
   NVRHI принимает и простое имя (`Vendor/NVRHI/CMakeLists.txt:85-90`), поэтому
   цель называется `Vulkan-Headers`.
2. **`/EHsc` обязан быть добавлен руками.** Верхний CMakeLists обнуляет
   `CMAKE_CXX_FLAGS` (флаги приходят из vcproj, где `/EHsc` вычищен), а NVRHI
   живёт на try/catch → без него cl даёт C4530 и стек-развёртка не работает.
3. **`HAVE_ROAPI_H` у SDL3 прописан в кэш наперёд** (`cmake/PreseedMSVCCache.cmake:176`
   для MSVC≥19.1), а `roapi.h`/`windows.gaming.input.h` лежат в
   `Include/<ver>/winrt`, которого в include-пути клиента нет (там ucrt/um/shared).
   `set(... CACHE ... FORCE)` SDL3 перезаписывает, помогает обычная переменная
   `set(HAVE_ROAPI_H OFF)` в родительской области (она перекрывает кэш при
   `#cmakedefine`). Фолбэк апстримовый: SDL сам объявляет `RO_INIT_TYPE` и
   грузит `RoInitialize` динамически.
4. **Подсистемы SDL, кроме видео, выключены** (`SDL_JOYSTICK/HAPTIC/SENSOR/
   AUDIO/CAMERA/RENDER/GPU=OFF`): они тянут те самые WinRT-заголовки, а клиенту
   от SDL нужны только окно, `SDL_Vulkan_CreateSurface` и события окна (ввод —
   свой `WndProc`, звук — FMOD, рендер — NVRHI).

Статика собрана `/MT` (тот же CRT, что у клиента). Размеры библиоток — ориентир
для этапа 3 (в exe попадут только используемые объекты):
`nvrhi.lib` 5,4 МБ, `nvrhi_d3d12.lib` 12,2 МБ, `nvrhi_vk.lib` 15,3 МБ,
`SDL3-static.lib` 4,7 МБ, `DirectX-Headers.lib` 0,1 МБ, `DirectX-Guids.lib` 0,1 МБ.

Гейт: `PW_Game.exe` **побитово совпал с эталоном до вендоринга**
(`sha256 6f09b48e…`, 15 196 160 Б) — `ninja: no work to do` по клиентским TU,
`check_exe.py` ИТОГ: OK. E2E не требовался: артефакт не изменился.

### Этап 3. `NvrhiBackend`: DX12 + Vulkan (6–10 смен)

Предполётный дизайн (макет дескриптор-сетов, разделение UBO/SSBO, контейнеры,
драйвер компиляции, инстансинг шаг 2, winding, кэш пайплайнов) и открытые
вопросы — **§3.z**.

- [x] **Вендоринг и CMake-таргеты — сделано (2026-10-14, §3.0).** `Vendor/NVRHI`
      + `Vendor/SDL3` + хэдеры, версии зафиксированы в `Vendor/fetch_render_deps.sh`;
      цели в графе (`gen_cmake.py`), проверка — `--vendor`. Статика `/MT`
      собралась без проблем с CRT, `nvrhi.dll` не понадобился. В TU клиента пока
      ни одного `#include <nvrhi.h>` — это появится вместе с бэкендом.
- [ ] Один бэкенд за `IRenderBackend` — **Vulkan**, дефолт. Интерфейс
      рассчитан на последующее добавление DX12 (никаких Vulkan-типов в
      заголовках вне реализации); dev-var `rndr_backend=vulkan` закладывается
      сразу, DX12 появится в нём позже.
- [ ] Swapchain: **через SDL3 (3.2.x), решение от 2026-10-14 — см. §3.x GAP:
      swapchain**. Окно, Vulkan surface, кадры, ресайз/минимизация/
      `OUT_OF_DATE_KHR` и полноэкранные режимы — на SDL; `isBorderless`/
      `vsyncCount` из `RenderMode` → `SDL_SetWindowFullscreenMode` /
      `SDL_WINDOW_BORDERLESS`; DX12-путь позже берёт HWND из
      `SDL_PROP_WINDOW_WIN32_HWND_POINTER`. Ввод остаётся на существующем
      `WndProc` (subclass), `SDL_main` не используется.
- [ ] Шейдеры: **DXC как единый компилятор обеих целей** (решение 2026-10-14):
      `-spirv` (Vulkan) и `-T vs_5_0/ps_5_0` → DXBC SM5.1 (DX12); легаси
      `D3DCompile` остаётся только за SM3/D3D9 до этапа 4. Новый `.shd`-формат
      с несколькими блобами; шаг SM3→SM6 — в драйвере компиляции (прототип —
      `tools/spirv_probe/shim.py`), а не в `.hlsl`-заголовке (почему — §2.6 п.3);
      предкомпиляция `deprecated_shaders/*` в `OnlineShaders/` и удаление
      рантайм-компиляции. Замер профилей и следствия — §2.6.
- [ ] Тени: `D24S8`/`D32F` + depth SRV вместо DF16/DF24/INTZ; ESM без Fetch4;
      NULL-RT → color-write 0. **Скриншот-дифф с ожиданием дрейфа** —
      зафиксировать новые эталоны.
      ⚠️ **Покрытие:** на E2E-стенде аппаратные карты теней ВЫКЛЮЧЕНЫ
      (`HasD16T/HasD24X/HasDSM` = 0, замер §2.5: `caps=0x0` на «Microsoft Basic
      Render Driver») — путь shadow maps стендом не проверяется. Временный форс
      этих флагов **невозможен**: стенд не создаёт depth-текстуры вообще
      (`CreateTexture(DEPTHSTENCIL, D24S8/D24X8/D16) = 0x8876086C`,
      `tools/dsm_probe`, §2.15 п.9) — форс дал бы исключение вместо покрытия.
      Покрытие переносится на Vulkan-контур (хостовый RTX 3060 / lavapipe);
      вопрос тестовой GPU-машины — открытый, §3.z.
      Механизм `force_dsm` (`-forceDsm`) всё равно сделан на шаге 4 — он для
      реального D3D9-железа и прогонов поверх `NvrhiBackend`.
- [ ] SH/PostFX: vertex-texture/R2VB → structured buffer.
- [x] Occlusion-запросы — **решение принято: отказываемся** (2026-10-12,
      §3.y). Реализация удаляется целиком на этапе 1 (остаток), в NVRHI-бэкенде
      GPU-окклюзии не будет; при необходимости вернёмся отдельной задачей
      (CPU-culling / HZB), вне рамок миграции.
      UP-вызовы → временные буферы; `StretchRect` →
      `copyTexture`/fullscreen-blit; `GetRenderTargetData` → `copyTextureRegion`
      в staging.
- [x] `configdatabase`: **`D3DCAPS9` целиком не синтезируется** (решение
      2026-10-14, §2.5; **закрыто 2026-10-15, шаг 4, `3b01b92c4`**): в seam —
      `BackendSystemInfo` (идентичность адаптера, `MaxVertexShaderConst=256`,
      поддержка depth-текстур, версии шейдерных моделей), матчер и `config.txt`
      не меняются. Регресс — дифф дампа разрешённых пинов: эталон 2026-10-14
      совпал дословно после перевода дампа на `BackendSystemInfo` (§2.5).
- [x] **Окно: `Renderer(HWND)` перестаёт принимать HWND** (закрыто 2026-10-14).
      `Render/renderwindow.h` (`NativeWindow` + `MakeNativeWindow`),
      `Renderer` хранит `NativeWindow`, `IRenderBackend::Present` — тоже
      `NativeWindow`; граница платформы — `RenderInterface.cpp`. Подробности и
      остаток — §2.8 «Четвёртый шаг».
- [x] **`RECT` из seam'а убран** (закрыто 2026-10-14): `BackendRect` в
      `Present`/`SetScissorRect`/`StretchRect`/`ReadSurface`, конвертация только в
      `d3d9backend.cpp`. Подробности и гравля с null — §2.8 «Пятый шаг».
- [ ] **Публичный API `Renderer` вне Win32/D3D9-типов** (частично закрыто
      2026-10-15): `SetScissorRect(const BackendRect*)` и
      `SaveFrameToFile(const char*, bool, const BackendRect*, bool)` — сделано
      (вызовы из `PF_GameLogic/PFRenderInterface.cpp` переведены на
      `MakeBackendRect`; полный тип в `renderer.h` не включается, вперёд
      `class IRenderBackend;` добавлено `struct BackendRect;`). Осталось:
      `StretchSurfaceRect(IDirect3DSurface9*, const RECT*, …)` — требует
      нейтральной ручки поверхности в слое ресурсов (вместе с удалением
      `GetDevice()`), `GetViewport(D3DVIEWPORT9&)`, scissor-кэш в `SmartRenderer`
      и `GetDevice()->GetViewport` (2 места).
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
пересобирать.

**Замер на реальном железе (2026-10-15, RTX 3060, драйвер 595.99.02,
`tools/nvrhi_probe/build-linux/nvrhi_probe`):** `maxBoundDescriptorSets = 32`,
`maxPerStageDescriptor{SampledImages,Samplers,UniformBuffers} = 1 048 576`,
`maxImageDimension2D = 32768`, `maxColorAttachments = 8`,
`maxVertexInputAttributes = 32`, `maxSamplerArraySize = 1 048 576`;
`ComputeQueue=1`, `Meshlets=0`, `RayTracingPipeline=0`, `VRS=0` (Ampere — mesh
shaders нет: ограничение железа, не пробы). Прежняя оговорка «у NVIDIA может
быть 7» закрыта: на Ampere + 595-м драйвере это 32. **Проектирующее число
остаётся 8** — слабое звено переносимости это lavapipe и модель root-параметров
D3D12, а не NVIDIA; 32 не повод разбрасывать сеты.

**Замер по самому контенту шейдеров (2026-10-15, `tools/shader_budget`)** —
прежде чем проектировать сеты, посчитано, сколько ресурсов реально требует
один пайплайн. 35 материалов из `MaterialSpec.xml` прогнаны через DXC
(`-spirv`, прослойка `shim.py`), полученные SPIR-V разобраны (число текстур /
сэмплеров / uniform-буферов, их размеры, номера set/binding, факт
НЕконстантной индексации члена uniform-буфера). 70/70 единиц (pins=0) и 69/70
(pins=1) компилируются и разобраны.

| | pins=0 (минимальная конфигурация генератора) | pins=1 (все PIN'ы включены, синтетика) |
|---|---|---|
| текстур на стадию | макс **7** (`RoadMaterial` ps), среднее 0,66 | макс 6 |
| сэмплеров | макс **1** (общий `g_smp` из прослойки) | макс 1 |
| uniform-буферов | **1** (`$Globals`) | 1 |
| структурных буферов (SSBO) | **0** | 0 |
| размер `$Globals` | макс **3744 Б** (`PaintMaterial` vs), среднее 422 Б | макс 7040 Б (`BasicMaterial` vs) |
| layout по умолчанию у DXC | всё в set 0: sampler bind 0, UBO bind 1, текстуры bind 2..9 | то же, bind до 8 |
| всего различных `(set, binding)` по корпусу | **10** | 9 |

Выводы (они меняют форму §3.w):

1. **Бюджет дескрипторов — не ограничение.** Максимум на стадию: 7 текстур +
   1 сэмплер + 1 uniform-буфер. В один set это ложится с запасом; 8 сетов
   хватает с избытком, и «по сету на каждый ресурс» не грозит ничем —
   запаса хватает даже на 16 сэмплеров модели клиента.
2. **Один `$Globals` на стадию = один uniform-буфер.** Это ровно то, что даёт
   тень `ConstBlock` (§2.9): 3744 Б против измеренных в бою 235 VS-регистров
   (3760 Б) — независимое подтверждение, что дизайн «один буфер констант на
   стадию» совпадает с тем, что требует сгенерированный шейдер.
3. **Главная проблема — не число сетов, а размер и частота обновления
   uniform-буфера** (3744 Б на рисование, 458 рисований/кадр — §1.4). Отсюда
   кольцо + `TakeDirty` (§2.9), а не хитрая раскладка сетов.
4. Размер 7040 Б при всех PIN'ах **превышает лимит D3D9 в 256 регистров
   (4096 Б)** — значит в реальном контенте диапазоны `GlobalMasks.h`
   перекрываются (взаимоисключающие фичи делят одни и те же `c`), и
   «суммарный» макет в Vulkan должен наследовать эти перекрытия, а не
   складывать все фичи. Проверка: `tools/const_layout` (§2.9) — пересечения
   диапазонов там уже посчитаны.

### 3.z Предполётный дизайн этапа 3 (2026-10-15)

Дизайн, с которым этап 3 начинает писать `NvrhiBackend`. Всё, что ниже,
опирается на замеры §1.2.1, §1.4, §2.9, §3.w и спайки B0/`nvrhi_probe`;
открытые вопросы вынесены в конец.

**1. Макет дескриптор-сетов (фиксированный, до первой строчки бэкенда).**
По замеру §3.w хватает трёх сетов; берём четыре, с запасом под DX12
(root-параметры) и постэффекты:

| set | содержимое | частота смены |
|---|---|---|
| 0 | «кадр»: общие константы (View/ViewProjection/eye/time/светы), один на кадр | 1 раз/кадр |
| 1 | материал: остальной `$Globals` (см. п.2) + SSBO динамических массивов | на батч/рисование |
| 2 | текстуры материала (bind 0..7 по замеру) | на батч |
| 3 | сэмплеры (bind 0..15 — по модели клиента `MIN/MAX_PS_SAMPLER_INDEX`) | на батч |

Числа `VulkanBindingOffsets` по умолчанию (`t:0, s:128, b:256, uav:384`)
задаются **под эту раскладку**, а не наоборот: сгенерированный HLSL обязан
ставить `register(tN)` → bind N в сете 2, `register(sN)` → 128+N в сете 3,
`register(bN)` → 256+N в сете 0/1. Это продолжение `GlobalMasks.h`: тот же
табличный источник, что и для `c`-регистров (§2.7).

**2. Разделение констант: UBO + SSBO.** Динамическая (неконстантная) индексация
члена uniform-буфера в Vulkan допустима только при uniform-индексе, а в D3D12
запрещена прямо — поэтому массивы, которые шейдер индексирует динамически,
обязаны уйти в структурный буфер. По замеру `tools/shader_budget` (pins=1)
таких единиц 13 из 69, и это ровно те массивы, о которых говорилось в §2.7:

| массив | размер | где |
|---|---|---|
| `BonesMatrices` | 70 × 48 Б = 3360 Б | `BasicFX/Debug/Drop/Shadow/Silhouette` vs |
| `instanceData` | 19 × 160 Б = 3040 Б (`BasicMaterial` vs), 19 × 64 Б (Decal, DecalTerrain) | инстансинг (§2.10 шаг 2) |
| `data` (PFX) | 47 × 80 Б = 3760 Б | `ParticleFXMaterial` vs |
| `hexParams` | 40 × 16 Б | `NatureMapMaterial` vs |
| `position` | 9 × 16 Б | `AOEMaterial` vs |
| `Params` | 18 × 16 Б | `FullScreenMaterial` vs (PostFX) |

Плюс отдельный вид: **динамическая индексация компоненты `float4`**
(`g_sharpCoef[i]`, `g_sharpPixelSize[i]`, `ColorCastCoef[i]` в
`FullScreenMaterial` vs) — формально не массив, но даёт динамическое
смещение внутри UBO; при переносе такие места правятся на явные `.x/.y/.z/.w`
или переезжают в тот же SSBO.

Смысл разделения: после переноса этих массивов UBO уменьшается до ~1 КБ
(против 3744 Б), и цена загрузки констант на рисование (§1.4 п.4: 1,72 МБ/кадр)
падает в разы; SSBO при этом обновляется раз в батч, а не раз в рисование.

**3. Контейнеры шейдеров.** Один `.shd` = несколько блобов: SM3 DXBC (D3D9 до
этапа 4), DXBC SM5 (DX12), SPIR-V (Vulkan) + один и тот же `sortId`/ключ.
Заголовок контейнера должен нести число блобов и их цель, иначе генератор
§2.6 не переиспользуется. Второй контейнер (`OnlineShaders/`, 44 блоба) —
параллельно `.shd`; у него **нет исходников** и его надо переписывать руками
(факт и перечень — §2.6).

**4. Драйвер компиляции.** DXC — единый компилятор двух целей (`-spirv` и
`-T vs_5_0`); обязательные флаги из спайков: `-Zpr` (row-major, иначе
`mul(v,M)` теряет перевод), `setEntryName` (DXC сохраняет имя функции HLSL),
прослойка SM3→SM6 как шаг драйвера (`shim.py` → `Src/ShaderCompiler`),
`-enable-16bit-types` не нужен. Отдельный класс правок, найденный замером:
**неизвестные по размеру массивы в `register(cN)`** у PostFX-шейдеров — DXC
выпускает для них `OpTypeRuntimeArray` внутри uniform-буфера, что для Vulkan
невалидно (runtime array разрешён только в SSBO); при генерации такие массивы
обязаны получить явный размер или уехать в SSBO (п.2).

**5. Инстансинг, шаг 2** (§2.10): `SV_InstanceID` вместо
`int InstanceId : BLENDINDICES0`; лексема шва `SetInstanceSource` решает, откуда
берётся индекс (поток сдвигается только в клиентской части — `ParticleFX`
привязывает физический поток 1 через `BindVertexBufferRaw`, сдвиг в
`BindVertexBuffer` в бэкенд не переезжает); `instanceData` из `c`-регистров —
в SSBO (п.2).

**6. Ориентация и Y.** NVRHI отдаёт viewport с отрицательной высотой →
`frontCounterClockwise = true` во **всех** пайплайнах (замер B0: `b0cull`
3072 пикселя против 0 без этого флага). Это единственное глобальное правило
по winding; отдельных инверсий в шейдерах не делаем.

**7. Кэш пайплайнов** (§1.4): 206 ключа на бой, 85 активных в кадре →
открытая хеш-таблица без LRU, компиляция вариантов синхронная (в кадре
переключений 85, но новых ключей почти не появляется: `semTotal` растёт только
при смене сцены). Raw-состояние в ключ не входит (+4 из 206).

**8. Окно и swapchain** — SDL3 3.2.30 (решение §3.x), ввод остаётся на
subclass `WndProc`, `SDL_main` не используется.

Открытые вопросы этапа 3 (решаются до/в начале этапа, не в ходе кода):

- **GPU для тестов.** На tiny10 только WARP: `HasDSM=0`, `caps=0x0`,
  depth-текстуры не создаются (`tools/dsm_probe`, §2.15 п.9) — путь теней и
  половина caps стендом не покрываются. Железо есть на хосте (RTX 3060, Linux,
  Vulkan 1.4), но клиент Windows-ный: нужен либо Windows-контур с NVIDIA,
  либо запуск клиента под Wine на хосте. **Решение пользователя принято
  2026-10-15: вариант «клиент под Wine на хосте»; проба сделана в тот же
  день** — клиент под Wine доезжает до боя, но 3D-сцена под wined3d рисуется
  чёрной (wined3d идёт через OpenGL). Для Vulkan-пути это не мешает:
  `NvrhiBackend` говорит с Vulkan напрямую, а Vulkan-смоук под Wine на
  RTX 3060 проходит целиком (риск 5). Процедура и цифры — §6 «Проба: клиент
  под Wine на хосте»; дефект чёрной сцены — открытый вопрос.
- **Порядок**: сначала `NvrhiBackend` + Vulkan и переключение по env, или
  сначала переделка констант/инстансинга (п.2, п.5) на D3D9? Замеры говорят в
  пользу второго: обе правки проверяемы на текущем стенде и без них бэкенд
  придётся переписывать. **Предложение агента (2026-10-15, решение за
  пользователем):** вторым — но по частям: сначала узкий кусок, который меняет
  лексику шва и корпус шейдеров (инстансинг шаг 2 + вынос динамических массивов
  из `c`-регистров в SSBO-форму + мультиблоб `.shd` и DXC-драйвер, всё
  проверяемо на tiny10), затем `NvrhiBackend` уже под готовый корпус; иначе
  каждая правка корпуса требует повторной валидации бэкенда, а layout из п.1
  предполагает уже сделанное разделение UBO/SSBO.
- **44 PostFX-шейдера**: переписать на HLSL (оценка: 1 смена) или оставить
  D3D9-путь для PostFX до этапа 4 (тогда D3D9 не удаляется целиком).


### 3.x GAP: swapchain — **решение: SDL3 (2026-10-14)**

В NVRHI `6b96fb03` **нет ни `ISwapchain`, ни `createSwapchain`** (проверено
по `include/` и по `doc/Tutorial.md`). Раньше это означало «приложение само
создаёт DXGI swapchain / `VkSwapchainKHR`» — отдельный кусок нативного кода,
единственный, где пришлось бы держать Vulkan/DXGI в обход NVRHI.

**Принято: эту часть закрывает SDL3 (3.2.x)** — окно, surface, кадры и
пересоздание при ресайзе отдаются SDL; нативного Vulkan-кода в клиенте не
появляется (кроме `createHandleForNativeTexture` для бэк-буфера, если NVRHI
потребует ручку вместо SDL-обёртки).

Что берёт на себя SDL:
- создание окна и message pump (`SDL_CreateWindow`, `SDL_PumpEvents`/`SDL_WaitEvent`);
- Vulkan surface: `SDL_Vulkan_CreateSurface` + `SDL_Vulkan_GetInstanceExtensions`
  (список расширений для `vkCreateInstance` — тоже оттуда);
- HWND для будущего DX12-пути: `SDL_GetWindowProperties` →
  `SDL_PROP_WINDOW_WIN32_HWND_POINTER`;
- пересоздание при ресайзе/смене монитора: `SDL_EVENT_WINDOW_RESIZED`,
  `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED`, `SDL_EVENT_WINDOW_MINIMIZED`,
  `SDL_EVENT_WINDOW_OCCLUDED`, `SDL_EVENT_DISPLAY_ORIENTATION` — на них же
  обрабатывается `VK_ERROR_OUT_OF_DATE_KHR`/`VK_ERROR_SURFACE_LOST_KHR`
  (минимизированное окно = нулевой размер, swapchain не создаётся);
- режимы показа: `isBorderless`/`vsyncCount` из `RenderMode` →
  `SDL_SetWindowFullscreenMode` + `SDL_WINDOW_BORDERLESS` вместо
  `EnumDisplaySettings`-валидации ресов; `SDL_GetFullscreenDisplayModes` —
  источник валидных ресов.

Границы (чтобы не раздуть задачу):
- **SDL забирает окно и pump, но НЕ ввод.** Клиент сам регистрирует класс окна и
  обрабатывает `WndProc` (мышь/клава/курсор/IME). Решение: SDL создаёт окно,
  существующая процедура ввода подсаживается subclass'ом
  (`SetWindowLongPtrW(GWLP_WNDPROC)`); перевод ввода на SDL-события — отдельная
  задача вне миграции рендера.
- `SDL_main` не используется: `SDL_SetMainReady()` (или `SDL_MAIN_HANDLED`),
  `WinMain` остаётся наш.
- Линковка: статика (`/MT`-совместимая сборка SDL3); если со статикой проблемы —
  `SDL3.dll` возим в `Bin/` по образцу `d3dx9_43.dll` (§2.15 п.3).
- Порядок: **этап 2** — `Renderer(HWND)` перестаёт принимать HWND (окно уходит в
  platform-слой, Win32-окно пока как есть); **этап 3** — это окно заменяется на
  SDL-овское, к нему цепляются surface/swapchain.
- SDL3 лежит в `Vendor/SDL3` (3.2.30, `Vendor/fetch_render_deps.sh`, §3.0) —
  собирается целью `render_vendor` (`./build_client_x64_wine.sh --vendor`),
  подсистемы кроме видео выключены.

Запасной вариант — SDL2 2.32.x: `SDL_Vulkan_CreateSurface` есть с 2.0.7, но
события ресайза/минимизации и fullscreen-режимы там хуже, а ветка 2.x —
maintenance-ветка. К SDL2 переходим только при упоре в Win7 или в поведение
SDL3-окон.

### 3.x Спайк A: HLSL -> SPIR-V (сделано 2026-10-13, `tools/spirv_probe`)

Главный риск этапа 3 был в шейдерах: 49 HLSL-файлов в диалекте SM3.0
(`sampler2D`/`samplerCUBE`, `register(cN)`/`register(sN)`, `tex2D*`,
`: POSITION`/`: COLOR`, `VPOS`). DXC (SM6) их не принимает.

Замер: **35 материалов × VS/PS = 70 единиц компиляции; без прослойки — 0/70,
с механической прослойкой — 67/70** (состояние на спайк A). После правок
исходников (§2.6 п.4, 2026-10-14) — **70/70**: `UIFlashMaterial` (запасной
`return`) и `TerrainElementMaterial` (развёртка цикла по пину); прослойка
dополнительно научилась разбирать `sampler NAME[N] : register(...)` и
`tex2D(arr[i], uv)`.

Исходные правки легаси-путь не касаются: регресс-прогон `D3DCompile` на
`vs_3_0`/`ps_3_0` по перестановкам затрагиваемых PIN'ов — 272 компиляции,
0 провалов.

Прослойка (`shim.py`, ~90 строк правил): combined-сэмплер → `Texture2D` +
один общий `SamplerState`; `register(...)` снимается (раскладку зададим сами);
`tex2D/texCUBE/tex2Dlod/tex2Dproj` → `.Sample/.SampleLevel` (proj — вручную
через `c.xy/c.w`); `POSITION` → `SV_POSITION`; `COLOR` выхода PS → `SV_TARGET`
(в структуре-возврате → `SV_TARGET0..n`, что заодно раскрывает MRT у
`BasicMaskMaterial`: цвет + глубина); `VPOS` → `SV_Position` **обязательно
float4** (Vulkan требует, `VUID-FragCoord-FragCoord-04212`).

Вывод: компилируемость — не риск. Риск — семантическая эквивалентность
(sampler-state на слот, раскладка `GlobalMasks.h` в uniform-буферы, Y-инверсия,
`clip()`, MRT, строково-векторный `mul`, offset-формы `tex2D`). Отвечает на это
спайк B: нарисовать один SPIR-V-шейдер через NVRHI на lavapipe и сравнить
пиксели с D3D9.

### 3.x Спайк B0: SPIR-V через NVRHI, пиксели против CPU-эталона (сделано 2026-10-13)

Нарисован один «материал» в диалекте клиента
(`tools/spirv_probe/shaders/b0_probe.hlsl`: `register(cN)`, `sampler2D`, `tex2D`,
`: POSITION`/`: COLOR`, `clip()`, `mul( float4(pos,1), WorldXf )`), переведён
прослойкой + DXC в SPIR-V и отрисован через NVRHI в 64x64 RGBA8; кадр сверен с
эталоном, посчитанным на CPU. Итог: **4096/4096 пикселей совпали** — и на хосте
(llvmpipe), и на tiny10 (lavapipe). Отдельный кейс с ненулевой `WorldXf`
(масштаб 0.5 + перенос в 4-й строке) — тоже 4096/4096.

По пути измерены пять вещей, которые определяют этап 3. Все они — **молчаливые**
отказы: вызовы возвращают успех/`nullptr` без сообщения, по логам не находятся.

1. **Имя точки входа.** DXC оставляет в SPIR-V имя HLSL-функции
   (`MainPass_VSMain`), NVRHI по умолчанию ищет `main` →
   `createGraphicsPipeline` = `nullptr`, а прямой
   `vkCreateGraphicsPipelines` отдаёт `VK_ERROR_UNKNOWN` (−13).
   Обязателен `ShaderDesc::setEntryName`.
2. **Динамический рендеринг.** NVRHI строит пайплайны с
   `renderPass = VK_NULL_HANDLE` (рендер-пассов в его API нет; есть
   `beginRenderPass`/`endRenderPass` поверх `vkCmdBeginRendering`). Одной
   фичи `VkPhysicalDeviceVulkan13Features.dynamicRendering` на Mesa **мало** —
   нужно включённое расширение `VK_KHR_dynamic_rendering` при создании
   устройства, иначе пайплайн не создаётся.
3. **Смещения binding'ов.** По умолчанию
   `VulkanBindingOffsets = {t:0, s:128, b:256, uav:384}` — слоты `s*` уходят в
   binding 128+, `b*` в 256+. DXC в нашей сборке раздаёт binding'и сам (0,1,2)
   → дескрипторы не совпадают с шейдером, выборка возвращает нули, кадр чёрный
   при полностью успешных вызовах. Лечится либо обнулением смещений в
   `BindingLayoutDesc`, либо генерацией SPIR-V с регистрами, согласованными с
   offsets. Для клиента второй и правильнее: `s0..s15` → 128..143,
   `b0..b13` → 256+, `t*` → 0+ (это и есть план раскладки `GlobalMasks.h`).
4. **Намотка.** NVRHI отдаёт viewport с отрицательной высотой
   (`VKViewportWithDXCoords`), т.е. Y развёрнут как в D3D9 — и из-за этого
   треугольники с намоткой D3D9 оказываются **тыльными**: при
   `cullMode=Back, frontCounterClockwise=false` не рисуется ничего
   (замер: 0 пикселей против 3072 при `frontCounterClockwise=true`).
   Этап 3: `frontCounterClockwise=true` во всех пайплайнах (или иная смена
   намотки, но одна и та же во всех).
5. **Раскладка матриц.** DXC по умолчанию кладёт `float4x4` в SPIR-V
   **столбцами**: матрицы клиента (D3D9: строки, перенос в `_41.._43`) читаются
   транспонированными, `mul(v, M)` теряет перенос (замер: квадрат уезжает в
   центр вместо правого верхнего квадранта). DXC-флаг `-Zpr` оставляет память
   строчной — и `mul(v, M)` совпадает с D3D9 пиксель в пиксель. `-Zpr` зашит в
   генератор (`gen_b0_shader.py`).

Что B0 **не** проверял (дальше по этапу 3): sampler-state на слот (в пробе один
сэмплер на всё), mipmap-фильтрацию и offset-формы `tex2D`, MRT
(`BasicMaskMaterial` — цвет + глубина), раскладку `GlobalMasks.h` в
uniform-буферы (16 сэмплеров, таблица из 261 текстуры), Y-инверсию при рендере
в RT с последующим чтением (пост-обработка клиента).

Запуск: `tools/nvrhi_probe/build_probe_linux.sh` (хост) /
`build_probe_wine.sh` + `setup_tiny10_vulkan.sh` (tiny10). Регенерация SPIR-V и
заголовка: `tools/spirv_probe/gen_b0_shader.py`.

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

1. **Генераторов больше нет как блокера** (2026-10-14): `.shd` —
   `Src/ShaderCompiler/`, `MaterialSpec.{h,cpp,inl}` — `Src/DBCodeGen/`
   (перегенерация воспроизводит дерево побайтово). 36 uber-файлов, 49 966
   вариаций, «GENERATED AUTOMATICALLY» в заголовках. Остаётся расхождение
   остального generated-слоя с текущей версией `DBCodeGen` (10 файлов по
   содержимому, 72 по EOL) — его не перегенерировать, пока не согласовано
   с редакторной веткой.
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
   **Вариант (а) реализован 2026-10-15.** Причина, почему модули «пропали»
   (`nvidia-smi` работал раньше): загружено ядро `7.0.0-34-generic`, а
   prebuilt-пакеты `linux-modules-nvidia-595-open-*` стояли только под
   `7.0.0-28`/`7.0.0-31` → `modinfo nvidia` = «Module not found», `lsmod` пуст.
   Поставить prebuilt под `-34` из архива нельзя: он жёстко привязан к
   `nvidia-kernel-common-595 (= 595.91.07)`, которого в архиве уже нет (есть
   595.99.02 и 595.58.03). Поставлен `nvidia-dkms-595` (595.99.02): он подтянул
   весь userspace-стек 595.84 → 595.99.02 и собрал модули под `-31`/`-34`/`-38`;
   **перезагрузка не потребовалась** — `modprobe nvidia nvidia-modeset
   nvidia-uvm` поднимает GPU на текущем ядре. Побочный эффект: apt удалил
   мета-пакет `nvidia-driver-595-open` и prebuilt-пакеты модулей (с dkms они
   конфликтуют), дальше модули на новые ядра собирает dkms — следить через
   `dkms status` после обновления ядра. Проверка: `nvidia-smi` → RTX 3060 12 ГБ /
   595.99.02; `nvrhi_probe` выбирает `NVIDIA GeForce RTX 3060` (Vulkan 1.4.329),
   все спайки B0 проходят, `b0cull` подтверждает `frontCounterClockwise=true` и на
   реальном GPU (3072 пикселя против 0). Что снимает: замеры производительности
   этапа 3 возможны (хост, Linux/Vulkan). Чего не снимает: DX12-путь проверять
   негде (Windows+NVIDIA нет, tiny10 = WARP D3D11) — DX12-ветка бэкенда остаётся
   непроверенной до появления железа (вариант (в)).
   **Wine как среда Vulkan-смоука: была закрыта, снова открыта (2026-10-15,
   два замера).** Первый замер: `tools/nvrhi_probe/build/nvrhi_probe.exe`
   под Wine на живом GPU падает на `vkCreateInstance` с `res=-7`
   (`VK_ERROR_INCOMPATIBLE_DRIVER`); `VK_LOADER_DEBUG=error` даёт причину:
   `loader_validate_instance_extensions:
   Extension VK_WINE_nulldrv_surface not found in list of known instance
   extensions` — при пустом `DISPLAY` winevulkan выставляет свой null-driver
   surface и передаёт его host-загрузчику, который его не знает.
   Вины NVIDIA нет: тот же ICD (`/usr/share/vulkan/icd.d/nvidia_icd.json` →
   `libGLX_nvidia.so.0`) в нативном Linux-пути работает целиком.
   **Повторный замер в тот же день (после загрузки nvidia-модулей): вывод
   снимается достаточно `DISPLAY=:97`** (это Xvfb 1280×1024, настоящий
   X-сервер не нужен): `vkCreateInstance со surface-расширениями` OK,
   `2 устройств(а); выбрано: NVIDIA GeForce RTX 3060 (api 1.4.329)`,
   `vkCreateDevice ext: swapchain + synchronization2 + dynamic_rendering`,
   далее `win32: окно 128×128` → `VkSurfaceKHR` → `VkSwapchainKHR 120×94,
   картинок 3, формат 44` → `vkQueuePresentKHR` PASS, `итог: d3d11=1
   vulkan=1`. Все спайки B0 на RTX 3060 под Wine тоже проходят.
   Следствие: Windows-клиент под Wine на хосте с реальным GPU — кандидат на
   среду разработки/замеров этапа 3 (без гонки исходников на tiny10 и без
   растера lavapipe). Не проверено: что **сам клиент** под Wine запускается
   (раскладка `Game/Bin` + `Data` + `Packs/result.pile`, `config.txt`,
   D3D9-часть через wined3d, ввод/окно) — это отдельная проба, см. §3.z
   «Открытые вопросы».
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
| `Render/Shaders/FullScreenMaterial.hlsl` | `coeffMap: register(s0)` → `register(s3)` (см. §2.7). Инертна в **репозиторном контенте**: `Data/Shaders/UberShaders/*.shd` не перегенерированы. На тестовом стенде правка активна — там лежит перегнанный `FullScreenMaterial.shd` в `zzzresult.pile` (см. §2.6). |
| `PF_GameLogic/PFPostFXParams.cpp` | гейт `!HasVT() && !HasR2VB()` убран (чтение текстур в VS гарантировано SM3.0); `TextureVtx::Sizes(1,1)`. |
| `Render/renderer.cpp`, `shadercompiler.cpp`, `multishader.cpp` | SM2.0-путь удалён: `forceSM20Usage` dev-var, `#ifdef FORCE_SM20` клампы, `FORCE_SM20`/`ps_2_0`/`vs_2_0` в рантайм-компиляторе, суффикс `.shd2` в `MultiShader::OpenFile`. `bSupportSM30` оставлен — это гейт запуска (`PW_Client/Game.cpp::CheckHardwareCompatibility`), не путь рендера. |

### Сделано (этап 1, шейдерный путь — закрыт 2026-10-13)

| Область | Изменение |
|---|---|
| `Tools/WineClientBuild/parse_vcproj.py`, `Tools/ClientCMake/gen_cmake.py` | `ShaderCompiler` — 26-й проект CMake-графа (`EXTRA_TOOLS`/`TOOL_OBJS`, exe-цель `ShaderCompiler_tool` с `OUTPUT_NAME ShaderCompiler`, линковка как у `PW_Game` + свои либы). В `PW_Game` его объекты не попадают. |
| `Src/ShaderCompiler/multythread/mtworkerlite.cpp` | `_asm mfence` → `MemoryBarrier()` (x64 не поддерживает inline `__asm`, C4235). |
| `Src/ShaderCompiler/ShaderCompiler.cpp`, `UberShaderDesc.{h,cpp}` | генератор переведён с `D3DXCompileShader` на `D3DCompile`; `D3DCompile` грузится динамически (`d3dcompiler_47/48/50/43`) — SDK-`d3dcompiler.lib` привязывает к `D3DCOMPILER_43.dll`, которого на стенде нет. Без импорта D3D-DLL. |
| `Src/ShaderCompiler/ShaderCompiler.cpp` | оба писателя `.shd` делают `fclose` (без него убитые прогоны оставляли 0-байтовые/обрезанные контейнеры); если хоть одна вариация не скомпилировалась — `.shd` целиком не пишется (иначе shipping-сборка падала на null-буфере). |
| `Shaders/UIButtonMaterial.hlsl`, `UIFontMaterial.hlsl`, `UIGlassMaterial.hlsl` | `VS_OUTPUT Output = (VS_OUTPUT)0;` — fxc отвергает частично инициализированный `VS_OUTPUT` (X4000), старый D3DX пропускал. |
| `Shaders/BasicMaskMaterial.hlsl` | `uvSpeed = 0;` (запись в register-глобал) → локальная тень `float2 uvSpeed = 0;`; fxc даёт X3025, флагом не снимается. |
| `Shaders/FullScreenMaterial.hlsl` | `coeffMap` объявлен `sampler2D` (для `tex2Dlod` в VS). |
| `tools/hlsl_compile_test/` | одиночный прогон `D3DCompile` (динамическая загрузка, свой `ID3DInclude`, `--def`/`--flags`) — показал, что текущие `.hlsl` компилируются современным `D3DCompile`, но не `d3dx9_43` (E5017 на `tex2Dlod` в `vs_3_0`). |
| `tools/shader_pile/` | `gen_shaders.sh` (перегонка на стенде), `build_pile.sh`, `mkpile.py` — свой сборщик/читатель pile: готовый `FilePileCompiler.exe` (x86 VS2008, требует MSVCR90/MSVCP90/CrashRpt/zlib1) на урезанной Windows не запускается. |
| Стенд tiny10 | `Packs\zzzresult.pile` (168 125 Б, 35 материалов) — имя не случайное: `ScanForPiles()` сортирует `*.pile` по убыванию, поиск до первого совпадения, поэтому `zzz*` перекрывает `result.pile`/`zpatch_fev.pile`. Отдельными файлами в `Data/` нельзя: `PileFileSystem` регистрируется раньше `WinFileSystem(Data)`. |

Гейты: E2E на tiny10 с перегнанным контентом — login → lobby → game service →
mapload → `Switch to EGameState::Finished by 3`, дампов нет, строк про
битый pile/CRC/шейдеры нет (прогоны `2026.10.13-03.21.11`, `03.39.03`,
`08.45.29`). Контрольный A/B: инвертированный байт в теле `BasicMaterial.shd`
внутри нашего pile вешает клиент сразу после `Got login reply` (модалка
`ShowMessageAndTerminate` из SSH не видна) — значит запись читается, новый DXBC
принимается.

Состояние стенда (2026-10-13, вечер): обычный (не stats) билд HEAD —
`PW_Game.exe` 15 175 680 Б — + `zzzresult.pile` из 35 материалов; прогон
`2026.10.13-12.06.41`: полный цикл до `Finished`, ноль ошибок, ноль дампов.
Stats-сборки (15 183 360 / 15 185 408 Б) для замеров §1.2.1 ставились временно
и сняты.

Грабля для инструментальных сборок: `PW_EXTRA_DEFS` попадает в командную строку
**каждой** TU, поэтому stats-сборка = полная пересборка (~35 мин под Wine), и
обычный артефакт после неё тоже приходится пересобирать с нуля. Отдельного
build-каталога под инструментальные сборки скрипт не предусматривает — при
частых замерах это стоит починить.

**Что не сделано:** репозиторные `Data/Shaders/UberShaders/*.shd` по-прежнему
старые (перегенерированный контент живёт только на стенде). Решение, куда их
класть (репо vs контентный пайплайн `content_pwc`/`pwc-gitupdates`), не принято.

### Сделано (спайк B0: SPIR-V через NVRHI, 2026-10-13)

- `tools/spirv_probe/shaders/b0_probe.hlsl` — тестовый материал в диалекте
  клиента; `gen_b0_shader.py` — прослойка + DXC (`-Zpr`) + разбор SPIR-V +
  генерация `tools/nvrhi_probe/b0_shader_spv.h`.
- `tools/nvrhi_probe/probe.cpp` — `SmokeShaderPipeline`: пайплайн из SPIR-V,
  вершинный буфер, uniform, текстура 8x8, сэмплер, отрисовка, readback и
  сравнение с CPU-эталоном; плюс лестница `b0bare`/`b0vb`/`b0tex`/`b0triv`,
  кейс ненулевой матрицы `b0xfm` и замер намотки `b0cull`.
- Результат: 4096/4096 пикселей совпали (host llvmpipe и tiny10 lavapipe);
  найденные молчаливые отказы и обязательные настройки — в §3.x (имя точки
  входа, `VK_KHR_dynamic_rendering`, `VulkanBindingOffsets`,
  `frontCounterClockwise=true`, `-Zpr`).

### Сделано (этап 2, снимок кадра без D3DX — закрыто 2026-10-14)

| Область | Изменение |
|---|---|
| `Render/renderbackend.h`, `d3d9backend.{h,cpp}` | `ReadSurface` (staging-копия для пула DEFAULT + `LockRect`, кроп по строкам) и `GetBackBuffer`; формат пикселя — явный `EBackendPixelLayout` |
| `Render/renderpixellayout.h` | новый заголовок: `plBGRA`/`plRGBA` — вынесен из интерфейса намеренно, чтобы читатель пикселей не зависел от D3D9/Windows/stdafx |
| `Render/screenshot.{h,cpp}` | свои png/bmp/dds вместо `D3DXSaveSurfaceToFile*`; `ScreenshotFormatFromName` (jpg/hdr → png), `WriteScreenshotFile`, `BuildDdsImage` |
| `Render/renderer.{h,cpp}` | `SaveFrameToFile` — общий путь (бэк-буфер или поверхность сцены, кроп, отброс альфы); `SaveScreenshotToFile` — обёртка; `GetBackBuffer`/`CreateOffscreenPlainSurface`/`D3DXLoadSurfaceFromSurface`/`D3DXSaveSurfaceToFileA` из `renderer.cpp` убраны |
| `Render/smartrenderer.{h,cpp}` | `DumpScreenshot` — через `SaveFrameToFile`; `DumpScreenshotToMemory` удалена (единственный читатель — `EditorNative`, вне графа); в `OnFrameStart()` — хук `auto_screenshot` (кадр N и N+100: scene/bb/crop) для проверки readback'а на устройстве |
| `PF_GameLogic/PFRenderUtils.cpp` | дефолт `screenshot_ext` `jpg` → `png`; снят Wine-гейт снимка (обход битого `D3DXSaveSurfaceToFile` в Wine <1.5.6) |
| `Render/Render.vcproj` | `screenshot.{h,cpp}`, `renderpixellayout.h` |
| `tools/screenshot_test/` | нативный смоук писателя (`main.cpp` + `check.py`): узоры 37×13, свой декодер png/bmp/dds + независимая сверка через PIL |

Детали и найденные смоуком баги (CRC PNG, смещения DDS-заголовка, смещения BMP
в самом декодере теста) — §2.13. Гейт: x64 (Wine) + E2E tiny10 (`2026.10.13-22.42.41`,
`finish 7e447f97…`) + скриншот-гейт (кадр боя полный, 5,3% между кадрами через
26 с). Readback на устройстве проверен хуком `auto_screenshot` (прогон
`2026.10.13-23.06.47`, `finish 4cd0a613…`): crop == область бэк-буфера пиксель в
пиксель, кроп ограничен, `scene` == `bb`, кадры на N и N+100 различаются (§2.13).

### Сделано (этап 2, состояния внутри `Render` — закрыто 2026-10-14)

| Файл | Было (сырых записей через `GetDevice()`) | Стало |
|---|---|---|
| `Render/FlashRenderer.cpp` | 50 `SetRenderState` (SRCBLEND/DESTBLEND/BLENDOP/BLENDOPALPHA, STENCIL*, COLORWRITEENABLE, ALPHABLENDENABLE) | 0 — всё через `GetStatesManager()->SetStateDirect` |
| `Render/uirenderer.cpp` | 2 `SetRenderState` (ALPHATESTENABLE вкл/выкл) | 0 |
| `Render/smartrenderer.cpp` | 1 `SetRenderState` (ALPHABLENDENABLE в `RenderDeferred2D`) | 0 |
| `Render/ShadowMap.cpp` | 1 `SetSamplerState(0, D3DSAMP_BORDERCOLOR, 0)` | 0 — `SetSamplerStateDirect` |

Прямые вызовы устройства `GetDevice()->` внутри `Render`: **96 → 42**, из них 7 —
сам `renderstatesmanager.cpp` (это законный владелец записей). Записей
состояния/сэмплеров мимо менеджера не осталось.

Гейт: x64 (Wine) + E2E tiny10 (`2026.10.13-23.15.03`, `finish a7c47f6e…`,
дампов нет) + **клиентские снимки** (хук `auto_screenshot`, §2.13): кадр и UI
отрисованы целиком, расхождения с предыдущим прогоном — только в зонах, где
изменяется состояние боя (счётчики, миникарта, партиклы): панель портрета 7,5%,
хотбар 0,6%, стрелки слева 0,0%, портрет 0,9% (max|d|=4). Точная сверка
«до/после» пиксель-в-пиксель между разными прогонами невозможна (бой не
детерминирован) — см. «Сознательные отклонения», п.3.

### Сделано (этап 2, тень состояний — 2026-10-14)

| Файл | Было | Стало |
|---|---|---|
| `Render/renderstatesmanager.{h,cpp}` | состояние пишется в устройство из 25 мест, прочитать его можно только с устройства | все записи через приватную `ApplyState()` (устройство + тень `stateShadow[256]`/`stateShadowKnown[256]`); публичная `GetStateDirectValue()` — ответ из тени, неизвестное один раз подтягивается с устройства; тень сбрасывается в `Init()`/`OnDeviceLost()`; `DumpStates()` читает тень |
| `Render/ShadowManager.cpp` | save/restore `DEPTHBIAS`/`SLOPESCALEDEPTHBIAS` через `pDev->GetRenderState`/`SetRenderState` (4+2) | `statesManager.GetStateDirectValue()` / `SetStateDirect()`; локальная переменная `pDev` из функции убрана |
| `Render/batch.cpp` (`ZeroScissor`, `#ifndef _SHIPPING`) | `pDevice->GetRenderState`/`SetRenderState(D3DRS_SCISSORTESTENABLE)` (2+1) | через менеджер; `Get/SetScissorRect` остались на устройстве (прямоугольник отсечения — не состояние, кэш viewport/scissor ещё D3D9-типизирован, §2.8) |

Живых записей и чтений состояния через устройство в `Render` (кроме менеджера и
реализации D3D9-бэкенда) не осталось; всего живых обращений к устройству там же —
**60** (ресурсы/поверхности, `RenderDeferred2D`, запросы возможностей,
`GetViewport`).

Гейт: x64 (Wine) + E2E tiny10 (`2026.10.13-23.47.31`, `finish 823a64bd…`,
дампов нет, счётчики штатных `FAILED`/`LoadTextureFromFileImpl`/`FormulaVM` —
столько же, сколько в предыдущем прогоне) + клиентские снимки (`auto_screenshot`,
кадры 3000/3100). Сверка кадров между прогонами показала, что **шум между
запусками того же стенда (~11% отличающихся пикселей на одном и том же номере
кадра, «грубых» отличий >64 — 0,13–0,33%) сопоставим с любой разницей до/после**:
A-B 11,73% / B-C 10,67% / A-C 11,41% — сигнала регресса нет; кадр глазами — сцена,
тени от построек и UI без изменений (ни acne, ни отрыва тени от объекта).

**Повторное измерение шума (2026-10-15, шаг 4, лог `2026.10.15-00.55.16`).**
Тот же стенд, те же кадры: `bb_3000` vs `bb_3100` (один прогон) = 5,13% / 1,09%
грубых; `bb_3000` против эталона шага инстансинга = 5,44% / **0,35%** грубых
(правый край UI 0,01%, низ 0,65%, верх 2,41%, лево 4,38%). Верхняя граница
полосы «грубых» между прогонами — не 0,33%, а ~0,35%: в этом прогоне вырос и
шум внутри прогона (4,65%→5,13% по кадру целиком, `ui_top` 1,77%→2,64%), то есть
разница идёт за стендом (WARP), а не за правкой. Для шага 4 это не регресс
впрямую: строка `ConfigPins` (все входы рендера, которые читает клиент)
совпала с эталоном дословно — см. §2.5.

### Сделано (этап 2, ре-аллокация вместо device-lost — закрыто 2026-10-14)

| Было | Стало |
|---|---|
| `Render/DeviceLost.{h,cpp}` | `Render/Reallocate.{h,cpp}` (`git mv`, обновлены `Render.vcproj` и `Render/RendererUtils.component`) |
| `DeviceLostHandler` | `ReallocateHandler` |
| `OnDeviceLost()` / `OnDeviceReset()` | `OnResourcesRelease()` / `OnResourcesReallocate()` |
| `NotifyDeviceLost()` / `NotifyDeviceReset()` | `NotifyResourcesRelease()` / `NotifyResourcesReallocate()` |
| `DeviceLostWrapper`, `DeviceLostHandlerLoadable` | `ReallocateWrapper`, `ReallocateHandlerLoadable` |
| `DeviceDeleteHandler` | без изменений (это про объект устройства, не про размер кадра) |

Затронуто 32 файла (кроме `Render`: `PF_Core`, `PF_GameLogic`, `Scene`,
`Terrain`, `PW_Client`). Лексика D3D9-устройства (`bDeviceLost`,
`RecoverFromDeviceLost`, `TestCooperativeLevel`) сохранена намеренно: это
реализация D3D9-пути, она уходит на этапе 4.

**Правка семантики, а не только имён:** `Renderer::ResetDevice()` (путь
`Renderer::Start()` → create-or-reset) раньше **не уведомлял обработчики** —
уведомления были только в `RecoverFromDeviceLost()` и `InvalidateResources()`.
Сейчас `ResetDevice()` = `NotifyResourcesRelease()` → `pDevice->Reset()` →
`OnDeviceChanged()` → `NotifyResourcesReallocate()`; при неудачном `Reset()`
состояние «ресурсы отпущены» сохраняется и разбирается повторным `Start()` либо
восстановлением на ближайшем `Present()`. В текущем клиенте путь не исполняется
(`Start()` вызывается один раз на старте, смены разрешения на лету нет) — правка
кладётся заранее, под SDL-ресайз этапа 3 (§3.x GAP: swapchain).

Гейт: x64 (Wine, 614 шагов графа — переименование задело `PF_Core`,
`PF_GameLogic`, `Scene`, `Terrain`, `PW_Client`), `check_exe.py` OK
(15 193 088 B) + E2E tiny10 (`2026.10.14-00.53.38`: AutoShot ×2,
`Switch to EGameState::Finished`, `finish 47682709…` в dev-бэкенде, дампов нет,
счётчики `LoadTextureFromFileImpl`/`FormulaVM` — как в предыдущем прогоне) +
клиентский снимок: кадр и UI целы, «грубые» отличия от предыдущего прогона
локализованы в светящемся портале (анимация партиклов), статичные зоны UI — 0.

### Сделано (этап 2, пулы — первый шаг, 2026-10-14)

| Файл | Что изменилось |
|---|---|
| `Render/renderbackend.h` | нейтральный `EBackendPool` (6 значений, порядок = `PoolType`) + `CreateVertexBuffer` / `CreateIndexBuffer` в `IRenderBackend` |
| `Render/d3d9backend.{h,cpp}` | реализация; `D3DPoolFromBackend()` — единственное место, где пул становится `D3DPOOL`+`usage`; там же `NI_STATIC_ASSERT` паритета enum'ов |
| `Render/dxutils.cpp` | `CreateVBInternal`/`CreateIBInternal` зовут `GetBackend()->Create*`; прямых `CreateVertexBuffer`/`CreateIndexBuffer` через устройство — 0 |

Замечания по реализации: `NI_STATIC_ASSERT` в этом коде разворачивается в блок
(`{ StaticAssert<…> ERROR_msg; … }`) — работать на уровне файла он не может,
поэтому паритет проверается внутри функции; `renderer.h` объявляет
`IRenderBackend` только forward-объявлением, поэтому `renderbackend.h` входит
лишь в несколько TU (пересборка после правки seam'а — 5 шагов, а не 500).

Гейт: x64 (Wine) + `check_exe.py` OK + E2E tiny10 (`2026.10.14-01.02.25`,
`finish 270728c4…`, дампов нет, AutoShot ×2, счётчики `LoadTextureFromFileImpl`
/`FormulaVM` как в предыдущих прогонах) + клиентский снимок: 11,97% отличий от
прошлого прогона при 0,97% «грубых» — тот же уровень шума, что между двумя
прогонами без правок (12,48%/0,91%), статичные зоны UI — 0,00%.

### Сделано (этап 2, блокировка буферов через seam — закрыто 2026-10-15, `d8e3f45eb`)

| Файл | Что изменилось |
|---|---|
| `Render/locktype.h` | `ERenderLockType` — словарь режимов блокировки; добавлен `LOCK_NOSYSLOCK` |
| `Render/renderbackend.h` | `LockVertexBuffer`/`UnlockVertexBuffer`/`LockIndexBuffer`/`UnlockIndexBuffer` в `IRenderBackend` (раздельно VB/IB — разные COM-интерфейсы в D3D9) |
| `Render/d3d9backend.{h,cpp}` | реализация через `IDirect3D*Buffer9::Lock/Unlock`; карты режимов в бэкенде нет |
| `Render/renderflagsconverter.cpp` | `ConvertRenderLockType()` — единственная карта режимов |
| `Render/dxutils.{h,cpp}` | `LockVB`/`LockIB`/`FillVB`/`FillIB` идут через seam; внутренний `LOCK_FLAGS` удалён; кольцо `SharedD3DBufferST` остаётся клиентской политикой |
| 20 потребителей (`Render`, `Scene`, `Terrain`, `PF_GameLogic/Minimap`) | вызовы переведены на `LOCK_*`; аудит `\->Lock(` по `Src/` — 0 обращений к буферам устройства вне бэкенда |

 staging-кольцо (курсор + фенсы + пересоздание на `Resize`) — реализация этих
 четырёх методов в `NvrhiBackend`, этап 3. Мёртвые `Render/mesh.cpp` и
 `Scene/ModelLoader.cpp` (не в графе, ссылаются на необъявленный
 `IVertexBuffer`) — при аудитах исключать.

### Сделано (шаг 1, вендоринг: дерево стало самодостаточным — 2026-10-15, `3ea42f9ac`)

Глобальное правило `.gitignore` `**/*.cmake` (заведено под сгенерированные
артефакты build-деревьев) глушило и привезённые исходники: 28 файлов SDL3
(в т.ч. `cmake/sdltargets.cmake`, `cmake/GetGitRevisionDescription.cmake`,
`cmake/sdlchecks.cmake`, `include/build_config/SDL_build_config.h.cmake`, из
которого SDL генерирует свой конфиг), 3 NVRHI, 1 DirectX-Headers. На хосте они
лежали untracked, поэтому Wine-гейт (120/120) этого не замечал; на нативной
сборке конфигурация падала:

```
CMake Error at Vendor/SDL3/CMakeLists.txt:75 (include):
  include could not find requested file: Vendor/SDL3/cmake/GetGitRevisionDescription.cmake
CMake Error at Vendor/SDL3/CMakeLists.txt:80 (SDL_DetectCompiler):
  Unknown CMake command "SDL_DetectCompiler".
```

Снято пятью точечными negation-правилами по каталогам вендоринга — общим
`!**/*.cmake` не меняем, чтобы сгенерированные в build-деревьях модули остались
игнорируемыми. Проверка самодостаточности: `git ls-files -o -i
--exclude-standard` и `git ls-files -o --exclude-standard` по четырём новым
вендор-каталогам — оба 0.

### Сделано (этап 2, `ConstBlock` — закрыто 2026-10-15, шаг 7 очереди)

Тень констант становится единственным источником чисел для Vulkan-пути. Что
сделано:

| Файл | Изменение |
|---|---|
| `Src/Render/conststore.h` | `ConstStore` дополнен: `revision()` (monotone счётчик изменённых по значению регистров), `touched`-счётчик, `TakeDirty(EConstStage, first, last, mask, dst, dstRegs)` — выгрузка диапазона + маска изменённых и сброс грязности; `ConstBlock { const float* data; unsigned firstReg, regCount; unsigned long long revision; }` |
| `Src/Render/conststore.cpp` | сравнение по значению при записи (не «вся запись = грязный»), битовая грязность по регистрам, `TakeDirty` |
| `Src/Render/renderbackend.h` | `virtual const ConstBlock* GetConstBlock(EConstStage) = 0;` — контракт: бэкенд обязан вернуть блок, побитово совпадающий с тем, что сейчас видит устройство |
| `Src/Render/d3d9backend.{h,cpp}` | `D3D9Backend::GetConstBlock` — ленивое чтение из устройства (`GetVertexShaderConstantF`/`GetPixelShaderConstantF` по диапазону тени) в свой буфер; при расхождении с тенью — `DebugTrace` и возврат тени (на D3D9 тень — не источник истины, это сигнал о обходном пути) |
| `Src/Render/smartrenderer.cpp` | под `USE_RENDER_SHADER_STATS`: снимок `ConstStore` (диапазон, размер блока, writes/touched/changed/`rev`) + верификация тени об устройство (`VerifyConstStoreAgainstBackend`) по dev-var `verify_const_block` |
| `tools/const_layout/const_layout.py` | новый инструмент: раскладка `c0..c255` по `GlobalMasks.h` — имена, диапазоны, пересечения, покрытие явными размерами |

Гейты: Wine x64 120/120 + `check_exe` OK (`build/PW_Game.exe` 15 201 280 Б),
нативная сборка на tiny10 (MSVC 14.44) RC=0, `check_exe` OK (15 196 160 Б,
Characteristics 0x0022, манифест 1); E2E на tiny10 с инструментальной сборкой —
`Finished by 3`, asserts 0; `verify_const_block 1` — 18 снимков × 2 стадии,
**mismatch=0** (VS 235 регистров, PS 221, `hr=0x0`). Визуальный гейт не требовался:
изменения не влияют на картинку (тень пишется рядом с существующими вызовами,
поведение D3D9-пути не меняется).

Числа и вывод — в §2.9 (они опровергли прежний расчёт про «~270 изменённых за
окно» и перенесли вес с «ключить по изменению» на «маленький грязный диапазон +
кольцевой буфер»).

Остаток по шагу: нет.

### Сделано (этап 2, ключ пайплайна + счётчик — закрыто 2026-10-15, шаг 8 очереди)

Формат ключа пайплайна определён и **измерен на живом D3D9** — это то, с чем
этап 3 заходит в `NvrhiBackend` (см. §1.4).

| Файл | Изменение |
|---|---|
| `Src/Render/renderstatesmanager.h` | `GetSamplerShadowHash()`, `GetStencilShadowKey()`, `GetRenderStateShadowHash()` — три готовых хеша теней (сэмплеры, stencil, полная D3DRS-тень) |
| `Src/Render/renderstatesmanager.cpp` | реализация: сэмплеры — обход `m_samplers` (16 × `D3DSAMPLERSTATETYPE`), stencil — обход `m_stencil`, raw — обход `m_state`; хеши инкрементальные по мере записи состояний |
| `Src/Render/smartrenderer.cpp` | `NotePipeline()` — сборка ключа перед рисованием (пара шейдеров, декларация вершин, `RenderState`, сэмплеры, stencil, тип примитива, инстансинг, формат RT[0] и depth) + счётчики `pipeSemMap`/`pipeRawMap` (кадр и сессия) и `pipeDrawsFrame`/`pipeDrawsTotal`; снимается в `OnFrameStart` (`PipeStats:` под `USE_RENDER_SHADER_STATS`); форматы RT/depth ловятся в `BindRenderTargetColor`/`BindRenderTargetDepth`; **фикс**: `BindRenderTargetColor(i, NULL)` больше не читает `GetDesc()` у пустой поверхности |

Всё, кроме обнуления форматов и защиты от `NULL`, живёт под
`USE_RENDER_SHADER_STATS` (`-DPW_RENDER_SHADER_STATS`, в обычной сборке не
компилируется) — E2E-гейт обычной сборки остаётся в силе.

Гейты: Wine x64 ИТОГ OK + `check_exe` OK (`build/PW_Game.exe` 15 201 280 Б);
нативная сборка на tiny10 (MSVC 14.44) RC=0, `check_exe` OK (15 196 160 Б,
Characteristics 0x0022, манифест 1, RT_MANIFEST id 1); E2E на tiny10 —
`ConfigPins` дословно совпал с эталоном (`MaxVertexShaderConst=256 IsNullRef=0
caps=0x0 pins=0`), login `Success(5)` → lobby → `gamesvc_00` →
`Switch to EGameState::Finished by 3`, asserts/FAILED 0; **визуальный гейт** —
bb_3000 против эталона шага 4: 2,33% отличий, **0,15% грубых (>64)** — внутри
полосы шума прогонов (0,13–0,35%, §6 ниже).

Числа и вывод — §1.4. Следующий шаг — предполётный дизайн этапа 3
(макет дескриптор-сетов, `.shd` с несколькими блобами, драйвер компиляции,
инстансинг шаг 2, SDL3-окно), потом зачистка (§Этап 1, остаток).

### Сделано (предполётный дизайн этапа 3 — 2026-10-15, §3.z)

Дизайн зафиксирован в §3.z до написания `NvrhiBackend`. Опора — новый
инструмент `tools/shader_budget/shader_budget.py` (stdlib + `dxc` из
`tools/spirv_probe/vendor`, скачивается `fetch_dxc.sh`): 35 материалов из
`Data/Shaders/MaterialSpec.xml` компилируются DXC'ом в SPIR-V (прослойка
`shim.py`, пины из `MaterialSpec.xml`), полученный SPIR-V разбирается без
внешних библиотек (`OpName`/`OpMemberName`/`OpDecorate`/`OpType*`/`OpAccessChain`)
и считается бюджет одного пайплайна: текстуры/сэмплеры/UBO/SSBO, их размеры,
`set`/`binding`, байты констант, факт неконстантной индексации члена UBO.
Опции `--pins N`, `--only NAME`, `--dump`, `--no-shim`. 70/70 единиц (pins=0),
69/70 (pins=1) — не компилируется только `ParticleFXMaterial` vs (массив
неизвестной длины в `register(cN)`).

Что это изменило в плане: бюджет дескрипторов оказался **не ограничением**
(макс 7 текстур + 1 сэмплер + 1 UBO на стадию, всего 10 различных
`(set,binding)` по корпусу) — вместо тревоги про 8 сетов в §3.w встал
вопрос размера и частоты загрузки uniform-буфера (3744 Б на рисование);
разделение UBO/SSBO (§3.z п.2) получило точный перечень массивов с размерами;
найдены два класса провалов генератора — runtime-массивы в UBO и
динамическая индексация компонент `float4`.

Открытые вопросы (нужны решения пользователя, §3.z в конце): тестовая GPU-
машина для этапа 3 (tiny10 = только WARP, путь теней не покрывается);
порядок «сначала переделка констант/инстансинга на D3D9 vs сразу бэкенд»;
судьба 44 PostFX-шейдеров без исходников (переписывать или держать D3D9).

### Сделано (зачистка: мёртвое, `--sm20`, RECT-слой публичного API — 2026-10-15)

| Файл | Правка |
|---|---|
| `Src/Render/mesh.{cpp,h}`, `Src/Scene/ModelLoader.{cpp,h}` | удалены (в дереве не собирались; `mesh.h` включал отсутствующий `submesh.h`) |
| `Src/ShaderCompiler/parse.{h,cpp}` | удалены `isV20`, `GetVersion()`, константа `--sm20`, формат в `Print()` |
| `Src/ShaderCompiler/ShaderCompiler.cpp` | `macroV20`/`macroV20_dummy`, член `isV20`, ветки `vs_2_0/ps_2_0` (теперь всегда `vs_3_0/ps_3_0`), параметр `isV20` у `BuildShaderMatrix`/`WriteMatrix`, `ext = ".shd2"` |
| `Src/ShaderCompiler/UberShaderDesc.{h,cpp}` | `Read()/Write()` без `isV20`, `ext = ".codes"`, вырезан 40-строчный мёртвый блок чтения `.codes/.codes2` |
| `Src/Render/renderer.{h,cpp}` | `SetScissorRect(const BackendRect*)`, `SaveFrameToFile(…, const BackendRect*, …)`, объявление `struct BackendRect;` вместо include `renderbackend.h` |
| `Src/PF_GameLogic/PFRenderInterface.cpp` | два места scissor-вызова на `MakeBackendRect(...)` (файл cp1251 — правки только байтовые) |
| `Src/Render/renderwindow.h`, `Src/Render/renderbackend.h` | NSDMI → конструкторы (`NativeWindow`, `HBuffer`/`HTexture`/`HSurface`/`HShader`/`HInputLayout`) — C++03-совместимость seam |
| `Src/Render/renderer.cpp:565,579,591`, `smartrenderer.cpp:215` | `HInputLayout x = {};` / `HShader x = {};` → обычная инициализация (после конструкторов типы перестали быть агрегатами; MSVC x64 принимал `= {}` как расширение, VS2008 — `error C2552`) |

Гейты: x64 Wine — `ИТОГ: OK`, `build/PW_Game.exe` 15 201 280 Б (размер не
менялся — удалённые файлы не входили в граф), `check_exe.py` OK; x64 native на
tiny10 — `RC=0`, 15 196 160 Б, `check_exe.py` OK; E2E tiny10
(лог `2026.10.15-04.36.54`) — `Got login reply. code=Success(5), uid=131` →
лобби → `gamesvc_00` → `Switch to EGameState::Finished by 3`, asserts 0,
`ConfigPins` дословно как эталон (`caps=0x0 pins=0`), `unitstats` 75 строк;
скриншот-дифф `bb_3000` против эталона инстансинга — 3,97% отличий /
**0,26% грубых** (полоса шума прогонов 0,13–0,35%), против прогона шага 7 —
2,93%/0,27%; внутри прогона 6,73%/1,85% (анимация сцены). `Src/ShaderCompiler`
после удаления `--sm20` собирается x86-контуром (`cl ShaderCompiler: 12 objs`).

Там же закрыт пробел гейта «эталон фиксируется в ветке»: раньше кадр-эталон
жил только в `/tmp`. Добавлен `tools/render_shot/shot_diff.py` (метрика
diff/gross по поканальной дельте, полосы UI, сетка 16×12 и эвристика
«похоже на полосу» — полоса это регресс, кластер в сцене это анимация) и
`tools/render_shot/refs/stage2-2026-10-15/{pw_auto_bb_3000.png,
pw_auto_crop_3000.png}` — эталон конца этапа 2 (прогон `2026.10.15-04.36.54`).
Полоса шума прогонов зафиксирована в `tools/render_shot/README.md`.

### Сделано (проба: клиент под Wine на хосте — 2026-10-15, решение §3.z «GPU для тестов»)

Цель пробы: дешёвый вариант тестовой GPU-среды этапа 3 (RTX 3060 на хосте,
клиент Windows-ный). **Итог: клиент под Wine (`wine-10.0`, prefix `~/.wine-pw`)
запускается и доезжает до боя**, но 3D-сцена рисуется чёрной.

Процедура (повторяемая, каталог `PW/wine-client/`):

- `Game/Bin` = наш `build/PW_Game.exe` + x64-DLL со стенда tiny10: `ACE.dll`,
  `CensorDll.dll`, `d3dx9_43.dll` (x64, 2 401 112 Б), `fmodex64.dll`,
  `fmod_event64.dll`, `IOTerabit.dll`, `steam_api64.dll`, `TProactor.dll`,
  `zlib1.dll`.
- `Game/Packs` = `data01..06.pile` (1,4 ГБ — есть **только** на tiny10, это
  базовый дистрибутив, в `pwc-gitupdates*` их нет) + `epatch*.pile` /
  `zpatch_fev.pile` из `UpdateRepos/pwc-gitupdates-test` + `zzzresult.pile`
  (tiny10). `Game/Data` — из `pwc-gitupdates-test` (Audio, Localization).
- `Game/Profiles` — **все 16 cfg с tiny10**: без `game.cfg` клиент доходит только
  до `social.cfg` и завершается (молча, rc=0). В `default.cfg` обязательно
  `gfx_fullscreen = 0` (иначе обрыв на том же месте) и явный `gfx_resolution`;
  `setvar`-переопределения — в `Profiles/autoexec.cfg`.
- запуск: cwd = `Game/Bin`, `DISPLAY=:97 WINEPREFIX=~/.wine-pw wine ./PW_Game.exe
  protocol <url> -dumpUnitStats -dumpConfigPins`; в URL ip = **7f000001**
  (стенд на той же машине), сессию создавать заново на каждый прогон
  (`cli/session.js --ips 7f000001`). Логи — `~/Документы/My Games/Prime World
  Classic/Session/Logs/<UTC>` (в prefix симлинк на `$HOME`); снимок —
  `DISPLAY=:97 import -window root`.

Что работает: `Got login reply. code=Success(5), uid=131` → `Entered lobby` →
`Got lobby instance` → `Connected to game service` → `mapload: all map objects
loaded` → `unitstats` (75 строк) → бой идёт. Импорт DLL полный: `d3d9.dll`
builtin (wined3d), `d3dx9_43.dll` native, `GLU32.dll` builtin, `steam_api64.dll`
native; при `WINEDEBUG=err+all` — **0 строк `err:`**.

Что не работает: **3D-сцена чёрная** при полностью исправном 2D (UI, миникарта,
имена, таймер). Замер причины: wined3d идёт через **OpenGL**, а не Vulkan —
`fixme:d3d:wined3d_guess_card No card selector available for card vendor 0000
(using GL_REN…)`; `ConfigPins: adapter="NVIDIA GeForce GTX 470"
vendor=0x10de device=0x06cd driver=589845.861103 MaxVertexShaderConst=256
IsNullRef=0 caps=0x7 pins=0` (матчер видит NVIDIA-подобное устройство и
ненулевые `caps` — в отличие от tiny10 `caps=0x0`), `DepthFormat: D24S8`.
Бисет `shadows / shadow_terrain / shadowPCF4_enable / bloomEnable / pfx_enable /
waterrefl = 0` картинку не меняет → дело не в PostFX. Диагноз **не закрыт**;
кандидаты: DXVK вместо wined3d (только как тестовая обвязка D3D9-эталона, в
продукт DXVK не идёт — §2.15) или разбор wined3d/GLSL.

Значение для этапа 3: дефект не блокирует Vulkan-путь — `NvrhiBackend` говорит с
Vulkan напрямую, и смоук (`tools/nvrhi_probe`) под Wine на RTX 3060 проходит
целиком. То есть «клиент под Wine на хосте» годится как стенд этапа 3 (реальный
GPU, HW-тени, замеры), а D3D9-эталон на нём требует отдельной починки; пока
D3D9-регресс остаётся на tiny10.

### Сознательные отклонения от «картинка не меняется»

1. **PostFX sharp / color-correct.** `coeffMap` был не привязан (бинд в
   слот 257 + конфликт с `DepthMap` в s0). После переноса в s3 веса
   начинают читаться — картинка PostFX поменяется (в лучшую сторону).
2. **Тени.** `ShadowMapPCF_DS` вместо `D16`/`D24X8` теперь всегда `D24S8`
   (было `shadowFat && HasD24X() ? D24X8 : D16`, `shadowFat` — выключенный
   dev-var → фактически был `D16`). Разрядность глубины выросла →
   возможны сдвиги в bias-чувствительных местах.
3. **Состояния, записанные мимо менеджера (этап 2, 2026-10-14).** Пока
   `FlashRenderer`/`uirenderer`/`ShadowMap` писали состояние прямо в устройство,
   кэш `RenderStatesManager` об этом не знал: следующий `SetState(preset)`
   считал, что пресет уже выставлен, **пропускал** применение — и в устройстве
   оставалось чужое значение (например `SRCBLEND=ONE` из Flash-прохода вместо
   `SRC_ALPHA` из пресета материала). После перевода на `SetStateDirect` такой
   пропуск невозможен, значит там, где код полагался на «протёкшее» состояние,
   картинка **может измениться** — это и есть чинившийся дефект (§2.1), а не
   регресс. Признак, если он вылезет: конкретный слой (Flash-панель, деколь,
   тень) рисуется с неправильным блендом. Проверено клиентскими снимками
   (`auto_screenshot`) и глазами: расхождений с предыдущим прогоном в
   статичных зонах UI нет.

### Не сделано / под вопросом

- Эмблемы гильдий: путь с D3DX оставлен как есть, переделывается заново
  (`PENDING.md` §9). Не блокирует этап 2 — фасад закрывает DDS-путь.
  После снимка (§2.13) импорт `d3dx9_43.dll` в собранном exe — **14 символов**
  (замер разбором импорт-таблицы `check_exe.py`): 13 математики (§2.14 — решение
  возить DLL до этапа 4) + `D3DXCreateTextureFromFileInMemoryEx` этого пути
  эмблем; `d3d9.dll` — 1 символ (`Direct3DCreate9`). Всего DLL в импорте — 28.
- `.shd2`-файлы в `Data/Shaders/UberShaders/` (17 шт.) клиентом больше не
  читаются, но из контента не удалены — удаление трогает `files.list`
  (манифест обновлений), делать в контентном контуре.
- `Session.ROOT_*.shader3compatibilityError` (текст про SM3.0) — поле БД,
  оставлено: гейт `CheckHardwareCompatibility` жив, а менять xdb-раскладку
  ради мёртвого текста не нужно.
- `ShaderCompiler` перенесён в граф и переведён на `D3DCompile` (см. выше);
  `--sm20` в нём так и остался мёртвым кодом — на этапе 3 его логичнее удалить
  целиком, когда появится SM5/SPIR-V путь.
- `Render/SHCoeffs.cpp` → `D3DXSHEvalDirectionalLight` заменена своей
  реализацией с бит-в-бит константами D3DX (замер против настоящего
  `d3dx9_43.dll`: максимум ~1 ULP, см. `tools/sh_math_test`). Остальная
  D3DX-математика (`D3DXMatrix*`, `D3DXVec3*`, `D3DXSHEvalDirection` в
  `Scene/SHGridCalculator.cpp`) перенесена в этап 4: это зависимость от
  `d3dx9_43.dll`, а не утечка D3D9-устройства, этапу 2/3 она не мешает.
