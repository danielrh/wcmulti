"""The flight's music, for our OPL3 bank (tools/oplbank.py); CC0, ours.

The user's brief (2026-10-23): many space songs, pensive but curious, like
the Vega Strike pieces they played us (terranpeace, terranpeace2, peace4,
peace5_p: minor and modal, sus chords and open fifths, pedals and drones,
arpeggios turning under a sparse tune, layers coming in one by one); a very
calm one for the way home, once the mission is done and the carrier is
targeted; battle songs like terranbattle (a driving pedal, minor, hits on
every beat); three short victory songs for an enemy splashed and a short
tragic one anyone would know again; and three panic songs, from tense to
full panic, as more and more of the ship falls apart.  Not long songs: some
half an hour of music in all, the score (src/score.c) moving between them.

    CRUISE1  "Far Shore"         B minor, 96: a glass bell turning in eighths
                                 under a wide pad, a sub bass, the soft lead's
                                 tune, the vibraphone answering
    CRUISE2  "Cartographer"      D dorian, 84: a harp in 3+3+2 under strings,
                                 the clarinet's tune, celesta answers
    CRUISE3  "Long Watch"        E minor, the lydian fourth, 100: electric
                                 piano eighths, ride and synth bass, the
                                 vibraphone's tune, the flute's second half
    CRUISE4  "Quiet Instruments" C minor over a drone, 92: a bell ostinato,
                                 ticking hats, a slow lead, celesta flurries
    CRUISE5  "Beacon"            F sharp minor into A, 76: choir, harp, the
                                 horn's tune, celesta pings, far timpani
    CRUISE6  "Nav Point"         G dorian, 108: a saw pluck in sixteenths,
                                 pizzicato, a light groove, the oboe
    RETURN1  "Homeward"          E flat, 66: pad, harp rolled up, the horn,
                                 strings; no drums
    RETURN2  "Running Lights"    B flat, 72: electric piano, flute, a shaker
    BATTLE1  "Contact"           C minor on a sus pedal, 132: synth bass
                                 eighths, a pluck in sixteenths, strings,
                                 brass hits, the horn's call, timpani
    BATTLE2  "Break and Attack"  D minor, 144: four on the floor, saw arps,
                                 the synth lead, brass
    BATTLE3  "Furball"           E phrygian, 152: overdriven fifths, toms,
                                 the lead's riff
    BATTLE4  "Crossfire"         G minor, 120: an orchestra: pizzicato
                                 ostinato, horns, trumpets, snare, timpani
    PANIC1   "Hull Breach"       tense, 96: a low cluster, a heartbeat, ticks
    PANIC2   "Systems Failing"   worse, 116: a bass climbing by half steps,
                                 brass alarms in seconds, toms
    PANIC3   "Abandon Ship"      panic, 152: sixteenths, tritones, a crash
                                 every bar, the snare rolling
    VICTORY1..3, TRAGEDY         stings, played once over whatever plays (it
                                 goes on after them)

Each song but the stings is a loop: its last bar leads back into its first.
The tunes of the A sections are written out note by note ('PITCH:BEAT:LENGTH'
a bar); the later ones are written by tools/compose.py from their chords
(seeded: the same notes every time).
"""
import random

from compose import chord, Voicer, Bass, Tune, CALM, CALM_END
from songs import Comp, p, put

NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
DRUM = {'kick': 36, 'stick': 37, 'snare': 38, 'hat': 42, 'open': 46, 'ride': 51, 'crash': 49, 'shaker': 82,
        'tom1': 50, 'tom2': 47, 'tom3': 43, 'tom4': 41}
# a tune's rhythms for a bar (beat, length): driving ones for the battles
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
            for name, hits in use.items():
                key = DRUM.get(name, name)
                for beat, v in hits:
                    self.play(9, b, beat, 0.2, key, v + vel, 3)

    def tune_text(self, ch, bar0, bars, vel):
        self.tune(ch, bar0, bars, vel)

    def made(self, ch, bar0, nbars, key, mode, lo, hi, vel, rhythms=CALM, ends=CALM_END, seed=0, phrase=4):
        """a tune written from the chords of these bars"""
        t = Tune(random.Random(seed), key, mode, lo, hi)
        chords = [self.harm[b] for b in range(bar0, bar0 + nbars)]
        put(self, ch, t.period(bar0, chords, rhythms, ends, phrase=phrase), vel)

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


# --- CRUISE1: "Far Shore" (B minor, 96) ---------------------------------------

FS_A = ['Bmadd9', 'Gmaj7', 'Dsus2', 'Asus4 A', 'Bmadd9', 'Em9', 'Gmaj7#11', 'F#sus4 F#m']
FS_B = ['Gmaj7', 'A6', 'Bm7', 'F#m7', 'Gmaj9', 'Em9', 'Asus4', 'Asus4 A']
FS_C = ['Dmaj7', 'Asus2', 'Bm9', 'Gmaj9', 'D/F#', 'Em9', 'Gmaj7#11', 'F#sus4 F#']
FS_BREAK = ['Gmaj7#11', 'Em9', 'Gmaj7', 'F#sus4 F#']
FS_OUT = ['Gmaj7', 'Em9', 'Asus4', 'F#sus4 F#m']
FS_THEME = ['F#5:0:1.5 E5:1.5:.5 D5:2:1 C#5:3:1', 'B4:0:3 D5:3:1', 'E5:0:1.5 F#5:1.5:.5 A5:2:2',
            'G5:0:1 F#5:1:1 E5:2:2', 'F#5:0:1.5 E5:1.5:.5 D5:2:1 B4:3:1', 'D5:0:1 E5:1:1 G5:2:1 F#5:3:1',
            'C#6:0:2 B5:2:1 A5:3:1', 'B5:0:2 A5:2:1 F#5:3:1']
FS_THEME_B = ['D5:0:2 F#5:2:2', 'E5:0:3 C#5:3:1', 'D5:0:1.5 C#5:1.5:.5 B4:2:2', 'A4:0:3 C#5:3:1',
              'D5:0:1 F#5:1:1 A5:2:2', 'G5:0:1.5 F#5:1.5:.5 E5:2:1 D5:3:1', 'E5:0:4', 'D5:0:2 C#5:2:2']


