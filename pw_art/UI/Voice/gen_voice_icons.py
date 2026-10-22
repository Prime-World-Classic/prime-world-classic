"""Textures for the in-battle voice on ally portraits (VoiceChat/MateVoiceIndicator.as).

The look follows the launcher lobby (pw-launcher main.css .mm-voice-btn and the speaking glow of
.mm-lobby-header-team-player.mm-voice): a round button in the top right corner of the portrait
with a gold rim and a teal gradient, the icon on top of it, and a soft green glow around the
portrait while the player speaks.

The Flash UI engine rasterises vector graphics without proper anti-aliasing (circles become
coarse polygons, no strokes or polygon fills at all), so everything is a texture like the stock
UI icons: SVG rendered at 256 px (librsvg via ImageMagick), downscaled to 64x64 and written as
uncompressed 32-bit DDS (same writer as pw_art/UI/SmartCast/gen_smartcast.py).

usage: gen_voice_icons.py <png dir> <dds dir>
  plates (64x64, the button itself):  plate_gold, plate_red (the player muted me)
  icons (64x64, laid over the plate at the same position and scale):
    v_mic, v_mic_off, v_mic_none (own microphone), v_spk_on, v_spk_off (ally sound),
    v_peer_mic_off, v_peer_mic_none (the ally's microphone), v_headset_off (not in voice)
  glow (128x128): speaking glow, the portrait circle is PLATE-independent, see GLOW_* below
  vol_010 .. vol_100 (64x64): ally volume arc around the button, see ARC_* below
  _preview.png next to the png dir: all states on the launcher background
"""
import math, os, struct, subprocess, sys

PNG_OUT, DDS_OUT = sys.argv[1], sys.argv[2]
os.makedirs(PNG_OUT, exist_ok=True)
os.makedirs(DDS_OUT, exist_ok=True)
N = 64
RENDER = 256

# launcher colours (main.css .mm-voice-btn)
GOLD = '#D0B776'
RED_RIM = '#FF3B30'
TEAL_LIGHT = '#1D6F86'
TEAL_DARK = '#08394A'
ICON = '#8FE3C0'
MUTED = '#E0675C'
GREY = '#949BA4'
GLOW_RGB = (102, 226, 170)

# The plate in the 200x200 box: radius 84 (MateVoiceIndicator.as PLATE_FRACTION = 168 / 200),
# the rest is room for the shadow and the red glow. Rim = 13% of the radius, like 2.2cqw of 34cqw.
PLATE_R = 84
INNER_R = 73
# icon = 56% of the plate diameter (font-size 19cqw of a 34cqw button), launcher 24x24 icons
ICON_SIZE = 94


def plate(rim, glow):
    s = ('<defs>'
         '<radialGradient id="teal" cx="0.35" cy="0.3" r="0.7">'
         '<stop offset="0" stop-color="{tl}"/><stop offset="1" stop-color="{td}"/></radialGradient>'
         '<filter id="blur" x="-30%" y="-30%" width="160%" height="160%"><feGaussianBlur stdDeviation="5"/></filter>'
         '</defs>').format(tl=TEAL_LIGHT, td=TEAL_DARK)
    if glow:
        s += '<circle cx="100" cy="100" r="94" fill="{c}" fill-opacity="0.75" filter="url(#blur)"/>'.format(c=rim)
    else:
        s += '<circle cx="100" cy="105" r="86" fill="#000" fill-opacity="0.55" filter="url(#blur)"/>'
    s += ('<circle cx="100" cy="100" r="{pr}" fill="{rim}"/>'
          '<circle cx="100" cy="100" r="{ir}" fill="url(#teal)"/>').format(pr=PLATE_R, ir=INNER_R, rim=rim)
    return s


# launcher content/icons/ui/*.svg (viewBox 24x24), colour substituted
MIC = ('<path d="M12 2.5a3.2 3.2 0 0 0-3.2 3.2v6.1a3.2 3.2 0 0 0 6.4 0V5.7A3.2 3.2 0 0 0 12 2.5z" fill="{c}"/>'
       '<path d="M5.8 11.2a6.2 6.2 0 0 0 12.4 0M12 17.6v3.4M8.6 21h6.8" fill="none" stroke="{c}" stroke-width="2" stroke-linecap="round"/>')
