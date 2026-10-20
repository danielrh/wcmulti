"""The three room songs, written for our OPL3 bank (tools/oplbank.py); CC0, ours.

    QUARTERS.MID  "Bunk Light": the quarters and the locker.  Calm, 68 a
                  minute, some nine and a half minutes before it comes round:
                  I   Bunk Light (D major): an electric piano turning over in
                      eighths under a wide pad, an upright bass, the vibraphone
                      with the tune, a flute answering it the last time round;
                  II  Porthole (B minor): soft chords on the beat, a celesta
                      tune, the vibraphone in the middle, the flute to close;
                  III Night Cycle (F major): the piano's eighths in threes
                      and twos, the flute and the vibraphone;
                  IV  Letters Home (A major): the vibraphone and a second
                      piano take turns, two bars each;
                  V   Bunk Light again (D major): the flute has the tune, then
                      the vibraphone, then it all thins out into the start.
    BAR.MID       "Off-Watch": the bar and the lounge.  Warm, moving, still
                  easy, 100 a minute, some twelve and a half minutes round:
                  I   Off-Watch (F, swung): a jazz guitar with the tune over
                      electric-piano comping, a walking bass, ride and
                      cross-stick; the vibraphone has the second chorus;
                  II  Second Shift (B flat): the vibraphone's tune, then a
                      guitar chorus;
                  III Liberty Port (E flat, a bossa nova, straight eighths):
                      nylon guitar, flute, vibraphone, shaker and rim;
                  IV  Cargo Blues (G minor blues): the guitar's head twice,
                      then the vibraphone, the piano and the guitar take a
                      chorus each, the organ under them, and the head again;
                  V   Off-Watch's changes again (F): the vibraphone over the
                      organ, the old bridge on the guitar, then guitar and
                      vibraphone trading four bars at a time, into the top.
    BRIEF.MID     "Ready Room": the briefing.  Pensive: 72 a minute in D
                  minor, a harp ostinato that never stops, strings, a horn
                  with the long line, a clarinet against it, timpani at the
                  ends of phrases and a far-off celesta ping.

Each is a loop: its last bar leads back into its first.  midi.py writes them
with the other cues.  The first section of each and the briefing are
written out note by note, as 'PITCH:BEAT:LENGTH' bar by bar (beats from the
bar's start, lengths in beats); the later sections are written by
tools/compose.py from their chords (seeded: the same notes every time).
The bar song swings its off-beat eighths, except in the bossa nova.
"""
import random

import midi
from compose import (chord, bars_of, Voicer, Bass, Tune, CALM, CALM_END, SWING, SWING_END, SOLO, SOLO_END,
                     BOSSA, BOSSA_END)

FLAT = {'Db': 'C#', 'Eb': 'D#', 'Gb': 'F#', 'Ab': 'G#', 'Bb': 'A#'}


def p(name):
    """'Bb3' -> 58"""
    n, octave = name[:-1], int(name[-1])
    return midi.pitch(FLAT.get(n, n) + str(octave))


class Comp(midi.Song):
    def __init__(self, bpm, seed, swing=0.5):
        super().__init__(bpm)
        self.rng = random.Random(seed)
        self.swing = swing
        self.sounding = {}

    def note(self, ch, beat, dur, pitch, vel=90):
        """a note; one still sounding at that pitch on that channel is let go just before"""
        t0, t1 = int(beat * midi.PPQ), int((beat + dur) * midi.PPQ) - 1
        ev = self.tracks.setdefault(ch, [])
        last = self.sounding.get((ch, pitch))
        if last is not None and ev[last][0] >= t0:
            ev[last] = (max(t0 - 1, ev[last - 1][0] + 1), ev[last][1])
        ev.append((t0, bytes((0x90 | ch, pitch, vel))))
        ev.append((t1, bytes((0x80 | ch, pitch, 0))))
        self.sounding[(ch, pitch)] = len(ev) - 1

    def at(self, bar, beat):
        """a beat of a bar (from 0), its off-beat eighths swung"""
        whole, frac = divmod(beat, 1)
        if abs(frac - 0.5) < 1e-6:
            frac = self.swing
        return bar * self.beats + whole + frac

    def setup(self, ch, prog, vol, pan=64):
        self.program(ch, prog, min(127, round(vol * 1.22)))     # (the songs' level: a little louder than written)
        self.tracks[ch].append((0, bytes((0xB0 | ch, 10, pan))))

    def play(self, ch, bar, beat, dur, pitches, vel, spread=4):
        """a note or a chord, the velocity a little uneven, as played"""
        t0 = self.at(bar, beat)
        t1 = self.at(bar, beat + dur) if dur > 0 else t0 + 0.1
        for q in pitches if isinstance(pitches, (list, tuple)) else [pitches]:
            v = max(1, min(127, vel + self.rng.randint(-spread, spread)))
            self.note(ch, t0, max(0.05, t1 - t0 - 0.02), p(q) if isinstance(q, str) else q, v)

    def line(self, ch, bar, text, vel):
        """'C5:0:1 A4:1:.5 ...' in one bar"""
        for tok in text.split():
            name, beat, dur = tok.split(':')
            self.play(ch, bar, float(beat), float(dur), name, vel)

    def tune(self, ch, bar0, bars, vel):
        for i, text in enumerate(bars):
            if text:
                self.line(ch, bar0 + i, text, vel)


def put(s, ch, notes, vel):
    """notes from compose.Tune, the strong beats a little louder"""
    for bar, beat, dur, q in notes:
        s.play(ch, bar, beat, dur, q, vel + (4 if beat in (0, 2) else 0) - (5 if dur <= 0.5 else 0))


