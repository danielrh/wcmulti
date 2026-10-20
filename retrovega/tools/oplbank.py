#!/usr/bin/env python3
"""Our OPL3 instrument bank (CC0, ours): RVOPL.BNK for src/opl.c.

    oplbank.py [--out build/dos/RVOPL.BNK] [--list]

Every General MIDI program and every key of channel 10 that a song may use
names one of the patches below; programs nobody wrote a patch for fall to
their family's (the pianos to the electric piano, the reeds to the
clarinet, ...).  A patch is one or two layers, each a two-operator voice:

    op(mult, tl, ar, dr, sl, rr, ws, ksl, egt, ksr, am, vib)  one operator:
        mult  frequency multiple (0 = half; 11 = 10, 13 = 12, 14 = 15 as the chip has it)
        tl    attenuation, 0.75 dB steps (0..63); the carrier's is the patch's loudness,
              the modulator's (in FM) its brightness
        ar dr sl rr   attack, decay, sustain level (3 dB steps), release (0..15)
        ws    waveform (0 sine, 1 half, 2 abs, 3 quarter, 4 alt, 5 camel, 6 square, 7 log-saw)
        ksl   level drop with pitch (0 none, 1 3 dB, 2 1.5 dB, 3 6 dB an octave)
        egt   1: holds at the sustain level while the key is down; 0: keeps decaying
        ksr   envelope faster with pitch; am, vib: the chip's tremolo and vibrato
    layer(mod, car, fb, additive, transpose, detune, vsense, pan)
        fb       the modulator's feedback (0..7)
        additive both operators heard (organ) instead of FM
        detune   F-number steps at 512 (about 3.4 cents each)
        vsense   modulator attenuation at velocity 0: soft notes mellower
        pan      0 the channel's side, 1 left, 2 right (a wide pad: two layers, two sides)

How long a layer is heard (while held, and after the key goes up) comes from
the carrier's envelope by the chip's own rate table; the driver uses it to
reuse voices whose sound has died away.
"""
import argparse
import os
import struct

# the chip's decay time from 0 to -96 dB for each rate (ms; YM3812 / YMF262 manuals)
DECAY_MS = [0, 39280.64, 19640.32, 9820.16, 4910.08, 2455.04, 1227.52, 613.76, 306.88, 153.44, 76.72, 38.36,
            19.20, 9.60, 4.80, 2.40]
HEARD_DB = 40                       # a voice under -40 dB is as good as quiet


def op(mult=1, tl=0, ar=15, dr=0, sl=0, rr=7, ws=0, ksl=0, egt=1, ksr=0, am=0, vib=0):
    return dict(mult=mult, tl=tl, ar=ar, dr=dr, sl=sl, rr=rr, ws=ws, ksl=ksl, egt=egt, ksr=ksr, am=am, vib=vib)


def layer(mod, car, fb=0, additive=False, transpose=0, detune=0, vsense=0, pan=0):
    return dict(mod=mod, car=car, fb=fb, additive=additive, transpose=transpose, detune=detune, vsense=vsense, pan=pan)


def regs(o):
    ksl = {0: 0, 1: 2, 2: 1, 3: 3}[o['ksl']]           # the chip's bit order: 1 = 3 dB, 2 = 1.5 dB
    return bytes([(o['am'] << 7) | (o['vib'] << 6) | (o['egt'] << 5) | (o['ksr'] << 4) | o['mult'],
                  (ksl << 6) | o['tl'],
                  (o['ar'] << 4) | o['dr'],
                  (o['sl'] << 4) | o['rr'],
                  o['ws']])


def heard(o):
    """(while held, after the key goes up) in 1/20 s; 0 while held = as long as it is held"""
    left = max(0.0, HEARD_DB - o['tl'] * 0.75)          # dB above the threshold at the start
    t = lambda rate, db: db / 96 * DECAY_MS[rate] if rate else None
    sl = o['sl'] * 3
    if sl >= left:                                      # gone before the sustain level
        held = t(o['dr'], left)
    elif o['egt']:
        held = None                                     # holds at the sustain level
    else:                                               # down to the sustain level, then on at the release rate
        a, b = t(o['dr'], sl), t(o['rr'], left - sl)
        held = None if a is None or b is None else a + b
    rel = t(o['rr'], left)
    q = lambda ms: max(1, min(255, int(ms * 1.25 / 50) + 1))
    return (0 if held is None else q(held)), (255 if rel is None else q(rel))


