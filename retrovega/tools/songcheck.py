#!/usr/bin/env python3
"""The flight songs measured against the feel the user asked for (CC0, ours).

    songcheck.py [build/dos/CRUISE1.MID ...]     (default: the cruise songs and the way home)

The user (2026-10-24): the cruise songs "sound like elevator music--not memorable"; the two cues they sent for
the feeling of excitement and wonder measured fast, lean and restless where ours were slow, thick and still.
This puts numbers on that, a band each from those two cues, so a draft that drifts back is caught before it is
played to them:

    notes/s     notes started a second (drums apart)                       10 or more
    motor       the share of fifth-of-a-second windows that start a note    0.90 or more
    voices      notes sounding at once, on average (held, not released)    2.5 to 5
    off-key     the share of note time outside the best-fitting major scale 0.08 or more
    fast/s      notes within a tenth of a second of the last on their channel (runs, sweeps)  0.5 or more
    drums/s     drum hits a second, and no steady beat                     0.3 to 1.5
    same        the most bars in a row with the same parts and key          8 at most

The way home (HOMEWARD, HOMELOSS) is held to gentler bands: no hits or runs asked of it, 8 notes a second, up
to 5.5 voices.  A song outside a band says so; the exit status is the number of songs that did.
"""
import os
import struct
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
MAJOR = (0, 2, 4, 5, 7, 9, 11)
BANDS = {'notes/s': (10, None), 'motor': (0.90, None), 'voices': (2.5, 5), 'off-key': (0.08, None),
         'fast/s': (0.5, None), 'drums/s': (0.3, 1.5), 'same': (None, 8)}
HOME_BANDS = dict(BANDS, **{'notes/s': (8, None), 'voices': (2.5, 5.5), 'fast/s': (None, None),
                            'drums/s': (None, 1.5)})


def varlen(d, i):
    v = 0
    while True:
        b = d[i]
        i += 1
        v = (v << 7) | (b & 0x7F)
        if not b & 0x80:
            return v, i


def read(path):
    """(notes [(channel, start s, end s, pitch)], seconds, bar length in s, loop marker in s or None)"""
    d = open(path, 'rb').read()
    ntr, ppq = struct.unpack('>HH', d[10:14])
    i, evs = 14, []
    for _ in range(ntr):
        n = struct.unpack('>L', d[i + 4:i + 8])[0]
        j, end, t, run = i + 8, i + 8 + n, 0, 0
        while j < end:
            dt, j = varlen(d, j)
            t += dt
            st = d[j]
            if st == 0xFF:
                kind, (ln, j) = d[j + 1], varlen(d, j + 2)
                evs.append((t, 'meta', kind, d[j:j + ln]))
                j += ln
                continue
            if st & 0x80:
                run, j = st, j + 1
            k = 1 if run & 0xF0 in (0xC0, 0xD0) else 2
            evs.append((t, 'ch', run, d[j:j + k]))
            j += k
        i = end
    evs.sort(key=lambda e: e[0])
    tempo, beats = 500000, 4
    for t, kind, a, b in evs:
        if kind == 'meta' and a == 0x51 and t == 0:
            tempo = int.from_bytes(b, 'big')
        if kind == 'meta' and a == 0x58:
            beats = b[0]
    # seconds along the tempo map
    marks, last_t, last_s, us = [], 0, 0.0, tempo
    for t, kind, a, b in evs:
        if kind == 'meta' and a == 0x51:
            last_s += (t - last_t) * us / ppq / 1e6
            last_t, us = t, int.from_bytes(b, 'big')
            marks.append((t, last_s, us))
    marks = marks or [(0, 0.0, tempo)]

    def sec(t):
        m = max((x for x in marks if x[0] <= t), key=lambda x: x[0], default=marks[0])
        return m[1] + (t - m[0]) * m[2] / ppq / 1e6

    on, notes, loop, last = {}, [], None, 0
    for t, kind, a, b in evs:
        last = max(last, t)
        if kind == 'meta' and a == 0x06 and b == b'loop':
            loop = sec(t)
        if kind != 'ch' or a & 0xF0 not in (0x80, 0x90):
            continue
        key = (a & 15, b[0])
        if a & 0xF0 == 0x90 and b[1]:
            on[key] = t
        elif key in on:
            notes.append((a & 15, sec(on.pop(key)), sec(t), b[0]))
    return notes, sec(last), beats * tempo / 1e6, loop


