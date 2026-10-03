#!/usr/bin/env python3
"""
build_client.py — сборка клиента PW_Game.exe (VS2008 SP1, x86) под Wine.

devenv.com и MSBuild в Wine-префиксе не работают (VS2008 требует .NET Framework),
поэтому сборка воспроизводится напрямую инструментами VC90: cl / lib / rc / link / mt.
Модель проектов — из parse_vcproj.py (model.json).

Проверено на wine-10 (32-битный префикс + VS2008 SP1 + Windows SDK 6.0A).

Особенности Wine, которые здесь обходятся (проверены эмпирически):
  * инструменты вызываются голыми именами через `wine cmd /c` (прямой запуск
    `wine /path/cl.exe` ломает собственный парсер аргументов cl);
  * значения с двоеточием после флага cl ломается (двоеточие утекает в значение),
    а значения через пробел не потребляются:
      - без /Fo  -> cl запускается с CWD = IntDir, имена .obj = базовые имена источников;
      - без /Fp  -> PCH по умолчанию (vc90.pch) в CWD;
      - без /Fe  -> линковка идёт link.exe /OUT: (у линкера двоеточная форма работает);
      - /Yc и /Yu только голые (through-header по умолчанию stdafx.h); файлы с другим
        through-header компилируются без PCH (семантически то же, медленнее);
      - каталоги include -> переменная окружения INCLUDE (без /I);
      - /DNAME=VAL и /D работают.
  * PDB невозможны (C1902/LNK1101: mspdb80 + wine builtin msvcr90) -> /Zi и /DEBUG
    не передаются; /OPT:REF тоже не передаётся (под wine COMDAT-свёртка давала
    бинарник, падающий при запуске).
  * rc /fo: теряет двоеточие -> выход переименовывается (:out.res -> out.res).
  * длинные списки файлов -> response-файлы (cmd под wine ограничивает строку ~1023
    символами, у rc response-файлов нет).
  * PCH эмулируется wrapper-источником: cl под wine не может /Yu//Yc с through-header,
    но многие TU зависят от контекста PCH (платформенные define, заголовки проекта),
    поэтому для каждого PCH-файла компилируется обёртка, форсирующая through-header:
        #include "<through-header>"
        #line 1 "<original>"
        #include "<original>"
    Обёртка названа как оригинал, поэтому .obj сохраняет имя.

Использование:
  python3 parse_vcproj.py <Src> model.json
  python3 build_client.py [--dryrun] [--only=PROJECT]

Переменные окружения:
  PW_SRC      путь к pw/branches/r1117/Src
  PW_MODEL    model.json
  PW_LOGDIR   куда писать fail_*.log
  PW_CFG      конфигурация (ShippingSingleExe|Win32)
  PW_BUILD_JOBS      параллельность (default 8)
  PW_LINK_LIBS=1     линковать .lib-архивы вместо .obj (см. комментарий у линковки)
  PW_KEEP_GOING=1    не останавливаться на первой ошибке (полная картина ошибок)
"""
import json, os, re, shutil, subprocess, sys, time
from collections import OrderedDict

SRC = os.path.abspath(os.environ.get("PW_SRC", "/home/rekon/PWC/prime-world-classic/pw/branches/r1117/Src"))
VENDOR = os.path.join(os.path.dirname(SRC), "Vendor")
WINEPREFIX = os.environ.get("WINEPREFIX", os.path.expanduser("~/.wine-vs2008"))
VC = r"C:\Program Files (x86)\Microsoft Visual Studio 9.0"
SDK = r"C:\Program Files\Microsoft SDKs\Windows\v6.0A"
CFG = os.environ.get("PW_CFG", "ShippingSingleExe|Win32")
OUTDIR = os.path.join(SRC, "_" + CFG.split("|")[0])
WORKERS = int(os.environ.get("PW_BUILD_JOBS", "8"))
LOGDIR = os.environ.get("PW_LOGDIR", os.path.dirname(os.path.abspath(__file__)))
DRY = "--dryrun" in sys.argv
ONLY = None
for a in sys.argv:
    if a.startswith("--only="):
        ONLY = a.split("=", 1)[1]


def winpath(p):
    """linux path -> wine Z: path"""
    p = os.path.normpath(p)
    return "Z:\\" + p.lstrip("/").replace("/", "\\")


