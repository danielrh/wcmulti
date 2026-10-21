#!/usr/bin/env python3
"""The spherical projectiles' sprites (src/balls.c): every type, frame and
diameter, drawn procedurally and put in the flight palette.

    balls.py [--out build/dos/BALLS.DAT] [--preview docs/balls.png]

A ball is round, so a sprite needs only its size: every diameter from 1 to
DMAX pixels is drawn here, supersampled (SS x SS samples a pixel), each
pixel the mean of its samples: opaque where they mostly are, a stipple (every
other pixel) where a glow covers it partly, clear elsewhere; then the flight
palette's nearest colour among the fixed ones (rvpack.UI in 1..15 and
palette.json's 16..207: never a cockpit's own 208..254), or a checker of two
of them where their mix is much nearer (the palette has no pinks: salmon and
lavender make them).  At 1 or 2 px a ball is its inner part's colour.

The looks (the user's, 2026-10-21): the slug polished silver; the plasma a
blue ball of fiery plasma, its arms swirling, white flecks and clear rents
boiling through it; the fire ball searing white to pink, discharges forking
out of its rim; the star a continuous four-pointed star, white-green at its
heart, turning.

BALLS.DAT: "RVBALLS\\0", u16 version, u16 types, u16 dmax, u16 0, the types'
frame counts (a byte each, padded to 4), then for each type, frame and
diameter d = 1 .. dmax: d x d bytes, row by row, 0 clear.
"""
import argparse
import json
import math
import os
import struct

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
VERSION = 1
DMAX = 32
SS = 8
# rvpack.UI: the flight palette's 1..15 as shown (palette.json's are the rooms' then)
UI = [(0, 0, 0), (28, 28, 30), (60, 60, 64), (100, 100, 104), (140, 140, 144), (180, 180, 184),
      (220, 220, 222), (255, 255, 255), (96, 58, 12), (168, 108, 28), (222, 160, 60), (252, 214, 124),
      (12, 18, 30), (52, 70, 98), (120, 216, 255), (210, 44, 32)]
NAMES = ['slug', 'plasma', 'fire', 'star']
FRAMES = [1, 4, 4, 4]


def flight_palette():
    pal = np.array(json.load(open(os.path.join(ROOT, 'data', 'palette.json')))['rgb'], dtype=np.float64)
    pal[0:16] = UI
    return pal


def ramp(stops, x):
    """sRGB colour along stops [(t, (r, g, b)), ...] at x (array, 0..1)."""
    x = np.clip(x, 0, 1)
    out = np.zeros(x.shape + (3,))
    ts = [t for t, _ in stops]
    for c in range(3):
        out[..., c] = np.interp(x, ts, [col[c] for _, col in stops])
    return out


def sphere_normal(u, v, r0):
    """the normal of a sphere of radius r0 at (u, v) (v up), and where it is"""
    q = (u * u + v * v) / (r0 * r0)
    inside = q <= 1
    z = np.sqrt(np.clip(1 - q, 0, 1))
    return u / r0, v / r0, z, inside


def shade(nx, ny, nz, power):
    """diffuse and specular from a light up and to the left, the viewer on +z"""
    lx, ly, lz = -0.55, 0.6, 0.58
    ln = math.sqrt(lx * lx + ly * ly + lz * lz)
    lx, ly, lz = lx / ln, ly / ln, lz / ln
    diff = np.clip(nx * lx + ny * ly + nz * lz, 0, 1)
    hx, hy, hz = lx, ly, lz + 1
    hn = math.sqrt(hx * hx + hy * hy + hz * hz)
    spec = np.clip((nx * hx + ny * hy + nz * hz) / hn, 0, 1) ** power
    return diff, spec


def slug(u, v, frame):
    """a silver mass driver slug: a polished metal ball"""
    nx, ny, nz, inside = sphere_normal(u, v, 0.92)
    diff, spec = shade(nx, ny, nz, 30)
    base = np.array([150.0, 152.0, 162.0])
    col = base * (0.16 + 0.84 * diff)[..., None] + 255.0 * (0.9 * spec)[..., None]
    col += np.array([18.0, 24.0, 40.0]) * np.clip(-ny, 0, 1)[..., None]   # the sky below, in the metal
    return col, inside.astype(np.float64)


def blobs(u, v, seed, cells=5):
    """smooth noise in -1 .. 1: a coarse random grid, interpolated (the fire's boiling)"""
    g = np.random.default_rng(seed).uniform(-1, 1, (cells + 1, cells + 1))
    x = np.clip((u + 1) / 2, 0, 1) * cells
    y = np.clip((v + 1) / 2, 0, 1) * cells
    i, j = np.minimum(x.astype(int), cells - 1), np.minimum(y.astype(int), cells - 1)
    fx, fy = x - i, y - j
    fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    a = g[j, i] * (1 - fx) + g[j, i + 1] * fx
    b = g[j + 1, i] * (1 - fx) + g[j + 1, i + 1] * fx
    return a * (1 - fy) + b * fy


