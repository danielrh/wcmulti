#!/usr/bin/env python3
"""The flight's sound effects (src/sfx.c), synthesized: each sound of data/sounds.csv made by a recipe below.

    sfx.py [--out build/dos/SFX.DAT] [--wav DIR]

The user's brief (2026-10-28): synthetic blaster sounds for the different guns, waveforms that make them retro but
not too Game Boy: good retro.  So no bare square beeps: the sounds are made the way an analog synth patch is, then
sampled the way a Sound Blaster plays them.

  - oscillators band-limited (PolyBLEP saws and pulses, so a sweep doesn't alias into a whistle), FM pairs for the
    bells, white noise;
  - a resonant state-variable filter, its cutoff swept by an envelope (the "pew" is mostly the filter closing);
  - pitch envelopes, exponential; amplitude envelopes with a sharp attack; a little saturation (tanh) for body;
  - a short echo (one tap, 40-80 ms) for space, so a shot has a tail rather than a cut;
  - then 22,050 Hz, 8 bits, with a triangular dither under the last bit so the tails fade rather than buzz.

Each faction its own family: the Terrans' saws and kinetic hits, the Aera's FM bells (glassy, inharmonic), the
Rlaan's breathy formant noise and drones.

SFX.DAT: "RVSFX\\0\\0\\0", u16 version, u16 sounds, u16 rate, u16 0, then per sound u32 offset (from the file's
start), u32 samples; then the samples, signed 8-bit.
"""
import argparse
import csv
import math
import os
import struct
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
VERSION = 1
SR = 22050


def sounds():
    return list(csv.DictReader(open(os.path.join(ROOT, 'data', 'sounds.csv'))))


# --- the parts ---

def tl(dur):
    return np.arange(int(dur * SR)) / SR


def sweep(t, f0, f1, tau):
    """exponential glide from f0 to f1 with time constant tau (s)"""
    return f1 + (f0 - f1) * np.exp(-t / tau)


def phase(freq):
    return np.cumsum(freq / SR) % 1.0


def blep(p, dt):
    """PolyBLEP correction at a phase-wrap"""
    out = np.zeros_like(p)
    a = p < dt
    x = p[a] / dt[a]
    out[a] = x + x - x * x - 1
    b = p > 1 - dt
    x = (p[b] - 1) / dt[b]
    out[b] = x * x + x + x + 1
    return out


def saw(freq):
    p = phase(freq)
    dt = np.clip(freq / SR, 1e-6, 0.5)
    return 2 * p - 1 - blep(p, dt)


def pulse(freq, width=0.5):
    p = phase(freq)
    dt = np.clip(freq / SR, 1e-6, 0.5)
    w = np.broadcast_to(width, p.shape)
    s = np.where(p < w, 1.0, -1.0)
    s += blep(p, dt)
    s -= blep((p - w) % 1.0, dt)
    return s


def sine(freq):
    return np.sin(2 * math.pi * phase(freq))


def fm(carrier, ratio, index):
    """a two-operator FM pair: the modulator at carrier x ratio, its depth index (radians, may vary)"""
    pm = phase(carrier * ratio)
    pc = np.cumsum(carrier / SR)
    return np.sin(2 * math.pi * pc + index * np.sin(2 * math.pi * pm))


def noise(t, seed):
    return np.random.default_rng(seed).uniform(-1, 1, len(t))


def env(t, attack, tau, hold=0.0):
    a = np.clip(t / max(attack, 1e-4), 0, 1)
    d = np.where(t < attack + hold, 1.0, np.exp(-(t - attack - hold) / tau))
    return a * d


def svf(x, cutoff, q, mode='lp'):
    """a Chamberlin state-variable filter, its cutoff (Hz, per sample or fixed) and resonance q (0.5 .. 10)"""
    cutoff = np.broadcast_to(np.asarray(cutoff, dtype=float), x.shape)
    f = 2 * np.sin(math.pi * np.clip(cutoff, 20, SR / 6.5) / SR)
    damp = 1.0 / q
    low = band = 0.0
    out = np.empty_like(x)
    for i in range(len(x)):
        low += f[i] * band
        high = x[i] - low - damp * band
        band += f[i] * high
        out[i] = low if mode == 'lp' else band if mode == 'bp' else high
    return out


