#!/usr/bin/env python3
"""The carrier's rooms: painted layers -> the game's 640x400 layers, with the
player's fighter on the deck and the actor's loops; a preview GIF.

    places.py [KEY ...] [--ship REDEEMR] [--out art/places] [--gifs build/places]
    places.py deck --still docs/deck.png    every deck fighter on the pad, one sheet

data/places.json describes each room.  Its painted sources sit in
art/places/src/KEY/ (git-ignored):

    background         the room with no one in it (RGB)
    front              what stands in front of the fighter, transparent
                       elsewhere (RGBA), and its animation pack: animations.json
                       says where the actor's patch is and where each frame of
                       each clip sits in that clip's atlas (8 columns)

Everything is cropped to 16:10 (crop, in source pixels) and scaled to
640x400.  OUT/KEY/ holds:

    back.png             the room (RGB)
    front.png            the front layer at frame 0, hard alpha
    ACTOR-CLIP.png       that clip's frames: the patch rectangle, 8 to a row
    ship-SHIP.png        a fighter as it stands on the pad, hard alpha
    ship-SHIP-under.png  its shadow and its reflection in the deck: colour and
                         how much of it to blend over the room (soft alpha)
    place.json           where all of it goes, in screen pixels; the clips'
                         frame times; the launch's timings; the script

Draw order: back, under, sleds, ship, front with the current frame's patch
pasted in (a copy, transparent pixels included).  A sled is its foot on the
deck, the strut repeated up to the saddle, the saddle under the hull
(sled-*.png; place.json gives each ship's sleds as their middle and top).

The fighters are rendered by shipviews.py from the deck's own camera
(elevation, heading) and lit by the deck's warm lamps (light).  They share
one scale, Vega Strike's own sizes (the mesh times the unit's Unit_Scale)
at metre source pixels a metre, so a heavy fighter shows only its front
part: the nose stands at nose_x, the rest runs off behind the front layer
and off the screen, the belly sits on the deck at bottom.  ships is
a list of keys, or "player": every ship in data/ships.csv whose status
starts with player, so a new fighter only needs its row there and a re-run.
"""
import argparse
import csv
import json
import math
import os
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

SCREEN = (640, 400)
TICK = 50          # ms per frame of the launch
LAUNCH = {'spool_ms': 600, 'lift_ms': 400, 'lift_px': 4, 'run_ms': 900}
# the landing sleds, in screen pixels: how high they hold a ship's lowest
# point, their parts' sizes, where they stand along the hull that shows (front
# always, rear too past two_from pixels)
SLED = {'clearance': 12, 'foot': (56, 14), 'strut': 14, 'saddle': 44,
        'front': [0.72], 'rear': [0.2], 'two_from': 300}
RENDER = 1280      # the size the fighters are rendered at before scaling down


def places():
    with open(os.path.join(ROOT, 'data', 'places.json')) as f:
        return json.load(f)


def deck_ships(ships):
    if ships != 'player':
        return ships
    with open(os.path.join(ROOT, 'data', 'ships.csv')) as f:
        return [r['key'] for r in csv.DictReader(f) if r['status'].startswith('player') and r['mesh']]


def premul_resize(img, size, box=None):
    """RGBA resize without dark fringes: in premultiplied alpha."""
    return img.convert('RGBa').resize(size, Image.LANCZOS, box=box).convert('RGBA')


def hard(img):
    """Hard alpha, as the engine draws it: a pixel is in or out."""
    a = np.asarray(img).copy()
    inside = a[..., 3] >= 128
    a[..., 3] = inside * 255
    a[~inside, :3] = 0
    return Image.fromarray(a, 'RGBA')


class Frame:
    """Source pixels -> screen pixels for one room."""

    def __init__(self, crop):
        self.x0, self.y0, x1, y1 = crop
        self.k = SCREEN[0] / (x1 - self.x0)
        if abs((y1 - self.y0) * self.k - SCREEN[1]) > 0.5:
            sys.exit('crop %r is not 16:10' % (crop,))
        self.box = tuple(crop)

    def xy(self, x, y):
        return (x - self.x0) * self.k, (y - self.y0) * self.k

    def image(self, img):
        if img.mode == 'RGBA':
            return premul_resize(img, SCREEN, self.box)
        return img.convert('RGB').resize(SCREEN, Image.LANCZOS, box=self.box)


