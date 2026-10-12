# main.swf без Flash CS6 (JPEXS FFDec)

Боевой интерфейс собирается из `../main.fla` + `../classes` во Flash CS6. Пока Flash CS6 нет,
`../main.swf` (и копия в `pw/branches/r1117/Data/UI/Screens/Combat/Flash/main.swf`) получен так:
CS6-сборка `main.swf` из `main` (коммит 91d1f60) + классы из этой папки, вкомпилированные через
JPEXS FFDec 26.3.0 (`patch_main_swf.bat`).

**Правки нужно вносить в `../classes`** — это настоящие исходники, при сборке во Flash CS6 эта папка
не нужна. Здесь лежат те же классы в виде декомпиляции FFDec (FFDec не компилирует оригинальные
исходники как есть), чтобы swf можно было пересобрать без Flash.

| Класс | Что добавлено |
|---|---|
| `EscMenu/OptionsWindow`, `EscMenu/Preferences` | страницы настроек «Дополнительно» и «Общение»; строки создаёт C++ (`PreferencesProcessor.cpp`, `s_advancedVars`) |
| `TalentBar/Talent` | `Press()` сначала шлёт `TalentPress` (Alt+клик) |
| `TalentBar/TalentShortCut` | `TalentObj` для чекбоксов smart cast |
| `ActionBar/ActionButton` | чекбоксы smart cast; `doubleClick` не нажимает кнопку третий раз (каст на себя) |
| `Chat/ChatBar` | чат по центру (`SetCentered`) |
| `MainInterface` | колесо пингов, тексты пингов, praise, smart cast, `SetChatCentered`, «тихий» `AddSignal` |
| `Effects/AttentionEffect`, `Controllers/SoundController` | звук сигнала на миникарте можно выключить (`soundEnabled`, `muteMinimapAlert`) |

Картинки колеса пингов, praise и smart cast swf грузит из Data (`/UI/Styles/Icons/PingWheel|Praise|SmartCast/`),
в `.fla` их добавлять не нужно. Новые функции вызывает C++ (ветка с Laguna/пингами); без него
работает всё как раньше, только кнопки «Дополнительно» и «Общение» в настройках будут пустыми.

Подводные камни FFDec: в классах, которые стоят на таймлайне, новые поля только `static`;
не писать `?:` с ветками разных типов в аргументах (VerifyError #1068); рискованные вызовы из
обработчиков событий оборачивать в `try/catch`.
