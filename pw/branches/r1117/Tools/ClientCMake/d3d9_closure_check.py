#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
d3d9_closure_check.py — гейт среза «вычищаем D3D9 из заголовков клиента» (этап 4
плана docs/PLAN_NVRHI.md, шаг 14).

Считает по графу клиента (TU берутся из model.json, как их берёт gen_cmake.py),
какие TU ТРАНЗИТИВНО дотягиваются до заголовков D3D9/D3DX/DirectInput/D3DCompiler.
Компилятор не запускается: обход только по #include, поэтому работает быстро и
не зависит от остальных ошибок порта.

Разрешение include'ов имитирует MSVC: quoted — сначала каталог включающего файла
(по всему стеку), затем каталоги из -I; angle — только каталоги из -I. Каталоги -I
берутся из сгенерированных projects/*.cmake (тот же список, что у реального билда).

Выход:
  - сколько TU дотягиваются до каждого вендорного заголовка;
  - какие заголовки Src/ НЕСУТ эту зависимость (т.е. сами включают d3d9.h и т.п.);
  - построчный список TU вне allowlist'а (файлы, которым D3D9 запрещён).

Allowlist — TU, которым D3D9 разрешён по построению (реализация D3D9-бэкенда и
прочий Windows-only код). Он в ALLOW below; сокращать его — смысл среза.

Запуск:
  python3 Tools/ClientCMake/d3d9_closure_check.py            # сводка
  python3 Tools/ClientCMake/d3d9_closure_check.py --list     # список TU вне allowlist
  python3 Tools/ClientCMake/d3d9_closure_check.py --why Render/texture.h  # цепочка до d3d9
"""

import json
import os
import re
import sys
from collections import deque

HERE = os.path.dirname(os.path.abspath(__file__))
R1117 = os.path.abspath(os.path.join(HERE, "..", ".."))
SRC = os.path.join(R1117, "Src")
GEN = os.path.join(HERE, "gen", "x64-wine")

# Вендорные заголовки, до которых клиенту нельзя дотягиваться (имена как в #include)
FORBIDDEN = [
    "d3d9.h",
    "d3dx9.h",
    "d3dcompiler.h",
    "dinput.h",
    "dinputd.h",
    "dxerr.h",
    "dxfile.h",
    "d3dx9math.h",
]

# TU (путь относительно Src/), которым D3D9 разрешён: реализация D3D9-бэкенда,
# D3DX-загрузка контента, Windows-only обвязка. Сокращать этот список — цель среза.
ALLOW = set([
    "Render/d3d9backend.cpp",
    "Render/DxIntrusivePtr.h",
    "Render/DxResourcesControl.cpp",
    "Render/ReadDDS.cpp",
    "Render/renderflagsconverter.cpp",
    "Render/shadercompiler.cpp",
    "Render/stdafx.cpp",
    "ShaderCompiler/ShaderCompiler.cpp",
])

RE_QUOTED = re.compile(rb'^\s*#\s*include\s*"([^"]+)"', re.M)
RE_ANGLE = re.compile(rb'^\s*#\s*include\s*<([^>]+)>', re.M)


def include_dirs():
    """Каталоги -I из сгенерированных projects/*.cmake + каталоги Src/ (как в билде)."""
    dirs = []
    for name in sorted(os.listdir(os.path.join(GEN, "projects"))):
        if not name.endswith(".cmake"):
            continue
        text = open(os.path.join(GEN, "projects", name), "rb").read().decode("utf-8", "replace")
        for m in re.finditer(r'"(\$\{[^}]*\}[^"]*)"', text):
            pass
        for m in re.finditer(r'include_directories\(([^)]*)\)', text, re.S):
            for tok in re.findall(r'[^\s)]+', m.group(1)):
                tok = tok.strip('"')
                tok = tok.replace("${R1117}", R1117).replace("${PW_SRC}", SRC)
                tok = re.sub(r'\$\{[A-Za-z0-9_]+\}', lambda mm: "", tok)
                tok = os.path.normpath(tok) if tok else ""
                if tok and os.path.isdir(tok):
                    dirs.append(tok)
    # каталоги Src/ — в билде включаются через ${PW_SRC}/<project> и относительные
    # quoted-пути; для обхода добавляем все каталоги Src/ (MSVC и так ищет по стеку)
    for root, sub, files in os.walk(SRC):
        if "_ShippingSingleExe" in root:
            continue
        dirs.append(root)
    seen, out = set(), []
    for d in dirs:
        if d not in seen:
            seen.add(d)
            out.append(d)
    return out


def graph_sources():
    """TU графа клиента из model.json — те же фильтры, что у gen_cmake.py:441."""
    model = json.load(open(os.path.join(HERE, "model.json"), encoding="utf-8"))
    out = []
    for name, proj in model.items():
        for f in proj["files"]:
            absf, low = f["abs"], f["rel"].lower()
            if not os.path.isfile(absf):
                continue
            per = f["cfg"].get("VCCLCompilerTool", {})
            if per.get("ExcludedFromBuild") == "true":
                continue
            if not low.endswith((".cpp", ".c")):
                continue
            out.append(os.path.relpath(absf, SRC).replace("\\", "/"))
    return out


class Scanner:
    def __init__(self, incdirs):
        self.incdirs = incdirs
        self.cache = {}
        self.resolve_cache = {}

    def resolve(self, name, cur_dir, angle_only=False):
        key = (name, cur_dir, angle_only)
        r = self.resolve_cache.get(key)
        if r is not None:
            return r
        bases = [] if angle_only else [cur_dir]
        bases += self.incdirs
        res = None
        for b in bases:
            p = os.path.normpath(os.path.join(b, name))
            if os.path.isfile(p):
                res = p
                break
        self.resolve_cache[key] = res
        return res

    def includes(self, path):
        if path in self.cache:
            return self.cache[path]
        try:
            data = open(path, "rb").read()
        except OSError:
            self.cache[path] = []
            return []
        cur = os.path.dirname(path)
        res = []
        for m in RE_QUOTED.findall(data):
            n = m.decode("utf-8", "replace").replace("\\", "/")
            p = self.resolve(n, cur)
            if p:
                res.append(p)
        for m in RE_ANGLE.findall(data):
            n = m.decode("utf-8", "replace")
            p = self.resolve(n, cur, angle_only=True)
            if p:
                res.append(p)
        self.cache[path] = res
        return res

    def reach(self, tu, targets):
        """BFS от TU: возвращает {target_path: [цепочка include'ов]}."""
        base = os.path.dirname(tu)
        found = {}
        start = os.path.normpath(tu)
        q = deque([(start, [start])])
        seen = {start}
        while q:
            cur, chain = q.popleft()
            for nxt in self.includes(cur):
                if nxt in seen:
                    continue
                seen.add(nxt)
                nchain = chain + [nxt]
                name = os.path.basename(nxt)
                if name in targets and nxt not in found:
                    found[name] = nchain
                # в вендор не идём глубже — он сам по себе цель
                if name in targets:
                    continue
                q.append((nxt, nchain))
        return found


def rel(p):
    try:
        return os.path.relpath(p, SRC)
    except ValueError:
        return p


def main():
    list_only = "--list" in sys.argv
    why = None
    if "--why" in sys.argv:
        i = sys.argv.index("--why")
        why = sys.argv[i + 1] if i + 1 < len(sys.argv) else None

    incdirs = include_dirs()
    sc = Scanner(incdirs)
    tus = graph_sources()
    tus = [t for t in tus if os.path.isfile(os.path.join(SRC, t))]

    if why:
        target = why.replace("\\", "/")
        path = os.path.join(SRC, target)
        if not os.path.isfile(path):
            path = sc.resolve(target, SRC)
        if not path:
            print("не найден файл: %s" % why)
            return 1
        f = sc.reach(path, set(FORBIDDEN))
        for k in sorted(f):
            print("%s <- %s" % (k, " <- ".join(rel(x) for x in f[k])))
        return 0

    bad = []
    counts = dict((k, 0) for k in FORBIDDEN)
    carriers = {}
    for t in tus:
        full = os.path.join(SRC, t)
        f = sc.reach(full, set(FORBIDDEN))
        for k, chain in f.items():
            counts[k] += 1
            # кто в цепочке из Src/ включает вендорный заголовок
            for a, b in zip(chain, chain[1:]):
                if os.path.basename(b) in FORBIDDEN and rel(a).startswith(("Render", "ShaderCompiler", "Scene", "PF_", "UI", "System", "Core", "Client", "PW_", "Terrain", "Scripts", "Shared", "Sound", "Network", "libdb", "MemoryLib", "NivalInput", "RemoteObjects", "transport", "naio", "RpcBase", "Game")):
                    carriers.setdefault(rel(a), set()).add(os.path.basename(b))
        if f and t not in ALLOW:
            bad.append((t, sorted(f)))

    print("TU графа клиента: %d" % len(tus))
    print("Дотягиваются до вендорного D3D9/DInput/D3DCompiler:")
    for k in FORBIDDEN:
        if counts[k]:
            print("  %-16s %4d TU" % (k, counts[k]))
    print("Разрешено (allowlist): %d TU, нарушают: %d" % (len(ALLOW), len(bad)))
    print("\nЗаголовки Src/, которые НЕСУТ зависимость (число TU, где они в цепочке):")
    hdr_count = {}
    for t in tus:
        full = os.path.join(SRC, t)
        f = sc.reach(full, set(FORBIDDEN))
        for k, chain in f.items():
            for a in chain[1:-1]:
                r = rel(a)
                if r.startswith(".."):
                    continue
                hdr_count[r] = hdr_count.get(r, 0) + 1
    for h, c in sorted(hdr_count.items(), key=lambda x: -x[1])[:40]:
        print("  %-46s %4d  (%s)" % (h, c, ",".join(sorted(carriers.get(h, [])))))

    if list_only or bad:
        print("\nTU вне allowlist'а, которым D3D9 доступен (%d):" % len(bad))
        for t, k in bad:
            print("  %-52s %s" % (t, ",".join(k)))
    return 0 if not bad else 1


if __name__ == "__main__":
    sys.exit(main())
