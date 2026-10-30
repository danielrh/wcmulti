#!/usr/bin/env python3
"""A flight trace (the layer's WCNET_SHIPTRACE, or RetroVega's own in the same form) read for the AI's behaviour:
the flights in it (the frame count restarts with each), each ship's life in a slot (a slot changes ships at a nav
point), and for each AI ship a summary of what it did against the player (slot 0): how it came in, how fast it
turned, when and from how far it fired, its passes, and a timeline.

    trace2.py TRACE [--fps N] [--timeline] [--slot S]

The lines: "S frame slot entity type side pilot x y z fx fy fz ux uy uz vx vy vz speed setspeed aistate aitimer
missiletarget parent hull shf shr arf arr arl arrt", "F frame ship gun", "D frame src dst qty".  Positions are
256 a km; velocities positions a frame; the speed columns the game's own units.
"""
import math, sys
from collections import defaultdict

def length(a):
    return math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])

def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]

def sub(a, b):
    return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]

def read(path):
    """flights: a list of (frames dict frame -> slot -> record, fires, hits)"""
    flights = []
    cur = None
    last = -1
    for line in open(path):
        p = line.split()
        if not p:
            continue
        if p[0] == 'S':
            if len(p) < 32:
                continue
            v = [int(x) for x in p[1:]]
            frame = v[0]
            if cur is None or frame < last - 5:
                cur = (defaultdict(dict), defaultdict(list), defaultdict(list))
                flights.append(cur)
            last = frame
            cur[0][frame][v[1]] = dict(entity=v[2], type=v[3], side=v[4], pilot=v[5], pos=v[6:9], front=v[9:12], up=v[12:15],
                                       vel=v[15:18], speed=v[18], set=v[19], ai=v[20], timer=v[21], mtarget=v[22],
                                       parent=v[23], hull=v[24], sh=v[25:27], ar=v[27:31])
        elif cur is not None and p[0] == 'F':
            cur[1][int(p[1])].append((int(p[2]), int(p[3])))
        elif cur is not None and p[0] == 'D':
            cur[2][int(p[1])].append((int(p[2]), int(p[3]), int(p[4])))
    return flights

def lives(frames):
    """(slot, entity, type, side, first frame, last frame) for each ship a slot held"""
    out = []
    open_ = {}
    for fr in sorted(frames):
        for slot, r in frames[fr].items():
            key = (r['entity'], r['type'], r['side'])
            if slot in open_ and open_[slot][0] == key and fr - open_[slot][2] <= 5:
                open_[slot][2] = fr
            else:
                if slot in open_:
                    k, a, b = open_[slot]
                    out.append((slot, k[0], k[1], k[2], a, b))
                open_[slot] = [key, fr, fr]
    for slot, (k, a, b) in open_.items():
        out.append((slot, k[0], k[1], k[2], a, b))
    return sorted(out, key=lambda x: (x[4], x[0]))

def pct(xs, q):
    return sorted(xs)[min(len(xs) - 1, int(q * len(xs)))] if xs else 0

