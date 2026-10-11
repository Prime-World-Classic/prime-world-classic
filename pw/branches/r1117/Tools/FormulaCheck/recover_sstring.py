#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""recover_sstring.py — восстановление sString для формул без него.

Идея. В контенте 41 937 формул имеют и sString, и compiledString. compiledString —
детерминированная функция от sString (FormulaBuilder: тот же cpp-шаблон, тот же cl,
тот же /O-режим), поэтому одинаковый blob = одинаковый исходник. Значит для формулы
с пустым sString исходник восстанавливается точным совпадением blob'а по индексу
«blob -> sString», собранному по остальному контенту. Совпадение blob'а — это
доказательство эквивалентности кода, а не догадка.

Что не восстанавливается так (нужен декомпилятор / ручная правка):
  * уникальные blob'ы (формула больше нигде не встречается с заполненным sString);
  * формулы, где sString есть, но blob пересобран после правки констант (разные blob'ы).

Запуск:
  Tools/FormulaCheck/recover_sstring.py Data
"""
import base64
import collections
import os
import sys
import xml.etree.ElementTree as ET


def scan(root):
    """(blob -> Counter(sString)) и список формул без sString."""
    index = collections.defaultdict(collections.Counter)
    orphans = []
    n_both = 0
    for dp, _d, fs in os.walk(root):
        for fn in sorted(fs):
            if not fn.endswith('.xdb'):
                continue
            path = os.path.join(dp, fn)
            try:
                t = ET.parse(path)
            except Exception:
                continue

            def rec(el, chain):
                nonlocal n_both
                c = el.find('compiledString')
                s = el.find('sString')
                tag = el.tag
                ident = el.get('id') or ''
                if c is not None and (c.text or '').strip():
                    comp = ' '.join((c.text or '').split())
                    src = ' '.join(((s.text if s is not None else '') or '').split())
                    if src:
                        n_both += 1
                        index[comp][src] += 1
                    else:
                        orphans.append((path, ' / '.join(chain + [tag]), comp))
                    for ch in el:
                        rec(ch, chain + [tag + ('[%s]' % ident if ident else '')])
                else:
                    for ch in el:
                        rec(ch, chain + [el.tag + ('[%s]' % ident if ident else '')])
            rec(t.getroot(), [])
    return index, orphans, n_both


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else 'Data'
    index, orphans, n_both = scan(root)
    print('индекс: %d уникальных blob (всего вхождений с sString: %d), без sString: %d'
          % (len(index), n_both, len(orphans)))
    recovered = collections.Counter()
    unique = []
    for path, chain, comp in orphans:
        hits = index.get(comp)
        if hits:
            best, cnt = hits.most_common(1)[0]
            recovered[best] += 1
            print('RECOVERED  %-70s  %s  <= %r (%d совпадений%s)' %
                  (path.replace(root + '/', ''), chain, best, len(hits),
                   ', варианты: ' + ' | '.join(hits) if len(hits) > 1 else ''))
        else:
            unique.append((path, chain, comp))
    print('\nНЕ восстановлено (уникальный blob): %d' % len(unique))
    for path, chain, comp in unique:
        blob = base64.b64decode(comp)
        print('  %-70s %s  blob=%d Б' % (path.replace(root + '/', ''), chain, len(blob)))
    print('\nвосстановлено: %d из %d' % (sum(recovered.values()), len(orphans)))
    print('топ восстановленных sString:')
    for s, n in recovered.most_common(20):
        print('  %2d  %s' % (n, s))


if __name__ == '__main__':
    main()
