"""Voice icons for the in-battle voice on ally portraits (VoiceChat/MateVoiceIndicator.as).

The Flash UI engine rasterises vector graphics without proper anti-aliasing (circles become
coarse polygons, no strokes or polygon fills at all), so the icons are textures like the
stock UI icons: drawn as SVG at 256 px (librsvg via ImageMagick), downscaled to 64x64 and
written as uncompressed 32-bit DDS (same writer as pw_art/UI/SmartCast/gen_smartcast.py).

usage: gen_voice_icons.py <outdir>   ->  <name>.dds + <name>.png for every icon below
  button icons (transparent background): mic_on, mic_off, mic_none, spk_on, spk_muted, spk_off
  portrait badges (round dark background): b_mic_off, b_mic_none, b_spk_muted, b_headset_off
"""
import os, struct, subprocess, sys

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
N = 64
RENDER = 256

MIC = '#C9CDD4'      # light grey: the icons sit on a dark background
OFF = '#949BA4'      # no microphone / no connection
RED = '#ED4245'
WHITE = '#FFFFFF'
BG = '#1E1F22'

# 200x200 shapes after the reference icons
SLASH = (28, 178, 180, 26)


def mic(color):
    return ('<rect x="70" y="6" width="60" height="114" rx="30" fill="{c}"/>'
            '<path d="M32 98 A68 68 0 0 0 168 98" fill="none" stroke="{c}" stroke-width="16"/>'
            '<rect x="92" y="164" width="16" height="32" fill="{c}"/>').format(c=color)


def speaker(color, waves):
    s = '<path d="M22 72 H62 L114 28 V172 L62 128 H22 Z" fill="{c}"/>'.format(c=color)
    if waves:
        s += ('<path d="M138 74 A36 36 0 0 1 138 126" fill="none" stroke="{c}" stroke-width="14" stroke-linecap="round"/>'
              '<path d="M156 46 A68 68 0 0 1 156 154" fill="none" stroke="{c}" stroke-width="14" stroke-linecap="round"/>').format(c=color)
    return s


def cross(color):
    return ('<path d="M136 76 L184 124 M184 76 L136 124" fill="none" stroke="{c}" stroke-width="16" stroke-linecap="round"/>'
            .format(c=color))


def headset(color):
    return ('<path d="M30 122 A70 70 0 0 1 170 122" fill="none" stroke="{c}" stroke-width="16"/>'
            '<rect x="14" y="108" width="38" height="66" rx="14" fill="{c}"/>'
            '<rect x="148" y="108" width="38" height="66" rx="14" fill="{c}"/>').format(c=color)


def slashed(body, color):
    """The body with a transparent gap along the diagonal and the slash on top."""
    x1, y1, x2, y2 = SLASH
    return ('<defs><mask id="gap"><rect width="200" height="200" fill="white"/>'
            '<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="black" stroke-width="40"/></mask></defs>'
            '<g mask="url(#gap)">{body}</g>'
            '<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{c}" stroke-width="18" stroke-linecap="round"/>'
            ).format(x1=x1, y1=y1, x2=x2, y2=y2, body=body, c=color)


def badge(content):
    """Round dark plate with a thin light rim; the icon is scaled to 64% and centred."""
    return ('<circle cx="100" cy="100" r="97" fill="#FFFFFF" fill-opacity="0.3"/>'
            '<circle cx="100" cy="100" r="91" fill="{bg}"/>'
            '<g transform="translate(36 36) scale(0.64)">{c}</g>').format(bg=BG, c=content)


ICONS = {
    'mic_on': mic(MIC),
    'mic_off': slashed(mic(MIC), RED),
    'mic_none': slashed(mic(OFF), OFF),
    'spk_on': speaker(WHITE, True),
    'spk_muted': speaker(RED, False) + cross(RED),
    'spk_off': speaker(OFF, False),
    'b_mic_off': badge(slashed(mic(MIC), RED)),
    'b_mic_none': badge(slashed(mic(OFF), OFF)),
    'b_spk_muted': badge(speaker(RED, False) + cross(RED)),
    'b_headset_off': badge(slashed(headset(OFF), RED)),
}


def render(svg_body):
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" width="{r}" height="{r}" viewBox="0 0 200 200">{b}</svg>'
           .format(r=RENDER, b=svg_body))
    rgba = subprocess.run(['magick', '-background', 'none', 'svg:-', '-filter', 'Lanczos', '-resize', '%dx%d' % (N, N),
                           '-depth', '8', 'RGBA:-'], input=svg.encode('utf-8'), capture_output=True, check=True).stdout
    assert len(rgba) == N * N * 4, len(rgba)
    return rgba


def save_dds(rgba, path):
    flags = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000
    pf = struct.pack('<II4sIIIII', 32, 0x41, b'\0\0\0\0', 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    header = struct.pack('<4sIIIIIII', b'DDS ', 124, flags, N, N, N * 4, 0, 0) + b'\0' * 44 + pf + struct.pack('<IIIII', 0x1000, 0, 0, 0, 0)
    bgra = bytearray(rgba)
    bgra[0::4], bgra[2::4] = rgba[2::4], rgba[0::4]
    with open(path, 'wb') as f:
        f.write(header + bytes(bgra))


for name, body in ICONS.items():
    rgba = render(body)
    save_dds(rgba, os.path.join(OUT, name + '.dds'))
    subprocess.run(['magick', '-size', '%dx%d' % (N, N), '-depth', '8', 'RGBA:-', os.path.join(OUT, name + '.png')],
                   input=rgba, check=True)
print('ok', len(ICONS))
