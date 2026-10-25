#!/usr/bin/env python3
"""The pilot's hand on the joystick: nine poses a cockpit, from the user's renders (art/cockpits/KEY/stick/).

    joysticks.py --zip arm2.zip [--out art/cockpits] [--sheet docs/joysticks.png] [KEY ...]

The user's set (2026-10-23, arm2.zip): for each ship nine handed poses (NW .. SE: the stick pushed forward, upright,
pulled back, each leaning left, upright or right), each a right arm on the stick, its sleeve cut off by a
horizontal line where it comes from; and the same nine bare (the stick alone) as 640 x 400 overlays, at the
cockpit's own scale and place (exactly the kit's old stick: its size and its mount).  The user's rulings: the hand
on the stick, the stick at its intended place and its cockpit's scale, the arm never synthesised - the cut edge off
the bottom of the screen; where that means it, the stick goes lower (its base cropped by the screen's edge, the old
socket painted out of the cockpit).  Then (2026-10-24) the hands read far too small beside the pilot's legs (tools/legs.py):
an arm is not that much thinner than a leg, and nearer - so a set is grown about its fist (where the kit's grip was)
until the fist is FIST of a knee's width (data/legs.json), the stick with it (its base off the bottom of the screen),
and lowered (nearer, as it is) until its upright pose hides none of the displays and the panel's controls, by at most
CLEAR - where that is not enough (a display right above the stick), only as far as hides no more of them than CLEAR
would.
The user named seven cockpits; the Franklin's side stick, at the screen's edge, keeps the kit's size.

For each cockpit:
  * the scale: the handed upright pose's stick (its own pixels, neither skin nor sleeve: the base) matched by
    overlap against the bare upright one's, over a range of scales and offsets;
  * each handed pose at that scale, put on its own bare pose (matched the same way, the offset only), so the stick
    stands where and as big as the cockpit's was;
  * the growth: the whole set scaled about the upright pose's fist, by FIST x the knee's width / the fist's;
  * the drop: the least that puts every pose's cut edge (its sleeve's lowest row) at or past the screen's bottom
    row, the whole set lowered by it; a grown set's, the least that leaves the displays (cockpit.json's regions) and
    the controls (build/panels/KEY/panel.json: a build first) clear of its upright pose, up to CLEAR (else the least
    that hides no more of them than CLEAR does).

Out: art/cockpits/KEY/stick/p0.png .. p8.png (pose: row * 3 + column; rows forward / upright / back, columns left /
upright / right; cropped), stick.json (each pose's place), socket.png / .json (the old socket painted out, when the
kit's socket painted out); rvpack packs them, cockpit.c draws the one the steering picks.
"""
import argparse
import json
import os
import zipfile

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage
from scipy.signal import fftconvolve

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SCREEN = (640, 400)
SHIPS = {'SCHROED': 'schroedinger', 'FRANKLN': 'franklin', 'GAWAIN': 'gawain', 'REDEEMR': 'redeemer',
         'PACIFER': 'pacifier', 'LANCELT': 'lancelot', 'PROGENY': 'progeny', 'GODDARD': 'goddard'}
POSES = ['NW', 'N', 'NE', 'W', 'CENTER', 'E', 'SW', 'S', 'SE']   # p0 .. p8
UP = 4                                  # (the matching done at 4 x the screen's scale)
LEGS = os.path.join(ROOT, 'data', 'legs.json')
FIST = 0.9                              # the fist's width to a knee's: a fist is a little narrower, and nearer
KIT_SIZE = {'FRANKLN'}                  # (not grown: the user named the other seven)
CLEAR = 35                              # a grown set lowered at most this far to clear the panel (the fist in sight)


def parts(a):
    """skin, sleeve, and the stick's own pixels (opaque, neither) of an RGBA array"""
    c = a[..., :3].astype(np.float64)
    r, g, b = c[..., 0], c[..., 1], c[..., 2]
    mx, mn = c.max(-1), c.min(-1)
    sat = (mx - mn) / np.maximum(mx, 1)
    op = a[..., 3] > 128
    skin = op & (r > g) & (g > b) & (r - b > 40) & (sat > 0.25) & (mx > 90)
    sleeve = op & (g >= r * 0.92) & (g > b * 1.12) & (sat > 0.15) & (mx > 30) & (mx < 210)
    # the sleeve is one big piece: drop stray greenish pixels of the stick
    lab, k = ndimage.label(sleeve)
    if k:
        big = np.argmax(ndimage.sum(np.ones(lab.shape), lab, range(1, k + 1))) + 1
        sleeve = lab == big
    return skin, sleeve, op & ~skin & ~sleeve


