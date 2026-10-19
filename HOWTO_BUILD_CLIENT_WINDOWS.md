# Сборка клиента на Windows — короткая инструкция

Одна команда из корня worktree ветки `client-modern`:

```bat
cd C:\path\to\prime-world-classic-client-modern
build_client_x64_win.cmd
```

Готовый файл — **`build\PW_Game.exe` в корне репозитория** (папка `build/` в
`.gitignore`; туда же попадают `.pdb`/`.map`, если собирали с `PW_PDB=1` /
`PW_EXTRA_LINK_OPTS=/MAP`). Промежуточное дерево сборки — тоже в `build/`:
`<репо>\build\x64-win` (x64, ShippingSingleExe). Скрипт сам генерирует CMake-граф, сконфигурирует,
соберёт и **в конце сам проверит артефакт** (`check_exe.py`: x64 / PE32+ /
`/SUBSYSTEM:WINDOWS` / наличие ресурса манифеста / `FileVersion` против
`Src\Version.h`) — в конце должно быть `ИТОГ: OK`.

Замер на 4 ядрах: **6m54s** с PCH (по умолчанию), 12m02s без PCH
(`PW_PCH=off`).

## 1. Что нужно один раз

| нужно | как проверить |
|---|---|
| Visual Studio 2022 (BuildTools / Community / Professional), workload «Desktop development with C++», компонент **MSVC v143 — VS 2022 C++ x64/x86 build tools** | `dir "C:\Program Files (x86)\Microsoft Visual Studio\2022\*\VC\Auxiliary\Build\vcvars64.bat"` |
| Windows 10/11 SDK | есть `C:\Program Files (x86)\Windows Kits\10\Include\10.0.*` |
| CMake ≥ 3.15 и ninja (в PATH или через `PW_CMAKE` / `PW_NINJA`) | `cmake --version`, `ninja --version` |
| Python 3 в PATH | `python --version` |
| ~3 ГБ свободного места на диске сборки | build-каталог вне дерева репо |

Ставить Visual Studio IDE не нужно — достаточно Build Tools. Если тулчейн уже
есть в другом месте (или ставить нельзя), см. §5.

## 2. Сборка и полезные флаги

```bat
build_client_x64_win.cmd                    REM configure + сборка
build_client_x64_win.cmd --clean            REM снести gen/ и build-каталог
build_client_x64_win.cmd --gen-only         REM только граф CMake (компилятор не запускается)
build_client_x64_win.cmd --check            REM только проверить готовый PW_Game.exe
build_client_x64_win.cmd --keep-going       REM показать все ошибки сразу (ninja -k0)
build_client_x64_win.cmd --build-dir D:\pw\x64-win
set PW_BUILD_JOBS=4 & build_client_x64_win.cmd
```

