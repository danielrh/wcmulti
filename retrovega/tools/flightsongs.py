"""The flight's music, for our OPL3 bank (tools/oplbank.py); CC0, ours.

The user's brief (2026-10-23): many space songs, pensive but curious, like
the Vega Strike pieces they played us; a very calm one for the way home;
battle songs like terranbattle; three short victory stings and a short
tragic one; three panic songs, worse and worse as the ship falls apart.
Some half an hour of music in all, the score (src/score.c) moving between
the songs.

Their notes on the first draft (2026-10-24) made the second:

- **Cruise:** orchestral wonder, pensive and anticipatory. Strings, horns,
  choir, timpani and harp, the glass bell's eighths kept as the pulse of
  space. Builds that pay off in a climax. Nothing casual: no vibraphone
  tunes, ride cymbal or synth bass grooves.
- **Loops:** seamless. A song has a "loop" marker after its intro, and
  the engine plays the intro once and then goes round from the marker.
- **The way home:** one song that loops, starting at once, never sleepy.
  Relief, with a key change up a step for the payoff.
- **Battle:** lego pieces. Every battle and panic song is at 140 a minute
  in C, so the score can chain them on the bar, each from its loop.
- **Panic:** keeps the fight's pace with the damage layered over it.
- **Asteroid fields:** the first draft's Crossfire, given a new last third.
- **Stings:** victories with longing in them, and the loss a dirge anyone
  knows for one.

    CRUISE1  "Far Shore"         B minor, 96: the bell's eighths, choir and
                                 strings, the violins' tune, horn answers, a
                                 bridge into the theme with the orchestra, a
                                 break, and a payoff on the lydian peak
    CRUISE2  "Deep Field"        E lydian, 72: strings, the horn's long tune,
                                 a flute against it, the minor turn, a build,
                                 the theme high on violins with the choir
    CRUISE3  "Long Watch"        E minor, 100: built on the draft's flute
                                 passage over C maj7#11, D6, Bm7, Em9; piano
                                 eighths, strings, an oboe, a big return
    CRUISE4  "Quiet Instruments" C minor on a drone, 92: the ticking and the
                                 celesta's flurries, now with a payoff: the
                                 theme on horns and strings, then the oboe
    CRUISE5  "Beacon"            F sharp minor into A, 76: a string pulse,
                                 the horn's tune, the violins' passage, a
                                 climax, then the lift into A major
    CRUISE6  "Nav Point"         G dorian, 108: as the draft, with a loop
    HOMEWARD "Homeward"          E flat, 84: the piano's eighths, the horn's
                                 tune, the strings' tune, the flute's, then
                                 the climax a step up in F, and round again
    BATTLE1  "Contact"           C minor on a sus pedal, 140 (was 132)
    BATTLE2  "Break and Attack"  C minor, 140: a half-time fight, the riff,
                                 brass calls, a horn tune over a Neapolitan,
                                 a lift into A flat
    BATTLE3  "Furball"           C phrygian, 140: the riff, and a surprise
                                 at its second time: up a minor third with
                                 brass answers, then a tutti stop
    ROCKS    "Rock Field"        G minor, 120: an asteroid field (the draft's
                                 Crossfire) with a new last third
    PANIC1   "Hull Breach"       C, 140: Contact's drive under a cluster,
                                 alarms in seconds, the horn call a tritone off
    PANIC2   "Systems Failing"   C rising, 140: each build pays off in two wild
                                 bars of tritone hits before the theme
    PANIC3   "Abandon Ship"      152 into 184: panic, hope, hope dashed, hope
                                 again, dashed harder, a panic that speeds up
    VICTORY1..3, TRAGEDY         stings, played once over the battle (it goes
                                 on after them); TRAGEDY is a funeral march

The A sections' tunes are written out note by note ('PITCH:BEAT:LENGTH' a
bar); the later ones are written by tools/compose.py from their chords
(seeded: the same notes every time).
"""
import random

from compose import chord, Voicer, Bass, Tune, CALM, CALM_END
from songs import Comp, p, put

NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
DRUM = {'kick': 36, 'stick': 37, 'snare': 38, 'hat': 42, 'open': 46, 'ride': 51, 'crash': 49, 'shaker': 82,
        'tom1': 50, 'tom2': 47, 'tom3': 43, 'tom4': 41}
# a tune's rhythms for a bar (beat, length): driving ones for the battles, slow ones for the strings
DRIVE = [[(0, 1), (1, .5), (1.5, .5), (2, 1), (3, 1)], [(0, 1.5), (1.5, .5), (2, 2)],
         [(0, .5), (.5, .5), (1, 1), (2, 1.5), (3.5, .5)], [(0, 2), (2, 1), (3, 1)],
         [(0, 1), (1, 1), (2, .5), (2.5, .5), (3, 1)]]
DRIVE_END = [[(0, 4)], [(0, 3)], [(0, 2), (2, 2)]]
SLOW = [[(0, 3), (3, 1)], [(0, 2), (2, 2)], [(0, 4)], [(0, 1.5), (1.5, .5), (2, 2)], [(1, 3)]]
SLOW_END = [[(0, 4)], [(0, 3)]]


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

    def swell(self, ch, bar, bars, note, vel=60, v0=20, v1=120):
        """the noise swell (program 119) held over these bars, rising by expression"""
        self.play(ch, bar, 0, bars * 4 - 0.1, note, vel)
        self.ramp(ch, bar, bars, v0, v1)
        self.cc(ch, bar + bars, 0, 11, v0)

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


# --- CRUISE1: "Outbound" (E minor, 160): the third draft's proof piece ----------------
# Launch, open space ahead.  Three lifts of a major third (E minor, G sharp minor, C) and home to E, major this
# time; then a riser into the loop.  The second pass rescored: the call on the horns with brass stabs, a harp's
# sweep out of the break, the tune on the strings in octaves with a horn line under it, the hits on every
# accent, and the harp into the loop.

OB_PASS = (['Em', 'Cmaj7/E', 'Em', 'D/E'] + ['G#m', 'Emaj7/G#', 'G#m', 'F#/G#'] + ['Cadd9', 'D/C']
           + ['C', 'Ab', 'F', 'G', 'Em', 'C', 'Am7', 'B7'] + ['E', 'C#m', 'A', 'B'] + ['Bsus4', 'B'])
OB_CALL = ['B3:1:.5 E4:1.5:1.5 F#4:3:.5 G4:3.5:.5', 'B4:0:3 A4:3:.5 G4:3.5:.5', 'F#4:0:1.5 G4:1.5:.5 A4:2:1 B4:3:1',
           'E5:0:2 D5:2:1 B4:3:1']
OB_ANSWER = ['G3:0:4', 'G3:0:2 B3:2:2', 'B3:0:4', 'A3:0:2 F#3:2:2']
OB_TUNE = ['E4:0:3 G4:3:1', 'C5:0:3 Eb5:3:1', 'C5:0:1.5 A4:1.5:.5 F4:2:2', 'D5:0:3 B4:3:1', 'G4:0:2 B4:2:2',
           'E5:0:3 D5:3:1', 'C5:0:1.5 B4:1.5:.5 A4:2:2', 'D#5:0:4']
OB_UNDER = ['C4:0:4', 'Eb4:0:4', 'A3:0:4', 'B3:0:4', 'E4:0:4', 'G4:0:4', 'E4:0:4', 'F#4:0:4']
OB_HOME = ['B3:1:.5 E4:1.5:1.5 F#4:3:.5 G#4:3.5:.5', 'C#5:0:3 B4:3:.5 G#4:3.5:.5', 'A4:0:1.5 B4:1.5:.5 C#5:2:1 E5:3:1',
           'F#5:0:2 D#5:2:1 B4:3:1']
OB_HORNS = (['E4:0:4', 'E4:0:4', 'C#4:0:4', 'D#4:0:4'], ['B3:0:4', 'G#3:0:4', 'A3:0:4', 'B3:0:4'])
MAJOR, LYDIAN = (0, 2, 4, 5, 7, 9, 11), (0, 2, 4, 6, 7, 9, 11)


