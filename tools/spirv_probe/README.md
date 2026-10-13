# spirv_probe — спайк: перевод HLSL клиента в SPIR-V (PLAN_NVRHI §3, этап 3)

Вопрос спайка: **можно ли вообще собрать шейдеры клиента под Vulkan, и какой
ценой.** Ответ: да, 67 из 70 единиц компиляции (35 материалов × VS/PS) собираются
в валидный SPIR-V после **механической** прослойки; три оставшихся — три
конкретные правки (две в исходнике, одна в прослойке).

## Состав

| файл | что делает |
|---|---|
| `fetch_dxc.sh` | качает DXC (Microsoft `v1.9.2609`, Linux x86_64) в `vendor/dxc` (не коммитить) |
| `probe.py` | прогоняет DXC по всем материалам из `MaterialSpec.xml` с теми же PIN'ами, что боевой генератор; группирует неудачи по тексту ошибки |
| `shim.py` | прослойка HLSL SM3 → SM6 (правила ниже) |

```
./fetch_dxc.sh
./probe.py            # без прослойки: 0/70
./probe.py --shim     # с прослойкой: 67/70
```

## Что именно не так в исходниках (замер по 49 .hlsl)

| конструкция | вхождений | почему не компилируется в SM6/SPIR-V |
|---|---|---|
| `sampler2D` / `samplerCUBE` / `sampler` | 15 объявлений | combined-сэмплер удалён из SM6: нужны `Texture2D` + `SamplerState` |
| `register( cN )` / `register( sN )` | 312 | DXC для SPIR-V понимает только `b`/`t`/`s`; `c`/`s` — ошибка «invalid register specification» |
| `tex2D` / `texCUBE` / `tex2Dlod` / `tex2Dproj` | 132 / 4 / 6 / 3 | интринсики SM1-SM3, в SM6 — методы `.Sample`/`.SampleLevel` |
| `: POSITION` на выходе VS | 68 | нужен `SV_POSITION` |
| `: COLOR` / `: COLOR0..1` | — | в SM6 выход PS — `SV_TARGET`; `COLOR` в профиле ps запрещён |
| `VPOS` | 9 | `SV_Position`, и по Vulkan-спеке он **обязан быть float4** (в исходниках `float2` → `VUID-FragCoord-FragCoord-04212`) |

## Правила прослойки (`shim.py`)

1. `sampler2D X : register(...)` → `Texture2D X;` + один общий `SamplerState g_smp`
   (с guard-макросом: файл включается в несколько TU).
2. `: register(...)` снимается везде — DXC сам раскладывает константы по cBuffers
   и ресурсы по binding'ам (реальные binding'ы зададим позже, §3 плана).
3. `tex2D(X, uv)` → `X.Sample(g_smp, uv)`; `texCUBE` → `.Sample`;
   `tex2Dlod(X, float4(u,v,0,mip))` и `tex2Dlod(X, float4(uv2,0,0))` →
   `.SampleLevel(g_smp, uv, mip)`; `tex2Dproj(X, c)` → `.Sample(g_smp, c.xy/c.w)`.
4. Семантики: `POSITION` → `SV_POSITION`; `COLOR` у точки входа PS → `SV_TARGET`;
   `COLOR` в структуре, которой возвращает PS, → `SV_TARGET0..n`; остальные
   `COLOR` → незанятые `TEXCOORDn`; `VPOS` → `SV_Position` с расширением до
   `float4`.
5. Прослойка применяется ко **всем** файлам каталога `Shaders/` (копируется дерево
   `Src/Render/zzz_probe_shaders_<stage>`), иначе `samplerCUBE` из
   `_common.hlsl` ломает каждый материал. Дерево временное, в git не попадает.

## Результат

```
материалов в спецификации: 35   режим: SHIM
PASS: 67   FAIL: 3
  2  control reaches end of non-void function   (UIFlashMaterial vs+ps)
  1  deprecated tex2D intrinsic function ...    (TerrainElementMaterial ps)
```

Оставшиеся три — не блоке­ры:

* `UIFlashMaterial` (VS и PS): при части перестановок PIN'ов все `return`
  оказываются внутри `#if`, и функция без `return` доходит до конца. `fxc` это
  терпел, DXC — ошибка. Правка в исходнике (запасной `return`), как уже делали
  с `X4000`/`X3025`.
* `TerrainElementMaterial` PS: `tex2D(diffuseMapSampler[0], uv)` — **индекс в
  массиве сэмплеров**. Прослойка его не разбирает; в SM6 это массив `Texture2D`
  + индекс, правка механическая.

## Вывод для плана

Компилируемость — **не риск**. Реальный риск — семантическая эквивалентность,
её спайк не проверял:

* `g_smp` один на все выборки, а в D3D9 на каждый слот свой sampler-state
  (filter/address) — нужна таблица сэмплеров и её отображение;
* `register(cN)` снят, а раскладка констант у клиента фиксирована
  (`GlobalMasks.h`, замер §2.12) — её надо перевести в uniform-буферы;
* Y-инверсия viewport, `clip()`, MRT (`COLOR0`/`COLOR1` = цвет+глубина в
  `BasicMaskMaterial`), `mul(vector, matrix)` (строково-векторная конвенция),
  `tex2D` с константным offset;
* SPIR-V валидирован DXC (`-spirv` включает валидатор) — но не запущен на GPU.

Следующий шаг — **спайк B**: нарисовать один SPIR-V-шейдер через NVRHI на
lavapipe и сравнить пиксели с D3D9-кадром. Он и отвечает на вопрос про
семантику.
