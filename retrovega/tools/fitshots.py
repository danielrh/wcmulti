#!/usr/bin/env python3
"""Shots whose table was redrawn: fitted onto the empty plate's table, so
tools/rooms.py can cut their people as it cuts any shot.

    fitshots.py KEY [--model isnet-general-use.onnx]

rooms.py needs every shot of a room to be an edit of its empty plate,
pixel for pixel.  Some shots are not: the image model drew the barracks
table, its chairs and the people at it larger, wider and lower in four of
them (the room around them is the plate's own).  For each shot in
data/places.json KEY.fit.shots this tool

    - finds the plate's table landmarks (marks: a patch and the point in
      it) in the shot by normalised cross-correlation over a range of
      sizes, and fits the shot's x and y scale and shift through them
      (x and y apart: the redrawn table is wider for its size too);
    - matches the shot's colour to the plate's in its own frame, where its
      room still lines up, then warps it onto the plate's table;
    - pastes the shot's table group (a matting model's mask, cached in
      src/KEY/masks/) onto the plate, less what the plate keeps in front
      (keep: the plate's own pixels of the table's front and legs, the
      chair across the table) and less the shot's own chair across the
      table, wherever it landed;
    - paints the plate's side chairs out underneath (chairs), so a pilot
      sitting there brings their own chair and the plate's empty one does
      not show through them; the fill runs along the floor's rows.

The fitted shots go to src/KEY/fitted/, and data/places.json points the
room's seats at them.  The masks need onnxruntime and rembg's
isnet-general-use.onnx (its GitHub releases); with the masks cached the
tool runs without them.
"""
import argparse
import json
import os
import sys

import numpy as np
from PIL import Image
from scipy import ndimage, signal

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import portraits  # noqa: E402
import rooms  # noqa: E402

SIZES = np.arange(0.96, 1.40, 0.02)     # the sizes a landmark is searched at
REACH = 150                              # source pixels: how far from its guess a landmark may be
SPECK = 400                              # source pixels: smaller bits of a shot's mask are dropped


def segment(img, box, model):
    """The matting model's foreground (0..1) over box, the size of img."""
    import onnxruntime as ort
    crop = img.crop(box)
    a = np.asarray(crop.resize((1024, 1024), Image.LANCZOS), np.float32) / 255
    a = (a / max(a.max(), 1e-6) - 0.5)[None].transpose(0, 3, 1, 2).astype(np.float32)
    s = ort.InferenceSession(model, providers=['CPUExecutionProvider'])
    y = s.run(None, {s.get_inputs()[0].name: a})[0][0, 0]
    y = (y - y.min()) / max(y.max() - y.min(), 1e-6)
    out = np.zeros(img.size[::-1], np.float32)
    out[box[1]:box[3], box[0]:box[2]] = np.asarray(
        Image.fromarray((y * 255).astype(np.uint8)).resize(crop.size, Image.LANCZOS)) / 255
    return out


def mask_of(src, name, box, model):
    """The table group in a shot (bool), from the cache or the model."""
    path = os.path.join(src, 'masks', os.path.splitext(name)[0] + '.png')
    if not os.path.exists(path):
        if not model:
            sys.exit('%s: no mask cached; give --model' % path)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        m = segment(Image.open(os.path.join(src, name)).convert('RGB'), box, model)
        Image.fromarray((m * 255 + 0.5).astype(np.uint8)).save(path)
    return largest(np.asarray(Image.open(path)) >= 128)


def largest(m):
    lab, n = ndimage.label(m)
    if not n:
        return m
    return lab == 1 + int(np.argmax(ndimage.sum(m, lab, range(1, n + 1))))


def no_specks(m):
    lab, n = ndimage.label(m)
    return np.isin(lab, 1 + np.nonzero(ndimage.sum(m, lab, range(1, n + 1)) >= SPECK)[0])


def boxes(shape, rects):
    m = np.zeros(shape, bool)
    for x0, y0, x1, y1 in rects:
        m[y0:y1, x0:x1] = True
    return m


def ncc(img, tpl):
    """Normalised cross-correlation of tpl at every place it fits in img."""
    t = tpl - tpl.mean()
    ones = np.ones_like(tpl)
    s1 = signal.fftconvolve(img, ones, mode='valid')
    s2 = signal.fftconvolve(img * img, ones, mode='valid')
    var = np.maximum(s2 - s1 * s1 / tpl.size, 1e-6)
    return signal.fftconvolve(img, t[::-1, ::-1], mode='valid') / (np.sqrt(var) * np.sqrt((t * t).sum()))


