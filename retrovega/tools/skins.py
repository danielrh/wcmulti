#!/usr/bin/env python3
"""Skins: a ship re-textured with another texture, projected onto it.

A mesh's texture coordinates only fit its own maps.  A skin replaces them:
each triangle takes the texture by straight projection along its main
axis (a top or bottom face from above, a side face from the side, a front
or back face from ahead), `tile` mesh units to a repeat of the texture.
The skin's diffuse and specular maps are made once and cached.

    aera    textures/AeraHull2.bmp, the hull map the Aera capital ships
            tile: grey panels ruled in thin green lines.  Tinted to the warm
            grey-tan of the Aera fighters' own hulls (Ariston, Nicander,
            Areus), its lines turned the blue of their accent marks (green
            is kept for the glows and light strips), and bars of their
            bright accent blue laid along some of the lines, as the Ariston
            and the Nicander carry them; specular from its own shading,
            strong enough for their metal sheen, less on the paint.
"""
import hashlib
import os

import numpy as np
from PIL import Image
from scipy import ndimage

SKINS = {
    'aera': {'diffuse': 'textures/AeraHull2.bmp', 'tile': 4.0,
             # the fighters' hull median, (74, 65, 57) in Ariston's map, as a cast
             'tint': (1.14, 1.0, 0.88), 'gain': 1.25, 'spec': 0.75,
             # Ariston's accent marks, median (17, 105, 189)
             'lines': (0.07, 0.41, 0.74), 'line_mix': 0.6, 'calm': 1.5,
             # the fighters' blue marks: bars laid along some of the panel lines
             'accents': {'count': 70, 'length': (36, 110), 'width': 7, 'colour': (0.09, 0.45, 0.86), 'seed': 3}},
}


def maps(name, assets, cache):
    """(diffuse path, specular path) of a skin, made if missing."""
    s = SKINS[name]
    os.makedirs(cache, exist_ok=True)
    tag = hashlib.md5(repr(sorted(s.items())).encode()).hexdigest()[:8]    # remade when the recipe changes
    dpath = os.path.join(cache, 'skin-%s-%s.png' % (name, tag))
    spath = os.path.join(cache, 'skin-%s-%s-spec.png' % (name, tag))
    if not (os.path.exists(dpath) and os.path.exists(spath)):
        a = np.asarray(Image.open(os.path.join(assets, s['diffuse'])).convert('RGB')).astype(np.float32) / 255
        lum = a.mean(axis=2, keepdims=True)
        green = np.clip((a[..., 1:2] - np.maximum(a[..., 0:1], a[..., 2:3])) * 8, 0, 1)
        if s.get('calm'):
            # its speckle softened (the lines are put back over it), wrapping round as it tiles
            lum = ndimage.gaussian_filter(lum, (s['calm'], s['calm'], 0), mode='wrap')
        green *= s.get('line_mix', 1.0)
        hull = lum * np.array(s['tint'], np.float32) * s['gain']
        lines = np.array(s['lines'], np.float32) if 'lines' in s else a * s['gain']
        out = hull * (1 - green) + lines * green
        paint = accents(green[..., 0] > 0.3 * s.get('line_mix', 1.0), s['accents']) if 'accents' in s else None
        spec = np.clip(lum[..., 0] * s['spec'] + green[..., 0] * 0.2, 0, 1)
        if paint is not None:
            out = out * (1 - paint[..., None]) + np.array(s['accents']['colour'], np.float32) * paint[..., None]
            spec *= 1 - 0.5 * paint
        Image.fromarray((np.clip(out, 0, 1) * 255 + 0.5).astype(np.uint8)).save(dpath)
        Image.fromarray((spec * 255 + 0.5).astype(np.uint8)).convert('RGB').save(spath)
    return dpath, spath


def accents(line, cfg):
    """Accent bars over a tiling texture: the straight runs of its panel
    lines (line, a mask) long enough to carry one, a random few of them
    painted along part of their length, `width` pixels thick.  Wraps round
    as the texture tiles.  (0..1 coverage)"""
    rng = np.random.default_rng(cfg['seed'])
    h, w = line.shape
    lo, hi = cfg['length']
    runs = []
    for axis in (0, 1):                                      # along rows, then along columns
        m = line if axis == 0 else line.T
        for r in range(m.shape[0]):
            row = np.concatenate([[False], m[r], [False]])
            d = np.diff(row.astype(np.int8))
            for a, b in zip(np.nonzero(d == 1)[0], np.nonzero(d == -1)[0]):
                if b - a >= lo:
                    runs.append((axis, r, a, b))
    out = np.zeros((h, w), np.float32)
    if not runs:
        return out
    half = cfg['width'] // 2
    for k in rng.choice(len(runs), size=min(cfg['count'], len(runs)), replace=False):
        axis, r, a, b = runs[k]
        n = int(rng.integers(lo, min(hi, b - a) + 1))
        start = int(rng.integers(a, b - n + 1))
        along = np.arange(start, start + n) % (w if axis == 0 else h)
        across = np.arange(r - half, r + half + 1) % (h if axis == 0 else w)
        if axis == 0:
            out[np.ix_(across, along)] = 1
        else:
            out[np.ix_(along, across)] = 1
    return ndimage.uniform_filter(out, 2, mode='wrap')


def apply(mesh, name, assets, cache, tile=None):
    """Re-texture mesh in place with skin name: projected coordinates, its maps.
    tile (mesh units a repeat) overrides the skin's."""
    tile = tile or SKINS[name]['tile']
    t = np.asarray(mesh.tri_index).reshape(-1, 3)
    p = mesh.positions[t]                                   # (F, 3, 3)
    fn = np.cross(p[:, 1] - p[:, 0], p[:, 2] - p[:, 0])
    axis = np.argmax(np.abs(fn), axis=1)                    # 0 side, 1 top/bottom, 2 front/back
    plane = {0: (2, 1), 1: (0, 2), 2: (0, 1)}
    uv = np.empty((len(t), 3, 2))
    for k, (i, j) in plane.items():
        sel = axis == k
        uv[sel, :, 0] = p[sel, :, i] / tile
        uv[sel, :, 1] = p[sel, :, j] / tile
    # each triangle moved by whole repeats so it lies near 0..1 (the maps repeat)
    uv -= np.floor(uv.min(axis=1, keepdims=True))
    mesh.tri_uv = uv
    dpath, spath = maps(name, assets, cache)
    mesh.textures = {0: [(2, dpath)], 1: [(2, spath)]}
