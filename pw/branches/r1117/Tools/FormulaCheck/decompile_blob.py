#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""decompile_blob.py — декомпиляция предкомпилированных формул (compiledString).

Формат blob'а (DataExecutor::FormulaHeader + то, что собрал FormulaBuilder):
  u8  version (= FORMULA_VERSION, 21)
  u32 entryPointOffset          (смещение abilityFunctor внутри образа)
  u16 relocsNum                 внутренних релокаций (абсолютных, image-relative)
  u16 extRelocsNum              внешних (вызовы через link-таблицу)
  u32 relocations[relocsNum]    смещения 4-байтных слотов внутри образа
  u32 extRelocations[extNum]    то же, для внешних
  image = код + данные (секции .obj, склеенные FormulaBuilder)
При загрузке DataExecutor: reloc[i] -> *(image+off) += base образа;
extReloc[i] -> *(image+off) += (linkJumpTable + linkCount) - base.
Значение в ext-слоте, записанное FormulaBuilder: GetSymbolOffset(name) - (off+4),
GetSymbolOffset = (idx - 8) * sizeof(JumpAbsoluteFar=7), таблица имён —
ExecutionMemoryManager::pLinkTable (_sin _cos _sqrt _pow _fmod _fabs _floor _ceil).
Значит имя внешнего вызова восстанавливается из blob'а однозначно.

Аксессоры скомпилированы как виртуальные вызовы (call [reg + 4*slot]) — имя слота
даёт kFormulaAccessors (Src/System/FormulaAccessors.inc, генерируется из
FormulaPars.h: {имя, интерфейс, сигнатура, слот}).

Запуск:
  Tools/FormulaCheck/decompile_blob.py --blob '<base64>'
  Tools/FormulaCheck/decompile_blob.py --xdb Data/Items/Talents/Class/Bomber/Ability_A3.TALENT.xdb
  Tools/FormulaCheck/decompile_blob.py --no-source Data          # все 62 «blob без sString»
