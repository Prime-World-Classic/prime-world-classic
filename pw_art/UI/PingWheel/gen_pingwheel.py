"""Generates the ping wheel textures (Dota-like look) as PNG previews + uncompressed DDS for the game."""
import math, struct, os, sys
from PIL import Image, ImageDraw, ImageFilter, ImageChops

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
SS = 4  # supersampling

N = 5                       # sectors, item 0 at the top, clockwise
BG = 256                    # wheel texture size
R_OUT = 124                 # outer radius
R_IN = 42                   # center circle radius
R_SLOT = 84                 # icon slot center radius
SLOT_R = 27                 # icon slot radius


def save_dds(img, path):
    img = img.convert('RGBA')
    w, h = img.size
    # DDS header: uncompressed A8R8G8B8
    DDSD_CAPS, DDSD_HEIGHT, DDSD_WIDTH, DDSD_PITCH, DDSD_PIXELFORMAT = 0x1, 0x2, 0x4, 0x8, 0x1000
    flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT
    pf = struct.pack('<II4sIIIII', 32, 0x41, b'\0\0\0\0', 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    header = struct.pack('<4sIIIIIII', b'DDS ', 124, flags, h, w, w * 4, 0, 0) + b'\0' * 44 + pf + struct.pack('<IIIII', 0x1000, 0, 0, 0, 0)
    assert len(header) == 128
    r, g, b, a = img.split()
    data = Image.merge('RGBA', (b, g, r, a)).tobytes()  # BGRA in memory = A8R8G8B8
    with open(path, 'wb') as f:
        f.write(header + data)


def finish(img, name, size):
    img = img.resize((size, size), Image.LANCZOS)
    img.save(os.path.join(OUT, name + '.png'))
    save_dds(img, os.path.join(OUT, name + '.dds'))
    return img


def radial(size, center, r0, r1, c0, c1):
    """radial gradient disc from color c0 at r0 to c1 at r1 (RGBA tuples)"""
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    px = img.load()
    cx, cy = center
    for y in range(size):
        for x in range(size):
            d = math.hypot(x - cx, y - cy)
            if d > r1:
                continue
            t = 0 if d <= r0 else (d - r0) / (r1 - r0)
            px[x, y] = tuple(int(c0[i] + (c1[i] - c0[i]) * t) for i in range(4))
    return img


def sector_angle(i):
    return -90 + i * 360.0 / N


# ---------------------------------------------------------------- wheel background
S = BG * SS
c = S / 2
bg = Image.new('RGBA', (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(bg)
# soft drop shadow
sh = Image.new('RGBA', (S, S), (0, 0, 0, 0))
ImageDraw.Draw(sh).ellipse([c - (R_OUT + 2) * SS, c - (R_OUT + 2) * SS, c + (R_OUT + 2) * SS, c + (R_OUT + 2) * SS], fill=(0, 0, 0, 90))
sh = sh.filter(ImageFilter.GaussianBlur(5 * SS))
bg = Image.alpha_composite(bg, sh)
# main disc: dark with a slightly lighter middle ring
disc = radial(BG, (BG / 2, BG / 2), R_IN, R_OUT, (33, 74, 72, 214), (14, 40, 40, 238)).resize((S, S), Image.BICUBIC)
mask = Image.new('L', (S, S), 0)
ImageDraw.Draw(mask).ellipse([c - R_OUT * SS, c - R_OUT * SS, c + R_OUT * SS, c + R_OUT * SS], fill=255)
disc.putalpha(ImageChops.multiply(disc.getchannel('A'), mask))
bg = Image.alpha_composite(bg, disc)
d = ImageDraw.Draw(bg)
# rim
d.ellipse([c - R_OUT * SS, c - R_OUT * SS, c + R_OUT * SS, c + R_OUT * SS], outline=(70, 56, 24, 255), width=3 * SS)
d.ellipse([c - (R_OUT - 2.5) * SS, c - (R_OUT - 2.5) * SS, c + (R_OUT - 2.5) * SS, c + (R_OUT - 2.5) * SS], outline=(168, 170, 127, 255), width=int(1.5 * SS))
d.ellipse([c - (R_OUT - 4.5) * SS, c - (R_OUT - 4.5) * SS, c + (R_OUT - 4.5) * SS, c + (R_OUT - 4.5) * SS], outline=(67, 67, 29, 220), width=1 * SS)
# separators between sectors (fading toward the rim, like in Dota)
for i in range(N):
    a = math.radians(sector_angle(i) + 180.0 / N)
    for k in range(40):
        t0 = R_IN + 10 + (R_OUT - R_IN - 22) * k / 40.0
        t1 = R_IN + 10 + (R_OUT - R_IN - 22) * (k + 1) / 40.0
        alpha = int(110 * math.sin(math.pi * (k + 0.5) / 40.0))
        d.line([(c + math.cos(a) * t0 * SS, c + math.sin(a) * t0 * SS), (c + math.cos(a) * t1 * SS, c + math.sin(a) * t1 * SS)], fill=(150, 190, 175, alpha), width=int(1.5 * SS))
# center circle
d.ellipse([c - R_IN * SS, c - R_IN * SS, c + R_IN * SS, c + R_IN * SS], fill=(8, 34, 32, 240), outline=(168, 170, 127, 255), width=int(1.5 * SS))
# icon slots
for i in range(N):
    a = math.radians(sector_angle(i))
    sx, sy = c + math.cos(a) * R_SLOT * SS, c + math.sin(a) * R_SLOT * SS
    slot = radial(64, (32, 30), 0, SLOT_R, (26, 66, 62, 245), (8, 36, 34, 250)).resize((64 * SS, 64 * SS), Image.BICUBIC)
    m = Image.new('L', (64 * SS, 64 * SS), 0)
    ImageDraw.Draw(m).ellipse([(32 - SLOT_R) * SS, (32 - SLOT_R) * SS, (32 + SLOT_R) * SS, (32 + SLOT_R) * SS], fill=255)
    slot.putalpha(ImageChops.multiply(slot.getchannel('A'), m))
    sd = ImageDraw.Draw(slot)
    sd.ellipse([(32 - SLOT_R) * SS, (32 - SLOT_R) * SS, (32 + SLOT_R) * SS, (32 + SLOT_R) * SS], outline=(168, 170, 127, 255), width=int(1.5 * SS))
    bg.alpha_composite(slot, (int(sx - 32 * SS), int(sy - 32 * SS)))
finish(bg, 'wheel_bg', BG)

# ---------------------------------------------------------------- highlight of a sector (one texture per sector, pre-rotated)
for i in range(N):
    hl = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    a0 = sector_angle(i) - 180.0 / N + 1.5
    a1 = sector_angle(i) + 180.0 / N - 1.5
    px_alpha = Image.new('L', (S, S), 0)
    ImageDraw.Draw(px_alpha).pieslice([c - (R_OUT - 3) * SS, c - (R_OUT - 3) * SS, c + (R_OUT - 3) * SS, c + (R_OUT - 3) * SS], a0, a1, fill=255)
    ImageDraw.Draw(px_alpha).ellipse([c - (R_IN + 3) * SS, c - (R_IN + 3) * SS, c + (R_IN + 3) * SS, c + (R_IN + 3) * SS], fill=0)
    # brighter toward the rim
    grad = radial(BG, (BG / 2, BG / 2), R_IN, R_OUT, (110, 220, 205, 25), (140, 235, 220, 95)).resize((S, S), Image.BICUBIC)
    grad.putalpha(ImageChops.multiply(grad.getchannel('A'), px_alpha))
    hl = Image.alpha_composite(hl, grad)
    dd = ImageDraw.Draw(hl)
    dd.arc([c - (R_OUT - 3) * SS, c - (R_OUT - 3) * SS, c + (R_OUT - 3) * SS, c + (R_OUT - 3) * SS], a0, a1, fill=(236, 232, 180, 235), width=3 * SS)
    a = math.radians(sector_angle(i))
    sx, sy = c + math.cos(a) * R_SLOT * SS, c + math.sin(a) * R_SLOT * SS
    dd.ellipse([sx - (SLOT_R + 2) * SS, sy - (SLOT_R + 2) * SS, sx + (SLOT_R + 2) * SS, sy + (SLOT_R + 2) * SS], outline=(236, 232, 180, 255), width=2 * SS)
    finish(hl, 'wheel_hl%d' % i, BG)

# ---------------------------------------------------------------- icons (64x64)
I = 64 * SS


def icon_canvas():
    return Image.new('RGBA', (I, I), (0, 0, 0, 0))


def with_shadow(img):
    sh = Image.new('RGBA', img.size, (0, 0, 0, 0))
    sh.putalpha(img.getchannel('A').point(lambda v: int(v * 0.7)))
    sh = sh.filter(ImageFilter.GaussianBlur(2 * SS))
    out = Image.new('RGBA', img.size, (0, 0, 0, 0))
    out.alpha_composite(sh, (int(1.5 * SS), int(2 * SS)))
    return Image.alpha_composite(out, img)


def P(x, y):
    return (x * SS, y * SS)


# 0: "going here" - map pin
ic = icon_canvas(); d = ImageDraw.Draw(ic)
pin = [P(32, 58)]
for k in range(0, 361, 6):
    a = math.radians(150 + k * 240 / 360.0)
    pin.append(P(32 + math.cos(a) * 17, 25 + math.sin(a) * 17))
d.polygon(pin, fill=(30, 60, 120, 255))
pin2 = [P(32, 54)]
for k in range(0, 361, 6):
    a = math.radians(150 + k * 240 / 360.0)
    pin2.append(P(32 + math.cos(a) * 14.5, 25 + math.sin(a) * 14.5))
d.polygon(pin2, fill=(110, 170, 245, 255))
d.ellipse([P(26, 19), P(38, 31)], fill=(235, 245, 255, 255))
d.ellipse([P(21, 13), P(31, 21)], fill=(200, 225, 255, 120))
finish(with_shadow(ic), 'ping_icon0', 64)

# 1: "attack" - crossed swords
ic = icon_canvas(); d = ImageDraw.Draw(ic)


def sword(dr, ang):
    ca, sa = math.cos(ang), math.sin(ang)
    def T(x, y):  # local sword coords (x along blade) -> canvas
        return P(32 + x * ca - y * sa, 32 + x * sa + y * ca)
    dr.polygon([T(-20, -3.2), T(16, -3.2), T(24, 0), T(16, 3.2), T(-20, 3.2)], fill=(250, 210, 90, 255), outline=(120, 70, 10, 255))
    dr.line([T(-18, 0), T(16, 0)], fill=(255, 245, 200, 255), width=int(1.2 * SS))
    dr.polygon([T(-20, -9), T(-17, -9), T(-17, 9), T(-20, 9)], fill=(200, 120, 30, 255), outline=(110, 60, 10, 255))
    dr.polygon([T(-28, -2.5), T(-20, -2.5), T(-20, 2.5), T(-28, 2.5)], fill=(150, 80, 20, 255))
    px_, py_ = T(-28.5, 0)
    dr.ellipse([px_ - 3 * SS, py_ - 3 * SS, px_ + 3 * SS, py_ + 3 * SS], fill=(230, 160, 40, 255))


sword(d, math.radians(-45))
sword(d, math.radians(-135))
finish(with_shadow(ic), 'ping_icon1', 64)

# 2: "danger" - orange shield with "!"
ic = icon_canvas(); d = ImageDraw.Draw(ic)
shield = [P(10, 14), P(32, 6), P(54, 14), P(50, 36), P(32, 58), P(14, 36)]
d.polygon(shield, fill=(140, 40, 10, 255))
shield2 = [P(14, 16), P(32, 10), P(50, 16), P(46.5, 35), P(32, 53), P(17.5, 35)]
d.polygon(shield2, fill=(250, 120, 30, 255))
d.polygon([P(14, 16), P(32, 10), P(50, 16), P(48, 26), P(16, 26)], fill=(255, 165, 60, 255))
d.rounded_rectangle([P(28.5, 16), P(35.5, 37)], radius=3 * SS, fill=(255, 250, 235, 255))
d.ellipse([P(28, 40), P(36, 48)], fill=(255, 250, 235, 255))
finish(with_shadow(ic), 'ping_icon2', 64)


# 3/4: wards - eye
def eye(col_dark, col, col_light, name):
    ic = icon_canvas(); d = ImageDraw.Draw(ic)
    top = [P(4 + k * 56 / 30.0, 32 - 18 * math.sin(math.pi * k / 30.0)) for k in range(31)]
    bot = [P(60 - k * 56 / 30.0, 32 + 13 * math.sin(math.pi * k / 30.0)) for k in range(31)]
    d.polygon(top + bot, fill=col_dark)
    top2 = [P(8 + k * 48 / 30.0, 32 - 14 * math.sin(math.pi * k / 30.0)) for k in range(31)]
    bot2 = [P(56 - k * 48 / 30.0, 32 + 10 * math.sin(math.pi * k / 30.0)) for k in range(31)]
    d.polygon(top2 + bot2, fill=col)
    d.ellipse([P(23, 22), P(41, 40)], fill=(20, 20, 20, 255))
    d.ellipse([P(27, 26), P(37, 36)], fill=col_light)
    d.ellipse([P(28, 26), P(32, 30)], fill=(255, 255, 255, 230))
    finish(with_shadow(ic), name, 64)


eye((90, 10, 10, 255), (225, 40, 40, 255), (255, 150, 140, 255), 'ping_icon3')   # enemy ward
eye((10, 80, 20, 255), (60, 200, 70, 255), (190, 255, 170, 255), 'ping_icon4')   # need a ward

# preview sheet
prev = Image.new('RGBA', (BG * 2 + 20, BG + 20), (88, 96, 70, 255))
w = Image.open(os.path.join(OUT, 'wheel_bg.png'))
prev.alpha_composite(w, (10, 10))
prev.alpha_composite(w, (BG + 10, 10))
prev.alpha_composite(Image.open(os.path.join(OUT, 'wheel_hl1.png')), (BG + 10, 10))
for i in range(N):
    a = math.radians(sector_angle(i))
    icn = Image.open(os.path.join(OUT, 'ping_icon%d.png' % i))
    for ox in (10, BG + 10):
        scale = 1.0
        prev.alpha_composite(icn.resize((int(46 * scale), int(46 * scale)), Image.LANCZOS), (int(ox + BG / 2 + math.cos(a) * R_SLOT - 23), int(10 + BG / 2 + math.sin(a) * R_SLOT - 23)))
prev.save(os.path.join(OUT, '_preview.png'))
print('done')
