"""Tools for writing the longer songs (tools/songs.py): chord symbols, voicings
that move as little as they can from one chord to the next, a bass line, and
a melody writer that works phrase by phrase.

    chord('Gmaj9#11')           -> Chord(root=7, tones=(0, 4, 11, 14, 18), bass=7)
    Voicer(lo, hi, n).next(c)   -> n notes of c, close to the last voicing
    Tune(rng, key, mode, lo, hi).phrase(chords, rhythms, ...)
                                -> [(bar, beat, length, pitch)], a phrase over
                                   those bars: chord tones on the strong beats,
                                   steps of the scale between, an arc up and a
                                   settled end, few repeats and no wild leaps

Everything is seeded: the same song comes out every time.  Ours, CC0.
"""
import itertools
from collections import namedtuple

PC = {'C': 0, 'C#': 1, 'Db': 1, 'D': 2, 'D#': 3, 'Eb': 3, 'E': 4, 'F': 5, 'F#': 6, 'Gb': 6, 'G': 7, 'G#': 8,
      'Ab': 8, 'A': 9, 'A#': 10, 'Bb': 10, 'B': 11, 'Cb': 11}
QUALITY = {
    '': (0, 4, 7), 'm': (0, 3, 7), 'sus4': (0, 5, 7), 'sus2': (0, 2, 7), 'add9': (0, 4, 7, 14),
    'madd9': (0, 3, 7, 14), '6': (0, 4, 7, 9), 'm6': (0, 3, 7, 9), '69': (0, 4, 9, 14),
    'maj7': (0, 4, 7, 11), 'maj9': (0, 4, 7, 11, 14), 'maj7#11': (0, 4, 7, 11, 18), 'maj9#11': (0, 4, 11, 14, 18),
    'm7': (0, 3, 7, 10), 'm9': (0, 3, 7, 10, 14), 'm11': (0, 3, 10, 14, 17), 'mmaj7': (0, 3, 7, 11),
    '7': (0, 4, 7, 10), '9': (0, 4, 7, 10, 14), '13': (0, 4, 10, 14, 21), '7sus4': (0, 5, 7, 10),
    '9sus4': (0, 5, 10, 14), '13sus4': (0, 5, 10, 14, 21), '7b9': (0, 4, 7, 10, 13), '7#9': (0, 4, 10, 15),
    '7#11': (0, 4, 7, 10, 18), 'm7b5': (0, 3, 6, 10), 'dim7': (0, 3, 6, 9),
}
Chord = namedtuple('Chord', 'root tones bass')
MODES = {'major': (0, 2, 4, 5, 7, 9, 11), 'minor': (0, 2, 3, 5, 7, 8, 10), 'dorian': (0, 2, 3, 5, 7, 9, 10),
         'lydian': (0, 2, 4, 6, 7, 9, 11), 'mixolydian': (0, 2, 4, 5, 7, 9, 10), 'blues': (0, 3, 5, 6, 7, 10)}


def chord(sym):
    """'Bbm9', 'D/F#', 'A7sus4' ..."""
    sym, _, bass = sym.partition('/')
    root = sym[:2] if len(sym) > 1 and sym[1] in '#b' else sym[:1]
    q = sym[len(root):]
    if q not in QUALITY:
        raise ValueError('chord ' + sym)
    r = PC[root]
    return Chord(r, QUALITY[q], PC[bass] if bass else r)


def bars_of(text):
    """'Fmaj9 | Dm9 | Am7 D9 |' -> [['Fmaj9'], ['Dm9'], ['Am7', 'D9']]"""
    return [b.split() for b in text.split('|') if b.strip()]


def colour_pcs(c):
    """the pitch classes a voicing is made of, the root last (left out when there are enough others)"""
    rest = [(c.root + t) % 12 for t in c.tones if t % 12]
    out = []
    for p in rest:
        if p not in out:
            out.append(p)
    return out, c.root


