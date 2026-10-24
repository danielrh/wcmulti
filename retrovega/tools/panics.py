"""The flight's panics and stings for the third draft's orchestra (CC0, ours): tools/orch.py's parts.

    PANIC1   "Hull Breach"      C, 140: the fight's drive (a sixteenth motor on a C pedal, the low strings,
                                timpani) with the damage over it: a low horn cluster, alarms in seconds high up
                                on the brass and the piccolo, the fight's call a tritone off
    PANIC2   "Systems Failing"  climbing by half steps, 140: blocks of six bars, four climbing a half step a bar
                                and two wild ones (tritones on every eighth, a crash, a brass run, toms)
    PANIC3   "Abandon Ship"     152 into 196: panic, hope (the fight's call in major), hope dashed, hope again a
                                step higher, dashed harder, then the panic speeding up, and round again
    VICTORY1..3                 the splashes, bittersweet: A flat and B flat into C; D's minor plagal; B flat's
                                deceptive turn, ending open
    TRAGEDY  "Wingman Lost"     a funeral march in B flat minor at 52

The user (2026-10-24, the third draft): "fix all the flight music to have that same style [Outbound's] but
their own unique character and be remixable and loopable".  The panics come in on the bar over the battles
(PANIC1 and PANIC2 at the battles' 140 in C); each block starts on its downbeat with the motor and leaves
nothing hanging over the next ("sect" markers at every block, src/music.c's follow-ons).  Their drums are hits,
rolls and toms, no kit's beat.  The stings play once, over the battle, which then goes on where it was.
"""
from compose import chord
from orch import (NAMES, DRUM, Piece, hits, up, scale_run, ORCH, ACC332, orchestra, in_range, octaves, pulses,
                  pedal332, hit, held, sung, sweep, MAJOR, LYDIAN)
from songs import p

BATTLE_BPM = 140
# the fight's call (Contact's, the second draft's): ours, so the panics may quote it
CT_CALL = ['C4:0:1.5 G4:1.5:.5 G4:2:2', 'Ab4:0:1 G4:1:1 F4:2:1 Eb4:3:1', 'C4:0:2 D4:2:2', 'Eb4:0:4',
           'G4:0:1.5 C5:1.5:.5 C5:2:2', 'D5:0:1 C5:1:1 Bb4:2:1 G4:3:1', 'Ab4:0:2 C5:2:2', 'B4:0:2 D5:2:2']
DIM = (0, 2, 3, 5, 6, 8, 9, 11)          # (a run's steps: the diminished scale, a half and a whole step about)


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


def tones(c, lo, hi):
    """the chord's root, third and fifth, each placed in lo..hi"""
    return [in_range((c.root + t) % 12, lo, hi) for t in c.tones[:3]]


