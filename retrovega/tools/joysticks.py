#!/usr/bin/env python3
"""The pilot's hand on the joystick: nine poses a cockpit, from the user's sprite sheets (art/cockpits/KEY/stick/).

    joysticks.py --zip AllJoysticks.zip [--out art/cockpits] [--sheet docs/joysticks.png] [KEY ...]

The user's ask (2026-10-23): renderings of the stick pushed each way with the pilot's hand on it, in place of the
old stick that slid about; the arm running out of the frame.  Their zip holds 3 x 3 sheets (rows: pushed forward,
the middle, pulled back; columns: left, the middle, right) and, where a sheet's column came out wrong (the arm from
the other side), a strip of three poses to replace it.  Each cockpit gets the sheet that matches its stick (CHOICE):

  * the poses cut out by their outlines (the sheets are not on an exact grid), sorted into rows and columns;
  * each lined up on the middle pose by its base (the part that does not move: the pixels that are neither skin
    nor sleeve, in the bottom of the stick), matched by overlap over a range of scales and offsets, so the base
    stays put from pose to pose and only the stick and the hand move;
  * scaled so the middle pose's stick is the old stick's height, its base where the old one stood;
  * the sleeve extended along the forearm, a plain shaded tube with the sleeve's own colours across it, until it
    runs off the screen's edge (the sheets cut it short).

Out: art/cockpits/KEY/stick/p0.png .. p8.png (pose row * 3 + column, cropped) and stick.json (each pose's place on
the screen); rvpack packs them, cockpit.c draws the one the steering picks.
"""
import argparse
import json
import os
import re
import zipfile

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage
from scipy.signal import fftconvolve

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SCREEN = (640, 400)
# the sheet for each cockpit's stick, and the strips that replace a column of it ({column: strip}); MIRROR: drawn
# the other way round (the user's, 2026-10-23: the sidestick sheets have the arm from the left, the left hand
# reaching across the cockpit; mirrored, it is the pilot's right arm)
CHOICE = {'SCHROED': (12, {2: 15}), 'FRANKLN': (6, {}), 'GAWAIN': (4, {2: 14}), 'REDEEMR': (11, {2: 19}),
          'PACIFER': (13, {2: 17}), 'LANCELT': (1, {}), 'PROGENY': (8, {0: 20}), 'GODDARD': (5, {})}
MIRROR = {'FRANKLN', 'PROGENY'}
DROP_MAX = {'FRANKLN': 0}               # (pixels a stick may be lowered, where not by the rule)
DROP_MORE = {'GODDARD': 10}             # (and lowered further: the Goddard's sheet has a slight forearm)


def sheets_of(zip_path):
    """{number: RGBA image} from the zip (the number at the end of each name)"""
    out = {}
    with zipfile.ZipFile(zip_path) as z:
        for name in z.namelist():
            m = re.search(r'-(\d+)\.png$', name)
            if m and '__MACOSX' not in name:
                with z.open(name) as f:
                    out[int(m.group(1))] = Image.open(f).convert('RGBA')
    return out


def poses_of(im, n):
    """the n biggest figures of a sheet (by outline), as (crop RGBA array, x, y), in reading order"""
    a = np.asarray(im)
    solid = ndimage.binary_dilation(a[..., 3] > 16, iterations=6)
    lab, k = ndimage.label(solid)
    sizes = ndimage.sum(np.ones(lab.shape), lab, range(1, k + 1))
    keep = np.argsort(sizes)[::-1][:n] + 1
    out = []
    for i in keep:
        sl = ndimage.find_objects((lab == i).astype(int))[0]
        crop = a[sl].copy()
        crop[..., 3] = np.where(lab[sl] == i, crop[..., 3], 0)
        out.append((crop, sl[1].start, sl[0].start))
    # rows by y (three a row for a sheet, one for a strip), then columns by x
    out.sort(key=lambda p: p[2] + p[0].shape[0] / 2)
    per = 3 if n == 9 else 1
    rows = [sorted(out[i:i + per], key=lambda p: p[1] + p[0].shape[1] / 2) for i in range(0, n, per)]
    return [p for r in rows for p in r]


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


