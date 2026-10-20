#!/usr/bin/env python3
"""The cockpit kits, cut for the game: art/cockpits/KEY/ (docs/engine.md).

    cockpits.py [--unzip DIR] [--src art/cockpits/src] [--out art/cockpits] [--sheet docs/cockpits.png]

The kits (the user's generated art: a 640x400 RGBA base whose canopy opening
is transparent, full-frame joystick and throttle layers, a set of full-frame
damage overlays, JSON maps of the displays) come as zips; --unzip DIR
unpacks every *cockpit*.zip in DIR into --src (git-ignored).  Each ship of
data/cockpits.json is then cut into committed, game-ready pieces:

    base.png             the base as it came (RGBA: alpha < 128 is the opening)
    throttle.png, joystick.png   the controls cropped; cockpit.json places them
    damage/NN-id.png     each damage overlay cropped to its painted pixels
    cockpit.json         the displays in one vocabulary (mfd_left, mfd_right,
                         radar, survival, speed, fuel, lamps), the controls'
                         placement, the damage layers with their bit and what
                         they are attached to

The kits name the same things differently (weapons/flight, targeting/systems,
radar/sensor, survival/engineering), and the Lancelot kit has no display map
at all: its displays are found inside its damage regions by the glass
colour.  Damage layer ids map to fixed bits (BITS), so the engine's
rvCockpitDamage word means the same thing in every cockpit.
"""
import argparse
import glob
import json
import os
import sys
import zipfile

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SCREEN = (640, 400)

# damage layer id -> bit of rvCockpitDamage, the same in every cockpit
BITS = {
    'weapons': 0, 'flight_vdu': 0,
    'targeting': 1, 'systems_vdu': 1,
    'radar': 2, 'sensor_scope': 2,
    'shields': 3, 'armor': 4, 'hull': 5,
    'speed': 6, 'fuel': 7,
    'propulsion': 8,
    'power_distribution': 9,
    'em': 10, 'stealth': 10, 'automation': 10,
    'engine_status': 11,
    'navigation': 12, 'communications': 13, 'iff': 14, 'transponder': 14,
    'flight_controls': 15,
    'aux_flight_display': 16, 'defence_control': 16,
    'weapon_controls': 17, 'target_controls': 18, 'radar_controls': 19,
    'ejection': 20, 'warnings': 21, 'annunciators': 21, 'lock': 22,
    'canopy': 23, 'canopy_left': 23, 'canopy_right': 24, 'canopy_overhead': 25,
    'survival_vdu': 26, 'engineering_vdu': 27, 'support_vdu': 27,
    'sensor_aux_vdu': 28, 'radar_status': 28,
}
BIT_NAMES = {}
for _k, _v in BITS.items():
    BIT_NAMES.setdefault(_v, _k)
ATTACH = {None: 0, 'throttle': 1, 'joystick': 2}
# the displays: our name, the kits' names for it
RECTS = {'mfd_left': ('weapons', 'flight'), 'mfd_right': ('targeting', 'systems'),
         'survival': ('survival', 'engineering'), 'speed': ('speed',), 'fuel': ('fuel',)}
# the Lancelot kit: the damage layer whose region holds each display
LANCELOT = {'mfd_left': 'weapons', 'mfd_right': 'targeting', 'survival': 'survival_vdu', 'speed': 'speed',
            'fuel': 'fuel', 'radar': 'radar'}


def unzip(zips, src):
    os.makedirs(src, exist_ok=True)
    for z in sorted(glob.glob(os.path.join(zips, '*cockpit*.zip'))):
        with zipfile.ZipFile(z) as zf:
            for name in zf.namelist():
                # flatten the pack directory: KIT/... lands as src/KIT/...
                parts = name.split('/')
                if len(parts) < 2 or not parts[-1]:
                    continue
                inner = parts[1:] if parts[0].endswith('-640x400') else parts
                dst = os.path.join(src, *inner)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                with open(dst, 'wb') as f:
                    f.write(zf.read(name))
        print('unpacked', os.path.basename(z))


def rect_of(v):
    if 'rect' in v:
        return [int(x) for x in v['rect']]
    return [int(v['x']), int(v['y']), int(v['width']), int(v['height'])]