def far_shore():
    form = FS_A + FS_A + FS_A + FS_B + FS_C + FS_A + FS_BREAK + FS_A + FS_OUT
    s = Piece(96, 101, form)
    s.setup(0, 89, 70)          # warm pad
    s.setup(1, 98, 84)          # glass bell, the arpeggio
    s.setup(2, 39, 104)         # sub bass
    s.setup(3, 85, 96)          # soft lead
    s.setup(4, 11, 74)          # vibraphone
    s.setup(5, 48, 62)          # strings
    n = len(form)
    s.pad(0, range(0, n), 54, 71, 3, 50)
    s.ramp(0, 0, 4, 40, 110)
    s.ramp(0, n - 4, 4, 110, 70)
    turn = [0, 2, 1, 3, 2, 4, 3, 1]
    s.arp(1, range(0, 48), turn, 0.5, 62, 79, 4, 46)
    s.arp(1, range(48, 52), turn, 0.5, 62, 79, 4, 34)
    s.arp(1, range(52, n), turn, 0.5, 62, 79, 4, 46)
    s.ramp(1, 0, 8, 60, 112)
    s.bass(2, range(8, 48), [(0, 2.5, 'r'), (2.5, 1.5, 'r')], 70)
    s.bass(2, range(52, n - 2), [(0, 1.5, 'r'), (1.5, .5, '8'), (2, 2, 'r')], 70)
    s.tune(3, 16, FS_THEME, 74)
    s.tune(3, 24, FS_THEME_B, 72)
    s.tune(3, 40, FS_THEME, 76)
    s.tune(3, 52, FS_THEME, 78)
    s.made(4, 32, 8, 'D', 'major', 74, 88, 60, seed=11)          # the vibraphone has the bridge
    s.made(4, 52, 8, 'B', 'minor', 59, 71, 50, seed=12)          # and answers under the last theme
    s.pad(5, range(32, 40), 59, 76, 3, 44)
    s.ramp(5, 32, 4, 30, 100)
    s.ramp(5, 36, 4, 100, 50)
    shaker = {'shaker': [(k * .5, 34 if k % 2 else 44) for k in range(8)]}
    s.drums(range(8, 24), shaker)
    s.drums(range(24, 40), dict(shaker, stick=hits(1, 3, v=42)))
    groove = dict(shaker, stick=hits(1, 3, v=44), kick=[(0, 58), (2.5, 50)])
    s.drums(range(40, 48), groove, fill=dict(groove, tom3=[(3, 40), (3.5, 36)]))
    s.drums(range(52, n - 2), groove, fill=dict(groove, tom3=[(3, 40), (3.5, 36)]))
    s.end(n)
    return s


# --- CRUISE2: "Cartographer" (D dorian, 84) -----------------------------------

CG_A = ['Dm9', 'G/D', 'Dm9', 'C/D', 'Bbmaj7', 'C6', 'Dm9', 'Asus4 A']
CG_B = ['Fmaj7', 'C/E', 'Dm7', 'Bbmaj9', 'Gm9', 'C', 'Asus4', 'A']
CG_C = ['Bbmaj7', 'F/A', 'Gm7', 'Dm/F', 'Ebmaj7#11', 'Bbmaj7', 'Csus4', 'C']
CG_THEME = ['A4:0:1.5 C5:1.5:.5 D5:2:2', 'E5:0:1 D5:1:1 B4:2:2', 'A4:0:1 F4:1:1 E4:2:1 F4:3:1', 'G4:0:3 E4:3:1',
            'F4:0:1.5 A4:1.5:.5 D5:2:2', 'E5:0:1 G5:1:1 E5:2:1 C5:3:1', 'D5:0:2 E5:2:1 F5:3:1', 'E5:0:3 C#5:3:1']
CG_ANSWER = ['', 'D6:2:.5 F6:2.5:.5 A6:3:1', '', 'G6:2:.5 E6:2.5:.5 C6:3:1', '', 'C6:2:.5 E6:2.5:.5 G6:3:1', '',
             'A6:2:.5 E6:2.5:.5 C#6:3:1']


def cartographer():
    form = CG_A[:4] + CG_A + CG_A + CG_B + CG_C + CG_A + CG_A[:4] + CG_A + CG_B[4:]
    s = Piece(84, 202, form, level=-2)
    s.setup(0, 48, 66)          # strings
    s.setup(1, 46, 92)          # harp
    s.setup(2, 39, 100)         # sub bass
    s.setup(3, 71, 100)         # clarinet
    s.setup(4, 8, 70)           # celesta
    s.setup(5, 98, 64)          # glass bell
    n = len(form)
    s.pad(0, range(0, n), 50, 67, 3, 48)
    s.ramp(0, 0, 4, 50, 100)
    three = [0, 1, 2, 0, 1, 2, 3, 2]                # eighths in 3 + 3 + 2
    s.arp(1, range(0, n), three, 0.5, 50, 69, 4, 52, accent=10, reset=True)
    s.bass(2, range(4, 44), [(0, 3, 'r'), (3, 1, '5')], 70)
    s.bass(2, range(48, n), [(0, 1.5, 'r'), (1.5, 1.5, 'r'), (3, 1, '5')], 70)
    s.tune(3, 4, CG_THEME, 74)
    s.tune(3, 12, CG_THEME, 76)
    s.tune(4, 12, CG_ANSWER, 52)
    s.made(3, 20, 8, 'D', 'dorian', 62, 76, 72, seed=21)
    s.made(4, 28, 8, 'F', 'major', 79, 91, 54, seed=22)
    s.made(5, 28, 8, 'F', 'major', 67, 79, 48, seed=23, phrase=8)
    s.ramp(0, 28, 4, 100, 120)
    s.ramp(0, 32, 4, 120, 95)
    s.tune(3, 36, CG_THEME, 78)
    s.tune(4, 36, CG_ANSWER, 50)
    s.tune(3, 48, CG_THEME, 76)
    s.made(4, 56, 4, 'D', 'dorian', 74, 86, 48, seed=24)
    soft = {'kick': [(0, 50), (2.5, 42)], 'shaker': [(k * .5, 30 if k % 2 else 38) for k in range(8)]}
    s.drums(range(36, 44), soft)
    s.drums(range(48, n - 2), soft)
    s.end(n)
    return s


# --- CRUISE3: "Long Watch" (E minor, 100) -------------------------------------

LW_A = ['Em9', 'Cmaj7#11', 'G6', 'D/F#', 'Em9', 'Cmaj7#11', 'Am9', 'Bsus4 B7']
LW_B = ['Cmaj7#11', 'D6', 'Bm7', 'Em9', 'Cmaj7', 'D/C', 'Bm7', 'Bsus4 B']
LW_C = ['Gmaj7', 'D/F#', 'Em9', 'Cmaj9', 'Am9', 'D', 'Gmaj7', 'Bsus4 B7']
LW_THEME = ['B4:0:1 E5:1:1 F#5:2:1.5 G5:3.5:.5', 'F#5:0:3 E5:3:1', 'D5:0:1 E5:1:1 G5:2:2', 'F#5:0:2 A5:2:1 F#5:3:1',
            'G5:0:1.5 F#5:1.5:.5 E5:2:1 B4:3:1', 'E5:0:1 F#5:1:1 B5:2:2', 'C6:0:1.5 B5:1.5:.5 A5:2:1 G5:3:1',
            'E5:0:2 D#5:2:2']


