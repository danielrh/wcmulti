#!/usr/bin/env python3
"""Comm-link faces for the cockpit's VDUs: pilots in their helmets over a low-bandwidth green link.

    comms.py [KEY ...] [--out build/comms] [--cols 112] [--face] [--preview DIR]

The user's brief (2026-10-22): every human pilot in the same helmet, styled for this universe, futuristic but
functional (after their reference: a clear-domed hood seen head on); the mouth covered by breathing gear and the
eyes shown, so only the eyes differ from pilot to pilot and only they animate; the callsign on the helmet; a low
bandwidth, encrypted link: each row drawn twice, the picture deliberately coarse; little memory.  Then: Aera and
Rlaan pilots in heavy gear, anonymous, each in a helmet of their own (data/aliens.csv, tools/aliens3d.py); and the
bare face again (--face: no breathing gear, the mouth speaking in lipsync's shapes) as a build option, to weigh its
cost.

The pilots' helmet is one render for everyone (tools/helmet3d.py: signed distance fields ray-marched in numpy, lit,
the visor's reflection kept apart).  Each pilot's portrait (art/people/faces/KEY/) goes behind the visor, its eyes
onto the helmet's eye points.  An alien's helmet is its own render, its lit parts lit afterwards in each state (eyes
open, dimmed, dark, glancing; the speech lights partly or fully on).  Then to the link's look: luminance, a dark
ground so the outline reads, the range stretched, reduced to COLS x ROWS (ROWS = 40: the engine draws each row
twice, the second a scan line darker; COLS 112, or 56 for 2 x 2 blocks), LEVELS greens (flight palette entries), a
light ordered dither.

A voice is what the engine shows for one speaker: a base picture (the shared helmet, or an alien's own), a window
over it (what differs from pilot to pilot: their eyes and brows; none for an alien), and patches (eyes half, shut,
left, right; then the mouths, lipsync's 1 .. 8 with --face, or an alien's two speech levels), each holding only the
pixels that change (255 elsewhere: the engine leaves those).  What the engine keeps (OUT/): helmet.png; for each
voice KEY/: base.png (an alien's), window.png, the patches; comms.json: the sizes, the ramps, and each voice (its
pictures and where they sit, how many mouths, where the callsign is stencilled and the regulator's light lit, its
callsign, line and ships).  With --preview DIR: a contact sheet and each voice's GIF, drawn as the engine draws it.

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
import aliens3d  # noqa: E402
import helmet3d  # noqa: E402
import lipsync  # noqa: E402

ROOT = os.path.join(HERE, '..')
FACES = os.path.join(ROOT, 'art', 'people', 'faces')
PILOTS = ['COMET', 'FROST', 'ECHO', 'TORCH', 'KESTREL', 'DRIFTER', 'PRIME', 'ANCHOR', 'HERO_M', 'HERO_F']
SIDES = {'terran': 0, 'aera': 1, 'rlaan': 2}
SW, SH = 112, 80                        # on the VDU (most cockpits' VDUs are 103..109 x 70..74)
ROWS = 40                               # kept: each row drawn twice
SS = 4                                  # the pilots' helmet rendered this much finer than the VDU picture
SS_ALIEN = 2                            # (an alien's: no portrait to fit, half as fine is plenty for 56 x 40)
LEVELS = 8
CLEAR = 255                             # a patch's pixel that does not change
# the shades are flight palette entries (the same in every cockpit): black, the carrier-green livery's ramp (64 ..
# 71), a pale green white at the top; a row's second drawing (the scan line) the same two steps down
RAMP = [16, 64, 66, 67, 68, 69, 71, 155]
RAMP_DARK = [16, 16, 64, 66, 67, 68, 69, 71]
_FLIGHT = json.load(open(os.path.join(ROOT, 'data', 'palette.json')))['rgb']
GREENS = [tuple(_FLIGHT[i]) for i in RAMP]
GREENS_DARK = [tuple(_FLIGHT[i]) for i in RAMP_DARK]
LOOK = dict(bg=0.045, lo=0.05, hi=0.75, gamma=0.7, dither=0.25)
EYES = ['half', 'shut', 'left', 'right']        # RV.DAT's order, after each voice's window; then its mouths
BLOOM = 0.6                             # an alien's lights: this much of their glow again, blurred round them
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
        band shifted in its box, the box's edges kept) and a mouth frame (lipsync's nine: the bare face's)"""
        im = self.base.copy()
        if eyes:
            im.alpha_composite(self.eyes[eyes], tuple(self.meta['eye_box'][:2]))
        if mouth and self.mouths[mouth] is not None:
            im.alpha_composite(self.mouths[mouth], tuple(self.meta['mouth_box'][:2]))
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
    head = r['mat'] == r['names'].index('head')
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


