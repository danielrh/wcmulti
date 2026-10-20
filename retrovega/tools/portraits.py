#!/usr/bin/env python3
"""The cast's portraits: generated art -> aligned talking faces.

    portraits.py [KEY ...] [--src art/people/src] [--out art/people/faces]
                 [--gifs build/people] [--size 256]
    portraits.py --gallery docs/people.png    the cast sheet, and GIFS/cast.gif

The portraits are drawn by image synthesis (docs/portrait-prompts.md) and
arrive as chat exports, one folder per character.  art/people/manifest.csv
says which file is which frame:

    key,frame,file
    COMET,base,w3/chat-image-012.png
    COMET,mouth_1,w3/chat-image-007.png
    ...

frame is base, mouth_1..8, eyes_1..2 or comm (the headset look, kept for
later).  A missing frame borrows its nearest shape (STAND_IN).

Each frame is a separate generation, so none lines up exactly with the
base: some are framed differently, all are redrawn a little.  For every
frame the tool finds the scale and shift that best fit it onto the base
(edges of the whole bust, coarse to fine), then cuts the two boxes.  Each
box frame is the base with the edit blended in towards the middle, so its
border matches the base exactly and no seam shows when the engine pastes
it.

For each character OUT/KEY/ holds, at --size:

    base.png          the bust at rest, alpha cut out (RGBA)
    mouth_0..8.png    the mouth box in each of lipsync.py's nine shapes
    eyes_0..2.png     the eye box open, half closed, closed
    comm.png          the headset look, where there is one
    face.json         where the boxes sit, and how each frame was fitted

and GIFS/KEY.gif is the character saying their line, blinking.

Framing is normalised across the cast: the eyes sit at EYE_Y of the height
and are EYE_SPAN of the width apart, measured on each base.
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
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import lipsync  # noqa: E402

SLATE = np.array([0x1C, 0x28, 0x30]) / 255.0
FRAMES = ['base'] + ['mouth_%d' % i for i in range(1, 9)] + ['eyes_1', 'eyes_2']
STAND_IN = {'mouth_1': 'base', 'mouth_2': 'mouth_1', 'mouth_3': 'mouth_4', 'mouth_4': 'mouth_3',
            'mouth_5': 'mouth_6', 'mouth_6': 'mouth_5', 'mouth_7': 'mouth_8', 'mouth_8': 'mouth_7',
            'eyes_1': 'eyes_2', 'eyes_2': 'base'}


# ---------------------------------------------------------------- loading

def load(path):
    """RGBA float32 in 0..1.  An opaque image has its flat backdrop keyed out."""
    im = Image.open(path).convert('RGBA')
    a = np.asarray(im).astype(np.float32) / 255.0
    if a[..., 3].min() > 0.99:
        a[..., 3] = key_out(a[..., :3])
    return a


def key_out(rgb):
    """Alpha for a bust painted on a flat (slightly vignetted) backdrop.

    The backdrop is fitted as a quadratic over the top rows and the upper
    side columns; the bust is whatever differs from it and is connected to
    the bottom edge."""
    h, w = rgb.shape[:2]
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    sel = np.zeros((h, w), bool)
    sel[:h // 16] = True
    sel[:h // 2, :w // 16] = True
    sel[:h // 2, -w // 16:] = True
    X = np.stack([np.ones_like(xx), xx / w, yy / h, (xx / w) ** 2, (yy / h) ** 2, xx * yy / (w * h)], -1)
    coef, *_ = np.linalg.lstsq(X[sel], rgb[sel], rcond=None)
    bg = X @ coef
    d = np.linalg.norm(rgb - bg, axis=-1)
    fg = d > 0.10
    fg = ndimage.binary_opening(fg, iterations=2)
    lab, n = ndimage.label(fg)
    keep = np.unique(lab[-1][lab[-1] > 0])
    fg = np.isin(lab, keep)
    fg = ndimage.binary_fill_holes(fg)
    return np.clip(ndimage.gaussian_filter(fg.astype(np.float32), 1.2), 0, 1)


def over(rgba, bg=SLATE):
    a = rgba[..., 3:4]
    return rgba[..., :3] * a + bg * (1 - a)


# ---------------------------------------------------------------- fitting

def resize(img, size):
    """Area-average resize of an HxWxC or HxW float array to size x size."""
    chans = img.shape[2] if img.ndim == 3 else 1
    pil = [Image.fromarray(img[..., c] if img.ndim == 3 else img, 'F') for c in range(chans)]
    out = [np.asarray(p.resize((size, size), Image.BOX if size < img.shape[0] else Image.BICUBIC)) for p in pil]
    return np.stack(out, -1) if img.ndim == 3 else out[0]


def edges(rgba, size):
    """Gradient magnitude of the luminance over mid grey: what the fit compares."""
    g = over(rgba, np.array([0.5, 0.5, 0.5])) @ np.array([0.30, 0.59, 0.11], np.float32)
    g = resize(g.astype(np.float32), size)
    g = ndimage.gaussian_filter(g, size / 400.0)
    e = np.hypot(ndimage.sobel(g, 0), ndimage.sobel(g, 1))
    return (e - e.mean()) / (e.std() + 1e-6)


def warp(img, s, tx, ty, order=1):
    """Scale img by s about its centre, then shift by (tx, ty) in pixels of img."""
    h, w = img.shape[:2]
    c = np.array([(h - 1) / 2.0, (w - 1) / 2.0])
    t = np.array([ty, tx])
    off = c - (c + t) / s
    if img.ndim == 2:
        return ndimage.affine_transform(img, np.eye(2) / s, off, order=order, mode='constant')
    return np.stack([ndimage.affine_transform(img[..., k], np.eye(2) / s, off, order=order, mode='constant')
                     for k in range(img.shape[2])], -1)


def xcorr_shift(a, b):
    """Shift (dx, dy) that moves b onto a, by FFT cross-correlation with a sub-pixel peak."""
    win = np.outer(np.hanning(a.shape[0]), np.hanning(a.shape[1]))
    F = np.fft.rfft2(a * win) * np.conj(np.fft.rfft2(b * win))
    r = np.fft.irfft2(F, a.shape)
    i, j = np.unravel_index(np.argmax(r), r.shape)
    peak = r[i, j] / (np.sqrt(((a * win) ** 2).sum() * ((b * win) ** 2).sum()) + 1e-9)

    def sub(m, p, n):
        y0, y1, y2 = m[(p - 1) % n], m[p], m[(p + 1) % n]
        d = (y0 - 2 * y1 + y2)
        return 0.0 if d == 0 else 0.5 * (y0 - y2) / d
    dy = i + sub(r[:, j], i, r.shape[0])
    dx = j + sub(r[i, :], j, r.shape[1])
    if dy > a.shape[0] / 2:
        dy -= a.shape[0]
    if dx > a.shape[1] / 2:
        dx -= a.shape[1]
    return dx, dy, peak


def fit(ref, img):
    """(s, tx, ty, score): warp(img, s, tx, ty) best matches ref.

    tx, ty are in pixels of a frame of ref's size (images are square)."""
    best = None
    for size, scales in ((128, np.geomspace(0.72, 1.40, 41)), (320, None), (640, None)):
        er, ei = edges(ref, size), edges(img, size)
        if scales is None:
            s0 = best[0]
            scales = s0 * (1 + np.linspace(-0.02, 0.02, 9) * (128.0 / size) * 2)
        cand = []
        for s in scales:
            dx, dy, peak = xcorr_shift(er, warp(ei, s, 0, 0))
            cand.append((peak, s, dx, dy))
        peak, s, dx, dy = max(cand)
        k = ref.shape[0] / float(size)
        best = (s, dx * k, dy * k, peak)
    return best


