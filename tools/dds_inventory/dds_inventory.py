#!/usr/bin/env python3
"""dds_inventory.py — инвентаризация DDS-контейнеров в Data/.

Зачем: клиент грузит текстуры своим загрузчиком (Render/ReadDDS.cpp), а D3DX —
только как фолбэк для не-DDS. Чтобы убрать фолбэк (этап 2/4 плана NVRHI), надо
знать, какие варианты DDS реально встречаются в контенте: fourCC/битовые маски,
сжатие (DXT1/3/5), DX10-заголовок, кубы, число mipmap.

Использование:
  ./dds_inventory.py <каталог> [<каталог> ...]
"""
import collections
import os
import struct
import sys

DDS_MAGIC = 0x20534444                      # 'DDS '
DDSD_MIPMAPCOUNT = 0x20000
DDSCAPS2_CUBEMAP = 0x200
DDSCAPS2_VOLUME = 0x200000
DDPF_FOURCC = 0x4
DDPF_RGB = 0x40
DDPF_LUMINANCE = 0x20000

DXGI_NAMES = {87: "BC1_UNORM", 88: "BC1_UNORM_SRGB", 89: "BC2_UNORM", 90: "BC2_UNORM_SRGB",
              91: "BC3_UNORM", 92: "BC3_UNORM_SRGB", 95: "BC4_UNORM", 96: "BC4_SNORM",
              97: "BC5_UNORM", 98: "BC5_UNORM_SRGB", 99: "BC6H_SF16", 100: "BC6H_UF16",
              101: "BC7_UNORM", 102: "BC7_UNORM_SRGB", 28: "R8G8B8A8_UNORM",
              29: "R8G8B8A8_UNORM_SRGB", 24: "R8G8B8A8_TYPELESS", 83: "B8G8R8A8_UNORM",
              85: "B8G8R8A8_TYPELESS", 61: "R16G16B16A16_UNORM", 10: "R16G16B16A16_FLOAT",
              2: "R32G32B32A32_FLOAT", 66: "D24_UNORM_S8_UINT", 71: "R16_UNORM",
              62: "R16_FLOAT", 63: "R32_FLOAT", 65: "D32_FLOAT_S8X24_UINT", 44: "B5G6R5_UNORM",
              82: "B5G5R5A1_UNORM", 115: "A8_UNORM", 67: "R24G8_TYPELESS", 68: "R24_UNORM_X8_TYPELESS"}


def masks_key(r, g, b, a):
    return f"{r:#010x}/{g:#010x}/{b:#010x}/{a:#010x}"


def scan(path):
    with open(path, "rb") as f:
        head = f.read(128 + 40)
    if len(head) < 128 or struct.unpack_from("<I", head, 0)[0] != DDS_MAGIC:
        return None
    size, flags, height, width = struct.unpack_from("<IIII", head, 4)
    pitch, depth, mip = struct.unpack_from("<III", head, 20)
    pf_size, pf_flags, fourcc, rgb_bit, rmask, gmask, bmask, amask = struct.unpack_from(
        "<8I", head, 76)   # DDPIXELFORMAT начинается с 76 (reserved1[11] после 32)
    dxcaps = struct.unpack_from("<I", head, 108)[0]   # dwCaps
    caps2 = struct.unpack_from("<I", head, 112)[0]   # dwCaps2
    key = {
        "fourcc": (fourcc.to_bytes(4, "little").decode("latin1") if pf_flags & DDPF_FOURCC else None),
        "rgb": (rgb_bit, masks_key(rmask, gmask, bmask, amask)) if pf_flags & (DDPF_RGB | DDPF_LUMINANCE) else None,
        "cube": bool(caps2 & DDSCAPS2_CUBEMAP),
        "volume": bool(caps2 & DDSCAPS2_VOLUME),
        "mips": (mip if flags & DDSD_MIPMAPCOUNT else 1),
        "dxgi": None,
    }
    if key["fourcc"] == "DX10" and len(head) >= 168:
        dxgi, dim, dmip, darr, misc = struct.unpack_from("<5I", head, 128)
        key["dxgi"] = DXGI_NAMES.get(dxgi, f"dxgi{dxgi}")
        key["cube"] = key["cube"] or (dim == 4) or (darr >= 6 and misc & 0x8)
        key["mips"] = dmip or key["mips"]
    return key



