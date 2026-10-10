# Редактор PF_Editor на Visual Studio 2022

Состояние работ по редактору (этапы E1/E3 из ТЗ `PW_x64_ClaudeCode_PROMPT.md`).
Файл — «память» между сессиями: при старте прочитать его и `git log --oneline -20`.

Ветка: `editor-modern` (от `client-modern`). Движок x64 — зона Rekongstor (`client-modern`),
файлы движка здесь не правятся.

## Договорённости (2026-10-10)

- Редактор остаётся **x86** (E2 «редактор на x64» снят).
- Формулы: `compiledString` устарел — Rekongstor переводит формулы на локальный парсер `sString`.
  Редактор не должен долгосрочно зависеть от машинного кода `FormulaBuilder`.

## Статус

| Шаг | Состояние |
|---|---|
| E1a — C#-часть на VS2022 / .NET 4.8, нативная часть готовая из `Bin_Original` | **готово** |
| E1b — пересборка нативной части (`EditorNative`, `PF_EditorNative`, DLL движка `Release\|Win32`) на v143 | не начато |
| E1c — DBCodeGen: генерация идентична закоммиченному коду | расхождения найдены, см. ниже; нужно согласование с Rekongstor |
| E3 — удобство | не начато, нужен список от пользователя |

### E1a: что сделано

- Все 16 C#-проектов: `TargetFrameworkVersion` v2.0/v3.5 → **v4.8**. Формат `.csproj` старый (не SDK-style):
  `Vendor/BuildUtils/DBCodeGen.exe` читает `PF.sln` и `*.csproj` старым API MSBuild, SDK-style он может не понять.
