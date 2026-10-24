"""Ping mute button of the statistics window (Tab): the chat mute button of main.fla
(optionsAsset/muteCheckbox_cmp, bitmaps chat_buttons_*.png exported to src/) with "!" instead of the speech bubble.

  python gen_ping_mute.py   ->  ping_mute_<state>.png here and
                                Data/UI/Styles/Icons/PingMute/ping_mute_<state>.dds (32x32, uncompressed, picture 31x31 at 0,0)
States: default, over, down, disable and the same with _mute (the red crossed ring of the chat mute is kept).
"""
import os
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DDS = os.path.join(HERE, '..', '..', '..', 'pw', 'branches', 'r1117', 'Data', 'UI', 'Styles', 'Icons', 'PingMute')
STATES = ['default', 'over', 'down', 'disable']
CX, CY = 15.0, 15.5      # center of the button
R_ICON = 11.5            # the icon lies inside this circle


def lum(p):
    return (p[0] + p[1] + p[2]) / 3.0


def icon_mask(img):
    w, h = img.size
    mask = [[False] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            if (x - CX) ** 2 + (y - CY) ** 2 <= R_ICON ** 2 and lum(img.getpixel((x, y))) > 85:
                mask[y][x] = True
    # one pixel more around: the anti-aliased edge of the bubble
    grown = [row[:] for row in mask]
    for y in range(h):
        for x in range(w):
            if mask[y][x]:
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        xx, yy = x + dx, y + dy
                        if 0 <= xx < w and 0 <= yy < h and (xx - CX) ** 2 + (yy - CY) ** 2 <= (R_ICON + 0.5) ** 2:
                            grown[yy][xx] = True
    return mask, grown


def icon_color(img, mask):
    # the flat inner color of the bubble (its brightest pixels)
    pixels = [img.getpixel((x, y)) for y in range(img.size[1]) for x in range(img.size[0]) if mask[y][x]]
    pixels.sort(key=lum)
    top = pixels[len(pixels) // 2:]
    return tuple(int(sum(p[i] for p in top) / len(top)) for i in range(3))


def inpaint(img, mask):
    """the background under the bubble: diffusion from the pixels around it"""
    w, h = img.size
    data = [[list(img.getpixel((x, y))) for x in range(w)] for y in range(h)]
    holes = [(x, y) for y in range(h) for x in range(w) if mask[y][x]]
    for x, y in holes:
        data[y][x] = [0, 0, 0, 255]
    known = [[not mask[y][x] for x in range(w)] for y in range(h)]
    for _ in range(400):
        new = {}
        for x, y in holes:
            acc = [0.0, 0.0, 0.0]
            n = 0
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                xx, yy = x + dx, y + dy
                if 0 <= xx < w and 0 <= yy < h and (known[yy][xx] or mask[yy][xx]):
                    if not known[yy][xx] and _ == 0:
                        continue
                    for i in range(3):
                        acc[i] += data[yy][xx][i]
                    n += 1
            if n:
                new[(x, y)] = [acc[i] / n for i in range(3)]
        for (x, y), c in new.items():
            data[y][x] = [c[0], c[1], c[2], 255]
            known[y][x] = True
    out = img.copy()
    for x, y in holes:
        out.putpixel((x, y), tuple(int(round(v)) for v in data[y][x][:3]) + (255,))
    return out


def draw_exclamation(img, color):
    """"!" in the middle of the button, anti-aliased (drawn 8x bigger and scaled down)"""
    s = 8
    w, h = img.size
    layer = Image.new('L', (w * s, h * s), 0)
    d = ImageDraw.Draw(layer)
    bar_w = 4.2
    top, bottom = 6.8, 18.0
    d.rounded_rectangle([(CX - bar_w / 2) * s, top * s, (CX + bar_w / 2) * s, bottom * s], radius=bar_w / 2 * s, fill=255)
    dot = 4.4
    dy = 21.6
    d.ellipse([(CX - dot / 2) * s, (dy - dot / 2) * s, (CX + dot / 2) * s, (dy + dot / 2) * s], fill=255)
    alpha = layer.resize((w, h), Image.LANCZOS)
    # thin dark outline like the bubble has
    shadow = alpha.resize((w, h))
    out = img.copy()
    dark = Image.new('RGBA', (w, h), (10, 18, 16, 255))
    out.paste(dark, (1, 1), shadow.point(lambda v: v * 0.45))
    out.paste(Image.new('RGBA', (w, h), color + (255,)), (0, 0), alpha)
    return out


def crossed_overlay(normal, muted):
    """pixels of the red crossed ring of the chat mute (differ from the normal button and are not the bubble)"""
    w, h = normal.size
    mask = Image.new('L', (w, h), 0)
    for y in range(h):
        for x in range(w):
            a = normal.getpixel((x, y))
            b = muted.getpixel((x, y))
            diff = sum(abs(a[i] - b[i]) for i in range(3)) / 3.0
            bubble = b[0] > 120 and b[1] > 110 and abs(b[0] - b[1]) < 50 and b[2] < b[1] + 10 and (x - CX) ** 2 + (y - CY) ** 2 < 10.5 ** 2
            gray_bubble = abs(b[0] - b[1]) < 12 and abs(b[1] - b[2]) < 12 and b[0] > 130 and (x - CX) ** 2 + (y - CY) ** 2 < 10.5 ** 2
            if diff > 18 and not bubble and not gray_bubble:
                mask.putpixel((x, y), 255)
            elif abs(a[3] - b[3]) > 20:
                mask.putpixel((x, y), 255)
    return mask


def main():
    if not os.path.isdir(OUT_DDS):
        os.makedirs(OUT_DDS)
    for state in STATES:
        normal = Image.open(os.path.join(HERE, 'src', 'chat_buttons_%s.png' % state)).convert('RGBA')
        muted = Image.open(os.path.join(HERE, 'src', 'chat_buttons_%s_mute.png' % state)).convert('RGBA')
        core, grown = icon_mask(normal)
        color = icon_color(normal, core)
        clean = inpaint(normal, grown)
        result = draw_exclamation(clean, color)
        result_mute = result.copy()
        result_mute.paste(muted, (0, 0), crossed_overlay(normal, muted))
        for name, img in (('ping_mute_%s' % state, result), ('ping_mute_%s_mute' % state, result_mute)):
            img.save(os.path.join(HERE, name + '.png'))
            canvas = Image.new('RGBA', (32, 32), (0, 0, 0, 0))
            canvas.paste(img, (0, 0))
            canvas.save(os.path.join(OUT_DDS, name + '.dds'), format='DDS')


if __name__ == '__main__':
    main()
