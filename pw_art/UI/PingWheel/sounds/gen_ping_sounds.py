"""Soft ping sounds, synthesized (no samples). usage: gen_ping_sounds.py <outdir>
Each sound: mono 44.1 kHz 16-bit WAV, peak about -6 dBFS, soft attack, gentle reverb tail."""
import os, sys
import numpy as np
from scipy.io import wavfile
from scipy.signal import butter, sosfilt

SR = 44100
OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(7)


def t_axis(dur):
    return np.arange(int(dur * SR)) / SR


def env(t, attack, decay):
    """soft attack (raised cosine) + exponential decay"""
    a = np.clip(t / attack, 0, 1)
    a = 0.5 - 0.5 * np.cos(np.pi * a)
    return a * np.exp(-np.maximum(t - attack, 0) / decay)


def bell(freq, dur, decay, attack=0.008, bright=0.25, detune=1.5):
    """soft bell/chime: sine partials with slightly inharmonic overtones, upper partials fade faster"""
    t = t_axis(dur)
    out = np.zeros_like(t)
    partials = [(1.0, 1.0, 1.0), (2.0, bright, 0.55), (3.01, bright * 0.35, 0.35), (4.2, bright * 0.12, 0.22)]
    for ratio, amp, dk in partials:
        f = freq * ratio
        ph = rng.uniform(0, 2 * np.pi)
        # two slightly detuned voices = gentle shimmer
        out += amp * env(t, attack, decay * dk) * (np.sin(2 * np.pi * f * t + ph) + np.sin(2 * np.pi * (f + detune) * t + ph)) * 0.5
    return out


def marimba(freq, dur, decay=0.35):
    t = t_axis(dur)
    body = np.sin(2 * np.pi * freq * t) * env(t, 0.004, decay)
    ov = 0.18 * np.sin(2 * np.pi * freq * 3.93 * t) * env(t, 0.002, decay * 0.18)
    return body + ov


def lowpass(x, cutoff, order=2):
    sos = butter(order, cutoff, 'low', fs=SR, output='sos')
    return sosfilt(sos, x)


def highpass(x, cutoff, order=2):
    sos = butter(order, cutoff, 'high', fs=SR, output='sos')
    return sosfilt(sos, x)


def reverb(x, tail=0.9, wet=0.22, predelay=0.012):
    """small soft room: convolution with exponentially decaying filtered noise"""
    n = int(tail * SR)
    t = np.arange(n) / SR
    ir = rng.standard_normal(n) * np.exp(-t / (tail / 5.5))
    ir = lowpass(ir, 4500)
    ir[: int(predelay * SR)] = 0
    ir /= np.sqrt(np.sum(ir ** 2))
    y = np.zeros(len(x) + n)
    c = np.convolve(x, ir)
    y[: min(len(c), len(y))] = c[: len(y)]
    dry = np.concatenate([x, np.zeros(n)])
    return dry * (1 - wet) + y * wet * 3.0


def place(dst, src, at):
    i = int(at * SR)
    dst[i:i + len(src)] += src[: max(0, len(dst) - i)]


def finish(x, name, peak_db=-6.0):
    x = highpass(x, 120)
    # cut the silent end of the reverb tail (below -50 dB of the peak)
    e = np.abs(x) > np.max(np.abs(x)) * 10 ** (-50 / 20)
    x = x[: np.nonzero(e)[0][-1] + int(0.05 * SR)]
    # fade the very end
    f = int(0.03 * SR)
    x[-f:] *= np.linspace(1, 0, f)
    x = x / (np.max(np.abs(x)) + 1e-9) * 10 ** (peak_db / 20)
    wavfile.write(os.path.join(OUT, name + '.wav'), SR, (x * 32767).astype(np.int16))
    return x


def note(n):  # 'A4' -> Hz
    names = {'C': -9, 'C#': -8, 'D': -7, 'D#': -6, 'E': -5, 'F': -4, 'F#': -3, 'G': -2, 'G#': -1, 'A': 0, 'A#': 1, 'B': 2}
    return 440.0 * 2 ** ((names[n[:-1]] + 12 * (int(n[-1]) - 4)) / 12.0)


# ================================================================ "Иду сюда" (going here)
# friendly rising two-note chime, soft and warm
def going_a():
    """A: soft glass chime, rising fourth E5 -> A5"""
    x = np.zeros(int(1.3 * SR))
    place(x, bell(note('E5'), 1.0, 0.45, attack=0.010, bright=0.22), 0.0)
    place(x, bell(note('A5'), 1.1, 0.55, attack=0.010, bright=0.22), 0.13)
    x = lowpass(x, 5200)
    return finish(reverb(x, tail=0.9, wet=0.25), 'going_here_A_chime')


def going_b():
    """B: warm wooden marimba, rising major third + octave sparkle C5 -> E5 (+ quiet G5)"""
    x = np.zeros(int(1.0 * SR))
    place(x, marimba(note('C5'), 0.7, 0.30), 0.0)
    place(x, marimba(note('E5'), 0.8, 0.34), 0.11)
    place(x, 0.35 * marimba(note('G5'), 0.8, 0.30), 0.11)
    x = lowpass(x, 4000)
    return finish(reverb(x, tail=0.7, wet=0.18), 'going_here_B_marimba')


