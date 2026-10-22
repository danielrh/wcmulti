#!/usr/bin/env python3
"""Comm-link faces for the cockpit's VDUs: a pilot in a flight helmet over a low-bandwidth green link.

    comms.py [KEY ...] [--out build/comms] [--cols 112] [--preview DIR]

The user's brief (2026-10-22): every human pilot in the same helmet, styled for this universe, futuristic but
functional (after their reference: a clear-domed hood seen head on); the mouth covered by breathing gear and the
eyes shown, so only the eyes differ from pilot to pilot and only they animate; the callsign on the helmet; a low
bandwidth, encrypted link: each row drawn twice, the picture deliberately coarse; little memory.

The helmet is one render for everyone (tools/helmet3d.py: signed distance fields ray-marched in numpy, lit, the
visor's reflection kept apart; the oxygen mask, its regulator and hose, inside the visor).  Each pilot's portrait
(art/people/faces/KEY/) goes behind the visor, its eyes onto the helmet's eye points.  Then to the link's look:
luminance, a dark ground so the helmet's outline reads, the range stretched, reduced to COLS x ROWS (ROWS = 40:
the engine draws each row twice, the second a scan line darker; COLS 112, or 56 for 2 x 2 blocks), LEVELS greens
(flight palette entries), a light ordered dither.

What the engine keeps (OUT/): helmet.png, the shared picture (shade indices, mode L); for each pilot KEY/:
window.png, the part that differs from pilot to pilot (the eyes and brows), and eyes_half, eyes_shut, eyes_left,
eyes_right.png, the patches that animate it, with comm.json saying where each sits; comms.json: the sizes, the
ramps, where the engine stencils the callsign (it draws it at the VDU's own resolution, in a 4 x 6 pixel font:
at the link's it could not be read) and where the regulator's light is (lit while the pilot speaks).  With
--preview DIR: a contact sheet and each pilot's GIF (blinking, glancing, the light with their line), drawn as the
engine draws them.

Everything here is rendered from scratch or made from the user's portraits.
"""
import argparse
import csv
import json
import os
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import helmet3d  # noqa: E402
import lipsync  # noqa: E402

ROOT = os.path.join(HERE, '..')
FACES = os.path.join(ROOT, 'art', 'people', 'faces')
PILOTS = ['COMET', 'FROST', 'ECHO', 'TORCH', 'KESTREL', 'DRIFTER', 'PRIME', 'ANCHOR', 'HERO_M', 'HERO_F']
SW, SH = 112, 80                        # on the VDU (most cockpits' VDUs are 103..109 x 70..74)
ROWS = 40                               # kept: each row drawn twice
SS = 4                                  # the helmet rendered this much finer than the VDU picture
LEVELS = 8
# the shades are flight palette entries (the same in every cockpit): black, the carrier-green livery's ramp (64 ..
# 71), a pale green white at the top; a row's second drawing (the scan line) the same two steps down
RAMP = [16, 64, 66, 67, 68, 69, 71, 155]
RAMP_DARK = [16, 16, 64, 66, 67, 68, 69, 71]
_FLIGHT = json.load(open(os.path.join(ROOT, 'data', 'palette.json')))['rgb']
GREENS = [tuple(_FLIGHT[i]) for i in RAMP]
GREENS_DARK = [tuple(_FLIGHT[i]) for i in RAMP_DARK]
LOOK = dict(bg=0.045, lo=0.05, hi=0.75, gamma=0.7, dither=0.25)
EYE_FRAMES = ['open', 'half', 'shut', 'left', 'right']
PATCHES = ['eyes_half', 'eyes_shut', 'eyes_left', 'eyes_right']   # RV.DAT's order, after each pilot's window
BAYER4 = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) / 16.0 - 0.5

