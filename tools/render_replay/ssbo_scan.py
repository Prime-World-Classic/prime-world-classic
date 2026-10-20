#!/usr/bin/env python3
"""ssbo_scan.py <rec.pwrd> — какие живые шейдеры требуют SSBO (набор 1 seam).

Seam привязывает к набору 1 (SSBO) заглушку — «ResMap: SSBO в наборе 1 ещё не
привязан (шаг 13.2)». Чтобы решать, чем её заменять, надо знать, КАКИЕ шейдеры
вообще объявляют StructuredBuffer и как они называются.

Берёт PWRD-запись (PW_VK_RECORD=...), вытаскивает SPIR-V каждого шейдера и ищет
в дизассемблии StorageBuffer/BufferBlock. Фрейминг записей читается через
parse_pwrd (один источник правды про формат), текстуры и буферы сразу
освобождаются, поэтому пик памяти ~2x размер файла.

Запуск с лимитом: ulimit -v 2000000 python3 tools/render_replay/ssbo_scan.py ...
"""
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import parse_pwrd  # noqa: E402


def ssbo_of(spv, tmpdir, sid):
    """имена SSBO-переменных шейдера (пусто = SSBO нет)"""
    if len(spv) < 20 or (struct_unpack(spv) != 0x07230203):
        return None            # не SPIR-V (blob не дописан)
    p = os.path.join(tmpdir, "s%u.spv" % sid)
    open(p, "wb").write(spv)
    try:
        txt = subprocess.run(["spirv-dis", p], capture_output=True, timeout=30).stdout.decode()
    except Exception as e:
        return ["spirv-dis: %s" % e]
    ids, hits = set(), []
    for l in txt.splitlines():
        body = l.split(";")[0].strip()
        if "StorageBuffer" in body or "BufferBlock" in body:
            hits.append(body)
            ids.update(w for w in body.split() if w.startswith("_"))
    if not hits:
        return []
    names = []
    for l in txt.splitlines():
        w = l.split(";")[0].split()
        if len(w) >= 4 and w[1] == "OpName" and w[2] in ids:
            names.append(w[3].strip('"'))
    return names or [h.split()[-1] for h in hits[:4]]


def struct_unpack(spv):
    import struct
    return struct.unpack_from("<I", spv, 0)[0]


def main():
    path = sys.argv[1]
    hdr, shaders, buffers, textures, layouts, draws, endpos = parse_pwrd.parse(path)
    buffers.clear()
    textures.clear()
    layouts.clear()
    draws.clear()
    tmp = tempfile.mkdtemp(prefix="ssbo_")
    res = {}
    for sid, sh in shaders.items():
        res[sid] = (sh["stage"], sh["entry"], ssbo_of(sh["spv"], tmp, sid))
    n = sum(1 for v in res.values() if v[2])
    print("шейдеров в записи: %u, объявляют SSBO: %u" % (len(res), n))
    for sid, (stage, entry, hits) in sorted(res.items()):
        if not hits:
            continue
        st = {0: "VS", 1: "PS"}.get(stage, str(stage))
        print("  #%u %s %-30s %s" % (sid, st, entry, ", ".join(hits[:8])))


if __name__ == "__main__":
    main()
