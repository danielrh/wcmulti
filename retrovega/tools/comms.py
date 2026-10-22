#!/usr/bin/env python3
"""Comm-link faces for the cockpit's VDUs: a pilot in a flight helmet over a grainy green-and-white link.

    comms.py [KEY ...] [--out build/comms] [--preview DIR]

The user's brief (2026-10-22): every human pilot in the same helmet, styled for this universe, futuristic but
functional (after their reference: a clear-domed hood seen head on), the callsign on it, the pilot's own face
behind the visor, the eyes animating; the helmets shared and only the differences animated; good on most of the
cockpits' VDUs, cropped for the rest; the scan lines left to the engine.

The helmet is one render for everyone (tools/helmet3d.py: signed distance fields ray-marched in numpy, lit, the
visor's reflection kept apart).  Each pilot's portrait (art/people/faces/KEY/: base.png, the eye and mouth frames,
face.json) goes behind the visor, its eyes onto the helmet's eye points, lit by the light that falls inside and
under the visor's reflection.  Then to the link's look: luminance, the background a dark tone so the helmet's
outline reads, the range stretched, W x H (what most of the cockpits' VDUs show: the engine crops it for smaller
ones and fills round it for bigger), LEVELS greens, a light ordered dither.  The callsign is stencilled on the
shell above the visor in a 4 x 6 pixel font at that size (a smoothed one is mush at seven pixels).

What the engine keeps (OUT/): helmet.png, the shared picture (shade indices, mode L); for each pilot KEY/:
window.png, the part that differs from pilot to pilot (the visor and the callsign); eyes_NAME.png and mouth_N.png,
the patches that animate it; comm.json, where each sits.  With --preview DIR: a contact sheet and each pilot
talking and blinking as a GIF, in the link's greens with scan lines and grain as the engine will draw them.

Everything here is rendered from scratch or made from the user's portraits.
"""
import argparse
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
W, H = 112, 80                          # the VDU picture (most cockpits' VDUs are 103..109 x 70..74)
SS = 4                                  # rendered this much finer, then reduced
LEVELS = 8
GREENS = [(3, 12, 5), (8, 34, 12), (16, 62, 22), (28, 96, 34), (46, 132, 50), (74, 170, 74), (122, 210, 116),
          (206, 246, 196)]              # black green to green white: the engine's palette takes these
LOOK = dict(bg=0.045, lo=0.05, hi=0.75, gamma=0.7, dither=0.25)   # the link's treatment (chosen from four; bg linear)
EYE_FRAMES = ['open', 'half', 'shut', 'left', 'right']
BAYER4 = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) / 16.0 - 0.5
CALLSIGN_SHADE = 6

