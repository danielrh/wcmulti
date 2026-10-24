"""The flight's music, for our OPL3 bank (tools/oplbank.py); CC0, ours.

The user's brief (2026-10-23): many space songs, pensive but curious; a very calm one for the way home;
battle songs; three short victory stings and a short tragic one; three panic songs, worse and worse as the
ship falls apart; some half an hour of music in all, the score (src/score.c) moving between the songs.

The third draft (2026-10-24): the user found the second's cruise songs "elevator music" and sent two cues for
the feeling of excitement and wonder (MT-32 orchestral: fast, lean, restless); ours stay our own, the cues
setting only the feel.  Outbound, the proof piece, "hits on all counts", and then: "fix all the flight music to
have that same style but their own unique character and be remixable and loopable so the orchestral score is
complete".  So every piece is written for the same small orchestra (tools/orch.py: spiccato motor, low
strings, horns, synth brass, piccolo, timpani, harp, orchestra hit, at one measured balance), each with its own
motor, tune and colour; each loops (a "loop" marker after its intro) and marks its blocks ("sect"), which the
score splices between on the bar line: the cruise songs one after another round a cycle of keys, the battles
cut together block by block (docs/music.md).

    CRUISE1  "Outbound"          E minor, 160 (this module)
    CRUISE2..5                   tools/cruise_a.py: Wide Orbit (G minor, 12/8), Sensor Sweep (B minor), Far
                                 Light (D, 12/8), Deep Lane (F minor)
    CRUISE6..8, ROCKS            tools/cruise_b.py: Long Burn (A minor, 12/8), Waypoint (C), Starwake (G, 12/8),
                                 Rock Field (G minor march)
    HOMEWARD, HOMELOSS           the way home, won and lost (this module)
    BATTLE1..3                   tools/battles.py: Contact, Break and Attack, Furball, all 140 in C: lego blocks
    PANIC1..3, VICTORY1..3, TRAGEDY   tools/panics.py

The cruise cycle: each piece's riser lands on its own dominant and the next starts in its key, E minor, G
minor, B minor, D, F minor, A minor, C, G and round to E.  tools/songcheck.py holds every piece to bands
measured from the user's two cues.
"""
from compose import chord
from orch import (DRUM, Piece, up, scale_run, ACC332, orchestra, in_range, octaves, pulses, pedal332, hit, held, sung,
                  sweep, MAJOR)
import cruise_a                         # (the parts' modules, written with orch.py as this one)
import cruise_b
import battles
import panics



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


def outbound():
    form = ['Em', 'Em'] + OB_PASS + OB_PASS
    s = Piece(160, 160, form)
    ch = orchestra(s)
    mo, hi_, lo, br, hn, hn2, pc, tp, hp, st, bs = (ch[k] for k in ('motor', 'hit', 'low', 'brass', 'horn', 'horn2',
                                                                 'piccolo', 'timpani', 'harp', 'strings', 'bass'))
    n = len(form)                                   # 2 + 24 + 24 = 50 bars, 75 s
    s.loop_at(2)
    for P in (2, 26):                               # its blocks (the risers not: they lead into the top)
        for b in (0, 4, 8, 10, 14, 18):
            s.sect(P + b)
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
    s.sections(1, n)
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


SONGS = {'CRUISE1.MID': outbound, **cruise_a.SONGS, **cruise_b.SONGS,
         'HOMEWARD.MID': homeward, 'HOMELOSS.MID': homeloss, **battles.SONGS, **panics.SONGS}