def register(ref, pose):
    """the scale and offset that put pose's base on ref's: (s, dx, dy), pose pixel (x, y) -> ref (x * s + dx, ...)"""
    mr, _t, br = base_mask(ref)
    mp, _t2, bp = base_mask(pose)
    s0 = np.sqrt(mr.sum() / max(1, mp.sum()))
    best = (-1, 1.0, 0, 0)
    D = 2                               # (at half size, then refined)
    rs = np.asarray(Image.fromarray(mr.astype(np.uint8) * 255).resize((mr.shape[1] // D, mr.shape[0] // D),
                                                                      Image.BILINEAR), dtype=np.float64) / 255
    for s in s0 * np.linspace(0.86, 1.14, 15):
        h, w = max(1, int(mp.shape[0] * s / D)), max(1, int(mp.shape[1] * s / D))
        ps = np.asarray(Image.fromarray(mp.astype(np.uint8) * 255).resize((w, h), Image.BILINEAR),
                        dtype=np.float64) / 255
        cor = fftconvolve(rs, ps[::-1, ::-1], mode='full')
        score = cor / np.sqrt(rs.sum() * ps.sum() + 1e-9)
        j = np.unravel_index(np.argmax(score), score.shape)
        if score[j] > best[0]:
            # (full mode: index j means the pose's (0, 0) at ref (j - (h - 1)))
            best = (score[j], s, (j[1] - (w - 1)) * D, (j[0] - (h - 1)) * D)
    return best[1], best[2], best[3], best[0]


def warp(a, s, dx, dy, shape):
    """a pose's RGBA into ref's frame (shape h, w), scaled by s and offset (premultiplied, so edges stay clean)"""
    h, w = a.shape[:2]
    im = Image.fromarray(premul(a))
    im = im.resize((max(1, int(round(w * s))), max(1, int(round(h * s)))), Image.LANCZOS)
    out = Image.new('RGBA', (shape[1], shape[0]), (0, 0, 0, 0))
    out.paste(im, (int(round(dx)), int(round(dy))))
    return np.asarray(out)


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


def extend_sleeve(a, to_edge):
    """the sleeve run on along the forearm (a plain tube: the sleeve's colours across it, averaged along it) until
    to_edge(points) says each point is off the screen; a: RGBA (straight) in the frame where to_edge applies"""
    skin, sleeve, _own = parts(a)
    if sleeve.sum() < 50 or skin.sum() < 50:
        return a
    ys, xs = np.nonzero(sleeve)
    # the forearm's axis: from the hand (the skin's middle) to the sleeve's (the cuffs are often wider than long,
    # so the sleeve's own long axis can lie across the arm)
    hy, hx = np.nonzero(skin)
    W = np.array([xs.mean(), ys.mean()])
    d = W - np.array([hx.mean(), hy.mean()])
    d = d / max(1e-6, np.hypot(*d))
    n = np.array([-d[1], d[0]])
    P = np.stack([xs - W[0], ys - W[1]], -1)
    p, q = P @ d, P @ n
    pmax = np.percentile(p, 99.5)
    far = p > np.median(p)
    width = np.percentile(q[far], 98) - np.percentile(q[far], 2)
    p_cut = pmax - 0.30 * width         # (before the cuff's ragged end)
    band = (p > p_cut - 0.45 * width) & (p <= p_cut)
    if band.sum() < 20:
        return a
    qb = q[band]
    # (the tube as wide as the sleeve itself: the cut end, sliced on a slant by the sheet, is narrower)
    q0, q1 = np.percentile(q[far], 2), np.percentile(q[far], 98)
    bins = np.linspace(q0, q1, 24)
    cols = a[ys[band], xs[band], :3].astype(np.float64)
    prof = np.zeros((len(bins) - 1, 3))
    for i in range(len(bins) - 1):
        sel = (qb >= bins[i]) & (qb < bins[i + 1])
        prof[i] = cols[sel].mean(0) if sel.any() else cols.mean(0)
    # how far to run: until both edges of the tube are off the screen
    t = p_cut
    step = 2.0
    for _ in range(4000):
        e0 = W + d * t + n * q0
        e1 = W + d * t + n * q1
        if to_edge(e0) and to_edge(e1):
            break
        t += step
    t_end = t + 4
    # grow the canvas if the tube leaves it
    corners = [W + d * pp + n * qq for pp in (p_cut, t_end) for qq in (q0, q1)]
    xs_c = [c[0] for c in corners]
    ys_c = [c[1] for c in corners]
    pad_l = int(max(0, -min(xs_c))) + 2
    pad_t = int(max(0, -min(ys_c))) + 2
    pad_r = int(max(0, max(xs_c) - a.shape[1])) + 2
    pad_b = int(max(0, max(ys_c) - a.shape[0])) + 2
    out = np.zeros((a.shape[0] + pad_t + pad_b, a.shape[1] + pad_l + pad_r, 4), np.uint8)
    out[pad_t:pad_t + a.shape[0], pad_l:pad_l + a.shape[1]] = a
    W2 = W + np.array([pad_l, pad_t])
    gy, gx = np.mgrid[0:out.shape[0], 0:out.shape[1]]
    G = np.stack([gx - W2[0], gy - W2[1]], -1)
    gp, gq = G @ d, G @ n
    # the old end past the cut goes; the tube from the cut on
    sl_full = np.zeros(out.shape[:2], bool)
    sl_full[pad_t:pad_t + a.shape[0], pad_l:pad_l + a.shape[1]] = sleeve
    out[sl_full & (gp > p_cut)] = 0
    tube = (gp > p_cut - 1) & (gp <= t_end) & (gq >= q0) & (gq <= q1)
    idx = np.clip(((gq - q0) / (q1 - q0) * (len(bins) - 1)).astype(int), 0, len(bins) - 2)
    rng = np.random.default_rng(3)
    keep = tube & (out[..., 3] < 128)   # (what the sheet already has stays: the wrist end of the band)
    out[keep, :3] = np.clip(prof[idx[keep]] * (1 + rng.normal(0, 0.03, (int(keep.sum()), 1))), 0, 255)
    out[keep, 3] = 255
    return out, (pad_l, pad_t)


def fill_socket(d, cj, old):
    """the old stick's socket painted out of the base (its hole and collar: the new stick stands lower, its own
    base in the picture): stick/socket.png, the changed pixels, for tools/panels.py to lay on the base.  The hole is
    the biggest dark blob under the old stick; it and a collar round it are filled with the plate round them (the
    edge's colours spread in), the plate's grain on top (the fine detail of a plain patch of it nearby)"""
    base = np.asarray(Image.open(os.path.join(d, 'base.png')).convert('RGBA')).copy()
    jx, jy = cj['controls']['joystick']['x'], cj['controls']['joystick']['y']
    h, w = old.shape
    foot = np.zeros(base.shape[:2], bool)              # (the old stick but its top quarter, and a margin round it)
    foot[jy + h // 4:jy + h, jx:jx + w] = old[h // 4:]
    foot = ndimage.binary_dilation(foot, iterations=10) & (base[..., 3] >= 128)
    rgb = base[..., :3].astype(np.float64)
    L = rgb @ [0.299, 0.587, 0.114]
    dark = foot & (L < min(50, np.percentile(L[foot], 25)))
    dark = ndimage.binary_opening(dark, iterations=1)
    lab, k = ndimage.label(dark)
    if not k:
        return
    hole = lab == (np.argmax(ndimage.sum(np.ones(lab.shape), lab, range(1, k + 1))) + 1)
    hole = ndimage.binary_fill_holes(hole)
    r = np.sqrt(hole.sum() / np.pi)
    region = ndimage.binary_dilation(hole, iterations=max(3, int(round(r * 0.8)))) & (base[..., 3] >= 128)
    # the colours spread in from the edge (a diffusion)
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
    # the grain: the fine detail of the plainest patch of plate beside it
    ys, xs = np.nonzero(region)
    hp = rgb - ndimage.gaussian_filter(rgb, (2, 2, 0))
    best = None
    for dx, dy in [(int(round(m * (np.ptp(xs) + 4))) * sx, 0) for m in (1, 1.5) for sx in (-1, 1)] + \
                  [(0, int(round(m * (np.ptp(ys) + 4))) * sy) for m in (1, 1.5) for sy in (-1, 1)]:
        sy_, sx_ = ys + dy, xs + dx
        if sy_.min() < 0 or sx_.min() < 0 or sy_.max() >= 400 or sx_.max() >= 640:
            continue
        if region[sy_, sx_].any() or (base[sy_, sx_, 3] < 128).any():
            continue
        e = np.abs(hp[sy_, sx_]).mean(-1)
        cost = np.percentile(e, 97)     # (no lines or edges in it)
        if best is None or cost < best[0]:
            best = (cost, dx, dy)
    out = base.copy()
    grain = hp[ys + best[2], xs + best[1]] if best else 0
    out[ys, xs, :3] = np.clip(low[ys, xs] + grain * 0.9, 0, 255).astype(np.uint8)
    y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
    patch = out[y0:y1, x0:x1].copy()
    patch[..., 3] = np.where(region[y0:y1, x0:x1], 255, 0)
    Image.fromarray(patch, 'RGBA').save(os.path.join(d, 'stick', 'socket.png'))
    json.dump({'x': int(x0), 'y': int(y0), 'grain_from': [int(best[1]), int(best[2])] if best else None},
              open(os.path.join(d, 'stick', 'socket.json'), 'w'))


def build(key, sheets, out_root):
    num, fixes = CHOICE[key]
    poses = poses_of(sheets[num], 9)
    for col, strip in fixes.items():
        for row, p in enumerate(poses_of(sheets[strip], 3)):
            poses[row * 3 + col] = p
    arrays = [p[0] for p in poses]
    if key in MIRROR:                   # (mirrored: a left lean becomes a right one, so the columns swap too)
        arrays = [np.ascontiguousarray(arrays[r * 3 + (2 - c)][:, ::-1]) for r in range(3) for c in range(3)]
    ref = arrays[4]
    # every pose into the middle one's frame, its base on the middle one's (a margin round it for the leans)
    M = int(max(ref.shape) * 0.6)
    frame = (ref.shape[0] + 2 * M, ref.shape[1] + 2 * M)
    framed = []
    scores = []
    for i, a in enumerate(arrays):
        if i == 4:
            s, dx, dy, sc = 1.0, 0, 0, 1.0
        else:
            s, dx, dy, sc = register(ref, a)
        scores.append(sc)
        framed.append(unpremul(warp(a, s, dx + M, dy + M, frame)))
    # the old stick: its height and its base's bottom middle, on the screen
    d = os.path.join(out_root, key)
    cj = json.load(open(os.path.join(d, 'cockpit.json')))
    old = np.asarray(Image.open(os.path.join(d, 'joystick.png')).convert('RGBA'))[..., 3] > 128
    oy = np.nonzero(old.any(axis=1))[0]
    old_h = oy[-1] - oy[0] + 1
    ox = np.nonzero(old[oy[-1] - 3:oy[-1] + 1].any(axis=0))[0]
    anchor = (cj['controls']['joystick']['x'] + (ox[0] + ox[-1]) / 2.0, cj['controls']['joystick']['y'] + oy[-1] + 1)
    mb, top, bottom = base_mask(framed[4])
    bx = np.nonzero(mb[bottom - 3:bottom + 1].any(axis=0))[0]
    art_anchor = ((bx[0] + bx[-1]) / 2.0, bottom + 1)
    k = old_h / float(bottom - top + 1)
    # (the user's, 2026-10-23: no stretched sleeves) the stick set low enough that every pose's sleeve end, as the
    # sheet cut it, is just off the bottom of the screen: the screen crops the arm
    # (the user's balance, 2026-10-23: lowered only until the base is flush with the bottom, a sliver of it cropped -
    # pushed further the hand sank too far; a pose whose sleeve still shows has it run on the pixels left; the
    # Franklin's stick, by the right edge, stays where it stood)
    drop = 0.0
    for a in framed:
        e = sleeve_end(a)
        if len(e):
            drop = max(drop, SCREEN[1] + 1 - ((e[:, 1].min() - art_anchor[1]) * k + anchor[1]))
    drop = min(drop, max(0.0, SCREEN[1] + 0.06 * old_h - anchor[1]) + DROP_MORE.get(key, 0), DROP_MAX.get(key, 1e9))
    anchor = (anchor[0], anchor[1] + drop)
    to_screen = lambda P: (np.asarray(P) - art_anchor) * k + anchor
    off = lambda P: (lambda S: S[0] < -2 or S[0] > SCREEN[0] + 2 or S[1] > SCREEN[1] + 2)(to_screen(P))
    os.makedirs(os.path.join(d, 'stick'), exist_ok=True)
    fill_socket(d, cj, old)
    placed = []
    for i, a in enumerate(framed):
        e = sleeve_end(a)
        if len(e) and ((e[:, 1].min() - art_anchor[1]) * k + anchor[1]) <= SCREEN[1]:
            res = extend_sleeve(a, off)
            a2, (pl, pt) = res if isinstance(res, tuple) else (res, (0, 0))
        else:
            a2, pl, pt = a, 0, 0
        # onto the screen: scaled by k, its art anchor at the screen anchor
        im = Image.fromarray(premul(a2))
        w, h = max(1, int(round(a2.shape[1] * k))), max(1, int(round(a2.shape[0] * k)))
        im = np.asarray(im.resize((w, h), Image.LANCZOS))
        x0 = int(round(anchor[0] - (art_anchor[0] + pl) * k))
        y0 = int(round(anchor[1] - (art_anchor[1] + pt) * k))
        screen = np.zeros((SCREEN[1], SCREEN[0], 4), np.uint8)
        sx0, sy0 = max(0, x0), max(0, y0)
        sx1, sy1 = min(SCREEN[0], x0 + w), min(SCREEN[1], y0 + h)
        screen[sy0:sy1, sx0:sx1] = im[sy0 - y0:sy1 - y0, sx0 - x0:sx1 - x0]
        screen = unpremul(screen)
        ys, xs = np.nonzero(screen[..., 3] >= 128)
        cx0, cx1, cy0, cy1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
        Image.fromarray(screen[cy0:cy1, cx0:cx1], 'RGBA').save(os.path.join(d, 'stick', 'p%d.png' % i))
        placed.append({'file': 'p%d.png' % i, 'x': int(cx0), 'y': int(cy0)})
    json.dump({'sheet': num, 'fixes': {str(c): v for c, v in fixes.items()}, 'scale': round(float(k), 4),
               'dropped': round(float(drop), 1), 'anchor': [round(float(v), 1) for v in anchor],
               'match': [round(float(s), 3) for s in scores], 'poses': placed},
              open(os.path.join(d, 'stick', 'stick.json'), 'w'), indent=1)
    return placed, scores


def preview(keys, out_root, path):
    """each cockpit's base with its nine poses, magnified round the stick"""
    tiles = []
    for key in keys:
        d = os.path.join(out_root, key)
        base = Image.open(os.path.join(d, 'base.png')).convert('RGBA')
        sj = json.load(open(os.path.join(d, 'stick', 'stick.json')))
        xs = [p['x'] for p in sj['poses']]
        ys = [p['y'] for p in sj['poses']]
        x0, y0 = max(0, min(xs) - 10), max(0, min(ys) - 10)
        x1 = min(640, max(p['x'] + Image.open(os.path.join(d, 'stick', p['file'])).width for p in sj['poses']) + 10)
        row = []
        for p in sj['poses']:
            im = Image.new('RGBA', (640, 400), (20, 20, 30, 255))
            im.alpha_composite(base)
            st = Image.open(os.path.join(d, 'stick', p['file'])).convert('RGBA')
            a = np.asarray(st).copy()
            a[..., 3] = np.where(a[..., 3] >= 128, 255, 0)
            im.alpha_composite(Image.fromarray(a), (p['x'], p['y']))
            row.append(im.crop((x0, y0, x1, 400)).convert('RGB'))
        tiles.append((key, row))
    Z = 2
    W = max(sum(t.width * Z + 4 for t in r) for _k, r in tiles)
    H = sum(r[0].height * Z + 16 for _k, r in tiles)
    sheet = Image.new('RGB', (W, H), (0, 0, 0))
    dr = ImageDraw.Draw(sheet)
    y = 0
    for key, row in tiles:
        dr.text((4, y + 2), key, fill=(255, 255, 0))
        x = 0
        for t in row:
            sheet.paste(t.resize((t.width * Z, t.height * Z), Image.NEAREST), (x, y + 14))
            x += t.width * Z + 4
        y += row[0].height * Z + 16
    sheet.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('keys', nargs='*')
    ap.add_argument('--zip', required=True)
    ap.add_argument('--out', default=os.path.join(ROOT, 'art', 'cockpits'))
    ap.add_argument('--sheet')
    args = ap.parse_args()
    sheets = sheets_of(args.zip)
    keys = args.keys or list(CHOICE)
    for key in keys:
        placed, scores = build(key, sheets, args.out)
        print('joysticks: %-8s sheet %2d, poses matched on their bases %s' % (key, CHOICE[key][0],
                                                                            ' '.join('%.2f' % s for s in scores)))
    if args.sheet:
        preview(keys, args.out, args.sheet)


if __name__ == '__main__':
    main()
