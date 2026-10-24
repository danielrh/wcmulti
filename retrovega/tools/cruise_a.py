"""Four of the third draft's cruise pieces (CC0, ours): Wide Orbit, Sensor Sweep, Far Light, Deep Lane.

The user (2026-10-24), on Outbound: "The proof piece hits on all counts... fix all the flight music to have
that same style but their own unique character and be remixable and loopable".  So these are written as it
was (tools/flightsongs.py outbound(), tools/orch.py): a quick motor under a tune in long notes, a few parts each
with one job, keys that move by thirds, hits where a groove would be, runs and sweeps, a riser into the loop,
a block every four bars marked for the score's remixes.  Each has its own motor, tune and colour.

The cruise cycle (src/score.c plays them in turn, each riser on its piece's dominant, the next piece's top in
the key it leads to): E (Outbound), G minor (Wide Orbit), B minor (Sensor Sweep), D (Far Light), F minor
(Deep Lane), then cruise_b.py's A minor, C and G, and round to E.
"""
from compose import chord
from songs import p
from orch import (NAMES, DRUM, Piece, hits, up, scale_run, ORCH, ACC332, orchestra, in_range, octaves, pulses,
                  pedal332, hit, held, sung, sweep, MAJOR, LYDIAN)

THIRD, SIXTH = 1 / 3, 1 / 6             # in 12/8 (four dotted beats a bar): an eighth, a sixteenth


def chans(s):
    """the parts' channels: horn, horn2, motor, low, hit, brass, piccolo, timpani, harp, strings, bass"""
    ch = orchestra(s)
    return tuple(ch[k] for k in ('horn', 'horn2', 'motor', 'low', 'hit', 'brass', 'piccolo', 'timpani', 'harp',
                                 'strings', 'bass'))


def scale_up(lo, root, steps, count):
    """so many notes of a scale (its root's pitch class, the steps from it) from lo upward"""
    pcs = {(root + k) % 12 for k in steps}
    out, q = [], lo
    while len(out) < count:
        if q % 12 in pcs:
            out.append(q)
        q += 1
    return out


# --- CRUISE2: "Wide Orbit" (G minor, 12/8 at 116) -----------------------------------
# Wonder, turning slowly.  The harp rolls two octaves up its chord and down again in eighths, the spiccato
# turns under it (root, fifth, third, fifth), and the horn sings in long swelling notes.  Its four blocks go
# round the minor-third cycle, G minor, B flat, D flat, E minor, each a third away from the last, and the
# fourth turns back through C major and C minor to D: the harp sweeps up on D into the top.  The second pass
# has the tune on the strings in octaves with the horn under it, hits on the dotted beats, a brass chord under
# the last block, piccolo runs.

WO_PASS = (['Gm', 'Ebmaj7', 'Cm9', 'D7sus4 D'] + ['Bb', 'Dm7', 'Ebmaj7', 'Ab']
           + ['Db', 'Bbm7', 'Gbmaj7', 'B'] + ['Em', 'C', 'Cm', 'D'])
WO_TUNE = [['D4:0:3 G4:3:1', 'G4:0:2 Bb4:2:1 D5:3:1', 'C5:0:3 Bb4:3:1', 'A4:0:2 F#4:2:2'],
           ['F4:0:3 Bb4:3:1', 'A4:0:2 C5:2:1 F5:3:1', 'G5:0:3 F5:3:1', 'Eb5:0:2 C5:2:2'],
           ['Ab4:0:3 Db5:3:1', 'Db5:0:2 F5:2:1 Ab5:3:1', 'Bb5:0:3 F5:3:1', 'F#5:0:2 D#5:2:2'],
           ['E5:0:3 B4:3:1', 'G5:0:2 E5:2:1 C5:3:1', 'Eb5:0:3 C5:3:1', 'D5:0:4']]
WO_UNDER = [['Bb3:0:4', 'Bb3:0:4', 'G3:0:4', 'A3:0:2 F#3:2:2'], ['D4:0:4', 'F4:0:4', 'Eb4:0:4', 'C4:0:4'],
            ['F4:0:4', 'Db4:0:4', 'Db4:0:4', 'D#4:0:4'], ['G4:0:4', 'E4:0:4', 'Eb4:0:4', 'F#4:0:4']]