def base_mask(a):
    """the base: the stick's own pixels in the bottom 30% of the stick (top of the figure to its lowest such
    pixel)"""
    _skin, _sleeve, own = parts(a)
    ys = np.nonzero(own.any(axis=1))[0]
    top = np.nonzero((a[..., 3] > 128).any(axis=1))[0][0]
    bottom = ys[-1]
    m = own.copy()
    m[:int(bottom - 0.30 * (bottom - top))] = False
    return m, top, bottom


def premul(a):
    out = a.astype(np.float64)
    out[..., :3] *= out[..., 3:4] / 255.0
    return np.clip(out, 0, 255).astype(np.uint8)


def unpremul(a):
    out = a.astype(np.float64)
    al = np.maximum(out[..., 3:4], 1) / 255.0
    out[..., :3] = np.where(out[..., 3:4] > 0, out[..., :3] / al, 0)
    return np.clip(out, 0, 255).astype(np.uint8)


def sleeve_end(a):
    """the sleeve's cut end (the sheet's edge of the arm): its points (x, y), the far tenth of the sleeve along the
    forearm (from the hand's middle to the sleeve's)"""
    skin, sleeve, _own = parts(a)
    if sleeve.sum() < 50 or skin.sum() < 50:
        return np.zeros((0, 2))
    ys, xs = np.nonzero(sleeve)
    hy, hx = np.nonzero(skin)
    W = np.array([xs.mean(), ys.mean()])
    d = W - np.array([hx.mean(), hy.mean()])
    d = d / max(1e-6, np.hypot(*d))
    p = (xs - W[0]) * d[0] + (ys - W[1]) * d[1]
    end = p > np.percentile(p, 90)
    return np.stack([xs[end], ys[end]], -1).astype(np.float64)