def pack_layer(l):
    mod, car = l['mod'], l['car']
    life, rel = heard(car)
    if l['additive']:
        ml, mr = heard(mod)
        life = 0 if 0 in (life, ml) else max(life, ml)
        rel = max(rel, mr)
    return (regs(mod) + regs(car) + bytes([(l['fb'] << 1) | int(l['additive'])])
            + struct.pack('<bbBBBB3x', l['transpose'], l['detune'], l['vsense'], l['pan'], life, rel))


# --- the patches ---

P = {}
PROGRAMS = {}       # GM program -> patch name
DRUMS = {}          # key -> patch name


def patch(name, layers, programs=(), drums=(), note=0):
    P[name] = (layers, note)
    for p in programs:
        PROGRAMS[p] = name
    for k in drums:
        DRUMS[k] = name


# The default: a soft rounded lead, for anything with no better patch.
patch('soft lead', [layer(op(1, 38, 12, 4, 4, 6, ksl=2), op(1, 2, 12, 2, 2, 6, vib=1), fb=2, vsense=10)])

# Keys.  The electric piano is a soft FM body with a short metallic tine over
# it (the tine dies within half a second, and its voice is free again); the
# second is the body alone, warmer, for comping.  Both have the suitcase's
# slow tremolo.
EP_BODY = layer(op(1, 30, 15, 5, 5, 6, ksl=2, ksr=1), op(1, 0, 15, 3, 15, 6, am=1), fb=2, vsense=16)
EP_TINE = layer(op(14, 44, 15, 8, 15, 8, egt=0), op(1, 15, 15, 6, 15, 8, egt=0, ksr=1), fb=0, vsense=24)
patch('electric piano', [EP_BODY, EP_TINE], programs=[4, 6, 7])
patch('mellow piano', [layer(op(1, 34, 15, 4, 6, 6, ksl=2, ksr=1), op(1, 0, 15, 3, 15, 6, am=1), fb=1, vsense=18)],
      programs=[5])
patch('piano', [layer(op(1, 28, 15, 4, 7, 7, ksl=2, ksr=1), op(1, 0, 15, 4, 15, 7, ksr=1), fb=3, vsense=16),
                layer(op(1, 46, 15, 6, 15, 7, egt=0), op(2, 18, 15, 5, 15, 7, egt=0, ksr=1), vsense=20)],
      programs=[0, 1, 2, 3])
patch('harp', [layer(op(1, 34, 15, 7, 12, 6, egt=0), op(1, 0, 15, 4, 15, 5, egt=0, ksr=1), fb=3, vsense=18)],
      programs=[46, 45, 104, 105, 106, 107, 108])
patch('organ', [layer(op(2, 12, 14, 0, 0, 8), op(1, 4, 14, 0, 0, 8), additive=True)], programs=list(range(16, 24)))

# Mallets: vibraphone (its motor's tremolo), celesta and music box (an octave up, bell-like)
patch('vibraphone', [layer(op(4, 42, 15, 5, 15, 6, egt=0), op(1, 0, 15, 3, 15, 5, egt=0, ksr=1, am=1), vsense=20)],
      programs=[11, 12, 13, 14, 15, 112, 113, 114])
patch('celesta', [layer(op(7, 40, 15, 6, 15, 6, egt=0), op(2, 4, 15, 4, 15, 5, egt=0, ksr=1), vsense=22)],
      programs=[8, 9, 10])

# Pads and strings.  The warm pad is two layers a few cents apart, one left
# and one right; its brightness rises after the note (the modulator's slower
# attack).  The soft pad is one layer, with the chip's vibrato.
PAD_L = layer(op(1, 34, 3, 0, 0, 5), op(1, 4, 4, 0, 0, 5, ksl=1), fb=5, detune=-2, pan=1)
PAD_R = layer(op(1, 34, 3, 0, 0, 5), op(1, 4, 4, 0, 0, 5, ksl=1, vib=1), fb=5, detune=2, pan=2)
patch('warm pad', [PAD_L, PAD_R], programs=[89, 88, 92, 95])
patch('soft pad', [layer(op(1, 36, 3, 0, 0, 5), op(1, 4, 4, 0, 0, 5, ksl=1, vib=1), fb=4)],
      programs=[90, 91, 93, 94, 52, 53, 54, 96, 97, 98, 99, 100, 101, 102, 103] + list(range(120, 128)))