def long_watch():
    form = LW_A + LW_A + LW_A + LW_B + LW_C + LW_A + LW_C[:4] + LW_A + LW_B[4:]
    s = Piece(100, 303, form, level=2.5)
    s.setup(0, 90, 64)          # soft pad
    s.setup(1, 4, 78)           # electric piano
    s.setup(2, 38, 92)          # synth bass
    s.setup(3, 11, 100)         # vibraphone
    s.setup(4, 73, 90)          # flute
    s.setup(5, 98, 60)          # glass bell
    n = len(form)
    s.pad(0, range(0, n), 52, 69, 3, 46)
    s.arp(1, range(0, n), [0, 1, 2, 3, 1, 2, 0, 3], 0.5, 52, 72, 4, 44, accent=10)
    s.ramp(1, 0, 8, 50, 110)
    s.bass(2, range(8, 48), [(0, 1.5, 'r'), (1.5, 1, 'r'), (2.5, 1.5, '5')], 64)
    s.bass(2, range(52, n), [(0, 1.5, 'r'), (1.5, 1, 'r'), (2.5, 1, '5'), (3.5, .5, '8')], 64)
    s.tune(3, 16, LW_THEME, 76)
    s.made(4, 24, 8, 'E', 'minor', 69, 84, 70, seed=31)
    s.made(3, 32, 8, 'G', 'major', 67, 84, 70, seed=32)
    s.made(4, 32, 8, 'G', 'major', 64, 76, 56, seed=33, rhythms=SLOW, ends=SLOW_END)
    s.tune(3, 40, LW_THEME, 78)
    s.tune(5, 40, LW_THEME, 40)                    # (an octave doubling, far off)
    s.tune(3, 52, LW_THEME, 80)
    s.made(4, 52, 8, 'E', 'minor', 64, 76, 54, seed=34, rhythms=SLOW, ends=SLOW_END)
    ride = {'ride': [(k * .5, 40 if k % 2 else 50) for k in range(8)], 'kick': [(0, 56), (2.5, 46)],
            'stick': hits(1, 3, v=40)}
    s.drums(range(8, 48), ride, fill=dict(ride, tom2=[(3, 42)], tom3=[(3.5, 40)]))
    s.drums(range(52, n - 2), ride, fill=dict(ride, tom2=[(3, 42)], tom3=[(3.5, 40)]))
    s.end(n)
    return s


# --- CRUISE4: "Quiet Instruments" (C minor on a drone, 92) ---------------------

QI_A = ['Cm9', 'Abmaj7/C', 'Bb/C', 'Cm9', 'Fm9/C', 'Abmaj7/C', 'Gsus4/C', 'Cm']
QI_B = ['Abmaj7', 'Bb6', 'Gm7', 'Cm9', 'Fm9', 'Abmaj9', 'Gsus4', 'G']
QI_THEME = ['G4:0:3 Bb4:3:1', 'C5:0:2 Eb5:2:2', 'D5:0:3 F5:3:1', 'Eb5:0:4', 'F5:0:2 Eb5:2:1 C5:3:1', 'Eb5:0:3 G5:3:1',
            'F5:0:2 D5:2:2', 'C5:0:4']
QI_FLURRY = 'C6:0:.25 Eb6:.25:.25 G6:.5:.25 Bb6:.75:.25 C7:1:.25 D7:1.25:.25 Eb7:1.5:.5'


def quiet_instruments():
    form = QI_A + QI_A + QI_A + QI_B + QI_A + QI_A + QI_A + QI_B
    s = Piece(92, 404, form)
    s.setup(0, 89, 68)          # warm pad, the drone's chords
    s.setup(1, 98, 82)          # glass bell ostinato
    s.setup(2, 39, 100)         # sub bass, the pedal
    s.setup(3, 85, 94)          # soft lead
    s.setup(4, 8, 66)           # celesta
    s.setup(5, 48, 58)          # strings, high
    n = len(form)
    s.pad(0, range(0, n), 55, 70, 3, 48)
    s.arp(1, range(0, n), [0, 2, 4, 3, 1, 4, 2, 3], 0.5, 60, 75, 3, 44, accent=6)
    s.ramp(1, 0, 8, 40, 108)
    for b in range(0, n):
        if form[b] in QI_A:
            s.play(2, b, 0, 3.9, 'C2', 72)
        else:
            s.bass(2, range(b, b + 1), [(0, 3.9, 'r')], 70)
    s.tune(3, 16, QI_THEME, 70)
    s.made(3, 24, 8, 'C', 'minor', 65, 79, 68, seed=41, rhythms=SLOW, ends=SLOW_END)
    s.tune(3, 32, QI_THEME, 72)
    s.pad(5, range(32, 40), 72, 86, 2, 40)
    s.tune(3, 48, QI_THEME, 74)
    s.made(3, 56, 8, 'C', 'minor', 65, 79, 66, seed=42, rhythms=SLOW, ends=SLOW_END)
    for b in (7, 23, 39, 47, 55):
        s.line(4, b, QI_FLURRY, 46)
    tick = {'hat': [(k * .25, 26 if k % 4 else 36) for k in range(16)]}
    s.drums(range(8, 40), tick)
    s.drums(range(40, 48), {'kick': [(0, 46), (.5, 36)]})
    s.drums(range(48, n), dict(tick, kick=[(0, 48), (.5, 38)]))
    s.end(n)
    return s


# --- CRUISE5: "Beacon" (F# minor into A, 76) ------------------------------------

BC_A = ['F#m9', 'Dmaj7', 'A/C#', 'E', 'F#m9', 'Dmaj7', 'Bm9', 'C#sus4 C#']
BC_B = ['Dmaj7', 'E', 'C#m7', 'F#m9', 'Bm9', 'E/G#', 'Asus2', 'C#sus4']
BC_C = ['Amaj7', 'E/G#', 'F#m9', 'Dmaj9', 'Amaj7/C#', 'Dmaj7', 'Bm9', 'Esus4 E']
BC_THEME = ['C#4:0:1 F#4:1:1 G#4:2:1 A4:3:1', 'C#5:0:3 A4:3:1', 'B4:0:1.5 A4:1.5:.5 E4:2:2', 'G#4:0:3 B4:3:1',
            'A4:0:1 G#4:1:1 F#4:2:1 C#5:3:1', 'E5:0:2 D5:2:1 C#5:3:1', 'D5:0:2 C#5:2:1 B4:3:1', 'F#4:0:2 F4:2:2']