def echo(x, ms, gain, taps=2):
    y = np.concatenate([x, np.zeros(int(SR * ms / 1000 * taps))])
    d = int(SR * ms / 1000)
    for k in range(1, taps + 1):
        y[d * k:d * k + len(x)] += x * gain ** k
    return y


def crackle(t, rate, seed, decay):
    """sparse clicks, fewer as it decays"""
    rng = np.random.default_rng(seed)
    c = np.zeros(len(t))
    prob = rate / SR * np.exp(-t / decay)
    hits = rng.random(len(t)) < prob
    c[hits] = rng.uniform(-1, 1, hits.sum())
    return svf(c, 3500, 1.2, 'bp') * 3


def mix(*parts):
    n = max(len(p) for p in parts)
    out = np.zeros(n)
    for p in parts:
        out[:len(p)] += p
    return out


def drive(x, k):
    return np.tanh(k * x) / np.tanh(k)


# --- the recipes ---

def laser(pitch=1.0, dur=0.20):
    t = tl(dur)
    f = sweep(t, 1800 * pitch, 380 * pitch, 0.045)
    osc = 0.5 * saw(f) + 0.5 * saw(f * 1.012) + 0.35 * pulse(f * 0.5, 0.3)
    body = svf(osc, sweep(t, 7000, 900 * pitch, 0.05), 3.2) * env(t, 0.002, 0.065)
    click = svf(noise(t, 1), 5000, 0.8, 'hp') * env(t, 0.0005, 0.004)
    return echo(drive(body + 0.6 * click, 1.6), 55, 0.22)


def particle(pitch=1.0, dur=0.28, seed=2):
    t = tl(dur)
    f = sweep(t, 900 * pitch, 160 * pitch, 0.08)
    sq = pulse(f, 0.5) * sine(2700 * pitch + 0 * t)
    buzz = svf(noise(t, seed), sweep(t, 4200, 700, 0.07), 2.5, 'bp')
    x = (0.6 * sq + 0.8 * buzz) * env(t, 0.002, 0.09)
    return echo(drive(x, 2.2), 60, 0.2)


def ion():
    t = tl(0.38)
    f = sweep(t, 230, 105, 0.12) * (1 + 0.06 * np.sin(2 * math.pi * 28 * t))
    body = fm(f, 1.5, sweep(t, 4.0, 0.5, 0.1))
    sub = sine(sweep(t, 90, 55, 0.15))
    x = svf(body, sweep(t, 2600, 500, 0.12), 1.4) + 0.6 * sub
    return echo(drive(x * env(t, 0.008, 0.13), 1.8), 70, 0.2)


def disruptor():
    t = tl(0.26)
    a = saw(sweep(t, 1300, 320, 0.07))
    b = saw(sweep(t, 520, 1500, 0.09))
    x = svf(0.6 * a + 0.6 * b, sweep(t, 2400, 900, 0.1), 4.0, 'bp') * env(t, 0.002, 0.08)
    return echo(drive(x * 1.6, 1.5), 50, 0.25)


def massdriver():
    t = tl(0.32)
    click = np.zeros(len(t))
    click[0:3] = [1.0, -0.8, 0.4]
    chunk = svf(noise(t, 3), 1700, 1.6, 'bp') * env(t, 0.0005, 0.028) * 1.6
    thump = sine(sweep(t, 150, 52, 0.04)) * env(t, 0.001, 0.06)
    ring = sum(a * sine(fr + 0 * t) * env(t, 0.001, tau)
               for fr, a, tau in ((920, 0.25, 0.11), (2530, 0.16, 0.08), (4100, 0.1, 0.05)))
    return echo(drive(click + chunk + thump + ring, 1.4), 45, 0.2)