Build-каталог по умолчанию — `<репо>\build\x64-win` (gitignored). **Наружу от
рабочей копии не пишется ничего**: запись в корень `C:\` на управляемых машинах
часто запрещена политиками Windows (SRP/AppLocker/WDAC), поэтому дефолт вида
`C:\pwbuild` там просто не работает.

Бюджет `MAX_PATH` (замер на VM): объекты лежат как
`<build>\CMakeFiles\<Проект>.dir\<зеркало абсолютного пути источника>`, самый
длинный путь ≈ `2·len(репо) + 110` (при репо в `C:\pwbuild\pwc` — 130 симв.). До
`260` хватает места для пути репо ~75 симв.; если клон глубже — собирать в
короткий путь: `--build-dir D:\pw\x64-win`.

`--gen-only` безопасен, пока на машине идёт другая сборка (компилятор не
запускается).

## 3. Где что лежит

- готовый экзешник: `<корень репозитория>\build\PW_Game.exe` (gitignored; то же
  место и у Wine-скрипта — куда бы ни собирал, артефакт приезжает сюда);
- сгенерированный CMake-граф: `pw\branches\r1117\Tools\ClientCMake\gen\x64-win\`
  (gitignored, пересоздаётся на каждый прогон);
- `pw\branches\r1117\Src\PW_Game\server_ip.h` (gitignored): IP/порты/токен
  вшиваются в экзешник. Значения берутся из
  `pw\branches\r1117\Tools\WineClientBuild\server_ip.env` или из окружения
  (`PW_SERVER_IP`, `PW_SERVER_PORT`, `PW_LOGIN_PORT`, `PW_CLUSTER_PORT_FRONT`,
  `PW_CLUSTER_PORT_BACK`, `PW_SYNCHRONIZER_PORT`, `PW_SESSION_TOKEN`,
  `PW_API_KEY`). **Файл не перезаписывается, если уже существует** — сменив
  значения, удалите `server_ip.h` руками.

## 4. Режимы, которые надо понимать

- **Релизный экзешник** — без PCH и без кэша:
  ```bat
  set PW_PCH=off & build_client_x64_win.cmd
  ```
  PCH меняет байты артефакта (−4% размера: функции из заголовков эмитятся один
  раз в TU-продюсере), кэш — тоже. Для разработки PCH включён по умолчанию: он
  гейтнут E2E и золотыми статами.
- **Ускорить повторные сборки** (типично после мержа) — кэш объектов:
  ```bat
  set PW_CACHE_LAUNCHER=C:\pwbuild\wintools\sccache.exe
  set SCCACHE_DIR=C:\pwbuild\sccache
  set SCCACHE_CACHE_SIZE=12G
  set PW_PCH=off & build_client_x64_win.cmd
  ```
  Полная пересборка с тёплого кэша: **3m04s** вместо 12m02s. Первый прогон
  холодного кэша, наоборот, медленнее (+33%). **С PCH кэш бесполезен** —
  sccache помечает компиляции с `/Fp` и `/Yc` как non-cacheable (статистика:
  `sccache --show-stats`).
- **Несколько сборок рядом** (база и эксперимент, чтобы не затирать друг друга):
  ```bat
  set PW_BUILD_TAG=x64-win-pch
  ```
  даёт свои `gen\x64-win-pch` и `build\x64-win-pch`.
- **PDB для отладки на Windows**: `set PW_PDB=1` (добавляет `/Zi` и `/DEBUG`;
  после этого артефакт не сравним с Wine-сборкой).
- **Замер времени сборки**: `set PW_NINJA_STATS=1`, потом
  ```bat
  python pw\branches\r1117\Tools\ClientCMake\build_time.py build\x64-win\.ninja_log
  ```
- **Доп. флаги**: `PW_EXTRA_OPTS` **дополняет** дефолты тулчейна (`/WX- /MT`), а
  не заменяет их; `/MAP` для разбора крэшей — `PW_EXTRA_LINK_OPTS=/MAP`.

## 5. Если Visual Studio ставить нельзя (VM, CI, контейнер)

Скрипт умеет собирать по **скопированному** тулчейну — без установки и без прав
администратора:

```bat
set PW_MSVC_DIR=C:\pwbuild\vs2022\VC\Tools\MSVC\14.44.35207
set PW_KITS_DIR=C:\pwbuild\vs2022\Windows Kits\10
set PW_CMAKE=C:\pwbuild\wintools\cmake-3.31.6-windows-x86_64\bin\cmake.exe
set PW_NINJA=C:\pwbuild\wintools\ninja.exe
set PATH=C:\pwbuild\python;%PATH%
build_client_x64_win.cmd
```

`PW_MSVC_DIR` — каталог `VC\Tools\MSVC\<версия>` (нужны `bin\HostX64\x64`,
`include`, `lib\x64`), `PW_KITS_DIR` — каталог `Windows Kits\10` (нужны
`Include\10.0.*\{ucrt,um,shared}` и `Lib\10.0.*\{ucrt,um}\x64`). В этом режиме
`vcvars64.bat` не вызывается, `PATH`/`INCLUDE`/`LIB` собираются вручную. Так
собрана сборка на тестовой Windows-ВМ.

Тулчейн можно положить куда угодно (в том числе рядом с репо — например
`<репо>\..\tools\vs2022`): сборка по-прежнему пишет только внутрь рабочей копии.

## 6. Типовые ошибки

| сообщение | что делать |
|---|---|
| `ERROR: python not found in PATH` | поставить Python 3 и добавить в PATH (embeddable-дистрибутив тоже годится — тогда в `PATH` каталог с `python.exe`) |
| `ERROR: vcvars64.bat not found under C:\Program Files [x86]\…` | не установлены VC++ 2022 build tools → поставить; либо `set VCVARS64=C:\...\vcvars64.bat`; либо режим скопированного тулчейна (§5) |
| `ERROR: no Include\10.* under …` | у `PW_KITS_DIR` не перенесён `Include\10.0.*` (SDK не скопирован целиком) |
| `fatal error C1083: … Data/GameLogic/Vec2_Base.h` | в переносимое дерево не положены `pw\branches\r1117\Data\GameLogic\*.h` (всего 2 файла, 24 КБ) — они нужны для компиляции; `Data/` целиком (7,2 ГБ) не нужен |
| ошибки CMake вида «does not match the generator used» / «source directory … does not match» после переноса или смены компилятора | `build_client_x64_win.cmd --clean`: CMake-кэш привязан и к пути, и к компилятору |
| `fatal error C1083`/`C1041`, «cannot open program database», обрывы на длинных путях | путь репо слишком длинный (см. бюджет `MAX_PATH` в §2): `--build-dir D:\pw\x64-win` или перенести клон выше |
| `check_exe.py` ругается на версию | протух `.res`: правка `Src\Version.h` обязана пересобирать ресурс (зависимость добавлена); собрать заново, при `--clean` — точно |
| экзешник не стартует (`0xC0000135`) | не хватает x64-зависимостей (`d3dx9_43.dll` x64, `fmodex64.dll`, `steam_api64.dll`, …) — набор и источник: `BUILD_CLIENT_X64.md`, раздел «Запуск результата на Windows» |

## 7. Дальше

- инварианты паритета Wine ↔ Windows, почему артефакты обязаны совпадать,
  замеры и грабли batch: `BUILD_CLIENT_X64.md`;
- запуск и E2E-проверка на Windows-машине: скил `pw-client` (стенд `C:\pw64`,
  задача `PwE2E64W`).
