#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""localign.py — канонические location для varying'ей SPIR-V (шаг 12, починка).

Диагноз (подтверждён записью PWRD + валидатором, §6 «шаг 12.1»): DXC выдаёт
location каждому шейдеру независимо — по порядку полей структуры. У пары VS/PS
один семантик оказывается на разных location (VUID-RuntimeSpirv-OpEntryPoint-
08743), а у VS остаются дырки, которых нет в вершинной декларации клиента
(VUID-...-Input-07904). D3D9 связывает пару по имени семантики, Vulkan — по
location, поэтому связка обязана быть восстановлена офлайн.

Лечение: одна на контейнер каноническая карта «семантика -> location»
(build_loc_map в spirv_iface.py), применяемая ко всем SPIR-V-блобам контейнера.
Карта строится по семантикам, реально присутствующим в SPIR-V этого контейнера,
порядок детерминирован -> повторная сборка побайтово стабильна. Клиент location
не хардкодит: layout он строит по SPIR-V (pVs->inputs[k].second), так что
перестановка безопасна и для вершинных атрибутов.

Режимы:
  --check   пройтись по SPIR-V-слоям контейнеров и показать пары, у которых
            семантика стоит на разных location в VS и PS (до перестановки)
  --census  переписать блоки, выровняв location (пишет рядом .aligned.spv)
  --map     каноническая карта контейнера (JSON)