def motor16(s, ch, b, vel, pedal=None, lo=48, figure=(0, 1, 2, 1), accents=(0, 3, 6, 8, 11, 14), up_lo=55):
    """the fights' motor: sixteenths between a bass (the pedal, or the chord's) and the chord's tones over it
    (figure: which tone on each off-sixteenth, round), louder on the sixteenths' 3+3+2"""
    for k in range(16):
        beat = k * 0.25
        c = chord(s.sym_at(b, beat))
        bass = in_range(c.bass if pedal is None else pedal, lo, lo + 11)
        q = bass if k % 2 == 0 else tones(c, up_lo, up_lo + 11)[figure[(k // 2) % len(figure)]]
        s.play(ch, b, beat, 0.2, q, vel + (12 if k in accents else 0), 3)


def low8(s, ch, b, vel, pedal=None, figure=(0, 0, 12, 0, 7, 0, 12, 0), lo=36):
    """the low strings' eighths (Contact's 'rr8r5r8r'), on a pedal or the chord's bass"""
    for k, w in enumerate(figure):
        c = chord(s.sym_at(b, k * 0.5))
        q = in_range(c.bass if pedal is None else pedal, lo, lo + 11) + w
        s.play(ch, b, k * 0.5, 0.45, q, vel + (8 if k == 0 else 0), 3)


def swelled(s, ch, bar, beat, dur, pitches, vel, e0=40, e1=110, e2=70):
    """a chord that swells and falls back (one expression ramp for its channel), kept inside its note"""
    s.play(ch, bar, beat, dur, pitches, vel)
    steps = max(2, int(dur * 4))
    for k in range(steps + 1):
        x = k / steps
        e = e0 + (e1 - e0) * x * 2 if x <= .5 else e1 + (e2 - e1) * (x - .5) * 2
        pos = bar * 4 + beat + dur * x - (0.05 if k == steps else 0)
        s.cc(ch, int(pos // 4), pos % 4, 11, round(e))


def trill(s, ch, bar, beat, beats, a, b, vel, step=1 / 6):
    """an alarm: two notes in turn, fast"""
    n = int(round(beats / step))
    for k in range(n):
        at = beat + k * step
        s.play(ch, bar, at, step * 0.9, a if k % 2 == 0 else b, vel + (6 if k % 2 == 0 else 0), 2)


def flurry(s, ch, bar, beat, pitches, step, vel, rise=0, until=4.0):
    """a run, each note let go by `until` (the bar's end, or the block's): nothing hangs over"""
    for k, q in enumerate(pitches):
        at = beat + k * step
        if at >= until - 0.02:
            break
        s.play(ch, bar, at, min(step * 1.5, until - at - 0.03), q, vel + rise * k, 2)


def snare_roll(s, bar, beat, beats, v0, v1):
    s.roll(9, bar, beat, beats, DRUM['snare'], v0, v1, step=0.125)


# --- PANIC1: "Hull Breach" (C, 140): the hull under 60% ---------------------------
# The fight goes on, Contact's drive in the orchestra: sixteenths on a C pedal, the low strings' eighths, the
# timpani; and the damage over it: a horn cluster swelling on C and D flat, alarms in seconds high up (the brass
# on the off-beats, the piccolo's trills), the call a tritone off.  Eight blocks of four bars, each its own.

HB_BAR = ['Cm', 'Db/C', 'Cm', 'Gb/C']


def hull_breach():
    form = HB_BAR * 8
    s = Piece(BATTLE_BPM, 2313, form, level=1.0)
    ch = orchestra(s)
    hn, mo, hi_, lo, br, hn2, pc, tp, st = (ch[k] for k in ('horn', 'motor', 'hit', 'low', 'brass', 'horn2',
                                                          'piccolo', 'timpani', 'strings'))
    n = len(form)                                   # 32 bars, 54.9 s
    s.loop_at(0)
    s.sections(0, n)
    for b in range(n):
        blk, k = b // 4, b % 4
        sym = form[b]
        gb = sym.startswith('Gb')
        # the drive: the motor on its C pedal (quieter in the breakdown), the low strings but there
        motor16(s, mo, b, 52 if blk == 4 else 60 + 2 * k, pedal=0, figure=(0, 1, 2, 1) if k % 2 == 0 else (2, 1, 0, 1))
        if blk != 4:
            low8(s, lo, b, 72, pedal=0, figure=(0, 0, 12, 0, 6 if gb else 7, 0, 12, 0))
        # the damage: the horns' cluster, swelling (wider in the breakdown)
        cl = ['C3', 'Db3', 'Gb3'] if blk == 4 else (['C3', 'Db3'] if not gb else ['C3', 'Gb3'])
        swelled(s, hn2, b, 0, 3.8, cl, 70, e0=40, e1=118, e2=60)
        # the alarms: seconds on the brass's off-beats from the second block, the piccolo's from the third
        if blk >= 1 and b % 2 == 1:
            s.play(br, b, 1.5, .4, ['C5', 'Db5'], 74)
            s.play(br, b, 3, .4, ['C5', 'Db5'], 70)
        if blk >= 2 and k == 2:
            trill(s, pc, b, 0, 1, 'C6', 'Db6', 52)
            trill(s, pc, b, 2, 1, 'F#6', 'G6', 50)
        # the timpani: the first bar's figure, and a roll at each block's end
        if k == 0:
            s.line(tp, b, 'C2:0:.5 C2:2:.5 Gb1:3:.25 Gb1:3.5:.25', 84)
        if k == 3:
            s.roll(tp, b, 2, 2, 'C2', 26, 92, step=0.125)
        # the hits: the 3+3+2 of the block's first bar, and its third's first two; every accent in block 5
        if blk == 5:
            for beat in ACC332:
                hit(s, hi_, b, beat, 108)
        elif blk != 4 and k in (0, 2):
            for beat in (ACC332 if k == 0 else ACC332[:2]):
                hit(s, hi_, b, beat, 110 if beat == 0 else 100)
        if k == 3 and blk % 2 == 1:
            snare_roll(s, b, 3, 1, 30, 96)
    for blk in (0, 2, 4, 6):
        s.crash(blk * 4, 0, 104)
    # the call, a tritone off: on the horn (blocks 2 and 3), then the brass with the horn an octave under (6, 7)
    s.tune(hn, 8, [up(t, 6) for t in CT_CALL[:4]], 84)
    s.tune(hn, 12, [up(t, 6) for t in CT_CALL[4:]], 88)
    s.tune(br, 24, [up(t, 6) for t in CT_CALL[:4]], 86)
    s.tune(hn, 24, [up(t, -6) for t in CT_CALL[:4]], 76)
    s.tune(br, 28, [up(t, 6) for t in CT_CALL[4:]], 90)
    s.tune(hn, 28, [up(t, -6) for t in CT_CALL[4:]], 78)
    # the breakdown's strings: the cluster high, held under the alarms
    for b in range(16, 20):
        s.play(st, b, 0, 3.8, ['Db5', 'C5'] if b % 2 == 0 else ['Gb5', 'F5'], 58)
    s.end(n)
    return s


# --- PANIC2: "Systems Failing" (climbing by half steps, 140): the hull under 35% ----
# Blocks of six bars, each a half step up from the last and back down again (C, D flat, D, E flat, D, D flat):
# four bars climbing a half step a bar, the horns' alarm rising, the strings' cluster swelling, the timpani
# rolling into two wild bars, tritones on every eighth, a crash, the orchestra hitting the 3+3+2, a brass run
# up and the toms down.  Then the theme, the climb, comes back.

SF_STARTS = [0, 1, 2, 3, 2, 1]


def systems_failing():
    form = []
    for r0 in SF_STARTS:
        form += [NAMES[(r0 + k) % 12] + 'm' for k in range(4)] + [NAMES[(r0 + 4) % 12] + 'dim'] * 2
    s = Piece(BATTLE_BPM, 2414, form, level=0.0)
    ch = orchestra(s)
    hn, mo, hi_, lo, br, pc, tp, st = (ch[k] for k in ('horn', 'motor', 'hit', 'low', 'brass', 'piccolo',
                                                     'timpani', 'strings'))
    n = len(form)                                   # 36 bars, 61.7 s
    s.loop_at(0)
    s.sections(0, n, every=6)
    for b in range(n):
        k = b % 6
        r = chord(form[b]).root
        if k < 4:                                   # the climb
            motor16(s, mo, b, 56 + 5 * k)
            low8(s, lo, b, 68 + 4 * k, figure=(0, 0, 12, 0, 0, 12, 0, 12))
            swelled(s, st, b, 0, 3.8, [in_range(r, 60, 71), in_range(r, 60, 71) + 1], 52 + 4 * k,
                    e0=50 + 18 * k, e1=90 + 10 * k, e2=70 + 14 * k)
            if k % 2 == 1:
                s.play(br, b, 1.5, .4, [in_range(r, 72, 83), in_range(r, 72, 83) + 1], 76)
                s.play(br, b, 3, .4, [in_range(r, 72, 83), in_range(r, 72, 83) + 1], 70)
                x = in_range(r + 7, 55, 66)         # the horns' alarm: the fifth, a half step up, a tritone on
                s.line(hn, b, '%s:0:1.5 %s:1.5:.5 %s:2:1.9' % (nm(x), nm(x + 1), nm(x + 6)), 82)
            s.play(tp, b, 0, .5, in_range(r, 33, 44), 84)
            s.play(tp, b, 2, .5, in_range(r, 33, 44), 72)
            s.play(9, b, 0, .2, DRUM['kick'], 110)
            if k == 3:
                s.roll(tp, b, 2, 2, in_range(r, 33, 44), 30, 104, step=0.125)
                snare_roll(s, b, 3, 1, 34, 100)
        else:                                       # wild: tritones on every eighth
            motor16(s, mo, b, 70, figure=(1, 2, 1, 0))
            for e in range(8):
                q = in_range(r, 36, 47)
                s.play(lo, b, e * .5, .45, q + (6 if e % 2 else 0), 84)
                s.play(tp, b, e * .5, .3, in_range(r + (6 if e % 2 else 0), 33, 44), 74 + (e % 2) * 10)
                if k == 4 or e < 4:
                    s.play(br, b, e * .5, .3, [in_range(r, 60, 71), in_range(r, 60, 71) + 6], 82 + (e % 2) * 10)
            for beat in ACC332:
                hit(s, hi_, b, beat, 112)
            if k == 4:
                s.crash(b, 0, 112)
                trill(s, pc, b, 0, 2, in_range(r, 84, 95), in_range(r, 84, 95) + 6, 60)
            else:                                   # the brass's run up, the toms down
                run = [q for q in range(in_range(r, 64, 75), in_range(r, 64, 75) + 25) if (q - r) % 12 in DIM]
                flurry(s, br, b, 2, run, 0.125, 74, rise=1)
                for i, t in enumerate(('tom1', 'tom1', 'tom2', 'tom2', 'tom3', 'tom3', 'tom4', 'tom4')):
                    s.play(9, b, 2 + i * .25, .2, DRUM[t], 80 + i * 3, 2)
    s.end(n)
    return s


def nm(q):
    return '%s%d' % (NAMES[q % 12], q // 12 - 1)


# --- PANIC3: "Abandon Ship" (152 into 196): the hull under 15% ----------------------
# The arc the user asked for: panic; hope (the fight's call in major on the horn, the strings warm, the harp);
# hope dashed (the chords falling apart chromatically, the piccolo tumbling, timpani rolling); hope again, a step
# higher on the brass; dashed harder (tritones on every eighth); then the panic again, speeding up from 152 to
# about 196 over its last eight bars (the user loved the draft's speed-up), timed to take seven bars' time so
# the piece stays whole bars at 152, and round again.

AS_PANIC = ['Cm', 'Gb', 'Cm', 'Gb', 'Db', 'G', 'Ab', 'D']
AS_HOPE = ['C', 'G/B', 'Am', 'F', 'C', 'G', 'Ab', 'Bb']
AS_HOPE2 = ['D', 'A/C#', 'Bm', 'G', 'D', 'A', 'Bb', 'C']
AS_DASH = ['Gb', 'F', 'E', 'Eb']


def abandon_ship():
    form = AS_PANIC + AS_HOPE + AS_DASH + AS_HOPE2 + AS_DASH + AS_PANIC
    s = Piece(152, 2515, form, level=0.0)
    ch = orchestra(s)
    hn, mo, hi_, lo, br, hn2, pc, tp, hp, bs = (ch[k] for k in ('horn', 'motor', 'hit', 'low', 'brass', 'horn2',
                                                              'piccolo', 'timpani', 'harp', 'bass'))
    n = len(form)                                   # 8 + 8 + 4 + 8 + 4 + 8 = 40 bars; 39 bars' time at 152
    s.loop_at(0)
    for b in range(0, 33, 4):                       # (none inside the speed-up: its bars are not 152's)
        s.sect(b)

    def panic(b, push=0):
        r = chord(form[b]).root
        motor16(s, mo, b, 62 + push, figure=(0, 1, 0, 2))
        for e in range(8):
            q = in_range(r, 36, 47)
            s.play(lo, b, e * .5, .45, q + (6 if e % 4 == 3 else 0), 74 + (8 if e == 0 else 0))
            s.play(tp, b, e * .5, .3, in_range(r % 6, 36, 41), 60 + (e % 2) * 10)
        swelled(s, hn2, b, 0, 3.8, [in_range(r, 60, 71), in_range(r, 60, 71) + 1], 66, e0=50, e1=120, e2=70)
        for beat in (0, 1.5):
            s.play(br, b, beat, .45, [in_range(r, 60, 71), in_range(r, 60, 71) + 6], 88 if beat == 0 else 80)
            hit(s, hi_, b, beat, 104 + push // 2)
        top = in_range(r, 84, 95)
        flurry(s, pc, b, 2, [top - (k * 3) % 13 for k in range(12)], 1 / 6, 54 + push // 2)

    for b in range(0, 8):
        panic(b)
    for b in (3, 7):
        snare_roll(s, b, 3, 1, 30, 96)
    s.crash(0, 0, 104)
    # hope: the call in major on the horn, warm strings, the harp turning, the motor in octaves, a steadier
    # timpani; the brass an octave up for the call's second half
    octaves(s, mo, range(8, 16), 58)
    s.arp(hp, range(8, 16), [0, 1, 2, 3, 2, 1, 2, 3], 0.25, 60, 84, 4, 50)
    s.bass(bs, range(8, 16), [(0, 1.9, 'r'), (2, 1.9, 'r')], 74, lo=28, hi=40)
    sung(s, hn, 8, [to_major(t) for t in CT_CALL], 90, e0=80, e1=127, e2=104)
    s.tune(br, 12, [up(to_major(t), 12) for t in CT_CALL[4:]], 78)
    for b in range(8, 16):
        s.play(tp, b, 0, .5, in_range(chord(form[b]).bass, 33, 44), 80)
        s.play(tp, b, 2, .5, in_range(chord(form[b]).bass, 33, 44), 68)
        if b % 2 == 0:
            s.play(9, b, 0, .2, DRUM['kick'], 104)
    s.crash(8, 0, 100)
    # dashed: the hope's chords falling apart, the piccolo tumbling, the timpani rolling
    for D in (16, 28):
        harder = D == 28
        for b in range(D, D + 4):
            r = chord(form[b]).root
            motor16(s, mo, b, 50 + (10 if harder else 0), figure=(0, 2, 1, 2))
            if harder:
                for e in range(8):
                    s.play(br, b, e * .5, .35, [in_range(r, 60, 71), in_range(r, 60, 71) + 6], 80 + (e % 2) * 10)
                hit(s, hi_, b, 0, 110)
            else:
                s.play(br, b, 0, 1.4, tones(chord(form[b]), 60, 72), 88)
                s.play(br, b, 2, 1.4, [q - 1 for q in tones(chord(form[b]), 60, 72)], 82)
                s.play(9, b, 0, .2, DRUM['kick'], 104)
            flurry(s, pc, b, 0, [88 + (4 if harder else 0) - k for k in range(24)], 1 / 6, 62, rise=-1)
            s.play(lo, b, 0, 3.8, in_range(r, 36, 47), 84)
            s.roll(tp, b, 0, 4, in_range(r % 6, 36, 41), 44 + (10 if harder else 0), 92 + (6 if harder else 0),
                   step=0.125)
        s.crash(D, 0, 110)
        if harder:
            s.crash(D + 2, 0, 104)
    # hope again, a step higher: the brass with the call, the horn its second half
    octaves(s, mo, range(20, 28), 60)
    s.arp(hp, range(20, 28), [0, 1, 2, 3, 2, 1, 2, 3], 0.25, 62, 86, 4, 52)
    s.bass(bs, range(20, 28), [(0, 1.9, 'r'), (2, 1.9, 'r')], 76, lo=28, hi=40)
    sung(s, br, 20, [up(to_major(t), 14) for t in CT_CALL], 92, e0=80, e1=127, e2=104)
    s.tune(hn, 24, [up(to_major(t), 2) for t in CT_CALL[4:]], 86)
    for b in range(20, 28):
        s.play(tp, b, 0, .5, in_range(chord(form[b]).bass, 33, 44), 82)
        s.play(tp, b, 2, .5, in_range(chord(form[b]).bass, 33, 44), 70)
        if b % 2 == 0:
            s.play(9, b, 0, .2, DRUM['kick'], 106)
    s.crash(20, 0, 104)
    # the panic, faster and faster
    for b in range(32, 40):
        panic(b, push=(b - 32) * 2)
    for b in (35, 39):
        snare_roll(s, b, 3, 1, 34, 104)
    s.crash(32, 0, 110)
    for k, bpm in enumerate(accelerando(152, 32, 1)):     # eight bars in seven's time: 152 up to about 196
        s.tempo(32 * 4 + k, bpm)
    s.end(n)
    return s


# --- the stings: VICTORY1..3, TRAGEDY ------------------------------------------------
# The user: the splashes' idea good but "way too happy: they need a tinge of sadness or longing"; the loss "not
# tragic enough: it needs to be immediately recognizable as some sort of short dirge".  Now on the orchestra:
# the synth brass with the tune, the horns' chords, strings, timpani, and a touch of harp or piccolo.

def victory1():
    """A flat and B flat into C: the borrowed chords, the tune's E flat rising to E at the end, the harp up"""
    s = Piece(116, 2601, ['Abmaj7 Bb6', 'Csus4 Cadd9'], level=0.0)
    ch = orchestra(s)
    s.line(ch['brass'], 0, 'G4:0:.5 C5:.5:.5 Eb5:1:1 D5:2:1 F5:3:1', 94)
    s.line(ch['brass'], 1, 'G5:0:1 E5:1:2.8', 100)
    s.play(ch['horn'], 0, 0, 1.9, ['C4', 'Eb4'], 86)
    s.play(ch['horn2'], 0, 0, 1.9, ['Ab3', 'G4'], 80)
    s.play(ch['horn'], 0, 2, 1.9, ['D4', 'F4'], 86)
    s.play(ch['horn2'], 0, 2, 1.9, ['Bb3', 'G4'], 80)
    s.play(ch['horn'], 1, 0, .9, ['F4', 'G4'], 88)
    held(s, ch['horn'], 1, 1, 2.8, 'E4', 90, e0=110, e1=127, e2=90)
    s.play(ch['horn2'], 1, 0, 3.8, ['C4', 'G4'], 84)
    s.play(ch['strings'], 0, 0, 1.9, ['Eb4', 'C5'], 74)
    s.play(ch['strings'], 0, 2, 1.9, ['F4', 'D5'], 76)
    s.play(ch['strings'], 1, 0, 3.8, ['G4', 'E5'], 80)
    s.play(ch['bass'], 0, 0, 1.9, 'Ab2', 82)
    s.play(ch['bass'], 0, 2, 1.9, 'Bb2', 82)
    s.play(ch['bass'], 1, 0, 3.8, 'C3', 86)
    s.roll(ch['timpani'], 0, 2, 2, 'G1', 40, 92, step=0.125)
    s.play(ch['timpani'], 1, 0, .5, 'C2', 100)
    sweep(s, ch['harp'], 1, {0, 4, 7, 2}, 60, 96, 60, beats=2)
    s.crash(1, 0, 96)
    return s


def victory2():
    """D major, its minor fourth (G minor with E) under the tune's B flat, home: the longing of the minor plagal,
    and the piccolo sighing down at the end"""
    s = Piece(116, 2602, ['D Gm6/D', 'D'], level=0.0)
    ch = orchestra(s)
    s.line(ch['brass'], 0, 'A4:0:.25 D5:.25:.25 F#5:.5:.5 A5:1:1 Bb5:2:1.5 A5:3.5:.5', 96)
    s.line(ch['brass'], 1, 'F#5:0:3.6', 96)
    s.play(ch['horn'], 0, 0, 2, ['D4', 'A4'], 86)
    s.play(ch['horn'], 0, 2, 2, ['G4', 'Bb4'], 86)
    held(s, ch['horn2'], 0, 0, 3.9, 'D4', 82, e0=100, e1=127, e2=110)
    s.play(ch['horn'], 1, 0, 3.6, ['F#4', 'A4'], 90)
    s.play(ch['strings'], 0, 0, 2, ['A3', 'F#4'], 76)
    s.play(ch['strings'], 0, 2, 2, ['Bb3', 'E4'], 78)
    s.play(ch['strings'], 1, 0, 3.6, ['A3', 'F#4'], 82)
    s.play(ch['bass'], 0, 0, 3.9, 'D3', 84)
    s.play(ch['bass'], 1, 0, 3.6, 'D3', 86)
    s.line(ch['timpani'], 0, 'D2:0:.5 A1:2:.5', 90)
    s.roll(ch['timpani'], 0, 3, 1, 'A1', 50, 96, step=0.125)
    s.play(ch['timpani'], 1, 0, 1, 'D2', 100)
    s.line(ch['piccolo'], 1, 'A6:1.5:.5 G6:2:.5 F#6:2.5:1', 60)
    s.crash(1, 0, 92)
    return s


def victory3():
    """E flat, F, then G minor where B flat was due (the deceptive turn), E flat, and B flat at last, on its fifth"""
    s = Piece(120, 2603, ['Eb F', 'Gm7 Ebmaj7', 'Bbadd9'], level=0.0)
    ch = orchestra(s)
    s.line(ch['brass'], 0, 'Bb4:0:.5 Eb5:.5:.5 G5:1:1 F5:2:1 A5:3:1', 94)
    s.line(ch['brass'], 1, 'Bb5:0:1.5 A5:1.5:.5 G5:2:2', 96)
    s.line(ch['brass'], 2, 'F5:0:3.6', 94)
    s.play(ch['horn'], 0, 0, 1.9, ['G4', 'Bb4'], 84)
    s.play(ch['horn'], 0, 2, 1.9, ['A4', 'C5'], 84)
    s.play(ch['horn'], 1, 0, 1.9, ['G4', 'D5'], 82)
    s.play(ch['horn'], 1, 2, 1.9, ['G4', 'D5'], 82)
    held(s, ch['horn'], 2, 0, 3.6, 'D4', 88, e0=100, e1=127, e2=104)
    s.play(ch['horn2'], 2, 0, 3.6, ['F4', 'C5'], 84)
    s.play(ch['strings'], 0, 0, 1.9, ['Eb4', 'G4'], 72)
    s.play(ch['strings'], 0, 2, 1.9, ['F4', 'A4'], 72)
    s.play(ch['strings'], 1, 0, 1.9, ['D4', 'Bb4'], 76)
    s.play(ch['strings'], 1, 2, 1.9, ['Eb4', 'G4'], 76)
    s.play(ch['strings'], 2, 0, 3.6, ['D4', 'F4'], 80)
    for b, beat, q in ((0, 0, 'Eb2'), (0, 2, 'F2'), (1, 0, 'G2'), (1, 2, 'Eb2'), (2, 0, 'Bb1')):
        s.play(ch['bass'], b, beat, 3.6 if b == 2 else 1.9, q, 84)
    s.roll(ch['timpani'], 1, 2, 2, 'F1', 40, 90, step=0.125)
    s.play(ch['timpani'], 2, 0, .6, 'Bb1', 100)
    sweep(s, ch['harp'], 2, {10, 2, 5, 0}, 58, 94, 56, beats=2.5)
    s.crash(2, 0, 90)
    return s


def tragedy():
    """a funeral march in B flat minor: the low horns, synth brass and strings in its rhythm (one, one-and-a-two,
    three, four), the timpani on the beats, a horn lamenting over it, the last chord left to ring over a soft
    roll"""
    s = Piece(52, 2604, ['Bbm', 'Gb F7', 'Bbm'], level=1.2)
    ch = orchestra(s)
    lb, hn, hn2, st, bs, tp = ch['brass'], ch['horn'], ch['horn2'], ch['strings'], ch['bass'], ch['timpani']
    march = [(0, 1), (1, .75), (1.75, .25), (2, 1), (3, 1)]
    for beat, dur in march:
        s.play(lb, 0, beat, dur * .9, ['Bb2', 'F3'], 82 if beat in (0, 2) else 70)
        s.play(hn2, 0, beat, dur * .9, ['Db3', 'F3'], 76 if beat in (0, 2) else 66)
        s.play(st, 0, beat, dur * .9, ['Bb3', 'Db4'], 62)
        s.play(bs, 0, beat, dur * .9, 'Bb1', 82)
    for beat, dur in march[:3]:
        s.play(lb, 1, beat, dur * .9, ['Gb2', 'Db3'], 80 if beat == 0 else 70)
        s.play(hn2, 1, beat, dur * .9, ['Bb2', 'Db3'], 74 if beat == 0 else 64)
        s.play(st, 1, beat, dur * .9, ['Gb3', 'Bb3'], 60)
        s.play(bs, 1, beat, dur * .9, 'Gb1', 80)
    s.play(lb, 1, 2, 1.9, ['F2', 'Eb3'], 84)
    s.play(hn2, 1, 2, 1.9, ['A2', 'C3'], 76)
    s.play(st, 1, 2, 1.9, ['A3', 'Eb4'], 64)
    s.play(bs, 1, 2, 1.9, 'F1', 84)
    s.play(lb, 2, 0, 3.8, ['Bb2', 'F3'], 72)
    s.play(hn2, 2, 0, 3.8, ['Db3', 'F3'], 68)
    s.play(st, 2, 0, 3.8, ['Bb3', 'Db4', 'F4'], 56)
    s.play(bs, 2, 0, 3.8, 'Bb1', 76)
    sung(s, hn, 0, ['F4:0:1 Gb4:1:.75 F4:1.75:.25 Db4:2:2', 'Db5:0:1.5 C5:1.5:.5 Bb4:2:1 A4:3:1', 'Bb4:0:3.6'],
         92, longest=2, e0=100, e1=127, e2=80)
    for b, beat, q in ((0, 0, 'Bb1'), (0, 2, 'Bb1'), (1, 0, 'Gb1'), (1, 2, 'F1')):
        s.play(tp, b, beat, .5, q, 92)
    s.roll(tp, 2, 0, 3.5, 'Bb1', 70, 30, step=0.125)
    return s


SONGS = {'PANIC1.MID': hull_breach, 'PANIC2.MID': systems_failing, 'PANIC3.MID': abandon_ship,
         'VICTORY1.MID': victory1, 'VICTORY2.MID': victory2, 'VICTORY3.MID': victory3, 'TRAGEDY.MID': tragedy}