def wide_orbit():
    form = ['Gm'] + WO_PASS + WO_PASS
    s = Piece(116, 2116, form, level=1.0)
    hn, hn2, mo, lo, hi_, br, pc, tp, hp, st, bs = chans(s)
    n = len(form)                                   # 1 + 16 + 16 = 33 bars, 68 s
    s.loop_at(1)
    s.arp(hp, range(0, n), [0, 1, 2, 3, 4, 5, 6, 5, 4, 3, 2, 1], THIRD, 55, 70, 4, 52, accent=6, gate=0.6)
    s.arp(mo, range(0, n), [0, 2, 1, 2], THIRD, 43, 58, 3, 58, accent=8, gate=0.6)
    s.ramp(mo, 0, 1, 50, 127)                       # the intro: the harp and the spiccato coming up
    s.ramp(hp, 0, 1, 60, 127)
    s.play(tp, 0, 0, 1, 'G2', 76)
    s.bass(bs, range(1, n), [(0, 1.9, 'r'), (2, 1.9, '5')], 72, lo=31, hi=43)
    for P, second in ((1, False), (17, True)):
        B1, B2, B3, B4 = P, P + 4, P + 8, P + 12
        for b in (B1, B2, B3, B4):
            s.sect(b)
        if not second:
            sung(s, hn, B1, WO_TUNE[0], 80)
            s.play(tp, B1, 0, 1, 'G2', 84)
            sung(s, st, B2, WO_TUNE[1], 78)
            s.crash(B2, 0, 100)
            hit(s, hi_, B2, 0, 100, timpani=tp)
            s.run(pc, B2 + 3, 2, scale_up(75, 8, MAJOR, 10), SIXTH, 58, 2)      # up A flat from E flat
            sung(s, st, B3, WO_TUNE[2], 82)
            s.tune(hn, B3, [up(t, -12) for t in WO_TUNE[2]], 64)
            s.run(pc, B3 + 3, 2, scale_up(78, 11, MAJOR, 10), SIXTH, 58, 2)     # up B from F sharp
            sung(s, st, B4, WO_TUNE[3], 84)
            s.tune(hn, B4, [up(t, -12) for t in WO_TUNE[3]], 72)
            s.crash(B4, 0, 104)
            for b in (B4, B4 + 2):
                hit(s, hi_, b, 0, 104, timpani=tp)
        else:
            for k, b in enumerate((B1, B2, B3, B4)):
                if k != 1:
                    s.crash(b, 0, 106)
                    for bb in (b, b + 2):
                        for beat in (0, 8 / 3):             # the downbeat and the eighth before beat three
                            hit(s, hi_, bb, beat, 104, timpani=tp if beat == 0 else None)
            sung(s, st, B1, [up(t, 12) for t in WO_TUNE[0]], 78)
            s.tune(st, B1, WO_TUNE[0], 70)
            sung(s, hn, B1, WO_UNDER[0], 68)
            s.run(pc, B1 + 3, 2, scale_up(74, 7, (0, 2, 3, 5, 7, 9, 11), 10), SIXTH, 58, 2)   # G melodic minor
            sung(s, hn, B2, WO_TUNE[1], 82)
            sung(s, hn2, B2, WO_UNDER[1], 64)
            s.tune(pc, B2 + 2, [up(t, 12) for t in WO_TUNE[1][2:]], 50)
            sung(s, st, B3, [up(t, 12) for t in WO_TUNE[2][:2]] + WO_TUNE[2][2:], 80)
            s.tune(st, B3, WO_TUNE[2][:2], 70)
            sung(s, hn, B3, WO_UNDER[2], 68)
            s.run(pc, B3 + 3, 2, scale_up(78, 11, MAJOR, 10), SIXTH, 60, 2)
            sung(s, st, B4, [up(t, 12) for t in WO_TUNE[3][:3]] + WO_TUNE[3][3:], 82)
            s.tune(st, B4, WO_TUNE[3][:3], 72)
            s.tune(hn, B4, [up(t, -12) for t in WO_TUNE[3]], 74)
            sung(s, hn2, B4, WO_UNDER[3], 66)
            s.pad(br, range(B4, B4 + 3), 55, 70, 3, 60)
        R = B4 + 3                                  # the riser: the harp up D's chord in the last two beats
        sweep(s, hp, R, {2, 6, 9}, 50, 86, 58 if second else 52, beats=2)
        s.roll(tp, R, 0, 4, 'D2', 8, 96 if second else 72)
        if second:
            s.roll(9, R, 3, 1, DRUM['snare'], 24, 80, step=0.125)
    s.end(n)
    return s