def mouth_patch(shape, nmouth):
    """lipsync's shape (1 .. 8) to a voice's mouth patch (1 .. nmouth): all eight, or by how open (the engine's)"""
    return (shape * nmouth + 7) // 8


# --- the voices ---

def pilots(keys, cols, face, people):
    """the human pilots: one helmet render, each portrait behind its visor; the shared helmet (the first pilot's
    picture) and each pilot's voice (its window: where any pilot's picture differs from another's)"""
    model = helmet3d.Human(mask=not face)
    r = helmet3d.render(SW * SS, SH * SS, model)
    brow = [round(r['brow'][0] / SS), round(r['brow'][1] / SS)]
    led = None if face else [round(r['led'][0] / SS), round(r['led'][1] / SS)]
    voices = []
    for key in keys:
        por = Portrait(key)
        pic = lambda e=0, g=0, m=0: shades(composite(r, por, por.picture(e, g, m)), cols)
        frames = [('eyes_half', pic(1)), ('eyes_shut', pic(2)), ('eyes_left', pic(0, -1)), ('eyes_right', pic(0, 1))]
        if face:
            frames += [('mouth_%d' % m, pic(0, 0, m) if por.mouths[m] is not None else None) for m in range(1, 9)]
        voices.append({'key': key, 'side': 0, 'callsign': por.callsign.upper(), 'brow': brow, 'led': led,
                       'line': people.get(key, {}).get('line', ''), 'ships': [], 'nmouth': 8 if face else 0,
                       'open': pic(), 'frames': frames})
    helmet = voices[0]['open']
    differ = np.zeros(helmet.shape, bool)
    for v in voices:
        differ |= v['open'] != helmet
    win = bbox(differ)
    for v in voices:
        v['base'], v['under'], v['window'] = 'helmet.png', helmet, win
    return helmet, voices


def aliens(cols, rows):
    """an alien's voice: its own helmet's picture, no window; its lights' states as patches"""
    voices = []
    for row in rows:
        model = aliens3d.MODELS[row['helmet']]()
        r = helmet3d.render(SW * SS_ALIEN, SH * SS_ALIEN, model)
        grey = np.where(r['mat'] < 0, LOOK['bg'], r['lum'])

        def pic(**state):
            e = helmet3d.emissive(r, state)
            return shades(grey + e + BLOOM * ndimage.gaussian_filter(e, SS_ALIEN * 1.5), cols)
        frames = [('eyes_' + e, pic(eyes=e)) for e in EYES] + [('mouth_%d' % k, pic(talk=k)) for k in (1, 2)]
        own = pic()
        voices.append({'key': row['key'], 'side': SIDES[row['side']], 'callsign': '', 'brow': None, 'led': None,
                       'line': row['line'], 'ships': row['ships'].split(), 'nmouth': 2, 'open': own,
                       'frames': frames, 'base': row['key'] + '/base.png', 'under': own, 'window': None})
    return voices