def plasma(u, v, frame):
    """a ball of fiery blue plasma: three arms of brighter light swirling round a white core, its edge ragged,
    white flecks and clear rents boiling through it (new every frame)"""
    r = np.hypot(u, v)
    th = np.arctan2(v, u)
    turn = frame * (2 * math.pi / 12)
    cu, cv = u * math.cos(turn) + v * math.sin(turn), v * math.cos(turn) - u * math.sin(turn)
    boil = 0.6 * blobs(cu, cv, 3000 + frame, 4) + 0.4 * blobs(cu, cv, 3100 + frame, 9)
    fleck = blobs(u, v, 3200 + frame, 11)
    rent = blobs(u, v, 3300 + frame, 6)
    core = np.exp(-(r / 0.26) ** 2)
    swirl = 0.5 + 0.5 * np.sin(3 * (th - turn) + 9 * r)
    edge = 0.8 + 0.12 * blobs(np.cos(th) * 0.9, np.sin(th) * 0.9, 3400 + frame, 7)
    body = np.clip(1 - (r / edge) ** 2, 0, 1) * (0.4 + 0.6 * swirl)
    i = np.clip(core + 0.62 * body + 0.22 * boil, 0, 1)
    col = ramp([(0.0, (12, 22, 100)), (0.3, (30, 70, 215)), (0.6, (80, 150, 255)), (0.85, (175, 222, 255)),
                (1.0, (255, 255, 255))], i)
    hot = (fleck > 0.42) & (r < edge * 0.9) & (r > 0.18)
    col[hot] = (240, 248, 255)
    alpha = np.clip((edge + 0.12 - r) / 0.24, 0, 1)
    alpha[(rent < -0.38) & (r > 0.32) & ~hot] = 0.0      # the rents: clear
    return col, alpha


def fire(u, v, frame):
    """a searing ball, white at the core through pale pink to hot pink and crimson, discharges forking out of
    its rim (new every frame)"""
    rng = np.random.default_rng(1000 + frame)
    r = np.hypot(u, v)
    th = np.arctan2(v, u)
    r0 = 0.56
    heat = np.clip(1 - r / r0, 0, 1) ** 0.7 + 0.22 * blobs(u, v, 2000 + frame)
    col = ramp([(0.0, (150, 20, 60)), (0.25, (230, 60, 95)), (0.5, (255, 125, 160)), (0.75, (255, 200, 225)),
                (1.0, (255, 250, 252))], heat)
    alpha = (r < r0).astype(np.float64)
    corona = (r >= r0) & (r < r0 + 0.09)
    col[corona] = (240, 95, 150)
    alpha[corona] = 0.4

    def arc(a, r_from, r_to, wob, ph, width):
        ang = a + wob * np.sin(r * 23 + ph) / np.maximum(r, 0.3)
        d = np.abs(np.angle(np.exp(1j * (th - ang)))) * r
        return (d < width * (1.2 - 0.6 * (r - r_from) / max(r_to - r_from, 1e-3))) & (r > r_from) & (r < r_to)

    # the discharges: jagged arcs out of the rim, most of them forking half way
    for k in range(13):
        a = rng.uniform(0, 2 * math.pi)
        reach = rng.uniform(0.74, 1.0)
        wob, ph = rng.uniform(0.08, 0.22), rng.uniform(0, 6.3)
        on = arc(a, r0 * 0.85, reach, wob, ph, 0.05)
        if rng.uniform() < 0.7:
            mid = r0 + (reach - r0) * rng.uniform(0.3, 0.6)
            on |= arc(a + rng.choice([-1, 1]) * rng.uniform(0.25, 0.5), mid, min(1.0, reach + 0.05),
                      wob * 0.7, ph + 1.0, 0.04)
        col[on] = (255, 228, 248)
        alpha[on] = 1.0
    return col, alpha


def star(u, v, frame):
    """a continuous four-pointed star, its sides curving in to its points, white-green at its heart through
    green to its tips, a faint glow round it; turning an eighth of its points' spacing a frame"""
    phi = frame * (math.pi / 8)
    x = u * math.cos(phi) + v * math.sin(phi)
    y = v * math.cos(phi) - u * math.sin(phi)
    q = 0.5
    s = (np.abs(x) ** q + np.abs(y) ** q) ** (1 / q)     # 1 on the star's edge (its tips at the sprite's edge)
    s = s / 0.98
    col = ramp([(0.0, (235, 255, 235)), (0.22, (170, 250, 175)), (0.55, (80, 220, 100)), (0.85, (40, 170, 60)),
                (1.0, (28, 130, 45))], s)
    alpha = np.where(s < 1, 1.0, np.where(s < 1.07, 0.3, 0.0))
    return col, alpha


