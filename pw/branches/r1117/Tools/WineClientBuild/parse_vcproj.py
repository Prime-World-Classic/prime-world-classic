#!/usr/bin/env python3
"""
parse_vcproj.py — разбор .vcproj клиента на модель сборки для Wine.

VS2008 SP1 под Wine запускается, но devenv/MSBuild — нет (в Wine-префиксе нет
.NET Framework), поэтому сборку выполняет build.py: он читает vcproj, восстанавливает
фактические флаги cl/lib/rc/link и вызывает инструменты напрямую.

Модель: {project: {name, dir, type, tools{...}, files[{rel, abs, cfg{...}}]}}

Использование:
  python3 parse_vcproj.py <Src> <model.json> [Конфигурация]

Список проектов берётся из PF.sln (Build.0 для выбранной конфигурации), за вычетом
EXCLUDE — серверных/редакторных проектов, которые в клиентский бинарник не входят.
"""
import xml.etree.ElementTree as ET
import os, sys, re, json

CFG = os.environ.get("PW_CFG", "ShippingSingleExe|Win32")

# Не собираются в клиентский PW_Game.exe:
#   UniServerApp      — сервер, собирается на Linux (build_server.sh)
#   FilePileCompiler  — падает и в оригинальной VS-сборке (D:\Projects)
#   PWClassicSandbox  — падает и в оригинальной VS-сборке
EXCLUDE = {"UniServerApp", "FilePileCompiler", "PWClassicSandbox"}


def projects_from_sln(sln_path, cfg):
    """Имена проектов + их vcproj, которые собираются (Build.0) в конфигурации cfg."""
    text = open(sln_path, encoding="utf-8-sig").read()
    lines = text.split("\n")
    proj_re = re.compile(r'^Project\("\{([^"]+)\}"\) = "([^"]+)", "([^"]+)", "\{([0-9a-fA-F-]+)\}"')
    guid2 = {}
    projs = []
    for l in lines:
        m = proj_re.match(l)
        if m:
            guid2[m.group(4).upper()] = (m.group(2), m.group(3), m.group(1))
    build = set()
    for l in lines:
        if f'.{cfg}.Build.0' in l:
            g = l.strip().split('.')[0].strip('{}').upper()
            build.add(g)
    out = []
    for guid, (name, path, ptype) in guid2.items():
        if guid in build and name not in EXCLUDE and path.endswith('.vcproj'):
            out.append((name, path))
    return sorted(out)


def parse_vcproj(path, src_root):
    tree = ET.parse(path)
    root = tree.getroot()
    proj = {
        'name': root.get('Name'),
        'path': path,
        'dir': os.path.abspath(os.path.dirname(path)),
    }
    cfg = None
    for c in root.iter('Configuration'):
        if c.get('Name') == CFG:
            cfg = c
            break
    if cfg is None:
        return None
    proj['type'] = cfg.get('ConfigurationType')
    proj['outDir'] = cfg.get('OutputDirectory')
    proj['outName'] = cfg.get('OutputName')
    tools = {}
    for t in cfg.iter('Tool'):
        tn = t.get('Name')
        tools[tn] = {k: v for k, v in t.attrib.items() if k != 'Name' and v is not None}
    proj['tools'] = tools

    files = []
    for f in root.iter('File'):
        rel = f.get('RelativePath')
        if not rel:
            continue
        fentry = {'rel': rel, 'abs': os.path.normpath(os.path.join(proj['dir'], rel.replace('\\', '/'))),
                  'cfg': {}}
        for fc in f.findall('FileConfiguration'):
            if fc.get('Name') != CFG:
                continue
            fc_attrs = {k: v for k, v in fc.attrib.items() if k != 'Name' and v is not None}
            cl = fentry['cfg'].setdefault('VCCLCompilerTool', {})
            cl.update(fc_attrs)  # ExcludedFromBuild живёт здесь, а не в Tool
            for t in fc.findall('Tool'):
                tn = t.get('Name')
                attrs = {k: v for k, v in t.attrib.items() if k != 'Name' and v is not None}
                for ch in t:
                    attrs[ch.tag] = (ch.get('Value') or ch.text or '').strip()
                fentry['cfg'].setdefault(tn, {}).update(attrs)
        files.append(fentry)
    proj['files'] = files
    return proj


def main():
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    src, out_path = sys.argv[1], sys.argv[2]
    src = os.path.abspath(src)
    projs = projects_from_sln(os.path.join(src, 'PF.sln'), CFG)
    out = {}
    for name, rel in projs:
        d = parse_vcproj(os.path.normpath(os.path.join(src, rel.replace('\\', '/'))), src)
        if d is None:
            print(f"!! {name}: конфигурация {CFG} не найдена", file=sys.stderr)
            continue
        out[name] = d
    with open(out_path, 'w') as f:
        json.dump(out, f, indent=1, ensure_ascii=False)

    # Сводка по особенностям: PCH, исключённые файлы, custom build, midl, rc
    for name, d in sorted(out.items()):
        pch_create, excluded, custom, midl, rc = [], [], [], [], []
        for fe in d['files']:
            c = fe['cfg']
            cl = c.get('VCCLCompilerTool', {})
            low = fe['rel'].lower()
            if cl.get('UsePrecompiledHeader') == '1':
                pch_create.append(fe['rel'])
            if cl.get('ExcludedFromBuild') == 'true':
                excluded.append(fe['rel'])
            if 'VCCustomBuildTool' in c and c['VCCustomBuildTool'].get('CommandLine'):
                custom.append((fe['rel'], c['VCCustomBuildTool']['CommandLine']))
            if 'VCMIDLTool' in c and c['VCMIDLTool'].get('ExcludedFromBuild') != 'true':
                midl.append(fe['rel'])
            if 'VCResourceCompilerTool' in c and c['VCResourceCompilerTool'].get('ExcludedFromBuild') != 'true':
                rc.append(fe['rel'])
        print(f"== {name}: files={len(d['files'])} type={d['type']} pch_create={pch_create}")
        if excluded: print("   excluded:", excluded)
        if custom: print("   custom:", custom)
        if midl: print("   midl:", midl)
        if rc: print("   rc:", rc)


if __name__ == '__main__':
    main()
