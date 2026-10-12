# Сборка клиента x64 (ShippingSingleExe) — Wine и нативный Windows

Два скрипта собирают **один и тот же** `PW_Game.exe` (x64, конфигурация
`ShippingSingleExe|Win32`):

| Скрипт (корень репозитория) | Компилятор | Где | build-каталог |
|---|---|---|---|
| `build_client_x64_wine.sh` | cl 14.44 (`~/.wine-vs`) | Linux + Wine | `pw/branches/r1117/Tools/ClientCMake/build/x64-wine/` |
| `build_client_x64_win.cmd` | cl 14.44 (VS2022 нативно) | Windows | `C:\pwbuild\x64-win` (переопределяется `--build-dir PATH` / `PW_BUILD_DIR`) |

Оба вызывают **один и тот же** граф: `Tools/WineClientBuild/parse_vcproj.py` →
`model.json` → `Tools/ClientCMake/gen_cmake.py` → CMake/Ninja. Отличается только
то, чем нативный тулчейн отличается от Wine. Ветка — `client-modern`
(worktree `prime-world-classic-client-modern/`); x86/VS2008 остаётся эталонным
регресс-чеком (`build_client_wine.sh`, см. `BUILD_CLIENT_WINE.md`).

## Команды

```bash
# Linux + Wine
./build_client_x64_wine.sh                    # configure + сборка
./build_client_x64_wine.sh --gen-only         # только граф (компилятор не запускается)
./build_client_x64_wine.sh --check            # только сверка готового артефакта
./build_client_x64_wine.sh --clean | --keep-going | --prefix=PATH
PW_BUILD_JOBS=4 ./build_client_x64_wine.sh
```

```bat
REM Windows (из корня репозитория)
build_client_x64_win.cmd                      REM configure + сборка
build_client_x64_win.cmd --gen-only           REM только граф
build_client_x64_win.cmd --check              REM только сверка артефакта
build_client_x64_win.cmd --clean              REM удалить gen/ и build-каталог
build_client_x64_win.cmd --build-dir D:\pwbuild\x64-win
set PW_BUILD_JOBS=8 & build_client_x64_win.cmd
```

Требования нативного пути: Python 3 в PATH; VS2022 (BuildTools/Community) с
компонентом «MSVC v143 — C++ x64/x86 build tools» + Windows 10/11 SDK
(`vcvars64.bat` ищется в `C:\Program Files [x86]\Microsoft Visual Studio\2022\*`,
перекрытие — `VCVARS64=`); CMake ≥ 3.15 и ninja в PATH (или `PW_CMAKE` /
`PW_NINJA`).

## Правила паритета (правильный ответ на «одинаковый файл»)

Генератор один, поэтому паритет держится только при **одинаковых опциях**:

1. `PW_MACHINE=X64`, `PW_CFG=ShippingSingleExe|Win32` — в обоих скриптах жёстко.
2. `PW_DEFAULT_OPTS=/WX- /MT`, `PW_DROP_LIBS=comsuppw comsuppwd`,
   `PW_DROP_NODEFAULT=libcmt.lib` — одинаковые дефолты. `/MT` не опция вкуса:
   весь x64-вендор в дереве собран `/MT` (в Wine-префиксе нет `msvcprt.lib`),
   `/MD` с ними линковать нельзя.
3. `PW_EXTRA_OPTS` **дополняет** дефолты, а не заменяет (иначе молча теряются
   `/WX-` и `/MT`).
4. `-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=` оставлен пустым в **обоих** скриптах.
   Если PDB включит только одна из сборок, сравнение артефактов теряет смысл.
   Отладочная сборка на Windows — `PW_PDB=1` (артефакт тогда не сравним).
5. `PW_WIN_ROOT` — корень, под которым компилятор видит дерево: в Wine `Z:`
   (unix-«/»), нативно — буква диска репозитория. Его читает `winpath()` в
   `gen_cmake.py`; им отличаются **только** PCH-обёртки (`#include` внутри C++),
   всё остальное в графе идентично.