GLYPHS = {   # 4 x 6 (M, W 5 wide; I, T, Y 3)
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
    """a pilot's portrait (256 x 256), its eye and mouth frames, and where its eyes are"""

    def __init__(self, key):
        d = os.path.join(FACES, key)
        self.key = key
        self.meta = json.load(open(os.path.join(d, 'face.json')))
        self.callsign = self.meta.get('callsign') or key.title()
        self.base = Image.open(os.path.join(d, 'base.png')).convert('RGBA')
        self.eyes = [Image.open(os.path.join(d, 'eyes_%d.png' % i)).convert('RGBA') for i in range(3)]
        self.mouths = []
        for i in range(9):
            f = os.path.join(d, 'mouth_%d.png' % i)
            self.mouths.append(Image.open(f).convert('RGBA') if os.path.exists(f) else None)
        src, b = self.meta['source'], self.meta['eye_box']
        sb = src['eye_box']
        k = (b[2] - b[0]) / (sb[2] - sb[0])
        self.eye_pts = [((ex - sb[0]) * k + b[0], (ey - sb[1]) * k + b[1]) for ex, ey in src['eyes']]

    def picture(self, eyes=0, glance=0, mouth=0):
        """the portrait's luminance with an eye frame (0 open, 1 half, 2 shut), a glance (-1 left, 1 right: the eye
        band shifted in its box) and a mouth frame (lipsync's nine) laid over it"""
        im = self.base.copy()
        if eyes:
            im.alpha_composite(self.eyes[eyes], tuple(self.meta['eye_box'][:2]))
        if mouth and self.mouths[mouth] is not None:
            im.alpha_composite(self.mouths[mouth], tuple(self.meta['mouth_box'][:2]))
        a = np.asarray(im.convert('RGB'), dtype=np.float64) / 255.0
        lum = 0.3 * a[..., 0] + 0.59 * a[..., 1] + 0.11 * a[..., 2]
        if glance:
            x0, y0, x1, y1 = self.meta['eye_box']
            band = lum[y0:y1, x0:x1].copy()
            shift = int(round(glance * 2.5))
            moved = np.roll(band, shift, axis=1)
            edge = np.ones(band.shape)
            edge[:, :3] = edge[:, -3:] = 0       # (the band's own edges stay: the whites and lids move, not the box)
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


def shades(gray):
    """luminance (gamma 1.8 display, as the grey previews) to the link's shades at W x H"""
    disp = np.clip(gray, 0, 1.5) ** (1 / 1.8)
    small = np.asarray(Image.fromarray((np.clip(disp, 0, 1) * 255).astype(np.uint8)).resize((W, H), Image.LANCZOS),
                       dtype=np.float64) / 255.0
    s = np.clip((small - LOOK['lo']) / (LOOK['hi'] - LOOK['lo']), 0, 1) ** LOOK['gamma']
    b = np.tile(BAYER4, (H // 4 + 1, W // 4 + 1))[:H, :W]
    return np.clip(np.round(s * (LEVELS - 1) + b * LOOK['dither']), 0, LEVELS - 1).astype(np.uint8)


def stencil(idx, text, cx, cy):
    """the callsign in the pixel font, centred at (cx, cy): light on a dark shell, dark on a light one, a shadow
    a pixel down and right in the other shade, so it reads on whatever is behind it"""
    text = ''.join(ch for ch in text.upper() if ch in GLYPHS)
    width = sum(len(GLYPHS[ch][0]) + 1 for ch in text) - 1
    x0, y0 = int(round(cx - width / 2.0)), int(round(cy - 3))
    under = idx[max(y0, 0):y0 + 6, max(x0, 0):x0 + width]
    light = under.size == 0 or under.mean() < (LEVELS - 1) * 0.55
    ink, shadow = (CALLSIGN_SHADE, 1) if light else (1, LEVELS - 2)
    pts = []
    x = x0
    for ch in text:
        g = GLYPHS[ch]
        pts += [(y0 + rr, x + k) for rr, row in enumerate(g) for k, p in enumerate(row) if p == '#']
        x += len(g[0]) + 1
    for (y, x) in pts:
        if 0 <= y + 1 < idx.shape[0] and 0 <= x + 1 < idx.shape[1]:
            idx[y + 1, x + 1] = shadow
    for (y, x) in pts:
        if 0 <= y < idx.shape[0] and 0 <= x < idx.shape[1]:
            idx[y, x] = ink
    return idx


def bbox(mask):
    ys, xs = np.nonzero(mask)
    if not len(xs):
        return None
    return [int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1]


def to_rgb(idx, frame=0, scan=True, grain=True, seed=0):
    rng = np.random.default_rng(seed + frame * 7919)
    i = idx.astype(np.int32)
    if grain:
        i = np.clip(i + (rng.random(i.shape) < 0.025) * rng.choice([-1, 1], i.shape), 0, LEVELS - 1)
    rgb = np.array(GREENS, dtype=np.uint8)[i]
    if scan:
        rgb[1::2] = (rgb[1::2].astype(np.int32) * 5 // 8).astype(np.uint8)
    return Image.fromarray(rgb, 'RGB')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('keys', nargs='*')
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'comms'))
    ap.add_argument('--preview')
    args = ap.parse_args()
    keys = args.keys or PILOTS
    r = helmet3d.render(W * SS, H * SS)
    brow = (r['brow'][0] / SS, r['brow'][1] / SS)
    pilots = {}
    for key in keys:
        por = Portrait(key)
        frames = {}
        for name in EYE_FRAMES:
            e = {'open': 0, 'half': 1, 'shut': 2}.get(name, 0)
            g = {'left': -1, 'right': 1}.get(name, 0)
            frames['eyes_' + name] = stencil(shades(composite(r, por, por.picture(e, g))), por.callsign, *brow)
        for m in range(1, 9):
            if por.mouths[m] is not None:
                frames['mouth_%d' % m] = stencil(shades(composite(r, por, por.picture(0, 0, m))), por.callsign, *brow)
        pilots[key] = (por, frames)
        print('%-8s %d frames' % (key, len(frames)))
    # the shared helmet, and each pilot's window: where any pilot's picture differs from another's
    first = pilots[keys[0]][1]['eyes_open']
    differ = np.zeros((H, W), bool)
    for key in keys:
        differ |= pilots[key][1]['eyes_open'] != first
    head = np.asarray(Image.fromarray((r['mat'] == helmet3d.MAT['head']).astype(np.uint8) * 255).resize((W, H),
                      Image.BILINEAR)) > 0
    win = bbox(differ | head)
    os.makedirs(args.out, exist_ok=True)
    Image.fromarray(first, 'L').save(os.path.join(args.out, 'helmet.png'))
    for key in keys:
        por, frames = pilots[key]
        base = frames['eyes_open']
        d = os.path.join(args.out, key)
        os.makedirs(d, exist_ok=True)
        x0, y0, x1, y1 = win
        Image.fromarray(np.ascontiguousarray(base[y0:y1, x0:x1]), 'L').save(os.path.join(d, 'window.png'))
        info = {'key': key, 'callsign': por.callsign, 'w': W, 'h': H, 'levels': LEVELS, 'window': win, 'patches': {}}
        for name, f in frames.items():
            if name == 'eyes_open':
                continue
            bb = bbox(f != base)
            if bb is None:
                continue
            info['patches'][name] = bb
            Image.fromarray(np.ascontiguousarray(f[bb[1]:bb[3], bb[0]:bb[2]]), 'L').save(os.path.join(d, name + '.png'))
        json.dump(info, open(os.path.join(d, 'comm.json'), 'w'), indent=1)
    if args.preview:
        preview(args.preview, keys, pilots)


def preview(out, keys, pilots):
    """each pilot saying their line and blinking, glancing now and then; a contact sheet"""
    os.makedirs(out, exist_ok=True)
    Z = 3
    tiles = []
    people = {row['key']: row for row in __import__('csv').DictReader(open(os.path.join(ROOT, 'data', 'people.csv')))}
    for n, key in enumerate(keys):
        por, frames = pilots[key]
        line = people.get(key, {}).get('line', 'Copy that.')
        mouths = lipsync.frames(line, 1)
        total = max(len(mouths) + 12, 60)
        rng = np.random.default_rng(n + 5)
        eyes = ['open'] * total
        k = 8
        while k < total - 3:                      # blinks every second or two, a glance now and then
            if rng.random() < 0.3:
                g = rng.choice(['left', 'right'])
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
            m = mouths[f - 6] if 6 <= f < 6 + len(mouths) else 0
            name = ('mouth_%d' % m) if m and ('mouth_%d' % m) in frames else 'eyes_open'
            pic = frames[name].copy()
            if eyes[f] != 'open':
                eb = frames['eyes_' + eyes[f]]
                diff = eb != frames['eyes_open']
                pic[diff] = eb[diff]
            gif.append(to_rgb(pic, f, seed=n).resize((W * Z, H * Z), Image.NEAREST))
        gif[0].save(os.path.join(out, key + '.gif'), save_all=True, append_images=gif[1:], duration=100, loop=0)
        tiles.append(to_rgb(frames['eyes_open'], 0, seed=n))
    cols = 5
    rows = (len(tiles) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * (W * Z + 8) + 8, rows * (H * Z + 8) + 8), (8, 8, 10))
    for i, t in enumerate(tiles):
        sheet.paste(t.resize((W * Z, H * Z), Image.NEAREST), (8 + (i % cols) * (W * Z + 8), 8 + (i // cols) * (H * Z + 8)))
    sheet.save(os.path.join(out, 'sheet.png'))


if __name__ == '__main__':
    main()