def case_resolve(base, rel):
    """Resolve rel (may use Windows-style separators and wrong case) against the
    real filesystem case under base."""
    parts = re.split(r"[\\/]+", rel.replace("\\", "/"))
    parts = [p for p in parts if p not in ("", ".")]
    cur = base
    for part in parts:
        if part == "..":
            cur = os.path.dirname(cur)
            continue
        try:
            entries = os.listdir(cur)
        except OSError:
            return None
        if part in entries:
            cur = os.path.join(cur, part)
            continue
        low = part.lower()
        match = [e for e in entries if e.lower() == low]
        if len(match) == 1:
            cur = os.path.join(cur, match[0])
        else:
            return None
    return cur


def to_dflags(defineds):
    out = []
    for d in (defineds or "").split(";"):
        d = d.strip()
        if d:
            out.append("/D" + d)
    return out


PASSTHROUGH = {"/Od", "/O1", "/O2", "/Os", "/OX", "/Ob1", "/Ob2", "/Oy-",
               "/MD", "/MDd", "/MT", "/ML", "/EHsc", "/EHa", "/GS-", "/GF",
               "/GL", "/Gy", "/Gm", "/W0", "/W1", "/W2", "/W3", "/W4", "/WX",
               "/RTC1", "/RTCs", "/TP", "/Tc", "/nologo", "/Z7", "/J", "/Gd"}


def parse_extra_options(opts):
    """Split AdditionalOptions into flags we pass through (minus /MP)."""
    flags = []
    for tok in (opts or "").split():
        if tok == "/MP":
            continue
        if tok.startswith(("/D", "/F")):
            flags.append(tok)
        elif tok in PASSTHROUGH:
            flags.append(tok)
        else:
            print(f"  !! unhandled AdditionalOption: {tok}")
    return flags