def photon(light):
    t = tl(0.16 if light else 0.24)
    f0, f1 = (2400, 1500) if light else (1500, 800)
    f = sweep(t, f0, f1, 0.04 if light else 0.06)
    bell = fm(f, 1.41, sweep(t, 3.0, 0.2, 0.05)) + 0.4 * fm(f * 1.5, 2.0, sweep(t, 2.0, 0.0, 0.04))
    x = bell * env(t, 0.001, 0.055 if light else 0.09)
    if not light:
        x += 0.5 * sine(f * 0.5) * env(t, 0.002, 0.08)
        x += 0.25 * svf(noise(t, 4), 3000, 1.0, 'bp') * env(t, 0.001, 0.03)
    x = svf(x, 250, 0.7, 'hp')
    return echo(x, 40 if light else 55, 0.28)


def penetrator():
    t = tl(0.42)
    f = sweep(t, 1100, 420, 0.07)
    bell = fm(f, 1.41, sweep(t, 4.0, 0.4, 0.08)) * env(t, 0.001, 0.1)
    chop = (np.sin(2 * math.pi * 60 * t) > 0) * 1.0
    tear = svf(noise(t, 5), sweep(t, 3000, 400, 0.12), 1.5) * chop * env(t, 0.003, 0.12)
    sub = sine(sweep(t, 80, 40, 0.1)) * env(t, 0.002, 0.14)
    return echo(drive(0.7 * bell + 0.7 * tear + 0.8 * sub, 1.7), 65, 0.22)


def ktek(pitch=1.0, dur=0.26):
    t = tl(dur)
    n = noise(t, 6)
    f1 = sweep(t, 900 * pitch, 300 * pitch, 0.08)
    f2 = sweep(t, 2500 * pitch, 1100 * pitch, 0.07)
    vowel = svf(n, f1, 6.0, 'bp') + 0.7 * svf(n, f2, 7.0, 'bp')
    pw = 0.3 + 0.2 * np.sin(2 * math.pi * 23 * t)
    under = svf(pulse(sweep(t, 420 * pitch, 140 * pitch, 0.09), pw), 1800, 1.0)
    x = (1.4 * vowel + 0.35 * under) * env(t, 0.005, 0.085)
    return echo(drive(x, 1.8), 50, 0.25)


def drone(base, dur, hard):
    t = tl(dur)
    a = saw(base + 0 * t) + saw(base * 1.023 + 0 * t) + (0.7 if hard else 0.5) * pulse(base * 2 + 0 * t, 0.35)
    cut = 1600 + 900 * np.sin(2 * math.pi * 7 * t) if not hard else 2600 + 1200 * np.sin(2 * math.pi * 11 * t)
    body = svf(a, cut, 2.5)
    cr = crackle(t, 900, 7, 1.0) * 0.5
    e = np.clip(t / 0.015, 0, 1) * np.clip((dur - t) / 0.15, 0, 1)
    return drive((0.35 * body + cr) * e, 1.5)


def crippler():
    t = tl(0.32)
    f = sweep(t, 620, 300, 0.12) * (1 + 0.28 * np.sin(2 * math.pi * 14 * t))
    x = svf(pulse(f, 0.4), sweep(t, 3000, 800, 0.1), 2.0) * env(t, 0.003, 0.11)
    return echo(drive(x, 1.4), 60, 0.25)


def thumper(scale=1.0, dur=0.5):
    t = tl(dur)
    boom = sine(sweep(t, 95 * scale, 32 * scale, 0.11)) * env(t, 0.001, 0.16)
    click = svf(noise(t, 8), 2000, 0.8) * env(t, 0.0005, 0.006)
    rumble = svf(noise(t, 9), 280, 1.0) * env(t, 0.01, 0.18) * 2.5
    return drive(boom + 0.5 * click + 0.6 * rumble, 3.0)


