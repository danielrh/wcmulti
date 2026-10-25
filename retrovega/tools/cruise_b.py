"""The third draft's cruise pieces six to eight and the rock field (CC0, ours), in Outbound's style
(tools/flightsongs.py): a fast motor under a tune in long notes, a few parts each with its own job, keys that
move by thirds, hits where a groove would be, piccolo runs and harp sweeps, a riser into the loop.

    CRUISE6  "Long Burn"   A minor into C sharp minor, 12/8 at 112: repeated notes burning for eight bars,
                           then a long tune on the strings with the horn under it; a harp sweep on E
    CRUISE7  "Waypoint"    C minor, 168: a galloping string motor over a pedal C, the brass's call in long
                           notes over the horns' falling line, a lift into A flat and B flat, home through
                           A flat and B flat to C, the horns swelling on G
    CRUISE8  "Starwake"    D minor, 150, pensive and very fast in turn: chimes tolling over a lament, then
                           runs (sixteenths, the chimes' fast figure, the horns falling D, C, B flat, A), the
                           lament darker in G minor, the runs wilder round the Neapolitan; chimes on A
    ROCKS    "Rock Field"  G minor, 120: asteroid and mine fields: a march (snare, timpani, low strings), the
                           strings tumbling in sixteenths, the horn's theme; a drift in B flat with harp and
                           piccolo glints; the theme again with a synth-brass descant

The cruise pieces are in the set's cycle of keys (each riser on its own dominant): Outbound's E minor, G minor,
B minor, D, F minor, A minor (Long Burn), C minor (Waypoint), D minor (Starwake), and E minor again.  Every piece
marks its blocks for the score's follow-ons (Piece.sect).
"""
from compose import chord
from songs import p
from orch import (NAMES, DRUM, Piece, hits, up, scale_run, ORCH, ACC332, orchestra, in_range, octaves, pulses,
                  pedal332, hit, held, sung, sweep, MAJOR, LYDIAN)

THIRD = 1 / 3                           # an eighth in 12/8 (a bar is four dotted beats)
SIXTH = 1 / 6                           # a sixteenth in 12/8
MINOR = (0, 2, 3, 5, 7, 8, 10)          # (a run's steps from its first note)


def pulses12(s, ch, bars, vel, lo=45, pitch=None):
    """12/8: repeated eighths in octaves on the chord's bass (or a pedal), the dotted beats louder"""
    for b in bars:
        for k in range(12):
            beat = k * THIRD
            q = pitch if pitch is not None else in_range(chord(s.sym_at(b, beat)).bass, lo, lo + 11)
            s.play(ch, b, beat, 0.28, [q, q + 12], vel + (10 if k % 3 == 0 else 0) + (6 if k == 0 else 0), 3)


def strokes12(s, ch, bars, vel, lo=28, at=(0, 2)):
    """12/8: the low strings' strokes on the chord's bass, on these beats"""
    for b in bars:
        for beat in at:
            q = in_range(chord(s.sym_at(b, beat)).bass, lo, lo + 11)
            s.play(ch, b, beat, 0.9, q, vel + (8 if beat == 0 else 0), 3)


def run12(s, ch, bar, beat, pitches, vel, rise=1):
    """12/8: a run in sixteenths (a sixth of a dotted beat each), every note let go inside the bar"""
    for k, q in enumerate(pitches):
        pos = beat + k * SIXTH
        s.play(ch, bar, pos, min(SIXTH * 1.4, 3.98 - pos), q, vel + rise * k, 2)


def gallop(s, ch, bars, vel, lo=40):
    """the motor at a gallop: each beat the bass, its octave, its twelfth (an eighth and two sixteenths)"""
    for b in bars:
        for beat in range(4):
            c = chord(s.sym_at(b, beat))
            q = in_range(c.bass, lo, lo + 11)
            fifth = 7 if 7 in c.tones else 6
            a = 12 if beat == 0 else 8 if beat == 2 else 0
            s.play(ch, b, beat, 0.4, q, vel + a, 3)
            s.play(ch, b, beat + 0.5, 0.2, q + 12, vel - 6, 3)
            s.play(ch, b, beat + 0.75, 0.2, q + 12 + fifth, vel - 10, 3)


def stabs(s, ch, bar, rhythm, vel, lo=60, hi=74):
    """synth-brass stabs: the chord's tones in lo..hi on each (beat, length)"""
    for beat, dur in rhythm:
        c = chord(s.sym_at(bar, beat))
        tones = sorted({in_range((c.root + t) % 12, lo, hi) for t in c.tones[:3]})
        s.play(ch, bar, beat, dur, tones, vel + (6 if beat == 0 else 0), 3)


