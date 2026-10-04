# Сборка клиента (PW_Game.exe) под Wine

Клиент Prime World Classic — 32-битный x86, оригинально собирался в **Visual Studio 2008
Professional SP1** (MSVC 15.00.30729.01) по решению
`pw/branches/r1117/Src/PF.sln`, конфигурация **ShippingSingleExe|Win32**
(публичная; тестовая с читами — ReleaseSingleExe).

Под Wine devenv/MSBuild не работают, поэтому сборку выполняет драйвер
`Tools/WineClientBuild/build_client.py`: он читает `.vcproj`, воспроизводит флаги
конфигурации и вызывает `cl`/`lib`/`rc`/`link`/`mt` напрямую.

Быстрая сборка одним запуском (из чистого клона — других условий нет):

```bash
cd prime-world-classic-new-client
./build_client_wine.sh
```

Что обёртка делает сама:

* **Wine-префикс ищется сам** — перебираются `~/pwbuild/wine32`, `~/.wine-vs2008`,
  `~/.wine`, `~/<каталог>/wine*`; пригодность проверяется по наличию `cl.exe`
  VS2008 и `Include`/`Lib` Windows SDK v6.0A. Вручную — `--prefix=PATH` или
  `WINEPREFIX=`. На этой машине подходящий префикс один —
  `/home/rekon/pwbuild/wine32`; `rc.exe`/`mt.exe`/`midl.exe` лежат в `C:\vcbin`
  префикса (каталог `SDK\Bin` пуст).
* **`Src/PW_Game/server_ip.h`** — per-deploy файл, в git не лежит (`.gitignore`);
  без него `C1083: Cannot open include file: 'PW_Game/server_ip.h'` на Network,
  Shared и PF_GameLogic. Если файла нет, он генерируется из
  `Tools/WineClientBuild/server_ip.h.template`; значения берутся из переменных
  `PW_SERVER_IP`, `PW_SESSION_TOKEN`, `PW_API_KEY`, `PW_SERVER_PORT`, … либо из
  `Tools/WineClientBuild/server_ip.env` (тоже gitignored — там лежат значения
  стенда). Существующий файл не перезаписывается; `--regen-ip` — пересоздать.

**Результат (чистая сборка ветки `new-client`, 2026-10-03, wine-10.0):** 0 ошибок —
`pw/branches/r1117/Src/_ShippingSingleExe/PW_Game.exe` (PE32 i386, 9 809 920 байт;
релизный VS-билд 2.15.4 — 10 077 696 байт) + `PW_Game.exe.manifest`. Импорт-таблица
(35 DLL) совпадает с релизным клиентом. Клиент **проверен на реальном Windows**
(tiny10, стенд `local-gameserver-test` + dev-бэкенд): полный E2E пройден —
login → lobby → gamesvc → `TALENTS-DELIVERY` → `Game result delivered … attempts=0`.
Полная сборка на 8 ядрах — ~13 минут.

---

## 1. Что нужно

### Тулчейн