def summarise(flight, fi, fps, timeline, only):
    frames, fires, hits = flight
    order = sorted(frames)
    f0, f1 = order[0], order[-1]
    print('\n=== flight %d: frames %d .. %d (%.1f s at %g fps)' % (fi, f0, f1, (f1 - f0) / fps, fps))
    for slot, ent, typ, side, a, b in lives(frames):
        print('  slot %d: entity %d type %d side %d, frames %d .. %d (%.1f .. %.1f s)' % (slot, ent, typ, side, a, b, (a - f0) / fps, (b - f0) / fps))
    for slot, ent, typ, side, a, b in lives(frames):
        if slot == 0 or ent not in (12, 13) or (only is not None and slot != only):
            continue
        rows = [(fr, frames[fr][slot], frames[fr][0]) for fr in order if a <= fr <= b and slot in frames[fr] and 0 in frames[fr]]
        if len(rows) < 2:
            continue
        print('\n  --- slot %d, type %d side %d, %.1f .. %.1f s ---' % (slot, typ, side, (a - f0) / fps, (b - f0) / fps))
        speeds, turns, dists, aims, its = [], [], [], [], []
        prev = None
        for fr, r, pl in rows:
            vel = length(r['vel']) / 256.0 * fps
            d = sub(pl['pos'], r['pos'])
            dk = length(d) / 256.0
            cn = dot(d, r['front']) / (length(d) * 256.0) if dk > 0 else 1
            ci = -dot(d, pl['front']) / (length(d) * 256.0) if dk > 0 else 1
            speeds.append(vel)
            dists.append(dk)
            aims.append(cn)
            its.append(ci)
            if prev is not None:
                c = max(-1.0, min(1.0, dot(prev['front'], r['front']) / max(1.0, length(prev['front']) * length(r['front']))))
                turns.append(math.degrees(math.acos(c)) * fps)
            prev = r
        print('  speed km/s: median %.0f, 90%% %.0f, max %.0f; turn deg/s: median %.0f, 90%% %.0f, max %.0f' % (
            pct(speeds, .5), pct(speeds, .9), max(speeds), pct(turns, .5), pct(turns, .9), max(turns) if turns else 0))
        print('  distance km: first %.0f, least %.0f, last %.0f; nose on the player (cos): median %.2f, share > 0.9: %.0f%%' % (
            dists[0], min(dists), dists[-1], pct(aims, .5), 100.0 * sum(1 for x in aims if x > 0.9) / len(aims)))
        shots = []
        for fr, r, pl in rows:
            n = sum(1 for sh, g in fires.get(fr, []) if sh == slot)
            if n:
                d = sub(pl['pos'], r['pos'])
                ld = max(1.0, length(d))
                c = max(-1.0, min(1.0, dot(d, r['front']) / (256.0 * ld)))
                shots.append((fr, ld / 256.0, math.degrees(math.acos(c)), n))
        if shots:
            ds = [d for fr, d, an, n in shots]
            an = [an for fr, d, an, n in shots]
            print('  fired on %d frames (%d shots): at %.0f .. %.0f km (median %.0f), %.1f .. %.1f degrees off the player (median %.1f); first at %.1f s, %.0f km' % (
                len(shots), sum(n for fr, d, an, n in shots), min(ds), max(ds), pct(ds, .5), min(an), max(an), pct(an, .5),
                (shots[0][0] - f0) / fps, shots[0][1]))
        else:
            print('  never fired')
        passes = []
        for i in range(1, len(dists) - 1):
            if dists[i] < 2500 and dists[i] <= dists[i - 1] and dists[i] < dists[i + 1]:
                if not passes or rows[i][0] - passes[-1][0] > fps:
                    passes.append((rows[i][0], dists[i], aims[i], speeds[i]))
        if passes:
            print('  closest points under 2,500 km: ' + ', '.join('%.1f s %.0f km (nose %.2f, %.0f km/s)' % ((fr - f0) / fps, d, c, v)
                                                                    for fr, d, c, v in passes[:10]))
        states = defaultdict(int)
        for fr, r, pl in rows:
            states[r['ai']] += 1
        print('  AI state words: ' + ', '.join('%d x%d' % (k, n) for k, n in sorted(states.items(), key=lambda kv: -kv[1])[:8]))
        got = defaultdict(int)
        for fr in order:
            for s, dd, q in hits.get(fr, []):
                if dd == slot and a <= fr <= b:
                    got[s] += q
        if got:
            print('  hurt by (src: points): ' + ', '.join('%d: %d' % kv for kv in sorted(got.items())))
        if timeline:
            for i, (fr, r, pl) in enumerate(rows):
                fired = sum(1 for sh, g in fires.get(fr, []) if sh == slot)
                if (fr - a) % max(1, int(fps / 2)) == 0 or fired:
                    print('    %6.1f s %6.0f km  nose %5.2f  its %5.2f  v %4.0f  turn %4.0f  ai %3d t %5d  hull %d sh %s%s' % (
                        (fr - f0) / fps, dists[i], aims[i], its[i], speeds[i], turns[i - 1] if i else 0, r['ai'], r['timer'],
                        r['hull'], r['sh'], '  FIRE x%d' % fired if fired else ''))
    nh = sum(len(v) for v in hits.values())
    if nh:
        by = defaultdict(int)
        for fr, hs in hits.items():
            for s, d, q in hs:
                by[(s, d)] += q
        print('\n  hits: %d calls; points by (src, dst): %s' % (nh, ', '.join('%s: %d' % (k, n) for k, n in sorted(by.items(), key=lambda kv: -kv[1])[:12])))

def main():
    path = sys.argv[1]
    fps = float(sys.argv[sys.argv.index('--fps') + 1]) if '--fps' in sys.argv else 20.0
    only = int(sys.argv[sys.argv.index('--slot') + 1]) if '--slot' in sys.argv else None
    flights = read(path)
    print('%d flights in %s' % (len(flights), path))
    for i, fl in enumerate(flights):
        summarise(fl, i, fps, '--timeline' in sys.argv, only)

if __name__ == '__main__':
    main()