GLYPHS = {   # 4 x 6 (M, W 5 wide; I, T, Y 3): the engine's table is the same (comms.c)
    'A': ['.##.', '#..#', '#..#', '####', '#..#', '#..#'], 'B': ['###.', '#..#', '###.', '#..#', '#..#', '###.'],
    'C': ['.###', '#...', '#...', '#...', '#...', '.###'], 'D': ['###.', '#..#', '#..#', '#..#', '#..#', '###.'],
    'E': ['####', '#...', '###.', '#...', '#...', '####'], 'F': ['####', '#...', '###.', '#...', '#...', '#...'],
    'G': ['.###', '#...', '#.##', '#..#', '#..#', '.###'], 'H': ['#..#', '#..#', '####', '#..#', '#..#', '#..#'],
    'I': ['###', '.#.', '.#.', '.#.', '.#.', '###'], 'J': ['..##', '...#', '...#', '...#', '#..#', '.##.'],
    'K': ['#..#', '#.#.', '##..', '#.#.', '#..#', '#..#'], 'L': ['#...', '#...', '#...', '#...', '#...', '####'],
    'M': ['#...#', '##.##', '#.#.#', '#...#', '#...#', '#...#'], 'N': ['#..#', '##.#', '#.##', '#..#', '#..#', '#..#'],
    'O': ['.##.', '#..#', '#..#', '#..#', '#..#', '.##.'], 'P': ['###.', '#..#', '#..#', '###.', '#...', '#...'],
    'Q': ['.##.', '#..#', '#..#', '#..#', '#.#.', '.#.#'], 'R': ['###.', '#..#', '#..#', '###.', '#.#.', '#..#'],
    'S': ['.###', '#...', '.##.', '...#', '...#', '###.'], 'T': ['###', '.#.', '.#.', '.#.', '.#.', '.#.'],
    'U': ['#..#', '#..#', '#..#', '#..#', '#..#', '.##.'], 'V': ['#..#', '#..#', '#..#', '#..#', '.#.#', '..#.'],
    'W': ['#...#', '#...#', '#...#', '#.#.#', '##.##', '#...#'], 'X': ['#..#', '#..#', '.##.', '.##.', '#..#', '#..#'],
    'Y': ['#.#', '#.#', '#.#', '.#.', '.#.', '.#.'], 'Z': ['####', '...#', '..#.', '.#..', '#...', '####'],
}


class Portrait:
    """a pilot's portrait (256 x 256), its eye frames, and where its eyes are"""

    def __init__(self, key):
        d = os.path.join(FACES, key)
        self.key = key
        self.meta = json.load(open(os.path.join(d, 'face.json')))
        self.callsign = self.meta.get('callsign') or key.title()
        self.base = Image.open(os.path.join(d, 'base.png')).convert('RGBA')
        self.eyes = [Image.open(os.path.join(d, 'eyes_%d.png' % i)).convert('RGBA') for i in range(3)]
        src, b = self.meta['source'], self.meta['eye_box']
        sb = src['eye_box']
        k = (b[2] - b[0]) / (sb[2] - sb[0])
        self.eye_pts = [((ex - sb[0]) * k + b[0], (ey - sb[1]) * k + b[1]) for ex, ey in src['eyes']]

    def picture(self, eyes=0, glance=0):
        """the portrait's luminance with an eye frame (0 open, 1 half, 2 shut) and a glance (-1 left, 1 right: the eye
        band shifted in its box, the box's edges kept)"""
        im = self.base.copy()
        if eyes:
            im.alpha_composite(self.eyes[eyes], tuple(self.meta['eye_box'][:2]))
        a = np.asarray(im.convert('RGB'), dtype=np.float64) / 255.0
        lum = 0.3 * a[..., 0] + 0.59 * a[..., 1] + 0.11 * a[..., 2]
        # for a coarse link: the skin to the middle, the eyes and brows made to stand out (local contrast: the
        # face less its blur, three times over)
        soft = ndimage.gaussian_filter(lum, 6.0)
        lum = np.clip(0.45 + 0.55 * (soft - soft.mean()) + 3.0 * (lum - soft), 0, 1)
        if glance:
            x0, y0, x1, y1 = self.meta['eye_box']
            band = lum[y0:y1, x0:x1].copy()
            moved = np.roll(band, int(round(glance * 3)), axis=1)
            edge = np.ones(band.shape)
            edge[:, :4] = edge[:, -4:] = 0
            lum[y0:y1, x0:x1] = band * (1 - edge) + moved * edge
        return lum


def composite(r, por, lum_face):
    """the helmet render with a pilot's face behind the visor: luminance at the render's size"""
    h, w = r['lum'].shape
    (sx0, sy0), (sx1, _) = r['eyes']
    (px0, py0), (px1, _) = por.eye_pts
    s = (px1 - px0) / (sx1 - sx0)
    ys, xs = np.mgrid[0:h, 0:w]
    px = (xs + 0.5 - sx0) * s + px0
    py = (ys + 0.5 - sy0) * s + py0
    face = ndimage.map_coordinates(lum_face, [py - 0.5, px - 0.5], order=1, mode='nearest')
    head = r['mat'] == helmet3d.MAT['head']
    out = np.where(head, face * (0.35 + 0.75 * np.clip(r['light'], 0, 1.2)), r['lum'])
    out = np.where(r['mat'] < 0, LOOK['bg'], out)
    return out * r['visor_pass'] + r['visor_refl']


