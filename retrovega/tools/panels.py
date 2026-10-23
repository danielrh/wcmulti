#!/usr/bin/env python3
"""The cockpits' panels made to work: every control's states as art (data/panels.json -> build/panels/KEY/).

    panels.py [KEY ...] [--out build/panels] [--preview docs/panels]

The user's brief (2026-10-22): every dial, display and light of each cockpit does something (or is blanked); the
power-distribution knobs, which the game has no use for, repainted as a radio's frequency dials that turn to
whoever is talking (wing, the carrier, an enemy on Guard).  data/panels.json says, for each cockpit, what each
control is and does; this makes what the engine draws:

  lamp    its other state: a painted lit lamp (bright, saturated pixels round its centre) darkened, a painted dark
          one (its face, the lamp's colour) lit, a dark square button given a glow of that colour
  knob    its body turned to each of its positions (rendered 4x, rotated about its centre, back down)
  arcs    the dial scales round a set of knobs, all one green: dim in the base, lit as a state
  needle  a gauge's painted needle painted out of the base (the engine draws a moving one)
  relabel lettering painted out (its light pixels filled in from the plate round them) and new lettering, a light
          condensed face with a dark drop shadow as the kits letter theirs, at the panel's slant
  cabin   every other lit lamp of the cockpit, found by colour (outside the opening, the displays and the other
          controls): one state, all of them dark (the running lights off)

Out, for each cockpit: base.png (the kit's base relabelled, the needles painted out), states/NN-S.png (a state:
only the pixels that differ from the base, the rest clear) and panel.json (each control: its function, kind,
argument, rectangle, the state the base shows, its states' files and places).  With --preview: a sheet of every
control's states, magnified.
"""
import argparse
import json
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FONT = os.path.join(ROOT, 'art', 'fonts', 'RobotoCondensed-Bold.ttf')
COLOURS = {'amber': (255, 176, 60), 'red': (255, 64, 48), 'green': (110, 255, 140), 'blue': (110, 190, 255),
           'white': (255, 244, 220)}
DIGITS = [-108 + 24 * d for d in range(10)]      # a frequency knob's ten positions, degrees from straight up
# the engine's numbering (rvpack writes RV_PF_* and RV_PK_* from these): data/panels.json's functions, and kinds
FUNCTIONS = ['lock', 'warn', 'ab_lamp', 'nav_lamp', 'com_lamp', 'com_on', 'iff_lamp', 'em_lamp', 'emcon_lamp',
             'sensor_lamp', 'target_lamp', 'status_lamp', 'mfd_lamp', 'surv_page', 'radio_chan', 'arm_lamp',
             'auto_lamp', 'defence', 'sig_ladder', 'range_ladder', 'cabin', 'radio_digit', 'radio_lit', 'chan_knob',
             'emcon_knob', 'nav_knob', 'iff_knob', 'radar_knob', 'arm_knob', 'vol_knob', 'speed_dial', 'clock_dial',
             'shield_bar', 'armor_bar', 'hull_bar', 'fuel_bar', 'power_bar', 'engine_bar', 'speed_bar', 'range_bar',
             'heat', 'signature', 'aux_nav', 'target_id', 'throttle_ticks', 'survival']
KINDS = ['lamp', 'knob', 'arcs', 'needle', 'ticks', 'bar', 'text', 'map']
# a map's pixel values (kind map: the engine colours each by what it stands for), the survival display: SV_SHIP the
# ship's silhouette; a region's pixel SV_BASE + region * SV_RANKS + its rank (0 .. SV_RANKS - 1, the order the
# pixels go in as the shield or the armour weakens, pixel by pixel, the outermost first - the user's picks,
# 2026-10-23: the shields ring by ring like a tree's from the outside in, the front armour front to back, the rear
# back to front, the left left to right, the right right to left); the regions SV_SHIELD_F, SV_SHIELD_R,
# SV_ARMOR_F, SV_ARMOR_R, SV_ARMOR_L, SV_ARMOR_RT
SV_SHIP, SV_BASE, SV_RANKS = 1, 2, 42
SV_SHIELD_F, SV_SHIELD_R, SV_ARMOR_F, SV_ARMOR_R, SV_ARMOR_L, SV_ARMOR_RT = range(6)
_SV_AREA = 100                          # (the layout's own codes, before the ranks: 100 + region, 99 the ship)


def lum(rgb):
    return rgb[..., :3].astype(np.float64) @ np.array([0.299, 0.587, 0.114])


def sat(rgb):
    c = rgb[..., :3].astype(np.float64)
    mx, mn = c.max(-1), c.min(-1)
    return (mx - mn) / np.maximum(mx, 1)


