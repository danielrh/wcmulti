"""The flight's panics and stings for the third draft's orchestra (CC0, ours): tools/orch.py's parts.

    PANIC1   "Hull Breach"      C, 140: the fight's drive (a sixteenth motor on a C pedal, the low strings,
                                timpani) with the damage over it: a low horn cluster, alarms in seconds high up
                                on the brass and the piccolo, the fight's call a tritone off
    PANIC2   "Systems Failing"  140: the three battles' blocks quoted in turn, two-bar bursts between them (the
                                half-step climb, wild tritone bars, the power failing, the alarm)
    PANIC3   "Abandon Ship"     140 into 200: shorter quotes and wilder bursts (panic in tritones, hope dashed,
                                dashed harder, a cluster, the call a tritone off), speeding up into the loop
    VICTORY1..3                 the splashes, bittersweet: A flat and B flat into C; D's minor plagal; B flat's
                                deceptive turn, ending open
    TRAGEDY  "Wingman Lost"     a funeral march in B flat minor at 52

The user (2026-10-24, the third draft): "fix all the flight music to have that same style [Outbound's] but
their own unique character and be remixable and loopable".  The panics come in on the bar over the battles
(all three at the battles' 140 in C; PANIC2 and PANIC3 quote them); each block starts on its downbeat with the
motor and leaves nothing hanging over the next ("sect" markers at every block).  Their drums are hits,
rolls and toms, no kit's beat.  The stings play once, over the battle, which then goes on where it was.
"""
from compose import chord
from orch import (NAMES, DRUM, Piece, hits, up, scale_run, ORCH, ACC332, orchestra, in_range, octaves, pulses,
                  pedal332, hit, held, sung, sweep, MAJOR, LYDIAN, excerpt, restore)
import battles
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


# --- PANIC2 and PANIC3: the fight goes on, breaking up --------------------------------------------------------
# The user (2026-10-24), on the first Systems Failing and Abandon Ship: "a bit too repetitive with themselves...
# somehow you get stuck in this loop... maybe it can cut in pieces of the battle various musics and just add little
# spurts of panic to an otherwise recognizable song... interspersed with those tritones or wild bars before going
# back to the fight music", and then: bake it into the songs.  So each is a run through the three battles' own
# blocks, quoted note for note (orch.excerpt: Contact's call and answer, Break and Attack's riff and its
# Neapolitan horn, Furball's riff and its surprise), with two-bar bursts of panic between them, written with the
# first versions' techniques.  Both at the battles' 140 in C: a panic comes in from the fight on the bar.


