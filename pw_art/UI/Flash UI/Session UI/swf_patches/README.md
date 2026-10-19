# Патчи боевого интерфейса (main.swf)

Исходники в `pw_art/UI/Flash UI/Session UI` **не совпадают** с собранным
`pw/branches/r1117/Data/UI/Screens/Combat/Flash/main.swf` (в swf есть правки, которых нет
в исходниках, и наоборот). Поэтому интерфейс правится **прямо в main.swf**: классы
декомпилируются из swf, правятся и вкомпилируются обратно через JPEXS FFDec
(https://github.com/jindrapetrik/jpexs-decompiler, проверено на 26.3.0, нужна Java).

Здесь лежат изменённые классы (взяты из декомпиляции текущего main.swf):

| Класс | Что изменено |
|---|---|
| `EscMenu/OptionsWindow.as` | кнопки и страницы «Дополнительно» и «Общение» (строки создаёт C++, `SetAdvancedTargetPage` переключает страницу) |
| `EscMenu/Preferences.as` | мост C++ → Flash: `AddAdvancedHeader`, `AddAdvancedCheckBox`, `AddAdvancedSlider`, `SetAdvancedValueText` |
| `TalentBar/Talent.as` | `Press()` сначала всегда шлёт FS-команду `TalentPress` (до проверок кулдауна/маны/прайма) — для Alt+клика |
| `ActionBar/ActionButton.as` | событие `doubleClick` больше не нажимает кнопку третий раз (нужно для двойного клика — каст на себя) |
| `Chat/ChatBar.as` | чат по центру экрана (`SetCentered`, опция `chat_centered` / `chat_center_raise`) |
| `MainInterface.as` | мост C++ → Flash: `SetChatCentered`; колесо сигналов `ShowPingWheel` / `HighlightPingWheel` / `HidePingWheel` и всплывающие тексты пингов `SetPingMarker` |

Строки страницы описаны в C++: `Src/UI/PreferencesProcessor.cpp`, массив `s_advancedVars`.

## Как пересобрать main.swf

```
patch_main_swf.bat path\to\ffdec.jar
```

Скрипт берёт `main.swf.orig` (оригинал) и вкомпилирует классы отсюда в `main.swf`.
Чтобы поправить другой класс: декомпилировать его из main.swf
(`java -jar ffdec.jar -format script:as -selectclass Пакет.Класс -export script out main.swf`),
положить сюда, добавить в скрипт.

## Подводные камни

- Классы, которые стоят на таймлайне (OptionsWindow, ChatBar, ActionButton…), нельзя расширять **полями экземпляра** — таймлайн привязывает детей к слотам. Новое состояние — только `static`.
- Не использовать `?:` (и подобное) прямо в аргументах/выражениях, если ветки дают разные типы (`int`/`Number`, `String`/элемент массива): JPEXS не вставляет приведение, и рантайм отклоняет метод (`VerifyError #1068`). Писать через `if` в типизированную локальную переменную.
- Исключение в обработчике события Flash, вызванном из движка, роняет игру — оборачивать рискованные вызовы в `try/catch`.
