#!/usr/bin/env python3
"""The asteroid fields' rocks (src/rocks.c): every kind, tumble frame and
size, rendered and put in the flight palette.

    rocks.py [--out build/dos/ROCKS.DAT] [--preview docs/rocks.png] [--gif build/rocks.gif]

The user's brief (2026-10-23): about four kinds of asteroid, tumbling
endlessly and quickly, that only ever scale (no turning with the camera's
roll: they stay upright through it), drawn as plain XMS copies into the
frame.  So a rock's sprite is one picture per tumble frame and size, and each
row of it is ONE run of opaque pixels (whatever a row holds between its first
and last opaque pixel is drawn), an even number of them (XMS moves even
lengths): a row is a single XMS move straight into the band buffer.

The rocks are procedural meshes (an icosphere pushed in and out: fractal
noise, craters with rims, each kind's own body), lit by the balls' key light
(up and to the left, the viewer's side), rendered on Mesa through moderngl as
the ships are (docs/art.md), TUMBLE frames a turn about a tilted axis, three
times finer than the biggest size, then box-filtered to each size.  A pixel
mostly covered is the rock; the colours are the flight palette's fixed ones
(1..207) or a checker of two (tools/sky.py's table).

Kinds: a grey stony potato with craters; a dark carbon ball pocked all over;
a rusty iron peanut (two lobes); a pale angular shard, broken flat.

Sizes: every diameter 1 .. SMALL (the far ones, nearly all of them: kept
d x d with 0 clear, as the balls are, SMALL_FRAMES of the tumble's frames,
the engine holds them in conventional memory), then BIG sizes from SMALL+1 to
BIG_MAX on a ladder (every pixel to 16, then 7% steps).  Nearer than BIG_MAX
the engine draws the ladder's sprites as 2 x 2, 3 x 3 or 4 x 4 blocks.

ROCKS.DAT (little-endian):
    "RVROCKS\\0", u16 version, u16 kinds, u16 frames (the tumble), u16
    small_frames, u16 small (the biggest small diameter), u16 nbig, u16
    big_max, u16 0, then u8 the big diameters [nbig] (padded to even),
    then the small sprites, kind-major: for each small frame (frame
    f x frames / small_frames) and d = 1 .. small, d x d bytes (0 clear),
    then the big sprites' index: for each kind, frame, big size, u32 its
    record's offset in the file, then the records (each at an even offset):
    u16 rows (= its diameter d), then a pair a row: u8 left + 1 (the run's
    first column, -1 .. d), u8 length (even, 0 an empty row), then every
    row's run of pixels in turn.
"""
import argparse
import math
import os
import struct
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, HERE)
import sky  # noqa: E402  (the flight palette and its checker table)

VERSION = 1
TUMBLE = 32                 # frames a turn
SMALL = 8                   # the small sizes: 1 .. SMALL
SMALL_FRAMES = 8            # ... at every fourth frame of the tumble
BIG_MAX = 128
SS = 3                      # the render: SS times finer than BIG_MAX
NAMES = ['stony', 'carbon', 'iron', 'shard']
KEY = np.array([-0.55, 0.6, 0.58])   # the balls' key light: up, left, toward the viewer
KEY = KEY / np.linalg.norm(KEY)


def big_sizes():
    out, d = [], SMALL + 1
    while d <= BIG_MAX:
        out.append(d)
        d = d + 1 if d < 16 else max(d + 1, int(round(d * 1.07)))
    if out[-1] != BIG_MAX:
        out.append(BIG_MAX)
    return out


# --- the meshes ---

def icosphere(level):
    t = (1 + 5 ** 0.5) / 2
    v = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t),
         (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
    f = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6),
         (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10),
         (8, 6, 7), (9, 8, 1)]
    verts = [np.array(p, float) / np.linalg.norm(p) for p in v]
    faces = np.array(f)
    for _ in range(level):
        cache, nf = {}, []

        def mid(a, b):
            k = (a, b) if a < b else (b, a)
            if k not in cache:
                m = verts[a] + verts[b]
                verts.append(m / np.linalg.norm(m))
                cache[k] = len(verts) - 1
            return cache[k]
        for a, b, c in faces:
            ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
            nf += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        faces = np.array(nf)
    return np.array(verts), faces


