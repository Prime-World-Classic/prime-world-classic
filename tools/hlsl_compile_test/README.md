# hlsl_compile_test

Одиночный прогон HLSL через `D3DCompile` — нужен, чтобы решить, **чем**
компилировать шейдеры клиента. Генератор (`.shd`) сегодня сидит на
`D3DXCompileShader` (`d3dx9_43`), и этот компилятор **отказывается**
компилировать текущие исходники (`E5017: not yet implemented feature:
Resource load type 4` на `tex2Dlod` в `vs_3_0`), поэтому перегенерация
контента текущим путём невозможна (подробности — `docs/PLAN_NVRHI.md`).

## Сборка

```
./build.sh          # cl 14.44 x64 /MT под Wine, берёт окружение из x64-wine.cmd
```

Требует уже сгенерированный граф клиента (`./build_client_x64_wine.sh --gen-only`).
`/MT` — чтобы exe запускался на тестовой Windows без рантаймов.

## Запуск

```
hlsl_compile_test <hlsl> <profile> <entry> [--def NAME=VALUE]... [--inc <dir>]... [--dll <dll>] [--flags <hex>]
```

* профили/точки входа — как у генератора: `vs_3_0`/`ps_3_0`,
  `MainPass_VSMain`/`MainPass_PSMain`;
* `--def` повторяет `D3DXMACRO` генератора: `Name` = `pinName` из
  `MaterialSpec.xml`, `Definition` = число (pin-вариация);
* флаги по умолчанию `0x400` (`OPTIMIZATION_LEVEL3`, как
  `D3DXSHADER_OPTIMIZATION_LEVEL3` у генератора);
* `d3dcompiler_XX` грузится динамически (`LoadLibrary`+`GetProcAddress`),
  импортная либа не нужна; по очереди пробуются 47/48/50/46/43.

Include-обработчик ищет файлы относительно каталога главного `.hlsl`
(все include в наших шейлах отсчитаны оттуда: `../GlobalMasks.h`,
`..\OnlineShaders\_commonDepthRead.h`) и дописывает каталоги из `--inc`.

## Где запускать: только на Windows

В Wine-префиксе `~/.wine-vs` лежат `d3dcompiler_46/42/40.dll`, но это
**заглушки**: `D3DCompile` в них возвращает `E_NOTIMPL` (`0x80004001`).
Встроенный в Wine `d3dcompiler_47` компилирует, но ведёт себя иначе, чем
MS `fxc` (например, `MainPass_PSMain` у `FullScreenMaterial` он отвергает с
`E5017: SM1 non-float expression`, тогда как настоящий `d3dcompiler_47`
компилирует). Выводы принимает только прогон на настоящей Windows.

Стенд: tiny10 (`ssh pw-tiny10`), каталог `C:\tools\hlsl\`:

```
C:\tools\hlsl\hlsl_compile_test.exe
C:\tools\hlsl\Src\Render\Shaders\FullScreenMaterial.hlsl   MaterialSpec.hlsl   UIBaseMaterial.hlsl ...
C:\tools\hlsl\Src\Render\GlobalMasks.h
C:\tools\hlsl\Src\Render\OnlineShaders\_commonDepthRead.h
```

```
ssh pw-tiny10 "C:\tools\hlsl\hlsl_compile_test.exe C:\tools\hlsl\Src\Render\Shaders\FullScreenMaterial.hlsl \
  vs_3_0 MainPass_VSMain --def BloomPinValue=1 --def ColorCorrectPinValue=3 --def DoDepthOutValue=1 \
  --def R2VBPinValue=1 --def SharpPinValue=1 --def WarFogPinValue=1 > C:\tools\hlsl\vs.log 2>&1 & type C:\tools\hlsl\vs.log"
```

## Снятый результат (2026-10-13, tiny10, `C:\windows\SYSTEM32\d3dcompiler_47.dll`)

| материал | стадия | `d3dx9_43` (Wine, x64) | `D3DCompile` (MS 47, tiny10) |
|---|---|---|---|
| FullScreenMaterial | `vs_3_0` `MainPass_VSMain` | E5017 на всех вариациях | **OK**, DXBC 1568 Б |
| FullScreenMaterial | `ps_3_0` `MainPass_PSMain` | E5017 | **OK** |
| UIBaseMaterial | `vs_3_0` / `ps_3_0` | 3× E5017 | **OK**, 768 Б / 1612 Б |
| SkyMaterial | — | OK | — |

Т.е. текущие `.hlsl` компилируются современным `D3DCompile` в профили SM3, но
не компилируются тем `d3dx9_43`, на котором сидит генератор. Отсюда вывод
плана: переносить генератор на `D3DCompile` (с теми же `vs_3_0/ps_3_0` для
D3D9-паритета), а не чинить D3DX-путь.
