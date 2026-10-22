#!/usr/bin/env python3
"""The sky's far things (src/sky.c): nebulae, galaxies, planets, at infinity,
so each is one size and only turns as the camera rolls.

    sky.py [--out build/dos/SKY.DAT] [--preview docs/sky.png]

Each is drawn procedurally once, SS times finer than shown, in a box D
pixels across (odd: its middle a pixel's), then for every turn it may show
at (TURNS a whole turn) the drawing is turned, box-filtered down and put in
the flight palette: the nearest of its fixed colours (1..207, never a
cockpit's own), or a checker of two where their mix is much nearer (through
a 32^3 table).  What is drawn is light over black space: a pixel dimmer than
DIM is clear (a stipple under it down to half), unless it is a planet's body
(its night side too: the palette has near-blacks from 16 on).  A turn is
run-length coded as the ships' sprites are (a row table, then each row's
(skip, count, pixels) .. 0, 0), its middle at (ox, oy).

The blit can mirror and flip a sprite, and a half turn is both, so only the
first half of the turns is kept; a thing that is its own mirror image (the
ringed planet: lit from above, its bands and rings level) keeps only the
first quarter (and the quarter's end), the rest mirrored or flipped.

A thing may keep every other row only (half the bytes: each row then
stands for two screen rows, the pair's mean), and is drawn every row,
doubled, or interlaced (only every other screen row: what is behind shows
between the lines, a gas's translucence, at half the cost: the user's idea
for nebulae, 2026-10-21).

SKY.DAT: "RVSKY\\0\\0\\0", u16 version, u16 things, then each thing's 16
bytes: u16 turns kept, u16 turns a whole turn, u8 symmetry (0: halves, 1:
quarters), u8 layer (0 behind the stars, 1 over them), u16 its box, u32
where its turns' headers are, u8 rows kept (1 all, 2 every other), u8 how
they are drawn (1 all, 2 doubled, 3 interlaced), u16 0; then the headers
(u16 w, h, ox, oy, u32 where its row table is), then the sprites; offsets
from the data's start (the byte after the things).
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
SS = 3
TURNS = 128
DIM = 9.0                                       # luminance (0..255) under which the light is clear
UI = [(0, 0, 0), (28, 28, 30), (60, 60, 64), (100, 100, 104), (140, 140, 144), (180, 180, 184),
      (220, 220, 222), (255, 255, 255), (96, 58, 12), (168, 108, 28), (222, 160, 60), (252, 214, 124),
      (12, 18, 30), (52, 70, 98), (120, 216, 255), (210, 44, 32)]


def flight_palette():
    pal = np.array(json.load(open(os.path.join(ROOT, 'data', 'palette.json')))['rgb'], dtype=np.float64)
    pal[0:16] = UI
    return pal[:208]


# --- the palette table: for each cell of a 32^3 cube, the nearest colour and the best checker pair ---

W = np.array([2.0, 4.0, 3.0])


def palette_table(pal):
    allowed = np.arange(1, 208)
    P = pal[allowed]
    c = (np.arange(32) * 255 / 31.0)
    cells = np.stack(np.meshgrid(c, c, c, indexing='ij'), -1).reshape(-1, 3)
    dist = (((cells[:, None, :] - P[None]) ** 2) * W).sum(-1)
    order = np.argsort(dist, 1)[:, :10]
    best = order[:, 0]
    err = dist[np.arange(len(cells)), best]
    ia, ib = np.triu_indices(10, 1)
    A, B = order[:, ia], order[:, ib]
    mix = (P[A] + P[B]) / 2
    perr = (((cells[:, None, :] - mix) ** 2) * W).sum(-1)
    k = perr.argmin(1)
    pa, pb = A[np.arange(len(cells)), k], B[np.arange(len(cells)), k]
    lum = P @ np.array([0.3, 0.59, 0.11])
    lo, hi = np.where(lum[pa] <= lum[pb], pa, pb), np.where(lum[pa] <= lum[pb], pb, pa)
    use = perr[np.arange(len(cells)), k] < 0.5 * err
    even = np.where(use, hi, best)
    odd = np.where(use, lo, best)
    return allowed[even], allowed[odd]


def quantize(rgb, table):
    even, odd = table
    q = np.clip(np.rint(rgb * 31 / 255), 0, 31).astype(int)
    cell = (q[..., 0] * 32 + q[..., 1]) * 32 + q[..., 2]
    yy, xx = np.mgrid[0:rgb.shape[0], 0:rgb.shape[1]]
    return np.where(((xx + yy) & 1) == 0, even[cell], odd[cell])


# --- noise ---

def value_noise(u, v, cells, seed):
    """smooth noise in -1 .. 1 over [-1, 1]^2 (a random grid, smoothstepped)"""
    g = np.random.default_rng(seed).uniform(-1, 1, (cells + 2, cells + 2))
    x = np.clip((u + 1) / 2, 0, 1) * cells
    y = np.clip((v + 1) / 2, 0, 1) * cells
    i, j = np.minimum(x.astype(int), cells - 1), np.minimum(y.astype(int), cells - 1)
    fx, fy = x - i, y - j
    fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    a = g[j, i] * (1 - fx) + g[j, i + 1] * fx
    b = g[j + 1, i] * (1 - fx) + g[j + 1, i + 1] * fx
    return a * (1 - fy) + b * fy


def fbm(u, v, seed, base=3, octaves=5):
    out = np.zeros_like(u)
    amp, tot = 1.0, 0.0
    for o in range(octaves):
        out += amp * value_noise(u, v, base * 2 ** o, seed + 17 * o)
        tot += amp
        amp *= 0.55
    return out / tot


def ramp(stops, x):
    x = np.clip(x, 0, 1)
    out = np.zeros(x.shape + (3,))
    ts = [t for t, _ in stops]
    for c in range(3):
        out[..., c] = np.interp(x, ts, [col[c] for _, col in stops])
    return out


def lum(rgb):
    return rgb @ np.array([0.3, 0.59, 0.11])


# --- the things: each (u, v) in -1 .. 1 (v up) -> light over black (sRGB 0..255), and how much is solid body ---

def nebula(u, v):
    """an emission nebula: glowing gas, hydrogen's pink at its heart, oxygen's teal outside it, dark dust and
    rents across it, a few young stars in it; inside its circle whatever its turn"""
    r = np.hypot(u * 0.8, v * 1.2)
    rc = np.hypot(u, v)
    warp = 0.3 * fbm(u, v, 11, 2, 4)
    env = np.clip(1 - (r + warp) / 0.85, 0, 1) ** 1.1 * np.clip((0.97 - rc) / 0.15, 0, 1)
    gas = 0.5 + 0.5 * fbm(u + 0.3 * warp, v - 0.2 * warp, 23, 3, 6)
    fil = 1 - np.abs(fbm(u * 1.3 + warp, v * 1.3, 37, 3, 5))   # filaments: ridges of the noise
    dust = np.clip(fbm(u, v, 51, 4, 5) * 2.4 - 0.2, 0, 1)
    i = env * (0.3 * gas + 0.75 * fil ** 4) * (1 - 0.9 * dust)
    hue = np.clip(1.0 - 1.3 * r + 0.9 * fbm(u, v, 63, 2, 3), 0, 1)   # 1: pink, 0: teal
    pink = np.array([255.0, 60.0, 140.0])
    teal = np.array([40.0, 175.0, 215.0])
    col = (teal * (1 - hue[..., None]) + pink * hue[..., None]) * (2.6 * i)[..., None]
    col += 255 * np.clip(i * 2.2 - 0.7, 0, 1)[..., None] ** 2   # its brightest knots white
    rng = np.random.default_rng(77)
    for k in range(9):                                          # young stars in it
        cx, cy = rng.uniform(-0.5, 0.5), rng.uniform(-0.35, 0.35)
        s = np.exp(-((u - cx) ** 2 + (v - cy) ** 2) / (2 * 0.012 ** 2))
        col += np.array([230.0, 240.0, 255.0]) * s[..., None]
    return np.clip(col, 0, 255), np.zeros_like(u)


def galaxy(u, v):
    """a spiral galaxy seen at a slant: a yellow-white bulge, two blue-white arms with knots of new stars, dust
    along their inner edges"""
    incl = 0.42                                                  # minor over major axis
    x, y = u / 0.95, v / (0.95 * incl)
    r = np.hypot(x, y) + 1e-6
    phi = np.arctan2(y, x)
    wind = 1 / math.tan(math.radians(17))                        # the arms' pitch
    arm = np.cos(2 * (phi - wind * np.log(r))) * 0.5 + 0.5
    arms = arm ** 4 * np.exp(-r / 0.38) * np.clip(r / 0.12, 0, 1)
    knots = np.clip(fbm(u * 2, v * 2, 101, 6, 3), 0, 1) * arms
    lane = np.cos(2 * (phi - wind * np.log(r)) + 0.9) * 0.5 + 0.5
    dust = (lane ** 8) * np.exp(-r / 0.5) * np.clip(r / 0.15, 0, 1)
    disk = np.exp(-r / 0.3)
    bulge = np.exp(-(r / 0.13) ** 2)
    col = np.array([255.0, 225.0, 165.0]) * (330 * bulge + 70 * disk)[..., None] / 255
    col += np.array([160.0, 185.0, 255.0]) * (2.0 * arms + 2.5 * knots)[..., None]
    col *= (1 - 0.7 * dust)[..., None]
    col *= np.clip((1.0 - r) / 0.15, 0, 1)[..., None]
    return np.clip(col, 0, 255), np.zeros_like(u)


def sphere(u, v, R, light):
    """a sphere of radius R: its normal, where it is, its lighting (a soft terminator, the limb darkened)"""
    q = (u * u + v * v) / (R * R)
    inside = q <= 1
    nz = np.sqrt(np.clip(1 - q, 0, 1))
    nx, ny = u / R, v / R
    l = np.array(light) / np.linalg.norm(light)
    d = nx * l[0] + ny * l[1] + nz * l[2]
    lit = np.clip((d + 0.08) / 1.08, 0, 1) ** 0.9 * (0.55 + 0.45 * nz)
    return nx, ny, nz, inside, lit


def giant(u, v):
    """a gas giant: bands turned by turbulence, a storm, lit from the upper left, a blue haze on its lit limb"""
    R = 0.88
    nx, ny, nz, inside, lit = sphere(u, v, R, (-0.55, 0.55, 0.63))
    lat = ny + 0.05 * fbm(nx * 3, ny * 10, 201, 3, 4) + 0.02 * fbm(nx * 8, ny * 30, 211, 3, 3)
    band = ramp([(0.0, (225, 205, 170)), (0.18, (200, 150, 105)), (0.3, (240, 228, 205)), (0.45, (175, 100, 65)),
                 (0.55, (230, 200, 160)), (0.7, (190, 130, 90)), (0.85, (245, 235, 215)), (1.0, (205, 160, 115))],
                (np.sin(lat * 7.5) * 0.5 + 0.5))
    storm = np.exp(-(((nx - 0.28) / 0.17) ** 2 + ((ny + 0.32) / 0.08) ** 2))
    band = band * (1 - storm[..., None]) + np.array([215.0, 95.0, 60.0]) * storm[..., None]
    col = band * lit[..., None]
    col = np.where(inside[..., None], np.maximum(col, 3.0), 0.0)
    r = np.hypot(u, v) + 1e-9
    halo = np.clip(1 - (r - R) / 0.06, 0, 1) * (r > R) * np.clip((-u * 0.55 + v * 0.55) / r + 0.3, 0, 1)
    col += np.array([90.0, 140.0, 230.0]) * (halo * 0.8)[..., None]
    return np.clip(col, 0, 255), inside.astype(np.float64)


def ringed(u, v):
    """a pale gold planet with rings, lit from above, its bands and rings level: its own mirror image"""
    R = 0.42
    nx, ny, nz, inside, lit = sphere(u, v, R, (0.0, 0.8, 0.6))
    band = ramp([(0.0, (230, 210, 160)), (0.3, (210, 180, 125)), (0.5, (240, 225, 185)), (0.7, (200, 165, 110)),
                 (1.0, (235, 215, 170))], np.sin(ny * 9) * 0.5 + 0.5)
    planet = np.where(inside[..., None], np.maximum(band * lit[..., None], 3.0), 0.0)
    tilt = 0.3                                                   # the rings' minor over major axis
    rr = np.hypot(u, v / tilt)
    stops = [0.55, 0.58, 0.70, 0.74, 0.76, 0.88, 0.90, 0.92, 0.98, 1.0]
    alphas = [0.0, 0.55, 0.8, 0.25, 0.9, 0.75, 0.1, 0.6, 0.45, 0.0]
    a = np.interp(rr, stops, alphas) * ((rr >= 0.55) & (rr <= 1.0))
    ring_col = np.array([225.0, 205.0, 165.0]) * (0.75 + 0.25 * np.cos(rr * 60) ** 2)[..., None]
    near = v < 0                                                 # the rings' near side, below its middle
    behind = inside & ~near                                      # the far side, hidden by the planet
    a = np.where(behind, 0.0, a)
    col = planet * (1 - a[..., None]) + ring_col * a[..., None]
    solid = np.maximum(inside.astype(np.float64), a)
    return np.clip(col, 0, 255), solid


THINGS = [
    # name, drawing, its box (odd), symmetry (0 halves, 1 quarters), layer (0 behind the stars, 1 over them),
    # rows kept (1 all, 2 every other), drawn (1 all, 2 doubled, 3 interlaced)
    ('nebula', nebula, 241, 0, 0, 2, 3),
    ('galaxy', galaxy, 201, 0, 0, 1, 1),
    ('giant', giant, 131, 0, 1, 1, 1),
    ('ringed', ringed, 181, 1, 1, 1, 1),
]


def master(draw, D):
    n = D * SS
    c = (np.arange(n) + 0.5) / n * 2 - 1
    u, v = np.meshgrid(c, -c)
    col, solid = draw(u, v)
    return col, solid


def turned(col, solid, D, deg):
    """the master turned `deg` clockwise on the screen, box-filtered to D x D"""
    n = D * SS
    chans = []
    for ch in [col[..., 0], col[..., 1], col[..., 2], solid]:
        im = Image.fromarray(ch.astype(np.float32), 'F').rotate(-deg, resample=Image.BICUBIC, center=(n / 2, n / 2))
        chans.append(np.asarray(im, dtype=np.float64).reshape(D, SS, D, SS).mean(axis=(1, 3)))
    return np.nan_to_num(np.clip(np.stack(chans[:3], -1), 0, 255)), np.nan_to_num(np.clip(chans[3], 0, 1))


def halve(col, solid):
    """every other row: each the mean of a pair (the box's last row with a clear one)"""
    D = col.shape[0]
    col = np.concatenate([col, np.zeros((1,) + col.shape[1:])]) if D & 1 else col
    solid = np.concatenate([solid, np.zeros((1,) + solid.shape[1:])]) if D & 1 else solid
    return (col.reshape(-1, 2, *col.shape[1:]).mean(1), solid.reshape(-1, 2, *solid.shape[1:]).mean(1))


def sprite(col, solid, table):
    """palette indices, 0 clear"""
    D, Wd = col.shape[0], col.shape[1]
    yy, xx = np.mgrid[0:D, 0:Wd]
    L = lum(col)
    show = (solid >= 0.5) | (L >= DIM) | ((L >= DIM / 2) & (((xx + yy) & 1) == 0))
    idx = quantize(np.maximum(col, 2.0), table)
    return np.where(show, idx, 0).astype(np.uint8)


def rle(spr, cy):
    """(w, h, ox, oy, the row table and rows): cropped to what shows, its middle (row cy) kept"""
    D = spr.shape[1]
    ys, xs = np.nonzero(spr)
    if not len(ys):
        return 1, 1, 0, 0, struct.pack('<2I', 0, 2) + b'\0\0'
    top, bot, left, right = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
    rows, tab = bytearray(), []
    for y in range(top, bot):
        tab.append(len(rows))
        row = spr[y]
        x, last = left, left
        while x < right:
            if not row[x]:
                x += 1
                continue
            s = x
            while x < right and row[x] and x - s < 255:
                x += 1
            rows += bytes([s - last, x - s]) + bytes(row[s:x])
            last = x
        rows += b'\0\0'
    tab.append(len(rows))
    if len(rows) & 1:
        rows += b'\0'
    w, h = int(right - left), int(bot - top)
    return w, h, int((D - 1) // 2 - left), int(cy - top), struct.pack('<%dI' % len(tab), *tab) + bytes(rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'dos', 'SKY.DAT'))
    ap.add_argument('--preview', default=None)
    args = ap.parse_args()
    pal = flight_palette()
    table = palette_table(pal)
    previews = []
    # the things' records come first, then all their headers, then the sprites: offsets from the data's start
    kept_all = []
    for name, draw, D, sym, layer, step, rows in THINGS:
        col, solid = master(draw, D)
        kept = TURNS // 2 if sym == 0 else TURNS // 4 + 1
        turns = []
        for k in range(kept):
            c, s = turned(col, solid, D, k * 360.0 / TURNS)
            if step == 2:
                c, s = halve(c, s)
            if rows == 3:
                c = np.clip(c * 1.5, 0, 255)                     # (interlaced: half its lines black, so brighter)
            spr = sprite(c, s, table)
            turns.append(rle(spr, (D - 1) // 2 // step))
            if step == 2:
                spr = np.repeat(spr, 2, 0)[:D]                  # (the preview: as it is drawn doubled)
            if k in (0, kept // 2) or (sym and k == kept - 1):
                previews.append((name, k, spr))
        kept_all.append((name, D, sym, layer, step, rows, turns))
        size = sum(len(t[4]) for t in turns)
        px = int(np.count_nonzero(sprite(*turned(col, solid, D, 0.0), table))) // step
        print('%-7s %3d px box, %3d turns kept of %d: %4d KB (%.1f KB a turn, %d px drawn)'
              % (name, D, kept, TURNS, size // 1024, size / 1024 / kept, px))
    nheads = sum(len(t[6]) for t in kept_all)
    head_bytes = 12 * nheads
    body = bytearray()
    heads = bytearray()
    things = bytearray()
    for name, D, sym, layer, step, rows, turns in kept_all:
        things += struct.pack('<HHBBHIBBH', len(turns), TURNS, sym, layer, D, len(heads), step, rows, 0)
        for w, h, ox, oy, blob in turns:
            heads += struct.pack('<4hI', w, h, ox, oy, head_bytes + len(body))
            body += blob
    data = b'RVSKY\0\0\0' + struct.pack('<HH', VERSION, len(kept_all)) + bytes(things) + bytes(heads) + bytes(body)
    open(args.out, 'wb').write(data)
    print('%s: %d bytes' % (args.out, len(data)))
    if args.preview:
        cell = 241
        img = np.zeros((((len(previews) + 2) // 3) * (cell + 4), 3 * (cell + 4), 3), dtype=np.uint8)
        for i, (name, k, spr) in enumerate(previews):
            D = spr.shape[0]
            y0, x0 = (i // 3) * (cell + 4) + (cell - D) // 2, (i % 3) * (cell + 4) + (cell - D) // 2
            img[y0:y0 + D, x0:x0 + D] = np.where(spr[..., None] > 0, pal[spr].astype(np.uint8), 0)
        Image.fromarray(img).resize((img.shape[1] * 2, img.shape[0] * 2), Image.NEAREST).save(args.preview)
        print('preview', args.preview)


if __name__ == '__main__':
    main()
