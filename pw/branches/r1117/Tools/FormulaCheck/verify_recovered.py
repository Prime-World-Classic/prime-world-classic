#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""verify_recovered.py — проверка восстановленных sString перекомпиляцией.

recover_sstring.py находит для формул без <sString> такие тексты, чей скомпилированный
blob совпадает побайтово. Побайтовое совпадение blob'а — сильный аргумент, но не
доказательство: текст мог быть другим (комментарии, `0` vs `0.0f`). Здесь кандидат
реально компилируется ТЕМ же компилятором (Tools/CCompiler/cl.exe под Wine) и сравнивается
с хранимым blob'ом по нормализованной сигнатуре (как в batch_check.py).

Перебираются варианты текста (у одного blob их несколько) и returnType (float/int/bool —
в таких элементах <returnType> отсутствует, а тип возврата влияет на кодогенерацию).

Запуск:
  python3 Tools/FormulaCheck/verify_recovered.py Data [--work /tmp/vrec]
"""
import argparse
import base64
import collections
import os
import subprocess
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import batch_check as BC              # noqa: E402
import recompile_check as RC          # noqa: E402
import recover_sstring as RS          # noqa: E402

RET_TYPES = ['float', 'int', 'bool']


def fresh_signatures(obj_path):
    """batch_check.fresh_signatures, но tag остаётся строкой (у нас g<r>c<variant>)."""
    import re
    c = RC.Coff(obj_path)
    code_sec = next((s for s in c.sections
                     if (s["flags"] & RC.IMAGE_SCN_CNT_CODE)
                     and not s["name"].startswith(".debug") and s["name"] != ".drectve"), None)
    if code_sec is None:
        return {}
    raw = code_sec["raw"]
    funcs = []
    for s in c.symbols:
        m = re.match(r"\?fml_(\w+)@@", s["name"])
        if m and 0 < s["sec"] <= c.nsec and c.sections[s["sec"] - 1] is code_sec:
            funcs.append((m.group(1), s["value"], s["name"]))
    funcs.sort(key=lambda x: x[1])
    out = {}
    for i, (tag, off, name) in enumerate(funcs):
        end = funcs[i + 1][1] if i + 1 < len(funcs) else len(raw)
        sig = bytearray(raw[off:end])
        relocs = []
        for r in code_sec["rels"]:
            if not (off <= r["offs"] < end):
                continue
            buf, tgt = c.sym_target(r["sym"])
            if r["type"] == RC.IMAGE_REL_I386_DIR32:
                relocs.append((r["offs"] - off, RC.target_content(buf, tgt)
                               if buf is not None else ("ext", tgt)))
            elif r["type"] == RC.IMAGE_REL_I386_REL32:
                relocs.append((r["offs"] - off, ("extrel", tgt)))
            else:
                relocs.append((r["offs"] - off, ("reloctype%d" % r["type"], None)))
            for k in range(4):
                if r["offs"] - off + k < len(sig):
                    sig[r["offs"] - off + k] = 0
        out[tag] = (bytes(sig), sorted(relocs))
    return out


def scan_with_src(root, wanted):
    """элементы с blob из wanted и непустым sString: {blob: (sString, путь)}"""
    out = {}
    for dp, _d, fs in os.walk(root):
        for fn in sorted(fs):
            if not fn.endswith('.xdb'):
                continue
            path = os.path.join(dp, fn)
            try:
                t = ET.parse(path)
            except ET.ParseError as e:
                print("не парсится %s: %s" % (path, e))
                continue
            for el in t.getroot().iter():
                c = el.find('compiledString')
                s = el.find('sString')
                if c is None or not (c.text or '').strip():
                    continue
                comp = ' '.join((c.text or '').split())
                if comp not in wanted:
                    continue
                src = ' '.join(((s.text if s is not None else '') or '').split())
                if src:
                    out.setdefault(comp, (src, path))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root", nargs="?", default="Data")
    ap.add_argument("--work", default="/tmp/vrec")
    ap.add_argument("--json", help="записать доказанное соответствие blob -> {sString, returnType}")
    ap.add_argument("--check-json", help="режим проверки после патча: для blob из справочника "
                                         "взять ТЕКУЩИЙ контент и убедиться, что он компилируется в него")
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True)

    index, orphans, _n = RS.scan(a.root)
    items = []
    tags = {}
    if a.check_json:
        import json
        cm = json.load(open(a.check_json, encoding="utf-8"))
        found = scan_with_src(a.root, cm)
        print("режим проверки патча: blob в справочнике %d, найдено в контенте %d"
              % (len(cm), len(found)))
        groups = {b: [(p, "")] for b, (_s, p) in found.items()}
        for i, b in enumerate(found):
            tag = "p%dc0" % i
            tags[tag] = (i, cm[b]["sString"], cm[b]["returnType"])
            items.append((tag, cm[b]["sString"], cm[b]["returnType"]))
    else:
        groups = collections.OrderedDict()
        for path, chain, comp in orphans:
            groups.setdefault(comp, []).append((path, chain))
        print("уникальных blob без sString: %d (всего вхождений: %d)"
              % (len(groups), len(orphans)))
        for i, (comp, where) in enumerate(groups.items()):
            variants = list(index.get(comp, collections.Counter()))
            for j, s in enumerate(variants):
                for k, rt in enumerate(RET_TYPES):
                    tag = "g%dr%dc%d" % (i, k, j)
                    tags[tag] = (i, s, rt)
                    items.append((tag, s, rt))
            print("  blob %d: %d вариант(ов) текста, %d вхождений: %s"
                  % (i, len(variants), len(where), " | ".join(repr(x) for x in variants[:6])))

    # хранимые сигнатуры
    stored = {}
    for i, comp in enumerate(groups):
        try:
            stored[i] = RC.stored_signature(comp)
        except Exception as e:
            print("blob %d: не разобран (%s)" % (i, e))

    matched = {}
    BATCH = 100
    for b0 in range(0, len(items), BATCH):
        batch = items[b0:b0 + BATCH]
        cpp, start = BC.gen_batch_cpp([(t, s, rt) for t, s, rt in batch])
        path = os.path.join(a.work, "b%d.cpp" % b0)
        open(path, "w", encoding="cp1251").write(cpp)
        obj, log = BC.compile_batch(path, a.work)
        if obj is None:
            print("пачка %d: ошибка компиляции:\n%s" % (b0, log[:2000]))
            continue
        fresh = fresh_signatures(obj)
        for tag, (fsig, frels) in fresh.items():
            i, s, rt = tags.get(tag, (None, None, None))
            if i is None or i in matched or i not in stored:
                continue
            f_code = BC.cut(fsig, False)
            s_code = BC.cut(stored[i][0], True)
            f_code, f_rel = BC.normalize_local_calls(f_code, [x for x in frels if x[0] < len(f_code)])
            s_code, s_rel = BC.normalize_local_calls(s_code, [x for x in stored[i][1] if x[0] < len(s_code)])
            if f_code == s_code and f_rel == s_rel:
                matched[i] = (s, rt)
                print("MATCH blob %d  sString=%r  returnType=%s" % (i, s, rt))
            elif f_code == s_code:
                print("CONST_DIFF blob %d  %r (%s): %s vs %s"
                      % (i, s, rt, BC.fmt_rel(f_rel)[:4], BC.fmt_rel(s_rel)[:4]))
            else:
                print("CODE_DIFF  blob %d  %r (%s): len %d vs %d"
                      % (i, s, rt, len(f_code), len(s_code)))

    print("\nдоказано совпадением кода: %d из %d blob" % (len(matched), len(groups)))
    if a.json:
        import json
        out = {}
        for i, comp in enumerate(groups):
            if i in matched:
                s, rt = matched[i]
                out[comp] = dict(sString=s, returnType=rt, n=len(groups[comp]),
                                 where=[p for p, _c in groups[comp][:3]])
        json.dump(out, open(a.json, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
        print("записано: %s (%d blob)" % (a.json, len(out)))
    for i, comp in enumerate(groups):
        where = groups[comp]
        s, rt = matched.get(i, (None, None))
        print("blob %d  %s  %s" % (i, ("OK   %r : %s" % (s, rt)) if s else "НЕ ДОКАЗАНО",
                                   "; ".join(p.split('/', 1)[-1] + ' :: ' + c for p, c in where[:3])
                                   + (' … +%d' % (len(where) - 3) if len(where) > 3 else '')))


if __name__ == "__main__":
    main()