def tumble(s, ch, bars, vel, lo=50):
    """the rocks tumbling: sixteenths over the chord, bass, fifth, octave, fifth, then up a tenth"""
    for b in bars:
        for beat in range(4):
            c = chord(s.sym_at(b, beat))
            q = in_range(c.bass, lo, lo + 11)
            third = 3 if 3 in c.tones else 4
            fifth = 7 if 7 in c.tones else 6
            figure = (0, fifth, 12, fifth) if beat % 2 == 0 else (0, 12, 12 + third, 12)
            for k, d in enumerate(figure):
                s.play(ch, b, beat + k * 0.25, 0.22, q + d, vel + (10 if k == 0 and beat % 2 == 0 else 0), 3)


def glints(s, ch, bar, notes, vel):
    """the piccolo's glints: (beat, pitch) short and high, each with a grace a step under it"""
    for beat, q in notes:
        s.play(ch, bar, beat, 0.06, q - 2, vel - 14, 2)
        s.play(ch, bar, beat + 0.0625, 0.3, q, vel, 2)


# --- CRUISE6: "Long Burn" (A minor into C sharp minor, 12/8 at 112) ----------------
# Anticipation.  Eight bars of repeated A's, burning: the chords turn over the pedal (F, D minor, a phrygian B
# flat), the horn climbs in long swells, the timpani come more often and the hits arrive; then E7 over G sharp
# opens into C sharp minor, a major third up, and the release: a long tune on the strings with the horn under
# it, the harp flowing.  A harp sweep on E (A minor's dominant) throws it back to the burn.  The second pass:
# the pulses an octave higher, the brass's stabs and the horns in thirds over the burn, and the tune on the
# strings in octaves with the piccolo over its end.

LB_BURN = ['Am', 'Am', 'Fmaj7/A', 'Fmaj7/A', 'Dm9/A', 'Dm9/A', 'Bb/A', 'E7/G#']
LB_RELEASE = ['C#m', 'Amaj7', 'F#m9', 'G#sus4 G#', 'C#m', 'E/B', 'Amaj7', 'E']
LB_HORN = ['C4:0:4', 'E4:0:4', 'E4:0:4', 'A4:0:4', 'F4:0:4', 'A4:0:4', 'F4:0:2 D4:2:2', 'D4:0:2 E4:2:2']
LB_HORN2 = ['A3:0:4', 'C4:0:4', 'C4:0:4', 'F4:0:4', 'D4:0:4', 'F4:0:4', 'D4:0:2 Bb3:2:2', 'B3:0:2 G#3:2:2']
LB_TUNE = ['G#4:0:2 C#5:2:1 E5:3:1', 'E5:0:3 C#5:3:1', 'F#5:0:2 E5:2:1 C#5:3:1', 'D#5:0:2 C#5:2:1 C5:3:1',
           'C#5:0:1 D#5:1:1 E5:2:2', 'G#5:0:3 F#5:3:1', 'E5:0:2 G#5:2:1 A5:3:1', 'G#5:0:4']
LB_UNDER = ['E4:0:4', 'E4:0:4', 'C#4:0:4', 'D#4:0:4', 'C#4:0:4', 'B3:0:4', 'C#4:0:4', 'B3:0:4']