MIC_SLASH = '<path d="M4 3.5l16 17" fill="none" stroke="{c}" stroke-width="2.4" stroke-linecap="round"/>'
SPEAKER = '<path d="M3 9h4l5-4.5v15L7 15H3z" fill="{c}"/>'
WAVES = '<path d="M15.5 8.5a5 5 0 0 1 0 7M18.5 5.5a9 9 0 0 1 0 13" fill="none" stroke="{c}" stroke-width="2" stroke-linecap="round"/>'
CROSS = '<path d="M15.5 9l6 6M21.5 9l-6 6" fill="none" stroke="{c}" stroke-width="2.2" stroke-linecap="round"/>'
HEADSET = ('<path d="M4.5 14v-2a7.5 7.5 0 0 1 15 0v2" fill="none" stroke="{c}" stroke-width="2" stroke-linecap="round"/>'
           '<rect x="3" y="13" width="4.2" height="7" rx="1.6" fill="{c}"/><rect x="16.8" y="13" width="4.2" height="7" rx="1.6" fill="{c}"/>')


def icon(parts):
    k = ICON_SIZE / 24.0
    o = 100 - ICON_SIZE / 2.0
    return '<g transform="translate({o} {o}) scale({k})">{p}</g>'.format(o=o, k=k, p=''.join(p for p in parts))


def tint(template, color):
    return template.format(c=color)


TEXTURES = {
    'plate_gold': plate(GOLD, False),
    'plate_red': plate(RED_RIM, True),
    'v_mic': icon([tint(MIC, ICON)]),
    'v_mic_off': icon([tint(MIC, MUTED), tint(MIC_SLASH, MUTED)]),
    'v_mic_none': icon([tint(MIC, GREY), tint(MIC_SLASH, GREY)]),
    'v_spk_on': icon([tint(SPEAKER, ICON), tint(WAVES, ICON)]),
    'v_spk_off': icon([tint(SPEAKER, MUTED), tint(CROSS, MUTED)]),
    'v_peer_mic_off': icon([tint(MIC, GREY), tint(MIC_SLASH, MUTED)]),
    'v_peer_mic_none': icon([tint(MIC, GREY), tint(MIC_SLASH, GREY)]),
    'v_headset_off': icon([tint(HEADSET, GREY), tint(MIC_SLASH, MUTED)]),
}


def render(svg_body):
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" width="{r}" height="{r}" viewBox="0 0 200 200">{b}</svg>'
           .format(r=RENDER, b=svg_body))
    rgba = subprocess.run(['magick', '-background', 'none', 'svg:-', '-filter', 'Lanczos', '-resize', '%dx%d' % (N, N),
                           '-depth', '8', 'RGBA:-'], input=svg.encode('utf-8'), capture_output=True, check=True).stdout
    assert len(rgba) == N * N * 4, len(rgba)
    return rgba


# Speaking glow, like the launcher box-shadow at full voice level: a solid ring 10% of the
# portrait radius R wide and a blur up to ~0.8 R outside. The texture radius is GLOW_EXTENT * R
# (MateVoiceIndicator.as GLOW_EXTENT); inside the portrait it is transparent.
GLOW_N = 128
GLOW_EXTENT = 1.9


def glow_alpha(r):
    if r < 0.95:
        return 0.0
    ring = 0.75 * min(1.0, (r - 0.95) / 0.05) if r < 1.1 else 0.0
    blur = 0.85 * math.exp(-((max(0.0, r - 1.1)) / 0.32) ** 2) * min(1.0, max(0.0, (r - 0.95) / 0.05))
    return max(ring, blur)


def glow():
    out = bytearray()
    sub = 4
    half = GLOW_N / 2.0
    for y in range(GLOW_N):
        for x in range(GLOW_N):
            a = 0.0
            for sy in range(sub):
                for sx in range(sub):
                    dx = (x + (sx + 0.5) / sub - half) / half * GLOW_EXTENT
                    dy = (y + (sy + 0.5) / sub - half) / half * GLOW_EXTENT
                    a += glow_alpha(math.hypot(dx, dy))
            a /= sub * sub
            out += bytes(GLOW_RGB) + bytes([int(round(255 * min(1.0, a)))])
    return bytes(out)