def shades(gray, cols):
    """luminance (gamma 1.8 display, as the grey previews) to the link's shades at cols x ROWS"""
    disp = np.clip(gray, 0, 1.5) ** (1 / 1.8)
    small = np.asarray(Image.fromarray((np.clip(disp, 0, 1) * 255).astype(np.uint8)).resize((cols, ROWS),
                                                                                            Image.LANCZOS),
                       dtype=np.float64) / 255.0
    s = np.clip((small - LOOK['lo']) / (LOOK['hi'] - LOOK['lo']), 0, 1) ** LOOK['gamma']
    b = np.tile(BAYER4, (ROWS // 4 + 1, cols // 4 + 1))[:ROWS, :cols]
    return np.clip(np.round(s * (LEVELS - 1) + b * LOOK['dither']), 0, LEVELS - 1).astype(np.uint8)


def bbox(mask):
    ys, xs = np.nonzero(mask)
    if not len(xs):
        return None
    return [int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1]


# --- what the engine draws, done here for the previews ---

def on_vdu(idx, rng=None, grain=True):
    """a kept picture (cols x ROWS shades) as the VDU shows it: SW x SH, each row twice (the second the scan line's
    ramp), each column (SW / cols) times; a few grains a row"""
    sx = SW // idx.shape[1]
    big = np.repeat(np.repeat(idx, 2, axis=0), sx, axis=1).astype(np.int32)
    rgb = np.array(GREENS, dtype=np.uint8)[big]
    rgb[1::2] = np.array(GREENS_DARK, dtype=np.uint8)[big[1::2]]
    if grain and rng is not None:
        for y in range(SH):
            for _ in range(3):
                x = int(rng.integers(0, SW))
                s = int(np.clip(big[y, x] + rng.choice([-1, 1]), 0, LEVELS - 1))
                rgb[y, x] = (GREENS_DARK if y & 1 else GREENS)[s]
    return rgb


def stencil(rgb, idx_big, text, cx, cy):
    """the callsign in the 4 x 6 font at the VDU's resolution: light on a dark shell, dark on a light one, a shadow a
    pixel down and right"""
    text = ''.join(ch for ch in text.upper() if ch in GLYPHS)
    width = sum(len(GLYPHS[ch][0]) + 1 for ch in text) - 1
    x0, y0 = int(round(cx - width / 2.0)), int(round(cy - 3))
    under = idx_big[max(y0, 0):y0 + 6, max(x0, 0):x0 + width]
    light = under.size == 0 or under.mean() < (LEVELS - 1) * 0.55
    ink, shadow = (GREENS[6], GREENS[1]) if light else (GREENS[1], GREENS[6])
    pts, x = [], x0
    for ch in text:
        g = GLYPHS[ch]
        pts += [(y0 + rr, x + k) for rr, row in enumerate(g) for k, p in enumerate(row) if p == '#']
        x += len(g[0]) + 1
    for y, x in pts:
        if 0 <= y + 1 < SH and 0 <= x + 1 < SW:
            rgb[y + 1, x + 1] = shadow
    for y, x in pts:
        if 0 <= y < SH and 0 <= x < SW:
            rgb[y, x] = ink
    return rgb


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('keys', nargs='*')
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'comms'))
    ap.add_argument('--cols', type=int, default=112, help='kept columns: 112, or 56 for 2 x 2 blocks')
    ap.add_argument('--preview')
    args = ap.parse_args()
    keys = args.keys or PILOTS
    cols = args.cols
    r = helmet3d.render(SW * SS, SH * SS)
    brow = (r['brow'][0] / SS, r['brow'][1] / SS)
    led = (r['led'][0] / SS, r['led'][1] / SS)
    pilots = {}
    for key in keys:
        por = Portrait(key)
        frames = {}
        for name in EYE_FRAMES:
            e = {'open': 0, 'half': 1, 'shut': 2}.get(name, 0)
            g = {'left': -1, 'right': 1}.get(name, 0)
            frames['eyes_' + name] = shades(composite(r, por, por.picture(e, g)), cols)
        pilots[key] = (por, frames)
    # the shared helmet, and the window: where any pilot's picture differs from another's
    first = pilots[keys[0]][1]['eyes_open']
    differ = np.zeros(first.shape, bool)
    for key in keys:
        differ |= pilots[key][1]['eyes_open'] != first
    win = bbox(differ)
    os.makedirs(args.out, exist_ok=True)
    Image.fromarray(first, 'L').save(os.path.join(args.out, 'helmet.png'))
    total = first.size
    for key in keys:
        por, frames = pilots[key]
        base = frames['eyes_open']
        d = os.path.join(args.out, key)
        os.makedirs(d, exist_ok=True)
        x0, y0, x1, y1 = win
        Image.fromarray(np.ascontiguousarray(base[y0:y1, x0:x1]), 'L').save(os.path.join(d, 'window.png'))
        total += (x1 - x0) * (y1 - y0)
        info = {'key': key, 'callsign': por.callsign, 'window': win, 'patches': {}}
        for name in PATCHES:
            bb = bbox(frames[name] != base)
            if bb is None:
                continue
            info['patches'][name] = bb
            total += (bb[2] - bb[0]) * (bb[3] - bb[1])
            Image.fromarray(np.ascontiguousarray(frames[name][bb[1]:bb[3], bb[0]:bb[2]]), 'L').save(
                os.path.join(d, name + '.png'))
        json.dump(info, open(os.path.join(d, 'comm.json'), 'w'), indent=1)
    json.dump({'cols': cols, 'rows': ROWS, 'w': SW, 'h': SH, 'levels': LEVELS, 'ramp': RAMP, 'ramp_dark': RAMP_DARK,
               'patches': PATCHES, 'pilots': keys, 'brow': [round(brow[0]), round(brow[1])],
               'led': [round(led[0]), round(led[1])]}, open(os.path.join(args.out, 'comms.json'), 'w'), indent=1)
    print('comms: %d pilots at %d x %d (each row twice), the window %s, %d bytes in all' % (len(keys), cols, ROWS,
                                                                                         win, total))
    if args.preview:
        preview(args.preview, keys, pilots, brow, led)