def going_c():
    """C: airy: a soft breath swell leading into a gentle rising chime D5 -> G5"""
    t = t_axis(0.35)
    breath = rng.standard_normal(len(t))
    breath = lowpass(highpass(breath, 900), 3500) * np.sin(np.pi * np.clip(t / 0.35, 0, 1)) ** 2 * 0.10
    x = np.zeros(int(1.4 * SR))
    place(x, breath, 0.0)
    place(x, bell(note('D5'), 1.0, 0.45, attack=0.020, bright=0.15, detune=2.0), 0.16)
    place(x, bell(note('G5'), 1.1, 0.60, attack=0.020, bright=0.15, detune=2.0), 0.28)
    x = lowpass(x, 4800)
    return finish(reverb(x, tail=1.1, wet=0.30), 'going_here_C_airy')


for fn in (going_a, going_b, going_c):
    y = fn()
    print(fn.__doc__.strip().split(':')[0], '%.2fs' % (len(y) / SR))


# ================================================================ Dota-like family: soft sine "boop" swells + fading echoes
def boop(freq, dur=0.26, sub=0.07, glide=0.0):
    """soft sine swell (raised-cosine envelope, no attack click) + a quiet sub two octaves lower;
    glide: relative pitch change over the note (e.g. +0.06 = slides 6% up)"""
    t = t_axis(dur)
    e = np.sin(np.pi * t / dur) ** 2
    f = freq * (1.0 + glide * t / dur)
    ph = 2 * np.pi * np.cumsum(f) / SR
    return e * (np.sin(ph) + sub * np.sin(ph / 4.0))


def echoes(x, interval=0.23, gain=0.46, count=6, darken=3200):
    """repeats of the whole phrase, each quieter and a little darker (like the Dota ping)"""
    out = np.zeros(len(x) + int(interval * SR * count) + 1)
    rep = x.copy()
    g = 1.0
    for k in range(count + 1):
        place(out, rep * g, k * interval)
        g *= gain
        rep = lowpass(rep, darken)
    return out


def ping_plain():
    """plain ping: one soft sonar boop with echoes"""
    x = boop(note('A5'), 0.26)
    return finish(reverb(echoes(x), tail=0.6, wet=0.12), 'D_ping_plain')


def going_dota():
    """going here: two boops stepping up a fourth (E5 -> A5), echoed"""
    x = np.zeros(int(0.45 * SR))
    place(x, boop(note('E5'), 0.20), 0.0)
    place(x, boop(note('A5'), 0.26), 0.15)
    return finish(reverb(echoes(x, interval=0.30, gain=0.40, count=4), tail=0.6, wet=0.12), 'D_going_here')


def going_dota_glide():
    """going here (variant): one boop that slides softly upward, echoed"""
    x = boop(note('F#5'), 0.32, glide=0.19)
    return finish(reverb(echoes(x, interval=0.34, gain=0.42, count=4), tail=0.6, wet=0.12), "D_going_here_glide")


for fn in (ping_plain, going_dota, going_dota_glide):
    y = fn()
    print(fn.__doc__.strip().split(':')[0], '%.2fs' % (len(y) / SR))


def attack_dota():
    """attack: three quick boops climbing a major chord (D5 F#5 A5), a bit more energy"""
    x = np.zeros(int(0.55 * SR))
    place(x, boop(note('D5'), 0.15, sub=0.10), 0.00)
    place(x, boop(note('F#5'), 0.15, sub=0.10), 0.10)
    place(x, boop(note('A5'), 0.24, sub=0.10), 0.20)
    return finish(reverb(echoes(x, interval=0.40, gain=0.36, count=3), tail=0.6, wet=0.12), 'D_attack')


def danger_dota():
    """danger: two boops stepping down (A5 -> E5), a calm warning"""
    x = np.zeros(int(0.45 * SR))
    place(x, boop(note('A5'), 0.18), 0.00)
    place(x, boop(note('E5'), 0.24), 0.15)
    return finish(reverb(echoes(x, interval=0.30, gain=0.42, count=4), tail=0.6, wet=0.12), 'D_danger')


def enemy_ward_dota():
    """enemy ward: one darker boop sliding down"""
    x = boop(note('E5'), 0.34, sub=0.14, glide=-0.16)
    x = lowpass(x, 2500)
    return finish(reverb(echoes(x, interval=0.34, gain=0.40, count=4, darken=2400), tail=0.6, wet=0.12), 'D_enemy_ward')


def need_ward_dota():
    """need ward: a short low boop, then a 'question' boop gliding up"""
    x = np.zeros(int(0.55 * SR))
    place(x, boop(note('C#5'), 0.16), 0.00)
    place(x, boop(note('E5'), 0.30, glide=0.12), 0.13)
    return finish(reverb(echoes(x, interval=0.40, gain=0.38, count=3), tail=0.6, wet=0.12), 'D_need_ward')


for fn in (attack_dota, danger_dota, enemy_ward_dota, need_ward_dota):
    y = fn()
    print(fn.__doc__.strip().split(':')[0], '%.2fs' % (len(y) / SR))