BC_PINGS = ['C#7:0:2', '', 'E6:2:2', '', 'A6:0:2', '', 'F#6:2:2', '']


def beacon():
    form = BC_A[:4] + BC_A + BC_A + BC_B + BC_C + BC_A + BC_A[4:]
    s = Piece(76, 505, form)
    s.setup(0, 52, 70)          # choir (the soft pad)
    s.setup(1, 46, 88)          # harp
    s.setup(2, 39, 96)          # sub bass
    s.setup(3, 60, 100)         # horn
    s.setup(4, 8, 66)           # celesta
    s.setup(5, 48, 64)          # strings
    s.setup(6, 47, 84)          # timpani
    n = len(form)
    s.pad(0, range(0, n), 52, 69, 3, 50)
    s.ramp(0, 0, 4, 40, 105)
    s.arp(1, range(0, n), [0, 1, 2, 3, 4, 3, 2, 1], 0.5, 49, 68, 4, 48, accent=8)
    s.bass(2, range(4, n), [(0, 4, 'r')], 70)
    s.tune(3, 4, BC_THEME, 72)
    s.tune(3, 12, BC_THEME, 74)
    s.tune(4, 12, BC_PINGS, 46)
    s.made(5, 20, 8, 'A', 'major', 64, 79, 62, seed=51, rhythms=SLOW, ends=SLOW_END)
    s.made(3, 28, 8, 'A', 'major', 57, 72, 74, seed=52)
    s.tune(4, 28, BC_PINGS, 44)
    s.tune(3, 36, BC_THEME, 76)
    s.pad(5, range(36, 44), 61, 76, 3, 40)
    for b in (3, 19, 27, 35, n - 1):
        s.line(6, b, 'C#2:2:.25 C#2:2.5:.25 C#2:3:.25 C#2:3.25:.25 C#2:3.5:.25 C#2:3.75:.25', 40)
    s.end(n)
    return s


# --- CRUISE6: "Nav Point" (G dorian, 108) ---------------------------------------

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
    s.drums(range(52, n - 2), groove, fill=dict(groove, tom2=[(3, 46), (3.25, 40)], tom3=[(3.5, 44), (3.75, 40)]))
    s.end(n)
    return s


# --- RETURN1: "Homeward" (E flat, 66) -------------------------------------------

HW_A = ['Ebmaj9', 'Abmaj7#11', 'Cm9', 'Bbsus4 Bb', 'Ebmaj9', 'Gm7', 'Abmaj7', 'Bbsus4']
HW_B = ['Abmaj7', 'Bb/Ab', 'Gm7', 'Cm9', 'Fm9', 'Bb', 'Ebmaj7/G', 'Absus2 Bbsus4']
HW_THEME = ['Bb4:0:2 G4:2:1 Bb4:3:1', 'C5:0:3 Bb4:3:1', 'G4:0:2 F4:2:1 Eb4:3:1', 'F4:0:4',
            'G4:0:1.5 Bb4:1.5:.5 Eb5:2:2', 'D5:0:2 Bb4:2:2', 'C5:0:1 Bb4:1:1 Ab4:2:1 G4:3:1', 'F4:0:4']


def homeward():
    form = HW_A[:4] + HW_A + HW_B + HW_A + HW_B + HW_A[4:]
    s = Piece(66, 707, form, level=0.5)
    s.setup(0, 89, 70)          # warm pad
    s.setup(1, 46, 86)          # harp
    s.setup(2, 39, 92)          # sub bass
    s.setup(3, 60, 98)          # horn
    s.setup(4, 48, 66)          # strings
    s.setup(5, 8, 60)           # celesta
    n = len(form)
    s.pad(0, range(0, n), 51, 68, 3, 46)
    s.ramp(0, 0, 4, 40, 100)
    s.arp(1, range(0, n), [0, 1, 2, 3, 4, 5, 6, None], 0.5, 46, 70, 4, 44, accent=6, reset=True)
    s.bass(2, range(4, n), [(0, 4, 'r')], 64)
    s.tune(3, 4, HW_THEME, 70)
    s.made(4, 12, 8, 'Eb', 'major', 67, 82, 62, seed=71, rhythms=SLOW, ends=SLOW_END)
    s.tune(3, 20, HW_THEME, 72)
    s.tune(5, 20, ['', 'G6:2:2', '', 'F6:2:2', '', 'Bb6:2:2', '', 'D6:2:2'], 40)
    s.made(3, 28, 8, 'Eb', 'major', 58, 72, 70, seed=72, rhythms=SLOW, ends=SLOW_END)
    s.pad(4, range(28, 36), 63, 79, 3, 40)
    s.end(n)
    return s


# --- RETURN2: "Running Lights" (B flat, 72) -------------------------------------

RL_A = ['Bbmaj9', 'Gm9', 'Ebmaj7', 'F6', 'Bbmaj9', 'Dm7', 'Ebmaj7#11', 'Fsus4 F']
RL_B = ['Ebmaj9', 'F/Eb', 'Dm7', 'Gm9', 'Cm9', 'F', 'Bb/D', 'Ebmaj7 Fsus4']
RL_THEME = ['D5:0:1.5 F5:1.5:.5 Bb5:2:2', 'A5:0:3 F5:3:1', 'G5:0:2 Bb5:2:1 G5:3:1', 'F5:0:4',
            'D5:0:1 Eb5:1:1 F5:2:1 A5:3:1', 'C6:0:2 A5:2:2', 'Bb5:0:1.5 A5:1.5:.5 G5:2:2', 'F5:0:2 C5:2:2']


def running_lights():
    form = RL_A[:4] + RL_A + RL_B + RL_A + RL_B + RL_A[4:]
    s = Piece(72, 808, form)
    s.setup(0, 90, 64)          # soft pad
    s.setup(1, 4, 76)           # electric piano
    s.setup(2, 39, 92)          # sub bass
    s.setup(3, 73, 94)          # flute
    s.setup(4, 11, 76)          # vibraphone
    n = len(form)
    s.pad(0, range(0, n), 53, 70, 3, 44)
    s.arp(1, range(0, n), [0, 2, 1, 3, 2, 1, 3, 2], 0.5, 53, 72, 4, 42, accent=8)
    s.bass(2, range(4, n), [(0, 3, 'r'), (3, 1, '5')], 62)
    s.tune(3, 4, RL_THEME, 70)
    s.made(4, 12, 8, 'Bb', 'major', 67, 84, 62, seed=81)
    s.tune(3, 20, RL_THEME, 72)
    s.made(3, 28, 8, 'Bb', 'major', 70, 86, 66, seed=82, rhythms=SLOW, ends=SLOW_END)
    s.drums(range(4, n - 2), {'shaker': [(k * .5, 26 if k % 2 else 34) for k in range(8)]})
    s.end(n)
    return s


