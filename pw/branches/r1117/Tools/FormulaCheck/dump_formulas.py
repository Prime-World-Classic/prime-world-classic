#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""dump_formulas.py — выгрузить формулы контента в TSV для harness'а:
   returnType \t sString \t compiledString(base64)
sString сворачивается в одну строку (tab/newline -> пробел).
Уникальные sString — первые вхождения (одинаковый код в контенте дублируется).
Запуск: python3 Tools/FormulaCheck/dump_formulas.py Data > /tmp/formulas.tsv [--all]
"""
import argparse, os, sys, xml.etree.ElementTree as ET

ap = argparse.ArgumentParser()
ap.add_argument("root")
ap.add_argument("--all", action="store_true", help="не дедуплицировать по sString")
a = ap.parse_args()

seen = set()
n = 0
for dp, _d, fs in os.walk(a.root):
    for fn in sorted(fs):
        if not fn.endswith(".xdb"):
            continue
        try:
            t = ET.parse(os.path.join(dp, fn))
        except Exception:
            continue
        for el in t.getroot().iter():
            s = el.find("sString")
            if s is None:
                continue
            src = " ".join((s.text or "").split())
            if not src:
                continue
            c = el.find("compiledString")
            comp = " ".join((c.text or "").split()) if c is not None and c.text else ""
            rt = el.find("returnType")
            ret = (rt.text or "float").strip() if rt is not None and rt.text else "float"
            if not a.all:
                if src in seen:
                    continue
                seen.add(src)
            n += 1
            sys.stdout.write("%s\t%s\t%s\n" % (ret, src, comp))
sys.stderr.write("выгружено формул: %d\n" % n)