| Компонент | Версия | Где в префиксе |
|---|---|---|
| Wine | 10.0 (подойдёт и 9.x) | — |
| VS2008 Professional SP1 (VC) | cl 15.00.30729.01, link 9.00.30729.01 | `C:\Program Files (x86)\Microsoft Visual Studio 9.0` |
| Windows SDK | v6.0A | `C:\Program Files\Microsoft SDKs\Windows\v6.0A` (`Bin` пуст) |
| rc / mt / midl | — | `C:\vcbin` (скопированы из VS/SDK; bare-имена резолвятся через PATH префикса) |
| VC90 CRT | 9.0.21022.8 (`msvcr90`/`msvcp90`) | `windows\winsxs\x86_microsoft.vc90.crt_...` или рядом с exe (`Microsoft.VC90.CRT\`) |

Рабочий префикс на этой машине: `/home/rekon/pwbuild/wine32`. Создание с нуля:

```bash
WINEARCH=win32 WINEPREFIX=~/.wine-vs2008 winecfg
# установить VS2008 SP1 (нужна только VC-часть) и Windows SDK v6.0A
```

IDE не нужна — достаточно `VC\bin` (cl, c1xx, link, lib, rc, cvtres, mt) и
`SDK\Include` + `SDK\Lib`.

### Почему нельзя devenv/MSBuild

* `devenv.com` → `Cannot find one or more components. Please reinstall the application.`
  — VS2008 требует .NET Framework 2.0/3.5, которого в Wine нет (Mono не заменяет).
* `msbuild` отсутствует: в префиксе нет `windows\Microsoft.NET\Framework\`.

Поэтому сборка воспроизводится на уровне инструментов.

### Зависимости исходников

Все vendor-библиотеки уже лежат в репозитории (`Vendor/**/lib/*.lib`, 348 шт.) —
это готовые import-библиотеки, их не нужно пересобирать: под Wine линкуется тот же
набор, что и на Windows. Отсутствующие в репозитории `Psapi`, `shlwapi`, `wbemuuid`,
`comsuppw`, `Rpcrt4`, `version`, `winmm`, `ws2_32` берутся из `SDK\Lib` и `VC\lib`.

---

## 2. Состав сборки

28 проектов собираются в `ShippingSingleExe|Win32`, из них 25 участвуют в
`PW_Game.exe` (исключены `UniServerApp` — сервер собирается на Linux,
`FilePileCompiler` и `PWClassicSandbox` — падают и в оригинальной VS-сборке):

```
MemoryLib, Foundation, Scripts, libdb, NivalInput, Render, Terrain, Core, Scene,
Sound, UI, Client, Network, RpcBase, RemoteObjects, naio, mballocator, transport,
GameChatClient, Shared, PF_Core, PF_GameLogic, PF_Minigames, PW_Client, PW_Game
```

Порядок — по `ProjectDependencies` из `PF.sln` (сборка отдельных vcproj не работает:
зависимости объявлены на уровне решения).

Выход: `pw/branches/r1117/Src/_ShippingSingleExe/PW_Game.exe` (~9,8 МБ) +
`PW_Game.exe.manifest` (последний нужен только для запуска под Wine).

Линковка по умолчанию идёт **`.obj`-файлами**, а не `.lib`-архивами: MSVC достаёт
из архива только те члены, которые закрывают незакрытый символ, поэтому TU,
sуществующие ради статических регистраций (`REGISTER_DBRESOURCE`,
`DEFINE_RE_FACTORY`, `REGISTER_VAR`, консольные команды и cvars), теряются.
Симптом сборки на архивах (проверено на tiny10): в логе `Unknown command or
variable "bind" / "login" / "lobby"`, клиент замирает после `Got login reply` и не
подключается к лобби. Старое поведение — `PW_LINK_LIBS=1`.

---

## 3. Wine-квирки, которые обходит драйвер

Проверено эмпирически на wine-10 (wow64-префикс):

1. **Инструменты вызываются голыми именами** через `wine cmd /c` — прямой
   `wine /path/cl.exe` ломает собственный парсер аргументов cl.
2. **Флаги с двоеточием у cl сломаны**: значение после `:` утекает в значение,
   а значение через пробел не потребляется. Поэтому:
   * нет `/Fo:` — cl запускается с CWD = IntDir, имена `.obj` = базовые имена источников;
   * нет `/Fp:` — PCH по умолчанию (`vc90.pch`) в CWD;
   * нет `/Fe:` — линковка идёт `link.exe /OUT:` (у линкера двоеточная форма работает);
   * `/Yc` и `/Yu` только голые, through-header по умолчанию `stdafx.h`;
   * каталоги include — через переменную окружения `INCLUDE`, без `/I`.
3. **PCH эмулируется wrapper-источником** (для каждого TU с `UsePrecompiledHeader=2`):
   ```cpp
   #include "<through-header>"   // содержимое PCH как обычный include
   #line 1 "<original>"           // диагностики остаются на строках оригинала
   #include "<original>"
   ```
   Это нужно, потому что многие TU зависят от контекста PCH (платформенные define,
   заголовки проекта). Обёртка названа как оригинал → имя `.obj` сохраняется.
4. **PDB невозможны** (`C1902`/`LNK1101`: mspdb80 + wine builtin msvcr90) → `/Zi` и
   `/DEBUG` не передаются. Клиентский бинарник получается без отладочных символов.
5. **`/OPT:REF` не передаётся**: под wine COMDAT-свёртка давала бинарник, падающий при
   запуске. В оригинальной VS-сборке `LinkIncremental=1` отключает `/OPT:REF`, так что
   поведение совпадает.
6. **`rc /fo:` теряет двоеточие** → выход переименовывается (`:out.res` → `out.res`).
   Response-файлов у rc нет, поэтому rc-команды держатся короче 1023 символов.
7. **Длинные списки файлов** → response-файлы cl/lib/link.
8. **Манифест приложения — отдельный rc-ресурс 24/1.** link 9.0 не знает
   `/MANIFESTINPUT` (это опция VS2010+), а `mt -outputresource:` под wine падает с
   page fault, поэтому встроить смерженный манифест нельзя. Проблема не только в
   этом: `Application.rc` написан как `CREATEPROCESS_MANIFEST_RESOURCE_ID
   RT_MANIFEST "Application.manifest"`, но rc не разворачивает эти константы в
   числа — получается ресурс с *именем* `CREATEPROCESS_MANIFEST_RESOURCE_ID`,
   который Fusion не видит (в релизном exe он тоже есть, но рядом лежит настоящий
   `24/1`, который VS встраивает своим mt). Драйвер генерирует
   `wine_manifest.rc` (`1 24 "wine_app.manifest"`) и `wine_app.manifest` =
   `Application.manifest` + зависимость `Microsoft.VC90.CRT` 9.0.21022.8 +
   `trustInfo/asInvoker`, компилирует rc и подаёт на линковку. Без `24/1` CRT не
   активируется и Windows отказывает в запуске с `0xC0000135`
   (STATUS_DLL_NOT_FOUND); лог-папка при этом не создаётся вообще. Внешний
   `PW_Game.exe.manifest` (mt merge) остаётся только для запуска под Wine: на
   Windows он игнорируется, когда в exe есть встроенный манифест.
9. **Регистр путей**: Linux-ФС чувствителен к регистру, vcproj-пути — в Windows-регистре
   (`newdelete.h` vs `NewDelete.h`) → `case_resolve()` резолвит по фактическому содержимому.
10. **`$(ProjectDir)` не добавляется в angle-поиск include** (VS добавляет её только в
    quote-поиск): иначе, например, `<Rpc.h>` резолвится в проектную `Rpc.h` вместо SDK-шной.

---

## 4. Что пришлось исправить в исходниках

**Ветка `new-client` (2026-10-03): правки исходников не понадобились** — чистая
сборка проходит без единой ошибки. Ниже — каталог фиксов ветки `headless_client`
(=`linux_server`): там Linux-порт ломает MSVC-сборку. Если эти файлы придут в
`new-client` мержем порта, фиксы берутся оттуда (все guards —
`#if defined(NV_LINUX_PLATFORM)` / else-Windows, Linux-путь не меняется).

| Файл | Проблема для MSVC 9 | Решение |
|---|---|---|
| `Src/System/systemStdAfx.h` | безусловный `#include <stdexcept>` тянет `<xstring>` в каждый TU → `C4530` при `ExceptionHandling=0` + `/WX` | includes под `NV_LINUX_PLATFORM` |
| `Src/System/MetaProg.h` | `std::is_base_of`/`is_same`/`is_convertible` — C++11, в VS2008 их нет | макросы `META_IS_*`: `std::` на Linux, `std::tr1::` на MSVC |
| `Src/System/StackWalker.cpp` | файл компилируется без PCH → `NV_WIN_PLATFORM` не определён → guard не выбирает ничего, obj пустой → LNK2019 на `StackWalker` | явный `#include "config.h"` перед guard |
| `Src/Network/RUDP/RdpProto.h` | `#include <stdint.h>` — в VS2008 нет `stdint.h` | guard + using-декларации `nival::int8_t/...` из `System/types.h` |
| `Src/Network/RUDP/UdpSocket.cpp` | `::close(s)` в 9 местах без guard (на Windows — `closesocket`) | макрос `CLOSE_SOCKET()` |
| `Src/Network/RUDP/UdpAddr.cpp` | `snprintf` без guard (в VS2008 CRT — `_snprintf`) | guard |
| `Src/Network/RUDP/RdpStats.h` | Windows-ветка вызывает `NiInterlockedExchangeAdd64`, которую порт удалил | helper возвращён под `NV_WIN_PLATFORM` |
| `Src/Network/ClusterConfiguration.cpp` | `int usedServer = 0;` — дубликат определения с `Shared/WebRequests.cpp` → LNK2005 | определение под `NV_LINUX_PLATFORM` |
| `Src/Shared/WebRequests.h` | `#if defined(NV_WIN_PLATFORM)` в общем заголовке: TU без PCH не получает `config.h` → не выбирается ни одна ветка | `#if !defined(NV_LINUX_PLATFORM)` (Windows = else) |
| `Src/PF_GameLogic/WebLauncher.h` | `extern`-декларация, затем `static`-определение → `C4211` при `/WX` | декларации сделаны `static` |
| `Src/PF_Core/ColorModificationChannel.h` | использует `NDb::BlendMode`, но `Render/DBRender.h` не подключён (порт заменил forward-declaration enum на include) | self-contained include |
| `Vendor/ACE_wrappers/ace/OS_NS_stropts.inl` | безусловный `#include <sys/ioctl.h>` | guard `#if !defined(WIN32)` |
| `Src/Server/RPC/RpcArgs.h` | `C4519` (default template argument на функции-шаблоне) при `/WX` | подавлено в драйвере (`/wd4519`) — легальный C++, предупреждение MSVC 9 консервативно |

**Правило для будущих правок** (уже зафиксировано в скиле `pw-client`): в общих
заголовках не использовать `#if defined(NV_WIN_PLATFORM)` — платформенный выбор делать
как `#if defined(NV_LINUX_PLATFORM) … #else (Windows) … #endif`, потому что `WIN32`
определён не во всех проектах решения и `config.h` подключён не везде.

---

## 5. Runtime и запуск под Wine

`.refs` (аналог post-build `CopyReference.exe`) — `Tools/WineClientBuild/refs_copy.py`:
`CensorDll.dll`, `CrashRpt.dll`, `CrashSender.exe`, `d3dx9_43.dll`, `dbghelp.dll`,
`fmod*.dll`, `jpeg62.dll`, `libpng12.dll`, `libtiff3.dll`, `zlib1.dll`,
`steam_api.dll`, `sakijapi.dll`+`pcnsl.exe` (StarForce), `Microsoft.VC90.CRT\`
(side-by-side CRT), `crashrpt_lang.ini`, `Game.cmd`.

Этого мало: `PW_Game.exe` импортирует ещё `ACE.dll`, `IOTerabit.dll`,
`TProactor.dll` (из `Vendor/ACE_wrappers/lib` и `Vendor/Terabit/lib`) — их в
`.refs` нет, полный набор лежит в `pw_publish/branch/Client/PvP/Bin` и в
установленном клиенте (`...\Game\Bin`). Проверенный список импортов билда
(35 DLL) совпадает с релизным exe; `D3DCompiler_43.dll`/`nvtt.dll` среди них **нет**.

Структура запуска: exe лежит в `.../Game/Bin`, данные — в `.../Game/{Data,Localization,Profiles,Tools}`
(клиент резолвит их как `..\` от CWD; на Windows это junction'ы из `make-links.bat`).

Проверка под Wine (headless, Xvfb) имеет смысл только как smoke: без игровых
данных (`Packs`) клиент выходит сразу и лог не пишет. **Реальная проверка — на
tiny10** (скил `pw-client`, «E2E-стенд на tiny10»): бэкап `PW_Game.exe.bak-<дата>`,
`scp` нового exe в `...\Game\Bin`, сессия `cli/session.js --ips 0a4e0001`,
`schtasks /run /tn PwE2E`. Если exe не стартует — задача с `start /wait` и
`echo RC=%errorlevel%`: `0xC0000135` = не найден DLL / не сработала активация CRT
(проверить, что в exe есть ресурс `24/1`, см. §3.8).

Замечания по запуску:
* exe 32-битный → в 64-битном префиксе работает в experimental wow64-режиме;
* без дисплея клиент выходит сразу (`nodrv_CreateWindow`);
* `Renderer: CreateDevice() failed, hr = 0x8876086C` — не фатально (D3D headless);
* `pcnsl.exe` (StarForce) запускается — при проблемах его можно не копировать.

---

## 6. Команды

```bash
cd prime-world-classic-new-client
./build_client_wine.sh                                        # полная сборка
./build_client_wine.sh --config="ReleaseSingleExe|Win32"      # тестовая (с читами)
./build_client_wine.sh --only=PW_Client                       # один проект
./build_client_wine.sh --dryrun                               # план jobs без запуска
./build_client_wine.sh --keep-going                           # все ошибки за проход
./build_client_wine.sh --clean                                # пересобрать всё
./build_client_wine.sh --deploy                               # exe в pw_publish/branch/Client/PvP/Bin
./build_client_wine.sh --regen-ip                             # пересоздать server_ip.h из шаблона
PW_SERVER_IP=10.0.0.1 ./build_client_wine.sh --regen-ip        # значения стенда через окружение
PW_LINK_LIBS=1 PW_BUILD_JOBS=8 ./build_client_wine.sh          # линковка на .lib (не рекомендуется)

# диагностическая сборка (реестр аллокаций со стеками); /Oy- обязателен:
# в 32-битном процессе на x64 без кадровых указателей стеки не собираются
PW_EXTRA_DEFS='MAX_STACK_SIZE=10;NI_DUMP_LEAKS_TO_FILE' PW_EXTRA_OPTS='/Oy-' \
  ./build_client_wine.sh --clean
```

Смена `PW_EXTRA_DEFS`/`PW_EXTRA_OPTS`/`PW_CFG` не видна логике актуальности TU —
такую сборку запускать только с `--clean`.

Вручную (этапы):

```bash
BR=pw/branches/r1117
python3 $BR/Tools/WineClientBuild/parse_vcproj.py "$PWD/$BR/Src" runtime/model.json
WINEPREFIX=~/.wine-vs2008 python3 $BR/Tools/WineClientBuild/build_client.py
python3 $BR/Tools/WineClientBuild/refs_copy.py "$PWD/$BR/Src" "$PWD/$BR/Src/_ShippingSingleExe"
```

Важно: драйвер считает TU актуальным по mtime источников; заголовки учитываются
только по максимуму mtime в каталоге проекта и include-путях. При правке заголовков
надёжнее `--clean`.

Время полной чистой сборки на 8 ядрах: **~11 минут** (проверено: 130 cl-пакетов +
25 lib + link + mt, 0 ошибок). cl под wine однопоточен на TU, поэтому сборка
масштабируется только параллельностью пакетов.

---

## 7. Ограничения

* Нет PDB → отладка под `cdb` невозможна (на Windows-ВМ она есть).
* `/OPT:REF` не применяется (в VS-сборке `LinkIncremental=1` тоже отключает его) —
  размер бинарника отличается от релизного (~9,81 МБ против ~10,08 МБ в билде .82
  2.15.4); при линковке на `.lib` разница больше (~9,07 МБ) — там теряются TU
  статических регистраций, такая сборка неработоспособна.
* `PW_Game.exe.manifest` содержит только CRT-зависимость; VS2008 дополнительно
  мержит `Application.manifest` (Common-Controls v6) — драйвер делает это `mt.exe`.
* `PF_Editor` не собирается (как и в оригинале).
* Linux-сборка сервера (`build_server.sh`) этими правками не затронута: все guards
  сохраняют поведение Linux-пути.