DRAW = [slug, plasma, fire, star]


def sprite(kind, frame, d, pal, allowed):
    """d x d palette indices, 0 clear"""
    n = d * SS
    c = (np.arange(n) + 0.5) / n * 2 - 1
    u, v = np.meshgrid(c, -c)                  # v up
    col, alpha = DRAW[kind](u, v, frame)
    a = alpha.reshape(d, SS, d, SS).mean(axis=(1, 3))
    pm = (col * alpha[..., None]).reshape(d, SS, d, SS, 3).mean(axis=(1, 3))
    rgb = pm / np.maximum(a, 1e-6)[..., None]
    if d <= 2:
        # a pixel or four: the ball's own colour, its inner part's (the whole's mean is dimmed by its dark edge)
        inner = (np.hypot(u, v) < 0.55) & (alpha > 0.5)
        rgb[:] = (col[inner].mean(0) if inner.any() else col.reshape(-1, 3).mean(0))
        a = np.ones_like(a)
    yy, xx = np.mgrid[0:d, 0:d]
    show = (a >= 0.5) | ((a >= 0.2) & (((xx + yy) & 1) == 0))
    idx = quantize(rgb, (xx + yy) & 1, pal, allowed, dither=d > 2)
    return np.where(show, idx, 0).astype(np.uint8)


def quantize(rgb, parity, pal, allowed, dither):
    """palette indices for an image: each pixel the nearest colour, or (dither) a checker of the two whose mix is
    much nearer than any one"""
    w = np.array([2.0, 4.0, 3.0])
    flat = rgb.reshape(-1, 3)
    P = pal[allowed]
    dist = (((flat[:, None, :] - P[None, :, :]) ** 2) * w).sum(-1)
    order = np.argsort(dist, 1)[:, :10]
    best = order[:, 0]
    err = dist[np.arange(len(flat)), best]
    out = best.copy()
    if dither:
        ia, ib = np.triu_indices(10, 1)
        A, B = order[:, ia], order[:, ib]                  # (pixels, pairs)
        mix = (P[A] + P[B]) / 2
        perr = (((flat[:, None, :] - mix) ** 2) * w).sum(-1)
        k = perr.argmin(1)
        pa, pb = A[np.arange(len(flat)), k], B[np.arange(len(flat)), k]
        lum = P @ np.array([0.3, 0.59, 0.11])
        lo, hi = np.where(lum[pa] <= lum[pb], pa, pb), np.where(lum[pa] <= lum[pb], pb, pa)
        use = perr[np.arange(len(flat)), k] < 0.5 * err
        pick = np.where(parity.reshape(-1) == 0, hi, lo)
        out = np.where(use, pick, best)
    return np.array(allowed)[out].reshape(rgb.shape[:2])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'dos', 'BALLS.DAT'))
    ap.add_argument('--preview', default=None)
    args = ap.parse_args()
    pal = flight_palette()
    allowed = list(range(1, 208))
    data = bytearray(b'RVBALLS\0' + struct.pack('<4H', VERSION, len(DRAW), DMAX, 0)
                     + bytes(FRAMES) + bytes(-len(FRAMES) % 4))
    sprites = {}
    for k in range(len(DRAW)):
        for f in range(FRAMES[k]):
            for d in range(1, DMAX + 1):
                s = sprite(k, f, d, pal, allowed)
                sprites[(k, f, d)] = s
                data += s.tobytes()
    open(args.out, 'wb').write(bytes(data))
    print('%s: %d bytes' % (args.out, len(data)))
    if args.preview:
        sizes = [32, 24, 16, 12, 8, 6, 4, 3, 2, 1]
        rows = [(k, f) for k in range(len(DRAW)) for f in range(FRAMES[k])]
        w = sum(s + 6 for s in sizes) + 6
        img = np.zeros((len(rows) * (DMAX + 6) + 6, w, 3), dtype=np.uint8)
        for ri, (k, f) in enumerate(rows):
            x = 6
            for d in sizes:
                s = sprites[(k, f, d)]
                y = 6 + ri * (DMAX + 6) + (DMAX - d) // 2
                img[y:y + d, x:x + d] = np.where(s[..., None] > 0, pal[s].astype(np.uint8), 0)
                x += d + 6
        Image.fromarray(img).resize((img.shape[1] * 3, img.shape[0] * 3), Image.NEAREST).save(args.preview)
        print('preview', args.preview)


if __name__ == '__main__':
    main()