def long_burn():
    form = ['Am'] + LB_BURN + LB_RELEASE + LB_BURN + LB_RELEASE
    s = Piece(112, 1606, form, level=2.0)
    ch = orchestra(s)
    mo, hi_, lo, br, hn, hn2, pc, tp, hp, st, bs = (ch[k] for k in ('motor', 'hit', 'low', 'brass', 'horn', 'horn2',
                                                                 'piccolo', 'timpani', 'harp', 'strings', 'bass'))
    n = len(form)                                   # 1 + 16 + 16 = 33 bars, 70.7 s
    s.loop_at(1)
    pulses12(s, mo, range(0, 1), 60, pitch=57)      # the intro: the pulses alone, coming up
    s.ramp(mo, 0, 1, 40, 80)
    s.play(tp, 0, 0, 1, 'A1', 64)
    for P, second in ((1, False), (17, True)):
        R = P + 8
        s.sect(P)
        s.sect(P + 4)
        s.sect(R)
        s.sect(R + 4)
        # the burn: A's in octaves on every eighth, growing; the chords turn over them
        pulses12(s, mo, range(P, R), 62, pitch=69 if second else 57)
        s.ramp(mo, P, 8, 70, 127)
        strokes12(s, lo, range(P, R), 84)
        if not second:
            sung(s, hn, P, LB_HORN, 74, e0=50, e1=124, e2=90)
        else:
            sung(s, hn, P, LB_HORN, 76, e0=50, e1=124, e2=90)
            sung(s, hn2, P, LB_HORN2, 68, e0=50, e1=118, e2=86)
            for b in range(P + 4, R):                # the brass's stabs on the off-beats, the burn's last half
                stabs(s, br, b, [(1 + THIRD * 2, 0.3), (3 + THIRD, 0.3)], 66)
        for k, b in enumerate(range(P, R)):         # the timpani: once a bar, then twice, then every beat
            beats = (0,) if k < 4 else (0, 2) if k < 6 else (0, 1, 2, 3)
            for beat in beats:
                s.play(tp, b, beat, 0.6, 'A1', 62 + 4 * k)
        for b in (P + 4, P + 5, P + 6, P + 7):      # the hits arriving through the burn's second half
            hit(s, hi_, b, 0, 100 + (b - P - 4) * 3)
            if b in (P + 5, P + 7):
                hit(s, hi_, b, 1 + THIRD * 2, 96)
        s.roll(9, R - 1, 2, 2, DRUM['snare'], 16, 88, step=SIXTH)
        run12(s, pc, P + 3, 2 + THIRD * 2, scale_run(81, 93, MINOR)[:8], 58)      # A up to A
        run12(s, pc, P + 7, 2 + THIRD * 2, [80, 83, 86, 88, 92, 95, 98, 100], 62)  # E7 over G sharp, up
        s.roll(tp, R - 1, 2, 2, 'E2', 30, 96, step=SIXTH)
        # the release: C sharp minor, the strings' long tune, the horn under it, the harp flowing
        s.crash(R, 0, 112)
        hit(s, hi_, R, 0, 110, timpani=tp)
        s.arp(hp, range(R, R + 7), [0, 1, 2, 3, 2, 1], THIRD, 61, 85, 4, 54, accent=6)
        s.arp(mo, range(R, R + 8), [0, 2, 1], THIRD, 44, 60, 3, 54, accent=8)
        s.bass(bs, range(R, R + 8), [(0, 1.9, 'r'), (2, 1.9, '5')], 76, lo=28, hi=40)
        if not second:
            sung(s, st, R, LB_TUNE, 84, e0=70, e1=124, e2=100)
            sung(s, hn, R, LB_UNDER, 66)
        else:
            sung(s, st, R, [up(t, 12) for t in LB_TUNE], 80, e0=70, e1=124, e2=100)
            s.tune(st, R, LB_TUNE, 70)
            sung(s, hn, R, LB_UNDER, 66)
            s.tune(pc, R + 6, [up(t, 12) for t in LB_TUNE[6:7]], 54)
        hit(s, hi_, R + 2, 0, 96)
        hit(s, hi_, R + 4, 0, 102)
        hit(s, hi_, R + 6, 0, 96)
        # the riser: a harp sweep up E major into the burn
        sweep(s, hp, R + 7, {4, 8, 11}, 40, 88, 60)
        if second:
            s.roll(tp, R + 7, 0, 4, 'E2', 10, 100, step=SIXTH)
    s.end(n)
    return s


# --- CRUISE7: "Waypoint" (C minor, 168) ---------------------------------------------
# Brisk travel between nav points.  The strings gallop (each beat the bass, its octave, its twelfth) over a
# pedal C; the synth brass sounds the call in long notes, a fifth up and a climb, over the chords moving above
# the pedal (C minor, B flat, A flat) and down to G over B, while the horns' line falls under it to the leading
# note; then again, rising to G.
# A lift into A flat and B flat for the horn's tune (D flat on the way: the Neapolitan's colour), two bars of
# the motor and the timpani alone, and the way home: A flat, B flat, C sus and C, the strings' broad tune with
# the horns under it and the brass's chords; the horns swell on G over a timpani roll, the harp sweeping up.
# The second pass: the call on the horns in octaves over the brass's chords, then on the strings in octaves,
# every accent hit, the tune on the strings.  (The user, 2026-10-24, on the first version: the horns' short
# answers "too repetitive and happy", the stabs and the way home "too happy and jumpy", "too much like a video
# game"; the lift "gets good again".)

WP_A = ['Cm', 'Bb/C', 'Ab/C', 'G/B']
WP_AM = ['Cm', 'Eb/Bb', 'Ab', 'Gsus4 G']
WP_LIFT = ['Ab', 'Fm7', 'Db', 'Eb', 'Bb', 'Gm7', 'Eb', 'F']
WP_BREAK = ['Cm', 'Cm']
WP_HOME = ['Ab', 'Bb', 'Csus4 C', 'C']
WP_RISE = ['G', 'G']
WP_CALL = ['G4:0:1.5 C5:1.5:2.5', 'Bb4:0:1 D5:1:1 F5:2:2', 'Eb5:0:1.5 C5:1.5:1.5 Ab4:3:1', 'D5:0:4']
WP_CALL_M = ['G4:0:1.5 C5:1.5:2.5', 'Bb4:0:1 Eb5:1:1 G5:2:2', 'F5:0:1.5 Eb5:1.5:1.5 C5:3:1', 'C5:0:2 B4:2:2']
WP_HORNS = ['Eb4:0:4', 'D4:0:4', 'C4:0:4', 'B3:0:4']
WP_HORNS_M = ['Eb4:0:4', 'Eb4:0:4', 'C4:0:4', 'D4:0:4']
WP_TUNE = ['C4:0:3 Eb4:3:1', 'Ab4:0:2 G4:2:1 F4:3:1', 'F4:0:3 Ab4:3:1', 'G4:0:2 Bb4:2:2',
           'D5:0:3 Bb4:3:1', 'Bb4:0:2 A4:2:1 G4:3:1', 'G4:0:2 Bb4:2:1 Eb5:3:1', 'C5:0:4']