def fill_socket(d, cj, old):
    """the kit's socket painted out of the base (its hole and collar): stick/socket.png, the changed pixels, for
    tools/panels.py to lay on the base.  The hole: of the dark blobs round the old stick's foot, the roundest
    biggest nearest the foot; it and a collar round it are filled with the plate round them (its colours spread
    in, smoothed) and speckled like the plate (random, as strong as the plate's own fine texture)"""
    base = np.asarray(Image.open(os.path.join(d, 'base.png')).convert('RGBA')).copy()
    jx, jy = cj['controls']['joystick']['x'], cj['controls']['joystick']['y']
    h, w = old.shape
    oy, ox = np.nonzero(old)
    foot_at = np.array([jx + ox[oy > oy.max() - 6].mean(), jy + oy.max()])
    near = np.zeros(base.shape[:2], bool)
    near[jy + h // 4:jy + h, jx:jx + w] = old[h // 4:]
    near = ndimage.binary_dilation(near, iterations=10) & (base[..., 3] >= 128)
    rgb = base[..., :3].astype(np.float64)
    L = rgb @ [0.299, 0.587, 0.114]
    dark = near & (L < min(50, np.percentile(L[near], 25)))
    dark = ndimage.binary_opening(dark, iterations=1)
    lab, k = ndimage.label(dark)
    best, hole = -1, None
    for i in range(1, k + 1):
        m = lab == i
        ys, xs = np.nonzero(m)
        if len(ys) < 12:
            continue
        ext = max(np.ptp(xs), np.ptp(ys)) + 1
        round_ = len(ys) / (np.pi * (ext / 2.0) ** 2)
        dist = np.hypot(xs.mean() - foot_at[0], ys.mean() - foot_at[1])
        score = len(ys) * min(1.0, round_) ** 2 / (1 + dist / 12.0)
        if score > best:
            best, hole = score, m
    if hole is None:
        return
    hole = ndimage.binary_fill_holes(hole)
    r = np.sqrt(hole.sum() / np.pi)
    region = ndimage.binary_dilation(hole, iterations=max(4, int(round(r * 1.15)))) & (base[..., 3] >= 128)
    low = rgb.copy()
    known = ~region
    for _ in range(400):
        if known.all():
            break
        acc = np.zeros_like(low)
        n = np.zeros(region.shape)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            kk = np.roll(np.roll(known, dy, 0), dx, 1)
            acc += np.roll(np.roll(low, dy, 0), dx, 1) * kk[..., None]
            n += kk
        fill = ~known & (n > 0)
        low[fill] = acc[fill] / n[fill][:, None]
        known |= fill
    for _ in range(30):                 # (smoothed inside, the edge held)
        sm = ndimage.uniform_filter(low, size=(3, 3, 1))
        low[region] = sm[region]
    # the speckle: the plate's fine texture just round the region (its strong edges left out)
    ring = ndimage.binary_dilation(region, iterations=6) & ~region & (base[..., 3] >= 128)
    hp = rgb - ndimage.gaussian_filter(rgb, (1.5, 1.5, 0))
    v = hp[ring]
    sd = np.clip(np.std(np.clip(v, -2.5 * v.std(0), 2.5 * v.std(0)), axis=0), 0, 18)
    rng = np.random.default_rng(11)
    noise = ndimage.gaussian_filter(rng.normal(0, 1, rgb.shape[:2]), 0.6)
    noise /= max(1e-6, noise.std())
    ys, xs = np.nonzero(region)
    out = base.copy()
    out[ys, xs, :3] = np.clip(low[ys, xs] + noise[ys, xs, None] * sd.mean() * 0.55, 0, 255).astype(np.uint8)
    y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
    patch = out[y0:y1, x0:x1].copy()
    patch[..., 3] = np.where(region[y0:y1, x0:x1], 255, 0)
    Image.fromarray(patch, 'RGBA').save(os.path.join(d, 'stick', 'socket.png'))
    json.dump({'x': int(x0), 'y': int(y0), 'hole_px': int(hole.sum())},
              open(os.path.join(d, 'stick', 'socket.json'), 'w'))


def match(ref, pose, scales):
    """the scale (of those given) and offset that put pose's base on ref's: ref at UP x the screen, pose at its
    own; -> (s, dx, dy, score): pose pixel (x, y) at ref (x * s + dx, y * s + dy)"""
    mr = base_mask(ref)[0].astype(np.float64)
    mp = base_mask(pose)[0]
    best = (-1, 1.0, 0, 0)
    for s in scales:
        h, w = max(1, int(round(mp.shape[0] * s))), max(1, int(round(mp.shape[1] * s)))
        ps = np.asarray(Image.fromarray(mp.astype(np.uint8) * 255).resize((w, h), Image.BILINEAR),
                        dtype=np.float64) / 255
        cor = fftconvolve(mr, ps[::-1, ::-1], mode='full')
        score = cor / np.sqrt(mr.sum() * ps.sum() + 1e-9)
        j = np.unravel_index(np.argmax(score), score.shape)
        if score[j] > best[0]:
            best = (score[j], s, j[1] - (w - 1), j[0] - (h - 1))
    return best[1], best[2], best[3], best[0]


def growth(key, hand, k):
    """how much the set grows (1: not at all) and its fist's middle in the handed pose's pixels: the fist (the
    widest row of the biggest piece of skin: a bronze base reads as skin too) at FIST of the knees' width"""
    lab, n = ndimage.label(parts(hand)[0])
    skin = lab == np.argmax(ndimage.sum(np.ones(lab.shape), lab, range(1, n + 1))) + 1
    ys, xs = np.nonzero(skin)
    legs = json.load(open(LEGS))['cockpits'].get(key, {}).get('legs', [])
    if key in KIT_SIZE or not legs:
        return 1.0, (xs.mean(), ys.mean())
    knee = np.mean([2 * l['r'][1] for l in legs])
    return float(FIST * knee / (skin.sum(axis=1).max() * k)), (xs.mean(), ys.mean())


def scaled(a, k):
    """a handed pose at k screen pixels to its own, premultiplied"""
    w, h = max(1, int(round(a.shape[1] * k))), max(1, int(round(a.shape[0] * k)))
    return np.asarray(Image.fromarray(premul(a)).resize((w, h), Image.LANCZOS))


def on_screen(m, x0, y0):
    """a mask put at x0, y0 on the screen, clipped"""
    out = np.zeros((SCREEN[1], SCREEN[0]), bool)
    sx0, sy0 = max(0, x0), max(0, y0)
    sx1, sy1 = min(SCREEN[0], x0 + m.shape[1]), min(SCREEN[1], y0 + m.shape[0])
    if sx1 > sx0 and sy1 > sy0:
        out[sy0:sy1, sx0:sx1] = m[sy0 - y0:sy1 - y0, sx0 - x0:sx1 - x0]
    return out


def keep_clear(key, cj):
    """what a hand should not hide: the displays (cockpit.json's regions) and the panel's controls (the built
    panel.json, when there is one; the cabin's lamps aside), as (name, mask)"""
    yy, xx = np.mgrid[0:SCREEN[1], 0:SCREEN[0]]
    out = []
    for n, r in cj.get('regions', {}).items():
        if isinstance(r, dict):
            m = (xx - r['cx']) ** 2 + (yy - r['cy']) ** 2 <= r['r'] ** 2
        else:
            m = np.zeros((SCREEN[1], SCREEN[0]), bool)
            m[r[1]:r[1] + r[3], r[0]:r[0] + r[2]] = True
        out.append((n, m))
    pj = os.path.join(ROOT, 'build', 'panels', key, 'panel.json')
    if os.path.exists(pj):
        for c in json.load(open(pj))['controls']:
            if c['fn'] != 'cabin':
                x, y, w, h = c['rect']
                m = np.zeros((SCREEN[1], SCREEN[0]), bool)
                m[max(0, y):y + h, max(0, x):x + w] = True
                out.append(('%s %d' % (c['fn'], c.get('arg', 0)), m))
    return out


def up(a):
    return np.asarray(Image.fromarray(a).resize((a.shape[1] * UP, a.shape[0] * UP), Image.NEAREST))


def build(key, src, out_root):
    ship = SHIPS[key]
    bare = json.load(open(os.path.join(src, ship, 'bare.json')))
    handed = json.load(open(os.path.join(src, ship, 'poses.json')))
    bframes = {f['id']: f for f in bare['frames']}
    hframes = {f['id']: f for f in handed['frames']}
    # the bare poses, cropped round the stick (screen scale), and the handed poses (their own scale)
    refs, ref_at = {}, {}
    for pid in POSES:
        ov = np.asarray(Image.open(os.path.join(src, bframes[pid]['overlay'])).convert('RGBA'))
        ys, xs = np.nonzero(ov[..., 3] > 0)
        x0, y0 = max(0, xs.min() - 4), max(0, ys.min() - 4)
        refs[pid] = up(np.ascontiguousarray(ov[y0:ys.max() + 5, x0:xs.max() + 5]))
        ref_at[pid] = (x0, y0)
    hands = {pid: np.asarray(Image.open(os.path.join(src, hframes[pid]['file'])).convert('RGBA')) for pid in POSES}
    # the scale: the upright poses' bases (a coarse pass, then a fine one)
    mr, mh = base_mask(refs['CENTER'])[0], base_mask(hands['CENTER'])[0]
    s0 = np.sqrt(mr.sum() / max(1, mh.sum()))
    s, _dx, _dy, _sc = match(refs['CENTER'], hands['CENTER'], s0 * np.linspace(0.8, 1.2, 21))
    s, _dx, _dy, _sc = match(refs['CENTER'], hands['CENTER'], s * np.linspace(0.97, 1.03, 13))
    k = s / UP                          # (handed pixels to screen pixels)
    placed, scores = [], []
    for pid in POSES:
        _s, dx, dy, sc = match(refs[pid], hands[pid], [s])
        scores.append(float(sc))
        placed.append((pid, ref_at[pid][0] + dx / UP, ref_at[pid][1] + dy / UP))
    # the growth: about the upright pose's fist, so the grip stays where the kit's was
    g, (fx, fy) = growth(key, hands['CENTER'], k)
    ax, ay = placed[4][1] + fx * k, placed[4][2] + fy * k
    placed = [(pid, ax + g * (x - ax), ay + g * (y - ay)) for pid, x, y in placed]
    k *= g
    # the drop: every pose's cut edge (its sleeve's lowest row) at or past the screen's last row
    drop = 0.0
    for pid, x, y in placed:
        _skin, sleeve, _own = parts(hands[pid])
        cut = np.nonzero(sleeve.any(axis=1))[0][-1]
        drop = max(drop, (SCREEN[1] - 1) - (y + (cut + 1) * k - 1))
    drop = float(np.ceil(drop))
    d = os.path.join(out_root, key)
    cj = json.load(open(os.path.join(d, 'cockpit.json')))
    hidden = []
    if g != 1:
        keep = keep_clear(key, cj)
        m = scaled(hands['CENTER'], k)[..., 3] >= 128
        x0, y0 = int(round(placed[4][1])), int(round(placed[4][2]))
        hides = [{n for n, t in keep if (on_screen(m, x0, y0 + dd) & t).any()}
                 for dd in range(int(drop), max(int(drop), CLEAR) + 1)]
        i = next(i for i, h in enumerate(hides) if h <= hides[-1])
        drop, hidden = float(int(drop) + i), sorted(hides[i])
    os.makedirs(os.path.join(d, 'stick'), exist_ok=True)
    for f in ('socket.png', 'socket.json'):
        if os.path.exists(os.path.join(d, 'stick', f)):
            os.remove(os.path.join(d, 'stick', f))
    # (the kit's socket painted out: the new stick has its own base, and a leaning one showed the empty hole beside
    # it)
    old = np.asarray(Image.open(os.path.join(d, 'joystick.png')).convert('RGBA'))[..., 3] > 128
    fill_socket(d, cj, old)
    out = []
    for i, (pid, x, y) in enumerate(placed):
        im = scaled(hands[pid], k)
        h, w = im.shape[:2]
        x0, y0 = int(round(x)), int(round(y + drop))
        screen = np.zeros((SCREEN[1], SCREEN[0], 4), np.uint8)
        sx0, sy0 = max(0, x0), max(0, y0)
        sx1, sy1 = min(SCREEN[0], x0 + w), min(SCREEN[1], y0 + h)
        screen[sy0:sy1, sx0:sx1] = im[sy0 - y0:sy1 - y0, sx0 - x0:sx1 - x0]
        screen = unpremul(screen)
        ys, xs = np.nonzero(screen[..., 3] >= 128)
        cx0, cx1, cy0, cy1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
        Image.fromarray(screen[cy0:cy1, cx0:cx1], 'RGBA').save(os.path.join(d, 'stick', 'p%d.png' % i))
        out.append({'file': 'p%d.png' % i, 'pose': pid, 'x': int(cx0), 'y': int(cy0)})
    json.dump({'set': 'arm2', 'scale': round(float(k), 4), 'grown': round(g, 3), 'dropped': drop, 'hides': hidden,
               'mount': bare['world_mount_anchor'], 'match': [round(v, 3) for v in scores], 'poses': out},
              open(os.path.join(d, 'stick', 'stick.json'), 'w'), indent=1)
    return k, g, drop, scores, hidden


def preview(keys, out_root, path):
    """each cockpit with its nine poses, round the stick, magnified"""
    rows = []
    for key in keys:
        d = os.path.join(out_root, key)
        base = Image.open(os.path.join(d, 'base.png')).convert('RGBA')
        sj = json.load(open(os.path.join(d, 'stick', 'stick.json')))
        sock = os.path.join(d, 'stick', 'socket.json')
        if os.path.exists(sock):
            q = json.load(open(sock))
            base.alpha_composite(Image.open(os.path.join(d, 'stick', 'socket.png')).convert('RGBA'), (q['x'], q['y']))
        cx = int(sj['mount'][0])
        x0 = max(0, min(SCREEN[0] - 150, cx - 75))
        row = []
        for p in sj['poses']:
            im = Image.new('RGBA', SCREEN, (20, 20, 30, 255))
            im.alpha_composite(base)
            a = np.asarray(Image.open(os.path.join(d, 'stick', p['file'])).convert('RGBA')).copy()
            a[..., 3] = np.where(a[..., 3] >= 128, 255, 0)
            im.alpha_composite(Image.fromarray(a), (p['x'], p['y']))
            row.append(im.crop((x0, 270, x0 + 150, 400)).convert('RGB'))
        rows.append((key, row))
    Z = 2
    W = 9 * (150 * Z + 4)
    sheet = Image.new('RGB', (W, len(rows) * (130 * Z + 16)), (0, 0, 0))
    dr = ImageDraw.Draw(sheet)
    for j, (key, row) in enumerate(rows):
        y = j * (130 * Z + 16)
        dr.text((4, y + 2), key, fill=(255, 255, 0))
        for i, t in enumerate(row):
            sheet.paste(t.resize((150 * Z, 130 * Z), Image.NEAREST), (i * (150 * Z + 4), y + 14))
    sheet.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('keys', nargs='*')
    ap.add_argument('--zip', required=True)
    ap.add_argument('--out', default=os.path.join(ROOT, 'art', 'cockpits'))
    ap.add_argument('--sheet')
    args = ap.parse_args()
    src = os.path.join(ROOT, 'build', 'arm2')
    with zipfile.ZipFile(args.zip) as z:
        z.extractall(src, [n for n in z.namelist() if '__MACOSX' not in n])
    keys = args.keys or list(SHIPS)
    for key in keys:
        k, g, drop, scores, hidden = build(key, src, args.out)
        print('joysticks: %-8s scale %.3f (grown %.2f), lowered %2d px, poses on their bare ones %s%s'
              % (key, k, g, drop, ' '.join('%.2f' % v for v in scores),
                 '; upright, it hides ' + ', '.join(hidden) if hidden else ''))
    if args.sheet:
        preview(keys, args.out, args.sheet)


if __name__ == '__main__':
    main()