def fit(plate, shot, marks):
    """(sx, sy, tx, ty): the shot's pixel of the plate's (x, y) is (sx x + tx, sy y + ty)."""
    P, S = plate.convert('L'), np.asarray(shot.convert('L'), np.float32)
    pts = np.array([m[4:] for m in marks.values()], float)
    cx, cy = pts.mean(axis=0)
    src, dst = [], []
    for x0, y0, x1, y1, px, py in marks.values():
        best = None
        for s in SIZES:
            T = np.asarray(P.crop((x0, y0, x1, y1)).resize((round((x1 - x0) * s), round((y1 - y0) * s)),
                                                           Image.BICUBIC), np.float32)
            # the guess: the plate's table scaled about its middle
            gx, gy = cx + ((x0 + x1) / 2 - cx) * s - T.shape[1] / 2, cy + ((y0 + y1) / 2 - cy) * s - T.shape[0] / 2
            wx, wy = max(0, int(gx) - REACH), max(0, int(gy) - REACH)
            r = ncc(S[wy:int(gy) + T.shape[0] + REACH, wx:int(gx) + T.shape[1] + REACH], T)
            i, j = np.unravel_index(np.argmax(r), r.shape)
            if best is None or r[i, j] > best[0]:
                best = (r[i, j], wx + j + (px - x0) * s, wy + i + (py - y0) * s)
        src.append((px, py))
        dst.append(best[1:])
    src, dst = np.array(src), np.array(dst)
    sx, tx = np.polyfit(src[:, 0], dst[:, 0], 1)
    sy, ty = np.polyfit(src[:, 1], dst[:, 1], 1)
    if abs(sx - 1) < 0.005 and abs(sy - 1) < 0.005 and abs(tx) < 1.5 and abs(ty) < 1.5:
        return 1.0, 1.0, 0.0, 0.0           # an edit of the plate after all
    return sx, sy, tx, ty


def fill_rows(img, hole, valid):
    """The holes filled row by row between the valid pixels either side (the
    floor's seams run across), softened down the columns."""
    out = img.copy()
    xs = np.arange(img.shape[1])
    for y in np.nonzero(hole.any(axis=1))[0]:
        v = valid[y] & ~hole[y]
        if v.sum() >= 2:
            for c in range(3):
                out[y, hole[y], c] = np.interp(xs[hole[y]], xs[v], img[y, v, c])
    soft = ndimage.gaussian_filter(out, (2.5, 0.8, 0))
    out[hole] = soft[hole]
    return out


def warp(a, size, f, resample):
    sx, sy, tx, ty = f
    return np.asarray(Image.fromarray(a).transform(size, Image.AFFINE, (sx, 0, tx, 0, sy, ty), resample))


def build(cfg, model, log=print):
    src = os.path.join(ROOT, cfg['src'])
    F = cfg['fit']
    plate_img = Image.open(os.path.join(src, cfg['background'])).convert('RGB')
    plate = rooms.load(os.path.join(src, cfg['background']))
    shape = plate.shape[:2]
    group = mask_of(src, cfg['background'], F['segment'], model)
    keep = ndimage.binary_fill_holes(group & boxes(shape, F['keep']))
    chairs = group & boxes(shape, F['chairs']) & ~keep
    erase = ndimage.binary_dilation(chairs, iterations=4) & ~(group & ~chairs)
    bare = fill_rows(plate, erase, ~ndimage.binary_dilation(group, iterations=2))
    across = group & boxes(shape, [F['across']])          # the chair across the table, the plate's
    od = os.path.join(src, 'fitted')
    os.makedirs(od, exist_ok=True)
    for name in F['shots']:
        img = Image.open(os.path.join(src, name)).convert('RGB')
        f = fit(plate_img, img, F['marks'])
        shot = rooms.load(os.path.join(src, name))
        mask = mask_of(src, name, F['segment'], model)
        # colour and what differs, in the shot's own frame (its room is the plate's)
        same = rooms.lab_distance(plate, shot) < rooms.CHANGED
        shot = rooms.toned(plate, shot, same & ~ndimage.binary_dilation(mask, iterations=8))
        differs = ndimage.binary_dilation(rooms.clean(rooms.lab_distance(plate, shot) >= rooms.CHANGED), iterations=4)
        differs = ndimage.binary_fill_holes(differs)
        moved = warp((shot * 255 + 0.5).astype(np.uint8), img.size, f, Image.BICUBIC).astype(np.float32) / 255
        mine = warp(((mask & differs) * 255).astype(np.uint8), img.size, f, Image.BILINEAR) >= 128
        mine = no_specks(ndimage.binary_erosion(mine, iterations=1))
        # the shot's chair across the table, where the warp left it
        x0, y0, x1, y1 = F['across']
        dx, dy, _ = portraits.xcorr_shift(edges(plate[y0:y1, x0:x1]), edges(moved[y0:y1, x0:x1]))
        theirs = ndimage.binary_dilation(ndimage.shift(across, (-dy, -dx), order=0), iterations=2)
        paste = mine & ~keep & ~theirs
        out = bare.copy()
        out[paste] = moved[paste]
        Image.fromarray((out * 255 + 0.5).astype(np.uint8)).save(os.path.join(od, name))
        log('  %-52s x %.3f%+.0f  y %.3f%+.0f' % (name, f[0], f[2], f[1], f[3]))


def edges(rgb):
    g = ndimage.gaussian_filter(rgb @ np.array([0.30, 0.59, 0.11], np.float32), 1.2)
    e = np.hypot(ndimage.sobel(g, 0), ndimage.sobel(g, 1))
    return (e - e.mean()) / (e.std() + 1e-6)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('key')
    ap.add_argument('--model', help="rembg's isnet-general-use.onnx, for shots with no cached mask")
    args = ap.parse_args()
    with open(os.path.join(ROOT, 'data', 'places.json')) as f:
        cfg = json.load(f)[args.key]
    build(cfg, args.model)


if __name__ == '__main__':
    main()
