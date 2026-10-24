"""Wheel textures shared by the ping wheel (gen_pingwheel.py) and the chat wheel (../ChatWheel/gen_chatwheel.py).

wheel(out, prefix, size, r_out, r_in, slot_r) writes for every sector count N = 4..8:
  <prefix>_bg_<N>.dds        size x size, wheel center in the middle (DXT5)
  <prefix>_hl_<N>_<i>.dds    highlight of sector i, cropped to a power-of-two size, (item 0 at the top, clockwise), DXT5
and returns {N: [(x, y), ...]}: top-left corners of the highlight textures relative to the wheel center.
slot_r > 0 draws round icon slots at the sector centers (ping wheel).
"""
import math, os
from PIL import Image, ImageDraw, ImageFilter, ImageChops

SS = 4
COUNTS = range(4, 9)


def save_dds_dxt5(img, path):
    img.convert('RGBA').save(path, format='DDS', pixel_format='DXT5')


def radial(size, center, r0, r1, c0, c1):
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


def pow2(v):
    p = 16
    while p < v:
        p *= 2
    return p


def sector_angle(i, n):
    return -90 + i * 360.0 / n


def wheel(out, prefix, size, r_out, r_in, slot_r, preview=True):
    os.makedirs(out, exist_ok=True)
    S = size * SS
    c = S / 2.0
    k = size / 256.0  # line widths grow with the texture

    def circle(r):
        return [c - r * SS, c - r * SS, c + r * SS, c + r * SS]

    disc_src = radial(size, (size / 2, size / 2), r_in, r_out, (33, 74, 72, 214), (14, 40, 40, 238)).resize((S, S), Image.BICUBIC)
    grad_src = radial(size, (size / 2, size / 2), r_in, r_out, (110, 220, 205, 30), (140, 235, 220, 100)).resize((S, S), Image.BICUBIC)
    offsets = {}
    for n in COUNTS:
        bg = Image.new('RGBA', (S, S), (0, 0, 0, 0))
        sh = Image.new('RGBA', (S, S), (0, 0, 0, 0))
        ImageDraw.Draw(sh).ellipse(circle(r_out + 2 * k), fill=(0, 0, 0, 90))
        sh = sh.filter(ImageFilter.GaussianBlur(5 * k * SS))
        bg = Image.alpha_composite(bg, sh)
        disc = disc_src.copy()
        mask = Image.new('L', (S, S), 0)
        ImageDraw.Draw(mask).ellipse(circle(r_out), fill=255)
        disc.putalpha(ImageChops.multiply(disc.getchannel('A'), mask))
        bg = Image.alpha_composite(bg, disc)
        d = ImageDraw.Draw(bg)
        d.ellipse(circle(r_out), outline=(70, 56, 24, 255), width=int(3 * k * SS))
        d.ellipse(circle(r_out - 2.5 * k), outline=(168, 170, 127, 255), width=int(1.5 * k * SS))
        d.ellipse(circle(r_out - 4.5 * k), outline=(67, 67, 29, 220), width=max(SS, int(k * SS)))
        for i in range(n):
            a = math.radians(sector_angle(i, n) + 180.0 / n)
            steps = 50
            for s in range(steps):
                t0 = r_in + 8 * k + (r_out - r_in - 20 * k) * s / float(steps)
                t1 = r_in + 8 * k + (r_out - r_in - 20 * k) * (s + 1) / float(steps)
                alpha = int(115 * math.sin(math.pi * (s + 0.5) / steps))
                d.line([(c + math.cos(a) * t0 * SS, c + math.sin(a) * t0 * SS), (c + math.cos(a) * t1 * SS, c + math.sin(a) * t1 * SS)],
                       fill=(150, 190, 175, alpha), width=int(1.5 * k * SS))
        d.ellipse(circle(r_in), fill=(8, 34, 32, 240), outline=(168, 170, 127, 255), width=int(1.5 * k * SS))
        if slot_r > 0:
            r_slot = (r_in + r_out) / 2.0 + 2
            for i in range(n):
                a = math.radians(sector_angle(i, n))
                sx, sy = c + math.cos(a) * r_slot * SS, c + math.sin(a) * r_slot * SS
                d.ellipse([sx - slot_r * SS, sy - slot_r * SS, sx + slot_r * SS, sy + slot_r * SS],
                          fill=(14, 46, 44, 245), outline=(168, 170, 127, 255), width=int(1.5 * SS))
        bg = bg.resize((size, size), Image.LANCZOS)
        if preview:
            bg.save(os.path.join(out, '%s_bg_%d.png' % (prefix, n)))
        save_dds_dxt5(bg, os.path.join(out, '%s_bg_%d.dds' % (prefix, n)))

        offsets[n] = []
        for i in range(n):
            hl = Image.new('RGBA', (S, S), (0, 0, 0, 0))
            a0 = sector_angle(i, n) - 180.0 / n + 1.2
            a1 = sector_angle(i, n) + 180.0 / n - 1.2
            m = Image.new('L', (S, S), 0)
            ImageDraw.Draw(m).pieslice(circle(r_out - 3 * k), a0, a1, fill=255)
            ImageDraw.Draw(m).ellipse(circle(r_in + 3 * k), fill=0)
            g = grad_src.copy()
            g.putalpha(ImageChops.multiply(g.getchannel('A'), m))
            hl = Image.alpha_composite(hl, g)
            ImageDraw.Draw(hl).arc(circle(r_out - 3 * k), a0, a1, fill=(236, 232, 180, 235), width=int(3 * k * SS))
            hl = hl.resize((size, size), Image.LANCZOS)
            x0, y0, x1, y1 = hl.getchannel('A').getbbox()
            # the smallest power-of-two texture around the sector
            hw = pow2(x1 - x0)
            hh = pow2(y1 - y0)
            ox = min(max(int((x0 + x1) / 2 - hw / 2), 0), size - hw)
            oy = min(max(int((y0 + y1) / 2 - hh / 2), 0), size - hh)
            crop = hl.crop((ox, oy, ox + hw, oy + hh))
            save_dds_dxt5(crop, os.path.join(out, '%s_hl_%d_%d.dds' % (prefix, n, i)))
            offsets[n].append((ox - size // 2, oy - size // 2))
            if preview and i == 1:
                bg.alpha_composite(crop, (ox, oy))
                bg.save(os.path.join(out, '%s_preview_%d.png' % (prefix, n)))
    return offsets


def as_offsets(name, offsets):
    """ActionScript literal: name[n] = [x0, y0, x1, y1, ...]"""
    rows = []
    for n in COUNTS:
        rows.append('[' + ','.join('%d,%d' % o for o in offsets[n]) + ']')
    return '%s = [null,null,null,null,%s];' % (name, ','.join(rows))