def disc(shape, cx, cy, r):
    ys, xs = np.mgrid[0:shape[0], 0:shape[1]]
    return np.hypot(xs + 0.5 - cx, ys + 0.5 - cy) <= r


def box_mask(shape, rect):
    m = np.zeros(shape[:2], bool)
    x, y, w, h = rect
    m[max(y, 0):y + h, max(x, 0):x + w] = True
    return m


def inpaint(a, mask, noise=3.0, seed=1):
    """the masked pixels filled in from round them (a diffusion), a little grain added"""
    out = a[..., :3].astype(np.float64)
    known = ~mask
    for _ in range(64):
        if known.all():
            break
        acc = np.zeros_like(out)
        n = np.zeros(mask.shape)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            k = np.roll(np.roll(known, dy, 0), dx, 1)
            acc += np.roll(np.roll(out, dy, 0), dx, 1) * k[..., None]
            n += k
        fill = ~known & (n > 0)
        out[fill] = acc[fill] / n[fill][:, None]
        known |= fill
    out[mask] += np.random.default_rng(seed).normal(0, noise, (int(mask.sum()), 1))
    a[..., :3] = np.clip(out, 0, 255).astype(np.uint8)


# --- relabelling ---

def erase_lettering(a, box, dark=False):
    """lettering in a box [x0, y0, x1, y1] painted out: light on a dark plate, or dark on a light one"""
    x0, y0, x1, y1 = box
    sub = a[y0:y1, x0:x1]
    L = lum(sub)
    mask = ndimage.binary_dilation(L < np.median(L) - 18 if dark else L > np.median(L) + 18, iterations=1)
    inpaint(sub, mask)


