#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""gen_accessors.py — генерирует Src/System/FormulaAccessors.inc из
Data/GameLogic/FormulaPars.h.

Зачем: VM формул вызывает аксессоры интерфейсов формул так же, как это делает
предкомпилированный x86-код, — через vtable-слот (`call [this + 4*slot]`).
Порядок слотов = порядок объявления virtual в FormulaPars.h (это подтверждено
аудитом: verify_formulas.py сверял disp'и 41 918 скомпилированных формул и
получил MATCH). Поэтому таблицу «имя -> (интерфейс, слот, сигнатура)» надо
ГЕНЕРИРОВАТЬ из заголовка, а не писать руками: сдвиг слота при правке заголовка
= тихая порча всех формул.

Запуск (из pw/branches/r1117):
  python3 Tools/FormulaCheck/gen_accessors.py > Src/System/FormulaAccessors.inc
После правки FormulaPars.h — перегенерировать и коммитить .inc (генератор
печатает в файл шапку «НЕ ПРАВИТЬ РУКАМИ»).
"""
import os
import re
import sys

R1117 = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(R1117, "Data", "GameLogic", "FormulaPars.h")

IFACES = ["IUnitFormulaPars", "IMiscFormulaPars", "ICustomFormulaPars"]

# тип -> канонический символ (для классификации сигнатур)
TY = {
    "float": "F", "bool": "B", "int": "I", "unsigned int": "U", "unsigned": "U",
    "CVec2": "V", "char": "S",
    "IUnitFormulaPars": "P", "IMiscFormulaPars": "P", "ICustomFormulaPars": "P",
    "void": "X",
    # enum'ы из FormulaPars.h — все int-совместимы
    "UnitCheckID": "E", "ApplicatorSearchType": "E", "AbilityID": "E",
    "EAbilityScaleMode": "E", "HeroStatisticsId": "E", "HeroClassId": "E",
}


def canon_type(t):
    t = t.strip()
    t = re.sub(r"\bconst\b", " ", t)
    t = re.sub(r"\bstruct\b", " ", t)
    t = re.sub(r"\*", " ", t)
    t = " ".join(t.split())
    if t in TY:
        return TY[t], t
    raise SystemExit("неизвестный тип в FormulaPars.h: %r" % t)


def strip_comments(txt):
    """Вырезать // и /* */ — закомментированные virtual сдвигают нумерацию
    vtable-слотов (в FormulaPars.h есть закомментированный дубль GetObjectMaster)."""
    txt = re.sub(r"/\*.*?\*/", " ", txt, flags=re.S)
    txt = re.sub(r"//[^\n]*", " ", txt)
    return txt


def parse(txt):
    """{iface: [(ret_canon, [arg_canon...], meth, ret_c, args_src)]}"""
    out = {}
    for m in re.finditer(r"struct\s+(\w+)\s*\{(.*?)\n\};", txt, re.S):
        name, body = m.group(1), m.group(2)
        if name not in IFACES:
            continue
        methods = []
        for vm in re.finditer(r"virtual\s+([^;=]+?)\b(\w+)\s*\(([^)]*)\)\s*(const)?\s*(?:=\s*0)?\s*;", body):
            ret, meth, args, _ = vm.group(1), vm.group(2), vm.group(3), vm.group(4)
            ret = " ".join(ret.split())
            rc, _ = canon_type(ret)
            ac = []
            if args.strip():
                for a in args.split(","):
                    a = re.sub(r"=.*$", "", a).strip()          # default value
                    m = re.match(r"^(.*?)([A-Za-z_]\w*)$", a)   # имя параметра
                    if m and m.group(1).strip():
                        a = m.group(1).strip()
                    ac.append(canon_type(a)[0])
            methods.append((rc, ac, meth, ret, args))
        out[name] = methods
    return out


def enum_blocks(txt):
    """[(namespace_or_None, enum_body)] — с учётом `namespace X { enum {...} }`."""
    ns_spans = []
    for m in re.finditer(r"namespace\s+(\w+)\s*\{", txt):
        j = m.end() - 1
        depth = 0
        while j < len(txt):
            if txt[j] == "{":
                depth += 1
            elif txt[j] == "}":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        ns_spans.append((m.group(1), m.start(), j))
    out = []
    for m in re.finditer(r"enum\s+(?:[A-Za-z_]\w*\s*)?\{([^}]*)\}", txt):
        ns = None
        for name, a, b in ns_spans:
            if a <= m.start() < b:
                ns = name
                break
        out.append((ns, m.group(1)))
    return out


def parse_enums(txt):
    """[(имя, значение)] по всем enum FormulaPars.h; имена из namespace
    регистрируются и как `X`, и как `Namespace::X`."""
    out = []
    known = {}

    def ev(expr):
        e = expr.strip().replace("::", "__")
        def sub(m):
            k = m.group(0)
            if k in known:
                return str(known[k])
            raise ValueError("неизвестный идентификатор %r" % k)
        e2 = re.sub(r"[A-Za-z_]\w*(?:__[A-Za-z_]\w*)?", sub, e)
        if not re.match(r"^[0-9+\-*/%()<>|&^ ~\t]*$", e2):
            raise ValueError("невычислимое выражение %r" % expr)
        return int(eval(e2))

    for ns, body in enum_blocks(txt):
        auto = 0
        for item in body.split(","):
            item = re.sub(r"//[^\n]*", "", item).strip()
            if not item:
                continue
            mm = re.match(r"([A-Za-z_]\w*)\s*(?:=\s*(.*))?$", item, re.S)
            if not mm:
                raise ValueError("непонятный элемент enum: %r" % item)
            name, expr = mm.group(1), mm.group(2)
            val = ev(expr) if expr else auto
            auto = val + 1
            known[name] = val
            if ns:
                known["%s__%s" % (ns, name)] = val
                out.append(("%s::%s" % (ns, name), val))
            out.append((name, val))
    return out


def emit_mock(parsed):
    """mock-реализации интерфейсов формул для golden-диффа (harness)."""
    RET = {"F": "float", "B": "bool", "I": "int", "U": "unsigned int", "E": "int",
           "V": "CVec2", "P": "void const *", "S": "char const *", "X": "void"}
    CALL = {"F": "MockF", "B": "MockB", "I": "MockI", "U": "MockI", "E": "MockI",
            "V": "MockV", "P": "MockP", "X": "MockX"}
    print("// НЕ ПРАВИТЬ РУКАМИ — сгенерировано gen_accessors.py --mock")
    print("#ifndef MOCKFORMULAPARS_H_")
    print("#define MOCKFORMULAPARS_H_")
    print('#include "../../../Data/GameLogic/FormulaPars.h"')
    print("")
    print("float  MockF(int nIface, int nSlot, char const * cpName);")
    print("int    MockI(int nIface, int nSlot, char const * cpName);")
    print("bool   MockB(int nIface, int nSlot, char const * cpName);")
    print("CVec2  MockV(int nIface, int nSlot, char const * cpName);")
    print("void const * MockP(int nKind, int nSelfIface);   // 0 = unit, 1 = misc")
    print("void   MockX(int nIface, int nSlot, char const * cpName);")
    print("")
    q = chr(34)
    UNIT_PTR = ("GetObjectMaster", "GetObjectFavorite", "GetObjectTarget")
    for ii, iface in enumerate(IFACES):
        print("struct Mock%s : public %s" % (iface[1:], iface))
        print("{")
        for slot, (rc, ac, meth, ret, args) in enumerate(parsed.get(iface, [])):
            params = []
            k = 0
            for a in args.split(","):
                a = re.sub(r"=.*$", "", a).strip()
                if not a:
                    continue
                m = re.match(r"^(.*?)([A-Za-z_]\w*)$", a)
                ty = m.group(1).strip() if (m and m.group(1).strip()) else a
                params.append("%s a%d" % (ty.replace("const", "const").strip(), k))
                k += 1
            ps = ", ".join(params)
            has_str = any("char" in x for x in params)
            nm = "Mock%s" % iface[1:]
            if rc == "P":
                # тип возврата решает, какой mock-объект возвращать
                if "IUnitFormulaPars" in ret:    kind = 0
                elif "ICustomFormulaPars" in ret: kind = 2
                else:                            kind = 1
                body = "return (%s)MockP(%d, %d);" % (ret, kind, ii)
            else:
                key = "cpName" if has_str else "0"
                # имя метода передаём ключом: mock возвращает значения по имени
                body = ("return %s(%d, %d, " + q + "%s" + q + ");") % (CALL[rc], ii, slot, meth)
            rtype = ret if rc == "P" else RET[rc]
            print("  virtual %s %s(%s) const { %s }" % (rtype, meth, ps, body))
        print("};")
        print("")
    print("#endif")


def main():
    txt = strip_comments(open(SRC, "rb").read().decode("cp1251", "replace"))
    parsed = parse(txt)
    if "--mock" in sys.argv:
        emit_mock(parsed)
        return

    sigs = {}          # sigkey -> (ret, args)
    entries = []       # (iface_idx, name, slot, sigkey, kind)
    for ii, iface in enumerate(IFACES):
        for slot, (rc, ac, meth, ret, args) in enumerate(parsed.get(iface, [])):
            key = "%s__%s" % (rc, "".join(ac))
            sigs.setdefault(key, (rc, ac))
            # FR_NONE=0, FR_UNIT=1, FR_MISC=2, FR_CUSTOM=3 — по типу возврата:
            # какой интерфейс у результата (нужен для цепочек .oX->meth()).
            kind = 0
            if "IUnitFormulaPars" in ret:
                kind = 1
            elif "IMiscFormulaPars" in ret:
                kind = 2
            elif "ICustomFormulaPars" in ret:
                kind = 3
            entries.append((ii, meth, slot, key, kind))

    # ---- enum-константы из FormulaPars.h (имя -> значение)
    enums = parse_enums(txt)

    # ---- typedef'ы
    TD = {"F": "float", "B": "bool", "I": "int", "U": "unsigned int",
          "V": "CVec2", "P": "void const *", "E": "int", "X": "void",
    "S": "char const *"}
    lines = []
    w = lines.append
    w("// НЕ ПРАВИТЬ РУКАМИ — сгенерировано Tools/FormulaCheck/gen_accessors.py")
    w("// из Data/GameLogic/FormulaPars.h (порядок virtual = vtable-слоты).")
    w("// Перегенерировать: python3 Tools/FormulaCheck/gen_accessors.py > Src/System/FormulaAccessors.inc")
    w("")
    w("enum EFormulaIface { FI_UNIT = 0, FI_MISC = 1, FI_CUSTOM = 2, FI_COUNT = 3 };")
    w("")
    w("// --- сигнатуры аксессоров (thiscall: self первым аргументом) ---")
    sigid = {}
    for n, key in enumerate(sorted(sigs)):
        rc, ac = sigs[key]
        sigid[key] = n
        ret = TD[rc]
        args = ", ".join(["void const *self"] + [TD[a] + (" a%d" % i) for i, a in enumerate(ac)])
        w("// %s -> %s(%s)" % (key, ret, ",".join(ac)))
        w("typedef %s (__thiscall *Sig_%d)(%s);" % (ret, n, args))
    w("")
    w("enum EFormulaAccSig {")
    for key in sorted(sigs):
        w("  SIG_%s = %d," % (re.sub(r"[^A-Z0-9]", "_", key).upper(), sigid[key]))
    w("  SIG_COUNT = %d" % len(sigs))
    w("};")
    w("")
    w("struct FormulaAccEntry {")
    w("  char const   *cpName;   // имя метода как в FormulaPars.h")
    w("  unsigned char nIface;   // EFormulaIface")
    w("  unsigned char nSig;     // EFormulaAccSig")
    w("  unsigned short nSlot;   // vtable-слот (порядок объявления virtual)")
    w("};")
    w("")
    w("static const FormulaAccEntry kFormulaAcc[] = {")
    for ii, meth, slot, key, kind in entries:
        w('  {"%s", %d, %d, %d},' % (meth, ii, sigid[key], slot))
    w("};")
    w("")
    w("static const int kFormulaAccCount = %d;" % len(entries))
    w("static const unsigned char kFormulaAccKind[] = {")
    for ii, meth, slot, key, kind in entries:
        w('  %d,  // %s.%s' % (kind, IFACES[ii], meth))
    w("};")
    w("")
    w("// --- enum-константы, доступные формулам (из FormulaPars.h) ---")
    w("struct FormulaEnumEntry { char const *cpName; int nValue; };")
    w("static const FormulaEnumEntry kFormulaEnum[] = {")
    for name, val in enums:
        w('  {"%s", %d},' % (name, val))
    w("};")
    w("static const int kFormulaEnumCount = %d;" % len(enums))
    w("")

    # --- диспетчер вызова
    w("// --- вызов аксессора: аргументы снимаются со стека VM, результат кладётся ---")
    TYMAP = {"F": "FT_F", "D": "FT_D", "I": "FT_I", "U": "FT_I", "E": "FT_I",
             "B": "FT_B", "V": "FT_V", "P": "FT_P", "S": "FT_P", "X": "FT_U"}
    w("static const unsigned char kFormulaSigArgType[SIG_COUNT * 6] = {")
    for key in sorted(sigs):
        ac = list(sigs[key][1]) + [""] * (6 - len(sigs[key][1]))
        w("  %s,  // %s" % (", ".join(TYMAP[a] if a else "FT_U" for a in ac), key))
    w("};")
    w("static const unsigned char kFormulaSigArgs[SIG_COUNT] = {")
    for key in sorted(sigs):
        w("  %d,  // %s" % (len(sigs[key][1]), key))
    w("};")
    w("static const unsigned char kFormulaSigRet[SIG_COUNT] = {")
    for key in sorted(sigs):
        w("  %s,  // %s" % (TYMAP[sigs[key][0]], key))
    w("};")
    w("")

    # --- прямые вызовы для сигнатур с CVec2: у CVec2 нетривиальный ctor, MSVC
    # возвращает/принимает такой тип через скрытый указатель, и вызов
    # «через vtable-слот» с typedef'ом даёт несовпадение ABI (проверено
    # экспериментом: затирается vtable вызываемого объекта).
    w("struct FmVec2Raw { float x, y; };")
    w("static void FormulaAccCallVec(int nAcc, void const *self, void const *const *ap,")
    w("                            float &rf, int &ri, bool &rb, CVec2 &rv, void const *&rp)")
    w("{")
    w("  switch (nAcc)")
    w("  {")
    n_direct = 0
    for idx, (ii, meth, slot, key, kind) in enumerate(entries):
        rc, ac = sigs[key]
        if rc != "V" and "V" not in ac:
            continue
        iface = IFACES[ii]
        args_decl = []
        k = 0
        for a in ac:
            if a == "V":
                args_decl.append("CVec2(((FmVec2Raw const *)ap[%d])->x, ((FmVec2Raw const *)ap[%d])->y)" % (k, k))
            elif a == "S":
                args_decl.append("*reinterpret_cast<char const * const *>(ap[%d])" % k)
            elif a == "P":
                args_decl.append("*reinterpret_cast<void const * const *>(ap[%d])" % k)
            else:
                args_decl.append("*reinterpret_cast<%s const *>(ap[%d])" % (TD[a], k))
            k += 1
        call = "((%s const *)self)->%s(%s)" % (iface, meth, ", ".join(args_decl))
        w("  case %d:  // %s.%s" % (idx, iface, meth))
        if rc == "V":
            w("    rv = %s; break;" % call)
        elif rc == "F":
            w("    rf = %s; break;" % call)
        elif rc == "B":
            w("    rb = %s; break;" % call)
        elif rc in ("I", "U", "E"):
            w("    ri = (int)%s; break;" % call)
        elif rc == "P":
            w("    rp = %s; break;" % call)
        n_direct += 1
    w('  default: NI_ASSERT(false, "прямой вызов только для сигнатур с CVec2"); break;')
    w("  }")
    w("}")
    sys.stderr.write("прямых вызовов (CVec2): %d\n" % n_direct)
    w("")

    w("static const unsigned char kFormulaSigHasV[SIG_COUNT] = {")
    for key in sorted(sigs):
        rc, ac = sigs[key]
        w("  %d,  // %s" % (1 if (rc == "V" or "V" in ac) else 0, key))
    w("};")
    w("")
    w("static void FormulaAccCall(int nSig, void *fp, void const *self, void const *const *ap,")
    w("                         float &rf, int &ri, bool &rb, CVec2 &rv, void const *&rp)")
    w("{")
    w("  switch (nSig)")
    w("  {")
    for key in sorted(sigs):
        rc, ac = sigs[key]
        cast = "((Sig_%d)fp)" % sigid[key]
        argl = ", ".join(["self"] + ["*reinterpret_cast<%s const *>(ap[%d])" % (TD[a], i)
                                     for i, a in enumerate(ac)])
        call = "%s(%s)" % (cast, argl)
        w("  case SIG_%s:" % re.sub(r"[^A-Z0-9]", "_", key).upper())
        if rc == "F":
            w("    rf = %s; break;" % call)
        elif rc == "B":
            w("    rb = %s; break;" % call)
        elif rc in ("I", "U", "E"):
            w("    ri = (int)%s; break;" % call)
        elif rc == "V":
            w("    rv = %s; break;" % call)
        elif rc == "P":
            w("    rp = %s; break;" % call)
        else:
            w("    %s; break;" % call)
    w("  default: NI_ASSERT(false, \"unknown formula accessor signature\"); break;")
    w("  }")
    w("}")
    w("")
    print("\n".join(lines))
    sys.stderr.write("сгенерировано: %d аксессоров, %d сигнатур\n" % (len(entries), len(sigs)))
    for key in sorted(sigs):
        sys.stderr.write("  %-14s ret=%s args=%s\n" % (key, sigs[key][0], "".join(sigs[key][1]) or "-"))


if __name__ == "__main__":
    main()
