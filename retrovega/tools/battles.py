"""The battles of the third draft's orchestral score (CC0, ours): BATTLE1..3 for tools/flightsongs.py.

The user (2026-10-24): "fix all the flight music to have that same style but their own unique character and be
remixable and loopable so the orchestral score is complete".  The style is Outbound's (orch.py): a motor that never
stops under calls and tunes, lean parts each with one job, hits where a kit would groove, rolls and runs.  Each
battle keeps the second draft's character (the user called Contact perfect, Furball nearly so):

    BATTLE1  "Contact"           the C sus pedal into C minor, the horn's call answered by synth brass, brass stabs
                                 on 3+3+2, the low strings and timpani driving eighths
    BATTLE2  "Break and Attack"  a half-time fight (the motor at full speed, the hits half-time), a riff of its own on
                                 strings in octaves, brass calls and answers, the horn over a Neapolitan D flat, a
                                 lift into A flat
    BATTLE3  "Furball"           the C phrygian riff on brass and horns over driving fifths; its surprise a minor
                                 third up with the brass answering, a tutti stop, a tom run, and home

The lego contract (the score cuts between blocks of all three and splices them on the bar, src/music.c): all at 140
in 4/4 in C minor's family, the same motor (spiccato sixteenths) in each; a "sect" marker every four bars of the
body; every block starts on its downbeat with the motor and a hit and stands alone, ends on C or G (its last bar
may fill), and holds no note over its end.  Every channel's expression is set at the start, so a cut into the
middle of a piece finds it as the piece has it.
"""
from compose import chord, Voicer
from songs import p
from orch import DRUM, Piece, up, scale_run, ACC332, orchestra, in_range, hit, sung

BPM = 140
DRIVE = [[(0, 1), (1, .5), (1.5, .5), (2, 1), (3, 1)], [(0, 1.5), (1.5, .5), (2, 2)],
         [(0, .5), (.5, .5), (1, 1), (2, 1.5), (3.5, .5)], [(0, 2), (2, 1), (3, 1)],
         [(0, 1), (1, 1), (2, .5), (2.5, .5), (3, 1)]]
DRIVE_END = [[(0, 4)], [(0, 3)], [(0, 2), (2, 2)]]
DIM7_G = [71, 74, 77, 80, 83, 86, 89, 92]           # G7's flat nine broken upward: B, D, F, A flat


def begin(s, extra=()):
    """the orchestra and its expressions at full (the chase of a cut takes them from here); {part: channel}"""
    ch = orchestra(s)
    for c in list(ch.values()) + list(extra):
        s.cc(c, 0, 0, 11, 127)
    return ch


def motor(s, ch, bars, pattern, vel, lo=55, hi=74, n=4):
    """the battles' motor: spiccato sixteenths breaking each chord, the beats a little louder"""
    s.arp(ch, bars, pattern, 0.25, lo, hi, n, vel, accent=10, gate=0.8)


def block_pad(s, ch, bars, lo, hi, n, vel):
    """held chords a block at a time, so none hangs over a block's end"""
    for b0 in range(bars.start, bars.stop, 4):
        s.pad(ch, range(b0, min(b0 + 4, bars.stop)), lo, hi, n, vel)


def stabs(s, ch, bars, vel, beats=ACC332, lo=60, hi=74):
    """three-note brass stabs on the accents"""
    for b in bars:
        for beat in beats:
            v = Voicer(lo, hi, 3)
            s.play(ch, b, beat, 0.35, v.next(chord(s.sym_at(b, beat))), vel if beat == 0 else vel - 8, 3)


def fill(s, bar, vel=86, toms=False, beat=2):
    """a snare roll from the bar's third beat into the next block (or a tom run down its last beat)"""
    s.roll(9, bar, beat, 4 - beat - (1 if toms else 0), DRUM['snare'], 36, vel, step=0.25)
    if toms:
        for k, t in enumerate(('tom1', 'tom2', 'tom3', 'tom4')):
            s.play(9, bar, 3 + k * 0.25, 0.2, DRUM[t], vel - 6 + k * 3, 2)


