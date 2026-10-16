"""Praise sound (Data/Audio/Ping/praise.wav): short reward chime, synthesized (pure Python, no numpy).
usage: gen_praise_sound.py <outdir>      mono 44.1 kHz 16-bit WAV, peak about -6 dBFS"""
import math, os, random, struct, sys, wave

SR = 44100
OUT = sys.argv[1]
if not os.path.isdir(OUT):
    os.makedirs(OUT)
rnd = random.Random(11)


def note(n):
    names = {'C': -9, 'C#': -8, 'D': -7, 'D#': -6, 'E': -5, 'F': -4, 'F#': -3, 'G': -2, 'G#': -1, 'A': 0, 'A#': 1, 'B': 2}
    return 440.0 * 2 ** ((names[n[:-1]] + 12 * (int(n[-1]) - 4)) / 12.0)


def env(t, attack, decay):
    a = min(t / attack, 1.0)
    a = 0.5 - 0.5 * math.cos(math.pi * a)
    return a * math.exp(-max(t - attack, 0.0) / decay)


def bell(freq, dur, decay, attack=0.004, bright=0.30, detune=1.8):
    """glassy bell: sine partials, the upper ones fade faster, two detuned voices"""
    parts = [(1.0, 1.0, 1.0), (2.0, bright, 0.5), (3.01, bright * 0.4, 0.3), (5.4, bright * 0.15, 0.15)]
    phases = [rnd.uniform(0, 2 * math.pi) for _ in parts]
    out = []
    for i in range(int(dur * SR)):
        t = i / float(SR)
        v = 0.0
        for (ratio, amp, dk), ph in zip(parts, phases):
            if amp == 0.0:
                continue
            f = freq * ratio
            v += amp * env(t, attack, decay * dk) * 0.5 * (math.sin(2 * math.pi * f * t + ph) + math.sin(2 * math.pi * (f + detune) * t + ph))
        out.append(v)
    return out


def place(dst, src, at, gain=1.0):
    i0 = int(at * SR)
    for i, v in enumerate(src):
        if i0 + i >= len(dst):
            break
        dst[i0 + i] += v * gain


def lowpass(x, cutoff):
    a = math.exp(-2 * math.pi * cutoff / SR)
    y, out = 0.0, []
    for v in x:
        y = (1 - a) * v + a * y
        out.append(y)
    return out


def highpass(x, cutoff):
    low = lowpass(x, cutoff)
    return [a - b for a, b in zip(x, low)]


def reverb(x, tail=0.9, wet=0.22):
    """small room: Schroeder (4 parallel combs + 2 allpasses)"""
    n = len(x) + int(tail * SR)
    src = x + [0.0] * (n - len(x))
    wetsig = [0.0] * n
    for ms, g in ((29.7, 0.80), (37.1, 0.78), (41.1, 0.76), (43.7, 0.74)):
        d = int(ms * SR / 1000)
        buf = [0.0] * n
        lp = 0.0
        for i in range(n):
            fb = buf[i - d] if i >= d else 0.0
            lp = 0.6 * fb + 0.4 * lp  # damping
            buf[i] = src[i] + g * lp
            wetsig[i] += buf[i] * 0.25
    for ms, g in ((5.0, 0.7), (1.7, 0.7)):
        d = int(ms * SR / 1000)
        out = [0.0] * n
        for i in range(n):
            xd = wetsig[i - d] if i >= d else 0.0
            yd = out[i - d] if i >= d else 0.0
            out[i] = -g * wetsig[i] + xd + g * yd
        wetsig = out
    return [s * (1 - wet) + w * wet for s, w in zip(src, wetsig)]


def praise():
    """quick rising major arpeggio G5 C6 E6 -> G6 ringing with a soft C6 under it and high glints"""
    x = [0.0] * int(1.2 * SR)
    step = 0.065
    for k, n in enumerate(['G5', 'C6', 'E6']):
        place(x, bell(note(n), 0.5, 0.16), k * step, 0.75)
    place(x, bell(note('G6'), 1.0, 0.42), 3 * step)
    place(x, bell(note('C6'), 1.0, 0.45, bright=0.15), 3 * step, 0.35)
    for _ in range(12):
        place(x, bell(rnd.uniform(3200, 5200), 0.18, 0.05, attack=0.002, bright=0.0), 3 * step + rnd.uniform(0.0, 0.6), rnd.uniform(0.04, 0.09))
    x = lowpass(x, 7000)
    x = reverb(x)
    x = highpass(x, 150)
    peak = max(abs(v) for v in x)
    last = max(i for i, v in enumerate(x) if abs(v) > peak * 10 ** (-50 / 20.0))
    x = x[: last + int(0.05 * SR)]
    fade = int(0.03 * SR)
    for i in range(fade):
        x[len(x) - fade + i] *= 1.0 - i / float(fade)
    k = 10 ** (-6.0 / 20) / peak
    w = wave.open(os.path.join(OUT, 'praise.wav'), 'wb')
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(SR)
    w.writeframes(b''.join(struct.pack('<h', int(max(-1.0, min(1.0, v * k)) * 32767)) for v in x))
    w.close()
    return x


y = praise()
print('praise %.2fs' % (len(y) / float(SR)))
