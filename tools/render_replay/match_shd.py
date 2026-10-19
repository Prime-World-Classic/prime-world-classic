#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""match_shd.py — чем именно seam исполняет данный вывод: SPIR-V -> .shd + DXBC.

Зачем. Кадры Vulkan-бэкенда расходятся с DXVK-эталоном. DXBC (v1-часть .shd)
исполняет DXVK, и он даёт правильный кадр; SPIR-V (v2-слой) исполняет seam.
Оба скомпилированы из одного HLSL, значит расхождение — в соглашении о
раскладке матриц/регистров, а не в исходнике. Чтобы сравнивать их попарно,
нужно для конкретного вывода из записи PWRD узнать, из какого контейнера и
какого индекса взят SPIR-V, и вытащить рядом DXBC той же комбинации.

Совпадение — по байтам блоба (sha1). Индексы в v1 и v2 слоях означают одну и ту
же комбинацию пинов, поэтому v1[индекс] и есть эталонный DXBC.

Режимы:
  --pwrd ФАЙЛ          показать по шейдерам записи: материал, индекс, пару
  --dump-dxbc КАТАЛОГ  выложить v1-DXBC сопоставленных пар (для дизассемблирования)
  --stats              только сводка по корпусу
"""
import argparse
import collections
import hashlib
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "shader_build"))
import parse_pwrd                                                      # noqa: E402
import localign                                                        # noqa: E402


def read_v1_pairs(path):
    """{index: (vs_bytes|None, ps_bytes|None)} из v1-части (SM3 DXBC)."""
    data = open(path, "rb").read()
    # отрезать v2-слой, если он есть (иначе таблица съедет)
    if len(data) >= 16:
        tail = data[-16:]
        if tail[:4] == b"SHD2" and tail[12:16] == b"SHD2":
            v2off, _ver = struct.unpack_from("<II", tail, 4)
            if v2off + 16 <= len(data):
                tag, _ver2, v1size, _tc = struct.unpack_from("<4sIII", data, v2off)
                if tag == b"V2H\0" and v1size <= v2off:
                    data = data[:v1size]
    if len(data) < 4:
        return {}
    count = struct.unpack_from("<I", data, 0)[0]
    table = list(struct.unpack_from("<%dI" % (2 * count), data, 4))
    ends = []
    for i in range(2 * count):
        nxt = len(data)
        for j in range(2 * count):
            if table[j] > table[i] and table[j] < nxt:
                nxt = table[j]
        ends.append(nxt)
    out = {}
    for p in range(count):
        vo, po = table[2 * p], table[2 * p + 1]
        vb = data[vo:ends[2 * p]] if vo and vo < len(data) else None
        pb = data[po:ends[2 * p + 1]] if po and po < len(data) else None
        out[p] = (vb, pb)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shd", required=True, help="файл или каталог *.shd")
    ap.add_argument("--pwrd", help="запись PWRD для сопоставления")
    ap.add_argument("--dump-dxbc", help="каталог для v1-DXBC сопоставленных пар")
    ap.add_argument("--stats", action="store_true")
    a = ap.parse_args()

    files = []
    if os.path.isdir(a.shd):
        for root, _d, fs in os.walk(a.shd):
            for f in fs:
                if f.endswith(".shd"):
                    files.append(os.path.join(root, f))
    else:
        files = [a.shd]
    files.sort()

    by_hash = {}          # sha1 -> (имя, индекс, стадия, слой)
    pairs = {}            # (имя, индекс) -> {spv vs/ps, dxbc vs/ps}
    for path in files:
        name = os.path.basename(path)[:-4]
        spv, err = localign.read_spv_layer(path)
        if not spv:
            continue
        dxbc = read_v1_pairs(path)
        for index, (vs, ps) in spv.items():
            rec = pairs.setdefault((name, index), {"spv": [None, None],
                                                   "dxbc": dxbc.get(index, (None, None))})
            rec["spv"] = [vs, ps]
            for stage, b in ((0, vs), (1, ps)):
                if b:
                    by_hash.setdefault(hashlib.sha1(b).hexdigest(),
                                       (name, index, stage))
    if a.stats:
        print("контейнеров: %d, пар со слоем SPIR-V: %d, уникальных блобов: %d"
              % (len(files), len(pairs), len(by_hash)))

    if not a.pwrd:
        return
    hdr, shaders, buffers, textures, layouts, draws, _end = parse_pwrd.parse(a.pwrd)
    print("запись: выводов %d, шейдеров %d" % (len(draws), len(shaders)))
    matched = collections.Counter()
    unknown = []
    for sid in sorted(shaders):
        s = shaders[sid]
        h = hashlib.sha1(s["spv"]).hexdigest()
        hit = by_hash.get(h)
        if hit:
            matched[(hit[0], hit[1], s["stage"])] += 1
            s["mat"], s["index"] = hit[0], hit[1]
        else:
            unknown.append((sid, s["stage"], s["entry"], len(s["spv"])))
            s["mat"], s["index"] = None, None
    print("сопоставлено блобов: %d из %d" % (len(shaders) - len(unknown), len(shaders)))
    for sid, stage, entry, size in unknown:
        print("  нет совпадения: id=%d %s %s (%d Б)"
              % (sid, "VS" if stage == 0 else "PS", entry, size))

    # что реально рисует каждый вывод
    per_draw = collections.Counter()
    for dr in draws:
        vs = shaders.get(dr["vs"], {})
        ps = shaders.get(dr["ps"], {})
        per_draw[(vs.get("mat"), dr["vs"], ps.get("mat"), dr["ps"])] += 1
    print("материал выводов (VS-материал, индекс, PS-материал, индекс):")
    for (vm, vi, pm, pi), n in per_draw.most_common():
        print("   %-24s #%-6s | %-24s #%-6s  выводов %d" % (vm, vi, pm, pi, n))

    if a.dump_dxbc:
        os.makedirs(a.dump_dxbc, exist_ok=True)
        seen = set()
        for dr in draws:
            for sid in (dr["vs"], dr["ps"]):
                rec = shaders.get(sid)
                if not rec or rec.get("mat") is None:
                    continue
                key = (rec["mat"], rec["index"])
                if key in seen:
                    continue
                seen.add(key)
                p = pairs[key]
                for stage, ext in ((0, "vs"), (1, "ps")):
                    b = p["dxbc"][stage]
                    if b:
                        out = os.path.join(a.dump_dxbc, "%s_%d_%s.dxbc"
                                           % (key[0], key[1], ext))
                        open(out, "wb").write(b)
                        print("dxbc: %s (%d Б)" % (out, len(b)))


if __name__ == "__main__":
    main()
