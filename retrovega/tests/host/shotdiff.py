#!/usr/bin/env python3
"""shotdiff.py: the game's screenshots (NAME.BMP: 640 x 400, the palette shown) from two runs compared pixel by
pixel, as the eye sees them (each pixel's colour through its own file's palette): RV.EXE in DOSBox against the
native build, the same test script driving both.

    tests/host/shotdiff.py DIR_A DIR_B [--out DIR]   every NAME.BMP in both; --out: NAME-diff.png sheets (A, B and
                                                     the differing pixels in red) for the shots that differ
Prints a line a shot (the differing pixels and their box) and "SHOTS SAME" when all agree."""
import os
import sys

from PIL import Image, ImageChops


def load(path):
    return Image.open(path).convert('RGB')


def main():
    args = sys.argv[1:]
    out = None
    if '--out' in args:
        i = args.index('--out')
        out = args[i + 1]
        del args[i:i + 2]
    if len(args) != 2:
        print(__doc__)
        return 2
    a_dir, b_dir = args
    names = sorted(n for n in os.listdir(a_dir) if n.upper().endswith('.BMP') and os.path.exists(os.path.join(b_dir, n)))
    if not names:
        print('no screenshots in both')
        return 1
    bad = 0
    for n in names:
        a, b = load(os.path.join(a_dir, n)), load(os.path.join(b_dir, n))
        diff = ImageChops.difference(a, b).convert('L').point(lambda v: 255 if v else 0)
        box = diff.getbbox()
        count = sum(1 for v in diff.getdata() if v)
        print('%-14s %s' % (n, 'same' if not count else '%d pixels differ, in %s' % (count, box)))
        if count:
            bad += 1
            if out:
                os.makedirs(out, exist_ok=True)
                sheet = Image.new('RGB', (a.width * 3, a.height))
                sheet.paste(a, (0, 0))
                sheet.paste(b, (a.width, 0))
                red = Image.new('RGB', a.size, (255, 0, 0))
                sheet.paste(Image.composite(red, a.point(lambda v: v // 3), diff), (2 * a.width, 0))
                sheet.save(os.path.join(out, n[:-4] + '-diff.png'))
    print('SHOTS SAME' if not bad else 'SHOTS DIFFER: %d of %d' % (bad, len(names)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