def actor_frames(src, front_cfg, frame):
    """The front layer with each clip's frames pasted in, at screen size.
    Returns the patch rectangle on screen and {clip: ([RGBA patch], [ms])}."""
    with open(os.path.join(src, front_cfg['animations'])) as f:
        anim = json.load(f)
    base = Image.open(os.path.join(src, front_cfg['file'])).convert('RGBA')
    p = anim['patch']
    # the screen rectangle the patch can touch: its source rectangle mapped,
    # widened by the resampling filter's reach
    x0, y0 = frame.xy(p['x'], p['y'])
    x1, y1 = frame.xy(p['x'] + p['width'], p['y'] + p['height'])
    rect = [max(0, math.floor(x0) - 3), max(0, math.floor(y0) - 3),
            min(SCREEN[0], math.ceil(x1) + 3), min(SCREEN[1], math.ceil(y1) + 3)]
    first = None
    clips = {}
    for clip in anim['clips']:
        atlas = Image.open(os.path.join(src, clip['atlas'])).convert('RGBA')
        pics, times = [], []
        for r in clip['frameRects']:
            layer = base.copy()
            cell = atlas.crop((r['x'], r['y'], r['x'] + r['width'], r['y'] + r['height']))
            layer.paste(cell, (p['x'], p['y']))   # a copy: transparent pixels too
            small = hard(frame.image(layer))
            if first is None:
                first = small
            else:
                d = np.any(np.asarray(small) != np.asarray(first), axis=2)
                d[rect[1]:rect[3], rect[0]:rect[2]] = False
                if d.any():
                    sys.exit('%s changes the front outside the patch' % clip['name'])
            pics.append(small.crop(rect))
            times.append(r.get('durationMs', clip['frameMs']))
        clips[clip['name']] = (pics, times)
    return first, rect, clips