def measure(path):
    notes, length, bar, loop = read(path)
    mel = [x for x in notes if x[0] != 9]
    drums = [x for x in notes if x[0] == 9]
    starts = sorted(x[1] for x in mel)
    windows = int(length / 0.2) + 1
    hit = {int(s / 0.2) for s in starts}
    held = sum(x[2] - x[1] for x in mel)
    pcs = [0.0] * 12
    for x in mel:
        pcs[x[3] % 12] += x[2] - x[1]
    fit = max(sum(pcs[(k + m) % 12] for m in MAJOR) for k in range(12)) / max(1e-9, sum(pcs))
    fast = 0
    for c in {x[0] for x in mel}:
        ts = sorted({round(x[1], 3) for x in mel if x[0] == c})
        fast += sum(1 for a, b in zip(ts, ts[1:]) if 0.02 < b - a < 0.1)
    # a steady beat: the same drum on most of a run of beats
    steady = False
    if drums:
        beat = bar / 4
        by = {}
        for x in drums:
            by.setdefault(x[3], set()).add(int(round(x[1] / beat)))
        steady = any(len(v) > 0.6 * length / beat for v in by.values())
    # bars in a row with the same parts starting notes and the same key (the best-fitting major scale)
    nbars = int(length / bar + 0.5)
    sig = []
    for k in range(nbars):
        a, b = k * bar, (k + 1) * bar
        parts = frozenset(x[0] for x in notes if a <= x[1] < b)
        w = [0.0] * 12
        for x in mel:
            o = min(x[2], b) - max(x[1], a)
            if o > 0:
                w[x[3] % 12] += o
        key = max(range(12), key=lambda r: sum(w[(r + m) % 12] for m in MAJOR))
        sig.append((parts, key))
    same = run = 1
    for a, b in zip(sig, sig[1:]):
        run = run + 1 if a == b else 1
        same = max(same, run)
    return {'notes/s': len(mel) / length, 'motor': len(hit) / windows, 'voices': held / length,
            'off-key': 1 - fit, 'fast/s': fast / length, 'drums/s': len(drums) / length, 'same': same,
            'steady': steady, 'seconds': length, 'loop': loop}


def main():
    paths = sys.argv[1:] or [os.path.join(ROOT, 'build', 'dos', f + '.MID') for f in
                             ['CRUISE%d' % k for k in range(1, 7)] + ['HOMEWARD', 'HOMELOSS']]
    names = list(BANDS)
    print('%-13s %6s' % ('song', 'secs') + ''.join('%9s' % n for n in names))
    failed = 0
    for path in paths:
        m = measure(path)
        bands = HOME_BANDS if os.path.basename(path).upper().startswith('HOME') else BANDS
        cells, bad = [], []
        for n in names:
            lo, hi = bands[n]
            v = m[n]
            out = (lo is not None and v < lo) or (hi is not None and v > hi) or (n == 'drums/s' and m['steady'])
            cells.append(('%8.2f' % v if isinstance(v, float) else '%8d' % v) + ('!' if out else ' '))
            if out:
                bad.append(n + (' (a steady beat)' if n == 'drums/s' and m['steady'] else ''))
        print('%-13s %6.0f' % (os.path.basename(path), m['seconds']) + ''.join(cells))
        if bad:
            failed += 1
            print('%13s outside: %s' % ('', ', '.join(bad)))
    return failed


if __name__ == '__main__':
    sys.exit(main())