"""
import argparse
import base64
import bisect
import os
import re
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
R1117 = os.path.abspath(os.path.join(HERE, '..', '..'))
ACC_INC = os.path.join(R1117, 'Src', 'System', 'FormulaAccessors.inc')
LINK_SYMS = ['_sin', '_cos', '_sqrt', '_pow', '_fmod', '_fabs', '_floor', '_ceil']
LINK_SIZE = 7

IFACE = {0: 'unit', 1: 'misc', 2: 'custom'}


def load_accessors():
    """slot -> {iface: [имена]} и имя -> (iface, slot)."""
    by_slot = {}
    by_name = {}
    try:
        txt = open(ACC_INC, encoding='utf-8', errors='replace').read()
    except OSError:
        return by_slot, by_name
    m = re.search(r'kFormulaAcc\[\]\s*=\s*\{(.*?)\n\};', txt, re.S)
    if not m:
        return by_slot, by_name
    for name, iface, sig, slot in re.findall(r'\{"(\w+)",\s*(\d+),\s*(\d+),\s*(\d+)\}', m.group(1)):
        iface, slot = int(iface), int(slot)
        by_slot.setdefault((iface, slot), []).append(name)
        by_name[name] = (iface, slot, int(sig))
    return by_slot, by_name


ACC_BY_SLOT, ACC_BY_NAME = load_accessors()


def parse_blob(b64):
    blob = base64.b64decode(b64)
    if len(blob) < 9:
        return None
    version = blob[0]
    entry, = struct.unpack_from('<I', blob, 1)
    relocs_num, ext_num = struct.unpack_from('<HH', blob, 5)
    hdr = 9 + 4 * (relocs_num + ext_num)
    relocs = list(struct.unpack_from('<%dI' % relocs_num, blob, 9))
    ext = list(struct.unpack_from('<%dI' % ext_num, blob, 9 + 4 * relocs_num))
    return dict(blob=blob, version=version, entry=entry, relocs=relocs, ext=ext,
                image=blob[hdr:], size=len(blob), hdr=hdr)


def strings_in(image, minlen=3):
    out = []
    for m in re.finditer(rb'[\x20-\x7e]{%d,}' % minlen, image):
        out.append((m.start(), m.group().decode('cp1251', 'replace')))
    return out


def ext_symbol(value, site):
    """значение в ext-релокации -> имя из pLinkTable (или None)."""
    sym_off = value + site + 4          # FormulaBuilder: symOff - (site+4)
    if sym_off % LINK_SIZE:
        return None
    idx = len(LINK_SYMS) + sym_off // LINK_SIZE
    if 0 <= idx < len(LINK_SYMS):
        return LINK_SYMS[idx]
    return None


def disasm(image):
    with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as f:
        f.write(image)
        path = f.name
    try:
        p = subprocess.run(['objdump', '-D', '-b', 'binary', '-m', 'i386', '-M', 'intel', path],
                           capture_output=True, text=True)
    finally:
        os.unlink(path)
    if p.returncode != 0:
        return []
    out = []
    for line in p.stdout.splitlines():
        m = re.match(r'\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(.*)', line)
        if m:
            out.append((int(m.group(1), 16), m.group(2).strip(), m.group(3).strip()))
    return out


ARG_IFACE = {0x8: 'unit(pFirst)', 0xc: 'second(pSecond)', 0x10: 'misc(pMisc)'}


def annotate(info):
    image = info['image']
    reloc_sites = {o: 'reloc' for o in info['relocs']}
    ext_sites = {o: 'ext' for o in info['ext']}
    strs = strings_in(image)
    str_at = dict(strs)
    lines = disasm(image)
    print('  version=%d entry=0x%x relocs=%s ext=%s image=%d Б' %
          (info['version'], info['entry'], [hex(x) for x in info['relocs']],
           [hex(x) for x in info['ext']], len(image)))
    if strs:
        print('  строки в данных: ' + ', '.join('0x%x="%s"' % (o, s) for o, s in strs))
    det = []
    for off in info['relocs']:
        if off + 4 <= len(image):
            v, = struct.unpack_from('<I', image, off)
            s = str_at.get(v)
            det.append('0x%x -> image+0x%x%s' % (off, v, (' "%s"' % s) if s else ''))
    for off in info['ext']:
        if off + 4 <= len(image):
            v, = struct.unpack_from('<I', image, off)
            det.append('0x%x -> link:%s' % (off, ext_symbol(v, off) or '?'))
    if det:
        print('  релокации: ' + ', '.join(det))
    cur_iface = '?'
    for off, bytes_, text in lines:
        notes = []
        if off in reloc_sites:
            notes.append('RELOC(абс.)')
        if off in ext_sites:
            val, = struct.unpack_from('<I', image, off)
            sym = ext_symbol(val, off)
            notes.append('EXT->%s' % (sym or '?'))
        m = re.search(r'mov\s+(?:ecx|esi|edi|eax),\s*DWORD PTR \[ebp\+0x([0-9a-f]+)\]', text)
        if m:
            cur_iface = ARG_IFACE.get(int(m.group(1), 16), '?')
        m = re.search(r'call\s+(?:DWORD PTR )?\[(\w+)(?:\+0x([0-9a-f]+))?\]', text)
        if m:
            slot = int(m.group(2), 16) // 4 if m.group(2) else -1
            names = ACC_BY_SLOT.get((0, slot)) or []
            notes.append('vtable slot %d%s -> %s' %
                         (slot, ' (recv=%s)' % cur_iface if cur_iface != '?' else '',
                          '|'.join(names[:3]) if names else '?'))
        # push imm32 -> image-relative адрес (релокация закроется при загрузке)
        m = re.match(r'push\s+(\d+|0x[0-9a-f]+)$', text)
        if m:
            v = int(m.group(1), 0)
            if 0 <= v < len(image):
                s = str_at.get(v)
                notes.append('-> image+0x%x%s' % (v, ' "%s"' % s if s else ''))
        yield_ = ' <== ENTRY' if off == info['entry'] else ''
        print('    %04x  %-28s %s%s%s' % (off, bytes_, text,
                                          ('   ; ' + '; '.join(notes)) if notes else '', yield_))


def walk_xdb(path, want_no_source=False):
    """возвращает (путь к элементу, sString, compiledString, returnType)."""
    try:
        t = ET.parse(path)
    except Exception:
        return []
    res = []

    def rec(el, chain):
        c = el.find('compiledString')
        s = el.find('sString')
        if c is not None and (c.text or '').strip():
            src = ' '.join(((s.text if s is not None else '') or '').split())
            if not want_no_source or not src:
                rt = el.find('returnType')
                res.append((' / '.join(chain), src, ' '.join((c.text or '').split()),
                            (rt.text or '').strip() if rt is not None and rt.text else ''))
        tag = el.tag
        ident = el.get('id') or el.get('name') or ''
        for ch in el:
            rec(ch, chain + [tag + ('[%s]' % ident if ident else '')])
    rec(t.getroot(), [])
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--blob', help='base64 одного blob')
    ap.add_argument('--xdb', help='показать все формулы .xdb')
    ap.add_argument('--no-source', metavar='DATA', help='все формулы Data с blob, но без sString')
    a = ap.parse_args()

    jobs = []
    if a.blob:
        jobs.append(('<cli>', '', a.blob, ''))
    if a.xdb:
        jobs += walk_xdb(a.xdb)
    if a.no_source:
        for dp, _d, fs in os.walk(a.no_source):
            for fn in sorted(fs):
                if fn.endswith('.xdb'):
                    jobs += walk_xdb(os.path.join(dp, fn), want_no_source=True)

    n = 0
    for chain, src, comp, ret in jobs:
        info = parse_blob(comp)
        if info is None:
            print('%s: blob не разобран' % chain)
            continue
        n += 1
        print('=' * 78)
        print('%s\n  returnType=%r sString=%r' % (chain, ret, src))
        annotate(info)
    print('=' * 78)
    print('разобрано blob: %d' % n, file=sys.stderr)


if __name__ == '__main__':
    main()