Поиск контейнеров: --shd ФАЙЛ|КАТАЛОГ (рекурсивно *.shd со слоем v2/tag=2).
"""
import argparse
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import spirv_iface as si                                    # noqa: E402

TAG_SPIRV = 2
SPIRV_MAGIC = 0x07230203


def read_spv_layer(path):
    """{index: (vs_bytes|None, ps_bytes|None)} из SPIR-V-слоя контейнера v2.

    Формат слоя (mkshd2.py): u32 tag; u32 entryCount; entryCount x
    {u32 index; u32 vsOff; u32 vsSize; u32 psOff; u32 psSize}; затем блобы.
    """
    data = open(path, "rb").read()
    if len(data) < 16 or data[-16:][:4] != b"SHD2" or data[-16:][12:16] != b"SHD2":
        return None, "слоя v2 нет"
    v2hdr, ver = struct.unpack_from("<II", data, len(data) - 12)
    hdr = data[v2hdr:]
    magic, hver, v1size, tcount = struct.unpack_from("<4sIII", hdr, 0)
    if magic != b"V2H\0":
        return None, "заголовок v2 не бьётся"
    sec = None
    for i in range(tcount):
        tag, off, cnt = struct.unpack_from("<III", hdr, 16 + 12 * i)
        if tag == TAG_SPIRV:
            sec = (off, cnt)
    if sec is None:
        return None, "SPIR-V-слоя нет (цели: %s)" % [
            struct.unpack_from("<I", hdr, 16 + 12 * i)[0] for i in range(tcount)]
    off, cnt = sec
    tag, ecount = struct.unpack_from("<II", data, off)
    assert tag == TAG_SPIRV and ecount == cnt, (tag, ecount, cnt)
    base = off + 8 + 20 * ecount
    out = {}
    for i in range(ecount):
        index, vsOff, vsSize, psOff, psSize = struct.unpack_from(
            "<IIIII", data, off + 8 + 20 * i)
        vs = data[vsOff:vsOff + vsSize] if vsSize else None
        ps = data[psOff:psOff + psSize] if psSize else None
        for b in (vs, ps):
            if b is not None and struct.unpack_from("<I", b, 0)[0] != SPIRV_MAGIC:
                return None, "индекс %d: блоб не SPIR-V" % index
        out[index] = (vs, ps)
    return out, "пар %d" % ecount


def shd_files(arg):
    if os.path.isdir(arg):
        res = []
        for root, _d, files in os.walk(arg):
            for f in files:
                if f.endswith(".shd"):
                    res.append(os.path.join(root, f))
        return sorted(res)
    return [arg]


def collect(paths, log):
    """{путь: {index: (vs, ps)}} только контейнеры со слоем SPIR-V."""
    res = {}
    for p in paths:
        layer, why = read_spv_layer(p)
        if layer is None:
            log("  -- %s: %s" % (os.path.basename(p), why))
            continue
        res[p] = layer
    return res


def semantics_of(layer):
    sem = set()
    for index, (vs, ps) in layer.items():
        for b in (vs, ps):
            if b is None:
                continue
            r, err = si.interface_vars(b)
            if r is None:
                print("  !! индекс %d: %s" % (index, err))
                continue
            sem |= {v[1] for v in r[1] if not v[1].startswith("SV_")}
    return sem


def check(paths, log):
    """По каждой паре: у какого семантика location в VS-выходе и PS-входе
    различаются (это и есть 08743), и какие location VS-входа не покрыты."""
    bad_pairs = 0
    total_pairs = 0
    sem_all = set()
    for p, layer in collect(paths, log).items():
        sem = semantics_of(layer)
        sem_all |= sem
        mism = {}
        pairs_bad = 0
        for index, (vs, ps) in layer.items():
            if vs is None or ps is None:
                continue
            total_pairs += 1
            rv, e1 = si.interface_vars(vs)
            rp, e2 = si.interface_vars(ps)
            if rv is None or rp is None:
                log("  !! %s #%d: %s%s" % (os.path.basename(p), index,
                                           e1 or "", e2 or ""))
                continue
            vout = {v[1]: v[2] for v in rv[1] if v[0] == "out"}
            pin = {v[1]: v[2] for v in rp[1] if v[0] == "in"}
            bad = {s: (vout[s], pin[s]) for s in pin
                   if s in vout and vout[s] != pin[s]}
            missing = {s: pin[s] for s in pin if s not in vout}
            if bad or missing:
                bad_pairs += 1
                pairs_bad += 1
                for s, (a, b) in sorted(bad.items()):
                    mism.setdefault("разные location", []).append(
                        "%s: VS->%d, PS<-%d" % (s, a, b))
                for s, l in sorted(missing.items()):
                    mism.setdefault("нет выхода VS", []).append("%s<-loc%d" % (s, l))
        print("%-42s пар %d: с рассогласованием %d" %
              (os.path.basename(p), len(layer), pairs_bad))
        for kind, items in sorted(mism.items()):
            print("    %-18s %d: %s" % (kind, len(items), "; ".join(items[:6]) +
                                        (" ..." if len(items) > 6 else "")))
    print("\nконтейнеров со слоем SPIR-V: %d, пар: %d, с рассогласованием: %d"
          % (len(collect(paths, log)), total_pairs, bad_pairs))
    print("семантик в корпусе: %d" % len(sem_all))
    print("  " + " ".join(sorted(sem_all)))


def align(paths, log):
    for p, layer in collect(paths, log).items():
        sem = semantics_of(layer)
        m = si.build_loc_map(sem)
        print("%-42 семантик %d -> %s" % (os.path.basename(p), len(sem),
                                          json.dumps(m, sort_keys=True)))
        n = 0
        for index, (vs, ps) in layer.items():
            for stage, b in (("vs", vs), ("ps", ps)):
                if b is None:
                    continue
                new, ch = si.remap_locations(b, m)
                if new is None:
                    log("  !! #%d %s: %s" % (index, stage, ch))
                    continue
                bad = si.check_collisions(new, "#%d %s" % (index, stage))
                if bad:
                    for x in bad:
                        log("  !! " + x)
                    continue
                ok, err = si.validate(new)
                if not ok:
                    log("  !! #%d %s после выравнивания: %s" % (index, stage, err))
                    continue
                if ch:
                    n += 1
                out = "%s.%d.%s.aligned.spv" % (p, index, stage)
                open(out, "wb").write(new)
        print("    блоков изменено: %d" % n)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shd", required=True, help="ФАЙЛ или каталог с .shd")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--check", action="store_true",
                   help="показать рассогласование location в парах")
    g.add_argument("--align", action="store_true",
                   help="выровнять и выложить блоки рядом с контейнером")
    g.add_argument("--map", action="store_true", help="каноническая карта")
    a = ap.parse_args()
    paths = shd_files(a.shd)

    def log(*args):
        print(*args)

    if not paths:
        raise SystemExit("нет .shd в %s" % a.shd)
    if a.check:
        check(paths, log)
    elif a.align:
        align(paths, log)
    else:
        for p, layer in collect(paths, log).items():
            print(json.dumps(si.build_loc_map(semantics_of(layer)),
                             sort_keys=True))


if __name__ == "__main__":
    main()