def gauss():
    t = tl(0.85)
    crack = laser(0.55, 0.3)
    low = thumper(0.8, 0.85)
    roar = svf(noise(t, 10), sweep(t, 1800, 150, 0.25), 1.0) * env(t, 0.01, 0.3) * 1.4
    return mix(0.7 * crack, low, roar)


def icepick():
    t = tl(0.24)
    tick = svf(noise(t, 11), 6000, 0.8, 'hp') * env(t, 0.0003, 0.003)
    zing = sine(sweep(t, 3200, 700, 0.05)) * env(t, 0.001, 0.06)
    ring = sine(1900 + 0 * t) * sine(170 + 0 * t) * env(t, 0.002, 0.08) * 0.5
    return echo(tick + 0.8 * zing + ring, 45, 0.25)


def launch(deep=False, dur=0.65):
    t = tl(dur)
    s = 0.6 if deep else 1.0
    cut = 400 * s + (3200 * s) * (1 - np.exp(-t / 0.05)) * np.exp(-t / 0.35)
    roar = svf(noise(t, 12), cut, 1.3) * env(t, 0.02, 0.22 / s)
    motor = svf(saw(70 * s + 6 * np.sin(2 * math.pi * 9 * t)), 400, 1.2) * env(t, 0.03, 0.25 / s) * 0.6
    x = roar * 1.6 + motor
    if deep:
        clunk = sine(sweep(t, 220, 90, 0.02)) * env(t, 0.0005, 0.03)
        x += 0.9 * clunk
    return drive(x, 1.6)


def rocket():
    return launch(False, 0.28) * np.clip((0.28 - tl(0.28)) / 0.1, 0, 1)


def mine():
    t = tl(0.3)
    clunk = sine(sweep(t, 300, 120, 0.02)) * env(t, 0.0005, 0.03) + svf(noise(t, 13), 900, 2.0, 'bp') * env(t, 0.0005, 0.02)
    beep = pulse(sweep(t, 1400, 900, 0.1), 0.5) * ((t > 0.08) & (t < 0.24)) * 0.25
    return svf(clunk + svf(beep, 2500, 1.0), 6000, 0.7)


def boom(size):
    dur = {'small': 0.9, 'ship': 1.5, 'capital': 3.0}[size]
    t = tl(dur)
    deep = {'small': 1.0, 'ship': 0.75, 'capital': 0.5}[size]
    crack = svf(noise(t, 14), 5000, 0.9) * env(t, 0.0005, 0.02) * 1.5
    roar = svf(noise(t, 15), sweep(t, 4500 * deep, 160 * deep, 0.18 / deep), 1.1) * env(t, 0.004, 0.35 / deep) * 1.6
    if size == 'ship':                 # the roar rolls
        roar *= 0.75 + 0.25 * np.sin(2 * math.pi * 3.5 * t)
    sub = sine(sweep(t, 90 * deep, 28, 0.2)) * env(t, 0.002, 0.3 / deep) * 1.2
    cr = crackle(t, 260, 16 + int(10 * deep), 0.3 / deep) * 0.6
    x = crack + roar + sub + cr
    if size == 'capital':              # bursts through it
        for k, at in enumerate((0.35, 0.8, 1.3, 1.75)):
            b = boom('small') * (0.7 - 0.1 * k)
            i = int(at * SR)
            n = min(len(b), len(x) - i)
            x[i:i + n] += b[:n]
    return drive(x, 2.2)


def hit_shield():
    t = tl(0.2)
    zap = pulse(sweep(t, 1300, 400, 0.03), 0.5) * sine(3100 + 0 * t) * env(t, 0.001, 0.05)
    fizz = svf(noise(t, 17), 6000, 0.8, 'hp') * env(t, 0.001, 0.07)
    hum = sine(sweep(t, 660, 600, 0.1)) * env(t, 0.005, 0.08) * 0.4
    return echo(drive(zap + 0.7 * fizz + hum, 1.6), 35, 0.2)