def save_dds(rgba, size, path):
    flags = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000
    pf = struct.pack('<II4sIIIII', 32, 0x41, b'\0\0\0\0', 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    header = struct.pack('<4sIIIIIII', b'DDS ', 124, flags, size, size, size * 4, 0, 0) + b'\0' * 44 + pf + struct.pack('<IIIII', 0x1000, 0, 0, 0, 0)
    bgra = bytearray(rgba)
    bgra[0::4], bgra[2::4] = rgba[2::4], rgba[0::4]
    with open(path, 'wb') as f:
        f.write(header + bytes(bgra))


def save_png(rgba, size, path):
    subprocess.run(['magick', '-size', '%dx%d' % (size, size), '-depth', '8', 'RGBA:-', path], input=rgba, check=True)


for name, body in TEXTURES.items():
    rgba = render(body)
    save_dds(rgba, N, os.path.join(DDS_OUT, name + '.dds'))
    save_png(rgba, N, os.path.join(PNG_OUT, name + '.png'))

g = glow()
save_dds(g, GLOW_N, os.path.join(DDS_OUT, 'glow.dds'))
save_png(g, GLOW_N, os.path.join(PNG_OUT, 'glow.png'))


# Ally volume arc around the button (launcher .mm-voice-btn--ring::before): a ring 4cqw wide
# 2.2cqw outside a 34cqw button, gold clockwise from the top up to volume / 100 of the circle,
# the rest dark. The volume is 10..100% in steps of 5 -> vol_010 .. vol_100. In the texture the
# outer radius (23.2cqw of a 17cqw button radius) is ARC_OUTER px of N / 2
# (MateVoiceIndicator.as ARC_FRACTION).
ARC_OUTER = 31.0
ARC_INNER = ARC_OUTER * 19.2 / 23.2
ARC_ON = (0xF0, 0xD2, 0x7A, 255)
ARC_OFF = (4, 22, 28, int(round(255 * 0.85)))


def arc(fraction):
    out = bytearray()
    sub = 4
    half = N / 2.0
    for y in range(N):
        for x in range(N):
            on = off = 0
            for sy in range(sub):
                for sx in range(sub):
                    dx = x + (sx + 0.5) / sub - half
                    dy = y + (sy + 0.5) / sub - half
                    r = math.hypot(dx, dy)
                    if r < ARC_INNER or r > ARC_OUTER:
                        continue
                    # clockwise from the top, 0..1
                    angle = (math.atan2(dx, -dy) / (2 * math.pi)) % 1.0
                    if angle < fraction:
                        on += 1
                    else:
                        off += 1
            total = sub * sub
            a_on = on / total * ARC_ON[3] / 255.0
            a_off = off / total * ARC_OFF[3] / 255.0
            a = a_on + a_off
            if a <= 0:
                out += b'\0\0\0\0'
                continue
            rgb = [int(round((ARC_ON[i] * a_on + ARC_OFF[i] * a_off) / a)) for i in range(3)]
            out += bytes(rgb) + bytes([int(round(255 * min(1.0, a)))])
    return bytes(out)


for percent in range(10, 101, 5):
    name = 'vol_%03d' % percent
    rgba = arc(percent / 100.0)
    save_dds(rgba, N, os.path.join(DDS_OUT, name + '.dds'))
    save_png(rgba, N, os.path.join(PNG_OUT, name + '.png'))


# Preview: every state as the indicator lays it out, on the lobby header colour.
def png(name):
    return os.path.join(PNG_OUT, name + '.png')


STATES = [('plate_gold', 'v_mic', None), ('plate_gold', 'v_mic_off', None), ('plate_gold', 'v_mic_none', None),
          ('plate_gold', 'v_spk_on', 'vol_050'), ('plate_gold', 'v_spk_off', 'vol_050'), ('plate_red', 'v_spk_on', 'vol_100'),
          ('plate_gold', 'v_peer_mic_off', 'vol_025'), ('plate_gold', 'v_peer_mic_none', 'vol_075'), ('plate_gold', 'v_headset_off', None)]
STEP = 88
# arc size for a plate drawn at N px: plate diameter N * 0.84, arc = plate * 23.2 / 17 * (N / 2) / ARC_OUTER
ARC_SIZE = int(round(N * 0.84 * 23.2 / 17 * (N / 2) / ARC_OUTER))
cmd = ['magick', '-size', '%dx%d' % (STEP * len(STATES) + 8 + GLOW_N, GLOW_N + 8), 'xc:#03303D']
for i, (p, ic, vol) in enumerate(STATES):
    x, y = 8 + (ARC_SIZE - N) // 2 + STEP * i, 4 + (GLOW_N - N) // 2
    if vol:
        cmd += ['(', png(vol), '-resize', '%dx%d' % (ARC_SIZE, ARC_SIZE), ')', '-geometry',
                '+%d+%d' % (x - (ARC_SIZE - N) // 2, y - (ARC_SIZE - N) // 2), '-composite']
    for layer in (p, ic):
        cmd += [png(layer), '-geometry', '+%d+%d' % (x, y), '-composite']
cmd += ['-fill', '#5A4632', '-draw', 'circle %d,%d %d,%d' % (
    8 + STEP * len(STATES) + GLOW_N // 2, 4 + GLOW_N // 2, 8 + STEP * len(STATES) + GLOW_N // 2 + int(GLOW_N / 2 / GLOW_EXTENT), 4 + GLOW_N // 2)]
cmd += [png('glow'), '-geometry', '+%d+4' % (8 + STEP * len(STATES)), '-composite']
cmd += [os.path.join(os.path.dirname(os.path.abspath(PNG_OUT)), '_preview.png')]
subprocess.run(cmd, check=True)
print('ok', len(TEXTURES) + 1)
