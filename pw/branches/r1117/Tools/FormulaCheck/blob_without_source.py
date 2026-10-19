#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""blob_without_source.py — формулы, у которых есть compiledString, но пуст sString.

Зачем. На x64 предкомпилированный x86-код неисполним, формула собирается из
sString (FormulaVM). Если sString пуст — собирать нечего, и формула на x64
молча возвращает 0, тогда как x86 исполняет свой blob. Это расхождение баланса,
чинится контентом (вернуть sString), а не кодом.

Попутно печатает печатаемые строки из blob'а (имена констант/аксессоров) — по ним
обычно восстанавливается исходник.

Запуск:
  python3 Tools/FormulaCheck/blob_without_source.py Data
"""
import argparse
import base64
import os
import re
import sys
import xml.etree.ElementTree as ET

ap = argparse.ArgumentParser()
ap.add_argument("root", help="каталог Data (или любой корень с *.xdb)")
a = ap.parse_args()

STR_RE = re.compile(rb'[ -~]{3,}')
total = 0
files = 0
for dp, _d, fs in os.walk(a.root):
    for fn in sorted(fs):
        if not fn.endswith(".xdb"):
            continue
        path = os.path.join(dp, fn)
        try:
            t = ET.parse(path)
        except Exception:
            continue
        files += 1
        for el in t.getroot().iter():
            s = el.find("sString")
            c = el.find("compiledString")
            if c is None:
                continue
            src = " ".join(((s.text if s is not None else "") or "").split())
            comp = " ".join((c.text or "").split())
            if not comp or src:
                continue
            try:
                blob = base64.b64decode(comp)
            except Exception:
                blob = b""
            strs = sorted(set(x.decode("cp1251", "replace") for x in STR_RE.findall(blob)))
            rt = el.find("returnType")
            ret = (rt.text or "").strip() if rt is not None else ""
            print("%s | ret=%s | blob=%d Б | %s" % (path, ret, len(blob), ", ".join(strs)))
            total += 1

print("проверено .xdb: %d, формул с blob и без sString: %d" % (files, total), file=sys.stderr)
sys.exit(1 if total else 0)