def run(s, ch, bar, beat, pitches, step, vel, rise=2):
    """a run (piccolo, harp), each note let go by the bar's end: no note over a block's end"""
    for k, q in enumerate(pitches):
        at = beat + k * step
        s.play(ch, bar, at, min(step * 1.5, 3.98 - at), q, vel + rise * k, 2)


def timp_roll(s, ch, bar, beat, beats, pitch, v0, v1):
    """a timpani roll in thirty-seconds, swelling"""
    s.roll(ch, bar, beat, beats, pitch, v0, v1, step=0.125)


# --- BATTLE1: "Contact" (C minor on a sus pedal, 140) -------------------------------
# The user: perfect, then: lego pieces, and now the orchestra's.  Its call (the second draft's, ours) on the horn,
# synth brass answering; the low strings' eighths (root, root, octave, root, fifth ...) and the timpani on the
# pedal; brass stabs on 3+3+2.  Two passes, the second rescored (the call on brass over the horn, every accent hit,
# the piccolo over the call's end); a tom fill into the loop.

CT_A1 = ['Csus4 Cm', 'Csus4 Cm', 'Ab Bb', 'Csus4 Cm']
CT_A2 = ['Csus4 Cm', 'Csus4 Cm', 'Fm Ab', 'Gsus4 G']
CT_B1 = ['Ab', 'Bb', 'Cm', 'Cm']
CT_B2 = ['Ab', 'Bb', 'Gsus4', 'G7b9']
CT_C1 = ['Fm9', 'Bb', 'Ebmaj7', 'Abmaj7']
CT_C2 = ['Fm9', 'Dbmaj7', 'Absus2', 'Gsus4 G']
CT_C1B = ['Fm9', 'Bb', 'Ebmaj7', 'Emaj7']             # (the second time: E flat slides up to E, then F)
CT_CALL = ['C4:0:1.5 G4:1.5:.5 G4:2:2', 'Ab4:0:1 G4:1:1 F4:2:1 Eb4:3:1', 'C4:0:2 D4:2:2', 'Eb4:0:4',
           'G4:0:1.5 C5:1.5:.5 C5:2:2', 'D5:0:1 C5:1:1 Bb4:2:1 G4:3:1', 'Ab4:0:2 C5:2:2', 'B4:0:2 D5:2:2']
CT_ANSWER = ['Eb5:0:1.5 D5:1.5:.5 C5:2:1 Bb4:3:1', 'D5:0:1.5 C5:1.5:.5 Bb4:2:2', 'G4:0:1 C5:1:1 Eb5:2:1 G5:3:1',
             'F5:0:2 Eb5:2:1 D5:3:1', 'C5:0:1.5 Bb4:1.5:.5 Ab4:2:1 C5:3:1', 'D5:0:1.5 F5:1.5:.5 Bb5:2:2',
             'G5:0:2 F5:2:1 D5:3:1', 'B4:0:2 D5:2:2']
CT_LOW = [(k * .5, .45, w) for k, w in enumerate('rr8r5r8r')]
CT_TURN = [0, 1, 2, 1, 0, 1, 2, 3]