def fitted(ref, img):
    """img fitted onto ref, how, and where it covers ref (a frame framed tighter
    than the base leaves the base's edges uncovered)."""
    s, tx, ty, score = fit(ref, img)
    cover = warp(np.ones(img.shape[:2], np.float32), s, tx, ty, order=0) > 0.5
    cover = ndimage.binary_erosion(cover, iterations=max(2, img.shape[0] // 150))
    return warp(img, s, tx, ty), dict(scale=round(float(s), 4), dx=round(float(tx), 1),
                                      dy=round(float(ty), 1), score=round(float(score), 3)), cover


# ---------------------------------------------------------------- colour

def srgb_to_lab(rgb):
    c = np.where(rgb <= 0.04045, rgb / 12.92, ((rgb + 0.055) / 1.055) ** 2.4)
    xyz = c @ np.array([[0.4124, 0.2126, 0.0193], [0.3576, 0.7152, 0.1192], [0.1805, 0.0722, 0.9505]])
    xyz /= np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116.0)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


def lab_to_srgb(lab):
    fy = (lab[..., 0] + 16) / 116.0
    f = np.stack([fy + lab[..., 1] / 500.0, fy, fy - lab[..., 2] / 200.0], -1)
    xyz = np.where(f > 0.2069, f ** 3, (f - 16 / 116.0) / 7.787) * np.array([0.95047, 1.0, 1.08883])
    c = xyz @ np.array([[3.2406, -0.9689, 0.0557], [-1.5372, 1.8758, -0.2040], [-0.4986, 0.0415, 1.0570]])
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, 12.92 * c, 1.055 * c ** (1 / 2.4) - 0.055)


