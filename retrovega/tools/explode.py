#!/usr/bin/env python3
"""The explosions' art (src/explode.c): a few small heat cels, and a preview of the explosion as the game draws it.

    explode.py [--out build/dos/EXPLODE.DAT] [--preview build/explode] [--seed N]

The user's brief (2026-10-28): compact, simple, lo-fi pixel art, unique; the old games' trick of few cels was
clever but looked odd held for many frames, and the good burst fades as it reaches its edge rather than vanishing
or shrinking back.  So the art is not colours but heat: CELS frames of an expanding, billowing fireball, CEL x CEL
pixels of 0 (clear) .. 255 (white hot), two variants.  The game draws a cel through a table made new every frame:

  - the cel advances slowly (each is held for frames), but every frame the ball grows a little (ease-out), cools a
    little (heat x gain falls: white, yellow, orange, red, the embers, then the smoke browns), and dissolves a
    little (a threshold climbs through the heat, dithered by a 4 x 4 ordered pattern in the cel's own pixels);
  - so between two cels the picture still moves every frame, and the cool edges go first: by its full size the
    ball has thinned to dim wisps, then nothing.
  - over it, at the screen's own pixels: a flash (a white core and a four-pointed glint, two frames), sparks
    (pixels and short streaks, cooling white to red as they fly), and debris (dark hull-grey chunks tumbling out).
  - each explosion turns its cel one of eight ways (flips and a transpose) and takes one of the variants, so no
    two look alike.

render_frame() below is the draw, step for step as explode.c does it: the preview is what the game shows.

EXPLODE.DAT: "RVBOOM\\0\\0", u16 version, u16 cels, u16 size, u16 variants, u16 the timeline's entries (TL), u8 the
ramp's colours, u8 the sparks', u8 the debris', u8 the dither's step; those colours (padded to even); the timeline, TL records of
u8 cel, u8 threshold, u16 gain (/256), u16 radius (/256 of the full), u16 the sparks' reach and u16 the debris'
(/256 of the full radius); then variants x cels x size x size bytes of heat, row by row.  The game reads the
timeline at age x TL / life, so a longer explosion (a capital ship's) is the same one slower.
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
CELS = 8
CEL = 48
VARIANTS = 2

# the colours by heat, cool to hot: the smoke browns, the embers, the fire ramp to white (the flight palette's fixed
# entries: 104.. browns, 128..143 fire)
RAMP = [105, 106, 107, 108, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143]
SPARK = [143, 141, 139, 137, 135, 134, 133, 131, 129, 128]   # a spark's colours as it cools
DEBRIS = [161, 168, 176, 192]                                # dark hull greys, the last a glint
BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]
DITHER = 3                              # the dissolve's dither: a pixel's heat + BAYER x DITHER against the threshold
UI = [(0, 0, 0), (28, 28, 30), (60, 60, 64), (100, 100, 104), (140, 140, 144), (180, 180, 184),
      (220, 220, 222), (255, 255, 255), (96, 58, 12), (168, 108, 28), (222, 160, 60), (252, 214, 124),
      (12, 18, 30), (52, 70, 98), (120, 216, 255), (210, 44, 32)]


def palette():
    pal = np.array(json.load(open(os.path.join(ROOT, 'data', 'palette.json')))['rgb'], dtype=np.uint8)
    pal[0:16] = UI
    return pal


# --- the cels: heat, 0 .. 255 ---

def value_noise(rng, size, cells):
    """smooth noise: a random grid, bicubically (smoothstep) interpolated, tiling"""
    g = rng.random((cells + 1, cells + 1))
    g[-1, :] = g[0, :]
    g[:, -1] = g[:, 0]
    t = np.linspace(0, cells, size, endpoint=False)
    i = t.astype(int)
    f = t - i
    f = f * f * (3 - 2 * f)
    a = g[i][:, i] * (1 - f)[None, :] + g[i][:, i + 1] * f[None, :]
    b = g[i + 1][:, i] * (1 - f)[None, :] + g[i + 1][:, i + 1] * f[None, :]
    return a * (1 - f)[:, None] + b * f[:, None]


def fbm(rng, size, octaves=4, base=3):
    out = np.zeros((size, size))
    amp, tot = 1.0, 0.0
    for o in range(octaves):
        out += amp * value_noise(rng, size, base * 2 ** o)
        tot += amp
        amp *= 0.5
    return out / tot


def angular_noise(rng, theta, lobes, octaves=4):
    """noise round a circle (periodic in theta): many narrow lobes, the rays' reach"""
    out = np.zeros_like(theta)
    amp, tot = 1.0, 0.0
    for o in range(octaves):
        n = lobes * 2 ** o
        g = rng.random(n + 1)
        g[-1] = g[0]
        t = (theta % (2 * math.pi)) / (2 * math.pi) * n
        i = t.astype(int)
        f = t - i
        f = f * f * (3 - 2 * f)
        out += amp * (g[i] * (1 - f) + g[np.minimum(i + 1, n)] * f)
        tot += amp
        amp *= 0.55
    return out / tot


