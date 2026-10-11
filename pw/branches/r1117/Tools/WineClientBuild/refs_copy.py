#!/usr/bin/env python3
"""
refs_copy.py — копия runtime-файлов, перечисленных в .refs, в OutDir.

Это аналог post-build события vcproj:
  "$(ProjectDir)..\\..\\vendor\\BuildUtils\\CopyReference.exe" $(InputPath) $(ConfigurationName) $(TargetDir)

Формат .refs:
  [debug, release, shipping, shippingsingleexe, releasesingleexe]
  ..\\..\\Vendor\\Steam\\...\\steam_api.dll
  ..\\..\\Vendor\\Libc\\Microsoft.VC90.CRT\\*.*=>Microsoft.VC90.CRT
Путь — относительно каталога проекта; '=>subdir' копирует в OutDir\\subdir.

Использование: python3 refs_copy.py <Src> <OutDir> [Конфигурация]
"""
import os, re, sys, glob, shutil, json

CFG = (os.environ.get("PW_CFG", "ShippingSingleExe|Win32")).split("|")[0].lower()

# project name -> каталог проекта (из vcproj)
def project_dirs(src):
    out = {}
    for root, dirs, files in os.walk(src):
        for f in files:
            if f.endswith(".vcproj"):
                try:
                    import xml.etree.ElementTree as ET
                    name = ET.parse(os.path.join(root, f)).getroot().get("Name")
                    if name:
                        out[name] = root
                except Exception:
                    pass
    return out


def case_resolve(base, rel):
    parts = [p for p in re.split(r"[\\/]+", rel.replace("\\", "/")) if p not in ("", ".")]
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


def main():
    src = sys.argv[1]
    outdir = sys.argv[2]
    dirs = project_dirs(src)
    copied = []
    for name, pdir in sorted(dirs.items()):
        refs = os.path.join(pdir, name.lower() + ".refs")
        if not os.path.isfile(refs):
            continue
        section = None
        for raw in open(refs, encoding="utf-8", errors="replace"):
            line = raw.strip()
            if not line:
                continue
            if line.startswith("["):
                section = [c.strip().lower() for c in line.strip("[]").split(",")]
                continue
            if section is None or CFG not in section:
                continue
            sub = ""
            if "=>" in line:
                line, sub = line.split("=>", 1)
                line, sub = line.strip(), sub.strip()
            if "*" in line or "?" in line:
                d = line.replace("\\", "/").rsplit("/", 1)[0] or "."
                base = case_resolve(pdir, d)
                if base is None:
                    print(f"!! {name}: cannot resolve base of {line}")
                    continue
                files = sorted(glob.glob(os.path.join(base, os.path.basename(line.replace("\\", "/")))))
            else:
                src_abs = case_resolve(pdir, line)
                files = [src_abs] if (src_abs and os.path.isfile(src_abs)) else []
            if not files:
                print(f"!! {name}: no files match {line}")
                continue
            for f in sorted(files):
                dst = os.path.join(outdir, sub, os.path.basename(f)) if sub else os.path.join(outdir, os.path.basename(f))
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copy2(f, dst)
                copied.append(os.path.relpath(dst, outdir))
    print(f"copied {len(copied)} files:")
    for c in sorted(copied):
        print("  ", c)


if __name__ == '__main__':
    main()
