#!/usr/bin/env python3
"""The pilot's legs under the dash (data/legs.json; tools/panels.py paints them into each cockpit's base).

    legs.py [KEY ...] [--out PNG]        a sheet of the cockpits' lower halves, the legs painted, the hand on the stick

The user's (2026-10-24): the footpads either side of the stick read as knees with nothing after them, as if the
pilot's legs were curled back (in Goddard and Redeemer, legs against the control panel); the legs go on under the
dash.  No shoes: they come up from the bottom edge in the flight suit's olive and fade out to black in the footwells,
which go dark, the footpads mostly hidden.  Lancelot's COMM FREQ and E/M panels lose their inner ends for them, its
NAV readout moved out into the room; the Schroedinger's EJECT box and the Pacifier's lower-left panel are cut away
(the user's, the same day: no one-legged pilot), and so are the Gawain's and the Lancelot's EJECT boxes (they
blocked the legs).

A cockpit's entry in data/legs.json:
  moves   [x, y, w, h, dx, dy]: a box of the kit's art copied dx, dy along (from the art as it was, in order); a panel
          control or a damage layer inside it goes with it.  Also what the bigger hand hid (tools/joysticks.py; the
          user's pick, 2026-10-25): the Redeemer's SHIELD, ARMOR and HULL bars swapped with the grille left of them,
          the Progeny's E/M box with its EJECT box
  wells   the footwells: a polygon, deep (the row it is black from, under the dash) and near (the row it would be
          lightest at, below the screen); fill: the art in it replaced (a panel cut away) rather than darkened;
          feather: how many pixels in from its edge the dark comes in full (4)
  legs    hip (below the screen) and knee, the half widths there, the well it shows in, light (1: a leg glimpsed
          between two boxes is in their shade): the thigh and its knee, the shin on from it into the dark
The rest of the base, the panels' controls and the damage layers are drawn in front of the legs: a control or a layer
left inside a well is dropped (a layer's pixels there cleared).
"""
import argparse
import json
import os

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SPEC = os.path.join(ROOT, 'data', 'legs.json')
SUIT = np.array([104, 101, 64], float)          # the sleeve's olive (the hand's poses, lit) - tone 1.0, full light
FLOOR = np.array([30, 28, 22], float)           # a cut-away panel's footwell, nearest the pilot


def load(key):
    spec = json.load(open(SPEC))['cockpits']
    return spec.get(key)


