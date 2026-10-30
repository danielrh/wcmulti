#!/usr/bin/env python3
"""A compact digest of a flight trace for the tuning loop: for each AI fighter against the player: speed, turn,
nose-on share, first shot, passes (closest points under 2,500 km) with the turn-around time after each, rams
(passes under the two hulls' sum), and the damage pairs.   digest.py DIR TRACE FPS HULLSUM_KM"""
import math, sys
sys.path.insert(0, sys.argv[1])
from trace import read, lives, length, dot, sub
path, fps, hull = sys.argv[2], float(sys.argv[3]), float(sys.argv[4])
for flight in read(path):
    frames, fires, hits = flight
    order = sorted(frames)
    f0 = order[0]
    for slot, ent, typ, side, a, b in lives(frames):
        if side != 1 or slot == 0 or ent not in (12, 13):
            continue
        rows = [(fr, frames[fr][slot], frames[fr][0]) for fr in order if a <= fr <= b and slot in frames[fr] and 0 in frames[fr]]
        if len(rows) < 2 * fps:
            continue
        dists, aims, speeds, turns = [], [], [], []
        prev = None
        for fr, r, pl in rows:
            d = sub(pl['pos'], r['pos'])
            dists.append(length(d) / 256.0)
            aims.append(dot(d, r['front']) / (256.0 * max(1, length(d))))
            speeds.append(length(r['vel']) / 256.0 * fps)
            if prev is not None:
                turns.append(math.degrees(math.acos(max(-1, min(1, dot(prev['front'], r['front']) / max(1.0, length(prev['front']) * length(r['front'])))))) * fps)
            prev = r
        shots = [(fr, dists[i]) for i, (fr, r, pl) in enumerate(rows) if any(sh == slot for sh, g in fires.get(fr, []))]
        w = int(fps / 2)
        passes = [i for i in range(w, len(rows) - w) if dists[i] < 2500 and dists[i] <= min(dists[i - w:i + w]) and dists[i] < dists[i - w] - 50]
        out = []
        for i in passes:
            back = next((j for j in range(i, min(len(rows), i + int(20 * fps))) if aims[j] > 0.9), None)
            out.append('%d%s' % (dists[i], '/%.1fs' % ((back - i) / fps) if back is not None else '/never'))
        sp = sorted(speeds); tu = sorted(turns)
        print('slot %d (%.0f-%.0f s): speed med %.0f 90%% %.0f max %.0f; turn 90%% %.0f max %.0f; nose>0.9 %.0f%%; first shot %s; shots %d' % (
            slot, (a - f0) / fps, (b - f0) / fps, sp[len(sp) // 2], sp[int(.9 * len(sp))], sp[-1], tu[int(.9 * len(tu))] if tu else 0, tu[-1] if tu else 0,
            100.0 * sum(1 for x in aims if x > 0.9) / len(aims), '%.1f s at %.0f km' % ((shots[0][0] - f0) / fps, shots[0][1]) if shots else 'none', len(shots)))
        print('   passes km/turn-around: ' + (' '.join(out) if out else 'none') + '   rams: %d' % sum(1 for i in passes if dists[i] < hull))
    by = {}
    for fr, hs in hits.items():
        for s, d, q in hs:
            by[(s, d)] = by.get((s, d), 0) + q
    print('   damage (src,dst): ' + ', '.join('%s:%d' % (k, n) for k, n in sorted(by.items(), key=lambda kv: -kv[1])[:10]))
