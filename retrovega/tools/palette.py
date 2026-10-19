#!/usr/bin/env python3
"""Build RetroVega's flight palette from the rendered roster.

    palette.py [--sprites build/sprites] [--out build]  ->  OUT/flight.pal, OUT/palette.json, docs/palette.png

The layout is spec section 9's.  Fixed ramps are generated; the rest comes
from the rendered sprites (render_all.py) and their paint masks:

  hull block (16-63, 160-207)  96 colours by median cut over every ship's hull pixels
  livery primary (64-71)       the default livery's primary in 8 lighting shades; the
  livery secondary (72-79)     shades are the quantiles of how lit the painted pixels
                               are (their brightness over the livery colour's)
  glass (80-87)                the canopy pixels by brightness
  livery targets (88-127)      five more colours in the same shades, so a livery swap is
                               a remap of 64-79 at decode time

flight.pal is 768 bytes of 6-bit VGA values; palette.json holds the ranges,
the shades and each livery's ramps for the packer and the engine.
"""
import argparse
import csv
import json
import os

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
HULL = list(range(16, 64)) + list(range(160, 208))
PRIMARY, SECONDARY, GLASS, TARGETS = 64, 72, 80, 88
FIRE, GLOW = 128, 144
SCENE, STARS, SKIN = 208, 224, 240
DEFAULT_LIVERY = 'carrier-green'


def lin(c):
    return (np.asarray(c, dtype=np.float64) / 255.0) ** 2.2


def srgb(c):
    return np.clip(np.round(np.clip(c, 0, 1) ** (1 / 2.2) * 255), 0, 255)


def luma(c):
    return np.asarray(c) @ np.array([0.2126, 0.7152, 0.0722])


def ramp(a, b, n):
    """n colours from a to b (sRGB), evenly in linear light."""
    a, b = lin(a), lin(b)
    return [srgb(a + (b - a) * t) for t in np.linspace(0, 1, n)]


def table(name, key='key'):
    return {r[key]: r for r in csv.DictReader(open(os.path.join(HERE, '..', 'data', name)))}


def rgb_of(text):
    return tuple(int(v) for v in text.split()) if text.strip() else None


def collect(sprites):
    """Pixels by class over every ship's views: hull, glass, and each
    painted pixel's lighting factor (its luminance over its livery colour's)."""
    ships = [r for r in csv.DictReader(open(os.path.join(HERE, '..', 'data', 'ships.csv'))) if r['mesh']]
    art, liveries = table('art.csv'), table('liveries.csv', 'name')
    hull, glass, f1, f2 = [], [], [], []
    rng = np.random.default_rng(1)
    for r in ships:
        livery = (art.get(r['key']) or {}).get('livery') or 'none'
        liv = liveries[livery]
        d = os.path.join(sprites, r['key'])
        for v in range(62):
            p = os.path.join(d, '%s-%s_%02d.png' % (r['key'], livery, v))
            if not os.path.exists(p):
                break
            px = np.asarray(Image.open(p)).reshape(-1, 4)
            mk = np.asarray(Image.open(os.path.join(d, '%s-%s_paint_%02d.png' % (r['key'], livery, v)))).reshape(-1)
            on = px[:, 3] > 0
            h = px[on & (mk == 0), :3]
            if len(h) > 3000:
                h = h[rng.choice(len(h), 3000, replace=False)]
            hull.append(h)
            glass.append(px[on & (mk == 3), :3])
            for m, out, col in ((1, f1, liv['primary']), (2, f2, liv['secondary'])):
                c = rgb_of(col)
                if c:
                    out.append(luma(lin(px[on & (mk == m), :3])) / max(luma(lin(c)), 1e-6))
    cat = lambda xs, w: np.concatenate(xs) if xs else np.zeros((0,) + w)  # noqa: E731
    return cat(hull, (3,)), cat(glass, (3,)), cat(f1, ()), cat(f2, ())


def shades(f, n=8):
    """n lighting factors: the means of n equal quantile bins of f."""
    if len(f) < n:
        return list(np.linspace(0.2, 1.2, n))
    q = np.quantile(f, np.linspace(0, 1, n + 1))
    return [float(f[(f >= q[i]) & (f <= q[i + 1])].mean()) for i in range(n)]