def noise(shape, scale, seed):
    r = np.random.default_rng(seed)
    n = r.standard_normal((shape[0] // scale + 3, shape[1] // scale + 3))
    n = ndimage.zoom(n, scale, order=3)[:shape[0], :shape[1]]
    return (n - n.mean()) / n.std()


def tube(shape, p0, p1, r0, r1, cap=0.55):
    """a tapered tube p0 -> p1, half widths r0, r1, a foreshortened round end at p1: (inside, across -1..1,
    along 0 at p0 .. 1 at p1 (more on the end), the distance along in pixels)"""
    H, W = shape
    yy, xx = np.mgrid[0:H, 0:W].astype(float)
    d = np.array(p1, float) - p0
    L = np.hypot(*d)
    u = d / L
    px, py = xx - p0[0], yy - p0[1]
    t = (px * u[0] + py * u[1]) / L
    s = -px * u[1] + py * u[0]
    r = r0 + (r1 - r0) * np.clip(t, 0, 1)
    inside = (np.abs(s) < r) & (t >= 0) & (t <= 1)
    k = (t - 1) * L / (r1 * cap)
    inside |= (t > 1) & (k ** 2 + (s / r1) ** 2 < 1)
    return inside, np.clip(s / r, -1, 1), np.where(t > 1, 1 + np.clip(k, 0, 1) * 0.3, np.clip(t, 0, 1)), t * L


def wells_mask(shape, spec):
    out = []
    for w in spec.get('wells', []):
        im = Image.new('L', (shape[1], shape[0]), 0)
        ImageDraw.Draw(im).polygon([tuple(p) for p in w['poly']], fill=255)
        out.append(np.asarray(im) > 0)
    return out


def moved(rect, spec):
    """a rectangle [x, y, w, h] (a control's, a layer's) as the moves leave it: (dx, dy) when it is inside a moved
    box, else (0, 0)"""
    x, y, w, h = rect
    for bx, by, bw, bh, dx, dy in spec.get('moves', []):
        if bx <= x and by <= y and x + w <= bx + bw and y + h <= by + bh:
            return dx, dy
    return 0, 0


def covered(shape, spec):
    """what the legs' work painted over: the wells and the moves' destinations"""
    m = np.zeros(shape, bool)
    for w in wells_mask(shape, spec):
        m |= w
    for bx, by, bw, bh, dx, dy in spec.get('moves', []):
        m[max(0, by + dy):by + dy + bh, max(0, bx + dx):bx + dx + bw] = True
    return m


def fix_layer(x, y, im, spec):
    """a damage layer as the legs leave it: moved with its box, or its pixels over the legs' work cleared (None:
    nothing left)"""
    dx, dy = moved([x, y, im.width, im.height], spec)
    if dx or dy:
        return x + dx, y + dy, im
    a = np.asarray(im.convert('RGBA')).copy()
    m = covered((400, 640), spec)[y:y + im.height, x:x + im.width]
    a[m[:a.shape[0], :a.shape[1]]] = 0
    if not (a[..., 3] >= 128).any():                   # (what a sprite keeps: rvpack)
        return None
    return x, y, Image.fromarray(a, 'RGBA')


def paint(a, spec, seed=11):
    """the base (H x W x 4, uint8) with the moves made, the wells dark and the legs in them"""
    orig = a.copy()
    out = a.copy()
    H, W = a.shape[:2]
    for bx, by, bw, bh, dx, dy in spec.get('moves', []):
        out[by + dy:by + dy + bh, bx + dx:bx + dx + bw] = orig[by:by + bh, bx:bx + bw]
    rgb = out[..., :3].astype(float)
    yy = np.mgrid[0:H, 0:W][0].astype(float)
    masks = wells_mask((H, W), spec)
    depths, edges = [], []
    for w, m in zip(spec.get('wells', []), masks):
        edge = np.clip(ndimage.distance_transform_edt(m) / w.get('feather', 4.0), 0, 1)
        depth = np.clip((w['near'] - yy) / (w['near'] - w['deep']), 0, 1)     # 1 deep under the dash, 0 nearest
        if w.get('fill'):
            grain = 1 + 0.12 * noise((H, W), 2, seed + 7)
            floor = FLOOR[None, None, :] * ((1 - depth) ** 1.5 * grain)[..., None]
            rgb[m] = floor[m]
        else:
            dark = np.clip(0.55 + 0.45 * depth, 0, 1) ** 0.8 * edge                # the footpads mostly hidden
            rgb[m] = (rgb * (1 - dark[..., None]))[m]
        depths.append(depth)
        edges.append(edge)
    warp = noise((H, W), 14, seed)
    tone_n = noise((H, W), 11, seed + 1)
    for leg in spec.get('legs', []):
        i = leg.get('well', 0)
        m, depth, edge = masks[i], depths[i], edges[i]
        p0, p1 = np.array(leg['hip'], float), np.array(leg['knee'], float)
        r0, r1 = leg['r']
        inner = 1 if p1[0] < W / 2 else -1                  # (the side towards the stick: in its shadow)
        lit = leg.get('light', 1.0)                         # (a leg glimpsed between two boxes: in their shade)
        # the shin, on from the knee under the dash: down and away, in the dark
        q = p1 + (p1 - p0) * 0.35
        sin_, sacr, salong, _d = tube((H, W), p1, q, r1 * 0.9, r1 * 0.75, cap=0.8)
        sin_ &= m
        sh = lit * 0.4 * np.sqrt(1 - sacr ** 2) * (1 - 0.8 * np.clip(salong, 0, 1)) * (1 - depth) ** 1.2 * edge
        rgb[sin_] = (SUIT[None, None, :] * sh[..., None])[sin_]
        inside, across, along, dist = tube((H, W), p0, p1, r0, r1)
        inside &= m
        n = np.sqrt(1 - across ** 2)
        # lit from the canopy, above and ahead: the thigh's top brightest, round to dark sides, the side towards the
        # stick darker
        light = 0.1 + 0.9 * np.clip(n ** 1.3 - 0.22 * across * inner, 0, 1)
        light *= np.where(along > 1, 1.1 - 0.85 * (along - 1) / 0.3, 1.0)           # the knee turning away
        light *= 1 + 0.18 * np.exp(-((along - 0.9) / 0.07) ** 2) * n ** 2            # its cap's highlight
        # the cloth: soft creases slanting across the leg as across the sleeve, a seam down its outer side
        phase = dist / 9.0 + 0.5 * across * inner + 0.25 * warp
        f = 0.5 + 0.5 * np.sin(2 * np.pi * phase)
        crease = np.clip((f - 0.4) / 0.6, 0, 1) * (0.16 + 0.14 * np.clip(along, 0, 1))
        seam = 0.22 * np.exp(-((across * inner + 0.62) / 0.06) ** 2)
        tone = (1 - crease) * (1 - seam) * (1 + 0.05 * tone_n)
        fade = np.clip(1.2 - depth, 0, 1) ** 1.3 * edge                               # on into the dark
        col = SUIT[None, None, :] * (lit * light * tone * fade)[..., None]
        rgb[inside] = col[inside]
    out[..., :3] = np.clip(rgb, 0, 255).astype(np.uint8)
    return out


def sheet(keys, path, panels_dir):
    rows = []
    for key in keys:
        base = os.path.join(panels_dir, key, 'base.png')
        im = Image.open(base).convert('RGBA')
        sj = os.path.join(ROOT, 'art', 'cockpits', key, 'stick', 'stick.json')
        if os.path.exists(sj):
            p = json.load(open(sj))['poses'][4]
            im.alpha_composite(Image.open(os.path.join(os.path.dirname(sj), p['file'])).convert('RGBA'), (p['x'], p['y']))
        bg = Image.new('RGBA', im.size, (0, 0, 0, 255))
        bg.alpha_composite(im)
        rows.append((key, bg.convert('RGB').crop((0, 240, 640, 400))))
    Z = 2
    out = Image.new('RGB', (640 * Z, (160 * Z + 18) * len(rows)), (18, 18, 22))
    d = ImageDraw.Draw(out)
    for i, (key, im) in enumerate(rows):
        y = i * (160 * Z + 18)
        d.text((6, y + 3), key, fill=(255, 220, 120))
        out.paste(im.resize((640 * Z, 160 * Z), Image.NEAREST), (0, y + 18))
    out.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('keys', nargs='*')
    ap.add_argument('--panels', default=os.path.join(ROOT, 'build', 'panels'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'legs.png'))
    args = ap.parse_args()
    keys = args.keys or list(json.load(open(SPEC))['cockpits'])
    sheet(keys, args.out, args.panels)


if __name__ == '__main__':
    main()