WP_UNDER = ['Ab3:0:4', 'C4:0:4', 'Ab3:0:4', 'Bb3:0:4', 'F4:0:4', 'D4:0:4', 'Bb3:0:4', 'A3:0:4']
WP_HOME_TUNE = ['C5:0:3 Eb5:3:1', 'D5:0:3 F5:3:1', 'F5:0:2 E5:2:2', 'G5:0:4']
WP_HOME_UNDER = ['Eb4:0:4', 'D4:0:4', 'C4:0:4', 'E4:0:4']
G_PHRYGIAN_DOM = (0, 1, 4, 5, 7, 8, 10)  # (G, A flat, B, C, D, E flat, F: C minor's scale over its G)


def waypoint():
    pass_ = WP_A + WP_AM + WP_LIFT + WP_BREAK + WP_HOME + WP_RISE    # 4 + 4 + 8 + 2 + 4 + 2 = 24
    form = ['Cm', 'Cm'] + pass_ + pass_
    s = Piece(168, 1707, form, level=0.5)
    ch = orchestra(s)
    mo, hi_, lo, br, hn, hn2, pc, tp, hp, st, bs = (ch[k] for k in ('motor', 'hit', 'low', 'brass', 'horn', 'horn2',
                                                                 'piccolo', 'timpani', 'harp', 'strings', 'bass'))
    n = len(form)                                   # 2 + 24 + 24 = 50 bars, 71.4 s
    s.loop_at(2)
    gallop(s, mo, range(0, 2), 62)                  # the intro: the gallop alone, the timpani rolling into the call
    s.ramp(mo, 0, 2, 50, 127)
    s.roll(tp, 1, 0, 4, 'G1', 10, 88)
    for P, second in ((2, False), (26, True)):
        A, AM, L, B, H, R = P, P + 4, P + 8, P + 16, P + 18, P + 22
        for b in (A, AM, L, L + 4, B, H):
            s.sect(b)
        gallop(s, mo, range(P, R + 2), 64)
        pedal332(s, lo, range(A, L), 88)
        pedal332(s, lo, range(H, R), 92)
        # the call over the pedal: the brass in long notes, the horns' line falling under it
        for blk in (A, AM):
            s.crash(blk, 0, 106)
            for b in (blk, blk + 2):
                for beat in (ACC332 if second else (0, 1.5)):
                    hit(s, hi_, b, beat, 108, timpani=tp if beat == 0 else None)
        if not second:
            s.tune(br, A, WP_CALL, 84)
            sung(s, hn, A, WP_HORNS, 72)
            s.tune(br, AM, WP_CALL_M, 86)
            sung(s, hn, AM, WP_HORNS_M, 74)
        else:
            s.tune(hn, A, WP_CALL, 86)               # on the horns in octaves, the brass's chords under
            s.tune(hn2, A, [up(t, -12) for t in WP_CALL], 72)
            s.pad(br, range(A, AM), 55, 70, 3, 60)
            sung(s, st, AM, [up(t, 12) for t in WP_CALL_M], 80, e0=70, e1=124, e2=100)
            s.tune(st, AM, WP_CALL_M, 70)
            sung(s, hn, AM, WP_HORNS_M, 70)
        s.run(pc, AM + 3, 2, scale_run(79, 91, G_PHRYGIAN_DOM)[:8], 0.25, 58, 2)     # G up to G, into the lift
        # the lift: A flat, F minor, D flat, E flat, B flat, G minor, E flat, F: the horn's tune in long notes
        s.crash(L, 0, 104)
        hit(s, hi_, L, 0, 104, timpani=tp)
        s.bass(bs, range(L, B), [(0, 1.9, 'r'), (2, 1.9, '5')], 76, lo=28, hi=40)
        if not second:
            sung(s, hn, L, WP_TUNE, 84, e0=70, e1=124, e2=100)
        else:
            sung(s, st, L, [up(t, 12) for t in WP_TUNE], 80, e0=70, e1=124, e2=100)
            s.tune(st, L, WP_TUNE, 70)
            sung(s, hn, L, WP_UNDER, 66)
        hit(s, hi_, L + 4, 0, 100)
        s.run(pc, L + 3, 2, scale_run(75, 87, MAJOR)[:8], 0.25, 60, 2)          # E flat up to E flat
        s.run(pc, L + 7, 2, scale_run(77, 89, MAJOR)[:8], 0.25, 62, 2)          # F up to F
        # the break: the motor and the timpani alone in C minor, a piccolo run into the way home
        for b in (B, B + 1):
            for beat in range(4):
                s.play(tp, b, beat, 0.5, 'C2', 70 + 6 * beat + 10 * (b - B))
        s.run(pc, B + 1, 2, scale_run(72, 84, MINOR)[:8], 0.25, 60, 2)          # C up to C
        # home: A flat, B flat, C: the strings' broad tune, the horns under it, the brass's chords
        s.crash(H, 0, 110)
        for b in (H, H + 2):
            for beat in ((0, 1.5) if second else (0,)):
                hit(s, hi_, b, beat, 108, timpani=tp if beat == 0 else None)
        s.bass(bs, range(H, R), [(0, 3.9, 'r')], 78, lo=28, hi=40)
        s.pad(br, range(H, R), 55, 70, 3, 64 if second else 58)
        if not second:
            sung(s, st, H, WP_HOME_TUNE, 84, e0=76, e1=124, e2=104)
        else:
            sung(s, st, H, [up(t, 12) for t in WP_HOME_TUNE], 80, e0=76, e1=124, e2=104)
            s.tune(st, H, WP_HOME_TUNE, 72)
        sung(s, hn, H, WP_HOME_UNDER, 70)
        # the riser on G: the horns swelling, the timpani rolling up from nothing, the harp sweeping up G
        held(s, hn, R, 0, 8, 'D4', 74, e0=40, e1=127, e2=127)
        held(s, hn2, R, 0, 8, 'B3', 70, e0=40, e1=127, e2=127)
        s.roll(tp, R, 0, 8, 'G1', 8, 104)
        sweep(s, hp, R + 1, {7, 11, 2}, 43, 86, 58)
        if second:
            s.roll(9, R + 1, 2, 2, DRUM['snare'], 18, 86, step=0.125)
    s.end(n)
    return s


