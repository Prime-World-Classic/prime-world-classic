"""Smart-cast checkbox for the action bar (our teal/olive/cream UI style).
usage: gen_smartcast.py <outdir>  ->  smartcast_off.dds, smartcast_on.dds (32x32, shown at 14 px)"""
import os, sys, struct
from PIL import Image, ImageDraw, ImageFilter

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
S = 8
N = 32


def save_dds(img, path):
    w, h = img.size
    flags = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000
    pf = struct.pack('<II4sIIIII', 32, 0x41, b'\0\0\0\0', 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    header = struct.pack('<4sIIIIIII', b'DDS ', 124, flags, h, w, w * 4, 0, 0) + b'\0' * 44 + pf + struct.pack('<IIIII', 0x1000, 0, 0, 0, 0)
    r, g, b, a = img.split()
    with open(path, 'wb') as f:
        f.write(header + Image.merge('RGBA', (b, g, r, a)).tobytes())


def box(checked):
    im = Image.new('RGBA', (N * S, N * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([1 * S, 1 * S, 31 * S, 31 * S], radius=5 * S, fill=(70, 56, 24, 255))                      # dark olive rim
    d.rounded_rectangle([3 * S, 3 * S, 29 * S, 29 * S], radius=4 * S, fill=(10, 36, 36, 240), outline=(168, 170, 127, 255), width=int(1.6 * S))
    if checked:
        pts = [(8.5 * S, 16.5 * S), (13.5 * S, 22 * S), (24 * S, 9.5 * S)]
        d.line(pts, fill=(20, 40, 20, 255), width=int(6.5 * S), joint='curve')                                    # dark outline
        d.line(pts, fill=(170, 240, 140, 255), width=int(3.8 * S), joint='curve')                                 # light green check
    im = im.resize((N, N), Image.LANCZOS)
    return im


for name, chk in (('smartcast_off', False), ('smartcast_on', True)):
    im = box(chk)
    im.save(os.path.join(OUT, name + '.png'))
    save_dds(im, os.path.join(OUT, name + '.dds'))

pv = Image.new('RGBA', (140, 70), (60, 75, 55, 255))
pv.alpha_composite(Image.open(os.path.join(OUT, 'smartcast_off.png')).resize((56, 56), Image.NEAREST), (8, 7))
pv.alpha_composite(Image.open(os.path.join(OUT, 'smartcast_on.png')).resize((56, 56), Image.NEAREST), (76, 7))
pv.save(os.path.join(OUT, '_smartcast_preview.png'))
print('done')
