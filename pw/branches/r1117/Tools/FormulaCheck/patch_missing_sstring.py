#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""patch_missing_sstring.py — дописать восстановленные <sString>/<returnType> в .xdb.

Правит ТОЛЬКО элементы, где есть непустой <compiledString> и нет <sString> (в контенте
это элементы, состоящие ровно из одного ребёнка <compiledString>), и только если blob
есть в справочнике, полученном verify_recovered.py (--json) — т.е. если текст формулы
доказан совпадением скомпилированного кода.

<compiledString> НЕ трогается: x86 (ConstructFormulaCached) приоритетно берёт blob,
поэтому поведение x86 не меняется; на x64 FormulaVM собирает формулу из sString.

Порядок детей и отступы — как у сериализатора (compiledString, returnType, sString),
переносы строк и кодировка файла сохраняются (вставка только ASCII + XML-экранирование).

Запуск:
  python3 Tools/FormulaCheck/patch_missing_sstring.py Data --map /tmp/rec.json          # dry-run
  python3 Tools/FormulaCheck/patch_missing_sstring.py Data --map /tmp/rec.json --apply
"""
import argparse
import json
import os
import re
import sys

# compiledString должен быть последним ребёнком элемента (у здоровых элементов после него
# идут returnType и sString) — так правка гарантированно не задевает целые формулы.
BLOCK_RE = re.compile(rb'(<(\w+)>(\s*))(\<compiledString\>)([^<]*)(</compiledString>)(?=\s*</\2>)', re.S)


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def patch_file(path, mapping, apply_, used):
    raw = open(path, 'rb').read()
    eol = b'\r\n' if b'\r\n' in raw else b'\n'
    out = bytearray()
    pos = 0
    n = 0
    for m in BLOCK_RE.finditer(raw):
        blob = b''.join(m.group(5).split()).decode('ascii', 'replace')
        info = mapping.get(blob)
        if not info:
            continue
        used.add(blob)
        indent = m.group(3)
        cut = indent.rfind(b'\n')
        if cut >= 0:
            indent = indent[cut + 1:].replace(b'\r', b'')
        add = (eol + indent + b'<returnType>' + info['returnType'].encode('ascii') + b'</returnType>'
               + eol + indent + b'<sString>' + esc(info['sString']).encode('utf-8') + b'</sString>')
        out += raw[pos:m.end()]
        out += add
        pos = m.end()
        n += 1
        print('  %-64s %s -> %r : %s' % (path.replace('Data/', 'Data/'), m.group(2).decode(),
                                         info['sString'], info['returnType']))
    if not n:
        return 0
    out += raw[pos:]
    if apply_:
        open(path, 'wb').write(bytes(out))
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root", nargs="?", default="Data")
    ap.add_argument("--map", required=True, help="json из verify_recovered.py --json")
    ap.add_argument("--apply", action="store_true")
    a = ap.parse_args()

    mapping = json.load(open(a.map, encoding='utf-8'))
    print("справочник: %d blob; режим: %s" % (len(mapping), "ЗАПИСЬ" if a.apply else "dry-run"))

    used = set()
    total = files = 0
    for dp, _d, fs in os.walk(a.root):
        for fn in sorted(fs):
            if not fn.endswith('.xdb'):
                continue
            n = patch_file(os.path.join(dp, fn), mapping, a.apply, used)
            if n:
                files += 1
                total += n
    print("\nзатронуто файлов: %d, элементов: %d" % (files, total))
    missing = len(mapping) - len(used)
    print("не использовано blob из справочника: %d (нормально: дубли)" % missing)
    if not a.apply:
        print("dry-run: файлы не изменены; повторить с --apply")


if __name__ == '__main__':
    main()
