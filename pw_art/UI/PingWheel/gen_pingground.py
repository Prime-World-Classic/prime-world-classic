"""Ground ping visuals (Dota-like): ring on the ground (white, tinted by the game), colored beams, plain ping icon.
usage: gen_pingground.py <outdir>
  ping_ring.dds      256x256 + mips, white glow ring -> Data/Effects/UI/Ping/PingRing.dds (AOE material)
  ping_beam<type>.dds 32x128 vertical light beam per ping type -> Data/UI/Styles/Icons/PingWheel/
  ping_icon5.dds     64x64 "!" badge of the plain ping -> Data/UI/Styles/Icons/PingWheel/
"""
import math, struct, os, sys
from PIL import Image, ImageDraw, ImageFilter

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
SS = 4

# ping type -> color, same as s_pingColors in AdventureScreen.cpp (5 = the plain ping)
COLORS = [0x60B0FF, 0xFFB040, 0xFF5A3A, 0xFF4090, 0x70E070, 0x48C8FF,
          0xFFD040, 0x60E0A0, 0x40D8FF, 0x5A7CFF, 0xFFE040, 0xFF3030, 0xC070FF, 0xFF9030, 0x80D050]


def rgb(c):
    return ((c >> 16) & 255, (c >> 8) & 255, c & 255)