def contact():
    pass_ = CT_A1 + CT_A2 + CT_B1 + CT_B2 + CT_C1 + CT_C2
    form = ['Csus4 Cm', 'Csus4 Cm'] + pass_ + pass_[:16] + CT_C1B + CT_C2
    s = Piece(BPM, 909, form, level=1.0)
    ch = begin(s)
    mo, hi_, lo, br, hn, hn2, pc, tp, st = (ch[k] for k in ('motor', 'hit', 'low', 'brass', 'horn', 'horn2',
                                                            'piccolo', 'timpani', 'strings'))
    n = len(form)                                   # 2 + 24 + 24 = 50 bars, 85.7 s
    s.loop_at(2)
    s.sections(2, n)
    motor(s, mo, range(0, n), CT_TURN, 58)
    s.ramp(mo, 0, 2, 60, 127)                       # the intro: the motor coming up, a roll into the first hit
    s.bass(lo, range(0, n), CT_LOW, 84, lo=36, hi=47)
    timp_roll(s, tp, 1, 0, 4, 'G1', 10, 96)
    for P, second in ((2, False), (26, True)):
        A1, A2, B1, B2, C1, C2 = (P + 4 * k for k in range(6))
        # the call over the pedal
        for blk in (A1, A2, B1, B2, C1, C2):
            s.crash(blk, 0, 104 if blk in (A1, B1, C1) else 92)
            for b in (blk, blk + 2):
                for beat in (ACC332 if second and blk in (A1, A2) else (0, 1.5)):
                    hit(s, hi_, b, beat, 110, timpani=tp if beat == 0 else None)
            for b in range(blk, blk + 4):           # the timpani on the pedal between the hits
                if b % 2:
                    s.line(tp, b, 'C2:0:.5 C2:2:.5 G1:3:.25 G1:3.5:.25', 78)
        if not second:
            s.tune(hn, A1, CT_CALL, 90)
            stabs(s, br, range(A1, B1), 74)
        else:
            s.tune(br, A1, CT_CALL, 88)             # the call on the brass, the horn an octave under
            s.tune(hn, A1, [up(t, -12) for t in CT_CALL], 76)
            s.tune(pc, A2 + 2, [up(t, 24) for t in CT_CALL[6:]], 54)
            stabs(s, hn2, range(A1, B1), 70, beats=(1.5, 3))
        # the answer: synth brass (then the strings), the horn holding under it
        if not second:
            s.tune(br, B1, CT_ANSWER, 84)
            sung(s, hn, B1, ['Ab3:0:4', 'Bb3:0:4', 'C4:0:4', 'C4:0:2 Eb4:2:2'], 72)
            sung(s, hn, B2, ['Ab3:0:4', 'Bb3:0:4', 'D4:0:4', 'B3:0:2 D4:2:2'], 74)
        else:
            s.tune(br, B1, CT_ANSWER, 86)
            s.tune(hn, B1, [up(t, -12) for t in CT_ANSWER], 72)
            run(s, pc, B2 + 3, 2, DIM7_G, 0.25, 58, 2)
        stabs(s, hn2, range(B1, C1), 64, beats=(1.5, 3))
        fill(s, B2 + 3, 84)
        # in E flat: the horn's tune over Fm9, B flat, E flat, A flat, the brass holding the chords
        s.made(hn, C1, 8, 'Eb', 'major', 58, 74, 86 if not second else 90, seed=92 if not second else 94,
               rhythms=DRIVE, ends=DRIVE_END)
        block_pad(s, br, range(C1, C2 + 4), 58, 72, 3, 60)
        if second:
            s.made(st, C1, 8, 'Eb', 'major', 67, 82, 70, seed=95, rhythms=DRIVE, ends=DRIVE_END)
        timp_roll(s, tp, C2 + 3, 0, 4, 'G1', 12, 96)
        run(s, pc, C2 + 3, 2, DIM7_G, 0.25, 60, 2)
        fill(s, C2 + 3, 88, toms=second)
    s.end(n)
    return s


# --- BATTLE2: "Break and Attack" (C minor, 140) ------------------------------------
# The user: too repetitive, racing more than fighting; the riff not set apart enough.  So: half-time, the motor at
# full speed under hits on one and three; a riff of its own on strings in octaves (spiccato, quick enough for it),
# syncopated against the others' squareness; the brass call answered by the strings; the horn's tune over a
# Neapolitan D flat; a lift into A flat with a crash; then the riff again, its second half up an octave with the
# brass on it.

