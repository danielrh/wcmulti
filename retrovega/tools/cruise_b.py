"""The third draft's cruise pieces six to eight and the rock field (CC0, ours), in Outbound's style
(tools/flightsongs.py): a fast motor under a tune in long notes, a few parts each with its own job, keys that
move by thirds, hits where a groove would be, piccolo runs and harp sweeps, a riser into the loop.

    CRUISE6  "Long Burn"   A minor into C sharp minor, 12/8 at 112: repeated notes burning for eight bars,
                           then a long tune on the strings with the horn under it; a harp sweep on E
    CRUISE7  "Waypoint"    C, major and minor, 168: a galloping string motor, synth-brass stabs answered by
                           the horns, a lift into A flat and B flat, a snare roll on G
    CRUISE8  "Starwake"    G, B, E flat and home, 12/8 at 120: harp and strings flowing, the set's biggest
                           tune, horns in thirds; timpani and harp on D (into Outbound's E minor)
    ROCKS    "Rock Field"  G minor, 120: asteroid and mine fields: a march (snare, timpani, low strings), the
                           strings tumbling in sixteenths, the horn's theme; a drift in B flat with harp and
                           piccolo glints; the theme again with a synth-brass descant

The cruise pieces are in the set's cycle of keys (each riser on its own dominant, the next piece in the key a
third or a fifth on): Outbound's E minor, G minor, B minor, D, F minor, A minor (Long Burn), C (Waypoint), G
(Starwake), and E minor again.  Every piece marks its blocks for the score's follow-ons (Piece.sect).
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


# --- CRUISE7: "Waypoint" (C, major and minor, 168) -----------------------------------
# Brisk travel between nav points.  The strings gallop (each beat the bass, its octave, its twelfth); the synth
# brass stabs out the call in C major and the horns answer, then the same in C minor; a lift into A flat and B
# flat for the horn's tune (D flat on the way: the Neapolitan's colour), two bars of the motor and timpani alone,
# and C major again with the strings' tune over the stabs; a snare roll on G into the loop.  The second pass:
# the piccolo answers, the tune on the strings in octaves with the horn under, every accent hit.

WP_A = ['C', 'Bb/C', 'F/C', 'C']
WP_AM = ['Cm', 'Ab/C', 'Fm/C', 'G7']
WP_LIFT = ['Ab', 'Fm7', 'Db', 'Eb', 'Bb', 'Gm7', 'Eb', 'F']
WP_BREAK = ['Cm', 'Cm']
WP_HOME = ['C', 'Ab', 'Bb', 'C']
WP_RISE = ['G', 'G']
WP_CALL = [(0, .5), (.75, .25), (1.5, .5), (3, .75)]
WP_CALL2 = [(0, .5), (1, .25), (1.5, .25), (2.5, .5), (3.5, .25)]
WP_ANSWER = ['D4:0:1 F4:1:1 Bb4:2:2', 'G4:0:1 E4:1:1 C5:2:2']
WP_ANSWER_M = ['C4:0:1 Eb4:1:1 Ab4:2:2', 'B3:0:1 D4:1:1 F4:2:2']
WP_TUNE = ['C4:0:3 Eb4:3:1', 'Ab4:0:2 G4:2:1 F4:3:1', 'F4:0:3 Ab4:3:1', 'G4:0:2 Bb4:2:2',
           'D5:0:3 Bb4:3:1', 'Bb4:0:2 A4:2:1 G4:3:1', 'G4:0:2 Bb4:2:1 Eb5:3:1', 'C5:0:4']
WP_UNDER = ['Ab3:0:4', 'C4:0:4', 'Ab3:0:4', 'Bb3:0:4', 'F4:0:4', 'D4:0:4', 'Bb3:0:4', 'A3:0:4']
WP_HOME_TUNE = ['E5:0:3 G5:3:1', 'Ab5:0:2 G5:2:1 Eb5:3:1', 'F5:0:2 D5:2:2', 'E5:0:4']


def waypoint():
    pass_ = WP_A + WP_AM + WP_LIFT + WP_BREAK + WP_HOME + WP_RISE    # 4 + 4 + 8 + 2 + 4 + 2 = 24
    form = ['C', 'C'] + pass_ + pass_
    s = Piece(168, 1707, form, level=1.5)
    ch = orchestra(s)
    mo, hi_, lo, br, hn, hn2, pc, tp, hp, st, bs = (ch[k] for k in ('motor', 'hit', 'low', 'brass', 'horn', 'horn2',
                                                                 'piccolo', 'timpani', 'harp', 'strings', 'bass'))
    n = len(form)                                   # 2 + 24 + 24 = 50 bars, 71.4 s
    s.loop_at(2)
    gallop(s, mo, range(0, 2), 62)                  # the intro: the gallop alone, and a snare roll into the call
    s.ramp(mo, 0, 2, 50, 127)
    s.roll(9, 1, 2, 2, DRUM['snare'], 18, 80, step=0.25)
    for P, second in ((2, False), (26, True)):
        A, AM, L, B, H, R = P, P + 4, P + 8, P + 16, P + 18, P + 22
        for b in (A, AM, L, L + 4, B, H):
            s.sect(b)
        gallop(s, mo, range(P, R + 2), 64)
        pedal332(s, lo, range(A, L), 88)
        pedal332(s, lo, range(H, R), 92)
        # the call: stabs in C major, the horns answering; then in C minor
        for blk, ans in ((A, WP_ANSWER), (AM, WP_ANSWER_M)):
            s.crash(blk, 0, 108)
            for k in (0, 2):
                stabs(s, br, blk + k, WP_CALL if k == 0 else WP_CALL2, 78)
                hit(s, hi_, blk + k, 0, 108)
                if second:
                    hit(s, hi_, blk + k, 1.5, 96)
            if not second:
                s.tune(hn, blk + 1, [ans[0]], 80)
                s.tune(hn, blk + 3, [ans[1]], 82)
            else:
                s.tune(pc, blk + 1, [up(ans[0], 24)], 60)
                s.tune(st, blk + 3, [up(ans[1], 12)], 82)
                s.tune(st, blk + 3, [ans[1]], 70)
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
        # the break: the motor and the timpani alone in C minor, a piccolo run into C major
        for b in (B, B + 1):
            for beat in range(4):
                s.play(tp, b, beat, 0.5, 'C2', 70 + 6 * beat + 10 * (b - B))
        s.run(pc, B + 1, 2, scale_run(72, 84, MINOR)[:8], 0.25, 60, 2)          # C up to C
        # home: C major, A flat, B flat, C: the stabs and the strings' tune over them
        s.crash(H, 0, 112)
        for k in range(4):
            stabs(s, br, H + k, WP_CALL if k % 2 == 0 else WP_CALL2, 74)
            hit(s, hi_, H + k, 0, 108 if k % 2 == 0 else 100)
            if second:
                hit(s, hi_, H + k, 1.5, 94)
        sung(s, st, H, WP_HOME_TUNE, 84, e0=76, e1=124, e2=104)
        if second:
            s.tune(pc, H + 2, [up(t, 12) for t in WP_HOME_TUNE[2:]], 54)
        # the riser on G: horns swelling, the timpani rolling, a snare roll into the call
        held(s, hn, R, 0, 8, 'D4', 74, e0=40, e1=127, e2=127)
        held(s, hn2, R, 0, 8, 'B3', 70, e0=40, e1=127, e2=127)
        s.roll(tp, R, 0, 4, 'G1', 14, 90, step=0.25)
        s.roll(9, R + 1, 0, 4, DRUM['snare'], 16, 92, step=0.25)
        if second:
            sweep(s, hp, R + 1, {7, 11, 2}, 43, 86, 58)
    s.end(n)
    return s


# --- CRUISE8: "Starwake" (G, B, E flat, G; 12/8 at 120) ----------------------------
# The big open vista.  Harp eighths flowing over the strings' broken chords, the low strings on roots and fifths,
# and the set's biggest tune, climbing through three lifts of a major third (G, B, E flat) and home to G with
# horns in thirds under it; the brass's fanfare in the last block.  Timpani and harp on D, G's dominant, throw it
# back to the top, or on into Outbound's E minor (D to E minor: V to vi).  The second pass: the tune on the
# strings in octaves, the horns under it, the piccolo over the lifts, every lift hit.

SW_G = ['G', 'Cmaj7/G', 'Em', 'D/F#']
SW_B = ['B', 'G#m7', 'Emaj7', 'Bb7']
SW_EB = ['Eb', 'Cm7', 'Abmaj7', 'D']
SW_HOME = ['G', 'Em7', 'Cmaj7', 'D']
SW_TUNE = ['D4:0:2 G4:2:1 B4:3:1', 'E5:0:3 D5:3:1', 'B4:0:2 G4:2:1 E4:3:1', 'F#4:0:2 A4:2:1 D5:3:1',
           'D#5:0:3 F#5:3:1', 'B5:0:2 A#5:2:1 G#5:3:1', 'G#5:0:3 B5:3:1', 'D6:0:2 C6:2:1 Ab5:3:1',
           'G5:0:3 Bb5:3:1', 'C6:0:2 Bb5:2:1 G5:3:1', 'Eb5:0:2 G5:2:1 C6:3:1', 'D6:0:2 C6:2:1 A5:3:1',
           'B5:0:3 D6:3:1', 'E6:0:2 D6:2:1 B5:3:1', 'C6:0:2 B5:2:1 G5:3:1', 'A5:0:4']
SW_THIRDS = ['B3:0:4', 'C4:0:4', 'G3:0:4', 'A3:0:4', 'D#4:0:4', 'D#4:0:4', 'D#4:0:4', 'D4:0:4',
             'G4:0:4', 'Eb4:0:4', 'Eb4:0:4', 'F#4:0:4', 'B3:0:4', 'G3:0:4', 'E4:0:4', 'F#4:0:4']
SW_FANFARE = ['B4:0:2 D5:2:1 G5:3:1', 'E5:0:2 G5:2:1 B5:3:1', 'G5:0:2 E5:2:1 C5:3:1', 'A4:0:4']


def starwake():
    pass_ = SW_G + SW_B + SW_EB + SW_HOME           # 16 bars
    form = ['G'] + pass_ + pass_
    s = Piece(120, 1808, form, level=1.0)
    ch = orchestra(s)
    mo, hi_, lo, br, hn, hn2, pc, tp, hp, st, bs = (ch[k] for k in ('motor', 'hit', 'low', 'brass', 'horn', 'horn2',
                                                                 'piccolo', 'timpani', 'harp', 'strings', 'bass'))
    n = len(form)                                   # 1 + 16 + 16 = 33 bars, 66 s
    s.loop_at(1)
    s.arp(hp, range(0, 1), [0, 1, 2, 3, 2, 1], THIRD, 55, 79, 4, 50, accent=6)   # the intro: the harp alone
    s.play(tp, 0, 0, 1, 'G1', 66)
    for P, second in ((1, False), (17, True)):
        Bk, E, H = P + 4, P + 8, P + 12
        for b in (P, Bk, E, H):
            s.sect(b)
        s.arp(hp, range(P, P + 15), [0, 1, 2, 3, 2, 1], THIRD, 55, 79, 4, 54, accent=6)
        s.arp(mo, range(P, P + 16), [0, 2, 1], THIRD, 40, 55, 3, 52, accent=8)
        s.bass(bs, range(P, P + 16), [(0, 0.9, 'r'), (2, 0.9, '5')], 78, lo=28, hi=40)
        # the tune through the lifts: the horn's (then the strings' in octaves), the horns' thirds under it
        if not second:
            sung(s, hn, P, [up(t, -12) if k >= 4 else t for k, t in enumerate(SW_TUNE)], 80, e0=70, e1=124, e2=100)
            sung(s, hn2, H, SW_THIRDS[12:], 66)
        else:
            sung(s, st, P, SW_TUNE, 82, e0=70, e1=124, e2=100)
            s.tune(st, P, [up(t, -12) for t in SW_TUNE], 68)
            sung(s, hn, P, SW_THIRDS, 66)
        # the lifts: a crash and a hit at each, the timpani on the new root
        for b in (Bk, E, H):
            s.crash(b, 0, 110 if b == H else 100)
            hit(s, hi_, b, 0, 108, timpani=tp)
            hit(s, hi_, b + 2, 0, 96)
            if second:
                hit(s, hi_, b, 1 + THIRD * 2, 96)
        s.roll(9, H - 1, 3, 1, DRUM['snare'], 20, 84, step=SIXTH)
        s.play(tp, P, 0, 1, 'G1', 80)
        run12(s, pc, Bk - 1, 2 + THIRD * 2, [78, 81, 83, 86, 90, 93, 95, 98], 58)          # F sharp up, B in it
        run12(s, pc, E - 1, 2 + THIRD * 2, scale_run(82, 94, (0, 2, 4, 5, 7, 9, 10))[:8], 60)   # B flat mixolydian
        if second:
            run12(s, pc, H - 1, 2 + THIRD * 2, [74, 78, 81, 84, 86, 90, 93, 96], 62)       # D7 up
        # home: the brass's fanfare over the tune
        s.tune(br, H, SW_FANFARE, 80 if second else 72)
        # the riser on D: the timpani rolling, the harp sweeping up D major
        s.roll(tp, H + 3, 0, 4, 'D2', 10, 104, step=SIXTH)
        sweep(s, hp, H + 3, {2, 6, 9}, 38, 86, 60)
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