def compose(v, eyes=None, mouth=None):
    """a voice's picture as the engine puts it together: the base, the window, the eyes' patch, the mouth's"""
    out = v['under'].copy()
    if v['window']:
        x0, y0, x1, y1 = v['window']
        out[y0:y1, x0:x1] = v['open'][y0:y1, x0:x1]
    for name in (eyes, mouth):
        p = v['patches'].get(name) if name else None
        if p is not None:
            (x0, y0, _, _), img = p
            sub = out[y0:y0 + img.shape[0], x0:x0 + img.shape[1]]
            sub[img != CLEAR] = img[img != CLEAR]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('keys', nargs='*')
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'comms'))
    ap.add_argument('--cols', type=int, default=112, help='kept columns: 112, or 56 for 2 x 2 blocks')
    ap.add_argument('--face', action='store_true', help='no breathing gear: the bare face, its mouth speaking')
    ap.add_argument('--no-aliens', action='store_true')
    ap.add_argument('--preview')
    args = ap.parse_args()
    cols = args.cols
    people = {row['key']: row for row in csv.DictReader(open(os.path.join(ROOT, 'data', 'people.csv')))}
    helmet, voices = pilots(args.keys or PILOTS, cols, args.face, people)
    if not args.no_aliens:
        voices += aliens(cols, list(csv.DictReader(open(os.path.join(ROOT, 'data', 'aliens.csv')))))
    os.makedirs(args.out, exist_ok=True)
    Image.fromarray(helmet, 'L').save(os.path.join(args.out, 'helmet.png'))
    total = helmet.size
    out = []
    for v in voices:
        d = os.path.join(args.out, v['key'])
        os.makedirs(d, exist_ok=True)
        if v['base'] != 'helmet.png':
            Image.fromarray(v['under'], 'L').save(os.path.join(args.out, v['base']))
            total += v['under'].size
        if v['window']:
            x0, y0, x1, y1 = v['window']
            Image.fromarray(np.ascontiguousarray(v['open'][y0:y1, x0:x1]), 'L').save(os.path.join(d, 'window.png'))
            total += (x1 - x0) * (y1 - y0)
        v['patches'], listed = {}, []
        for name, frame in v['frames']:
            bb = None if frame is None else bbox(frame != v['open'])
            listed.append([name, bb])
            if bb is None:
                continue
            x0, y0, x1, y1 = bb
            img = np.where(frame != v['open'], frame, CLEAR)[y0:y1, x0:x1].astype(np.uint8)
            Image.fromarray(np.ascontiguousarray(img), 'L').save(os.path.join(d, name + '.png'))
            v['patches'][name] = (bb, img)
            total += img.size
        for name, frame in v['frames']:     # (each patch over the picture is that frame again)
            if frame is not None:
                assert (compose(v, name) == frame).all(), (v['key'], name)
        out.append({k: v[k] for k in ('key', 'side', 'callsign', 'brow', 'led', 'line', 'ships', 'nmouth', 'base',
                                      'window')} | {'patches': listed})
    json.dump({'cols': cols, 'rows': ROWS, 'w': SW, 'h': SH, 'levels': LEVELS, 'ramp': RAMP, 'ramp_dark': RAMP_DARK,
               'clear': CLEAR, 'face': args.face, 'voices': out}, open(os.path.join(args.out, 'comms.json'), 'w'),
              indent=1)
    print('comms: %d voices (%d pilots%s, %d aliens) at %d x %d (each row twice), %d bytes in all'
          % (len(voices), len(voices) - sum(v['side'] != 0 for v in voices), ', bare faces' if args.face else '',
             sum(v['side'] != 0 for v in voices), cols, ROWS, total))
    if args.preview:
        preview(args.preview, voices)


def preview(out, voices):
    """each voice saying its line (the mouth, the speech lights or the regulator's light) and blinking, glancing now
    and then; a contact sheet"""
    os.makedirs(out, exist_ok=True)
    Z = 3
    tiles = []
    for n, v in enumerate(voices):
        talk = lipsync.frames(v['line'] or 'Copy that.', 1)
        total = max(len(talk) + 12, 60)
        rng = np.random.default_rng(n + 5)
        eyes = [None] * total
        k = 8
        while k < total - 3:
            if rng.random() < 0.3:
                g = 'eyes_' + str(rng.choice(['left', 'right']))
                for j in range(k, min(k + 6, total)):
                    eyes[j] = g
                k += 7
            else:
                for j, e in zip(range(k, k + 3), ['eyes_half', 'eyes_shut', 'eyes_half']):
                    if j < total:
                        eyes[j] = e
                k += 3
            k += int(rng.integers(10, 22))
        gif = []
        for f in range(total):
            shape = talk[f - 6] if 6 <= f < 6 + len(talk) else 0
            mouth = 'mouth_%d' % mouth_patch(shape, v['nmouth']) if shape and v['nmouth'] else None
            gif.append(Image.fromarray(draw(v, compose(v, eyes[f], mouth), rng, bool(shape)), 'RGB')
                       .resize((SW * Z, SH * Z), Image.NEAREST))
        gif[0].save(os.path.join(out, v['key'] + '.gif'), save_all=True, append_images=gif[1:], duration=100, loop=0)
        tiles.append(Image.fromarray(draw(v, v['open'], np.random.default_rng(1), False), 'RGB'))
    cols = 5
    rows = (len(tiles) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * (SW * Z + 8) + 8, rows * (SH * Z + 8) + 8), (8, 8, 10))
    for i, t in enumerate(tiles):
        sheet.paste(t.resize((SW * Z, SH * Z), Image.NEAREST), (8 + (i % cols) * (SW * Z + 8), 8 + (i // cols) * (SH * Z + 8)))
    sheet.save(os.path.join(out, 'sheet.png'))


def draw(v, idx, rng, speaking):
    """a voice's picture on the VDU: the link, the callsign, the regulator's light while it speaks"""
    rgb = on_vdu(idx, rng)
    if v['callsign']:
        stencil(rgb, np.repeat(np.repeat(idx, 2, axis=0), SW // idx.shape[1], axis=1), v['callsign'], *v['brow'])
    if speaking and v['led']:
        lx, ly = v['led']
        rgb[ly - 1:ly + 1, lx - 1:lx + 1] = GREENS[7]
    return rgb


if __name__ == '__main__':
    main()