class Project:
    def __init__(self, name, model):
        self.name = name
        self.m = model
        self.dir = model["dir"]
        self.is_exe = model["type"] == "1"
        self.intdir = os.path.join(OUTDIR, name)
        os.makedirs(self.intdir, exist_ok=True)
        self.outdir = OUTDIR

        cl = model["tools"].get("VCCLCompilerTool", {})
        self.flags = []
        opt = cl.get("Optimization", "0")
        self.flags.append({0: "/Od", 1: "/O1", 2: "/O2", 3: "/Os", 4: "/OX"}.get(opt, "/O2"))
        if cl.get("InlineFunctionExpansion") in ("1", "2"):
            self.flags.append("/Ob" + cl["InlineFunctionExpansion"])
        rl = cl.get("RuntimeLibrary", "2")
        self.flags.append({0: "/ML", 1: "/MT", 2: "/MD", 3: "/MDd"}.get(rl, "/MD"))
        eh = cl.get("ExceptionHandling", "0")
        if eh == "1":
            self.flags.append("/EHsc")
        elif eh == "2":
            self.flags.append("/EHa")
        if cl.get("BufferSecurityCheck") == "false":
            self.flags.append("/GS-")
        if cl.get("EnableFunctionLevelLinking") == "true":
            self.flags.append("/GF")
        wl = cl.get("WarningLevel", "3")
        self.flags.append("/W" + wl)
        if cl.get("WarnAsError") == "true":
            self.flags.append("/WX")
        # /Zi намеренно опущен: PDB под wine не работают
        self.flags += parse_extra_options(cl.get("AdditionalOptions", ""))
        # C4519 ("default template arguments are only allowed on a class template") —
        # консервативное предупреждение MSVC 9: легальный C++, но при /WX становится
        # ошибкой (RpcArgs.h: функция-шаблон с default TChar, добавленная Linux-портом).
        self.flags.append("/wd4519")
        self.base_warning_level = wl
        self.base_warnaserror = cl.get("WarnAsError") == "true"
        self.defines = to_dflags(cl.get("PreprocessorDefinitions", ""))
        self.pch_through = cl.get("PrecompiledHeaderThrough", "stdafx.h")
        self.pch_on = cl.get("UsePrecompiledHeader", "0")
        self.includes = []
        for inc in (cl.get("AdditionalIncludeDirectories", "") or "").split(";"):
            inc = inc.strip()
            if not inc:
                continue
            real = case_resolve(self.dir, inc)
            if real is None:
                real = case_resolve(SRC, inc)
            if real is None:
                print(f"  !! {name}: unresolved include dir {inc!r}")
                continue
            self.includes.append(real)
        rc = model["tools"].get("VCResourceCompilerTool", {})
        self.rc_defines = to_dflags(rc.get("PreprocessorDefinitions", ""))
        self.rc_includes = []
        for inc in (rc.get("AdditionalIncludeDirectories", "") or "").split(";"):
            inc = inc.strip()
            if not inc:
                continue
            real = case_resolve(self.dir, inc)
            if real:
                self.rc_includes.append(real)

    def _resolve_through(self, src_abs, th):
        """PCH строится из through-header проекта (PCH-файл лежит в каталоге
        проекта), поэтому каталог проекта важнее заголовка с тем же именем в
        каталоге файла (напр. Game/PF/Server/Statistic/stdafx.h — заглушка)."""
        cands = [self.dir, os.path.dirname(src_abs)] + self.includes
        t = th.replace("\\", "/")
        for base in cands:
            p = os.path.normpath(os.path.join(base, t))
            if os.path.isfile(p):
                return p
            d, low_name = os.path.split(p)[0], os.path.basename(t).lower()
            try:
                entries = os.listdir(d)
            except OSError:
                continue
            match = [e for e in entries if e.lower() == low_name]
            if len(match) == 1:
                return os.path.join(d, match[0])
        return None

    def file_jobs(self):
        jobs = []
        pch_create, use_pch, no_pch, rcs = [], [], [], []
        for f in self.m["files"]:
            rel = f["rel"]
            low = rel.lower()
            absf = f["abs"]
            if not os.path.isfile(absf):
                if not low.endswith((".h", ".hpp", ".rc2", ".component", ".refs", ".manifest",
                                     ".idl", ".txt", ".cd", ".resx", ".ico")):
                    print(f"  !! {self.name}: missing file {rel}")
                continue
            per = f["cfg"].get("VCCLCompilerTool", {})
            if per.get("ExcludedFromBuild") == "true":
                continue
            if low.endswith(".rc"):
                rcs.append((f, per))
                continue
            if not low.endswith((".cpp", ".c")):
                continue
            info = dict(file=f, per=per)
            up = per.get("UsePrecompiledHeader", "")
            if low.endswith(".c") or up == "0" or up == "1" or self.pch_on != "2":
                no_pch.append(info)
                continue
            th = (per.get("PrecompiledHeaderThrough") or self.pch_through or "stdafx.h").strip()
            th_abs = self._resolve_through(f["abs"], th)
            if th_abs is None:
                print(f"  !! {self.name}: cannot resolve through header {th!r} for {rel}")
                no_pch.append(info)
                continue
            base = os.path.splitext(os.path.basename(f["abs"]))[0]
            wrapper = os.path.join(self.intdir, base + ".cpp")
            # #line принимает строковый литерал — обратные слебы становятся
            # escape-последовательностями (C4129), поэтому прямые слебы
            line_path = winpath(f["abs"]).replace("\\", "/")
            content = '#include "%s"\n#line 1 "%s"\n#include "%s"\n' % (winpath(th_abs), line_path, winpath(f["abs"]))
            if not os.path.isfile(wrapper) or open(wrapper).read() != content:
                with open(wrapper, "w") as fh:
                    fh.write(content)
            info2 = dict(info, file=dict(f, abs=wrapper), orig=f["abs"])
            use_pch.append(info2)

        self.batch(jobs, use_pch, pch_flag=None, deps=[])
        self.batch(jobs, no_pch, pch_flag=None, deps=[])

        # rc: response-файлов нет, строка короткая; CWD = каталог проекта
        for f, per in rcs:
            base = os.path.splitext(os.path.basename(f["abs"]))[0] + "_" + re.sub(r"\W", "_", self.name)
            rcdir = self.dir
            parts = ["rc", f"/fo:{base}.res"]
            parts += self.rc_defines
            for inc in self.rc_includes:
                parts += ["/i" + winpath(inc)]
            parts.append(winpath(f["abs"]))
            if len(" ".join(parts)) > 900:
                raise RuntimeError(f"rc command too long for wine cmd: {' '.join(parts)}")
            jobs.append(dict(cwd=rcdir, parts=parts, out=os.path.join(self.intdir, base + ".res"),
                             kind="rc", deps=[], rc_rename=(base + ".res",)))
        return jobs

    def batch(self, jobs, infos, pch_flag, deps):
        groups = OrderedDict()
        for info in infos:
            key = tuple(self.cl_parts(info, pch=False))
            groups.setdefault(key, []).append(info)
        for key, items in groups.items():
            for i in range(0, len(items), 12):
                chunk = items[i:i + 12]
                cwd = self.intdir
                parts = ["cl"] + list(key)
                if pch_flag:
                    parts.append(pch_flag)
                for info in chunk:
                    parts.append(winpath(info["file"]["abs"]))
                resp = write_resp(os.path.join(cwd, f"cl_{os.getpid()}_{len(jobs)}.rsp"), parts[1:])
                origs = [x.get("orig") for x in chunk if x.get("orig")]
                jobs.append(dict(cwd=cwd, parts=["cl", "@" + winpath(resp)], respfile=resp,
                                 out=os.path.join(cwd, os.path.basename(chunk[0]["file"]["abs"])),
                                 kind="cl", deps=list(deps), srcs_orig=origs,
                                 expect=[os.path.join(cwd, os.path.splitext(os.path.basename(x["file"]["abs"]))[0] + ".obj")
                                         for x in chunk]))

    def cl_parts(self, info, pch=False):
        per = info["per"]
        base = [fl for fl in self.flags if not fl.startswith("/W")]
        parts = ["/nologo", "/c"] + base
        wl = per.get("WarningLevel") or self.base_warning_level
        parts.append("/W" + wl)
        if self.base_warnaserror and per.get("WarnAsError") != "false":
            parts.append("/WX")
        if pch:
            parts.append("/Yc")
        eh = per.get("ExceptionHandling")
        if eh == "1":
            if "/EHa" in base:
                parts.remove("/EHa")
            parts.append("/EHsc")
        elif eh == "2":
            if "/EHsc" in base:
                parts.remove("/EHsc")
            parts.append("/EHa")
        parts += self.defines
        parts += to_dflags(per.get("PreprocessorDefinitions", ""))
        parts += parse_extra_options(per.get("AdditionalOptions", ""))
        return parts

    def include_env(self):
        # $(ProjectDir) в env-include НЕ добавляется: VS добавляет её только в
        # QUOTE-поиск, а в angle-поиск — нет, и это ломает, например, <Rpc.h>
        # (wine выбрал бы проектную Rpc.h вместо SDK-шной).
        incs = self.includes + [VC + "\\VC\\include", VC + "\\VC\\atlmfc\\include", SDK + "\\Include"]
        return ";".join(winpath(i) if os.path.sep in i else i for i in incs)


