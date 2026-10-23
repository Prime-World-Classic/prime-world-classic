# NVRHI в client-modern: план (DX12/Vulkan, D3D9 удаляется)

Статус: **код есть, этап 3 идёт.** Текущее состояние шагов и очередь — §6
«Статус в ветке `nvrhi`» и секция «Очередь после A/B 2026-10-21»; здесь —
решение, замер поверхности и этапы, не срез состояния. Ветка `nvrhi` (worktree
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

### Очередь этапа 3 (зафиксирована 2026-10-15, решение §2.15 п.11)

Шаги 9–11 — корпусные: проверяемы на живом D3D9 (E2E tiny10 + скриншот-дифф),
рантайм-Vulkan ещё не существует. Шаг 12 — первый спайк бэкенда, 13 — перевод
проходов, 14 — блобы без исходников. Каждый шаг — отдельный коммит со своим
гейтом.

| # | шаг | почему здесь |
|---|---|---|
| 9 | ✅ инстансинг, шаг 2 — закрыто 2026-10-15 (§2.10): лексика шва `kInstanceStream` (маркер потока данных экземпляра) + `SetInstanceSource(HBuffer, stride, offset)` + `SetInstanceCount`; сдвиг геометрийных потоков, VB-счётчик `0..N-1`, частоты потоков и инстансный вариант декларации — внутри `D3D9Backend`; в клиенте остались только `BindInstanceCounter`/`BindInstanceSource` | сделано до бэкенда (§3.z п.5): Vulkan-цели не нужны ни VB-счётчик, ни атрибут экземпляра в раскладке (`SV_InstanceID`); замер: раскладок на бой **15** (было бы ~30 с подменой деклараций), инстансных рисований 39/кадр |
| 10 | ✅ динамические массивы из `c`-регистров → SSBO-форма — закрыто 2026-10-15 (§3.z п.2): макрос `PW_MODERN_ARRAYS` в 11 `.hlsl` (`StructuredBuffer<T>` вместо `T name[count] : register(...)`), развёртка циклов с постоянным пределом — только под макрос; SM3-путь побайтово не изменился (32/32 единицы, `tools/hlsl_sm3_check`) | без этого layout §3.z п.1/п.2 нереализуем (неконстантная индексация члена UBO запрещена в Vulkan и в D3D12); замер: единиц с неконстантной индексацией членов UBO **13 → 0**, байт UBO макс **7040 → 796**, среднее **811,0 → 358,0**, различных `(set,binding)` 9 → 10 (max binding 8 → 9) |
| 11 | ✅ мультиблоб `.shd` (SM3 DXBC / DXBC SM5 / SPIR-V) + драйвер компиляции DXC — закрыто 2026-10-15 (§3.z п.3): писатель `tools/shader_build/mkshd2.py` (SM3-слой копируется, SPIR-V только для живых индексов), читатель v2 `Src/Render/shadercontainer.h` + `multishader.cpp` (dev-var `shader_target`), корпус 15 контейнеров / 58 индексов / 0 пропусков | compile-only: рантайм D3D9-путь не тронут (читатель под dev-var'ом, по умолчанию SM3); `OnlineShaders/` — шаг 14 |
| 12 | спайк `NvrhiBackend`: SDL3-окно + swapchain + clear/present + **один** DIP, за dev-var/env; D3D9-путь не тронут. **Предварительный спайк сделан 2026-10-15 (`ff7d237c0`)**: блобы v2 грузятся в Vulkan (116/116 шейдеров, 58/58 пайплайнов с раскладкой), SDL3-окно + surface + swapchain + приём бэк-буфера NVRHI — PASS, фактическая карта дескрипторов снята (§3.z п.1); **одиночный DIP блобом из `.shd` v2 рисует в Vulkan** — 8 из 12 пробованных материалов дают геометрию, `WaterDummyMaterial` пиксель-в-пиксель (`1d2ac6a45`); в клиенте собственного бэкенда ещё нет | дёшево снимает главные неизвестные (winding/`frontCounterClockwise`, Y-flip, 4 дескриптор-сета, форматы RT/DS, DSM); стенд — Wine на хосте с RTX 3060 (§6 «Проба: клиент под Wine») |
| 13 | перевод проходов на NVRHI по work-list'у ключей (206 на бой, 85 активных) + кэш пайплайнов (открытая таблица без LRU, компиляция синхронная) | объём и порядок работ зафиксированы замером §1.4; метрика прогресса — доля DIP'ов, идущих через новый путь |
| 14 | портирование 34 блобов `OnlineShaders/` через дизассемблирование SM3; 10 мёртвых удалить сразу (§2.15 п.12) | без них этап 4 (удаление D3D9) недостижим; порядок: `default_shader`/`VertexShader` → `ShadowPass*`/`SA*` → остальное |

### Гейт качества (замена «золотых unitstats»)

Рендер-этапы ломают байтовый паритет по определению, поэтому:

| Тип правки | Гейт |
|---|---|
| вне рендер-пути (этап 1 — зачистка мёртвого, ликвидация утечек) | обычный: x86 VS2008 + x64 Wine + x64 native + E2E tiny10 + **золотые `unitstats` байт-в-байт** |
| рендер-этапы (2, 3, 4) | сборка x64 (Wine + native) + **E2E на tiny10: вход → лобби → бой** + скриншот-дифф (новый эталон фиксируется в ветке: `tools/render_shot/refs/`, сравнение — `tools/render_shot/shot_diff.py`) + `unitstats` как smoke (бой/физика живы, значения не эталон) |
| A/B паритета двух бэкендов (Vulkan против DXVK/D3D9, этап 3) | те же кадры, что и выше, но **сведённые по игровым часам**: стороны входят в матч с расхождением ~20 с, поэтому кадр DXVK берётся на том же значении обратного отсчёта HUD (кроп `(492,130)-(612,164)`), шаг снимков 2–3 с. Метрики — `shot_stats.py` (+`--defects`, `--lines`, `--tone`) и `scale_probe.py`. Шум прогона ≈ 0 (регионный \|D\| ≤0,3, gross ≤1,3 %), пороги: регионный \|D\| <5, плашка 0 белых px (`--defects`), строки `--lines` не повторяются. Несведённые кадры для сравнения бэкендов **не использовать** (§ «Очередь после A/B 2026-10-21», п.5) |

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
пользователя, 2026-10-13; задача — в архивном `PENDING.md` §9, файл снят в
`/home/rekon/PW/_archive/2026-10-21-docs-cleanup/`; ниже ссылки вида
`PENDING §N` означают этот архивный файл).** Это единственный
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
5. **Эмблемы гильдий** — не трогать, переделываются заново (`PENDING §9`, архив).
6. **`Bin/D3DCompiler_43.dll` в прод-обновлениях** не импортируется
   `PW_Game.exe` (импорты exe — только `d3d9.dll` и `d3dx9_43.dll`), т.е. либо
   грузится другим модулем, либо мёртвый груз в доставке. Запомнено, по решению
   пользователя игнорируется (`PENDING §10`, архив).
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
11. **Порядок этапа 3 утверждён (2026-10-15): сначала корпус, потом бэкенд.**
    Последовательность: инстансинг шаг 2 → вынос динамических массивов в SSBO +
    явные размеры → мультиблоб `.shd` + DXC-драйвер → спайк `NvrhiBackend`
    (окно/clear/present/первый DIP) → перевод проходов по work-list'у ключей
    пайплайна. Основание: макет сетов §3.z п.1/п.2 предполагает уже сделанное
    разделение UBO/SSBO, а каждая правка корпуса после написания бэкенда требует
    повторной валидации бэкенда; все три корпусные правки проверяемы на живом
    D3D9-стенде. Открытый вопрос §3.z «Порядок» этим закрыт.
12. **Блобы `OnlineShaders/` (2026-10-15, замер):** 10 из 44 не имеют ссылок в
    коде — удаляются при зачистке; **34 портируются на HLSL через
    дизассемблирование SM3** (`D3DDisassemble`/`D3DXDisassembleShader`, свой тул
    по образцу `tools/dsm_probe`), а не реверсом по картинке; D3D9-путь ради
    PostFX не сохраняется (иначе этап 4 «D3D9 удалён» недостижим). Порядок
    портирования: `default_shader` + `VertexShader` (без них не работает ни один
    `ImmRenderer::RenderScreenQuad`) → 10 `ShadowPass*`/`SA*` (HW-тени) →
    остальное; каждый проверяется скриншот-гейтом. Замер и перечень — §2.6.
13. **DXVK допускается только как тестовая обвязка D3D9-пути на хосте**
    (2026-10-15): проба «клиент под Wine» дала чёрную 3D-сцену под wined3d/GL;
    DXVK может починить D3D9-референс на новом стенде. В продукт DXVK не идёт
    (этап 3 = собственный Vulkan через NVRHI); D3D9-регресс пока остаётся на
    tiny10.
14. **Лексика инстансинга в seam — маркер потока, а не отдельный вызов**
    (2026-10-15, шаг 9): вместо планировавшегося `SetInstanceSource` как
    единственной ручки источник экземпляра задаётся маркером `kInstanceStream`
    в описании раскладки и в `BindVertexBuffer`, а `SetInstanceSource` отвечает
    только за данные. Основание: `ParticleFX` сам описывает per-instance
    атрибуты и биндит их обычным `BindVertexBuffer(1, …)` — отдельный вызов
    пришлось бы вставлять в каждый такой путь, маркер же приезжает вместе с
    раскладкой и не создаёт второй источник истины. Числа и реализация — §2.10.

15. **SM3-ветка шейдеров остаётся побайтовой; современная форма массивов и
    развёртка циклов — только под `PW_MODERN_ARRAYS`** (2026-10-15, шаг 10):
    в `FullScreenMaterial/vs` и `TerrainElementDebugMaterial/ps` оставлены
    дублирующие друг друга ветки (цикл для SM3, ручная развёртка для
    современной цели). Основание: fxc циклы с постоянным пределом НЕ
    разворачивает (замер `hlsl_compile_test --disasm`: `defi i0, 3, 0, 1, 0` +
    `loop aL … endloop` с `c[aL]`), поэтому «безобидная» развёртка меняла
    DXBC поставляемых шейдеров (876 → 776 Б и 992 → 816 Б) и требовала
    перегенерации `.shd` + нового визуального эталона при том, что картинка
    обязана остаться. Дублирование кода сознательное: оно исчезнет вместе с
    D3D9-путём на этапе 4. Проверка эквивалентности правок корпуса —
    `tools/hlsl_sm3_check` (32/32 одинаковых DXBC).

16. **Бюджет пинов в новом `.shd`: только живые комбинации + рантайм-фолбэк
    DXC'ом** (решение пользователя 2026-10-15, вариант «(1)» из трёх):
    прекомпилировать все комбинации нельзя — `BasicMaterial.shd` и так
    1 950 128 Б на 49 966 пар, а SPIR-V-блоб 1–3 КБ на единицу даёт десятки
    МБ на материал. В контейнер кладутся живые комбинации (замер боя: 185 пар
    VS/PS, 117 `sortId`, 206 ключей пайплайна), промах компилируется на
    клиенте (`dxc` DLL-ом в `Bin`, тот же путь нужен для
    `deprecated_shaders/*`). Отказ от варианта «всё прекомпилировать» — размер
    патча; от варианта «только живые, без фолбэка» — риск чёрного материала на
    редком пине. Числа и вывод — §3.z п.3.

17. **Среда генерации контента — хост + Wine + нативная `d3dcompiler_47.dll`**
    (2026-10-15): wine-ный `d3dcompiler` (vkd3d) SM3 не компилирует, нативная
    MS-DLL x64 под Wine работает и даёт DXBC, побайтово равный полученному на
    tiny10 (32/32 единицы affected-профиля; `AOEMaterial.shd` перегенерирован
    побайтово равным поставляемому). В репозиторий DLL не кладётся —
    проприетарный бинарник, лежит в `/home/rekon/PW/wine-tools/`.
    tiny10 остаётся только для нативной сборки клиента и E2E. Детали и команды —
    §3.z п.4 и скил `pw-client`.
18. **Визуальный гейт этапа 3 — два контура, не один** (2026-10-15, подтверждено
    пользователем): (а) **DXVK как D3D9-эталон на Wine-стенде** — принят, стенд
    работает, дефект «чёрная сцена» закрыт; (б) **межстендовое сравнение кадров
    (tiny10 WARP против Wine+DXVK) гейтом не является** — замер даёт 81,57% /
    7,40% gross на одном и том же кадре 3000 из-за зума камеры и состояния UI,
    годится только как смоук «сцена есть»; (в) для шага 13 дополнительно нужен
    детерминированный гейт «микро-сцена, один проход, два бэкенда» — живой кадр
    не локализует ошибку конкретного прохода (458 рисований/кадр, 206 ключей).
    tiny10 остаётся продуктовым гейтом D3D9-регресса без изменений.
19. **Рареженная таблица целей `.shd` v2 привязана к профилю устройства —
    рантайм-фолбэк DXC обязателен** (2026-10-15, замер): на tiny10 (WARP,
    `caps=0x0`) бой делает 58 обращений к цели и все 58 покрыты корпусом; на
    Wine+DXVK (RTX 3060, `caps=0x7`) — 77 обращений и **0** покрытий (все —
    «комбинации нет в цели, нужен рантайм-компилятор»). Плотная SM3-таблица при
    этом не пострадала (картинка корректна), но на Vulkan-контуре промах = чёрный
    материал. Значит: корпус нужно строить объединением снимков живых индексов по
    профилям (минимум WARP + NVIDIA/DXVK), и путь рантайм-компиляции (п.16)
    становится обязательной частью шага 13, а не страховкой.

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

**Шаг 9 закрыт (2026-10-15): сдвиг потоков и атрибут экземпляра ушли в
бэкенд.** Лексика шва оказалась **маркером в описании раскладки**, а не отдельной
функцией (`SetInstanceSource` как планировалось — теперь это имя несёт только
источник данных):

* `renderbackend.h:81` — `const unsigned kInstanceStream = 0xffffffffu;`
  клиент называет этим потоки данных экземпляра в `VertexFormatDescriptor` и в
  `BindVertexBuffer`; бэкенд сам решает, в какой физический слот их положить
  (`D3D9Backend::kInstanceSlot = 1`, геометрийные потоки сдвигаются внутри
  `SetStreamSource`). Маркер выбран вместо отдельного вызова потому, что
  `ParticleFX` описывает per-instance атрибуты **сам** и биндит их через
  обычный `BindVertexBuffer(1, …)`: отдельный вызов пришлось бы вставлять в
  каждый такой путь, а маркер приезжает вместе с раскладкой;
* `SetInstanceSource(HBuffer perInstanceData, unsigned stride, offset)` —
  источник данных экземпляра: пустая ручка = встроенный счётчик `0..N-1`
  (бэкенд сам строит VB и атрибут; на Vulkan это `SV_InstanceID`, VB не нужен),
  непустая = per-instance данные клиента (партиклы). Порядок относительно
  `SetInstanceCount` и биндингов геометрии не фиксирован: `D3D9Backend`
  применяет состояние дельтой в `ApplyInstanceState()` перед каждым `Draw*`;
* `CreateInstancedInputLayout` переименован в `CreateInputLayout(descr, out)`:
  инстансный вариант — не отдельный запрос клиента, а свойство раскладки
  (`kInstanceStream` в описании), кэш вариантов живёт в бэкенде
  (`InstancedVariant` + `instancedDecls`, ключ `void*` — см. грабли ниже);
* из клиента удалены `ApplyStates`, `lastStreamSourceFreqs`, `DeclMap`
  подмены, `s_pInstanceIndexVB`/`s_instancedVDecl`, `BindInstanceVB` →
  `BindInstanceCounter()` / `BindInstanceSource()` (`smartrenderer.h:65-66`).
  Клиентский флаг `instancingEnabled` остался: он отвечает за соглашение о
  раскладке (сдвиг в `BindVertexBuffer`, выбор инстансного варианта в
  `BindVertexDeclaration`), а не за механизм устройства.

Замер после правки (инструментальный билд, E2E tiny10, лог
`2026.10.15-08.35.58`, бой):
`PipeStats: semTotal=206 semFrame=85 rawTotal=210 rawFrame=85 drawsFrame=458
drawsTotal=1916677 declTotal=15 declFrame=14 instDrawsFrame=39
instDrawsTotal=165749`. Мощность ключа пайплайна **не изменилась** против шага 8
(206 / 85 / +4): в ключе уже был бит инстансинга, поэтому слияние деклараций её
не меняет — зато раскладок (входных layout'ов, которые надо создавать на Vulkan)
стало **15 на бой, 14 в кадре** вместо ~двух вариантов на материал. Инстансный
путь стендом покрывается реально: 39 инстансных рисований на кадр, 165 749 за
сессию. `ConstStore`-строка не изменилась (VS 235 reg / 3760 B, PS 221 / 3536 B).

Грабли правки: `nstl::hash` специализован только для `void*` (ключ `hash_map`
для указателя на класс — `error C2079: uses undefined struct 'nstl::hash<_Key>'`);
`IDirect3DDevice9::CreateVertexBuffer` в SDK 10.0.26100 — 6 аргументов (FVF
обязателен, `0` для раскладки у декларации); `kInstanceStream` требует явного
`#include "renderbackend.h"` в `InstancedMeshResource.cpp` и `ParticleFX.cpp`
(они не видят seam через `smartrenderer.h`); VB-счётчик обязан быть **int**
(`InstancedMeshResource` это и проверял — float-счётчик ломает вершины).

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
  Проект есть в `Src/PF.sln`.
  ~~не входит в CMake-граф клиента (25 проектов) — собирается отдельно, под
  Win32/D3DX9~~ — **устарело, проверено 2026-10-15**: тул уже в x64-графе
  (`Tools/ClientCMake/gen_cmake.py:72` — `TOOL_OBJS = {"ShaderCompiler":
  ["ShaderCompiler", "libdb", "Foundation", …]}`, там же `:329` и `:733` —
  «exe-проект, но НЕ PW_Game → инструмент сборки»), артефакт —
  `Tools/ClientCMake/build/x64-wine/ShaderCompiler.exe`; в `Src/CMakeLists.txt:146`
  из клиентских источников он исключён (`list_remove_regexp`), но собирается
  отдельной целью.
  ~~`D3DXCompileShader` → `D3DCompile`~~ — **сделано раньше (коммит `1d9586a62`
  и ранее)**: в `ShaderCompiler.cpp:15-30` `D3DCompile` грузится динамически
  (`LoadLibraryA` по списку `d3dcompiler_47/48/50/43.dll`), комментарий у
  `:111` фиксирует причину отказа от D3DX: «current .hlsl do not build with
  d3dx9_43 at all (E5017 …)». Профили в туле — `vs_3_0/ps_3_0` (SM3-выход
  обязан остаться: D3D9 не умеет SM5), SM5/SPIR-V — работа шага 11.
  **Смоук паритета (шаг (2) из порядка ниже) проходит на хосте**: прогон
  `ShaderCompiler.exe` под Wine с нативной `d3dcompiler_47.dll` даёт
  `AOEMaterial.shd` 1368 Б с md5 `676ed3c656401e4bb7faf25e221e8632` —
  побайтово равный поставляемому (`cmp` чист). Полная сверка 36 контейнеров —
  ниже по мере прохождения.
  Что в нём надо менять: профили
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

Замер 2026-10-15 (спайк `ProbeShd2` в `tools/nvrhi_probe`, 15 контейнеров v2 /
58 живых пар, RTX 3060 через Wine): **шейдеров создано 116/116, пайплайнов с
раскладкой 58/58** — блобы писателя `mkshd2.py` Vulkan принимает целиком, ни
одного отказа. Фактически DXC раскладывает всё в **один** набор (set 0),
максимум `binding` = 9 (`RoadMaterial` ps: 7 текстур); счётчики по корпусу
`smp=52 tex=97 ubo=109 ssbo=13`. Значит бюджет дескрипторов ограничением НЕ
является (лимит наборов: 8 у lavapipe, 32 у NVIDIA) — настоящая работа в том,
чтобы разложить ресурсы по сетам.

Механизм раскладки — HLSL `space`, а не флаги сдвигов. Проба
`register(t0, space2) / register(t1, space2) / register(s0, space3) /
register(b0, space1)` даёт `sets [0,1,2,3]` и `bindings [(0,0),(1,0),(2,0),
(2,1),(3,0)]`, т.е. **`space` = descriptor set**. `-fvk-b-shift / -fvk-t-shift /
-fvk-s-shift / -fvk-u-shift <shift> <space>` на явно зарегистрированные ресурсы
НЕ действуют (замер: `bindings` не изменились); они работают только вместе с
`-fvk-auto-shift-bindings` и только для ресурсов без `register`. Отсюда
решение: фиксированный макет выше достигается **впрыском `, spaceN` в
прослойку `tools/spirv_probe/shim.py`** по классу ресурса. Из прочих полезных
флагов DXC: `-fvk-bind-globals <binding> <set>`, `-fvk-bind-register
<type-number> <space> <binding> <set>`, `-fspv-preserve-bindings`,
`-fvk-invert-y`. На стороне NVRHI один набор = один `BindingLayout` с
`setRegisterSpaceAndDescriptorSet(N)` (`Vendor/NVRHI/include/nvrhi/nvrhi.h:2111`,
реализация `Vendor/NVRHI/src/vulkan/vulkan-resource-bindings.cpp:1031-1090`,
комментарий «Can't have multiple binding sets with the same registerSpace»).
Рефлексии SPIR-V в вендоренном NVRHI нет (`reflectPipelineStates` отсутствует)
— карту binding'ов берём из замера (`tools/shader_budget`, `ProbeShd2`).

**Макет реализован в прослойке (2026-10-15).** `tools/spirv_probe/shim.py`
получил режим `set_layout`: `SPACE_TEXTURES = 2`, `SPACE_SAMPLERS = 3`,
`SPACE_BUFFER = 1`; явные `register(tN)/sN/bN/uN` получают `, spaceN`, `c`-регистры
как и раньше снимаются (они уходят в авто-`$Globals`, set 0), а ресурсам без
`register` (наши `StructuredBuffer<T> имя;` из шага 10 и `Texture2D`, выросшие из
`sampler2D`) номер и set назначает прослойка — текстуры `tN space2`,
`SamplerState` `sN space3` (`s0` занят общим `g_smp`), буферы `tN space1`
(DXC для `StructuredBuffer` требует класс `t`). Грабли: шаблон в типе
(`StructuredBuffer<InstanceData>`) regex обязан проглатывать, иначе объявление
проходит мимо и SSBO уезжает в set 0; класс ресурса определяется по базовому
типу, а не по строке с шаблоном.
Замер после включения: `shader_budget --pins 1 --define PW_MODERN_ARRAYS=1
--set-layout` — 70/70 единиц, наборы ровно `0 ($Globals UBO) / 1 (SSBO) /
2 (текстуры, binding до 6) / 3 (сэмплер 0)`; `mkshd2` по умолчанию пишет корпус с
макетом (флаг `--flat-sets` — для сравнения), и `ProbeShd2` на новом корпусе
(58 пар): **наборов 4, 116/116 шейдеров, 58/58 пайплайнов с четырьмя
`BindingLayout`** (по одному на набор, `setRegisterSpaceAndDescriptorSet(N)`),
максимум binding в сете 2 = 7 (`RoadMaterial` ps — 7 текстур). Размер корпуса
3 027 332 → 3 006 856 Б (−20 476 Б — кодогенерация DXC с явными регистрами
чуть компактнее).

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

**Шаг 10 закрыт (2026-10-15, `7716944fd`).** Форма массива выбирается
макросом `PW_MODERN_ARRAYS` (по умолчанию не определён — сборка клиента его
не задаёт, SM3-путь не затронут):

```hlsl
#if PW_MODERN_ARRAYS
StructuredBuffer<InstanceData> instanceData;
#else
InstanceData instanceData[MAX_INSTANCES_COUNT] : register ( INSTANCE_DATA );
#endif
```

Затронуты 11 файлов: `AOEMaterial`, `BasicMaterial`, `DebugMaterial`,
`DecalMaterial`, `DecalTerrainMaterial`, `FullScreenMaterial`,
`NatureMapMaterial`, `ParticleFXMaterial`, `ShadowMaterial`,
`TerrainElementDebugMaterial`, `_commonSkinning`. Записи в эти массивы в
исходниках нет (проверено по корпусу), поэтому достаточно
`BufferDeviceParameter` (п.1) — `RWStructuredBuffer` не понадобился.

Замер `tools/shader_budget --pins 1 --define PW_MODERN_ARRAYS=1` (70 единиц,
FAIL 0 — после починки прослойки, см. ниже): единиц с неконстантной
индексацией членов UBO **0 из 70** (было 13), раскладка SSBO — **11
буферов**, байт UBO макс **796** (было 7040), среднее **358,0** (было 811,0),
различных `(set,binding)` 10 / max binding 9 (было 9 / 8). При `--pins 0`:
UBO макс 3744 → **768**, SSBO 2 (`hexParams`, `BonesMatrices` у
`PaintMaterial`). Таблица раскладки (ArrayStride по SPIR-V — это контракт для
C++-части на шаге 12): `BonesMatrices` 48 Б/элемент (70 → 3360 Б),
`instanceData` 160 Б (`BasicMaterial`, 19 → 3040 Б) и 64 Б
(`Decal`/`DecalTerrain`), `data` 80 Б (PFX, 47 → 3760 Б), `hexParams` 16 Б
(40), `position` 16 Б (9). Число элементов в SPIR-V — `runtime`
(`StructuredBuffer` — несокращаемый массив), размер задаёт бэкенд.

Важная деталь макета: `float4x3` в SSBO занимает **64 Б**, а в блоке
`c`-регистров — **48 Б** (проверено декорацией `ArrayStride`). Чтобы хост,
пишущий один блок констант (§2.9 `ConstBlock`), мог отдать те же байты,
элементы объявлены wrapper-структами (`InstanceData`, `INSTANCE_DATA`) либо
массивом матриц внутри структуры (`_commonSkinning`) — замер подтвердил
совпадение с числами таблицы выше.

**Раскладка `$Globals` в UBO — не копия блока констант (замер спайка
шага 12, 2026-10-15).** Спайк `ProbeShd2Draw` (`tools/nvrhi_probe`, §6)
нарисовал кадр блобом `WaterDummyMaterial` из `.shd` v2 и попутно снял байтовую
раскладку блока: `$Globals` VS = **384 Б, 13 членов**; `OpMemberDecorate` даёт
`Offset` 0/64/128/192 для первых четырёх (`MatrixStride` 16 + декорация
`RowMajor`=5), далее 256/272/288 (`WEyePos`/`eyeDirection`/`totalTime`) и
292..368 — PS-глобалы. Порядок членов = **порядок объявления** в `_common.hlsl`
(`View`, `World`, `ViewProjection`, `projection`, …), а НЕ номера `register(...)`:
прослойка переписывает только файл материала, `_common.hlsl` остаётся с
`register(WORLD)` и т.д., а имена членов DXC из SPIR-V выбрасывает
(`OpMemberName` отсутствует). Следствие для бэкенда: `ConstBlock` (§2.9) нельзя
отдавать в UBO плоским `memcpy` — нужна таблица переноса **«const-регистр → байт
UBO» на материал**; строится она из SPIR-V того же контейнера
(`OpMemberDecorate Offset` + порядок объявления + `GlobalMasks.h` для имён) и
складывается в писатель, а не в рантайм. Проверено: VS `WaterDummyMaterial`
читает members 1 и 2 → Offset 64 и 128 = `World` и `ViewProjection`; патч только
этих двух диапазонов единичными матрицами даёт ровно ожидаемый кадр
(32768/32768 пикселей в левой половине).

**Замена решения (шаг 13, константы, 2026-10-15):** таблица «const-регистр →
байт UBO» НЕ строится — вместо авто-`$Globals` введён **канонический c-банк**
(`cbuffer` с `packoffset(cN)` по номерам из исходников), где байтовое соседство
членов совпадает с D3D9 и перенос сводится к одному буферу на стадию. Механизм,
числа и подводные камни — §«Сделано (шаг 13.0: канонический c-банк)» в конце
документа.

Развёртка циклов. Два места индексируют массивы **постоянными** индексами в
цикле с постоянным пределом — формально это не запрещено, но DXC цикл не
разворачивает и оставляет `c[aL]`. Оказалось, что и **fxc их не
разворачивает**: дизассемблирование (`hlsl_compile_test --disasm`) показывает
`defi i0, 3, 0, 1, 0` + `loop aL … endloop` с `c[aL]`, а ручная развёртка
меняла DXBC (`FullScreenMaterial/vs` 876 → 776 Б, `TerrainElementDebugMaterial/ps`
992 → 816 Б). Поэтому развёртка оформлена второй веткой того же макроса, а
SM3-ветка оставлена циклом дословно (§2.15 п.15).

Сверка SM3-пути (новый `tools/hlsl_sm3_check`, профиль `affected`: 16
комбинаций пинов × 2 стадии = 32 единицы, компиляция настоящим `D3DCompile`
на tiny10): **одинаковых DXBC 32, изменившихся 0** — `.shd` и бинарник клиента
не меняются, поэтому сборочные/E2E-гейты к правке не применялись (менять
нечего). Под Wine этот путь непроверяем: встроенный в Wine `d3dcompiler`
работает через vkd3d и даёт `E5017: Aborting due to not yet implemented
feature: SM1 cast from bool to integer` на `AOEMaterial` (замер 2026-10-15).

Починка прослойки `tools/spirv_probe/shim.py`: out-параметры PS с `COLORn`
становятся `SV_TARGETn` с сохранением индекса (в SM6 произвольная
пользовательская семантика на выходе запрещена — `invalid usage of semantic
'TEXCOORD101' in shader profile ps` на `WaterDummyMaterial/ps`); корпус
разбирается **70/70** и до правки массивов, и после.

Что остаётся на шаге 12: таблица «диапазон `c`-регистров → SSBO» (по
`GlobalMasks.h` + таблице выше) и копирование этих диапазонов из
`ConstBlock` в буферы бэкендом; хост при этом не меняется.

СНЯТО ЧАСТИЧНО 2026-10-20 (`d6856956c`): таблица `kSsboBridges` введена
(`BonesMatrices` — c24, 232 регистра, ArrayStride 48, буфер 256*48 Б;
`hexParams` — c22, 40 регистров, ArrayStride 16), `SpvResScan` читает `OpName`
и `ArrayStride`, привязка набора 1 выбирает буфер по имени SSBO из SPIR-V.
Остаток: мосты `instanceData` (базисов два — `INSTANCE_DATA`=c20 со структурой
`InstanceData` 160 Б в `BasicMaterial.hlsl:134` и `VSHADER_LOCALCONST9`=c29 с
`float4x3` 64 Б в `BasicMaterial.hlsl:140`, поэтому ключа по одному имени
недостаточно) и `data` (`ParticleFXMaterial.hlsl:18`, `StructuredBuffer<INSTANCE_DATA>`);
сейчас оба получают dummy (нули) — в логе это заметки «SSBO без моста —
подставлен dummy». См. секцию «Сделано: мосты c-банк → SSBO» в конце.

**3. Контейнеры шейдеров.** Один `.shd` = несколько блобов: SM3 DXBC (D3D9 до
этапа 4), DXBC SM5 (DX12), SPIR-V (Vulkan) + один и тот же `sortId`/ключ.
Заголовок контейнера должен нести число блобов и их цель, иначе генератор
§2.6 не переиспользуется. Второй контейнер (`OnlineShaders/`, 44 блоба) —
параллельно `.shd`; у него **нет исходников** и его надо переписывать руками
(факт и перечень — §2.6).

**Бюджет пинов (решение пользователя 2026-10-15 — вариант «живые комбинации +
рантайм-фолбэк»).** Комбинаторика пинов даёт раздувание контента: у
`BasicMaterial.shd` 49 966 пар (кэш `.codes` в `UberShaderFileInfo::Read()`
фактически выключен — ранний `return` после `Reset()`, т.е. всегда полная
компиляция всех пинов), сам контейнер 1 950 128 Б (весь каталог
`Data/Shaders/UberShaders/` — 36 файлов, 3,1 МБ). SPIR-V-блоб на единицу по
замеру — 1–3 КБ, т.е. «прекомпилировать всё» = десятки мегабайт на один
материал. Поэтому: в контейнер кладутся **только реально используемые
комбинации** (замер боя: 185 пар VS/PS, 117 различных `sortId`, 206 ключей
пайплайна, 85 активных в кадре), остальные компилируются на клиенте DXC'ом
(`dxc` кладётся в `Bin` как DLL; тот же путь уже нужен для
`deprecated_shaders/*`, которые и сейчас компилируются в рантайме вне
`#ifndef _SHIPPING`). Обязательное следствие: у клиента обязан быть список
«какие комбинации есть в контейнере» (в заголовке `.shd`) и путь отката при
промахе; промах не должен ронять рендер — это тот же код, что обрабатывает
отсутствующий шейдер сегодня.

***Формат `.shd`: что есть сейчас (снято с кода 2026-10-15).*** Писатель —
`Src/ShaderCompiler/ShaderCompiler.cpp:591` (`shaderCount =
shader.GetTotalCount()`) + `UberShaderDesc.cpp`; макет:
`{ uint32 shadersCount, [ {uint32 vsOffset, uint32 psOffset} × count ], блобы }`.
Читатель — `Src/Render/multishader.cpp:38-60`: `shadersCount = *(unsigned*)pBegin`,
таблица копируется в выровненный блок, смещения **абсолютные от начала файла**
(в `:80` `unsigned char* pBegin = (unsigned char*)pInfo - 4;`). Шейдеры
создаются лениво — на первом обращении `GetShaderPairByIndex(index)`
(`multishader.cpp:69-90`), при промахе по индексу возвращается
`defaultShaderPair` (`:24-31`, `:72`). Индекс пары = `Σ pinValue ×
pinMultiplier`: генерируется в `Src/Render/MaterialSpec.h:213`
(`GetShaderIndex() { return staticShaderIndex + GetDynamicShaderIndex(); }`,
пример `:200` `1 * GetRuntimePins().DoDepthOutValue`; ручной пример —
`DebugMaterial.h:55` `1 * DebugPinValue + 3 * SkeletalMeshPinValue`).
Обратное преобразование (индекс → значения пинов) в генераторе УЖЕ есть —
`ShaderCompiler.cpp:186-201`:
```cpp
int currentPinValue = idx;
for (int i = totalPinsCnt - 1; i > -1; --i) {
  int temp = currentPinValue / curPin.pinMultiplier;   // значение пина
  currentPinValue = currentPinValue % curPin.pinMultiplier;
}
```
и `GetTotalCount() = pins.back().maxPinValue * pins.back().pinMultiplier`
(`UberShaderDesc.cpp:25-29`).

***Проект `.shd` v2 (для шага 11).*** Семантика индекса НЕ меняется
(`GetShaderIndex()`/`BindShader` не трогаются), меняется заголовок:
```c
{ uint32 magic 'SHD2', uint32 version,
  uint32 pinsCount,   { uint16 maxPinValue, uint16 pinMultiplier } × pinsCount,
  uint32 targetsCount,{ uint32 targetTag,   uint32 tableOffset }      × targetsCount,
  uint32 shadersCount,
  для каждой цели: [ {uint32 vsOffset, uint32 psOffset} × shadersCount ] }
```
* `targetTag`: 0 = SM3 DXBC (D3D9), 1 = DXBC SM5 (DX12), 2 = SPIR-V (Vulkan);
  цель выбирает `MultiShader::OpenFile` по типу бэкенда (п.1-2 §3.z), а не
  dev-var'ом — второго источника истины быть не должно;
* плотная таблица сохраняется, **отсутствующая комбинация = offset 0**
  (0 никогда не валидное смещение: заголовок занимает начало файла) — это и
  есть «живой» бюджет пинов без смены индексации;
* таблица пинов в заголовке нужна только для рантайм-фолбэка: по ней клиент
  декодирует индекс в `D3D_SHADER_MACRO`-определения (тот же код, что
  `ShaderCompiler.cpp:186-201`) и компилирует `Shaders/<материал>.hlsl`
  DXC'ом. Исходники надо класть в контент: 49 `.hlsl` + `GlobalMasks.h` =
  **272 КБ** на весь корпус — копейки против 3,1 МБ текущих `.shd`;
* обратная совместимость: нет магии → читать как старый заголовок (SM3-only),
  чтобы старый контент пережил выкатку;
* промах фолбэка (нет исходника/ошибка компиляции) → `defaultShaderPair`,
  счётчик через `DebugVar` (по образцу `render_TranslationCount`,
  `multishader.cpp:12`), молча — не ронять рендер.

**Шаг 11.0 (перед кодом): снять список живых индексов.** Нужен входной набор
для генератора: dev-var `dump_shader_index` печатает пары
(`multiShaderIndex` = имя материала, `shaderIndex`) по всем
`GetShaderPairByIndex`/`BindShader` за прогон; прогоны — E2E-бой + несколько
локаций/режимов. Ориентир по объёму уже есть: 185 различных пар VS/PS на бой
(§1.4) ⇒ SPIR-V-набор ≈ 185 пар × 1–3 КБ ≈ 0,2–0,6 МБ на весь контент против
3,1 МБ SM3-блобов.

**Порядок реализации шага 11 (принят 2026-10-15).** SM3-слой НЕ перегенерируется:
он уже лежит в `Data/Shaders/UberShaders/*.shd` и доказан побайтовым паритетом
тула, поэтому v2 собирается **из существующего `.shd`** (таблица + блобы SM3
переносятся как есть) плюс SPIR-V для живых индексов. Писатель v2 — Python
(`tools/shader_build/`), а не C++: DXC нативный, прослойка SM3→SM6 (`shim.py`)
уже на Python, итерировать формат быстрее, чем в `Src/ShaderCompiler`. Тул
`ShaderCompiler.exe` остаётся производителем SM3 (для случаев, когда контент
пересобирается целиком) и не меняется. Клиентская часть — читатель v2 в
`Src/Render/multishader.cpp` (выбор цели по бэкенду, offset 0 → DXC в рантайме).
Решение не трогать `Src/ShaderCompiler` пересматривается, если понадобится
собирать SPIR-V внутри Windows-сборки (тогда нужен `dxcompiler.dll`).

**Шаг 11 закрыт (2026-10-15).** Реализовано ровно то, что описано выше, с двумя
отклонениями от проекта — оба сознательные:

* пины в заголовок **не положены**: рантайм-фолбэк DXC отложен вместе с SPIR-V-
  веткой (шейдеры из SPIR-V пока нечем создавать — шаг 12/13), а без фолбэка
  таблица пинов мёртвая. Итоговый заголовок: `V2H\0`, version, **v1Size**,
  targetsCount, `{tag, offset, count}` × целей; в конце файла трэйлер `SHD2`,
  `v2HeaderOffset`, version, `SHD2` (16 Б) — по нему читатель отличает v2, а
  читатель v1 остаётся внутри v1-префикса и не выходит за него;
* таблица цели **разреженная** (`{index, vsOffset, psOffset}` только для живых
  индексов), а не плотная `shadersCount × 8`: плотная стоила бы 4 Б × 36 864
  индексов у `BasicMaterial` (147 КБ) против 58 записей на весь корпус.
  Пропуск = отсутствие записи → в клиенте это ветка «нужен рантайм-компилятор».

Дополнение к формату (шаг 11.1, `dadadc6d3`): запись цели стала `{index,
vsOffset, vsSize, psOffset, psSize}` — **20 Б вместо 12**. Причина:
`IDevice::createShader(desc, binary, binarySize)` требует длину, а вывести её
из соседних смещений нельзя, потому что блобы дедуплицированы и их порядок не
связан с порядком записей. D3D9-читателю длина не нужна (`CreateVertexShader`
сама идёт по блобу), поэтому v1-префикс не тронут. Корпус: 3 026 868 →
**3 027 332 Б** (+464 Б = 58 записей × 8 Б). Гейты 11.1: Wine x64 `ИТОГ: OK`
(15 206 400 Б), native tiny10 `RC=0` (15 196 160 Б, `check_exe.py` OK), E2E
`2026.10.15-12.20.01` — 15 `ShaderContainer` + 58 `ShaderTarget` +
0 «нужен рантайм-компилятор», `ConfigPins` как эталон; `shot_diff` против
`refs/stage3-shd2-2026-10-15` — 0,97 % грубых при внутрисменном шуме прогона
1,32 % (SM3-слой побайтово не менялся — `--ref`).

Инструменты: `tools/shader_build/dxcdrv.py` — общий драйвер компиляции
(`spec_materials`, `total_pairs`, `decode_index` — тот же разбор, что
`ShaderCompiler.cpp:186-201`, `ShimTree`, `compile_dxc` с `-Zpr` по умолчанию);
`tools/shader_build/mkshd2.py` — писатель (`--live`, `--targets`, `--materials`,
`--verify`, `--ref`); `tools/shader_budget/shader_budget.py` переведён на
`dxcdrv` (числа бюджета не поехали: прогон `--pins 1 --only AOEMaterial` даёт
те же 560 Б и `position[9 x 16 B]`).

Замер корпуса: **15 контейнеров, 58 индексов, 0 пропусков**; v1-часть
2 724 840 Б → v2 3 026 868 Б, секции SPIR-V **302 028 Б (+11 %)** — бюджет
подтверждает оценку «0,2–0,6 МБ» выше.

Две правки исходников, которые вскрыл DXC (обе — только под `PW_MODERN_ARRAYS`,
SM3-ветка дословно старая):

* `Src/Render/Shaders/UIBaseMaterial.hlsl:98` — `volatile float line_factor;`
  (квалификатор SM3, fxc принимает) → DXC: `error: 'volatile' is a reserved
  keyword in HLSL`. Под современной целью — обычный `float`.
* `tools/spirv_probe/shim.py` — `UIFlashMaterial/ps` падал с
  `generated SPIR-V is invalid: [VUID-StandaloneSpirv-OpEntryPoint-09658]
  OpEntryPoint contains duplicate input variables with FragCoord builtin`:
  прослойка переводит `: POSITION` → `: SV_POSITION` в структуре, которой VS
  возвращает себя, а та же структура входит в параметр PS, у которого есть
  `VPOS` → два FragCoord. Починка: в PS-входе позиция такой структуры →
  `TEXCOORD191`. **Долг шага 13:** у PS с `VPOS` позиция из входа убрана, а
  читать её нечем — нужен `PixelPos` (прослойка печатает об этом заметку).
  Профиль `tools/hlsl_sm3_check --profile affected` дополнен `UIBaseMaterial
  Stripes=1` — профиль обязан расти вместе с любой правкой исходника.

Читатель v2 — `Src/Render/shadercontainer.h` (inline-парсер: `Shd2FindTrailer`,
`Shd2ParseHeader`, `Shd2Lookup`, `Shd2FindSection`, `enum ShaderTargetTag
{ stSm3=0, stDxbcSm5=1, stSpirv=2 }`) + `multishader.{h,cpp}`: `OpenFile()`
разбирает трэйлер и печатает `ShaderContainer: <имя> — v2, v1 N Б, целей K`
(при неудаче — откат к v1 с явной строкой), `GetShaderPairByIndex` под
dev-var'ом `shader_target` ищет индекс в цели и печатает `ShaderTarget: <имя>
#index — смещения блобов цели A/B, бэкенд цель ещё не умеет (шаг 12), беру SM3`.
В `pInfo` копируется весь файл, поэтому смещения секций остаются абсолютными.
Ветка создания шейдеров из SPIR-V сознательно не написана (нечем кормить до
шага 12) — мёртвый код в рантайме.

Доставка контейнеров на стенд: `python3 tools/shader_pile/mkpile.py build
--root /tmp/packdir --out /tmp/zzzresult_v2.pile --mask '*.shd'` на хосте и scp
в `C:\pw64\Game\Packs\zzzresult.pile` (бэкап старого обязателен). Тул
`FilePileCompiler.exe` на tiny10 сейчас **молчит** (RC=0, пустой `pile.log`,
pile не создаётся) — тупик, не воспроизводить.

Отклонение по картинке (сознательное, §2.15 п.15): вместе с шагом 11
перегенерирован `Data/Shaders/UberShaders/TerrainElementMaterial.shd` — он
устарел от исходников с `562b4c1fb` (развёртка цикла с динамическим индексом
сэмплера), отличались 12 из 41 PS-блобов (`texld_pp` → `texld`). A/B на стенде
один к одному (тот же exe, разный pile): старый контейнер против эталона этапа
2 — **0,36 %** грубых (верх полосы), новый против эталона — **0,59 %**, старый
против нового — **0,59 %**; значит читатель v2 побайтово-нейтрален, а сдвиг
даёт именно перегенерация. Новый эталон закоммичен:
`tools/render_shot/refs/stage3-shd2-2026-10-15/{pw_auto_bb_3000.png,
pw_auto_crop_3000.png}` (прогон `2026.10.15-11.50.52`) — дальше сверять по нему.

**4. Драйвер компиляции.** DXC — единый компилятор двух целей (`-spirv` и
`-T vs_5_0`); обязательные флаги из спайков: `-Zpr` (row-major, иначе
`mul(v,M)` теряет перевод), `setEntryName` (DXC сохраняет имя функции HLSL),
прослойка SM3→SM6 как шаг драйвера (`shim.py` → `Src/ShaderCompiler`),
`-enable-16bit-types` не нужен. Отдельный класс правок, найденный замером:
**неизвестные по размеру массивы в `register(cN)`** — DXC выпускает для них
`OpTypeRuntimeArray` внутри uniform-буфера, что для Vulkan невалидно (runtime
array разрешён только в SSBO); при генерации такие массивы обязаны получить
явный размер или уехать в SSBO (п.2).

Поправка к формулировке (2026-10-15, замер корпуса `tools/shader_budget`):
**массивов без размера в `Src/Render/Shaders/*.hlsl` нет** — утверждение про
«44 PostFX-шейдера с массивами без размера» было основано на выводе `shader_budget`
(в SPIR-V встречаются `OpTypeRuntimeArray`), а не на исходниках: файлов
`PostFX.hlsl`/`Bloom.hlsl` в дереве нет (Bloom/Blur — C++-модули, их шейдеры —
блобы `OnlineShaders/`, п.3), а runtime-массивы в отчёте — это либо
`StructuredBuffer` (легально), либо следствие динамической индексации
компоненты `float4`. Реальная правка — п.2 (SSBO-форма), она сделана шагом 10.

Ограничение среды генерации (замер 2026-10-15, снято в тот же день): встроенный
в Wine `d3dcompiler` реализован через vkd3d и на части материалов SM3 даёт
`E5017: Aborting due to not yet implemented feature: SM1 cast from bool to
integer` (`AOEMaterial`), `Failed to compile shader, vkd3d result -5`. Лечится
нативной MS-библиотекой: `d3dcompiler_47.dll` (x64, берётся с Windows-хоста —
`C:\Windows\System32\D3DCompiler_47.dll` 4 477 440 Б или SDK-ная
`Windows Kits\10\bin\10.0.26100.0\x64\d3dcompiler_47.dll` 4 741 560 Б) кладётся
рядом с тулом, загрузка — по полному пути (`hlsl_compile_test --dll`) либо
через `WINEDLLOVERRIDES="d3dcompiler_47=n,b"` (тул грузит `D3DCompile` сам по
имени). Замер: `tools/hlsl_sm3_check --profile affected --dll …` — **32/32
одинаковых DXBC** против снимка с tiny10 (`AOEMaterial/vs` 632 Б,
md5 `77c15e7c…`), прогон <45 с. Значит **среда шага 11 — хост + Wine +
нативная `d3dcompiler_47.dll`**; tiny10 нужен только для нативной сборки
клиента и E2E. В репозиторий DLL не кладётся (проприетарный бинарник), лежит
на хосте в `/home/rekon/PW/wine-tools/`.

**5. Инстансинг, шаг 2** (§2.10) — **закрыт 2026-10-15 (шаг 9 очереди)**:
лексика шва — маркер `kInstanceStream` в описании раскладки и в
`BindVertexBuffer` + `SetInstanceSource` (источник данных экземпляра: пустая
ручка = встроенный счётчик бэкенда, непустая = данные клиента) +
`SetInstanceCount`. Клиент больше не сдвигает потоки сам и не подменяет
декларацию: `D3D9Backend` держит слот экземпляра (`kInstanceSlot = 1`), VB-
счётчик и кэш инстансных вариантов декларации; `NvrhiBackend` от этого пути не
нужно ничего, кроме `instanceCount` в `drawIndexedInstanced`. Остаток —
`instanceData` из `c`-регистров → SSBO (п.2) — **закрыт шагом 10 (2026-10-15)**.

Замер семантики для драйвера компиляции — `tools/sem_probe` (DXC, 10 режимов
объявления индекса экземпляра, обе цели): `SV_InstanceID` **обязан быть `uint`**
(`error: SV_InstanceID must be uint.` в DXBC-пути, при этом SPIR-V-путь с `int`
компилируется молча — расхождение целей не видно на одной цели); макрос
раскрывается и в позиции семантики, и во всём объявлении, и если объявление
передаёт драйвер (`-D "INSTANCE_INDEX_DECL=uint inst : SV_InstanceID"`), т.е.
один исходник на обе цели реализуем без генерации текста; выдуманная семантика
(`V_SV_InstanceID`) компилируется как обычный вход потока — опечатку ловит только
декодирование SPIR-V (там `uint inst : SV_InstanceID` → `BuiltIn InstanceIndex`);
`-T vs_5_0` DXC поднимает до SM6 (`Promoting older shader model profile to 6.0
version`) — настоящего SM5 DXBC этим компилятором нет, для SM5 нужен `fxc`.

Остаток (не закрыт): в живом SPIR-V **ни один** из 58 блобов VS не использует
`BuiltIn InstanceIndex` (замер `ProbeShd2`, 2026-10-15) — индекс экземпляса
остаётся пользовательским атрибутом потока (`BLENDINDICES0`), потому что
лексический переход на `SV_InstanceID` (макрос `INSTANCE_INDEX_DECL`) отложен
в драйвер компиляции (п.4). Без него на Vulkan инстансные вызовы будут читать
атрибут из потока данных экземпляра — это тот же механизм, что и в D3D9, но
`SV_InstanceID` нужен для путей, где поток экземпляра не подаётся вовсе.

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

Замер 2026-10-15 (спайк `ProbeSdl3Window` в `tools/nvrhi_probe`, Wine +
RTX 3060): `SDL_Init(SDL_INIT_VIDEO)` → `SDL_CreateWindow 160x120` →
`SDL_PROP_WINDOW_WIN32_HWND_POINTER` → `vkCreateWin32SurfaceKHR` →
`VkSwapchainKHR 160x120, картинок 3` → `createHandleForNativeTexture` —
**всё PASS**. Значит окно можно отдавать SDL, не трогая WndProc-ввод. Грабли
статической линковки SDL3 нашим тулчейном (все три — следы
`Vendor/SDL3/cmake/PreseedMSVCCache.cmake`; перекрываются обычной, НЕ кэшевой
переменной ДО `add_subdirectory`): `HAVE_ROAPI_H=1` при отсутствующем `roapi.h`
(`fatal error C1083`, строка 176), `LIBC_HAS_ITOA=1` при сборке на `/MT`
(`unresolved external symbol itoa`, строка 63; `legacy_stdio_definitions.lib`
в этом тулчейне нет) и `/MD`-дефолт SDL (`LNK2005/LNK4098/LNK2019` из-за смеси
CRT — нужен `MSVC_RUNTIME_LIBRARY "MultiThreaded"` на цели `SDL3-static`, не на
ALIAS `SDL3::SDL3-static`). Прирост артефакта спайка: 811 008 → 2 266 624 Б
(SDL3-статика +1,45 МБ) — ориентир для `PW_Game.exe`.

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
  придётся переписывать. **Закрыто 2026-10-15: утверждён второй вариант**
  (сначала корпус, потом бэкенд) — очередь шагов 9–14 выше, решение §2.15 п.11.
- **44 PostFX-шейдера**: переписать на HLSL (оценка: 1 смена) или оставить
  D3D9-путь для PostFX до этапа 4 (тогда D3D9 не удаляется целиком).
  **Закрыто 2026-10-15:** D3D9 не сохраняем; замер сузил объём до **34** живых
  блобов (10 не имеют ссылок в коде), портим через дизассемблирование SM3 —
  решение §2.15 п.12, шаг 14 очереди.


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

6. **Клиповая позиция через out-параметр (ловушка корпуса, снята 2026-10-15).**
   28 материалов из 49 отдают позицию так:
   `VS_OUT MainPass_VSMain( VS_IN In, out float4 ClipSpacePosition: POSITION )`
   (`BasicMaterial.hlsl:147`, `AOEMaterial.hlsl:24`, `ParticleFXMaterial.hlsl:77`,
   `WaterDummyMaterial.hlsl`, `SkyMaterial.hlsl:29`, `RoadMaterial.hlsl:27`, …).
   В SM3 это валидная семантика, в SM6 системная семантика — `SV_POSITION`, а
   `POSITION` DXC трактует как пользовательскую: **gl_Position не пишется
   вообще** (замер SPIR-V: ни одной `BuiltIn`-декорации, `OpEntryPoint
   interface = (2,3,4)` — только пользовательские переменные). Пайплайн при
   этом создаётся без ошибки, вершина уходит в неопределённую позицию — то
   есть дефект немой. Прослойка `tools/spirv_probe/shim.py` в режиме `vs`
   переписывает `out <тип> <имя> : POSITION` → `: SV_POSITION`.
   Замер по живому корпусу (`ProbeShd2`, счётчик `BuiltIn`): до починки
   **41 пара из 58** без записи `Position`, после — **0**. Материалы, которые
   возвращают структуру с `: POSITION` (`UIBaseMaterial`, `UIFlashMaterial`,
   `UIFontMaterial`), попадали под прежнее правило `vs_ret_structs` и были
   корректны.

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
продукт DXVK не идёт — §2.15) или разбор wined3d/GLSL. *(Закрыто в тот же день:
DXVK, см. следующий раздел.)*

Значение для этапа 3: дефект не блокирует Vulkan-путь — `NvrhiBackend` говорит с
Vulkan напрямую, и смоук (`tools/nvrhi_probe`) под Wine на RTX 3060 проходит
целиком. То есть «клиент под Wine на хосте» годится как стенд этапа 3 (реальный
GPU, HW-тени, замеры), а D3D9-эталон на нём требует отдельной починки; пока
D3D9-регресс остаётся на tiny10.

### Сделано (проба: DXVK как D3D9-эталон под Wine — 2026-10-15, дефект «чёрная сцена» закрыт)

Решение пользователя — вариант (а) из трёх предложенных (§2.15 п.18): DXVK как
тестовая обвязка, в продукт не идёт.

Установка: DXVK 3.1.1 (tarball 18 041 512 Б, `wine-tools/dxvk-3.1.1/`, в git не
кладётся), `x64/d3d9.dll` (7 286 798 Б) → `PW/wine-client/Game/Bin/d3d9.dll`, запуск
с `WINEDLLOVERRIDES="d3d9=n,b"`. Импорт-таблица `d3d9.dll` DXVK не требует ни
`dxgi.dll`, ни `vulkan-1.dll` (Vulkan грузится динамически: «Vulkan: Found
vkGetInstanceProcAddr in winevulkan.dll»). Достаточно одного файла.

**Итог: 3D-сцена рисуется корректно** (спавн-площадка MOBA: земля, юнит, портал,
миникарта, UI), E2E доезжает до боя (`Entered lobby`, `mapload: all map objects
loaded` = 1, `unitstats`, `unitdamage`), `err:` 0 и `DXVK-[E]` 0 в stderr. Причина
прежней чёрной сцены подтверждена: wined3d шёл через OpenGL
(`wined3d_guess_card No card selector available for card vendor 0000`).

`ConfigPins: adapter="NVIDIA GeForce RTX 3060" vendor=0x10de device=0x2487
driver=2147483647.4294967295 mem=1024 vid=64 cpu=1700 MaxVertexShaderConst=256
IsNullRef=0 caps=0x7 pins=0` — имя и ID устройства теперь настоящие (у wined3d
было «GTX 470» / 0x06cd), версия драйвера фейковая (UINT_MAX — DXVK её не
публикует). `caps=0x7` и `pins=0` — как у wined3d, т.е. матчер `config.txt` не
сдвинулся (опасения по пинам не подтвердились).

**Живые индексы зависят от профиля устройства** (важное для шагов 13–14, §2.15
п.19): при `setvar shader_target 2` этот стенд делает 15 `ShaderContainer`, **77**
обращений к цели и **0** попаданий в корпус, собранный по снимку tiny10 (там 58
обращений, 58 попаданий, 0 пропусков). Примеры промахов: `UIFlashMaterial #1152`,
`#1024`, `#0`, `UIFontMaterial #4`, `#6`, `UIBaseMaterial #48`. Клиент при промахе
печатает «комбинации нет в цели, нужен рантайм-компилятор» и берёт плотную
SM3-таблицу (`multishader.cpp:180-190`), поэтому картинка не страдает; на
Vulkan-контуре такой промах = материал без шейдера.

Шум и эталон: `tools/render_shot/refs/wine-dxvk-2026-10-15/` (прогон
`2026.10.15-17.30.25`). Между прогонами одного бинарника `diff` 3,58% /
**`gross` 0,39%**, внутри прогона (3000 против 3100) 0,18%, `crop` между прогонами
0,00% (maxdelta=0) — та же полоса, что и на WARP, пороги `shot_diff.py` не меняются.
Сверять кадр Wine с кадром tiny10 нельзя: 81,57% / 7,40% gross на кадре 3000 из-за
зума камеры и состояния UI (на Wine-кадре оверлей выбора героя).

Процедура — `tools/wine_dxvk/dxvk_probe.sh [tag]` (в стенде копия —
`PW/wine-client/dxvk_probe.sh`, источник — репо): новая сессия → запуск →
маркеры лога → снимок → `pkill`. Грабли: клиент принимает только один инстанс —
второй прогон при живом первом молча выходит («Memory leaks info START» в stderr,
каталог сессии не создаётся), поэтому скрипт обязан убивать клиент в конце;
`DXVK_HUD=devinfo` полезен для замера FPS, но для гейта его ставить нельзя (рисует
оверлей поверх кадра).

Стенд переведён на v2-корпус: `Game/Packs/zzzresult.pile` = `/tmp/zzzresult_v3.pile`
(231 591 Б, 37 записей), откат `zzzresult.pile.bak-2026-10-15-dxvk` (168 125 Б,
v1 от 13.10). `Game/Profiles/autoexec.cfg` создан: `setvar auto_screenshot 3000`,
`setvar shader_target 2`.

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
  (`PENDING §9`, архив). Не блокирует этап 2 — фасад закрывает DDS-путь.
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

### Сделано (шаг 9 очереди: сдвиг потоков и атрибут экземпляра уходят в бэкенд — 2026-10-15)

Смысл: клиент перестаёт знать механизм инстансинга. В seam уходит лексика
(«это поток данных экземпляра»), в бэкенд — реализация (частоты потоков D3D9,
VB-счётчик `0..N-1`, инстансный вариант декларации). Реализация и обоснование
выбора лексики — §2.10 («Шаг 9 закрыт»), семантика для драйвера компиляции —
§3.z п.5 + `tools/sem_probe`.

| файл | что |
|---|---|
| `Src/Render/renderbackend.h` | `kInstanceStream = 0xffffffffu` (маркер потока данных экземпляра), `SetInstanceCount(unsigned)`, `SetInstanceSource(HBuffer, stride, offset)`, `CreateInstancedInputLayout` → `CreateInputLayout(const VertexFormatDescriptor&, HInputLayout*)` |
| `Src/Render/d3d9backend.{h,cpp}` | `kInstanceSlot = 1`; `ApplyInstanceState()` — дельта частот потоков + слот данных экземпляра перед каждым `Draw*`; `EnsureInstanceCounter(count)` — VB-счётчик (`int`, не float); `InstancedVariant(base)` + кэш `instancedDecls` (ключ `void*`); в `CreateInputLayout` — добавление атрибута `SHORT2/BLENDINDICES` и сдвиг потоков |
| `Src/Render/smartrenderer.{h,cpp}` | `BindInstanceVB` → `BindInstanceCounter()` / `BindInstanceSource()`; `EnableHardwareInstancing`/`DisableHardwareInstancing` вызывают `SetInstanceCount`; удалены `ApplyStates`, `lastStreamSourceFreqs`, `DeclMap`-подмена; в `NotePipeline` — новые счётчики `declTotal/declFrame`, `instDrawsFrame/instDrawsTotal` |
| `Src/Render/batch.cpp`, `InstancedMeshResource.cpp`, `ParticleFX.cpp` | потоки объявляются логически (`kInstanceStream`), сдвига в клиенте нет; `ParticleFX` биндит per-instance данные тем же `BindVertexBuffer`, что и геометрию |
| `Src/Render/renderer.cpp` | после `Reset()`/`ClearDeviceResources` — `SetInstanceCount(0)` вместе с `InvalidateBoundState()` |
| `tools/sem_probe/{sem_probe.hlsl, sem_probe.sh, README.md}` | проба семантики индекса экземпляра в DXBC и SPIR-V (10 режимов, см. §3.z п.5) |

Гейты (все пройдены):

* x64 Wine `./build_client_x64_wine.sh` — `ИТОГ: OK`, `build/PW_Game.exe`
  **15 200 256 Б** (−1024 против 15 201 280 до правки), `check_exe.py` OK
  (x64 PE32+, subsystem 2, Characteristics 0x0022, манифест id 1, 2.16.1.0).
* x64 native на tiny10 (задача `PwNativeBuild`, `C:\pwbuild\native_build.log`):
  `RC=0`, `C:\pwbuild\pwc\build\PW_Game.exe` 15 196 160 Б, `check_exe.py` OK.
* E2E обычный билд, лог `2026.10.15-08.44.10`: login `Success(5) uid=131` →
  lobby → `gs_svcid=gamesvc_00` → `Switch to EGameState::Finished by 3`,
  asserts 0, `ConfigPins: … MaxVertexShaderConst=256 IsNullRef=0 caps=0x0
  pins=0` дословно как эталон, сервер: `Game result delivered … attempts=0`.
* E2E инструментальный билд (`-DPW_RENDER_SHADER_STATS`), лог
  `2026.10.15-08.35.58`: числа выше (`semTotal=206`, `declTotal=15`,
  `instDrawsTotal=165749`).
* скриншот-гейт `tools/render_shot/shot_diff.py` против эталона
  `tools/render_shot/refs/stage2-2026-10-15/pw_auto_bb_3000.png`: diff **4,64%**,
  gross **0,40%** (внутри прогона bb_3000↔bb_3100 — 4,83% / 1,07%); грубые
  пиксели кластером в колонках 11–12 / рядах 4–6 (анимированная сцена), полос
  нет; кадр глазами корректен (база, герой, эффекты, UI, миникарта).

Грабли прогона (в скиле `pw-client`): после `Switch to EGameState::Finished`
процесс `PW_Game.exe` **продолжает жить**, поэтому `C:\tools\pw_exit.txt`
протухает, а `move /y` поверх `PW_Game.exe` даёт «Access is denied» — перед
деплоем нового бинарника нужен `taskkill /f /im PW_Game.exe`.

### Сделано (шаг 10 очереди: динамические массивы уходят из `c`-регистров в SSBO — закрыто 2026-10-15, `7716944fd`)

Мотивация и дизайн — §3.z п.2 (там же закрытие с числами), решение о
побайтовой SM3-ветке — §2.15 п.15.

| файл | что изменилось |
|---|---|
| `Src/Render/Shaders/_commonSkinning.hlsl` | `BonesMatrices`: `StructuredBuffer<float4x3>` под `PW_MODERN_ARRAYS` (70 × 48 Б) |
| `Src/Render/Shaders/BasicMaterial.hlsl` | обе формы `instanceData` (wrapper-структура 19 × 160 Б и `float4x3` 19 × 64 Б) |
| `Src/Render/Shaders/ShadowMaterial.hlsl`, `DecalMaterial.hlsl`, `DecalTerrainMaterial.hlsl` | `instanceData` (19 × 48 Б / 19 × 64 Б) |
| `Src/Render/Shaders/DebugMaterial.hlsl` | дубль объявления `BonesMatrices` |
| `Src/Render/Shaders/ParticleFXMaterial.hlsl` | `data`: `StructuredBuffer<INSTANCE_DATA>` (47 × 80 Б) |
| `Src/Render/Shaders/NatureMapMaterial.hlsl` | `hexParams` (40 × 16 Б) |
| `Src/Render/Shaders/AOEMaterial.hlsl` | `position` (9 × 16 Б) |
| `Src/Render/Shaders/FullScreenMaterial.hlsl` | два цикла постоянной длины развёрнуты под макросом (`SharpPinValue`, `Params`/`ColorCastCoef`), SM3-ветка — цикл |
| `Src/Render/Shaders/TerrainElementDebugMaterial.hlsl` | 4 слоя текстур развёрнуты под макросом, SM3-ветка — цикл |
| `tools/hlsl_sm3_check/` (новый) | снимок и сверка md5 DXBC по корпусу; профили `all`/`affected`/`fast`, `--save`/`--compare`, `--remote` (компилятор на tiny10) |
| `tools/hlsl_compile_test/` | `--disasm`/`--blob` (`D3DDisassemble`), `/Fo:build\` (`.obj` убран из репозитория) |
| `tools/shader_budget/shader_budget.py` | `--define NAME=ЗНАЧЕНИЕ`, таблица раскладки SSBO, класс UBO/SSBO по декорации `Block`/`BufferBlock` |
| `tools/spirv_probe/shim.py` | out-параметры PS: `COLORn` → `SV_TARGETn` с сохранением индекса |

Гейт. Правка живёт в препроцессоре с выключенным макросом, поэтому проверялась
не сборкой, а эквивалентностью компиляции: `tools/hlsl_sm3_check --profile
affected --remote pw-tiny10` — 16 комбинаций пинов × 2 стадии = 32 единицы,
**одинаковых DXBC 32, изменившихся 0, пропало единиц 0, новых 0**. Клиентский
бинарник и `.shd` не меняются (генерация `.shd` — отдельный шаг 11), сборка
и E2E к этой правке не применялись.

Замеры до/после (`--pins 1`, 70 единиц): неконстантная индексация членов UBO
**13 → 0**; байт UBO макс **7040 → 796**, среднее **811,0 → 358,0**; SSBO —
**11 буферов**; различных `(set,binding)` 9 → 10, max binding 8 → 9. При
`pins=0`: UBO макс 3744 → 768, SSBO 2.

Грабли (записаны в скил `pw-client`):
* под Wine `d3dcompiler` встроен через **vkd3d**: `E5017: Aborting due to not
  yet implemented feature: SM1 cast from bool to integer` на `AOEMaterial`,
  `Failed to compile shader, vkd3d result -5` — SM3-сверку можно делать только
  на Windows-хосте (`C:\tools\hlsl\hlsl_compile_test.exe`);
* класс дескриптора в SPIR-V надо определять по декорации (`Block` против
  `BufferBlock`), а не по storage class: DXC кладёт `StructuredBuffer` тоже в
  storage class `Uniform` (2);
* `float4x3` в SSBO — 64 Б, в `c`-регистрах — 48 Б: без wrapper-структ байты
  блока констант не совпали бы с макетом буфера;
* `ShaderCompiler.exe` под Wine раньше считался непрогоняемым (vkd3d) — снято:
  с нативной `d3dcompiler_47.dll` рядом с exe и
  `WINEDLLOVERRIDES="d3dcompiler_47=n,b"` корпус прогоняется и даёт побайтово
  верный DXBC (§6 «шаг 11.0»); полный прогон корпуса — ~30 мин.

### Сделано (шаг 11.0: паритет генератора `.shd` + список живых индексов — 2026-10-15)

**Паритет генератора под Wine.** `WINEDLLOVERRIDES="d3dcompiler_47=n,b" wine
../Tools/ClientCMake/build/x64-wine/ShaderCompiler.exe
../Src/Render/Shaders/MaterialSpec.xml '--outZ:\tmp\shd_wine_all\'` (cwd =
`pw/branches/r1117/Bin`, нативная `d3dcompiler_47.dll` копируется рядом с exe).
35 материалов, ~30 мин (`BasicMaterial` — 49 966 пар, ~32 пары/с). Результат:
**34 из 35 контейнеров побайтово совпали с репозиторными**. OS-ная и SDK-ная
`d3dcompiler_47.dll` (обе x64, привезены с tiny10) дают идентичный вывод —
версия компилятора исключена.

**Единственное расхождение — устаревший контент.**
`Data/Shaders/UberShaders/TerrainElementMaterial.shd`: таблица идентична
(240 пар, смещения совпали), различаются **12 из 41** уникальных PS-блобов
(35 байт в файле, все в диапазоне 15610..22406). Дизассемблирование
(`tools/hlsl_compile_test --blob`, для SM3 — `D3DXDisassembleShader` из
`d3dx9_43.dll`): в репозиторном блобе `texld_pp` (point-sampled), в свежем —
`texld`. Причина: `562b4c1fb` (10-14 23:48) развернул цикл
`for (i = 1; i <= TextureCountPinValue; ++i) tex2D(diffuseMapSampler[i], …)`
(динамический индекс сэмплера запрещён DXC/SM6), а контейнеры перегенялись
раньше — `ee747a959` (10-13). Вывод: **контейнер устарел относительно
исходников**; при сборке `.shd` v2 SM3-слой этого материала обязан быть
перегенерирован, и картинка terrain-кэша может сдвинуться → визуальный гейт с
новым эталоном (отдельная задача, трогает контент).

**Осиротевший контент.** `BasicWeatherMaterial.shd` (95 492 Б) лежит в
`Data/Shaders/UberShaders/` и числится в
`Src/System/FileSystem/processing_filelists/files.list`, но его нет ни в
`MaterialSpec.xml`, ни в коде материалов — генератор его не производит.
Кандидат на удаление (отдельная задача: контент-репо). Рядом в том же
каталоге — `.codes`, `.codes2`, `.shd2`: остатки SM2.0-пути, после зачистки
`--sm20` тул их больше не пишет.

**Список живых индексов (вход для генератора v2).** Новый dev-var
`dump_shader_index` (`Src/Render/multishader.cpp`, печатает
`ShaderIndex: Shaders/UberShaders/<Материал> #<индекс> (of <всего>)` один раз
на пару) + `tools/shader_build/collect_live_indices.py` + снимок
`tools/shader_build/live_indices-2026-10-15-moba1p.json`: по одному E2E-бою —
**58 живых пар по 15 материалам из 48 816 возможных индексов (0,12 %)**.
Топ: `BasicMaterial` 15 из 36 864, `UIFlashMaterial` 12 из 4 096,
`BasicFXMaterial` 7 из 96, `TerrainElementMaterial` 5 из 240. Это
количественное подтверждение выбранного варианта «живые комбинации +
рантайм-фолбэк» (§2.15 п.16). Покрытие сценария узкое (одна карта, один
режим) — список обязан расширяться прогонами других локаций/режимов.

Гейт. E2E tiny10 с включённым dev-var'ом (лог `2026.10.15-10.20.40`):
login `Success(5) uid=131` → lobby → `gs_svcid=gamesvc_00` →
`Switch to EGameState::Finished by 3`; `ConfigPins: … MaxVertexShaderConst=256
IsNullRef=0 caps=0x0 pins=0` — как эталон. Wine-сборка `build/PW_Game.exe`
15 202 816 Б, `check_exe.py` OK. Визуальный гейт не снимался: правка под
dev-var'ом, по умолчанию выключена.

Грабли: `nstl::hash_set::find` возвращает итератор (`error C2678: binary '!':
no operator found … 'nstl::_Ht_it<…>'`, `error C2088`) — дедуп по изменению
`size()`; `D3DDisassemble` не читает SM3-блобы (первые байты `00 03 FF FF`, а
не `DXBC`) — для SM3 нужен `D3DXDisassembleShader` из `d3dx9_43.dll`;
`--out<путь>` генератора не создаёт каталог (нужен `mkdir -p` до запуска).

### Сделано (шаг 11 очереди: `.shd` v2 — мультиблоб + DXC-драйвер + читатель — закрыто 2026-10-15)

Дизайн, отклонения от проекта и числа — §3.z п.3 (блок «Шаг 11 закрыт»).

| файл | что изменилось |
|---|---|
| `Src/Render/shadercontainer.h` (новый) | inline-парсер v2: `ShaderTargetTag`, `Shd2FindTrailer`, `Shd2ParseHeader`, `Shd2Lookup`, `Shd2FindSection` |
| `Src/Render/multishader.h` | поля `v1Size`, `imageSize`, `nstl::vector<Shd2Section> targets`; `HasShaderTarget`, `GetShaderTargetOffsets`, `GetV1Size` |
| `Src/Render/multishader.cpp` | `OpenFile()` разбирает трэйлер (`ShaderContainer: … v2, v1 N Б, целей K`, при провале — откат к v1), `GetShaderPairByIndex` под dev-var'ом `shader_target` ищет индекс в SPIR-V-цели и печатает `ShaderTarget: …`; ветка создания шейдеров из SPIR-V не написана (бэкенда нет) |
| `Src/Render/Shaders/UIBaseMaterial.hlsl` | `volatile` → `float` только под `PW_MODERN_ARRAYS` (DXC резервирует ключевое слово) |
| `tools/spirv_probe/shim.py` | починка дубля `FragCoord` в PS-входе (`UIFlashMaterial`), позиция структуры → `TEXCOORD191` + заметка про `PixelPos` (долг шага 13) |
| `tools/shader_build/dxcdrv.py` (новый) | общий DXC-драйвер: `spec_materials`, `total_pairs`, `decode_index`, `ShimTree`, `compile_dxc` (`-Zpr` по умолчанию) |
| `tools/shader_build/mkshd2.py` (новый) | писатель v2: SM3-слой из существующего `.shd`, SPIR-V по живым индексам, `--verify`, `--ref`, `--targets`, `--materials` |
| `tools/shader_budget/shader_budget.py` | переведён на `dxcdrv` (числа бюджета сохранены) |
| `tools/hlsl_sm3_check/hlsl_sm3_check.py` | профиль `affected` + `UIBaseMaterial Stripes=1` |
| `Data/Shaders/UberShaders/TerrainElementMaterial.shd` | перегенерирован (устарел с `562b4c1fb`): 12 из 41 PS-блобов, `texld_pp` → `texld` |
| `tools/render_shot/refs/stage3-shd2-2026-10-15/` | новый кадр-эталон (следствие перегенерации) |
| `.gitignore` | `zzz_budget_shaders_*/`, `zzz_build_shaders_*/` |

Гейты (все пройдены):

* x64 Wine `./build_client_x64_wine.sh` — `ИТОГ: OK`, `build/PW_Game.exe`
  **15 206 400 Б** (было 15 202 816), 0 `warning C`, `check_exe.py` OK.
* x64 native на tiny10 (`PwNativeBuild`, синк 15 файлов zip'ом, md5
  `multishader.cpp` совпал): `RC=0`, `C:\pwbuild\pwc\build\PW_Game.exe`
  15 196 160 Б, `check_exe.py` OK.
* E2E tiny10 с v2-контентом (pile 231 314 Б, 36 записей), лог
  `2026.10.15-11.42.13`: **15 строк `ShaderContainer: … v2`** (все контейнеры
  разобраны), **58 строк `ShaderTarget:`** — ровно по снимку живых индексов,
  **0 строк «нужен рантайм-компилятор»** ⇒ SPIR-V-секции покрывают все живые
  пары; login `Success(5) uid=131` → `gs_svcid=gamesvc_00` → `Switch to
  EGameState::Finished by 3`, asserts 0, `ConfigPins` дословно как эталон.
* визуальный гейт + A/B-атрибуция (один exe, старый pile против нового):
  старый pile против эталона этапа 2 — 4,62 % / **0,36 %** грубых; новый — 4,44 %
  / **0,59 %**; старый против нового — 3,98 % / 0,59 %; внутри прогона
  5,45–6,18 % / 1,07–2,30 % (анимация). Вывод: читатель v2 нейтрален, сдвиг
  даёт перегенерация `TerrainElementMaterial` (в бою он используется — пары
  #95/#155/#215). Новый эталон закоммичен.

Воспроизведение контента для стенда (v2-корпус в репозиторий НЕ кладётся —
артефакт 3,0 МБ, выводится детерминированно):

```sh
python3 tools/shader_build/mkshd2.py \
        --live tools/shader_build/live_indices-2026-10-15-moba1p.json \
        --out /tmp/shd2_v2 --verify --ref pw/branches/r1117/Data/Shaders/UberShaders
mkdir -p /tmp/packdir/Shaders/UberShaders
cp pw/branches/r1117/Data/Shaders/UberShaders/*.shd /tmp/packdir/Shaders/UberShaders/
cp /tmp/shd2_v2/*.shd /tmp/packdir/Shaders/UberShaders/
python3 tools/shader_pile/mkpile.py build --root /tmp/packdir \
        --out /tmp/zzzresult_v2.pile --mask '*.shd'
# scp /tmp/zzzresult_v2.pile pw-tiny10:C:/pw64/Game/Packs/zzzresult.pile (с бэкапом старого)
```

Грабли: `nstl::vector::operator[]` берёт `difference_type` → `warning C4267` на
`size_t`-счётчике; `FilePileCompiler.exe` на tiny10 молчит (pile собирается
хостовым `mkpile.py`); `schtasks /run` по уже идущей задаче даёт «is currently
running» и прогон не стартует — перед запуском нужен `taskkill /f /im
PW_Game.exe` и проверка `schtasks /query … /fo LIST | findstr Status`.

### Сделано (макет descriptor set в прослойке + починка клиповой позиции — 2026-10-15)

Два закрывающих шага перед спайком `NvrhiBackend`.

**Макет сетов (п.1).** `tools/spirv_probe/shim.py` — режим `set_layout`:
`SPACE_TEXTURES = 2`, `SPACE_SAMPLERS = 3`, `SPACE_BUFFER = 1`; явные
`register(tN/sN/bN/uN)` получают `, spaceN` (HLSL `space` == descriptor set),
`c`-регистры снимаются в авто-`$Globals` (set 0), ресурсам без `register`
номер и set назначает прослойка: текстуры `t space2`, `SamplerState`
`s space3` (`s0` — общий `g_smp`), `StructuredBuffer`/`ByteAddressBuffer` →
`t space1` (DXC требует класс `t` для `StructuredBuffer`).
`dxcdrv.ShimTree(set_layout=...)`, `mkshd2` пишет корпус с макетом по умолчанию
(`--flat-sets` — для сравнения), `shader_budget --set-layout` + печать раскладки.

Замер корпуса (`--pins 1 --define PW_MODERN_ARRAYS=1 --set-layout`, 70/70
единицы): наборы ровно `0 ($Globals UBO) / 1 (SSBO) / 2 (текстуры, binding
до 6) / 3 (сэмплер 0)`; SSBO 11 буферов; единиц с неконстантной индексацией
членов UBO — 0. `ProbeShd2` на новом корпусе (58 живых пар, RTX 3060 через
Wine): **наборов 4, 116/116 шейдеров, 58/58 пайплайнов с четырьмя
`BindingLayout`** (`setRegisterSpaceAndDescriptorSet(N)`), максимум binding
в сете 2 = 7 (`RoadMaterial` ps — 7 текстур). Типичная строка:
`#13 vs[5060 Б]: 0:0=ubo 1:0=ssbo  ps[3088 Б]: 3:0=smp 0:0=ubo 2:0=tex`.
Размер корпуса 3 027 332 → 3 006 856 → **3 006 044 Б** (макет, затем починка
позиции).

**Починка клиповой позиции (п.6 спайка B0).** 28 материалов из 49 отдают
позицию `out float4 … : POSITION`; в SM6 это пользовательская семантика, и
DXC **не пишет gl_Position** (в SPIR-V нет ни одной `BuiltIn`-декорации) —
пайплайн создаётся, вершина уходит в неопределённость. Прослойка в режиме
`vs` переписывает такие out-параметры на `SV_POSITION`. Счётчик `BuiltIn` в
`ProbeShd2`: до починки **41 пара из 58** без `Position`, после — **0**.

**Остаток по инстансингу:** `VS с InstanceIndex = 0` во всём живом корпусе —
переход на `SV_InstanceID` (лексика `INSTANCE_INDEX_DECL`) по-прежнему
отложен в драйвер компиляции (п.4).

Файлы: `tools/spirv_probe/shim.py`, `tools/shader_build/{dxcdrv.py,mkshd2.py}`,
`tools/shader_budget/shader_budget.py`, `tools/nvrhi_probe/probe.cpp`
(`SpvRes::builtIns/biPosition/biFragCoord/biInstanceIndex`, печать
`BuiltIn: VS без Position N, …`). Коммиты `8a641c8ac` (макет) и следующий
(починка позиции + счётчики).


### Сделано (шаг 11.1 + предварительный спайк шага 12: .shd v2 грузится в Vulkan, SDL3-окно под Wine — 2026-10-15)

Два коммита: `dadadc6d3` (формат: размеры блобов в записях цели) и `ff7d237c0`
(спайк в `tools/nvrhi_probe`). Смысл: до написания `NvrhiBackend` сняты три
неизвестных — грузятся ли блобы нашего писателя, сколько дескрипторов они
требуют и поднимает ли vendored SDL3 окно под Wine.

| место | что добавлено |
|---|---|
| `tools/nvrhi_probe/probe.cpp` | `Shd2Spike` (читатель v2: трэйлер → заголовок → секции), `ScanSpv` (мини-парсер SPIR-V: `Binding=33`, `DescriptorSet=34`, `Block=2`, `BufferBlock=3`, `OpTypeImage=25`/`Sampler=26`/`SampledImage=27`/`Array=28`/`RuntimeArray=29`/`Struct=30`/`Pointer=32`, `OpVariable=59`, `OpDecorate=71`), `ProbeShd2` — создаёт `IShader` и пайплайн с `BindingLayout` по фактической карте; env `PW_SHD2=<.shd>`, `PW_SHD2_MAX`, `PW_SHD2_VERBOSE`; `ProbeSdl3Window` — SDL3-окно → HWND → surface → swapchain → `createHandleForNativeTexture` |
| `tools/nvrhi_probe/CMakeLists.txt` | vendored SDL3 в спайке: путь `../../pw/branches/r1117/Vendor/SDL3`, описанные выше оверрайды (`HAVE_ROAPI_H`, `LIBC_HAS_ITOA`), `MSVC_RUNTIME_LIBRARY "MultiThreaded"` на `SDL3-static` |
| `Src/Render/shadercontainer.h`, `multishader.{h,cpp}`, `tools/shader_build/mkshd2.py` | запись цели v2 = 20 Б с размерами блобов (`kShd2EntrySize`), `Shd2Lookup`/`GetShaderTargetOffsets` отдают `vsSize`/`psSize`, `DebugTrace` печатает «блоб цели <off>+<size> / <off>+<size> Б» |

Замеры (RTX 3060 через Wine, `DISPLAY=:97`):

* **15 контейнеров, 58 пар: 116/116 шейдеров создано, 58/58 пайплайнов с
  раскладкой создано** — ни одного отказа у Vulkan на блобах `mkshd2.py`;
* все ресурсы в одном наборе (set 0), `binding` максимум 9 (`RoadMaterial` ps —
  7 текстур); по корпусу `smp=52 tex=97 ubo=109 ssbo=13` (SSBO: BasicMaterial 7,
  ParticleFX 2, Drop 2, NatureMap 1, BasicFX 1) — вывод в §3.z п.1;
* SDL3: `SDL_CreateWindow 160x120` → `SDL_PROP_WINDOW_WIN32_HWND_POINTER` →
  `VkSurfaceKHR` → `VkSwapchainKHR 160x120, картинок 3` → NVRHI принял
  бэк-буфер — всё PASS; win32-спайк рядом тоже PASS (формат 44 =
  `VK_FORMAT_B8G8R8A8_UNORM`).

Гейты шага 11.1: Wine x64 `ИТОГ: OK` (15 206 400 Б), native tiny10 `RC=0`
(15 196 160 Б) + `check_exe.py` OK, E2E `2026.10.15-12.20.01` (15
`ShaderContainer`, 58 `ShaderTarget`, 0 пропусков цели, `ConfigPins` = эталон,
`Switch to EGameState::Finished by 3`), `shot_diff` 0,97 % грубых при шуме
прогона 1,32 % — SM3-слой побайтово не менялся.

Грабли спайка: у `OpVariable` первое слово — Result Type, а не id переменной
(первая версия сканера давала пустой список ресурсов); `opOfId` обязан
заполняться только объявлениями типов, иначе у инструкций-значений туда
попадает Result Type; `OpDecorate` без операнда (`Block`, `BufferBlock`) имеет
word count 3 — условие `wc >= 4` молча их пропускало, и SSBO оставались
невидимы даже после перехода на декорации; `std::max` ломается из-за макросов
`max/min` в `windows.h` (`error C2589`); у `nvrhi::Format` нет `B8G8R8A8_*`
(есть `BGRA8_UNORM`), у `TextureDesc` нет `setIsDepthStencil` (глубина из
формата), у `FramebufferDesc` — `setDepthAttachment`; `build_probe_wine.sh`
возвращает RC=1, когда не передан `--run` (ориентироваться на `error C` в логе).

### Сделано (спайк шага 12: блоб из `.shd` v2 рисует в Vulkan — 2026-10-15, `1d2ac6a45`)

`ProbeShd2Draw` в `tools/nvrhi_probe/probe.cpp` (env `PW_SHD2_DRAW=1`): RT
256×256 `BGRA8_UNORM` + depth `D24S8`, 6 вершин левой половины клип-пространства,
по одному `BindingLayout`/`BindingSet` на descriptor set
(`setRegisterSpaceAndDescriptorSet`), `frontCounterClockwise=true`, readback через
`createStagingTexture`/`mapStagingTexture`. Константы набиваются по фактическим
доступам шейдера: `ScanSpv` собирает `SpvRes::accesses`
(`OpAccessChain`/`OpInBoundsAccessChain` с постоянным индексом + `OpMemberDecorate`
`Offset`/`MatrixStride`) — матрицам единичные, скалярам 1.0; текстуры и сэмплеры —
пустышки (4×4 без содержимого / point-clamp); SSBO — пропуск (их содержимое нулями
не подменить).

Замер по 15 контейнерам v5: **3 пропущено** (`Drop`, `NatureMap`, `ParticleFX` —
SSBO), **12 пробовано, 8 нарисовали геометрию строго в левой половине**
(`Basic`, `BasicFX`, `Decal`, `Sky`, `TerrainElement`, `UIFlash`, `Water`,
`WaterDummy`); у `WaterDummy` точное совпадение — `красных слева 32768/32768,
справа 0, зелёных 32768`. **4 не нарисовали** (`FullScreen`, `Road`, `UIBase`,
`UIFont`): у них вершинных атрибутов больше, чем `POSITION`, и/или матрица собрана
из четырёх скалярных членов (патч «1.0 в 16 Б» её схлопывает) — это работа шага 13
(реальные атрибуты и константы из клиента), к самим блобам претензий нет.

Грабли: `depth`-текстуре обязательны `setInitialState(DepthWrite)` +
`keepInitialState`, иначе `Unknown prior state of texture draw ds`; глубину надо
очищать (`clearDepthStencilTexture` — в этой версии NVRHI нет `clearDepthTexture`),
при дефолтном `depthFunc=Less` нечищеный буфер вырезает кадр **без единого
сообщения NVRHI**; `OpConstant` = **43** (не 50) — на неверном номере сканер молча
не нашёл ни одного доступа к членам; VS и PS делят `$Globals` → дедуп по
`(set,binding)`, иначе в один layout попадают два `ConstantBuffer(0)`; operand у
`OpAccessChain` — id константы, а не номер члена (первый разбор дал «members 10 и
11», на самом деле 1 и 2).

### Сделано (шаг 12a: Vulkan-бэкенд поднимается внутри PW_Game.exe — 2026-10-15)

Сняты все неизвестные этапа 3, которые нельзя было снять спайком в `tools/`:

- **граф сборки клиента**: `Tools/ClientCMake/gen_cmake.py` получил vendor-блок
  (`add_subdirectory(Vendor/NVRHI)` с выключенными `NVRHI_WITH_*`, кроме Vulkan),
  `nvrhi.lib` + `nvrhi_vk.lib` линкуются в `PW_Game.exe`, CRT согласован (`/MT`);
- **пофайловый `/std:c++17 /EHsc`** только на `Src/Render/nvrhibackend.cpp`
  (`X64_EXTRA_OPTS`): остальной `Render` компилируется на дефолте cl 14.44
  (C++14), поэтому у бэкенда pimpl и в заголовке нет ни одного типа NVRHI/Vulkan;
- **динамический диспетчер Vulkan внутри exe**: `LoadLibraryA("vulkan-1.dll")` +
  `VULKAN_HPP_DEFAULT_DISPATCHER`; импорт `vulkan-1.dll` в exe запрещён проверкой
  `check_exe.py` (только x64) — машина без Vulkan-драйвера запускается как раньше;
- **`NativeWindow` seam'а годится для `VkSurfaceKHR`** (`vkCreateWin32SurfaceKHR`
  на `NativeWindow.p` = HWND клиента), отдельное SDL3-окно шагу 12 не нужно.

Реализация — `Src/Render/nvrhibackend.{h,cpp}` (`NvrhiBackend : IRenderBackend`).
Он создаётся в `Renderer::CreateDevice` **дополнительно** к D3D9-бэкенду и только
при env `PW_VK_SPIKE=1` (в проде не выставлен), его кадр (clear + present)
кладётся поверх D3D9-вывода в `Renderer::Present`; результат в `hr` не попадает,
чтобы потеря D3D-устройства определялась именно по D3D-вызову. Незакрытые методы
seam'а — счётчики `Unsupported(st_*)`, их дамп и есть work-list шага 13, снятый
по фактическим вызовам клиента, а не по догадке.

Замер (Wine-стенд `PW/wine-client/`, `PW_VK_SPIKE=1`, прогон `2026.10.15-17.00.30`):

```
NvrhiBackend: устройство 2 шт., выбрано NVIDIA GeForce RTX 3060 (api 1.4.329)
NvrhiBackend: swapchain 800x600, картинок 3, формат 44, режим present 2
NvrhiBackend: Vulkan-бэкенд поднят — OK NVIDIA GeForce RTX 3060
Renderer: PW_VK_SPIKE up
NvrhiBackend: пересоздание swapchain'а #1
NvrhiBackend: swapchain 1024x768, картинок 3, формат 44, режим present 2
VkStatsframe: presents=599 recreates=1 unsupported=0 swap=1024x768 device=NVIDIA GeForce RTX 3060
```

Клиент доезжает до `mapload: all map objects loaded`, скриншот — всё окно
1024×768 залито цветом Vulkan-кадра (0.10, 0.35, 0.65), т.е. present идёт каждый
кадр. Темп ~5 present/с совпадает с темпом D3D9-кадров этого же клиента под Wine
(CPU/Wine, не бэкенд) + синхронный `waitForIdle()` в спайке.

Два замера, каждый из которых стоил отдельной правки:

1. **Вендор обязан собираться как Release (`/O2 -DNDEBUG`).** Флаги клиента
   приходят из vcproj, `-DNDEBUG` в vendor-целях не было → живые `assert` внутри
   Vulkan-Hpp: `VULKAN_HPP_ASSERT(d.getVkHeaderVersion() == VK_HEADER_VERSION)`
   в каждом wrapper'е (`Vendor/Vulkan-Headers/include/vulkan/vulkan_funcs.hpp:1748`,
   `VK_HEADER_VERSION = 352`). Под Wine winevulkan сообщает **свою** версию
   заголовков, и на первом же `vk::Device::createSemaphore` из nvrhi вылезает
   модальный CRT-диалог «Assertion failed! … Expression: d.getVkHeaderVersion()
   == VK_HEADER_VERSION» — поток рендера висит на диалоге, клиент стоит
   (прогоны `16.35.32`, `16.43.46`, `16.46.36`). В `tools/nvrhi_probe` дефект не
   проявлялся: там `CMAKE_BUILD_TYPE=Release` и у `nvrhi_vk` в
   `build/build.ninja:1073` стоят `/O2 /DNDEBUG`. Правка в vendor-блоке:
   `target_compile_options(${_vt} PRIVATE /EHsc /O2 /Ob2)` +
   `target_compile_definitions(${_vt} PRIVATE NDEBUG)`.
2. **Окно клиента resize'ится ПОСЛЕ `CreateDevice`** (800×600 → 1024×768 при входе
   в лобби). Без пересоздания swapchain'а каждый кадр даёт
   `VK_ERROR_OUT_OF_DATE_KHR` — 1378 строк за прогон, `VkStats presents=1`,
   картинка остаётся 800×600 посреди окна. Правка: `DestroySwapchain` /
   `RecreateSwapchain`; в `Present` — сверка `GetClientRect` с текущим размером
   (размер спрашивается у **окна**: `currentExtent` в уже созданном swapchain'е
   равен старому размеру) и обработка `OUT_OF_DATE`/`SURFACE_LOST` с пропуском
   кадра; лог дозируется (`#<=4 || %120`). После правки: `recreates=1`,
   `OUT_OF_DATE` — 0 строк.

Грабли прогона: `pkill -f 'PW_Game.exe'` матчит собственную команду агента
(самоубийство оболочки, rc=143) — убивать через `pgrep -x PW_Game.exe`; второй
клиент при живом первом молча выходит и своего каталога логов не создаёт, а
`ls -t` логов показывает каталог живого процесса — перед каждым прогоном старый
процесс обязан быть убит.

Не закрыто: `unsupported=0` лишь потому, что клиент вызывает на спайке только
`Present` — перевод настоящих проходов (шейдеры из `.shd` v2 в клиентском exe,
атрибуты, константы, глубина, `GetBackBuffer`) это шаг 13.

Регресс-гейт на tiny10 снят (прогон `2026.10.15-15.05.42`, exe 15 602 688 Б,
откат `PW_Game.exe.bak-2026-10-15-12a`): login `Success`, `mapload: all map
objects loaded`, `ShaderContainer` 15 / `ShaderTarget` 58, «нужен рантайм-
компилятор» 0, `CRC32 mismatch` 0, `Virtual Memory Usage: 2235 mb`. Кадр-эталон
`pw_auto_bb_3000.png` против `refs/stage3-shd2-2026-10-15/`: **diff 4,27%,
грубых 0,44%**; для масштаба — те же два кадра **одного** прогона (3000 против
3100) дают 6,59% / **2,42%**, т.е. расхождение в 5,5 раза меньше шума анимации.
Эвристика «ПОХОЖЕ НА ПОЛОСУ: ряд 4» при этом ложная: `--mask` показывает
кластеры вокруг юнита/портала (анимация), полос нет — вывод по маске, а не по
строке эвристики (та же строка выпадает и при сравнении 3000/3100 внутри
прогона).

### Сделано (шаг 13.0: канонический c-банк вместо таблицы переноса — 2026-10-15)

Мотивация. §3.z п.2 требовал складывать в писатель таблицу «const-регистр →
байт UBO» на материал. Замер показал, что дешевле сделать раскладку UBO
**побайтовой копией** D3D9-таблицы констант: тогда бэкенд отдаёт свой `ConstBlock`
(§2.7 `conststore`) как есть, без таблицы. Механизм — в прослойке объявления
c-констант снимаются, а вместо них генерируется один `cbuffer` на стадию с
`packoffset` по номеру из исходника:

```
cbuffer pwCBankVS : register(b0, space0) {   // PS — b1
    float4x4 World           : packoffset(c0);
    float4x4 ViewProjection  : packoffset(c8);
    ...
};
```

Инструменты: `tools/shader_build/cbank.py` (обзор корпуса `--max-pins`, генератор
`bank_block`), режим `--cbank` в `mkshd2.py`, `strip_cdecls` в `shim.py`,
`dxcdrv.ShimTree(cbank=...)`.

Что дал замер (всё — по живому корпусу 15 контейнеров / 58 пар):

1. **`packoffset` обязателен.** Без него DXC раскладывает по своим правилам:
   `float4x4 World` + `float4 amb` дают Offset 0 и 16 вместо 0 и 64.
2. **Конфликтов live-членов — 0** на всём корпусе (в т.ч. при всех пинах в
   `maxPinValue`). Максимумы диапазона: `GrassMaterial` VS c253, `RoadMaterial`
   VS c235, `BasicMaterial` PS c221 — в пределах 256 регистров D3D9.
3. **Live-множество зависит от пинов.** `POINTLIGHTSDATA c10`,
   `POINTLIGHTFACTOR c18`, `SCREEN_TO_TEXTURE c10`, `PSHADER_SHCONSTS c12`
   перекрываются НАМЕРЕННО: в SM3 fxc для неиспользуемого имени регистр не
   выделяет. Поэтому банк строится по препроцессу DXC (`dxc -P -Fi`) конкретной
   пары, а не материала целиком.
4. **Два класса членов.** `live` — имя дошло до точки входа, номер из исходника.
   `extra` — не дошло, но имя упомянуто в теле функции, оставшейся в TU: DXC
   разрешает имена во ВСЕХ телах, даже недостижимых (`screenScaleOffset` читает
   мёртвая `ScreenToTexture`, `_common.hlsl:52`; без объявления —
   «use of undeclared identifier»). Таким членам выдаётся свободный номер в
   хвосте банка; код их не читает. Из-за этого у `GrassMaterial` VS банк дотягивает
   до 272 регистра — вне D3D9-пространства (256), для Vulkan-UBO безразлично.
5. **Размеры массивов — из числовых макросов** `GlobalMasks.h`
   (`[2 * POINTLIGHTSCOUNT]`, `[(POINTLIGHTSCOUNT + 3)/4]` → `num_map()`). Без
   этого шаг считался 1 регистром, DXC резервировал 8 и падал
   «packoffset overlap between 'screenScaleOffset', 'lightData'».
6. **Экзотических типов в живом корпусе нет**: 185 членов — только
   `float4x4/float4/float3/float2/float`. Это важно: у `float4x3` размер в
   std140 (64 Б) больше, чем 3 c-регистра (48 Б), и раскладка разошлась бы с
   D3D9. При появлении DXC сам упадёт на пересечении.
7. **OnlineShaders доходят через `..\OnlineShaders\_commonDepthRead.h`** (обратные
   слэши) и содержат c-объявления. Дерево прослойки перенесено на уровень глубже
   (`<tree>/src/`, рядом зеркало `GlobalMasks.h` и переписанный
   `<tree>/OnlineShaders/`), иначе `..` ведёт в настоящий `Src/Render` и DXC
   падает «redefinition of 'madValueUV'».

Гейты. `mkshd2.py --cbank` по 15 контейнерам / 58 парам: **0 пропусков, 0
ошибок** (без правки дерева было 2 падения на FullScreenMaterial).
`tools/nvrhi_probe` по тому же корпусу: **116/116 шейдеров, 58/58 пайплайнов с
раскладкой**; `WaterDummyMaterial` рисует пиксель-в-пиксель, а авто-патч констант
теперь бьёт по смещениям **0 и 128** = `c0`/`c8` (в старом авто-`$Globals` было
64 и 128) — раскладка стала ровно той, что у D3D9.

Артефакт пробы (не раскладка): `Water/Road/UIBase/UIFont/FullScreen` при
авто-патче не рисуют геометрию — спайк пишет `1.0` во все доступные члены, включая
массивы-матрицы (`mtxRefraction[4]`). Для `WaterMaterial` при ручной раскладке
`PW_SHD2_MAT0=0 PW_SHD2_MAT1=128` геометрия появляется (32768/32768 слева) →
вопрос к патчеру спайка, не к банку.

Решение. Корпус v2 с банком в репо пока НЕ коммитится: клиент SPIR-V-слой ещё не
читает, SM3-слой не меняется (визуальный гейт не трогаем), а раскладка может
уточниться на шаге 13. Перегенерация и коммит — вместе с потреблением. В репо
сейчас лежат v1-контейнеры (`Data/Shaders/UberShaders/*.shd`, 35 файлов), v2
доставлялся на стенд pile'ом (`Packs/zzzresult.pile`).

### Сделано (шаг 12b: первый вывод через seam внутри PW_Game.exe — 2026-10-15, `386cf1016`)

`Src/Render/nvrhibackend.cpp` 934 → ~1900 строк: закрыта 28-методная часть
work-list'а (заглушек осталось 10: `SetTexture`, `DrawIndexedUP`, `DrawUP`,
`CreateTexture2D`, `CreateTextureCube`, `UpdateTexture`, `GetRenderTargetData`,
`ColorFill`, `StretchRect`, `GetRenderTarget` — это и есть очередь шага 13).

Что заработало: `CreateVertexBuffer/CreateIndexBuffer` (CPU-зеркало
`std::vector<unsigned char> cpu`, на `Unlock*` — `writeBuffer` целиком),
`CreateVertexShader/CreatePixelShader` через `SpvScan` (мини-сканер SPIR-V:
`OpEntryPoint` — имя точки входа, `OpVariable` класса Input + `OpDecorate
Location` — входы VS; заголовок SPIR-V — **5 слов**, разбор обязан начинаться с
`i = 5`), `CreateInputLayout` + `BuildInputLayout` (location берётся из SPIR-V по
имени семантики, `elementStride` — из `SetStreamSource`, `isInstanced` — из
`kInstanceStream`), `FindOrCreatePipeline` (кэш по
`vs,ps,layout,rt,ds,prim`; ключ шага 8 ещё не участвует), `CreateSurface` (RT
`BGRA8_UNORM`, глубина `D24S8` + `setInitialState(DepthWrite)` +
`setKeepInitialState(true)`), `Clear`, `SetViewport`/сциссор, `Draw`/`DrawIndexed`
(`drawIndexed.setVertexCount` = число индексов), `ReadSurface` (ленивая
staging-текстура → `copyTexture` → `mapStagingTexture`, ответ `plBGRA`).
Константы: два UBO по 4096 Б в set 0 (`binding 0` = VS-банк, `1` = PS-банк),
загрузка = срез тени, смещение члена = `16*номер регистра` — таблица переноса не
нужна (подтверждение §3.z п.2, шаг 13.0).

КЛЮЧЕВОЙ ДЕФЕКТ, найденный только тестом. `FindOrCreatePipeline` строил
раскладку в `LayoutObj::il`, а в `setInputLayout` передавал пустой локальный
handle. Vulkan-бэкенд обёрнут в `if (inputLayout)`
(`Vendor/NVRHI/src/vulkan/vulkan-graphics.cpp:300`) → пайплайн создавался
**без** вершинных описаний, атрибуты читались нулями, геометрия вырождалась
молча: `пайплайны=1/1, вывод=1, загруз=2`, нарисовано 0 пикселей, ни одного
сообщения об ошибке. Правка — `il = d.layoutObjs[d.curLayout].il` + проверка на
ноль.

Гейт (`RunSeamDipTest`, env `PW_VK_DIP=1`, однократно на 30-м кадре спайка;
прогон `2026.10.15-19.13.35`, exe 15 655 424 Б, Wine + Xvfb :97, DXVK не нужен):
`DIP: пиксели на месте — OK комбинация #1, блобы VS 1120 Б / PS 448 Б; нарисовано
16384/65536 пикселей, каналы чистые 16384, справа 0, макс R=255, выборка
BGRA=0,0,0,255` → `Renderer: PW_VK_DIP OK`. 16384 = ровно площадь треугольника
(-1,-1)-(0,-1)-(-1,1) в offscreen 256×256; правая половина пуста. Это доказывает
одновременно: атрибуты доходят из VB, банк разложен побайтово как в D3D9 (при
нулевом банке `World`/`ViewProjection` обнулились бы → пикселей не было бы), PS
пишет в цвет. Индекс комбинации берётся из первой записи SPIR-V-секции
(`Shd2U32(base + spv->offset + 8)`), а не из нулевого члена таблицы.

Диагностика, добавленная по ходу. (1) `nvrhi::vulkan::DeviceDesc::errorCB` →
`VkMsg[severity]: текст` (без колбэка сообщения NVRHI уходят в
`OutputDebugStringA`, который под Wine не виден вовсе — wine-лог прогона пуст).
(2) `RecreateSurface()` — при входе в бой клиент пересоздаёт окно, surface
оставался привязан к мёртвому HWND: `surface capabilities — НЕ УДАЛОСЬ` /
`VK_ERROR_SURFACE_LOST_KHR` (с DXVK), без DXVK не проявлялось. (3) `VkRes%s:`
счётчики ресурсов/пайплайнов/вывода/загруз. (4) `VkNote` в systemLog-ветке
склеивал имя и деталь без пробела.

Грабли. Порог теста `painted > W*H/4` отсекал точное попадание (16384 при
пороге >16384) — теперь сравнение с ожидаемой площадью с допуском `2*W` и
проверкой пустоты правой половины. `cd X && nohup … &` фоновит всю цепочку (cwd
не меняется → деплой exe падал). URL обязателен в argv: без него клиент печатает
`Unknown command or variable "protocol"` и живёт без игры — для прогонов
использовать `wine-client/run_vk_dip.sh` (читает `/tmp/vkurl.txt`).

Не закрыто (очередь шага 13): текстуры и сэмплеры (`SetTexture`,
`CreateTexture2D/Cube`, `UpdateTexture` — 97 текстур и 52 сэмплера по корпусу,
макет set 2/set 3 уже размечен писателем), `DrawUP/DrawIndexedUP` (PostFX),
`GetRenderTarget/GetRenderTargetData/ColorFill/StretchRect`, ключ пайплайна из
шага 8 вместо `(vs,ps,layout,rt,ds,prim)`, диапазон загрузки UBO из
`ConstBlock::TakeDirty()` вместо 4 КБ на стадию, и **шаг раскладки**: в Vulkan
шаг вершины зашит в раскладку (`bindingMap[bufferIndex].stride =
elementStride`), поэтому кэш раскладки обязан учитывать шаг потока, иначе
второй VB с другим шагом получит чужую раскладку молча.

Регресс-гейт D3D9-контура на tiny10 для exe шага 12b (прогон `2026.10.15-17.18.34`,
задача `PwE2E64W`, откат `PW_Game.exe.bak-2026-10-15-12b`): `login reply` 1,
`all map objects loaded` 1, `ShaderContainer` 15 / `ShaderTarget` 58, «нужен
рантайм-компилятор» 0, `CRC32 mismatch` 0. Кадр `pw_auto_bb_3000.png` против
`tools/render_shot/refs/stage3-shd2-2026-10-15/`: **diff 5,33%, грубых 0,67%**;
шум анимации **того же** прогона (`bb_3000` против `bb_3100`) — 5,97% / **1,20%**,
т.е. расхождение ниже шума. Сетка `--grid` показывает кластеры у юнита/портала
(максимум 1633 в ячейке), полос нет → регресса нет. Правки 12b в D3D9-пути
отсутствуют (всё под env `PW_VK_SPIKE`/`PW_VK_DIP`), гейт снят для полноты.

### Сделано (шаг 13.1: текстуры и сэмплеры через seam — 2026-10-15, `e88dc7238`)

Seam (`Src/Render/renderbackend.h:345`) получил `LockTexture2D(HTexture, unsigned
level, const BackendRect*, ERenderLockType, void** ppData, unsigned* pRowPitch)` и
`UnlockTexture2D(HTexture, unsigned level)`. Основание: клиент наполнял текстуры
**мимо seam'а** — напрямую через COM (`Render/texture.cpp:106` `LockRect`,
`Render/rendersurface.cpp:89`), в Vulkan-контуре эти вызовы просто некуда
переводить. Шаг строки возвращает бэкенд: у D3D9 он кратен 4 и не обязан равняться
`width*4`, у Vulkan — как у staging-текстуры, поэтому вызывать seam'у «свой шаг»
нельзя.

Реализации. D3D9 (`d3d9backend.cpp:450`): `IDirect3DTexture9::LockRect/UnlockRect`,
подпрямоугольник и уровень пробрасываются как есть. Vulkan (`nvrhibackend.cpp`):
CPU-зеркало уровня 0 (`TextureObj::mirror`) + staging (`mapStagingTexture` →
построчный перенос) + `copyTexture` на Unlock; `level != 0`, cube и сжатые форматы
→ `E_FAIL` с логом (`BppOf` знает только незапечатанные форматы). mip-цепочка через
seam не грузится вовсе, `usageAutoGenMipmap` игнорируется (`mipLevels=0` → 1 mip) —
клиент создаёт текстуры с автогенерацией (`texture.cpp:104`
`usageAutoGenMipmap | usageRenderTarget`, `BACKEND_POOL_MANAGED`), значит mip'ы —
отдельная работа шага 13.

Остальное закрыто в том же коммите: `CreateTexture2D/CreateTextureCube`
(`keepInitialState` обязателен), `UpdateTexture` = `copyTexture` целиком,
`SetTexture(samplerIndex, HTexture)` — слот сэмплера D3D9 = `binding` в наборе 2.
Набор текстур строится лениво по подписи привязок (`TexSignature`, FNV-1a по
`(binding, texture)`, кэш `setCache` с очисткой при >4096), сэмплеры статичны —
один `defSampler` (линейный/wrap), потому что **состояние сэмплера через seam не
приходит**: `RenderStatesManager` пишет фильтры прямо в D3D9 (`GetSamplerShadowHash`
из шага 8 — заготовка, в seam она не используется). В несвязанный слот кладётся
`dummyTex` 1×1, промах считается в `VkTex: … (промахов текстур N)`.

Писатель (`tools/spirv_probe/shim.py`): номер D3D9-сэмплера становится номером
`binding` в наборе текстур (`register(sN)` → `register(tN, space2)`). Прежняя
нумерация по порядку объявлений молча уводила привязку клиента
(`SetTexture(samplerIndex, …)`) в чужой слот — без ошибки и без изменения
SM3-слоя (правка касается только SPIR-V).

Два дефекта, найденных тестом (оба — «молчаливые»):
1. `SpvResScan`: `OpTypePointer` (код 32) входит в диапазон объявлений типов
   19..33, и проверка диапазона стояла **до** ветки указателей → ветка
   недостижима, `ptrStore` пуст, не находился **ни один** ресурс: в логе
   `ResMap — OK наборов 0, текстур 0, сэмплеров 0` у PS, который читает текстуру.
   Починка — разбирать `OpTypePointer` первым.
2. `SetPixelShaderConstants` не выставлял `uboDirty[csPixel]` → PS-банк никогда не
   загружался, `outputMul` (SAMPLER0_MUL = c20) читался нулём → чёрный вывод при
   полностью исправной геометрии. Косвенный признак, по которому нашли: `загруз=5`
   вместо 6 (не хватало одной загрузки UBO).

Инструмент: `tools/shader_build/spv_scan_check.py` — офлайн-порт клиентского
сканера (читает `.shd` v2, печатает ресурсы пары). Он дешевле прогона клиента:
по корпусу с каноническим банком — **58 пар, tex 97, smp 54, ubo 125, ssbo 13,
макс binding 7** (прежние цифры спайка smp 52 / ubo 109 снимались до явных PS-банков).
Заодно проверил, что `binding` = номер слота D3D9 (у `WaterMaterial` PS — наборы
`2:1..2:5`).

Тест: `NvrhiBackend::RunTextureDipTest()` (`SkyMaterial` #1: VS — `World` c0,
`ViewProjection` c8, `uvOffset` c20; PS — одна текстура `s0`, один сэмплер `s0`,
`outputMul` c20 = 1, `outputAdd` c21 = 0). Треугольник покрывает весь offscreen RT
256×256, ожидание — весь кадр равен цвету текстуры. `PW_VK_DIP` теперь битовая
маска: 1 = геометрия (`WaterDummyMaterial`), 2 = текстура, 3 = оба. Прогон под Wine
на RTX 3060 (`2026.10.15-20.06.39`): `TexDIP: текстура доехала — OK комбинация #1,
блобы VS 1628 Б / PS 1664 Б, питч 32; залито 65536/65536, цвет текстуры
65536/65536; выборка BGRA=0,64,255,255`, рядом `DIP: пиксели на месте — OK`,
`VkTex: текстуры=1 наборы=1/2 (промахов текстур 0)`, `VkRes: … загруз=6`. То есть
путь «CPU-пиксели → seam → staging → текстура → набор 2/3 → сэмпл → пиксель»
работает, и PS-банк доезжает.

Регресс-гейт D3D9-контура на tiny10 (прогон `2026.10.15-18.12.52`, задача
`PwE2E64W`, exe 15 684 096 Б, откат `PW_Game.exe.bak-2026-10-15-131`): `login reply`
1, `all map objects loaded` 1, `ShaderContainer` 15 / `ShaderTarget` 58, «нужен
рантайм-компилятор» 0, `CRC32 mismatch` 0; кадр против
`refs/stage3-shd2-2026-10-15/` — **diff 5,99%, грубых 0,71%** при шуме анимации
того же прогона 5,15% / **1,15%**; сетка — кластеры у юнита/портала, полос нет.
Гейт обязателен: в этом коммите правился общий заголовок `renderbackend.h` и
`d3d9backend.cpp`.

Грабли. Сессия для tiny10 обязана генерироваться с `-e PW_VERSION=2.16.1`
(`pw-api/objects/sessionLaunch.js:15` `LAUNCH_VERSION = process.env.PW_VERSION ||
'2.16.0'`): с версией 2.16.0 в URL клиент печатает `Protocol server base port:` и
зависает на модалке «Game update has failed!» (в рабочем наборе это выглядело как
«клиент запустился, но логина нет»: `login reply` 0, рабочий набор процесса 14 МБ).
`++d.texMissing` стоял вне `else` — счётчик показывал «промах» даже при успешной
привязке (первый прогон дал «промахов текстур 1» при корректной текстуре).

Не закрыто (очередь шага 13; список заглушек `NVRHI_STUB_LIST` пуст с шага 13.3,
прежние 6: `DrawIndexedUP`, `DrawUP`, `GetRenderTargetData`, `ColorFill`,
`StretchRect`, `GetRenderTarget` — закрыты 13.3):
(1) **перевести вызовы клиента** с COM на `LockTexture2D/UnlockTexture2D` —
**закрыто шагом 13.2** (остатки COM перечислены в секции 13.2: учёт ресурсов
`DxResourcesControl`, заполнение mip'ов в `TextureManager` при загрузке DDS,
`Texture2D::GetSurface`); (2) mip'ы и `usageAutoGenMipmap` — **закрыто 13.2**
(`GenerateMipmaps` в обоих бэкендах, ветка `IsRunnedUnderWine` из `texture.h` убрана);
(3) состояние сэмплера в seam (`GetSamplerShadowHash` из шага 8) — сейчас везде
линейный/wrap; (4) SSBO в наборе 1 (per-instance данные: seam несёт их как вершинный
буфер, SPIR-V ждёт `StructuredBuffer`); (5) `DrawUP/DrawIndexedUP` (PostFX) и
группа surface-операций — **закрыто 13.3**; (6) ключ пайплайна из шага 8 вместо
`(vs,ps,layout,rt,ds,prim)` — частично 13.3 (в ключ вошла подпись шагов потоков,
само состояние из `NotePipeline` ещё не участвует); (7) диапазон загрузки UBO из
`ConstBlock::TakeDirty()` вместо 4 КБ на стадию; (8) шаг потока в кэше раскладки
(Vulkan зашивает stride в раскладку → второй VB с другим шагом получит чужую
молча) — **закрыто 13.3** (`ilCache` по (раскладка, подпись шагов)).

### Сделано (шаг 13.2: текстурный контур клиента убран из COM — 2026-10-15, `9a717014f`; код и доки легли одним коммитом)

Seam дополнен пятью методами: `GetTextureInfo`, `GetSurfaceInfo`, `GenerateMipmaps`,
`LockSurface`, `UnlockSurface` (`Src/Render/renderbackend.h`; `GetSurfaceInfo` не
было — правка клиента не компилировалась). Реализованы в обоих бэкендах.

Нейтральный словарь форматов/использований/пулов (`renderflagsconverter.*`):
`D3DFormatToBackend`/`BackendFormatToD3D`, `D3DUsageToBackend`/`BackendUsageToD3D`,
`D3DPoolToBackend`/`BackendPoolToD3D` + `CheckTextureInfoRoundTrip` (печатает
`round-trip` при расхождении). Это обязательный инвариант: клиентские классы
хранят `D3DSURFACE_DESC` и по нему принимают решения (деструктор — `D3DPOOL_MANAGED
!= desc.Pool` → снимает учёт; `OnResourcesReallocate` пересоздаёт только
`D3DRTYPE_SURFACE`), поэтому восстановление описания из seam-структуры обязано быть
побитовым. `desc.Type` сохраняется явно; мультисэмплирование в `BackendTextureInfo`
не описано — для поверхностей восстанавливается `D3DMULTISAMPLE_NONE` (созданных с
MSAA в клиенте нет).

Клиент: `texture.cpp` — конструктор `Texture2D(IDirect3DTexture9*)`,
`GetMipSubLevelsCount()`, `LockRect`/`UnlockRect` (все три перегрузки),
`GenerateMipSubLevels_()` (стал виртуальным); `rendersurface.cpp` — конструктор,
`LockRectInternal`, `Unlock`; `renderer.cpp` — снимок экрана через `GetSurfaceInfo`.
Ветка `Compatibility::IsRunnedUnderWine()` из `texture.h` убрана: выбор способа
генерации mipmap переехал в бэкенд (у D3D9 — цепочка `StretchRect` по уровням,
перенесена в `D3D9Backend::GenerateMipmaps` из `GenerateSubLevels_1/2`).

Vulkan: `LockTexture2D`/`UnlockTexture2D` и `LockSurface`/`UnlockSurface` —
CPU-зеркало + staging + `copyTexture` (глубина → `E_FAIL`, сжатые форматы →
`E_FAIL`, питч возвращает бэкенд); `GenerateMipmaps` — цепочка уровней через
`copyTexture` без фильтрации (то же качество, что прежняя Wine-цепочка), cube →
`E_FAIL` с `VkNote`.

Дефект, вскрытый прогоном со спайком: после пересоздания swapchain'а картинки
бэк-буфера добавляются в КОНЕЦ `d.surfaces` (там уже лежат поверхности клиента), а
`Present` брал `d.surfaces[d.imageIndex]` → мёртвая запись с `tex == nullptr` →
`clearTextureFloat(nullptr,…)` → access violation (`exception.dmp`, лог обрывается
на `RdpChannel: Delayed active channel established`). Починка: индекс берётся из
`backSurfaceIdx`, добавлена проверка живости ручки; `DestroySwapchain` отпускает
только записи бэк-буферов и чистит `pipes`/`setCache` (framebuffer'ы кэша указывают
на мёртвые картинки).

Гейты. Wine со спайком (`2026.10.15-21.14.27`, exe 15 692 800 Б, `PW_VK_DIP=3`):
краха нет, `Entered lobby` 1, `all map objects loaded` 1, `DIP: пиксели на месте —
OK`, `TexDIP: текстура доехала — OK`, `VkMsg` 0, `round-trip` 0. Регресс D3D9 на
tiny10 (`2026.10.15-19.17.38`, откат `PW_Game.exe.bak-2026-10-15-132`): login 1,
`all map objects loaded` 1, `ShaderContainer` 15 / `ShaderTarget` 58, «нужен
рантайм-компилятор» 0, `CRC32 mismatch` 0, `round-trip` 0, `Texture2D: формат …` 0;
кадр против `refs/stage3-shd2-2026-10-15/` — **diff 2,52%, грубых 0,19%** (ниже
полосы шума разных прогонов 0,13–0,35%).

Остаток COM в текстурном контуре (переносить отдельно): `DxResourcesControl.cpp:190,
197,206,215,233,245,258,668,676,684,692,700` (учёт потерянных ресурсов — под Vulkan
вызовет `GetDesc` на не-COM указателе), `smartrenderer.cpp:400,843,853,870`,
`renderresourcemanager.cpp:316,329`, `StaticMesh.cpp:390,496`,
`TextureManager.cpp:188,194` (заполнение mip-уровней при загрузке DDS),
`Texture2D::GetSurface(level)`.

### Сделано (шаг 13.3: последние шесть заглушек Vulkan-бэкенда закрыты — 2026-10-15, `bb8224a72`)

`NVRHI_STUB_LIST` пуст: `DrawUP`, `DrawIndexedUP`, `GetRenderTarget`,
`GetRenderTargetData`, `ColorFill`, `StretchRect` реализованы; при всех тестах
`unsupported=0` (счётчик незакрытых вызовов больше не растёт).

**Вывод из приведённой памяти.** D3D9 умеет рисовать прямо из памяти вызывающего
(`DrawPrimitiveUP`), Vulkan — нет, поэтому кольцо: VB 4 МБ и IB 1 МБ
(`RegisterUpRing`, регистрируются как обычные `BufferObj`, чтобы общий путь
`DrawImpl` не ветвился), `writeBuffer` со смещением, `ReserveUp` выравнивает по 16
и при нехватке хвоста возвращает «сброс с нуля» — перед сбросом обязателен
`FlushRecording()` + `waitForIdle()`, иначе запись затрёт данные, которые ещё
исполняющийся командный список. Индексы обходятся за `maxIdx`: размер приведённого
VB D3D9 тоже не знает (в seam-слоте `indexCount` лежит число индексов,
`smartrenderer.cpp:638`), а в Vulkan выход за конец буфера — UB, а не «терпимо».
`DrawImpl` получил 6-й параметр `explicitVtx` (для UP-батов число вершин ≠
число примитивов).

**Поверхности.** `GetRenderTarget` — реестр `rtSlot[]` (раньше `SetRenderTarget`
делал `(void)index`), для слота 0 без записи возвращается бэк-буфер. БЛОКЕР шага
13.4: контракт seam'а — владеющая ручка, а клиент хранит их в COM-умных указателях
(`renderer.cpp:301 pDeviceColorSurface.Attach(...)`), т.е. позже вызовет `Release()`
не на COM. `GetRenderTargetData` — staging → CPU-зеркало поверхности назначения
(дальше клиент читает её через `LockSurface`, это путь снимка экрана). `ColorFill` —
`clearTextureFloat`. `StretchRect` — только без скейла (`copyTexture` с
`TextureSlice`); со скейлом нужен блит-пайплайн с фильтром, в живом клиенте таких
вызовов нет (`renderer.cpp:955`, `rendersurface.cpp:129` — пустые прямоугольники,
цепочка mip ушла в `D3D9Backend::GenerateMipmaps`), поэтому оставлен явный отказ
с `VkNote`, а не тихая деградация.

**Шаг потока в кэше раскладки (пункт 8 очереди).** В Vulkan шаг потока зашит в
раскладку (`Vendor/NVRHI/src/vulkan/vulkan-shader.cpp`: `bindingMap[bufferIndex] =
{binding=bufferIndex, stride=elementStride, …}`), поэтому два VB с разными шагами
при одной раскладке получали чужую молча. Реализовано: `StrideSignature` (FNV-64 по
`vbStride[0..7]`, `instStride`, `instSlot`; смещения не входят — они относятся к
привязке), `ilCache` по (раскладка, подпись) со сбросом при >512, подпись входит в
ключ пайплайна (`PipeObj::lsig`). Замер: `раскладок построено=3 (кэш 3)` при трёх
тестах — дедуп работает.

**Тест `RunSurfaceOpsTest`** (`PW_VK_DIP` стал маской: 1 геометрия, 2 текстура,
4 поверхности/UP; хук `renderer.cpp`, лог `Renderer: PW_VK_DIP_SURF`):
`GetRenderTarget(0).p == rt.p`, `ColorFill(0xFF00FF00)` → 16384/16384 зелёных,
`DrawUP(TRIANGLELIST, 1, …, stride 12)` → 4096/16384 (ровно четверть кадра 128×128,
правая половина пуста), `StretchRect(rt→rt2)` → 16384/16384 совпало побайтово,
`GetRenderTargetData(rt→plain)` + `LockSurface(plain)` → те же 4096.

Дефект, вскрытый первым прогоном теста: `hr=0x00000000`, 0 нарисованных пикселей
при исправных `ColorFill`/`StretchRect`. Причина: у только что созданной
поверхности глубины содержимое не определено, а `depthFunc=Less` при случайном
значении < 0.5 отсекает геометрию молча — тест (и любой будущий проход) обязан
явно чистить глубину, а не только цвет.

Гейты. Wine (`2026.10.15-22.18.24`, exe 15 705 600 Б, RTX 3060, `PW_VK_DIP=7`):
`DIP: пиксели на месте — OK`, `TexDIP: текстура доехала — OK`,
`SurfOps: поверхности и UP-вывод на месте — OK`, `VkUp: UP-вывод=1
колец-сбросов=0 раскладок построено=3 (кэш 3)`, `VkRes: буферы=3 шейдеры=6
раскладки=3 пайплайны=3/3 вывод=3 загруз=10`, `unsupported=0`, `VkMsg` 0,
`mapload: all map objects loaded` 1. Регресс D3D9 на tiny10 (`2026.10.15-20.20.41`,
откат `PW_Game.exe.bak-2026-10-15-133`): login 1, `all map objects loaded` 1,
`ShaderContainer` 15 / `ShaderTarget` 58, «нужен рантайм-компилятор» 0,
`CRC32 mismatch` 0, `round-trip` 0; кадр против `refs/stage3-shd2-2026-10-15/` —
**diff 4,70%, грубых 0,60%** при шуме анимации того же прогона 2,09% (сетка —
кластеры у юнита/портала, полос нет).

Остаток шага 13: (3) состояния сэмплеров в seam, (4) SSBO в наборе 1 из
`SetInstanceSource`, (6) ключ пайплайна из шага 8 (`NotePipeline`/
`GetRenderStateShadowHash`), (7) загрузка UBO диапазоном `ConstBlock::TakeDirty()`;
плюс перевод владения COM-ручками (197 упоминаний `IDirect3D*` вне бэкендов —
`smartrenderer.cpp` 21, `dxutils.h` 18, `TextureManager.cpp` 18,
`DxResourcesControl.cpp` 18, `renderer.cpp` 16) — без этого «переворот» бэкенда
(шаг 13.4) рвётся на `Release()` и на `GetDesc` в учёте ресурсов.

### Сделано (шаг 13.4-разведка: второй снимок живых индексов + карта COM-связности — 2026-10-16)

**Второй снимок живых индексов — DXVK/NVIDIA-профиль** (`tools/shader_build/
live_indices-2026-10-15-dxvk.json`, прогон `2026.10.15-22.31.06` под Wine с DXVK,
dev-var `dump_shader_index 1` в `wine-client/Game/Profiles/autoexec.cfg`):
**79 пар по 16 материалам** против 58 пар по 15 материалам в прежнем снимке
(WARP на tiny10). Новый материал — `SilhouetteMaterial` (2 пары).

Главное: **пересечение двух профилей пустое** (58 + 79 = 137 пар в объединении).
Индексы разъезжаются не из-за устройства, а из-за **рантайм-пинов**, которые
умножаются в индекс пары:

- `DoDepthOutValue` ставится в `PF_GameLogic/PFRenderInterface.cpp:1501`
  (`= s_readableDepth ? FALSE : TRUE`) — у WARP-профиля он 1 у большинства
  материалов, у DXVK-профиля 0 (BasicMaterial #1 против #0, Sky #1 против #0,
  WaterDummy #1 против #0);
- `RenderModeValue` (`RENDERTOSHADOW` / `RENDERSHAPEONLY` / `RENDERNORMAL`) и
  `InstancingValue` — `Render/batch.cpp:129-754` (теневой проход и инстансинг
  добавляют отдельные комбинации);
- PostFX расходится сильнее всего: `FullScreenMaterial` #113 против #120 —
  `BloomPinValue` 2→0, `ColorCorrectPinValue` 1→0, `DoDepthOutValue` 2→0.

Следствие (подтверждает решение шага 11 «живые комбинации + рантайм-фолбэк DXC»):
корпус нельзя строить по одному прогону — он строится из **объединения** снимков,
а промах по индексу обязан вести в рантайм-компиляцию, а не в `default_shader`.

Корпус на объединении (`mkshd2.py --live live_indices-2026-10-15-union.json
--cbank`): **16 контейнеров, 137 пар, 0 пропусков**; байты 2 979 260 → 3 120 012
(+4,7%), SPIR-V-блобы 252 480 → 389 456 Б (+52%) — дедупликация блобов съедает
половину роста, цена второго профиля копеечная.

**Карта COM-связности клиента (блоке­ры «переворота» `pBackend = NvrhiBackend`).**
Прямые обращения к `IDirect3DDevice9` вне `d3d9backend` (13 в `HwInput.cpp` — это
DINPUT, не считается):

1. *Жизненный цикл устройства*: `renderer.cpp:121 Release`, `:252 GetDisplayMode`,
   `:305` и `:732 CreateQuery(D3DQUERYTYPE_EVENT)` (синхронизация кадров),
   `:511 Reset`, `:561 GetDeviceCaps`, `:893 TestCooperativeLevel`, `:1041
   GetDirect3D`, `:1046 GetCreationParameters` (машинерия device-lost целиком
   D3D9-ная).
2. *Фиксированный конвейер 2D*: `smartrenderer.cpp:238-241` —
   `SetVertexShader(NULL)` + `SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE)` +
   `DrawPrimitiveUP` — путь «без шейдера», в Vulkan требует шейдера и раскладки.
3. *Загрузка текстур через D3DX*: `TextureManager.cpp:483
   D3DXCreateTextureFromFileInMemoryEx(GetDevice(), …)` — DDS-текстуры создаются
   COM'ом мимо seam'а (`ReadDDS.cpp:318,344` тоже смотрят на устройство);
   закрывается декодером в клиенте + `LockTexture2D` (метод уже есть).
4. *Состояния*: `renderstatesmanager.cpp` (11 сайтов: `SetRenderState`,
   `SetSamplerState`, блоки состояний) — пункт 3 очереди шага 13.
5. *Чтение состояния*: `smartrenderer.cpp:588,659 GetViewport`,
   `ShadowManager.cpp:351,353 GetViewport/GetScissorRect` — в seam такие методы
   есть, сайты их обходят.
6. *Владение ручками*: seam-ручка сегодня и есть COM-указатель, поэтому клиент
   делает `Attach`/`Release` (`renderer.cpp:299-302 pDeviceDepthSurface/
   pDeviceColorSurface`) и обратные `MakeBackendHandle<…>(COM*)` — **34 сайта**
   (`smartrenderer.cpp` 9, `texture.cpp` 8, `renderer.cpp` 6, `rendersurface.cpp`
   5, `dxutils.cpp` 4, `WaterManager.cpp` 2, последний передаёт
   `SmartRenderer::GetRenderTarget(0)` прямо в `StretchRect`). Учёт памяти
   `DxResourcesControl.cpp:185-215` (`GetLevelCount/GetType/GetLevelDesc`)
   переносится на `GetTextureInfo` без изменений в семантике.

Порядок, в котором это раскрывается: (6) владение → (5) чтение состояния →
(4) состояния/сэмплеры → (3) загрузка DDS → (2) фиксированный 2D-путь →
(1) жизненный цикл (его можно оставить на D3D9-устройстве до полноценного
Vulkan-_present-пути, но тогда «переворот» остаётся гибридом).

### Сделано (шаг 13.4a: `.shd` v2 с SPIR-V-слоем легли в репозиторий — 2026-10-16, `98f7ece3d`)

Закоммичено 16 контейнеров из 36 (`pw/branches/r1117/Data/Shaders/UberShaders/`):
к побайтово неизменной v1-части дописана секция цели tag=2 (SPIR-V, канонический
c-банк) по **объединённому** снимку живых индексов
(`tools/shader_build/live_indices-2026-10-15-union.json`: moba1p 58 + dxvk 79 =
137 пар, пересечение пустое). Остальные 20 контейнеров остаются v1 без трэйлера
(читатель переводит молча) — их комбинации ещё не сняты. Каталог 3,1 МБ → 3,4 МБ.
Разрешение пользователя (m08533): коммиты только в текущую ветку (`nvrhi`),
каналы `UpdateRepos/*-test`, `content-test`, `pwc-gitupdates*` и прод-репо не
трогать — так и сделано, на стенды контент доставляется pile'ом.

Гейт содержимого: `mkshd2.py --verify --out <репо>` — 16 разобраны, 20 «v2 НЕТ
(только v1)», **0 структурных ошибок**. D3D9-путь читает только v1-часть, а она
не изменилась, поэтому визуальный гейт на сами файлы не требовался.

**Прежняя претензия «`TerrainElementMaterial.shd` устарел относительно
исходников» НЕ подтвердилась.** Перегенерация текущим генератором
(`cd pw/branches/r1117/Bin && wine ../Tools/ClientCMake/build/x64-wine/ShaderCompiler.exe
../Src/Render/Shaders/MaterialSpec.xml --clean --out'Z:\tmp\v1t2\' --name'TerrainElementMaterial'`)
даёт файл, побайтово совпадающий с репозиторным (22 552 Б); `NatureMapMaterial`
— тоже. Причина прежнего ложного вывода — форма `--out`: `ShaderCompiler.cpp:374,696`
делают `s_outDirectory + имя + ".shd"` простой конкатенацией, поэтому без
конечного разделителя файл лёг в `/tmp/v1tNatureMapMaterial.shd`, каталог остался
пуст, а в логе нет ни ошибки, ни предупреждения. Полный переген всех 36 контейнеров
не делался и не нужен: `DropMaterial` = 6720 пар, `BasicMaterial` = 36864 — за
1200 с прогон не доходит (RC=124).

Попутно в `--verify` печатался размер среза перед трэйлером с подписью
«v1-часть» (по нему я и заподозрил несостыковку: 48 948 Б при настоящем v1Size
22 552 Б) — теперь печатается настоящий `v1Size`.

Стенды: pile собран из репозиторного каталога (`tools/shader_pile/mkpile.py build
--root /tmp/packdir_union --out /tmp/zzzresult_union.pile --mask '*.shd'` → 228 221 Б;
побайтовая идентичность 16 файлов с `/tmp/cbank_union` проверена) и установлен на
оба контура: tiny10 `C:\pw64\Game\Packs\zzzresult.pile` (откат
`.bak-2026-10-16-union` = 231 591 Б, шаг 11) и Wine
`wine-client/Game/Packs/zzzresult.pile` (откат `.bak-2026-10-16-union` = 198 350 Б,
58-парный корпус без банка).

Гейт DXVK/NVIDIA (прогон `2026.10.15-22.38.08`): `ShaderContainer` 16,
`ShaderTarget` 79, «нужен рантайм-компилятор» **0** — с прежним 58-парным корпусом
было 21 попадание в фолбэк.

Гейт tiny10/WARP (прогон `2026.10.15-21.21.00`, exe 13.3 не менялся): `login reply` 1,
`all map objects loaded` 1, `ShaderContainer` **16** (было 15 — добавился
`SilhouetteMaterial`), `ShaderTarget` 58, «нужен рантайм-компилятор» 0,
`CRC32 mismatch` 0, `round-trip` 0. Кадр против
`tools/render_shot/refs/stage3-shd2-2026-10-15/pw_auto_bb_3000.png`: **diff 4,64%,
грубых 0,63%** при шуме того же прогона (`bb_3000` против `bb_3100`) 5,34% /
**1,05%** → ниже шума, PASS.

Дальше по 13.4 (решение по порядку принято здесь, а не в разделе разведки):
**13.4b** = env-гейт `PW_VK_BACKEND=1` (переназначение `pBackend` на
`NvrhiBackend` при сохранении D3D9-устройства и его lifecycle) + закрытие
классов (5) чтение состояния и (4) состояния/сэмплеры + первый **offscreen A/B**
(клиентский вывод в собственный RT, `ReadSurface` → PNG против D3D9-кадра той же
сцены). Классы (6) владение и (1) lifecycle на этом шаге не трогаются — они
нужны только для present-пути. Затем (3) DDS и (2) фиксированный 2D-путь.


### Сделано (шаг 13.4b: модель состояний конвейера легла в seam — 2026-10-16, `8264233c9`)

Seam (`Src/Render/renderbackend.h`, +99 строк): `BackendPipelineState` (cull,
fill, scissor, zEnable/zWriteEnable/zFunc, slopeScaleDepthBias + depthBiasRaw,
трафарет (func/ref/readMask/writeMask/fail/zFail/pass), бленд (вкл., separate
alpha, src/dest/op, альфа-вариант, colorWriteEnable), alphaTest/alphaRef/
alphaFunc, clipPlaneEnable, 16 `BackendSamplerState`) и
`virtual void SetPipelineState(const BackendPipelineState&, unsigned long long
stateKey)`. Значения в полях — **числа D3D9**: полный нейтральный словарь —
этап 4, здесь только переводчики в бэкендах. Вызов — из четырёх путей вывода
`SmartRenderer` (`NotePipelineState()`, `smartrenderer.cpp:585-590`, вызовы
:607, :650, :679, :702); `NotePipeline()` остаётся только под
`USE_RENDER_SHADER_STATS`.

Менеджер состояний (`renderstatesmanager.{h,cpp}`, +309): тень стала единственным
источником модели. **Найдено и закрыто:** `SetStateDirect` и
`SetSamplerStateDirect` писали прямо в устройство, минуя тень, а
`SetStencilStateAddonBits` — прямо в `D3DRS_STENCILREF`; значит `GetRenderStateShadowHash()`
(шаг 8) и вся модель не видели прямых записей (ShadowManager: DEPTHBIAS /
SLOPESCALEDEPTHBIAS, PostFX, альфа-тест, скачок). Теперь `SetStateDirect` →
`ApplyState`, сэмплерные пути пишут `samplerShadow[16][12]`, `SetStencilState`
пишет тень и в cache-hit ветке. Добавлены `BuildPipelineState()` (30 D3DRS +
10 D3DSAMP на слот, хэш FNV по полям, не по байтам), `GetPipelineState/
GetPipelineStateHash` (ленивая пересборка по флагу) и
`VerifyShadowAgainstDevice(maxReports)` — сверка тени с устройством по таблицам
`g_renderStates[]`/`g_samplerStates[]` (те же, что у `dump_states`), строка
`StateShadow: <имя> тень 0x…, устройство 0x… — запись мимо менеджера`.

Vulkan-бэкенд (`nvrhibackend.cpp`, +402): переводчики `CmpOf/StencilOpOf/
BlendOf/CullOf/FillOf/AddressOf` + 4 `static_assert` на совпадение нумерации
D3D9/NVRHI (смена вендора = падение сборки, а не инверсные сравнения);
`FillPipelineState(d, pd)` вместо жёсткого `setCullNone()`; `stateHash` вошёл в
ключ кэша пайплайнов; сэмплеры — кэш по ключу модели сэмплера и набор сэмплеров
по подписи (прежний статичный `rm.setSmp` удалён); счётчики `VkState: смен
состояний=… сэмплеров=… наборов сэмплеров=… потерь=…`.

Замеры и гейты.
- tiny10/WARP, `setvar verify_state_model 1` (прогон `2026.10.16-07.42.37`,
  exe 15 719 424 Б, md5 `02b53442…`): **17 200 сверок тени с устройством,
  0 расхождений** — тень покрывает все состояния и в загрузке, и в бою. Рядом
  `login reply` 1, `all map objects loaded` 1, `ShaderContainer` 16,
  `ShaderTarget` 58, «нужен рантайм-компилятор» 0, `CRC32 mismatch` 0,
  `StateShadow` 0. Кадр против `refs/stage3-shd2-2026-10-15/`: **diff 4,80%,
  грубых 0,68%** при шуме того же прогона 5,07% / **1,22%** → PASS.
- Wine/RTX 3060 (`2026.10.16-09.09.33`): `DIP`/`TexDIP`/`SurfOps` — все OK,
  `VkState: потерь=0`, `VkMsg` 0. Первый прогон давал `потерь=9` —
  `StencilOpOf` считал отсутствующее значение (0) «вне словаря», тогда как
  `D3DSTENCILOP_KEEP = 1`, а `Reset()` оставляет нули: теперь `0 → Keep`.

Что выяснилось про состояния (важно для этапа 4).
- Нумерация `D3DCMP_*` и `D3DSTENCILOP_*` в вендоренном `d3d9types.h` **1-based
  и совпадает с NVRHI дословно** (`INCRSAT=4 → IncrementAndClamp=4`,
  `INCR=7 → IncrementAndWrap=7`); `BlendFactor` 1..11 и 14/15 тоже совпадают.
  Прежнее утверждение «нужен сдвиг +1» было неверно. Таблицы нужны только для
  FILL / CULL / ADDRESS / фильтров.
- Клиент пишет в «DWORD»-состояния **биты float**: `ShadowMap.cpp:238-239`
  (`D3DRS_DEPTHBIAS`, `D3DRS_SLOPESCALEDEPTHBIAS`), `PFRenderInterface.cpp:1613`
  (`D3DSAMP_MIPMAPLODBIAS` по всем 16 слотам). В модели это биты float; у NVRHI
  `RasterState::depthBias` — `int`, дробная часть теряется (бэкенд печатает
  заметку). Постоянный сдвиг при этом целочисленный — это другое состояние.
- Дефолт `D3DRS_COLORWRITEENABLE` в D3D9 = **7** (RGB, без альфы), а не 0x0f —
  в модели трактуются оба значения.
- Аналогов у Vulkan нет: `ALPHATESTENABLE`/`ALPHAREF`/`ALPHAFUNC` (фиксированной
  альфа-смены нет — состояние обязано переехать в шейдер, шаг 14),
  `CLIPPLANEENABLE`, `D3DBLEND_BOTHSRCALPHA/BOTHINVSRCALPHA` (12/13).
  `D3DFILL_POINTS` не поддержан.
- Во shipping `REGISTER_DEV_VAR` свёрнут в пустоту (`System/Commands.h:457-469`:
  `_SHIPPING` без `_RELEASE` и без `USE_DEVELOPER_COMMANDS`) — из-за этого
  `verify_state_model` молча не регистрировался, а `shadowBias`, `shadowSlope`,
  `shadowForceMRT` (`ShadowMap.cpp:14-19`) во shipping **не устанавливаются
  вообще**. Рабочие переключатели этого ствола зарегистрированы `REGISTER_VAR`
  (`multishader.cpp:31,51`, `smartrenderer.cpp:20,29,37`).

Не закрыто: `смен состояний=0` на Wine-спайке — `pBackend` ещё D3D9, поэтому
частота смены состояний (а значит и число разных пайплайнов в бою) не измерена;
измерение переносится на 13.4c вместе с env-гейтом `PW_VK_BACKEND=1` и
offscreen A/B.

### Сделано (шаги 13.4c–13.4e: клиент рисуется через Vulkan-бэкенд — 2026-10-17, WIP)

Env-гейт `PW_VK_BACKEND=1` (`renderer.cpp:443`, рядом с `PW_VK_SPIKE`/`PW_VK_DIP`):
`pBackend = NvrhiBackend::Create(window, pp.BackBufferWidth, pp.BackBufferHeight)`,
D3D9-устройство **сохраняется** (его lifecycle: `CreateQuery`, `Reset`,
`TestCooperativeLevel`; D3DX-загрузка; PostFX/ShadowMap/ImmediateRenderer).
Клиент доходит до боя и рисует через seam: `VkStatsflip: presents=2099
recreates=1 swap=1024x768 device=NVIDIA GeForce RTX 3060`, `VkResflip:
буферы=10858 шейдеры=120 раскладки=20 пайплайны=6911/6911 (попаданий 924834)
вывод=931959`, `вывод: в бэк-буфер=2099 в offscreen=967891`, RSS ~2,1 ГБ
(DXVK-эталон того же клиента 2,44 ГБ).

Мост владения (`nvrhibackend.cpp`): клиент хранит seam-ручки в COM-умных
указателях (`DxIntrusivePtr.h:53` → `AddRef()/Release()`), поэтому у ручки свой
vtable-префикс `SeamUnknown` (вендорный DX-SDK не даёт `unknwn.h`) и
`SeamHandle{magic='SEAM', impl, index, kind, refs}`. `Hnd2Idx` проверяет магию;
освобождённая обёртка помечается `kFreed=0xDEAD5245` (не удаляется) — это сняло
4495 «чужих» ручек, которые на самом деле были нашими мёртвыми. **Корень
прежних крахов:** `NewHandle` возвращал существующую обёртку БЕЗ `AddRef` →
умный указатель клиента ронял счётчик единственного владельца в 0, ресурс умирал,
а индекс оставался в кэшах (`curRt`/`rtSlot`/`backDepthIdx`) → access violation
в `clearTextureFloat`. Теперь каждая выдача ручки = отдельная ссылка (как в COM).

Инструмент поиска остатков UB: **пады vtable** `SeamHandle` на слоты 3..29
(`SEAM_PAD_SLOT(n)`, деструктор `SeamUnknown` сделан НЕ виртуальным, иначе
нумерация разъехалась бы с индексами методов D3D9). Номер слота = индекс метода
по `Vendor/DirectX/Include/d3d9.h`; лог `клиент позвал COM-метод по seam-ручке
(слот %u, kind=%u index=%u refs=%ld, вызов из RVA 0x%x)` сам указывает, какого
метода seam'а не хватает. Так был найден `IDirect3DVertexDeclaration9::GetDeclaration`
(слот 4) — и объяснены прежние fastfail-крахи: пад возвращал 0, не записав
out-параметры → `strideElementsCount` = мусор → `resize(мусор)` → порча кучи.

Seam расширен (все три бэкенда): `GetBufferInfo(HBuffer, unsigned* pByteSize)`,
`GetTextureSurface(HTexture, level, HSurface* pOut)` (D3D9 — `GetSurfaceLevel`,
владение переходит; Vulkan — просмотрка на ту же картинку, держит внутреннюю
ссылку текстуры, `level != 0` отказ), `GetInputLayoutElements(HInputLayout,
VertexElementDescriptor*, maxElements, unsigned* pCount)` + `kMaxVertexElements=32`
(обрезка при переполнении запрещена — сдвигает шаг вершины),
`GetShaderTargetTag()` (слой шейдеров объявляет бэкенд: D3D9 → 0, Vulkan →
`Materials::stSpirv`), `LockTexture2D/UnlockTexture2D` с параметром `face`
(0=2D, 0..5 = `D3DCUBEMAP_FACES`), `BackendTextureInfo.cube`.
Переведённые COM-сайты клиента: `ReadDDS.cpp:305-340`, `TextureManager.cpp:179-229,566-583`
(mip-подкраска и проверка 2D через `GetTextureInfo`, а не `GetType()`),
`renderresourcemanager.cpp:317,331` и `StaticMesh.cpp:392,499` (`GetBufferInfo`),
`smartrenderer.cpp:402,860,872,891` (`GetSurfaceInfo`, в т.ч. формат глубины,
который кормит ключ состояний), `texture.cpp:195` (`GetSurface` → `GetTextureSurface`).
`DX_RESOURCE_CONTROL` (`DxResourcesControl.h:6`) закомментирован → учёт ресурсов
не компилируется, сайтом не является.

Память (было 25,3 ГБ RSS, 11 636 отображений `nvidiactl` = 22,7 ГБ при 0,95 ГБ
видеопамяти): (а) загрузка бит везде через `ICommandList::writeTexture` —
`createStagingTexture` у этой версии NVRHI делает отдельное host-visible
выделение (`vulkan-staging-texture.cpp:111`), а NVRHI делает по одному
`vkAllocateMemory` на ресурс (`vulkan-allocator.cpp:56`), драйвер поднимает
минимум ~2 МБ; (б) слэбы VB/IB по 64 МБ (`Slab`/`SlabAlloc`/`SlabFree`, дыры с
слиянием, `BufferObj::slab/off`, привязки через `setOffset(off + vbOffset)`);
(в) `FreeMirror(v)` = `vector().swap(v)` — `clear()` не отдаёт ёмкость.
Барьеры буферов NVRHI не генерирует (`transitionBufferStates` отсутствует) →
смешивать VB и IB в одном слэбе безопасно.

Потоки: клиент грузит геометрию НЕ из рендер-потока (`(0036)` рендер, `(0368)`
загрузчик) → два потока в одном командном списке NVRHI = `THREADING ERROR …
VkCommandPool is simultaneously used` и `vkCmdEndRendering-renderpass`
(`CommandList::open()` не вызывает `endRenderPass()` и не чистит
`m_CurrentGraphicsState`, в отличие от `close()`/`clearState()`). Рекурсивный
мьютекс (`VK_API_GUARD`, 68 методов) гонку сериализует, но дефект не снимает.
Решение: **отсрочка загрузок** — если `GetCurrentThreadId() != ownerThread`,
в список не пишем, ставим `needUpload` (у текстуры дополнительно `upLevel/
upFace/upRowBytes`, т.к. следующий Lock перезапишет уровень) и исполняем в
`FlushPendingUploads()`, которую вызывает `DrawImpl`. Важно: `EnsureRecording()`
обязан предшествовать КАЖДОЙ записи, т.к. `NoteUpload` закрывает и исполняет
список (иначе запись в закрытый буфер = `VUID-vkCmdUpdateBuffer-commandBuffer-recording`).
Загрузки батчатся по байтам: `uploadPending >= 64 МБ` → `FlushRecording`,
`flushesSinceIdle >= 4` → `waitForIdle`; без порогов winevulkan падает на
ассертах `vkBeginCommandBuffer`/`vkQueueSubmit` (пул команд пополняется только
после исполнения на GPU, `vulkan-queue.cpp:329`).

Swapchain: acquire переведён с семафора на **fence** (один семафор на все
acquire'ы = VUID-01779; fence создаётся без SIGNALED и сбрасывается ДО acquire);
`Present` больше не делает acquire в начале — кадр рисуется в картинку текущего
acquire'а, acquire идёт ПОСЛЕ present'а, первый acquire выполняется в
`CreateSwapchain` (иначе `вывод: в бэк-буфер=0 в offscreen=107143`);
`SurfaceObj::backBufferAlias` + `ResolveSurfaceIdx` (клиент кэширует поверхность
бэк-буфера один раз в `renderer.cpp:301-303`); при пересоздании swapchain'а
записи бэк-буфера и выданные ручки **переиспользуются** (иначе клиент держал
освобождённую картинку → зависание в `createImageView`); NVRHI не моделирует
`PRESENT_SRC_KHR` → ручные переходы `TransitionBackImage` до и после present'а;
readback картинки swapchain'а — прямым путём `RawReadback` (staging-путь для
presented-изображения зависает в `mapStagingTexture`: `mapBuffer` ждёт
`waitCommandList` по таймлайну, у native-обёртки пара «submission → сигнал» не
сходится); снимок кадра делается ВНУТРИ `Present` (из `renderer.cpp` после
`Present` читается картинка СЛЕДУЮЩЕГО acquire'а — потому vk_shot.bmp был чёрным).

Валидационный слой (`VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`,
`apt install vulkan-validationlayers spirv-tools vulkan-tools`) дал точный
список дефектов; сняты: порядок `addBindingSet` должен совпадать с порядком
`addBindingLayout` (NVRHI сопоставляет **позиционно**; было 00358/08600),
`setArraySize(6)` для куба (08866/07968), `BindingLayoutItem::setSize(count)` +
цикл по `BindingSetItem::arrayElement` для массивов ресурсов (08114), dummy VB
для пустых вершинных слотов (04001), depth-usage для D24S8-текстур (06088/06089:
клиент создаёт карту теней как обычную текстуру и привязывает как глубину),
привязка набора 1 (SSBO) dummy-буфером (08600 для set 1), `KindOk` — проверка
`kind` ручки (индекс сквозной внутри рода, поэтому hkTexture читался как запись
`d.surfaces`), подмена глубины не-равного размера (Vulkan требует равных
размеров RT/DS, D3D9 — нет), `FormatIntClass` (D3DCOLOR обязан стать
RGBA8_SINT/UINT — 08733), `SemEq` (`POSITION0`↔`POSITION`, `COLORn`↔
`TEXCOORD(100+n)`), закрытие атрибута без входа + заполнители location.
Остаток: 07752 (2D-вью там, где шейдер ждёт `Dim = Cube`), 00331 (dummy SSBO без
STORAGE_BUFFER-usage), 07904/08743 (интерфейс пар VS/PS: у VS вход без атрибута;
у PS вход без выхода VS — чинится согласованием пар в писателе).

Срез A шага 14 (портирование блобов `OnlineShaders`): дизассемблировано SM3
(`tools/hlsl_compile_test --blob --disasm` под Wine с подменой `d3dcompiler_47`),
восстановлены 4 пары (`default_shader`, `VertexShader`+`DiffuseMap`), написаны
HLSL-порты `Data/Shaders/OnlineShaders/port/*.hlsl` (константы — канонический
c-банк с `packoffset`), писатель `tools/online_shaders/build.py` (общий драйвер
`tools/shader_build/dxcdrv.py`, гейт по BuiltIn Position/FragCoord), sidecar-и
`<блоб>.spv` в pile (`mkpile.py --mask` стал повторяемым), читатель
`ImmRenderer::OpenSpvSidecar` + `LoadVertexShader/LoadPixelShader` (сначала
SPIR-V, при неудаче прежний DXBC). Композит создаётся: финальный вывод кадра —
экран-квад `ImmRenderer::RenderScreenQuad` с парой `VertexShader`+`DiffuseMap`,
именно он и давал `нет VS=…` каждый кадр и полностью чёрный кадр.

Дефект писателя `.shd`: `mkshd2.py:strip_trailer` резал вход по `v2HeaderOffset`,
но секции лежат ПЕРЕД заголовком → старый SPIR-V-слой попадал в «v1-часть»
мёртвым грузом (BasicMaterial 2 133 940 → 2 317 708 Б). Починка: читать `v1Size`
из заголовка (`V2H\0` + version + v1Size + targetCount) и резать по нему.
Корпус перегенерён (16 материалов, 0 пропусков), **v1-слой побайтово идентичен
16/16** → D3D9-путь не меняется. Грабля: без `--materials` писатель затирает
лишние контейнеры (31 файл вместо 16).

Не закрыто (WIP): кадр не осмысленный — `vk_shot.bmp` = 2 цвета: (230,230,30)
785664 px + (0,0,0) 768 px (одна колонка) при 2099 present'ах и 931959 выводах;
гипотеза — геометрия не проходит из-за несогласованного вершинного интерфейса
(07904/08733/08743) и/или выбора варианта материала (у Vulkan-бэкенда
`caps=0x5`, у DXVK `caps=0x7` → разные индексы материалов, третий снимок живых
индексов не снят). Не снят регресс tiny10 на этой сборке, диагностика не обрезана
(`VK_TRACE`-кольцо, пады vtable, `VkBeat`, `Watchdog`, `NoteForeign`, `VkFb`,
`VkDs`, `VkThread`, `VkProbe`, `VkDiag`, `VkIlMap`), SSBO-набор пока dummy.

RenderDoc (разрешение пользователя m12251): **захват получить не удалось**,
ветка тупиковая. Сделано: скачан официальный 1.46 (`renderdoc.org/stable/1.46/
renderdoc_1.46.tar.gz`), манифест слоя починен (в поставке заглушка
`"library_path": "/io/dist/lib/librenderdoc.so"`), слой виден хостовому
загрузчику (`VK_ADD_IMPLICIT_LAYER_PATH` + `ENABLE_VULKAN_RENDERDOC_CAPTURE=1` →
`vulkaninfo --summary` показывает `VK_LAYER_RENDERDOC_Capture`), GUI поднят под
Xvfb и управляется XTEST (`/tmp/xui.py`: click/type/key/ctrl/schord; координаты
брать из root-скриншота, поля чистить `End`+`Shift+Home`+`Delete`, `Ctrl+A` в Qt
не работает). Проверено и отвергнуто: `renderdoccmd capture` не делает
авто-захват при выходе; `RENDERDOC_CAPOPTS` — 48-символьная кодировка полей
(расшифрована: api-validation→5, capture-callstacks→7, …), диапазона кадров нет;
в официальном tarball НЕТ python-модуля (`_renderdoc*.so`), поэтому
`qrenderdoc --python` скрипт не выполняет; crash-capture (`delayforcrash`) в либе
нет. Инъекция: `/usr/bin/wine` и `wine64-stable` — шелл-скрипты, хуки уходили в
`/usr/bin/dash`; настоящий ELF — `/usr/lib/x86_64-linux-gnu/wine/wine`, при нём
`Loading into …/wine/wine` и `Registering Vulkan hooks` есть, но `.rdc` не
появляется. Добавлено в код: env `PW_VK_LAYERS` (включение слоёв инстанса самим
приложением, максимум 8) — слой подтверждается в логе клиента, захвата всё равно
нет: под winevulkan хуки RenderDoc обходятся загрузчиком winevulkan.

**2026-10-22: вывод «тупиковая» отменён** — захват под Wine получен; ошибка была
в способе включения (хуки функций вместо слоя) и в отсутствии внешнего триггера.
Рабочий контур — ниже, раздел «RenderDoc под Wine: капча кадра на заданной
секунде матча (2026-10-22)».


### Сделано (шаг 14 срез A + нативный RenderDoc-стенд + рекордер вывода — 2026-10-17, WIP; продолжение в новой сессии)

Ветка `nvrhi`, HEAD `887f6421f` (шаги 13.4c–13.4e + срез A). В дереве
**незакоммичены** правки рекордера (`Src/Render/nvrhibackend.cpp`,
`nvrhibackend.h`, `LayoutObj::desc`) и `tools/nvrhi_probe/probe.cpp`
(X11-стенд + RenderDoc-триггер) + `tools/nvrhi_probe/CMakeLists.txt` (`X11`).

#### Почему RenderDoc только в нативном процессе (замер, не повторять)

Полный порт клиента под Linux = недели: в `Src/` (3094 `.cpp`) SEH `__try` —
47 файлов / `__except` — 44 (у GCC аналога нет), `windows.h` — 38, `WinMain` — 11,
`#pragma comment` — 20, `intrin.h` — 550, `nstl::` — 783; в `Src/Render` 81
`.cpp`, из них **32 трогают d3d9/D3DX и 0 платформенных `#ifdef`**, `D3DXCreate*`
— 7 файлов (D3DX9 = только MSVC .lib). Граф MSVC-зависим
(`Tools/ClientCMake/gen_cmake.py` переводит `vcxflags`). НЕ блокираторы:
`Src/System`, `Network`, `Shared`, `Game` компилируются GCC (сервер
`UniServerApp` собирается нативно; `LINUX_PORT_REPORT.md`, ветка `linux_server`,
`57f57c147`).

Вывод: рендер-подмножество (NVRHI + Vulkan + SDL3/X11) независимо и собирается
СЕЙЧАС → отладочный контур строится на нём, клиента не портируем. Прежний вывод
«RenderDoc — тупик» (§ выше) верен только для Wine: инъекция уходит в
wine-загрузчик, а гость (Windows PE) не может вызвать `RENDERDOC_GetAPI`.

#### п.1 — ГОТОВ: нативный X11-стенд кадра + захват RenderDoc

`tools/nvrhi_probe/probe.cpp`: на Linux `#define VK_USE_PLATFORM_XLIB_KHR 1` +
`#include <X11/Xlib.h>` ДО `<vulkan/vulkan_xlib.h>` (сам он Xlib не включает),
затем ОБЯЗАТЕЛЬНО `#undef None True False Status Success Bool Always Never
Complex AnyOrder GrayScale StaticGray StaticColor PseudoColor TrueColor
DirectColor TileShape Unsorted Any` (X.h/Xlib.h переопределяют идентификаторы
`nvrhi.h:685 struct TileShape`, `nvrhi.h:3818`, `nvrhiHLSL.h:85` →
`expected identifier before numeric constant`). Указатели surface/swapchain и их
загрузка выведены из-под `#ifdef _WIN32` в
`#if defined(_WIN32) || defined(VK_USE_PLATFORM_XLIB_KHR)` (иначе
`нет VK_KHR_xlib_surface в драйвере` — загрузка оставалась внутри `#ifdef`).
Экземплярные расширения на Linux: `VK_KHR_surface` + `VK_KHR_xlib_surface`.
`ProbeNativeWindow(...)`: `XOpenDisplay` → `XCreateWindow` 640x480 + `XMapWindow`
+ `XSync` → `vkCreateXlibSurfaceKHR` → swapchain (FIFO, 3 картинки) →
`createHandleForNativeTexture(VK_Image, …, SBGRA8_UNORM, setIsRenderTarget,
setInitialState(RenderTarget), setKeepInitialState(true))` → цикл
acquire/clear/execute/present. Замер: `[PASS] x11: … цикл present'а present'ов 60
из 3 картинок`. `tools/nvrhi_probe/CMakeLists.txt`: `if(UNIX AND NOT APPLE)
target_link_libraries(nvrhi_probe PRIVATE X11) endif()`. Сборка:
`./tools/nvrhi_probe/build_probe_linux.sh --clean` → `build-linux/nvrhi_probe`.

**Рабочий рецепт захвата** (тупик `renderdoccmd capture` — он НИЧЕГО не
инжектирует: у цели `LD_PRELOAD=` пусто, `VK_INSTANCE_LAYERS` нет; есть только
`ENABLE_VULKAN_RENDERDOC_CAPTURE`, `RENDERDOC_CAPFILE`, `RENDERDOC_CAPOPTS`):

    LD_PRELOAD=/tmp/renderdoc_1.46/lib/librenderdoc.so \
    VK_ADD_IMPLICIT_LAYER_PATH=/tmp/rdlayer/implicit_layer.d \
    ENABLE_VULKAN_RENDERDOC_CAPTURE=1 DISPLAY=:97 \
    PW_PROBE_FRAMES=40 PW_PROBE_MANUAL=20 ./build-linux/nvrhi_probe

Слой — правленый манифест `/tmp/rdlayer/implicit_layer.d/renderdoc_capture.json`
(в поставке заглушка `"library_path": "/io/dist/lib/librenderdoc.so"` →
`/tmp/renderdoc_1.46/lib/librenderdoc.so`). В коде: `RenderDocApi()` =
`dlsym(RTLD_DEFAULT, "RENDERDOC_GetAPI")` + `getApi(eRENDERDOC_API_Version_1_4_0,
&out)` (**1 = успех, 0 = отказ**, `renderdoc_app.h:867`; в 1.46
`RENDERDOC_API_1_4_0` — typedef на `RENDERDOC_API_1_7_0`, `StartFrameCapture`/
`EndFrameCapture` возвращают `void`), env `PW_PROBE_MANUAL=<кадр>` (Start/End
вокруг кадра) и `PW_PROBE_CAPTURE=<кадр>` (`TriggerCapture`). Результат:
`/tmp/rdcap/manual_capture.rdc` 8929 Б; проверка без GUI:
`/tmp/renderdoc_1.46/bin/renderdoccmd thumb -o /tmp/rdcap/thumb.jpg -s 512
manual_capture.rdc` → однотонный красный кадр = ровно тот `Color(1,0,0,1)`, что
залит внутри захваченного кадра.

#### п.2 — ГОТОВ (не коммичен): рекордер вывода клиента

`Src/Render/nvrhibackend.cpp`: env `PW_VK_RECORD=<имя файла>` (имя БЕЗ `/`,
иначе Wine трактует путь), `PW_VK_RECORD_MAX` (дефолт 40), `PW_VK_RECORD_RT=ШхВ`
фильтр по размеру цели. `RecOpen` в конце блока env в `Init`, `RecClose` в
`Shutdown()`, `RecDraw(...)` в `DrawImpl` перед `VK_TRACE("setGfxState >")`
(т.е. после `FindOrCreateResMap`, потоков, IB и вьюпорта — всё состояние
доступно). Логи: `VkRec: вывод пишется в %s (лимит %u, цель %ux%u)`,
`VkRec: записано выводов=%u ресурсов=%u`.

Формат (всё u32/f32 LE): `"PWRD"` + u32 **версия 2** + u32 ширина + u32 высота
swapchain'а (может быть `0x0` — swapchain создаётся после `Init`; реплеер берёт
размер цели из каждой записи вывода). Далее записи: u32 тип + поля. Типы
`kRecShader=1, kRecBuffer=2, kRecTexture=3, kRecLayout=4, kRecDraw=5, kRecEnd=6`.
Блоб = `u32 размер; u32 записано; байты`, потолок `kRecBlobCap = 8 MiB`
(геометрия карты — гигабайты; при превышении байты НЕ пишутся, реплеер подставляет
dummy). Записи ресурсов идут ПОСЛЕ тела вывода, id дедуплицированы по seam-индексу
(`recShader/recBuffer/recTexture/recLayout` — векторы пар), `0xffffffff` = `kNoRes`.

- SHADER: id, стадия (0 VS / 1 PS), entry (блоб), SPIR-V (блоб; `ShaderObj::spv`
  наполняется при создании шейдера).
- BUFFER: id, isIndex, index32, блоб байтов CPU-зеркала (`BufferObj::cpu`).
  Смещения потоков пишутся **логическими** (внутри seam-буфера), slab-смещение
  `BufferObj::off` в файл НЕ попадает → реплеер делает каждому буферу отдельный
  nvrhi-буфер.
- TEXTURE: id, w, h, mipLevels, cube, seamFormat, блоб `mirror` (уровень 0).
- LAYOUT (v2): count `VertexElementDescriptor` + по 5 слов (`stream, offset,
  type, usage, usageIndex`), ЗАТЕМ count `lo.desc` + на атрибут блоб имени и
  6 слов (`format, arraySize, bufferIndex, offset, elementStride, isInstanced`) —
  это готовый массив, реально переданный в `createInputLayout` (`LayoutObj::desc`,
  стэш в `BuildInputLayout` после `lo.il = il;`). Дублирование сознательное:
  в нативном реплеере нельзя повторять ни сканер SPIR-V, ни сопоставление
  семантик (`SemEq`, `FormatIntClass`, заполнители location).
- DRAW: 28 слов — `recDraws, vsId, psId, layId, rtW, rtH, rtDepth, dsW, dsH,
  dsDepth, prim, indexed, startIndexLocation, startVertexLocation, primitiveCount,
  vtx, instanceCount, vpX, vpY, vpW, vpH, minZ(f32), maxZ(f32), scissorValid,
  scL, scT, scR, scB`; затем u32 `kMaxVertexStreams`(=8) + по 3 слова на поток
  (`bufId, vbOffset, vbStride`); затем 5 слов `ibId, instBufId, instSlot,
  instOffset, instStride`; затем u32 `kBackendSamplerSlots`(=16) + по 12 слов на
  слот (`texId` + `minFilter, magFilter, mipFilter, addressU, addressV, addressW,
  borderColor, lodBias, maxAnisotropy, maxMipLevel, set`); затем **29** слов модели
  состояний (`cullMode, fillMode, scissorTestEnable, zEnable, zWriteEnable, zFunc,
  slopeScaleDepthBias(f32), depthBiasRaw, stencilEnable, stencilFunc, stencilRef,
  stencilReadMask, stencilWriteMask, stencilFail, stencilZFail, stencilPass,
  alphaBlendEnable, separateAlphaBlend, srcBlend, destBlend, blendOp,
  srcBlendAlpha, destBlendAlpha, blendOpAlpha, colorWriteEnable, alphaTestEnable,
  alphaRef, alphaFunc, clipPlaneEnable`); затем два банка констант (`u32 1024` +
  1024 float VS, то же PS — из `m_constShadow[csVertex]/[csPixel]`,
  `SHADER_CONST_REGISTERS=256`); затем (v2) count ресурсов набора + по 5 слов
  (`set, binding, kind, dim, count`) из `ResMapObj::items` — нумерация
  `enum EResKind { rkSampler=0, rkTexture, rkUbo, rkSsbo, rkOther }`
  (`nvrhibackend.cpp:118`).

**Дефект формата, найденный офлайн-парсером (починен):** id ресурсов считались
как побочный эффект вызовов `RecShaderId/RecBufferId/RecTextureId/RecLayoutId`,
поэтому записи ресурсов втиснуты в ТЕЛО первого вывода — парсер расходился на
`struct.error: unpack_from requires a buffer of at least 19589490848 bytes`,
после поправки счётчика (30→28 слов) — на `…402992500 bytes at offset 136`
(offset 136 и доказал, что первая запись уже DRAW). Починка: в начале `RecDraw`
собираются ВСЕ id, потом пишется тело.

Первый снимок (до починки): `wine-client/Game/Bin/vk_rec.pwrd` 18 223 104 Б,
клиент при этом штатно в бою (`VkStatsflip: presents=1799 recreates=1
swap=1024x768`, `VkResflip: буферы=10858 шейдеры=120 раскладки=20
пайплайны=3978/3978 (попаданий 602269) вывод=606461 загруз=1266198
(ожиданий кольца 202)`). Сборка с починками и v2 — `/tmp/vk14a_build51.log`
`ИТОГ: OK`, 0 `error C`, exe 15 930 880 Б; **на стенд ещё не развёрнут**
(там exe 15 927 296 Б и старый `vk_rec.pwrd` v1).

#### п.3 — КОНТРАКТ реплеера (следующее действие, кода ещё нет)

`tools/render_replay/` (нативный Linux, g++/CMake по образцу
`tools/nvrhi_probe/CMakeLists.txt`, линкует нативные `nvrhi` + `nvrhi_vk` +
Vulkan-Headers; `X11` не нужен — рисуем offscreen, захват работает без swapchain'а):
1. Бутстрап Vulkan — копия `ProbeVulkan()` (`tools/nvrhi_probe/probe.cpp:2299`):
   `VkApplicationInfo.apiVersion = VK_API_VERSION_1_3`, exts только instance-level
   (surface не просить), `loadInstanceLevel`, `EnumeratePhysicalDevices`,
   `VK_QUEUE_GRAPHICS_BIT`, `vkCreateDevice` (NVRHI требует
   `VK_KHR_synchronization2` и timeline semaphore), затем
   `nvrhi::vulkan::DeviceDesc dd; dd.errorCB = &g_probeMsg; dd.instance/physicalDevice/
   device/graphicsQueue/graphicsQueueIndex` → `nvrhi::vulkan::createDevice(dd)`.
2. Чтение PWRD v2 → шейдеры (`createShader(ShaderDesc().setShaderType(...).
   setEntryName(entry), spv, size)`), буферы (`createBuffer` + `writeBuffer` из
   байтов; отдельный буфер на запись, slab не воспроизводить), текстуры
   (`createTexture` + `writeTexture(tex, 0, 0, bytes, rowPitch)`; mip>0 и
   сжатые форматы — dummy), раскладки (`createInputLayout(desc, n, vs)` —
   **из `lo.desc`**, location = индекс массива), наборы (из `ResMapObj::items`:
   set 0 = два UBO-банка (binding 0 VS, 1 PS), set 1 = SSBO (пока dummy, как в
   клиенте), set 2 = текстуры по `binding`/`arrayElement`, set 3 = сэмплеры из
   11 слов состояния — таблицы `AddressOf`/фильтры перенести из
   `nvrhibackend.cpp:3843,3886`).
3. Цель: RT `BGRA8_UNORM` `rtW x rtH` + DS `D24S8` `dsW x dsH` (глубина обязана
   быть равного размера — иначе VUID-06088), `createGraphicsPipeline` из модели
   (переводчики `CullOf`/`FillOf`/`ZFuncOf`/blend — из `FillPipelineState`,
   `nvrhibackend.cpp:3733`; `setFrontCounterClockwise(true)` обязательно),
   `createFramebuffer`, `setViewport`/`addScissorRect` по записанным значениям,
   `setGraphicsState` + `drawIndexed(DrawArguments().setVertexCount(vtx)...)`.
4. Результат: `mapStagingTexture` → число ненулевых пикселей + PPM/BMP; при
   `PW_RP_CAPTURE=1` — `StartFrameCapture`/`EndFrameCapture` вокруг вывода тем же
   рабочим способом (LD_PRELOAD + implicit-слой) → капча, в которой видно
   Pipeline State конкретного вывода, дающего пустоту.
5. Цель реплеера — объяснить пустой кадр: остаток VUID 07904 (у VS вход без
   атрибута), 08733 (формат вершинного входа vs тип в шейдере), 08743 (у PS вход
   без выхода VS) и выбор варианта материала (у Vulkan-бэкенда `caps=0x5`, у DXVK
   `caps=0x7` → разные индексы, третий снимок живых индексов не снят).

#### Процедура прогона Wine-стенда (повторять дословно)

1. `cp prime-world-classic-client-modern/pw/branches/r1117/Tools/ClientCMake/
   build/x64-wine/PW_Game.exe /home/rekon/PW/wine-client/Game/Bin/PW_Game.exe`
   — **после каждой сборки**, иначе прогоны меряют старый exe (грабля: артефакт в
   двух местах, `build/PW_Game.exe` и `Tools/ClientCMake/build/x64-wine/`;
   `wine-client` лежит в КОРНЕ `PW`, не в worktree).
2. Сервер: `pkill -x UniServerApp; cd /home/rekon/PW/local-gameserver-test &&
   setsid nohup ./run_local.sh > server.log 2>&1 &` (ждать 12 с, порты 27301/27310
   в `ss -lun`).
3. Токен ОДНОРАЗОВЫЙ: `timeout 90 docker compose -f pw-api-docker/docker-compose.dev.yml
   exec -T -e PW_VERSION=2.16.1 api node cli/session.js --ips 7f000001 |
   grep -oE "pwclassic://runGame/[a-f0-9]+/2\.16\.1/2/7f000001/27300" > /tmp/vkurl.txt`
   (повторный URL → `pick: n=1 alive=0 dead=…` + `web session push failed on ALL
   servers`).
4. Запуск: `cd /home/rekon/PW/wine-client && setsid nohup env PW_VK_BACKEND=1
   PW_VK_TRACE=0 PW_VK_WATCHDOG=0 PW_VK_SHOT=600 PW_VK_SHOT_PROBE=0
   PW_VK_RECORD=vk_rec.pwrd PW_VK_RECORD_RT=1024x768 PW_VK_RECORD_MAX=80
   VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation timeout 190 ./run_vk_flip.sh
   > /tmp/runNN.log 2>&1 &`.
5. Лог клиента — `~/Документы/My Games/Prime World Classic/Session/Logs/<UTC>/
   PW-pwclassic-2.16.01.0000-<UTC>-output.txt`, каталоги **прунятся самим
   клиентом** при следующем запуске → сразу `cp` в `/tmp/run_now.log`.
6. В `wine-client/Game/Profiles/autoexec.cfg` обязателен `setvar auto_screenshot 0`
   (иначе `ReadSurface` по presented-картинке вешает `waitForIdle` навсегда),
   Xvfb `:97` (скрипт поднимает сам), `setvar shader_target 2` в конфиге —
   оставляет старый диагностический блок `ShaderTarget: … беру SM3`.
7. Разбор `vk_shot.bmp` (BMP 24 bpp, height отрицательный): `off=d[10:14]`,
   `w,h,bpp = d[18:28]`, `row=((w*3+3)//4)*4`, считать уникальные цвета.

### Сделано (шаг 14 срез B: пустой кадр закрыт офлайн-выравниванием интерфейсов — 2026-10-19/20, `e5a4b174e`)

HEAD был `7fcf214ab`; в дереве лежал незакоммиченный срез от 18.10. Закоммичено
одним срезом `e5a4b174e` (74 файла, +4831/−75): бэкенд, sidecar-и `.spv`,
инструменты, PCH-починка, обрезка диагностики.

#### Пустой кадр — ЗАКРЫТ (причина: несовпадение in/out-интерфейсов пар VS/PS)

Сработал не реплеер (п.3 выше), а офлайн-разбор рекорда: `tools/render_replay/parse_pwrd.py`
читает PWRD v2, `iface_check.py` сравнивает out-интерфейс VS с in-интерфейсом PS по
SPIR-V, `match_shd.py` / `bank_check.py` / `bank_poison.py` / `inspect_draw.py` /
`texdump_view.py` — банк констант и одиночный вывод. Починка в писателе:
`tools/shader_build/spirv_iface.py` (каноническая карта `semantics -> location` на
весь корпус) + `tools/shader_build/localign.py` (перекладка location в sidecar-ах).
Закрыты VUID 07904 (вход VS без атрибута), 08733 (формат вершинного входа vs тип в
шейдере), 08743 (вход PS без выхода VS).

Замер (прогон 18.10, `/tmp/run106_cli.log`): presents=3899, вывод=1891009,
0 `D3DERR_INVALIDCALL`, 0 «Вывод: нет PS/VS», unsupported=0;
`wine-client/Game/Bin/vk_shot.bmp` 1024x768: 97087 уникальных цветов, чёрного 2.15%
(до починки — почти весь экран чёрного).

PostFX-шейдеры: исходники нашлись в `Src/Render/OnlineShaders/*.psh|*.pss|*.vsh` →
`tools/online_shaders/build_all.py` собирает 44 sidecar-а тем же конвейером, что
корпус (SM3-дизассемблирование среза A не понадобилось).

#### Валидация впервые включена: что реально осталось (прогон 19.10, `/tmp/run107.log`)

Грабля процедуры: вывод слоя уходит в **stderr обёртки** (`/tmp/runNN.log`), а не в
лог клиента — искать в клиентском логе бесполезно. `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`
работает из коробки: winevulkan использует хостовый `libvulkan`, слой — хостовый
(`/usr/share/vulkan/explicit_layer.d/VkLayer_khronos_validation.json`).
`duplicate_message_limit = 10` → счётчики ниже истинных.

- `VUID-vkCmdBindVertexBuffers-pBuffers-04001` ×10 — в слот потока биндится
  `VK_NULL_HANDLE` (у вывода потоков меньше, чем объявлено). Лечится `nullDescriptor`
  или dummy-VB в пустые слоты.
- `VUID-vkCmdDrawIndexed-imageLayout-00344` ×10 — **новая находка**: сэмпл `ZMap`
  (Set 2, Binding 0) читает картинку в layout `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`,
  нужно `DEPTH_STENCIL_READ_ONLY_OPTIMAL`. Это глубина как текстура → все эффекты по
  глубине сейчас считаются по мусору.
- `VUID-vkCmdDrawIndexed-viewType-07752` ×10 — `envMapSampler` (Set 2, Binding 12):
  вью `VIEW_TYPE_2D` против `Dim = Cube` в шейдере.
- `VUID-VkGraphicsPipelineCreateInfo-Input-07904` ×2 и `-Input-08733` ×6 — остаток
  несовпадений интерфейса (выровнен не весь корпус).
- `00331` (dummy SSBO без `STORAGE_BUFFER`-usage) в этом прогоне не сработал —
  проверять после шага 13.5.

#### Кадр есть, но гейт паритета нерабочий: A/B 3000-го кадра

`PW_VK_SHOT=3000` (снимок внутри `NvrhiBackend::Present`) против эталона
`tools/render_shot/refs/wine-dxvk-2026-10-15/pw_auto_bb_3000.png`, меряет
`tools/render_shot/shot_stats.py`: средняя |Δ| 35.1, пикселей |Δ|>32 — 258666
(32.89%), светимость 73.6 против 70.7, уникальных цветов 97358 против 172705,
чёрного 2.15% против 0.12%.

Визуально (монтаж `/tmp/ab_vk_dxvk.png`): сцена рисуется — геометрия, свет, UI
(иконки, портрет, полоса здоровья), но кадр Vulkan'а снят в **другом ракурсе и в
другом моменте** (камера ближе, нет панели «До выхода солдат», нет неймплэйта) →
32.89% ничего не измеряют. Реальные дефекты левого кадра: (а) белый прямоугольник
~43560 пикселей в левой средней части, (б) миникарта — чёрный круг (не рисуется),
(в) ярко-пурпурные квадры вместо текстур на части объектов, (г) синее пятно блика не
на месте.

Гейт надо перестроить: **парный** прогон одного стенда (Vulkan `PW_VK_SHOT=N` и
DXVK `auto_screenshot N` при `PW_VK_BACKEND=0` + `WINEDLLOVERRIDES d3d9=n,b`), один
и тот же кадр, сравнение по нему.

#### Новая находка: ключ пайплайна содержит индексы поверхностей (в плане не было)

`FindOrCreatePipeline`, `nvrhibackend.cpp:4632`: в ключ входят `p.rt == d.curRt &&
p.ds == d.curDs` — **индексы поверхностей**, а не формат+размер. Отсюда рост числа
пайплайнов вместе с поверхностями (`VkSum`, прогон 19.10): 230@299 → 3166@599 →
8998@1199 → 17873@2099 → 29955@3599 present'ов; в длинном прогоне 18.10 — 32782
пайплайна при 14536 поверхностях. Для сравнения §1.4 мерил 206 ключей. Каждый
пайплайн держит свой `Framebuffer`. Все три кэша — линейный скан на каждый вывод:
`pipes` (`:610`), `setCache` (`:682`, чистится при >4096), `resMaps` (`:679`) при
1.86M попаданий за прогон.

#### Диагностика обрезана (п.5 старого списка) — как теперь мерить

`VkVerbose()` (`nvrhibackend.cpp:1310`) читает env `PW_VK_STATS` (по умолчанию ВЫКЛ):
под ним живут `VK_TRACE`-кольцо, пады vtable, `VkBeat`, `Watchdog`, `NoteForeign`,
`VkFb`, `VkDs`, `VkThread`, `VkProbe`, `VkDiag`, `VkIlMap` и полные `DumpStats`.
Без env раз в 300 кадров — одна строка `NvrhiBackend::DumpCompact()` (`:2437`):
`VkSum: presents=… вывод=… пайплайны=N/M (попаданий …) загруз=… объектов B/T/S=…`.
Рекордер вывода — по-прежнему за env `PW_VK_RECORD`.

#### Гейты: пройдено / не пройдено

- Wine x64 shipping: OK, exe 15 961 088 Б, `check_exe.py` OK (x64, PE32+, subsystem 2,
  Characteristics 0x0022, манифест `RT_MANIFEST` id 1, FileVersion 2.16.1.0).
- `vulkan-1.dll` в импортах exe **нет** (закрытый импорт `LoadLibraryW` +
  `GetProcAddress`). D3D9 ещё жив: `d3d9.dll` — 1 функция (`Direct3DCreate9`),
  `d3dx9_43.dll` — 14 (13 матрично-векторных + `D3DXCreateTextureFromFileInMemoryEx`
  = гильдейские эмблемы, PENDING §9).
- Регресс tiny10 — **НЕ снят**: ВМ выключена (`ssh pw-tiny10` — таймаут на
  10.78.0.10:22), поднимать `vm10/run_vm10.sh`, мост — `sudo vm10/net10.sh`.
- Скриншот против эталона — снят, но моменты не совпадают (см. выше).

#### п.3 «контракт реплеера» — ЗАКРЫТ как устаревший

Нативный реплеер не построен и не нужен: причина пустого кадра найдена офлайн по
рекорду PWRD (парсер + чекер интерфейсов), починка сделана в писателе sidecar-ов.
`tools/render_replay/` остаётся набором офлайн-инструментов разбора, не реплеером.
RenderDoc-стенд `tools/nvrhi_probe` остаётся — он закрыл другую задачу (захват
нативного процесса).

#### Открытое (по порядку, 2026-10-20)

1. ЗАКРЫТО 2026-10-20 (`0cd618e51`) — см. секцию ниже: ключ пайплайна по
   формату+размеру, framebuffer отдельным кэшем, все три кэша на поиск.
2. Валидация (`VK_LAYER_KHRONOS_validation` на Wine-стенде) — из списка VUID
   закрыты все, кроме 07904. Закрыто: 07752 (cube-вьюха) и 04001 (null-VB) —
   `21fe654f8`; 08733 (единственный целый формат в живых раскладках — `UBYTE4
   TEXCOORD2` маски углов `ParticleFXMaterial`, вход переведён на `uint4` под
   `PW_MODERN_ARRAYS`, x12 -> 0) — `11d05752c`; 00344 (`ZMap`: PostFX читает
   ровно ту же картинку, что приложена depth attachment'ом того же вывода —
   attachment теперь снимается, когда тест глубины и трафарет выключены;
   попутно исправлена семантика `depthWriteEnable` по D3D9; x20 -> 0, кадр не
   изменился) — `80909c1cb`. Остаётся 07904 (у VS вход без атрибута): три
   подхода измерены, все трое ломают кадр — фича
   `VK_EXT_vertex_attribute_robustness` (мир чёрный, 81,7%), свой VB-слот со
   значением (0,0,0,1) (0,14% -> 82,67% чёрного), пэддинг хвоста location
   дубликатом атрибута (VUID исчезает, но мир режется чёрным клином — 40,64%
   чёрного); `VK_KHR_maintenance9` задаёт то же (0,0,0,1) и повторит второй
   исход. См. секции «vertexAttributeRobustness», «Незакрытые входы VS»,
   «07904: три подхода» в конце.
3. Гейт паритета: парный прогон Vulkan/DXVK на одном стенде, тот же кадр, тот же
   момент; эталоны переснять, дальше мерить только по этому диффу. С 2026-10-20
   (`e7e80ef0c`) есть общий способ снимать кадр с обоих бэкендов —
   `tools/render_shot/xvfb_shot.sh` (снимок окна с Xvfb, клиентский readback не
   нужен); сам дифф ещё не собран. По ходу — 4 видимых дефекта (белый квад,
   чёрная миникарта, отсутствие текста, пурпурные цилиндры); разбор — в секции
   «Визуальные дефекты: что измерено» в конце. Из четырёх текст HUD закрыт
   2026-10-20 (`27b2662d9`) — вход цвета в UI-шейдерах переведён с `int4` на
   `float4`, см. секцию «Сделано: текст HUD» в конце. Белая плашка 330x132
   ЗАКРЫТА 2026-10-20 (`54d329e22`) как **не дефект**: в длинном DXVK-прогоне
   того же стенда — тот же прямоугольник и то же число белых пикселей, значит
   рисует клиент, а не бэкенд (см. секцию «Белая плашка 330x132» в конце).
   Пурпурные цилиндры — 0 пурпурных px, не воспроизводится. Остаётся одно:
   миникарта **темнее** эталона (содержимое в ней есть; «пустой круг» было
   неверной формулировкой). 2026-10-20 претензия подтверждена независимым
   эталоном: в нативном D3D9-кадре tiny10 (`PwE2E64`, +85 с) миникарта
   заполнена — местность, сетка, иконки; в Vulkan-кадре того же стенда — тёмный
   круг, значит дело в бэкенде, а не в состоянии матча. **Причина найдена и
   закрыта 2026-10-20 (`8108477cb`)**: вьюпорт и сциссор не ограничивались
   размером цели. Клиент рисует экранные квады, не меняя вьюпорт (по PWRD у
   ВСЕХ выводов vp=(0,0,1024x768), в том числе у 16 выводов в миникартный
   200x201 и у композита тумана 205x205), D3D9/DXVK применьшают вьюпорт к цели,
   Vulkan — нет, и офскрин-проходы видели только верхний левый угол источника.
   Замер: композит тумана был 100% сплошного тумана (uniq 1) при источнике с
   16,9% ясных пикселей и связной ясной областью 34x34; после климпа — 87,1%
   тумана / 12,9% ясных, uniq 60. RT миникарты: ненулевых 47,3% -> 84,5%,
   уникальных цветов 79 -> 420, mean (4,9,2) -> (20,33,10); в кадре появилась
   шестиугольная сетка, полосы местности, красно-зелёные маркеры и прямоугольник
   камеры — в тех же местах, что в нативном кадре. См. секцию «Сделано: вьюпорт
   ограничивается размером цели» в конце. Остаток по миникарте (не закрыт):
   (а) она всё ещё темнее нативной (кроп 220x220+790+530: mean (39,47,36)
   против (71,70,56) у tiny10, ярких пикселей 38,7% против 55,4%, тёмных 39,5%
   против 3,9%) — нет голубой подсветки реки/местности, (б) нет декоративных
   иконок миникарты (`UI/Textures_default/AdvMap/minimap_icons.dds`,
   `Minimap.cpp:611`) — в Vulkan-кадре есть только красно-зелёные маркеры
   отрядов. Кандидат «маска природы 656x656 (`blendMask`) не заполнена»
   ПРОВЕРЕН и ОТВЕРГНУТ 2026-10-20 (`d6856956c`): её GPU-содержимое — штатный
   one-hot (0,255,0) 75,4% / (255,0,0) 11,5% / (0,0,255) 11,3% при альфе 0
   (`NatureMap.psh`: `totalColor = colorA*r + colorB*b + colorN*g`), и мост
   `hexParams` его не изменил; прежнее утверждение «blendMask не
   инициализирована» было ложным. Мост всё же обогатил саму цель миникарты
   (уникальных цветов 420 → 5066). Главная помеха дальше — нет эталонной пары кадров:
   DXVK на этом хосте деградировал (мир чёрный, 86%), короткие DXVK-кадры для
   сравнения невалидны.
4. 13.5 закрыт 2026-10-20 целиком: загрузка UBO диапазоном (`bae7a241d`) и
   настоящий SSBO костей (`e78b8fc79`) — см. секции «UBO по грязному диапазону» и
   «SSBO = BonesMatrices» в конце. Остаётся мелочь: чистка авто-патча спайка.
   Шаг 12 (мосты `c`-регистров в SSBO) закрыт частично (`d6856956c`) — см. п.
   «Что остаётся на шаге 12» выше: нужны мосты `instanceData` и `data`.
5. ЗАКРЫТО 2026-10-20 — регресс tiny10 снят: нативная сборка HEAD (`fb9ece507`)
   `RC=0`, `check_exe.py` ИТОГ: OK, E2E (`PwE2E64`, токен `--ips 0a4e0001`) доходит
   до боя, кадр 3000 против эталона `stage3-shd2-2026-10-15` — средняя |Δ| 3,0,
   >64 1,43%, что МЕНЬШЕ разброса двух прогонов того же exe (>64 2,23%) ⇒
   D3D9-контур не деградировал. Замеры и процедура — в секции «Сделано: регресс
   нативной сборки на tiny10» в конце; ВМ по ходу перестала грузиться и была
   поднята (диагностика — скилл `win-vm`, раздел «A disk read error occurred»).
6. Шаг 14 закрыт 2026-10-20: pile воспроизводится из репозитория —
   `mkpile.py build --root pw/branches/r1117/Data --mask '*.shd' --mask '*.spv'
   --only-v2` даёт 60 записей (16 корпусных v2 + 44 sidecar), 263 824 Б,
   побайтово равный собранному вручную; на стенде кадр нормальный (белых
   43 560, чёрных 1 030, 0 ERROR/FATAL, 0 «комбинации нет»). Третий снимок
   живых индексов снят (Wine + Vulkan, RTX 3060, `caps=0x5`): 58 запросов
   (материал, индекс), 58 целей найдено, 0 «комбинации нет», 0 «цель не
   создана».
7. Этап 4: метрика снята `Tools/ClientCMake/d3d9_closure_check.py` — из 1150 TU графа
   607 достают `d3d9.h`, allowlist 8 → 601 нарушение. Разносчики: `Render/dxutils.h`
   858 TU, `Render/renderer.h` 584, `PF_GameLogic/stdafx.h` 498,
   `Render/DxIntrusivePtr.h` 348, `Render/texture.h` 206. Дешевле всего —
   `DxIntrusivePtr.h` (у seam уже свой `SeamUnknown`-префикс vtable, `unknwn.h` не нужен).
8. Коммит/пуш — ТОЛЬКО в ветку `nvrhi` (разрешение m08533: «Коммитить можно только в
   текущую ветку prime-world-classic, но не в тесты и не в прод»).

### Сделано (кэш пайплайнов по формату+размеру цели — 2026-10-20, `0cd618e51`)

Ключ пайплайна включал **индексы поверхностей** (`p.rt == d.curRt && p.ds == d.curDs`,
`nvrhibackend.cpp:4632` до правки), поэтому пайплайнов было столько же, сколько пар
(шейдеры x поверхности): 32782 за прогон 18.10 при 14536 поверхностях, каждый со своим
`Framebuffer`. Теперь в ключе формат и размер RT/DS (`SurfKey`: fmt/w/h по 20 бит +
признак глубины в 61-м бите, упаковка без потерь), `Framebuffer` — отдельный кэш
`fbIdx` по паре индексов поверхностей (`FindOrCreateFb`). Контейнеры переведены со
сканов на поиск: `pipeIdx` (`std::map` по кортежу vs,ps,layout,prim,lsig,stateHash,
rtKey,dsKey), `setCache` → `unordered_map` по подписе, `resMapIdx` по (vs,ps).
Кэш fb чистится вместе с кэшем пайплайнов (swapchain, `Shutdown`) — поверхности
индексы не переиспользуют (записи не сжимаются), застоявшихся вьюх не появляется.

Замеры одного стенда (Wine, `PW_VK_BACKEND=1`, кадр 3000, карта `_.ADMPDSCR.xdb`):

| прогон | слой валидации | presents | пайплайны | попадания | present'ов/с |
|---|---|---|---|---|---|
| run107 (до) | вкл | 2099 | 17873/17873 | 963637/981701 (98,2%) | 12,5 |
| run109 (после) | вкл | 1799 | 191/191 | 826225/826531 (99,96%) | 14,9 |
| run110 (после) | выкл | 4199 | 190/190 (fb=23821) | 2037003/2037308 (99,985%) | 33,6 |

Число пайплайнов перестало расти с поверхностями и совпало с оценкой §1.4 (206
смысловых ключей). Регресс картинки: кадр 3000 до/после на одном стенде — средняя
|Δ| 1,6, пикселей |Δ|>32 1,48% (анимация), светимость 73,9 против 73,6. Валидация:
тот же набор VUID (07752, 00344, 04001, 08733, 07904), новых не появилось.
`VkSum` печатает ещё `fb=N`; fb растёт с поверхностями (23821 при 15245
поверхностях) — это пары (RT,DS), и это следующий резерв по памяти.

### Сделано (валидация: 07752 и 04001 — 2026-10-20, `21fe654f8`)

**07752** (`envMapSampler`, Set 2): подмена типа view (`dummyCubeTex`) работала
только для привязанной текстуры; в **пустой** слот ложился 2D-dummy, а шейдер
объявил `samplerCUBE`. Теперь выбор dummy идёт по `dim` из SPIR-V до проверки
привязки.

**04001** (`pBuffers[1..7] is VK_NULL_HANDLE`): инстансный поток привязывается к
слоту `kMaxVertexStreams` = 8 **после** цикла заполнения дыр, а NVRHI объявляет
массив слотов до `nvrhi::c_MaxVertexAttributes` = 16 (`nvrhi.h:76`) и вызывает
`bindVertexBuffers(0, maxVbIndex+1, …)`, промежуточные элементы массива — мусор со
стека (`vulkan-graphics.cpp:621-638`). Верхняя граница заполнения теперь учитывает
`instSlot` (при слоте за пределами наших 8 закрываются dummy'ом все 0..7). Это
доказательно не влияет на картинку: закрываются только слоты, на которые нет
атрибутов раскладки, — fetch их не читает. Проверка скоростей: run116 40 present'ов/с
против run110 33,6.

Замер (Wine, один стенд, карта `_.ADMPDSCR.xdb`, слой вкл):

| прогон | 07752 | 04001 | 00344 | 08733 | 07904 |
|---|---|---|---|---|---|
| run112 (до) | 20 | 20 | 20 | 12 | 4 |
| run115 (после) | **0** | **0** | 20 | 12 | 4 |

Грабли прогона: с включённым слоем клиент за 190 с доходит только до ~2400 present'ов
(12,6/с вместо 33-40/с), поэтому `PW_VK_SHOT=3000` при валидации **не срабатывает** —
для валидационного прогона ставить `PW_VK_SHOT<=2000` или поднимать `timeout`.
Счётчики VUID в логе обрезаются слоем (~20 на MessageID), так что «20» — это
«не ноль», а не величина. DXVK-только прогон (`run_dxvk_only.sh`, `PW_VK_BACKEND=0`,
слой вкл) даёт **ноль** VUID — все сообщения выше принадлежат нашему экземпляру.

### Гейт паритета: кадры двух бэкендов не сопоставимы по номеру (2026-10-20)

Замер на Wine-стенде (`run_dxvk_only.sh` против `run_vk_flip.sh`, та же карта
`_.ADMPDSCR.xdb`, тот же токен-флоу): **DXVK ~4 к/с** (frame 655 -> 899 за 60 с),
**Vulkan-бэкенд ~40 present'ов/с**. Номер кадра — не время игры: мир живёт по
сетевым тикам (`ServerNode: Lags detected: step:1766`), поэтому кадр 3000 у DXVK
и кадр 3000 у Vulkan — это разные секунды матча (и разные 7x по игровому времени).
Именно поэтому прежний «дифф кадра 3000 с эталоном `wine-dxvk-2026-10-15/`»
бессмысленен: средняя |Δ| 35 при том, что в эталоне кадр с подписью героя и
tаймером «До выхода солдат 0:47», а в снимке — другой ракурс/момент.

Сделано (механизм серии по часам + подбор пары):
- клиент, любой бэкенд: `setvar auto_screenshot_time <сек от первого кадра>` +
  `setvar auto_screenshot_step <период, сек>` (`Render/smartrenderer.cpp`,
  файлы `pw_auto_bb_t<сек>.png`);
- Vulkan-бэкенд: `PW_VK_SHOT_TIME=<сек>` + `PW_VK_SHOT_STEP=<сек>` — снимок внутри
  `NvrhiBackend::Present` (`vk_shot_t<сек>.bmp`), это надёжный readback; плюс
  `PW_VK_SHOT_STEP` в кадровом режиме (серия `vk_shot_<кадр>.bmp`);
- `tools/render_shot/shot_pair.py A B` — матрица средних |Δ| по всем парам серий,
  лучшая пара, диагональ (равные секунды), плюс по каждому снимку доля чёрных
  пикселей и светимость (отсекает чёрные readback'и).

Мешает DXVK-стороне сейчас: на этом стенде DXVK деградировал до ~4 к/с
(октябрьские эталоны сняты при ~33 к/с), и **первые ~5 минут все три readback-пути
клиента (bb / scene / crop) дают 100% чёрное** — снимки t60..t140 по 2367 байт,
786432/786432 чёрных пикселей, при этом лог клиента чист (ни DEVICELOST, ни ошибок).
Замер 2026-10-20 закрыл версию «это регрессия нашей ветки»: снимок окна с Xvfb
(`xvfb_shot.sh`, клиентский readback не участвует) на DXVK даёт 649 460 чёрных
пикселей = **86,08%** — и ровно столько же (до пикселя) на exe от 16.10,
`PW_Game.exe.bak-2026-10-16-pre134e`: значит мир не рисуется одинаково на старом и
новом билде. Причина — скорость: за 3 минуты при 4 к/с клиент не доходит до
`mapload: all map objects loaded` (в логе только `Connected to game service`,
`Queue lags … max:238`), а загрузка карты идёт по кадрам. DXVK-сторону гейта надо
либо греть длинным прогоном (12+ мин), либо снимать эталоны на tiny10 нативно.
Пока что единственные годные DXVK-кадры — эталоны `tools/render_shot/refs/`
от 2026-10-15/16.

**Поправка 2026-10-21: деградация DXVK — холодный шейдерный кэш, а не стена
скорости.** На стенде нет `~/.cache/dxvk` вообще (проверено `find`), то есть
каждый прогон компилирует все D3D9-шейдеры заново. С `DXVK_SHADER_CACHE_PATH=/tmp/dxvk-cache`
+ `DXVK_SHADER_CACHE=512` (строки есть в `d3d9.dll` 3.1.1) DXVK доходит до
`mapload: all map objects loaded` за **12 с** после `Connected to game service`,
а годные кадры есть с t20 по t480. Греть 12+ минут не нужно; нужно задать кэш.

### VUID 00344: механизм понятен, правка не тривиальна (2026-10-20)

`VUID-vkCmdDrawIndexed-imageLayout-00344` (сэмпл `ZMap`, layout ATTACHMENT_OPTIMAL).
Что проверено по коду: (a) depth-текстуры у нас создаются с SAMPLED-использованием
(`nvrhi::TextureDesc::isShaderResource` по умолчанию `true`, `nvrhi.h:508`);
(b) форк NVRHI уже умеет depth-as-SRV — в дескриптор кладётся
`eDepthStencilReadOnlyOptimal` (`vulkan-resource-bindings.cpp:407-415`), а
стейт-трекер просит `ShaderResource|DepthRead`, что в
`vulkan-constants.cpp:344-381` сворачивается именно в
`eDepthStencilReadOnlyOptimal`. Значит дескриптор правильный, а **фактический**
layout картинки — ATTACHMENT_OPTIMAL, потому что в момент сэмплирования эта же
texture стоит depth-аттачментом открытого рендер-пасса (у нас явных
`beginRenderPass`/`endRenderPass` нет — пасс ведёт NVRHI). Один и тот же образ
одновременно как attachment и как sampled в одном субпассе в Vulkan нелегален.
Что делать (по возрастанию цены): (1) разрывать пасс, когда сэмплируется текущий
DS/RT; (2) теневая копия глубины при конфликте и привязка копии — так делает сам
DXVK; (3) принять дрейф (на NVIDIA работает, но UB). Пока не сделано.


### Визуальные дефекты: что измерено (2026-10-20, `e7e80ef0c`)

A/B снят новым путём — `tools/render_shot/xvfb_shot.sh` (кадр окна прямо с Xvfb,
клиентский readback не участвует). Один и тот же Wine-стенд, одна карта
`Maps/Multiplayer/MOBA/_.ADMPDSCR.xdb`, один и тот же аккаунт:

| признак | DXVK | Vulkan-бэкенд |
|---|---|---|
| текст HUD (полоска «До выхода солдат 0:08», имя «Rekongstor», 6830/6830, цифры 1..9) | есть | **нет нигде** |
| миникарта (205x205) | заполнена | **чёрный круг** |
| белая плашка | нет (28 белых пикселей из 758 784) | **43 560 белых (5,54%)** |
| цилиндры справа | тёмно-синие машины | **ярко-фиолетовые** |

Белая плашка — крупнейший сплошной прямоугольник 330x132 в (29,443);
`shot_stats.py --defects` даёт те же числа в t40 и t60 одного прогона и в разных
прогонах, т.е. это не «undefined память», а детерминированный результат.

Что исключено измерением:
- **не блит**: частичных `StretchRect` в прогоне нет вообще (`VkBlit` пишет только
  частичные; полноэкранные PostFX-блисты `1024x768 -> 1024x768` и
  `1024x768 -> 512x384` — это цепочка пост-эффектов, к плашке отношения не имеет);
- **не заливка**: `ColorFill` с ненулевым цветом и `Clear` с ненулевым цветом в
  прогоне не вызывались (`VkFill`/`VkClear`); у `Clear` в seam нет прямоугольника
  (`renderbackend.h:289`), так что частичную белую область он дать не мог;
- **не замены несвязанных слотов**: `подстановок=4` за весь прогон; заполнение
  dummy-текстур нулями (сделано в этом же коммите — раньше image создавался без
  инициализации) картинку не изменило ни на пиксель;
- **не вывод треугольников с таким bbox**: в записи PWRD кадра (2499 выводов, все
  в back buffer 1024x768) нет ни одного вывода, чьи вершины (в пиксельных UI-coord
  с перевёрнутым y и в NDC) накрывали бы плашку. В её NDC-области
  (x −0,96..−0,28, y −0,52..−0,13) рисуют только `vs=4/ps=5` (prim 562..1810) и
  `vs=0/ps=1` (prim 4900), которые сэмплируют **GPU-only DXT5 128x128 / 128x512 /
  512x1024 / 1024x1024** и **205x205**; в окне записи в эти текстуры никто не
  пишет (все 2499 выводов идут в 1024x768).

Readback самих подозрительных поверхностей (`PW_VK_TEXGPU_SIZE`):
656x656 A8R8G8B8 GPU-only — 100% (0,0,0) alpha 0; 205x205 — плоский
(30,53,96) alpha 127 (у одного алиаса 86% пикселей ненулевые). То есть миникарта
не «не отрисована» — она залита константой (`VkFill: поверхность#119..#121
205x205 цвет 00000000` + 399 выводов в 205x205 в отдельной записи).

Рабочие версии на сегодня (обе не проверены):
1. UI/Flash-слой клиента наполняет DXT5-кэши (128x128 и др.) **до** окна записи или
   путём, который рекордер не видит; в Vulkan эти кэши остаются пустыми →
   плашка/текст/миникарта теряются целиком, потому что это один и тот же слой.
   Следующий шаг — запись с `PW_VK_RECORD_RT=128x128` (и 512x1024, 1024x1024) на
   всём прогоне: если выводов в них ноль, искать путь, которым клиент их наполняет
   (LockRect по сжатой текстуре, отдельный device, Legacy-путь).
2. Неверная раскладка каналов у A8/L8: seam маппит `bfA8` и `bfL8` оба в
   `R8_UNORM` (`nvrhibackend.cpp:370-371`), а семантика D3D9 — A8 -> (0,0,0,A),
   L8 -> (L,L,L,1); Vulkan для `R8_UNORM` даёт (R,0,0,1). В окне записи A8-текстур
   нет вовсе, L8 две (4x4 и 2048x2048) — версия про текст/плашку требует проверки
   (атлас шрифта в записи — DXT1/DXT5, не A8).

### Сделано (vertexAttributeRobustness — не включать; 2026-10-20, `d5792de5a`)

Сообщение валидации по `VUID-VkGraphicsPipelineCreateInfo-Input-07904` прямо
предлагает включить `VK_EXT_vertex_attribute_robustness` («это легально, если
включены vertexAttributeRobustness или maintenance9»). Расширение на этом хосте
есть (`vulkaninfo`: RTX 3060, `VK_EXT_vertex_attribute_robustness` rev.1; в
заголовках `VkPhysicalDeviceVertexAttributeRobustnessFeaturesEXT` есть), фича была
включена при создании устройства — и картинка сломалась целиком. Замер на том же
Wine-стенде, та же карта, та же секунда (`PW_VK_SHOT_TIME=40`):

| вариант | чёрных | уникальных цветов | светимость |
|---|---|---|---|
| без фичи (run125/run135) | 2,15% | 108 197 | 74,5 |
| с фичей (run133/run134) | 81,72% | 46 317 | 24,2 |

Механизм: вход VS, для которого в вершинном layout нет атрибута, с фичей
читается как нули -> позиции схлопываются в точку -> мир не виден целиком,
остаётся только UI. Без фичи драйвер читает мусор из несвязанного слота, и по
случайности геометрия остаётся на месте. Фича оставлена за env
`PW_VK_VARBUST=1` (по умолчанию выключена) — код запроса расширения и цепочки
`pNext` на месте, чтобы замер можно было повторить.

Вывод по 07904: чинить надо не разрешением неопределённого чтения, а
вершинным layout — на каждый вход SPIR-V (по Location) объявлять атрибут и
привязывать его к `dummyVb` (дыры по слотам уже закрываются `dummyVb` с
2026-10-20, `21fe654f8`; здесь дыра другого рода — в самом списке атрибутов).
Сообщений было 4 на прогон, это самый дешёвый из оставшихся VUID.

Проверка отката (тот же кадр t40, run135 против run125): средняя |delta| 2,0,
пикселей с |delta|>32 — 2,38% (уровень анимации), белая плашка 43 560 px без
изменений. Т.е. дыры вершинных слотов/индексов и `dummyVb` 4096 Б на картинку
не влияют.

### Сделано (шаг 13.5: SSBO набора 1 = кости BonesMatrices — 2026-10-20, `e78b8fc79`)

**Чем на самом деле является SSBO.** Новый `tools/render_replay/ssbo_scan.py`
(читает PWRD-запись через `parse_pwrd`, для каждого шейдерного SPIR-V гоняет
`spirv-dis` и ищет `BufferBlock`/`StorageBuffer`) по записи run122 даёт:
из 109 живых шейдеров SSBO объявляют 12, все — `MainPass_VSMain`, переменная одна:

```
%type_StructuredBuffer_mat4v3float = OpTypeStruct %_runtimearr_mat4v3float
%pwCBankVS   = OpVariable %_ptr_Uniform_type_pwCBankVS Uniform
%BonesMatrices = OpVariable %_ptr_Uniform_type_StructuredBuffer_mat4v3float Uniform
```

т.е. это скиннинг: `Src/Render/Shaders/_commonSkinning.hlsl:6` при
`PW_MODERN_ARRAYS` объявляет `StructuredBuffer<float4x3> BonesMatrices` (шаг 10,
§3.z п.2 — индекс берётся из атрибута BLENDINDICES, `int4 indices = 255 * idxs`,
неконстантный и расходящийся). Прежняя заглушка `dummySsbo` (4 КБ, содержимое
неопределено) означала, что скиндовые модели читают мусор.

**Мост c-банк -> SSBO.** Клиент кости через seam не передаёт:
`Src/Render/SkeletalMesh.cpp:113` вызывает
`SetVertexShaderConstantsMatrix43(BONES, currentBoneCount, matrixArray)`,
`BONES = c24`, `MAX_BONES = 70` (`GlobalMasks.h:88-89`), 3 регистра на кость
(= c24..c233). Поэтому seam: (1) ведёт `boneDirty/boneFirst/boneLast` —
объединённый грязный диапазон записей VS, пересекающих c24..c255 (считается в
`MarkConstDirty`); (2) на выводе перекладывает его из тени в `boneSsbo` со
смещением `(регистр - 24) * 16`; (3) набор 1 привязывает `boneSsbo` вместо
dummy. Буфер — 256 записей по 48 Б (12 КБ), обнуляется отдельным командным
списком при Init (индекс доходит до 255, за currentBoneCount — детерминированный
ноль, а не мусор). `setStructStride(48)` обязателен: NVRHI ассертит
`structStride != 0` для `StructuredBuffer_SRV`
(`Vendor/NVRHI/src/vulkan/vulkan-resource-bindings.cpp:536`).

Загрузка делается **независимо** от того, есть ли SSBO у текущего шейдера:
нематериальные шейдеры используют c24/c25 под свои константы (в дизассемблированном
`pwCBankVS` члены на offset 384/400 = c24/c25), и без постоянной перекладки между
двумя скиндовыми выводами в SSBO оседают чужие значения.

**Замер** (Wine-стенд, валидация ON, run138, `VkUbo` в `DumpCompact`):
кости — 504 401 загрузка, 55 611 216 Б (~110 Б на загрузку). Новых VUID нет:
тот же набор 00344/08733/07904; 00331 (dummy SSBO без STORAGE_BUFFER usage) не
возникает. Метрики дефектов кадра не изменились (белый прямоугольник 330x132 =
43 560 px в (29,443)-(359,575) — тот же).

**Чего эта правка НЕ доказывает.** Визуальный A/B по костям не снят: кадры двух
прогонов одного стенда не сопоставлены (открытый п.3 гейта паритета), поэтому
«картинка не изменилась» здесь — только отсутствие регрессии по метрикам дефектов,
а не подтверждение, что скиндовые модели стали правильными. Проверить это можно,
когда гейт даст парный кадр, либо точечно: запись PWRD + сравнение позиции вершин
до/после умножения на кости.

**Бытовая грабли:** `tools/render_shot/xvfb_shot.sh` не создавал каталог
назначения — `import` молча падал, снимков не было (в `--series` тоже). Исправлено.

### Сделано (шаг 13.5: UBO грузится грязным диапазоном — 2026-10-20, `bae7a241d`)

Путь констант шейдеров был наивным: любой `SetVertexShaderConstants`/`SetPixelShaderConstants`
ставил флаг грязности, а на выводе грузился весь канонический банк
(`SHADER_CONST_REGISTERS = 256` (`renderbackend.h:25`) -> 4096 Б на стадию).
Сделано два рычага: (1) запись сравнивается с тенью `memcmp`-ом и при совпадении
флаг не ставится; (2) в `NvrhiBackendImpl` появились `uboFirst/uboLast` —
объединённый диапазон записанных регистров с прошлой загрузки, и `writeBuffer`
вызывается с `destOffsetBytes = first*16` и размером `(last-first)*16`
(сигнатура `writeBuffer(IBuffer*, const void*, size_t, uint64_t destOffsetBytes)`,
`Vendor/NVRHI/include/nvrhi/nvrhi.h:3402`). Семантика как у
`ShaderConstStore::TakeDirty()` (`Src/Render/conststore.cpp:50`), но магазин
клиенту seam не виден, поэтому диапазон ведётся у нас.

Замер (`VkUbo` — новая строка в `NvrhiBackend::DumpCompact()`; Wine-стенд, та же
карта, `PW_VK_STATS=1`, run136, presents=7199 за 190 с = 37,9 к/с):

| | загрузок | байт |
|---|---|---|
| диапазон (новое) | 4 012 045 | 748 309 KiB (~191 Б/загрузка) |
| целый банк на том же числе загрузок | 4 012 045 | 16 048 180 KiB (4096 Б/загрузка) |

Экономия в 21,4 раза по байтам (~15,1 ГБ за прогон). Средний грязный диапазон —
~12 регистров, т.е. клиент на каждый draw переставляет несколько констант, а не
банк: это и есть ответ на вопрос шага 7 («~513k изменённых VS-регистров за окно»).

Картинка не поменялась: run137 против run135 (тот же кадр t40 того же стенда) —
средняя |Δ| 0,4, пикселей с |Δ|>32 — 0,07%, светимость 74,6 против 74,5.

Отдельная бытовая грабли на этой правке: сборка с ошибкой компиляции
(`error C3892` «cannot assign to a variable that is const») останавливает ninja,
но если копировать exe в `wine-client/Game/Bin` в одной строке со сборкой
(`build; cp`), копируется **прошлый** exe — и замер идёт по нему. До копирования
обязательно `grep -c "error C" /tmp/buildNNN.log` == 0.



### Найдено: перегенерация корпуса `.shd` меняет нумерацию SPIR-V binding'ов -> красный экран (2026-10-20)

Проверка «pile повторяем из репозитория» (`mkpile.py build --root Data --mask '*.shd' --mask '*.spv'`)
вскрывает регрессию: pile, собранный из репозиторного корпуса, даёт **полностью красный кадр**
(`mean = srgb(99.9%, 0%, 0%)`, 120–170 уникальных цветов), прежний pile со стенда — нормальный кадр.

Бисект по контейнерам (pile = старый состав + один новый `.shd`, прогон Wine-стенда 140 с,
снимок с Xvfb на ~70/100 с жизни клиента, метрика — средний цвет кадра и число уникальных цветов):

| состав pile | кадр |
|---|---|
| старый (16 `.shd` из pile 18.10, 44 sidecar) | нормальный (84k–394k цветов) |
| + новый `BasicMaterial.shd` | нормальный |
| + новые `UIBase/UIFlash/UIFont` | нормальный |
| **только новый `FullScreenMaterial.shd`** | **красный** |

Состав pile при этом совпадает по именам и размерам: 60 записей (16 `.shd` + 44 `.spv`),
sidecar-и побайтово идентичны репозиторным (44/44), v1-слой `FullScreenMaterial` побайтово
идентичен (46 076 Б). Различаются **14 байт** в SPIR-V-слое (первое расхождение — смещение
внутри модуля), и в новом варианте PS набор привязок сдвинут: было
`diffuseMap, bloomMap, DepthMap, warFogSampler` с binding 1,2,3,4 — стало
`diffuseMap, DepthMap, warFogSampler` с binding 0,1,2 (байты `01->00`, `02->01`, `03->02`).

Механизм: seam привязывает текстуры по **номеру binding из SPIR-V** (позиционно к D3D-слотам
сэмплера), а писатель корпуса нумерует binding'ы сам — при перегенерации нумерация
**не стабильна**. Сдвиг на один слот = PS `FullScreenMaterial` читает карту глубины как цвет:
R=1 (далёкая глубина), G=B=0 -> ровно красный экран.

Что из этого следует (частично снято — см. следующую секцию):
1. ~~Писатель обязан фиксировать нумерацию ресурсов~~ — **снято**: нумерация в
   SPIR-V и так канонична (binding = номер регистра из `.hlsl`, сверка
   `bind_check.py` по эталонному и по репозиторному контейнеру совпала),
   писатель детерминирован; ломал не порядок нумерации, а потерю аннотации
   регистра (следующая секция).
2. Seam обязан сверять ожидаемое число и тип ресурсов PS с фактическими в SPIR-V и при
   расхождении не рисовать (сейчас молча привязывает по позиции) — остаётся.
3. Стенд: ЗАКРЫТО 2026-10-20 — репозиторный pile ставится на стенд и даёт
   нормальный кадр; сборка — `mkpile.py build --root pw/branches/r1117/Data
   --mask '*.shd' --mask '*.spv' --only-v2` (флаг отсекает 20 v1-контейнеров,
   у которых нет SPIR-V-слоя).

Инструменты: `mkpile.py list --all` (без флага видно только 40 записей из 60 — замеры состава
врут), `mkpile.py extract <pile> <dir>` (побайтовое сравнение слоёв; `offset` в записи —
абсолютный от начала файла, не от данных).

### Закрыто: причина красного экрана — прослойка теряла макросы слотов; корпус снова воспроизводим побайтово (2026-10-20)

Причина не в «нестабильности DXC», а в конкретной строке прослойки:
`tools/spirv_probe/shim.py:sampler_sub` разбирал аннотацию регистра сырым текстом
(`re.match(r"\s*([tsub])\s*(\d+)\s*$", reg)`), а `register( WARFOGSAMPLERINDEX )` —
это макрос из `Src/Render/GlobalMasks.h` (`#define WARFOGSAMPLERINDEX s10`).
Макрос не раскрывался -> `keep` пустой -> аннотация не переносится в SM6 -> DXC сам
назначает binding 0, и он же у `DepthMap` (`register(s0)`). Коллизия
`(DescriptorSet 2, Binding 0)` = `warFog` читает R32F-карту глубины вместо 205x205
карты: R=1 (далёкая глубина), G=B=0 -> сплошной красный композит.

Правка: в `sampler_sub` перед разбором `reg = expand_reg(reg)` (тот же
`shim.slot_macros()`, что читает GlobalMasks.h; путь c-банков его уже использовал,
путь ресурсов — нет).

Контроль: новый `tools/shader_build/bind_check.py` — читает v2-контейнер
(заголовок `V2H\0` + таблица целей 12 Б + секции с абсолютными смещениями), для
каждого варианта печатает дескрипторы `(set, binding, имя, род)` и рядом номер
регистра из `.hlsl`; режим `--check` печатает только (а) `binding != номер регистра`
и (б) коллизии `(set, binding)` внутри варианта.

Замер до/после (все 16 корпусных контейнеров):

| корпус | `--check` |
|---|---|
| закоммиченный (срез B, перегенерирован 18.10) | `BasicFXMaterial` 4, `FullScreenMaterial` 4 — остальные 0 |
| пересобранный с правкой (`mkshd2.py --live live_indices-2026-10-15-union.json --cbank`) | 0 по всем 16 |

Воспроизводимость (гейт шага 14): 16 контейнеров, собранных из репозитория,
**побайтово совпали** с рабочим pile стенда (`zzzresult.pile.bak-2026-10-20-pre-corpus`,
60 записей, 263 819 Б) — `mkpile.py build --root <16 .shd + 44 .spv>` дал тот же файл.
Корпус в репозитории заменён на воспроизведённый (16 `.shd`).

Проверка end-to-end (2026-10-20, после правок `_commonUI.hlsl` и
`ParticleFXMaterial.hlsl`): pile собран прямо из дерева репозитория
(`mkpile.py build --root pw/branches/r1117/Data --mask '*.shd' --mask '*.spv'
--only-v2`) — 60 записей, 263 824 Б, md5 `7d645120803f2fe01c7d698e35bebb38`,
побайтово равный собранному из копий; задеплоен на Wine-стенд, прогон — кадр
нормальный (белых 43 560 = 5,77%, пурпурных 0, чёрных 1 030 = 0,14%, крупнейший
белый прямоугольник 330x132 в (29,443)-(359,575)), 0 ERROR/FATAL, 0 «комбинации
нет в цели». Красного экрана больше нет. Новый флаг `--only-v2` отсекает
v1-контейнеры (у них нет SPIR-V-слоя, в Vulkan-контуре они бесполезны): в
`Data/Shaders/UberShaders` 36 `.shd`, из них v2 — ровно 16 корпусных.

Грабли: `mkpile.py list` без `--all` показывает 40 записей из 60 (состав по нему
не мерить); `--root pw/branches/r1117/Data` без `--only-v2` тащит все 36 `.shd`
(80 записей) — с `--only-v2` остаются ровно 16 корпусных + 44 sidecar; `offset` в
записи pile — абсолютный от начала файла.

### Визуальные дефекты: одноканальные форматы починены; атлас шрифта на GPU есть — текст HUD рисуется не им (2026-10-20)

Найдена и закрыта реальная ошибка раскладки каналов: seam вёл `bfA8` и `bfL8`
(`renderflagsconverter.cpp:227,249` из `FORMAT_A8`/`D3DFMT_A8`/`FORMAT_L8`/`D3DFMT_L8`)
на `R8_UNORM` (`nvrhibackend.cpp:370-371`), а Vulkan при выборке даёт `(R,0,0,1)`.
D3D9 даёт `L8 -> (L,L,L,1)`, `A8 -> (0,0,0,A)`. Правка в `CreateTextureCommon`:
`td.setDefaultComponentMapping(...)` — L8/L16: R,R,R,One; A8: Zero,Zero,Zero,R.
Патча вендора не потребовалось: форк NVRHI уже умеет — `TextureDesc::setDefaultComponentMapping`
(`Vendor/NVRHI/include/nvrhi/nvrhi.h:540`) уходит в `VkImageViewCreateInfo::componentMapping`
(`src/vulkan/vulkan-texture.cpp:334`), SRV берёт его же (`src/vulkan/vulkan-resource-bindings.cpp:405`).

Проверка, что загрузка атласа в GPU работает (гипотеза «текста нет, потому что атлас пуст» — опровергнута):
`PW_VK_SHOT_TIME=60 PW_VK_TEXGPU=/tmp/tex_l8.txgp PW_VK_TEXGPU_SIZE=2048x2048 PW_VK_TEXGPU_ALL=1`
→ запись idx 18, 2048x2048, seamFormat 4 (L8), pitch 2048: ненулевых **29 989** из 4 194 304,
максимум **255**, гистограмма покрывает весь диапазон (32..255), bbox **(1,1)-(2042,85)** —
строка глифов, упакованных слева направо. То есть `LockTexture2D`/`UnlockTexture2D` + `writeTexture`
для L8-атласа (`UI/FontRender.cpp:43`, `FORMAT_L8`, 2048x2048) работают.

Грабли дампа: `TexGpuDump` по умолчанию читает обратно только `mipLevels >= 2`
(`nvrhibackend.cpp:7828`), а атлас шрифта и миникарта — mips=1, поэтому без
`PW_VK_TEXGPU_ALL=1` они в файл не попадают вообще (замер: 7 записей 2048x2048, все DXT1).

Что осталось по тексту HUD: разбор дался только через запись выводов — см. следующий
раздел. Коротко: текст рисуется, но читается как ряд тёмных квадратиков; 656x656 —
это эмблемы гильдий (`Render/SkeletalMesh.cpp` -> `Render::CreateARGBTextureFromFileInMemory`,
то есть PENDING §9), к тексту HUD отношения не имеют.

## Найдено: незакрытые входы VS; подмена (0,0,0,1) делает мир чёрным (2026-10-20)

Инструмент — `tools/render_replay/font_draws.py`: по PWRD находит выводы, которые
сэмплируют заданный атлас (фильтр по размеру и seamFormat текстуры), и считает их
экранный bbox по CPU-зеркалу вершинного буфера и раскладке.

Замер (запись `/tmp/rec158.pwrd`, `PW_VK_RECORD_AT=40 PW_VK_RECORD_MAX=600`, тот же
стенд и карта `_.ADMPDSCR.xdb`):

- выводов, сэмплирующих атлас шрифта (2048x2048, seamFormat 4 = `bfL8`), — 42 из 599;
  геометрия в пиксельных координатах (621..746, 391..430). Но VS пересчитывает их в NDC
  с масштабом 0,75 (`c23 = [2*0,75/1024, 2*0,75/768, 0,750183, 0,75]`), поэтому на экране
  текст лежит в (466..560, 293..322). Прежние кропы по пиксельным координатам показывали
  пустое место — вывод «текста нет» был сделан по неверному кропу; при верном кропе
  виден **ряд тёмных квадратиков** вместо букв, то есть вывод происходит,
  но цвет/альфа неверные.
- `gl_Position = (x*c23.x - 1, 1 - y*c23.y, c25.x, c26.x) x (c0..c3)`; по значениям из
  записи `c25 = 0`, `c26 = [1,0,0,0]` -> w = 1, трансформация верна (проверено
  пересчётом, не догадкой).
- альфа вывода в PS: `alpha = clamp(dot(...)) * in_var_TEXCOORD100.w` (Location 4), а VS
  пишет туда `OpConvertSToF(in_var_TEXCOORD100)` — альфа приходит **из вершинного
  атрибута**. Входы VS: POSITION@0, TEXCOORD0@1, TEXCOORD1@2 (все float2) и
  **TEXCOORD100@4 (int4)**; в раскладке клиента (layout 7) атрибута на Location 4 нет.

Эксперимент: незакрытые входы VS закрыты атрибутом-константой (0,0,0,1) — отдельный
16-байтный VB, `elementStride = 0` (Vulkan: шаг 0 = те же данные для всех вершин),
формат по классу входа (float/int/uint), слот — первый не занятый раскладкой; в D3D9
на вход без записи в декларации отдаётся ровно (0,0,0,1). Подмена сработала для
8 раскладок (например `TEXCOORD1@2 -> слот 1`).

A/B на стенде (один сервер, одна карта, снимок с Xvfb на 50 с, `PW_VK_DEFATTR` —
гейт в `BuildInputLayout`):

| подмена | чёрного в кадре | мир | текст |
|---|---|---|---|
| выключена | 0,14% | рисуется | тёмные квадратики |
| включена | 82,67% | **чёрный экран** | не появился |

Вывод (первоначальный) был неверным — незакрытых входов у раскладки текста нет.
`SemEq` (`nvrhibackend.cpp:3434`) уже покрывает соглашение DXC: семантика `COLOR<n>`
в SPIR-V живёт как `TEXCOORD(100+n)`, поэтому клиентский элемент `COLOR` лёг ровно
на Location 4 (`in.var.TEXCOORD100`). Запись layout 7 это подтверждает: 4 элемента
-> 5 атрибутов (POSITION@0, TEXCOORD0@1, TEXCOORD1@2, дубликат на дыру Location 3,
COLOR0@4). Эксперимент с подменой (0,0,0,1) поэтому и сломал мир: он добавлял
атрибуты там, где они уже были, и привязывал их на свободный слот поверх реальных
данных. Код подмены откачен, в дереве его нет; гейт `PW_VK_DEFATTR` вместе с ним.

## Найдено: почему нет текста HUD — D3DCOLOR подогнан под SINT и даёт −1 (2026-10-20)

Механизм (запись `/tmp/rec158.pwrd`, 42 вывода с атласом шрифта 2048x2048 bfL8):

- `_commonUI.hlsl:30` — `struct VS_INPUT { float2 Position : POSITION; float2 Texcoord
  : TEXCOORD0; float2 Texcoord2 : TEXCOORD1; int4 Vertexcolor : COLOR0; };` вход цвета
  объявлен **`int4`**, поэтому DXC делает из него `in.var.TEXCOORD100` типа
  `OpTypeInt 32 1` (signed) и в VS ставит `OpConvertSToF`.
- элемент клиента — `COLOR`, тип `VERTEXELEMENTTYPE_D3DCOLOR` (enum 4) ->
  `VtxFormat` даёт `BGRA8_UNORM`; `FormatIntClass` (`nvrhibackend.cpp:3546`, вызов
  `:3547`) по kind==1 (signed) меняет его на **`RGBA8_SINT`** (в записи — fmt 18).
- фактические байты цвета в геометрии — `(255,255,255,255)` во всех 372 вершинах
  42 выводов (замер по CPU-зеркалу VB, слот 0, offset 24, шаг 28). Прочитанные как
  SINT, это **(−1,−1,−1,−1)**.
- PS (`MainPass_PSMain`, Location 4 = `in_var_TEXCOORD100`): цвет пикселя =
  `FMix(pwCBankPS[0].rgb, Vertexcolor.rgb, tex.r)` — внутри глифа `tex.r = 1`, то есть
  цвет берётся ровно из `Vertexcolor` -> −1 -> кламп в 0; альфа умножается на
  `TEXCOORD100.w` = −1. Отсюда «ряд тёмных квадратиков» вместо букв: покрытие
  глифов рисуется, цвет и альфа — отрицательные.

Почему в D3D9/DXVK текст белый: байты не знаковые, и «сырое» чтение даёт 255
(у DXVK цвет получается >=1 и клампится в 1). То есть знаковый класс выбран неверно:
`FormatIntClass` выбирает класс по объявлению в шейдере, а надо — по знаку исходных
данных элемента (D3DCOLOR/UBYTE4/UBYTE4N/USHORT*N — беззнаковые).

Что это чинит и чего не делает: по Vulkan'у знаковый класс формата обязан совпасть
с базовым типом входа SPIR-V (`VUID-VkGraphicsPipelineCreateInfo-Input-08733` —
тот же класс ошибок), поэтому просто поставить `RGBA8_UINT` при `int4`-входе нельзя.
Варианты (по возрастанию цены):

1. корпус: собрать SPIR-V с `uint4 Vertexcolor` (те же `PW_MODERN_ARRAYS`-те, но
   знаковый вариант только для SPIR-V-сборки) -> seam поставит UINT, значение 255,
   цвет и альфа клампятся в 1, текст белый. Корпус после фикса `shim.py`
   (`5288b78de`) воспроизводится побайтово, так что пересборка предсказуема.
2. офлайн-правка SPIR-V в писателе (рядом с `localign.py`): `OpTypeInt 32 1` ->
   `32 0` и `OpConvertSToF` -> `OpConvertUToF` у вершинных входов. Исходники не
   трогает, но правка должна быть детерминированной и с проверкой.
3. правка данных на CPU (развернуть байты в знаковый формат) — не подходит,
   меняет геометрию.

Побочный вывод по 08733 (остаток открытого п.2): вторая семья ошибок — элемент
`UBYTE4` (-> `RGBA8_UINT`), а вход SPIR-V `float4`. Там знаковый класс тоже надо
выбирать по данным, но расхождение именно «целое <-> вещественное», оно требует
либо целого входа в шейдере, либо предконверсии потока; решается отдельно.

## Сделано: текст HUD — вход цвета в UI-шейдерах переведён на float (2026-10-20, `27b2662d9`)

Выбран не вариант `uint4`, а `float4`. Замер по записи `/tmp/rec158.pwrd`: элементы
`usage=COLOR` по выводам — `D3DCOLOR` 525, `FLOAT4` 18, `UBYTE4` для COLOR нет ни
одного. D3D9 при декларации `D3DCOLOR` отдаёт нормализованные 0..1, а `int4`-регистр
усекал их до 0/1 (белый 255 -> 1,0). С `uint4` seam поставил бы `RGBA8_UINT` и в
шейдере было бы 255: финальный кламп в RT дал бы белый, но промежуточные
`Input.Vertexcolor * gradient` (`UIBaseMaterial.hlsl:25`, `UIGlassMaterial.hlsl:16`,
`UIButtonMaterial.hlsl:19`) перенасыщались бы. `float4` + `BGRA8_UNORM` = ровно
D3D9-поведение, и `FormatIntClass` больше не вмешивается (kind = float).

Правка — одна строка: `Src/Render/Shaders/_commonUI.hlsl:35` `int4 Vertexcolor :
COLOR0;` -> `float4`. `AIGeometryMaterialType.hlsl:6` (`int4 Color : COLOR;`) не
правится — материала нет среди 16 корпусных
(`tools/shader_build/live_indices-2026-10-15-union.json`). D3D9-контур не затронут:
DXBC в контейнерах предкомпилирован, runtime-компиляция HLSL в клиенте только для
`deprecated_shaders/*` (`Render/debugrenderer.cpp:228-238`), офлайн-компилятор —
`Src/ShaderCompiler/`.

Пересборка корпуса: `python3 tools/shader_build/mkshd2.py --live
tools/shader_build/live_indices-2026-10-15-union.json --cbank --out /tmp/shd_uint`
-> 16 контейнеров, 0 пропусков; из 16 изменились только `UIBaseMaterial.shd`
(33 788 -> 33 724) и `UIFontMaterial.shd` (9 064 -> 9 000), остальные 14 побайтово
совпали с репозиторными. pile (16 `.shd` + 44 `.spv`) = 60 записей, 263 806 Б
(прежний 263 819). На стенде pile заменён на собранный из репозитория, прежний —
`wine-client/Game/Packs/zzzresult.pile.bak-2026-10-20-pre-corpus`.

Результат (Wine-стенд, снимок окна `import -window 0xc00001` на Xvfb `:97`):
**текст HUD появился** — на кропе 140x70+440+280 читается «Rekongstor» жёлтым, в
хотбаре видны номера 1..9,0 и «6830/6830», «700/700». Метрики кадра не изменились:
белых 43 560 (5,77%), пурпурных 0, чёрных 1 030 (0,14%), крупнейший белый
прямоугольник прежний — 330x132 в (29,443)-(359,575).

Валидация (тот же exe + pile, `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`):
00344 x20, 08733 x12, 07904 x4 — набор не изменился, значит источник 08733/07904 не
UI-раскладки (это `UBYTE4` в скиннинговых раскладках), открытое п.2 остаётся
отдельной задачей.


## Белая плашка 330x132 — рисует клиент, не бэкенд (2026-10-20, `54d329e22`)

Метрика `tools/render_shot/shot_stats.py --defects` (не зависит от момента сцены):
белых 43 560 px, крупнейший сплошной прямоугольник 330x132 в (29,443)-(359,575) —
одинаково в 6+ прогонах Vulkan.

Как искали (инструменты в том же коммите):
- `PW_VK_SKIPPS_FROM/TO/EXCEPT` — пропуск выводов по seam-индексу PS; бисект
  (`tools/bisect_ps.sh`) даёт ps=8, но пропуск только ps=8 делает кадр 100%
  чёрным: ps=8 — финальная композиция (единственный вывод в бэк-буферы), а не
  генератор плашки;
- `PW_VK_SKIPDRAW_FROM/TO` — пропуск по порядку вывода внутри кадра; бисект
  окна выводов -> [493,499], из его состава (`PW_VK_TRACEORD_FROM/TO`) плашку
  убирает только пропуск **ps=15** (белых 0, чёрных 0, кадр целый);
- ps=15 опознан по логу: сразу после `ShaderIndex: Shaders/UberShaders/UIFlashMaterial
  #0 (of 4096)` идут `CreateShader: MainPass_VSMain — OK seam-индекс 14` и
  `MainPass_PSMain — OK seam-индекс 15` -> **UIFlashMaterial, вариант #0**;
- у этого варианта в SPIR-V **нет ни одной текстуры** (подпись PS — только
  константный банк), трассировка `VkResMap` подтверждает: `layoutTex=0
  layoutSmp=0` — набор текстур не привязывается вовсе. Поэтому подмена текстур
  на dummy (`PW_VK_FORCETEX`) не давала ровно ничего;
- раскладка lay=5 = POSITION(FLOAT2) + COLOR(D3DCOLOR) + TEXCOORD(FLOAT1);
  константы вывода (`PW_VK_TRACECONST=15`): `color1` (c20) = (1,1,1,1),
  `color2` (c21) = (0,0,0,0) -> `color = color * color1 + color2` = цвет вершины.
  Номера регистров взяты из `GlobalMasks.h` (первая версия трассировки печатала
  c0/c1 и показала нули — ложный след);
- `tools/render_replay/panel_hunt.py` — поиск вывода, чей прямоугольник вершин
  покрывается плашкой: ни один вывод в записях PWRD её не рисует. Причина —
  UI-координаты в буфере умножаются на `c23`/`c26` (масштаб 0,75), а не лежат в
  NDC; после пересчёта плашка всё равно не находится (выводы в окне [493,499]
  в запись не попали — запись начиналась раньше/позже).

Исключено по ходу: трафарет/смешивание/запись глубины/тест глубины
(`PW_VK_STATE_MASK`, биты 1/2/4/8 — белых везде 43 560), X11-артефакт (на Xvfb
одно окно `0xc00001 1024x768+3+29`, дочерних нет), `bloomEnable 0`, форматы
A8/L8 (коммит `7de7395be` — красных поубавилось, плашку не тронуло), незагруженные
текстуры (дамп GPU-содержимого 64x64/256x1: белый RGB при **альфе 0**, т.е. при
SRCALPHA/INVSRCALPHA непрозрачного белого дать не могли).

Решающий замер: длинный DXVK-прогон того же стенда (`run_dxvk_only.sh`, снимки
Xvfb) — `dxvkL_t360.png` и `dxvkL_t480.png`: белых ровно **43 560**, крупнейший
прямоугольник **330x132 в (29,443)-(359,575)** — побайтово та же плашка. В
коротких DXVK-кадрах (t60..t180) её нет: другое состояние матча. Вывод: плашку
рисует клиент в обоих бэкендах, к Vulkan-бэкенду претензий нет.

**Вывод неверен, снят 2026-10-21** (см. секцию «A/B двух бэкендов»): DXVK
прогон 8 мин / 25 кадров t0..t480 того же стенда даёт 33…42 белых пикселя
(≤0,01%) в каждом кадре, включая пост-кик (t300..t480 с модалкой «Вы исключены
из сессии»). Плашка — дефект Vulkan-бэкенда; файлы `dxvkL_t*.png`, на которые
опирался вывод, в /tmp удалены и, судя по всему, были подписаны неверно.

Попутно пересмотрена миникарта: в Vulkan-кадре её содержимое есть (тёмное, с
геометрией), а кадр DXVK с 86% чёрного для сравнения невалиден. Претензия
переформулирована: «миникарта темнее эталона», а не «пустая».

Методический вывод для будущих разборов: сравнивать кадр с кадром можно только
после того, как подтверждено одинаковое состояние матча; при деградировавшем
DXVK (см. секцию про гейт паритета) единственный надёжный способ — метрики
дефектов (`--defects`) на обоих бэкендах, а не дифф.

## Сделано: 08733 — маска углов частиц стала целочисленным входом (2026-10-20, `11d05752c`)

Симптом: `VUID-VkGraphicsPipelineCreateInfo-Input-08733` x12 за прогон —
`pVertexAttributeDescriptions[4].format (VK_FORMAT_R8G8B8A8_UINT) at Location 4
does not match vertex shader input type (vec4 of float32)`.

Разбор по записям PWRD (rec122/rec158/rec172 — три прогона): во всём наборе
раскладок ровно ОДИН целый формат — `UBYTE4 TEXCOORD2` (в rec172 — раскладка 7,
126 выводов из 2999). Раскладка инстансная: поток инстансов несёт
POSITION/NORMAL/TEXCOORD0/TEXCOORD1/COLOR0 (шаг 76), per-vertex поток — только
4 байта. Это `ParticleFXMaterial`: `float4 CornerMask : TEXCOORD2`,
`UnpackCorner(value, mask) = (value*mask).xy + (value*mask).zw`, то есть маска
0/1 по компонентам. Байты из вершинных буферов записи — (0,1,1,0), (1,1,0,0),
(1,0,0,1), (0,0,1,1): значения ровно 0 и 1.

В D3D9 декларация UBYTE4 = «несжатые целые», IA приводит их к float 0.0/1.0.
В Vulkan числовой класс входа обязан совпадать с форматом атрибута. Правка в
исходнике: под `#if PW_MODERN_ARRAYS` (его определяет `mkshd2.py:82
MODERN_DEFINE`, DX9-слой собирается без него) вход объявлен `uint4`, а
приведение к float явное в четырёх местах (`float4(Input.CornerMask)`). Для
DX9-слоя семантика не меняется.

Пересборка корпуса (`mkshd2.py --live live_indices-2026-10-15-union.json --cbank`):
из 16 контейнеров изменился ровно один — `ParticleFXMaterial.shd` 24552 ->
24600 Б; в SPIR-V VS появился `%v4uint = OpTypeVector %uint 4` (в прежней сборке
uint-вектора не было). Обновлённый контейнер в `Data/Shaders/UberShaders/`.

Замер после (Wine-стенд, pile 60 записей 263 824 Б): 08733 x12 -> **0**;
кадр (снимок Xvfb на 65 с) — белых 43 560 (5,77%), пурпурных 0, чёрных 1 030
(0,14%), крупнейший белый прямоугольник 330x132 в (29,443)-(359,575) — те же
числа, что и до правки; 0 ERROR/FATAL, `mapload: all map objects loaded`.
Попутно: «пурпурные цилиндры» из списка дефектов — объекты сцены (есть и в
прежнем pile, и в эталонном DXVK-кадре), пурпурных px по мерке 0.

## 07904: три подхода, все трое ломают кадр (2026-10-20, `1e05e6ff3`)

Осталось: `VUID-VkGraphicsPipelineCreateInfo-Input-07904` x4 — «у VS есть вход
на Location 2, а атрибута нет». Это реальный клиентский пропуск: по трассировке
`VkIlMap` (кап поднят 6 -> 40) пар layout/vs за прогон 40, проблемная пара —
`layout 5 / vs 10` = `UIFlashMaterial #1024`: VS ждёт `POSITION0@0
TEXCOORD100@5 TEXCOORD1@2`, клиент объявляет только POSITION и COLOR0. Проверка
по SPIR-V контейнера: все входы `in.var.*` читаются (OpLoad x1), «мёртвых»
входов нет, значит значение нужно настоящее (в D3D9 — (0,0,0,0)).

Что мерилось:
1. `VK_EXT_vertex_attribute_robustness` (запрос расширения + фичи при создании
   устройства) — мир чёрный: 81,7% чёрного вместо 2,15%. Оставлено за env
   `PW_VK_VARBUST=1`.
2. Свой VB-слот с константой (0,0,0,1) на каждый незакрытый вход — чёрного
   0,14% -> 82,67%, текст при этом не появился (секция «Незакрытые входы VS»).
3. Пэддинг хвоста location дубликатом последнего атрибута (NVRHI нумерует
   location индексом в массиве — `vulkan-shader.cpp:186`, а seam до этого
   закрывал только дырки СРЕДИ атрибутов): VUID исчезает (x4 -> 0), но мир
   режется чёрным клином по диагонали — чёрного 40,64% вместо 0,14%. Откачено,
   в коде остался только комментарий с замером.

Не годится и `VK_KHR_maintenance9` (на RTX 3060 присутствует,
`VkPhysicalDeviceMaintenance9PropertiesKHR.defaultVertexAttributeValue =
ZERO_ZERO_ZERO_ONE_KHR`): он задаёт для отсутствующего атрибута (0,0,0,1), то
есть повторит исход 2. Правильный кандидат — обнулённый буфер со значением
(0,0,0,0) (как в D3D9) и привязка строго на конкретный location, с проверкой на
кадре; либо правка клиентской вершинной декларации под ожидаемый вход.

**Продолжение 2026-10-21 (пп. 4–6 замерены, кандидат отклонён, вопрос закрыт как
безвредный): секция «07904: закрытие хвоста константой убивает мир» ниже.**

## Сделано: 00344 — глубина как текстура, attachment снимается (2026-10-20, `80909c1cb`)

Симптом: `VUID-vkCmdDrawIndexed-imageLayout-00344` x20 за прогон — «Cannot use
VkImage in layout `VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL` (sampled
image descriptor [Set 2, Binding 0, variable "ZMap"]) that doesn't match the
previous known layout `VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL`».
Прежняя версия плана предполагала «не хватает перехода layout»; трассировка
показала другое.

Механизм (трассировка `Draw: чтение глубины` в `FindOrCreateTexSet`):
`binding 0 текстура#0 1024x768 curDs#9 тот же=1 z=0 zW=1 st=0` — PostFX читает
как текстуру ровно тот же образ, который в этом же выводе приложен depth
attachment'ом текущего framebuffer'а. Vulkan такой конфигурации не разрешает
(sampled-вьюха изображения, являющегося depth attachment текущего subpass'а),
D3D9 — разрешает. Никакой переход не помогает: пока attachment в subpass'е,
layout картинки — attachment.

Две правки в `Src/Render/nvrhibackend.cpp`:
1. `FillPipelineState`: `ds.depthWriteEnable = (st.zEnable && st.zWriteEnable)`.
   В D3D9 `D3DRS_ZWRITEENABLE` действует только при включённом `D3DRS_ZENABLE`;
   в Vulkan при `depthTestEnable = VK_FALSE` сравнение считается пройденным, а
   запись определяется `depthWriteEnable` — прямая передача писала в Z там, где
   D3D9 не пишет. PostFX-проходы ровно такие: `z=0 zW=1`.
2. `FindOrCreatePipeline`: при выключенном тесте глубины и выключенном трафарете
   depth attachment в framebuffer не кладётся (`effDs = kNoRes`, входит и в ключ
   пайплайна, и в `FindOrCreateFb`). Запись глубины в условие не входит по
   причине из п.1. A/B-гейт — env `PW_VK_DS_KEEP=1`.

Замер (Wine-стенд, карта `_.ADMPDSCR.xdb`, RTX 3060): 00344 x20 -> **0**, новых
VUID нет (остаток — 07904 x4); кадр без валидации (снимок Xvfb на 65 с) — белых
43 560 (5,77%), пурпурных 0, чёрных 1 030 (0,14%), крупнейший белый прямоугольник
330x132 в (29,443)-(359,575) — те же числа, что и до правки; средний |Δ| кадров
до/после 0,4 (шум анимации), светимость 79,0 -> 79,1. То есть правка убирает
нарушение, ничего в картинке не меняя, — но она же означает, что раньше в эти
проходы шла запись глубины, которой в D3D9 быть не могло: возможный источник
расхождений глубины (Z-артефакты поверх PostFX) надо перепроверять на DXVK-парах.

### Сделано: регресс нативной сборки на tiny10 (2026-10-20)

Гейт из п.5 очереди — нативный x64-контур (D3D9) после 23 коммитов ветки
(`e5a4b174e` … `fb9ece507`) не должен был измениться; проверялось на ВМ tiny10.

- **ВМ не поднималась** («A disk read error occurred» после «Press any key to boot
  from CD or DVD…..»). Диагностика (qcow2/ISO/qemu/SeaBIOS целы, `ntfsfix -n` чист,
  Alpine в том же конфиге грузится) и то, чем ВМ поднялась — в скилле `win-vm`,
  раздел «A disk read error occurred». Образ и диск НЕ чинили, данных не потеряно.
- **Синк дерева** (git на ВМ нет): база — последний синк `157586f73` (10-15 04:05,
  совпало с mtime файлов ВМ 10/15 05:19); `git diff --name-status $BASE..HEAD --
  pw/branches/r1117/{Src,Tools,Data/Shaders} pw/branches/r1117/CMakeLists.txt
  build_client_x64_win.cmd pw/branches/r1117/Vendor` = 85 A / 4 D / 88 M (177).
  Архив — **С префиксом** `pw/branches/r1117/` (правило в скилле `pw-client`
  исправлено: прежняя редакция «без префикса» была неверна для текущего дерева —
  `build_client_x64_win.cmd:130` берёт `%REPO%pw\branches\r1117`, `REPO` = каталог
  `.cmd`; рядом лежит мусор-дерево `C:\pwbuild\pwc\Src\Render` из 26 файлов — след
  прежнего синка без префикса). `python3 -m zipfile -c` пути **теряет** (кладёт
  базовые имена) — собирать через `zipfile.ZipFile(...).write(p, p)`; 174 записи,
  812 КБ. Удалённые в репо (`Src/Render/mesh.{cpp,h}`, `Src/Scene/ModelLoader.{cpp,h}`)
  сняты руками.
- **Сборка**: `schtasks /run /tn PwNativeBuild` → `RC=0` за ~7 мин, артефакт
  `C:\pwbuild\pwc\build\PW_Game.exe` 15 995 392 Б (против 15 719 936 Б у exe от
  10/16). `Tools/ClientCMake/check_exe.py` на скачанном exe: x64, PE32+,
  subsystem 2 (WINDOWS), Characteristics 0x0022 (бит LAA на месте), RT_MANIFEST id 1,
  FileVersion 2.16.1.0 — **ИТОГ: OK**.
- **E2E**: бэкап `PW_Game.exe.bak-2026-10-20-pre-nvrhi`, новый exe на месте;
  токен `cli/session.js --ips 0a4e0001` (bridge-гейтвей хоста), URL в
  `C:\tools\pw_url.txt`, `schtasks /run /tn PwE2E64`. На +85 с (снимок VNC
  `vm7/vncshot.py 127.0.0.1 5918`) — бой: мир, текст HUD («До выхода солдат 0:10»,
  «Rekongstor», 6830/6830, номера хотбара), **заполненная миникарта**, портрет.
  Метрика `shot_stats.py --defects`: белых 535 (0,07%), пурпурных 0, чёрных 1081
  (0,14%); белой плашки 330x132 в этом кадре нет (появляется позже — совпадает с
  поведением DXVK в длинном прогоне).
- **Дифф кадра 3000** (`autoexec.cfg`: `auto_screenshot 3000`, `shader_target 2`,
  `verify_state_model 1`; файл `C:\pw64\Game\Bin\pw_auto_bb_3000.png`) против
  эталона `tools/render_shot/refs/stage3-shd2-2026-10-15/pw_auto_bb_3000.png`,
  метрика `shot_stats.py` (максимум по каналам):

  | сравнение | средняя |Δ| | >8 | >32 | >64 | >96 | светимость |
  |---|---|---|---|---|---|---|
  | новый exe ↔ эталон 10/15 (прогон 1) | 3,0 | 6,20% | 2,72% | **1,43%** | 0,79% | 80,1 / 80,3 |
  | новый exe ↔ эталон 10/15 (прогон 2) | 4,3 | 7,33% | 4,13% | 2,51% | 1,38% | 80,1 / 80,3 |
  | **тот же новый exe ↔ сам себя (прогон 1↔2)** | 3,9 | 6,88% | 4,08% | **2,23%** | 1,12% | 80,1 / 80,1 |

  Уникальных цветов 241 711 (прогон 1) / 241 037 (прогон 2) / 241 658 (эталон),
  чёрных 0,15% / 0,16%. Вывод: расхождение нового exe против эталона **не больше
  разброса между двумя прогонами одного и того же exe** ⇒ регресса D3D9-контура
  нет. Правка п.1 (запись глубины в PostFX) на нативном кадре не отразилась.
- **Пересмотреть полосу шума**: записанная ранее полоса «~11% пикселей различаются,
  грубых >64 — 0,13–0,33%» сегодняшним E2E не воспроизводится (бой не
  детерминирован: на кадр 3000 приходит разное состояние матча) — сегодня
  два прогона одного exe дают 2,23% грубых. Дальше сравнивать только с контролем
  «тот же exe, другой прогон», а не с абсолютной полосой.
- **Побочный результат**: нативный кадр — первый корректный эталон по миникарте
  (заполнена) ⇒ претензия «миникарта тёмная» подтверждена как Vulkan-сторона.

### Сделано: вьюпорт ограничивается размером цели — композит тумана и миникарта (2026-10-20, `8108477cb`)

**Симптом.** Миникарта в Vulkan-контуре — тёмный круг без местности и иконок,
при том что nature-карты (538x402, сэмплеры 0/1/2 по `Minimap.cpp:1315-1317`)
загружены и наполнены (mean (52,92,23) / (51,65,138) / (79,66,42)), а путь
загрузки честен (GPU-содержимое побайтово равно зеркалу LockRect).

**Развёртка.** Миникарта затемняется множителем тумана: `NatureMap.psh`
(`Src/Render/OnlineShaders/NatureMap.psh`) — `FoWScale = saturate(1 -
fogOfWar.a * alphaScale)`. Измерив mean RT (4,9,2) при nature mean ~50..80,
получаем FoWScale ~0,1; при fog-альфе 127/255 = 0,498 это alphaScale ~1,8.
Виноват не blendMask (656x656 = `NatureMapVisual::GetTerrainMask()`, он и
правда ни разу не загружается и на GPU undefined — но на яркость не влияет).

**Корень.** Клиент рисует экранные квады, не меняя вьюпорт: по записи
`/tmp/rec236.pwrd` (700 выводов) у ВСЕХ vp=(0,0,1024x768) и
scissor=(0,0)-(1024,768) — включая 97 выводов в 512x384, 16 в 200x201
(миникарта), 3 в 205x205 (композит тумана), 3 в 256x192. Умный рендерер пуш
не делает: `SmartRenderer::FixViewport` (`Src/Render/smartrenderer.cpp:1074`)
выходит при `!curDepthSurface`, а `BindRenderTarget(tex)` ставит глубину NULL;
`SetMainViewport` (:1043) только кэширует, пуш — в `ResetMainViewport` (:1067)
и `BindRenderTargetDefault` (:957). D3D9/DXVK применьшают вьюпорт к размеру
cели, Vulkan — нет: квад, спроецированный на 1024x768, в цель 205x205 отдаёт
верхний левый угол (UV 0..0,20 x 0..0,27), а там туман сплошной.

**Замер GPU-содержимого** (Wine-стенд, `PW_VK_TEXGPU_SIZE=205x205,200x201
ALL=1`, кадр t=170; до = run238, после = run239/240):

| объект | до | после |
|---|---|---|
| `m_texStep`/`m_texBlended` (205x205) | 100% сплошного тумана, uniq 1, ясных 0,0% | 87,1% тумана + 12,9% ясных, uniq 60 |
| `m_texSource` (205x205, CPU-зеркало) | 83,1% тумана + 16,9% ясных, связная ясная область 34x34 | то же |
| RT миникарты 200x201 | ненулевых 47,3%, uniq 79, mean (4,9,2) | ненулевых 84,5%, uniq 420, mean (20,33,10) |

Box blur 3x3 (`Blur9::Apply`, `Src/Render/Blur.cpp:24`) съесть связную ясную
область 34x34 не мог — значит композит читал не тот участок источника.

**Что изменилось в кадре.** Визуально миникарта из тёмного пятна стала картой:
шестиугольная сетка, зелёные полосы местности, красно-зелёные маркеры
отрядов, прямоугольник камеры — в тех же местах, что в нативном кадре tiny10.
Метрики дефектов кадра не изменились (белых 43 560 = 5,77%, пурпурных 0,
чёрных 946 = 0,13%). PostFX-буферы при климпе и без него практически совпали
(512x384 mean (80,132,164) против (94,159,192) — разный момент матча; 256x192
в обоих прогонах 100% чёрные), т.е. эффект именно по офскрин-проходам
клиента, а не по PostFX-цепочке. Валидация: остался только 07904 x4, новых
VUID нет.

**Гейт.** `PW_VK_VPCLAMP=0` выключает ограничение (для A/B). Попутно
добавлены фильтры дампов: `PW_VK_TEXDUMP_SIZE="205x205,656x656"` (несколько
перезаливок одной текстуры вместо дедуплицированной первой) и
`PW_VK_TEXGPU_MAXW=<N>` (не дампить атлас шрифта 2048x2048, 16 МБ на запись).

**Не закрыто.** (а) миникарта всё ещё темнее нативной — кроп 220x220+790+530:
mean (39,46,35) против (70,70,55) у tiny10, тёмных 45,7% против 18,9%; нет
голубой подсветки реки/местности; (б) нет декоративных иконок миникарты
(`UI/Textures_default/AdvMap/minimap_icons.dds`, загрузка `Minimap.cpp:611`) —
в Vulkan-кадре только красно-зелёные маркеры. Следующий шаг по (б): снять
выводы, сэмплирующие `minimap_icons.dds` (DDS-путь `Load2DTextureFromFile`), и
их GPU-содержимое.

### Сделано: мосты c-банк → SSBO по таблице имён (2026-10-20, `d6856956c`)

**Что было.** Набор 1 (SSBO) собирался один на все шейдеры: в каждый слот
набора клался `d.boneSsbo`, то есть `hexParams` у `NatureMapMaterial` читал
байты костей. Имена SSBO из SPIR-V нигде не участвовали.

**Как нашли.** `NatureMap.psh` берёт `blendCoeff` из маски природы, а та
рисуется материалом `NatureMapMaterial`, у которого в SPIR-V-варианте
(`PW_MODERN_ARRAYS`) массив `hexParams` объявлен `StructuredBuffer<float4>`, а
в DX9-варианте — `float4 hexParams[40] : register(VSHADER_LOCALCONST2)`
(`Src/Render/Shaders/NatureMapMaterial.hlsl:22/24`). Поставщик —
`Src/Terrain/NatureMapVisual.cpp:375`
`SetVertexShaderConstants(VSHADER_LOCALCONST2, prim.hexCount * 2,
prim.hexConstants)` прямо перед `DrawIndexedPrimitive`, то есть хост честно
пишет массив в c-банк и менять его не нужно.

**Реализация** (`Src/Render/nvrhibackend.cpp`):
- `struct SsboBridgeDef { const char* name; unsigned regBase, regCount, stride,
  byteSize; }` и таблица `kSsboBridges` — `{ "BonesMatrices", 24, 232, 48,
  256*48 }`, `{ "hexParams", 22, 40, 16, 40*16 }`; по строке таблицы — свой
  `nvrhi::BufferHandle` (`setStructStride(def.stride)`, `canHaveUAVs`,
  состояние UnorderedAccess) и свой грязный диапазон;
- `SpvResScan` читает `OpName` (opcode 5) и декорацию `ArrayStride` (7),
  `ResItem` получил `name[20]` и `stride`; привязка набора 1 выбирает буфер по
  `strcmp(it.name, kSsboBridges[b].name)`, **неизвестное имя получает dummy
  (нули)** вместо костей — раньше все слоты получали кости;
- `MarkConstDirty` и загрузка — цикл по таблице с пересечением
  `[startRegister, start+count)` и `[regBase, regBase+regCount)`, кламп по
  `byteSize`;
- заметки «SSBO без моста — подставлен dummy» (кап 8) и «SSBO: ArrayStride не
  совпал с таблицей» (кап 4).

**Грабли (повторяемые).** Размер буфера нельзя брать из `regCount*16`: у
`BonesMatrices` это 232*16 = 3712 Б = 77 записей по 48 Б, а индекс кости в
шейдере доходит до 255 → выход за буфер → клиент завис в `vkQueuePresentKHR`
(`VkHang: к бэкенду нет обращений 180 с`, лог продолжал грузить карту).
Поэтому в таблице отдельное поле `byteSize` = 256*48 Б, как было у прежнего
`boneSsbo`. Соответствие байт↔запись: кость k в c-банке на `c24+3k` →
`(3k)*16 = k*48` ✓.

**Замер** (Wine-стенд, карта `_.ADMPDSCR.xdb`, RTX 3060):
- живые имена SSBO (снимок по SPIR-V из записей PWRD — `sh[id]['spv']` →
  `spirv-dis`): `BonesMatrices`, `hexParams`, `instanceData`, `data`; в PS SSBO
  нет ни одного. Заметки прогонa: `SSBO без моста — подставлен dummy — data`,
  `— instanceData`; заметок про ArrayStride нет ⇒ stride таблицы совпал;
- счётчики: `VkUbo: … SSBO-мосты: загрузок=691232 байт=61077712`;
- цель миникарты 200x201: уникальных цветов 420 → **5066**, mean (22,35,11),
  альфа 255 у 84,5%; маска природы 656x656 — без изменений (one-hot
  (0,255,0) 75,4% / (255,0,0) 11,5% / (0,0,255) 11,3%, альфа 0) ⇒ кандидат
  «blendMask не заполнена» отвергнут, прежнее измерение было ложным;
- кадр в целом без регрессии: белых 43 582 (5,78%) против 43 560, пурпурных 0,
  чёрных 944 (0,13%) против 1030, тот же прямоугольник 330x132 в
  (29,443)-(359,575); 0 ERROR/FATAL;
- валидация: остаток только `VUID-VkGraphicsPipelineCreateInfo-Input-07904` ×4,
  новых нарушений мосты не добавили.

**Полезные трассировки того же коммита.** `VkRecTex: rec#N = seam#M …` — id
текстур в PWRD сквозные по записи (`RecTextureId` присваивает
`id = d.recTexture.size()`), а `TexGpuDump` печатает индекс шва: без этой
строки сопоставить вывод из записи с дампом GPU нельзя. `VkRtSize` — инвентарь
целей вывода (показал, что клиент ставит цель 656x656, то есть рендер маски
приrody идёт). `VkSurf` — соответствие поверхность ↔ текстура.

### A/B двух бэкендов на Wine-стенде: плашка — дефект Vulkan (2026-10-21)

Прогон: `/tmp/ab26/run_ab.sh` (одна сессия на сторону, `PW_VK_BACKEND=1` против
`WINEDLLOVERRIDES="d3d9=n,b" PW_VK_BACKEND=0`, та же карта
`Maps/Multiplayer/MOBA/_.ADMPDSCR.xdb`, тот же аккаунт, снимки только
`xvfb_shot.sh --series`): DXVK 25 кадров t0..t480 (шаг 20 с), Vulkan 15 кадров
t0..t280. Артефакты сохранены в `tools/render_ab-2026-10-21/` (вне репо): `{dxvk,vk}_shot_t*.png`
(по 4 representative), `ab_full.png`, `ab_regions.png`, `wide_ab.png`,
`stripes_ab.png`, `run_ab.sh`, `region_ab.py`, логи обеих сторон. Рабочий каталог
прогона — `/tmp/ab26`.

Починка инструмента: `tools/render_shot/xvfb_shot.sh:35` в режиме `--series`
обращался к `$out_t` (`"$out_t$((i*step)).png"` вместо `"${out}_t…"`) — при
`set -u` «out_t: не заданы границы переменной», серия умирала на первом же снимке.
Все прежние серии снимались вручную, отсюда и путаница с подписями `dxvkL_t*`.

Метрика дефектов (`shot_stats.py --defects`), не требует сопоставленного кадра:

| сторона | белых пикселей | крупнейший белый прямоугольник |
|---|---|---|
| Vulkan, t20…t280 (15 кадров) | 43 127…46 303 (5,72…6,14%) — во **всех** кадрах, с первого рисующего | 301x132 в (29,443)-(330,575) |
| DXVK, t0…t480 (25 кадров, включая пост-кик) | 33…42 (≤0,01%) | 7x1 |

Плашка появляется в Vulkan с первого кадра (t20 — до модалки кика) и не появляется
в DXVK ни в одном кадре, в том числе в состоянии «Вы исключены из сессии»
(t300…t480). Значит вывод `54d329e22` («плашку рисует клиент в обоих бэкендах»)
ошибочен: плашка — дефект Vulkan-бэкенда, и это самый крупный дефект кадра
(5,8% площади).

Подбор пары (`shot_pair.py`): лучшая пара dxvk t80 ↔ vk t140 при |Δ|=26,0,
диагональ (равные секунды) 26…28, максимум матрицы 100,6 (t0 — чёрный readback).
Матрица почти плоская → разницу даёт не момент матча, а систематические расхождения.

Метрики по регионам (среднее по 15 парам, A=DXVK, B=Vulkan; `region_ab.py`;
регионы не зависят от зума камеры):

| регион | meanD A/B | lum A/B | uniq A/B | белых A/B % | \|Δ\| |
|---|---|---|---|---|---|
| plate_area (29,443)-(360,575) | 75,8 / 233,3 | 70,9 / 233,3 | 4910 / 165 | 0,00 / 90,57 | **157,6** |
| minimap (810,555)-(1021,739) | 53,6 / 29,9 | 55,7 / 32,7 | 3470 / 2066 | 0,07 / 0,00 | **23,8** |
| hero_portrait (0,610)-(180,739) | 93,2 / 86,0 | 93,5 / 86,8 | 3149 / 2901 | 0 / 0 | 7,2 |
| center_world (300,200)-(700,450) | 72,9 / 68,6 | 72,0 / 66,9 | 4542 / 4630 | 0 / 0,52 | 4,3 |
| right_units (780,250)-(980,500) | 78,6 / 78,8 | 74,0 / 72,6 | 5435 / 5513 | 0 / 0 | 4,1 |
| ability_bar (200,695)-(800,739) | 79,2 / 75,6 | 83,0 / 80,3 | 2797 / 2397 | 0,01 / 0,00 | 3,6 |
| hud_top (350,80)-(650,200) | 77,4 / 76,4 | 76,1 / 75,4 | 2000 / 2543 | 0,04 / 0,00 | 1,8 |

Глазами те же регионы (`ab_regions.png`): в DXVK на миникарте — шестиугольная
сетка, рельеф, синяя река, портреты и красно-зелёные маркеры; в Vulkan — только
тёмные зелёные массы местности, ни одной иконки. В ability_bar в Vulkan слоты
пустые (нет иконок способностей), цифры и полоски на месте. В right_units у DXVK
тёмные бочки с фиолетовыми крышками, у Vulkan — сплошные ярко-фиолетовые
цилиндры (средняя светимость при этом почти равна — метрика цвета не ловит,
нужна метрика оттенка).

Итог по паритету: **мир и текст HUD почти паритетны** (|Δ| 4,3 и 1,8), вся
расходящаяся площадь — это UI/Flash-текстуры (плашка, иконки миникарты, иконки
способностей) плюс оттенок юнитов. Это согласуется с гипотезой, что UI-слой
загружает текстуры в обход того пути, который Vulkan-бэкенд видит.

**Поправка (та же дата, третья серия):** этот вывод держится на несведённых по
игровым часам кадрах. На сведённых парах средняя по миру/HUD сходится
(\|D\| 1,3…4,0 при шуме 0,0…0,1), но пиксельно кадры различаются (gross
12,7…49,6 % при шуме ≤1,7 %) — паритет по средней не равен паритету по
пикселям. Все числа таблицы выше (в том числе minimap 23,8) — из несведённых
кадров, для сравнения бэкендов их не использовать.

Новый дефект, ранее в плане не описан: в Vulkan-кадре через весь экран идут
тонкие полупрозрачные жёлтоватые горизонтальные линии (видно на `ab_full.png`,
в кропе `wide_ab.png` — верх Vulkan, низ DXVK: у Vulkan две линии через весь
пол, у DXVK их нет). Детектор «среднее по строке +6 к скользящему среднему» их
не ловит: 7 групп у Vulkan против 4 у DXVK, лишние группы — края самой плашки
(y 443…448, 570…574). Счёт по «жёлтым» пикселям (R>90, G>80, B<0,55·max(R,G))
даже чуть в пользу DXVK: Vulkan 7419 (0,98%) против DXVK 10146 (1,34%) — линия
тонкая и полупрозрачная, нужен локальный детектор (профиль по строкам внутри
окна, сравнение с соседними строками), а не глобальный. **Закрыто в тот же
день:** локальный детектор — `shot_stats.py --lines` (очередь, п.3).
Попутно в том же кропе видно, что у Vulkan пол теряет детальность (плитка
однотонная, швы почти не читаются), а «бочки» справа — сплошные пурпурные
цилиндры без тёмного корпуса; в DXVK корпус тёмный, пурпурная только крышка.

Шум и пробел в диагностике лога: за прогон 86 878 раз печатается
`Вывод: глубина не совпадает с RT и замена не создана` (`nvrhibackend.cpp:5752`).
При этом `Пайплайн: createFramebuffer` — 0, то есть выводы не теряются: для этих
выводов глубина снимается правкой `80909c1cb` (`effDs = kNoRes`), а note печатается
ДО этого решения — шум. Но кэш подмены глубины хранит **одну** запись
(`d.dsSubIdx`, `nvrhibackend.cpp:5719-5749`): при чередовании размеров целей
(`VkDs: 1024x768 → 200x201`, `200x201 → 1024x768`, `512x512`, `256x256`) каждая
смена создаёт новую поверхность, старая остаётся в `d.surfaces`; в note не
печатаются размеры, поэтому отличить «не удалось создать» от «подмена не нужна»
сейчас нельзя.

### Нативный эталон на tiny10 (2026-10-21): разрешение, `gfx_scale3D`, пропажа HUD

Зачем: A/B просил нативный референс — Wine-эталоны `refs/wine-dxvk-2026-10-15/`
сняты в 1024x768 и мыльные, сравнивать детали мира по ним нельзя.

Разрешение кадра = `gfx_resolution` (`Render/rendermode.cpp:130`,
`ResolutionVariable`, STORAGE_USER → `…\My Games\Prime World Classic\user.cfg`):
бэкбуфер берётся прямо из него — `Render/renderer.cpp:1218-1219
pp.BackBufferWidth = _renderMode.width`; режимы `<1024x768` отбраковывает
`Renderer::NeedToFilterResolutionFromUser` (`Render/renderer.cpp:266-269`).
Разрешение рабочего стола гостя — `C:\pwbuild\setres.exe` (исходник
`vm10/setres.cpp`, сборка `vm10/setres.cmd`), запуск только в интерактивной
сессии задачей `PwRes`, цель читается из `C:\pwbuild\setres_target.txt`.
QEMU stdvga отдаёт 1280x1024 / 1440x900 / 1600x1200 / 1680x1050 / 1920x1080 /
1920x1200 (32 bpp, 64 Гц).

**Главная причина «разрешение очень низкое» — не стол, а `gfx_scale3D = 0.25`
в профиле.** Это `s_resScale` (`PF_GameLogic/PFRenderInterface.cpp:84`),
которую `CorrectResolution()` (`PFRenderInterface.cpp:429-440`; кламп 0.25…1.0,
округление до 4, `min(newRes, _resolution)`) применяет в `Interface::Start()`:
`mode.width3D = CorrectResolution(mode.width)` — то есть 3D-сцена рендерится в
25% бэкбуфера (1600x1200 → 400x300) и растягивается на кадр. С `gfx_scale3D 1.0`
кадр 1920x1080 становится резким (`refs/tiny10-native-2026-10-21/scale3D_compare.png`
— один и тот же узел арены при 0.25 и 1.0). UI-слой от `scale3D` не зависит: текст
неймплейта и портрет были читаемыми и при 0.25 — значит мыльность относится к
3D-проходам, а не к Flash/HUD.

**Нижний HUD пропадает при окне больше 1024x768.** При 1600x1200 и при 1920x1080
нижняя панель (хоткеи `1…0 O P`, XP/полосы с числами `840/840`, миникарта, зелье
`880`, замок) рисуется на ~104 px ниже нижнего края окна: в кадре остаётся только
верхний зелёно-жёлтый «шеврон» левой кнопки в углу
(`pw_desktop_1920x1080_hudclip.png`). Проверено: не зависит от `gfx_resolution`
(стол 1600x1200 + `gfx_resolution 1024x768` — панели тоже нет), от `gfx_scale3D`,
от аспекта (4:3 и 16:9 одинаково) и от режима (borderless и оконный с шапкой).
Верхне-якорные элементы (портрет, неймплейт, иконки-баффы) на месте — уезжает
только нижне-якорная группа.

Следствие для гейта паритета: (а) кадры с полным HUD снимаются **только** в
1024x768 — все сравнения иконок/хотбара/миникарты вести там; (б) 1920x1080 +
`gfx_scale3D 1.0` — референс деталей мира (текстуры, геометрия, свет), но не UI;
(в) пункт очереди про «нет иконок способностей» нельзя сверять с нативным кадром
1600x1200 — там нижней панели нет в обоих бэкендах, это не дефект Vulkan-пути.

Эталоны: `tools/render_shot/refs/tiny10-native-2026-10-21/` —
`pw_desktop_1920x1080_scale3D1.png` (мир, полный кадр),
`pw_desktop_1920x1080_hudclip.png` (доказательство среза панели),
`pw_desktop_1024x768_hud.png` + `pw_desktop_1024x768_hud_crop.png` (полный HUD),
`scale3D_compare.png`. Снимки — VNC-экран (`vm7/vncshot.py 127.0.0.1 5918`), на
них есть оверлей «Activate Windows» — в `auto_screenshot`-readback его нет.
Состояние профиля гостя сейчас: `gfx_resolution 1920x1080`, `gfx_scale3D 1.0`,
`gfx_borderless 1`, `gfx_fullscreen 1`; оконный вариант сохранён в
`user.cfg.winbak`.

### Очередь после A/B 2026-10-21 (пп. 2–6 пересмотрены в тот же день по третьей серии)

1. ✅ **Плашка закрыта (2026-10-21, четвёртая серия).** Причина — seam глушил
   `colorWriteEnable = 0`: `Src/Render/nvrhibackend.cpp:5177` стояло
   `st.colorWriteEnable ? st.colorWriteEnable : 0x7u`, т.е. осознанный
   «цвет не писать» превращался в запись RGB. Плашка — трафаретный quad
   Flash-маски (`FlashRenderer.cpp:1509 BeginSubmitMaskApply`, `:1518
   BeginUnSubmitMaskApply`; то же `ShadowMap.cpp:234`, `batch.cpp:667`), при
   `c20=1 c21=0` её PS выдаёт `COLOR0=FFFFFFFF` → непрозрачное белое пятно.
   Замер и фикс — секция «Плашка закрыта: seam глушил colorWriteMask=0» в конце
   файла; белых 43 585 → 15 px, заодно проступили иконки хотбара и миникарты.
   Прежние гипотезы (вариант шейдера, `PW_VK_ILPAD`, конвенция умножения матриц,
   бисект по PS) — мертвы, ниже оставлен их протокол ради воспроизводимости.
   Гипотеза «один корень у плашки и 07904» опровергнута (закрытие хвоста белых не
   убавляет), гипотеза «плашку рисует другой вариант шейдера» опровергнута
   (живые индексы в обоих бэкендах одинаковы, #0 и #1 побайтово идентичны).
   Плашку рисует quad `UIFlashMaterial #0` (`vtx=6 lay=5`, вершины
   `x[60..8900] y[60..3620]`, `COLOR0=FFFFFFFF`); изоляция — `PW_VK_SKIPPJ`
   (окно по прямоугольнику ПОСЛЕ проекции): белых 43 562 → 2, остальные регионы
   кадра не меняются (см. секцию «A/B 2026-10-21, вторая серия» в конце файла).
   Клиент выпускает тот же Dip и в D3D9, и он проецируется ровно в тот же
   прямоугольник (`proj=(28,5,442,5)-(360,1,576,0)`, `wmax=1`), но пикселей не
   даёт. Основная непроверенная гипотеза — конвенция умножения матриц
   (клиент кладёт перенос в последнюю строку, HLSL умножает `M*v`): для матриц
   такого вида столбцовое умножение даёт `w≈3·10⁷` и схлопывание. Эксперимент с
   подменой c0..c7 на транспонированные сделан и не подтвердил (секция «вторая
   серия»); остаётся DXBC VS #0 против его же SPIR-V. Третья серия добавила
   только воспроизводимость: 43 562 белых px в двух независимых Vulkan-прогонах,
   `PW_VK_SKIPPJ` → 2 px.
2. ⚠️ **Миникарта — крупнейший разрыв после плашки; не цвет, не мягкость, не
   масштаб.** ПЯТОЙ СЕРИЕЙ ПЕРЕСМОТРЕНА: offscreen-проход жив (по PWRD в цель
   `200x201` идёт 48 выводов за ~6 кадров), так что искать надо не пропущенный
   проход, а тон/деталь слоя рельефа — см. «Пятая серия A/B» в конце файла. На сведённых по игровым часам парах: \|D\| 19,6, светимость 50,9
   против 33,0 (Vulkan темнее на 18), det 26,1 против 7,6. `scale_probe.py`:
   лучший масштаб s=1,000 (сдвиг −1 px), размытие DXVK gross не улучшает
   (21,9→24,2 %) → расхождение в содержании прохода. Прежний порог «minimap \|Δ\|
   с 23,8 до <5» считался по несведённым кадрам; теперь: **\|D\| <5 на сведённых
   парах** (шум 0,0…0,3). Проверять: чем и в какой последовательности рисуется
   миникарта (отдельный проход, текстура карты, LOD, blend) +
   `PW_VK_TEXDUMP`/`PW_VK_TEXGPU` + `VkRecTex` против загружаемого
   (`UI/Textures_default/AdvMap/minimap_icons.dds`, загрузка `Minimap.cpp:611`;
   иконки хотбара — Flash-слой). Сверять только с кадрами 1024x768: при большем
   окне нижняя панель нативного клиента уезжает за край кадра (см. раздел
   «Нативный эталон на tiny10» выше).
3. ✅ **Жёлтые линии — локальный детектор есть** (`shot_stats.py --lines`,
   профиль «жёлтости» по строкам внутри узких полос). Третья серия: в двух
   независимых Vulkan-прогонах **те же строки** — 315 (+25,7 / +25,6),
   309 (−20,7 / −20,7), 310, 372, 433, 262 — против одной строки у DXVK, значит
   дефект живёт в экранных координатах. Открытая часть — причина; кандидат
   «непереведённый альфа-тест» ОТВЕРГНУТ пятой серией (см. «Пятая серия A/B»).
4. ✅ **Метрика оттенка, а не светимости** — `shot_stats.py --tone`: гистограмма
   тона (12 бинов + «серый»), средняя насыщенность, среднее B−R, L1 между
   кадрами. Пурпурные цилиндры против тёмных бочек дают \|Δ\| 4,1 по средней и
   L1 97 по тону.
5. **Гигиена гейта паритета (пополнилась главным):** DXVK-стороне всегда задавать
   `DXVK_SHADER_CACHE_PATH` + `DXVK_SHADER_CACHE` (иначе каждый прогон — холодный
   кэш и ложная «деградация»); **стороны входят в матч с расхождением ~20 с,
   поэтому кадры сводить по обратному отсчёту HUD (кроп `(492,130)-(612,164)`),
   шаг снимков 2–3 с** — без сведения регионные \|D\| врут на порядок
   (center_world 18,1 против 2,8); шум прогона ≈ 0 (\|D\| ≤0,3, gross ≤1,3 %),
   так что «шум момента матча» — не объяснение. `xvfb_shot.sh --series` починен
   (`c23becd55`), эталоны `tools/render_shot/refs/` от 15/16.10 переснять — они
   с другим зумом камеры и без кэша.
6. **Шаг 13:** метрика прогресса — доля DIP'ов через новый путь. Прежняя
   формулировка «по A/B мир и текст HUD уже паритетны» **снята**: на сведённых
   парах мир и HUD сходятся по средней (\|D\| 1,3…4,0 при шуме 0,0…0,1), но
   пиксельно различаются (gross 12,7…49,6 % при шуме ≤1,7 %); в hero_portrait
   размытие DXVK уменьшает gross 27,9→18,5 %, размытие Vulkan — ухудшает
   (29,0 %), т.е. по HUD кадр Vulkan мягче. Порядок фронта: плашка → миникарта
   (п.2) → пиксельные расхождения мира/HUD.
7. ✅ **Производительная часть A/B замерена (счётчик есть).** `PW_FPS_STATS=<сек>`
   (`Src/Render/renderer.cpp`, `LogPresentRate` перед `Renderer::Present`) —
   строка `Renderer: fps <x> backend=vulkan|d3d9 кадров=N за Xмс всего=M`,
   счётчик общий для обоих бэкендов. Первый замер (Wine, RTX 3060, 1024x768,
   серия `ab5`): DXVK 100,7…131,4 к/с, Vulkan 38,2…44,8 к/с → **Vulkan-путь
   seam'а медленнее в 3,2…3,4 раза**, а не быстрее.
8. ✅ **Мелочь по логам закрыта.** NoteGate (`nvrhibackend.cpp:1365+`) сравнивал
   `strcmp` по `char what[48]`, а русские UTF-8 тексты длиннее 47 байт → таблица
   из 32 слотов забивалась «новыми» местами и гейт начинал пропускать всё; теперь
   сравнение по указателям литералов + счётчик неучтённых мест. Подмена глубины
   (`d.dsSubIdx` → `std::map` по размеру цели) и note с размерами; вывод без
   глубины подмены не касается (`DsKeepRequested() || zEnable || stencilEnable`).
   Замер: 86 878 строк → 0, сводка `VkNotes:`/`VkDsflip:` в конце прогона.

### 07904: закрытие хвоста константой убивает мир — подход закрыт, плашка не при чём (2026-10-21)

Что добавлено в код (`Src/Render/nvrhibackend.cpp`, всё за env, поведение по
умолчанию не изменилось):

- `BuildInputLayout` (`:3966`): env `PW_VK_ILPAD` — 0 = ничего не закрывать,
  1 = дубликат предыдущего атрибута только в дырках (**дефолт = прежнее
  поведение**), 2 = обнулённый VB и в дырках и на хвосте, 3 = дырки дубликатом +
  хвост обнулённым VB; env `PW_VK_NULLW` (0/1) — значение w подмены. Счётчик
  закрытых location'ов `d.ilNullPad` + note `VkIlPad: закрыто N location'ов
  (layout X vs Y (шейдер), maxLoc=…, attrs=…)`; note `VkIlTail:` (кап 80) в любом
  режиме печатает раскладки с открытым хвостом: входы VS и чем их покрывает
  клиент.
- Обнулённый VB `d.nullVb` (`createBuffer`, `kNullVtxSlot = kMaxVertexStreams+1 = 9`,
  `elementStride = 0`, формат по классу входа: `R32G32B32A32_SFLOAT` /
  `_SINT` / `_UINT` — `VertexFormatForSemantics`, `:3512`).
- `Draw` (`:6243`): если у текущей раскладки есть атрибуты на слоте
  `kNullVtxSlot`, привязать `d.nullVb` и на остальные свободные слоты 1…8.
  Без этого NVRHI кладёт `nullptr` в `pBuffers` (`vulkan-graphics.cpp:621-638`,
  `bindingLimit = first+count`) → `vkCmdBindVertexBuffers2` с null-буфером →
  `VUID-vkCmdBindVertexBuffers2-pBuffers-03360` ×10 000 за прогон (лог обрезан).

Матрица замеров (`shot_stats.py --defects`, t80/t140/t180, Wine-стенд, карта
`Maps/Multiplayer/MOBA/_.ADMPDSCR.xdb`, скрипты прогонов
`PW/tools/render_ab-2026-10-21/ilpad/run_*.sh` (вне репо):

| режим | белых | чёрных | 07904 |
|---|---|---|---|
| `PW_VK_ILPAD=1` (= HEAD) | 43 567 (5,77%) | 944 (0,13%) | ×4 |
| `=2 NULLW=0` (везде (0,0,0,0)) | 43 560 (5,77%) | 604 566 (80,13%) | 0 |
| `=2 NULLW=1` (везде (0,0,0,1)) | 43 560 | 604 566 (80,13%) | 0 |
| `=3 NULLW=0` (дырки дубл. + хвост (0,0,0,0)) | 43 560 | 627 073 (83,11%) | 0 |
| `=3 NULLW=1` | 43 560 | 604 566 (80,13%) | 0 |
| `=0` (пэддинга нет вообще) | 43 560 | 566 000 (75%) | — |

Выводы:

1. **Открытый хвост закрывать нечем.** За весь прогон раскладка с открытым
   хвостом ровно одна: `layout 0 / vs 101 (MainPass_VSMain)`, входы VS
   `POSITION@0 TEXCOORD0@1 TEXCOORD1@2`, у клиента — `POSITION@0 (off 0, str 16)`
   и `TEXCOORD0@1 (off 8, str 16)`. Это НЕ `UIFlashMaterial #1024` (пара
   `layout 5 / vs 10` в этом прогоне не всплывает). Вход `TEXCOORD1` реален и
   значим: любое его закрытие (нулями, (0,0,0,1), дубликатом `TEXCOORD0` —
   см. п.3 прежней секции: 40,64%) режет мир. «Undefined» на Mesa здесь случайно
   безвреден — ровно так же безвреден он в DXVK-прогоне на том же драйвере.
2. **Плашка не связана с 07904**: при закрытии хвоста белых не меньше
   (43 560…43 567), VUID при этом 0. Гипотеза п.1 очереди снята.
3. **Пэддинг дырок дубликатом — load-bearing**: `PW_VK_ILPAD=0` сдвигает
   location'ы последующих атрибутов (NVRHI нумерует location индексом в массиве,
   `vulkan-shader.cpp:186`) → мир чёрный 75%, плашка из белой становится
   тёмно-серой (геометрия на том же месте, данные другие).
4. **Решение:** дефолт `PW_VK_ILPAD=1`, 07904 ×4 принимается как безвредное
   предупреждение валиатора на этом драйвере (тот же UB есть у DXVK). Машинерия
   обнулённого VB оставлена за env: она понадобится, когда для входа найдётся
   настоящее значение (правка клиентской декларации или выбор варианта шейдера —
   территория шага 14).
5. Прогоны с валидацией (`PW_VK_LAYERS=1`) для A/B бесполезны: вывод валиатора
   идёт в stderr, wine-лог кадра пуст; `PW_VK_LAYERS="VK_LAYER_KHRONOS_validation"`
   роняет `createDevice` (`VUID-VkDeviceCreateInfo-pNext-06532`).
6. Полный список env-ручек бэкенда для дальнейшей охоты за плашкой: grep по
   `GetEnvironmentVariableA("PW_VK_` в `nvrhibackend.cpp` — в частности
   `PW_VK_SKIPDRAW_FROM/TO`, `PW_VK_SKIPPS_FROM/TO`, `PW_VK_SKIPPS_EXCEPT`,
   `PW_VK_TRACEORD_FROM/TO`, `PW_VK_TRACEPS`, `PW_VK_TRACECONST`,
   `PW_VK_TEXDUMP(_MIN/_SIZE)`, `PW_VK_TEXGPU_*`, `PW_VK_RECORD*`,
   `PW_VK_STATE_MASK`.

## Плашка: клиент выпускает тот же Dip в обоих бэкендах, в D3D9 он не растеризуется (2026-10-21, `f7b7f84cc`, `74f4dbee8`)

Инструмент, которого не хватало: трасса самих вызовов отрисовки, а не пикселей.
Она поставлена с обеих сторон (`PW_DRAW_TRACE=1` в клиенте — `NoteDipTrace` в
`smartrenderer.cpp`; на стороне seam'а — `PW_VK_TRACEORD_*` / `PW_VK_TRACEPS` /
`PW_VK_SKIPBB` в `nvrhibackend.cpp`).

Кто рисует плашку (боевой кадр, Wine-стенд, `PW_VK_SKIPBB` + отпечатки SPIR-V):

- Вывод изолирован бисектом по порядкам: `PW_VK_SKIPDRAW_FROM/TO=475,499`
  снимает белых с 43 560 до 2; `450..474` — не снимает.
- `PW_VK_SKIPBB=0,0,3240,520` (bbox первых вершин) — белых 2;
  `PW_VK_SKIPBB=0,0,3239,519` — белых 43 560. Т.е. плашку даёт ровно этот вывод.
- Отпечаток блоба: `spvVs=1580:ddd21e101202701f`, `spvPs=1160:54c85447d086f47f`
  → `UIFlashMaterial #0/#1` (`tools/render_replay/fnvmap.py` по корпусу
  `Data/Shaders/UberShaders`). Варианты #0 и #1 побайтово идентичны.
- Это quad: `vtx=6 prim=2 lay=5`, вершины `x[60..8900] y[60..3620]`
  (аспект 2,48 = 330/132), `COLOR0 = 0xFFFFFFFF`, константы PS
  `c20=(1,1,1,1) c21=(0,0,0,0)` → `color = VertexColor*color1 + color2` = белый
  непрозрачный. Цель — не бэкбуфер, а off-screen 1024x768 A8R8G8B8.

Тот же Dip в D3D9/DXVK (`PW_DRAW_TRACE=1`, прогон `dx9_dx`, 8000 трасс):

- Клиент выпускает его **так же**: 4541 Dip'ов в ту же цель, из них с bbox
  `x[60..8900] y[60..3620]` — 488 за окно трассы, `mat=Shaders/UberShaders/
  UIFlashMaterial #0 prim=2 type=3 nv=6`.
- Состояние совпадает точь-в-точь: `vp=0,0 1024x768 z[0..1]`,
  `sc=0,0..1024,768 scTest=0`, `ab=1 sb=5 db=6` (SRCALPHA/INVSRCALPHA),
  `z=0/0` (тест глубины выключен), `stencil=0`, `cull=NONE`, `cw=15`,
  `aTest=0`, `sep=0`. Поток: `str=16`, `vbSize=786432` при `need ≤ 672` —
  обрезки по концу буфера нет.
- **Quad в D3D9 не даёт пикселей нигде.** `PW_D3D_FORCEOPAQUE=2`: на Dip'ах
  плашки вершины перекрашены в чистый зелёный (`COLOR0=0xFF00FF00` в смещении 8)
  и `ALPHABLENDENABLE=0` (квад пишется начисто) → readback цели: `зелёных=0`.
  Положительный контроль `PW_D3D_FORCEOPAQUE=3` (перекрашены ВСЕ Dip'ы
  `UIFlashMaterial #0`) → `зелёных=2501`, bbox `(11,96)-(88,139)`: и запись в VB,
  и readback работают. Значит дело не в альфе и не в измерении — геометрия в
  D3D9 не доходит до растеризации.

Отклонено на этом пути (каждое — замером, не рассуждением):

- выбор варианта шейдера / промах по индексу: v1-таблица `UIFlashMaterial.shd`
  содержит все 12 живых индексов прогона, живые индексы Vulkan-прогона
  совпадают с DXVK-прогоном (`0,64,256,270,320,1024,1038,1048,1054,1088,1152,1536`);
- незакрытые входы VS (VUID-07904) — `PW_VK_ILPAD` 0/1/2/3, плашка во всех
  режимах (см. предыдущую секцию);
- `Clear` без scissor — `VkWhiteClear` (clear'ы белым) за прогон 0;
- состояние: `PW_VK_STATE_MASK` 1 (трафарет) / 2 (смешивание) / 4 (запись
  глубины) / 8 (тест глубины) — белых 43 560…47 308 во всех четырёх;
- бисект по PS: плашку рисует не финальная композиция (`ps=8` — её пропуск гасит
  весь кадр, прежнее «бисект показал ps=8» было артефактом);
- dummy-текстуры (1x1 заполнены (0,0,0,0)), размер потока, диапазон глубины
  вьюпорта, hex вершин (идентичны seam'овским).

Машинерия обнулённого VB после предыдущей секции доработана и тоже не помогла:
`d.nullVb` вырос с 4 вершин до 65 536 (64-байтный буфер читался при индексах
вершин в тысячи — UB), инстансный слот `kMaxVertexStreams` при `maxSlot=9`
закрыт dummy'ем (иначе в `GraphicsState` дыра и NVRHI кладёт туда мусор стека).
Замер не изменился: `PW_VK_ILPAD=2` и `=3` → чёрных 604 561…604 577 (80,13%),
белых 43 560 (5,77%), плашка на месте. Т.е. обнуление дырок убивает мир не из-за
размера буфера и не из-за дыры в слотах.

Что остаётся как объяснение (не проверено):

1. **Входы VS без клиентского потока.** D3D9 подставляет в них (0,0,0,1);
   seam ставит дубликат предыдущего атрибута (`PW_VK_ILPAD=1`, load-bearing для
   мира). Для `UIFlashMaterial` VS читает `Shift`/`Aliasing`/`DiffuseUV`, которых
   в декларации нет — выход `Output.Position` от них зависит (Scale9Grid /
   нормировка `Aliasing`), и в D3D9 quad уходит в клип/вырождение.
2. **Порядок умножения матриц** в SPIR-V (DXC транслирует `mul(modelMatrix, v)`
   через транспонирование) против DXBC — тогда позиция расходится молча.

Следующий шаг (дешевле новых прогонов): статическое сравнение `UIFlashMaterial`
VS #0 — DXBC из v1-слоя `Data/Shaders/UberShaders/UIFlashMaterial.shd`
(читается `tools/render_replay/match_shd.py:read_v1_pairs`) против его же SPIR-V
из v2-слоя (`spirv-dis`): как вычисляется `Output.Position`, какие входы в этом
умножении участвуют и что происходит с входами без имён.


## A/B 2026-10-21, вторая серия: плашка изолируется окном по проекции

Тот же Wine-стенд (`wine-client/Game/Bin/PW_Game.exe`, Xvfb `:97`, карта
`Maps/Multiplayer/MOBA/_.ADMPDSCR.xdb`), **один и тот же exe** на обеих сторонах
(сборка со всей диагностикой), DXVK 3.1.1 + `DXVK_SHADER_CACHE_PATH`/
`DXVK_SHADER_CACHE=512`, 4 снимка × 25 с, кадр 1021x739. Прогоны
`PW/tools/render_ab-2026-10-21/ab3/run_ab3.sh`, артефакты (снимки, логи,
картинки `ab3_full.png`, `ab3_plate.png`) — там же, вне репо.

Дефекты (`tools/render_shot/shot_stats.py --defects`):

| сторона | t25 | t50 | t75 |
|---|---|---|---|
| DXVK (эталон) | 81 белых (0,01 %) | 81 (0,01 %) | 33 (0,00 %) |
| Vulkan | 46 333 (6,14 %) | 43 562 (5,77 %) | 43 562 (5,77 %) |
| Vulkan + `PW_VK_SKIPPJ=29,443,359,575` | 46 334 | **2 (0,00 %)** | 2 (0,00 %) |

Регионы (`tools/render_shot/region_ab.py`, среднее по 4 снимкам; A = DXVK):

| регион | A ↔ Vulkan | A ↔ Vulkan+SKIPPJ | Vulkan ↔ Vulkan+SKIPPJ |
|---|---|---|---|
| plate_area (29,443)-(360,575) | **98,6** | **8,2** | 95,9 |
| minimap | 27,4 | 27,4 | 0,0 |
| hero_portrait | 25,2 | 25,2 | 0,0 |
| center_world | 14,1 | 14,3 | 0,4 |
| ability_bar | 13,5 | 13,5 | 0,0 |
| right_units | 12,8 | 12,8 | 0,0 |
| hud_top | 5,3 | 5,4 | 0,0 |

Что это даёт:

- **Выводы, проецирующиеся ровно в прямоугольник плашки, в Vulkan-пути есть.**
  Прежнее утверждение «в seam нет выводов в этот прямоугольник» — артефакт
  гейта: трасса `PW_VK_TRACEPROJ` фильтровала по **вершинному** bbox, а не по
  проекции. Новый `PW_VK_SKIPPJ=x0,y0,x1,y1` (`DrawImpl`, `nvrhibackend.cpp`)
  пропускает вывод, если его прямоугольник **после проекции** матрицами клиента
  совпадает с окном (допуск 25 px): белых 43 562 → 2, `VkSkipPJ` — 6 записей.
- **Пропуск точечный.** Отличия Vulkan от Vulkan+SKIPPJ — только в plate_area
  (|Δ|=95,9); миникарта, хотбар, портрет, HUD, юниты — |Δ|=0,0 (center_world
  0,4 — шум момента матча). Прежний `PW_VK_SKIPBB` (по вершинному bbox) вместе с
  плашкой срезал полосы опыта: у них тот же quad `x[0..3240] y[0..520]`.
- **`SKIPPJ` — инструмент изоляции, не обходной путь.** Плашка движется с
  камерой: на t25 она в (29,398)-(359,531), и фиксированное окно туда не
  попадает (белых 46 334). Поэтому в t25 замер не меняется.
- **Сторона D3D9 проецирует туда же и не красит.** `D3dProj` (`PW_DRAW_TRACE=1`,
  `NoteDipTrace` в `smartrenderer.cpp`): 32 вывода за прогон с тем же вершинным
  bbox дают `proj=(28,5,442,5)-(360,1,576,0)` при `wmax=1`, `raw8=FFFFFFFF` —
  то есть ровно прямоугольник плашки; при этом readback цели сразу после вывода
  не даёт там ни белых, ни зелёных, а `PW_D3D_DRAWERR=1` не дал ни одной ошибки
  вызова (0 строк). Расхождение — в исполнении, не в клиенте.

Конвенция умножения матриц — теперь основная гипотеза (не проверена):

- трасса констант с обеих сторон (`D3dC` в клиенте, `VkTraceC` в seam, формат
  `%.6g` — раньше было `%.4f` и матрицы выглядели «разными») показывает
  **одинаковые** банки; в них перенос лежит в **последней строке**, например
  `W=(−0,00538623,0,0)(0,0,513012,0,0)(0,0,1,0)(6431,52,18927,7,0,1)`,
  `V=(7,32601e-05,0,0)(0,−9,76563e-05,0,0)(0,0,1,0)(−1,00147,1,00195,0,1)` —
  то есть матрицы записаны под **строковые** векторы (`v*M`);
- HLSL пишет `mul(modelMatrix, v)` — это **столбцовое** умножение (`M*v`). Для
  матриц такого вида `w = 6431,52·x + 18927,7·y + 1 ≈ 3·10⁷`, и квад схлопывается
  в точку — поэтому офлайн-разбор `tools/render_replay/plate_geom.py` считает
  по `v*M`, а `M*v` даёт коллапс в центр;
- DXC при генерации SPIR-V транспонирует, и в Vulkan-пути `v*M` даёт `w=1` и
  правильный прямоугольник. Для матриц без больших элементов в 4-м столбце обе
  конвенции совпадают — потому мир и остальной HUD не расходятся.

Эксперимент с конвенцией сделан (`PW_D3D_FORCEOPAQUE=4`: для выводов плашки
вершины перекрашены в зелёный, `ALPHABLENDENABLE=0`, а `c0..c7` подменены на
транспонированные с восстановлением после Dip'а) — **не подтвердил**: 42 readback
цели дают `зелёных=0`, на снимках кадра зелёного тоже нет (в отличие от
положительного контроля `=3`, где зелёный появляется). Значит транспонирование
WORLD/VIEW квад в D3D9 не оживляет; конвенция умножения остаётся возможной, но
только вместе с чем-то ещё (например, с тем, как DXBC читает незакрытые входы).

Что осталось непроверенным: DXBC VS #0 (`Data/Shaders/UberShaders/
UIFlashMaterial.shd`, v1-слой, 396 Б — читается `match_shd.py:read_v1_pairs`)
против его же SPIR-V из v2-слоя (`spirv-dis`): чем именно `Output.Position`
отличается на этих вершинах. Дешевле этого — на стороне D3D9 подменить позиции
вершин плашки на заведомо «заметные» (свой прямоугольник) и посмотреть, появится
ли он в кадре: это отделит «вывод в D3D9 мёртв» от «вывод жив, но VS уводит
геометрию».


## A/B 2026-10-21, третья серия: воспроизводимость стенда и граница регионных метрик

Прогоны `PW/tools/render_ab-2026-10-21/ab4/run_ab4.sh` (+ `run_pj.sh`,
`run_noise.sh`), артефакты — там же, вне репо: снимки всех шести прогонов,
логи (`metrics.log` — регионные матрицы, `scale.log` — поиск масштаба и
мягкости), `ab4_full.png`, `ab4_plate.png`, `ab4_countdown2.png`,
`ab4_clocks.png`, `metrics.sh`. Тот же стенд, что в
двух предыдущих сериях (тот же exe со всей диагностикой, Xvfb `:97`, карта
`Maps/Multiplayer/MOBA/_.ADMPDSCR.xdb`, DXVK 3.1.1 + прогретый
`DXVK_SHADER_CACHE_PATH`/`DXVK_SHADER_CACHE=512`), кадр 1021x739, 6 снимков с
шагом 20 с (t0…t100). Главное отличие от ab3 — **шесть прогонов вместо трёх**:
`dx`, `vk`, `pj` (`PW_VK_SKIPPJ=29,443,359,575`) и **повторы сторон** `dx2`,
`vk2`. Повторы нужны были, чтобы перестать гадать, что в регионной матрице —
дефект, а что шум матча.

### 1. Шум прогона ≈ ноль

`region_ab.py "ab4_dx*" "ab4_vk*" "ab4_dx2*"` (третий шаблон — повтор стороны A,
он и даёт полосу шума):

| регион | \|D\| сигнал | \|D\| шум | gross сигнал | gross шум |
|---|---|---|---|---|
| plate_area | 167,6 | 0,0 | 99,3 % | 0,6 % |
| hud_top | 26,2 | 0,0 | 36,9 % | 0,1 % |
| center_world | 18,1 | 0,1 | 29,2 % | 0,9 % |
| minimap | 16,1 | 0,3 | 33,6 % | 0,2 % |
| hero_portrait | 10,8 | 0,0 | 44,6 % | 0,2 % |
| right_units | 9,9 | 0,0 | 57,2 % | 1,3 % |
| ability_bar | 4,4 | 0,0 | 54,2 % | 0,0 % |

По всему кадру (`shot_stats.py --tone`): dx↔dx2 L1 0,1 (B-R +22,2/+22,2,
насыщенность 0,355/0,355, серых 7,0 %/7,0 %), vk↔vk2 L1 0,5 (+14,7/+14,6,
0,314/0,314, 18,5 %/18,5 %). Дефекты: dx 83–85 белых против dx2 85; vk 43 562
против vk2 43 562; чёрных в мире 724–733 у dx и dx2.

Вывод: стенд детерминирован — два независимых прогона **одной** стороны в те же
wall-clock секунды отличаются не более чем на 0,3 по средней региона и на 1,3 %
по gross. Объяснение «это шум момента матча» для малых величин больше не
принимается.

### 2. Но стороны в ту же секунду — РАЗНЫЙ МОМЕНТ матча

Кроп обратного отсчёта `(470,126)-(620,162)` (этот элемент HUD лежит на экране
в обоих бэкендах в одном месте): на t60 у DXVK **0:08**, у Vulkan **0:24**, у
Vulkan+SKIPPJ **0:24** (`ab4_countdown2.png`). Различается и зум камеры — на
`ab4_full.png` кадр DXVK заметно крупнее.

Следствие (главный результат серии): межсторонние \|D\|/gross/tone из п.1 —
**не метрики паритета рендера**, в них входит разница момента и ракурса. Значит:

- матрица второй серии (minimap 27,4; hero_portrait 25,2; hud_top 5,3 …)
  не читается как «где именно расходятся регионы»;
- по этим числам нельзя утверждать ни «мир паритетен», ни «мир не паритетен»;
- воспроизводимость из п.1 работает только **внутри** стороны, и именно она
  даёт право считать дефект-счётчики (п.4) — они камера-независимы.

### 3. Новая метрика детальности — и почему она пока не аргумент

`det` в `region_ab.py` = среднее |перепада соседних пикселей| (энергия граней)
по региону. DXVK выше Vulkan во всех регионах: minimap 26,4 против 8,5;
hero_portrait 35,1 против 20,8; ability_bar 28,2 против 18,2; hud_top 17,7
против 10,8; right_units 10,6 против 9,0; center_world 8,2 против 6,8. При
разном зуме (п.2) это **не** доказательство потери детальности — детальность
зависит от масштаба кадра. Метрика остаётся пригодной для сравнений внутри
одной стороны и станет аргументом после сведения кадров (п.5).

### 4. Что установлено надёжно (метрики, не зависящие от момента)

- **Белая плашка.** Vulkan 43 562 px (5,77 %) и повтор vk2 те же 43 562
  (5,77 %) против DXVK 83–85 (0,01 %). `PW_VK_SKIPPJ=29,443,359,575` → 2 px
  (0,00 %). В регионе plate_area: белых 93,05 % → 13,29 %, \|D\| 153,4,
  gross 80,0 %; остальные регионы Vulkan↔Vulkan+SKIPPJ — \|D\| 0,0
  (ability_bar 0,6 и center_world 0,7 при шуме 0,1…0,3). Независимый прогон
  подтвердил изоляцию плашки из ab3.
- **Горизонтальные жёлтые линии** (`shot_stats.py --lines`). В двух
  независимых Vulkan-прогонах — **те же строки**: 315 (+25,7 / +25,6),
  309 (−20,7 / −20,7), 310 (−10,2 / −10,4), 372 (+10,4 / +10,3),
  433 (+9,4 / +9,4), 262 (+9,0). У DXVK — одна строка 297 (−10,4). Строки
  совпадают между прогонами, значит артефакт живёт в экранных координатах,
  а не в содержимом мира. Расстояния между хитами 53–61 px — наблюдение,
  не вывод.
- **Тон кадра** воспроизводится внутри стороны (п.1), но межстороннее
  сравнение (+22,2 против +14,7, серых 7,0 % против 18,5 %) — под запретом п.2.

### 5. Сведение по игровым часам — матрица, которой не получалось в прошлых сериях

Ключ — цифры обратного отсчёта (этот элемент HUD лежит на экране в обоих
бэкендах в одном месте, кроп `(492,130)-(612,164)`, `ab4_clocks.png`):

| снимок | t0 | t20 | t40 | t60 | t80 | t100 |
|---|---|---|---|---|---|---|
| DXVK | — | 0:49 | 0:28 | 0:08 | — | — |
| Vulkan | — | — | 0:45 | 0:24 | 0:03 | — |

Vulkan входит в матч на ~20 с позже; ход отсчёта у обеих сторон одинаков
(~21 с отсчёта на 20 с стенных часов). Сведённые пары (остаток 4–5 с
игрового времени): dx t20↔vk t40, dx t40↔vk t60, dx t60↔vk t80 — сведение
сделано копиями `ab4_vks_shot_t20/40/60.png` (это `ab4_vk_shot_t40/60/80.png`),
чтобы `region_ab.py` свёл их по равным `t` без правки инструмента.

Матрица на сведённых парах против матрицы по wall-clock (`region_ab.py`,
A = DXVK, шум по dx2 — 0,0…0,1):

| регион | \|D\| по часам | \|D\| по стенам | gross часы | gross стены |
|---|---|---|---|---|
| plate_area | 179,6 | 167,6 | 100,0 % | 99,3 % |
| minimap | **19,6** | 16,1 | 26,6 % | 33,6 % |
| right_units | 6,4 | 9,9 | 49,6 % | 57,2 % |
| center_world | **2,8** | 18,1 | **12,7 %** | 29,2 % |
| hud_top | **2,0** | 26,2 | **20,0 %** | 36,9 % |
| hero_portrait | **1,3** | 10,8 | 33,9 % | 44,6 % |
| ability_bar | 4,0 | 4,4 | 44,6 % | 54,2 % |

- Сведение роняет мир и HUD на порядок (center_world 18,1→2,8; hud_top
  26,2→2,0): бо́льшая часть межстороннего расхождения в прошлых сериях —
  разница моментов, а не рендера.
- По средней мир и HUD близки к паритету (\|D\| 1,3…4,0 при шуме 0,0…0,1),
  но пиксельно различаются (gross 12,7…49,6 % при шуме ≤1,7 %).
- **Миникарта — крупнейший разрыв после плашки:** \|D\| 19,6, светимость
  50,9 против 33,0 (Vulkan темнее на 18), det 26,1 против 7,6.

Масштаб и мягкость (`scale_probe.py`, новый инструмент):

- лучший масштаб **s=1,000** во всех трёх проверенных регионах
  (hero_portrait, minimap, center_world), сдвиг 0…−1 px → версия «Vulkan
  рендерит в другой внутренний размер и растягивает в окно» **отклонена**;
- тест на мягкость: в hero_portrait размытие **DXVK** уменьшает gross
  27,9 % → 18,5 % (r=1), размытие Vulkan — ухудшает (29,0 %) → по HUD кадр
  Vulkan **мягче**, и det это видит (35,1 против 23,7);
- в minimap и center_world размытие gross почти не меняет (21,9→24,2 и
  10,9→10,3) → их расхождение **не** в мягкости: миникарта у Vulkan темнее
  и плосче по содержанию.

### 6. Что дальше

Дальнейший порядок работ — не здесь, а в «Очередь после A/B 2026-10-21»
(пп. 2, 6, 7): миникарта, пиксельные расхождения мира/HUD при совпавшей
средней, счётчик Present'ов для производительной части A/B.

- Инструмент серии: `region_ab.py` — `gross%` (доля пикселей региона с
  \|Δ\|>32; средние по региону компенсируются и молчат при полностью разной
  картинке), `det` (энергия граней), проверка одинакового размера кадра,
  третий шаблон = повторный прогон стороны A; `shot_stats.py` — `--tone`,
  `--lines`; `scale_probe.py` — поиск масштаба/сдвига и тест на мягкость.
- Ограничение частоты кадров на обеих сторонах без кода не получается:
  env-ключей (`PW_*fps`/`*limit*`) в клиенте нет, `PresentationInterval`
  берётся из конфига рендера (`renderer.cpp:1236`), Xvfb вертикалку не
  ограничивает — отсюда и сведение по HUD-часам вместо синхронных кадров.

## Плашка закрыта: seam глушил colorWriteMask=0 (2026-10-21, четвёртая серия A/B)

### Причина — одна строка

`Src/Render/nvrhibackend.cpp:5177` (заполнение `nvrhi::BlendState::RenderTarget`):

```cpp
target.colorWriteMask = static_cast<nvrhi::ColorMask>(
    st.colorWriteEnable ? st.colorWriteEnable : 0x7u);   // было
```

с комментарием «D3D9 по умолчанию пишет RGB без альфы (7)». Клиент же выключает
маску **целиком** в трафаретных проходах Flash-масок: `FlashRenderer.cpp:1509
BeginSubmitMaskApply` (`COLORWRITEENABLE=0`, `STENCILPASS=INCR`,
`STENCILREF=maskLevel`, `STENCILFUNC=EQUAL`), `:1518 BeginUnSubmitMaskApply`
(DECR), `:1528 EndSubmitMaskApply` возвращает `0x0F`; тот же ноль —
`ShadowMap.cpp:234`, `batch.cpp:667` («disable back buffer»), а
`WaterManager.cpp:244`/`ShadowManager.cpp:443` пишут `0x07` («disable alpha
corruption») и потом `0x0F`. Фолбэк превращал трафаретный проход в вывод цвета.

Фолбэк был ещё и неверен по факту: значение в модели состояний берётся из тени
менеджера (`renderstatesmanager.cpp:573 st.colorWriteEnable =
ShadowValue(D3DRS_COLORWRITEENABLE)`), а `ShadowValue` (`:204-218`) для
незаписанного состояния один раз читает устройство → там реальный дефолт D3D9
`0x0F`, а не 0. Биты `D3DCOLORWRITEENABLE_*` совпадают с `nvrhi::ColorMask`
(Red=1, Green=2, Blue=4, Alpha=8, All=0xF —
`Vendor/NVRHI/include/nvrhi/nvrhi.h:1042-1049`), маппинг не нужен. Исправлено на
дословную передачу; комментарий в `renderstatesmanager.cpp:531-535` поправлен
(там стояла та же ложная формулировка, она и оправдывала фолбэк).

### Как это было найдено (новый env `PW_VK_PJTRACE`)

`PW_VK_PJTRACE=x0,y0,x1,y1` (`nvrhibackend.cpp`, рядом с `PW_VK_SKIPPJ`): тот же
CPU-пересчёт вершин в экранные координаты (тень VB + `m_constShadow[csVertex]`),
но вывод **не** пропускается, а печатается полное состояние вывода + z/w-диапазон
вершин. Кап 8 строк. Ключевой замер (`PW_VK_PJTRACE=29,443,359,575`, лог
`PW/tools/render_ab-2026-10-21/ps15/pjtr_vk_client.log`):

```
VkPjTrace: ps=15 vs=14 vtx=6 prim=2 idx=0 lay=5 rt#89 proj=(28.5,442.5)-(360.1,576.0)
z=[0,0] w=[1,1] blend=1/5/6 sep=0 srcA=1 dstA=2 opA=1 cw=0 z=0/0 zF=4 stencil=1
aTest=0 cull=1 fill=3 vp=0,0 1024x768 sc=0,0..1024,768 scTest=1 scValid=1
rt=1024x768 bb=0 fmt=1 dep=0 back=0 PS c20=(1,1,1,1) c21=(0,0,0,0)
```

`cw=0` + `stencil=1` + `z=0/0` = трафаретный quad маски; `c20=1 c21=0` ⇒ выход PS
равен `COLOR0=FFFFFFFF`. Дальше сработала арифметика: `cw=0` в модели → `0x7` в
NVRHI → непрозрачный белый прямоугольник ровно 330x132.

Попутно: `PW_VK_TRACEORD_PS=<ps>` сам по себе молчит — блок требует ненулевого
`PW_VK_TRACEORD_TO` (или `FROM`/`AT`), кап 96 строк; ни в одном сохранённом логе
состояния плашки не было, поэтому `PJTRACE` и добавлен.

### Замер до/после (`shot_stats.py --defects`, t40/t80, Wine-стенд)

| | белых | крупнейший белый прямоугольник |
|---|---|---|
| до (`pjtr_vk`) | 43 585 (5,78 %) / 43 574 | 330x132 = 43 560 px в (29,443)-(359,575) |
| после (`cwfix_vk`) | 15 (0,00 %) / 2 | 4x2 / 1x2 |

Палитра: `RGB(255,255,255)` x43 562 исчез, уникальных цветов 150 573 → 154 284.
Побочный эффект — проступили слои, которые плашка закрашивала: иконки хотбара
(нижняя панель) и иконки миникарты. Жёлтые линии (п.3) и пурпурные цилиндры
остались.

### Четвёртая серия A/B (`PW/tools/render_ab-2026-10-21/ab5`, `run_ab5.sh`)

Три прогона одним бинарём: `ab5_dx` (DXVK), `ab5_vk` (seam, с фиксом), `ab5_dx2`
(повтор DXVK = полоса шума), 6 снимков x 20 с, DXVK с прогретым кэшем
(`DXVK_SHADER_CACHE_PATH=/tmp/dxvk-cache DXVK_SHADER_CACHE=512`). Сведение — по
`mapload: all map objects loaded` (dx 11:10:25.016, vk 11:12:40.688): метки
`t20…t100` оказываются друг от друга в 3…4 с игрового времени, повторная
DXVK-сторона при этом даёт \|D\| 0,0…0,3 → метки годны как сведение, сигнал реален.

| регион | \|D\|сигн | \|D\|шум | gross % | тон L1 | det DXVK / det Vulkan |
|---|---|---|---|---|---|
| plate_area | 11,3 | 0,0 | 52,5 | 101,0 | 12,3 / 6,7 |
| minimap | 7,5 | 0,3 | 32,8 | 124,4 | 26,4 / 10,7 |
| hud_top | 26,2 | 0,0 | 36,9 | 59,6 | 17,7 / 10,8 |
| center_world | 16,4 | 0,0 | 28,6 | 16,2 | 8,3 / 6,8 |
| hero_portrait | 10,8 | 0,0 | 44,7 | 31,5 | 35,1 / 20,8 |
| ability_bar | 3,6 | 0,0 | 52,5 | 37,0 | 28,2 / 18,2 |
| right_units | 9,8 | 0,0 | 57,3 | 73,6 | 10,6 / 9,0 |

`plate_area` — было \|D\| 75,8/233,3 и 90,6 % белых → стало 11,3 и 0,0 % белых:
п.1 очереди закрыт числом. Оставшийся сигнал в нём — B−R (у DXVK +39,9, у Vulkan
+10,8): под маской теперь лежит разный контент, а не белое пятно.

**Общая детальность (det) у Vulkan ниже примерно вдвое во всех регионах, включая
чисто UI (hero_portrait, ability_bar)** — отдельная зацепка: мягче не только мир,
но и UI, значит дело не в `gfx_scale3D`.

### Производительность (закрыт п.7)

`PW_FPS_STATS=20` на обеих сторонах одного и того же бинаря, Wine, RTX 3060,
1024x768: DXVK 100,74 / 129,99 / 131,36 / 130,79 / 128,74 к/с (всего 12 435 кадров
за 220 с); Vulkan 44,83 / 38,84 / 38,48 / 38,20 к/с (`VkStats: presents=3299
recreates=1 unsupported=0`). Итого **seam'овый Vulkan-путь в 3,2…3,4 раза медленнее
DXVK** — прежняя рабочая гипотеза «Vulkan быстрее» не подтверждена.

### Новые зацепки из логов этой же серии (в очередь не входили)

- **Миникарта (п.2) — это не атлас иконок.** В кадре Vulkan есть рамка миникарты и
  маркер области просмотра, но нет слоя рельефа (зелёная разведанная область) и
  иконок юнитов. Слой рельефа — отдельный render target: `Minimap.cpp:650
  CreateRenderTexture2D(… FORMAT_A8R8G8B8)`, в него рисуют на каждом шаге
  (`Minimap.cpp:1243 SmartRenderer::BindRenderTarget(m_targetTexture)`) и
  передают Flash'у как `miniMap_mc.miniMapAnim_mc.mapImage` (`:657`). Смотреть
  надо перевод прохода в offscreen-цель и чтение этой цели текстурой, а не
  `minimap_icons.dds`.
- **Альфа-тест не переведён:** `Состояния: аналога у Vulkan нет — альфа-тест
  (ALPHATESTENABLE) = 150 / 127 / 60 / 160` — 8 мест за прогон (`NoteStateIssue`,
  `nvrhibackend.cpp:5192-5194`). В D3D9 это fixed-function вырезание после PS; в
  Vulkan его нет ⇒ материалы рисуются целиком. Кандидат и на жёлтые линии (п.3),
  и на «пурпурные бочки».
- **`CreateSurface` умеет только три формата** (`nvrhibackend.cpp:7710-7721`:
  `bfD24S8`, `bfA8R8G8B8`, `bfX8R8G8B8`), всё остальное — E_FAIL. В прогоне один
  отказ: `формат 8 не переведён` = `bfA16B16G16R16F` (постэффекты/HDR?).
  Замечание: `SurfaceFormat()` (`:368-389`) покрывает форматов больше, чем
  `CreateSurface` — таблицы разъехались.
- **Автогенерация mipmap** — note 67 раз за прогон (`:4105`); текстура при этом
  создаётся (E_FAIL только на непереведённом формате), так что это не источник
  пропусков, но и mips нет (шаг 13).
- Счётчик для метрики шага 13 (п.6) есть: `VkStats: … unsupported=%u` (в этой
  серии 0) + строки `VkStub` (`:2755-2757`) и `stateUnsupported` (`:4828`, `:5130`).

## Пятая серия A/B (2026-10-21): гипотезы, отвалившиеся после фикса маски

### Что ОТВЕРГНУТО замерами (к ним не возвращаться)

- **Альфа-тест (кандидат п.3) — не причина.** `ALPHATESTENABLE` — fixed-function,
  в seam не переведён (`Src/Render/nvrhibackend.cpp:5197` только `NoteStateIssue`),
  но отсечение в живых материалах делает сам PS: по корпусу `Data/Shaders/**/*.shd`
  `OpKill` встречается только в `UberShaders/GrassMaterial.shd` (1 шт.) и
  `UberShaders/RoadMaterial.shd` (2 шт.) — он уже в SPIR-V и работает. Фиксированного
  альфа-теста, на который полагается кадр, нет ⇒ жёлтые линии и «пурпурные бочки»
  им не объясняются.
- **Mip-уровни не при чём.** `AUTOGENMIPMAP` в seam не реализован (note 67 раз за
  прогон, `:4105`), но прогон Vulkan с `PW_VK_SAMPLER_LOD0=1` (все сэмплеры берут
  mip 0, серия `ab8/lod0_vk`) даёт те же метрики, что и без него: пурпурного 1167 px
  против 1105 (t60), \|D\| миникарты 7,6 против 7,5, det 11,13 против 10,7,
  center_world \|D\| 16,4 в обоих. Автогенерация — реальный пробел шага 13
  (далёкий зум), но не источник текущих расхождений.
- **«Бочки» — не несвязанная текстура.** Замена в несвязанный слот с 2026-10-20
  заведомо чёрная `(0,0,0,0)` (`:2002-2035`; до этого фиксa именно она давала
  «пурпурные плашки»).
- **Глобальной гамма-кривой расхождения мира не объясняются.** Квантили каналов
  сведённых пар почти совпадают (center_world, медиана R 57 против 54, G 68 против
  66, B 80 против 76), ни `x^(1/2.2)`, ни `x^2.2` не подходят ⇒ расхождения по
  содержанию кадра (момент матча), а не по цветовому тракту.

### Пурпурные «бочки» — замер и первый трассировочный след

- Счётчик пурпурного (`R>110 & B>110 & G<R-40 & G<B-40`): Vulkan 1105 px (t60) /
  1279 (t100), bbox `(837,265)-(927,453)`; DXVK 0 / 1 px; повтор DXVK 0.
  Доминирующий цвет в окне — `(67,26,117)`, у DXVK там же `(32,48,68)`…`(38,55,73)`.
- `PW_VK_PJTRACE=837,265,927,453` даёт единственный попавший в область вывод:
  `ps=41 vs=40 vtx=96 prim=32 idx=1 lay=1`, `proj=(509.1,242.2)-(921.7,386.6)`,
  `z=[-0.0061,0.504] w=[-371.4,373.4]`, `blend=0 cw=15 z=1/1 zF=4 stencil=1 cull=2
  fill=3`, `PS c20=(1,1,1,1) c21=0` ⇒ модуляция цвета нейтральная, альфа-бленды нет,
  глубина и трафарет включены, часть вершин за камерой (`w<0` — клиппинг обязан
  совпасть с D3D9). Следующий шаг: `PW_VK_TEXGPU` по этому выводу (какая текстура
  привязана и что в ней в GPU) и сверка с D3D9-стороной.

### Новый env: `PW_VK_PJTRACE` — теперь экранная ОБЛАСТЬ, а не точный прямоугольник

Прежняя версия совпадала с прямоугольником вывода угол в угол (так и нашли плашку)
и для 3D-геометрии не срабатывала; наивное «пересечение bbox» ловит только
полноэкранные блиты (`ps=11 proj=(-208.6,-21.4)-(1231.8,788.6)` — они съедали весь
лимит строк). Теперь порог: `пересечение >= 30 % площади самого вывода` ИЛИ
`пересечение >= 50 % площади области И площадь вывода <= 4x площади области`;
лимит 24 строки. `PW_VK_SKIPPJ` остаётся строгим (иначе под пропуск попадут чужие
выводы).

### Рекордер PWRD: из чего состоит кадр (`ab6/rec_vk`, `parse_pwrd.py`)

Файл 135 741 440 Б, окно ~6 кадров: выводов 2999, шейдеров 116, буферов 188,
текстур 122, раскладок 13; период кадра 538 выводов. Состав целей: `1024x768` —
2426, `512x384` — 485, `200x201` — 48, `256x192` — 18, `205x205` — 12, `256x256` —
10. Выводы в `200x201` ЕСТЬ ⇒ offscreen-проход миникарты (`Minimap.cpp:650`,
`:1243`) жив и доходит до Vulkan; п.2 очереди переформулирован: расхождение —
тон/деталь слоя, а не пропущенный проход.
Важно для метрик: 3D-сцена рисуется в `512x384` (Wine-стенд, `gfx_scale3D=0.5`) при
backbuffer 1024x768, вьюпорт при этом `0,0 1024x768` (запись #427+) — в D3D9 так же,
расхождений не даёт, но мир заведомо мягкий ⇒ регионные метрики мира сравнимы
только на статичной сцене (обратный отсчёт выхода, п.5 очереди). Ограничение
рекордера: имена шейдеров generic (`MainPass_VSMain`/`MainPass_PSMain` на всех
2999 выводах), материал различается только индексом в заголовке.

### Производительность — главный риск ветки (замерен, не закрыт)

`PW_FPS_STATS` (четвёртая серия): DXVK 100,7…131,4 к/с, Vulkan 38,2…44,8 к/с при
`unsupported=0`, `recreates=1` ⇒ узкое место не пересоздание конвейеров. Нужен
профиль per-draw (сколько стоит `BuildPipelineState`, загрузка констант, привязка
текстур на вывод), прежде чем говорить о преимуществе Vulkan.

### Очередь (заменяет пп. 2 и 3 нумерации выше)

1. «Бочки»: `PW_VK_TEXGPU` по выводу `ps=41 vs=40` + сверка текстуры с D3D9.
2. Миникарта: тон/деталь слоя рельефа в цели `200x201` (проход жив) —
   `PW_VK_TEXGPU` цели до и после чтения её текстурой.
3. Профиль Vulkan-пути (в 3,2…3,4 раза медленнее DXVK).
4. `AUTOGENMIPMAP` (шаг 13): генерация mips blit-цепочкой, гейт `PW_VK_AUTOMIP`;
   на текущие дефекты не влияет (замер выше).
5. `CreateSurface`: перевести отсутствующие форматы (`bfA16B16G16R16F` отказывает,
   `:7710-7721`) и свести таблицы с `SurfaceFormat()` (`:368-389`).
6. Статичное сведение по обратному отсчёту HUD → пересъёмка эталонов
   `tools/render_shot/refs/`, порог \|D\| <5 на сведённых парах.
7. Метрика шага 13: `unsupported=%u` (0 в этой серии) + `VkStub` + доля выводов
   через новый путь (по PWRD: 2999 выводов за ~6 кадров, все через seam).

## Шестая серия A/B (`ab10`) и разбор кадров с участием пользователя (2026-10-21)

### Свежесть снимков — как проверять (часовые пояса)

Логи клиента пишут **UTC**, хост — MSK (UTC+3): расхождение ровно 3 часа, это и
есть источник путаницы «скриншоты не свежие». Проверка: `mtime` снимка (MSK) обязан
совпасть с последней строкой лога прогона + 3 ч. Замер серии: `ab10_dx` 15:31:37 /
лог 12:31:37; `ab10_vk` 15:33:37 / 12:33:36; `ab10_dx2` 15:35:38 / 12:35:37.
Сведение по обратному отсчёту HUD (кроп `480,125 +130x40`): DXVK `t20=0:49,
t40=0:28, t60=0:08`, Vulkan `t40=0:45, t60=0:24, t80=0:04` ⇒ пары `dx tN ↔
 vk t(N+20)`, остаток ~4 с.

### Что видно на кадрах (замеры)

- **Анимированная геометрия (герой) рисуется сильно уменьшенной**, аутлайн следует
  за ней. Жёлтого контура выделения в мире: DXVK 431–688 px, Vulkan 150–197 px.
  Статические модели (статуи, арена) на месте.
- **Часть геометрии не рисуется вовсе**: у «бочек» видны только 4 внутренних
  цилиндра (сплошные пурпурные), наружной металлической основы больших колб нет.
- **Жёлтые горизонтальные полосы** поперёк мира: `shot_stats.py --lines` — Vulkan
  12 строк (315 +25,6 / 309 −20,8 / 372 / 310 / 433 / 262), DXVK 1 строка.
- **Миникарта:** видна только часть иконок (портал есть; башни, база, мини-игра,
  арсенал, иконки героев — нет). Окрас: зелёный (Native terrain одной фракции)
  вместо синего слева снизу (вторая фракция) и коричневого в середине (нейтралы).
  Красных (иконки) px: 192 против 816; зелёных: 4052 против 1027.
- **Мягкость кадра** (тексты, тайлы) — отложена сознательно: предположение автора —
  blur-проход либо растягивание с пониженного разрешения.

### Ответы пользователя на разбор (m01357) — направляют разбор

1. Анимированная геометрия: «проблемы скорее всего с анимированной геометрией,
   аутлайн на герое виден, но он **сильно уменьшен**».
2. «Часть геометрии не рисуется или рисуется некорректно (с некорректным **fov,
   скейлом** или в принципе **позиционированием**). Рассматривать как related с 1».
   По бочкам: «4 цилиндра — внутренняя геометрия, снаружи должна быть металлическая
   основа для больших колб — тут только пропавшую геометрию искать по п.2».
3. **Эталон сам под вопросом:** «проблема на DXVK — дым от трубы в нижней части
   экрана выглядит светлым пятном, затемнённого экрана быть не должно, если взять
   эталонный кадр с tiny10. Сначала стоит выяснить, почему DXVK рисует картинку,
   отличную от стенда tiny10. Светлые полосы, возможно, окажутся сломанным дымом,
   который в DXVK создавал просто светлое пятно».
4. Миникарта: текстур слоя **три** (две фракции + нейтралы), между ними «хитрое
   смешивание»; если в оригинале это один дроп — должен быть биндинг сразу 3
   текстур.
5. Мягкость: «пока не обращать внимания, сначала собрать картинку в целом».
6. Зум камеры: «на мой взгляд идентичен на обоих кадрах», но «артефакты размера
   объекта персонажа смахивают на проблему FOV» — проверить, что зум корректен для
   всей геометрии; в константные буферы могут попадать не те значения, особенно
   если анимированная геометрия рисуется отдельно от статичной.
7. Где искать камеру: «есть конфиг **fixedTeamCam**, который частично контролирует
   рендер-камеру. Найдёшь его — найдёшь рендер-камеру, а с ней и всё остальное».
   Найден: `REGISTER_VAR("fixedTeamCam", g_fixedTeamCam, STORAGE_USER)` —
   `Src/PF_GameLogic/AdventureCameraController.cpp:17`, определение
   `Src/Shared/shared_data.cpp:3`; используется как `teamCorrectionYaw` в
   `AdventureCameraController.cpp:96`, `Scene/Scene.cpp:688`,
   `Render/ShadowManager.cpp:89,552`. Сама перспектива —
   `AdventureScreen.cpp:1654 SetPerspectiveTransform(fov, renderMode.width,
   renderMode.height, 0.5f, …)` → `Scene/Camera.cpp:136 CCamera::SetPerspectiveTransform`
   и `Camera.cpp:74-102` (корректор FOV по размеру экрана/вьюпорта,
   `fFOVCorrector`).

### Очередь по этой ветке (порядок пользователя)

1. Сначала — **почему DXVK отличается от нативного эталона tiny10** (затемнение
   экрана, дым от трубы светлым пятном): переснять tiny10 в тех же условиях и
   сравнить; возможно «жёлтые полосы» — это сломанный дым.
2. Анимированная геометрия и пропавшая геометрия = одна ветка: рендер-камера,
   FOV/скейл/позиционирование, значения в константных буферах; проверить
   раздельные пути статической и анимированной геометрии (`BonesMatrices` —
   `kSsboBridges` `nvrhibackend.cpp:467`, мост c24..c255 → SSBO, загрузка
   грязным диапазоном `:6739-6757`; проверка: CPU-проекция вершин до скиннинга
   `PW_VK_TRACEPROJ` против `PW_D3D_TRACEPJ` на той же геометрии).
3. Миникарта: 3 текстуры слоёв (фракции + нейтралы) и их смешивание; проверить
   биндинг трёх текстур и порядок (иконки поверх слоёв).
4. Мягкость/разрешение — позже.


### Константы камеры доезжают корректно — гипотеза «не те значения в c-банке» мертва (ab11–ab16)

Инструменты (обе стороны, симметричные): `PW_D3D_TRACEDRAW=мин,макс[,задержка_сек]`
(`Src/Render/smartrenderer.cpp`, в начале `NoteDipTrace`, читает
`GetVertexShaderConstantF(0, vc, 256)` с устройства) и
`PW_VK_TRACEDRAW=…` (`Src/Render/nvrhibackend.cpp`, в `DrawImpl`, читает
тень `m_constShadow[csVertex]`). Печатают `D3dDraw:` / `VkDraw:` с
W=c0..c3, V=c4..c7, VP=c8..c11, bnz (сколько c24..c255 ненулевых) и b24/b25.
Диапазон — по **числу вершин ИЛИ примитивов**: число вершин двух сторон
несопоставимо (у одного вывода dx `nv=1559`, seam `nv=1710`), примитивы —
надёжный ключ. Задержка в секундах от первого Dip'а обязательна: без неё лимит
трассы съедался за 0,6 с загрузки карты. Дедуп по хэшу c-банка печатает только
уникальные наборы, но **лимит считается по всем оттрассированным выводам**
(у скелетных выводы с костью на каждом выводе хэш разный; замер ab16 до правки —
100 тыс. строк, лог 97 МБ).

Замер пары одних и тех же скелетных выводов героя (prim=552 и prim=562,
`lay=11`, mat=`Shaders/UberShaders/BasicMaterial`), снятых при одинаковом
состоянии камеры (V совпадает дословно):

```
D3D9  W=(0.6,0.6,0.6,1)(1,1,1,1)(0.137255,0.137255,0.137255,0)(2,2,2,0)
      V=(0.866025,-0.5,-2.98023e-08,63.4409)…  bnz=139
      VP=(2.42404,-1.39952,-8.34178e-08,177.573)(1.52856,2.64754,2.14062,-509.937)
         (0.287075,0.497229,-0.819972,-23.2605)(0.286788,0.496732,-0.819152,-22.7373)
      b24=(-0.114133,-0.18364,0.207183,0.187359) b25=(-0.0510475,-0.175442,0.154337,0.159037)
Vulkan W/V/VP/b24/b25 — ТЕ ЖЕ, побайтово
```

Вывод: WORLD, VIEW, **VIEWPROJECTION** (c8..c11 — именно её использует
`BasicMaterial.hlsl:164 mul(float4(WPos,1.0), ViewProjection)`, а не View) и
кости приходят в seam в идентичном виде. Эксперимент `PW_VK_SSBO_FULL=1`
(загружать мост BonesMatrices целиком, а не грязным диапазоном) — на размер
модели НЕ влияет (жёлтый контур 433 px против 440 px, светимость кадра
совпадает до 0,1). Значит дефект «герой сильно уменьшен» лежит НИЖЕ тени
констант: либо загрузка в UBO/SSBO (байты не те на GPU), либо вершинные
атрибуты/раскладка, либо математика SPIR-V VS.

Следующий шаг (ab17): короткий окно-рекорд PWRD (`PW_VK_RECORD_AT=60
PW_VK_RECORD_MAX=700`) → дамп SPIR-V VS/PS и раскладки `lay=11` вывода героя
(`tools/render_replay/parse_pwrd.py --draw N --dump-dir`) и статическое
сравнение с DXBC того же варианта `BasicMaterial.shd` — тот же метод, что
закрыл плашку (см. «Плашка закрыта»). Отдельно проверить наложение диапазонов в
`kSsboBridges` (`nvrhibackend.cpp:465-473`): `hexParams` = c22..c61 ПЕРЕКРЫВАЕТ
`BonesMatrices` = c24..c233 (в `GlobalMasks.h` `VSHADER_LOCALCONST2 = c22`,
`BONES = c24`) — запись hexParams помечает грязным и костяной мост, и наоборот;
это лишние загрузки, но при проходе, который НЕ пишет свой массив, SSBO
остаётся от чужой записи (кандидат в дефект миникарты, где цвет шестигранников
идёт из `hexParams` — `Src/Render/Shaders/NatureMapMaterial.hlsl:24`,
поставщик `Src/Terrain/NatureMapVisual.cpp:375`).

### Выводы героя — инстансный скиннинг; что видно из рекорда (`rec_ab6.pwrd`, вывод #281)

Разбор вывода с `primCount=562` (`lay=9`, rt=1024x768, vtx=1686) — тот же
материал, что печатает D3D9-трасса (`Shaders/UberShaders/BasicMaterial`):

- SPIR-V VS (`spirv-dis` из блоба рекорда) объявляет входы ровно четыре:
  `%in_var_POSITION` (v3float), `%in_var_NORMAL` (v3float), `%in_var_TEXCOORD0`
  (v2float), `%in_var_BLENDINDICES0` (**скалярный `int`**, не ivec4) — т.е. это
  вариант скиннинга «одна кость на инстанс» (`NewInstancing` /
  `LIGHTINGINSTANCEDSH` в `BasicMaterial.hlsl`), а не 4-весовой путь
  `_commonSkinning.hlsl`. **BLENDWEIGHT0 у этого VS вообще нет.**
- Раскладка: POSITION/NORMAL/TEXCOORD0 в потоке 0 (stride 32), а
  `BLENDINDICES0` — `bufferIndex=8`, `elementStride=4`, `isInstanced=1`.
  Слот 8 — инстансный (`kMaxVertexStreams = 8`, `nvrhibackend.cpp:120`;
  `SetStreamSource` с `slot >= 8` возвращает `D3DERR_INVALIDCALL` :3358,
  поэтому инстансные данные идут отдельным путём — `kInstanceStream`,
  `d.instSrc`, `SlotOfStream`, режим счётчика `BindInstanceCounter` в
  `BuildInputLayout` :3688-3790).
- Стримы вывода в рекорде: `[0]=buf52 stride 32`, `[1]=buf1 stride 4`,
  `[2]=buf2 stride 4`, остальные нет. Т.е. два по 4-байтных потока на кадр —
  кандидаты на индекс кости + что-то ещё.

Куда это ведёт: размер/позиция такой геометрии задаётся **костью из SSBO по
индексу из инстансного потока**. Раз c-банк (включая b24/b25) доезжает
идентично (замер выше), а `PW_VK_SSBO_FULL=1` ничего не меняет, проверять надо
цепочку «инстансный поток → индекс → выбор кости в SPIR-V»: совпадает ли
`int`-вход с тем, что кладёт клиент (в D3D9 это, вероятно, `D3DDECLTYPE_UBYTE4`
/ `FLOAT` в потоке 8), и не подгоняет ли `FormatIntClass` (`:3766`) класс
формата в другую сторону. Инструмент — дамп инстансных байтов (`buf1/buf2` уже
выгружаются парсером) против DXBC того же варианта.

## Седьмая серия A/B (`ab18`–`ab25`): три причины найдены — SSBO-мосты, `int`-вход BLENDINDICES, массив сэмплеров

Дефекты п.1/п.2 очереди («модель героя сильно уменьшена», «часть геометрии не
рисуется») закрыты **двумя** правками; дефект миникарты «окрашивается в один
цвет» — **третьей**. Все три — про доставку данных в Vulkan, не про камеру:
гипотеза пользователя «в константные буферы попадают не те значения» проверена
выше и мертва (W/V/VP/кости доезжают побайтово).

### 1. SSBO без моста и разъезд stride: `instanceData`, `data`, `BonesMatrices`

- Инвентарь uniform-буферов по SPIR-V всех 116 шейдеров окна рекорда
  (`rec_ab6.pwrd`, `parse_pwrd.parse` → `sh[id]['spv']` → `spirv-dis`): кроме
  `pwCBankVS/PS` буферов ровно три — `data` (шейдер 0), `BonesMatrices`
  (8 шейдеров), `instanceData` (шейдеры 59, 61). Моста у них не было —
  бэкенд подставлял `d.dummySsbo` (нули): в логах прогонов было
  `NvrhiBackend: SSBO без моста — подставлен dummy — instanceData`.
  Нулевая матрица экземпляра = геометрия схлопнута в точку/полосу.
- Второй дефект того же механизма: у `BonesMatrices` в SPIR-V **ArrayStride 64**
  (`OpDecorate %_runtimearr_mat4v3float ArrayStride 64`, MatrixStride 16), а в
  таблице шва стояло 48 — клиент кладёт в c-банк 3 регистра на матрицу
  (`renderer.h:179 SetVertexShaderConstantsMatrix43` → `SetVertexShaderConstants(start, 3*n, p)`,
  вызов `Src/Render/SkeletalMesh.cpp:113`), а шейдер читает элемент по `i*64`
  → матрицы костей съезжают.
- Правка: `struct SsboBridgeDef { name, regBase, regCount, stride, byteSize,
  arrayStride, srcStride, matCols }` (arrayStride — ожидаемый ArrayStride из
  SPIR-V, различает варианты с одним именем: у `instanceData` две раскладки —
  `InstanceData{float4x3; float4 sh[7]}` c20 stride 176 и голый `float4x3`
  c29 stride 64, `GlobalMasks.h:96 INSTANCE_DATA=c22`… `:96 INSTANCE_DATA=c20`,
  `VSHADER_LOCALCONST9=c29`, `MAX_INSTANCES_COUNT=19`); добавлены мосты
  `instanceData` (оба варианта) и `data` (`ParticleFXMaterial.hlsl:18-20`,
  `INSTANCE_DATA` = 5 float4 = 80 Б, `register(VSHADER_LOCALCONST1)`=c21,
  `PFX_MAX_INSTANCES=47` → c21..c255, поставщик `Src/Render/ParticleFX.cpp:756`).
- **Между c-банком и SPIR-V — транспонирование 3x4 → 4x3.** Замер ab19
  (`PW_D3D_TRACEDRAW` / `PW_VK_TRACEDRAW`, дамп c20..c31 одного вывода):
  `c20=(-0.690549,-0.723285,0,68.0808) c21=(0.723285,-0.690549,0,181.441)
  c22=(0,0,1,7.511)` — 4-я компонента каждой группы = координаты мира ⇒
  группы в c-банке = **столбцы** матрицы 4x3 (перенос в последней строке,
  `Matrix43` = 12 float подряд, `Src/System/matrix43.h:10`). Память
  `mat4v3float` (MatrixStride 16) = 4 столбца по 3 float, где столбец r =
  **строка** r HLSL-матрицы. Переупаковка: `d4[r*4+i] = s4[r + 4*i]`,
  `d4[r*4+3]=0`; хвост элемента (SH-константы, 112 Б) переносится дословно в
  `dst + matCols*16`. Прежний memcpy 12 подряд идущих байт давал
  транспонированную матрицу → «размазанный шип» вместо модели.
- Загрузка — по диапазону **элементов**, пересекающих грязный диапазон
  регистров (`e0=(f-regBase)*16/srcE`, `e1=((l-regBase)*16+srcE-1)/srcE`);
  первая версия (весь буфер на каждый вывод) дала 4,08 ГБ SSBO-загрузок за 30 с
  и fps 26,20 (было 38-45), после ограничения диапазоном — 37,66.
- Замер ab20 (переупаковка): Vulkan `model=6869…6902` px против DXVK
  `6389…7160` (было 549-551), пурпур `233/192/217/161` против DXVK 0 (было
  1096-1397), светимость 72,3-73,3 против DXVK 70,3-71,1 (было 59,7).

### 2. `int4 Indices : BLENDINDICES0` — DXC делает int-вход и `OpIMul`

- Герой рисуется **4-весовым скиннингом** (`lay=11`: POSITION/NORMAL/TANGENT/
  BINORMAL/TEXCOORD0/BLENDWEIGHT FLOAT4@56/BLENDINDICES D3DCOLOR@72, stride 76),
  материал — `DropMaterial` (SPIR-V побайтово совпал с v2-блобом
  `Data/Shaders/UberShaders/DropMaterial.shd`, вариант 1890/1895).
- Атрибут D3DCOLOR seam маппит в `nvrhi::Format::BGRA8_UNORM`
  (`VtxFormat`, `nvrhibackend.cpp:289-307`) — нормализованный [0,1], поэтому
  шейдер обязан получать **float** и восстанавливать байт множителем 255.
- Старый SPIR-V: `%in_var_BLENDINDICES0 = OpVariable %_ptr_Input_v4int Input`,
  `%62 = OpIMul %v4int %30 %61` — fetch приводит UNORM к целому, индекс
  `255*23 = 5865` при `MAX_BONES = 70` (`GlobalMasks.h:88-89`) → чтение мусора
  → модель «размазывает» на сотни пикселей, жёлтый контур `SilhouetteMaterial`
  тянется за ней.
- Источник — HLSL: `Src/Render/Shaders/_commonSkinning.hlsl:11`
  `MakeSkinnedMatrix(float4 weights, int4 idxs) { int4 indices = 255 * idxs; }`
  и `int4 Indices : BLENDINDICES0;` в `BasicMaterial.hlsl:59`,
  `BasicFXMaterial.hlsl:29`, `BasicMaskMaterial.hlsl:18`, `DropMaterial.hlsl:24`,
  `PaintMaterial.hlsl:15`, `ShadowMaterial.hlsl:26`, `SilhouetteMaterial.hlsl:12`.
  FXC (SM3) кладёт вершинный вход в float-регистр и умножает в float — на D3D9
  дефекта нет; DXC при int-объявлении генерирует int-вход.
- Правка: вход `float4 Indices : BLENDINDICES0;`, в `_commonSkinning.hlsl`
  параметр `float4 idxs` и `int4 indices = (int4)(255.0f * idxs);`. Не тронуты
  `int InstanceId : BLENDINDICES0;` (инстансный индекс, атрибут SHORT2 → int
  корректен) и `DebugMaterial.hlsl`, `AOEMaterial.hlsl` (не зовут MakeSkinnedMatrix).
- Перегенерация: `python3 tools/shader_build/mkshd2.py --live
  tools/shader_build/live_indices-2026-10-15-union.json --cbank --materials
  BasicMaterial,BasicFXMaterial,BasicMaskMaterial,DropMaterial,PaintMaterial,ShadowMaterial,SilhouetteMaterial`
  → **v1 (SM3 DXBC) слой побайтово идентичен** во всех контейнерах (D3D9-путь не
  затронут); новый SPIR-V: `%59 = OpLoad %v4float %in_var_BLENDINDICES0`,
  `%60 = OpVectorTimesScalar … %float_255`, `%61 = OpConvertFToS %v4int` ✓.
- Пайк контента стенда: `python3 tools/shader_pile/mkpile.py build --root
  pw/branches/r1117/Data --out /tmp/zzz_new.pile --mask '*.shd' --mask '*.spv'
  --only-v2` → 60 записей (16 `.shd` с v2-слоем + 44 `.spv` OnlineShaders),
  263 707 Б; `mkpile.py list` по умолчанию показывает 40 записей — нужен
  `--all`.
- Замер ab23: модель героя в Vulkan — та же поза, размер, контур (глазами);
  `model=6383…6516` против DXVK `6402…6464`; металлическая основа бочек и
  фиолетовая жидкость есть на обеих сторонах (дефект «пропавшая геометрия»
  закрыт; метрика barrel теперь меряет насыщенность фиолетового, а не
  отсутствие геометрии).

### 3. Миникарта: seam биндил ОДНУ текстуру на все элементы массива сэмплеров

- Слой фракций миникарты — **OnlineShader** `Src/Render/OnlineShaders/NatureMap.psh`
  (в контенте `Data/Shaders/OnlineShaders/NatureMap.psb` + `.psb.spv`), а не
  `Shaders/NatureMapMaterial.hlsl` (ложная зацепка: hexParams в c-банке этого
  вывода никто не заполнял). Источник:
  `sampler diffuseMap[3] : register(s0); sampler blendMask : register(s3);
  sampler fogOfWarMask : register(s4);` →
  `totalColor = colorA*blendCoeff.r + colorB*blendCoeff.b + colorN*blendCoeff.g`,
  `FoWScale = saturate(1 - fogOfWar.a*alphaScale)` — ровно 3 текстуры слоёв
  (две фракции + нейтралы), как и предполагал пользователь.
- Клиент биндит 5 сэмплеров в `PF_GameLogic/Minimap.cpp:1318-1323`
  (s0/s1/s2 = `m_natureTextureA/B/N`, s3 = наткарта, s4 = видимость), вызов —
  `Minimap.cpp:1328 RenderScreenQuad(..., Params("NatureMap"))`.
- Рекорд подтверждает: у вывода слоя (`vs=14 ps=49`, rt=200x201) resItems
  `(set,binding,kind,dim,count)` = `(2,0,1,1,3)` — **массив из 3** на binding 0,
  плюс `(2,3,1,1,1)`, `(2,4,1,1,1)`, сэмплер `(3,0,0,0,1)`; в `samplers[]`
  (16 слотов) у этого вывода слоты 0/1/2 = текстуры **53/54/55** (разные!).
- Дефект: `FindOrCreateTexSet`/`FindOrCreateSamplerSet` брали ресурс одного
  слота (`d.texBound[it.binding]`) и писали его во **все** элементы массива
  (`bi.arrayElement = e`), а подписи `TexSignature`/`SamplerSetSignature`
  учитывали только `it.binding` — смена текстуры во втором слоте подпись не
  меняла. Итог: `totalColor = texA*(r+b+g)` → один оттенок с передержкой.
- Правка: элемент e массива = D3D9-слот `it.binding + e` (текстура, сэмплер и
  обе подписи), разрешение текстуры/куба/заметки перенесены внутрь цикла по e.
- Замер ab24 (`mmask` = зелёные px в кропе миникарты (810,555)-(1021,739)):
  Vulkan **4302 → 198** (DXVK 25…202); глазами — синие/коричневые/тёмно-зелёные
  участки совпали с DXVK (было: всё залито ярко-зелёным).
- Что осталось в миникарте: **иконки** (башни, база, мини-игра, арсенал, герои;
  виден только портал). Выводы иконок есть (`vs=19 ps=20 lay=7`,
  `vtx=180 prim=60` и др., rt=200x201), слот 0 = текстура 37 (2048x2048,
  seamFormat 1 = A8R8G8B8), состояние `alphaBlendEnable=1 separateAlphaBlend=1
  srcBlend=5(SRCALPHA) destBlend=6(INVSRCALPHA) srcBlendAlpha=1 destBlendAlpha=2`
  (нумерация D3DBLEND: ZERO=1, ONE=2, SRCCOLOR=3, INVSRCCOLOR=4, SRCALPHA=5,
  INVSRCALPHA=6, DESTALPHA=7 — по ней это обычный альфа-бленд
  `out = src*srcA + dst*(1-srcA)`, т.е. при `srcA≡0` иконка невидима).
  Проверяется альфа атласа (GPU-readback цели 200x201 и атласа, `ab25`).

### Производительность (замер ab24, тот же бинарь на обеих сторонах)

DXVK 127,45…129,99 к/с; Vulkan 38,15…39,67 к/с (`VkStats: presents=3299
recreates=1 (surface 0) unsupported=0 swap=1024x768 device=NVIDIA GeForce
RTX 3060`). Просадка в 3,2…3,4× не закрыта — это главный риск ветки, профиль
ещё не снимался (подтверждено и в ab26: 36,67…37,24).

### Иконки миникарты: разбор атласов (ab26) — НЕ закрыто, механизм не найден

Инструмент: рекордер выводов + GPU-readback в ОДНОМ прогоне
(`tools/render_ab-2026-10-21/ab26/run_rec.sh`: `PW_VK_RECORD_AT=90
PW_VK_RECORD_RT=200x201 PW_VK_RECORD_MAX=600` + `PW_VK_TEXGPU_ALL=1
PW_VK_SHOT_TIME=90`; файлы пишутся в cwd клиента
`/home/rekon/PW/wine-client/Game/Bin/`, а не в OUT скрипта). Итог:
`rec_mm.pwrd` 32 145 408 Б (599 выводов, 6 шейдеров, 53 текстуры, 2
раскладки), `rec_mm_texgpu.bin` — 200 записей (кап), fps 36,67…37,24.

- Выводы иконок: `vs=4 ps=5 lay=1`, `vtx/prim = 30/10, 6/2, 180/60, 12/4`;
  раскладка lay=1 = POSITION FLOAT2 + TEXCOORD FLOAT2 + COLOR D3DCOLOR,
  стрид 20 (совпадает с `PF_GameLogic/Minimap.cpp:579-584`), все вершины
  COLOR=FFFFFFFF. Слот 0 = атлас 2048x2048 A8R8G8B8, сэмплер
  POINT/POINT, mip NONE, BORDER, maxMipLevel 0 — сэмплер корректен.
- Сопоставление id рекорда с GPU-дампом — по SHA1 первых 8 МБ (запись
  рекорда = CPU-зеркало seam'а): rec tex7 = dump#94, tex8 = dump#97,
  tex9 = dump#96 ⇒ загрузка атласа на GPU побайтово верна (для первой
  половины; данные текстур в рекорде усекаются 8 МБ из 16 МБ).
- UV-прямоугольники иконок (брать из `startVtx`!): draw n=3 →
  uv=(0.6226,0.9937)-(0.6284,0.9995) → атлас (1275,2035)-(1288,2048):
  в привязанном tex7 контент ЕСТЬ (122/169 ненулевых, средний BGRA
  (122,98,62,136)); draw n=5 (`vtx=180 prim=60` — основная пачка башен) →
  uv=(0.8848,0.9927)-(0.8906,0.9985) → (1812,2033)-(1825,2046): в
  привязанном tex7 ПУСТО (1/169), а в атласе dump#93 (тем же прогоном, тем
  же кадром, но другими выводами НЕ привязан) там 124/169 со средним
  (164,115,31,50) — оранжевый контент иконки.
- Систематической потери нижней половины при загрузке НЕТ: по всем семи
  атласам 2048x2048 нижняя половина заполнена сильнее верхней
  (напр. #96: верх 470 788 / низ 1 129 487 ненулевых по альфе).
- Клиент биндит атлас явно на каждый чанк иконок:
  `Minimap.cpp:1288 renderMaterial.GetRenderMaterial()->GetDiffuseMap()
  ->SetTexture( chunk.texture )`, а `chunk.texture` — результат
  `cache->PlaceTextureToAtlas(...)` (`Minimap.cpp:744`, `:1226`). Значит
  «не тот атлас» либо привязывает seam, либо упаковка атласа разошлась с UV.
- Подозрение, не проверено: атлас правится с ЧУЖОГО потока — в логах
  `VkThread: UnlockTexture2D — новый поток 0x00000170 (владелец 0x00000024)`,
  при этом `UnlockTexture2D` делает `writeTexture` сразу
  (`nvrhibackend.cpp:4628-4691`); счётчик кольца к концу прогона — 27,6 млн
  событий, т.е. атласы переливаются постоянно. Гонок между записью атласа из
  потока загрузки и его использованием в кадре — кандидат на потерю части
  содержимого.
- Следующий шаг (дешёвый → дорогой): (а) в seam логировать для выводов
  миникарты клиентский указатель текстуры слота 0 и сопоставить с
  указателями, выданными `PlaceTextureToAtlas`; (б) проверить, не приходит ли
  `SetTexture` с тем же указателем, что и в предыдущем выводе (т.е. не
  читается ли устаревший `d.texBound[0]`); (в) поднять кап записи текстур в
  рекорде, чтобы видеть вторую половину атласа.

### Гейты и коммиты пачки (2026-10-22)

- `727b30396 fix(shaders): вход BLENDINDICES — float, а не int` (8 `.hlsl` +
  4 `.shd`), `767ac0288 fix(render): seam — SSBO-мосты instanceData/data,
  переупаковка матриц, массив сэмплеров по слотам` (`nvrhibackend.cpp`
  +265/−41, `smartrenderer.cpp` +26/−3).
- Нативная сборка на tiny10: `[3/3] Linking CXX executable PW_Game.exe`,
  `C:\pwbuild\pwc\build\PW_Game.exe` 16 041 984 Б, check_exe OK (x64 PE32+,
  Characteristics 0x0022, RT_MANIFEST id 1, FileVersion 2.16.1.0), RC=0.
  Синк — zip С префиксом `pw/branches/r1117/` в `C:\pwbuild\pwc`, контроль
  MD5 (nvrhibackend 149519c511e620b9dd8c8c1e9f20854a, smartrenderer
c16c1d109a2e0fa463e91d0aa75b026a).
- E2E на tiny10 (`PwE2E64W`, каталог логов `2026.10.21-20.56.34`): login
  `code=Success(5), uid=131` → `Entered lobby` → `Starting game session
  gamesvc` → юнитстатистика → бой (`Frame time histo: [2.0, 10.0]: 56`).
  unitstats: 73 записи, **70 общих ключей с прежней сборкой, 0 расхождений
  значений** (разница только по составу заспавнившихся юнитов).
- Нативный эталон кадра (auto_screenshot, `pw_auto_bb_3000/3100.png`,
  1920x1080, сняты в этом же прогоне; лежат в
  `tools/render_ab-2026-10-21/native-e2e-2026-10-22/`): светимость 78,1/78,8,
  RGB (77,82,76) — нейтральный. Wine-стороны темнее и синее: DXVK lum
  70,2…71,0 RGB (67,77,86), Vulkan lum 71,6…72,7. Сравнение трёх кадров
  (`/tmp/native_dx_vk.png`): геометрия (арена, портал с шестернями, бочки с
  металлическим основанием, модель героя с аутлайном) в Vulkan-пути теперь
  совпадает с DXVK; в нативном кадре открыт модал «Выход» — кадры не
  сводятся по времени, вывод по цветовому балансу требует выравнивания
  конфигов (ВМ 1920x1080 fullscreen + `gfx_skip_mip_levels=1`, Wine
  1024x768 windowed).

### Восьмая серия A/B (`ab27`–`ab33`): трасса блокировок атласа, грабля wineserver, паритет модели

**Инструменты (env в seam'е, `Src/Render/nvrhibackend.cpp`).**

- `PW_VK_RECBLOBMAX=<МБ>` — потолок блоба в рекорде PWRD (был жёсткий
  `kRecBlobCap = 8 MiB`): из-за него в рекорд попадала ровно ВЕРХНЯЯ половина
  атласа 2048x2048 (16 МБ), а нижние строки (UV y≈0,99) были недоступны —
  именно это давало «пустые» UV-прямоугольники в первых замерах. Строка
  `VkRecTex: rec#k = seam#N WxH fmtM mipsK` печатается для id < 64 (было 24) —
  сопоставляет id рекорда с индексом seam'а.
- `PW_VK_TEXLOCK="WxH[,WxH…][,задержка_сек]"` — трасса `LockTexture2D` /
  `UnlockTexture2D` для текстур указанного размера:
  `VkTexLock: <lock|unlock> #u WxH nz=N nz64=N hash=NNNNNNNN` (ненулевые пиксели
  всего зеркала и нижних 64 строк; FNV-хэш печатается двумя половинками —
  `DebugTrace` не понимает `%llu` и съезжает аргументами на `%zu`).
- `PW_VK_TEXLOCKTILES=1` — дополнительно diff с ПРОШЛЫМ зеркалом той же текстуры
  по тайлам 32x32 через `memcmp` (`static std::map<unsigned,
  std::vector<unsigned char> > s_prevM`): `VkTexLockTiles: #u WxH [ tx,ty …]
  изменено тайлов=N`. Показывает, КУДА именно клиент пишет иконку. Первая
  версия хэшировала всё зеркало дважды (~32 МБ процессора на каждый Unlock при
  пачке ~1600 блокировок) — клиент не доходил до матча, отсюда `memcmp` и
  ранний `return` до расчёта nz/hash.

**Замер ab30 (трасса без задержки, 1638 строк `VkTexLock`).** События
lock/unlock по seam-индексам атласов: #18 — 552, #93 — 546, #95 — 208,
#94 — 196, #96 — 80, #97 — 20, #98/#99 — по 18; вся упаковка — первые ~30 с
сессии. `nz` монотонно растёт на каждом Unlock, **падений `nz` — ноль** ⇒
зеркало атласа при повторной блокировке не обнуляется и не теряет ранее
размещённые иконки — гипотеза «`UnlockTexture2D` пишет полное зеркало и затирает
предыдущие размещения» мертва.

**Статика клиентского упаковщика (вины клиента не найдено).**
`Src/Render/UITextureAtlasItem.cpp:23-91 PlaceImage(data, w, h, uv1, uv2,
swizzle)`: полка по `freeSpace`, `if ((bestY + heightWithMargin) > size) return
false;`, затем `bestX += 1; bestY += 1;`, запись в
`lockedRect.data[((bestY+j)*size + bestX+i)*4]` при блокировке ВСЕГО уровня
(`LockRect(0, LOCK_DEFAULT)`), `freeSpace[bestX+i] = bestY + height`,
`UnlockRect(0)`; `uv1/uv2 = bestX/size … (bestX+width)/size` ⇒ максимальная
v2 = 2047/2048 = 0,99951 (прежний «rect y1=2048» — off-by-one в моём скрипте,
не баг клиента). `Src/Render/UITextureCache.cpp:95-117 PlaceImageToAtlas`
возвращает тот атлас, который принял размещение, `:142-166 PlaceTextureToAtlas`
пробрасывает его наружу, а `Src/PF_GameLogic/Minimap.cpp:715-750` сохраняет в
`desc.texture` ⇒ гипотеза «иконка размещена в атлас A, а привязан атлас B»
статикой НЕ подтверждена.

**Поправка к замеру атласов (ab29).** Паттерн альфы иконки из
`tex#7 rect(1786,2033)` (100 ненулевых из 169) искался по всем четырём атласам
рекорда — находится ТОЛЬКО в #7 ровно по своим UV; в #8/#9/#10 точных
совпадений нет (лучшее частичное 71-78/169 в ±160 px). Значит прежний вывод
«тот же UV заполнен в другом атласе» — совпадение (разные атласы пакуют разные
иконки в одинаковые слоты). Итог: привязанный атлас действительно не содержит
свою иконку по её UV, хотя трасса блокировок показывает, что пиксели при
размещении в зеркало долетают. П.2 очереди НЕ закрыт.

**Грабля стенда (объясняет «зависания» ab30/ab31).** Остаточный `wineserver`
префикса `~/.wine-pw` (в паре с неубитым `PW_Game.exe <defunct>`) глушит
следующий запуск: клиент выходит за секунды, каталог сессии в
`~/Документы/My Games/Prime World Classic/Session/Logs` НЕ создаётся, wine.log —
одна строка `Memory leaks info START`, снимки 230 Б (чёрные). Лечение:
`WINEPREFIX=$HOME/.wine-pw wineserver -k; sleep 3` перед запуском — добавлено в
`finish()` стенда (`tools/render_ab-2026-10-21/ab33/run_ab.sh`). Прежний вывод
«клиент зависает на лобби из-за стоимости трассы» неверен: `UniServerApp`
(`/tmp/srv_ab.log`) в это время здоров, `Status: 0 connections`.

**A/B ab33 (обе стороны одним бинарём 16 051 712 Б, 6 снимков x 20 с,
`mapload=1 lobby=1` у обеих).**

| метрика | DXVK t20…t100 | Vulkan t40…t100 |
|---|---|---|
| hero (жёлтый контур, кроп 380,230-680,450) | 430/430/430/430/446 | 214/207/195/198 |
| model (светлые px там же) | 6444/6429/6448/6434/7271 | 6316/6408/6459/6211 |
| barrel (пурпур по кадру) | 0 | 234/223/231/165 |
| mmask (зелёный, 810,555-1021,739) | 25/25/25/128/176 | 198/198/198/362 |
| lum | 70,1…71,0 | 72,3…72,7 |

- Площадь модели в паритете (в ab18 было 549 против ~6400) — фикс
  `BLENDINDICES` (float-вход) и SSBO-мосты держатся; `hero` ниже DXVK из-за
  общей мягкости кадра, не геометрии.
- `mmask` после фикса массива сэмплеров: 4302 → 198 (DXVK 25-176) — пересвет
  зелёным ушёл, осталось ~2× расхождение.
- Производительность: DXVK `fps 74,89 / 112,65 / 114,19 / 111,85 / 108,88`
  (10 452 кадра за прогон), Vulkan `fps 35,13`, `presents=2999` ⇒ Vulkan-путь
  в ~3 раза медленнее DXVK.
- Регионы (`tools/render_shot/region_ab.py`, БЕЗ третьей серии-шума ⇒ |D| мира
  включает разницу фаз матча, гейт «|D| > 2xшум» не применяется): `plate_area`
  |D| 3,0 и `wA=wB=0,00` (белой плашки нет с обеих сторон), `minimap` |D| 3,5
  (в ab5 было 7,5), `center_world` |D| 20,0 и `hud_top` 26,2 — последние два
  требуют сведения по обратному отсчёту HUD, а не по секундам прогона.

### Очередь (после ab33)

1. Иконки миникарты — см. «Иконки миникарты: разбор атласов (ab26)» и
   «Восьмая серия»: трасса `PW_VK_TEXLOCK` / `PW_VK_TEXLOCKTILES` рабочая,
   нужен валидный прогон (со `wineserver -k`) и сверка тайлов с UV-прямоугольниками
   из рекорда — `tools/render_ab-2026-10-21/ab31/correlate.py`.
2. Жёлтые горизонтальные полосы в мире (в Vulkan 12 строк против 1 у DXVK) и
   зелёная диагональная полоса.
3. Почему DXVK рисует иначе, чем нативный эталон tiny10 (дым светлым пятном,
   затемнение) — выровнять конфиги: ВМ `gfx_resolution=1920x1080`,
   `gfx_skip_mip_levels=1`, `gfx_fullscreen=1`; Wine 1024x768 windowed.
4. Профиль Vulkan-пути (35 против ~110 к/с, ab33).
5. Перегенерация пайка шейдеров на ВМ (`tools/shader_pile/{gen_shaders.sh,
   build_pile.sh}`) — в стенде хостa пайк перегенерирован
   (`wine-client/Game/Packs/zzzresult.pile`, бэкап
   `zzzresult.pile.bak-2026-10-22-pre-skinfix`); `.shd` BasicMask/Paint/Shadow
   не пересобраны (их нет в снимке живых индексов) — при появлении этих
   материалов в Vulkan-пути нужен их v2-слой.
6. Метрика шага 13 (доля DIP'ов через новый путь): счётчик `unsupported=%u`
   в `VkStats` + `VkStub`-строки.
7. Пересъёмка эталонов `tools/render_shot/refs/` (частично закрыто свежими
   нативными кадрами `native-e2e-2026-10-22/`).

### RenderDoc под Wine: капча кадра на заданной секунде матча (2026-10-22)

Захват `.rdc` кадра клиента под Wine **получен и воспроизводим**. Ключевое
отличие от попытки 2026-10-17: слой включает **хостовый Vulkan-загрузчик**
(`VK_ADD_IMPLICIT_LAYER_PATH` + `ENABLE_VULKAN_RENDERDOC_CAPTURE=1`), а не
хуки функций внутри процесса. При загрузке слоя отрабатывает
`library_loaded()` (`renderdoc/os/posix/posix_libentry.cpp`) — поднимает
target-control-сервер (порты 38920–38927) и читает `RENDERDOC_CAPFILE`, поэтому
`LD_PRELOAD` и вызов `RENDERDOC_GetAPI` из гостя не нужны вовсе.

Замер (стенд `wine-client/rdcap_match.sh`, RTX 3060, 1024x768):
маркер клиента `PWRDCAP 1 seq=1 cur=1855 frame=1945 sec=90 api=0` →
`rdwatch.py` шлёт `QueueCapture(1945)` → `Finished capture, Frame 1945` →
`pw_frame1945.rdc` 178 679 558 Б («Captured Vulkan frame with 418.6 MB capture
section in 3.42 с»); `renderdoccmd thumb` даёт живой кадр (база, герой,
миникарта). Счётчик кадров слоя **равен** счётчику present'ов клиента
(`VkSum: presents=N` ↔ `Finished capture, Frame N`), т.е. кадр берётся точно.
Под слоем прогон идёт на ~20–22 к/с.

Что добавлено в клиент (`Src/Render/nvrhibackend.cpp`, всё env-гейчено,
по умолчанию выключено):

- `Vendor/renderdoc/renderdoc_app.h` (MIT, v1.x, 36 480 Б) — `#include` без
  правки графа (`${R1117}` уже в include-путях, `gen_cmake.py:130/144`).
- env `PW_VK_RDCAP=<кадр>`, `PW_VK_RDCAP_TIME=<сек от 1-го present>`,
  `PW_VK_RDCAP_STEP`, `PW_VK_RDCAP_LEAD` (дефолт 60 кадров), `PW_VK_RDCAP_FILE`
  (маркер `rdcap.marker`, cwd = `Game/Bin`, имя **без `/`** — Wine портит пути).
  В `Present` — сессионные часы (от первого present; отдельного таймера матча в
  клиенте нет) и запись маркера `PWRDCAP 1 seq= cur= frame= sec= api=`.
- `RdApi()` — путь к `renderdoc.dll` (`GetModuleHandleA`, `LoadLibraryA` только
  при `PW_VK_RDC_DLL=1`): нужен для нативного Windows-контура, под Wine всегда
  `api=0` (гостевой PE не видит символов хостового `.so`) и не участвует в съёмке.
- `PW_VK_DBGNAME=1` — третий instance-ext `VK_EXT_debug_utils`; `VkSetName()`
  подписывает `VkImage` сам (backbuffer, back depth, `PW surf WxH[ depth]`,
  `PW tex WxH[ cube]`): NVRHI этой версии именует из `debugName` только хипы
  (`Vendor/NVRHI/src/vulkan/vulkan-device.cpp:817`), без этого в GUI RenderDoc
  все текстуры безымянные.

Инструменты хоста — в ветке `tools/rdcap/` (README там же; на машине рабочие
копии — симлинки `~/PW/tools/rd/`, ими пользуется `wine-client/rdcap_match.sh`):
`rdctl.py` (ports/trigger/queue/watch — протокол target-control),
`rdwatch.py` (маркер → `QueueCapture` → ожидание `NewCapture` ровно этого кадра),
`xkey.c` (XTEST-клавиша на `:97`, хоткей слоя F12),
`implicit_layer.d/renderdoc_capture.json` (манифест с абсолютным `library_path`),
RenderDoc 1.46 в `~/PW/tools/renderdoc_1.46/`. Процедура целиком и грабли —
скил `pw-render`, `references/renderdoc.md`.

Грабли, из-за которых попытка 2026-10-17 не сработала:

- `PW_VK_LAYERS` (явный instance-ext слоя) **вместе** с implicit-env — двойная
  регистрация и access violation клиента сразу после `Слой инстанса — OK`.
  Только один способ.
- Слой грузят два процесса Wine: `explorer.exe` (38920) и `PW_Game.exe` (38921);
  стрелять надо в порт `PW_Game.exe`, он появляется только после `vkCreateInstance`.
- Протокол target-control: чанки **дополняются до 64 байт** (`ChunkAlignment`,
  `serialiser.h:1536`) — без этого сервер молча не отвечает; первым обязан слать
  чанк `ePacket_Handshake` клиент (`version=9`, имя, `kick`);
  `QueueCapture` работает только на будущий кадр; сервер переигрывает все прежние
  `NewCapture` новому подключению.
- `RENDERDOC_DEBUG_LOG_FILE` — общий файл на оба процесса, второй усечёт лог
  первого.
- Проверенные тупики (не повторять): `renderdoccmd capture` (нет авто-захвата при
  выходе), `RENDERDOC_CAPOPTS` (диапазона кадров нет), `qrenderdoc --python`
  (в tarball нет python-модуля), crash-capture (`delayforcrash` отсутствует),
  `LD_PRELOAD`-инъекция в `wine` (`Loading into …/wine/wine`, `Registering Vulkan
  hooks` есть, `.rdc` нет).


### A/B: та же капча на DXVK-контуре (2026-10-22)

Поштучный разбор событий обеих капч (пары «Vulkan ↔ dxvk» по целям, ресурсам и
шейдерам, 9 диагностированных расхождений) — `docs/NVRHI_VULKAN_VS_DXVK_ISSUES.md`.

Слой снимает и DXVK: он сам говорит с Vulkan, поэтому контур съёмки не меняется —
отличаются только запуск (`WINEDLLOVERRIDES="d3d9=n,b"`, `PW_VK_BACKEND=0`) и
`RENDERDOC_CAPFILE`. Замер на одном стенде (прогон `wine-client/rdcap_dxvk.sh`):

| | Vulkan-срез | DXVK 3.1.1 |
|---|---|---|
| капча | `pw_frame1901.rdc` 178 МБ | `dx_frame10798.rdc` 1,25 ГБ (×7) |
| к/с под слоем | ~20 (1811 кадров за 90 с) | ~97 (10798 кадров за 111 с) |
| имена объектов | `PW backbuffer` / `PW tex` / `PW surf` | нет (в `.rdc` только `Swapchain blit`) |

Имен нет, потому что DXVK включает `VK_EXT_debug_utils` сам
(`warn: Debug Utils are enabled`) и не подписывает ресурсы; `Skipping: Device
does not support Vulkan 1.3` в его логе — про `llvmpipe`, выбран `RTX 3060`.
Маркера клиента в DXVK-контуре нет (`PW_VK_RDCAP*` в `NvrhiBackend::Present`),
клиентский лог без счётчика кадров → капчу берут вслепую: `rdctl.py trigger 1`
по wall-clock, `queue` — по номеру из уже готовой капчи. Точка вставки маркера,
если понадобится: `Src/Render/d3d9backend.cpp:92` `D3D9Backend::Present`.

Решение (2026-10-22): точная привязка к секунде матча и якорь по анонсу старта
боя **не делаются** — сцена на разных секундах матча отличается мало (главное
различие — анимация персонажей), для A/B хватает текущих пар капч; отдельные
скрипты сведения (`rdcap_ab.sh`) не заводятся. Разбор пары: `renderdoccmd thumb`
+ метрика `tools/render_shot/`, структурный дифф — `renderdoccmd convert -c xml`
(на 1,25 ГБ капче XML огромный, резать в GUI). Процедура — скил `pw-render`,
`references/renderdoc.md`, раздел «A/B: Vulkan-срез против DXVK».

## Девятая серия A/B (`rdcap/ab23*`, 2026-10-23): HW-тени восстановлены, метрика шва зафиксирована

Пара капч одного кадра на одном стенде (`wine-client/rdcap_match.sh` ↔
`rdcap_dxvk.sh`, канонический конфиг `tools/render_ab-2026-10-23/ab_user.cfg`,
`RENDERDOC_OVERLAYUI=0`): `rdcap/ab23b/pw_frame1826.rdc` (NVRHI) ↔
`rdcap/ab23b/dx_frame9105.rdc` (DXVK). Разбор — XML-дампы +
`tools/rd/rdcap_ab_report.py` (скил `pw-render`, «A/B: Vulkan-срез против DXVK»).

**Найдено и исправлено: аппаратных теней в NVRHI-пути не было целиком.**
`NvrhiBackend::CreateSurface` имел локальный `switch` только по `bfD24S8` и
`bfA8R8G8B8/bfX8R8G8B8`, всё остальное — `E_FAIL` с заметкой «формат N не
переведён». Формат 6 = `bfR5G6B5` — пустой color-attachment карты теней
(`Render/ShadowMap.cpp:207`), отказ обрывал `ShadowMapPCF_DS::Init`
(`ShadowMap.cpp:210`); формат 8 = `bfA16B16G16R16F` — таблица Ланцожа
двухпроходного `Stretcher` (`Render/Stretcher.cpp:32`). Правка: формат берётся
из общего `SurfaceFormat()` (тот же конвертер, что у `CreateTexture`), отказ
остался только для DXT-форматов. Замер после правки: отказов 0; в кадре
появились 80 проходов 2048x2048 со 107 рисунками и 133 589 треугольниками —
столько же, сколько у DXVK (1 проход, 107 рисунков, 133 589). Визуально на
площади разницы почти нет (там блоб-тень под героем), для визуального
подтверждения нужна сцена с падающими тенями.

**Метрика шва (кадр площади, 1024x768).** NVRHI: `passes=454` (1024x768=301,
2048x2048=80, 512x384=67), `barriers=1180` (ALL_COMMANDS↔TRANSFER — 1052),
`vkCmdUpdateBuffer=1729`, `draws=576`, `tris=290 940`. DXVK: `passes=33`,
`barriers=46`, `draws=659`. Проходов в 0,8 раза больше, чем рисунков — почти
каждый рисунок открывает свой проход; механизм — `writeBuffer` вне прохода
(`Vendor/NVRHI/src/vulkan/vulkan-buffer.cpp:464` делает `endRenderPass`) плюс
пара барьеров на каждую загрузку констант. Это главный резерв перфа ветки.

**Сделано по перфу (в рабочем дереве, не коммитилось):**
- маска SSBO-мостов `SsboMaskOf()` — набор дескрипторов получает SSBO-мосты
  только тем слотам, которые реально читаются как SSBO: загрузки констант в
  том же кадре 1729 → 719, картинка кадра идентична;
- пропуск загрузки констант, если байты совпадают с предыдущей (`uboLastBytes`);
- CPU-маппинг у колец `RegisterUpRing` (4 МиБ VB / 1 МиБ IB) — нейтрально.

**Откачено: волатильный c-банк.** Структурно он и есть решение (проходы
454 → 192, барьеры 1180 → 305, загрузки 1729 → 102 на заставке), но NVRHI
берёт свою версию на КАЖДУЮ запись внутри командного списка
(`vulkan-buffer.cpp:296-345`), а bind использует `latestVersion`; записей ~460
на кадр, поэтому при `maxVersions` 3/16 поиск версии падал, запись ПРОПУСКАЛАСЬ
и кадр площади становился чёрным (средний уровень пикселя 1,1 против 69,3).
При 2048 ошибок меньше, но не ноль → тихая порча кадра возможна. Чинить
батчингом выводов, а не гонкой версий.

**Грабля измерений (отменяет решение 2026-10-22 «сцена на разных секундах
отличается мало»).** За первые ~200 с прогона клиент проходит заставку (чёрный
кадр, уровень ~1), экран выбора героя (~118) и только потом площадь (~69).
К/с по wall-clock — не метрика (один билд даёт 11—30 к/с). Сравнивать надо
счётчики одного кадра у одной сцены; сцена капчи проверяется средним уровнем
пикселя `renderdoccmd thumb -s 1024`.

**Не закрыто: горизонтальные полосы на земле (жёлто-коричневые, ~17—23 линии
на 1024x768).** Есть в NVRHI-контуре (в DXVK — нет), было и до фикса
`CreateSurface`, не зависит от mip-цепочки (`PW_VK_SAMPLER_LOD0=1` не помогает,
автогенерация mip'ов через seam реализована и счётчик уровней показывает
заполненность), не зависит от `maxMipLevel` (зажим реализован, не помог), от
альфа-теста/блинда, и от содержимого bloom-буферов (дампы чистые). Бисект фич:
`bloomEnable 0` → 0 линий (`waterrefl 0`, `pfx_enable 0` → без изменений).
Зонд `PW_VK_BANDSCAN` (в `nvrhibackend.cpp`, env `PW_VK_BANDTHR`,
`PW_VK_BANDSCAN_MAX/STEP`) локализует скачок по рисунку: носитель — цель #91
(бэк-буфер), рисунок пост-обработки `ps=#122` с `tex0=#1 (fmt=9 = bfR32F)`;
сам рисунок затемняет кадр целиком (Δ ≈ −62,−84,−83), а не рисует линии.
Инструменты в рабочем дереве — не удалять, они же и точка входа для правки.

### Закрыто правкой 2026-10-23 (капчи `ab23j/pw_frame2920`, `ab23k/pw_frame2813`)

По `docs/NVRHI_VULKAN_VS_DXVK_ISSUES.md` закрыты проблемы 1, 2, 9 и найденное
там же расхождение сциссора. Правка в `Src/Render/nvrhibackend.cpp` + строка
`R5G6B5_UNORM` в `Vendor/NVRHI` (enum `nvrhi.h`, `format-info.cpp`,
`vulkan-constants.cpp`, `dxgi-format.cpp` — все три таблицы индексированы по
enum, порядок строк обязателен):

- `SetRenderTarget`: привязка слота 0 СБРАСЫВАЕТ viewport/scissor в размер цели
  (D3D9-правило: Wine `device.c:2424-2437`, DXVK `d3d9_device.cpp:1666-1702`).
  Прежний seam только УСЕКАЛ вьюпорт размером цели (`PW_VK_VPCLAMP`), а
  усекатель не расширяет — отсюда карта теней 2048², заполненная четвертью.
  `PW_VK_RTVIEWPORT=0` — старый контур.
- сборка viewport: +0.5 по X и Y (DXVK `BindViewportAndScissor`, cf=0.5);
  сциссор — без смещения и отдельным `addScissorRect` (`Rect(Viewport)` в NVRHI
  берёт floor/ceil и со смещением дал бы 0..1025). `PW_VK_HALFTEXEL=0` — старый
  контур.
- сциссор по DXVK: при выключенном `D3DRS_SCISSORTESTENABLE` прямоугольник
  приложения игнорируется и берётся прямоугольник вьюпорта; при включённом —
  пересечение с вьюпортом (`d3d9_device.cpp:6814-6856`). Seam резал геометрию,
  которую D3D9 не режет. `PW_VK_SCISSORTEST=0` — старый контур.
- `bfR5G6B5 → R5G6B5_UNORM` (Vulkan `*_PACK16` именует компоненты от старших
  бит, DXVK `d3d9_format.cpp:26-28`), попутно `bfX8R8G8B8 → BGRA8_UNORM`
  (у NVRHI `BGRX8_UNORM` на Vulkan = `VK_FORMAT_UNDEFINED`).

Замеры после правки: viewport карты теней `0.5 2048.5 2048 -2048` (до правки
viewport 2048² не было вовсе); viewport'ы приложенийых проходов со смещением
+0.5 — кроме `StretchBlit` (seam сам считает NDC-углы блина, смещения там нет
намеренно); 24× `VK_FORMAT_R5G6B5_UNORM_PACK16` / 0× `B5G6R5`; `VkNote` в логе 0.

Полосы правка НЕ сняла и снять не могла — гипотеза ISSUES (проблемы 1+3 →
полосы) не подтвердилась, правка в них оформлена. Метрика
`tools/render_shot/shot_stats.py --lines` по снимкам Xvfb того же билда после
правки: 17 / 10 / 10 строк с отклонением жёлтости >5.0 (t0/t20/t40 от входа в
площадь) — в пределах записанной полосы 17—23.

Зацепка, проверенная и отменённая: «у DXVK есть RGBA16F-цели, у нас нет» — по
инвентарю капчей все `R16G16B16A16_SFLOAT` у DXVK это 1×1 (служебные),
клиент 16F-целей на площади не создаёт ни в одном контуре. `R32F`-цели
bloom-цепочки создаются одинаково (1024×768 и 256×256, mips=1, usage
идентичен) — из п.1 очереди закрыта половина «чем создаётся».

### Очередь (после ab23j/ab23k)
1. Полосы: снять `ps=#122` (шейдер пост-обработки) и ЧЕМ СЭМПЛИРУЕТСЯ
   `bfR32F`-цель (создание совпадает с DXVK — снято). Полоса мировая (период
   растёт к камере, см. комментарий зонда `PW_VK_BANDSCAN`) — искать в сцене,
   а не в композите.
2. Перф: батчинг выводов — механизм конца прохода: `writeBuffer` по НЕ-volatile
   буферу (`vulkan-buffer.cpp:464` делает `endRenderPass`) плюс пара барьеров на
   каждую загрузку констант. Путь — хост-маппинг колец констант
   (`mapBuffer(CpuAccessMode::Write)`, как уже сделано в `RegisterUpRing` для
   VB/IB), тогда `writeBuffer` на загрузку не нужен вовсе.
3. load-op CLEAR: `vulkan-graphics.cpp:80-81,111-112` жёстко ставят
   `eLoad/eStore`; чинить вместе с п.2 (pending-clear на открытии прохода).
   Само по себе это НЕ потеря обнуления: по числу clear'ов контуры близки
   (NVRHI 3 color + 4 D/S отдельными командами, DXVK 3+4+3 в load-op).
4. Визуальное подтверждение теней: сцена с падающими тенями (не лобби).
5. Финальный композит/гамма (проблема 8): swapchain у DXVK `B8G8R8A8_SRGB`,
   у нас UNORM — проверять по тон-гистограмме (`shot_stats.py --tone`)
   синхронной пары, а не по формату.
6. Коммит пачки (CreateSurface + маска SSBO + дедуп загрузок + зонды +
   viewport/half-texel/сциссор + R5G6B5) — по решению пользователя.