BA_R1 = ['Cm', 'Ab', 'Bb', 'Gsus4 G']
BA_R2 = ['Cm', 'Ab', 'Fm', 'Gsus4 G']
BA_K1 = ['Ab', 'Bb', 'Cm', 'Eb']
BA_K2 = ['Fm', 'Ab', 'Bbsus4 Bb', 'Gsus4 G']
BA_N1 = ['Fm', 'Cm', 'Ab', 'G']
BA_N2 = ['Fm', 'Cm', 'Db', 'G']
BA_L1 = ['Ab', 'Db/Ab', 'Eb/G', 'Ab']
BA_L2 = ['Fm', 'Db', 'Bbsus4 Bb', 'Gsus4 G']
BA_RIFF = ['C5:0:.75 Eb5:.75:.75 G5:1.5:.5 F5:2:.5 Eb5:2.5:.5 D5:3:.5 Bb4:3.5:.5',
           'C5:0:.75 Ab4:.75:.75 C5:1.5:.5 Eb5:2:1 Ab5:3:1',
           'Bb4:0:.75 D5:.75:.75 F5:1.5:.5 Bb5:2:.5 Ab5:2.5:.5 F5:3:.5 D5:3.5:.5',
           'G5:0:.75 F5:.75:.75 D5:1.5:.5 B4:2:1 D5:3:.5 F5:3.5:.5',
           'C5:0:.75 Eb5:.75:.75 G5:1.5:.5 F5:2:.5 Eb5:2.5:.5 D5:3:.5 Bb4:3.5:.5',
           'C5:0:.75 Ab4:.75:.75 C5:1.5:.5 Eb5:2:1 Ab5:3:1',
           'Ab5:0:.75 G5:.75:.75 F5:1.5:.5 C5:2:.5 F5:2.5:.5 Ab5:3:.5 C6:3.5:.5',
           'B5:0:.75 G5:.75:.75 D5:1.5:.5 B4:2:1 G4:3:1']
BA_CALL = ['C5:0:.5 C5:.5:.5 Eb5:1:1', '', 'D5:0:.5 D5:.5:.5 F5:1:1', '', 'Eb5:0:.5 Eb5:.5:.5 G5:1:1', '',
           'F5:0:.5 Eb5:.5:.5 D5:1:1 B4:2:1', '']
BA_ANSWER = ['', 'Ab4:0:1 Bb4:1:1 C5:2:2', '', 'Bb4:0:1 C5:1:1 D5:2:2', '', 'C5:0:1 D5:1:1 Eb5:2:2', '', 'D5:0:4']
BA_HORN = ['F4:0:1.5 Ab4:1.5:.5 C5:2:2', 'Eb5:0:1 D5:1:1 C5:2:2', 'Eb5:0:1.5 F5:1.5:.5 Eb5:2:1 C5:3:1', 'B4:0:4',
           'F4:0:1.5 Ab4:1.5:.5 C5:2:2', 'G5:0:1 F5:1:1 Eb5:2:1 C5:3:1', 'Db5:0:2 F5:2:2', 'D5:0:2 B4:2:2']
BA_LIFT = ['Eb5:0:1.5 C5:1.5:.5 Ab4:2:1 C5:3:1', 'F5:0:3 Ab5:3:1', 'G5:0:1.5 Eb5:1.5:.5 Bb4:2:1 Eb5:3:1',
           'C5:0:4', 'C5:0:1.5 Ab4:1.5:.5 F4:2:1 Ab4:3:1', 'Db5:0:2 F5:2:1 Ab5:3:1', 'F5:0:2 D5:2:2', 'D5:0:2 B4:2:2']
BA_TURN = [0, 2, 1, 3, 2, 4, 3, 2]
BA_HOLD = (['G3:0:4', 'Ab3:0:4', 'F3:0:4', 'G3:0:4'], ['G3:0:4', 'Ab3:0:4', 'Ab3:0:4', 'G3:0:4'])   # the horns under it


def halftime(s, ch, bars, vel, full=False):
    """the hits half-time: kick and orchestra hit on one, a hit and the snare on three (full: on 3+3+2)"""
    for b in bars:
        if full:
            for beat in (0, 1.5) if b % 2 == 0 else (0,):
                hit(s, ch, b, beat, vel)
        else:
            hit(s, ch, b, 0, vel)
            s.play(9, b, 2, 0.2, DRUM['snare'], 92, 3)
            s.play(ch, b, 2, 0.4, in_range(chord(s.sym_at(b, 2)).root, 60, 71), vel - 10, 3)


