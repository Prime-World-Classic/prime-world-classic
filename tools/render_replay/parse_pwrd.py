#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Офлайн-разбор рекордера вывода Vulkan-бэкенда (файл PWRD).

Формат задаёт Src/Render/nvrhibackend.cpp (RecOpen/RecClose/RecDraw,
RecShaderId/RecBufferId/RecTextureId/RecLayoutId) — при расхождении парсера с
файлом виноват парсер, читать код бэкенда.

Записи идут в порядке появления; ресурс выгружен ОДИН РАЗ перед телом вывода,
который ссылается на него по id (0xffffffff = нет ресурса).

Использование:
  parse_pwrd.py ФАЙЛ [--draw N] [--dump-dir DIR] [-v]
"""
import argparse
import os
import struct
import sys

KNO = 0xFFFFFFFF

# типы записей (enum в nvrhibackend.cpp)
T_SHADER, T_BUFFER, T_TEXTURE, T_LAYOUT, T_DRAW, T_END = range(1, 7)

KMAX_VERTEX_STREAMS = 8
KBACKEND_SAMPLER_SLOTS = 16
DRAW_WORDS = 28
STATE_WORDS = 29

# ERenderPrimitiveType (renderbackend.h) — подписи совпадают с enum'ом бэкенда
PRIM = {
    0: "POINTLIST", 1: "LINELIST", 2: "LINESTRIP", 3: "TRIANGLELIST",
    4: "TRIANGLESTRIP", 5: "TRIANGLEFAN",
}

# ETextureFormat seam (renderbackend.h) — только расшифровка для лога
SEAM_FMT = {
    0: "UNKNOWN", 1: "A8R8G8B8", 2: "X8R8G8B8", 3: "R8G8B8", 4: "R5G6B5",
    5: "A1R5G5B5", 6: "A4R4G4B4", 7: "L8", 8: "A8", 9: "X1R5G5B5",
    10: "A2R10G10B10", 11: "F16", 12: "F32", 13: "R16F", 14: "G16R16",
    15: "A16B16G16R16", 16: "D24S8", 17: "D24X8", 18: "D16", 19: "R32F",
    20: "G16R16F", 21: "A32B32G32R32F", 22: "R8", 23: "G8R8",
    24: "A8B8G8R8_NONE", 25: "R10G10B10A2", 26: "B8G8R8A8",
}

RES_KIND = {0: "sampler", 1: "texture", 2: "ubo", 3: "ssbo", 4: "other"}


class R:
    def __init__(self, buf):
        self.b = buf
        self.o = 0
        self.short = None      # (offset, сколько просили) — обрезанный хвост

    def need(self, n):
        # процесс убит (timeout) до RecClose: stdio-буфер потерян, хвост
        # записи отсутствует. Не падаем: дополняем нулями и запоминаем факт —
        # неполную ПОСЛЕДНЮЮ запись отбрасывает parse().
        if self.o + n > len(self.b):
            if self.short is None:
                self.short = (self.o, n)
            self.b = self.b + b"\0" * (self.o + n - len(self.b))

    def u32(self):
        self.need(4)
        v = struct.unpack_from("<I", self.b, self.o)[0]
        self.o += 4
        return v

    def f32(self):
        self.need(4)
        v = struct.unpack_from("<f", self.b, self.o)[0]
        self.o += 4
        return v

    def blob(self):
        """u32 размер; u32 записано; байты. size>written = байты не писались."""
        size = self.u32()
        written = self.u32()
        self.need(written)
        data = self.b[self.o:self.o + written]
        self.o += written
        return data, size


def parse(path):
    buf = open(path, "rb").read()
    r = R(buf)
    magic = buf[:4]
    if magic != b"PWRD":
        raise SystemExit("не PWRD: %r" % magic)
    r.o = 4
    ver = r.u32()
    sw, sh = r.u32(), r.u32()
    hdr = {"ver": ver, "swapW": sw, "swapH": sh, "size": len(buf)}
    shaders, buffers, textures, layouts, draws = {}, {}, {}, {}, []
    order = []
    truncated = None
    while r.o < len(buf):
        rec_off = r.o
        t = r.u32()
        if t == T_END:
            break
        if t == T_SHADER:
            i = {"id": r.u32(), "stage": r.u32()}
            i["entry"], _ = r.blob()
            i["spv"], i["spvSize"] = r.blob()
            shaders[i["id"]] = i
        elif t == T_BUFFER:
            i = {"id": r.u32(), "isIndex": r.u32(), "index32": r.u32()}
            i["data"], i["size"] = r.blob()
            buffers[i["id"]] = i
        elif t == T_TEXTURE:
            i = {"id": r.u32(), "w": r.u32(), "h": r.u32(), "mips": r.u32(),
                 "cube": r.u32(), "seamFormat": r.u32()}
            i["data"], i["size"] = r.blob()
            textures[i["id"]] = i
        elif t == T_LAYOUT:
            i = {"id": r.u32()}
            n = r.u32()
            i["elems"] = [tuple(r.u32() for _ in range(5)) for _ in range(n)]
            n = r.u32()
            attrs = []
            for _ in range(n):
                name, _sz = r.blob()
                attrs.append({
                    "name": name.decode("ascii", "replace"),
                    "format": r.u32(), "arraySize": r.u32(),
                    "bufferIndex": r.u32(), "offset": r.u32(),
                    "elementStride": r.u32(), "isInstanced": r.u32()})
            i["attrs"] = attrs
            layouts[i["id"]] = i
        elif t == T_DRAW:
            i = {}
            keys = ("n", "vs", "ps", "layout", "rtW", "rtH", "rtDepth",
                    "dsW", "dsH", "dsDepth", "prim", "indexed", "startIdx",
                    "startVtx", "primCount", "vtx", "instCount", "vpX", "vpY",
                    "vpW", "vpH")
            for k in keys:
                i[k] = r.u32()
            i["minZ"] = r.f32()
            i["maxZ"] = r.f32()
            for k in ("scissorValid", "scL", "scT", "scR", "scB"):
                i[k] = r.u32()
            assert len(i) == DRAW_WORDS, len(i)
            ns = r.u32()
            assert ns == KMAX_VERTEX_STREAMS, ns
            i["streams"] = [(r.u32(), r.u32(), r.u32()) for _ in range(ns)]
            i["ib"], i["instBuf"], i["instSlot"], i["instOff"], i["instStride"] = \
                (r.u32() for _ in range(5))
            nsl = r.u32()
            assert nsl == KBACKEND_SAMPLER_SLOTS, nsl
            i["samplers"] = [tuple(r.u32() for _ in range(12))
                             for _ in range(nsl)]
            st = {}
            names = ("cullMode", "fillMode", "scissorTestEnable", "zEnable",
                     "zWriteEnable", "zFunc")
            for k in names:
                st[k] = r.u32()
            st["slopeScaleDepthBias"] = r.f32()
            for k in ("depthBiasRaw", "stencilEnable", "stencilFunc",
                      "stencilRef", "stencilReadMask", "stencilWriteMask",
                      "stencilFail", "stencilZFail", "stencilPass",
                      "alphaBlendEnable", "separateAlphaBlend", "srcBlend",
                      "destBlend", "blendOp", "srcBlendAlpha",
                      "destBlendAlpha", "blendOpAlpha", "colorWriteEnable",
                      "alphaTestEnable", "alphaRef", "alphaFunc",
                      "clipPlaneEnable"):
                st[k] = r.u32()
            assert len(st) == STATE_WORDS, len(st)
            i["state"] = st
            nf = r.u32()
            i["cVs"] = [r.f32() for _ in range(nf)]
            nf = r.u32()
            i["cPs"] = [r.f32() for _ in range(nf)]
            if ver >= 2:
                nres = r.u32()
                i["resItems"] = [tuple(r.u32() for _ in range(5))
                                 for _ in range(nres)]
            else:
                i["resItems"] = None
            draws.append(i)
        else:
            raise SystemExit("неизвестный тип записи %d на offset %d" % (t, r.o - 4))
        order.append(t)
    if r.short:
        truncated = ("нужно %d байт на offset %d, файла хватило до %d"
                     % (r.short[1], r.short[0], len(buf)))
        if order and order[-1] == T_DRAW:
            draws.pop()
        elif order:
            print("!!! обрезок в записи типа %d на offset %d"
                  % (order[-1], rec_off))
    hdr["truncated"] = truncated
    return hdr, shaders, buffers, textures, layouts, draws, r.o


def summarize(hdr, shaders, buffers, textures, layouts, draws, endpos, size):
    print("PWRD v%d  swap=%dx%d  файл %d Б" %
          (hdr["ver"], hdr["swapW"], hdr["swapH"], size))
    print("разобрано до offset %d (%d Б хвоста)" % (endpos, size - endpos))
    if hdr.get("truncated"):
        print("!!! файл обрезан: %s (процесс убит до RecClose — нет kRecEnd)" %
              hdr["truncated"])
    print("шейдеры=%d буферы=%d текстуры=%d раскладки=%d выводов=%d" %
          (len(shaders), len(buffers), len(textures), len(layouts), len(draws)))
    trunc = []
    for kind, coll in (("buf", buffers), ("tex", textures)):
        for i, o in coll.items():
            if len(o["data"]) < o["size"]:
                trunc.append("%s#%d %d/%d" % (kind, i, len(o["data"]), o["size"]))
    if trunc:
        print("урезанные блобы (kRecBlobCap): %s" % ", ".join(trunc[:12]) +
              (" … всего %d" % len(trunc) if len(trunc) > 12 else ""))
    print()
    for d in draws:
        vs, ps = shaders.get(d["vs"]), shaders.get(d["ps"])
        lay = layouts.get(d["layout"])
        print("#%-3d %-13s vtx=%-6d idx=%d prim=%d inst=%d rt=%dx%d ds=%dx%d "
              "vp=%d,%d %dx%d sc=%s" %
              (d["n"], PRIM.get(d["prim"], "?%d" % d["prim"]), d["vtx"],
               d["indexed"], d["primCount"], d["instCount"],
               d["rtW"], d["rtH"], d["dsW"], d["dsH"],
               d["vpX"], d["vpY"], d["vpW"], d["vpH"],
               "T" if d["scissorValid"] else "-"))
        print("     VS=%s PS=%s lay=%s" % (
            (vs["entry"].decode("ascii", "replace") if vs else "None"),
            (ps["entry"].decode("ascii", "replace") if ps else "None"),
            ("%d элм/%d атр" % (len(lay["elems"]), len(lay["attrs"]))
             if lay else "None")))
        streams = [(b, off, st) for b, off, st in d["streams"] if b != KNO]
        print("     VB: %s  IB=%s inst=%s(slot%d off%d stride%d)" % (
            ", ".join("b%d@%d/%d" % s for s in streams) or "-",
            d["ib"], d["instBuf"], d["instSlot"], d["instOff"], d["instStride"]))
        tex = [(i, s) for i, s, *_ in [(x[0], x[1]) for x in d["samplers"]]
               if i != KNO]
        if tex:
            print("     tex: %s" % ", ".join("%d:%s" %
                  (i, SEAM_FMT.get(textures[i]["seamFormat"], "?")
                   if i in textures else "?") for i, s in tex))
        if d["resItems"] is not None:
            sets = {}
            for st_, bd, kind, dim, cnt in d["resItems"]:
                sets.setdefault(st_, []).append(
                    "%d:%s%s" % (bd, RES_KIND.get(kind, "?"),
                                 ("x%d" % cnt) if cnt > 1 else ""))
            print("     sets: %s" % "  ".join("set%d[%s]" % (k, ",".join(v))
                                              for k, v in sorted(sets.items())))
        st = d["state"]
        print("     st: cull=%d fill=%d z=%d/%d f=%d stencil=%d/%d blend=%d "
              "cw=%02x alphaTest=%d/%d" %
              (st["cullMode"], st["fillMode"], st["zEnable"],
               st["zWriteEnable"], st["zFunc"], st["stencilEnable"],
               st["stencilFunc"], st["alphaBlendEnable"],
               st["colorWriteEnable"], st["alphaTestEnable"], st["alphaFunc"]))


def dump_dir(draws, out, shaders, buffers, textures, layouts):
    os.makedirs(out, exist_ok=True)
    # один вывод = один префикс: шейдеры/раскладка/буферы/текстуры, нужные ему
    for dr in draws:
        pref = os.path.join(out, "draw%03d" % dr["n"])
        for tag, idk in (("vs", "vs"), ("ps", "ps")):
            sh = dr[idk]
            if sh == KNO:
                continue
            open("%s_%s.spv" % (pref, tag), "wb").write(
                shaders[sh]["spv"])
            open("%s_%s.entry" % (pref, tag), "wb").write(shaders[sh]["entry"])
        if dr["layout"] != KNO and dr["layout"] in layouts:
            lay = layouts[dr["layout"]]
            with open("%s_layout.txt" % pref, "w") as f:
                for e in lay["elems"]:
                    f.write("elem stream=%d off=%d type=%d usage=%d idx=%d\n" % e)
                for a in lay["attrs"]:
                    f.write("attr %-12s fmt=%d arr=%d buf=%d off=%d stride=%d inst=%d\n"
                            % (a["name"], a["format"], a["arraySize"],
                               a["bufferIndex"], a["offset"],
                               a["elementStride"], a["isInstanced"]))
        for i, (b, off, stride) in enumerate(dr["streams"]):
            if b == KNO or b not in buffers:
                continue
            open("%s_vb%d.buf" % (pref, i), "wb").write(buffers[b]["data"])
        if dr["ib"] != KNO and dr["ib"] in buffers:
            open("%s_ib.buf" % pref, "wb").write(buffers[dr["ib"]]["data"])
        for i, s in enumerate(dr["samplers"]):
            if s[0] == KNO or s[0] not in textures:
                continue
            t = textures[s[0]]
            open("%s_tex%d.raw" % (pref, i), "wb").write(t["data"])
            with open("%s_tex%d.info" % (pref, i), "w") as f:
                f.write("w=%d h=%d mips=%d cube=%d fmt=%s\n" %
                        (t["w"], t["h"], t["mips"], t["cube"],
                         SEAM_FMT.get(t["seamFormat"], str(t["seamFormat"]))))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("--draw", type=int, default=None,
                    help="подробно только этот вывод (n)")
    ap.add_argument("--dump-dir", default=None,
                    help="выгрузить шейдеры/раскладки/буферы/текстуры выводов")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()
    hdr, shaders, buffers, textures, layouts, draws, endpos = parse(a.file)
    summarize(hdr, shaders, buffers, textures, layouts, draws, endpos,
              os.path.getsize(a.file))
    if a.draw is not None:
        for d in draws:
            if d["n"] != a.draw:
                continue
            print("\n=== вывод %d подробно ===" % d["n"])
            for k in ("vs", "ps", "layout", "ib", "instBuf"):
                print("  %s = %s" % (k, d[k]))
            print("  attrs:", layouts[d["layout"]]["attrs"] if d["layout"] in layouts else None)
            print("  resItems:", d["resItems"])
            print("  state:", d["state"])
            nz = sum(1 for x in d["cVs"] if x != 0.0)
            nz2 = sum(1 for x in d["cPs"] if x != 0.0)
            print("  c-bank VS ненулевых=%d PS ненулевых=%d" % (nz, nz2))
    if a.dump_dir:
        dump_dir(draws, a.dump_dir, shaders, buffers, textures, layouts)
        print("\nвыгрузка в %s" % a.dump_dir)


if __name__ == "__main__":
    main()