def expected_payload(fmt_label, w, h, mips):
    """Размер битовых данных, который ожидает загрузчик (правила GetSurfaceInfo
    из Render/ReadDDS.cpp: DXT — блоками, несжатые — pitch, кратный 4 байтам)."""
    total = 0
    for _ in range(max(mips, 1)):
        if fmt_label in ("DXT1", "BC1_UNORM", "BC1_UNORM_SRGB"):
            total += ((max(w, 1) + 3) // 4) * ((max(h, 1) + 3) // 4) * 8
        elif fmt_label.startswith(("DXT3", "DXT5", "BC2", "BC3", "BC7")):
            total += ((max(w, 1) + 3) // 4) * ((max(h, 1) + 3) // 4) * 16
        else:
            bpp = 4 if fmt_label.startswith("RGB32") else (1 if fmt_label.startswith("RGB8") else 4)
            pitch = ((max(w, 1) * bpp) + 3) // 4 * 4
            total += pitch * max(h, 1)
        w, h = max(w // 2, 1), max(h // 2, 1)   # D3D9: цепочка до 1x1, 1x1 тоже свой блок
    return total


def check(path, key):
    """Возвращает (ожидалось, фактически) для битовых данных файла."""
    with open(path, "rb") as f:
        f.seek(0, os.SEEK_END)
        size = f.tell()
    hdr = 128
    if key["fourcc"] == "DX10":
        hdr = 128 + 20
    w = struct.unpack_from("<I", open(path, "rb").read(20), 16)[0]
    h = struct.unpack_from("<I", open(path, "rb").read(20), 12)[0]
    label = key["dxgi"] or key["fourcc"] or (f"RGB{key['rgb'][0]}" if key["rgb"] else "???")
    faces = 6 if key["cube"] else 1
    exp = expected_payload(label, w, h, key["mips"]) * faces
    return exp, size - hdr

def main():
    roots = sys.argv[1:] or ["."]
    files = 0
    skipped = 0
    fmt = collections.Counter()
    dims = collections.Counter()
    mip_hist = collections.Counter()
    sizes = collections.Counter()
    examples = {}
    bad_size = []
    for root in roots:
        for dirpath, _d, names in os.walk(root):
            for n in names:
                if not n.lower().endswith(".dds"):
                    continue
                p = os.path.join(dirpath, n)
                k = scan(p)
                if k is None:
                    skipped += 1
                    continue
                files += 1
                exp, act = check(p, k)
                if exp != act:
                    bad_size.append((p, exp, act))
                if k["dxgi"]:
                    label = f"DX10/{k['dxgi']}"
                elif k["fourcc"]:
                    label = k["fourcc"]
                elif k["rgb"]:
                    bit, masks = k["rgb"]
                    label = f"RGB{bit} {masks}"
                else:
                    label = "???"
                kind = "cube" if k["cube"] else ("volume" if k["volume"] else "2D")
                fmt[(kind, label)] += 1
                mip_hist[k["mips"]] += 1
                sizes[(kind, label, k["mips"])] += 1
                examples.setdefault((kind, label), p)
    print(f"DDS-файлов разобрано: {files}, не-DDS/битых: {skipped}")
    print("\nформаты:")
    for (kind, label), c in sorted(fmt.items(), key=lambda kv: -kv[1]):
        print(f"  {kind:6} {label:34} {c:>6}   пример: {examples[(kind, label)]}")
    print(f"\nрасхождение размера битовых данных: {len(bad_size)}")
    for p, e, a in bad_size[:15]:
        print(f"  ожидалось {e}, файл {a}: {p}")
    print("\nmipmap-цепочки (число уровней -> файлов):")
    for m, c in sorted(mip_hist.items()):
        print(f"  {m:>3} -> {c}")


if __name__ == "__main__":
    main()
