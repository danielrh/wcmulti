#!/usr/bin/env python3
"""Mouth shapes from text: the comm faces' lip sync (no speech needed).

    lipsync.py "What'll it be, pilot?"     print the shapes, one per step

Nine mouth shapes (visemes), the order of each face's mouth frames:

    0 rest  closed, at ease          (spaces, punctuation)
    1 mbp   lips pressed             m b p
    2 fv    upper teeth on lip       f v ph
    3 th    tongue behind the teeth  th t d n l
    4 s     teeth together           s z c j x sh ch
    5 ah    wide open                a i(as in 'I')
    6 eh    open, wide               e i y k g h ee
    7 oh    round                    o ou
    8 oo    small round              u w q r oo qu wh

Digraphs are matched before single letters.  A vowel holds two steps, a
consonant one; a space is one resting step, a comma three, a full stop,
question or exclamation mark five.  Repeats merge into a longer hold.  A
step is 100 ms (two frames at 20 fps).  The engine needs only the table
SINGLE (128 bytes by ASCII) and the digraph pass.
"""
import sys

VISEMES = ('rest', 'mbp', 'fv', 'th', 's', 'ah', 'eh', 'oh', 'oo')
REST, MBP, FV, TH, S, AH, EH, OH, OO = range(9)
STEP_MS = 100

DIGRAPHS = {'th': TH, 'sh': S, 'ch': S, 'ph': FV, 'oo': OO, 'ee': EH, 'qu': OO, 'wh': OO, 'ou': OH, 'ck': EH}
SINGLE = {}
for letters, v in (('a', AH), ('i', EH), ('e', EH), ('y', EH), ('o', OH), ('u', OO),
                   ('bmp', MBP), ('fv', FV), ('tdnl', TH), ('szcjx', S), ('kgh', EH), ('rwq', OO)):
    for ch in letters:
        SINGLE[ch] = v
VOWELS = set('aeiouy')
PAUSE = {' ': 1, ',': 3, ';': 3, ':': 3, '-': 2, '.': 5, '!': 5, '?': 5}


def visemes(text):
    """[(viseme, steps)] for a line of text."""
    t = text.lower()
    out = []
    i = 0
    while i < len(t):
        ch = t[i]
        if ch in PAUSE:
            if out and out[-1][0] == REST:
                out[-1] = (REST, out[-1][1] + PAUSE[ch])
            else:
                out.append((REST, PAUSE[ch]))
            i += 1
            continue
        pair = t[i:i + 2]
        if pair in DIGRAPHS:
            v, steps = DIGRAPHS[pair], (2 if pair[0] in VOWELS else 1)
            i += 2
        elif ch in SINGLE:
            v, steps = SINGLE[ch], (2 if ch in VOWELS else 1)
            i += 1
        else:
            i += 1          # apostrophes, digits and the like make no shape of their own
            continue
        if out and out[-1][0] == v:
            out[-1] = (v, out[-1][1] + steps)
        else:
            out.append((v, steps))
    out.append((REST, 1))
    return out


def frames(text, frames_per_step=2):
    """The mouth shape of every frame."""
    seq = []
    for v, steps in visemes(text):
        seq += [v] * (steps * frames_per_step)
    return seq


def blinks(n_frames, fps=20, seed=1):
    """Eye state per frame (0 open, 1 half, 2 closed): a blink every 2.5-5 s."""
    import random
    rnd = random.Random(seed)
    eyes = [0] * n_frames
    t = rnd.randint(int(1.0 * fps), int(3.0 * fps))
    while t < n_frames:
        for k, s in enumerate((1, 2, 2, 1)):
            if t + k < n_frames:
                eyes[t + k] = s
        t += rnd.randint(int(2.5 * fps), int(5.0 * fps))
    return eyes


def main():
    for v, steps in visemes(' '.join(sys.argv[1:])):
        print('%-4s %s' % (VISEMES[v], '#' * steps))


if __name__ == '__main__':
    main()