def polar_noise(rng, theta, r, n_theta, n_r):
    """noise in polar coordinates, periodic round the circle: fast round it, slow outward: streaks radiating out"""
    g = rng.random((n_r + 2, n_theta + 1))
    g[:, -1] = g[:, 0]
    t = (theta % (2 * math.pi)) / (2 * math.pi) * n_theta
    u = np.clip(r, 0, 1.4) / 1.4 * n_r
    i, j = t.astype(int), u.astype(int)
    f, e = t - i, u - j
    f, e = f * f * (3 - 2 * f), e * e * (3 - 2 * e)
    a = g[j, i] * (1 - f) + g[j, i + 1] * f
    b = g[j + 1, i] * (1 - f) + g[j + 1, i + 1] * f
    return a * (1 - e) + b * e


def make_cels(seed):
    """CELS heat frames of one variant: a central blast going out in every direction.  A white-hot core; around it
    tongues of fire streaking outward, each direction its own reach (spiky noise round the circle: many narrow
    rays, no lumps) and its own streaks; the rays fly out and lengthen, the core burns out and the middle cools, so
    the burst ends as a ring of streaks; a fine turbulence over all"""
    rng = np.random.default_rng(seed)
    y, x = np.mgrid[0:CEL, 0:CEL]
    x = (x + 0.5) / (CEL / 2) - 1
    y = (y + 0.5) / (CEL / 2) - 1
    r = np.sqrt(x * x + y * y)
    theta = np.arctan2(y, x)
    curl = 0.18 * r * (fbm(np.random.default_rng(seed * 7 + 3), CEL, 2, 2) - 0.5)
    th = theta + curl
    spikes = angular_noise(rng, th, 30, 3)
    reach = 0.5 + 0.5 * spikes ** 1.7                      # narrow rays out of a rounder body
    streak = polar_noise(np.random.default_rng(seed * 7 + 4), th, r, 56, 3)
    cels = []
    for k in range(CELS):
        tau = k / (CELS - 1)
        turb = fbm(np.random.default_rng(seed * 100 + k), CEL, octaves=2, base=8)
        core = max(0.0, 1.05 - tau * 1.9)
        rc = 0.24 + 0.22 * tau
        h = core * np.clip(1 - (r / rc) ** 2, 0, 1) ** 0.4
        L = (0.42 + 0.56 * tau ** 0.55) * reach             # each ray's tip, of the cel's half
        along = np.clip(1 - r / L, 0, 1)
        # the burn-out: a front from the middle out to the rays' tips; behind it the fire has spent itself, so the
        # middle loses its energy first and the hot band rides outward with the spread (the user's, 2026-10-28)
        front = 1.2 * tau - 0.28
        burn = np.clip((r / (L + 1e-6) - front) / 0.3, 0, 1)
        rays = (1.3 - 0.45 * tau) * along ** (0.55 - 0.2 * tau) * (0.03 + 0.97 * burn)
        h = np.maximum(h, rays * (0.55 + 0.6 * streak))
        h *= 0.78 + 0.35 * turb
        h *= np.clip(1.02 - r, 0, 1) ** 0.25
        cels.append(np.clip(h * 255, 0, 255).astype(np.uint8))
    return cels


# --- the draw: what explode.c does, a frame at a time ---

FRAMES = 40                             # a fighter's: 1.33 s at 30 steps a second


def timeline_table():
    """the timeline as the game reads it: TL = FRAMES integer records"""
    out = []
    for t in range(FRAMES):
        cel, grow, gain, thr = timeline(t, FRAMES)
        tau = t / FRAMES
        spark = int(256 * 2.2 * (1 - (1 - tau) ** 1.8) / 1.8)
        debris = int(256 * 1.5 * tau ** 0.7)
        out.append((cel, thr, gain, int(grow * 256), spark, debris))
    return out