def hue_sat(rgb):
    mx, mn = rgb.max(-1), rgb.min(-1)
    d = mx - mn + 1e-6
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    h = np.where(mx == r, ((g - b) / d) % 6, np.where(mx == g, (b - r) / d + 2, (r - g) / d + 4)) * 60
    return h, d / (mx + 1e-6)


# The hero's slate-blue suit and gold patch, worn by pilots who began as
# hero portraits (the Mercer twins), become the pilots' olive drab and orange.
# Lab statistics measured on the pilots' suits and patches (Kestrel, Drifter, Frost, Echo).
OLIVE = dict(mean=np.array([32.0, -1.5, 15.0]), std=np.array([13.0, 2.4, 8.5]))
ORANGE = dict(mean=np.array([50.0, 39.0, 51.0]), std=np.array([10.0, 8.0, 10.0]))


def soft_band(h, lo, hi, edge=12.0):
    return np.clip(np.minimum(h - lo, hi - h) / edge + 0.5, 0, 1)


def transfer(lab, w, target):
    """Move the Lab statistics of the weighted pixels onto target's."""
    ws = w.sum() + 1e-6
    m = (lab * w[..., None]).sum((0, 1)) / ws
    s = np.sqrt((((lab - m) ** 2) * w[..., None]).sum((0, 1)) / ws) + 1e-6
    out = (lab - m) / s * target['std'] + target['mean']
    return lab * (1 - w[..., None]) + out * w[..., None]


def suit_to_olive(rgba, chin_y):
    """Recolour the blue suit below chin_y (a row above the collar, below the
    face's blue rim light) and the gold shoulder patch."""
    rgb = rgba[..., :3]
    h, w = rgb.shape[:2]
    hue, sat = hue_sat(rgb)
    below = np.zeros((h, w), np.float32)
    below[int(chin_y):] = 1
    below = ndimage.gaussian_filter(below, h / 200.0)
    suit = soft_band(hue, 185, 260) * np.clip((sat - 0.10) / 0.10, 0, 1) * below * rgba[..., 3]
    patch_zone = np.zeros((h, w), np.float32)
    patch_zone[int(chin_y + 0.08 * h):, int(0.78 * w):] = 1
    patch = soft_band(hue, 28, 62) * np.clip((sat - 0.25) / 0.15, 0, 1) * patch_zone * rgba[..., 3]
    lab = srgb_to_lab(rgb)
    lab = transfer(lab, suit, OLIVE)
    lab = transfer(lab, patch, ORANGE)
    out = rgba.copy()
    out[..., :3] = lab_to_srgb(lab)
    return out


# ---------------------------------------------------------------- the face

def luma(rgba):
    return over(rgba, np.array([0.5, 0.5, 0.5])) @ np.array([0.30, 0.59, 0.11], np.float32)


def change(a, b, sigma, cover=None):
    """Where b differs from a, smoothed (0..1-ish); nothing where b has no pixels."""
    d = np.abs(over(a) - over(b)).sum(-1)
    if cover is not None:
        d = d * cover
    return ndimage.gaussian_filter(d, sigma)