class Voicer:
    """n notes of each chord within lo..hi, moving as little as they can"""

    def __init__(self, lo, hi, n, span=14):
        self.lo, self.hi, self.n, self.span = lo, hi, n, span
        self.last = None

    def next(self, c):
        pcs, root = colour_pcs(c)
        if self.n == 3 and len(pcs) >= 3:
            third = [p for p in pcs if (p - c.root) % 12 in (3, 4, 5, 2)][:1]
            pcs = [root] + third + [p for p in pcs if p not in third][:1]
        else:
            for extra in (root, (root + 7) % 12):
                if len(pcs) < self.n and extra not in pcs:
                    pcs = pcs + [extra]
            pcs = pcs[:self.n]
        options = [[q for q in range(self.lo, self.hi + 1) if q % 12 == p] for p in pcs]
        best, bcost = None, None
        mid = (self.lo + self.hi) / 2
        for combo in itertools.product(*options):
            v = sorted(combo)
            if len(set(v)) < len(v):
                continue
            if v[-1] - v[0] > self.span:
                continue
            if self.last:
                cost = sum(abs(a - b) for a, b in zip(v, self.last))
            else:
                cost = abs(sum(v) / len(v) - mid)
            cost += 0.15 * abs(sum(v) / len(v) - mid)
            if bcost is None or cost < bcost:
                best, bcost = v, cost
        if best is None and self.span < 36:
            self.span += 4                      # (a wide chord in a narrow window: spread it)
            best = self.next(c)
            self.span -= 4
            return best
        while len(best) < self.n:               # (a triad for four voices: double a note an octave away)
            k = len(best) % len(best)
            q = best[k] + 12 if best[k] + 12 not in best else best[k] - 12
            best = sorted(best + [q])
        self.last = best
        return best