def outbound():
    form = ['Em', 'Em'] + OB_PASS + OB_PASS
    s = Piece(160, 160, form)
    ch = orchestra(s)
    mo, hi_, lo, br, hn, hn2, pc, tp, hp, st, bs = (ch[k] for k in ('motor', 'hit', 'low', 'brass', 'horn', 'horn2',
                                                                 'piccolo', 'timpani', 'harp', 'strings', 'bass'))
    n = len(form)                                   # 2 + 24 + 24 = 50 bars, 75 s
    s.loop_at(2)
    octaves(s, mo, range(0, 2), 64)                 # the intro: the motor alone, coming up, a roll into the hit
    s.ramp(mo, 0, 2, 50, 127)
    s.roll(tp, 1, 0, 4, 'B1', 8, 96)
    for P, second in ((2, False), (26, True)):
        A, A2, B, C, D, R = P, P + 4, P + 8, P + 10, P + 18, P + 22
        # the calls in E minor and G sharp minor: the motor in octaves, the low strings' strokes, the hits
        octaves(s, mo, range(A, B), 66)
        pedal332(s, lo, range(A, B), 92)
        for blk in (A, A2):
            s.crash(blk, 0, 112)
            for b in (blk, blk + 2):
                for beat in (0, 1.5):
                    hit(s, hi_, b, beat, 112, timpani=tp if beat == 0 else None)
        if not second:
            s.tune(br, A, OB_CALL, 84)
            sung(s, hn, A, OB_ANSWER, 74)
            s.tune(br, A2, [up(t, 4) for t in OB_CALL], 88)
            sung(s, hn, A2, [up(t, 4) for t in OB_ANSWER], 76)
        else:
            s.tune(hn, A, OB_CALL, 86)               # the call on the horns, an octave apart
            s.tune(hn2, A, [up(t, -12) for t in OB_CALL], 72)
            for b in range(A, A2):                   # the brass's stabs on the accents
                for beat in ACC332:
                    c = chord(s.sym_at(b, beat))
                    tones = [in_range((c.root + t) % 12, 64, 75) for t in c.tones[1:3]]
                    s.play(br, b, beat, 0.35, tones, 70 if beat else 76, 3)
            s.tune(br, A2, [up(t, 4) for t in OB_CALL], 90)
            s.tune(hn2, A2, [up(t, -8) for t in OB_CALL], 66)    # a third under (a sixth an octave down)
            s.tune(pc, A2 + 3, [up(OB_CALL[3], 16)], 54)          # the piccolo over its last bar
        # the break into C: the motor breaks the chords (or pulses), the horn holds the lydian sharp four
        bs_hold = 'C2'
        s.play(bs, B, 0, 7.8, bs_hold, 84)
        held(s, hn, B, 0, 4, 'E4', 74)
        held(s, hn, B + 1, 0, 4, 'F#4', 80)
        if not second:
            s.arp(mo, range(B, C), [0, 1, 2, 3, 2, 1, 2, 3], 0.5, 52, 76, 4, 62)
            s.run(pc, B + 1, 2, scale_run(79, 91, MAJOR), 0.25, 62, 2)      # G up to G: C lydian
        else:
            pulses(s, mo, range(B, C), 58)
            s.ramp(mo, B, 2, 70, 127)
            sweep(s, hp, B + 1, {0, 2, 4, 6, 7, 9, 11}, 48, 84, 56)
            s.roll(9, B + 1, 2, 2, DRUM['snare'], 20, 84, step=0.125)
        # the tune in C, A flat, F, G, E minor, C, A minor, B: the horn (then the strings in octaves) in long notes
        s.arp(mo, range(C, D), [0, 2, 1, 3, 2, 1, 3, 2], 0.5, 52, 76, 4, 60)
        s.bass(bs, range(C, D), [(0, 3.9, 'r')], 80, lo=28, hi=40)
        if not second:
            sung(s, hn, C, OB_TUNE, 82, e0=70, e1=124, e2=100)
        else:
            sung(s, st, C, [up(t, 12) for t in OB_TUNE], 80, e0=70, e1=124, e2=100)
            s.tune(st, C, OB_TUNE, 72)
            sung(s, hn, C, OB_UNDER, 68)
            s.arp(hp, range(C, D), [0, 1, 2, 3], 0.5, 64, 88, 4, 52)
        dim7 = [63, 66, 69, 72, 75, 78, 81, 84]                  # B7's flat nine: D sharp, F sharp, A, C up
        s.run(pc, D - 1, 2, dim7, 0.25, 60, 2)
        s.roll(9, D - 1, 2, 2, DRUM['snare'], 16, 86, step=0.125)
        s.roll(tp, D - 1, 0, 4, 'B1', 10, 92)
        # home to E, major now: the call in E, the horns in thirds under it, everything on the accents
        octaves(s, mo, range(D, R), 68)
        pedal332(s, lo, range(D, R), 96)
        s.crash(D, 0, 116)
        for b in (D, D + 2):
            for beat in (ACC332 if second else (0, 1.5)):
                hit(s, hi_, b, beat, 114, timpani=tp if beat == 0 else None)
        s.tune(br, D, OB_HOME, 90)
        sung(s, hn, D, OB_HORNS[0], 74)
        sung(s, hn2, D, OB_HORNS[1], 70)
        if not second:
            s.run(pc, D + 3, 2, scale_run(83, 95, MAJOR), 0.25, 62, 2)    # B up to B
        else:
            s.tune(pc, D + 2, [up(t, 12) for t in OB_HOME[2:]], 56)
        # the riser: the motor on B, the horns swelling, the timpani rolling up from nothing into the loop
        octaves(s, mo, range(R, R + 2), 66)
        held(s, hn, R, 0, 8, 'F#4', 76, e0=40, e1=127, e2=127)
        held(s, hn2, R, 0, 8, 'B3', 70, e0=40, e1=127, e2=127)
        s.roll(tp, R, 0, 8, 'B1', 6, 112)
        if second:
            sweep(s, hp, R + 1, {11, 3, 6}, 35, 83, 60)
            s.roll(9, R + 1, 2, 2, DRUM['snare'], 20, 90, step=0.125)
    s.end(n)
    return s


# --- CRUISE2: "Deep Field" (E lydian, 72) -------------------------------------
# New for the draft's Cartographer: "the instruments are clunky and they don't sound
# like space together; they don't have that orchestral feeling of wonder".

DF_A = ['Emaj9', 'F#/E', 'C#m9', 'Amaj7#11', 'Emaj9/G#', 'F#m9', 'Amaj7', 'Bsus4 B']
DF_B = ['C#m9', 'Amaj7', 'Emaj7/G#', 'F#m9', 'C#m9', 'Amaj7#11', 'Bsus4', 'B']
DF_BUILD = ['Amaj7', 'B', 'C#m', 'B/D#']
DF_TOP = ['Amaj7', 'B6', 'G#m7', 'C#m9', 'Amaj7#11', 'B/D#', 'Emaj9', 'Bsus4 B']
DF_REST = ['Emaj9', 'F#/E', 'C#m9', 'Bsus4 B']
DF_THEME = ['B3:0:1 E4:1:1 F#4:2:1 G#4:3:1', 'A#4:0:3 G#4:3:1', 'G#4:0:1.5 E4:1.5:.5 D#4:2:2',
            'C#4:0:2 D#4:2:1 E4:3:1', 'B4:0:3 G#4:3:1', 'A4:0:1 G#4:1:1 F#4:2:1 C#5:3:1',
            'E5:0:2 C#5:2:1 B4:3:1', 'B4:0:2 A#4:2:2']
DF_HIGH = ['E5:0:1 G#5:1:1 B5:2:1 C#6:3:1', 'D#6:0:3 B5:3:1', 'B5:0:1.5 G#5:1.5:.5 F#5:2:2',
           'G#5:0:2 E5:2:1 G#5:3:1', 'A#5:0:1 B5:1:1 C#6:2:2', 'D#6:0:2 F#6:2:2', 'E6:0:4', 'D#6:0:2 F#5:2:2']


def deep_field():
    form = DF_A[:2] + DF_A + DF_A + DF_B + DF_BUILD + DF_TOP + DF_REST
    s = Piece(72, 202, form)
    s.setup(0, 48, 70)          # strings, the chords
    s.setup(1, 98, 76)          # glass bell, the sparkle
    s.setup(2, 39, 100)         # sub bass
    s.setup(3, 60, 98)          # horn
    s.setup(4, 73, 86)          # flute
    s.setup(5, 48, 94)          # violins
    s.setup(6, 47, 90)          # timpani
    s.setup(7, 52, 70)          # choir
    s.setup(8, 119, 60)         # noise swell
    s.setup(10, 46, 80)         # harp
    n = len(form)                                   # 2 + 8 + 8 + 8 + 4 + 8 + 4 = 42
    s.loop_at(2)
    s.pad(0, range(0, n), 52, 68, 3, 48)
    s.ramp(0, 0, 2, 70, 110)
    s.arp(1, range(0, n), [0, 2, 1, 3, 4, 3, 1, 2], 0.5, 64, 83, 4, 40, accent=6)
    s.bass(2, range(2, n), [(0, 3.9, 'r')], 64)
    s.tune(3, 2, DF_THEME, 72)
    s.tune(3, 10, DF_THEME, 76)
    s.made(4, 10, 8, 'E', 'lydian', 71, 86, 56, seed=21, rhythms=SLOW, ends=SLOW_END)
    s.run(10, 17, 2, scale_run(52, 76, (0, 2, 4, 6, 7, 9, 11)), 0.125, 46, 1)
    s.made(5, 18, 8, 'C#', 'minor', 61, 76, 74, seed=22, rhythms=SLOW, ends=SLOW_END)   # the minor turn
    s.tune(3, 18, ['G#3:0:4', 'A3:0:4', 'B3:0:4', 'A3:0:4', 'G#3:0:4', 'A3:0:4', 'B3:0:4', 'A#3:0:4'], 58)
    s.pad(7, range(18, 26), 56, 71, 3, 40)
    for b in range(26, 30):                         # the build: the strings pulse, the timpani rolls
        s.arp(5, range(b, b + 1), [0, 0, 1, 1, 2, 2, 3, 3], 0.5, 52, 64, 4, 50 + (b - 26) * 6, accent=6)
    s.roll(6, 28, 0, 8, 'B1', 30, 90)
    s.swell(8, 26, 4, 'C4', 56)
    s.ramp(0, 26, 4, 100, 125)
    s.crash(30, 0, 84)
    s.tune(5, 30, DF_HIGH, 90)                      # the payoff: the violins high
    s.tune(3, 30, ['E4:0:2 C#4:2:2', 'D#4:0:4', 'B3:0:4', 'C#4:0:2 E4:2:2', 'C#4:0:4', 'F#4:0:4', 'G#4:0:4',
                   'F#4:0:2 D#4:2:2'], 78)
    s.made(4, 30, 8, 'E', 'lydian', 76, 91, 60, seed=23, rhythms=SLOW, ends=SLOW_END)
    s.pad(7, range(30, 38), 59, 76, 3, 54)
    for b in range(30, 38):
        s.play(6, b, 0, .5, [45, 47, 44, 49, 45, 47, 40, 47][b - 30] - 12, 72 if b % 2 == 0 else 58)
    s.run(10, 37, 2, scale_run(52, 76, (0, 2, 4, 6, 7, 9, 11))[::-1], 0.125, 56, -1)
    s.ramp(0, 38, 4, 120, 96)
    s.made(4, 38, 4, 'E', 'lydian', 71, 83, 50, seed=24, rhythms=SLOW, ends=SLOW_END)
    s.end(n)
    return s


# --- CRUISE3: "Long Watch" (E minor, 100) -------------------------------------
# The user: the draft's flute passage over C maj7#11, D6, Bm7, Em9 (at 0:58) was
# good: "maybe the whole song should be built around this". The ride, the synth
# bass and the vibraphone's tune turned it into a combat starter, Pokemon, a bar.
# The intro (the piano's eighths) they would hear for the way home: that is
# Homeward's now.

LW_B = ['Cmaj7#11', 'D6', 'Bm7', 'Em9', 'Cmaj7', 'D/C', 'Bm7', 'Bsus4 B']
LW_C = ['Gmaj7', 'D/F#', 'Em9', 'Cmaj9', 'Am9', 'D', 'Gmaj7', 'Bsus4 B7']
LW_BUILD = ['Cmaj7#11', 'D', 'Em', 'D/F#']
LW_OUT = ['Cmaj7#11', 'D6', 'Bm7', 'Bsus4 B']


def long_watch():
    form = LW_B[:2] + LW_B + LW_B + LW_C + LW_BUILD + LW_B + LW_OUT
    s = Piece(100, 303, form, level=1.0)
    s.setup(0, 48, 64)          # strings, the chords
    s.setup(1, 4, 70)           # electric piano, the eighths
    s.setup(2, 39, 96)          # sub bass
    s.setup(3, 73, 96)          # flute
    s.setup(4, 68, 90)          # oboe
    s.setup(5, 48, 90)          # violins and cellos
    s.setup(6, 47, 86)          # timpani
    s.setup(7, 60, 84)          # horn
    s.setup(8, 119, 58)         # noise swell
    n = len(form)                                   # 2 + 8 + 8 + 8 + 4 + 8 + 4 = 42
    s.loop_at(2)
    s.pad(0, range(0, n), 52, 69, 3, 44)
    s.arp(1, range(0, n), [0, 1, 2, 3, 1, 2, 0, 3], 0.5, 52, 72, 4, 38, accent=8)
    s.bass(2, range(2, n), [(0, 3.9, 'r')], 60)
    s.made(3, 2, 8, 'E', 'minor', 69, 84, 74, seed=31)     # the draft's flute passage, note for note
    s.made(4, 10, 8, 'E', 'minor', 64, 79, 70, seed=35)    # the oboe varies it
    s.made(5, 10, 8, 'E', 'minor', 48, 60, 56, seed=36, rhythms=SLOW, ends=SLOW_END)   # the cellos under it
    s.made(3, 18, 8, 'G', 'major', 71, 86, 70, seed=32)    # the lift into G: flute and oboe trade
    s.made(4, 18, 8, 'G', 'major', 62, 74, 56, seed=33, rhythms=SLOW, ends=SLOW_END)
    s.pad(7, range(18, 26), 55, 67, 2, 46)
    for b in range(26, 30):
        s.arp(5, range(b, b + 1), [0, 1, 2, 1, 0, 1, 2, 3], 0.5, 52, 67, 4, 46 + (b - 26) * 6, accent=6)
    s.roll(6, 28, 0, 8, 'B1', 30, 86)
    s.swell(8, 26, 4, 'C4', 54)
    s.crash(30, 0, 80)
    s.made(3, 30, 8, 'E', 'minor', 69, 84, 84, seed=31, double=[(5, -12, 76)])   # the passage again, the violins
                                                                                  # an octave under it
    s.tune(7, 30, ['E4:0:4', 'F#4:0:4', 'D4:0:4', 'E4:0:2 G4:2:2', 'E4:0:4', 'F#4:0:4', 'D4:0:4',
                   'D#4:0:4'], 70)
    for b in range(30, 38, 2):
        s.play(6, b, 0, .5, 'E2', 70)
    s.ramp(1, 38, 4, 120, 100)
    s.end(n)
    return s