def glass_pixels(base, rect, tol=30):
    """The display glass inside rect: pixels near the colour at the rect's
    centre (the blank glass is one flat dark colour)."""
    x, y, w, h = rect
    a = np.asarray(base.convert('RGB')).astype(np.int32)
    cx, cy = x + w // 2, y + h // 2
    glass = a[cy - 2:cy + 3, cx - 2:cx + 3].reshape(-1, 3).mean(axis=0)
    win = a[y:y + h, x:x + w]
    near = (np.abs(win - glass) < tol).all(axis=2)
    return near, glass


def glass_rect(base, rect):
    near, _g = glass_pixels(base, rect)
    ys, xs = np.nonzero(near)
    x, y = rect[0] + int(xs.min()) + 2, rect[1] + int(ys.min()) + 2
    return [x, y, int(xs.max() - xs.min()) - 3, int(ys.max() - ys.min()) - 3]


def glass_circle(base, rect):
    near, _g = glass_pixels(base, rect)
    ys, xs = np.nonzero(near)
    w, h = int(xs.max() - xs.min()), int(ys.max() - ys.min())
    return {'cx': rect[0] + int(xs.min()) + w // 2, 'cy': rect[1] + int(ys.min()) + h // 2, 'r': min(w, h) // 2 - 1}


def regions_of(kit, lj, base):
    ur = lj.get('ui_regions')
    out = {}
    if ur:
        for name, aliases in RECTS.items():
            for a in aliases:
                if a in ur and ('rect' in ur[a] or 'x' in ur[a]):
                    out[name] = rect_of(ur[a])
                    break
        for a in ('radar', 'sensor'):
            if a in ur and 'center' in ur[a]:
                out['radar'] = {'cx': int(ur[a]['center'][0]), 'cy': int(ur[a]['center'][1]), 'r': int(ur[a]['radius'])}
        lamps = ur.get('status_lamps') or {}
        for ours, theirs in (('lamp_lock', 'lock'), ('lamp_warn', 'caution')):
            if theirs in lamps:
                out[ours] = [int(v) for v in lamps[theirs]]
        for a in ('lock', 'warn', 'caution', 'warning'):
            if a in ur and 'rect' in ur[a]:
                out['lamp_lock' if a == 'lock' else 'lamp_warn'] = rect_of(ur[a])
    else:
        dm = {l['id']: l['damage_region'] for l in lj['layers']}
        for name, lid in LANCELOT.items():
            r = rect_of(dm[lid])
            out[name] = glass_circle(base, r) if name == 'radar' else glass_rect(base, r)
    missing = [n for n in ('mfd_left', 'mfd_right', 'radar', 'survival', 'speed', 'fuel') if n not in out]
    if missing:
        sys.exit('%s: no %s in its display map' % (kit, ', '.join(missing)))
    return out


def crop_alpha(im):
    """(cropped image, x, y) of an RGBA layer's painted pixels; None when blank."""
    a = np.asarray(im.convert('RGBA'))[..., 3]
    ys, xs = np.nonzero(a > 0)
    if not len(ys):
        return None, 0, 0
    x0, y0, x1, y1 = int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1
    return im.convert('RGBA').crop((x0, y0, x1, y1)), x0, y0


def cut(key, kit, src, out):
    d = os.path.join(src, kit)
    lj = json.load(open(os.path.join(d, 'layers.json')))
    base = Image.open(os.path.join(d, lj['base'])).convert('RGBA')
    if base.size != SCREEN:
        sys.exit('%s: the base is %dx%d' % (kit, *base.size))
    od = os.path.join(out, key)
    os.makedirs(os.path.join(od, 'damage'), exist_ok=True)
    base.save(os.path.join(od, 'base.png'))
    cj = {'ship': key, 'kit': kit, 'description': lj.get('description') or lj.get('base_description', ''),
          'regions': regions_of(kit, lj, base), 'controls': {}, 'damage': []}
    for c in lj['control_layers']:
        p = c['placement']
        im = Image.open(os.path.join(d, c['cropped_file'])).convert('RGBA') if c.get('cropped_file') else None
        if im is None or im.size != (p['width'], p['height']):
            full = Image.open(os.path.join(d, c['file'])).convert('RGBA')
            im = full.crop((p['x'], p['y'], p['x'] + p['width'], p['y'] + p['height']))
        im.save(os.path.join(od, c['id'] + '.png'))
        cj['controls'][c['id']] = {'x': p['x'], 'y': p['y'], 'w': p['width'], 'h': p['height'],
                                   'damage': c.get('damage_layer_id')}
    for l in lj['layers']:
        if l['id'] not in BITS:
            sys.exit('%s: damage layer %s has no bit (tools/cockpits.py BITS)' % (kit, l['id']))
        im = Image.open(os.path.join(d, l['file']))
        crop, x, y = crop_alpha(im)
        if crop is None:
            print('  %s: %s is blank, skipped' % (kit, l['id']))
            continue
        name = 'damage/%02d-%s.png' % (BITS[l['id']], l['id'])
        crop.save(os.path.join(od, name))
        att = (l.get('attachment') or {}).get('control_id')
        cj['damage'].append({'id': l['id'], 'bit': BITS[l['id']], 'file': name, 'x': x, 'y': y,
                             'w': crop.width, 'h': crop.height, 'attach': att})
    bits = [e['bit'] for e in cj['damage']]
    if len(bits) != len(set(bits)):
        sys.exit('%s: two damage layers share a bit: %s' % (kit, sorted(bits)))
    json.dump(cj, open(os.path.join(od, 'cockpit.json'), 'w'), indent=1)
    print('%-8s %-28s %2d damage layers, displays %s' % (key, kit, len(cj['damage']),
                                                         ' '.join(sorted(cj['regions']))))
    return cj


def sheet(out, keys, path):
    """The cockpits assembled, their displays outlined, four to a row at half size."""
    cols = 4
    rows = (len(keys) + cols - 1) // cols
    img = Image.new('RGB', (cols * 320, rows * 212), (20, 20, 24))
    dr = ImageDraw.Draw(img)
    for i, key in enumerate(keys):
        od = os.path.join(out, key)
        cj = json.load(open(os.path.join(od, 'cockpit.json')))
        im = Image.alpha_composite(Image.new('RGBA', SCREEN, (8, 10, 16, 255)),
                                   Image.open(os.path.join(od, 'base.png')).convert('RGBA'))
        for cid, c in cj['controls'].items():
            im.alpha_composite(Image.open(os.path.join(od, cid + '.png')).convert('RGBA'), (c['x'], c['y']))
        d2 = ImageDraw.Draw(im)
        for name, r in cj['regions'].items():
            if name == 'radar':
                d2.ellipse([r['cx'] - r['r'], r['cy'] - r['r'], r['cx'] + r['r'], r['cy'] + r['r']],
                           outline=(0, 255, 255))
            else:
                d2.rectangle([r[0], r[1], r[0] + r[2] - 1, r[1] + r[3] - 1], outline=(0, 255, 255))
        for e in cj['damage']:
            d2.rectangle([e['x'], e['y'], e['x'] + e['w'] - 1, e['y'] + e['h'] - 1], outline=(255, 90, 40))
        im = im.convert('RGB').resize((320, 200), Image.LANCZOS)
        x, y = (i % cols) * 320, (i // cols) * 212
        img.paste(im, (x, y))
        dr.text((x + 4, y + 200), '%s: %d damage layers' % (key, len(cj['damage'])), fill=(230, 230, 230))
    img.save(path)
    print('sheet', path)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--unzip', help='unpack every *cockpit*.zip of this directory into --src first')
    ap.add_argument('--src', default=os.path.join(ROOT, 'art', 'cockpits', 'src'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'art', 'cockpits'))
    ap.add_argument('--sheet', default=os.path.join(ROOT, 'docs', 'cockpits.png'))
    args = ap.parse_args()
    if args.unzip:
        unzip(args.unzip, args.src)
    cfg = json.load(open(os.path.join(ROOT, 'data', 'cockpits.json')))
    for key, kit in cfg['ships'].items():
        cut(key, kit, args.src, args.out)
    sheet(args.out, list(cfg['ships']), args.sheet)


if __name__ == '__main__':
    main()