# --- CRUISE3: "Sensor Sweep" (B minor, 160 -> 152) -----------------------------------
# Curious, searching.  Over a pizzicato pedal on B the spiccato plays pairs of notes that spread apart a
# semitone a beat, one up and one down, and close again: a scan going out and coming back.  The piccolo asks
# short questions and the muted trumpet answers them.  Then the search finds something: the key opens into G
# major for the horn's tune with the hits, climbs through A minor and C to F sharp, and the scan closes again
# over B before a snare roll and the timpani on F sharp take it round.  The second pass has the trumpet asking
# and the piccolo answering, the harp on the scan's upper notes, the tune on the strings in octaves.

SS_PASS = (['Bm', 'Bm', 'Bm', 'Bm'] + ['D', 'D', 'D', 'D'] + ['G', 'D/F#', 'Em7', 'Cmaj7']
           + ['G/B', 'Am7', 'Cmaj7#11', 'F#7'] + ['Bm', 'Bmmaj7', 'F#', 'F#'])
SS_ASK = (['B5:1:.5 C#6:1.5:.5 D6:2:1.5', None, 'F#6:0:.5 E6:.5:.5 D6:1:.5 C#6:1.5:.5 B5:2:1.5', None],
          ['D6:1:.5 E6:1.5:.5 F#6:2:1.5', None, 'A6:0:.5 G6:.5:.5 F#6:1:.5 E6:1.5:.5 D6:2:1.5', None])
SS_ANSWER = ([None, 'D4:0:1 E4:1:1 F#4:2:1.5', None, 'E4:0:.5 F#4:.5:.5 G4:1:1 F#4:2:1.5'],
             [None, 'F#4:0:1 G4:1:1 A4:2:1.5', None, 'G4:0:.5 A4:.5:.5 B4:1:1 A4:2:1.5'])
SS_TUNE = ['B4:0:3 D5:3:1', 'A4:0:2 F#4:2:1 A4:3:1', 'G4:0:3 B4:3:1', 'E5:0:4', 'D5:0:2 B4:2:1 D5:3:1',
           'C5:0:3 E5:3:1', 'F#5:0:2 E5:2:1 B4:3:1', 'A#4:0:4']
SS_UNDER = ['D4:0:4', 'D4:0:4', 'E4:0:4', 'G4:0:4', 'B3:0:4', 'A3:0:4', 'G4:0:4', 'E4:0:2 C#4:2:2']


