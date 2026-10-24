"""The flight music's orchestra (CC0, ours): what every piece of tools/flightsongs.py and its parts' modules
(cruise_a.py, cruise_b.py, battles.py) is written with.

    Piece                a piece over a harmony of bars (one chord symbol a bar, or two), on songs.Comp
    Piece.loop_at(bar)   the "loop" marker: what is before it is an intro, played once
    Piece.sect(bar)      a "sect" marker: a block the score may cut to or from (src/music.c's follow-ons)
    Piece.sections(a, b, every=4)   the markers every so many bars from a up to b
    orchestra(s)         the third draft's parts on their channels at their measured levels (ORCH)

and the third draft's figures: octaves, pulses, pedal332, hit, held, sung, sweep.
"""
import random

from compose import chord, Voicer, Bass, Tune, CALM, CALM_END
from songs import Comp, p, put


NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
DRUM = {'kick': 36, 'stick': 37, 'snare': 38, 'hat': 42, 'open': 46, 'ride': 51, 'crash': 49, 'shaker': 82,
        'tom1': 50, 'tom2': 47, 'tom3': 43, 'tom4': 41}


class Piece(Comp):
    """a piece over a harmony of bars, each one chord symbol or two (halves)"""

    def __init__(self, bpm, seed, harmony, swing=0.5, level=0.0):
        super().__init__(bpm, seed, swing)
        self.harm = [b.split() for b in harmony]
        self.level = level              # dB, to even the pieces out: half on the volumes, half on the velocities

    def setup(self, ch, prog, vol, pan=64):
        super().setup(ch, prog, vol * 10 ** (self.level / 2 / 40), pan)      # (the driver: 40 log10)

    def play(self, ch, bar, beat, dur, pitches, vel, spread=4):
        super().play(ch, bar, beat, dur, pitches, round(vel * 10 ** (self.level / 2 / 20)), spread)    # (20 log10)

    def loop_at(self, bar):
        """the song goes round again from this bar: what is before it is an intro, played once"""
        self.marker(bar * self.beats, 'loop')

    def sect(self, bar):
        """a block starts at this bar: the score may cut to it from another piece, or away from the piece at it
        (src/music.c's follow-ons), so nothing should hang over it that needs the block before"""
        self.marker(bar * self.beats, 'sect')

    def sections(self, bar0, bar1, every=4):
        """a block every so many bars from bar0 up to (not at) bar1"""
        for b in range(bar0, bar1, every):
            self.sect(b)

    def segs(self, bars):
        """(bar, beat, beats, symbol) for each chord of these bars"""
        for b in bars:
            syms = self.harm[b]
            each = 4 / len(syms)
            for i, sym in enumerate(syms):
                yield b, i * each, each, sym

    def sym_at(self, bar, beat):
        syms = self.harm[bar]
        return syms[min(len(syms) - 1, int(beat // (4 / len(syms))))]

    def pad(self, ch, bars, lo, hi, n, vel, spread=2):
        """held chords, a voicing kept on over the bar line while it does not change"""
        v = Voicer(lo, hi, n)
        held = []
        for b, beat, dur, sym in self.segs(bars):
            vo = v.next(chord(sym))
            start = b * 4 + beat
            if held and held[-1][2] == vo and abs(held[-1][0] + held[-1][1] - start) < 1e-6:
                held[-1][1] += dur
            else:
                held.append([start, dur, vo])
        for start, dur, vo in held:
            self.play(ch, int(start // 4), start % 4, dur - 0.06, vo, vel, spread)

    def arp(self, ch, bars, pattern, step, lo, hi, n, vel, accent=8, gate=0.95, reset=False):
        """the chord broken: pattern indexes its voicing (n and up: an octave higher; None: a rest)"""
        v = Voicer(lo, hi, n)
        k = 0
        for b, beat, dur, sym in self.segs(bars):
            vo = v.next(chord(sym))
            if reset and beat == 0:
                k = 0
            t = 0.0
            while t < dur - 1e-6:
                i = pattern[k % len(pattern)]
                if i is not None:
                    pos = beat + t
                    on = abs(pos - round(pos)) < 1e-6
                    a = accent if on and round(pos) % 2 == 0 else accent // 2 if on else 0
                    self.play(ch, b, pos, step * gate, vo[i % n] + 12 * (i // n), vel + a, 3)
                k += 1
                t += step

    def bass(self, ch, bars, rhythm, vel, lo=28, hi=43, pedal=None):
        """rhythm: (beat, length, which) a bar: 'r' the chord's bass, '5' its fifth, '8' an octave up,
        '-' the next chord's bass a step below (a lead-in)"""
        bs = Bass(lo, hi)
        roots = {}
        for b, beat, dur, sym in self.segs(bars):
            roots[(b, beat)] = pedal if pedal is not None else bs.note(chord(sym))
        keys = sorted(roots)
        for b in bars:
            for beat, dur, which in rhythm:
                at = max(k for k in keys if k <= (b, beat + 1e-6))
                r = roots[at]
                if which == '-':
                    later = [k for k in keys if k > at]
                    r = (roots[later[0]] if later else r) - 1
                q = r + {'r': 0, '5': 7, '8': 12, '-': 0, 'b': -12}[which]
                self.play(ch, b, beat, dur, q, vel + (6 if beat == 0 else 0), 3)

    def drums(self, bars, pat, fill=None, every=8, vel=0):
        """pat: {drum: [(beat, velocity)]} each bar; fill the same for the last bar of every `every`"""
        first = bars.start
        for b in bars:
            use = fill if fill and (b - first) % every == every - 1 else pat
            for name, hits_ in use.items():
                key = DRUM.get(name, name)
                for beat, v in hits_:
                    self.play(9, b, beat, 0.2, key, v + vel, 3)

    def made(self, ch, bar0, nbars, key, mode, lo, hi, vel, rhythms=CALM, ends=CALM_END, seed=0, phrase=4,
             double=()):
        """a tune written from the chords of these bars; double: (channel, semitones, velocity) to play it on too"""
        t = Tune(random.Random(seed), key, mode, lo, hi)
        chords = [self.harm[b] for b in range(bar0, bar0 + nbars)]
        notes = t.period(bar0, chords, rhythms, ends, phrase=phrase)
        put(self, ch, notes, vel)
        for dch, semis, dvel in double:
            put(self, dch, [(b, beat, dur, q + semis) for b, beat, dur, q in notes], dvel)

    def roll(self, ch, bar, beat, beats, pitch, v0, v1, step=0.25):
        """a timpani roll (or any drum's): strokes every step, from v0 to v1"""
        n = max(1, int(round(beats / step)))
        for k in range(n):
            pos = bar * 4 + beat + k * step
            self.play(ch, int(pos // 4), pos % 4, step * 0.9, pitch, round(v0 + (v1 - v0) * k / max(1, n - 1)), 2)

    def run(self, ch, bar, beat, pitches, step, vel, rise=0):
        """a run (a harp's glissando, a flurry): the pitches one after another"""
        for k, q in enumerate(pitches):
            pos = bar * 4 + beat + k * step
            self.play(ch, int(pos // 4), pos % 4, step * 1.5, q, vel + rise * k, 2)

    def crash(self, bar, beat=0, vel=84):
        self.play(9, bar, beat, .2, DRUM['crash'], vel)

    def ramp(self, ch, bar, bars, v0, v1, num=11):
        """expression (or another controller) from v0 to v1 over so many bars, a step a beat, the last a little
        before the bar line it ends on (on it, a song's last bar would gain one: the loop a bar long)"""
        n = max(1, int(bars * self.beats))
        for k in range(n + 1):
            self.cc(ch, bar, k - (0.05 if k == n else 0), num, round(v0 + (v1 - v0) * k / n))

    def swell(self, ch, bar, bars, note, vel=60, v0=20, v1=120):
        """the noise swell (program 119) held over these bars, rising by expression"""
        self.play(ch, bar, 0, bars * 4 - 0.1, note, vel)
        self.ramp(ch, bar, bars, v0, v1)
        self.cc(ch, bar + bars - 1, self.beats - 0.04, 11, v0)

    def end(self, bars):
        """a silent controller at the end of the last bar: the loop is whole bars"""
        self.cc(0, bars - 1, 3.95, 11, 127)


def hits(*beats, v=60):
    return [(b, v) for b in beats]


def up(text, semis):
    """a bar of 'PITCH:BEAT:LENGTH' notes moved by so many semitones"""
    out = []
    for tok in text.split():
        name, beat, dur = tok.split(':')
        q = p(name) + semis
        out.append('%s%d:%s:%s' % (NAMES[q % 12], q // 12 - 1, beat, dur))
    return ' '.join(out)


def scale_run(lo, hi, steps):
    """the notes of steps (semitones of a scale from its root, 0..11) from lo up to hi"""
    return [q for q in range(lo, hi + 1) if (q - lo) % 12 in steps]


# --- the third draft's orchestra (2026-10-24) ----------------------------------------
# The user: the cruise songs "sound like elevator music--not memorable".  The two cues they sent for the feeling
# (excitement and wonder) are fast, lean and restless: a motor of eighths under a tune in long notes, three to
# five parts each with one job, keys that lift by thirds, hits on 3+3+2 where a groove would be, piccolo runs and
# harp sweeps, a riser into the loop; scored for the MT-32's small orchestra.  Ours are original: the techniques
# only (docs/music.md).  The parts' levels match the balance of those cues on our bank, measured K-weighted: the
# brass 8 dB over the motor, the horns 5, the piccolo 4, the hits 7, the crash 5.
ORCH = {'horn': (0, 60, 100), 'motor': (1, 44, 104), 'hit': (2, 55, 120), 'low': (3, 44, 112),
        'brass': (4, 62, 92), 'horn2': (5, 60, 96), 'piccolo': (6, 72, 88), 'timpani': (7, 47, 100),
        'harp': (8, 46, 92), 'strings': (10, 48, 100), 'bass': (11, 48, 100)}
ACC332 = (0, 1.5, 3)                    # 3+3+2 in eighths: where a bar's accents fall
MAJOR, LYDIAN = (0, 2, 4, 5, 7, 9, 11), (0, 2, 4, 6, 7, 9, 11)   # (a run's steps from its first note)


def orchestra(s):
    """the parts on their channels at their levels, the drums' channel full: {part: channel}"""
    for ch, prog, vol in ORCH.values():
        s.setup(ch, prog, vol)
    s.setup(9, 0, 127)
    return {name: ch for name, (ch, _prog, _vol) in ORCH.items()}


def in_range(pc, lo, hi):
    q = lo + (pc - lo) % 12
    return q if q <= hi else q - 12


def octaves(s, ch, bars, vel, lo=40, turn=None):
    """the motor: eighths leaping between the chord's bass and the octaves over it, the off-beats turning
    (octave, octave, tenth, ninth), the 3+3+2 accents louder"""
    for b in bars:
        for k in range(8):
            beat = k * 0.5
            c = chord(s.sym_at(b, beat))
            low = in_range(c.bass, lo, lo + 11)
            if k % 2 == 0:
                q = low
            else:
                third = 3 if 3 in c.tones else 4
                q = low + (turn or (12, 12, 12 + third, 14))[k // 2]
            s.play(ch, b, beat, 0.4, q, vel + (12 if beat in ACC332 else 0), 3)


def pulses(s, ch, bars, vel, lo=48):
    """eighths on the chord's bass in octaves, all alike: a call to action"""
    for b in bars:
        for k in range(8):
            q = in_range(chord(s.sym_at(b, k * 0.5)).bass, lo, lo + 11)
            s.play(ch, b, k * 0.5, 0.35, [q, q + 12], vel + (8 if k % 2 == 0 else 0), 3)


def pedal332(s, ch, bars, vel, lo=28):
    """the low strings' strokes on the 3+3+2 accents, on the chord's bass"""
    for b in bars:
        for beat in ACC332:
            q = in_range(chord(s.sym_at(b, beat)).bass, lo, lo + 11)
            s.play(ch, b, beat, 1.2 if beat < 3 else 0.8, q, vel + (8 if beat == 0 else 0), 3)


def hit(s, ch, bar, beat, vel, kick=118, timpani=None):
    """the kick and the orchestra hit together, on the chord's root (and the timpani's channel too, given)"""
    root = chord(s.sym_at(bar, beat)).root
    s.play(9, bar, beat, 0.2, DRUM['kick'], kick, 3)
    s.play(ch, bar, beat, 0.4, in_range(root, 60, 71), vel, 3)
    if timpani is not None:
        s.play(timpani, bar, beat, 0.5, in_range(root, 33, 44), vel - 16, 3)


def held(s, ch, bar, beat, dur, pitch, vel, e0=60, e1=127, e2=96):
    """a long note that swells: expression from e0 up to e1 over its first half, down to e2 by its end"""
    s.play(ch, bar, beat, dur, pitch, vel)
    steps = max(2, int(dur * 4))
    for k in range(steps + 1):
        x = k / steps
        e = e0 + (e1 - e0) * x * 2 if x <= .5 else e1 + (e2 - e1) * (x - .5) * 2
        pos = bar * s.beats + beat + dur * x - (0.05 if k == steps else 0)   # (the last inside the note: a loop
        s.cc(ch, int(pos // s.beats), pos % s.beats, 11, round(e))           # stays whole bars)


def sung(s, ch, bar0, bars, vel, longest=1.5, **swell):
    """a tune ('PITCH:BEAT:LENGTH' a bar), each note this long or longer swelling"""
    for i, text in enumerate(bars):
        for tok in text.split():
            name, beat, dur = tok.split(':')
            if float(dur) >= longest:
                held(s, ch, bar0 + i, float(beat), float(dur), name, vel, **swell)
            else:
                s.play(ch, bar0 + i, float(beat), float(dur), name, vel)


def sweep(s, ch, bar, pcs, lo, hi, vel, beats=4):
    """a harp's sweep up through these pitch classes, lo to hi, over the bar"""
    notes = [q for q in range(lo, hi + 1) if q % 12 in pcs]
    step = beats / len(notes)
    for k, q in enumerate(notes):                   # (each let go by the bar's end: a loop stays whole bars)
        at = 4 - beats + k * step
        s.play(ch, bar, at, min(step * 1.5, 3.98 - at), q, vel + k, 2)
