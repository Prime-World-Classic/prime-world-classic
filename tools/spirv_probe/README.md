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

## Спайк B0: один материал через NVRHI, пиксели против CPU-эталона

`shaders/b0_probe.hlsl` — тестовый материал в диалекте клиента (`register(cN)`,
`sampler2D`, `tex2D`, `: POSITION`/`: COLOR`, `clip()`, `mul(float4(pos,1), M)`).
`gen_b0_shader.py` прогоняет его через прослойку + DXC, разбирает SPIR-V
(binding'и, location'ы, точки входа) и пишет
`../nvrhi_probe/b0_shader_spv.h` — SPIR-V как C-массивы. Рендерит и сверяет
`tools/nvrhi_probe` (`SmokeShaderPipeline`).

Регенерация:

    python3 gen_b0_shader.py          # пишет ../nvrhi_probe/b0_shader_spv.h

Флаги DXC для SPIR-V (`-Zpr` обязателен, см. ниже; доп. — через
`PW_DXC_EXTRA="..."`):

    DXC=$(pwd)/vendor/dxc/bin/dxc
    $DXC -T vs_6_0 -E MainPass_VSMain -spirv -Od -Zpr -Vd -I <shim-каталог> \
         <shimmed>.hlsl -Fo out.spv

Что показал B0 (подробный разбор — `docs/PLAN_NVRHI.md` §3.x):

* **4096/4096 пикселей** совпали с CPU-эталоном (host llvmpipe и tiny10
  lavapipe): UV-ориентация, POINT+Clamp, `clip()` → discard, location'ы
  VS→PS, uniform-буфер, вершинные атрибуты;
* **`-Zpr` обязателен**: без него DXC кладёт `float4x4` столбцами, матрицы
  клиента (D3D9, перенос в `_41.._43`) читаются транспонированными и
  `mul(v, M)` теряет перенос;
* имя точки входа в SPIR-V = имя HLSL-функции (`MainPass_VSMain`), не `main`;
* `register(...)` в прослойке снят, но NVRHI живёт со смещениями
  `VulkanBindingOffsets = {t:0, s:128, b:256, uav:384}` — раскладка клиента
  обязана им соответствовать (или смещения надо обнулять в `BindingLayoutDesc`).
