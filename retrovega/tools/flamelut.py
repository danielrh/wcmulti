#!/usr/bin/env python3
"""The engine flames' profile (flames.c's g_flame_lut): intensity 1..255 by
where a pixel is in a flame, 0 outside.

    flamelut.py  ->  the C table on stdout

Rows: s, along the flame, sixteenths from the nozzle (0) to the tip (16);
columns: r, across, sixteenths of the flame's root radius from its middle.
The flame is a teardrop, widest a third of the way along (w(s) =
sin(pi (0.22 + 0.78 s))), brightest at the nozzle on its middle, fading
along it and toward its edges:

    v = (1 - 0.7 s) (1 - (r / w)^2)^1.4 + 0.1

(tuned against the user's WC1 screenshots, 2026-10-21: a white root, a
lavender middle, a blue body, ragged violet edges).  The noise that makes
the edges ragged and the gradient shimmer is added per pixel at draw time."""
import math


def table():
    out = []
    for i in range(16):
        s = (i + 0.5) / 16
        w = math.sin(math.pi * (0.22 + 0.78 * s))
        row = []
        for j in range(16):
            r = (j + 0.5) / 16
            if r >= w:
                row.append(0)
                continue
            q = r / w
            v = (1 - 0.7 * s) * (1 - q * q) ** 1.4 + 0.1
            row.append(max(1, min(255, int(round(v * 255)))))
        out.append(row)
    return out


if __name__ == '__main__':
    print('static const unsigned char __far g_flame_lut[256] = {   /* tools/flamelut.py */')
    for row in table():
        print('    ' + ', '.join('%3d' % v for v in row) + ',')
    print('};')
