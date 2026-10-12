#!/usr/bin/env python3
"""
check_exe.py — сверка артефакта PW_Game.exe (общая для Wine- и native-сборки).

Использование:
  python3 check_exe.py <PW_Game.exe> [<Version.h>]     сверка артефакта
  python3 check_exe.py <build-dir> --count-obj         число .obj (--keep-going)

Проверяется то, что было гейтами этапа 3 (скил pw-client, PLAN_client_modern.md):
machine 0x8664 (x64), PE32+, subsystem 2 (Windows), наличие ресурса манифеста
RT_MANIFEST, и версия: FileVersion/ProdVersion из ресурса VS_VERSIONINFO против
Src/Version.h. Последнее — защита от бага протухания .res (PLAN_client_build_
speedup.md §3): add_custom_command для .rc до починки DEPENDS не зависел от
Version.h, и экзешник с Version.h 2.16.1 показывал 2.16.0.0.
"""
import os
import re
import struct
import sys

RT_VERSION = 16
RT_MANIFEST = 24


def _u16(d, o):
    return struct.unpack_from("<H", d, o)[0]


def _u32(d, o):
    return struct.unpack_from("<I", d, o)[0]


def pe_info(path):
    d = open(path, "rb").read()
    pe = _u32(d, 0x3C)
    mach, nsec = struct.unpack_from("<HH", d, pe + 4)
    chars = _u16(d, pe + 22)
    magic = _u16(d, pe + 24)
    subsys = _u16(d, pe + 92)
    dd = pe + 24 + (112 if magic == 0x20B else 96)
    dirs = [struct.unpack_from("<II", d, dd + 8 * i) for i in range(16)]
    sh = pe + 24 + _u16(d, pe + 20)
    sections = []
    for i in range(nsec):
        o = sh + 40 * i
        # IMAGE_SECTION_HEADER: Name[8], VirtualSize, VirtualAddress,
        # SizeOfRawData, PointerToRawData — поля начинаются с o+8 (не o+12)
        vsz, va, praw, prawptr = struct.unpack_from("<IIII", d, o + 8)
        sections.append((va, max(vsz, praw), praw, prawptr))

    def rva_to_off(rva):
        # секции в таблице идут по возрастанию VA; берём ближайшую предшествующую,
        # иначе широкая VirtualSize предыдущей секции перехватывает чужой RVA
        for va, span, praw, prawptr in sorted(sections, reverse=True):
            if va <= rva:
                if rva >= va + span:
                    return None
                off = prawptr + (rva - va)
                return off if 0 <= off < len(d) else None
        return None

    def res_tree(o, depth=0):
        """Дерево ресурсов: [(id|имя, дети)] до уровня данных; на уровне данных
        возвращаем (id|имя, (rva, size)). Границы файла проверяем: у корня
        ресурсной директории есть хвост за последней секцией."""
        out = []
        if o is None or o + 16 > len(d):
            return out
        n_named, n_id = struct.unpack_from("<HH", d, o + 12)
        e = o + 16
        for _ in range(n_named + n_id):
            if e + 8 > len(d):
                break
            name, child = _u32(d, e), _u32(d, e + 4)
            ent = None if name & 0x80000000 else name
            if name & 0x80000000:
                noff = rva_to_off(res_base + (name & 0x7FFFFFFF))
                if noff is not None and noff + 12 <= len(d):
                    ln = _u16(d, noff)
                    ent = d[noff + 12:noff + 12 + 2 * ln].decode("utf-16-le", "replace")
            # у child бит 31 = «это поддиректория»; снимается перед переводом
            co = rva_to_off(res_base + (child & 0x7FFFFFFF))
            if depth == 2:
                if co is not None and co + 12 <= len(d):
                    out.append((ent, (_u32(d, co), _u32(d, co + 4))))
            elif co is not None:
                out.append((ent, res_tree(co, depth + 1)))
            e += 8
        return out

    manifest, version = [], {}
    res_rva, _size = dirs[2]                  # IMAGE_DIRECTORY_ENTRY_RESOURCE
    # смещения в IMAGE_RESOURCE_DIRECTORY_ENTRY — ОТНОСИТЕЛЬНО начала .rsrc
    # (т.е. относительно RVA самой ресурсной директории), а не абсолютные RVA
    res_base = res_rva
    for typ, kids in res_tree(rva_to_off(res_rva) if res_rva else None):
        if typ == RT_MANIFEST:
            manifest += [str(k[0]) for k in kids if k[0] is not None]
        elif typ == RT_VERSION and kids:
            # уровень имён -> уровень языков -> IMAGE_RESOURCE_DATA_ENTRY
            langs = kids[0][1] if kids and isinstance(kids[0][1], list) else []
            data = langs[0][1] if langs else None
            if isinstance(data, tuple):
                off = rva_to_off(data[0])
                if off is not None:
                    version = vs_versioninfo(d, off, data[1])
    return dict(mach=mach, chars=chars, magic=magic, subsys=subsys,
                manifest=sorted(set(manifest)), version=version)


