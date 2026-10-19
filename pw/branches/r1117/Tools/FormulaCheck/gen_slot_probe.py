#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""gen_slot_probe.py — синтетический корпус для покрытия таблицы аксессоров.

Зачем. FormulaVM вызывает аксессоры так же, как предкомпилированный x86-код —
через vtable-слот (имя -> (интерфейс, слот, сигнатура) в FormulaAccessors.inc,
генерируется gen_accessors.py из FormulaPars.h). Неверный слот или неверная
раскладка аргумента на x64 = вызов чужого метода с тем же ABI: молча, без
падения. Реальный корпус (sString из контента) проверяет только те аксессоры,
которые встречаются в формулах; харнес печатает покрытие строкой
«COVER: аксессоров проверено=N из M».

Этот скрипт достраивает недостающее: для каждого аксессора, выразимого в DSL,
печатает формулу-пробу (TSV как у dump_formulas.py). Прогон харнеса на этом
корпусе сверяет VM с эталонным C++ (gen_ref_cpp.py) по значению И по контрольной
сумме вызовов (iface+slot+имя+аргументы) — т.е. проверяет именно слот и
маршаллинг аргументов.

Не выразимы обобщённой правилом (остаются слепой зоной, печатаются в stderr):
  * аксессоры с указателем/CVec2 в аргументах (DSL не принимает такие литералы);
  * IMiscFormulaPars с аргументами (в DSL аксессоры misc доступны только как
    mX()/mIsX() без параметров);
  * void-аксессоры (не могут быть выражением);
  * ICustomFormulaPars (receiver имеет статический тип ICustomFormulaPars только
    в части контекстов; в DSL это имя `rPos2D`).
Для части из них есть ручные пробы EXTRA (одна строка покрывает сразу несколько
аксессоров), например `appl.GetNatureTypeInPos(rPos2D) != 0` покрывает
ICustomFormulaPars.GetPos2D (iface 2, slot 0) и IMiscFormulaPars.GetNatureTypeInPos
(iface 1, slot 35, аргумент CVec2).

Запуск (из pw/branches/r1117):
  python3 Tools/FormulaCheck/gen_slot_probe.py > /tmp/slot_probe.tsv
Прогон:
  python3 Tools/FormulaCheck/gen_ref_cpp.py /tmp/slot_probe.tsv
  HARNESS_REF=1 Tools/FormulaCheck/harness/build_harness.sh --toolchain=vs2022
  wine Tools/FormulaCheck/harness/vm_harness.exe 'Z:\tmp\slot_probe.tsv' --out=...
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_accessors as GA  # noqa: E402

# символ типа из gen_accessors.TY -> литерал DSL
LIT = {"F": "1.5", "I": "2", "U": "2", "E": "1", "B": "true", "S": '"probe"'}
# символ возврата -> колонка retType в TSV (как в реальном корпусе)
RET = {"F": "float", "I": "int", "U": "int", "B": "bool"}

# Ручные пробы для аксессоров, которые обобщённое правило не строит
# (CVec2-аргументы, ICustomFormulaPars). Формы взяты из реального контента.
EXTRA = [
    ("int", "appl.GetNatureTypeInPos(rPos2D) != 0"),
]


def main():
    txt = GA.strip_comments(open(GA.SRC, "rb").read().decode("cp1251", "replace"))
    parsed = GA.parse(txt)
    n_ok, skipped = 0, {}
    for ii, iface in enumerate(GA.IFACES):
        for slot, (rc, ac, meth, ret, args) in enumerate(parsed.get(iface, [])):
            why = None
            if rc not in RET:
                why = "ret=" + rc
            elif any(c not in LIT for c in ac):
                why = "args=" + "".join(ac)
            elif ii == 1 and ac:
                why = "misc-with-args"
            elif "E" in ac:
                why = "enum-arg (эталонный C++ требует cast к enum, VM берёт int)"
            if why:
                skipped[why] = skipped.get(why, 0) + 1
                continue
            lit = [LIT[c] for c in ac]
            if ii == 1:
                # misc-аксессоры в DSL: mFoo() -> GetFoo(), mIsFoo() -> IsFoo()
                if meth.startswith("Get"):
                    name = "m" + meth[3:]
                elif meth.startswith("Is"):
                    name = "mIs" + meth[2:]
                else:
                    skipped["misc-name:" + meth] = skipped.get("misc-name:" + meth, 0) + 1
                    continue
                # в DSL misc-аксессор пишется БЕЗ скобок: правило mX -> pMisc->GetX()
                expr = name
            else:
                recv = "sender" if ii == 0 else "appl"
                expr = "%s.%s(%s)" % (recv, meth, ", ".join(lit))
            print("%s\t%s\t" % (RET[rc], expr))
            n_ok += 1
    for rt, expr in EXTRA:
        print("%s\t%s\t" % (rt, expr))
        n_ok += 1
    for k in sorted(skipped):
        sys.stderr.write("SKIP %-22s x%d\n" % (k, skipped[k]))
    sys.stderr.write("проб: %d, пропущено: %d\n" % (n_ok, sum(skipped.values())))


if __name__ == "__main__":
    main()