STR_L = layer(op(1, 29, 5, 0, 0, 6), op(1, 4, 5, 0, 0, 6, ksl=1, vib=1), fb=5, detune=-2, pan=1)
STR_R = layer(op(1, 29, 5, 0, 0, 6), op(1, 4, 5, 0, 0, 6, ksl=1, vib=1), fb=5, detune=2, pan=2)
patch('strings', [STR_L, STR_R], programs=[48, 49, 40, 41, 42, 43, 44, 50, 51, 55, 110])
patch('pizzicato', [layer(op(1, 30, 15, 7, 15, 6, egt=0), op(1, 0, 15, 6, 15, 6, egt=0, ksr=1), fb=4, vsense=12)],
      programs=[45])

# Basses: fingered (round, a little bite at the start), upright (a thump, then
# a bloom that dies away), synth (fuller, held).
patch('fingered bass', [layer(op(1, 26, 15, 6, 8, 8), op(1, 0, 15, 3, 4, 8), fb=4, vsense=14)],
      programs=[33, 34, 35, 36, 37])
patch('upright bass', [layer(op(1, 30, 15, 7, 10, 7, egt=0), op(1, 0, 15, 3, 15, 6, egt=0), fb=3, vsense=12)],
      programs=[32, 43])
patch('synth bass', [layer(op(1, 18, 15, 4, 6, 8), op(1, 0, 15, 2, 2, 8), fb=6, vsense=10)], programs=[38, 39, 87])

# Guitars
patch('nylon guitar', [layer(op(1, 34, 15, 6, 15, 6, egt=0), op(1, 0, 15, 4, 15, 6, egt=0, ksr=1), fb=3, vsense=16)],
      programs=[24, 25, 31])
patch('jazz guitar', [layer(op(1, 32, 15, 4, 8, 7, ksl=2), op(1, 0, 15, 3, 15, 7, ksr=1), fb=2, vsense=18)],
      programs=[26, 27, 28])
patch('overdrive guitar', [layer(op(1, 18, 15, 1, 2, 7), op(1, 0, 15, 2, 2, 7), fb=6, vsense=6)], programs=[29, 30])

# Winds: flute (a breath of feedback noise under a sine), clarinet (odd
# harmonics: carrier 1, modulator 2), oboe (half-sine carrier: nasal), horn
# (round, slow to brighten), brass and trumpet (brighter, the brass "blat").
patch('flute', [layer(op(1, 46, 8, 3, 6, 6, egt=1), op(1, 2, 7, 0, 0, 6, vib=1), fb=7, vsense=6)],
      programs=list(range(72, 80)))
patch('clarinet', [layer(op(2, 30, 7, 2, 3, 6), op(1, 2, 7, 0, 0, 6, vib=1), fb=2, vsense=10)],
      programs=[71, 64, 65, 66, 67])
patch('oboe', [layer(op(1, 26, 8, 2, 3, 6), op(1, 4, 8, 0, 0, 6, ws=1, vib=1), fb=6, vsense=10)], programs=[68, 69, 70])
patch('horn', [layer(op(1, 30, 5, 2, 2, 6, ksl=1), op(1, 2, 6, 0, 0, 6, vib=1), fb=3, vsense=12)], programs=[60, 58])
patch('brass', [layer(op(1, 22, 6, 3, 3, 6), op(1, 0, 7, 0, 0, 6, vib=1), fb=5, vsense=12)], programs=[61, 57, 62, 63])
patch('trumpet', [layer(op(1, 20, 9, 2, 2, 6), op(1, 0, 9, 0, 0, 6, vib=1), fb=6, vsense=12)], programs=[56])
patch('muted trumpet', [layer(op(2, 30, 9, 3, 4, 6), op(1, 2, 9, 0, 0, 6, vib=1), fb=4, vsense=10)], programs=[59])
patch('synth lead', [layer(op(1, 24, 13, 2, 2, 6), op(1, 0, 13, 0, 0, 6, vib=1), fb=5, vsense=8)],
      programs=list(range(80, 87)))
patch('timpani', [layer(op(1, 34, 15, 6, 15, 6, egt=0), op(1, 0, 15, 3, 15, 4, egt=0), fb=2, vsense=12)],
      programs=[47, 115, 116, 117, 118, 119])
