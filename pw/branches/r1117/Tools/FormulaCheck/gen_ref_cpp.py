#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_ref_cpp.py — эталонная (x64) реализация формул: из sString генерируется C++
теми же правилами, что и Src/FormulaBuilder/FormulaBuilder.cpp (через
verify_formulas.convert_formula), и компилируется обычным компилятором.

Зачем: предкомпилированный blob в контенте собран x86-компилятором и считает на
x87 (80-битные промежуточные значения). На x64 тот же C++ считается на SSE, где
каждая операция округляется к типу. Поэтому побитовая сверка VM с x86-blob'ом
невозможна в принципе, а вот сверка VM с «тем же C++, собранным под x64» — и есть
настоящий гейт: x64-клиент будет считать именно так.

Выход: ref_<k>.cpp в каталоге обвязки; каждый файл отдаёт
  extern "C" double HarnessRefCall_k(int idx, void const *pFirst,
                                     void const *pSecond, void const *pMisc, int *pOk)
для idx из своего диапазона (вне диапазона — возвращает 0, *pOk=0).

Запуск: python3 Tools/FormulaCheck/gen_ref_cpp.py /tmp/formulas.tsv [размер_пачки]
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import verify_formulas as VF          # noqa: E402

OUT_DIR = os.path.join(HERE, "harness")


def main():
    tsv = sys.argv[1] if len(sys.argv) > 1 else "/tmp/formulas.tsv"
    batch = int(sys.argv[2]) if len(sys.argv) > 2 else 1000

    skip = set()
    for a in sys.argv[3:]:
        if a.startswith('--skip='):
            for ln in open(a.split('=', 1)[1], encoding='utf-8'):
                ln = ln.strip()
                if ln: skip.add(int(ln))

    rows = []
    with open(tsv, encoding="utf-8", errors="replace") as f:
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if len(parts) < 2:
                continue
            rows.append((parts[0], parts[1]))

    nb = (len(rows) + batch - 1) // batch
    skipped = 0
    names = []
    for k in range(nb):
        lo, hi = k * batch, min(len(rows), (k + 1) * batch)
        out = ['#include <math.h>\n#include "FormulaPars.h"\n',
               '#define round(a) ni_round(a)\n\n']
        bodies = []
        cases = []
        linemap = []
        for i in range(lo, hi):
            if (i + 1) in skip:
                continue
            rt, src = rows[i]
            try:
                body = VF.convert_formula(src)
            except Exception as e:                      # noqa: BLE001
                skipped += 1
                sys.stderr.write("skip %d: %s\n" % (i + 1, e))
                continue
            ctype = {"int": "int", "bool": "bool", "boolean": "bool"}.get(rt, "float")
            linemap.append((sum(x.count("\n") for x in out) + sum(x.count("\n") for x in bodies) + 1, i + 1))
            # «;» отдельной строкой: в sString бывают «; // комментарий», и
            # точка с запятой в конце строки ушла бы в комментарий
            bodies.append("%s fml_%d(IUnitFormulaPars const *pFirst, IUnitFormulaPars const *pSecond,"
                          " IMiscFormulaPars const *pMisc)\n{\n  return %s\n  ;\n}\n"
                          % (ctype, i + 1, body))
            cases.append('    case %d: *pOk = 1; return (double)fml_%d((IUnitFormulaPars const *)pFirst,'
                         ' (IUnitFormulaPars const *)pSecond, (IMiscFormulaPars const *)pMisc);' % (i + 1, i + 1))
        out.append("".join(bodies))
        out.append("\nextern \"C\" double HarnessRefCall_%d(int idx, void const *pFirst, void const *pSecond,"
                   " void const *pMisc, int *pOk)\n{\n  *pOk = 0;\n  switch (idx)\n  {\n" % k)
        out.append("\n".join(cases))
        out.append("\n  default: return 0.0;\n  }\n}\n")
        with open(os.path.join(OUT_DIR, "ref_%d.map" % k), "w", encoding="utf-8") as f:
            for ln, idx in linemap:
                f.write("%d %d\n" % (ln, idx))
        path = os.path.join(OUT_DIR, "ref_%d.cpp" % k)
        with open(path, "w", encoding="utf-8") as f:
            f.write("".join(out))
        names.append("HarnessRefCall_%d" % k)
    with open(os.path.join(OUT_DIR, "ref_table.h"), "w", encoding="utf-8") as f:
        f.write("// НЕ ПРАВИТЬ РУКАМИ — сгенерировано gen_ref_cpp.py\n"
                "#ifndef REF_TABLE_H_\n#define REF_TABLE_H_\n"
                "typedef double (*RefCallFn)(int, void const *, void const *, void const *, int *);\n"
                "extern RefCallFn const kRefTable[];\n"
                "extern int const kRefTableCount;\n"
                "extern int const kRefBatchSize;\n#endif\n")
    with open(os.path.join(OUT_DIR, "ref_table.cpp"), "w", encoding="utf-8") as f:
        f.write('// НЕ ПРАВИТЬ РУКАМИ — сгенерировано gen_ref_cpp.py\n#include "ref_table.h"\n')
        for n in names:
            f.write('extern "C" double %s(int, void const *, void const *, void const *, int *);\n' % n)
        f.write("RefCallFn const kRefTable[] = {\n")
        for n in names:
            f.write("  %s,\n" % n)
        f.write("};\nint const kRefTableCount = %d;\nint const kRefBatchSize = %d;\n" % (len(names), batch))
    print("пачек: %d (по %d), формул: %d, пропущено: %d" % (nb, batch, len(rows), skipped))


if __name__ == "__main__":
    main()