def break_and_attack():
    pass_ = BA_R1 + BA_R2 + BA_K1 + BA_K2 + BA_N1 + BA_N2 + BA_L1 + BA_L2
    form = ['Cm'] + pass_ + BA_R1 + BA_R2
    s = Piece(BPM, 1010, form, level=1.0)
    riff = 12                                       # (the riff's own strings, spiccato in octaves)
    s.setup(riff, 44, 110)
    ch = begin(s, extra=(riff,))
    mo, hi_, lo, br, hn, hn2, pc, tp, st = (ch[k] for k in ('motor', 'hit', 'low', 'brass', 'horn', 'horn2',
                                                            'piccolo', 'timpani', 'strings'))
    n = len(form)                                   # 1 + 32 + 8 = 41 bars, 70.3 s
    R1, R2, K1, K2, N1, N2, L1, L2, Q1, Q2 = (1 + 4 * k for k in range(10))
    s.loop_at(1)
    s.sections(1, n)
    motor(s, mo, range(0, n), BA_TURN, 58)
    s.bass(lo, range(0, n), [(0, .45, 'r'), (.75, .45, 'r'), (1.5, .45, '8'), (2, .45, 'r'), (2.75, .45, 'r'),
                             (3.5, .45, '5')], 84, lo=36, hi=47)
    timp_roll(s, tp, 0, 0, 4, 'C2', 10, 92)
    # the riff, half-time hits under it
    for blk, text, horns in ((R1, BA_RIFF[:4], BA_HOLD[0]), (R2, BA_RIFF[4:], BA_HOLD[1])):
        s.crash(blk, 0, 100)
        s.tune(riff, blk, text, 92)
        s.tune(riff, blk, [up(t, -12) for t in text], 84)
        sung(s, hn, blk, horns, 78)
        halftime(s, hi_, range(blk, blk + 4), 110)
    fill(s, R2 + 3, 80, beat=3)
    # the brass call, the riff's strings answering
    s.crash(K1, 0, 100)
    s.tune(br, K1, BA_CALL, 90)
    s.tune(riff, K1, BA_ANSWER, 80)
    s.tune(riff, K1, [up(t, -12) if t else '' for t in BA_ANSWER], 72)
    halftime(s, hi_, range(K1, K1 + 8), 106)
    for b in range(K1, K1 + 8, 2):
        s.line(tp, b, 'C2:0:.5 C2:.5:.25 C2:.75:.25 G1:1:.5', 84)
    fill(s, K2 + 3, 84)
    # the horn over a Neapolitan D flat: the hits on 3+3+2 now, the brass holding the chords
    s.crash(N1, 0, 104)
    sung(s, hn, N1, BA_HORN, 92, e0=80, e1=127, e2=110)
    block_pad(s, br, range(N1, N1 + 8), 57, 70, 3, 58)
    halftime(s, hi_, range(N1, N1 + 8), 108, full=True)
    for b in range(N1, N1 + 8):
        s.play(tp, b, 0, .4, in_range(chord(s.sym_at(b, 0)).bass, 33, 44), 84)
    run(s, pc, N2 + 3, 2, DIM7_G, 0.25, 60, 2)
    timp_roll(s, tp, N2 + 3, 2, 2, 'G1', 20, 98)
    fill(s, N2 + 3, 88)
    # the lift into A flat: a crash, the brass's tune, the horns in long notes, the riff's strings pulsing
    s.crash(L1, 0, 110)
    s.tune(br, L1, BA_LIFT, 92)
    sung(s, hn, L1, ['Ab3:0:4', 'Ab3:0:4', 'G3:0:4', 'Ab3:0:4', 'F3:0:4', 'F3:0:4', 'F3:0:4', 'G3:0:4'], 74)
    sung(s, hn2, L1, ['C4:0:4', 'Db4:0:4', 'Eb4:0:4', 'Eb4:0:4', 'C4:0:4', 'Db4:0:4', 'D4:0:4', 'D4:0:4'], 70)
    for b in range(L1, L1 + 8):
        for k in range(8):
            q = in_range(chord(s.sym_at(b, k * .5)).root, 60, 71)
            s.play(riff, b, k * .5, .35, [q, q + 12], 66 + (8 if k % 2 == 0 else 0), 3)
    halftime(s, hi_, range(L1, L1 + 8), 108, full=True)
    timp_roll(s, tp, L2 + 3, 0, 4, 'G1', 12, 100)
    fill(s, L2 + 3, 88, toms=True)
    # the riff again: its second half up an octave, the brass on it, the piccolo over the end
    for blk, text, horns in ((Q1, BA_RIFF[:4], BA_HOLD[0]), (Q2, [up(t, 12) for t in BA_RIFF[4:]], BA_HOLD[1])):
        s.crash(blk, 0, 104)
        s.tune(riff, blk, text, 92)
        s.tune(riff, blk, [up(t, -12) for t in text], 84)
        sung(s, hn, blk, horns, 80)
        sung(s, hn2, blk, [up(t, 7) for t in horns], 70)
        halftime(s, hi_, range(blk, blk + 4), 112)
    s.tune(br, Q2, [up(t, -12) for t in BA_RIFF[4:]], 80)
    run(s, pc, Q2 + 3, 2, DIM7_G, 0.25, 60, 2)
    timp_roll(s, tp, Q2 + 3, 2, 2, 'G1', 20, 100)
    fill(s, Q2 + 3, 90, beat=3)
    s.end(n)
    return s


