#!/usr/bin/env python3
"""Просмотр дампа загружаемых текстур (PW_VK_TEXDUMP=<файл> в клиенте).

Формат (Src/Render/nvrhibackend.cpp, TexDumpLevel0): "TXDP" + u32 версия, далее
записи: u32[9] = {texIdx, w, h, mipLevels, seamFormat(EBackendFormat), cube,
rowBytes, level, byteCount} и byteCount байт зеркала.

Декодируются DXT1 (BC1), DXT5 (BC3), A8R8G8B8, L8/A8 — остальные помечаются как
неподдерживаемые. На выходе PNG на каждую запись: <каталог>/tex<idx>_<W>x<H>_<fmt>.png

Запуск: texdump_view.py ФАЙЛ [КАТАЛОГ]
"""
import os
import struct
import subprocess
import sys

import numpy as np

BF = ['unknown', 'A8R8G8B8', 'X8R8G8B8', 'A8', 'L8', 'L16', 'R5G6B5', 'R16F',
      'A16B16G16R16F', 'R32F', 'G32R32F', 'A32B32G32R32F', 'DXT1', 'DXT2',
      'DXT3', 'DXT4', 'DXT5', 'D24S8']


def unpack565(v):
    r = (v >> 11) & 0x1F
    g = (v >> 5) & 0x1F
    b = v & 0x1F
    return np.array([(r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2)],
                    dtype=np.uint8)


def _expand565(v):
    """(N,) uint16 565 -> (N,3) uint8 с растяжением 5 бит в 8."""
    r = ((v >> 11) & 31).astype(np.int32)
    g = ((v >> 5) & 31).astype(np.int32)
    b = (v & 31).astype(np.int32)
    return np.stack([(r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2)], axis=1)


