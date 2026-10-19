"""Icons of the replay panel (ReplayControls/ReplayControlsPanel.as in the combat main.swf).

The Flash UI engine draws vector shapes without proper anti-aliasing, so the button icons are
textures like the voice icons (pw_art/UI/Voice/gen_voice_icons.py): SVG rendered at 256 px
(librsvg via ImageMagick), downscaled to 64x64 and written as uncompressed 32-bit DDS.

usage: gen_replay_icons.py <outdir>   ->  <name>.dds + <name>.png for every icon below
  (game: pw/branches/r1117/Data/UI/Styles/Icons/Replay)
  back, forward - 10 s seek; play, pause; slower, faster - speed
"""
import os, struct, subprocess, sys

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
N = 64
RENDER = 256

FG = '#E8E2CF'   # light warm grey on the dark panel


def tri(x1, x2, color):
    """A right-pointing triangle between x1 and x2 (x2 < x1 points left)."""
    return '<path d="M{a} 40 L{b} 100 L{a} 160 Z" fill="{c}"/>'.format(a=x1, b=x2, c=color)


ICONS = {
    'back': tri(96, 26, FG) + tri(170, 100, FG),
    'forward': tri(30, 100, FG) + tri(104, 174, FG),
    'play': tri(56, 160, FG),
    'pause': '<rect x="50" y="40" width="36" height="120" rx="6" fill="{c}"/><rect x="114" y="40" width="36" height="120" rx="6" fill="{c}"/>'.format(c=FG),
    'slower': '<rect x="36" y="88" width="128" height="24" rx="6" fill="{c}"/>'.format(c=FG),
    'faster': ('<rect x="36" y="88" width="128" height="24" rx="6" fill="{c}"/>'
               '<rect x="88" y="36" width="24" height="128" rx="6" fill="{c}"/>').format(c=FG),
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