def atlas(pics, columns=8):
    w, h = pics[0].size
    rows = (len(pics) + columns - 1) // columns
    out = Image.new('RGBA', (w * columns, h * rows), (0, 0, 0, 0))
    for i, im in enumerate(pics):
        out.paste(im, ((i % columns) * w, (i // columns) * h))
    return out


def render_views(ship, cfg, cache, rerender=False):
    """The fighter from the deck's camera and from its mirror image under the deck."""
    e, h = cfg['elevation'], cfg['heading']
    import shipviews
    row = shipviews.ship_row(ship)
    art = shipviews.table_row('art.csv', ship) or {}
    tag = '%s-%s' % (ship, art.get('livery') or 'none')
    names = ['%s_e%g_h%g' % (tag, el, h) for el in (e, -e)]
    stale = not all(os.path.exists(os.path.join(cache, n + '.json')) for n in names)
    if not stale:
        with open(os.path.join(cache, names[0] + '.json')) as f:
            stale = json.load(f)['size'] != RENDER
    if rerender or stale:
        key = tuple(float(v) for v in cfg['key_light'].split(','))
        shipviews.render(row, cache, RENDER, os.path.expanduser('~/dev/vegastrike-assets'), key=key,
                         views=[(e, h), (-e, h)])
    above, below = (Image.open(os.path.join(cache, n + '.png')).convert('RGBA') for n in names)
    with open(os.path.join(cache, names[0] + '.json')) as f:
        info = json.load(f)
    info['metres'] = info['radius'] * unit_scale(row['vs_unit']) / (RENDER / 2)   # a rendered pixel
    return above, below, info


_SCALES = {}


def unit_scale(vs_unit):
    """Vega Strike's Unit_Scale for a unit: its mesh units -> metres."""
    if not _SCALES:
        with open(os.path.join(os.path.expanduser('~/dev/vegastrike-assets'), 'units', 'units.csv')) as f:
            rows = list(csv.reader(f))
        col = rows[0].index('Unit_Scale')
        _SCALES.update((r[0], float(r[col] or 1)) for r in rows[2:] if r and len(r) > col)
    return _SCALES.get(vs_unit, 1.0)


def lit(img, light):
    a = np.asarray(img).astype(np.float32)
    a[..., :3] *= np.array(light, np.float32)
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), 'RGBA')


def fighter(ship, cfg, frame, cache, cover, rerender=False):
    """A fighter on the pad: sprite, position, under layer, engine points, and
    the sleds it stands on."""
    above, below, info = render_views(ship, cfg, cache, rerender)
    bb = above.getbbox()
    s = cfg['metre'] * frame.k * info['metres']
    size = (round((bb[2] - bb[0]) * s), round((bb[3] - bb[1]) * s))
    sprite = hard(lit(premul_resize(above.crop(bb), size), cfg['light']))
    nose, deck = frame.xy(cfg['nose_x'], cfg['bottom'])
    deck = round(deck)
    # every ship rides on sleds, its lowest point a little above the deck
    x, y = round(nose - size[0]), round(deck - SLED['clearance'] - size[1])
    # the ship's centre on the screen: the renders are centred on it
    cx, cy = x + (RENDER / 2 - bb[0]) * s, y + (RENDER / 2 - bb[1]) * s
    engines = nozzles(sprite, [(ey - bb[1]) * s for _ex, ey, _r in info['engines']])
    under, ux, uy = under_layer(below, s, x, size[0], cx, cy, deck, cfg['light'])
    return {'sprite': sprite, 'x': x, 'y': y, 'under': under, 'ux': ux, 'uy': uy, 'engines': engines,
            'sleds': sled_spots(sprite, x, y, deck, cover)}


def covered(cover, x, y, h, w):
    """Which pixels of an h x w picture at (x, y) nobody sees: off the screen,
    or behind the front layer."""
    out = np.ones((h, w), bool)
    win = window(cover, x, y, h, w)
    if win:
        d, s = win
        out[s] = cover[d]
    return out


def sled_spots(sprite, x, y, deck, cover):
    """Where a fighter's sleds stand: one under the front of the longest
    stretch of belly that shows, one under its rear too when that stretch is
    long enough.  Each is
    [x of its middle, y of its top]; its top meets the belly, the lowest point
    under the saddle, and its foot stands on the deck."""
    a = np.asarray(sprite)[..., 3] > 0
    h, w = a.shape
    hid = covered(cover, x, y, h, w)
    has = a.any(axis=0)
    belly = np.where(has, h - 1 - np.argmax(a[::-1], axis=0), -1)
    shows = np.array([has[c] and not hid[belly[c], c] for c in range(w)])
    # the belly that shows: the longest run of such columns, small gaps closed
    runs, start = [], None
    shows = ndimage.binary_closing(np.pad(shows, 8), np.ones(9, bool))[8:-8] & has
    for c in range(w + 1):
        if c < w and shows[c]:
            start = c if start is None else start
        elif start is not None:
            runs.append((start, c - 1))
            start = None
    if not runs:
        return []
    left, right = max(runs, key=lambda r: r[1] - r[0])
    spots = []
    half = SLED['saddle'] // 2
    for f in SLED['front'] if right - left < SLED['two_from'] else SLED['front'] + SLED['rear']:
        c = int(left + f * (right - left))
        band = [i for i in range(c - half, c + half + 1) if 0 <= i < w and shows[i]]
        if band:
            spots.append([int(x + c), int(y + max(belly[i] for i in band) + 1)])
    return spots


def sled_parts(light, src=None):
    """The standard landing sled, in three parts the engine stacks: the foot
    on the deck, the strut repeated to the height it needs, the saddle under
    the hull.  From painted art (SRC/sled.png, side on, transparent or white
    around it) when there is some, else a drawn placeholder."""
    path = os.path.join(src, 'sled.png') if src else None
    if path and os.path.exists(path):
        return painted_sled(path, light)
    def shade(w, h, lo, hi, axis):
        t = np.linspace(0, 1, w if axis == 1 else h, dtype=np.float32)
        ramp = np.clip(1 - np.abs(t - 0.32) / 0.68, 0, 1) ** 0.8          # a highlight left of middle
        c = np.array(lo, np.float32) + (np.array(hi, np.float32) - lo) * ramp[:, None]
        return np.broadcast_to(c[None] if axis == 1 else c[:, None], (h, w, 3)).copy()

    def outline(rgba):
        a = rgba[..., 3] > 0
        edge = a & ~ndimage.binary_erosion(a)
        rgba[edge, :3] = (24, 22, 20)
        return rgba

    ink = (24, 22, 20)
    # the foot: a low wedge of gunmetal with a hazard band
    fw, fh = SLED['foot']
    foot = np.zeros((fh, fw, 4), np.float32)
    rgb = shade(fw, fh, (52, 54, 56), (118, 118, 116), 1)
    yy, xx = np.mgrid[0:fh, 0:fw]
    inset = (fh - 1 - yy) * 0.6
    inside = (xx >= inset) & (xx <= fw - 1 - inset)
    band = (yy >= fh // 3) & (yy < fh // 3 + 4)
    stripe = ((xx + yy) // 3) % 2 == 0
    rgb[band & stripe] = (206, 160, 40)
    rgb[band & ~stripe] = (30, 28, 26)
    foot[..., :3], foot[..., 3] = rgb, inside * 255.0
    foot = outline(foot)
    # the strut: a polished hydraulic ram, one row high
    sw = SLED['strut']
    strut = np.zeros((1, sw, 4), np.float32)
    strut[..., :3] = shade(sw, 1, (60, 60, 62), (196, 192, 184), 1)
    strut[..., 3] = 255
    strut[0, 0, :3] = strut[0, -1, :3] = ink
    # the saddle: a padded olive cradle
    cw, ch = SLED['saddle'], 8
    saddle = np.zeros((ch, cw, 4), np.float32)
    saddle[..., :3] = shade(cw, ch, (40, 44, 30), (104, 110, 74), 0)[::-1]
    yy, xx = np.mgrid[0:ch, 0:cw]
    saddle[..., 3] = ((xx >= (ch - 1 - yy) * 0.8 * 0) & (np.abs(xx - (cw - 1) / 2) <= (cw / 2 - (yy == ch - 1) * 2))) * 255.0
    saddle = outline(saddle)
    return [Image.fromarray(np.clip(lit_arr(p, light), 0, 255).astype(np.uint8), 'RGBA')
            for p in (foot, strut, saddle)]


def painted_sled(path, light):
    """Slice a painted sled: the strut is the longest run of rows at its
    narrowest width; the rows below are the foot, those above the saddle."""
    img = Image.open(path).convert('RGBA')
    a = np.asarray(img)
    if a[..., 3].min() == 255:                       # no alpha: key out the white around it
        white = np.all(a[..., :3] > 235, axis=2)
        lab, _ = ndimage.label(white)
        edge = set(lab[0]) | set(lab[-1]) | set(lab[:, 0]) | set(lab[:, -1])
        a = a.copy()
        a[..., 3] = np.where(np.isin(lab, [l for l in edge if l]), 0, 255)
        img = Image.fromarray(a, 'RGBA')
    img = img.crop(img.getbbox())
    w = SLED['foot'][0]
    img = premul_resize(img, (w, round(img.size[1] * w / img.size[0])))
    a = np.asarray(img)[..., 3] >= 128
    widths = a.sum(axis=1)
    narrow = (widths > 0) & (widths <= widths[widths > 0].min() + 2)
    runs, start = [], None
    for y in range(len(narrow) + 1):
        if y < len(narrow) and narrow[y]:
            start = y if start is None else start
        elif start is not None:
            runs.append((start, y))
            start = None
    top, bottom = max(runs, key=lambda r: r[1] - r[0])
    mid = (top + bottom) // 2
    W, H = img.size
    parts = [img.crop((0, bottom, W, H)), img.crop((0, mid, W, mid + 1)), img.crop((0, 0, W, top))]
    return [hard(lit(p, [c / max(light) for c in light])) for p in parts]


def lit_arr(a, light):
    a = a.copy()
    a[..., :3] *= np.array(light, np.float32) / max(light)
    return a


def sled(parts, height):
    """A sled height pixels tall, foot to saddle, as one picture."""
    foot, strut, saddle = parts
    w = max(p.size[0] for p in parts)
    img = Image.new('RGBA', (w, max(height, foot.size[1] + saddle.size[1])), (0, 0, 0, 0))
    H = img.size[1]
    for yy in range(saddle.size[1], H - foot.size[1] + 2):
        img.alpha_composite(strut, ((w - strut.size[0]) // 2, yy))
    img.alpha_composite(foot, ((w - foot.size[0]) // 2, H - foot.size[1]))
    img.alpha_composite(saddle, ((w - saddle.size[0]) // 2, 0))
    return img


def nozzles(sprite, ys):
    """Where the exhaust leaves the hull: the engines' heights, at the tail
    (the hull's last pixel on that row; the deck's fighters point right).
    Vega Strike puts the engine lights inside the hull, so their own x is no use."""
    a = np.asarray(sprite)[..., 3] > 0
    out = []
    for y in ys:
        y = int(round(min(max(y, 0), a.shape[0] - 1)))
        band = a[max(0, y - 2):y + 3].any(axis=0)
        if not band.any():
            continue
        pt = [int(np.argmax(band)), y]
        if all(abs(pt[1] - q[1]) > 3 or abs(pt[0] - q[0]) > 3 for q in out):
            out.append(pt)
    return out


def under_layer(below, s, x, w, cx, cy, deck, light):
    """The fighter's shadow and its reflection in the polished deck, as one
    soft-alpha picture to blend over the room.  The reflection is the view
    from the mirrored camera, flipped, its centre mirrored in the deck."""
    bb = below.getbbox()
    rsize = (round((bb[2] - bb[0]) * s), round((bb[3] - bb[1]) * s))
    refl = lit(premul_resize(below.crop(bb), rsize), light).transpose(Image.FLIP_TOP_BOTTOM)
    rx = round(cx - (RENDER / 2 - bb[0]) * s)
    ry = round(2 * deck - cy - (bb[3] - RENDER / 2) * s)
    sh = max(2, round(0.04 * w))                # the shadow's half height
    pad = 8
    X0, X1 = min(x, rx) - pad, max(x + w, rx + rsize[0]) + pad
    Y0, Y1 = deck - sh - pad, min(ry + rsize[1], SCREEN[1] + 16) + pad
    W, H = X1 - X0, Y1 - Y0
    r = np.zeros((H, W, 4), np.float32)
    win = window(r, rx - X0, ry - Y0, rsize[1], rsize[0])
    if win:
        d, src = win
        r[d] = (np.asarray(refl).astype(np.float32) / 255)[src]
    # the reflection: only on the deck, smeared down it like the lamps' reflections,
    # fading with distance
    yy = np.arange(H, dtype=np.float32)[:, None] + Y0 - deck
    r[yy[:, 0] < 0] = 0
    a = r[..., 3:4].copy()
    pre = np.dstack([r[..., :3] * a, a])
    pre = np.stack([ndimage.gaussian_filter(pre[..., c], (2.0, 0.6)) for c in range(4)], -1)
    reach = max(ry + rsize[1] - deck, 1)
    fade = 0.32 * np.clip(1 - yy / reach, 0, 1) ** 1.5 * (yy >= 0)
    ra = pre[..., 3] * fade
    rc = pre[..., :3] / np.maximum(pre[..., 3:4], 1e-6)
    # the shadow: a flat soft ellipse under the hull
    xx = np.arange(W, dtype=np.float32)[None, :] + X0
    e = ((xx - (x + w / 2)) / (0.47 * w)) ** 2 + ((yy - 1) / sh) ** 2
    sa = ndimage.gaussian_filter((e <= 1).astype(np.float32), 3.0) * 0.5
    # shadow first, reflection over it, as one blend
    alpha = 1 - (1 - sa) * (1 - ra)
    colour = rc * (ra / np.maximum(alpha, 1e-6))[..., None]
    out = np.dstack([np.clip(colour * 255, 0, 255), np.clip(alpha * 255, 0, 255)]).astype(np.uint8)
    img = Image.fromarray(out, 'RGBA')
    bb = img.getbbox()
    return img.crop(bb), X0 + bb[0], Y0 + bb[1]


def flame(length, height):
    """An exhaust plume pointing left from its right end, (length, height/2):
    a white-hot core, an amber body and a glow around it, added to the screen."""
    L, H = max(4, int(length)), max(5, int(height))
    pad = H // 2
    xx = np.arange(L, dtype=np.float32)[None, :] / L          # 0 = far end, 1 = nozzle
    yy = (np.arange(H, dtype=np.float32)[:, None] - (H - 1) / 2) / (H / 2)
    width = 0.3 + 0.7 * xx
    body = np.clip(1 - np.abs(yy) / width, 0, 1) ** 0.7 * xx ** 1.2
    core = np.clip(1 - np.abs(yy) / (0.4 * width), 0, 1) * xx ** 3
    rgb = body[..., None] * np.array([1.0, 0.5, 0.12]) + core[..., None] * np.array([0.9, 0.9, 0.8])
    out = np.zeros((H + 2 * pad, L + pad, 3), np.float32)
    out[pad:pad + H, :L] = rgb
    glow = np.stack([ndimage.gaussian_filter(out[..., c], pad / 1.5) for c in range(3)], -1)
    return np.clip(out + glow * 0.8, 0, 1)


def compose(back, front, rect, patch, ship=None, dx=0.0, dy=0.0, burn=0.0):
    """One screen: back, the fighter's under layer, its sleds, its exhaust
    (burn 0..1) and sprite (moved by dx, dy), the front with the patch."""
    img = np.asarray(back).astype(np.float32) / 255
    if ship:
        lift = max(0.0, -dy)
        u = np.asarray(ship['under']).astype(np.float32) / 255
        ua = u[..., 3:4] * max(0.0, 1 - lift / 12)
        paste_blend(img, u[..., :3], ua, ship['ux'] + dx, ship['uy'] + lift)
        for sx, top in ship['sleds']:
            sl = np.asarray(sled(ship['parts'], ship['deck'] - top)).astype(np.float32) / 255
            paste_blend(img, sl[..., :3], sl[..., 3:4], sx - sl.shape[1] // 2, ship['deck'] - sl.shape[0])
        if burn > 0:
            h = ship['sprite'].size[1]
            for ex, ey in ship['engines']:
                fl = flame(h * 0.4 + burn * h * 2.2, max(6, h * 0.22 * min(1, 0.5 + burn)))
                fx = ship['x'] + dx + ex - fl.shape[1] + 4 + fl.shape[0] // 4
                fy = ship['y'] + dy + ey - fl.shape[0] / 2
                paste_add(img, fl * min(1.0, burn * 2), fx, fy)
        s = np.asarray(ship['sprite']).astype(np.float32) / 255
        paste_blend(img, s[..., :3], s[..., 3:4], ship['x'] + dx, ship['y'] + dy)
    # the front at this frame: the patch is a copy into it, transparent pixels too
    f = np.asarray(front).astype(np.float32) / 255
    if patch is not None:
        f = f.copy()
        f[rect[1]:rect[3], rect[0]:rect[2]] = np.asarray(patch).astype(np.float32) / 255
    paste_blend(img, f[..., :3], f[..., 3:4], 0, 0)
    return Image.fromarray(np.clip(img * 255 + 0.5, 0, 255).astype(np.uint8), 'RGB')


def window(img, x, y, h, w):
    """The overlap of an h x w picture at (x, y) with img: slices for both."""
    x, y = int(round(x)), int(round(y))
    X0, Y0 = max(0, x), max(0, y)
    X1, Y1 = min(img.shape[1], x + w), min(img.shape[0], y + h)
    if X0 >= X1 or Y0 >= Y1:
        return None
    return (slice(Y0, Y1), slice(X0, X1)), (slice(Y0 - y, Y1 - y), slice(X0 - x, X1 - x))


def paste_blend(img, rgb, a, x, y):
    win = window(img, x, y, *rgb.shape[:2])
    if win:
        d, s = win
        img[d] = img[d] * (1 - a[s]) + rgb[s] * a[s]


def paste_add(img, rgb, x, y):
    win = window(img, x, y, *rgb.shape[:2])
    if win:
        d, s = win
        img[d] = np.minimum(1.0, img[d] + rgb[s])


def build(key, cfg, out, cache, rerender=False, log=print):
    src = os.path.join(ROOT, cfg['src'])
    frame = Frame(cfg['crop'])
    back = frame.image(Image.open(os.path.join(src, cfg['background'])))
    front, rect, clips = actor_frames(src, cfg['front'], frame)
    od = os.path.join(out, key)
    os.makedirs(od, exist_ok=True)
    back.save(os.path.join(od, 'back.png'))
    front.save(os.path.join(od, 'front.png'))
    actor = cfg['front']['actor']
    place = {'size': list(SCREEN), 'back': 'back.png', 'front': 'front.png',
             'actor': {'name': actor, 'patch': [rect[0], rect[1], rect[2] - rect[0], rect[3] - rect[1]],
                       'clips': {}},
             'script': cfg.get('script', [])}
    for name, (pics, times) in clips.items():
        fn = '%s-%s.png' % (actor, name)
        atlas(pics).save(os.path.join(od, fn))
        place['actor']['clips'][name] = {'atlas': fn, 'columns': 8, 'frames': len(pics), 'ms': times}
    ships = {}
    if 'fighter' in cfg:
        fc = cfg['fighter']
        parts = sled_parts(fc['light'], src)
        for name, part in zip(('foot', 'strut', 'saddle'), parts):
            part.save(os.path.join(od, 'sled-%s.png' % name))
        place['fighter'] = {'deck': round(frame.xy(0, fc['bottom'])[1]), 'ships': {},
                            'sled': {'foot': 'sled-foot.png', 'strut': 'sled-strut.png',
                                     'saddle': 'sled-saddle.png'}}
        place['launch'] = dict(LAUNCH)
        # where the front layer hides what is behind it, in every frame
        cover = np.asarray(front)[..., 3] > 0
        for pics, _ms in clips.values():
            for pic in pics:
                cover[rect[1]:rect[3], rect[0]:rect[2]] &= np.asarray(pic)[..., 3] > 0
        for ship in deck_ships(fc['ships']):
            sh = fighter(ship, fc, frame, cache, cover, rerender)
            sh['parts'], sh['deck'] = parts, place['fighter']['deck']
            ships[ship] = sh
            sh['sprite'].save(os.path.join(od, 'ship-%s.png' % ship))
            sh['under'].save(os.path.join(od, 'ship-%s-under.png' % ship))
            place['fighter']['ships'][ship] = {
                'sprite': 'ship-%s.png' % ship, 'x': sh['x'], 'y': sh['y'],
                'under': 'ship-%s-under.png' % ship, 'ux': sh['ux'], 'uy': sh['uy'],
                'engines': sh['engines'], 'sleds': sh['sleds']}
            log('  %s %dx%d at %d,%d' % (ship, *sh['sprite'].size, sh['x'], sh['y']))
    with open(os.path.join(od, 'place.json'), 'w') as f:
        json.dump(place, f, indent=1)
    return back, front, rect, clips, ships, place


def ease(t):
    return t * t * (3 - 2 * t)


def play(back, front, rect, clips, ship, script):
    """The script as (picture, ms) pairs: clips by name, and 'launch'."""
    out = []
    idle = clips.get('idle') or next(iter(clips.values()))
    for step in script:
        if step[0] == 'launch':
            if not ship:
                continue
            n_spool = LAUNCH['spool_ms'] // TICK
            n_lift = LAUNCH['lift_ms'] // TICK
            n_run = LAUNCH['run_ms'] // TICK
            # it must clear the screen's right edge, exhaust and all, by the end of the run
            dist = SCREEN[0] - ship['x'] + 2 * ship['sprite'].size[1]
            k = 0
            for i in range(n_spool + n_lift + n_run):
                pic = idle[0][k % len(idle[0])]
                k += 1
                if i < n_spool:
                    dx, dy, burn = 0, 0, 0.35 * ease((i + 1) / n_spool)
                elif i < n_spool + n_lift:
                    t = (i - n_spool + 1) / n_lift
                    dx, dy, burn = 0, -LAUNCH['lift_px'] * ease(t), 0.35
                else:
                    t = (i - n_spool - n_lift + 1) / n_run
                    dx, dy, burn = dist * t * t, -LAUNCH['lift_px'], 0.35 + 0.65 * t
                out.append((compose(back, front, rect, pic, ship, dx, dy, burn), TICK))
            ship = None   # it has gone
            continue
        name, times = step
        pics, ms = clips[name]
        for _ in range(times):
            for pic, t in zip(pics, ms):
                out.append((compose(back, front, rect, pic, ship), t))
    return out


def save_gif(frames, path):
    """One palette for the whole GIF, as the game will have: 256 colours, no dither."""
    sample = Image.new('RGB', (SCREEN[0], SCREEN[1] * 4))
    for i, j in enumerate(np.linspace(0, len(frames) - 1, 4).astype(int)):
        sample.paste(frames[j][0], (0, i * SCREEN[1]))
    pal = sample.quantize(256, method=Image.Quantize.MEDIANCUT)
    pics = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f, _ in frames]
    os.makedirs(os.path.dirname(path), exist_ok=True)
    pics[0].save(path, save_all=True, append_images=pics[1:], duration=[t for _, t in frames],
                 loop=0, optimize=False, disposal=1)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('keys', nargs='*')
    ap.add_argument('--ship', default=None, help='the fighter in the preview (default: the first)')
    ap.add_argument('--out', default=os.path.join(ROOT, 'art', 'places'))
    ap.add_argument('--gifs', default=os.path.join(ROOT, 'build', 'places'))
    ap.add_argument('--cache', default=os.path.join(ROOT, 'build', 'places', 'ships'))
    ap.add_argument('--rerender', action='store_true', help='render the fighters again')
    ap.add_argument('--still', default=None, help='a sheet of every fighter on the pad')
    args = ap.parse_args()
    table = places()
    for key in args.keys or list(table):
        cfg = table[key]
        print(key)
        back, front, rect, clips, ships, place = build(key, cfg, args.out, args.cache, args.rerender)
        first = clips[cfg['script'][0][0]][0][0] if cfg.get('script') else None
        if args.still and ships:
            cols = 3
            rows = (len(ships) + cols - 1) // cols
            sheet = Image.new('RGB', (SCREEN[0] * cols, SCREEN[1] * rows))
            for i, sh in enumerate(ships.values()):
                sheet.paste(compose(back, front, rect, first, sh), ((i % cols) * SCREEN[0], (i // cols) * SCREEN[1]))
            sheet.quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).save(args.still)
            print('  ' + args.still)
        pick = args.ship or cfg.get('fighter', {}).get('preview')
        ship = ships.get(pick) if pick else next(iter(ships.values()), None)
        frames = play(back, front, rect, clips, ship, cfg.get('script') or [[next(iter(clips)), 1]])
        path = os.path.join(args.gifs, '%s.gif' % key)
        save_gif(frames, path)
        print('  %s: %d frames, %.1f s' % (path, len(frames), sum(t for _, t in frames) / 1000))


if __name__ == '__main__':
    main()