def build(sprites):
    hull, glass, f1, f2 = collect(sprites)
    pal = np.zeros((256, 3))
    # UI: greys and the HUD's colours
    ui = ramp((40, 40, 48), (200, 200, 210), 6) + [(0, 200, 0), (0, 110, 0), (230, 180, 0), (220, 40, 30),
                                                   (60, 120, 240), (0, 200, 220), (240, 240, 240), (120, 0, 0),
                                                   (255, 120, 0)]
    pal[1:16] = ui
    # hull block: median cut
    img = Image.fromarray(hull.reshape(1, -1, 3).astype(np.uint8))
    q = img.quantize(colors=len(HULL), method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    cols = np.array(q.getpalette()[:3 * len(HULL)]).reshape(-1, 3)
    cols = cols[np.argsort(luma(lin(cols)))]
    pal[HULL] = cols
    # liveries
    liveries = table('liveries.csv', 'name')
    s1, s2 = shades(f1), shades(f2)
    default = liveries[DEFAULT_LIVERY]
    pal[PRIMARY:PRIMARY + 8] = [srgb(lin(rgb_of(default['primary'])) * f) for f in s1]
    pal[SECONDARY:SECONDARY + 8] = [srgb(lin(rgb_of(default['secondary'])) * f) for f in s2]
    # glass: by brightness
    if len(glass):
        g = glass[np.argsort(luma(lin(glass)))]
        pal[GLASS:GLASS + 8] = [g[len(g) * i // 8:len(g) * (i + 1) // 8].mean(0) for i in range(8)]
    else:
        pal[GLASS:GLASS + 8] = ramp((6, 10, 26), (150, 175, 220), 8)
    # target ramps: every other livery colour, primaries first, near-duplicates shared
    ramps = {'primary': {}, 'secondary': {}}
    base_of = {rgb_of(default['primary']): PRIMARY, rgb_of(default['secondary']): SECONDARY}
    nxt = TARGETS
    worn = [(table('art.csv').get(k) or {}).get('livery') for k in table('art.csv')]
    order = sorted(liveries, key=lambda n: (n not in worn, list(liveries).index(n)))
    for name in order:              # liveries a ship wears first; the others take what room is left
        liv = liveries[name]
        for role, sh in (('primary', s1), ('secondary', s2)):
            c = rgb_of(liv[role])
            if not c:
                continue
            near = [b for k, b in base_of.items() if np.abs(np.array(k) - c).max() <= 24]
            if near:
                ramps[role][name] = near[0]
                continue
            if nxt >= TARGETS + 40:
                ramps[role][name] = None      # no room: this livery keeps the default colour here
                continue
            pal[nxt:nxt + 8] = [srgb(lin(c) * f) for f in sh]
            base_of[c] = nxt
            ramps[role][name] = nxt
            nxt += 8
    # effects: fire, then engine glows (Terran orange, Aera cyan, Rlaan magenta)
    pal[FIRE:FIRE + 16] = ramp((40, 0, 0), (200, 30, 0), 6) + ramp((230, 90, 0), (255, 220, 60), 6) + \
        ramp((255, 235, 140), (255, 255, 255), 4)
    pal[GLOW:GLOW + 8] = ramp((90, 40, 0), (255, 230, 170), 8)
    pal[GLOW + 8:GLOW + 12] = ramp((0, 80, 70), (200, 255, 240), 4)
    pal[GLOW + 12:GLOW + 16] = ramp((80, 0, 80), (255, 180, 255), 4)
    pal[SCENE:SCENE + 16] = ramp((16, 16, 20), (180, 176, 168), 16)
    pal[STARS:STARS + 16] = ramp((20, 22, 40), (230, 235, 255), 16)
    pal[SKIN:SKIN + 15] = ramp((60, 30, 20), (250, 210, 180), 15)
    pal[255] = (255, 255, 255)
    meta = {
        'hull': HULL, 'primary': PRIMARY, 'secondary': SECONDARY, 'glass': GLASS, 'fire': FIRE,
        'glow': GLOW, 'shades_primary': s1, 'shades_secondary': s2, 'default_livery': DEFAULT_LIVERY,
        'livery_ramps': {name: {'primary': ramps['primary'].get(name), 'secondary': ramps['secondary'].get(name)}
                         for name in liveries},
    }
    return pal.astype(np.uint8), meta


def swatch(pal, path):
    img = Image.new('RGB', (16 * 24, 16 * 24))
    for i, c in enumerate(pal):
        img.paste(tuple(int(v) for v in c), ((i % 16) * 24, (i // 16) * 24, (i % 16) * 24 + 23, (i // 16) * 24 + 23))
    img.save(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--sprites', default=os.path.join(HERE, '..', 'build', 'sprites'))
    ap.add_argument('--out', default=os.path.join(HERE, '..', 'build'))
    ap.add_argument('--swatch', default=os.path.join(HERE, '..', 'docs', 'palette.png'))
    args = ap.parse_args()
    pal, meta = build(args.sprites)
    os.makedirs(args.out, exist_ok=True)
    open(os.path.join(args.out, 'flight.pal'), 'wb').write((pal >> 2).astype(np.uint8).tobytes())
    meta['rgb'] = pal.tolist()
    json.dump(meta, open(os.path.join(args.out, 'palette.json'), 'w'), indent=1)
    swatch(pal, args.swatch)
    print('palette: %d hull colours, primary shades %s' % (len(HULL), np.round(meta['shades_primary'], 2)))
    print('livery ramps:', {k: (v['primary'], v['secondary']) for k, v in meta['livery_ramps'].items()})


if __name__ == '__main__':
    main()