6. build-каталог нативной сборки — **вне дерева репозитория**: 1074 PCH-обёртки
   + response-файлы не должны упираться в MAX_PATH, а CMake-кэш привязан к пути.

Проверка паритета графа (без сборки):

```bash
./build_client_x64_wine.sh --gen-only
PW_WIN_ROOT="C:" PW_MACHINE=X64 PW_EXTRA_OPTS="/WX- /MT" \
  PW_DROP_LIBS="comsuppw comsuppwd" PW_DROP_NODEFAULT=libcmt.lib \
  python3 pw/branches/r1117/Tools/ClientCMake/gen_cmake.py \
    pw/branches/r1117/Src pw/branches/r1117/Tools/ClientCMake/model.json /tmp/gen-native
diff -r pw/branches/r1117/Tools/ClientCMake/gen/x64-wine /tmp/gen-native   # только пути в pch/
```

Проверено: `CMakeLists.txt`, `wine_app.manifest`, `wine_manifest.rc` и все 25
`projects/*.cmake` — побайтово идентичны; различаются только 1074 PCH-обёртки
(`Z:/…` против `C:/…`).

## Что делает x64-сборку x64 (в `gen_cmake.py`, не в скриптах)

Под `PW_MACHINE=X64` генератор: добавляет `System/FormulaVM.cpp` (парсер DSL +
байткод-машина: предкомпилированный x86-blob на x64 неисполним); подменяет
вендорские имена/каталоги (`fmod*`→`fmod*64`, `steam_api`→`steam_api64`,
`Lib/x86`→`Lib/x64`, `redistributable_bin`→`…/win64`, `<dir>/lib`→`<dir>/lib/x64`,
если каталог есть); убирает `Vendor/Gl/lib` (x86-ный `GlU32.Lib`); снимает
`sakijapi` (StarForce, x64-дистрибутива нет — вызовы `PSA_*` закрыты отсутствием
`STARFORCE_PROTECTED`), `gtrtst32`, `cxxtest` (мертва), `crashrpt` (требует
VC.ATL, код под `#if !defined(_M_X64)`); добавляет `/ALTERNATENAME` для
легаси-имён CRT (`stricmp`, POSIX-имена для libcurl). Пересобранный вендор —
`Tools/VendorX64/build_vendor_x64.sh`, артефакты в `Vendor/<вендор>/lib/x64/`.

Манифест: rc-ресурс `1 24` из `Src/Application.manifest` с
`processorArchitecture="amd64"` (базовый файл написан под X86 — без подмены
манифест к x64-экзешнику не применяется) и **без** CRT-зависимости (`/MT`).
Линкуется с `/MANIFEST:NO` в обоих скриптах — потому что так работает под Wine;
менять только в одном = расхождение артефактов.

## Сверка артефакта

`Tools/ClientCMake/check_exe.py` (общая для обоих путей, stdlib):

```
размер: 15802880 байт
machine: x64  PE: PE32+  subsystem: 2 (WINDOWS)
Characteristics: 0x0022
ресурс манифеста RT_MANIFEST, id: ['1']
FileVersion в exe: 2.16.1.0
Version.h: 2.16.1 (ожидание FileVersion 2.16.1.0)
ИТОГ: OK
```

Ловит: не-x64/machine, отсутствие ресурса манифеста (на Windows процесс падает
сразу, `0xC0000135`, лог-папка не создаётся) и **протухание версии** —
`FileVersion` из ресурса против `Src/Version.h` (баг `DEPENDS` у `.rc`,
`PLAN_client_build_speedup.md` §3: экзешник с `Version.h 2.16.1` показывал
`2.16.0.0`). Эталон этапа 3: 15 725 568 Б, machine `0x8664`, 0 незакрытых.

`server_ip.h` (gitignored, per-deploy) генерируется одинаково: в Wine — `sed` по
шаблону, нативно — `Tools/ClientCMake/gen_server_ip.py` с теми же дефолтами;
источник значений — `Tools/WineClientBuild/server_ip.env` или
`PW_SERVER_IP` / `PW_SERVER_PORT` / ….