# --- CRUISE4: "Quiet Instruments" (C minor on a drone, 92) ---------------------
# The user: the ticking built anticipation with no payoff; the celesta's flurry
# (at 1:00) brought something out briefly; 1:26 to 2:08 was good, then it got
# repetitive.

QI_A = ['Cm9', 'Abmaj7/C', 'Bb/C', 'Cm9', 'Fm9/C', 'Abmaj7/C', 'Gsus4/C', 'Cm']
QI_B = ['Abmaj7', 'Bb6', 'Gm7', 'Cm9', 'Fm9', 'Abmaj9', 'Gsus4', 'G']
QI_BUILD = ['Fm9', 'Abmaj9', 'Gsus4', 'G']
QI_THEME = ['G4:0:3 Bb4:3:1', 'C5:0:2 Eb5:2:2', 'D5:0:3 F5:3:1', 'Eb5:0:4', 'F5:0:2 Eb5:2:1 C5:3:1',
            'Eb5:0:3 G5:3:1', 'F5:0:2 D5:2:2', 'C5:0:4']
QI_FLURRY = 'C6:0:.25 Eb6:.25:.25 G6:.5:.25 Bb6:.75:.25 C7:1:.25 D7:1.25:.25 Eb7:1.5:.5'
QI_FLURRY_DOWN = 'G7:2:.25 Eb7:2.25:.25 D7:2.5:.25 C7:2.75:.25 Bb6:3:.25 G6:3.25:.25 Eb6:3.5:.5'


def quiet_instruments():
    form = QI_A[:2] + QI_A + QI_A + QI_A + QI_BUILD + QI_A + QI_B + QI_A[:4]
    s = Piece(92, 404, form)
    s.setup(0, 89, 66)          # warm pad, the drone's chords
    s.setup(1, 98, 80)          # glass bell ostinato
    s.setup(2, 39, 100)         # sub bass, the pedal
    s.setup(3, 85, 92)          # soft lead
    s.setup(4, 8, 70)           # celesta
    s.setup(5, 48, 70)          # strings, high
    s.setup(6, 47, 92)          # timpani
    s.setup(7, 60, 92)          # horn
    s.setup(8, 48, 90)          # violins
    s.setup(10, 68, 88)         # oboe
    s.setup(11, 52, 64)         # choir
    s.setup(12, 119, 58)        # noise swell
    n = len(form)                                   # 2 + 8 x 3 + 4 + 8 + 8 + 4 = 50
    s.loop_at(2)
    s.pad(0, range(0, n), 55, 70, 3, 46)
    s.arp(1, range(0, n), [0, 2, 4, 3, 1, 4, 2, 3], 0.5, 60, 75, 3, 42, accent=6)
    for b in range(0, n):
        if form[b] in QI_A:
            s.play(2, b, 0, 3.9, 'C2', 70)
        else:
            s.bass(2, range(b, b + 1), [(0, 3.9, 'r')], 68)
    s.tune(3, 10, QI_THEME, 68)
    s.tune(3, 18, QI_THEME, 72)                     # the part they liked: the theme with the strings
    s.pad(5, range(18, 26), 72, 86, 2, 40)
    for b in (9, 17, 25, 37, 45):                   # the flurries, more of them
        s.line(4, b, QI_FLURRY, 46)
    for b in (13, 21, 33, 41):
        s.line(4, b, QI_FLURRY_DOWN, 40)
    tick = {'hat': [(k * .25, 26 if k % 4 else 36) for k in range(16)]}
    s.drums(range(2, 26), tick)
    s.drums(range(18, 26), {'kick': [(0, 44), (.5, 34)]})
    s.drums(range(26, 30), {'hat': [(k * .125, 22 + k) for k in range(32)]})   # the ticking quickens
    s.roll(6, 28, 0, 8, 'G1', 30, 92)
    s.swell(12, 26, 4, 'C4', 56)
    s.crash(30, 0, 88)                              # the payoff: the theme broad on horns and violins
    s.tune(7, 30, [up(t, -12) for t in QI_THEME], 92)
    s.tune(8, 30, QI_THEME, 90)
    s.pad(11, range(30, 38), 55, 70, 3, 56)
    s.drums(range(30, 38), dict(tick, kick=[(0, 60), (.5, 46)]))
    for b in range(30, 38, 2):
        s.play(6, b, 0, .5, 'C2', 82)
        s.play(6, b + 1, 0, .5, 'G1', 68)
    s.made(10, 38, 8, 'Eb', 'major', 65, 79, 74, seed=43, rhythms=SLOW, ends=SLOW_END)   # something new: the oboe
    s.made(8, 38, 8, 'C', 'minor', 55, 67, 54, seed=44, rhythms=SLOW, ends=SLOW_END)
    s.drums(range(38, 46), {'kick': [(0, 46), (.5, 36)]})
    s.drums(range(46, n), tick)
    s.end(n)
    return s


# --- CRUISE5: "Beacon" (F# minor into A, 76) ------------------------------------
# The user: pleasant but forgettable; the strings' passage (1:03) could live on the
# base; the theme with the strings (1:53) was good briefly; it needs pensive,
# anticipatory music, fully orchestral, on both sides; it did not loop.

BC_A = ['F#m9', 'Dmaj7', 'A/C#', 'E', 'F#m9', 'Dmaj7', 'Bm9', 'C#sus4 C#']
BC_B = ['Dmaj7', 'E', 'C#m7', 'F#m9', 'Bm9', 'E/G#', 'Asus2', 'C#sus4']
BC_BUILD = ['Dmaj7', 'E', 'F#m', 'C#sus4 C#']
BC_C = ['Amaj7', 'E/G#', 'F#m9', 'Dmaj9', 'Amaj7/C#', 'Dmaj7', 'Bm9', 'Esus4 E']
BC_THEME = ['C#4:0:1 F#4:1:1 G#4:2:1 A4:3:1', 'C#5:0:3 A4:3:1', 'B4:0:1.5 A4:1.5:.5 E4:2:2', 'G#4:0:3 B4:3:1',
            'A4:0:1 G#4:1:1 F#4:2:1 C#5:3:1', 'E5:0:2 D5:2:1 C#5:3:1', 'D5:0:2 C#5:2:1 B4:3:1', 'F#4:0:2 F4:2:2']
BC_PINGS = ['C#7:0:2', '', 'E6:2:2', '', 'A6:0:2', '', 'F#6:2:2', '']


def beacon():
    form = BC_A[:2] + BC_A + BC_A + BC_B + BC_BUILD + BC_A + BC_C + BC_A[4:]
    s = Piece(76, 505, form)
    s.setup(0, 52, 70)          # choir (the soft pad)
    s.setup(1, 48, 74)          # strings, the pulse
    s.setup(2, 39, 96)          # sub bass
    s.setup(3, 60, 100)         # horn
    s.setup(4, 73, 84)          # flute
    s.setup(5, 48, 90)          # violins
    s.setup(6, 47, 90)          # timpani
    s.setup(7, 8, 64)           # celesta
    s.setup(8, 119, 58)         # noise swell
    n = len(form)                                   # 2 + 8 + 8 + 8 + 4 + 8 + 8 + 4 = 50
    s.loop_at(2)
    s.pad(0, range(0, n), 52, 69, 3, 48)
    s.ramp(0, 0, 2, 80, 110)
    s.arp(1, range(0, 26), [0, 0, 1, 1, 2, 2, 1, 1], 0.5, 49, 64, 3, 40, accent=6)      # the pulse: anticipation
    s.arp(1, range(30, n), [0, 0, 1, 1, 2, 2, 1, 1], 0.5, 49, 64, 3, 44, accent=6)
    s.bass(2, range(2, n), [(0, 3.9, 'r')], 66)
    s.tune(3, 2, BC_THEME, 72)
    s.tune(3, 10, BC_THEME, 76)
    s.made(4, 10, 8, 'F#', 'minor', 69, 83, 54, seed=53, rhythms=SLOW, ends=SLOW_END)
    s.tune(7, 10, BC_PINGS, 44)
    s.made(5, 18, 8, 'A', 'major', 64, 79, 70, seed=51, rhythms=SLOW, ends=SLOW_END)   # the strings' passage
    s.tune(3, 18, ['A3:0:4', 'B3:0:4', 'G#3:0:4', 'A3:0:4', 'F#3:0:4', 'G#3:0:4', 'E3:0:4', 'F3:0:4'], 56)
    s.arp(1, range(26, 30), [0, 1, 2, 3, 2, 1, 0, 1], 0.25, 49, 68, 4, 46, accent=6)
    s.roll(6, 28, 0, 8, 'C#2', 30, 90)
    s.swell(8, 26, 4, 'C4', 56)
    s.crash(30, 0, 84)
    s.tune(5, 30, [up(t, 12) for t in BC_THEME], 90)     # the theme high on the violins, the horn under
    s.tune(3, 30, BC_THEME, 84)
    for b in range(30, 38, 2):
        s.play(6, b, 0, .5, 'F#1', 76)
    s.made(5, 38, 8, 'A', 'major', 66, 81, 80, seed=52)   # the lift into A major
    s.made(4, 38, 8, 'A', 'major', 73, 86, 58, seed=54, rhythms=SLOW, ends=SLOW_END)
    s.tune(3, 38, ['C#4:0:4', 'B3:0:4', 'A3:0:4', 'F#3:0:2 A3:2:2', 'C#4:0:4', 'D4:0:4', 'B3:0:4', 'B3:0:2 G#3:2:2'], 70)
    s.tune(7, 38, BC_PINGS, 40)
    s.crash(38, 0, 60)
    s.roll(6, 45, 2, 2, 'E2', 40, 70)
    s.end(n)
    return s


# --- CRUISE6: "Nav Point" (G dorian, 108) ---------------------------------------
# (no notes from the user: as the draft, now looping past its intro)