def write_resp(path, parts):
    with open(path, "w", newline="") as fh:
        for p in parts:
            fh.write(('"' + p + '"') if (" " in p) else p + "\n")
    return path


def sln_deps(sln_path):
    """graph ProjectDependencies из PF.sln: name -> set(names)"""
    lines = open(sln_path, encoding="utf-8-sig").read().split("\n")
    proj_re = re.compile(r'^Project\("\{([^"]+)\}"\) = "([^"]+)", "([^"]+)", "\{([0-9a-fA-F-]+)\}"')
    info = {}
    for i in range(len(lines)):
        m = proj_re.match(lines[i])
        if not m:
            continue
        name, guid = m.group(2), m.group(4).upper()
        info[guid] = name
        i += 1
        d = set()
        while i < len(lines):
            l = lines[i].strip()
            if l.startswith("ProjectSection(ProjectDependencies)"):
                i += 1
                while i < len(lines) and lines[i].strip() != "EndProjectSection":
                    mm = re.match(r"\{([0-9a-fA-F-]+)\}", lines[i].strip())
                    if mm:
                        d.add(info.get(mm.group(1).upper(), mm.group(1).upper()))
                    i += 1
            elif l.startswith("EndProjectSection"):
                i += 1
            else:
                break
        info[name] = d
    return {k: v for k, v in info.items() if isinstance(v, set)}


def _win_to_unix(p):
    p = p.replace("\\", "/")
    return p[2:] if p.startswith("Z:") else p


def _job_up_to_date(j):
    outs = j.get("expect") or ([j["out"]] if j["out"] else [])
    if not outs or not all(os.path.exists(o) for o in outs):
        return False
    oldest_out = min(os.path.getmtime(o) for o in outs)
    # заголовки: PCH эмулируется wrapper-источником, поэтому изменение заголовка
    # (в каталоге проекта или в include-путях) должно приводить к пересборке
    hdr = j.get("hdr_mtime")
    if hdr and hdr > oldest_out:
        return False
    srcs = [_win_to_unix(p) for p in j["parts"]
            if p.startswith("Z:") and p.lower().endswith((".cpp", ".c", ".rc", ".res", ".obj", ".lib"))]
    for resp in (j.get("resp"), j.get("respfile")):
        if resp and os.path.isfile(resp):
            for line in open(resp):
                t = line.strip().strip('"')
                if t.startswith("Z:") and t.lower().endswith((".cpp", ".c", ".obj", ".res", ".lib")):
                    srcs.append(_win_to_unix(t))
    # выходы dep-job'ов (lib/obj) — линковка устаревает, когда пересобран любой .lib
    for d in j.get("deps", []):
        o = d.get("out")
        if o:
            srcs.append(o)
    for s in j.get("srcs_orig", []):
        srcs.append(s)
    for s in srcs:
        if os.path.exists(s) and os.path.getmtime(s) > oldest_out:
            return False
    return True