# --- CRUISE8: "Starwake" (D minor, 150) ---------------------------------------------
# Pensive, then very fast, in turn.  Chimes toll over a lament on the horn (D minor, B flat, G minor, A7), the
# spiccato soft, the low strings holding; then the music breaks into a run: the spiccato in sixteenths, the low
# strings driving eighths, the chimes ringing a fast figure in eighths (each three-note group a step on: 3+3+2), the
# horns in octaves falling down D minor's Andalusian steps (D minor, C, B flat, A), hits and a crash.  The lament
# again on the strings in octaves, in G minor and darker (B flat minor's D flat on the way); and the run again,
# wilder, rocking between D minor and its Neapolitan E flat, the brass an octave under the strings, the piccolo's
# runs.  The
# chimes toll A over a timpani roll and the horns swell into the top, or on into Outbound's E minor.  (The user,
# 2026-10-24, on its two earlier versions: "repetitive with the rest", then "too happy... maybe we should simply
# delete it... unless we can really soup it up with some very pensive minor pieces and some very fast minor pieces
# with chimes".)

SW_P1 = ['Dm', 'Bbmaj7/D', 'Gm/D', 'A7/C#', 'Dm', 'F/C', 'Bbmaj7', 'Asus4 A']
SW_F1 = ['Dm', 'C', 'Bb', 'A', 'Dm', 'C', 'Bb', 'A7']
SW_P2 = ['Gm', 'Ebmaj7', 'Cm9', 'D7sus4 D', 'Gm/Bb', 'Ebmaj7', 'Bbm6', 'A7sus4 A']
SW_F2 = ['Dm', 'Eb', 'Dm', 'Eb', 'Bb', 'Gm', 'Eb', 'A7']
SW_LAMENT = ['A4:0:3 F4:3:1', 'D5:0:2 C5:2:1 A4:3:1', 'Bb4:0:3 G4:3:1', 'E4:0:2 C#4:2:2', 'D4:0:1.5 F4:1.5:.5 A4:2:2',
             'C5:0:3 A4:3:1', 'D5:0:2 C5:2:1 Bb4:3:1', 'A4:0:4']
SW_LAMENT_UNDER = ['F3:0:4', 'F3:0:4', 'D3:0:4', 'G3:0:2 E3:2:2', 'F3:0:4', 'F3:0:4', 'F3:0:4', 'E3:0:2 C#3:2:2']
SW_DRIVE = ['D5:0:2 A4:2:2', 'C5:0:2 G4:2:2', 'Bb4:0:2 F4:2:2', 'A4:0:3 E4:3:1', 'D5:0:1.5 E5:1.5:.5 F5:2:2',
            'E5:0:1.5 D5:1.5:.5 C5:2:2', 'D5:0:2 Bb4:2:2', 'C#5:0:2 E5:2:2']
SW_LAMENT2 = ['D5:0:3 Bb4:3:1', 'G5:0:2 F5:2:1 Eb5:3:1', 'D5:0:3 C5:3:1', 'C5:0:2 A4:2:1 F#4:3:1',
              'Bb4:0:1.5 D5:1.5:.5 G5:2:2', 'G5:0:2 Bb5:2:1 G5:3:1', 'F5:0:2 Db5:2:2', 'E5:0:2 C#5:2:2']