def vs_versioninfo(d, off, size):
    """Разбор VS_VERSIONINFO: строки StringFileInfo + VS_FIXEDFILEINFO.
    wValueLength для vtype=1 (текст) — в символах UTF-16, для vtype=0 (данные) —
    в байтах; узлы и дети выровнены по 4 байта."""
    out = {}

    def node(o, depth=0):
        if depth > 4 or o + 6 > len(d) or o >= off + size:
            return
        length, vlen, vtype = struct.unpack_from("<HHH", d, o)
        if length < 6 or o + length > off + size:
            return
        end = o + length
        k = e = o + 6
        while e + 1 < len(d) and e < end and d[e:e + 2] != b"\0\0":
            e += 2
        key = d[k:e].decode("utf-16-le", "replace")
        p = (e + 2 + 3) & ~3
        nbytes = vlen if vtype == 0 else 2 * vlen
        if vtype == 1 and nbytes:
            out[key] = d[p:p + nbytes].decode("utf-16-le", "replace").rstrip("\0")
        elif vtype == 0 and nbytes >= 52 and _u32(d, p) == 0xFEEF04BD:
            ms, ls, pms, pls = struct.unpack_from("<IIII", d, p + 8)
            out["_file"] = (ms >> 16, ms & 0xFFFF, ls >> 16, ls & 0xFFFF)
            out["_prod"] = (pms >> 16, pms & 0xFFFF, pls >> 16, pls & 0xFFFF)
        c = (p + nbytes + 3) & ~3
        while c + 6 <= end:
            node(c, depth + 1)
            cl = _u16(d, c)
            if cl < 6:
                break
            c = (c + cl + 3) & ~3

    node(off)
    return out


def main():
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    try:  # консоль Windows может быть cp437/cp866 — печать по-русски не должна ронять тул
        sys.stdout.reconfigure(errors="replace")
    except (AttributeError, ValueError):
        pass
    if len(sys.argv) > 2 and sys.argv[2] == "--count-obj":
        import glob
        n = len(glob.glob(os.path.join(sys.argv[1], "CMakeFiles", "**", "*.obj"),
                          recursive=True))
        print(f"   скомпилировано .obj: {n}")
        return 0
    exe = sys.argv[1]
    i = pe_info(exe)
    mach = {0x8664: "x64", 0x14C: "x86"}.get(i["mach"], f"0x{i['mach']:04x}")
    print(f"   размер: {os.path.getsize(exe)} байт")
    print(f"   machine: {mach}  PE: {'PE32+' if i['magic'] == 0x20B else 'PE32'}  "
          f"subsystem: {i['subsys']} ({'WINDOWS' if i['subsys'] == 2 else '?'})")
    print(f"   Characteristics: 0x{i['chars']:04x}")
    print(f"   ресурс манифеста RT_MANIFEST, id: {i['manifest'] or 'НЕТ'}")
    bad = []
    if i["mach"] != 0x8664:
        bad.append("machine не x64")
    if i["magic"] != 0x20B:
        bad.append("не PE32+")
    if i["subsys"] != 2:
        bad.append("subsystem не WINDOWS")
    if not i["manifest"]:
        bad.append("нет ресурса манифеста (процесс падает сразу, 0xC0000135)")

    fv = i["version"].get("FileVersion") or ".".join(str(x) for x in i["version"].get("_file", ()))
    print(f"   FileVersion в exe: {fv or 'НЕТ'}")
    want = None
    if len(sys.argv) > 2:
        try:
            vh = open(sys.argv[2], encoding="utf-8", errors="replace").read()
            # Src/Version.rc: FILEVERSION и "FileVersion" =
            # MAJOR, MINOR, PATCH, REVISION_PATCHED (BUILD в версию файла не входит)
            v = {}
            for name in ("MAJOR", "MINOR", "PATCH", "BUILD", "REVISION_PATCHED"):
                m = re.search(r"#define\s+VERSION_" + name + r"\s+(\d+)", vh)
                if m:
                    v[name] = m.group(1)
            if v:
                last = v.get("REVISION_PATCHED", v.get("BUILD", "0"))
                want = ".".join([v.get("MAJOR", "?"), v.get("MINOR", "?"),
                                 v.get("PATCH", "?"), last])
                print(f"   Version.h: {v.get('MAJOR')}.{v.get('MINOR')}.{v.get('PATCH')} "
                      f"(ожидание FileVersion {want})")
        except OSError:
            pass
    if want and fv and not fv.startswith(want):
        bad.append(f"FileVersion {fv} != Version.h {want}: .res версии не пересобран "
                   "(см. DEPENDS в gen_cmake.py, PLAN_client_build_speedup.md §3)")
    print("   ИТОГ: " + ("OK" if not bad else "ПРОБЛЕМЫ: " + "; ".join(bad)))
    return 0 if not bad else 1


if __name__ == "__main__":
    sys.exit(main())
