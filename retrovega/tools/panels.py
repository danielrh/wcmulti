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
             'heat', 'signature', 'aux_nav', 'target_id', 'throttle_ticks']
KINDS = ['lamp', 'knob', 'arcs', 'needle', 'ticks', 'bar', 'text']


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
