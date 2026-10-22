#!/usr/bin/env python3
"""Each ship type's hit box and vital points, for collisions (src/collide.c),
in the frame its sprites are drawn in (as tools/engines.py's engines: the
mesh about its centre, 1.0 the view box's half, +x the ship's left, +y up,
+z the nose).

    hitboxes.py [--assets DIR]  ->  data/hitboxes.csv

The box is the mesh's own, about the centre the views turn about (the
middle of its extents, so the box is symmetric): its half extent on each
axis.  The vitals are points that, inside another ship's box, make a hit
(the user's idea, 2026-10-21: the smaller ship's box and the other's
vitals, one axis-aligned compare each): the weapon mounts of Vega Strike's
units.csv (its Mounts column, the mesh's units), and the mesh's extremities
(its nose, its tail, its wingtips: the vertices furthest along +z, -z, +x,
-x), which catch a wing through another ship where the mounts and the
engines (data/engines.csv) would not.  At most VITALS, the mounts nearest
the box's edges first.

And its core: the biggest sphere about its middle that fits inside its
sprite at every view angle (the user's definition), to COVER: in each of
its views (render_all.py's, build/sprites, one livery's), the biggest disc
about the middle that is 95% opaque; the least of them, over the view's
half width.
And its silhouettes (data/shapes.dat, SHAPES.DAT in the game): the mesh
seen from above, from the side and from the front, orthographically, each
a 64 x 64 bit mask over its box (the user's pick, 2026-10-21: a three-view
hull, "useful especially for point blaster shots"): a point is in the hull
when it is in all three.  In the collisions' frame (X the ship's right, Y
up, Z the nose), the cell of a coordinate v on an axis of half extent h
being (v + h) / 2h x 64: mask 0 the top (columns X, rows Z), 1 the side
(columns Z, rows Y), 2 the front (columns X, rows Y); a row 8 bytes, the
first column the low bit.  A cell is solid when the mesh covers 40% of it.

shapes.dat: "RVSHAPE\0", u16 version, u16 ships, u16 64, u16 0; then for
each ship its type byte, a 0, and its three masks (1,536 bytes).

Run once (it reads the meshes and the views); rvdata.py packs the csv, the
build copies shapes.dat."""
import argparse
import csv
import os
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import hulls  # noqa: E402
import shipviews  # noqa: E402

VITALS = 8


def unit_mounts(assets, unit):
    """Weapon mount points (x, y, z) from units.csv's Mounts column"""
    rows = csv.reader(open(os.path.join(assets, 'units', 'units.csv'), newline='', encoding='utf-8', errors='replace'))
    header = next(rows)
    col = header.index('Mounts')
    for r in rows:
        if r and r[0] == unit:
            out = []
            for item in r[col].split('}'):
                f = item.strip('{').split(';')
                try:
                    out.append(tuple(float(v) for v in f[4:7]))
                except (ValueError, IndexError):
                    pass
            return [o for o in out if len(o) == 3]
    return []


N = 64                                  # a silhouette's cells across
SS = 4                                  # drawn this much finer, then each cell's cover taken
SOLID = 0.4


def silhouettes(parts, center, radius, half):
    """the three masks (bytes): top (X, Z), side (Z, Y), front (X, Y)"""
    from PIL import ImageDraw
    pts = [np.column_stack([-(m.positions[:, 0] - center[0]), m.positions[:, 1] - center[1],
                            m.positions[:, 2] - center[2]]) / radius for m in parts]
    out = bytearray()
    for cu, cv in ((0, 2), (2, 1), (0, 1)):
        im = Image.new('L', (N * SS, N * SS), 0)
        dr = ImageDraw.Draw(im)
        for p, m in zip(pts, parts):
            u = (p[:, cu] + half[cu]) / (2 * half[cu]) * N * SS
            v = (p[:, cv] + half[cv]) / (2 * half[cv]) * N * SS
            for t in np.asarray(m.tri_index).reshape(-1, 3):
                dr.polygon([(u[t[0]], v[t[0]]), (u[t[1]], v[t[1]]), (u[t[2]], v[t[2]])], fill=255)
        cover = np.asarray(im, dtype=np.float64).reshape(N, SS, N, SS).mean(axis=(1, 3)) / 255
        bits = cover >= SOLID
        out += np.packbits(bits, axis=1, bitorder='little').tobytes()
    return bytes(out)


COVER = 0.95                            # the core's disc must be this full in every view (the user's, 2026-10-21)