for fam in (range(109, 112),):
    for p in fam:
        PROGRAMS.setdefault(p, 'oboe')

# Drums (channel 10).  Noise is a modulator at full feedback; its carrier's
# pitch is the noise's colour.
patch('kick', [layer(op(1, 12, 15, 11, 15, 11, egt=0), op(1, 0, 15, 6, 15, 7, egt=0), fb=3)], drums=[35, 36], note=36)
patch('snare', [layer(op(15, 0, 15, 0, 0, 8), op(1, 4, 15, 7, 15, 7, egt=0), fb=7, pan=0),
                layer(op(1, 20, 15, 10, 15, 10, egt=0), op(1, 6, 15, 8, 15, 8, egt=0), fb=2)],
      drums=[38, 40], note=72)
patch('side stick', [layer(op(3, 24, 15, 12, 15, 12, egt=0), op(2, 2, 15, 10, 15, 10, egt=0), fb=5)],
      drums=[37, 39], note=74)
patch('closed hat', [layer(op(15, 0, 15, 0, 0, 11), op(8, 4, 15, 10, 15, 10, egt=0), fb=7)], drums=[42, 44], note=84)
patch('open hat', [layer(op(15, 0, 15, 0, 0, 8), op(8, 10, 15, 8, 15, 8, egt=0), fb=7)], drums=[46], note=84)
patch('ride', [layer(op(15, 12, 15, 4, 15, 6, egt=0), op(4, 10, 15, 5, 15, 6, egt=0), fb=6)], drums=[51, 59], note=77)
patch('ride bell', [layer(op(7, 26, 15, 5, 15, 6, egt=0), op(4, 8, 15, 5, 15, 6, egt=0), fb=2)], drums=[53], note=81)
patch('crash', [layer(op(15, 0, 15, 0, 0, 5), op(6, 6, 15, 4, 15, 5, egt=0), fb=7)], drums=[49, 57, 52, 55], note=84)
patch('shaker', [layer(op(15, 0, 15, 0, 0, 10), op(12, 8, 12, 10, 15, 10, egt=0), fb=7)], drums=[69, 70, 82, 54], note=84)
patch('tom', [layer(op(1, 22, 15, 9, 15, 9, egt=0), op(1, 0, 15, 6, 15, 6, egt=0), fb=2)], drums=[41, 43, 45, 47, 48, 50])
patch('claves', [layer(op(3, 30, 15, 10, 15, 10, egt=0), op(1, 2, 15, 9, 15, 9, egt=0))], drums=[75, 76, 77], note=0)

FAMILY = [(0, 'piano'), (8, 'celesta'), (16, 'organ'), (24, 'nylon guitar'), (32, 'fingered bass'), (40, 'strings'),
          (48, 'strings'), (56, 'brass'), (64, 'clarinet'), (72, 'flute'), (80, 'synth lead'), (88, 'soft pad'),
          (96, 'soft pad'), (104, 'harp'), (112, 'vibraphone'), (120, 'soft pad')]


def build():
    names = list(P)
    index = {n: i for i, n in enumerate(names)}
    program = []
    for p in range(128):
        fam = [n for start, n in FAMILY if start <= p][-1]
        program.append(index[PROGRAMS.get(p, fam)])
    drum = [index[DRUMS[k]] if k in DRUMS else 0xFF for k in range(128)]
    out = b'RVOPL\x1A' + bytes([1, len(names)]) + bytes(program) + bytes(drum)
    for n in names:
        layers, note = P[n]
        body = b''.join(pack_layer(l) for l in layers)
        body += b'\0' * (40 - len(body))
        out += body + bytes([len(layers), note, 0, 0])
    return out, names


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build', 'dos',
                                                  'RVOPL.BNK'))
    ap.add_argument('--list', action='store_true', help='each patch: its layers and how long they are heard')
    args = ap.parse_args()
    data, names = build()
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, 'wb') as f:
        f.write(data)
    if args.list:
        for i, n in enumerate(names):
            layers, note = P[n]
            heard_s = ['%.2f/%.2f s' % tuple(x / 20 for x in heard(l['car'])) for l in layers]
            print('%2d %-16s %d layer(s) %s%s' % (i, n, len(layers), ', '.join(heard_s), ' key %d' % note if note else ''))
    print('%d patches, %d bytes in %s' % (len(names), len(data), args.out))


if __name__ == '__main__':
    main()