NP_A = ['Gm9', 'F/G', 'Ebmaj7/G', 'F/G', 'Gm9', 'F/G', 'Ebmaj7', 'Dsus4 D']
NP_B = ['Cm9', 'F', 'Bbmaj7', 'Ebmaj7', 'Cm9', 'Am7b5', 'Dsus4', 'D']
NP_C = ['Ebmaj7', 'F', 'Gm7', 'Dm7', 'Ebmaj7', 'F', 'Gsus4', 'G']
NP_THEME = ['D5:0:1.5 F5:1.5:.5 G5:2:1 A5:3:1', 'C6:0:2 A5:2:1 F5:3:1', 'G5:0:3 Bb5:3:1', 'A5:0:2 F5:2:2',
            'D5:0:1.5 F5:1.5:.5 G5:2:1 Bb5:3:1', 'C6:0:1 D6:1:1 C6:2:1 A5:3:1', 'Bb5:0:2 G5:2:2', 'A5:0:2 F#5:2:2']


def nav_point():
    form = NP_A + NP_A + NP_A + NP_B + NP_C + NP_A + NP_A[:4] + NP_A + NP_B[4:]
    s = Piece(108, 606, form, level=4)
    s.setup(0, 89, 62)          # warm pad
    s.setup(1, 81, 76)          # saw pluck, sixteenths
    s.setup(2, 38, 92)          # synth bass
    s.setup(3, 68, 98)          # oboe
    s.setup(4, 45, 72)          # pizzicato
    s.setup(5, 11, 80)          # vibraphone
    n = len(form)
    s.loop_at(8)
    s.pad(0, range(0, n), 53, 70, 3, 44)
    s.arp(1, range(0, n), [0, 1, 2, 3, 2, 1, 0, 2, 3, 4, 3, 2, 1, 2, 0, 1], 0.25, 60, 79, 4, 38, accent=10)
    s.ramp(1, 0, 8, 40, 108)
    s.bass(2, range(4, 48), [(0, .5, 'r'), (.5, .5, 'r'), (1, .5, '8'), (1.5, .5, 'r'), (2.5, .5, 'r'),
                             (3, .5, '5'), (3.5, .5, '8')], 62)
    s.bass(2, range(52, n), [(0, .5, 'r'), (.5, .5, 'r'), (1, .5, '8'), (1.5, .5, 'r'), (2.5, .5, 'r'),
                             (3, .5, '5'), (3.5, .5, '8')], 62)
    s.arp(4, range(24, 48), [0, None, 2, None, 1, 3, None, 2], 0.5, 55, 72, 4, 46, accent=6)
    s.tune(3, 16, NP_THEME, 74)
    s.made(5, 24, 8, 'G', 'dorian', 67, 84, 66, seed=61)
    s.made(3, 32, 8, 'G', 'dorian', 69, 86, 72, seed=62)
    s.tune(3, 40, NP_THEME, 76)
    s.tune(3, 52, NP_THEME, 78)
    s.made(5, 52, 8, 'G', 'dorian', 62, 76, 52, seed=63, rhythms=SLOW, ends=SLOW_END)
    groove = {'kick': [(0, 64), (1.5, 50), (2.5, 56)], 'stick': hits(1, 3, v=46),
              'hat': [(k * .5, 34 if k % 2 else 42) for k in range(8)]}
    s.drums(range(8, 48), groove, fill=dict(groove, tom2=[(3, 46), (3.25, 40)], tom3=[(3.5, 44), (3.75, 40)]))
    s.drums(range(52, n), groove, fill=dict(groove, tom2=[(3, 46), (3.25, 40)], tom3=[(3.5, 44), (3.75, 40)]))
    s.end(n)
    return s


# --- HOMEWARD and HOMELOSS: the way home, won (B flat, 108 in 12/8) and lost (B flat minor, 92) -----
# The user: one song for the way home that loops and "douses the player with dopamine and relief"; then
# (2026-10-24) a second for a mission lost, "mostly just a bit slower and sadder but otherwise same".  Both are
# one piece: harp eighths flowing in 12/8 (a bar is four dotted beats), the low strings' roots and fifths, the
# horn's tune in long swelling notes.  Won, it lifts a major third into D for a brass chorale, then turns home
# through D's flat six, B flat; lost, it is in the minor, a sixth slower, and where the won one rises it sinks a
# major third, to G flat, with horns where the brass was, no hits and no piccolo.  The second pass has the tune
# on the strings in octaves with the horn under it.  Which one plays is the mission's victory points
# (src/score.c), the number the layer carries to the other players.

HW_A = ['Bb', 'D/A', 'Gm', 'Eb', 'Bb/F', 'Gb', 'Eb', 'Fsus4 F']
HW_LIFT = ['D', 'Bm', 'G', 'A']
HW_HOME = ['D', 'Bb', 'Gm', 'Fsus4 F']
HW_TUNE = ['D4:0:2 F4:2:1 Bb4:3:1', 'A4:0:3 F#4:3:1', 'G4:0:2 Bb4:2:1 D5:3:1', 'Bb4:0:2 G4:2:1 Eb4:3:1',
           'F4:0:1 G4:1:1 Bb4:2:2', 'Db5:0:3 Bb4:3:1', 'Bb4:0:2 C5:2:1 Eb5:3:1', 'Bb4:0:2 A4:2:2']
HW_UNDER = ['F3:0:4', 'F#3:0:4', 'G3:0:2 Bb3:2:2', 'G3:0:4', 'F3:0:4', 'Gb3:0:4', 'G3:0:4', 'Bb3:0:2 A3:2:2']
HW_LIFT_TUNE = ['F#5:0:3 A5:3:1', 'B5:0:2 A5:2:1 F#5:3:1', 'G5:0:1 A5:1:1 B5:2:2', 'C#6:0:3 A5:3:1']
HW_HOME_TUNE = ['D6:0:3 A5:3:1', 'D6:0:2 C6:2:1 Bb5:3:1', 'Bb5:0:2 A5:2:1 G5:3:1', 'C6:0:2 A5:2:2']
# lost: the same rhythms; the minor's third, sixth and seventh (the leading tone kept over F), and the sinking
HL_A = ['Bbm', 'F/A', 'Gb', 'Ebm', 'Bbm/F', 'Db', 'Ebm6', 'Fsus4 F']
HL_SINK = ['Gb', 'Ebm', 'Cb', 'Db']
HL_HOME = ['Gb', 'Ebm', 'Cm7b5', 'Fsus4 F']
HL_TUNE = ['Db4:0:2 F4:2:1 Bb4:3:1', 'A4:0:3 F4:3:1', 'Gb4:0:2 Bb4:2:1 Db5:3:1', 'Bb4:0:2 Gb4:2:1 Eb4:3:1',
           'F4:0:1 Gb4:1:1 Bb4:2:2', 'Db5:0:3 Ab4:3:1', 'Bb4:0:2 C5:2:1 Eb5:3:1', 'Bb4:0:2 A4:2:2']
HL_UNDER = ['F3:0:4', 'F3:0:4', 'Gb3:0:2 Bb3:2:2', 'Gb3:0:4', 'F3:0:4', 'F3:0:4', 'Gb3:0:4', 'Bb3:0:2 A3:2:2']
HL_SINK_TUNE = [up(t, -8) for t in HW_LIFT_TUNE]                   # the lift's tune, in G flat
HL_HOME_TUNE = ['Db5:0:3 Bb4:3:1', 'Db5:0:2 C5:2:1 Bb4:3:1', 'Bb4:0:2 Ab4:2:1 Gb4:3:1', 'C5:0:2 A4:2:2']


def homeward(lost=False):
    A, LIFT, HOME = (HL_A, HL_SINK, HL_HOME) if lost else (HW_A, HW_LIFT, HW_HOME)
    tune, under = (HL_TUNE, HL_UNDER) if lost else (HW_TUNE, HW_UNDER)
    lift_tune, home_tune = (HL_SINK_TUNE, HL_HOME_TUNE) if lost else (HW_LIFT_TUNE, HW_HOME_TUNE)
    soft = -8 if lost else 0
    form = A[:1] + A + LIFT + HOME + A + LIFT + HOME
    s = Piece(92 if lost else 108, 808 if lost else 707, form, level=-2.0 if lost else 0.0)
    ch = orchestra(s)
    hn, hn2, br, pc, tp, hp, st, bs, mo = (ch[k] for k in ('horn', 'horn2', 'brass', 'piccolo', 'timpani', 'harp',
                                                         'strings', 'bass', 'motor'))
    third = 1 / 3
    n = len(form)                                   # 1 + 16 + 16 = 33 bars: 73 s won, 86 s lost
    s.loop_at(1)
    flow = [0, 1, 2, 3, 2, 1]                       # the harp's eighths: up the chord and back, half a bar
    s.arp(hp, range(0, n), flow, third, 53, 77, 4, 56 + soft, accent=6)
    s.bass(bs, range(1, n), [(0, 1.9, 'r'), (2, 1.9, '5')], 76 + soft, lo=29, hi=41)
    s.play(tp, 0, 0, 1, 'Bb1', 70 + soft)
    for P, second in ((1, False), (17, True)):
        L, H = P + 8, P + 12
        # the tune: the horn's, then the strings' in octaves with the horn under them
        if not second:
            sung(s, hn, P, tune, 80 + soft, e0=66, e1=124, e2=100)
        else:
            sung(s, st, P, [up(t, 12) for t in tune], 74 + soft, e0=66, e1=122, e2=100)
            s.tune(st, P, tune, 66 + soft)
            sung(s, hn, P, under, 68 + soft)
            for b in range(P, L):                    # the spiccato on the beats, under it all
                for beat in range(4):
                    q = in_range(chord(s.sym_at(b, beat)).bass, 41, 52)
                    s.play(mo, b, beat, 0.4, q, 54 + soft + (8 if beat == 0 else 0), 3)
        s.roll(tp, L - 1, 2, 2, 'F1', 10, 84 + soft)
        # the lift into D (or the sink into G flat): a brass chorale (horns, lost), the tune high on strings
        if lost:
            s.pad(hn2, range(L, H), 53, 67, 3, 66)
        else:
            s.crash(L, 0, 104)
            hit(s, ch['hit'], L, 0, 104, timpani=tp)
            s.pad(br, range(L, H), 57, 72, 3, 74)
        sung(s, st, L, lift_tune, 84 + soft, e0=80, e1=127, e2=110)
        s.tune(hn, L, [up(t, -12) for t in lift_tune], 74 + soft)
        if second and not lost:
            s.run(pc, H - 1, 2, scale_run(81, 93, MAJOR), 0.25, 58, 2)   # A up to A
        # and home: D to B flat (G flat to E flat minor), round to F, a roll and (the second time) the harp
        sung(s, st, H, home_tune, 82 + soft, e0=80, e1=124, e2=104)
        s.tune(hn, H, [up(t, -12) for t in home_tune], 70 + soft)
        if lost:
            s.pad(hn2, range(H, H + 4), 53, 67, 3, 60)
        else:
            s.pad(br, range(H, H + 3), 57, 72, 3, 64)
        s.roll(tp, H + 3, 0, 4, 'F1', 6, 100 + soft, step=third / 2)
        if second:
            sweep(s, hp, H + 3, {5, 9, 0}, 41, 89, 56 + soft)
    s.end(n)
    return s


