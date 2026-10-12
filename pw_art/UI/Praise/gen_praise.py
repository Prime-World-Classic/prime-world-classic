"""Praise notification textures in the style of our ping wheel (teal panel, olive/cream rim).
usage: gen_praise.py <outdir>
  praise_bg.dds    512x256 (panel in the top 512x160, shown at 0.625 = 320x100 px)
  prime_drop.dds   64x64 prime (nafta) drop
"""
import math, struct, os, sys
from PIL import Image, ImageDraw, ImageFilter, ImageChops

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
SS = 4


def save_dds(img, path):
    img = img.convert('RGBA')
    w, h = img.size
    flags = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000
    pf = struct.pack('<II4sIIIII', 32, 0x41, b'\0\0\0\0', 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    header = struct.pack('<4sIIIIIII', b'DDS ', 124, flags, h, w, w * 4, 0, 0) + b'\0' * 44 + pf + struct.pack('<IIIII', 0x1000, 0, 0, 0, 0)
    r, g, b, a = img.split()
    with open(path, 'wb') as f:
        f.write(header + Image.merge('RGBA', (b, g, r, a)).tobytes())


def finish(img, name, size):
    img = img.resize(size, Image.LANCZOS)
    img.save(os.path.join(OUT, name + '.png'))
    save_dds(img, os.path.join(OUT, name + '.dds'))
    return img


# ---------------------------------------------------------------- panel (texture px; 1 panel px = 1.6 texture px)
TW, TH = 512, 256
PW, PH = 512, 160
K = 1.6
S = SS
img = Image.new('RGBA', (TW * S, TH * S), (0, 0, 0, 0))

# soft shadow
sh = Image.new('RGBA', img.size, (0, 0, 0, 0))
ImageDraw.Draw(sh).rounded_rectangle([6 * S, 8 * S, (PW - 4) * S, (PH - 2) * S], radius=22 * S, fill=(0, 0, 0, 110))
sh = sh.filter(ImageFilter.GaussianBlur(6 * S))
img = Image.alpha_composite(img, sh)

# body: vertical teal gradient, lighter in the middle band
body = Image.new('RGBA', img.size, (0, 0, 0, 0))
bp = body.load()
grad = Image.new('RGBA', (1, PH), (0, 0, 0, 0))
gp = grad.load()
for y in range(PH):
    t = y / (PH - 1.0)
    mid = math.exp(-((t - 0.42) / 0.33) ** 2)
    r = int(14 + (33 - 14) * mid); g = int(40 + (74 - 40) * mid); b = int(40 + (72 - 40) * mid)
    gp[0, y] = (r, g, b, 236)
grad = grad.resize((PW * S, PH * S), Image.BICUBIC)
mask = Image.new('L', img.size, 0)
ImageDraw.Draw(mask).rounded_rectangle([4 * S, 4 * S, (PW - 4) * S, (PH - 4) * S], radius=20 * S, fill=255)
layer = Image.new('RGBA', img.size, (0, 0, 0, 0))
layer.paste(grad, (0, 0))
layer.putalpha(ImageChops.multiply(layer.getchannel('A'), mask))
img = Image.alpha_composite(img, layer)

d = ImageDraw.Draw(img)
# rim: dark olive + cream line + inner dark line (same palette as the ping wheel)
d.rounded_rectangle([4 * S, 4 * S, (PW - 4) * S, (PH - 4) * S], radius=20 * S, outline=(70, 56, 24, 255), width=3 * S)
d.rounded_rectangle([7 * S, 7 * S, (PW - 7) * S, (PH - 7) * S], radius=17 * S, outline=(168, 170, 127, 255), width=int(1.5 * S))
d.rounded_rectangle([9.5 * S, 9.5 * S, (PW - 9.5) * S, (PH - 9.5) * S], radius=15 * S, outline=(67, 67, 29, 200), width=1 * S)

# portrait slots: 64x64 panel px at (10, 8) and (246, 8) -> frame around a 60x60 portrait
for px0 in (10, 246):
    x0, y0 = px0 * K, 8 * K
    x1, y1 = (px0 + 64) * K, (8 + 64) * K
    d.rounded_rectangle([(x0 - 2) * S, (y0 - 2) * S, (x1 + 2) * S, (y1 + 2) * S], radius=7 * S, fill=(8, 30, 30, 255), outline=(70, 56, 24, 255), width=2 * S)
    d.rounded_rectangle([x0 * S, y0 * S, x1 * S, y1 * S], radius=6 * S, outline=(168, 170, 127, 255), width=int(1.5 * S))

# decorative separators around the center text area
cx = PW / 2
for side in (-1, 1):
    for k in range(30):
        t0 = 70 + 50 * k / 30.0
        t1 = 70 + 50 * (k + 1) / 30.0
        a = int(150 * math.sin(math.pi * (k + 0.5) / 30.0))
        d.line([((cx + side * t0) * S, 0.62 * PH * S), ((cx + side * t1) * S, 0.62 * PH * S)], fill=(168, 170, 127, a), width=int(1.2 * S))
# small diamond in the middle of the separator line
dy = 0.62 * PH
d.polygon([((cx - 5) * S, dy * S), (cx * S, (dy - 5) * S), ((cx + 5) * S, dy * S), (cx * S, (dy + 5) * S)], fill=(168, 170, 127, 220))

finish(img, 'praise_bg', (TW, TH))

# ---------------------------------------------------------------- prime drop (teal-green like the in-game prime)
I = 64 * S
ic = Image.new('RGBA', (I, I), (0, 0, 0, 0))
d = ImageDraw.Draw(ic)


def drop(dr, cx, cy, r, fill, scale=1.0):
    pts = []
    for k in range(0, 361, 4):
        a = math.radians(k)
        # drop: circle bottom + pointed top
        x = math.sin(a) * r
        y = math.cos(a) * r
        if y < 0:  # upper half -> taper to a point
            f = (-y / r)
            x *= (1 - f) ** 0.9
            y = -r - (r * 0.85) * f
            y = -(r + r * 0.9 * f ** 1.2)
            y = y if f > 0.02 else -r * 0.02
        pts.append(((cx + x) * S, (cy + y) * S))
    dr.polygon(pts, fill=fill)


drop(d, 32, 40, 17, (6, 70, 60, 255))
drop(d, 32, 40, 15, (40, 190, 160, 255))
drop(d, 31, 41, 11, (110, 235, 205, 255))
d.ellipse([(24) * S, (34) * S, (31) * S, (41) * S], fill=(235, 255, 250, 230))
shd = Image.new('RGBA', ic.size, (0, 0, 0, 0))
shd.putalpha(ic.getchannel('A').point(lambda v: int(v * 0.6)))
shd = shd.filter(ImageFilter.GaussianBlur(2 * S))
out = Image.new('RGBA', ic.size, (0, 0, 0, 0))
out.alpha_composite(shd, (int(1.5 * S), int(2 * S)))
finish(Image.alpha_composite(out, ic), 'prime_drop', (64, 64))

# ---------------------------------------------------------------- preview (panel at in-game size with stub portraits/text)
pv = Image.new('RGBA', (360, 250), (60, 75, 55, 255))
bg = Image.open(os.path.join(OUT, 'praise_bg.png')).resize((320, 160), Image.LANCZOS)
for row in range(2):
    oy = 10 + row * 106
    pv.alpha_composite(bg, (20, oy))
    dd = ImageDraw.Draw(pv)
    dd.rectangle([20 + 12, oy + 10, 20 + 12 + 59, oy + 10 + 59], fill=(120, 60, 50, 255))
    dd.rectangle([20 + 248, oy + 10, 20 + 248 + 59, oy + 10 + 59], fill=(70, 90, 120, 255))
    dd.text((20 + 135, oy + 18), 'PRAISE', fill=(240, 216, 144, 255))
    drop_i = Image.open(os.path.join(OUT, 'prime_drop.png')).resize((24, 24))
    pv.alpha_composite(drop_i, (20 + 132, oy + 36))
    dd.text((20 + 160, oy + 42), '+50', fill=(127, 232, 200, 255))
    dd.text((20 + 20, oy + 76), 'Name1', fill=(143, 224, 122, 255))
    dd.text((20 + 256, oy + 76), 'Name2', fill=(255, 122, 102, 255))
pv.save(os.path.join(OUT, '_praise_preview.png'))
print('done')