def shift(notes, bars):
    return [(b + bars, beat, dur, q) for b, beat, dur, q in notes]


# --- QUARTERS: "Bunk Light" ---

# bar: bass, the piano's four notes, the pad's three
Q_A = [('D2', 'F#3 A3 C#4 E4', 'D3 A3 E4'),        # Dmaj9
       ('B1', 'F#3 A3 D4 E4', 'B2 F#3 D4'),        # Bm11
       ('G1', 'F#3 A3 B3 D4', 'G2 D3 B3'),         # Gmaj9
       ('G1', 'G3 Bb3 D4 E4', 'G2 D3 Bb3'),        # Gm6, the borrowed iv
       ('F#1', 'F#3 A3 D4 E4', 'F#2 D3 A3'),       # D/F#
       ('E2', 'G3 B3 D4 F#4', 'E3 G3 D4'),         # Em9
       ('G1', 'F#3 A3 B3 D4', 'G2 D3 B3'),         # Gmaj9
       ('A1', 'G3 B3 D4 E4', 'A2 E3 D4')]          # A7sus4
Q_B = [('B1', 'A3 C#4 D4 F#4', 'B2 F#3 D4'),       # Bm9
       ('F#1', 'A3 C#4 E4 F#4', 'F#2 C#3 A3'),     # F#m7
       ('G1', 'F#3 A3 B3 C#4', 'G2 D3 B3'),        # Gmaj7#11
       ('F#1', 'F#3 A3 D4 E4', 'F#2 D3 A3'),       # D/F#
       ('E2', 'G3 B3 D4 F#4', 'E3 G3 D4'),         # Em9
       ('B1', 'A3 D4 E4 F#4', 'B2 F#3 D4'),        # Bm11
       ('C2', 'G3 B3 E4 F#4', 'C3 G3 E4'),         # Cmaj7#11, the lift
       ('A1', 'G3 B3 D4 E4', 'A2 E3 D4')]          # A7sus4
Q_TUNE_A = ['F#5:0:2 E5:2:1 A4:3:1',
            'D5:0:3 C#5:3:.5 D5:3.5:.5',
            'B4:0:1.5 A4:1.5:.5 F#4:2:2',
            'G4:0:1 Bb4:1:1 D5:2:2',
            'E5:0:2 F#5:2:1 A5:3:1',
            'G5:0:1.5 F#5:1.5:.5 E5:2:1 D5:3:1',
            'B4:0:3 A4:3:1',
            'A4:0:2 E5:3.5:.5']
Q_TUNE_B = ['F#5:0:1.5 E5:1.5:.5 D5:2:1 C#5:3:1',
            'A4:0:3 C#5:3:1',
            'B4:0:1 C#5:1:1 D5:2:1 F#5:3:1',
            'E5:0:3 D5:3:1',
            'B4:0:1.5 D5:1.5:.5 F#5:2:2',
            'E5:0:1.5 D5:1.5:.5 A4:2:2',
            'E5:0:1 F#5:1:1 G5:2:2',
            'E5:0:2 D5:2:1 E5:3:1']
Q_SPARKLE = ['', '', 'C#6:2:2', '', '', 'B5:2:2', '', 'E6:2:1.5']


def quarters_song():
    s = Comp(68, seed=11)
    s.setup(0, 4, 92)       # electric piano
    s.setup(1, 89, 74)      # warm pad (wide)
    s.setup(2, 32, 104)     # upright bass
    s.setup(3, 11, 100)     # vibraphone
    s.setup(4, 73, 92)      # flute
    s.setup(5, 5, 84)       # a second piano (the answers in IV)
    s.setup(6, 8, 74)       # celesta
    harmony = Q_A + Q_A + Q_B + Q_A
    for bar, (bass, keys, pad) in enumerate(harmony):
        keys, pad = keys.split(), pad.split()
        # the piano: broken chords, sparse for the first four bars, then turning over in eighths
        if bar < 4:
            for k, (beat, i) in enumerate([(0, 0), (1, 1), (2, 2), (3, 3)]):
                s.play(0, bar, beat, 2.5, keys[i], 54 if k == 0 else 48)
        else:
            for k, i in enumerate([0, 1, 2, 3, 1, 2, 3, 2]):
                s.play(0, bar, k * 0.5, 1.5, keys[i], (60 if k % 4 == 0 else 50) - (6 if bar >= 24 else 0))
        s.play(1, bar, 0, 4, pad, 52 if bar >= 2 else 40, spread=2)
        s.play(2, bar, 0, 2.5, bass, 76)
        s.play(2, bar, 3, 1, p(bass) + 7 if bar % 2 == 0 else p(bass) + 12, 58)
    s.tune(3, 8, Q_TUNE_A, 84)
    s.tune(3, 16, Q_TUNE_B, 86)
    s.tune(4, 24, Q_TUNE_A, 80)                     # the flute takes it, the vibraphone sparkles above
    s.tune(3, 24, Q_SPARKLE, 56)
    rng = random.Random(1968)
    bar = porthole(s, 32, rng)
    bar = night_cycle(s, bar, rng)
    bar = letters_home(s, bar, rng)
    bunk_light_again(s, bar, rng)
    return s