- Ссылки на `EditorNative.vcproj`/`PF_EditorNative.vcproj` заменены ссылками на готовые DLL:
  `$(EditorNativeBinDir)` (по умолчанию `r1117\Bin_Original\`). Вернуть ProjectReference — на шаге E1b.
- DockPanel Suite: одна версия **2.3.1** (`Vendor/WinFormsUI/`) во всех проектах; 2.1.6643 (`Vendor/WeifenLuo...dll`)
  больше не используется. ScintillaNET/DockPanel в `PF_Editor` брались из `$(OutDir)` — теперь из `Vendor`.
- `PF_Editor/app.config`, `PF_EditorC/app.config`: `useLegacyV2RuntimeActivationPolicy="true"` — нативные C++/CLI
  сборки собраны под CLR 2.0, без этого процесс .NET 4 их не грузит.
- Исправлено:
  - `EditorLib/ObjectsBrowser/ObjectsBrowser.resx` — вместо base64 стояла заглушка `app_key_binary`
    (вычищено при публикации исходников) → MSB3103. Ресурс и строка в `Designer.cs` удалены: поле
    `extensionsFilter` и так инициализируется `DataBase.KnownResourcesExtensions`, конструктор перезаписывает его тем же.
  - `libdb.NET/Diagnostics/Log.cs` — `StackTrace(1).GetFrame(1)` без проверки длины. JIT CLR 4 встраивает
    `TraceWarning` в `Main`, второго кадра нет → `NullReferenceException` при старте. Это и была причина
    «редактор падает на .NET 4» (старый `PF_Editor.exe`, принудительно запущенный на CLR 4, падал так же).

### Проверено

- Сборка: 0 ошибок (VS2022 17.10, MSBuild 17, .NET 4.8 targeting pack).
- Запуск: редактор открывает базу `Data`, дерево объектов строится.
- `PF_EditorC resave` (открыть → сохранить без правок): `Ability_A1.TALENT.xdb` — байт-в-байт; все таланты
  (`resave -t Talent`, 1740 файлов) — см. раздел «Пересохранение».
- Не проверено: рендер-превью (3D-вьюверы) и правка одного поля через UI — нужна ручная приёмка;
  генерация `DBCodeGen` — см. E1c.

## Пересохранение (правило 10 ТЗ)

Проверка «открыл → сохранил без правок»: `_EditorRun\Bin\PF_EditorC.exe resave -t Talent`, затем `git diff Data`.

| Редактор | Пересохранено | Изменилось в git | Дифф |
|---|---|---|---|
| оригинальный (`Bin_Original`, CLR 2.0) | 1740 талантов | 87 файлов | эталон |
| новый (VS2022, .NET 4.8) | 1740 талантов | 87 файлов | **побайтово совпадает с эталоном** |

Значит, сериализация `*.xdb` не изменилась. 87 файлов меняет и оригинальный редактор — это
нормализация данных, правленных в обход редактора:
- 76 файлов — дописываются поля `isUltimateTalent`/`conflictingTalents` со значениями по умолчанию
  (файлы старше этих полей в типах);
- 6 ссылок `href` — регистр приводится к реальному имени файла (`EvadeOrrange` → `EvadeOrRange`);
- 5 устаревших `BackLink`, 1 `textref`.

Эти изменения в репозиторий **не** вносились (`git checkout -- Data`). Нужна ли такая нормализация данных — решать
отдельно (правило 3 ТЗ: только воспроизводимым инструментом, с отчётом).

Повторить: `PF_EditorC.exe resave -t <Тип>` (рабочая папка `_EditorRun\Bin`), `git diff --stat Data`, затем
`git checkout -- Data`.

Скорость (`resave -t Talent`, 1740 файлов, прогретый дисковый кэш): оригинальный — 107 с, новый — **70 с**.
(Первый прогон нового на холодном кэше — 506 с; это диск, не редактор.)

## DBCodeGen (E1c)

Pre-build `*.DBTypes` и post-build `Types`/`PF_Types` вызывают DBCodeGen, который перегенерирует C#-типы
(`*.DBTypes/*.cs`), C++-код движка (`PF_GameLogic/DB*.cpp`, `Render/MaterialSpec.h`, …) и хэши
(`PW_Client/*Hash.h`, `PF_Editor/*Hash.cs`). **В `build_editor.ps1` эти шаги по умолчанию выключены.**

Найдено при пробном прогоне (результат откатан):
- DBCodeGen падал на `PF.sln`: в `UniServerApp.vcproj` дважды указан
  `Game/PF/HybridServer/LGameServerDispenserIface.auto.cpp` (так и в `main`). Исправлено в DBCodeGen:
  дубликат пишется в лог и пропускается.
- Сгенерированный C++ в репозитории **правлен руками**: `Render/MaterialSpec.h` (x64-проверки раскладки,
  `client-modern`), регистр `#include` в `PF_GameLogic/DB*.cpp` (Linux-порт). Перегенерация это затрёт —
  перед включением генерации договориться с Rekongstor (перенести правки в шаблоны `DBCodeGen/Patterns`).
- `FloatMinMax(0, 1)` в закоммиченном коде против `FloatMinMax(0f, 1f)` из текущего `libdb.NET/DB/Attributes.cs`:
  закоммиченный код сделан более старой версией инструментов, .NET 4.8 тут ни при чём.
- Риск: `Attributes.cs` форматирует float через `String.Format("{0}f", …)` с текущей культурой — на
  русской Windows `0.5` станет `0,5f` (ошибка компиляции сгенерированного кода). Чинить вместе с E1c.

## Как собрать и запустить

```powershell
powershell -ExecutionPolicy Bypass -File pw\branches\r1117\Tools\EditorBuild\build_editor.ps1 -Run
```

- Собирает C#-проекты (`Release|Win32`) в `Src\_Release\`, раскладывает `r1117\_EditorRun\Bin`
  (= `Bin_Original` + новые управляемые сборки) и `_EditorRun\Profiles` (копия, создаётся один раз;
  в `PF_Editor.config` прописан корень данных `-DataRoot`, по умолчанию `r1117\Data`).
- `_EditorRun` в `.gitignore`; отслеживаемый `r1117\Profiles` редактор не меняет.
- По умолчанию pre/post-build шаги DBCodeGen выключены; `-CodeGen` — включить.
- Консольный редактор: `_EditorRun\Bin\PF_EditorC.exe` (рабочая папка — `_EditorRun\Bin`), команды:
  `resave`, `validate`, `import`, `checksource`, `fixnames`, `importformulas`, `script`, `run`.

Требуется: Visual Studio 2022 с .NET desktop (targeting pack 4.8). Для E1b дополнительно —
компоненты «Поддержка C++/CLI для v143» и «C++ ATL/MFC для v143».

## Открытые вопросы

- E1b: собирать ли нативную часть редактора на v143 x86 (нужен движок в DLL-конфигурации `Release|Win32`,
  который давно не проверялся) или жить на готовых DLL из `Bin_Original` до перехода формул на парсер.
- SharpSvn (SVN-интеграция) → git / LibGit2Sharp?
- Northwoods GoExpress 4.1 — коммерческая библиотека, лицензия?
- Список неудобств редактора для E3.
