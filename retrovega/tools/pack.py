#!/usr/bin/env python3
"""Quantise the rendered roster to the flight palette and pack it, WC-style.

    pack.py [--sprites build/sprites] [--out build/rvdat] [--preview docs/packed.png]

Each view (render_all.py) is mapped to palette.json (palette.py):

    hull pixels     the nearest hull-block or engine-glow colour (no dithering)
    painted pixels  their livery ramp (64-71 primary, 72-79 secondary) by how lit
                    they are, so any livery is a remap of those 16 indices
    glass pixels    the glass ramp (80-87) by brightness

and encoded with WC's run-length sprite code (scripts/wc2data.py
encode_sprite), origin at the view's centre.  A ship becomes OUT/SHIPnn.RV
(nn its type in hex): a WC container whose block 0 is the table of its views
(37, or 62 for an asymmetric ship) and block 1 its record:

    'RVSH', type byte, view count word, flags byte (1 asymmetric),
    default livery index byte, lod0 px word, the lattice's bands and
    headings (bytes: 5 and 7 for the 30-degree layout, 11 and 13 for 15)

The preview decodes the packed files back, so it shows what the game draws.
"""
import argparse
import csv
import json
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'scripts'))
import wc2data  # noqa: E402


def lin(c):
    return (np.asarray(c, dtype=np.float64) / 255.0) ** 2.2


def luma(c):
    return np.asarray(c) @ np.array([0.2126, 0.7152, 0.0722])


def rgb_of(text):
    return tuple(int(v) for v in text.split()) if text and text.strip() else None


def quantise(rgba, mask, meta, pal, livery):
    """The view as palette indices (-1 where nothing is drawn)."""
    h, w = mask.shape
    out = -np.ones((h, w), dtype=np.int32)
    on = rgba[..., 3] > 0
    px = rgba[..., :3].astype(np.float64)
    # hull: nearest hull or glow colour, 'redmean' weighted
    cand = np.array(meta['hull'] + list(range(meta['glow'], meta['glow'] + 16)) + [255])
    sel = on & (mask == 0)
    if sel.any():
        p = px[sel][:, None, :]
        c = pal[cand][None, :, :].astype(np.float64)
        rm = (p[..., 0] + c[..., 0]) / 2
        d = (2 + rm / 256) * (p[..., 0] - c[..., 0]) ** 2 + 4 * (p[..., 1] - c[..., 1]) ** 2 + \
            (2 + (255 - rm) / 256) * (p[..., 2] - c[..., 2]) ** 2
        out[sel] = cand[np.argmin(d, axis=1)]
    # paint: the shade whose lighting factor is nearest
    for m, base, key, role in ((1, meta['primary'], 'shades_primary', 'primary'),
                               (2, meta['secondary'], 'shades_secondary', 'secondary')):
        sel = on & (mask == m)
        col = rgb_of(livery.get(role))
        if sel.any() and col:
            f = luma(lin(px[sel])) / max(luma(lin(col)), 1e-6)
            s = np.array(meta[key])
            out[sel] = base + np.argmin(np.abs(np.log(f[:, None] + 1e-4) - np.log(s[None, :])), axis=1)
    # glass: by brightness
    sel = on & (mask == 3)
    if sel.any():
        g = luma(lin(pal[meta['glass']:meta['glass'] + 8]))
        out[sel] = meta['glass'] + np.argmin(np.abs(luma(lin(px[sel]))[:, None] - g[None, :]), axis=1)
    return out


def encode(idx):
    """WC sprite bytes for an index image, origin at its centre."""
    h, w = idx.shape
    cy, cx = h // 2, w // 2
    ys, xs = np.nonzero(idx >= 0)
    if not len(ys):
        return wc2data.encode_sprite(0, 0, 0, 0, {})
    rows = {}
    for y, x in zip(ys, xs):
        rows.setdefault(int(y - cy), {})[int(x - cx)] = int(idx[y, x])
    x2, x1 = int(xs.max() - cx), int(-(xs.min() - cx) - 1)
    y2, y1 = int(ys.max() - cy), int(-(ys.min() - cy) - 1)
    return wc2data.encode_sprite(x2, x1, y1, y2, rows)


def draw(sprite, pal, remap=None, size=None):
    """A packed view back to an RGBA image (for the preview)."""
    x2, x1, y1, y2, rows = wc2data.decode_sprite(sprite)
    w, h = x1 + x2 + 2, y1 + y2 + 2
    img = np.zeros((h, w, 4), dtype=np.uint8)
    for y, row in rows.items():
        for x, c in row.items():
            c = remap.get(c, c) if remap else c
            img[y + y1 + 1, x + x1 + 1] = (*pal[c], 255)
    return Image.fromarray(img, 'RGBA')