def lattice_noise(p, seed):
    """smooth value noise in -1 .. 1 at points p (N, 3), a unit lattice"""
    rng = np.random.default_rng(seed)
    table = rng.uniform(-1, 1, 4096)
    perm = rng.permutation(4096)
    i = np.floor(p).astype(np.int64)
    f = p - i
    f = f * f * (3 - 2 * f)
    out = 0
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                h = perm[(perm[(perm[(i[:, 0] + dx) & 4095] + i[:, 1] + dy) & 4095] + i[:, 2] + dz) & 4095]
                w = (f[:, 0] if dx else 1 - f[:, 0]) * (f[:, 1] if dy else 1 - f[:, 1]) * (f[:, 2] if dz else 1 - f[:, 2])
                out = out + w * table[h]
    return out


def fbm(p, seed, base, octaves, gain=0.5):
    out, amp, tot = 0, 1.0, 0.0
    for o in range(octaves):
        out = out + amp * lattice_noise(p * base * 2 ** o + 17.3 * o, seed + 31 * o)
        tot += amp
        amp *= gain
    return out / tot


def craters(dirs, rng, n, rmin, rmax, depth):
    """the push of n craters (bowls with raised rims) at points of the unit sphere"""
    out = np.zeros(len(dirs))
    floor = np.zeros(len(dirs))
    for _ in range(n):
        c = rng.normal(size=3)
        c /= np.linalg.norm(c)
        rho = rmin * (rmax / rmin) ** rng.uniform() ** 1.6            # (more small ones than big)
        ang = np.arccos(np.clip(dirs @ c, -1, 1))
        t = ang / rho
        d = depth * rho
        bowl = np.where(t < 1, d * (t * t - 1) * 0.9, 0)
        rim = np.where((t >= 0.8) & (t < 1.6), d * 0.35 * np.cos((t - 1.0) / 0.6 * math.pi / 2) ** 2, 0)
        out += bowl + rim
        floor = np.maximum(floor, np.where(t < 0.85, 1 - t / 0.85, 0))
    return out, floor


def two_lobes(dirs, lobes, k=10.0):
    """the radius along each direction of the union of spheres (centre, radius) holding the origin, smoothed"""
    rs = []
    for c, R in lobes:
        c = np.asarray(c, float)
        b = dirs @ c
        rs.append(b + np.sqrt(np.maximum(b * b - c @ c + R * R, 0)))
    rs = np.stack(rs)
    return np.log(np.exp(k * rs).sum(0)) / k


def facets(dirs, rng, n, dmin, dmax):
    """the radius along each direction of a convex body cut by n planes"""
    r = np.full(len(dirs), 9.0)
    for _ in range(n):
        nrm = rng.normal(size=3)
        nrm /= np.linalg.norm(nrm)
        d = rng.uniform(dmin, dmax)
        c = dirs @ nrm
        r = np.where(c > 1e-3, np.minimum(r, d / np.maximum(c, 1e-3)), r)
    return r


def rock_mesh(kind, level=6):
    """positions, triangles, per-vertex albedo (linear RGB) and whether its shading is flat"""
    dirs, faces = icosphere(level)
    rng = np.random.default_rng(1000 + kind)
    name = NAMES[kind]
    if name == 'stony':
        r = 1 / np.sqrt((dirs[:, 0] / 1.0) ** 2 + (dirs[:, 1] / 0.72) ** 2 + (dirs[:, 2] / 0.62) ** 2)
        r = r * (1 + 0.26 * fbm(dirs, 11, 1.1, 3) + 0.05 * fbm(dirs, 12, 4.0, 4))
        cr, floor = craters(dirs, rng, 16, 0.10, 0.45, 0.30)
        base, spot = np.array([0.20, 0.175, 0.15]), np.array([0.27, 0.23, 0.18])
    elif name == 'carbon':
        r = 1 / np.sqrt((dirs[:, 0] / 1.0) ** 2 + (dirs[:, 1] / 0.93) ** 2 + (dirs[:, 2] / 0.88) ** 2)
        r = r * (1 + 0.08 * fbm(dirs, 21, 1.3, 3) + 0.03 * fbm(dirs, 22, 6.0, 3))
        cr, floor = craters(dirs, rng, 46, 0.06, 0.38, 0.32)
        base, spot = np.array([0.075, 0.072, 0.072]), np.array([0.11, 0.105, 0.10])
    elif name == 'iron':
        r = two_lobes(dirs, [((0.42, 0.04, 0.0), 0.62), ((-0.46, -0.05, 0.03), 0.50)])
        r = r * (1 + 0.07 * fbm(dirs, 31, 1.5, 3) + 0.03 * fbm(dirs, 32, 5.0, 4))
        cr, floor = craters(dirs, rng, 12, 0.08, 0.30, 0.25)
        base, spot = np.array([0.26, 0.12, 0.065]), np.array([0.34, 0.16, 0.07])
    else:
        # a broken piece: an elongated body cut flat by planes from every side
        body = 1 / np.sqrt((dirs[:, 0] / 1.0) ** 2 + (dirs[:, 1] / 0.62) ** 2 + (dirs[:, 2] / 0.48) ** 2)
        r = np.minimum(body, facets(dirs, rng, 18, 0.42, 0.85))
        r = r * (1 + 0.025 * fbm(dirs, 41, 3.0, 4))
        cr, floor = craters(dirs, rng, 5, 0.06, 0.18, 0.18)
        base, spot = np.array([0.22, 0.23, 0.26]), np.array([0.29, 0.30, 0.33])
    r = r + cr * 0.6
    pos = dirs * r[:, None]
    pos /= np.linalg.norm(pos, axis=1).max()
    mott = 0.5 + 0.5 * fbm(dirs, 50 + kind, 2.5, 4)
    alb = base[None] * (1 - mott[:, None]) + spot[None] * mott[:, None]
    alb = alb * (1 - 0.30 * floor[:, None])                       # crater floors darker
    nrm = np.zeros_like(pos)
    fn = np.cross(pos[faces[:, 1]] - pos[faces[:, 0]], pos[faces[:, 2]] - pos[faces[:, 0]])   # (area-weighted)
    for k in range(3):
        np.add.at(nrm, faces[:, k], fn)
    nrm /= np.maximum(np.linalg.norm(nrm, axis=1), 1e-12)[:, None]
    return pos, nrm, faces, alb, name == 'shard'