def timeline(t, frames):
    """the frame's cel, its radius (of the full), its heat gain (/256) and dissolve threshold (0..255)"""
    tau = t / frames
    cel = min(CELS - 1, int(CELS * tau ** 0.85))
    grow = 0.5 + 0.5 * (1 - (1 - tau) ** 2.6)
    # hot at first (white and yellow for a quarter of its life), then cooling through orange and red to the smoke
    gain = int(256 * (1.65 - 1.25 * tau ** 0.75))
    thr = int(255 * (0.03 + 0.99 * tau ** 3))                # the dissolve: slow, then the ring goes in the last third
    return cel, grow, gain, thr


def heat_table(gain, thr):
    """heat -> palette index: 0 below the threshold (dithered per pixel in the draw), else the ramp at heat x gain"""
    tab = [0] * 256
    for h in range(256):
        e = h * gain // 256
        idx = e * len(RAMP) // 256
        tab[h] = RAMP[min(len(RAMP) - 1, idx)]
    return tab


def orient(u, v, o, n):
    """a cel pixel read one of eight ways"""
    if o & 4:
        u, v = v, u
    if o & 1:
        u = n - 1 - u
    if o & 2:
        v = n - 1 - v
    return u, v


def render_fireball(img, cels, cx, cy, radius, t, frames, o):
    cel, grow, gain, thr = timeline(t, frames)
    r = max(1, int(radius * grow))
    tab = heat_table(gain, thr)
    c = cels[cel]
    size = 2 * r
    for sy in range(size):
        y = cy - r + sy
        if y < 0 or y >= img.shape[0]:
            continue
        v = sy * CEL // size
        for sx in range(size):
            x = cx - r + sx
            if x < 0 or x >= img.shape[1]:
                continue
            u = sx * CEL // size
            uu, vv = orient(u, v, o, CEL)
            h = int(c[vv, uu])
            if h == 0 or h + BAYER[v & 3][u & 3] * DITHER < thr:
                continue
            img[y, x] = tab[h]


def lcg(state):
    state = (state * 1103515245 + 12345) & 0xFFFFFFFF
    return state, (state >> 16) & 0x7FFF


