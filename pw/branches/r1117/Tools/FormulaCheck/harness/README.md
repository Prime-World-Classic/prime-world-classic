# Обвязка FormulaVM (golden-дифф)

`FormulaVM` (../../Src/System/FormulaVM.cpp) — порт предкомпилированных формул на
x64: парсер DSL (`sString`) → байткод → интерпретатор. Чтобы он был не «почти
такой же», а ровно тот же, здесь гоняются два эталона на одинаковых
mock-входах (значения аксессоров детерминированы из текста формулы + индекса
строки, `MockFormulaPars.h`).

## Два гейта (и почему их два)

| эталон | как получается | результат (10 838 уникальных sString) |
|---|---|---|
| **x64 C++** | `gen_ref_cpp.py`: из `sString` тем же конвертером, что и `Src/FormulaBuilder/FormulaBuilder.cpp`, генерируется C++ и компилируется **под x64** | **MATCH=10824, DIFF=0** — побитово |
| x86 blob | `compiledString` из `.xdb` исполняется настоящим `DataExecutor` в x86-процессе (`native_exec.cpp`) | MATCH=10729, DIFF=96 |

Расхождения со вторым эталоном (все 96 — < 1e-5 относительно, `int`/`bool` —
ни одного) **не баг VM**: предкомпилированный blob собран x86-компилятором и
считает на x87 с 80-битными промежуточными значениями, а `FormulaVM` (как и
будущий x64-клиент на SSE) округляет каждую операцию к типу. Побитовая сверка
с x86-blob'ом поэтому невозможна в принципе; настоящий гейт — первый.

## Запуск

```bash
cd pw/branches/r1117/Tools/FormulaCheck
python3 dump_formulas.py <Data-корень> > /tmp/formulas.tsv     # retType \t sString \t compiledString

# гейт 1 (x64): собрать эталон + обвязку, прогнать
python3 gen_ref_cpp.py /tmp/formulas.tsv 1000        # или ref_rounds.py — он сам отсеивает битый DSL
HARNESS_REF=1 ./harness/build_harness.sh --toolchain=vs2022
cd harness && wine vm_harness.exe 'Z:\tmp\formulas.tsv' --out=/tmp/gold_x64.txt --report=25

# гейт 2 (x86): тот же прогон против blob'а
HARNESS_NATIVE=1 ./harness/build_harness.sh --toolchain=vs2008
cd harness && WINEPREFIX=~/pwbuild/wine32 wine vm_harness.exe 'Z:\tmp\formulas.tsv' --out=/tmp/gold_x86.txt
```

Формулы, не проходящие в обоих контурах (битый DSL в контенте, чинить не в VM):
`tal("A","B")` (лишний аргумент у макроса), `refineScale(200,12,5)`,
`tal(...) + pSecond != 0` (арифметика указателей), `appl.GetNatureTypeInPos(rPos2D)`
(`rPos2D` — метод `ICustomFormulaPars`, а не `IUnitFormulaPars`), `%%`
(`f2l(x) %% 3` — C++ так не компилирует, в VM `%%` трактуется как `%`).
Список пропуска эталона — `ref_skip.txt`.

## Что обвязка поймала (неочевидное)

- `0.9` в C++ — это **double**: `float * 0.9` считается в double, в float
  приводится только на возврате. Float-литерал в VM давал расхождение в последний
  бит почти по всему контенту.
- `V2F` для bool читал поле `p` юниона: на x64 после записи в `i` старшие 32 бита
  остаются мусором → `false` превращался в `1`. На x86 не проявлялось.
- `talconst` = `tal(a) ? ... : 0`, а `tal(a)` включает `IsTalentBought()` — без
  него условие шире, чем у скомпилированного кода.
- `CVec2` (нетривиальный ctor) возвращается через скрытый указатель: вызов
  «через vtable-слот» с typedef'ом не совпадает по ABI и затирает vtable объекта
  (для V-сигнатур генерируется вызов через member pointer).