def letter(a, text, cx, cy, px, slant, dark=False):
    """light lettering with a dark drop shadow (or dark lettering, no shadow, on a light panel), drawn at 4x and
    brought down, centred on cx, cy"""
    Z = 4
    f = ImageFont.truetype(FONT, int(round(px * Z)))
    w = int(f.getlength(text)) + 8 * Z
    lay = Image.new('L', (w, int(px * Z * 2)), 0)
    ImageDraw.Draw(lay).text((4 * Z, px * Z / 2), text, font=f, fill=255)
    lay = lay.rotate(slant, resample=Image.BICUBIC, expand=True)
    small = np.asarray(lay.resize((max(1, lay.width // Z), max(1, lay.height // Z)), Image.LANCZOS),
                       dtype=np.float64) / 255.0
    x0, y0 = int(round(cx - small.shape[1] / 2)), int(round(cy - small.shape[0] / 2))
    for dx, dy, col, k in (((0, 0, (52, 50, 48), 1.0),) if dark else ((1, 1, (10, 9, 8), 0.8), (0, 0, (214, 210, 200), 1.0))):
        for y, x in zip(*np.nonzero(small > 0.05)):
            X, Y = x0 + x + dx, y0 + y + dy
            if 0 <= X < a.shape[1] and 0 <= Y < a.shape[0]:
                t = small[y, x] * k
                a[Y, X, :3] = (a[Y, X, :3] * (1 - t) + np.array(col) * t).astype(np.uint8)


def arc_mask(a, knobs):
    """the coloured scales round a set of knobs [cx, cy, r0, r1]: bright pixels in the ring above each"""
    L = lum(a)
    mx = a[..., :3].max(-1)
    S = sat(a)
    m = np.zeros(a.shape[:2], bool)
    ys = np.mgrid[0:a.shape[0], 0:a.shape[1]][0]
    for cx, cy, r0, r1 in knobs:
        ring = disc(a.shape, cx, cy, r1 + 1) & ~disc(a.shape, cx, cy, r0 - 1) & (ys < cy + 3)
        m |= ring & (((S > 0.2) & (mx > 120)) | (mx > 170))
    del L
    return m


def recolour(a, m, lit):
    v = a[m, :3].max(-1).astype(np.float64)[:, None] / 255.0
    g = np.array([150, 255, 170]) if lit else np.array([70, 125, 80])
    a[m, :3] = np.clip(v * g, 0, 255).astype(np.uint8)


# --- lamps ---

def lamp_states(a, c):
    """(painted state, the other state's picture) of a lamp: lit if the base paints it lit"""
    col = np.array(COLOURS[c.get('colour', 'amber')], dtype=np.float64)
    if 'rect' in c:
        region = box_mask(a.shape, c['rect'])
    else:
        region = disc(a.shape, c['at'][0], c['at'][1], c.get('r', 6))
    L, S = lum(a), sat(a)
    edge = region & ~ndimage.binary_erosion(region)
    ground = np.median(L[edge]) if edge.any() else np.median(L[region])
    lit = region & (((L > 140) & (S > 0.25)) | (L > 205)) & (L > ground + 40)
    other = a.copy()
    if lit.sum() >= 3 and not c.get('dark') and c.get('face') != 'all':
        # painted lit: those pixels (and the glow a pixel round them) darkened to the unlit glass
        glow = region & ndimage.binary_dilation(lit, iterations=1) & (L > ground + 12)
        m = lit | glow
        rgb = a[m, :3].astype(np.float64)
        k = np.clip((ground * 0.9 + 14) / np.maximum(lum(a[m]), 1), 0.18, 0.6)[:, None]
        grey = rgb.mean(-1, keepdims=True)
        other[m, :3] = np.clip((rgb * 0.6 + grey * 0.4) * k, 0, 255).astype(np.uint8)
        return 1, other
    # painted dark: its face (the lamp's colour, or the whole region for a plain button) lit
    hue_like = (S > 0.18) & (np.abs((a[..., :3].astype(np.float64) / np.maximum(a[..., :3].max(-1, keepdims=True), 1)
                                     - col / col.max())).sum(-1) < 0.9)
    face = region & hue_like & (L > ground * 0.7)
    if face.sum() < 4 or c.get('face') == 'all':
        face = ndimage.binary_erosion(region, iterations=1) if 'rect' in c else disc(a.shape, c['at'][0], c['at'][1],
                                                                                    max(1.5, c.get('r', 6) - 1.0))
    t = np.clip((L[face] - L[face].min()) / max(1.0, np.ptp(L[face])), 0, 1)[:, None]
    other[face, :3] = np.clip(a[face, :3] * 0.25 + col * (0.62 + 0.38 * t), 0, 255).astype(np.uint8)
    ring = ndimage.binary_dilation(face, iterations=1) & ~face & region
    other[ring, :3] = np.clip(a[ring, :3] * 0.6 + col * 0.35, 0, 255).astype(np.uint8)
    return 0, other


# --- knobs and needles ---

def knob_at(a, cx, cy, r, painted, angle):
    """the base with the knob's body (the disc of radius r) turned from painted to angle (degrees, clockwise from
    straight up)"""
    Z = 4
    x0, y0 = int(cx - r - 2), int(cy - r - 2)
    x1, y1 = int(cx + r + 3), int(cy + r + 3)
    crop = Image.fromarray(np.ascontiguousarray(a[y0:y1, x0:x1])).resize(((x1 - x0) * Z, (y1 - y0) * Z),
                                                                           Image.LANCZOS)
    rot = crop.rotate(-(angle - painted), resample=Image.BICUBIC, center=((cx - x0) * Z, (cy - y0) * Z))
    rot = np.asarray(rot.resize((x1 - x0, y1 - y0), Image.LANCZOS))
    ys, xs = np.mgrid[y0:y1, x0:x1]
    t = np.clip(r + 0.5 - np.hypot(xs + 0.5 - cx, ys + 0.5 - cy), 0, 1)[..., None]
    out = a.copy()
    out[y0:y1, x0:x1, :3] = (a[y0:y1, x0:x1, :3] * (1 - t) + rot[..., :3] * t).astype(np.uint8)
    return out


def erase_needle(a, cx, cy, r, angle, width=1.6, tail=2):
    """a gauge's painted needle (light pixels along angle from the centre, and tail pixels back past it) painted
    out"""
    ys, xs = np.mgrid[0:a.shape[0], 0:a.shape[1]]
    th = np.radians(angle)
    ux, uy = np.sin(th), -np.cos(th)
    px, py = xs + 0.5 - cx, ys + 0.5 - cy
    along = px * ux + py * uy
    across = np.abs(px * uy - py * ux)
    near = (along > -tail) & (along < r) & (across < width + 1.5)
    L = lum(a)
    face = np.median(L[disc(a.shape, cx, cy, r) & ~near])
    m = near & (L > face + 35)
    inpaint(a, ndimage.binary_dilation(m, iterations=1) & near, noise=2.0)


# --- the cabin's other lamps ---

def cabin_mask(a, exclude):
    L, S = lum(a), sat(a)
    lit = (((L > 150) & (S > 0.35)) | (L > 225)) & (a[..., 3] >= 128) & ~exclude
    lab, n = ndimage.label(lit)
    keep = np.zeros(lit.shape, bool)
    for i, sl in enumerate(ndimage.find_objects(lab), 1):
        blob = lab[sl] == i
        if blob.sum() >= 3:
            keep[sl] |= blob
    return keep & ~exclude


def darken(a, m):
    out = a.copy()
    L = lum(a)
    g = ndimage.binary_dilation(m, iterations=1) & (L > np.median(L) + 10) & ~m
    for mm, k in ((m, 0.3), (g, 0.6)):
        rgb = a[mm, :3].astype(np.float64)
        out[mm, :3] = np.clip((rgb * 0.7 + rgb.mean(-1, keepdims=True) * 0.3) * k, 0, 255).astype(np.uint8)
    return out


# --- the survival display and the thermal picture: the ship's silhouette (data/shapes.dat) ---

_TOPS = {}


def silhouette(key):
    """a ship's top view as the player sees it (view 0 of its sprites, art/ships/SHIPnn.RV: drawn from the mesh,
    its wings and all; the collision silhouettes in data/shapes.dat drop thin parts), nose up: a bool mask (rows
    from the nose), and its engines (data/engines.csv) on it: [u, v, size], u across (-1 left .. 1 right), v along
    (1 the nose .. -1 the tail), size as the mesh's"""
    import csv
    import pack                         # (tools/pack.py: the sprites' reader)
    if key in _TOPS:
        return _TOPS[key]
    ships = {r['key']: r for r in csv.DictReader(open(os.path.join(ROOT, 'data', 'ships.csv')))}
    hit = {r['key']: r for r in csv.DictReader(open(os.path.join(ROOT, 'data', 'hitboxes.csv')))}
    eng = {r['key']: r for r in csv.DictReader(open(os.path.join(ROOT, 'data', 'engines.csv')))}
    typ = int(ships[key]['type'], 16)
    files = [os.path.join(ROOT, d, 'SHIP%02X.RV' % typ) for d in ('art/ships', 'build/rvdat')]
    files = [f for f in files if os.path.exists(f)]
    if files:
        views, _rec = pack.read_ship(files[0])
        pal = [tuple(c) for c in json.load(open(os.path.join(ROOT, 'data', 'palette.json')))['rgb']]
        a = np.asarray(pack.draw(views[0], pal))[..., 3] > 0      # (nose to the left)
        ys, xs = np.nonzero(a)
        a = a[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
        top = np.ascontiguousarray(np.rot90(a, -1))               # nose up, its right wing on the right
    else:                               # (no sprites: the collision silhouette, data/shapes.dat's top, over its box)
        import struct
        blob = open(os.path.join(ROOT, 'data', 'shapes.dat'), 'rb').read()
        _v, n, N, _p = struct.unpack_from('<4H', blob, 8)
        at = 16
        for _ in range(n):
            if blob[at] == typ:
                m = np.unpackbits(np.frombuffer(blob[at + 2:at + 2 + N * N // 8], np.uint8), bitorder='little')
                break
            at += 2 + 3 * N * N // 8
        hx_, hz_ = float(hit[key]['hx']), float(hit[key]['hz'])
        sq = m.reshape(N, N)[::-1].astype(bool)
        top = np.asarray(Image.fromarray(sq.astype(np.uint8) * 255).resize((max(8, int(N * hx_ / hz_)), N),
                                                                         Image.NEAREST)) > 127
    hs, ws = top.shape
    hz = float(hit[key]['hz'])                                    # (the mesh's half length: the sprite's)
    e = eng.get(key)
    engines = []
    if e:
        for i in range(int(e['n'])):
            ex, ez = float(e['x%d' % i]), float(e['z%d' % i])
            engines.append([ex / hz * hs / ws, ez / hz, float(e['size%d' % i])])
    _TOPS[key] = (top, engines)
    return _TOPS[key]


def fit(shape_w, shape_h, w, h):
    """the scale and offset that fit shape_w x shape_h in w x h, centred, keeping its aspect"""
    k = min(w / shape_w, h / shape_h)
    return k, (w - shape_w * k) / 2, (h - shape_h * k) / 2


def ship_in(key, w, h, S):
    """the silhouette at S x over a w x h box (nose up, fitted, its aspect kept): a bool mask, and each pixel's
    place on the ship (u across, v along: -1 .. 1, the nose v = 1)"""
    top, _e = silhouette(key)
    hs, ws = top.shape
    k, ox, oy = fit(ws, hs, w * S, h * S)
    ys, xs = np.mgrid[0:h * S, 0:w * S]
    u = (xs + 0.5 - ox) / (ws * k)                     # 0 .. 1 across the ship
    v = (ys + 0.5 - oy) / (hs * k)                     # 0 .. 1 from the nose
    inside = (u >= 0) & (u < 1) & (v >= 0) & (v < 1)
    m = np.zeros(inside.shape, bool)
    m[inside] = top[(v[inside] * hs).astype(int), (u[inside] * ws).astype(int)]
    return m, (u * 2 - 1), (1 - v * 2)


def smooth_max(ds, k):
    m = np.max(ds, axis=0)
    return m + k * np.log(np.sum(np.exp((ds - m) / k), axis=0))


def survival_map(key, w, h):
    """the survival display (the user's design, 2026-10-23): the front and rear shields two rounded trapezoids
    round the ship, the armour four trapezoids inside them (a box's frame cut along its diagonals, one a side), the
    ship's silhouette in the middle; regions apart (a pixel's gap); nose up, or (a wide display) nose to the right.
    -> (h, w) bytes (SV_*; 0 nothing)"""
    if w > h * 1.3:
        return np.ascontiguousarray(np.rot90(survival_map(key, h, w), -1))
    S = 4
    W, H = w * S, h * S
    ys, xs = np.mgrid[0:H, 0:W]
    px, py = xs + 0.5, ys + 0.5
    m = 1.0 * S                                         # (a pixel's margin all round)
    x0, x1, y0, y1 = m, W - m, m, H - m
    cx, cy = W / 2, H / 2
    sx, sy = (x1 - x0) * 0.24, (y1 - y0) * 0.26         # the trapezoids' slants
    # the outline: a rounded octagon (two trapezoids, the front and the back, on a box): the distance inside < 0
    planes = [(-(px - x0)), (px - x1), (-(py - y0)), (py - y1)]
    for ax, ay, bx, by in ((x0, y0 + sy, x0 + sx, y0), (x1 - sx, y0, x1, y0 + sy), (x1, y1 - sy, x1 - sx, y1),
                           (x0 + sx, y1, x0, y1 - sy)):
        nx, ny = by - ay, ax - bx                       # (the outward normal of the edge a -> b, clockwise)
        ln = np.hypot(nx, ny)
        planes.append(((px - ax) * nx + (py - ay) * ny) / ln)
    d = smooth_max(np.stack(planes), 0.9 * S)
    t = max(2, round(min(w, h) * 0.09)) * S             # shield band, armour band
    gap = 1 * S
    out = np.zeros((H, W), np.uint8)
    band = (d <= 0) & (d > -t)
    side_gap = max(1.0, h * 0.07) * S
    A = _SV_AREA
    out[band & (py < cy - side_gap)] = A + SV_SHIELD_F
    out[band & (py > cy + side_gap)] = A + SV_SHIELD_R
    # the armour: the biggest box (the display's shape) inside the shields' inner edge, its frame (a little thicker
    # than the shields) cut along the box's diagonals into four trapezoids, a pixel's gap between them
    def corner_in(a_, b_):                          # (is the box's corner inside the shields' inner edge?)
        x_, y_ = int(min(W - 1, cx + a_)), int(min(H - 1, cy + b_))
        return smooth_max(np.stack([pl[y_, x_] for pl in planes])[:, None], 0.9 * S)[0] <= -(t + gap)
    a = b = 0.0
    for q in np.linspace(0.5, 2.5, 21):             # (the box's height over its width, against the display's: the
        k = q * H / W                               #  shape that gives the biggest box)
        lo, hi = 0.0, W / 2.0
        for _ in range(24):
            mid = (lo + hi) / 2
            fits = mid * k <= H / 2 - m and corner_in(mid, mid * k)
            lo, hi = (mid, hi) if fits else (lo, mid)
        if lo * lo * k > a * b:
            a, b = lo, lo * k
    ta = max(2, round(min(w, h) * 0.08)) * S
    u, v = (px - cx) / a, (py - cy) / b
    box = (np.abs(px - cx) <= a) & (np.abs(py - cy) <= b)
    frame = box & ~((np.abs(px - cx) <= a - ta) & (np.abs(py - cy) <= b - ta))
    # (the diagonals of the frame: from each corner of the box to the inner box's; a pixel's gap along them)
    dgx, dgy = np.abs(px - cx) - (a - ta), np.abs(py - cy) - (b - ta)
    diag = np.abs(dgx - dgy) < gap * 0.7
    out[frame & ~diag & (py < cy) & (dgy > dgx)] = A + SV_ARMOR_F
    out[frame & ~diag & (py > cy) & (dgy > dgx)] = A + SV_ARMOR_R
    out[frame & ~diag & (px < cx) & (dgx > dgy)] = A + SV_ARMOR_L
    out[frame & ~diag & (px > cx) & (dgx > dgy)] = A + SV_ARMOR_RT
    inner = (np.abs(px - cx) <= a - ta - gap) & (np.abs(py - cy) <= b - ta - gap)
    ys_in, xs_in = np.nonzero(inner)
    bx0, bx1, by0, by1 = xs_in.min(), xs_in.max() + 1, ys_in.min(), ys_in.max() + 1
    ship, _u, _v = ship_in(key, (bx1 - bx0) // S, (by1 - by0) // S, S)
    sub = out[by0:by0 + ship.shape[0], bx0:bx0 + ship.shape[1]]
    sh = ship[:sub.shape[0], :sub.shape[1]] & inner[by0:by0 + ship.shape[0], bx0:bx0 + ship.shape[1]][:sub.shape[0], :sub.shape[1]]
    sub[sh] = A - 1
    # down to the display's pixels: each pixel the region covering most of it (half at least), else nothing
    area = np.zeros((h, w), np.uint8)
    for r in [A + k for k in range(6)] + [A - 1]:
        cover = (out == r).reshape(h, S, w, S).mean(axis=(1, 3))
        area[(cover >= 0.5) & (area == 0)] = r
    # each region's pixels ranked, inward: the outermost along its own axis last in the rank (it goes first) - the
    # front shield and armour by how far ahead of the middle, the rear by how far behind, the left by how far left,
    # the right by how far right; a row's (or column's) outer ends before its middle
    cxs, cys = w / 2.0, h / 2.0
    small = np.where(area == A - 1, SV_SHIP, 0).astype(np.uint8)
    depth = np.floor(-d.reshape(h, S, w, S).mean(axis=(1, 3)) / S)   # (whole pixels in from the outline)
    for k in range(6):
        ys_, xs_ = np.nonzero(area == A + k)
        if not len(xs_):
            continue
        dx, dy = xs_ + 0.5 - cxs, ys_ + 0.5 - cys
        if k in (SV_SHIELD_F, SV_SHIELD_R):
            # the shields like a tree's rings (the user's, 2026-10-23): the outermost ring of the band first, then
            # the next in; round a ring, its ends before its middle (straight ahead of the nose, or behind the tail)
            out_, side = -depth[ys_, xs_], -np.arctan2(np.abs(dx) / w, np.abs(dy) / h)
        else:
            out_, side = {SV_ARMOR_F: (-dy, np.abs(dx)), SV_ARMOR_R: (dy, np.abs(dx)), SV_ARMOR_L: (-dx, np.abs(dy)),
                          SV_ARMOR_RT: (dx, np.abs(dy))}[k]
        order = np.lexsort((side, out_))                # (inward first: rank 0 the innermost, the last to go)
        rank = np.empty(len(order), int)
        rank[order] = np.arange(len(order))
        small[ys_, xs_] = (SV_BASE + k * SV_RANKS + rank * (SV_RANKS - 1) // max(1, len(order) - 1)).astype(np.uint8)
    return small


def thermal_map(key, w, h):
    """the ship's silhouette for the HEAT display, each pixel how hot it runs (1 .. 255; 0 not the ship): the hull
    warm, its engines hot (data/engines.csv), the nose and the edges cooler; nose up, or (a wide display) nose to
    the right"""
    if w > h * 1.3:
        return np.ascontiguousarray(np.rot90(thermal_map(key, h, w), -1))
    S = 4
    ship, u, v = ship_in(key, w - 2, h - 2, S)
    top, engines = silhouette(key)
    aspect = top.shape[1] / top.shape[0]               # (across over along: u's units into v's)
    heat = 0.25 + 0.15 * (1 - np.abs(u)) + 0.1 * (-v)
    for eu, ev, size in engines:
        r = max(0.12, size * 0.3)
        heat += 0.75 * np.exp(-(((u - eu) * aspect) ** 2 + (v - ev) ** 2) / r ** 2)
    heat = np.clip(heat, 0.05, 1.0) * ship
    hh, ww = ship.shape[0] // S, ship.shape[1] // S
    cover = ship.reshape(hh, S, ww, S).mean(axis=(1, 3))
    val = heat.reshape(hh, S, ww, S).sum(axis=(1, 3)) / np.maximum(ship.reshape(hh, S, ww, S).sum(axis=(1, 3)), 1)
    small = np.zeros((h, w), np.uint8)
    small[1:1 + hh, 1:1 + ww] = np.where(cover >= 0.4, np.clip(val * 255, 1, 255), 0).astype(np.uint8)
    return small


# --- a cockpit ---

def changed(a, b):
    return (np.abs(a[..., :3].astype(np.int16) - b[..., :3].astype(np.int16)).max(-1) > 2)


def save_state(out_dir, name, base, pic):
    """a state as only the pixels it changes: (file, x, y) or None"""
    m = changed(base, pic)
    if not m.any():
        return None
    ys, xs = np.nonzero(m)
    x0, x1, y0, y1 = int(xs.min()), int(xs.max()) + 1, int(ys.min()), int(ys.max()) + 1
    rgba = pic[y0:y1, x0:x1].copy()
    rgba[..., 3] = np.where(m[y0:y1, x0:x1], 255, 0)
    Image.fromarray(rgba, 'RGBA').save(os.path.join(out_dir, 'states', name))
    return [name, x0, y0]


def rect_of(c):
    if 'rect' in c:
        return list(c['rect'])
    if 'knobs' in c:
        xs = [k[0] for k in c['knobs']]
        ys = [k[1] for k in c['knobs']]
        r = max(k[3] for k in c['knobs']) + 1
        return [int(min(xs) - r), int(min(ys) - r), int(max(xs) - min(xs) + 2 * r + 1), int(max(ys) - min(ys) + r + 4)]
    r = c.get('r', 6)
    return [int(c['at'][0] - r), int(c['at'][1] - r), int(2 * r + 1), int(2 * r + 1)]


def build(key, spec, out_root):
    out_dir = os.path.join(out_root, key)
    os.makedirs(os.path.join(out_dir, 'states'), exist_ok=True)
    for f in os.listdir(os.path.join(out_dir, 'states')):
        os.remove(os.path.join(out_dir, 'states', f))
    a = np.asarray(Image.open(os.path.join(ROOT, 'art', 'cockpits', key, 'base.png')).convert('RGBA')).copy()
    sock = os.path.join(ROOT, 'art', 'cockpits', key, 'stick', 'socket.json')
    if os.path.exists(sock):            # (the old stick's socket painted out: tools/joysticks.py)
        sj = json.load(open(sock))
        patch = np.asarray(Image.open(os.path.join(os.path.dirname(sock), 'socket.png')).convert('RGBA'))
        sub = a[sj['y']:sj['y'] + patch.shape[0], sj['x']:sj['x'] + patch.shape[1]]
        sub[patch[..., 3] > 0] = patch[patch[..., 3] > 0]
    controls = spec['controls']
    # the base: relabelled, scales recoloured, needles painted out
    for rl in spec.get('relabel', []):
        for box in rl.get('erase', []):
            erase_lettering(a, box, rl.get('dark', False))
    for c in controls:
        if c['kind'] == 'arcs':
            c['_mask'] = arc_mask(a, c['knobs'])
            recolour(a, c['_mask'], False)
        elif c['kind'] == 'needle' and 'painted' in c:
            erase_needle(a, c['at'][0], c['at'][1], c['r'], c['painted'], tail=c.get('tail', 2))
    for rl in spec.get('relabel', []):
        for lab in rl.get('labels', []):
            letter(a, *lab, dark=rl.get('dark', False))
    base = a
    out = []
    exclude = np.zeros(a.shape[:2], bool)
    for n, c in enumerate(controls):
        if c['kind'] in ('cabin',):
            continue
        rect = rect_of(c)
        exclude |= box_mask(a.shape, [rect[0] - 3, rect[1] - 3, rect[2] + 6, rect[3] + 6])
        rec = {'fn': c['fn'], 'kind': c['kind'], 'arg': c.get('arg', 0), 'rect': rect, 'painted': 0, 'states': [],
               'flags': 1 if c.get('dir') == 'v' else sum(1 for o in controls if o['fn'] == c['fn'])
               if c['fn'].endswith('_ladder') else 0}
        if c['kind'] == 'lamp':
            painted, other = lamp_states(base, c)
            rec['painted'] = painted
            rec['states'] = [save_state(out_dir, '%02d-0.png' % n, base, other) if painted else None,
                             None if painted else save_state(out_dir, '%02d-1.png' % n, base, other)]
        elif c['kind'] == 'knob':
            angles = DIGITS if c.get('digits') else c['angles']
            cx, cy = c['at']
            best = min(range(len(angles)), key=lambda i: abs(angles[i] - c['painted']))
            rec['painted'] = best
            rec['states'] = [None if i == best and abs(ang - c['painted']) < 1 else
                             save_state(out_dir, '%02d-%d.png' % (n, i), base,
                                        knob_at(base, cx, cy, c['r'], c['painted'], ang))
                             for i, ang in enumerate(angles)]
        elif c['kind'] == 'arcs':
            lit = base.copy()
            recolour(lit, c['_mask'], True)
            rec['states'] = [None, save_state(out_dir, '%02d-1.png' % n, base, lit)]
            ys, xs = np.nonzero(c['_mask'])
            rec['rect'] = [int(xs.min()), int(ys.min()), int(np.ptp(xs)) + 1, int(np.ptp(ys)) + 1]
        elif c['kind'] == 'needle':
            rec.update({k: c[k] for k in ('at', 'r', 'a0', 'a1', 'max') if k in c})
        elif c['kind'] == 'map':
            x, y, w, h = rect
            mp = survival_map(key, w, h) if c['fn'] == 'survival' else thermal_map(key, w, h)
            os.makedirs(os.path.join(out_dir, 'maps'), exist_ok=True)
            Image.fromarray(mp, 'L').save(os.path.join(out_dir, 'maps', '%02d.png' % n))
            rec['map'] = 'maps/%02d.png' % n
        elif c['kind'] == 'ticks':
            # the lit ticks, the bottom first: state k has the first k lit, the rest dark
            m = box_mask(base.shape, c['rect']) & (((lum(base) > 140) & (sat(base) > 0.1)) | (lum(base) > 190))
            lab, nt = ndimage.label(m)
            blobs = sorted((ndimage.center_of_mass(lab == i)[0], i) for i in range(1, nt + 1)
                           if (lab == i).sum() >= 2)
            blobs = [i for _y, i in reversed(blobs)]
            rec['painted'] = len(blobs)
            for k in range(len(blobs) + 1):
                dark = np.isin(lab, blobs[k:])
                rec['states'].append(save_state(out_dir, '%02d-%d.png' % (n, k), base, darken(base, dark))
                                     if k < len(blobs) else None)
        out.append(rec)
    # the running lights: every other lit lamp
    for n, c in enumerate(controls):
        if c['kind'] != 'cabin':
            continue
        for d in spec.get('displays', []):
            exclude |= box_mask(a.shape, d)
        m = cabin_mask(base, exclude)
        st = save_state(out_dir, '%02d-0.png' % n, base, darken(base, m))
        if st:
            ys, xs = np.nonzero(m)
            out.append({'fn': 'cabin', 'kind': 'lamp', 'arg': 0, 'painted': 1, 'states': [st, None], 'flags': 0,
                        'rect': [int(xs.min()), int(ys.min()), int(np.ptp(xs)) + 1, int(np.ptp(ys)) + 1]})
    Image.fromarray(base, 'RGBA').save(os.path.join(out_dir, 'base.png'))
    json.dump({'key': key, 'controls': out}, open(os.path.join(out_dir, 'panel.json'), 'w'), indent=1)
    return base, out


def preview(key, base, recs, out_root, path):
    """every control in each state, magnified, the base's own state first"""
    tiles = []
    for rec in recs:
        x, y, w, h = rec['rect']
        x, y = max(0, x - 3), max(0, y - 3)
        w, h = min(w + 6, 640 - x), min(h + 6, 400 - y)
        row = []
        for i in range(len(rec['states'])):
            pic = base.copy()
            st = rec['states'][i]
            if st:
                im = np.asarray(Image.open(os.path.join(out_root, key, 'states', st[0])))
                sub = pic[st[2]:st[2] + im.shape[0], st[1]:st[1] + im.shape[1]]
                sub[im[..., 3] > 0] = im[im[..., 3] > 0]
            row.append(Image.fromarray(np.ascontiguousarray(pic[y:y + h, x:x + w])).convert('RGB'))
        if row:
            tiles.append((rec, row))
    Z = 4
    width = max(sum(t.width * Z + 4 for t in row) for _r, row in tiles) + 220
    height = sum(max(t.height for t in row) * Z + 6 for _r, row in tiles)
    sheet = Image.new('RGB', (min(width, 4000), height), (18, 18, 22))
    d = ImageDraw.Draw(sheet)
    yy = 0
    for rec, row in tiles:
        d.text((4, yy + 2), '%s %s %d' % (rec['fn'], rec['kind'], rec['arg']), fill=(220, 220, 220))
        xx = 220
        for i, t in enumerate(row):
            sheet.paste(t.resize((t.width * Z, t.height * Z), Image.NEAREST), (xx, yy))
            if i == rec['painted']:
                d.rectangle([xx, yy, xx + t.width * Z - 1, yy + t.height * Z - 1], outline=(255, 255, 0))
            xx += t.width * Z + 4
        yy += max(t.height for t in row) * Z + 6
    os.makedirs(os.path.dirname(path), exist_ok=True)
    sheet.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('keys', nargs='*')
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'panels'))
    ap.add_argument('--preview')
    args = ap.parse_args()
    spec = json.load(open(os.path.join(ROOT, 'data', 'panels.json')))
    for key in args.keys or list(spec['cockpits']):
        s = spec['cockpits'][key]
        if 'controls' not in s:
            continue
        base, recs = build(key, s, args.out)
        n = sum(1 for r in recs for st in r['states'] if st)
        print('panels: %s: %d controls, %d state pictures' % (key, len(recs), n))
        if args.preview:
            preview(key, base, recs, args.out, os.path.join(args.preview, key + '.png'))


if __name__ == '__main__':
    main()
