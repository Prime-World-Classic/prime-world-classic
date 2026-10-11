#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""naturemap_crc.py — сверка CRC32 сайдкара natureMap с рецептом из NatureMap.cpp.

Terrain-ресурс хранит бинарную фракцию NatureMap в отдельном файле
(<natureMapFile> = Data/.../<map>_.naturemap): первые 4 байта — CRC32 от
параметров карты (buildChecksum в Src/Terrain/NatureMap.cpp), дальше — узлы
attackSpace. Если CRC не совпадает, NatureMap::OnLoad печатает
"Ignoring stored NatureAttackSpace binary data because of CRC32 mismatch" и
отказывает; attackSpace остаётся пустым, а первый же PFRoadFlagpole
(загрузка карты) падает в NatureAttackSpace::FindSegment по pNodes==NULL.

Скрипт пересчитывает CRC по значениям из <map>.TERN.xdb и печатает результат для
разных размеров поля `sizeof(NatureMapElementInfo)` — именно оно в buildChecksum
добавляется как size_t: 4 байта на x86 и 8 байт на x64, поэтому на x64 CRC
расходится с записанным x86-редактором.

Запуск: python3 Tools/TerrainCheck/naturemap_crc.py Data/Maps/Multiplayer/MOBA/_.TERN.xdb
"""
import os
import re
import struct
import sys
import xml.etree.ElementTree as ET
import zlib

PATCH_SIZE = 10.0          # Patch::SIZE_OF_PATCH (см. EditorNative/TerrainUtils.h: PatchSize = 10.0f)
ELEMENT_INFO_SIZE = 16     # sizeof(NatureMapElementInfo): CVec2 + unsigned + битполя
ELEMENT_INFO_VERSION = 3   # NatureMapElementInfo::VERSION


def num(t, tag):
    m = re.search(r'<%s>([-\d.eE+]+)</%s>' % (tag, tag), t)
    return float(m.group(1)) if m else None


def vec2(node, tag):
    e = node.find(tag)
    if e is None:
        return (0.0, 0.0)
    return (float(e.find('x').text), float(e.find('y').text))


def roads(nm, tag):
    """<natureMap>/<tag>/Item -> [{left,center,right,nodes}] (порядок как в файле)"""
    out = []
    cont = nm.find(tag)
    if cont is None:
        return out
    for it in cont.findall('Item'):
        def f(k):
            e = it.find(k)
            return float(e.text) if e is not None and e.text else 0.0
        nodes = []
        nn = it.find('nodes')
        if nn is not None:
            for p in nn.findall('Item'):
                nodes.append((float(p.find('x').text), float(p.find('y').text)))
        out.append(dict(left=f('leftWidth'), center=f('centerWidth'), right=f('rightWidth'), nodes=nodes))
    return out


def build(t):
    """байты ровно в том порядке, как в NatureMap::buildChecksum"""
    root = ET.fromstring(t)
    nm = root.find('natureMap')
    d = bytearray()
    ex = int(root.find('elemXCount').text)
    ey = int(root.find('elemYCount').text)
    d += struct.pack('<f', ex * PATCH_SIZE)
    d += struct.pack('<f', ey * PATCH_SIZE)
    d += struct.pack('<f', float(nm.find('gridStep').text))
    d += struct.pack('<i', int(nm.find('segmentCount').text))
    for tag in ('attackSpaceRoads', 'logicRoads'):
        for r in roads(nm, tag):
            d += struct.pack('<fff', r['left'], r['center'], r['right'])
            for n in r['nodes']:
                d += struct.pack('<2f', *n)
    d += struct.pack('<2f', *vec2(nm, 'baseA'))
    d += struct.pack('<f', float(nm.find('baseAOffset').text))
    d += struct.pack('<2f', *vec2(nm, 'baseB'))
    d += struct.pack('<f', float(nm.find('baseBOffset').text))
    return bytes(d)


def main():
    path = sys.argv[1]
    t = open(path, 'rb').read().decode('utf-8', 'replace')
    # сайдкар ищется по <natureMapFile> ниже
    m = re.search(r'<natureMapFile>([^<]+)</natureMapFile>', t)
    if not m:
        print("нет <natureMapFile> — фракции нет, CRC не нужен")
        return
    sidecar = m.group(1).strip()
    # /Maps/... -> Data/Maps/...
    root = path.split('/Maps/')[0]
    side_path = os.path.join(root, sidecar.lstrip('/'))
    if not os.path.exists(side_path):
        print("сайдкар не найден: %s" % side_path)
        return
    stored = struct.unpack('<I', open(side_path, 'rb').read(4))[0]
    body = build(t)
    print("%s\n  сайдкар: %s (%d Б), stored CRC = 0x%08X"
          % (path, side_path, os.path.getsize(side_path), stored))
    for sz, fmt in ((4, '<I'), (8, '<Q')):
        crc = zlib.crc32(body + struct.pack(fmt, ELEMENT_INFO_SIZE)
                         + struct.pack('<i', ELEMENT_INFO_VERSION)) & 0xFFFFFFFF
        print("  sizeof(size_t)=%d -> CRC = 0x%08X  %s"
              % (sz, crc, "СОВПАДАЕТ" if crc == stored else "расходится"))


if __name__ == '__main__':
    main()