# --- BATTLE1: "Contact" (C minor, 132) ------------------------------------------

CT_A = ['Csus4 Cm', 'Csus4 Cm', 'Ab Bb', 'Csus4 Cm', 'Csus4 Cm', 'Csus4 Cm', 'Fm Ab', 'Gsus4 G']
CT_B = ['Ab', 'Bb', 'Cm', 'Cm', 'Ab', 'Bb', 'Gsus4', 'G']
CT_C = ['Fm9', 'Bb', 'Ebmaj7', 'Abmaj7', 'Fm9', 'Gm', 'Absus2', 'Bbsus4 Bb']
CT_CALL = ['C4:0:1.5 G4:1.5:.5 G4:2:2', 'Ab4:0:1 G4:1:1 F4:2:1 Eb4:3:1', 'C4:0:2 D4:2:2', 'Eb4:0:4',
           'G4:0:1.5 C5:1.5:.5 C5:2:2', 'D5:0:1 C5:1:1 Bb4:2:1 G4:3:1', 'Ab4:0:2 C5:2:2', 'B4:0:2 D5:2:2']
ROCK = {'kick': [(0, 84), (.75, 64), (2, 80), (2.5, 66)], 'snare': hits(1, 3, v=76),
        'hat': [(k * .5, 50 if k % 2 else 60) for k in range(8)]}
ROCK_FILL = {'kick': [(0, 84), (2, 80)], 'snare': [(1, 76), (3, 70), (3.25, 64), (3.5, 70), (3.75, 76)],
             'tom1': [(2, 70)], 'tom2': [(2.25, 68)], 'tom3': [(2.5, 68)], 'tom4': [(2.75, 70)]}


def contact():
    form = CT_A[:4] + CT_A + CT_A + CT_B + CT_C + CT_A + CT_A[:4] + CT_A + CT_B[4:]
    s = Piece(132, 909, form)
    s.setup(0, 48, 76)          # strings
    s.setup(1, 84, 70)          # short pluck, sixteenths
    s.setup(2, 38, 100)         # synth bass
    s.setup(3, 61, 84)          # brass, the hits
    s.setup(4, 60, 104)         # horn, the call
    s.setup(5, 56, 92)          # trumpet
    s.setup(6, 47, 96)          # timpani
    n = len(form)
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
        s.play(9, b, 0, .2, DRUM['crash'], 80)
    s.end(n)
    return s


# --- BATTLE2: "Break and Attack" (D minor, 144) ---------------------------------

BA_A = ['Dm', 'Bb', 'C', 'Asus4 A', 'Dm', 'Bb', 'Gm', 'Asus4 A']
BA_B = ['Bb', 'C', 'Dm', 'F', 'Gm', 'Bb', 'Csus4 C', 'Asus4 A']
BA_RIFF = ['D5:0:.5 F5:.5:.5 A5:1:1 G5:2:.5 F5:2.5:.5 E5:3:1', 'F5:0:1.5 D5:1.5:.5 F5:2:1 Bb5:3:1',
           'A5:0:1 G5:1:1 E5:2:1 C5:3:1', 'E5:0:3 C#5:3:1', 'D5:0:.5 F5:.5:.5 A5:1:1 G5:2:.5 F5:2.5:.5 E5:3:1',
           'F5:0:1 A5:1:1 D6:2:2', 'C6:0:1 Bb5:1:1 G5:2:1 Bb5:3:1', 'A5:0:4']
FLOOR = {'kick': hits(0, 1, 2, 3, v=84), 'snare': hits(1, 3, v=70),
         'hat': [(k * .25, 34 if k % 2 else 46) for k in range(16)], 'open': hits(.5, 1.5, 2.5, 3.5, v=40)}
FLOOR_FILL = {'kick': hits(0, 1, 2, v=84), 'snare': [(1, 70), (3, 72), (3.25, 64), (3.5, 70), (3.75, 76)],
              'tom2': [(2, 70), (2.5, 66)], 'tom4': [(2.25, 68), (2.75, 66)]}


def break_and_attack():
    form = BA_A + BA_A + BA_A + BA_B + BA_A + BA_A[:4] + BA_A + BA_B
    s = Piece(144, 1010, form)
    s.setup(0, 89, 64)          # warm pad
    s.setup(1, 81, 70)          # saw pluck
    s.setup(2, 38, 100)         # synth bass
    s.setup(3, 80, 92)          # synth lead
    s.setup(4, 62, 82)          # brass
    s.setup(5, 84, 60)          # short pluck, the counter
    s.setup(6, 119, 70)         # noise swell
    n = len(form)
    s.pad(0, range(0, n), 55, 72, 3, 50)
    s.arp(1, range(0, n), [0, 2, 1, 3, 2, 4, 3, 2], 0.25, 62, 81, 4, 40, accent=10)
    s.ramp(1, 0, 8, 50, 110)
    s.bass(2, range(0, n), [(k * .5, .45, w) for k, w in enumerate('r8r8r8r5')], 70)
    s.tune(3, 16, BA_RIFF, 80)
    s.made(3, 24, 8, 'D', 'minor', 67, 84, 80, seed=101, rhythms=DRIVE, ends=DRIVE_END)
    for b in range(24, 32):
        v = Voicer(57, 72, 3)
        for beat in (0, 2.5):
            s.play(4, b, beat, .6, v.next(chord(s.sym_at(b, beat))), 72)
    s.tune(3, 32, BA_RIFF, 82)
    s.arp(5, range(32, 40), [0, None, 1, None, 2, 1, None, 0], 0.25, 74, 88, 3, 46)
    s.tune(3, 44, BA_RIFF, 84)
    s.made(3, 52, 8, 'D', 'minor', 67, 86, 82, seed=102, rhythms=DRIVE, ends=DRIVE_END)
    for b in (6, 22, 42, 58):
        s.play(6, b, 0, 7.9, 'D5', 60)
    s.drums(range(8, 40), FLOOR, fill=FLOOR_FILL)
    s.drums(range(40, 44), {'kick': hits(0, 2, v=70), 'hat': [(k * .25, 30) for k in range(16)]})
    s.drums(range(44, n), FLOOR, fill=FLOOR_FILL)
    for b in (8, 16, 24, 32, 44, 52):
        s.play(9, b, 0, .2, DRUM['crash'], 80)
    s.end(n)
    return s