def save_dds(img, path, mips=False):
    img = img.convert('RGBA')
    w, h = img.size
    levels = [img]
    if mips:
        while levels[-1].size[0] > 1 and levels[-1].size[1] > 1:
            lw, lh = levels[-1].size
            levels.append(levels[-1].resize((max(1, lw // 2), max(1, lh // 2)), Image.BOX))
    DDSD_CAPS, DDSD_HEIGHT, DDSD_WIDTH, DDSD_PITCH, DDSD_PIXELFORMAT, DDSD_MIPMAPCOUNT = 0x1, 0x2, 0x4, 0x8, 0x1000, 0x20000
    flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT
    caps = 0x1000
    if mips:
        flags |= DDSD_MIPMAPCOUNT
        caps |= 0x400000 | 0x8
    pf = struct.pack('<II4sIIIII', 32, 0x41, b'\0\0\0\0', 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    header = struct.pack('<4sIIIIIII', b'DDS ', 124, flags, h, w, w * 4, 0, len(levels) if mips else 0) + b'\0' * 44 + pf + struct.pack('<IIIII', caps, 0, 0, 0, 0)
    assert len(header) == 128
    with open(path, 'wb') as f:
        f.write(header)
        for lv in levels:
            r, g, b, a = lv.split()
            f.write(Image.merge('RGBA', (b, g, r, a)).tobytes())


def finish(img, name, size, mips=False):
    img = img.resize(size, Image.LANCZOS)
    img.save(os.path.join(OUT, name + '.png'))
    save_dds(img, os.path.join(OUT, name + '.dds'), mips)
    return img


# ---------------------------------------------------------------- ground ring (white; the game tints it)
R = 256
ring = Image.new('RGBA', (R, R), (255, 255, 255, 0))
px = ring.load()
c = (R - 1) / 2.0
for y in range(R):
    for x in range(R):
        d = math.hypot(x - c, y - c) / (R / 2.0)  # 0 center .. 1 edge of the quad
        a = 0.0
        a += 1.00 * math.exp(-((d - 0.86) / 0.035) ** 2)        # bright main ring
        a += 0.45 * math.exp(-((d - 0.86) / 0.10) ** 2)         # its glow
        if d < 0.86:
            a += 0.22 * (d / 0.86) ** 3                         # soft fill growing to the ring
        a += 0.35 * math.exp(-((d - 0.52) / 0.025) ** 2)        # thin inner ring
        a += 0.18 * math.exp(-(d / 0.12) ** 2)                  # small center dot glow
        if d > 0.97:
            a *= max(0.0, (1.0 - d) / 0.03)
        px[x, y] = (255, 255, 255, int(max(0.0, min(1.0, a)) * 255))
finish(ring, 'ping_ring', (R, R), mips=True)

# ---------------------------------------------------------------- beams
BW, BH = 32, 128
for i, col in enumerate(COLORS):
    r0, g0, b0 = rgb(col)
    beam = Image.new('RGBA', (BW, BH), (0, 0, 0, 0))
    p = beam.load()
    for y in range(BH):
        t = y / (BH - 1.0)                      # 0 top .. 1 ground
        v = 0.22 + 0.78 * t ** 1.5               # fades toward the top
        if t > 0.80:
            v *= max(0.0, (1.0 - t) / 0.20) ** 1.3  # and dissolves before the ground (the foot glow is there)
        for x in range(BW):
            dx = (x - (BW - 1) / 2.0) / (BW / 2.0)
            core = math.exp(-(dx / 0.16) ** 2)
            glow = math.exp(-(dx / 0.55) ** 2)
            a = min(1.0, (0.95 * core + 0.45 * glow) * v)
            w = core * 0.75                      # white-hot core
            p[x, y] = (int(r0 + (255 - r0) * w), int(g0 + (255 - g0) * w), int(b0 + (255 - b0) * w), int(a * 255))
    beam.save(os.path.join(OUT, 'ping_beam%d.png' % i))
    save_dds(beam, os.path.join(OUT, 'ping_beam%d.dds' % i))

# ---------------------------------------------------------------- foot glow at the ping point (flattened flare + light ring)
FW, FH = 128, 64
for i, col in enumerate(COLORS):
    r0, g0, b0 = rgb(col)
    foot = Image.new('RGBA', (FW, FH), (0, 0, 0, 0))
    p = foot.load()
    for y in range(FH):
        for x in range(FW):
            ex = (x - (FW - 1) / 2.0) / (FW / 2.0)
            ey = (y - (FH - 1) / 2.0) / (FH / 2.0)
            d = math.hypot(ex, ey)
            glow = math.exp(-(d / 0.42) ** 2)                      # soft colored glow
            core = math.exp(-(d / 0.14) ** 2)                      # white-hot center
            lring = 0.55 * math.exp(-((d - 0.62) / 0.07) ** 2)      # thin light ring
            streak = 0.35 * math.exp(-(ey / 0.06) ** 2) * math.exp(-(ex / 0.75) ** 2)  # horizontal lens streak
            a = min(1.0, 0.85 * glow + lring + streak + core)
            if d > 0.92:
                a *= max(0.0, (1.0 - d) / 0.08)
            w = min(1.0, core * 1.2 + streak * 0.6)
            p[x, y] = (int(r0 + (255 - r0) * w), int(g0 + (255 - g0) * w), int(b0 + (255 - b0) * w), int(a * 255))
    foot.save(os.path.join(OUT, 'ping_foot%d.png' % i))
    save_dds(foot, os.path.join(OUT, 'ping_foot%d.dds' % i))

# ---------------------------------------------------------------- plain ping icon: "!" badge
I = 64 * SS


def P(x, y):
    return (x * SS, y * SS)


ic = Image.new('RGBA', (I, I), (0, 0, 0, 0))
d = ImageDraw.Draw(ic)
r0, g0, b0 = rgb(COLORS[5])
d.ellipse([P(7, 7), P(57, 57)], fill=(14, 50, 80, 255))
d.ellipse([P(10, 10), P(54, 54)], fill=(r0, g0, b0, 255))
d.ellipse([P(13, 11), P(51, 35)], fill=(min(255, r0 + 70), min(255, g0 + 50), 255, 120))
d.rounded_rectangle([P(28.5, 17), P(35.5, 38)], radius=3 * SS, fill=(255, 255, 255, 255), outline=(10, 40, 70, 255), width=SS)
d.ellipse([P(27.5, 41), P(36.5, 50)], fill=(255, 255, 255, 255), outline=(10, 40, 70, 255), width=SS)
sh = Image.new('RGBA', ic.size, (0, 0, 0, 0))
sh.putalpha(ic.getchannel('A').point(lambda v: int(v * 0.7)))
sh = sh.filter(ImageFilter.GaussianBlur(2 * SS))
out = Image.new('RGBA', ic.size, (0, 0, 0, 0))
out.alpha_composite(sh, (int(1.5 * SS), int(2 * SS)))
finish(Image.alpha_composite(out, ic), 'ping_icon5', (64, 64))

# ---------------------------------------------------------------- preview: ring in perspective + beam + icon
pv = Image.new('RGBA', (520, 300), (70, 88, 60, 255))
for k, col in enumerate([COLORS[5], COLORS[1]]):
    ox = 130 + k * 260
    tint = Image.new('RGBA', (R, R), rgb(col) + (255,))
    tint.putalpha(ring.getchannel('A'))
    flat = tint.resize((200, 90), Image.LANCZOS)
    add = Image.new('RGBA', pv.size, (0, 0, 0, 0))
    add.alpha_composite(flat, (ox - 100, 230 - 45))
    pv = Image.alpha_composite(pv, add)
    b = Image.open(os.path.join(OUT, 'ping_beam%d.png' % (5 if k == 0 else 1))).resize((32, 150))
    pv.alpha_composite(b, (ox - 16, 230 - 150))
    ft = Image.open(os.path.join(OUT, 'ping_foot%d.png' % (5 if k == 0 else 1))).resize((84, 42))
    pv.alpha_composite(ft, (ox - 42, 230 - 21))
    icn = Image.open(os.path.join(OUT, 'ping_icon5.png' if k == 0 else 'ping_icon5.png'))
    pv.alpha_composite(icn.resize((48, 48)), (ox - 24, 230 - 150 - 40))
pv.save(os.path.join(OUT, '_ground_preview.png'))
print('done')