def preview(out, keys, pilots, brow, led):
    """each pilot saying their line (the regulator's light) and blinking, glancing now and then; a contact sheet"""
    os.makedirs(out, exist_ok=True)
    Z = 3
    tiles = []
    people = {row['key']: row for row in csv.DictReader(open(os.path.join(ROOT, 'data', 'people.csv')))}
    for n, key in enumerate(keys):
        por, frames = pilots[key]
        talk = lipsync.frames(people.get(key, {}).get('line', 'Copy that.'), 1)
        total = max(len(talk) + 12, 60)
        rng = np.random.default_rng(n + 5)
        eyes = ['open'] * total
        k = 8
        while k < total - 3:
            if rng.random() < 0.3:
                g = str(rng.choice(['left', 'right']))
                for j in range(k, min(k + 6, total)):
                    eyes[j] = g
                k += 7
            else:
                for j, e in zip(range(k, k + 3), ['half', 'shut', 'half']):
                    if j < total:
                        eyes[j] = e
                k += 3
            k += int(rng.integers(10, 22))
        gif = []
        for f in range(total):
            idx = frames['eyes_' + eyes[f]]
            rgb = on_vdu(idx, rng)
            big = np.repeat(np.repeat(idx, 2, axis=0), SW // idx.shape[1], axis=1)
            stencil(rgb, big, por.callsign, *brow)
            speaking = 6 <= f < 6 + len(talk) and talk[f - 6] != 0
            if speaking:
                lx, ly = int(round(led[0])), int(round(led[1]))
                rgb[ly - 1:ly + 1, lx - 1:lx + 1] = GREENS[7]
            gif.append(Image.fromarray(rgb, 'RGB').resize((SW * Z, SH * Z), Image.NEAREST))
        gif[0].save(os.path.join(out, key + '.gif'), save_all=True, append_images=gif[1:], duration=100, loop=0)
        rgb = on_vdu(frames['eyes_open'], np.random.default_rng(1))
        stencil(rgb, np.repeat(np.repeat(frames['eyes_open'], 2, axis=0), SW // frames['eyes_open'].shape[1], axis=1),
                por.callsign, *brow)
        tiles.append(Image.fromarray(rgb, 'RGB'))
    cols = 5
    rows = (len(tiles) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * (SW * Z + 8) + 8, rows * (SH * Z + 8) + 8), (8, 8, 10))
    for i, t in enumerate(tiles):
        sheet.paste(t.resize((SW * Z, SH * Z), Image.NEAREST), (8 + (i % cols) * (SW * Z + 8), 8 + (i // cols) * (SH * Z + 8)))
    sheet.save(os.path.join(out, 'sheet.png'))


if __name__ == '__main__':
    main()