def porthole(s, b0, rng):
    """II, B minor: soft chords on the beat; the celesta, the vibraphone, the flute"""
    a1 = bars_of('Bm9 | Gmaj9#11 | Em9 | F#7sus4 | Bm9 | Gmaj9#11 | D/A | F#m7')
    a2 = bars_of('Bm9 | Gmaj9#11 | Em9 | A7sus4 | Dmaj9 | Gmaj9#11 | Em9 | F#7sus4')
    b = bars_of('Gmaj9 | F#m7 | Em9 | Bm9 | Cmaj7#11 | Gmaj9 | Em9 | F#7sus4')
    a3 = bars_of('Bm9 | Gmaj9#11 | Em9 | F#7sus4 | Bm9 | Gmaj9#11 | Em9 | C9sus4')
    keys, pads, bass = Voicer(52, 69, 4), Voicer(50, 69, 3, 16), Bass(28, 45)
    for i, syms in enumerate(a1 + a2 + b + a3):
        bar, c = b0 + i, chord(syms[0])
        v = keys.next(c)
        s.play(0, bar, 0, 2, v, 50)
        s.play(0, bar, 2.5, 1.4, v[-2:], 42)            # the chord, then its top two again
        s.play(1, bar, 0, 4, pads.next(c), 46, spread=2)
        s.play(2, bar, 0, 3, bass.note(c), 72)
        if i % 4 == 3:
            s.play(2, bar, 3, 1, bass.last + 7, 54)
    put(s, 6, Tune(rng, 'B', 'minor', 71, 88).period(b0, a1 + a2, CALM, CALM_END), 70)
    put(s, 3, Tune(rng, 'D', 'major', 66, 81).period(b0 + 16, b, CALM, CALM_END), 80)
    put(s, 4, Tune(rng, 'B', 'minor', 66, 81).period(b0 + 24, a3, CALM, CALM_END), 76)
    return b0 + 32


def night_cycle(s, b0, rng):
    """III, F major: the piano's eighths grouped three, three, two; the flute, the vibraphone"""
    a1 = bars_of('Fmaj9 | Am7 | Bbmaj9 | Gm9 C9sus4 | Fmaj9 | Dm9 | Bbmaj9 | C9sus4')
    a2 = bars_of('Fmaj9 | Am7 | Bbmaj9 | Bbm6 | Am7 | Dm9 | Gm9 | C9sus4')
    b = bars_of('Dm9 | Am7 | Bbmaj9 | F/A | Gm9 | Am7 | Bbmaj9 | C9sus4')
    a3 = bars_of('Fmaj9 | Am7 | Bbmaj9 | Bbm6 | Fmaj9 | Dm9 | Gm9 | E9sus4')
    keys, pads, bass = Voicer(53, 70, 4), Voicer(50, 69, 3, 16), Bass(29, 45)
    for i, syms in enumerate(a1 + a2 + b + a3):
        bar = b0 + i
        cs = [chord(x) for x in syms]
        vs = [keys.next(c) for c in cs]
        for k, idx in enumerate([0, 1, 3, 0, 1, 3, 2, 1]):
            half = 1 if k >= 4 and len(cs) == 2 else 0
            s.play(0, bar, k * 0.5, 1.25, vs[half][idx], 56 if k in (0, 3, 6) else 46)
        for h, c in enumerate(cs):
            s.play(1, bar, h * 2, 4 / len(cs), pads.next(c), 46, spread=2)
            r = bass.note(c)
            if len(cs) == 1:
                s.play(2, bar, 0, 1.4, r, 74)
                s.play(2, bar, 1.5, 2.3, r + 7, 60)
            else:
                s.play(2, bar, h * 2, 1.8, r, 72)
    put(s, 4, Tune(rng, 'F', 'major', 65, 81).period(b0, a1 + a2, CALM, CALM_END), 80)
    put(s, 3, Tune(rng, 'F', 'major', 67, 82).period(b0 + 16, b, CALM, CALM_END), 82)
    put(s, 4, Tune(rng, 'F', 'major', 65, 81).period(b0 + 24, a3, CALM, CALM_END), 78)
    return b0 + 32