def homeloss():
    return homeward(lost=True)


# --- BATTLE1: "Contact" (C minor, 140) ------------------------------------------
# The user: perfect, but it should loop: its intro once, the rest round again.

CT_A = ['Csus4 Cm', 'Csus4 Cm', 'Ab Bb', 'Csus4 Cm', 'Csus4 Cm', 'Csus4 Cm', 'Fm Ab', 'Gsus4 G']
CT_B = ['Ab', 'Bb', 'Cm', 'Cm', 'Ab', 'Bb', 'Gsus4', 'G']
CT_C = ['Fm9', 'Bb', 'Ebmaj7', 'Abmaj7', 'Fm9', 'Gm', 'Absus2', 'Bbsus4 Bb']
CT_CALL = ['C4:0:1.5 G4:1.5:.5 G4:2:2', 'Ab4:0:1 G4:1:1 F4:2:1 Eb4:3:1', 'C4:0:2 D4:2:2', 'Eb4:0:4',
           'G4:0:1.5 C5:1.5:.5 C5:2:2', 'D5:0:1 C5:1:1 Bb4:2:1 G4:3:1', 'Ab4:0:2 C5:2:2', 'B4:0:2 D5:2:2']
ROCK = {'kick': [(0, 84), (.75, 64), (2, 80), (2.5, 66)], 'snare': hits(1, 3, v=76),
        'hat': [(k * .5, 50 if k % 2 else 60) for k in range(8)]}
ROCK_FILL = {'kick': [(0, 84), (2, 80)], 'snare': [(1, 76), (3, 70), (3.25, 64), (3.5, 70), (3.75, 76)],
             'tom1': [(2, 70)], 'tom2': [(2.25, 68)], 'tom3': [(2.5, 68)], 'tom4': [(2.75, 70)]}
BATTLE_BPM = 140


def contact():
    form = CT_A[:4] + CT_A + CT_A + CT_B + CT_C + CT_A + CT_A[:4] + CT_A + CT_B[4:]
    s = Piece(BATTLE_BPM, 909, form)
    s.setup(0, 48, 76)          # strings
    s.setup(1, 84, 70)          # short pluck, sixteenths
    s.setup(2, 38, 100)         # synth bass
    s.setup(3, 61, 84)          # brass, the hits
    s.setup(4, 60, 104)         # horn, the call
    s.setup(5, 56, 92)          # trumpet
    s.setup(6, 47, 96)          # timpani
    n = len(form)
    s.loop_at(4)
    s.bass(2, range(0, n), [(k * .5, .45, w) for k, w in enumerate('rr8r5r8r')], 70)
    s.pad(0, range(4, n), 55, 72, 3, 56)
    s.arp(1, range(4, 44), [0, 1, 2, 1, 0, 1, 2, 3], 0.25, 60, 79, 4, 40, accent=10)
    s.arp(1, range(48, n), [0, 1, 2, 1, 0, 1, 2, 3], 0.25, 60, 79, 4, 42, accent=10)
    for b in range(12, 44):
        for beat in (0, 1.5, 3):
            v = Voicer(60, 74, 3)
            s.play(3, b, beat, .35, v.next(chord(s.sym_at(b, beat))), 70 if beat == 0 else 60)
    s.tune(4, 12, CT_CALL, 84)
    s.made(5, 20, 8, 'C', 'minor', 67, 81, 78, seed=91, rhythms=DRIVE, ends=DRIVE_END)
    s.made(4, 28, 8, 'Eb', 'major', 58, 74, 82, seed=92, rhythms=DRIVE, ends=DRIVE_END)
    s.tune(4, 36, CT_CALL, 88)
    s.tune(5, 36, [up(t, 12) for t in CT_CALL], 70)              # the trumpet an octave over the horn
    s.tune(4, 48, CT_CALL, 90)
    s.made(5, 48, 8, 'C', 'minor', 72, 84, 70, seed=93, rhythms=DRIVE, ends=DRIVE_END)
    for b in range(0, n, 4):
        s.line(6, b, 'C2:0:.5 C2:2:.5 G1:3:.25 G1:3.5:.25', 80)
    s.drums(range(4, 44), ROCK, fill=ROCK_FILL)
    s.drums(range(44, 48), {'kick': hits(0, 2, v=70), 'hat': hits(*[k * .5 for k in range(8)], v=40)})
    s.drums(range(48, n), ROCK, fill=ROCK_FILL)
    for b in (4, 12, 20, 28, 36, 48):
        s.crash(b, 0, 80)
    s.end(n)
    return s


# --- BATTLE2: "Break and Attack" (C minor, 140) ---------------------------------
# The user: too repetitive, more racing than fighting; good around 0:34 (the riff's
# second half); the riff not set apart enough; the break fine and short.  Now a
# half-time fight under the riff, brass calls and answers, a horn tune over a
# Neapolitan (D flat), a lift into A flat, and the riff back varied.

BA_A = ['Cm', 'Ab', 'Bb', 'Gsus4 G', 'Cm', 'Ab', 'Fm', 'Gsus4 G']
BA_B = ['Ab', 'Bb', 'Cm', 'Eb', 'Fm', 'Ab', 'Bbsus4 Bb', 'Gsus4 G']
BA_C = ['Fm', 'Cm', 'Ab', 'G', 'Fm', 'Cm', 'Db', 'G']
BA_D = ['Ab', 'Bb', 'Cm', 'Ab', 'Fm', 'Bb', 'Gsus4', 'G']
BA_RIFF = [up(t, -2) for t in ['D5:0:.5 F5:.5:.5 A5:1:1 G5:2:.5 F5:2.5:.5 E5:3:1', 'F5:0:1.5 D5:1.5:.5 F5:2:1 Bb5:3:1',
                                'A5:0:1 G5:1:1 E5:2:1 C5:3:1', 'E5:0:3 C#5:3:1',
                                'D5:0:.5 F5:.5:.5 A5:1:1 G5:2:.5 F5:2.5:.5 E5:3:1', 'F5:0:1 A5:1:1 D6:2:2',
                                'C6:0:1 Bb5:1:1 G5:2:1 Bb5:3:1', 'A5:0:4']]
BA_CALL = ['C5:0:.5 C5:.5:.5 Eb5:1:1', '', 'D5:0:.5 D5:.5:.5 F5:1:1', '', 'Eb5:0:.5 Eb5:.5:.5 G5:1:1', '',
           'F5:0:.5 Eb5:.5:.5 D5:1:1 B4:2:1', '']
BA_ANSWER = ['', 'Ab4:0:1 Bb4:1:1 C5:2:2', '', 'Bb4:0:1 C5:1:1 D5:2:2', '', 'C5:0:1 D5:1:1 Eb5:2:2', '', 'D5:0:4']
BA_HORN = ['F4:0:1.5 Ab4:1.5:.5 C5:2:2', 'Eb5:0:1 D5:1:1 C5:2:2', 'Eb5:0:1.5 F5:1.5:.5 Eb5:2:1 C5:3:1', 'B4:0:4',
           'F4:0:1.5 Ab4:1.5:.5 C5:2:2', 'G5:0:1 F5:1:1 Eb5:2:1 C5:3:1', 'Db5:0:2 F5:2:2', 'D5:0:2 B4:2:2']
HALF = {'kick': [(0, 86), (1.5, 72), (2.5, 78)], 'snare': [(2, 82)], 'hat': [(k * .5, 46 if k % 2 else 56) for k in range(8)]}
HALF_FILL = {'kick': [(0, 86)], 'snare': [(2, 82), (3, 66), (3.25, 70), (3.5, 74), (3.75, 80)],
             'tom2': [(2.5, 70)], 'tom4': [(2.75, 72)]}
FULL = {'kick': [(0, 84), (.75, 64), (2, 80), (2.5, 66)], 'snare': hits(1, 3, v=78),
        'hat': [(k * .5, 50 if k % 2 else 60) for k in range(8)]}


def break_and_attack():
    form = BA_A + BA_B + BA_C + BA_A + BA_A[:2] + BA_D
    s = Piece(BATTLE_BPM, 1010, form)
    s.setup(0, 48, 70)          # strings
    s.setup(1, 81, 66)          # saw pluck
    s.setup(2, 38, 100)         # synth bass
    s.setup(3, 80, 92)          # synth lead, the riff
    s.setup(4, 61, 88)          # brass, the calls
    s.setup(5, 84, 60)          # short pluck, sixteenths
    s.setup(6, 47, 96)          # timpani
    s.setup(7, 60, 100)         # horn
    s.setup(8, 56, 94)          # trumpet
    n = len(form)                                   # 8 + 8 + 8 + 8 + 2 + 8 = 42
    s.pad(0, range(0, n), 55, 72, 3, 52)
    s.bass(2, range(0, n), [(k * .5, .45, w) for k, w in enumerate('rr8rr58r')], 70)
    s.arp(1, range(0, 8), [0, 2, 1, 3, 2, 4, 3, 2], 0.25, 62, 81, 4, 36, accent=10)
    s.tune(3, 0, BA_RIFF, 80)
    for b in range(0, 8):
        s.play(4, b, 1.5, .3, Voicer(60, 72, 3).next(chord(s.sym_at(b, 1.5))), 62)
        s.play(4, b, 3.5, .3, Voicer(60, 72, 3).next(chord(s.sym_at(b, 3.5))), 58)
    s.tune(4, 8, BA_CALL, 86)                       # the brass call, the strings answer
    s.tune(8, 8, [up(t, 0) if t else '' for t in BA_ANSWER], 70)
    for b in range(8, 16, 2):
        s.line(6, b, 'C2:0:.5 C2:.5:.25 C2:.75:.25 G1:1:.5', 80)
    s.arp(5, range(16, 24), [0, 1, 2, 1, 3, 1, 2, 1], 0.25, 60, 79, 4, 44, accent=8)
    s.tune(7, 16, BA_HORN, 90)
    for b in range(16, 24):
        s.play(6, b, 0, .4, [41, 36, 44, 43, 41, 36, 37, 43][b - 16], 76)
    s.tune(3, 24, BA_RIFF[:4] + [up(t, 12) for t in BA_RIFF[4:]], 84)    # the riff, its second half up
    s.tune(8, 28, [up(t, 0) for t in BA_RIFF[4:]], 66)
    s.arp(1, range(24, 32), [0, 2, 1, 3, 2, 4, 3, 2], 0.25, 62, 81, 4, 40, accent=10)
    s.drums(range(32, 34), {'snare': [(k * .25, 50 + k * 2) for k in range(16)], 'kick': hits(0, 1, 2, 3, v=74)})
    s.crash(34, 0, 86)
    s.made(8, 34, 8, 'Ab', 'major', 67, 82, 86, seed=103, rhythms=DRIVE, ends=DRIVE_END)   # the lift into A flat
    s.tune(7, 34, ['Ab4:0:4', 'Bb4:0:4', 'G4:0:4', 'Ab4:0:4', 'F4:0:4', 'F4:0:4', 'G4:0:4', 'G4:0:2 B4:2:2'], 78)
    s.arp(5, range(34, n), [0, 1, 2, 3, 2, 1, 0, 1], 0.25, 60, 79, 4, 44, accent=8)
    s.drums(range(0, 16), HALF, fill=HALF_FILL)
    s.drums(range(16, 32), FULL, fill=ROCK_FILL)
    s.drums(range(34, n), FULL, fill=ROCK_FILL)
    for b in (0, 8, 16, 24):
        s.crash(b, 0, 80)
    s.end(n)
    return s