SW_LAMENT2_UNDER = ['G3:0:4', 'Bb3:0:4', 'G3:0:4', 'G3:0:2 F#3:2:2', 'G3:0:4', 'G3:0:4', 'F3:0:4', 'E3:0:4']
SW_DRIVE2 = ['A5:0:2 F5:2:2', 'G5:0:2 Eb5:2:2', 'F5:0:2 D5:2:2', 'Eb5:0:2 Bb4:2:2', 'D5:0:2 F5:2:2', 'G5:0:3 Bb5:3:1',
             'G5:0:2 Eb5:2:2', 'C#5:0:2 E5:2:2']
CARILLON = [0, 2, 4, 1, 3, 5, 2, 4]                     # the chimes' run: rising threes a step apart (3+3+2)
D_HARMONIC = (0, 1, 4, 5, 7, 8, 10)                     # (from A: D harmonic minor, A B flat C sharp D E F G)
LOW_DRIVE = [(k * .5, .45, w) for k, w in enumerate('rr8r5r8r')]


def toll(s, ch, bars, vel, lo=62, hi=73, dur=2.0):
    """the chimes tolling: each bar's root on its downbeat, ringing"""
    for b in bars:
        s.play(ch, b, 0, dur, in_range(chord(s.sym_at(b, 0)).root, lo, hi), vel, 3)


def starwake():
    form = ['Dm', 'Dm'] + SW_P1 + SW_F1 + SW_P2 + SW_F2 + ['A', 'A', 'A', 'A']
    s = Piece(150, 1808, form, level=-0.6)
    ch = orchestra(s)
    mo, hi_, lo, br, hn, hn2, pc, tp, hp, st, bs = (ch[k] for k in ('motor', 'hit', 'low', 'brass', 'horn', 'horn2',
                                                                 'piccolo', 'timpani', 'harp', 'strings', 'bass'))
    cm = 12
    s.setup(cm, 14, 96)                             # the chimes
    n = len(form)                                   # 2 + 32 + 4 = 38 bars, 60.8 s
    P1, F1, P2, F2, R = 2, 10, 18, 26, 34
    s.loop_at(P1)
    for b in range(P1, R, 4):
        s.sect(b)
    # the intro: the chimes tolling D over a low D, the timpani rolling in
    s.play(cm, 0, 0, 2.0, 'D5', 72)
    s.play(cm, 0, 0, 2.0, 'D4', 60)
    s.play(cm, 1, 0, 2.0, 'A4', 66)
    s.play(lo, 0, 0, 7.8, 'D2', 74)
    s.roll(tp, 1, 0, 4, 'D2', 10, 70)
    s.arp(mo, range(0, 2), [0, 1, 2, 1], 0.25, 50, 69, 3, 40, accent=6, gate=0.5)
    s.ramp(mo, 0, 2, 40, 127)
    # the laments: the chimes tolling, the spiccato soft, the low strings holding, the harp in quarters
    for P, tune, under, second in ((P1, SW_LAMENT, SW_LAMENT_UNDER, False), (P2, SW_LAMENT2, SW_LAMENT2_UNDER, True)):
        toll(s, cm, range(P, P + 8), 64 if not second else 68)
        s.arp(mo, range(P, P + 8), [0, 1, 2, 1], 0.25, 50, 69, 3, 40 if not second else 46, accent=6, gate=0.5)
        s.bass(lo, range(P, P + 8), [(0, 3.8, 'r')], 78, lo=33, hi=44)
        s.arp(hp, range(P, P + 8), [0, 1, 2, 3], 1, 57, 81, 4, 44, accent=4, gate=0.6)
        s.play(tp, P, 0, 1, in_range(chord(s.sym_at(P, 0)).root, 33, 44), 72)
        s.play(tp, P + 4, 0, 1, in_range(chord(s.sym_at(P + 4, 0)).root, 33, 44), 70)
        if not second:
            sung(s, hn, P, tune, 82, e0=60, e1=122, e2=94)
            sung(s, hn2, P + 4, under[4:], 62)
        else:
            sung(s, st, P, tune, 80, e0=60, e1=122, e2=94)
            s.tune(st, P, [up(t, -12) for t in tune], 70)
            sung(s, hn, P, under, 66)
        s.roll(tp, P + 7, 2, 2, 'A1', 20, 92, step=0.125)
    # the runs: the spiccato in sixteenths, the low strings driving, the chimes' run, the horns (then the strings)
    for F, tune, wild in ((F1, SW_DRIVE, False), (F2, SW_DRIVE2, True)):
        s.arp(mo, range(F, F + 8), [0, 2, 1, 3, 2, 4, 3, 2], 0.25, 50, 74, 4, 60 if not wild else 64, accent=10,
              gate=0.6)
        s.bass(lo, range(F, F + 8), [(t, .35, w) for t, _d, w in LOW_DRIVE], 84, lo=36, hi=47)
        s.arp(cm, range(F, F + 8), CARILLON, 0.5, 62 if not wild else 67, 86, 3, 60 if not wild else 64, accent=8,
              gate=0.5)
        for b in range(F, F + 8):
            if b % 2:
                r = in_range(chord(s.sym_at(b, 0)).root, 33, 44)
                s.play(tp, b, 0, .5, r, 80)
                s.play(tp, b, 2, .5, r, 74)
                s.play(tp, b, 3, .25, r + 7 if r + 7 <= 44 else r - 5, 70)
        for blk in (F, F + 4):
            s.crash(blk, 0, 108 if blk == F else (104 if wild else 96))
            for b in (blk, blk + 2):
                for beat in (ACC332 if wild else (0, 1.5)):
                    hit(s, hi_, b, beat, 108, timpani=tp if beat == 0 else None)
        if not wild:
            sung(s, hn, F, tune, 86, e0=80, e1=127, e2=108)
            s.tune(hn2, F, [up(t, -12) for t in tune], 74)
        else:
            sung(s, st, F, tune, 82, e0=80, e1=127, e2=108)
            s.tune(br, F, [up(t, -12) for t in tune], 82)
            sung(s, hn, F, ['F4:0:4', 'G4:0:4', 'F4:0:4', 'G4:0:4', 'F4:0:4', 'D4:0:4', 'Eb4:0:4', 'E4:0:4'], 70)
        s.run(pc, F + 3, 2, scale_run(81, 93, D_HARMONIC)[:8], 0.25, 58, 2)      # A up to A: D harmonic minor
        s.run(pc, F + 7, 2, scale_run(81, 93, D_HARMONIC)[:8], 0.25, 62, 2)
        s.roll(9, F + 7, 2, 2, DRUM['snare'], 18, 84, step=0.125)
    # the riser on A: the spiccato pulsing, the chimes tolling A louder each bar, the horns swelling, the timpani
    pulses(s, mo, range(R, n), 56)
    s.ramp(mo, R, 4, 50, 127)
    for k in range(4):
        s.play(cm, R + k, 0, 2.0, ['A4', 'A3'] if k < 3 else ['A4', 'E5', 'A3'], 60 + 6 * k, 3)
    held(s, hn, R, 0, 16, 'E4', 72, e0=40, e1=127, e2=127)
    held(s, hn2, R, 0, 16, 'C#4', 68, e0=40, e1=127, e2=127)
    s.roll(tp, R, 0, 16, 'A1', 6, 112)
    sweep(s, hp, R + 3, {9, 1, 4}, 45, 88, 58)
    s.roll(9, R + 3, 2, 2, DRUM['snare'], 18, 88, step=0.125)
    s.end(n)
    return s