def render_overlays(img, cx, cy, radius, t, frames, seed):
    """the flash, the sparks, the debris: at the screen's pixels"""
    hgt, wid = img.shape
    # the flash: two frames
    if t < 3:
        rr = max(1, int(radius * (0.12 if t == 0 else 0.16 if t == 1 else 0.19)))
        col = 143 if t < 2 else 141
        for y in range(cy - rr, cy + rr + 1):
            for x in range(cx - rr, cx + rr + 1):
                if 0 <= y < hgt and 0 <= x < wid and (x - cx) ** 2 + (y - cy) ** 2 <= rr * rr:
                    img[y, x] = col
        arm = int(radius * (1.3 if t == 0 else 0.9 if t == 1 else 0.5))
        for k in range(-arm, arm + 1):
            for (x, y) in ((cx + k, cy), (cx, cy + k)):
                if 0 <= y < hgt and 0 <= x < wid and img[y, x] == 0:
                    img[y, x] = 143 if abs(k) < arm // 2 else 140
    # the sparks: their own random stream (pictures never take the play's)
    st = seed
    for i in range(26):
        st, a = lcg(st)
        st, sp = lcg(st)
        st, life = lcg(st)
        ang = a / 32768 * 2 * math.pi
        speed = (0.35 + sp / 32768 * 0.9) * radius * 2.2 / frames
        life = int(frames * (0.35 + 0.45 * life / 32768))
        if t >= life:
            continue
        drag = (1 - (1 - t / frames) ** 1.8) * frames / 1.8
        dx, dy = math.cos(ang) * speed * drag, math.sin(ang) * speed * drag * 0.8
        x, y = int(cx + dx), int(cy + dy)
        col = SPARK[min(len(SPARK) - 1, t * len(SPARK) // life)]
        px, py = int(cx + dx * 0.88), int(cy + dy * 0.88)   # a short streak behind it
        for (qx, qy, cc) in ((px, py, SPARK[min(len(SPARK) - 1, t * len(SPARK) // life + 2)]), (x, y, col)):
            if 0 <= qy < hgt and 0 <= qx < wid:
                img[qy, qx] = cc
    # the debris: dark chunks tumbling out
    for i in range(6):
        st, a = lcg(st)
        st, sp = lcg(st)
        ang = a / 32768 * 2 * math.pi
        dist = (0.4 + sp / 32768 * 0.8) * radius * 1.5 * (t / frames) ** 0.7
        x, y = int(cx + math.cos(ang) * dist), int(cy + math.sin(ang) * dist * 0.8)
        n = 2 if (t + i) % 3 else 3                         # the tumble: its size flickers
        glint = (t + i * 5) % 9 == 0
        if t < 3 or (t > frames * 0.85 and (t + i) & 1):
            continue                                        # (not in the flash; a dithered fade at the end)
        for yy in range(y, y + n):
            for xx in range(x, x + n - (1 if (t + i) & 1 else 0)):
                if 0 <= yy < hgt and 0 <= xx < wid:
                    img[yy, xx] = DEBRIS[3] if glint else DEBRIS[(i + t // 4) % 3]


def stars(img, seed=7):
    rng = np.random.default_rng(seed)
    for _ in range(img.size // 900):
        img[rng.integers(img.shape[0]), rng.integers(img.shape[1])] = rng.choice([2, 3, 4, 5])


def preview(cels_by_variant, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    pal = palette()
    # a sheet: three explosions at their sizes (far, mid, near), every other frame, on the black of space
    sizes = [(22, 0, 1), (50, 1, 6), (95, 0, 3)]
    cw, ch = 220, 220
    sheet = np.zeros((ch * len(sizes), cw * (FRAMES // 2)), np.uint8)
    frames_gif = []
    for row, (radius, var, o) in enumerate(sizes):
        for t in range(0, FRAMES, 2):
            k = t // 2
            img = np.zeros((ch, cw), np.uint8)
            stars(img, 3 + row)
            render_fireball(img, cels_by_variant[var], cw // 2, ch // 2, radius, t, FRAMES, o)
            render_overlays(img, cw // 2, ch // 2, radius, t, FRAMES, 1234 + row)
            sheet[row * ch:(row + 1) * ch, k * cw:(k + 1) * cw] = img
    Image.fromarray(pal[sheet]).save(os.path.join(out_dir, 'sheet.png'))
    # the animation, all three side by side, every frame, at 30 a second
    for t in range(FRAMES + 8):
        img = np.zeros((ch, cw * 3), np.uint8)
        stars(img, 11)
        for i, (radius, var, o) in enumerate(sizes):
            if t < FRAMES:
                sub = img[:, i * cw:(i + 1) * cw]
                render_fireball(sub, cels_by_variant[var], cw // 2, ch // 2, radius, t, FRAMES, o)
                render_overlays(sub, cw // 2, ch // 2, radius, t, FRAMES, 1234 + i)
        frames_gif.append(Image.fromarray(pal[img]))
    frames_gif[0].save(os.path.join(out_dir, 'explode.gif'), save_all=True, append_images=frames_gif[1:],
                       duration=33, loop=0, disposal=1)
    # the cels themselves, heat as grey
    cs = np.concatenate([np.concatenate(v, axis=1) for v in cels_by_variant], axis=0)
    Image.fromarray(cs).resize((cs.shape[1] * 3, cs.shape[0] * 3), Image.NEAREST).save(
        os.path.join(out_dir, 'cels.png'))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out')
    ap.add_argument('--preview')
    ap.add_argument('--seed', type=int, default=5)
    a = ap.parse_args()
    variants = [make_cels(a.seed + 17 * v) for v in range(VARIANTS)]
    if a.out:
        with open(a.out, 'wb') as f:
            f.write(b'RVBOOM\0\0' + struct.pack('<HHHHHBBBB', VERSION, CELS, CEL, VARIANTS, FRAMES, len(RAMP),
                                                  len(SPARK), len(DEBRIS), DITHER))
            cols = bytes(RAMP + SPARK + DEBRIS)
            f.write(cols + b'\0' * (len(cols) & 1))
            for rec in timeline_table():
                f.write(struct.pack('<BBHHHH', *rec))
            for cels in variants:
                for c in cels:
                    f.write(c.tobytes())
        print('%s: %d variants of %d cels of %d x %d heat, %d bytes' % (a.out, VARIANTS, CELS, CEL, CEL,
                                                                      os.path.getsize(a.out)))
    if a.preview:
        preview(variants, a.preview)


if __name__ == '__main__':
    main()