def eye_centres(d_eyes, alpha):
    """The two eye centres, from where the blink frames differ from the base,
    looked for in the upper part of the head."""
    h, w = d_eyes.shape
    m = d_eyes * (alpha > 0.5)
    m[:, :int(0.2 * w)] = 0
    m[:, -int(0.2 * w):] = 0
    top = int(np.argmax(alpha[:, int(.35 * w):int(.65 * w)].mean(1) > 0.5))
    m[:top] = 0
    m[top + int(0.45 * h):] = 0
    y = int(np.argmax(m.max(1)))
    band = m[max(0, y - h // 30):y + h // 30]
    prof = band.max(0)
    mid = int((prof * np.arange(w)).sum() / (prof.sum() + 1e-9))
    xl = int(np.argmax(prof[:mid]))
    xr = mid + int(np.argmax(prof[mid:]))
    yl = max(0, y - h // 30) + int(np.argmax(band[:, xl]))
    yr = max(0, y - h // 30) + int(np.argmax(band[:, xr]))
    return (xl, yl), (xr, yr)


def boxes(base, frames, covers, sigma):
    """Eye box and mouth box (x0, y0, x1, y1) on base, from what the frames change.

    Every frame is redrawn a little all over, so the eyes are found where the
    blink frames change and the mouth frames do not."""
    h, w = base.shape[:2]
    mouths = [k for k in frames if k.startswith('mouth_')]
    d_ms = [change(base, frames[k], sigma, covers[k]) for k in mouths]
    d_e = sum(change(base, frames[k], sigma, covers[k]) for k in ('eyes_1', 'eyes_2') if k in frames)
    if d_ms:
        d_e = np.maximum(d_e - 2 * np.mean(d_ms, 0), 0)
    (xl, yl), (xr, yr) = eye_centres(d_e, base[..., 3])
    d = float(xr - xl)
    ye = (yl + yr) / 2.0
    eye = [xl - 0.62 * d, ye - 0.42 * d, xr + 0.62 * d, ye + 0.30 * d]
    d_m = np.max(d_ms, 0) if d_ms else np.zeros((h, w))
    xc = (xl + xr) / 2.0
    zone = np.zeros((h, w), bool)
    zone[int(ye + 0.75 * d):int(ye + 2.3 * d), int(xc - 0.9 * d):int(xc + 0.9 * d)] = True
    floor = np.percentile(d_m[zone], 50) if zone.any() else 0
    hot = (d_m > max(floor * 3, 0.12)) & zone
    if hot.sum() > 50:
        ys, xs = np.nonzero(hot)
        ym = float(np.percentile(ys, 50))
        top = min(np.percentile(ys, 2), ym - 0.30 * d)
        bot = max(np.percentile(ys, 99), ym + 0.40 * d)
        left = min(np.percentile(xs, 1), xc - 0.55 * d)
        right = max(np.percentile(xs, 99), xc + 0.55 * d)
    else:
        ym = ye + 1.25 * d
        top, bot, left, right = ym - 0.32 * d, ym + 0.48 * d, xc - 0.6 * d, xc + 0.6 * d
    pad = 0.10 * d
    mouth = [left - pad, max(top - pad, eye[3] + 2), right + pad, bot + pad]
    return [int(round(v)) for v in eye], [int(round(v)) for v in mouth], dict(
        eyes=[[xl, yl], [xr, yr]], span=d)


def window(shape, box, feather):
    """1 inside box, ramping to 0 at its border over feather pixels."""
    h, w = shape
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    x0, y0, x1, y1 = box
    dist = np.minimum.reduce([xx - x0, x1 - 1 - xx, yy - y0, y1 - 1 - yy])
    return np.clip(dist / feather, 0, 1)


def blend_in(base, edit, box, sigma, cover=None):
    """The edit inside box, blended to the base at its edges, tone-matched to it."""
    x0, y0, x1, y1 = box
    d = change(base, edit, sigma, cover)
    moved = ndimage.binary_dilation(d > 0.10, iterations=int(sigma * 2) + 1)
    keep = (~moved).astype(np.float32) * base[..., 3]
    if cover is not None:
        keep *= cover
    big = 6 * sigma
    diff = (over(base) - over(edit)) * keep[..., None]
    num = np.stack([ndimage.gaussian_filter(diff[..., c], big) for c in range(3)], -1)
    den = ndimage.gaussian_filter(keep, big)[..., None] + 1e-4
    toned = edit.copy()
    toned[..., :3] = np.clip(edit[..., :3] + num / den, 0, 1)
    m = np.clip(ndimage.gaussian_filter(moved.astype(np.float32), sigma) * 1.6, 0, 1)
    m *= window(base.shape[:2], box, max(4.0, 0.18 * min(x1 - x0, y1 - y0)))
    out = base * (1 - m[..., None]) + toned * m[..., None]
    return out[y0:y1, x0:x1]


# ---------------------------------------------------------------- the cast

EYE_SPAN = 0.16     # the eyes' distance apart, as a fraction of the output width
EYE_Y = 0.44        # their height, as a fraction of the output height


def read_manifest(path):
    cast = {}
    with open(path) as f:
        for row in csv.DictReader(f):
            c = cast.setdefault(row['key'], dict(frames={}, fix=set()))
            c['frames'][row['frame']] = row['file']
            c['fix'] |= set(x for x in (row.get('fix') or '').split('|') if x)
    return cast


def read_people():
    with open(os.path.join(ROOT, 'data', 'people.csv')) as f:
        return {r['key']: r for r in csv.DictReader(f)}


def guess_eyes(alpha):
    """Eye centres from the head's outline alone (a portrait with no blink frames)."""
    h, w = alpha.shape
    centre = alpha[:, int(.35 * w):int(.65 * w)].mean(1)
    top = int(np.argmax(centre > 0.5))
    row = alpha[min(h - 1, top + int(0.20 * h))] > 0.5
    xs = np.nonzero(row)[0]
    x0, x1 = (xs[0], xs[-1]) if len(xs) else (int(.3 * w), int(.7 * w))
    hw = x1 - x0
    ye = top + 0.62 * hw
    xc = (x0 + x1) / 2.0
    return (int(xc - 0.21 * hw), int(ye)), (int(xc + 0.21 * hw), int(ye))


def to_output(img, eyes, size):
    """Similarity map putting the eyes at EYE_Y, EYE_SPAN apart, centred."""
    (xl, yl), (xr, yr) = eyes
    d = float(xr - xl)
    k = EYE_SPAN * size / d                      # output pixels per source pixel
    cx, cy = (xl + xr) / 2.0, (yl + yr) / 2.0
    # output (oy, ox) samples source (cy + (oy - EYE_Y*size)/k, cx + (ox - size/2)/k)
    off = np.array([cy - EYE_Y * size / k, cx - 0.5 * size / k])
    out = []
    src = img.astype(np.float32)
    # area-average first when shrinking a lot, so the sampling does not alias
    blur = max(0.0, 0.5 / k - 0.5)
    for c in range(src.shape[2]):
        ch = ndimage.gaussian_filter(src[..., c], blur) if blur > 0 else src[..., c]
        out.append(ndimage.affine_transform(ch, np.eye(2) / k, off, output_shape=(size, size),
                                            order=3, mode='nearest'))
    return np.clip(np.stack(out, -1), 0, 1), (k, off)


def map_box(box, k, off, size):
    x0, y0, x1, y1 = box
    ox0 = int(np.floor((x0 - off[1]) * k)); oy0 = int(np.floor((y0 - off[0]) * k))
    ox1 = int(np.ceil((x1 - off[1]) * k)); oy1 = int(np.ceil((y1 - off[0]) * k))
    return [max(0, ox0), max(0, oy0), min(size, ox1), min(size, oy1)]


def png(arr, path):
    Image.fromarray((np.clip(arr, 0, 1) * 255 + 0.5).astype(np.uint8)).save(path)


def build(key, entry, person, src, out, gifs, size, log=print):
    files = entry['frames']
    base = load(os.path.join(src, files['base']))
    n = base.shape[0]
    sigma = n / 300.0
    frames, fits, covers = {}, {}, {}
    for name in FRAMES[1:] + ['comm']:
        if name not in files:
            continue
        im = load(os.path.join(src, files[name]))
        if im.shape[0] != n:
            im = resize(im, n)
        frames[name], fits[name], covers[name] = fitted(base, im)
    log('%-8s fitted %d frames: %s' % (key, len(frames), ' '.join(
        '%s=%.3f' % (k, v['score']) for k, v in fits.items())))
    have_eyes = any(k in frames for k in ('eyes_1', 'eyes_2'))
    if have_eyes:
        eye_box, mouth_box, geo = boxes(base, frames, covers, sigma)
    else:
        eyes = guess_eyes(base[..., 3])
        d = eyes[1][0] - eyes[0][0]
        ye = (eyes[0][1] + eyes[1][1]) / 2.0
        xc = (eyes[0][0] + eyes[1][0]) / 2.0
        eye_box = [int(eyes[0][0] - 0.62 * d), int(ye - 0.42 * d), int(eyes[1][0] + 0.62 * d), int(ye + 0.30 * d)]
        mouth_box = [int(xc - 0.7 * d), int(ye + 0.85 * d), int(xc + 0.7 * d), int(ye + 1.85 * d)]
        geo = dict(eyes=[list(eyes[0]), list(eyes[1])], span=float(d), guessed=True)
    if 'olive' in entry['fix']:
        chin = np.mean([e[1] for e in geo['eyes']]) + 1.45 * geo['span']
        base = suit_to_olive(base, chin)
        frames = {k: suit_to_olive(v, chin) for k, v in frames.items()}
    # every frame, whole: the base with its box blended in
    full = {'base': base}
    for name in FRAMES[1:]:
        k = name
        while k != 'base' and k not in frames:
            k = STAND_IN[k]
        if k == 'base':
            full[name] = base
            continue
        box = mouth_box if name.startswith('mouth_') else eye_box
        x0, y0, x1, y1 = box
        f = base.copy()
        f[y0:y1, x0:x1] = blend_in(base, frames[k], box, sigma, covers[k])
        full[name] = f
    dest = os.path.join(out, key)
    os.makedirs(dest, exist_ok=True)
    eyes = geo['eyes']
    ob, (k, off) = to_output(base, eyes, size)
    png(ob, os.path.join(dest, 'base.png'))
    oe, om = map_box(eye_box, k, off, size), map_box(mouth_box, k, off, size)
    if om[1] < oe[3]:
        om[1] = oe[3]
    shapes = {}
    for i in range(9):
        name = 'mouth_%d' % i if i else 'base'
        o, _ = to_output(full[name], eyes, size)
        shapes['mouth_%d' % i] = o
        png(o[om[1]:om[3], om[0]:om[2]], os.path.join(dest, 'mouth_%d.png' % i))
    for i in range(3):
        name = 'eyes_%d' % i if i else 'base'
        o, _ = to_output(full[name], eyes, size)
        shapes['eyes_%d' % i] = o
        png(o[oe[1]:oe[3], oe[0]:oe[2]], os.path.join(dest, 'eyes_%d.png' % i))
    if 'comm' in frames:
        o, _ = to_output(frames['comm'], eyes, size)
        png(o, os.path.join(dest, 'comm.png'))
    missing = [x for x in FRAMES if x not in files]
    meta = dict(key=key, name=person.get('name', key), callsign=person.get('callsign', ''),
                size=size, eye_box=oe, mouth_box=om, missing=missing, fits=fits,
                source=dict(files=files, eye_box=eye_box, mouth_box=mouth_box,
                            eyes=[list(map(int, e)) for e in eyes], fix=sorted(entry['fix'])))
    with open(os.path.join(dest, 'face.json'), 'w') as f:
        json.dump(meta, f, indent=1)
    os.makedirs(gifs, exist_ok=True)
    talk(ob, shapes, oe, om, person.get('line', ''), key, os.path.join(gifs, key + '.gif'))
    log('%-8s eyes %s mouth %s missing %s' % (key, oe, om, ','.join(missing) or '-'))
    return meta


def frame_at(base, shapes, oe, om, mouth, eye):
    f = base.copy()
    f[om[1]:om[3], om[0]:om[2]] = shapes['mouth_%d' % mouth][om[1]:om[3], om[0]:om[2]]
    f[oe[1]:oe[3], oe[0]:oe[2]] = shapes['eyes_%d' % eye][oe[1]:oe[3], oe[0]:oe[2]]
    return f


def talk_frames(line, seed, lead=20, tail=20):
    mouths = [0] * lead + lipsync.frames(line) + [0] * tail
    eyes = lipsync.blinks(len(mouths), seed=seed)
    return mouths, eyes


def talk(base, shapes, oe, om, line, key, path):
    mouths, eyes = talk_frames(line, sum(map(ord, key)))
    ims = [Image.fromarray((over(frame_at(base, shapes, oe, om, m, e)) * 255 + 0.5).astype(np.uint8))
           for m, e in zip(mouths, eyes)]
    ims[0].save(path, save_all=True, append_images=ims[1:], duration=50, loop=0, optimize=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('keys', nargs='*')
    ap.add_argument('--src', default=os.path.join(ROOT, 'art', 'people', 'src'))
    ap.add_argument('--manifest', default=os.path.join(ROOT, 'art', 'people', 'manifest.csv'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'art', 'people', 'faces'))
    ap.add_argument('--gifs', default=os.path.join(ROOT, 'build', 'people'))
    ap.add_argument('--size', type=int, default=256)
    ap.add_argument('--gallery', help='write the cast sheet here (and GIFS/cast.gif); no building')
    args = ap.parse_args()
    cast = read_manifest(args.manifest)
    people = read_people()
    keys = args.keys or [k for k in people if k in cast]
    if args.gallery:
        gallery(args.out, args.gifs, keys, people, args.gallery)
        return
    for key in keys:
        build(key, cast[key], people.get(key, {}), args.src, args.out, args.gifs, args.size)



def gallery(out, gifs, keys, people, path, cell=192, cols=5):
    """The cast side by side, named; and GIFS/cast.gif, everyone talking at once."""
    from PIL import ImageDraw, ImageFont
    try:
        font = ImageFont.load_default(14)
    except TypeError:
        font = ImageFont.load_default()
    metas = [json.load(open(os.path.join(out, k, 'face.json'))) for k in keys]
    rows = (len(keys) + cols - 1) // cols
    lab = 34
    sheet = Image.new('RGB', (cols * cell, rows * (cell + lab)), tuple((SLATE * 255).astype(int)))
    dr = ImageDraw.Draw(sheet)
    sets = []
    for i, (k, m) in enumerate(zip(keys, metas)):
        d = os.path.join(out, k)
        base = np.asarray(Image.open(os.path.join(d, 'base.png')).convert('RGBA')).astype(np.float32) / 255
        shapes = {}
        for j in range(9):
            shapes['mouth_%d' % j] = base.copy()
            x0, y0, x1, y1 = m['mouth_box']
            shapes['mouth_%d' % j][y0:y1, x0:x1] = np.asarray(Image.open(os.path.join(d, 'mouth_%d.png' % j)).convert('RGBA')) / 255.0
        for j in range(3):
            shapes['eyes_%d' % j] = base.copy()
            x0, y0, x1, y1 = m['eye_box']
            shapes['eyes_%d' % j][y0:y1, x0:x1] = np.asarray(Image.open(os.path.join(d, 'eyes_%d.png' % j)).convert('RGBA')) / 255.0
        sets.append((base, shapes, m))
        x, y = (i % cols) * cell, (i // cols) * (cell + lab)
        sheet.paste(Image.fromarray((over(base) * 255 + 0.5).astype(np.uint8)).resize((cell, cell), Image.LANCZOS), (x, y))
        p = people.get(k, {})
        cs = p.get('callsign') or ''
        dr.text((x + 6, y + cell + 2), p.get('name', k), fill=(230, 230, 220), font=font)
        dr.text((x + 6, y + cell + 17), ('"%s"  ' % cs if cs else '') + p.get('role', ''), fill=(232, 160, 60), font=font)
    sheet.save(path)
    # everyone talking: each says their line once, then rests and blinks
    small = cell * 2 // 3
    seqs = [talk_frames(people.get(k, {}).get('line', ''), sum(map(ord, k))) for k in keys]
    n = max(len(s[0]) for s in seqs)
    frames = []
    for t in range(0, n, 2):          # 10 fps keeps the file small
        canvas = Image.new('RGB', (cols * small, rows * small), tuple((SLATE * 255).astype(int)))
        for i, ((base, shapes, m), (mo, ey)) in enumerate(zip(sets, seqs)):
            mi = mo[t] if t < len(mo) else 0
            ei = lipsync.blinks(n, seed=i + 7)[t] if t >= len(ey) else ey[t]
            f = frame_at(base, shapes, m['eye_box'], m['mouth_box'], mi, ei)
            im = Image.fromarray((over(f) * 255 + 0.5).astype(np.uint8)).resize((small, small), Image.LANCZOS)
            canvas.paste(im, ((i % cols) * small, (i // cols) * small))
        frames.append(canvas)
    os.makedirs(gifs, exist_ok=True)
    frames[0].save(os.path.join(gifs, 'cast.gif'), save_all=True, append_images=frames[1:], duration=100, loop=0)


if __name__ == '__main__':
    main()