# --- BATTLE3: "Furball" (E phrygian, 152) ---------------------------------------

FB_A = ['E5', 'F5', 'E5', 'G5 F5', 'E5', 'F5', 'D5', 'C5 B5']
FB_B = ['A5', 'G5', 'F5', 'E5', 'A5', 'C5', 'D5', 'F5 E5']
FB_RIFF = ['E5:0:.5 F5:.5:.5 E5:1:.5 B4:1.5:.5 E5:2:1 G5:3:1', 'F5:0:1 E5:1:.5 D5:1.5:.5 C5:2:2',
           'B4:0:.5 E5:.5:.5 G5:1:.5 B5:1.5:.5 A5:2:1 G5:3:1', 'F5:0:2 E5:2:2',
           'E5:0:.5 F5:.5:.5 E5:1:.5 B4:1.5:.5 E5:2:1 G5:3:1', 'A5:0:1 G5:1:.5 F5:1.5:.5 E5:2:2',
           'D5:0:1 F5:1:1 A5:2:1 D6:3:1', 'C6:0:2 B5:2:2']
HEAVY = {'kick': [(0, 88), (.5, 70), (1.5, 74), (2, 86), (2.5, 70), (3.5, 74)], 'snare': hits(1, 3, v=80),
         'hat': [(k * .5, 54 if k % 2 else 64) for k in range(8)]}
HEAVY_FILL = {'kick': hits(0, 2, v=88), 'snare': [(1, 80), (2.5, 70), (2.75, 74)],
              'tom1': [(3, 78)], 'tom2': [(3.25, 76)], 'tom3': [(3.5, 76)], 'tom4': [(3.75, 80)]}


def furball():
    form = FB_A[:4] + FB_A + FB_A + FB_B + FB_A + FB_A[:4] + FB_A + FB_B
    s = Piece(152, 1111, form)
    s.setup(0, 30, 74)          # overdriven guitar, fifths
    s.setup(1, 84, 64)          # short pluck
    s.setup(2, 38, 100)         # synth bass
    s.setup(3, 80, 94)          # synth lead
    s.setup(4, 61, 80)          # brass
    s.setup(5, 48, 64)          # strings
    n = len(form)
    for b, beat, dur, sym in s.segs(range(0, n)):
        c = chord(sym)
        root = 40 + (c.root - 4) % 12
        for k in range(int(dur / .5)):
            pos = beat + k * .5
            s.play(0, b, pos, .4, [root, root + 7], 78 if k == 0 else 62)
    s.bass(2, range(0, n), [(k * .5, .4, 'r') for k in range(8)], 70)
    s.arp(1, range(12, n), [0, 1, 2, 1, 3, 1, 2, 1], 0.25, 64, 83, 3, 36, accent=8)
    s.tune(3, 12, FB_RIFF, 84)
    s.pad(5, range(20, 28), 64, 79, 3, 52)
    s.made(3, 20, 8, 'E', 'minor', 67, 84, 80, seed=111, rhythms=DRIVE, ends=DRIVE_END)
    for b in range(20, 28):
        v = Voicer(59, 74, 3)
        s.play(4, b, 0, .5, v.next(chord(s.sym_at(b, 0))), 74)
        s.play(4, b, 2.5, .5, v.next(chord(s.sym_at(b, 2.5))), 68)
    s.tune(3, 28, FB_RIFF, 86)
    s.tune(3, 40, FB_RIFF, 88)
    s.made(3, 48, 8, 'E', 'minor', 67, 86, 84, seed=112, rhythms=DRIVE, ends=DRIVE_END)
    s.drums(range(4, 36), HEAVY, fill=HEAVY_FILL)
    s.drums(range(36, 40), {'kick': hits(0, 1, 2, 3, v=76), 'snare': [(k * .25, 40 + k * 3) for k in range(16)]})
    s.drums(range(40, n), HEAVY, fill=HEAVY_FILL)
    for b in (4, 12, 20, 28, 40, 48):
        s.play(9, b, 0, .2, DRUM['crash'], 84)
    s.end(n)
    return s


# --- BATTLE4: "Crossfire" (G minor, 120) ----------------------------------------

CF_A = ['Gm', 'Eb', 'Cm', 'D', 'Gm', 'Bb', 'Eb Cm', 'D7']
CF_B = ['Eb', 'F', 'Bb', 'Gm', 'Cm', 'D', 'Eb', 'Dsus4 D']
CF_THEME = ['G4:0:1.5 Bb4:1.5:.5 D5:2:2', 'Eb5:0:1 D5:1:1 Bb4:2:1 G4:3:1', 'C5:0:2 Eb5:2:2', 'D5:0:4',
            'G5:0:1.5 F5:1.5:.5 D5:2:2', 'F5:0:1 D5:1:1 Bb4:2:2', 'Eb5:0:1 D5:1:1 C5:2:1 Eb5:3:1', 'D5:0:4']
MARCH = {'snare': [(0, 50), (.75, 44), (1, 66), (2, 50), (2.75, 44), (3, 66), (3.5, 50)],
         'kick': hits(0, 2, v=70)}
MARCH_FILL = {'snare': [(k * .25, 46 + k * 2) for k in range(16)], 'kick': hits(0, v=70)}