# --- ROCKS: "Rock Field" (G minor, 120): asteroid and mine fields --------------------
# A march through the rocks: the snare's ostinato, the timpani and the low strings marching, the strings
# tumbling in sixteenths, the horn's theme in G minor (B major over it at the fifth bar: a rock looming).  Then
# the drift in B flat, the march gone: the harp tumbling softly, the strings' long line through D flat and
# G flat, the piccolo's glints, the timpani rolling far off.  Then the theme on the strings with the synth
# brass's descant over it, every block hit, and a snare roll on D into the march.  The second pass: the theme
# on the horns in octaves, the glints doubled by the harp, a timpani roll under the snare's.

RF_A = ['Gm', 'Eb', 'Cm', 'D', 'Gm', 'B', 'Cm', 'D7']
RF_DRIFT = ['Bbmaj7', 'Gm9', 'Ebmaj7', 'Db', 'Bbmaj7', 'Dm7', 'Gb', 'Dsus4 D']
RF_RISE = ['Dsus4', 'D']
RF_THEME = ['D4:0:1.5 G4:1.5:.5 G4:2:1 Bb4:3:1', 'Bb4:0:2 G4:2:1 Eb4:3:1', 'C4:0:1 Eb4:1:1 G4:2:1.5 F4:3.5:.5',
            'F#4:0:3 A4:3:1', 'G4:0:1.5 Bb4:1.5:.5 D5:2:2', 'D#5:0:2 F#5:2:1 D#5:3:1',
            'Eb5:0:1 D5:1:1 C5:2:1 G4:3:1', 'F#4:0:2 A4:2:1 C5:3:1']
RF_DESCANT = ['D6:0:4', 'Eb6:0:2 G5:2:2', 'G5:0:4', 'F#5:0:4', 'Bb5:0:4', 'B5:0:2 A#5:2:2', 'C6:0:4',
              'A5:0:2 C6:2:2']
RF_LINE = ['D5:0:4', 'A5:0:4', 'G5:0:2 Bb5:2:2', 'F5:0:4', 'D5:0:2 F5:2:2', 'C5:0:4', 'Bb4:0:2 Db5:2:2',
           'G4:0:2 F#4:2:2']
RF_GLINTS = [[(1, 89), (2.75, 94)], [(0.5, 93), (3, 86)], [(1.5, 91), (3.25, 98)], [(0.75, 89), (2.5, 92)],
             [(1, 86), (2.25, 93)], [(0.5, 96), (3, 88)], [(1.25, 89), (2.75, 94)], [(0.5, 91), (2, 90)]]
MARCH = {'snare': [(0, 52), (.75, 44), (1, 64), (2, 52), (2.75, 44), (3, 64)], 'kick': hits(0, v=84)}