class Bass:
    """the chord's bass note between lo and hi, near the last"""

    def __init__(self, lo=28, hi=45):
        self.lo, self.hi, self.last = lo, hi, None

    def note(self, c, pc=None):
        pc = c.bass if pc is None else pc
        cands = [q for q in range(self.lo, self.hi + 1) if q % 12 == pc]
        q = min(cands, key=lambda x: abs(x - (self.last if self.last else (self.lo + self.hi) // 2 - 4)))
        self.last = q
        return q


def scale_for(key, mode, c):
    """the key's scale, bent to the chord where they disagree (a borrowed chord's notes win)"""
    sc = [(key + s) % 12 for s in MODES[mode]]
    for t in c.tones:
        p = (c.root + t) % 12
        if p in sc:
            continue
        for d in (1, -1):
            if (p + d) % 12 in sc and (p + d) % 12 not in [(c.root + u) % 12 for u in c.tones]:
                sc[sc.index((p + d) % 12)] = p
                break
        else:
            sc.append(p)
    return sorted(set(sc))


# rhythms for a bar: (beat, length); the last bar of a phrase settles
CALM = [[(0, 2), (2, 1), (3, 1)], [(0, 3), (3, 1)], [(0, 1.5), (1.5, .5), (2, 2)], [(0, 1), (1, 1), (2, 2)],
        [(0, 2), (2, 2)], [(1, 1), (2, 2)], [(0, 1), (1, 2), (3, 1)], [(0, 2.5), (2.5, .5), (3, 1)],
        [(0, 1), (1, 1), (2, 1), (3, 1)]]
CALM_END = [[(0, 4)], [(0, 3)], [(0, 2), (2, 2)], [(0, 3), (3.5, .5)]]
SWING = [[(0, 1), (1, .5), (1.5, .5), (2, 1.5), (3.5, .5)], [(0, .5), (.5, .5), (1, 1), (2, .5), (2.5, 1.5)],
         [(.5, .5), (1, .5), (1.5, .5), (2, 1), (3, 1)], [(0, 1.5), (1.5, .5), (2, 1), (3, 1)],
         [(0, 2), (2, .5), (2.5, 1.5)], [(0, 1), (1, 1), (2, 1.5), (3.5, .5)], [(.5, 1), (1.5, .5), (2, 2)]]
SWING_END = [[(0, 3), (3.5, .5)], [(0, 2.5)], [(0, 1), (1, 2.5)], [(0, 2), (2.5, .5), (3, 1)]]
SOLO = [[(0, .5), (.5, .5), (1, .5), (1.5, .5), (2, .5), (2.5, .5), (3, 1)],
        [(0, 1), (1, .5), (1.5, .5), (2, .5), (2.5, .5), (3, .5), (3.5, .5)],
        [(.5, .5), (1, .5), (1.5, .5), (2, 1), (3, .5), (3.5, .5)],
        [(0, .5), (.5, .5), (1, 1), (2, .5), (2.5, .5), (3, 1)],
        [(0, 1.5), (1.5, .5), (2, .5), (2.5, .5), (3, .5), (3.5, .5)]] + SWING[:3]
SOLO_END = [[(0, 1.5), (1.5, .5), (2, 2)], [(0, 3)], [(0, .5), (.5, .5), (1, 2.5)]]
BOSSA = [[(0, 1.5), (1.5, 1), (2.5, 1.5)], [(0, 1), (1, 1), (2, 1.5), (3.5, .5)], [(.5, 1), (1.5, .5), (2, 2)],
         [(0, 2), (2, 1), (3, 1)], [(0, .5), (.5, 1), (1.5, .5), (2, 2)]]
BOSSA_END = [[(0, 4)], [(0, 3)], [(0, 2), (2, 2)]]


class Tune:
    def __init__(self, rng, key, mode, lo, hi):
        self.rng, self.key, self.mode, self.lo, self.hi = rng, PC[key] if isinstance(key, str) else key, mode, lo, hi
        self.prev = (lo + hi) // 2
        self.prev2 = None

    def _pick(self, cands, target, strong, last_leap, first):
        scored = []
        for q in cands:
            d = q - self.prev
            cost = abs(q - target) * 0.7
            cost += abs(d) * (0.1 if first else 0.2 if strong else 0.45)
            if abs(d) > 7 and not first:
                cost += 4 if abs(d) <= 12 else 20
            if q == self.prev:
                cost += 3 if q != self.prev2 else 9
            elif q == self.prev2:
                cost += 2.5                     # no trill back and forth
            if last_leap and abs(last_leap) > 4 and d * last_leap > 0:
                cost += 4                       # after a leap, turn back
            scored.append((cost + self.rng.random() * 1.5, q))
        scored.sort()
        return scored[0][1]

    def _shape(self, shape):
        """the line a phrase follows: (start, turn, end, where it turns)"""
        lo, hi, r = self.lo, self.hi, self.rng
        mid = (lo + hi) // 2
        if shape == 'fall':
            a = r.randint(hi - 5, hi - 1)
            return a, a, r.randint(lo + 2, mid - 1), 0.1
        if shape == 'rise':
            z = r.randint(hi - 6, hi - 2)
            return r.randint(lo + 1, mid - 2), z, z, 0.95
        return r.randint(lo + 2, mid), r.randint(hi - 3, hi), r.randint(mid - 3, mid + 2), r.choice((0.5, 0.6, 0.7))

    def phrase(self, bar0, chords, rhythms, ends, shape=None, end_on=(0, 4, 3, 7), reuse=None, octave_shift=0):
        """notes over len(chords) bars (each bar a list of one or two chord symbols), following a
        shape ('arch', 'fall' or 'rise'; picked when not given); reuse: another phrase's
        rhythm (its notes as bar offsets and beats), kept"""
        n = len(chords)
        notes = []
        if reuse:
            plan = [(b, beat, dur) for b, beat, dur, _ in reuse]
        else:
            plan = []
            for i in range(n):
                pool = ends if i == n - 1 else rhythms
                for beat, dur in self.rng.choice(pool):
                    plan.append((i, beat, dur))
        a, peak, z, turn = self._shape(shape or self.rng.choice(('arch', 'arch', 'fall', 'rise')))
        last_leap = 0
        for k, (i, beat, dur) in enumerate(plan):
            syms = chords[i]
            c = chord(syms[0] if beat < 2 or len(syms) == 1 else syms[1])
            pos = (i * 4 + beat) / (n * 4)
            target = a + (peak - a) * pos / turn if pos < turn else peak + (z - peak) * (pos - turn) / (1 - turn)
            strong = beat in (0, 2) or dur >= 1.5
            last = k == len(plan) - 1
            tones = [(c.root + t) % 12 for t in c.tones]
            if last:
                pcs = [(c.root + t) % 12 for t in c.tones if t % 12 in end_on] or tones
            elif strong:
                pcs = tones
            else:
                pcs = scale_for(self.key, self.mode, c)
            cands = [q for q in range(self.lo, self.hi + 1) if q % 12 in pcs]
            q = self._pick(cands, target, strong, last_leap, k == 0)
            last_leap = q - self.prev
            self.prev2, self.prev = self.prev, q
            notes.append((bar0 + i, beat, dur, q + 12 * octave_shift))
        return notes

    def period(self, bar0, chords, rhythms, ends, phrase=4, answer=True, **kw):
        """phrases of `phrase` bars over all the chords; with answer, every second phrase keeps
        the first's rhythm (a question and its answer)"""
        out, first = [], None
        for k in range(0, len(chords), phrase):
            part = chords[k:k + phrase]
            odd = (k // phrase) % 2 == 1
            reuse = first if answer and odd and first and len(part) == phrase else None
            notes = self.phrase(bar0 + k, part, rhythms, ends, reuse=reuse, shape='fall' if reuse else None, **kw)
            if not odd:
                first = [(b - (bar0 + k), beat, dur, q) for b, beat, dur, q in notes]
            out += notes
        return out
