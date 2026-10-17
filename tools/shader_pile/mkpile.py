#!/usr/bin/env python3
"""mkpile.py — сборщик/читатель pile-контейнеров клиента (формат
Src/System/FileSystem/FilePileFormat.h + FilePileCompiler.cpp/FilePileLoader.cpp).

Зачем: единственный готовый FilePileCompiler.exe в дереве — x86-сборка VS2008,
ей нужны MSVCR90/MSVCP90/CrashRpt/zlib1, которых нет на урезанной тестовой
Windows. Формат простой, собирается без тулчейна.

Файл:
  [u32 TAG 'Pile']                      страховочная метка
  [данные файлов]                       как есть или zlib (compress2, уровень 9)
  [таблица имён]                        имена с NUL, XOR-поток SimpleCrypt
  [индекс]                              N записей SFilePileEntry (24 байта), XOR-поток
  [хвост SFilePileTail (24 байта)]      без шифрования

SimpleCrypt: state=0; для каждого байта state=(state+83)&0xFF; byte^=state.
Поток НЕ сбрасывается между записями внутри блока (сброс — только перед блоком).
Поиск имени в клиенте регистронезависимый (NStr::ICmp + tolower-хэш), имя не
должно начинаться с '/' или '0'.

Использование:
  ./mkpile.py build --root data --out zzzresult.pile [--mask '*.shd']
  ./mkpile.py list <pile>            # проверить контейнер (тот же код чтения)
"""
import argparse
import os
import struct
import sys
import zlib

TAG = 0x656c6950          # 'Pile'
VERSION = 0x00010002
COMPRESSED = 0x0001
TAIL_FMT = "<6I"          # tag, version, filesNumber, stringsOffset, stringsLength, fileIndexOffset
ENTRY_FMT = "<6I"         # size, sourceSize, offset, flags, fileNameOffset, crc
BASE_NUM = 83


def crypt(buf: bytes) -> bytes:
    """XOR-поток SimpleCrypt (шифрование и дешифрование — одна и та же операция)."""
    out = bytearray(buf)
    state = 0
    for i in range(len(out)):
        state = (state + BASE_NUM) & 0xFF
        out[i] ^= state
    return bytes(out)


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def build(root, out, mask=("*.shd",), comp_size=1, max_ratio=0.90):
    import fnmatch
    files = []
    for dirpath, _dirs, names in os.walk(root):
        for n in names:
            rel = os.path.relpath(os.path.join(dirpath, n), root).replace(os.sep, "/")
            if any(fnmatch.fnmatch(n, m) for m in mask):
                files.append(rel)
    files.sort()
    if not files:
        sys.exit(f"в {root} нет файлов по маске {mask}")

    data = bytearray()
    data += struct.pack("<I", TAG)
    entries = []
    for rel in files:
        with open(os.path.join(root, rel), "rb") as f:
            raw = f.read()
        e = {"name": rel, "size": len(raw), "src": len(raw), "offset": len(data),
             "flags": 0, "nameOff": 0, "crc": crc32(raw)}
        if comp_size >= 0 and len(raw) > comp_size:
            z = zlib.compress(raw, 9)
            if z and len(z) / len(raw) <= max_ratio:
                raw = z
                e["size"] = len(z)
                e["flags"] |= COMPRESSED
        data += raw
        entries.append(e)

    strings_off = len(data)
    names = b"".join(e["name"].encode("utf-8") + b"\x00" for e in entries)
    for e in entries:
        e["nameOff"] = strings_off + names.index(e["name"].encode("utf-8") + b"\x00") - strings_off
    blob = crypt(names)
    data += blob
    strings_len = len(blob)

    index_off = len(data)
    idx = b"".join(struct.pack(ENTRY_FMT, e["size"], e["src"], e["offset"], e["flags"],
                               e["nameOff"], e["crc"]) for e in entries)
    data += crypt(idx)

    data += struct.pack(TAIL_FMT, TAG, VERSION, len(entries), strings_off, strings_len, index_off)
    with open(out, "wb") as f:
        f.write(data)
    print(f"{out}: файлов {len(entries)}, размер {len(data)}")


def read_tail(path):
    with open(path, "rb") as f:
        f.seek(-struct.calcsize(TAIL_FMT), os.SEEK_END)
        tail = struct.unpack(TAIL_FMT, f.read(struct.calcsize(TAIL_FMT)))
    if tail[0] != TAG or tail[1] != VERSION:
        sys.exit(f"{path}: плохая метка/версия {tail[:2]!r}")
    return tail


def list_pile(path, grep=None):
    tag, ver, n, strings_off, strings_len, index_off = read_tail(path)
    with open(path, "rb") as f:
        f.seek(strings_off)
        names = crypt(f.read(strings_len))
        f.seek(index_off)
        idx = crypt(f.read(n * struct.calcsize(ENTRY_FMT)))
        data_start = 4                                  # страховочная метка в начале
        shown = 0
        for i in range(n):
            size, src, off, flags, name_off, crc = struct.unpack_from(ENTRY_FMT, idx, i * 24)
            end = names.index(b"\x00", name_off)
            name = names[name_off:end].decode("utf-8", "replace")
            if grep and grep not in name:
                continue
            print(f"{name:60s} {src:>10} {'zlib' if flags & COMPRESSED else 'raw ':4} crc={crc:08x}")
            shown += 1
            if shown >= 40:
                print(f"... ещё {n - shown} записей")
                break
    print(f"всего записей: {n}")


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("--root", required=True)
    b.add_argument("--out", required=True)
    # масок может быть несколько (напр. '*.shd' для контейнеров материалов и
    # '*.spv' для sidecar-слоя OnlineShaders — шаг 14)
    b.add_argument("--mask", action="append", default=[])
    b.add_argument("--comp-size", type=int, default=1)
    b.add_argument("--max-ratio", type=float, default=0.90)
    l = sub.add_parser("list")
    l.add_argument("pile")
    l.add_argument("--grep")
    a = ap.parse_args()
    if a.cmd == "build":
        build(a.root, a.out, a.mask or ["*.shd"], a.comp_size, a.max_ratio)
    else:
        list_pile(a.pile, a.grep)


if __name__ == "__main__":
    main()