# --- BATTLE3: "Furball" (C phrygian, 140) --------------------------------------------
# The user: nearly perfect, but it must fit the others; its riff's second time a surprise.  The second draft's
# overdriven fifths are the low strings' and the brass's fifths now, in eighths; its riff on the synth brass with
# the horn under it; the motor machine-gunning the power chords; the surprise a minor third up with the brass
# answering, then a tutti stop on the beats, a tom run, and the riff home.

FB_A1 = ['C5', 'Db5', 'C5', 'Eb5 Db5']
FB_A2 = ['C5', 'Db5', 'Bb5', 'Ab5 G5']
FB_B1 = ['F5', 'Eb5', 'Db5', 'C5']
FB_B2 = ['F5', 'Ab5', 'Bb5', 'Db5 C5']
FB_UP = ['Eb5', 'E5', 'Eb5', 'Gb5 E5']
FB_STOP = ['C5', 'Db5 C5', 'C5', 'Ab5 G5']
FB_RIFF = [up(t, -4) for t in ['E5:0:.5 F5:.5:.5 E5:1:.5 B4:1.5:.5 E5:2:1 G5:3:1', 'F5:0:1 E5:1:.5 D5:1.5:.5 C5:2:2',
                                'B4:0:.5 E5:.5:.5 G5:1:.5 B5:1.5:.5 A5:2:1 G5:3:1', 'F5:0:2 E5:2:2',
                                'E5:0:.5 F5:.5:.5 E5:1:.5 B4:1.5:.5 E5:2:1 G5:3:1', 'A5:0:1 G5:1:.5 F5:1.5:.5 E5:2:2',
                                'D5:0:1 F5:1:1 A5:2:1 D6:3:1', 'C6:0:2 B5:2:2']]
FB_TURN = [0, 1, 2, 1, 3, 1, 2, 1]


def fifths(s, ch, bars, vel, lo=36, skip=None):
    """the power chords in eighths, root and fifth (the second draft's guitar), the downbeats leaning"""
    for b, beat, dur, sym in s.segs(bars):
        root = in_range(chord(sym).root, lo, lo + 11)
        for k in range(int(dur / .5 + 1e-6)):
            pos = beat + k * .5
            if skip and skip(b, pos):
                continue
            s.play(ch, b, pos, .4, [root, root + 7], vel + (10 if k == 0 else 0), 3)