def nm(q):
    return '%s%d' % (NAMES[q % 12], q // 12 - 1)


_QUOTED = {}


def quoted(name):
    """the battle pieces, made once (the panics quote them)"""
    if name not in _QUOTED:
        make = {'contact': battles.contact, 'attack': battles.break_and_attack, 'furball': battles.furball}[name]
        _QUOTED[name] = make()
    return _QUOTED[name]


def panic_song(bpm, seed, plan, bursts, sects_until=None):
    """plan: ('fight', battle, its first bar, bars) or (burst, argument): the form, the bursts' chords from
    bursts[burst][0](argument), drawn by bursts[burst][1](s, parts, bar, argument)"""
    form = []
    for step in plan:
        if step[0] == 'fight':
            form += ['Cm'] * step[3]
        else:
            form += bursts[step[0]][0](step[1])
    s = Piece(bpm, seed, form, level=0.0)
    parts = orchestra(s)
    s.loop_at(0)
    b = 0
    for step in plan:
        if sects_until is None or b < sects_until:
            s.sect(b)
        if step[0] == 'fight':
            excerpt(s, quoted(step[1]), step[2], step[3], b)
            b += step[3]
        else:
            restore(s, b)
            bursts[step[0]][1](s, parts, b, step[1])
            b += 2
    s.end(len(form))
    return s


# Systems Failing's bursts (the hull under 35%)

def sf_climb(s, c, b0, r0):
    """the climb: the bass up a half step every two beats, the strings' cluster swelling, the horns' alarm (the
    fifth, a half step up, a tritone on), the timpani and a snare roll into the fight's next downbeat"""
    for k, b in enumerate((b0, b0 + 1)):
        motor16(s, c['motor'], b, 60 + 6 * k)
        low8(s, c['low'], b, 74 + 4 * k, figure=(0, 0, 12, 0, 0, 12, 0, 12))
        for half in (0, 2):
            r = chord(s.sym_at(b, half)).root
            q = in_range(r, 60, 71)
            swelled(s, c['strings'], b, half, 1.9, [q, q + 1], 56 + 6 * k, e0=60 + 20 * k, e1=118, e2=96)
            s.play(c['timpani'], b, half, .5, in_range(r, 33, 44), 84 + 4 * k)
            s.play(9, b, half, .2, DRUM['kick'], 112)
            if not (k == 1 and half == 2):
                x = in_range(r + 7, 55, 66)
                s.line(c['horn'], b, '%s:%g:.75 %s:%g:.25 %s:%g:.9' % (nm(x), half, nm(x + 1), half + .75, nm(x + 6),
                                                                       half + 1), 84)
        q = in_range(r0 + 2 * k, 72, 83)
        s.play(c['brass'], b, 1.5, .4, [q, q + 1], 78)
    r = chord(s.sym_at(b0 + 1, 2)).root
    s.roll(c['timpani'], b0 + 1, 2, 2, in_range(r, 33, 44), 40, 108, step=0.125)
    snare_roll(s, b0 + 1, 2, 2, 40, 104)


def sf_wild(s, c, b0, r):
    """two wild bars: tritones on every eighth, the orchestra hitting the 3+3+2, a crash, the piccolo's alarm,
    the brass's run up and the toms down"""
    for k, b in enumerate((b0, b0 + 1)):
        motor16(s, c['motor'], b, 70, figure=(1, 2, 1, 0))
        for e in range(8):
            q = in_range(r, 36, 47)
            s.play(c['low'], b, e * .5, .45, q + (6 if e % 2 else 0), 86)
            s.play(c['timpani'], b, e * .5, .3, in_range(r + (6 if e % 2 else 0), 33, 44), 76 + (e % 2) * 10)
            if k == 0 or e < 4:
                s.play(c['brass'], b, e * .5, .3, [in_range(r, 60, 71), in_range(r, 60, 71) + 6], 84 + (e % 2) * 8)
        for beat in ACC332:
            hit(s, c['hit'], b, beat, 112)
    s.crash(b0, 0, 112)
    trill(s, c['piccolo'], b0, 0, 2, in_range(r, 84, 95), in_range(r, 84, 95) + 6, 60)
    run = [q for q in range(in_range(r, 64, 75), in_range(r, 64, 75) + 25) if (q - r) % 12 in DIM]
    flurry(s, c['brass'], b0 + 1, 2, run, 0.125, 76, rise=1)
    for i, t in enumerate(('tom1', 'tom1', 'tom2', 'tom2', 'tom3', 'tom3', 'tom4', 'tom4')):
        s.play(9, b0 + 1, 2 + i * .25, .2, DRUM[t], 80 + i * 3, 2)


def sf_failure(s, c, b0, _arg):
    """the power failing: the motor gone, a low cluster swelling up from nothing, a heartbeat on the timpani, the
    alarms; then the motor stuttering back, tritone stabs on the 3+3+2, the toms down"""
    swelled(s, c['strings'], b0, 0, 3.9, [48, 49], 70, e0=20, e1=124, e2=110)
    s.play(c['low'], b0, 0, 3.9, 36, 82)
    for beat in (0, 2):
        s.play(c['timpani'], b0, beat, .3, 'C2', 90)
        s.play(c['timpani'], b0, beat + .375, .3, 'C2', 72)
    trill(s, c['horn'], b0, 1, 3, 67, 68, 72)
    trill(s, c['piccolo'], b0, 3, 1, 90, 91, 56)
    b = b0 + 1
    for k in range(16):
        if k % 4 == 3 or k in (5, 6):
            continue
        q = 48 if k % 2 == 0 else (60 if k % 4 == 1 else 54)
        s.play(c['motor'], b, k * .25, .2, q, 66 + (12 if k in (0, 8, 12) else 0), 3)
    for beat in ACC332:
        s.play(c['brass'], b, beat, .35, [60, 66], 90)
        hit(s, c['hit'], b, beat, 110)
    for i, t in enumerate(('tom1', 'tom2', 'tom3', 'tom4')):
        s.play(9, b, 3.25 + i * .1875, .15, DRUM[t], 86 + i * 4, 2)


def sf_alarm(s, c, b0, _arg):
    """the alarm: the motor on a C pedal under diminished chords, the brass and the piccolo trilling in seconds,
    the hits, the timpani and a snare roll into the fight's next downbeat"""
    for k, b in enumerate((b0, b0 + 1)):
        motor16(s, c['motor'], b, 64 + 6 * k, pedal=0)
        low8(s, c['low'], b, 80, pedal=0)
        trill(s, c['brass'], b, 0, 2 if k == 0 else 1.5, 72, 73, 76)
        trill(s, c['piccolo'], b, 2 if k == 0 else 0, 2, 90, 91, 58)
        s.play(c['timpani'], b, 0, .5, 'C2', 90)
        hit(s, c['hit'], b, 0, 110)
    hit(s, c['hit'], b0 + 1, 1.5, 104)
    s.crash(b0, 0, 104)
    s.roll(c['timpani'], b0 + 1, 2, 2, 'G1', 40, 110, step=.125)
    snare_roll(s, b0 + 1, 2, 2, 40, 106)


SF_BURSTS = {'climb': (lambda r0: ['%sm %sm' % (NAMES[r0 % 12], NAMES[(r0 + 1) % 12]),
                                   '%sm %sdim' % (NAMES[(r0 + 2) % 12], NAMES[(r0 + 3) % 12])], sf_climb),
             'wild': (lambda r: [NAMES[r % 12] + 'dim'] * 2, sf_wild),
             'failure': (lambda _a: ['Cm', 'Cm'], sf_failure),
             'alarm': (lambda _a: ['Cdim7', 'Cdim7'], sf_alarm)}
SF_PLAN = [('fight', 'contact', 2, 4), ('climb', 0), ('fight', 'attack', 1, 4), ('wild', 0),
           ('fight', 'furball', 2, 4), ('failure', 0), ('fight', 'contact', 10, 4), ('climb', 3),
           ('fight', 'attack', 17, 4), ('wild', 6), ('fight', 'furball', 22, 4), ('alarm', 0)]


def systems_failing():
    return panic_song(BATTLE_BPM, 2414, SF_PLAN, SF_BURSTS)          # 36 bars, 61.7 s


# Abandon Ship's bursts (the hull under 15%): shorter quotes, wilder bursts, and the speed-up into the loop

def as_panic(s, c, b0, push=0):
    """panic in tritones: the motor, the low strings' eighths with the tritone in them, the horns' cluster
    swelling, the brass's tritone stabs with the hits, the piccolo falling"""
    for b in (b0, b0 + 1):
        r = chord(s.sym_at(b, 0)).root
        motor16(s, c['motor'], b, 62 + push, figure=(0, 1, 0, 2))
        for e in range(8):
            q = in_range(r, 36, 47)
            s.play(c['low'], b, e * .5, .45, q + (6 if e % 4 == 3 else 0), 76 + (8 if e == 0 else 0))
            s.play(c['timpani'], b, e * .5, .3, in_range(r + (6 if e % 2 else 0), 33, 44), 62 + (e % 2) * 10)
        swelled(s, c['horn2'], b, 0, 3.8, [in_range(r, 60, 71), in_range(r, 60, 71) + 1], 66, e0=50, e1=120, e2=70)
        for beat in (0, 1.5):
            s.play(c['brass'], b, beat, .45, [in_range(r, 60, 71), in_range(r, 60, 71) + 6], 88 if beat == 0 else 80)
            hit(s, c['hit'], b, beat, 104 + push // 2)
        top = in_range(r, 84, 95)
        flurry(s, c['piccolo'], b, 2, [top - (k * 3) % 13 for k in range(12)], 1 / 6, 54 + push // 2)
    s.crash(b0, 0, 106)
    snare_roll(s, b0 + 1, 3, 1, 30, 96)


def as_hope(s, c, b0, _arg):
    """hope dashed: a bar of C major (the call's leap on the horn, warm strings, the harp), then its chords
    falling apart a half step a beat, the piccolo tumbling, the timpani rolling"""
    octaves(s, c['motor'], range(b0, b0 + 1), 58)
    s.pad(c['strings'], range(b0, b0 + 1), 55, 72, 3, 66)
    s.arp(c['harp'], range(b0, b0 + 1), [0, 1, 2, 3, 2, 1, 2, 3], 0.25, 60, 84, 4, 50)
    sung(s, c['horn'], b0, ['G4:0:1.5 C5:1.5:.5 E5:2:1.9'], 90, e0=80, e1=127, e2=104)
    s.play(c['timpani'], b0, 0, .5, 'C2', 80)
    s.play(9, b0, 0, .2, DRUM['kick'], 104)
    s.crash(b0, 0, 96)
    b = b0 + 1
    motor16(s, c['motor'], b, 56, figure=(0, 2, 1, 2))
    for beat in range(4):
        h = chord(s.sym_at(b, beat))
        s.play(c['brass'], b, beat, .9, tones(h, 60, 72), 90 - 2 * beat)
        s.play(c['low'], b, beat, .9, in_range(h.root, 36, 47), 86)
    flurry(s, c['piccolo'], b, 0, [88 - k for k in range(24)], 1 / 6, 62, rise=-1)
    s.roll(c['timpani'], b, 0, 4, 'C2', 44, 100, step=0.125)
    hit(s, c['hit'], b, 0, 108)


def as_harder(s, c, b0, _arg):
    """dashed harder: tritones on every eighth on the brass, the hits, a crash a bar, the piccolo falling, the
    timpani rolling"""
    for k, b in enumerate((b0, b0 + 1)):
        r = chord(s.sym_at(b, 0)).root
        motor16(s, c['motor'], b, 64, figure=(0, 2, 1, 2))
        for e in range(8):
            s.play(c['brass'], b, e * .5, .35, [in_range(r, 60, 71), in_range(r, 60, 71) + 6], 66 + (e % 2) * 8)
        hit(s, c['hit'], b, 0, 102)
        hit(s, c['hit'], b, 1.5, 94)
        flurry(s, c['piccolo'], b, 0, [92 - k for k in range(24)], 1 / 6, 54, rise=-1)
        s.play(c['low'], b, 0, 3.8, in_range(r, 36, 47), 80)
        s.roll(c['timpani'], b, 0, 4, in_range(r, 33, 44), 40, 88, step=0.125)
        s.crash(b, 0, 98 if k == 0 else 90)


def as_cluster(s, c, b0, _arg):
    """a cluster (C, D flat, F sharp, G) swelling on the brass and the strings over a timpani roll, then struck
    on the beats with the hits, the toms down"""
    cl = [60, 61, 66, 67]
    swelled(s, c['brass'], b0, 0, 3.8, cl, 80, e0=30, e1=127, e2=120)
    swelled(s, c['strings'], b0, 0, 3.8, [q - 12 for q in cl], 70, e0=30, e1=127, e2=120)
    motor16(s, c['motor'], b0, 60, pedal=0)
    s.roll(c['timpani'], b0, 0, 4, 'C2', 30, 110, step=0.125)
    snare_roll(s, b0, 2, 2, 30, 100)
    b = b0 + 1
    motor16(s, c['motor'], b, 70, pedal=0, figure=(1, 2, 1, 0))
    for beat in range(3):
        s.play(c['brass'], b, beat, .35, cl, 96)
        s.play(c['strings'], b, beat, .35, [q - 12 for q in cl], 84)
        hit(s, c['hit'], b, beat, 112)
    s.crash(b, 0, 110)
    for i, t in enumerate(('tom1', 'tom2', 'tom3', 'tom4')):
        s.play(9, b, 3 + i * .25, .2, DRUM[t], 88 + i * 3, 2)


def as_call_off(s, c, b0, _arg):
    """the fight's call a tritone off on the brass, the horn an octave under, over the motor's pedal C"""
    call = [up(t, 6) for t in CT_CALL[:2]]
    s.tune(c['brass'], b0, call, 92)
    s.tune(c['horn'], b0, [up(t, -12) for t in call], 80)
    for b in (b0, b0 + 1):
        motor16(s, c['motor'], b, 64, pedal=0)
        low8(s, c['low'], b, 82, pedal=0)
        for beat in (0, 1.5):
            hit(s, c['hit'], b, beat, 108)
        s.play(c['timpani'], b, 0, .5, 'C2', 86)
        s.play(c['timpani'], b, 2, .5, 'F#2', 80)
    snare_roll(s, b0 + 1, 3, 1, 36, 100)


def as_speedup(s, c, b0, _arg):
    """the panic again, pushed, a crash a bar and a long snare roll into the loop"""
    as_panic(s, c, b0, push=12)
    s.crash(b0 + 1, 0, 110)
    snare_roll(s, b0 + 1, 2, 2, 40, 110)


AS_BURSTS = {'panic': (lambda _a: ['Cm', 'Gb'], lambda s, c, b, _a: as_panic(s, c, b)),
             'hope': (lambda _a: ['C', 'Gb F E Eb'], as_hope),
             'harder': (lambda _a: ['Gb', 'Eb'], as_harder),
             'cluster': (lambda _a: ['C', 'C'], as_cluster),
             'calloff': (lambda _a: ['Gb/C', 'Gb/C'], as_call_off),
             'speedup': (lambda _a: ['Cm', 'Gb'], as_speedup)}
AS_PLAN = [('panic', 0), ('fight', 'contact', 2, 4), ('hope', 0), ('fight', 'attack', 33, 2), ('harder', 0),
           ('fight', 'furball', 18, 4), ('cluster', 0), ('fight', 'contact', 30, 4), ('calloff', 0),
           ('fight', 'attack', 9, 4), ('panic', 0), ('fight', 'furball', 34, 4), ('speedup', 0)]


def abandon_ship():
    s = panic_song(BATTLE_BPM, 2515, AS_PLAN, AS_BURSTS, sects_until=30)  # 36 bars; the last six faster
    for k, bpm in enumerate(accelerando(BATTLE_BPM, 24, 1)):     # six bars in five's time: 140 up to about 200
        s.tempo(30 * 4 + k, bpm)
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