# --- the render ---

VS = """
#version 330
uniform mat3 rot;
in vec3 in_pos;
in vec3 in_norm;
in vec3 in_alb;
out vec3 v_pos;
out vec3 v_obj;
out vec3 v_norm;
out vec3 v_alb;
void main() {
    v_obj = in_pos;
    v_pos = rot * in_pos;
    v_norm = rot * in_norm;
    v_alb = in_alb;
    gl_Position = vec4(v_pos.x, v_pos.y, -v_pos.z * 0.5, 1.0);
}
"""

FS = """
#version 330
uniform vec3 key;
uniform float flat_shade;
uniform float shine;
uniform mat3 rot;
in vec3 v_pos;
in vec3 v_obj;
in vec3 v_norm;
in vec3 v_alb;
out vec4 frag;
float hash(vec3 p) { return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453); }
float vnoise(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash(i), hash(i + vec3(1, 0, 0)), f.x), mix(hash(i + vec3(0, 1, 0)), hash(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(hash(i + vec3(0, 0, 1)), hash(i + vec3(1, 0, 1)), f.x), mix(hash(i + vec3(0, 1, 1)), hash(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}
void main() {
    // a shard's faces flat (the triangle's own normal), the others smooth
    vec3 n = flat_shade > 0.5 ? normalize(cross(dFdx(v_pos), dFdy(v_pos))) : normalize(v_norm);
    if (flat_shade > 0.5 && n.z < 0.0) n = -n;
    // fine grain on the surface: the normal tipped by a little noise, the albedo speckled
    vec3 q = v_obj * 26.0;
    vec3 g = vec3(vnoise(q + 1.7) - 0.5, vnoise(q + 9.1) - 0.5, vnoise(q + 4.3) - 0.5);
    n = normalize(n + (1.0 - flat_shade * 0.6) * 0.45 * (rot * g));
    float speck = 0.82 + 0.36 * vnoise(v_obj * 60.0);
    float diff = max(dot(n, key), 0.0);
    float wrap = max(dot(n, normalize(vec3(0.6, -0.35, 0.5))), 0.0);   // a faint fill from the lower right
    vec3 h = normalize(key + vec3(0.0, 0.0, 1.0));
    float spec = pow(max(dot(n, h), 0.0), 24.0) * shine;
    vec3 c = v_alb * speck * (0.04 + 1.9 * diff + 0.22 * wrap) + vec3(spec);
    frag = vec4(c, 1.0);
}
"""


def rotation(axis, angle):
    a = np.asarray(axis, float)
    a /= np.linalg.norm(a)
    k = np.array([[0, -a[2], a[1]], [a[2], 0, -a[0]], [-a[1], a[0], 0]])
    return np.eye(3) + math.sin(angle) * k + (1 - math.cos(angle)) * (k @ k)


