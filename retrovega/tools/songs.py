"""The three room songs, written for our OPL3 bank (tools/oplbank.py); CC0, ours.

    QUARTERS.MID  "Bunk Light": the quarters and the locker.  Calm: 68 a
                  minute in D major, an electric piano turning over in
                  eighths under a wide pad, an upright bass, the vibraphone
                  with the tune, a flute answering it the last time round.
    BAR.MID       "Off-Watch": the bar and the lounge.  Warm, moving, still
                  easy: 100 a minute, swung, in F; a jazz guitar with the
                  tune over electric-piano comping, a walking bass, ride and
                  cross-stick; the second chorus gives the tune to the
                  vibraphone and puts an organ under the bridge.
    BRIEF.MID     "Ready Room": the briefing.  Pensive: 72 a minute in D
                  minor, a harp ostinato that never stops, strings, a horn
                  with the long line, a clarinet against it, timpani at the
                  ends of phrases and a far-off celesta ping.

Each is a loop: its last bar leads back into its first.  midi.py writes them
with the other cues.  Notes are written bar by bar as 'PITCH:BEAT:LENGTH'
(beats from the bar's start, lengths in beats); the bar song swings its
off-beat eighths.
"""
import random

import midi

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
    return s


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
    s.setup(3, 11, 96)      # vibraphone
    s.setup(4, 16, 54)      # organ, under the second bridge
    s.setup(9, 0, 96)       # drums
    form = B_A1 + B_A2 + B_BR + B_A3
    harmony = form + form
    bars = len(harmony)
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
        nxt = harmony[(bar + 1) % bars].split()[0]
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
        s.tune(lead, chorus, B_TUNE_A, 88)
        s.tune(lead, chorus + 8, B_TUNE_A2, 88)
        s.tune(2, chorus + 16, B_TUNE_BR if chorus == 0 else B_SOLO_BR, 86)
        s.tune(lead, chorus + 24, B_TUNE_A3, 88)
    for bar in range(48, 56):                       # the organ under the second bridge
        chords = B_BR[bar - 48].split()
        s.play(4, bar, 0, 4, CH[chords[0]][0].split(), 70, spread=2)
    return s


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