def core_of(key, sprites):
    """the core's radius (a fraction of the half box): the least, over the views, of the biggest disc about the
    middle that is COVER opaque"""
    d = os.path.join(sprites, key)
    if not os.path.isdir(d):
        return 0.0
    names = sorted(n for n in os.listdir(d) if n.endswith('.png') and n[:-4].rsplit('_', 1)[-1].isdigit())   # (views)
    livery = names[0].rsplit('_', 1)[0]
    least = None
    for n in names:
        if n.rsplit('_', 1)[0] != livery:
            continue
        a = np.asarray(Image.open(os.path.join(d, n)))[..., 3] >= 128
        h, w = a.shape
        yy, xx = np.mgrid[0:h, 0:w]
        dist = np.hypot(xx + 0.5 - w / 2, yy + 0.5 - h / 2).ravel()
        order = np.argsort(dist, kind='stable')
        full = np.cumsum(a.ravel()[order]) / np.arange(1, len(order) + 1)   # how full the disc out to each pixel is
        ok = np.nonzero(full >= COVER)[0]
        r = dist[order][ok[-1]] / (w / 2) if len(ok) else 0.0   # (the biggest disc COVER full)
        least = r if least is None else min(least, r)
    return least or 0.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--assets', default=os.path.expanduser('~/dev/vegastrike-assets'))
    ap.add_argument('--out', default=os.path.join(HERE, '..', 'data', 'hitboxes.csv'))
    ap.add_argument('--sprites', default=os.path.join(HERE, '..', 'build', 'sprites'))
    args = ap.parse_args()
    rows = list(csv.DictReader(open(os.path.join(HERE, '..', 'data', 'ships.csv'))))
    out = []
    shapes = []
    for row in rows:
        if not row['mesh']:
            continue
        parts, _lights, center, radius, _strips = shipviews.ship_mesh(row, args.assets)
        art = shipviews.table_row('art.csv', row['key']) or {}
        stretch = np.array([float(v) for v in (art.get('stretch') or '1 1 1').split()])
        allpos = np.concatenate([m.positions for m in parts])
        half = (allpos.max(0) - allpos.min(0)) / 2 / radius
        unit = (hulls.RECIPES[row['mesh'][5:]][0].split('/')[0] if row['mesh'].startswith('hull:')
                else row['vs_unit'])
        mounts = [(np.array(p) * stretch - center) / radius for p in unit_mounts(args.assets, unit)]
        # inside the box only (a mount off the mesh is not the hull's), the nearest the box's edges first
        mounts = [p for p in mounts if np.all(np.abs(p) <= half * 1.05)]
        mounts.sort(key=lambda p: -np.max(np.abs(p) / np.maximum(half, 1e-6)))
        rel = (allpos - center) / radius
        tips = [rel[rel[:, 2].argmax()], rel[rel[:, 2].argmin()], rel[rel[:, 0].argmax()], rel[rel[:, 0].argmin()]]
        vitals = tips + mounts
        kept = []
        for p in vitals:                # (the same point twice is once)
            if all(np.max(np.abs(p - q)) > 0.04 for q in kept):
                kept.append(p)
        kept = kept[:VITALS]
        cells = ['%.3f' % v for p in kept for v in p]
        core = core_of(row['key'], args.sprites)
        shapes.append((int(row['type'], 16), silhouettes(parts, center, radius, half)))
        out.append([row['key']] + ['%.3f' % v for v in half] + ['%.3f' % core, str(len(kept))] + cells)
        print('%-8s box %.2f %.2f %.2f, core %.3f, %d vitals (%d mounts)' % (row['key'], half[0], half[1], half[2],
                                                                            core, len(kept), len(mounts)))
    with open(args.out, 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['key', 'hx', 'hy', 'hz', 'core', 'n'] + ['%s%d' % (c, i) for i in range(VITALS)
                                                            for c in ('x', 'y', 'z')])
        for r in out:
            w.writerow(r + [''] * (6 + 3 * VITALS - len(r)))
    import struct
    blob = b'RVSHAPE\0' + struct.pack('<4H', 1, len(shapes), N, 0)
    for t, m in shapes:
        blob += bytes([t, 0]) + m
    open(os.path.join(HERE, '..', 'data', 'shapes.dat'), 'wb').write(blob)
    print('data/shapes.dat: %d bytes' % len(blob))


if __name__ == '__main__':
    main()