def furball():
    form = (['C5', 'C5'] + FB_A1 + FB_A2 + FB_B1 + FB_B2 + FB_A1 + FB_UP + FB_STOP + FB_A2
            + FB_B1 + FB_B2)
    s = Piece(BPM, 1111, form, level=0.6)
    ch = begin(s)
    mo, hi_, lo, br, hn, hn2, pc, tp, st = (ch[k] for k in ('motor', 'hit', 'low', 'brass', 'horn', 'horn2',
                                                            'piccolo', 'timpani', 'strings'))
    n = len(form)                                   # 2 + 40 = 42 bars, 72 s
    F1, F2, G1, G2, F3, U, S, F4, H1, H2 = (2 + 4 * k for k in range(10))
    s.loop_at(2)
    s.sections(2, n)
    stop = lambda b, pos: S <= b < S + 2 and pos not in (0, 2)    # the stop: on the beats only
    motor(s, mo, [b for b in range(0, n) if not S <= b < S + 2], FB_TURN, 58, lo=60, hi=79, n=3)
    fifths(s, lo, range(0, n), 82, skip=stop)
    timp_roll(s, tp, 1, 0, 4, 'C2', 10, 96)
    s.ramp(mo, 0, 2, 70, 127)
    # the riff on the brass, the horn an octave under; hits on the accents
    for blk, text in ((F1, FB_RIFF[:4]), (F2, FB_RIFF[4:]), (F3, FB_RIFF[:4]), (F4, FB_RIFF[4:])):
        s.crash(blk, 0, 104)
        s.tune(br, blk, text, 90)
        s.tune(hn, blk, [up(t, -12) for t in text], 78)
        for b in (blk, blk + 2):
            for beat in (0, 1.5):
                hit(s, hi_, b, beat, 110, timpani=tp if beat == 0 else None)
    s.tune(pc, F3 + 2, [up(t, 12) for t in FB_RIFF[2:4]], 52)
    fill(s, F2 + 3, 84)
    # the strings' tune in C minor, the brass stabbing the off-beats, the horn holding
    for blk, seed in ((G1, 111), (H1, 113)):
        s.crash(blk, 0, 100)
        s.made(st, blk, 8, 'C', 'minor', 64, 80, 82, seed=seed, rhythms=DRIVE, ends=DRIVE_END)
        s.made(hn2, blk, 8, 'C', 'minor', 55, 67, 70, seed=seed + 1, rhythms=DRIVE, ends=DRIVE_END)
        for b in range(blk, blk + 8):
            v = Voicer(59, 74, 3)
            s.play(br, b, 0, .5, v.next(chord(s.sym_at(b, 0))), 76)
            s.play(br, b, 2.5, .5, v.next(chord(s.sym_at(b, 2.5))), 70)
            if b % 2 == 0:
                hit(s, hi_, b, 0, 104, timpani=tp)
        timp_roll(s, tp, blk + 7, 0, 4, 'C2', 12, 92)
        fill(s, blk + 7, 86, toms=blk == H1)
    run(s, pc, G2 + 3, 2, scale_run(72, 84, (0, 1, 3, 5, 7, 8, 10)), 0.25, 58, 2)   # C phrygian, up
    # the surprise: the riff a minor third up, the brass answering on the off-beats
    s.crash(U, 0, 112)
    s.tune(br, U, [up(t, 3) for t in FB_RIFF[:4]], 94)
    s.tune(hn, U, [up(t, -9) for t in FB_RIFF[:4]], 80)
    for b in range(U, U + 4):
        for beat, dur, v in ((2.5, .25, 82), (3, .25, 86), (3.5, .4, 90)):
            q = 60 + chord(s.sym_at(b, beat)).root % 12
            s.play(hn2, b, beat, dur, [q, q + 7], v)
        hit(s, hi_, b, 0, 112, timpani=tp)
    run(s, pc, U + 3, 0, [75, 78, 82, 87, 90, 94, 99, 94], 0.25, 60, 2)
    # the stop: tutti on the beats, a tom run, then the riff home
    for b in (S, S + 1):
        for beat, notes in ((0, ['C4', 'Gb4', 'C5']), (2, ['Db4', 'G4', 'Db5'] if b == S + 1 else ['C4', 'Gb4', 'C5'])):
            s.play(br, b, beat, .5, notes, 96)
            s.play(mo, b, beat, .4, [p(notes[0]) - 12, p(notes[0])], 84)      # the motor on the beats with them
            s.play(hn, b, beat, .5, [n_ for n_ in ('C3', 'G3')], 86)
            hit(s, hi_, b, beat, 116, timpani=tp)
    s.crash(S, 0, 110)
    for k, t in enumerate(('tom1', 'tom1', 'tom2', 'tom2', 'tom3', 'tom3', 'tom4', 'tom4')):
        s.play(9, S + 1, 2 + k * .25, .2, DRUM[t], 86 + k * 2, 2)
    s.tune(br, S + 2, FB_RIFF[2:4], 92)
    s.tune(hn, S + 2, [up(t, -12) for t in FB_RIFF[2:4]], 80)
    hit(s, hi_, S + 2, 0, 112, timpani=tp)
    fill(s, S + 3, 84, beat=3)
    s.end(n)
    return s


SONGS = {'BATTLE1.MID': contact, 'BATTLE2.MID': break_and_attack, 'BATTLE3.MID': furball}
