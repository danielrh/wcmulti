#!/usr/bin/env python3
"""Each ship type's engines, from Vega Strike: where the exhaust points of
its units.csv Light column are in the frame its sprites are drawn in.

    engines.py [--assets DIR]  ->  data/engines.csv

The views (shipviews.py) frame a ship's mesh about its centre, its radius
(x 1.04) the half width of the view box; an engine at mesh point p is at
(p - centre) / radius there: +x the ship's left, +y up, +z the nose, 1.0 the
box's half (lod0_px / 2 px at full size).  Its size is the light's, scaled
the same, and the glow the sprites carry is min(0.3 size, 0.09) of the half
box across (shipviews.post), so the flames are sized from that.

A ship with no exhaust in Vega Strike's data gets one engine at the middle
of its hull's tail ("of" 0 in the csv).  Run once (it reads the meshes);
rvdata.py packs data/engines.csv into each ship type's record, so the build
needs no Vega Strike assets."""
import argparse
import csv
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import shipviews  # noqa: E402

MAX_ENGINES = 4
FALLBACK_SIZE = 0.3                     # a ship with no exhaust in units.csv: one engine of this size at its tail


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--assets', default=os.path.expanduser('~/dev/vegastrike-assets'))
    ap.add_argument('--out', default=os.path.join(HERE, '..', 'data', 'engines.csv'))
    args = ap.parse_args()
    rows = list(csv.DictReader(open(os.path.join(HERE, '..', 'data', 'ships.csv'))))
    out = []
    for row in rows:
        if not row['mesh']:
            continue
        parts, lights, center, radius, _strips = shipviews.ship_mesh(row, args.assets)
        engines = []
        for pos, size in lights:
            n = (np.asarray(pos) - center) / radius
            engines.append((n, float(size) / radius))
        if not engines:
            # no exhaust in Vega Strike's data: one engine at the middle of the hull's tail
            allpos = np.concatenate([m.positions for m in parts])
            tail = (allpos[:, 2].min() - center[2]) / radius
            engines.append((np.array([0.0, 0.0, tail]), FALLBACK_SIZE))
        # the biggest first, at most MAX_ENGINES (a ship with more keeps its largest)
        engines.sort(key=lambda e: -e[1])
        kept = engines[:MAX_ENGINES]
        cells = []
        for n, s in kept:
            cells += ['%.3f' % n[0], '%.3f' % n[1], '%.3f' % n[2], '%.3f' % s]
        out.append([row['key'], str(len(kept)), str(len(lights))] + cells)
        print('%-8s %d of %d engines: %s' % (row['key'], len(kept), len(lights),
                                              ' '.join('(%.2f %.2f %.2f r%.3f)' % (n[0], n[1], n[2], s) for n, s in kept)))
    with open(args.out, 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['key', 'n', 'of'] + ['%s%d' % (c, i) for i in range(MAX_ENGINES) for c in ('x', 'y', 'z', 'size')])
        for r in out:
            w.writerow(r + [''] * (3 + 4 * MAX_ENGINES - len(r)))


if __name__ == '__main__':
    main()