def crossfire():
    form = CF_A[:4] + CF_A + CF_A + CF_B + CF_A + CF_B + CF_A[:4] + CF_A + CF_B[4:]
    s = Piece(120, 1212, form)
    s.setup(0, 48, 76)          # strings
    s.setup(1, 45, 86)          # pizzicato, the ostinato
    s.setup(2, 32, 100)         # upright bass, plucked low
    s.setup(3, 60, 104)         # horn
    s.setup(4, 56, 92)          # trumpet
    s.setup(5, 61, 80)          # brass, the chords
    s.setup(6, 47, 100)         # timpani
    n = len(form)
    s.pad(0, range(0, n), 55, 72, 3, 54)
    s.arp(1, range(0, n), [0, 0, 2, 0, 1, 0, 2, 3], 0.5, 55, 74, 4, 56, accent=12)
    s.bass(2, range(0, n), [(0, .5, 'r'), (1, .5, 'r'), (2, .5, 'r'), (2.5, .5, '5'), (3, .5, 'r')], 72)
    s.tune(3, 4, CF_THEME, 86)
    s.tune(3, 12, CF_THEME, 88)
    s.made(4, 12, 8, 'G', 'minor', 70, 82, 60, seed=121, rhythms=SLOW, ends=SLOW_END)
    s.made(4, 20, 8, 'Bb', 'major', 65, 81, 82, seed=122, rhythms=DRIVE, ends=DRIVE_END)
    for b in range(20, 28):
        v = Voicer(58, 72, 3)
        s.play(5, b, 0, 1.8, v.next(chord(s.sym_at(b, 0))), 66)
    s.tune(3, 28, CF_THEME, 90)
    s.made(4, 36, 8, 'Bb', 'major', 67, 82, 84, seed=123, rhythms=DRIVE, ends=DRIVE_END)
    s.tune(3, 48, CF_THEME, 92)
    for b in range(0, n):
        r = 43 + (chord(s.sym_at(b, 0)).root - 7) % 12
        r = r - 12 if r > 50 else r
        s.play(6, b, 0, .5, r, 70 if b % 2 == 0 else 56)
        if b % 8 == 7:
            s.line(6, b, ' '.join('D2:%g:.25' % (2 + k * .25) for k in range(8)), 64)
    s.drums(range(4, 44), MARCH, fill=MARCH_FILL)
    s.drums(range(48, n), MARCH, fill=MARCH_FILL)
    for b in (4, 12, 20, 28, 36, 48):
        s.play(9, b, 0, .2, DRUM['crash'], 74)
    s.end(n)
    return s


# --- PANIC1..3: "Hull Breach", "Systems Failing", "Abandon Ship" -----------------

def hull_breach():
    form = ['Cm', 'Db/C', 'Cm', 'Gb/C'] * 8
    s = Piece(96, 1313, form, level=2)
    s.setup(0, 48, 72)          # strings, low
    s.setup(1, 98, 70)          # glass bell
    s.setup(2, 39, 104)         # sub bass
    s.setup(3, 8, 60)           # celesta
    s.setup(4, 119, 60)         # noise swell
    n = len(form)
    for b in range(n):
        s.play(0, b, 0, 3.9, ['C3', 'Db3', 'G3'] if b % 2 == 0 else ['C3', 'Db3', 'Gb3'], 50)
        s.ramp(0, b, 1, 70, 110) if b % 4 == 3 else None
        for beat in range(4):
            s.play(2, b, beat, .5, 'C2', 64 if beat == 0 else 54)
        s.play(1, b, 0, .4, 'C5', 40)
        s.play(1, b, .75, .4, 'G5', 34)
        s.play(1, b, 1.5, .4, 'Db6', 38)
        if b % 4 == 3:
            s.play(3, b, 2, 1, 'F#6', 40)
            s.play(3, b, 3, 1, 'C6', 36)
    for b in range(0, n, 8):
        s.play(4, b + 6, 0, 7.9, 'C4', 54)
    s.drums(range(0, n), {'kick': [(0, 70), (.375, 56)], 'hat': [(k * .5, 30 if k % 2 else 38) for k in range(8)]})
    s.end(n)
    return s