def livery_remap(meta, name):
    """Indices 64-79 to the livery's ramps."""
    r = meta['livery_ramps'].get(name) or {}
    m = {}
    for role, base in (('primary', meta['primary']), ('secondary', meta['secondary'])):
        if r.get(role) is not None:
            m.update({base + k: r[role] + k for k in range(8)})
    return m


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--sprites', default=os.path.join(HERE, '..', 'build', 'sprites'))
    ap.add_argument('--out', default=os.path.join(HERE, '..', 'build', 'rvdat'))
    ap.add_argument('--preview', default=os.path.join(HERE, '..', 'docs', 'packed.png'))
    ap.add_argument('--preview-only', action='store_true', help='redraw the preview from the packed files')
    args = ap.parse_args()
    meta = json.load(open(os.path.join(args.sprites, '..', 'palette.json')))
    pal = np.array(meta['rgb'], dtype=np.uint8)
    ships = [r for r in csv.DictReader(open(os.path.join(HERE, '..', 'data', 'ships.csv'))) if r['mesh']]
    art = {r['key']: r for r in csv.DictReader(open(os.path.join(HERE, '..', 'data', 'art.csv')))}
    liveries = {r['name']: r for r in csv.DictReader(open(os.path.join(HERE, '..', 'data', 'liveries.csv')))}
    names = list(liveries)
    if args.preview_only:
        preview(args.out, args.preview, meta, pal, ships, names)
        return
    os.makedirs(args.out, exist_ok=True)
    packed, total = {}, 0
    for r in ships:
        livery = (art.get(r['key']) or {}).get('livery') or 'none'
        d = os.path.join(args.sprites, r['key'])
        views = []
        for v in range(1800):
            p = os.path.join(d, '%s-%s_%02d.png' % (r['key'], livery, v))
            if not os.path.exists(p):
                break
            rgba = np.asarray(Image.open(p))
            mask = np.asarray(Image.open(os.path.join(d, '%s-%s_paint_%02d.png' % (r['key'], livery, v))))
            views.append(encode(quantise(rgba, mask, meta, pal, liveries[livery])))
        if not views:
            continue                    # not rendered into this sprites directory
        tab = wc2data.compose_container(views, flags=[0] * len(views))
        # the lattice: 37 / 62 views are 5 bands x 7 headings (30 degrees), 145 / 266 are 11 x 13 (15),
        # 325 / 614 are 17 x 19 (10), 901 / 1742 are 29 x 31 (6)
        nb, nh, asym = {37: (5, 7, 0), 62: (5, 7, 1), 145: (11, 13, 0), 266: (11, 13, 1),
                        325: (17, 19, 0), 614: (17, 19, 1), 901: (29, 31, 0), 1742: (29, 31, 1)}[len(views)]
        record = struct.pack('<4sBHBBHBB', b'RVSH', int(r['type'], 16), len(views), asym,
                             names.index(livery), int(r['lod0_px']), nb, nh)
        data = wc2data.compose_container([tab, record], flags=[0, 0])
        name = 'SHIP%02X.RV' % int(r['type'], 16)
        open(os.path.join(args.out, name), 'wb').write(data)
        packed[r['key']] = (r, views, livery)
        total += len(data)
        print('%-8s %s %2d views %7d bytes' % (r['key'], name, len(views), len(data)))
    print('total %d bytes (%.1f MB) in %s' % (total, total / 1e6, args.out))
    preview(args.out, args.preview, meta, pal, ships, names)


def read_ship(path):
    """(views, record fields) of a packed SHIPnn.RV."""
    blocks = wc2data.blocks_of(path)
    views = wc2data.blocks_of(blocks[0])
    magic, typ, n, flags, livery, lod0, nb, nh = struct.unpack('<4sBHBBHBB', blocks[1][:13])
    return views, dict(type=typ, views=n, flags=flags, livery=livery, lod0=lod0, bands=nb, headings=nh)


def preview(packdir, out, meta, pal, ships, names):
    """View 10 of every packed ship in its default livery (decoded from the
    files on disk, as the engine will), then one ship in every livery."""
    cell, cols = 130, 8
    rows_n = (len(ships) + cols - 1) // cols + 1
    img = Image.new('RGB', (cols * cell, rows_n * (cell + 12)), (0, 0, 0))
    dr = ImageDraw.Draw(img)
    for i, r in enumerate(ships):
        views, rec = read_ship(os.path.join(packdir, 'SHIP%02X.RV' % int(r['type'], 16)))
        im = draw(views[10], pal, livery_remap(meta, names[rec['livery']]))
        im.thumbnail((cell, cell), Image.NEAREST)
        x, y = (i % cols) * cell, (i // cols) * (cell + 12)
        img.paste(im, (x + (cell - im.width) // 2, y + (cell - im.height) // 2), im)
        dr.text((x + 2, y + cell), r['name'], fill=(220, 220, 220))
    y = (rows_n - 1) * (cell + 12)
    views, _rec = read_ship(os.path.join(packdir, 'SHIP02.RV'))
    for j, name in enumerate([n for n in names if n != 'none'][:cols]):
        im = draw(views[10], pal, livery_remap(meta, name))
        im.thumbnail((cell, cell), Image.NEAREST)
        img.paste(im, (j * cell + (cell - im.width) // 2, y + (cell - im.height) // 2), im)
        dr.text((j * cell + 2, y + cell), 'Gawain ' + name, fill=(255, 210, 110))
    img.save(out)
    print('preview', out)


if __name__ == '__main__':
    main()