def letters_home(s, b0, rng):
    """IV, A major: the vibraphone and a second piano answer each other, two bars at a time"""
    a1 = bars_of('Amaj9 | F#m9 | Dmaj9 | E9sus4 | C#m7 | F#m9 | Bm9 | E9sus4')
    a2 = bars_of('Amaj9 | F#m9 | Dmaj9 | Dm6 | C#m7 | F#m9 | Bm9 | E9sus4')
    b = bars_of('Dmaj9 | C#m7 | Bm9 | Amaj9 | Dmaj9 | C#m7 | Bm9 | E7sus4')
    a3 = bars_of('Amaj9 | F#m9 | Dmaj9 | Dm6 | A/E | F#m9 | Bm9 | A9sus4')
    harmony = a1 + a2 + b + a3
    keys, pads, bass = Voicer(52, 69, 4), Voicer(50, 69, 3, 16), Bass(28, 45)
    for i, syms in enumerate(harmony):
        bar, c = b0 + i, chord(syms[0])
        v = keys.next(c)
        s.play(0, bar, 0, 1.9, v, 46)
        s.play(0, bar, 2, 1.9, v, 40)
        s.play(1, bar, 0, 4, pads.next(c), 44, spread=2)
        r = bass.note(c)
        s.play(2, bar, 0, 1.9, r, 72)
        s.play(2, bar, 2, 1.9, r + 7 if r + 7 <= 47 else r - 5, 62)
    notes = Tune(rng, 'A', 'major', 64, 81).period(b0, harmony, CALM, CALM_END, phrase=2)
    put(s, 3, [n for n in notes if (n[0] - b0) // 2 % 2 == 0], 82)     # the call
    put(s, 5, [n for n in notes if (n[0] - b0) // 2 % 2 == 1], 72)     # the answer
    return b0 + 32


Q_A_SYMS = bars_of('Dmaj9 | Bm11 | Gmaj9 | Gm6 | D/F# | Em9 | Gmaj9 | A7sus4')
Q_B_SYMS = bars_of('Bm9 | F#m7 | Gmaj7#11 | D/F# | Em9 | Bm11 | Cmaj7#11 | A7sus4')
SPARSE = [[(2, 2)], [(0, 3)], [], [(1, 1), (2, 2)], [(0, 2)]]
SPARSE_END = [[(0, 4)], [(0, 2)]]
LONG = [[(0, 4)], [(2, 2)], [(0, 2), (2, 2)], [(1, 3)]]


def bunk_light_again(s, b0, rng):
    """V, D major: the tune on the flute, the middle on the vibraphone, then the
    piano alone with a sparkle, thinning out into the first bars"""
    harmony = Q_A + Q_B + Q_A + Q_A
    for i, (bass, keys, pad) in enumerate(harmony):
        bar, keys, pad = b0 + i, keys.split(), pad.split()
        if i < 24:
            for k, idx in enumerate([0, 1, 2, 3, 1, 2, 3, 2]):
                s.play(0, bar, k * 0.5, 1.5, keys[idx], (58 if k % 4 == 0 else 48) - (4 if i >= 16 else 0))
        else:
            for k in range(4):
                s.play(0, bar, k, 2.5, keys[k], 50 if k == 0 else 44)
        s.play(1, bar, 0, 4, pad, 50 if i < 24 else 42, spread=2)
        s.play(2, bar, 0, 2.5, bass, 74 if i < 24 else 66)
        s.play(2, bar, 3, 1, p(bass) + 7 if i % 2 == 0 else p(bass) + 12, 56)
    s.tune(4, b0, Q_TUNE_A, 82)
    s.tune(3, b0 + 8, Q_TUNE_B, 84)
    put(s, 4, Tune(rng, 'D', 'major', 62, 74).period(b0 + 8, Q_B_SYMS, LONG, SPARSE_END, answer=False), 64)
    put(s, 6, Tune(rng, 'D', 'major', 76, 90).period(b0 + 16, Q_A_SYMS, SPARSE, SPARSE_END, answer=False), 58)


# --- BAR: "Off-Watch" ---

CH = {  # the piano's rootless voicings, and the chord tones the bass walks on
    'Fmaj9': ('A3 C4 E4 G4', 'F2 A2 C3'), 'Dm9': ('F3 A3 C4 E4', 'D2 F2 A2'),
    'Gm9': ('F3 A3 Bb3 D4', 'G1 Bb1 D2'), 'C13': ('Bb3 D4 E4 A4', 'C2 E2 G2'),
    'Am7': ('G3 B3 C4 E4', 'A1 C2 E2'), 'D9': ('F#3 A3 C4 E4', 'D2 F#2 A2'),
    'Bbmaj9': ('A3 C4 D4 F4', 'Bb1 D2 F2'), 'Bbm9': ('Ab3 C4 Db4 F4', 'Bb1 Db2 F2'),
    'Eb13': ('G3 C4 Db4 F4', 'Eb2 G2 Bb2'), 'Am9': ('G3 B3 C4 E4', 'A1 C2 E2'),
}
# a bar is one chord or two (two beats each)
B_A1 = ['Fmaj9', 'Dm9', 'Gm9', 'C13', 'Am7', 'D9', 'Gm9', 'C13']
B_A2 = ['Fmaj9', 'Dm9', 'Gm9', 'C13', 'Am7 D9', 'Gm9 C13', 'Fmaj9', 'Fmaj9']
B_BR = ['Bbmaj9', 'Bbm9 Eb13', 'Am9', 'D9', 'Gm9', 'C13', 'Am7 D9', 'Gm9 C13']
B_A3 = ['Fmaj9', 'Dm9', 'Gm9', 'C13', 'Am7 D9', 'Gm9 C13', 'Fmaj9', 'Gm9 C13']
B_TUNE_A = ['C5:0:1 A4:1:.5 C5:1.5:.5 E5:2:1.5 D5:3.5:.5',
            'C5:0:1.5 A4:1.5:.5 F4:2:1 E4:3:1',
            'F4:.5:.5 A4:1:.5 Bb4:1.5:.5 D5:2:1.5 C5:3.5:.5',
            'A4:0:2.5 G4:3:.5 A4:3.5:.5',
            'C5:0:1 B4:1:.5 C5:1.5:.5 E5:2:1.5 D5:3.5:.5',
            'C5:0:1 A4:1:.5 F#4:1.5:1.5 A4:3.5:.5',
            'Bb4:0:1 A4:1:.5 G4:1.5:.5 F4:2:1 D4:3:1',
            'E4:0:3 C5:3.5:.5']
B_TUNE_A2 = B_TUNE_A[:4] + ['E5:0:1 D5:1:.5 C5:1.5:.5 A4:2:1 F#4:3:1',
                            'Bb4:0:1 D5:1:1 E5:2:1.5 G4:3.5:.5',
                            'A4:0:.5 G4:.5:.5 F4:1:3',
                            'F4:2.5:.5 A4:3:.5 C5:3.5:.5']
B_TUNE_BR = ['D5:0:2 C5:2:.5 A4:2.5:1.5',
             'Db5:0:1.5 C5:1.5:.5 Bb4:2:1 G4:3:1',
             'C5:0:2 B4:2:.5 A4:2.5:.5 G4:3:1',
             'F#4:0:2 A4:2:1 C5:3:1',
             'Bb4:0:1.5 A4:1.5:.5 Bb4:2:.5 D5:2.5:1.5',
             'E5:0:2.5 D5:3:.5 C5:3.5:.5',
             'C5:0:1 A4:1:1 F#4:2:1 A4:3:1',
             'G4:0:1 Bb4:1:1 E4:2:1.5 C5:3.5:.5']
B_TUNE_A3 = B_TUNE_A[:6] + ['A4:0:.5 G4:.5:.5 F4:1:2', '']
# the guitar's turn on the bridge the second time: eighths, around the chords
B_SOLO_BR = ['F4:0:.5 A4:.5:.5 C5:1:.5 D5:1.5:.5 F5:2:1 E5:3:.5 D5:3.5:.5',
             'Db5:0:.5 C5:.5:.5 Ab4:1:.5 F4:1.5:.5 G4:2:.5 Bb4:2.5:.5 Db5:3:1',
             'C5:0:1.5 B4:1.5:.5 A4:2:.5 G4:2.5:.5 E4:3:1',
             'F#4:0:.5 A4:.5:.5 C5:1:.5 E5:1.5:.5 D5:2:2',
             'D5:0:.5 Bb4:.5:.5 A4:1:.5 G4:1.5:.5 F4:2:.5 G4:2.5:.5 A4:3:.5 Bb4:3.5:.5',
             'C5:0:1 Bb4:1:.5 A4:1.5:.5 G4:2:1 E4:3:1',
             'E4:0:.5 G4:.5:.5 C5:1:1 C5:2:.5 A4:2.5:.5 F#4:3:1',
             'G4:0:1 A4:1:.5 Bb4:1.5:.5 E4:2:1.5 C5:3.5:.5']
COMP = [[(0, 1.5), (1.5, 1.5)], [(0.5, 1), (2.5, 1)], [(0, 2), (2.5, 1), (3.5, .5)], [(1, 1), (2.5, 1.5)]]


def bar_song():
    s = Comp(100, seed=7, swing=0.64)
    s.setup(0, 5, 88)       # electric piano, the warm one
    s.setup(1, 33, 104)     # fingered bass
    s.setup(2, 26, 100)     # jazz guitar
    s.setup(3, 11, 74)      # vibraphone (the user: its bell was too far forward; it sits back in the band)
    s.setup(4, 16, 54)      # organ, under the second bridge
    s.setup(5, 4, 86)       # electric piano with the tine, for its solo in IV
    s.setup(6, 24, 92)      # nylon guitar (the bossa nova)
    s.setup(7, 73, 92)      # flute
    s.setup(9, 0, 96)       # drums
    form = B_A1 + B_A2 + B_BR + B_A3
    harmony = form + form
    whole = [h.split() for h in harmony] + BAR_LATER          # (every bar's chords: the bass walks into the next)
    for bar, chords in enumerate(harmony):
        chords = chords.split()
        second = bar >= 32
        # the piano comps: one of four rhythms, a chord a hit (two chords: one each)
        if len(chords) == 1:
            for beat, dur in COMP[(bar * 3 + (bar >> 2)) % 4]:
                s.play(0, bar, beat, dur, CH[chords[0]][0].split(), 62 if second else 58)
        else:
            for i, c in enumerate(chords):
                s.play(0, bar, i * 2 + (0.5 if bar % 2 else 0), 1.5, CH[c][0].split(), 60)
        # the bass walks: root, chord tones, then a step into the next bar's root
        nxt = whole[(bar + 1) % len(whole)][0]
        target = p(CH[nxt][1].split()[0])
        walk = []
        for i, c in enumerate(chords):
            tones = [p(x) for x in CH[c][1].split()]
            if len(chords) == 2:
                walk += [tones[0], tones[1 + (bar + i) % 2]]
            else:
                walk += [tones[0], tones[1], tones[2]] if bar % 2 == 0 else [tones[0], tones[2], tones[0] + 12]
        if len(walk) < 4:
            # a half step into the next root, from above or below, in the octave nearest the line
            step = target + (1 if bar % 3 else -1)
            walk.append(min((step - 12, step, step + 12), key=lambda q: abs(q - walk[-1])))
        walk = walk[:4]
        for beat, q in enumerate(walk):
            s.play(1, bar, beat, 0.9, q, 84 if beat == 0 else 74)
        if bar % 4 == 3:
            s.play(1, bar, 2.5, 0.4, walk[2] + 12 if walk[2] < 40 else walk[2] - 12, 60)    # a skip into the turn
        # drums: ride ding, ding-da; foot hat on 2 and 4; cross-stick on 2 and 4; a soft kick
        for beat in (0, 1, 1.5, 2, 3, 3.5):
            s.play(9, bar, beat, 0.2, 51, 56 if beat in (1, 3) else 46, spread=5)
        for beat in (1, 3):
            s.play(9, bar, beat, 0.2, 44, 50)
            s.play(9, bar, beat, 0.2, 37, 66 if second else 60)
        s.play(9, bar, 0, 0.2, 36, 50)
        if bar % 8 == 7:
            s.play(9, bar, 2.5, 0.2, 36, 44)
            s.play(9, bar, 3.5, 0.2, 37, 54)
        else:
            s.play(9, bar, 2, 0.2, 36, 40)
    # the tune: the guitar the first time, the vibraphone the second (the guitar on the bridge)
    for chorus, lead in ((0, 2), (32, 3)):
        v = 88 if lead == 2 else 74                 # (the vibraphone struck softer: a mellower bell)
        s.tune(lead, chorus, B_TUNE_A, v)
        s.tune(lead, chorus + 8, B_TUNE_A2, v)
        s.tune(2, chorus + 16, B_TUNE_BR if chorus == 0 else B_SOLO_BR, 86)
        s.tune(lead, chorus + 24, B_TUNE_A3, v)
    for bar in range(48, 56):                       # the organ under the second bridge
        chords = B_BR[bar - 48].split()
        s.play(4, bar, 0, 4, CH[chords[0]][0].split(), 70, spread=2)
    later(s, 64, whole, random.Random(2077))
    return s


# The later sections of the bar song: their chords bar by bar (one or two a bar)
SS_A1 = bars_of('Bbmaj9 | Gm9 | Cm9 | F13 | Dm7 | G7b9 | Cm9 | F13')
SS_A2 = bars_of('Bbmaj9 | Gm9 | Cm9 | F13 | Fm9 Bb13 | Ebmaj9 | Cm9 F13 | Bbmaj9')
SS_B = bars_of('D7b9 | Gm9 | C9 | Cm9 F13 | Ebmaj9 | Ebm6 | Dm7 G7b9 | Cm9 F13')
SECOND_SHIFT = SS_A1 + SS_A2 + SS_B + SS_A2 + SS_A1 + SS_A2 + SS_B + SS_A2[:-1] + [['Fm9', 'Bb13']]
LIBERTY = bars_of('Ebmaj9 | Ebmaj9 | Fm9 | Bb13 | Gm7 | C7b9 | Fm9 | Bb13 | Abmaj9 | Db13 | Gm7 | C7b9 | Fm9 | Bb13 '
                  '| Ebmaj9 | Bb13sus4')
LIBERTY_PORT = LIBERTY * 2 + LIBERTY[:-1] + [['D7b9']]
BLUES = bars_of('Gm9 | Gm9 | Gm9 | Gm9 | Cm9 | Cm9 | Gm9 | Gm9 | Eb9 | D7b9 | Gm9 | D7b9')
CARGO_BLUES = BLUES * 5 + BLUES[:-1] + [['Gm9', 'C13']]
REPRISE = [h.split() for h in B_A1 + B_A2 + B_BR + B_A3] * 2
BAR_LATER = SECOND_SHIFT + LIBERTY_PORT + CARGO_BLUES + REPRISE


def swing_drums(s, bar, lift=False, fill=False):
    """ride ding, ding-da; foot hat on 2 and 4; cross-stick on 2 and 4; a soft kick"""
    for beat in (0, 1, 1.5, 2, 3, 3.5):
        s.play(9, bar, beat, 0.2, 51, 56 if beat in (1, 3) else 46, spread=5)
    for beat in (1, 3):
        s.play(9, bar, beat, 0.2, 44, 50)
        s.play(9, bar, beat, 0.2, 37, 66 if lift else 60)
    s.play(9, bar, 0, 0.2, 36, 50)
    if fill:
        s.play(9, bar, 2.5, 0.2, 36, 44)
        s.play(9, bar, 3.5, 0.2, 37, 54)
    else:
        s.play(9, bar, 2, 0.2, 36, 40)


def bossa_drums(s, bar):
    """rim on the clave (three then two), the shaker's eighths, a soft kick"""
    for beat in ((0, 1.5, 3) if bar % 2 == 0 else (1, 2.5)):
        s.play(9, bar, beat, 0.2, 37, 58)
    for k in range(8):
        s.play(9, bar, k * 0.5, 0.2, 70, 40 if k % 2 == 0 else 32, spread=3)
    for beat in (0, 1.5, 2, 3.5):
        s.play(9, bar, beat, 0.2, 36, 46 if beat in (0, 2) else 36)
    for beat in (1, 3):
        s.play(9, bar, beat, 0.2, 44, 34)


def comp(s, ch, bar, syms, keys, vel):
    """the piano's comping: one of four rhythms, or a chord for each half"""
    if len(syms) == 1:
        v = keys.next(chord(syms[0]))
        for beat, dur in COMP[(bar * 3 + (bar >> 2)) % 4]:
            s.play(ch, bar, beat, dur, v, vel)
    else:
        for h, x in enumerate(syms):
            s.play(ch, bar, h * 2 + (0.5 if bar % 2 else 0), 1.5, keys.next(chord(x)), vel)


def walk(s, bar, syms, nxt, bass):
    """root, chord tones, then a half step into the next bar's root"""
    line = []
    for h, x in enumerate(syms):
        c = chord(x)
        r = bass.note(c)
        third = r + (3 if 3 in c.tones else 5 if 5 in c.tones and 4 not in c.tones else 4)
        fifth = r + (6 if 6 in c.tones and 7 not in c.tones else 7)
        if len(syms) == 2:
            line += [r, fifth if (bar + h) % 2 else third]
        else:
            line += [r, third, fifth] if bar % 2 == 0 else [r, fifth, r + 12 if r + 12 <= 48 else r - 5]
    if len(line) < 4:
        target = chord(nxt).bass
        step = [q + d for q in range(26, 50) if q % 12 == target for d in (-1, 1)]
        line.append(min(step, key=lambda q: abs(q - line[-1])))
    for beat, q in enumerate(line[:4]):
        s.play(1, bar, beat, 0.9, q, 84 if beat == 0 else 74)


def bossa_bass(s, bar, syms, bass):
    """root and fifth, a dotted quarter and an eighth, twice"""
    for h in range(2):
        c = chord(syms[min(h, len(syms) - 1)])
        r = bass.note(c)
        s.play(1, bar, h * 2, 1.4, r, 80)
        s.play(1, bar, h * 2 + 1.5, 0.45, r + 7 if r + 7 <= 47 else r - 5, 66)


def bossa_guitar(s, bar, syms, keys):
    pattern = [(0, 1), (1.5, .5), (2.5, 1), (3.5, .5)] if bar % 2 == 0 else [(.5, 1), (1.5, 1), (3, 1)]
    vs = [keys.next(chord(x)) for x in syms]
    for beat, dur in pattern:
        s.play(6, bar, beat, dur, vs[1 if beat >= 2 and len(vs) == 2 else 0], 60 if beat in (0, 1.5) else 52)


def later(s, b0, whole, rng):
    """II to V of the bar song, from bar b0; whole: every bar's chords"""
    keys, organ, bass = Voicer(52, 70, 4), Voicer(50, 67, 3, 16), Bass(31, 45)
    nylon = Voicer(55, 72, 4)
    nxt = lambda bar: whole[(bar + 1) % len(whole)][0]
    # II, Second Shift (B flat): the vibraphone's tune, then a guitar chorus
    ss = b0
    for i, syms in enumerate(SECOND_SHIFT):
        bar = ss + i
        comp(s, 0, bar, syms, keys, 60 if i >= 32 else 56)
        walk(s, bar, syms, nxt(bar), bass)
        swing_drums(s, bar, lift=i >= 32, fill=i % 8 == 7)
    t = Tune(rng, 'Bb', 'major', 65, 81)
    head_a1 = t.period(ss, SS_A1, SWING, SWING_END)
    head_a2 = shift(head_a1, 8)[:sum(1 for n in head_a1 if n[0] < ss + 4)] + t.phrase(ss + 12, SS_A2[4:], SWING, SWING_END, shape='fall')
    head_b = t.period(ss + 16, SS_B, SWING, SWING_END)
    put(s, 3, head_a1 + head_a2 + head_b + shift(head_a2, 16), 74)
    put(s, 2, Tune(rng, 'Bb', 'major', 60, 79).period(ss + 32, SECOND_SHIFT[32:], SOLO, SOLO_END), 74)
    # III, Liberty Port (E flat): a bossa nova, straight eighths
    lp = ss + len(SECOND_SHIFT)
    s.swing = 0.5
    for i, syms in enumerate(LIBERTY_PORT):
        bar = lp + i
        bossa_guitar(s, bar, syms, nylon)
        bossa_bass(s, bar, syms, bass)
        bossa_drums(s, bar)
    t = Tune(rng, 'Eb', 'major', 63, 79)
    first = t.period(lp, LIBERTY, BOSSA, BOSSA_END)
    put(s, 7, first, 84)
    put(s, 3, Tune(rng, 'Eb', 'major', 65, 82).period(lp + 16, LIBERTY, BOSSA, BOSSA_END), 72)
    # the flute's tune again, its last phrase bent to the turn into G minor
    put(s, 7, [n for n in shift(first, 32) if n[0] < lp + 44], 82)
    put(s, 7, t.phrase(lp + 44, LIBERTY_PORT[44:], BOSSA, BOSSA_END, shape='fall'), 82)
    s.swing = 0.64
    # IV, Cargo Blues (G minor): the head twice, three choruses, the head again
    cb = lp + len(LIBERTY_PORT)
    for i, syms in enumerate(CARGO_BLUES):
        bar, chorus = cb + i, i // 12
        solo = chorus in (2, 3, 4)
        comp(s, 0, bar, syms, keys, 50 if solo else 58)
        walk(s, bar, syms, nxt(bar), bass)
        swing_drums(s, bar, lift=solo, fill=i % 12 == 11)
        if solo:
            s.play(4, bar, 0, 4, organ.next(chord(syms[0])), 62, spread=2)
    t = Tune(rng, 'G', 'blues', 62, 79)
    head = t.period(cb, BLUES, SWING, SWING_END)
    put(s, 2, head + shift(head, 12), 88)
    put(s, 3, Tune(rng, 'G', 'blues', 63, 82).period(cb + 24, BLUES, SOLO, SOLO_END), 74)
    put(s, 5, Tune(rng, 'G', 'blues', 60, 79).period(cb + 36, BLUES, SOLO, SOLO_END), 80)
    put(s, 2, Tune(rng, 'G', 'blues', 60, 81).period(cb + 48, BLUES, SOLO, SOLO_END), 76)
    put(s, 2, [n for n in shift(head, 60) if n[0] < cb + 68], 88)
    put(s, 2, t.phrase(cb + 68, CARGO_BLUES[68:], SWING, SWING_END, shape='fall'), 88)
    # V, Off-Watch's changes again (F): the vibraphone over the organ, the old
    # bridge on the guitar; then guitar and vibraphone four bars each, into the top
    rp = cb + len(CARGO_BLUES)
    for i, syms in enumerate(REPRISE):
        bar = rp + i
        comp(s, 0, bar, syms, keys, 58)
        walk(s, bar, syms, nxt(bar), bass)
        swing_drums(s, bar, lift=i >= 32, fill=i % 8 == 7)
        if i < 32:
            s.play(4, bar, 0, 4, organ.next(chord(syms[0])), 58, spread=2)
    vib = Tune(rng, 'F', 'major', 65, 84)
    put(s, 3, vib.period(rp, REPRISE[:16], SOLO, SOLO_END) + vib.period(rp + 24, REPRISE[24:32], SOLO, SOLO_END), 74)
    s.tune(2, rp + 16, B_TUNE_BR, 86)
    gtr = Tune(rng, 'F', 'major', 60, 81)
    for k in range(8):
        part = REPRISE[32 + k * 4: 36 + k * 4]
        tune = gtr if k % 2 == 0 else vib
        put(s, 2 if k % 2 == 0 else 3, tune.phrase(rp + 32 + k * 4, part, SOLO, SOLO_END), 76 if k % 2 == 0 else 74)


# --- BRIEF: "Ready Room" ---

# bar: bass, the harp's four notes, the strings' three (a bar may change halfway: 'x | y')
R_A = [('D2', 'D3 A3 E4 F4', 'D3 A3 F4'),                     # Dm(add9)
       ('Bb1', 'D3 A3 D4 F4', 'F3 A3 D4'),                    # Bbmaj7
       ('G1', 'D3 A3 Bb3 F4', 'G3 Bb3 D4'),                   # Gm9
       ('A1', 'E3 A3 D4 G4 | E3 G3 C#4 E4', 'E3 A3 D4 | E3 G3 C#4'),   # A7sus4, A7
       ('D2', 'D3 A3 E4 F4', 'D3 A3 F4'),                     # Dm(add9)
       ('Bb1', 'D3 A3 E4 F4', 'F3 A3 E4'),                    # Bbmaj7#11
       ('C2', 'E3 A3 C4 F4', 'C3 A3 E4'),                     # F/C
       ('A1', 'E3 A3 D4 E4 | E3 A3 C#4 E4', 'A2 E3 D4 | A2 E3 C#4')]   # Asus4, A
R_C = [('Bb1', 'D3 A3 C4 F4', 'F3 A3 D4'),                    # Bbmaj7
       ('C2', 'E3 G3 D4 G4', 'E3 G3 D4'),                     # C(add9)
       ('A1', 'E3 G3 C4 E4', 'E3 A3 C4'),                     # Am7
       ('D2', 'F3 A3 C4 E4', 'F3 A3 D4'),                     # Dm9
       ('G1', 'D3 A3 Bb3 F4', 'Bb2 F3 D4'),                   # Gm9
       ('F1', 'D3 A3 D4 F4', 'A2 F3 D4'),                     # Dm/F
       ('Eb2', 'G3 Bb3 D4 A4', 'Eb3 Bb3 D4'),                 # Ebmaj7#11
       ('A1', 'E3 A3 D4 G4 | E3 G3 C#4 E4', 'E3 A3 D4 | E3 G3 C#4')]   # A7sus4, A7
R_HORN = ['A4:0:2 F4:2:1 E4:3:1',
          'D4:0:2.5 C4:2.5:.5 D4:3:1',
          'F4:0:1.5 G4:1.5:.5 A4:2:1 Bb4:3:1',
          'A4:0:2 C#5:2:2',
          'D5:0:3 C5:3:.5 Bb4:3.5:.5',
          'A4:0:2 E4:2:2',
          'F4:0:1 G4:1:1 A4:2:1 C5:3:1',
          'D5:0:2 C#5:2:2',
          'D5:0:1.5 C5:1.5:.5 A4:2:2',
          'G4:0:1 A4:1:1 C5:2:1 D5:3:1',
          'E5:0:3 D5:3:1',
          'C5:0:1.5 A4:1.5:.5 F4:2:2',
          'G4:0:1 A4:1:1 Bb4:2:2',
          'A4:0:2 F4:2:2',
          'G4:0:2 A4:2:2',
          'A4:0:4']
R_HORN_END = ['', '', '', '', 'D5:0:4', 'C5:0:2 Bb4:2:2', 'A4:0:4', 'A4:0:2']
R_CLARINET = ['F4:2:1 E4:3:1',
              'D4:0:2 F4:2:2',
              'Bb4:0:1 A4:1:1 G4:2:1 F4:3:1',
              'E4:0:2 C#4:2:2',
              'D4:0:3 E4:3:1',
              'F4:0:2 E4:2:2',
              'A4:0:2 G4:2:2',
              'E4:0:2 C#4:2:2']
R_ROCK = [0, 1, 2, 1, 3, 1, 2, 1]              # the harp's eighths over its four notes


def brief_song():
    s = Comp(72, seed=5)
    s.setup(0, 46, 92)      # harp
    s.setup(1, 48, 80)      # strings
    s.setup(2, 33, 96)      # bass
    s.setup(3, 60, 100)     # horn
    s.setup(4, 71, 90)      # clarinet
    s.setup(5, 47, 96)      # timpani
    s.setup(6, 8, 72)       # celesta
    harmony = R_A + R_A + R_C + R_A
    for bar, (bass, harp, strings) in enumerate(harmony):
        halves = [h.split() for h in harp.split('|')]
        shalves = [h.split() for h in strings.split('|')]
        for k, i in enumerate(R_ROCK):
            notes = halves[0] if k < 4 or len(halves) == 1 else halves[1]
            s.play(0, bar, k * 0.5, 0.9, notes[i], (58 if k % 4 == 0 else 48) + (4 if bar >= 16 else 0))
        if bar >= 4:
            for h, sv in enumerate(shalves):
                s.play(1, bar, h * 2, 4 / len(shalves), sv, 56 if bar < 16 else 62, spread=2)
        s.play(2, bar, 0, 3.8, bass, 70)
        if bar % 4 == 2:
            s.play(6, bar, 2, 2, 'A5' if bar % 8 == 2 else 'E6', 44)    # the far ping
        if bar % 8 == 7:                                                  # a timpani roll into the next phrase
            for k in range(8):
                s.play(5, bar, 2 + k * 0.25, 0.25, 'A2', 34 + k * 5, spread=2)
        if bar % 16 == 0:
            s.play(5, bar, 0, 1, 'D2', 66)
    s.tune(3, 8, R_HORN, 86)
    s.tune(4, 24, R_CLARINET, 78)
    s.tune(3, 24, R_HORN_END, 76)
    return s


SONGS = {'QUARTERS.MID': quarters_song, 'BAR.MID': bar_song, 'BRIEF.MID': brief_song}