def rock_field():
    pass_ = RF_A + RF_DRIFT + RF_A + RF_RISE        # 8 + 8 + 8 + 2 = 26 bars
    form = ['Gm', 'Gm'] + pass_ + pass_
    s = Piece(120, 1909, form, level=2.5)
    ch = orchestra(s)
    mo, hi_, lo, br, hn, hn2, pc, tp, hp, st, bs = (ch[k] for k in ('motor', 'hit', 'low', 'brass', 'horn', 'horn2',
                                                                 'piccolo', 'timpani', 'harp', 'strings', 'bass'))
    n = len(form)                                   # 2 + 26 + 26 = 54 bars, 108 s
    s.loop_at(2)
    tumble(s, mo, range(0, 2), 56)                  # the intro: the tumbling, coming up, the snare joining
    s.ramp(mo, 0, 2, 50, 120)
    s.drums(range(1, 2), MARCH)
    for P, second in ((2, False), (28, True)):
        A, D, A2, R = P, P + 8, P + 16, P + 24
        for b in (A, A + 4, D, D + 4, A2, A2 + 4):
            s.sect(b)
        # the march: snare, kick, timpani, the low strings striding, the strings tumbling; the horn's theme
        for blk in (A, A2):
            tumble(s, mo, range(blk, blk + 8), 58)
            s.drums(range(blk, blk + 8), MARCH)
            s.crash(blk, 0, 104)
            hit(s, hi_, blk, 0, 104)
            hit(s, hi_, blk + 4, 0, 98)
            for b in range(blk, blk + 8):
                c = chord(s.sym_at(b, 0))
                r = in_range(c.bass, 31, 42)
                fifth = 7 if 7 in c.tones else 6
                for beat, q in ((0, r), (1, r + fifth), (2, r), (3, r + fifth)):
                    s.play(lo, b, beat, 0.7, q, 86 + (8 if beat == 0 else 0), 3)
                s.play(tp, b, 0, 0.6, in_range(c.root, 31, 42), 76)
                s.play(tp, b, 2, 0.6, in_range(c.root, 31, 42), 66)
            if second and blk == A2:
                hit(s, hi_, blk + 2, 0, 96)
                hit(s, hi_, blk + 6, 0, 96)
        if not second:
            s.tune(hn, A, RF_THEME, 86)
        else:
            s.tune(hn, A, RF_THEME, 86)
            s.tune(hn2, A, [up(t, -12) for t in RF_THEME], 70)
        s.run(pc, A + 7, 2, [74, 78, 81, 84, 86, 90, 93, 96], 0.125, 58, 2)          # D7 up, quick
        # the drift: B flat, the march gone; the harp tumbling softly, the strings' line, the glints
        s.arp(hp, range(D, D + 8), [0, 2, 1, 3], 0.25, 58, 82, 4, 46, accent=6)
        s.bass(bs, range(D, D + 8), [(0, 3.9, 'r')], 70, lo=29, hi=41)
        sung(s, st, D, RF_LINE, 72, e0=50, e1=110, e2=80)
        for k, g in enumerate(RF_GLINTS):
            glints(s, pc, D + k, g, 60)
            if second:
                s.play(hp, D + k, g[1][0], 0.5, g[1][1] - 12, 50)
        s.run(pc, D + 3, 2, [77, 80, 82, 85, 89, 92, 94, 97], 0.125, 52, 1)         # D flat's F up, glinting
        s.run(pc, D + 6, 2, [70, 73, 78, 82, 85, 90, 94, 97], 0.125, 52, 1)         # G flat's chord up
        s.roll(tp, D, 0, 4, 'Bb1', 20, 50, step=0.25)
        s.roll(tp, D + 6, 0, 8, 'D2', 20, 84, step=0.25)
        # the theme on the strings with the descant over it
        if not second:
            s.tune(st, A2, RF_THEME, 82)
        else:
            s.tune(st, A2, [up(t, 12) for t in RF_THEME], 78)
            s.tune(st, A2, RF_THEME, 70)
        sung(s, br, A2, RF_DESCANT, 74, e0=70, e1=120, e2=96)
        # the riser on D: the tumbling on, a snare roll from nothing, the horns swelling
        tumble(s, mo, range(R, R + 2), 60)
        held(s, hn, R, 0, 8, 'A3', 72, e0=40, e1=127, e2=127)
        held(s, hn2, R, 0, 8, 'D3', 66, e0=40, e1=127, e2=127)
        s.roll(9, R + 1, 0, 4, DRUM['snare'], 14, 90, step=0.25)
        if second:
            s.roll(tp, R, 0, 8, 'D2', 10, 96, step=0.25)
        else:
            s.play(tp, R, 0, 1, 'D2', 80)
    s.end(n)
    return s


SONGS = {'CRUISE6.MID': long_burn, 'CRUISE7.MID': waypoint, 'CRUISE8.MID': starwake, 'ROCKS.MID': rock_field}