def systems_failing():
    steps = ['C', 'Db', 'D', 'Eb', 'E', 'Eb', 'D', 'Db']
    form = [steps[(b // 2) % 8] + 'm' for b in range(40)]
    s = Piece(116, 1414, form)
    s.setup(0, 48, 78)          # strings
    s.setup(1, 84, 70)          # short pluck
    s.setup(2, 38, 104)         # synth bass
    s.setup(3, 61, 88)          # brass, the alarms
    s.setup(4, 119, 66)         # noise swell
    s.setup(5, 47, 92)          # timpani
    n = len(form)
    s.bass(2, range(0, n), [(k * .5, .45, 'r') for k in range(8)], 72)
    s.arp(1, range(0, n), [0, 1, 2, 1], 0.25, 64, 79, 3, 44, accent=8)
    for b in range(n):
        r = 60 + chord(form[b]).root
        s.play(0, b, 0, 3.9, [r, r + 1, r + 6], 50)
        if b % 2 == 1:
            s.play(3, b, 1.5, .4, [r + 12, r + 13], 78)
            s.play(3, b, 3, .4, [r + 12, r + 13], 72)
        if b % 4 == 3:
            s.line(5, b, 'C2:2:.25 C2:2.25:.25 C2:2.5:.25 C2:2.75:.25 C2:3:.25 C2:3.25:.25 C2:3.5:.25 C2:3.75:.25', 70)
    for b in range(2, n, 8):
        s.play(4, b, 0, 15.9, 'C4', 60)
    beat = {'kick': hits(0, 1, 2, 3, v=74), 'snare': hits(1, 3, v=64), 'hat': [(k * .5, 50) for k in range(8)],
            'tom3': [(2.5, 56)], 'tom4': [(3.5, 60)]}
    s.drums(range(0, n), beat, fill=dict(beat, snare=[(k * .25, 44 + k * 2) for k in range(16)]))
    s.end(n)
    return s


def abandon_ship():
    form = ['Cm', 'Gb', 'Cm', 'Gb', 'Db', 'G', 'Ab', 'D'] * 5
    s = Piece(152, 1515, form)
    s.setup(0, 48, 80)          # strings, high
    s.setup(1, 84, 72)          # short pluck, runs
    s.setup(2, 38, 106)         # synth bass
    s.setup(3, 61, 92)          # brass, tritones
    s.setup(4, 47, 98)          # timpani
    s.setup(5, 119, 70)         # noise swell
    n = len(form)
    for b in range(n):
        r = chord(form[b]).root
        s.play(0, b, 0, 3.9, [72 + r, 73 + r, 78 + r], 46)
        for k in range(16):
            q = 36 + r + (6 if k % 4 == 3 else 0)
            s.play(2, b, k * .25, .22, q, 72 if k % 4 == 0 else 60)
        for k in range(8):
            s.play(1, b, 2 + k * .25, .2, 72 + r + (k * 3) % 13, 46 + k * 3)
        s.play(3, b, 0, .5, [60 + r, 66 + r], 86)
        s.play(3, b, 1.5, .5, [60 + r, 66 + r], 78)
        for k in range(8):
            s.play(4, b, k * .5, .3, 36 + r % 6, 62 + (k % 2) * 10)
    for b in range(0, n, 4):
        s.play(5, b, 0, 3.9, 'C4', 64)
    rush = {'kick': [(k * .5, 80) for k in range(8)], 'snare': [(k * .25, 46 + (k % 4 == 0) * 20) for k in range(16)],
            'crash': [(0, 84)]}
    s.drums(range(0, n), rush)
    s.end(n)
    return s


# --- the stings: VICTORY1..3, TRAGEDY ------------------------------------------

def victory1():
    """a brass fanfare, C major, with a timpani roll and a crash"""
    s = Piece(120, 1601, ['C', 'C'])
    s.setup(0, 61, 100)         # brass
    s.setup(1, 56, 110)         # trumpet
    s.setup(2, 47, 96)          # timpani
    s.line(1, 0, 'G4:0:.33 C5:.33:.33 E5:.67:.33 G5:1:1.5 E5:2.5:.5 G5:3:1', 96)
    s.line(1, 1, 'C6:0:3', 104)
    s.play(0, 0, 0, .4, ['C4', 'E4', 'G4'], 86)
    s.play(0, 0, 1, 1.4, ['C4', 'E4', 'G4'], 84)
    s.play(0, 0, 2.5, .4, ['B3', 'D4', 'G4'], 80)
    s.play(0, 0, 3, 1, ['C4', 'F4', 'A4'], 82)
    s.play(0, 1, 0, 3, ['C4', 'E4', 'G4', 'C5'], 92)
    s.line(2, 0, ' '.join('C2:%g:.25' % (k * .25) for k in range(4)) + ' G1:1:.5 C2:3:.25 C2:3.25:.25 C2:3.5:.25 C2:3.75:.25', 84)
    s.play(2, 1, 0, 1, 'C2', 96)
    s.play(9, 1, 0, .2, DRUM['crash'], 96)
    return s


def victory2():
    """the horns and trumpet up through D mixolydian, the flat seventh, home"""
    s = Piece(132, 1602, ['D', 'D'])
    s.setup(0, 60, 104)         # horn
    s.setup(1, 56, 110)         # trumpet
    s.setup(2, 48, 92)          # strings
    s.setup(3, 47, 96)          # timpani
    s.line(1, 0, 'A4:0:.25 D5:.25:.25 E5:.5:.25 F#5:.75:.25 A5:1:1 C6:2:.75 B5:2.75:.25 A5:3:.5 B5:3.5:.5', 98)
    s.line(1, 1, 'D6:0:3', 106)
    s.play(0, 0, 0, 2, ['D4', 'A4'], 86)
    s.play(0, 0, 2, 2, ['C4', 'G4'], 84)
    s.play(0, 1, 0, 3, ['D4', 'F#4', 'A4'], 92)
    s.play(2, 0, 2, 2, ['C3', 'E3', 'G3', 'C4'], 74)
    s.play(2, 1, 0, 3, ['D3', 'A3', 'D4', 'F#4'], 82)
    s.line(3, 0, 'D2:0:.5 A1:2:.5 A1:3.5:.25 A1:3.75:.25', 86)
    s.play(3, 1, 0, 1, 'D2', 98)
    s.play(9, 1, 0, .2, DRUM['crash'], 92)
    return s


def victory3():
    """a leap of a fifth and a sixth, then A flat to B flat: the borrowed step home"""
    s = Piece(126, 1603, ['Bb', 'Ab Bb'])
    s.setup(0, 61, 100)         # brass
    s.setup(1, 56, 108)         # trumpet
    s.setup(2, 48, 90)          # strings
    s.line(1, 0, 'F4:0:.5 Bb4:.5:.5 D5:1:1 F5:2:2', 96)
    s.line(1, 1, 'Eb5:0:.5 C5:.5:.5 Eb5:1:.5 F5:1.5:.5 D5:2:2', 100)
    s.play(0, 0, 0, 2, ['Bb3', 'D4', 'F4'], 86)
    s.play(0, 0, 2, 2, ['Bb3', 'Eb4', 'G4'], 82)
    s.play(0, 1, 0, 1.9, ['Ab3', 'C4', 'Eb4'], 86)
    s.play(0, 1, 2, 2, ['Bb3', 'D4', 'F4', 'Bb4'], 94)
    s.play(2, 1, 0, 1.9, ['Ab2', 'Eb3', 'C4'], 74)
    s.play(2, 1, 2, 2, ['Bb2', 'F3', 'D4'], 82)
    s.drums(range(0, 1), {'snare': [(3, 60), (3.25, 64), (3.5, 70), (3.75, 76)]})
    s.play(9, 1, 2, .2, DRUM['crash'], 90)
    return s


def tragedy():
    """the horn alone, falling: E D C | B A, over A minor, F, D minor and E with its flat ninth, home to A minor;
    the timpani under the last chord"""
    s = Piece(60, 1604, ['Am F', 'Dm/F E7b9', 'Am'])
    s.setup(0, 60, 104)         # horn
    s.setup(1, 48, 86)          # strings
    s.setup(2, 47, 90)          # timpani
    s.setup(3, 33, 92)          # bass
    s.line(0, 0, 'E5:0:2 D5:2:1 C5:3:1', 84)
    s.line(0, 1, 'B4:0:2 G#4:2:1 B4:3:1', 80)
    s.line(0, 2, 'A4:0:3.5', 76)
    s.play(1, 0, 0, 2, ['A3', 'C4', 'E4'], 62)
    s.play(1, 0, 2, 2, ['A3', 'C4', 'F4'], 62)
    s.play(1, 1, 0, 2, ['A3', 'D4', 'F4'], 60)
    s.play(1, 1, 2, 2, ['G#3', 'D4', 'F4', 'B4'], 64)
    s.play(1, 2, 0, 3.5, ['A3', 'C4', 'E4'], 56)
    s.line(3, 0, 'A2:0:2 F2:2:2', 70)
    s.line(3, 1, 'F2:0:2 E2:2:2', 70)
    s.play(3, 2, 0, 3.5, 'A1', 66)
    s.line(2, 2, ' '.join('A1:%g:.25' % (k * .25) for k in range(12)), 50)
    return s


SONGS = {'CRUISE1.MID': far_shore, 'CRUISE2.MID': cartographer, 'CRUISE3.MID': long_watch,
         'CRUISE4.MID': quiet_instruments, 'CRUISE5.MID': beacon, 'CRUISE6.MID': nav_point,
         'RETURN1.MID': homeward, 'RETURN2.MID': running_lights,
         'BATTLE1.MID': contact, 'BATTLE2.MID': break_and_attack, 'BATTLE3.MID': furball, 'BATTLE4.MID': crossfire,
         'PANIC1.MID': hull_breach, 'PANIC2.MID': systems_failing, 'PANIC3.MID': abandon_ship,
         'VICTORY1.MID': victory1, 'VICTORY2.MID': victory2, 'VICTORY3.MID': victory3, 'TRAGEDY.MID': tragedy}
