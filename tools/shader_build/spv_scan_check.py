#!/usr/bin/env python3
# Проверка клиентского сканера SPIR-V (SpvResScan из Src/Render/nvrhibackend.cpp)
# на реальном блобе: порт той же логики, печатает найденные ресурсы.
import struct, sys

def shd2_pairs(path):
    b = open(path, 'rb').read()
    off = len(b) - 16
    assert b[off:off+4] == b'SHD2', 'нет трэйлера'
    hdr_off, ver = struct.unpack_from('<II', b, off + 4)
    assert b[hdr_off:hdr_off+4] == b'V2H\0'
    version, v1size, tcount = struct.unpack_from('<III', b, hdr_off + 4)
    secs = []
    p = hdr_off + 16
    for i in range(tcount):
        tag, so, cnt = struct.unpack_from('<III', b, p); p += 12
        secs.append((tag, so, cnt))
    out = []
    for tag, so, cnt in secs:
        if tag != 2:
            continue
        q = so + 8
        for i in range(cnt):
            idx, vso, vss, pso, pss = struct.unpack_from('<IIIII', b, q); q += 20
            out.append((idx, b[vso:vso+vss], b[pso:pso+pss]))
    return out

def scan(code):
    w = struct.unpack('<%dI' % (len(code)//4), code)
    n = len(w)
    if n < 5 or w[0] != 0x07230203:
        return None
    opOfId, elemOfId, ptrBase, varType, decBind, decSet = {}, {}, {}, {}, {}, {}
    ptrStore, structBlock, structBufferBlock = {}, {}, {}
    i = 5
    while i < n:
        op, wc = w[i], w[i] >> 16
        if wc == 0 or i + wc > n:
            break
        c = op & 0xffff
        # OpTypePointer (32) входит в диапазон 19..33 — разбирать ДО него,
        # иначе ветка указателей недостижима (та же ошибка была в SpvResScan)
        if c == 32 and wc >= 4:
            ptrBase[w[i+1]] = w[i+3]; ptrStore[w[i+1]] = w[i+2]
        elif 19 <= c <= 33 and wc >= 2:
            opOfId.setdefault(w[i+1], c)
            if c in (28, 29) and wc >= 3:
                elemOfId[w[i+1]] = w[i+2]
        elif c == 59 and wc >= 4:
            varType[w[i+2]] = w[i+1]
        elif c == 71 and wc >= 3:
            tgt, dec = w[i+1], w[i+2]
            val = w[i+3] if wc >= 4 else 0
            if dec == 33: decBind[tgt] = val
            elif dec == 34: decSet[tgt] = val
            elif dec == 2: structBlock[tgt] = 1
            elif dec == 3: structBufferBlock[tgt] = 1
        i += wc
    res = []
    for id, vt in varType.items():
        if id not in decBind or id not in decSet:
            continue
        if vt not in ptrStore:
            continue
        st = ptrStore[vt]
        if st not in (0, 2, 12):
            continue
        base = ptrBase.get(vt, 0xffffffff)
        baseOp = opOfId.get(base, 0)
        if baseOp in (28, 29) and base in elemOfId:
            baseOp = opOfId.get(elemOfId[base], 0)
        kind = 'other'
        if baseOp == 26: kind = 'smp'
        elif baseOp in (25, 27): kind = 'tex'
        elif baseOp == 30:
            if structBufferBlock.get(base): kind = 'ssbo'
            elif structBlock.get(base): kind = 'ubo'
            else: kind = 'ssbo' if st == 12 else 'ubo'
        elif st == 12: kind = 'ssbo'
        elif st == 2: kind = 'ubo'
        if kind != 'other':
            res.append((decSet[id], decBind[id], kind, 'st=%d baseOp=%d' % (st, baseOp)))
    return res

tot = {'tex': 0, 'smp': 0, 'ubo': 0, 'ssbo': 0}
pairs = 0
maxb = 0
for path in sys.argv[1:]:
    for idx, vs, ps in shd2_pairs(path):
        pairs += 1
        rv, rp = scan(vs), scan(ps)
        allr = (rv or []) + (rp or [])
        for st, b, kind, note in allr:
            tot[kind] = tot.get(kind, 0) + 1
            if b > maxb: maxb = b
        print('%s #%d VS %d Б: %s | PS %d Б: %s' % (path.split('/')[-1], idx, len(vs), rv, len(ps), rp))
print('пар %d, ресурсы %s, макс binding %d' % (pairs, tot, maxb))