# --- BATTLE3: "Furball" (C phrygian, 140) ---------------------------------------
# The user: nearly perfect, but it must fit the others (their tempo, their key: it was
# E phrygian at 152); the riff's second time (1:05) repetitive: a surprise there.

FB_A = ['C5', 'Db5', 'C5', 'Eb5 Db5', 'C5', 'Db5', 'Bb5', 'Ab5 G5']
FB_B = ['F5', 'Eb5', 'Db5', 'C5', 'F5', 'Ab5', 'Bb5', 'Db5 C5']
FB_UP = ['Eb5', 'E5', 'Eb5', 'Gb5 E5']
FB_RIFF = [up(t, -4) for t in ['E5:0:.5 F5:.5:.5 E5:1:.5 B4:1.5:.5 E5:2:1 G5:3:1', 'F5:0:1 E5:1:.5 D5:1.5:.5 C5:2:2',
                                'B4:0:.5 E5:.5:.5 G5:1:.5 B5:1.5:.5 A5:2:1 G5:3:1', 'F5:0:2 E5:2:2',
                                'E5:0:.5 F5:.5:.5 E5:1:.5 B4:1.5:.5 E5:2:1 G5:3:1', 'A5:0:1 G5:1:.5 F5:1.5:.5 E5:2:2',
                                'D5:0:1 F5:1:1 A5:2:1 D6:3:1', 'C6:0:2 B5:2:2']]
HEAVY = {'kick': [(0, 88), (.5, 70), (1.5, 74), (2, 86), (2.5, 70), (3.5, 74)], 'snare': hits(1, 3, v=80),
         'hat': [(k * .5, 54 if k % 2 else 64) for k in range(8)]}
HEAVY_FILL = {'kick': hits(0, 2, v=88), 'snare': [(1, 80), (2.5, 70), (2.75, 74)],
              'tom1': [(3, 78)], 'tom2': [(3.25, 76)], 'tom3': [(3.5, 76)], 'tom4': [(3.75, 80)]}


def furball():
    form = FB_A[:4] + FB_A + FB_A + FB_B + FB_A + FB_A[:4] + FB_UP + FB_A[:4] + FB_B
    s = Piece(BATTLE_BPM, 1111, form)
    s.setup(0, 30, 74)          # overdriven guitar, fifths
    s.setup(1, 84, 64)          # short pluck
    s.setup(2, 38, 100)         # synth bass
    s.setup(3, 80, 94)          # synth lead
    s.setup(4, 61, 84)          # brass
    s.setup(5, 48, 64)          # strings
    s.setup(6, 47, 96)          # timpani
    n = len(form)                                   # 4 + 8 + 8 + 8 + 8 + 4 + 4 + 4 + 8 = 56
    s.loop_at(4)
    for b, beat, dur, sym in s.segs(range(0, n)):
        c = chord(sym)
        root = 41 + (c.root - 5) % 12
        for k in range(int(dur / .5)):
            pos = beat + k * .5
            if 44 <= b < 46 and pos not in (0, 2):  # the stop: the band on the beats only
                continue
            s.play(0, b, pos, .4, [root, root + 7], 78 if k == 0 else 62)
    s.bass(2, range(0, 44), [(k * .5, .4, 'r') for k in range(8)], 70)
    s.bass(2, range(44, 46), [(0, 1, 'r'), (2, 1, 'r')], 76)
    s.bass(2, range(46, n), [(k * .5, .4, 'r') for k in range(8)], 70)
    s.arp(1, range(12, 44), [0, 1, 2, 1, 3, 1, 2, 1], 0.25, 64, 83, 3, 36, accent=8)
    s.arp(1, range(46, n), [0, 1, 2, 1, 3, 1, 2, 1], 0.25, 64, 83, 3, 36, accent=8)
    s.tune(3, 12, FB_RIFF, 84)
    s.pad(5, range(20, 28), 64, 79, 3, 52)
    s.made(3, 20, 8, 'C', 'minor', 63, 80, 80, seed=111, rhythms=DRIVE, ends=DRIVE_END)
    for b in range(20, 28):
        v = Voicer(59, 74, 3)
        s.play(4, b, 0, .5, v.next(chord(s.sym_at(b, 0))), 74)
        s.play(4, b, 2.5, .5, v.next(chord(s.sym_at(b, 2.5))), 68)
    s.tune(3, 28, FB_RIFF, 86)
    # the surprise: the riff's second time a minor third up, the brass answering, then the band stops on
    # the beats, a tom run, and the riff home
    s.tune(3, 40, [up(t, 3) for t in FB_RIFF[:4]], 90)
    for b in range(40, 44):
        for beat, dur, v in ((2.5, .25, 80), (3, .25, 84), (3.5, .5, 88)):
            q = 60 + chord(s.sym_at(b, beat)).root % 12
            s.play(4, b, beat, dur, [q, q + 7, q + 12], v)
    s.crash(40, 0, 88)
    for b in (44, 45):
        s.play(4, b, 0, .5, ['C4', 'Gb4', 'C5'], 92)
        s.play(4, b, 2, .5, ['Db4', 'G4', 'Db5'], 92)
        s.play(6, b, 0, .5, 'C2', 90)
        s.play(6, b, 2, .5, 'Db2', 90)
    s.drums(range(44, 46), {'kick': hits(0, 2, v=90), 'crash': [(0, 80)]})
    s.drums(range(45, 46), {'tom1': [(3, 80)], 'tom2': [(3.25, 78)], 'tom3': [(3.5, 78)], 'tom4': [(3.75, 82)]})
    s.tune(3, 46, FB_RIFF[2:4], 88)
    s.made(3, 48, 8, 'C', 'minor', 63, 82, 84, seed=112, rhythms=DRIVE, ends=DRIVE_END)
    s.drums(range(4, 36), HEAVY, fill=HEAVY_FILL)
    s.drums(range(36, 40), {'kick': hits(0, 1, 2, 3, v=76), 'snare': [(k * .25, 40 + k * 3) for k in range(16)]})
    s.drums(range(40, 44), HEAVY)
    s.drums(range(46, n), HEAVY, fill=HEAVY_FILL)
    for b in (4, 12, 20, 28, 46, 48):
        s.crash(b, 0, 84)
    s.end(n)
    return s


# --- ROCKS: "Rock Field" (G minor, 120): an asteroid field --------------------------
# The draft's Crossfire: "a lethargic battle, but maybe for asteroids", with more
# difference toward its end (1:28): now a drifting passage in B flat, the theme with a
# trumpet descant, and round again past its intro.

CF_A = ['Gm', 'Eb', 'Cm', 'D', 'Gm', 'Bb', 'Eb Cm', 'D7']
CF_B = ['Eb', 'F', 'Bb', 'Gm', 'Cm', 'D', 'Eb', 'Dsus4 D']
CF_DRIFT = ['Bbmaj7', 'Gm9', 'Ebmaj7', 'F6', 'Bbmaj7', 'Dm7', 'Ebmaj7#11', 'Dsus4 D']
CF_THEME = ['G4:0:1.5 Bb4:1.5:.5 D5:2:2', 'Eb5:0:1 D5:1:1 Bb4:2:1 G4:3:1', 'C5:0:2 Eb5:2:2', 'D5:0:4',
            'G5:0:1.5 F5:1.5:.5 D5:2:2', 'F5:0:1 D5:1:1 Bb4:2:2', 'Eb5:0:1 D5:1:1 C5:2:1 Eb5:3:1', 'D5:0:4']
CF_DESCANT = ['D6:0:4', 'Eb6:0:2 D6:2:2', 'C6:0:4', 'A5:0:4', 'Bb5:0:4', 'D6:0:2 Bb5:2:2', 'G5:0:2 C6:2:2', 'A5:0:4']
MARCH = {'snare': [(0, 50), (.75, 44), (1, 66), (2, 50), (2.75, 44), (3, 66), (3.5, 50)],
         'kick': hits(0, 2, v=70)}
MARCH_FILL = {'snare': [(k * .25, 46 + k * 2) for k in range(16)], 'kick': hits(0, v=70)}


def rock_field():
    form = CF_A[:4] + CF_A + CF_A + CF_B + CF_A + CF_B + CF_DRIFT + CF_A
    s = Piece(120, 1212, form)
    s.setup(0, 48, 76)          # strings
    s.setup(1, 45, 86)          # pizzicato, the ostinato
    s.setup(2, 32, 100)         # upright bass, plucked low
    s.setup(3, 60, 104)         # horn
    s.setup(4, 56, 92)          # trumpet
    s.setup(5, 61, 80)          # brass, the chords
    s.setup(6, 47, 100)         # timpani
    s.setup(7, 98, 70)          # glass bell: the rocks glinting
    s.setup(8, 8, 66)           # celesta
    n = len(form)                                   # 4 + 8 x 5 + 8 + 8 = 60
    s.loop_at(4)
    s.pad(0, range(0, n), 55, 72, 3, 54)
    s.arp(1, range(0, 44), [0, 0, 2, 0, 1, 0, 2, 3], 0.5, 55, 74, 4, 56, accent=12)
    s.arp(1, range(52, n), [0, 0, 2, 0, 1, 0, 2, 3], 0.5, 55, 74, 4, 56, accent=12)
    s.bass(2, range(0, 44), [(0, .5, 'r'), (1, .5, 'r'), (2, .5, 'r'), (2.5, .5, '5'), (3, .5, 'r')], 72)
    s.bass(2, range(44, 52), [(0, 3.5, 'r')], 64)
    s.bass(2, range(52, n), [(0, .5, 'r'), (1, .5, 'r'), (2, .5, 'r'), (2.5, .5, '5'), (3, .5, 'r')], 72)
    s.tune(3, 4, CF_THEME, 86)
    s.tune(3, 12, CF_THEME, 88)
    s.made(4, 12, 8, 'G', 'minor', 70, 82, 60, seed=121, rhythms=SLOW, ends=SLOW_END)
    s.made(4, 20, 8, 'Bb', 'major', 65, 81, 82, seed=122, rhythms=DRIVE, ends=DRIVE_END)
    for b in range(20, 28):
        s.play(5, b, 0, 1.8, Voicer(58, 72, 3).next(chord(s.sym_at(b, 0))), 66)
    s.tune(3, 28, CF_THEME, 90)
    s.made(4, 36, 8, 'Bb', 'major', 67, 82, 84, seed=123, rhythms=DRIVE, ends=DRIVE_END)
    # the drift: the rocks turning past, slowly
    s.arp(7, range(44, 52), [0, 2, 3, 1, 4, 2, 5, 3], 0.5, 67, 86, 4, 44, accent=6)
    s.made(3, 44, 8, 'Bb', 'major', 58, 72, 74, seed=124, rhythms=SLOW, ends=SLOW_END)
    s.line(8, 47, QI_FLURRY, 44)
    s.line(8, 51, QI_FLURRY_DOWN, 40)
    s.roll(6, 44, 0, 4, 'Bb1', 30, 56)
    s.roll(6, 50, 0, 8, 'D2', 30, 86)
    s.crash(52, 0, 80)
    s.tune(3, 52, CF_THEME, 92)                     # the theme, a trumpet descant over it
    s.tune(4, 52, CF_DESCANT, 80)
    for b in range(0, 44):
        r = 43 + (chord(s.sym_at(b, 0)).root - 7) % 12
        r = r - 12 if r > 50 else r
        s.play(6, b, 0, .5, r, 70 if b % 2 == 0 else 56)
        if b % 8 == 7:
            s.line(6, b, ' '.join('D2:%g:.25' % (2 + k * .25) for k in range(8)), 64)
    for b in range(52, n):
        r = 43 + (chord(s.sym_at(b, 0)).root - 7) % 12
        r = r - 12 if r > 50 else r
        s.play(6, b, 0, .5, r, 72 if b % 2 == 0 else 58)
    s.drums(range(4, 44), MARCH, fill=MARCH_FILL)
    s.drums(range(52, n), MARCH, fill=MARCH_FILL)
    for b in (4, 12, 20, 28, 36):
        s.crash(b, 0, 74)
    s.end(n)
    return s