def wedge(s, ch, bar0, centre, vel, beats=16, closing=False):
    """pairs of notes spreading apart from centre a semitone a beat, one up, one down, and closing again over
    the last half (or only closing): each pair twice, in eighths"""
    for k in range(beats):
        w = (beats - 1 - k) if closing else (k if k < beats // 2 else beats - 1 - k)
        b, beat = bar0 + k // 4, k % 4
        pair = [centre] if w == 0 else [centre - w, centre + w]
        for half in (0, 0.5):
            s.play(ch, b, beat + half, 0.42, pair, vel + (8 if half == 0 and beat in (0, 2) else 0), 3)


def pizz_pedal(s, ch, bars, note, vel):
    """the pizzicato's pedal: the note low on the 3+3+2 accents, its octave between"""
    q = p(note)
    for b in bars:
        for beat in ACC332:
            s.play(ch, b, beat, 0.4, q, vel + (8 if beat == 0 else 0), 3)
        for beat in (1, 2.5, 3.5):
            s.play(ch, b, beat, 0.4, q + 12, vel - 10, 3)


def sensor_sweep():
    form = ['Bm', 'Bm'] + SS_PASS + SS_PASS
    s = Piece(152, 3152, form, level=0.5)
    hn, hn2, mo, lo, hi_, br, pc, tp, hp, st, bs = chans(s)
    pz, mt = 12, 13
    s.setup(pz, 45, 100)                            # pizzicato
    s.setup(mt, 59, 96)                             # muted trumpet
    n = len(form)                                   # 2 + 20 + 20 = 42 bars, 66 s
    s.loop_at(2)
    pizz_pedal(s, pz, range(0, 2), 'B2', 70)        # the intro: the pedal, then the scan starting
    s.play(tp, 0, 0, 1, 'B1', 70)
    wedge(s, mo, 1, 66, 54, beats=4)
    for P, second in ((2, False), (22, True)):
        S1, S2, S3, S4, S5 = P, P + 4, P + 8, P + 12, P + 16
        for b in (S1, S2, S3, S4, S5):
            s.sect(b)
        # the scan over B, then over D: the piccolo asking, the muted trumpet answering (the other way round
        # the second time, with the harp on the scan's upper notes)
        pizz_pedal(s, pz, range(S1, S2), 'B2', 74)
        pizz_pedal(s, pz, range(S2, S3), 'D3', 74)
        wedge(s, mo, S1, 66, 58)
        wedge(s, mo, S2, 69, 60)
        for blk, k in ((S1, 0), (S2, 1)):
            s.play(tp, blk, 0, 1, in_range(chord(s.sym_at(blk, 0)).root, 33, 44), 80)
            if not second:
                s.tune(pc, blk, SS_ASK[k], 64)
                s.tune(mt, blk, SS_ANSWER[k], 78)
            else:
                s.tune(mt, blk, [up(t, -12) if t else None for t in SS_ASK[k]], 78)
                s.tune(pc, blk, [up(t, 24) if t else None for t in SS_ANSWER[k]], 58)
                hit(s, hi_, blk, 0, 96)
                for j in range(16):                 # the harp on the scan's upper notes, an octave up
                    w = j if j < 8 else 15 - j
                    s.play(hp, blk + j // 4, j % 4, 0.9, (66 if k == 0 else 69) + w + 12, 46, 3)
        # found: G major, the horn's tune (the strings' in octaves the second time) over broken chords
        s.arp(mo, range(S3, S5), [0, 1, 2, 3, 2, 1, 2, 3], 0.5, 50, 74, 4, 62)
        pedal332(s, lo, range(S3, S5), 90)
        s.bass(bs, range(S3, S5), [(0, 3.9, 'r')], 74, lo=31, hi=43)
        s.crash(S3, 0, 108)
        if second:
            s.crash(S4, 0, 104)
        for blk in (S3, S4):
            for b in (blk, blk + 2):
                for beat in (ACC332 if second else (0, 1.5)):
                    hit(s, hi_, b, beat, 110, timpani=tp if beat == 0 else None)
        if not second:
            sung(s, hn, S3, SS_TUNE, 82, e0=70, e1=124, e2=100)
        else:
            sung(s, st, S3, [up(t, 12) for t in SS_TUNE], 80, e0=70, e1=124, e2=100)
            s.tune(st, S3, SS_TUNE, 72)
            sung(s, hn, S3, SS_UNDER, 68)
            s.run(pc, S4 + 2, 2, scale_up(83, 0, LYDIAN, 8), 0.25, 60, 2)     # C lydian, B up to B
        s.run(pc, S3 + 3, 2, scale_up(76, 0, MAJOR, 8), 0.25, 58, 2)          # E up to E over C
        # the scan closing over B, then F sharp: the spiccato pulsing, a timpani roll and a snare roll
        pizz_pedal(s, pz, range(S5, S5 + 2), 'B2', 74)
        wedge(s, mo, S5, 66, 60, beats=8, closing=True)
        s.tune(pc, S5, ['D6:0:1 C#6:1:1 B5:2:2', 'A#5:0:4'], 58)
        R = S5 + 2
        pulses(s, mo, range(R, R + 2), 56)
        s.ramp(mo, R, 1, 80, 127)
        held(s, hn, R, 0, 8, 'A#3', 72, e0=40, e1=127, e2=127)
        held(s, hn2, R, 0, 8, 'F#3', 68, e0=40, e1=127, e2=127)
        s.roll(tp, R, 0, 8, 'F#2', 6, 110)
        s.roll(9, R + 1, 2, 2, DRUM['snare'], 18, 88, step=0.125)
    s.end(n)
    return s


# --- CRUISE4: "Far Light" (D major, 12/8 at 124) -------------------------------------
# Heroic wonder, the brightest of the set.  The spiccato gallops (a quarter and an eighth on every beat, the
# chord's root and fifth with the octave between) while the harp runs up and down in sixteenths, and the
# synth brass has the fanfare.  The same four bars lift a major third twice, D to F sharp to B flat, the brass
# a third higher each time; the third block turns lyrical on the strings, and the fourth comes home to D with
# everything, the harp sweeping up on A into the top.  The second pass gives the fanfare to the horns with
# brass stabs, the piccolo above the brass, the turn to the strings in octaves, and hits on every dotted beat.

FL_PASS = (['D', 'Bm7', 'Gmaj7', 'Asus4 A'] + ['F#', 'D#m7', 'Bmaj7', 'C#sus4 C#']
           + ['Bb', 'Gm7', 'Ebmaj7', 'Fsus4 F'] + ['D', 'Em7', 'Gmaj7', 'A'])
FL_CALL = ['A3:0:.667 D4:.667:.333 F#4:1:1 A4:2:2', 'B4:0:2 A4:2:1 F#4:3:1', 'G4:0:1 A4:1:1 B4:2:1 D5:3:1',
           'E5:0:2 C#5:2:2']
FL_TURN = ['F4:0:2 Bb4:2:1 D5:3:1', 'D5:0:1 C5:1:1 Bb4:2:2', 'G4:0:1 Bb4:1:1 D5:2:1 G5:3:1', 'F5:0:2 A4:2:2']
FL_TOP = ['F#5:0:3 A5:3:1', 'G5:0:2 E5:2:1 B4:3:1', 'D5:0:2 F#5:2:1 A5:3:1', 'E5:0:4']
FL_HORNS = ['D4:0:4', 'D4:0:4', 'B3:0:4', 'D4:0:2 C#4:2:2']      # (under the fanfare, the second time: up a third after)


def gallop(s, ch, bars, vel, lo=50):
    """the motor in 12/8: on every beat the chord's root and fifth for a quarter, then its octave for an eighth"""
    for b in bars:
        for beat in range(4):
            r = in_range(chord(s.sym_at(b, beat)).bass, lo, lo + 11)
            s.play(ch, b, beat, 0.45, [r, r + 7], vel + (10 if beat in (0, 2) else 0), 3)
            s.play(ch, b, beat + 2 / 3, 0.28, r + 12, vel - 6, 3)


def far_light():
    form = ['D'] + FL_PASS + FL_PASS
    s = Piece(124, 4124, form, level=0.0)
    hn, hn2, mo, lo, hi_, br, pc, tp, hp, st, bs = chans(s)
    n = len(form)                                   # 1 + 16 + 16 = 33 bars, 64 s
    s.loop_at(1)
    sparkle = [0, 1, 2, 3, 4, 5, 4, 3, 2, 1, 2, 3]  # the harp's sixteenths, half a bar
    s.arp(hp, range(0, 1), sparkle, SIXTH, 62, 79, 4, 46, accent=4, gate=0.6)    # the intro: the harp and the gallop
    gallop(s, mo, range(0, 1), 56)
    s.ramp(mo, 0, 1, 60, 127)
    s.bass(bs, range(1, n), [(0, 1.9, 'r'), (2, 1.9, '5')], 74, lo=31, hi=43)
    for P, second in ((1, False), (17, True)):
        A, B, C, D = P, P + 4, P + 8, P + 12
        for b in (A, B, C, D):
            s.sect(b)
        gallop(s, mo, range(A, C), 64)
        gallop(s, mo, range(D, D + 4), 66)
        s.arp(hp, range(A, C), sparkle, SIXTH, 62, 79, 4, 48, accent=4, gate=0.6)
        s.arp(hp, range(C, D), [0, 1, 2, 3, 2, 1], THIRD, 58, 77, 4, 50, accent=6, gate=0.6)   # calmer under the turn
        s.arp(mo, range(C, D), [0, 2, 1, 2], THIRD, 45, 60, 3, 56, gate=0.6)
        s.arp(hp, range(D, D + 3), sparkle, SIXTH, 62, 81, 4, 50, accent=4, gate=0.6)
        for blk in (A, B, C, D):
            s.crash(blk, 0, 108 if blk != C else 92)
        for blk in (A, B, D):
            for b in (blk, blk + 2):
                for beat in ((0, 4 / 3, 8 / 3) if second and blk == D else (0, 8 / 3)):
                    hit(s, hi_, b, beat, 108, timpani=tp if beat == 0 else None)
        s.play(tp, C, 0, 1, 'Bb1', 80)
        if not second:
            s.tune(br, A, FL_CALL, 86)
            s.tune(br, B, [up(t, 4) for t in FL_CALL], 88)
            sung(s, hn, B, [up(t, 4) for t in FL_HORNS], 70)
            sung(s, st, C, FL_TURN, 80)
            sung(s, hn, C, ['D4:0:4', 'Bb3:0:4', 'Bb3:0:4', 'C4:0:4'], 64)
            s.run(pc, C + 3, 2, scale_up(77, 10, MAJOR, 8), 1 / 4, 58, 2)      # up B flat from F
            s.tune(br, D, FL_TOP, 90)
            s.tune(st, D, FL_TOP, 74)
        else:
            s.tune(hn, A, FL_CALL, 86)               # the fanfare on the horns, the brass stabbing
            s.tune(hn2, A, [up(t, -12) for t in FL_CALL], 70)
            for b in range(A, B):
                for beat in (0, 2):
                    c = chord(s.sym_at(b, beat))
                    s.play(br, b, beat, 0.6, [in_range((c.root + t) % 12, 64, 75) for t in c.tones[1:3]], 72, 3)
            s.tune(pc, A + 3, [up(FL_CALL[3], 12)], 54)
            s.tune(br, B, [up(t, 4) for t in FL_CALL], 90)
            s.tune(pc, B + 2, [up(t, 16) for t in FL_CALL[2:]], 52)
            sung(s, hn, B, [up(t, 4) for t in FL_HORNS], 70)
            sung(s, st, C, [up(t, 12) for t in FL_TURN], 80)
            s.tune(st, C, FL_TURN, 72)
            sung(s, hn, C, ['D4:0:4', 'Bb3:0:4', 'Bb3:0:4', 'C4:0:4'], 66)
            for b in range(C, D):                    # the brass answering at each bar's end
                c = chord(s.sym_at(b, 8 / 3))
                s.play(br, b, 8 / 3, 1.2, [in_range((c.root + t) % 12, 62, 73) for t in c.tones[:3]], 66, 3)
            s.run(pc, C + 3, 2, scale_up(77, 10, MAJOR, 12), SIXTH, 60, 2)
            s.tune(br, D, FL_TOP, 92)
            s.tune(st, D, [up(t, 12) for t in FL_TOP[:3]], 76)
            sung(s, hn, D, ['A4:0:4', 'G4:0:4', 'F#4:0:4', 'C#4:0:4'], 70)
            s.roll(9, D + 3, 3, 1, DRUM['snare'], 24, 84, step=0.125)
        R = D + 3                                   # the riser: the harp up A's chord, the timpani rolling
        sweep(s, hp, R, {9, 1, 4}, 45, 88, 56, beats=2)
        s.roll(tp, R, 0, 4, 'A1', 8, 100 if second else 84)
    s.end(n)
    return s


# --- CRUISE5: "Deep Lane" (F minor, 144) -----------------------------------------
# Dark wonder: the far side.  The low strings strike in 3+3+2 while the spiccato turns round each chord's root
# (root, third, second, root, the fifth below, root, second, third) and the harp glimmers above.  The horn's
# tune falls through F minor's flat six (D flat) and the Neapolitan G flat, the key opens into D flat major
# itself for the strings, and the way back is diminished: the piccolo's flourishes up and down E diminished
# and C's flat nine, then the timpani rolls on C into the top.  The second pass has the strings in octaves with
# the horn under them, the horns in octaves over a dark brass chord, the brass with the strings for D flat, the
# harp in sixteenths, hits on every accent.

DL_PASS = (['Fm', 'Dbmaj7', 'Bbm9', 'Csus4 C'] + ['Fm', 'Ab/Eb', 'Db', 'Gb']
           + ['Db', 'Bbm7', 'Gbmaj7', 'Ab7'] + ['Fm/Ab', 'Bbm', 'Edim7', 'C7b9'] + ['Db', 'Bbm6', 'Csus4', 'C'])
DL_TUNE = [['C4:0:3 F4:3:1', 'Ab4:0:2 F4:2:1 C5:3:1', 'Db5:0:3 C5:3:1', 'F4:0:2 E4:2:2'],
           ['Ab4:0:3 G4:3:1', 'C5:0:2 Eb5:2:1 Ab4:3:1', 'F5:0:3 Db5:3:1', 'Bb4:0:4'],
           ['F5:0:3 Ab5:3:1', 'F5:0:2 Db5:2:1 Bb4:3:1', 'Db5:0:2 F5:2:2', 'Eb5:0:2 C5:2:2'],
           ['C5:0:4', 'Db5:0:2 Bb4:2:2', 'G4:0:2 Bb4:2:2', 'Db5:0:2 Bb4:2:1 G4:3:1'],
           ['Ab4:0:3 F4:3:1', 'G4:0:2 F4:2:1 Db4:3:1', 'F4:0:4', 'E4:0:4']]
DL_UNDER = [['Ab3:0:4', 'F3:0:4', 'F3:0:4', 'G3:0:4'], ['C4:0:4', 'C4:0:4', 'Ab3:0:4', 'Db4:0:4'],
            ['Ab3:0:4', 'Db4:0:4', 'Bb3:0:4', 'C4:0:4'], ['F3:0:4', 'F3:0:4', 'E3:0:4', 'E3:0:4'],
            ['F3:0:4', 'Db3:0:4', 'C3:0:4', 'C3:0:4']]
DIM_UP = [76, 79, 82, 85, 88, 91, 94, 97]           # E diminished (C's flat nine's upper notes) up from E5


def turns(s, ch, bars, vel, lo=53):
    """the motor: eighths turning round the chord's root (root, third, second, root, the fifth below, root,
    second, third), its 3+3+2 accents louder"""
    for b in bars:
        for k in range(8):
            beat = k * 0.5
            c = chord(s.sym_at(b, beat))
            r = in_range(c.root, lo, lo + 11)
            third = 3 if 3 in c.tones else 4 if 4 in c.tones else 5
            s.play(ch, b, beat, 0.42, r + (0, third, 2, 0, -5, 0, 2, third)[k], vel + (12 if beat in ACC332 else 0), 3)


def deep_lane():
    form = ['Fm', 'Fm'] + DL_PASS + DL_PASS
    s = Piece(144, 5144, form, level=0.5)
    hn, hn2, mo, lo, hi_, br, pc, tp, hp, st, bs = chans(s)
    n = len(form)                                   # 2 + 20 + 20 = 42 bars, 70 s
    s.loop_at(2)
    pedal332(s, lo, range(0, 2), 84)                # the intro: the low strings, the turns coming in
    turns(s, mo, range(1, 2), 58)
    s.ramp(mo, 1, 1, 50, 127)
    s.play(tp, 0, 0, 1, 'F2', 80)
    for P, second in ((2, False), (22, True)):
        B1, B2, B3, B4, B5 = P, P + 4, P + 8, P + 12, P + 16
        for b in (B1, B2, B3, B4, B5):
            s.sect(b)
        turns(s, mo, range(B1, B3), 64)
        turns(s, mo, range(B4, B5 + 2), 64)
        s.arp(mo, range(B3, B4), [0, 1, 2, 3, 2, 1, 2, 3], 0.5, 53, 77, 4, 62)   # D flat: the chords broken
        pedal332(s, lo, range(B1, B5 + 2), 90)
        if not second:
            s.arp(hp, range(B1, B3), [0, 2, 1, 3], 0.5, 65, 84, 4, 42)
            s.arp(hp, range(B3, B4), [0, 1, 2, 3, 4, 3, 2, 1], 0.5, 65, 86, 4, 50)
            s.arp(hp, range(B4, B5 + 2), [0, 2, 1, 3], 0.5, 65, 84, 4, 42)
        else:
            s.arp(hp, range(B1, B3), [0, 2, 1, 3], 0.5, 65, 84, 4, 46)
            s.arp(hp, range(B3, B4), [0, 1, 2, 3, 4, 5, 4, 3, 2, 1, 2, 3], SIXTH, 65, 86, 4, 48, accent=4)
            s.arp(hp, range(B4, B5 + 2), [0, 2, 1, 3], 0.5, 65, 84, 4, 46)
        for blk in ((B1, B2, B4) if second else (B1, B2)):
            for b in (blk, blk + 2):
                for beat in (ACC332 if second else (0, 1.5)):
                    hit(s, hi_, b, beat, 106, timpani=tp if beat == 0 else None)
        for blk in ((B1, B3, B4) if second else (B1, B3)):
            s.crash(blk, 0, 106)
        if not second:
            sung(s, hn, B1, DL_TUNE[0], 80)
            sung(s, hn, B2, DL_TUNE[1], 82)
            sung(s, hn2, B2, DL_UNDER[1], 62)
            sung(s, st, B3, DL_TUNE[2], 82)
            sung(s, hn, B3, DL_UNDER[2], 64)
            sung(s, hn, B4, DL_TUNE[3], 80)
            sung(s, st, B5, DL_TUNE[4][:2], 78)
        else:
            sung(s, st, B1, [up(t, 12) for t in DL_TUNE[0]], 78)
            s.tune(st, B1, DL_TUNE[0], 70)
            sung(s, hn, B1, DL_UNDER[0], 66)
            s.tune(hn, B2, DL_TUNE[1], 82)
            s.tune(hn2, B2, [up(t, -12) for t in DL_TUNE[1]], 70)
            s.pad(br, range(B2, B3), 53, 67, 3, 56)
            s.tune(br, B3, DL_TUNE[2], 86)
            sung(s, st, B3, [up(t, 12) for t in DL_TUNE[2]], 76)
            sung(s, hn, B4, DL_TUNE[3], 82)
            sung(s, hn2, B4, DL_UNDER[3], 64)
            sung(s, st, B5, [up(t, 12) for t in DL_TUNE[4][:2]], 78)
            s.tune(st, B5, DL_TUNE[4][:2], 70)
            s.run(pc, B1 + 3, 2, scale_up(77, 0, (0, 2, 4, 5, 7, 9, 10), 12), SIXTH, 58, 2)  # C mixolydian
        sung(s, hn, B5, DL_UNDER[4][:2], 64)
        s.run(pc, B4 + 2, 2, DIM_UP, SIXTH, 60, 2)          # the diminished flourishes
        s.run(pc, B4 + 3, 2, DIM_UP[::-1], SIXTH, 60, -2)
        # the riser: the motor pulsing on C, the horns swelling, the timpani rolling up from nothing
        R = B5 + 2
        pulses(s, mo, range(R, R + 2), 58)
        s.ramp(mo, R, 1, 80, 127)
        held(s, hn, R, 0, 4, 'F4', 72, e0=40, e1=110, e2=110)          # (the suspension, then its third)
        held(s, hn, R + 1, 0, 4, 'E4', 76, e0=100, e1=127, e2=127)
        held(s, hn2, R, 0, 8, 'G3', 68, e0=40, e1=127, e2=127)
        s.roll(tp, R, 0, 8, 'C2', 6, 112)
        if second:
            sweep(s, hp, R + 1, {0, 4, 7}, 48, 84, 58, beats=2)
            s.roll(9, R + 1, 2, 2, DRUM['snare'], 18, 88, step=0.125)
    s.end(n)
    return s


SONGS = {'CRUISE2.MID': wide_orbit, 'CRUISE3.MID': sensor_sweep, 'CRUISE4.MID': far_light, 'CRUISE5.MID': deep_lane}