## Запуск результата на Windows

Ставится **отдельный** каталог (`C:\pw64\Game\Bin`), `Game\Packs`, `Game\Data`,
`Game\Profiles` — junction'ы (`mklink /J`) на существующую установку. Набор
зависимостей x64 (в `.refs` его нет): `PW_Game.exe`, `ACE.dll`, `TProactor.dll`,
`IOTerabit.dll`, `CensorDll.dll`, `zlib1.dll`, `fmodex64.dll`,
`fmod_event64.dll`, `steam_api64.dll`, `d3dx9_43.dll` **x64** (в дереве только
x86; берётся из `directx_Jun2010_redist.exe` → `Jun2010_d3dx9_43_x64.cab`).
`msvcp140.dll` не нужен (`/MT`). Финальный гейт — E2E на tiny10 (`PwE2E64W`),
а не линкер.

## Грабли `.cmd` (наступили семь)

- **cmd разделяет аргументы по `=`**, поэтому `--build-dir=X:\y` приходит как два
  аргумента. Формат — `--build-dir X:\y` (или `"--build-dir=X:\y"` в кавычках).
- **`%CFG%` с `|`** (`ShippingSingleExe|Win32`) подставляется до разбора строки и
  ломает команду как pipe → печатать через `!CFG!` (отложенное раскрытие).
- **`if cond echo X & exit /b N` без скобок**: `&` — разделитель команд, `exit`
  выполняется при любом условии. Скобки обязательны.
- **`%VAR%` внутри составного блока `if ( ... )` раскрывается на этапе РАЗБОРА
  всего блока** — значение, поставленное `for`'ом двумя строками выше, там всегда
  пусто (`KVER` молча ломал выбор версии SDK). Внутри блоков — только `!VAR!`.
- **`for /d %%d in ("C:\путь с пробелом\10.*")` маску не раскрывает** (кавычки
  отключают глоб, итерация по буквальному шаблону) — каталоги перечислять через
  `for /f "delims=" %%d in ('dir /ad /b "…\10.*" 2^>nul')`.
- **подстрока из `%~1` недоступна** (`%~1:~0,12%` — синтаксическая ошибка, cmd
  ругается, но продолжение работает) — аргумент сначала копируется в переменную.
- **в файле `.cmd` счётчик `for` — `%%d`**; одиночный `%d` валиден только в
  интерактивной строке (проверка «под Wine та же команда, что и в скрипте» на этом
  ломалась и показывала ложный результат).
- **Файл обязан быть ASCII и CRLF**: русские сообщения в консоли зависят от
  кодовой страницы (cp437/cp866), а LF-only + `^`-продолжения строк cmd читает
  нестабильно. Проверка: `file build_client_x64_win.cmd` →
  `DOS batch file, ASCII text, with CRLF line terminators`. Вывод python-хелперов
  (русский) в OEM-кодировке консоли — каша: `chcp 65001` + `PYTHONIOENCODING=utf-8`.

Проверка без Windows: `wine cmd /c "Z:\…\build_client_x64_win.cmd --check"` —
проходят разбор аргументов, вывод путей и `!CFG!`; дальше скрипт честно падает на
«python not found» (в Wine-префиксе Python нет).

## Нативный гейт на tiny10 (тулчейн скопирован, VS не установлена)

На VM нет Visual Studio и нет права на установку — тулчейн **скопирован** с
Wine-префикса (`VC\Tools\MSVC\14.44.35207` + `Windows Kits\10`, ~840 МБ) и
скрипт вызывается в режиме `PW_MSVC_DIR` + `PW_KITS_DIR` (PATH/INCLUDE/LIB
собираются вручную, `vcvars64.bat` не вызывается). Разложенное на VM:
`C:\pwbuild\vs2022` (MSVC+Kits), `C:\pwbuild\wintools` (cmake/ninja),
`C:\pwbuild\python` (embeddable), дерево — `C:\pwbuild\pwc`, сборка —
`C:\pwbuild\x64-win`, лог — `C:\pwbuild\native_build.log`, запуск задачой
`schtasks /tn PwNativeBuild`.