# each kind's tumble: the axis it turns about (screen: x right, y up, z to the viewer) and its first pose
TUMBLES = [((0.35, 0.80, 0.48), (0.4, 1.0, 0.2), 0.7), ((0.70, 0.30, 0.65), (1.0, 0.2, 0.4), 1.9),
           ((0.25, 0.55, 0.80), (0.2, 0.3, 1.0), 0.4), ((0.85, 0.45, 0.30), (0.5, 0.8, 0.3), 2.6)]


def render_kind(ctx, kind, n):
    """the kind's TUMBLE frames, n x n: linear RGB (premultiplied) and coverage"""
    import moderngl
    pos, nrm, faces, alb, flat = rock_mesh(kind)
    prog = ctx.program(vertex_shader=VS, fragment_shader=FS)
    prog['key'].value = tuple(KEY)
    prog['flat_shade'].value = 1.0 if flat else 0.0
    prog['shine'].value = {'iron': 0.25, 'shard': 0.08}.get(NAMES[kind], 0.04)
    corners = faces.reshape(-1)
    data = np.hstack([pos[corners], nrm[corners], alb[corners]]).astype('f4')
    vao = ctx.vertex_array(prog, [(ctx.buffer(data.tobytes()), '3f 3f 3f', 'in_pos', 'in_norm', 'in_alb')])
    color = ctx.texture((n, n), 4, dtype='f4')
    depth = ctx.depth_texture((n, n))
    fbo = ctx.framebuffer(color_attachments=[color], depth_attachment=depth)
    axis, pose_axis, pose_angle = TUMBLES[kind]
    pose = rotation(pose_axis, pose_angle)
    frames = []
    for f in range(TUMBLE):
        rot = rotation(axis, 2 * math.pi * f / TUMBLE) @ pose
        prog['rot'].write(rot.T.astype('f4').tobytes())
        fbo.use()
        fbo.clear(0, 0, 0, 0, depth=1.0)
        ctx.enable(moderngl.DEPTH_TEST)
        vao.render(moderngl.TRIANGLES)
        img = np.frombuffer(color.read(), dtype=np.float32).reshape(n, n, 4)[::-1]
        frames.append(img.copy())
    return frames


def shrink(img, d):
    """an n x n render box-filtered to d x d: sRGB colour (unpremultiplied) and coverage"""
    n = img.shape[0]
    chans = []
    for k in range(4):
        ch = img[..., k] * (img[..., 3] if k < 3 else 1)
        chans.append(np.asarray(Image.fromarray(ch.astype(np.float32), 'F').resize((d, d), Image.BOX), dtype=np.float64))
    a = np.clip(chans[3], 0, 1)
    lin = np.stack(chans[:3], -1) / np.maximum(a, 0.3)[..., None]
    srgb = np.where(lin <= 0.0031308, 12.92 * lin, 1.055 * np.power(np.clip(lin, 0, None), 1 / 2.4) - 0.055)
    return np.clip(srgb * 255, 0, 255), a


def small_sprite(col, a, table):
    d = a.shape[0]
    cut = 0.5 if d > 2 else 0.25
    idx = sky.quantize(np.maximum(col, 6.0), table) if d > 2 else sky.quantize(np.maximum(col, 6.0), (table[0], table[0]))
    if d <= 2:                         # (one colour: the rock's middle's)
        m = a > 0
        mean = (col * a[..., None])[m].sum(0) / max(a[m].sum(), 1e-6) if m.any() else np.array([60.0, 60, 60])
        idx = np.full((d, d), sky.quantize(np.maximum(mean, 6.0)[None, None], (table[0], table[0]))[0, 0])
    return np.where(a >= cut, idx, 0).astype(np.uint8)