def hit_hull():
    t = tl(0.34)
    crunch = svf(noise(t, 18), 900, 1.8, 'bp') * env(t, 0.0005, 0.05) * 2.0
    thud = sine(sweep(t, 120, 60, 0.05)) * env(t, 0.001, 0.08)
    rattle = crackle(t, 600, 19, 0.08) * 0.7
    ring = sine(1450 + 0 * t) * env(t, 0.001, 0.07) * 0.2
    return drive(crunch + thud + rattle + ring, 2.0)


def hit_far():
    t = tl(0.16)
    pop = svf(noise(t, 20), sweep(t, 2500, 500, 0.03), 1.2) * env(t, 0.0008, 0.035) * 1.5
    thud = sine(sweep(t, 160, 80, 0.03)) * env(t, 0.001, 0.04)
    return drive(pop + thud, 1.5)


RECIPES = {
    'laser': lambda: laser(),
    'laser_t': lambda: laser(0.75, 0.26),
    'particle': lambda: particle(),
    'particle_t': lambda: particle(0.5, 0.36, 21),
    'ion': ion,
    'disruptor': disruptor,
    'massdriver': massdriver,
    'photon2': lambda: photon(False),
    'photon3': lambda: photon(True),
    'penetrator': penetrator,
    'ktek': lambda: ktek(),
    'kbeam': lambda: drone(110, 0.6, False),
    'crippler': crippler,
    'thumper': lambda: thumper(),
    'lbeam': lambda: drone(165, 0.7, True),
    'gauss': gauss,
    'icepick': icepick,
    'launch': lambda: launch(),
    'torpedo': lambda: launch(True, 0.9),
    'rocket': rocket,
    'mine': mine,
    'boom_small': lambda: boom('small'),
    'boom_ship': lambda: boom('ship'),
    'boom_capital': lambda: boom('capital'),
    'hit_shield': hit_shield,
    'hit_hull': hit_hull,
    'hit_far': hit_far,
}


def render(name, gain):
    x = RECIPES[name]()
    x = x - np.mean(x[:64]) * 0                   # (no offset to remove: the parts are centred)
    peak = np.max(np.abs(x)) or 1.0
    x = x / peak * gain
    # the tail: trimmed where it has fallen under the 8 bits' last step for good
    q = np.abs(x) > 1.5 / 127
    end = len(x) - np.argmax(q[::-1]) if q.any() else len(x)
    x = x[:min(len(x), end + 32)]
    fade = min(64, len(x))
    x[-fade:] *= np.linspace(1, 0, fade)
    return x


def to8(x, seed=0):
    rng = np.random.default_rng(seed)
    d = (rng.random(len(x)) - rng.random(len(x)))  # triangular dither, one step wide each way
    return np.clip(np.round(x * 127 + d), -128, 127).astype(np.int8)


def write_wav(path, s8):
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(1)
        w.setframerate(SR)
        w.writeframes((s8.astype(np.int16) + 128).astype(np.uint8).tobytes())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out')
    ap.add_argument('--wav')
    a = ap.parse_args()
    rows = sounds()
    for r in rows:
        if r['name'] not in RECIPES:
            raise SystemExit('sfx.py: no recipe for %s (data/sounds.csv)' % r['name'])
    data = [to8(render(r['name'], float(r['gain'])), i) for i, r in enumerate(rows)]
    if a.wav:
        os.makedirs(a.wav, exist_ok=True)
        for r, s in zip(rows, data):
            write_wav(os.path.join(a.wav, r['name'] + '.wav'), s)
    if a.out:
        head = 16 + 8 * len(rows)
        with open(a.out, 'wb') as f:
            f.write(b'RVSFX\0\0\0' + struct.pack('<HHHH', VERSION, len(rows), SR, 0))
            off = head
            for s in data:
                f.write(struct.pack('<II', off, len(s)))
                off += len(s)
            for s in data:
                f.write(s.tobytes())
        print('%s: %d sounds, %.1f s, %d KB at %d Hz, 8 bits' % (a.out, len(rows), sum(len(s) for s in data) / SR,
                                                               os.path.getsize(a.out) // 1024, SR))


if __name__ == '__main__':
    main()
