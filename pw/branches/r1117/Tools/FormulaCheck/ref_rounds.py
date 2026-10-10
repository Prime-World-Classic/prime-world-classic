#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ref_rounds.py — собрать x64-эталон формул, отсеивая формулы с битым DSL.

Одна формула с синтаксической ошибкой роняет всю пачку, поэтому: генерация ->
компиляция -> разбор строк «ref_k.cpp(NNN) : error» -> по ref_k.map понять,
какие формулы виноваты -> добавить их в skip -> повторить.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HARNESS = os.path.join(HERE, "harness")
ERR_RE = re.compile(r"ref_(\d+)\.cpp\((\d+)\)")


def load_map(k):
    out = []
    with open(os.path.join(HARNESS, "ref_%d.map" % k), encoding="utf-8") as f:
        for line in f:
            a, b = line.split()
            out.append((int(a), int(b)))
    return out


def blame(k, ln):
    m = load_map(k)
    cand = [idx for start, idx in m if start <= ln]
    return max(cand) if cand else None


def main():
    tsv = sys.argv[1] if len(sys.argv) > 1 else "/tmp/formulas.tsv"
    skipfile = os.path.join(HARNESS, "ref_skip.txt")
    skip = set()
    if os.path.exists(skipfile):
        skip = {int(x) for x in open(skipfile, encoding="utf-8") if x.strip()}

    for it in range(10):
        args = [sys.executable, os.path.join(HERE, "gen_ref_cpp.py"), tsv, "1000"]
        if skip:
            with open(skipfile, "w", encoding="utf-8") as f:
                f.write("\n".join(str(x) for x in sorted(skip)))
            args.append("--skip=" + skipfile)
        subprocess.run(args, check=True, capture_output=True)

        env = dict(os.environ, HARNESS_REF="1")
        r = subprocess.run([os.path.join(HARNESS, "build_harness.sh"), "--toolchain=vs2022"],
                           cwd=HARNESS, env=env, capture_output=True, text=True, timeout=3600)
        log = r.stdout + r.stderr
        bad = set()
        for line in log.splitlines():
            if "error" not in line:
                continue                      # warning'ы не виновники
            m = ERR_RE.search(line)
            if not m:
                continue
            idx = blame(int(m.group(1)), int(m.group(2)))
            if idx:
                bad.add(idx)
        nerr = log.count("error C")
        print("итер %d: ошибок cl=%d, виновников=%d, всего в skip=%d" % (it, nerr, len(bad), len(skip)))
        if not bad:
            if nerr:
                print(log[:4000])
            break
        skip |= bad
        with open(skipfile, "w", encoding="utf-8") as f:
            f.write("\n".join(str(x) for x in sorted(skip)))
    print("готово; пропущено формул:", len(skip))


if __name__ == "__main__":
    main()
