#!/usr/bin/env python3
"""Разбор .ninja_log (M0 плана ускорения сборки, PLAN_client_build_speedup.md).

Эйджи ninja идут параллельно, поэтому «время проекта» = СУММА длительностей его
TU (стоимость на ядре), а не интервал wall; wall считается отдельно и по нему же
виден реальный параллелизм (sum/wall < jobs => ядра недогружены).

Использование:
    build_time.py <build_dir|ninja_log> [--top N] [--tu-only]
    build_time.py A.ninja_log B.ninja_log        # сравнение двух прогонов

Проект берётся из пути объекта (CMakeFiles/<Proj>.dir/...), тип — из команды
(cl / c1xx / rc / link). Файл только для чтения.
"""
import os
import re
import sys

DIR_RE = re.compile(r"CMakeFiles[\\/]+([^\\/]+)\.dir")


def log_path(arg):
    if os.path.isdir(arg):
        p = os.path.join(arg, ".ninja_log")
        if not os.path.exists(p):
            sys.exit("нет .ninja_log в %s" % arg)
        return p
    return arg


def parse(path):
    """-> (edges, wall_ms). edge = (start, end, out, cmd).

    .ninja_log — ЖУРНАЛ по выходам: одна строка на выход, дописывается при каждом
    прогоне, поэтому в файле смешаны несколько сборок (в vs2022-базе 2261 строка
    при 1154 выходах). Берём ПОСЛЕДНЮ запись на выход = последний прогон.
    В v5 колонка 5 — командная строка, в v6 — хэш команды (текста нет)."""
    order = {}
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        first = f.readline()
        if not first.startswith("# ninja log v"):
            sys.exit("%s: не .ninja_log (нет заголовка версии)" % path)
        n = 0
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if len(parts) < 4:
                continue
            try:
                start, end = int(parts[0]), int(parts[1])
            except ValueError:
                continue
            out, cmd = parts[3], (parts[4] if len(parts) > 4 else "")
            n += 1
            order[out] = (start, end, out, cmd)   # последняя запись побеждает
    edges = list(order.values())
    dup = n - len(edges)
    if not edges:
        return [], 0, 0
    wall = max(e[1] for e in edges) - min(e[0] for e in edges)
    return edges, wall, dup


def kind(out, cmd):
    """тип правила: в v6 командной строки нет, поэтому по расширению выхода."""
    o = out.lower()
    if o.endswith(".obj"):
        return "CXX/C"
    if o.endswith(".res"):
        return "RC"
    if o.endswith((".exe", ".dll", ".lib")):
        return "LINK"
    if o.endswith(".pch"):
        return "PCH"
    return "other"


def project(out, cmd):
    m = DIR_RE.search(out) or DIR_RE.search(cmd)
    return m.group(1) if m else "(root)"


def summarize(edges, top):
    by_proj, by_kind, tu = {}, {}, []
    for s, e, out, cmd in edges:
        d = e - s
        p = project(out, cmd)
        by_proj[p] = by_proj.get(p, 0) + d
        by_kind[kind(out, cmd)] = by_kind.get(kind(out, cmd), 0) + d
        tu.append((d, p, out))
    return by_proj, by_kind, tu


def fmt(ms):
    s = ms / 1000.0
    return ("%dm%02ds" % (int(s // 60), int(s % 60))) if s >= 90 else "%.1fs" % s


def report(path, top, want_projects=True, want_rules=True):
    edges, wall, dup = parse(path)
    if not edges:
        print("%s: пустой лог" % path)
        return {}
    by_proj, by_kind, tu = summarize(edges, top)
    total = sum(e[1] - e[0] for e in edges)
    print("== %s" % path)
    print("   эйджей: %d (последний прогон; %d более старых записей скрыто)" % (len(edges), dup))
    print("   wall: %s   сумма TU: %s   средний TU: %s" % (fmt(wall), fmt(total), fmt(total / len(edges))))
    print("   средний параллелизм: %.2f ядра (если заметно меньше -j — ядра простаивают)" %
          (total / float(wall) if wall else 0))
    if want_rules:
        print("   -- по типам правил --")
        for k, v in sorted(by_kind.items(), key=lambda kv: -kv[1]):
            print("   %10s  %s" % (fmt(v), k))
    if want_projects:
        print("   -- топ-%d проектов (сумма TU) --" % top)
        for p, v in sorted(by_proj.items(), key=lambda kv: -kv[1])[:top]:
            n = sum(1 for e in edges if project(e[2], e[3]) == p)
            print("   %10s  x%-5d %s" % (fmt(v), n, p))
    print("   -- топ-%d TU --" % top)
    for d, p, out in sorted(tu, reverse=True)[:top]:
        print("   %10s  %-22s %s" % (fmt(d), p, out))
    return by_proj


def compare(a, b, top):
    ea, wa, _ = parse(a)
    eb, wb, _ = parse(b)
    pa = summarize(ea, top)[0]
    pb = summarize(eb, top)[0]
    print("== сравнение: %s  ->  %s" % (a, b))
    print("   wall:  %s -> %s  (%+.0f%%)" % (fmt(wa), fmt(wb),
          100.0 * (wb - wa) / wa if wa else 0))
    ta = sum(e[1] - e[0] for e in ea)
    tb = sum(e[1] - e[0] for e in eb)
    print("   сумма TU: %s -> %s  (%+.0f%%)" % (fmt(ta), fmt(tb),
          100.0 * (tb - ta) / ta if ta else 0))
    print("   -- по проектам --")
    for p in sorted(set(pa) | set(pb), key=lambda p: -(pb.get(p, 0))):
        x, y = pa.get(p, 0), pb.get(p, 0)
        if x == y:
            continue
        d = 100.0 * (y - x) / x if x else float("inf")
        print("   %10s -> %10s  %+7.0f%%  %s" % (fmt(x), fmt(y), d, p))


def main():
    args, top, i = [], 20, 0
    argv = sys.argv[1:]
    while i < len(argv):
        a = argv[i]
        if a == "--top":
            top = int(argv[i + 1])
            i += 2
            continue
        if a.startswith("--top="):
            top = int(a.split("=", 1)[1])
            i += 1
            continue
        if a.startswith("--"):
            i += 1
            continue
        args.append(a)
        i += 1
    if not args:
        sys.exit(__doc__)
    if len(args) == 2:
        compare(log_path(args[0]), log_path(args[1]), top)
        return 0
    report(log_path(args[0]), top,
           want_projects="--tu-only" not in sys.argv,
           want_rules="--tu-only" not in sys.argv)
    return 0


if __name__ == "__main__":
    sys.exit(main())