def big_record(col, a, table):
    """one big sprite's record: each row one even run (u8 left + 1, u8 length), then the runs' pixels"""
    d = a.shape[0]
    idx = sky.quantize(np.maximum(col, 6.0), table)
    pad = np.zeros((d, d + 2), np.float64)
    pad[:, 1:-1] = a
    heads, pix, px = bytearray(), bytearray(), 0
    for y in range(d):
        on = np.nonzero(a[y] >= 0.5)[0]
        if not len(on):
            heads += bytes([0, 0])
            continue
        x0, x1 = int(on[0]), int(on[-1]) + 1
        if (x1 - x0) & 1:              # (an even run: the side more covered takes the pixel)
            left = pad[y, x0] if x0 > 0 else -1
            right = pad[y, x1 + 1] if x1 < d else -1
            if right >= left and x1 < d:
                x1 += 1
            elif x0 > 0:
                x0 -= 1
            else:
                x1 += 1                # (both at the box's edges: one past it, the box's own next column)
        row = np.zeros(x1 - x0, np.uint8)
        inside = idx[y, max(x0, 0):min(x1, d)]
        row[max(x0, 0) - x0:max(x0, 0) - x0 + len(inside)] = inside
        if x1 > d:
            row[-1] = row[-2]
        heads += bytes([x0 + 1, x1 - x0])
        pix += bytes(row)
        px += x1 - x0
    return struct.pack('<H', d) + bytes(heads) + bytes(pix), px


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'dos', 'ROCKS.DAT'))
    ap.add_argument('--preview', default=None)
    ap.add_argument('--gif', default=None, help='the four tumbling at a few sizes, as an animation')
    args = ap.parse_args()
    import moderngl
    ctx = moderngl.create_standalone_context(backend='egl')
    pal = sky.flight_palette()
    table = sky.palette_table(pal)
    sizes = big_sizes()
    n = BIG_MAX * SS
    small_blob, index, records = bytearray(), [], bytearray()
    sheet_cells = []
    gif_frames = [[] for _ in range(TUMBLE)]
    for kind in range(len(NAMES)):
        frames = render_kind(ctx, kind, n)
        kpx = 0
        for f in range(SMALL_FRAMES):
            img = frames[f * TUMBLE // SMALL_FRAMES]
            for d in range(1, SMALL + 1):
                small_blob += bytes(small_sprite(*shrink(img, d), table).reshape(-1))
        for f, img in enumerate(frames):
            for d in sizes:
                rec, px = big_record(*shrink(img, d), table)
                index.append(len(records))
                records += rec
                if len(records) & 1:
                    records += b'\0'
                kpx += px
            for d in (16, 48, BIG_MAX):
                col, a = shrink(img, d)
                spr = sky.quantize(np.maximum(col, 6.0), table)
                spr = np.where(a >= 0.5, spr, 0)
                gif_frames[f].append((kind, d, spr))
                if f in (0, 8, 16, 24) and d == BIG_MAX:
                    sheet_cells.append(spr)
        print('%-6s %2d frames x %d big sizes: %5.0f KB of runs' % (NAMES[kind], TUMBLE, len(sizes), kpx / 1024))
    nk = len(NAMES)
    head = b'RVROCKS\0' + struct.pack('<8H', VERSION, nk, TUMBLE, SMALL_FRAMES, SMALL, len(sizes), BIG_MAX, 0)
    head += bytes(sizes) + (b'\0' if len(sizes) & 1 else b'')
    at_index = len(head) + len(small_blob)
    at_records = at_index + 4 * len(index)
    if at_records & 1:
        sys.exit('odd offsets')
    data = head + bytes(small_blob) + struct.pack('<%dI' % len(index), *[at_records + i for i in index]) + bytes(records)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    open(args.out, 'wb').write(data)
    print('%s: %d bytes (%d kinds, %d frames, sizes 1..%d small, %d big to %d)'
          % (args.out, len(data), nk, TUMBLE, SMALL, len(sizes), BIG_MAX))
    if args.preview:
        cell = BIG_MAX + 8
        img = np.zeros((nk * cell, 4 * cell, 3), np.uint8)
        for i, spr in enumerate(sheet_cells):
            y0, x0 = (i // 4) * cell + 4, (i % 4) * cell + 4
            img[y0:y0 + BIG_MAX, x0:x0 + BIG_MAX] = np.where(spr[..., None] > 0, pal[spr].astype(np.uint8), 0)
        Image.fromarray(img).resize((img.shape[1] * 2, img.shape[0] * 2), Image.NEAREST).save(args.preview)
        print('preview', args.preview)
    if args.gif:
        W = 16 + 48 + BIG_MAX + 4 * 8
        ims = []
        for f in range(TUMBLE):
            img = np.zeros((nk * (BIG_MAX + 8), W, 3), np.uint8)
            for kind, d, spr in gif_frames[f]:
                y0 = kind * (BIG_MAX + 8) + (BIG_MAX + 8 - d) // 2
                x0 = {16: 8, 48: 32, BIG_MAX: 88}[d]
                img[y0:y0 + d, x0:x0 + d] = np.where(spr[..., None] > 0, pal[spr].astype(np.uint8), 0)
            ims.append(Image.fromarray(img).resize((W * 2, img.shape[0] * 2), Image.NEAREST))
        ims[0].save(args.gif, save_all=True, append_images=ims[1:], duration=33, loop=0)
        print('gif', args.gif)


if __name__ == '__main__':
    main()