def decode_bc(data, w, h, alpha):
    """BC1/BC3 -> (h, w, 4). Блок 4x4: BC1 = 8 байт, BC3 = 8 (альфа) + 8 (цвет).

    Индексы: BC1 — 2 бита на пиксель (32-битное поле, пиксель 0 в младших битах),
    альфа BC3 — 3 бита на пиксель (48-битное поле). Палитры — по таблице D3DX.
    """
    bw, bh = (w + 3) // 4, (h + 3) // 4
    size = 16 if alpha else 8
    need = bw * bh * size
    if len(data) < need:
        data = data + bytes(need - len(data))
    blk = np.frombuffer(data[:need], dtype=np.uint8).reshape(bw * bh, size)
    n = len(blk)
    # --- цвет (BC1 в последних 8 байтах у BC3) ---
    cb = blk[:, 8:16] if alpha else blk[:, 0:8]
    c0 = cb[:, 0:2].view(np.uint16)[:, 0]
    c1 = cb[:, 2:4].view(np.uint16)[:, 0]
    p0 = _expand565(c0)
    p1 = _expand565(c1)
    hi = (c0.astype(np.int32) > c1.astype(np.int32))[:, None]
    p2 = np.where(hi, (2 * p0 + p1) // 3, (p0 + p1) // 2)
    p3 = np.where(hi, (p0 + 2 * p1) // 3, 0)
    pal = np.stack([p0, p1, p2, p3], axis=1)          # (N,4,3)
    bits = cb[:, 4:8].view(np.uint32)[:, 0].astype(np.uint64)
    col = np.zeros((n, 4, 4, 3), dtype=np.uint8)
    for k in range(16):
        sel = ((bits >> (2 * k)) & 3).astype(np.int64)
        px = pal[np.arange(n), sel]                   # (N,3)
        col[:, k // 4, k % 4, :] = px
    # --- альфа (BC3) ---
    alp = np.full((n, 4, 4), 255, dtype=np.uint8)
    if alpha:
        ab = blk[:, 0:8]
        e0 = ab[:, 0].astype(np.int64)
        e1 = ab[:, 1].astype(np.int64)
        abits = np.zeros(n, dtype=np.uint64)
        for j in range(6):
            abits |= ab[:, 2 + j].astype(np.uint64) << (8 * j)
        t = np.zeros((n, 8), dtype=np.int64)
        t[:, 0] = e0
        t[:, 1] = e1
        d0 = (6 * e0 + e1) // 7
        d1 = (5 * e0 + 2 * e1) // 7
        d2 = (4 * e0 + 3 * e1) // 7
        d3 = (3 * e0 + 4 * e1) // 7
        d4 = (2 * e0 + 5 * e1) // 7
        d5 = (e0 + 6 * e1) // 7
        s0 = (4 * e0 + 1) // 5
        s1 = (3 * e0 + 2) // 5
        s2 = (2 * e0 + 3) // 5
        s3 = (e0 + 4) // 5
        gt = (e0 > e1)[:, None]
        for j, (a, b) in enumerate(((d0, s0), (d1, s1), (d2, s2), (d3, s3),
                                    (d4, 0), (d5, 255)), start=2):
            av = a if isinstance(a, np.ndarray) else np.zeros(n, dtype=np.int64)
            bv = np.full(n, b, dtype=np.int64) if isinstance(b, int) else b
            t[:, j] = np.where(gt[:, 0], av, bv)
        for k in range(16):
            sel = ((abits >> (3 * k)) & 7).astype(np.int64)
            alp[:, k // 4, k % 4] = t[np.arange(n), sel].astype(np.uint8)
    out = np.zeros((bh * 4, bw * 4, 4), dtype=np.uint8)
    for by in range(bh):
        for bx in range(bw):
            b = by * bw + bx
            out[by * 4:by * 4 + 4, bx * 4:bx * 4 + 4, 0:3] = col[b]
            out[by * 4:by * 4 + 4, bx * 4:bx * 4 + 4, 3] = alp[b]
    return out[:h, :w]


def decode(rec, data):
    idx, w, h, mips, fmt, cube, rowBytes, level, n = rec
    if fmt in (1, 2):                       # A8R8G8B8 / X8R8G8B8
        need = rowBytes * h
        data = data[:need]
        if len(data) < need:
            data += bytes(need - len(data))
        a = np.frombuffer(data, dtype=np.uint8).reshape(h, rowBytes)[:, :w * 4].reshape(h, w, 4)
        return a[:, :, [2, 1, 0, 3]]
    if fmt in (4, 5):                       # A8 / L8
        need = rowBytes * h
        data = data[:need]
        if len(data) < need:
            data += bytes(need - len(data))
        a = np.frombuffer(data, dtype=np.uint8).reshape(h, rowBytes)[:, :w]
        return np.stack([a, a, a, np.full_like(a, 255)], axis=2)
    if fmt in (12, 14, 16):
        # GPU-дамп (TXGP) идёт с шагом строки блоков, кратным 256 байт — блоки
        # надо снять с шага, иначе decode_bc читает чужие строки
        alpha = fmt in (14, 16)
        size = 16 if alpha else 8
        bw = (w + 3) // 4
        if rowBytes and rowBytes != bw * size:
            bh = (h + 3) // 4
            if len(data) >= rowBytes * bh:
                a = np.frombuffer(data[:rowBytes * bh], dtype=np.uint8).reshape(bh, rowBytes)
                data = a[:, :bw * size].tobytes()
        return decode_bc(data, w, h, alpha)
    return None


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else 'vk_texdump.bin'
    outdir = sys.argv[2] if len(sys.argv) > 2 else '/tmp/texdump'
    os.makedirs(outdir, exist_ok=True)
    blob = open(path, 'rb').read()
    if blob[:4] not in (b'TXDP', b'TXGP'):
        raise SystemExit('не TXDP/TXGP')
    off = 8
    n = 0
    while off + 36 <= len(blob):
        rec = struct.unpack_from('<9I', blob, off)
        off += 36
        idx, w, h, mips, fmt, cube, rowBytes, level, cnt = rec
        data = blob[off:off + cnt]
        off += cnt
        img = decode(rec, data)
        name = 'tex%d_%dx%d_%s%s.png' % (idx, w, h,
                                         BF[fmt] if fmt < len(BF) else 'f%d' % fmt,
                                         '_lvl%d' % level if level else '')
        if img is None:
            print('%-34s формат не декодируется' % name)
            continue
        if '--stats' in sys.argv:
            mean = img[:, :, 0:3].reshape(-1, 3).mean(axis=0)
            nz = int((img[:, :, 0:3].reshape(-1, 3).sum(axis=1) > 24).sum())
            print('%-34s rowBytes=%d байт=%d средний RGB=(%.0f,%.0f,%.0f) '
                  'ненулевых=%d/%d'
                  % (name, rowBytes, cnt, mean[0], mean[1], mean[2], nz, w * h))
            n += 1
            continue
        ppm = os.path.join(outdir, name + '.ppm')
        rgb = np.ascontiguousarray(img[:, :, [2, 1, 0]] if img.shape[2] == 4 else img)
        with open(ppm, 'wb') as f:
            f.write(b'P6\n%d %d\n255\n' % (w, h))
            f.write(rgb.tobytes())
        subprocess.run(['convert', ppm, os.path.join(outdir, name)], check=False)
        os.unlink(ppm)
        mean = img[:, :, 0:3].reshape(-1, 3).mean(axis=0)
        print('%-34s rowBytes=%d байт=%d средний RGB=(%.0f,%.0f,%.0f)'
              % (name, rowBytes, cnt, mean[0], mean[1], mean[2]))
        n += 1
    print('записей декодировано:', n)


if __name__ == '__main__':
    main()
