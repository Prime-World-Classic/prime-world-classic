"""Ping wheel textures (Dota-like look): wheels for 4..8 sectors (wheel_gen.py) + the icons of the ping types.

  python gen_pingwheel.py <out dir>   (game: pw/branches/r1117/Data/UI/Styles/Icons/PingWheel)

ping_icon<type>.dds 64x64; type = the network value of the ping (AdventureScreen.cpp s_pingTypes), 5 = the plain ping (gen_pingground.py).
The printed PING_HL_OFFSETS go to MainInterface.as.
"""
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

FONT = 'C:/Windows/Fonts/arialbd.ttf'


def badge(col, name, draw_fn):
    """round colored badge (like the plain ping) with a white symbol"""
    ic = icon_canvas(); d = ImageDraw.Draw(ic)
    r0, g0, b0 = col
    dark = (max(0, r0 // 4), max(0, g0 // 4), max(0, b0 // 4), 255)
    d.ellipse([P(6, 6), P(58, 58)], fill=dark)
    d.ellipse([P(9, 9), P(55, 55)], fill=(r0, g0, b0, 255))
    d.ellipse([P(13, 11), P(51, 33)], fill=(min(255, r0 + 60), min(255, g0 + 60), min(255, b0 + 60), 110))
    draw_fn(d, dark)
    finish(with_shadow(ic), name, 64)


def poly(d, pts, dark, fill=(255, 255, 255, 255)):
    d.polygon([P(x, y) for x, y in pts], fill=fill, outline=dark)


def text(d, s, dark, size=34, dy=0):
    from PIL import ImageFont
    f = ImageFont.truetype(FONT, size * SS)
    w = d.textlength(s, font=f)
    x, y = 32 * SS - w / 2, (32 + dy) * SS - size * SS * 0.62
    for ox, oy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
        d.text((x + ox * SS, y + oy * SS), s, font=f, fill=dark)
    d.text((x, y), s, font=f, fill=(255, 255, 255, 255))


# 6: retreat - arrow back (left-down)
badge((235, 180, 40), 'ping_icon6', lambda d, k: poly(d, [(14, 32), (30, 18), (30, 26), (48, 26), (48, 38), (30, 38), (30, 46)], k))
# 7: need help - white cross
badge((60, 190, 120), 'ping_icon7', lambda d, k: poly(d, [(27, 15), (37, 15), (37, 27), (49, 27), (49, 37), (37, 37), (37, 49), (27, 49), (27, 37), (15, 37), (15, 27), (27, 27)], k))


def gather(d, k):
    for ang in (90, 210, 330):
        a = math.radians(ang)
        ca, sa = math.cos(a), math.sin(a)

        def T(x, y):  # local: x toward the center
            return (32 - (22 - x) * ca - y * sa * -1 * 0 - y * -sa, 32 - (22 - x) * sa + y * ca)
        pts = [(0, -3), (8, -3), (8, -7), (15, 0), (8, 7), (8, 3), (0, 3)]
        poly(d, [(32 + math.cos(a) * (22 - px) - math.sin(a) * py, 32 + math.sin(a) * (22 - px) + math.cos(a) * py) for px, py in pts], k)
    d.ellipse([P(28, 28), P(36, 36)], fill=(255, 255, 255, 255), outline=k)


# 8: gather here - three arrows to the center
badge((40, 170, 220), 'ping_icon8', gather)
# 9: defend - shield
badge((70, 100, 230), 'ping_icon9', lambda d, k: poly(d, [(17, 18), (32, 13), (47, 18), (45, 34), (32, 50), (19, 34)], k))
# 10: enemy missing - "?"
badge((230, 200, 40), 'ping_icon10', lambda d, k: text(d, '?', k, 36, 2))


def target(d, k):
    w = (255, 255, 255, 255)
    d.ellipse([P(17, 17), P(47, 47)], outline=w, width=4 * SS)
    d.ellipse([P(28, 28), P(36, 36)], fill=w)
    for x0, y0, x1, y1 in ((30, 11, 34, 21), (30, 43, 34, 53), (11, 30, 21, 34), (43, 30, 53, 34)):
        d.rectangle([P(x0, y0), P(x1, y1)], fill=w)


# 11: target / focus - crosshair
badge((220, 40, 40), 'ping_icon11', target)


def ambush(d, k):
    top = [(12 + i * 40 / 20.0, 34 - 11 * math.sin(math.pi * i / 20.0)) for i in range(21)]
    bot = [(52 - i * 40 / 20.0, 34 + 8 * math.sin(math.pi * i / 20.0)) for i in range(21)]
    poly(d, top + bot, k)
    d.ellipse([P(26, 27), P(38, 39)], fill=(90, 30, 130, 255))
    d.ellipse([P(29, 30), P(35, 36)], fill=(20, 10, 30, 255))
    text(d, '!', k, 20, -16)


# 12: ambush - eye with "!"
badge((150, 80, 210), 'ping_icon12', ambush)


def flag(d, k):
    d.rectangle([P(19, 13), P(23, 52)], fill=(255, 255, 255, 255), outline=k)
    poly(d, [(23, 14), (47, 20), (40, 26), (47, 32), (23, 34)], k)


# 13: capture the flag
badge((235, 130, 40), 'ping_icon13', flag)


def tree(d, k):
    d.rectangle([P(29, 40), P(35, 52)], fill=(255, 255, 255, 255), outline=k)
    poly(d, [(32, 12), (44, 28), (38, 28), (47, 41), (17, 41), (26, 28), (20, 28)], k)


# 14: going to the forest
badge((100, 170, 60), 'ping_icon14', tree)

# ---------------------------------------------------------------- wheels for 4..8 sectors
from wheel_gen import wheel, as_offsets
offs = wheel(OUT, 'ping_wheel', 256, 124, 42, 26)
print(as_offsets('PING_HL_OFFSETS', offs))