def run_jobs(all_jobs):
    import threading
    state = {id(j): j for j in all_jobs}
    remaining = {id(j): {id(d) for d in j["deps"] if id(d) in state} for j in all_jobs}
    dependents = {id(j): [] for j in all_jobs}
    for j in all_jobs:
        for d in j["deps"]:
            if id(d) in dependents:
                dependents[id(d)].append(id(j))
    done, failed, scheduled = set(), set(), set()
    KEEP = os.environ.get("PW_KEEP_GOING") == "1"
    lock = threading.Lock()
    running = 0

    def run_job(j):
        pre = j.get("pre")
        if pre:
            pre()
        cmd = " ".join(j["parts"])
        full = ["wine", "cmd", "/c", cmd]
        t0 = time.time()
        r = subprocess.run(full, cwd=j["cwd"], env=j["env"], capture_output=True,
                           text=True, errors="replace", timeout=1800)
        dt = time.time() - t0
        out = (r.stdout or "") + (r.stderr or "")
        ok = r.returncode == 0
        if j["kind"] == "rc" and ok:
            bad = os.path.join(j["cwd"], ":" + j["rc_rename"][0])
            if os.path.exists(bad):
                os.rename(bad, j["out"])
        if j["kind"] == "cl" and ok:
            ok = all(os.path.exists(e) for e in j.get("expect", []))
        return j, ok, out, dt

    def worker():
        nonlocal running
        while True:
            with lock:
                if failed and not KEEP:
                    return
                pick = None
                for j in all_jobs:
                    if id(j) in scheduled or id(j) in done or id(j) in failed:
                        continue
                    if remaining[id(j)] <= done:
                        pick = j
                        break
                if pick is None:
                    return
                scheduled.add(id(pick))
                running += 1
            j, ok, out, dt = run_job(pick)
            with lock:
                running -= 1
                done.add(id(j))
                if not ok:
                    failed.add(id(j))
                    failpath = os.path.join(LOGDIR, f"fail_{j['kind']}_{j['project']}_{id(j) % 100000}.log")
                    with open(failpath, "w") as fh:
                        fh.write("CMD: " + " ".join(j["parts"]) + "\n\n" + out)
                    print(f"\n*** FAILED [{j['kind']}] {j['project']} (log: {failpath}): {out[-1200:]}", flush=True)
                    if KEEP:
                        for dep in dependents[id(j)]:
                            remaining[dep].discard(id(j))
                        continue
                    return
                if j["kind"] in ("pch", "lib", "link", "manifest"):
                    print(f"== {j['kind']} {j['project']}: {os.path.basename(j['out'])} ({dt:.1f}s)", flush=True)
                if j["kind"] == "cl":
                    print(f"   cl {j['project']}: {len(j.get('expect', []))} objs ({dt:.1f}s)", flush=True)
                for dep in dependents[id(j)]:
                    remaining[dep].discard(id(j))

    # up-to-date считается не только по mtime собственных выходов: job становится
    # устаревшим, если хоть один его dep-job будет пересобран (например линковка
    # должна идти заново, когда пересобирается .lib из-за изменённого .cpp).
    uptodate = {id(j): _job_up_to_date(j) for j in all_jobs}
    for _ in range(len(all_jobs)):
        changed2 = False
        for j in all_jobs:
            if uptodate[id(j)] and any(not uptodate[id(d)] for d in j["deps"] if id(d) in uptodate):
                uptodate[id(j)] = False
                changed2 = True
        if not changed2:
            break
    for j in all_jobs:
        if j["kind"] in ("cl", "rc", "lib", "link", "manifest") and uptodate[id(j)]:
            done.add(id(j))
            print(f"-- skip (up to date) [{j['kind']}] {j['project']}", flush=True)
    for j in all_jobs:
        if id(j) in done:
            for dep in dependents[id(j)]:
                remaining[dep].discard(id(j))

    threads = [threading.Thread(target=worker) for _ in range(WORKERS)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    if failed:
        sys.exit(1)


def main():
    model = json.load(open(os.environ.get("PW_MODEL", os.path.join(LOGDIR, "model.json"))))
    if ONLY:
        model = OrderedDict([(ONLY, model[ONLY])])

    names = list(model.keys())
    deps_graph = sln_deps(os.path.join(SRC, "PF.sln"))
    deps = {n: (deps_graph.get(n, set()) & set(names) - {n}) for n in names}
    order, seen = [], set()
    changed = True
    while changed:
        changed = False
        for n in names:
            if n in seen:
                continue
            if deps[n] <= seen:
                order.append(n)
                seen.add(n)
                changed = True
    if len(order) != len(names):
        sys.exit("cycle in project deps")

    env = dict(os.environ, WINEPREFIX=WINEPREFIX, WINEDEBUG="-all",
               LIB=VC + "\\VC\\lib;" + VC + "\\VC\\atlmfc\\lib;" + SDK + "\\Lib",
               INCLUDE=SDK + "\\Include;" + VC + "\\VC\\include;" + VC + "\\VC\\atlmfc\\include")

    all_jobs = []
    for n in order:
        p = Project(n, model[n])
        jobs = p.file_jobs()
        hdr = 0.0
        for d in [p.dir] + p.includes:
            try:
                for e in os.listdir(d):
                    if e.lower().endswith((".h", ".hpp", ".inl")):
                        t = os.path.getmtime(os.path.join(d, e))
                        if t > hdr:
                            hdr = t
            except OSError:
                pass
        for j in jobs:
            j["project"] = n
            j["env"] = dict(env, INCLUDE=p.include_env())
            j["hdr_mtime"] = hdr
        all_jobs.extend(jobs)
        if not p.is_exe:
            objs = sorted(f["abs"] for f in model[n]["files"]
                          if f["abs"].lower().endswith((".cpp", ".c")) and os.path.isfile(f["abs"])
                          and f["cfg"].get("VCCLCompilerTool", {}).get("ExcludedFromBuild") != "true")
            rcobjs = [j for j in jobs if j["kind"] == "rc"]
            resp = os.path.join(p.intdir, "libresp.txt")
            libout = os.path.join(p.outdir, n + ".lib")
            lines = ["/nologo", "/OUT:" + winpath(libout)]
            for o in objs:
                lines.append(winpath(os.path.join(p.intdir, os.path.splitext(os.path.basename(o))[0] + ".obj")))
            for r in rcobjs:
                lines.append(winpath(r["out"]))
            with open(resp, "w") as fh:
                fh.write("\n".join(lines) + "\n")
            all_jobs.append(dict(cwd=p.intdir, parts=["lib", "@" + winpath(resp)], out=libout,
                                 kind="lib", deps=jobs, project=n, env=env, resp=resp,
                                 hdr_mtime=hdr))

    if ONLY:
        if DRY:
            for j in all_jobs:
                print(f"[{j['kind']:4s}] {j['project']:15s} " + " ".join(j["parts"][:8]))
            return
        _run = run_jobs(all_jobs)
        return

    pwg = Project("PW_Game", model["PW_Game"])
    link = model["PW_Game"]["tools"]["VCLinkerTool"]

    # ---------------------------------------------------------------- RT_MANIFEST
    # Application.rc пишет `CREATEPROCESS_MANIFEST_RESOURCE_ID RT_MANIFEST
    # "Application.manifest"`, но rc не разворачивает эти константы в числа — в
    # файл попадает ресурс с ИМЕНЕМ "CREATEPROCESS_MANIFEST_RESOURCE_ID", который
    # Fusion не видит (в релизном exe он тоже есть, но рядом лежит настоящий
    # 24/1, который VS встраивает своим mt). link 9.0 манифест не встраивает, а
    # mt.exe под wine падает на -outputresource:. Поэтому драйвер сам компилирует
    # ресурс 1/24: Application.manifest + зависимость VC90 CRT + trustInfo.
    # Без 24/1 CRT не активируется и Windows отказывает в запуске с
    # 0xC0000135 (STATUS_DLL_NOT_FOUND) — MSVCR90/MSVCP90 не находятся.
    app_manifest_src = os.path.join(SRC, "Application.manifest")
    if os.path.isfile(app_manifest_src):
        crt_and_priv = (
            '<dependency><dependentAssembly><assemblyIdentity type="win32" '
            'name="Microsoft.VC90.CRT" version="9.0.21022.8" '
            'processorArchitecture="x86" publicKeyToken="1fc8b3b9a1e18e3b"/>'
            '</dependentAssembly></dependency>'
            '<trustInfo xmlns="urn:schemas-microsoft-com:asm.v3"><security>'
            '<requestedPrivileges><requestedExecutionLevel level="asInvoker" '
            'uiAccess="false"/></requestedPrivileges></security></trustInfo>\n')
        text = open(app_manifest_src, encoding="utf-8-sig").read()
        merged = text.replace("</assembly>", crt_and_priv + "</assembly>")
        if "Microsoft.VC90.CRT" not in merged:
            merged = text
        mpath = os.path.join(pwg.intdir, "wine_app.manifest")
        if not os.path.isfile(mpath) or open(mpath, encoding="utf-8").read() != merged:
            with open(mpath, "w", encoding="utf-8") as fh:
                fh.write(merged)
        rpath = os.path.join(pwg.intdir, "wine_manifest.rc")
        rctxt = '1 24 "wine_app.manifest"\n'
        if not os.path.isfile(rpath) or open(rpath).read() != rctxt:
            with open(rpath, "w") as fh:
                fh.write(rctxt)
        all_jobs.append(dict(cwd=pwg.intdir,
                             parts=["rc", "/fo:wine_manifest.res", winpath(rpath)],
                             out=os.path.join(pwg.intdir, "wine_manifest.res"),
                             kind="rc", deps=[], rc_rename=("wine_manifest.res",),
                             project="PW_Game", env=env))

    pwg_rc_jobs = [j for j in all_jobs if j["kind"] == "rc" and j["project"] == "PW_Game"]
    libs = [os.path.join(pwg.outdir, n + ".lib") for n in reversed(order) if n != "PW_Game"]
    # PW_LINK_LIBS=1: link the per-project .lib archives instead of the .obj files.
    # По умолчанию линкуются .obj напрямую. MSVC достаёт из архива только те члены,
    # которые закрывают незакрытый символ, поэтому TU, существующие ради статических
    # регистраций (REGISTER_DBRESOURCE / DEFINE_RE_FACTORY / REGISTER_VAR, консольные
    # команды и cvars), из архива не попадают. Проверено на tiny10 (2026-10-03):
    # сборка на .lib останавливается после login reply (в логе "Unknown command or
    # variable \"bind\"/\"login\"/\"lobby\"", в лобби не заходит), сборка на .obj
    # проходит полный E2E (login -> lobby -> gamesvc -> finish в бэкенд).
    if not os.environ.get("PW_LINK_LIBS"):
        # Список obj берётся из модели проектов, а не из каталога IntDir: при чистой
        # сборке каталогов ещё нет, и перечисление по ФС дало бы пустой список
        # (LNK2019 на StartPWApplication). Имена obj = базовые имена источников —
        # так их кладёт cl (CWD = IntDir, PCH-обёртки сохраняют имя оригинала).
        objs = []
        for n in reversed(order):
            if n == "PW_Game" or n not in model:
                continue
            p = Project(n, model[n])
            for f in model[n]["files"]:
                abs_ = f["abs"]
                if not abs_.lower().endswith((".cpp", ".c")) or not os.path.isfile(abs_):
                    continue
                if f["cfg"].get("VCCLCompilerTool", {}).get("ExcludedFromBuild") == "true":
                    continue
                objs.append(os.path.join(p.intdir,
                                         os.path.splitext(os.path.basename(abs_))[0] + ".obj"))
            # .res проекта кладёт в .lib архиватор; при линковке obj их нужно
            # передавать линкеру явно
            objs += [j["out"] for j in all_jobs if j["kind"] == "rc" and j["project"] == n]
        libs = sorted(set(objs))
    dep_libs = [d.strip() for d in link["AdditionalDependencies"].split() if d.strip()]
    r1117 = os.path.dirname(SRC)
    libdirs_win = []
    for d in (link.get("AdditionalLibraryDirectories", "") or "").split(";"):
        d = d.strip().strip('"').strip()
        if not d:
            continue
        if d == "$(OutDir)":
            libdirs_win.append(winpath(pwg.outdir))
            continue
        real = case_resolve(pwg.dir, d) or case_resolve(r1117, d)
        if real is None:
            print(f"!! unresolved libdir {d!r}")
            continue
        libdirs_win.append(winpath(real))
    nodflt = [x for x in (link.get("IgnoreDefaultLibraryNames", "") or "").split(";") if x]
    subsys = {1: "CONSOLE", 2: "WINDOWS"}.get(int(link.get("SubSystem", "2")), "WINDOWS")
    # LargeAddressAware из vcproj (VS отдаёт /LARGEADDRESSAWARE). Без него у
    # 32-битного процесса потолок адресного пространства 2 ГБ: в бою клиент
    # упирается в него, nedmalloc возвращает NULL и срабатывает new-handler
    # ("The program ran out of memory" / RaiseException 0xC000008C), а под
    # WARP ещё и CreateTexture отдаёт E_OUTOFMEMORY. Проверено 2026-10-03:
    # без бита пик commit 1748 mb + E_OUTOFMEMORY, с битом 2178 mb и чисто.
    # PW_NO_LAA=1 — собрать без флага (для A/B-замеров).
    laa = str(link.get("LargeAddressAware", "")).strip()

    parts = ["link", "/nologo", "/OUT:" + winpath(os.path.join(pwg.outdir, "PW_Game.exe")),
             winpath(os.path.join(pwg.intdir, "PW_Game.obj"))]
    parts += [winpath(r["out"]) for r in pwg_rc_jobs]
    parts += [winpath(l) for l in libs]
    parts += dep_libs
    for ld in libdirs_win:
        parts.append("/LIBPATH:" + ld)
    parts.append("/SUBSYSTEM:" + subsys)
    parts.append("/MACHINE:X86")
    # /MAP — карта символов для разбора крашей Wine-билда (в cdb кадры идут как
    # PW_Game+0xNNNN, PDB без /DEBUG не делается, а /DEBUG меняет сам exe).
    # На образ exe не влияет — проверено: sha256 совпадает с билдом без карты.
    # Отключается PW_NO_MAP=1.
    if not os.environ.get("PW_NO_MAP"):
        parts.append("/MAP:" + winpath(os.path.join(pwg.intdir, "PW_Game.map")))
    if laa == "2" and not os.environ.get("PW_NO_LAA"):
        parts.append("/LARGEADDRESSAWARE")
    elif laa == "1":
        parts.append("/LARGEADDRESSAWARE:NO")
    for nd in nodflt:
        parts.append("/NODEFAULTLIB:" + nd)
    # /MANIFESTINPUT в link 9.0 нет (это опция VS2010+): манифест собирается mt.exe
    parts.append("/MANIFEST")
    linkresp = write_resp(os.path.join(pwg.intdir, "link.rsp"), parts[1:])
    link_job = dict(cwd=pwg.intdir, parts=["link", "@" + winpath(linkresp)],
                    out=os.path.join(pwg.outdir, "PW_Game.exe"),
                    kind="link", deps=[j for j in all_jobs if j["kind"] in ("lib", "cl", "rc")],
                    project="PW_Game", env=env, resp=linkresp)
    all_jobs.append(link_job)

    manifest = os.path.join(SRC, "Application.manifest")
    if os.path.isfile(manifest):
        # mt.exe под wine падает на -outputresource:, поэтому манифест нельзя встроить
        # в exe: линкер сам встраивает свой default-манифест (зависимость VC90 CRT),
        # а mt merge'ит его с Application.manifest (Common-Controls v6) во внешний файл.
        # mt не может читать и писать один и тот же файл -> манифест линкера копируется
        # в IntDir и merge'ится оттуда.
        link_manifest = os.path.join(pwg.outdir, "PW_Game.exe.manifest")
        copy_manifest = os.path.join(pwg.intdir, "linker.manifest")
        mout = os.path.join(pwg.outdir, "PW_Game.exe.manifest")
        mjob = dict(cwd=pwg.intdir,
                    parts=["mt", "-nologo", "-manifest", winpath(copy_manifest),
                           "-manifest", winpath(manifest), "-out:" + winpath(mout)],
                    out=mout, kind="manifest", deps=[link_job], project="PW_Game", env=env,
                    pre=lambda: (os.path.isfile(link_manifest) and shutil.copyfile(link_manifest, copy_manifest)))
        all_jobs.append(mjob)

    if DRY:
        for j in all_jobs:
            print(f"[{j['kind']:4s}] {j['project']:15s} " + " ".join(j["parts"][:8]) + (" ..." if len(j["parts"]) > 8 else ""))
        print(f"total jobs: {len(all_jobs)}")
        return

    run_jobs(all_jobs)
    exe = os.path.join(pwg.outdir, "PW_Game.exe")
    print("\nALL DONE")
    print("PW_Game.exe:", exe, os.path.getsize(exe) if os.path.exists(exe) else "MISSING")


if __name__ == '__main__':
    main()