Что всплыло именно на нативном прогоне:
- **в переносимое дерево обязан попасть `pw/branches/r1117/Data/GameLogic/*.h`**
  (всего 2 файла, 24 КБ: `Vec2_Base.h`, `FormulaPars.h`) — их включают
  `Src/System/Geom.h` и формульный слой; без них `fatal error C1083` на первых же
  TU. `Data/` целиком (7,2 ГБ) не нужен.
- **Windows embeddable python держит `sys.path` в `pythonNN._pth` и каталог
  скрипта не добавляет** → `import pch_rules` падал `ModuleNotFoundError`;
  `gen_cmake.py` теперь сам вставляет свой каталог в `sys.path`.
- Замеры на VM (4 ядра, `-j4`, тот же граф, что и в Wine-пути):

  | прогон | wall | сумма TU | экзешник |
  |---|---|---|---|
  | нативный без PCH | 12m02s | 47m56s | 15 797 248 |
  | нативный + `PW_PCH=all` | 6m54s | 27m27s | 15 150 592 |
  | нативный без PCH + `sccache.exe`, холодный кэш | 16m01s | 63m50s | 15 797 248 |
  | нативный без PCH + `sccache.exe`, тёплый кэш | **3m04s** | 12m08s | 15 797 248 |
  | Wine, `-j4`, без PCH (для сравнения) | 64m06s | 337m23s | 15 802 880 |

  Тот же cl 14.44 нативно примерно в 7 раз быстрее, чем под Wine; PCH даёт свои
  −43% в обоих тулчейнах (размер экзешника −4,1% там и там). Сравнение wall между
  разными `-j` и разными машинами некорректно — только сумма TU (`build_time.py`).
- **Кэш и PCH несовместимы:** `sccache --show-stats` даёт
  `Non-cacheable reasons: /Fp 2148, /Yc 68` — с `PW_PCH=all` кэш не даёт НИЧЕГО
  (6m58s против 6m54s без кэша). Экзешник из тёплого кэша не байт-в-байт прямому
  прогону (тот же размер, ~37 КБ отличающихся байт — sccache для MSVC работает в
  preprocessor-режиме), поэтому **релизный артефакт собирается без кэша и без
  PCH**, а кэш/PCH — инструменты разработки.

## Хуки ускорения сборки (`PLAN_client_build_speedup.md`)

| Хук | Wine | Native |
|---|---|---|
| `PW_PCH=<проекты\|all>` | ✔ (через окружение в `gen_cmake.py`) | ✔ |
| `PW_NINJA_STATS=1` (`ninja -d stats`) | ✔ | ✔ |
| `PW_WINE_SYNC=fsync\|esync` | ✔ | — |
| `PW_CACHE_LAUNCHER=<sccache>` → `-DCMAKE_CXX_COMPILER_LAUNCHER` | ✗ — `ninja.exe` (Windows) не может запустить linux-лаунчер; под Wine работает и `sccache.exe`, но весь Wine-путь медленнее нативного в ~7 раз | ✔ — `sccache.exe` (один бинарь, без установки); **с `PW_PCH` инертен** (`/Fp`/`Yc` → non-cacheable) |

Плюс инвариант машины: **одна сборка за раз**, иначе делить `-j` (8+4, а не 8+8);
фоновые сборки на Windows-машине — задачой `schtasks /create /tn <имя> /tr
C:\tools\<обёртка>.cmd /sc once /st 00:00 /f` + `/run` и чтением лога (ssh-сессия
с долгим `call` обрывается), на Linux — `setsid nohup … &`; иначе прерванный вызов
инструмента снимает всю группу процессов и сборка обрывается посреди TU без
сообщения об ошибке.