# --- PANIC1..3: "Hull Breach", "Systems Failing", "Abandon Ship" -----------------

def hull_breach():
    """the fight goes on (Contact's drive: its bass, its beat, its sixteenths) and the damage over it: a cluster
    in the low strings, alarms in seconds, the horn's call a tritone off, a bell's tritones"""
    form = ['Cm', 'Db/C', 'Cm', 'Gb/C'] * 8
    s = Piece(BATTLE_BPM, 1313, form)
    s.setup(0, 48, 76)          # strings, low
    s.setup(1, 84, 66)          # short pluck
    s.setup(2, 38, 104)         # synth bass
    s.setup(3, 61, 88)          # brass, the alarms
    s.setup(4, 60, 96)          # horn
    s.setup(5, 98, 66)          # glass bell
    s.setup(6, 47, 96)          # timpani
    s.setup(7, 119, 60)         # noise swell
    n = len(form)                                   # 32
    s.bass(2, range(0, n), [(k * .5, .45, w) for k, w in enumerate('rr8r5r8r')], 72, pedal=36)
    s.arp(1, range(0, n), [0, 1, 2, 1, 0, 1, 2, 3], 0.25, 60, 79, 4, 40, accent=10)
    for b in range(n):
        s.play(0, b, 0, 3.9, ['C3', 'Db3', 'G3'] if b % 2 == 0 else ['C3', 'Db3', 'Gb3'], 54)
        if b % 4 == 3:
            s.ramp(0, b, 1, 80, 120)
        if b >= 8 and b % 2 == 1:
            s.play(3, b, 1.5, .4, ['C5', 'Db5'], 78)
            s.play(3, b, 3, .4, ['C5', 'Db5'], 72)
        if b >= 16 and b % 4 == 2:
            s.play(5, b, 0, .4, 'F#6', 44)
            s.play(5, b, 1, .4, 'C6', 40)
    s.tune(4, 8, [up(t, 6) for t in CT_CALL[:4]], 82)          # the call, a tritone off
    s.tune(4, 24, [up(t, 6) for t in CT_CALL[4:]], 86)
    for b in range(0, n, 4):
        s.line(6, b, 'C2:0:.5 C2:2:.5 Gb1:3:.25 Gb1:3.5:.25', 80)
    for b in (6, 14, 22, 30):
        s.swell(7, b, 2, 'C4', 56)
    s.drums(range(0, n), ROCK, fill=ROCK_FILL)
    for b in (0, 8, 16, 24):
        s.crash(b, 0, 80)
    s.end(n)
    return s


def systems_failing():
    """the bass climbs by half steps; each build (the noise swell) pays off in two wild bars, tritone hits on
    every eighth, before the theme comes back"""
    steps = ['C', 'Db', 'D', 'Eb', 'E', 'Eb', 'D', 'Db']
    form = [steps[(b // 2) % 8] + 'm' for b in range(48)]
    s = Piece(BATTLE_BPM, 1414, form)
    s.setup(0, 48, 78)          # strings
    s.setup(1, 84, 70)          # short pluck
    s.setup(2, 38, 104)         # synth bass
    s.setup(3, 61, 88)          # brass, the alarms
    s.setup(4, 119, 66)         # noise swell
    s.setup(5, 47, 96)          # timpani
    s.setup(6, 56, 94)          # trumpet
    n = len(form)                                   # 48: six builds of eight bars, the last two of each wild
    wild = set(b for b in range(n) if b % 8 in (6, 7))
    for b in range(n):
        r = chord(form[b]).root
        if b in wild:
            for k in range(8):
                s.play(2, b, k * .5, .4, 36 + r + (6 if k % 2 else 0), 80)
                s.play(3, b, k * .5, .35, [60 + r, 66 + r, 72 + r], 74 + (k % 2) * 8)
                s.play(5, b, k * .5, .3, 36 + r, 70 + (k % 2) * 10)
            s.play(0, b, 0, 3.9, [72 + r, 73 + r, 78 + r], 60)
            s.run(6, b, 0, [72 + r + k for k in range(0, 13, 2)] + [84 + r], .25, 70, 2) if b % 8 == 7 else None
            continue
        s.bass(2, range(b, b + 1), [(k * .5, .45, 'r') for k in range(8)], 72)
        s.arp(1, range(b, b + 1), [0, 1, 2, 1], 0.25, 64, 79, 3, 44, accent=8)
        s.play(0, b, 0, 3.9, [60 + r, 61 + r, 66 + r], 50)
        if b % 2 == 1:
            s.play(3, b, 1.5, .4, [72 + r, 73 + r], 78)
            s.play(3, b, 3, .4, [72 + r, 73 + r], 72)
        if b % 4 == 3:
            s.line(5, b, 'C2:2:.25 C2:2.25:.25 C2:2.5:.25 C2:2.75:.25 C2:3:.25 C2:3.25:.25 C2:3.5:.25 C2:3.75:.25', 70)
    for b in range(2, n, 8):
        s.swell(4, b, 4, 'C4', 60)
    beat = {'kick': hits(0, 1, 2, 3, v=74), 'snare': hits(1, 3, v=64), 'hat': [(k * .5, 50) for k in range(8)],
            'tom3': [(2.5, 56)], 'tom4': [(3.5, 60)]}
    rush = {'kick': [(k * .5, 84) for k in range(8)], 'snare': [(k * .25, 60 + (k % 4 == 0) * 20) for k in range(16)]}
    for b in range(n):
        s.drums(range(b, b + 1), rush if b in wild else beat)
        if b % 8 == 6:
            s.crash(b, 0, 92)
    s.end(n)
    return s


AS_PANIC = ['Cm', 'Gb', 'Cm', 'Gb', 'Db', 'G', 'Ab', 'D']
AS_HOPE = ['C', 'G/B', 'Am', 'F', 'C', 'G', 'Ab', 'Bb']
AS_HOPE2 = ['D', 'A/C#', 'Bm', 'G', 'D', 'A', 'Bb', 'C']
AS_DASH = ['Gb', 'F', 'E', 'Eb']


def to_major(text):
    """C minor's third, sixth and seventh raised: the call in C major"""
    out = []
    for tok in text.split():
        name, beat, dur = tok.split(':')
        q = p(name)
        q += 1 if q % 12 in (3, 8, 10) else 0
        out.append('%s%d:%s:%s' % (NAMES[q % 12], q // 12 - 1, beat, dur))
    return ' '.join(out)


def accelerando(bpm0, beats, bars_saved):
    """the tempo each beat of a speed-up from bpm0 that saves so many bars of bpm0 (so the song stays whole
    bars long at its first tempo, as the engine and oplrender pad it): (bpm for each beat)"""
    want = (beats - 4 * bars_saved) * 60.0 / bpm0
    lo, hi = bpm0, bpm0 * 4
    for _ in range(60):
        top = (lo + hi) / 2
        t = sum(60.0 / (bpm0 + (top - bpm0) * (k + 1) / beats) for k in range(beats))
        lo, hi = (top, hi) if t > want else (lo, top)
    return [bpm0 + (top - bpm0) * (k + 1) / beats for k in range(beats)]


def abandon_ship():
    """panic; hope (the fight's call, in major); hope dashed; hope again, higher; dashed harder; then the panic
    speeding up (the user loved the draft's speed-up), and round again at 152"""
    form = AS_PANIC + AS_HOPE + AS_DASH + AS_HOPE2 + AS_DASH + AS_PANIC
    s = Piece(152, 1515, form)
    s.setup(0, 48, 80)          # strings
    s.setup(1, 84, 72)          # short pluck, runs
    s.setup(2, 38, 106)         # synth bass
    s.setup(3, 61, 92)          # brass, tritones
    s.setup(4, 47, 98)          # timpani
    s.setup(5, 119, 70)         # noise swell
    s.setup(6, 60, 100)         # horn
    s.setup(7, 56, 100)         # trumpet
    n = len(form)                                   # 8 + 8 + 4 + 8 + 4 + 8 = 40

    def panic(b, sixteenths=True):
        r = chord(form[b]).root
        s.play(0, b, 0, 3.9, [72 + r, 73 + r, 78 + r], 46)
        for k in range(16 if sixteenths else 8):
            q = 36 + r + (6 if k % 4 == 3 else 0)
            st = .25 if sixteenths else .5
            s.play(2, b, k * st, st * .9, q, 72 if k % 4 == 0 else 60)
        for k in range(8):
            s.play(1, b, 2 + k * .25, .2, 72 + r + (k * 3) % 13, 46 + k * 3)
        s.play(3, b, 0, .5, [60 + r, 66 + r], 86)
        s.play(3, b, 1.5, .5, [60 + r, 66 + r], 78)
        for k in range(8):
            s.play(4, b, k * .5, .3, 36 + r % 6, 62 + (k % 2) * 10)

    for b in range(0, 8):
        panic(b)
    s.drums(range(0, 8), {'kick': [(k * .5, 80) for k in range(8)],
                          'snare': [(k * .25, 46 + (k % 4 == 0) * 20) for k in range(16)], 'crash': [(0, 84)]})
    # hope: warm chords, the call in major, a steadier beat
    s.pad(0, range(8, 16), 55, 72, 3, 60)
    s.bass(2, range(8, 16), [(k * .5, .45, 'r') for k in range(8)], 72)
    s.tune(6, 8, [to_major(t) for t in CT_CALL], 92)
    s.tune(7, 12, [up(to_major(t), 12) for t in CT_CALL[4:]], 80)
    s.drums(range(8, 16), FULL, fill=ROCK_FILL)
    s.crash(8, 0, 80)
    # dashed: the hope's chord falls apart, chromatic, tritones
    for b in range(16, 20):
        r = chord(form[b]).root
        s.play(3, b, 0, 1, [60 + r, 66 + r, 72 + r], 92)
        s.play(3, b, 2, 1, [59 + r, 65 + r, 71 + r], 86)
        s.run(1, b, 0, [84 - k for k in range(16)], .25, 70, -1)
        s.play(2, b, 0, 3.9, 36 + r, 80)
        s.roll(4, b, 0, 4, 36 + r % 6, 50, 90)
    s.crash(16, 0, 96)
    s.swell(5, 16, 4, 'C4', 66)
    # hope again, a step higher
    s.pad(0, range(20, 28), 57, 74, 3, 62)
    s.bass(2, range(20, 28), [(k * .5, .45, 'r') for k in range(8)], 74)
    s.tune(7, 20, [up(to_major(t), 14) for t in CT_CALL], 90)
    s.tune(6, 24, [up(to_major(t), 2) for t in CT_CALL[4:]], 84)
    s.drums(range(20, 28), FULL, fill=ROCK_FILL)
    s.crash(20, 0, 84)
    # dashed harder
    for b in range(28, 32):
        r = chord(form[b]).root
        for k in range(8):
            s.play(3, b, k * .5, .35, [60 + r, 66 + r, 72 + r], 80 + (k % 2) * 10)
        s.run(1, b, 0, [88 - k for k in range(16)], .25, 74, -1)
        s.play(2, b, 0, 3.9, 36 + r, 84)
        s.roll(4, b, 0, 4, 36 + r % 6, 60, 96)
    s.crash(28, 0, 100)
    s.crash(30, 0, 96)
    # the panic, faster and faster
    for b in range(32, 40):
        panic(b)
    s.drums(range(32, 40), {'kick': [(k * .5, 84) for k in range(8)],
                            'snare': [(k * .25, 50 + (k % 4 == 0) * 20 + k) for k in range(16)], 'crash': [(0, 90)]})
    for k, bpm in enumerate(accelerando(152, 32, 1)):     # eight bars in seven's time: 152 up to about 190
        s.tempo(32 * 4 + k, bpm)
    s.end(n)
    return s


# --- the stings: VICTORY1..3, TRAGEDY ------------------------------------------
# The user: the splashes' idea good but "way too happy: they need a tinge of sadness
# or longing"; the loss "not tragic enough: it needs to be immediately recognizable
# as some sort of short dirge".

def victory1():
    """A flat and B flat into C: the borrowed chords, E flat in the tune rising to E at the end"""
    s = Piece(116, 1601, ['Abmaj7 Bb6', 'Csus4 Cadd9'])
    s.setup(0, 61, 96)          # brass
    s.setup(1, 56, 104)         # trumpet
    s.setup(2, 48, 86)          # strings
    s.setup(3, 47, 92)          # timpani
    s.line(1, 0, 'G4:0:.5 C5:.5:.5 Eb5:1:1 D5:2:1 F5:3:1', 92)
    s.line(1, 1, 'G5:0:1 E5:1:2.8', 100)
    s.play(0, 0, 0, 1.9, ['Ab3', 'C4', 'Eb4', 'G4'], 82)
    s.play(0, 0, 2, 1.9, ['Bb3', 'D4', 'F4', 'G4'], 82)
    s.play(0, 1, 0, .9, ['C4', 'F4', 'G4'], 86)
    s.play(0, 1, 1, 2.8, ['C4', 'E4', 'G4', 'D5'], 90)
    s.play(2, 0, 0, 1.9, ['Ab2', 'Eb3', 'C4'], 72)
    s.play(2, 0, 2, 1.9, ['Bb2', 'F3', 'D4'], 74)
    s.play(2, 1, 0, 3.8, ['C3', 'G3', 'E4'], 78)
    s.roll(3, 0, 2, 2, 'G1', 40, 80)
    s.play(3, 1, 0, .5, 'C2', 90)
    s.crash(1, 0, 80)
    return s


def victory2():
    """D major, its minor fourth (G minor with E) under the tune's B flat, home: the longing of the minor plagal"""
    s = Piece(126, 1602, ['D Gm6/D', 'D'])
    s.setup(0, 60, 100)         # horn
    s.setup(1, 56, 104)         # trumpet
    s.setup(2, 48, 88)          # strings
    s.setup(3, 47, 92)          # timpani
    s.line(1, 0, 'A4:0:.25 D5:.25:.25 F#5:.5:.5 A5:1:1 Bb5:2:1.5 A5:3.5:.5', 94)
    s.line(1, 1, 'F#5:0:3.6', 96)
    s.play(0, 0, 0, 2, ['D4', 'A4'], 84)
    s.play(0, 0, 2, 2, ['D4', 'G4', 'Bb4'], 84)
    s.play(0, 1, 0, 3.6, ['D4', 'F#4', 'A4'], 88)
    s.play(2, 0, 0, 2, ['D3', 'A3', 'D4', 'F#4'], 74)
    s.play(2, 0, 2, 2, ['D3', 'G3', 'Bb3', 'E4'], 76)
    s.play(2, 1, 0, 3.6, ['D3', 'A3', 'D4', 'F#4'], 80)
    s.line(3, 0, 'D2:0:.5 A1:2:.5 A1:3.5:.25 A1:3.75:.25', 84)
    s.play(3, 1, 0, 1, 'D2', 92)
    s.crash(1, 0, 76)
    return s


def victory3():
    """E flat, F, then G minor where B flat was due (the deceptive turn), E flat, and B flat at last, on its fifth"""
    s = Piece(120, 1603, ['Eb F', 'Gm7 Ebmaj7', 'Bbadd9'])
    s.setup(0, 61, 96)          # brass
    s.setup(1, 56, 104)         # trumpet
    s.setup(2, 48, 88)          # strings
    s.setup(3, 47, 90)          # timpani
    s.line(1, 0, 'Bb4:0:.5 Eb5:.5:.5 G5:1:1 F5:2:1 A5:3:1', 92)
    s.line(1, 1, 'Bb5:0:1.5 A5:1.5:.5 G5:2:2', 94)
    s.line(1, 2, 'F5:0:3.6', 92)
    s.play(0, 0, 0, 1.9, ['Eb4', 'G4', 'Bb4'], 82)
    s.play(0, 0, 2, 1.9, ['F4', 'A4', 'C5'], 82)
    s.play(0, 1, 0, 1.9, ['F4', 'G4', 'Bb4', 'D5'], 80)
    s.play(0, 1, 2, 1.9, ['Eb4', 'G4', 'Bb4', 'D5'], 80)
    s.play(0, 2, 0, 3.6, ['Bb3', 'D4', 'F4', 'C5'], 86)
    s.play(2, 0, 0, 1.9, ['Eb3', 'Bb3', 'G4'], 72)
    s.play(2, 0, 2, 1.9, ['F3', 'C4', 'A4'], 72)
    s.play(2, 1, 0, 1.9, ['G2', 'D3', 'Bb3'], 74)
    s.play(2, 1, 2, 1.9, ['Eb3', 'Bb3', 'G4'], 74)
    s.play(2, 2, 0, 3.6, ['Bb2', 'F3', 'D4'], 80)
    s.roll(3, 1, 2, 2, 'F1', 40, 78)
    s.play(3, 2, 0, .6, 'Bb1', 90)
    s.crash(2, 0, 72)
    return s


def tragedy():
    """a funeral march in B flat minor: the low brass and strings in its rhythm (one, one-and-a-two, three, four),
    the timpani on the beats, a horn lamenting over it, the last chord left to ring with a soft roll"""
    s = Piece(52, 1604, ['Bbm', 'Gb F7', 'Bbm'])
    s.setup(0, 61, 96)          # brass, low
    s.setup(1, 48, 92)          # strings
    s.setup(2, 47, 96)          # timpani
    s.setup(3, 60, 104)         # horn
    s.setup(4, 33, 92)          # bass
    march = [(0, 1), (1, .75), (1.75, .25), (2, 1), (3, 1)]
    for beat, dur in march:
        s.play(0, 0, beat, dur * .9, ['Bb2', 'Db3', 'F3'], 78 if beat in (0, 2) else 66)
        s.play(1, 0, beat, dur * .9, ['Bb3', 'Db4', 'F4'], 64)
        s.play(4, 0, beat, dur * .9, 'Bb1', 76)
    for beat, dur in march[:3]:
        s.play(0, 1, beat, dur * .9, ['Gb2', 'Db3', 'Bb3'], 76 if beat == 0 else 66)
        s.play(1, 1, beat, dur * .9, ['Gb3', 'Bb3', 'Db4'], 62)
        s.play(4, 1, beat, dur * .9, 'Gb1', 74)
    s.play(0, 1, 2, 1.9, ['F2', 'Eb3', 'A3'], 80)
    s.play(1, 1, 2, 1.9, ['F3', 'A3', 'C4', 'Eb4'], 66)
    s.play(4, 1, 2, 1.9, 'F1', 78)
    s.play(0, 2, 0, 3.8, ['Bb2', 'Db3', 'F3'], 70)
    s.play(1, 2, 0, 3.8, ['Bb3', 'Db4', 'F4'], 56)
    s.play(4, 2, 0, 3.8, 'Bb1', 70)
    s.line(3, 0, 'F4:0:1 Gb4:1:.75 F4:1.75:.25 Db4:2:2', 88)      # the lament
    s.line(3, 1, 'Db5:0:1.5 C5:1.5:.5 Bb4:2:1 A4:3:1', 86)
    s.line(3, 2, 'Bb4:0:3.6', 80)
    for b, beat in ((0, 0), (0, 2), (1, 0), (1, 2)):
        s.play(2, b, beat, .5, 'Bb1' if b == 0 else ('Gb1' if beat == 0 else 'F1'), 86)
    s.roll(2, 2, 0, 3.5, 'Bb1', 70, 30, step=.125)
    return s


SONGS = {'CRUISE1.MID': outbound, 'CRUISE2.MID': deep_field, 'CRUISE3.MID': long_watch,
         'CRUISE4.MID': quiet_instruments, 'CRUISE5.MID': beacon, 'CRUISE6.MID': nav_point,
         'HOMEWARD.MID': homeward, 'HOMELOSS.MID': homeloss,
         'BATTLE1.MID': contact, 'BATTLE2.MID': break_and_attack, 'BATTLE3.MID': furball, 'ROCKS.MID': rock_field,
         'PANIC1.MID': hull_breach, 'PANIC2.MID': systems_failing, 'PANIC3.MID': abandon_ship,
         'VICTORY1.MID': victory1, 'VICTORY2.MID': victory2, 'VICTORY3.MID': victory3, 'TRAGEDY.MID': tragedy}
