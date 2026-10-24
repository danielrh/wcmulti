#!/usr/bin/env python3
"""The asteroid fields' rocks (src/rocks.c): every kind, tumble frame and
size, rendered and put in the flight palette.

    rocks.py [--out build/dos/ROCKS.DAT] [--preview docs/rocks.png] [--gif build/rocks.gif]

The user's brief (2026-10-23): about four kinds of asteroid, tumbling
endlessly and quickly, that only ever scale (no turning with the camera's
roll: they stay upright through it), drawn as plain XMS copies into the
frame.  So a rock's sprite is one picture per tumble frame and size, and each
row of it a few runs of opaque pixels (at most RUNS: the rocks have overhangs
and gaps; a gap under 3 px is drawn), each an even number of them (XMS moves
even lengths): a run is a single XMS move straight into the band buffer.

The rocks are procedural meshes, lit by the balls' key light (up and to the
left, the viewer's side) with a fill from the lower right, rendered on Mesa
through moderngl as the ships are (docs/art.md), TUMBLE frames of a turn
about the screen's vertical, three times finer than the biggest size, then
box-filtered to each size.  A pixel mostly covered is the rock; the colours
are the flight palette's fixed ones (1..207) or a checker of two
(tools/sky.py's table).

Kinds (the user, 2026-10-24: "jagged bright gray rocks... shattered planet
crust rather than Ceres... a sharp rock found on a playground... roughly
round and spin horizontally"): four stones, each a roughly round body cut
flat by planes from every side and chipped deeper here and there, its faces
flat-shaded, a little grain on them: a speckled granite, a blue-grey slate
of few broad faces, a warm flint of many small ones, a pale quartzite
chipped all over.

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
    u16 rows (= its diameter d), u16 the run table's bytes (even), the run
    table: for each row a count of runs (0 .. RUNS), then for each run u8
    left + 1 (its first column, -1 .. d) and u8 length (even), then every
    run's pixels in turn.
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

VERSION = 2                 # 2: a row holds up to RUNS runs (the rocks have overhangs)
TUMBLE = 32                 # frames a turn
SMALL = 8                   # the small sizes: 1 .. SMALL
SMALL_FRAMES = 8            # ... at every fourth frame of the tumble
BIG_MAX = 128
SS = 3                      # the render: SS times finer than BIG_MAX
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

# each kind (the user, 2026-10-24: the faces kept, "big noise... flash frozen metal possibly... rich noisy
# textures", their pictures a black glinting slag and a heap of grey and rose lava rock; then, on those: "more
# jagged and less convex... some overhangs etc very sharp", with a sheet of lumpy holed rocks): how many faceted
# chunks it is made of, how many faces each, how many bubble caves and bites are taken out of it, how rough its
# faces, its colour (linear), and its surface's look: how many of the bubble cells are pits, how fine, how big the
# light-dark blotches, how many metal glints, how shiny
KINDS = [
    # name     chunks faces caves bites rough   colour                 pits  scale blotch glint shine
    ('scoria',   4,    12,   9,    2,   0.030, (0.25, 0.245, 0.24),  0.45, 4.0, 0.85, 0.03, 0.10),
    ('slag',     3,    10,   7,    3,   0.024, (0.075, 0.075, 0.085), 0.35, 5.0, 0.70, 0.12, 0.55),
    ('lava',     5,    14,  12,    2,   0.036, (0.25, 0.17, 0.15),   0.50, 4.5, 0.90, 0.02, 0.08),
    ('pumice',   4,    11,  14,    3,   0.030, (0.33, 0.33, 0.32),   0.60, 6.0, 0.60, 0.02, 0.05),
]
CHUNKS, FACES, CAVES, BITES = 5, 16, 16, 4   # (the shader's array sizes: at most so many)


def spread(n, rng, jitter):
    """n directions spread evenly round the sphere (a Fibonacci lattice), jittered"""
    k = np.arange(n) + 0.5
    phi = np.arccos(1 - 2 * k / n)
    th = math.pi * (1 + 5 ** 0.5) * k + rng.uniform(0, 2 * math.pi)
    v = np.stack([np.cos(th) * np.sin(phi), np.sin(th) * np.sin(phi), np.cos(phi)], 1) + rng.normal(scale=jitter, size=(n, 3))
    return v / np.linalg.norm(v, axis=1)[:, None]


def solid(kind):
    """the kind's rock as a solid for the shader to march: chunks (each a convex polytope: its planes n . p <= d),
    their union the body (where two meet, a sharp crease and overhang); caves (spheres) and bites (small polytopes)
    taken out of it near its surface; scaled so it fits the unit sphere.  Returns (planes, chunk ends, caves, bite
    planes, bite ends)"""
    rng = np.random.default_rng(3000 + kind)
    _name, chunks, faces, caves, bites = KINDS[kind][:5]
    planes, ends = [], []
    for c in range(chunks):
        centre = rng.normal(size=3) * (0.0 if c == 0 else 0.30)
        size = rng.uniform(0.62, 0.78) if c == 0 else rng.uniform(0.40, 0.62)
        for nrm in spread(faces, rng, 0.25):
            planes.append((nrm, float(nrm @ centre + size * rng.uniform(0.72, 1.0))))
        ends.append(len(planes))
    P = np.array([n for n, _ in planes])
    D = np.array([d for _, d in planes])

    def body(p):
        out, a = np.full(len(p), 9.0), 0
        for e in ends:
            out = np.minimum(out, (p @ P[a:e].T - D[a:e]).max(1))
            a = e
        return out
    # caves and bites near the surface: along random ways, where the body ends
    def surface_point(u):
        t = np.linspace(0, 1.6, 400)
        d = body(t[:, None] * u[None])
        inside = np.nonzero(d < 0)[0]
        return t[inside[-1]] * u if len(inside) else 0.5 * u
    cave = []
    for _ in range(caves):
        u = rng.normal(size=3)
        u /= np.linalg.norm(u)
        r = rng.uniform(0.07, 0.20)
        cave.append((surface_point(u) * (1 + r * rng.uniform(-0.2, 0.6)), r))
    bite_planes, bite_ends = [], []
    for _ in range(bites):
        u = rng.normal(size=3)
        u /= np.linalg.norm(u)
        centre = surface_point(u)
        size = rng.uniform(0.14, 0.26)
        for nrm in spread(6, rng, 0.35):
            bite_planes.append((nrm, float(nrm @ centre + size * rng.uniform(0.6, 1.0))))
        bite_ends.append(len(bite_planes))
    # centred on its own middle (the mean of its inside, on a grid; it spins about it), then the scale that fits
    # it in the unit sphere: its farthest point inside
    g = np.linspace(-1.6, 1.6, 72)
    pts = np.stack(np.meshgrid(g, g, g, indexing='ij'), -1).reshape(-1, 3)
    inside = body(pts) < 0
    for c, r in cave:
        inside &= np.linalg.norm(pts - c, axis=1) > r
    m = pts[inside].mean(0)
    k = 0.96 / float(np.linalg.norm(pts[inside] - m, axis=1).max())
    planes = [(n, (d - n @ m) * k) for n, d in planes]
    cave = [((c - m) * k, r * k) for c, r in cave]
    bite_planes = [(n, (d - n @ m) * k) for n, d in bite_planes]
    return planes, ends, cave, bite_planes, bite_ends


# --- the render ---

VS = """
#version 330
in vec2 in_xy;
out vec2 v_xy;
void main() {
    v_xy = in_xy;
    gl_Position = vec4(in_xy, 0.0, 1.0);
}
"""

FS = """
#version 330
#define CHUNKS @CHUNKS@
#define FACES @FACES@
#define CAVES @CAVES@
#define BITES @BITES@
uniform vec3 key;
uniform float holes, scale, blotch, glint, shine;
uniform float seed, rough;
uniform vec3 alb;
uniform mat3 rot;
uniform vec4 planes[CHUNKS * FACES];
uniform int chunk_end[CHUNKS];
uniform int nchunk;
uniform vec4 caves[CAVES];
uniform int ncave;
uniform vec4 bite_planes[BITES * 6];
uniform int bite_end[BITES];
uniform int nbite;
in vec2 v_xy;
out vec4 frag;
float hash(vec3 p) { return fract(sin(dot(p + seed, vec3(127.1, 311.7, 74.7))) * 43758.5453); }
vec3 hash3(vec3 p) {
    p += seed;
    return fract(sin(vec3(dot(p, vec3(127.1, 311.7, 74.7)), dot(p, vec3(269.5, 183.3, 246.1)),
                          dot(p, vec3(113.5, 271.9, 124.6)))) * 43758.5453);
}
float vnoise(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash(i), hash(i + vec3(1, 0, 0)), f.x), mix(hash(i + vec3(0, 1, 0)), hash(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(hash(i + vec3(0, 0, 1)), hash(i + vec3(1, 0, 1)), f.x), mix(hash(i + vec3(0, 1, 1)), hash(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}
float fbm(vec3 p) {
    float s = 0.0, a = 0.5;
    for (int k = 0; k < 5; k++) { s += a * vnoise(p); p = p * 2.03 + 11.7; a *= 0.5; }
    return s / 0.97;
}
// the bubbles: Worley cells, a share of them holes, each a bowl; rim: just outside a hole; id: the nearest cell's
float bubbles(vec3 p, float frac, out float rim, out float id) {
    vec3 i = floor(p), f = fract(p);
    float pit = 0.0, near = 9.0;
    rim = 0.0;
    id = 0.0;
    for (int z = -1; z <= 1; z++) for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++) {
        vec3 g = vec3(x, y, z), o = hash3(i + g);
        float v = hash(i + g + 0.5), d = length(g + o - f), rad = 0.25 + 0.25 * fract(v * 7.31);
        if (d < near) { near = d; id = v; }
        if (v < frac) {
            pit = max(pit, clamp(1.0 - (d / rad) * (d / rad), 0.0, 1.0));
            rim = max(rim, clamp(1.0 - abs(d - rad * 1.1) / (rad * 0.25), 0.0, 1.0));
        }
    }
    return pit;
}
// the surface's height: big lumps, the bubbles' pits (two sizes), fine grit
float height(vec3 p) {
    float rim, id, rim2, id2;
    float h = 0.6 * fbm(p * 2.2) - 0.9 * bubbles(p * scale, holes, rim, id) - 0.5 * bubbles(p * scale * 2.3, holes, rim2, id2);
    return h + 0.15 * vnoise(p * 55.0);
}
// the solid: its chunks' union, less its caves and bites, its faces a little rough; < 0 inside (a bound: safe
// to march by)
float sdf(vec3 p) {
    float d = 1e9;
    int a = 0;
    for (int c = 0; c < nchunk; c++) {
        float w = -1e9;
        for (int i = a; i < chunk_end[c]; i++) w = max(w, dot(planes[i].xyz, p) - planes[i].w);
        d = min(d, w);
        a = chunk_end[c];
    }
    for (int h = 0; h < ncave; h++) d = max(d, caves[h].w - length(p - caves[h].xyz));
    a = 0;
    for (int b = 0; b < nbite; b++) {
        float w = -1e9;
        for (int i = a; i < bite_end[b]; i++) w = max(w, dot(bite_planes[i].xyz, p) - bite_planes[i].w);
        d = max(d, -w);
        a = bite_end[b];
    }
    return d + rough * (vnoise(p * 11.0) - 0.5);
}
void main() {
    // marched from the viewer (orthographic, +z toward them) into the turned solid
    mat3 irot = transpose(rot);
    float t = 0.0, d = 1.0;
    vec3 v_obj = vec3(0.0);
    for (int i = 0; i < 200; i++) {
        v_obj = irot * vec3(v_xy, 1.2 - t);
        d = sdf(v_obj);
        if (d < 0.0006) break;
        t += max(d * 0.7, 0.0012);
        if (t > 2.4) break;
    }
    if (d >= 0.0006) discard;
    // its faces' way (the solid's gradient: the creases stay sharp), tipped by the surface's bumps
    float q = 0.0015;
    vec3 gs = vec3(sdf(v_obj + vec3(q, 0, 0)) - sdf(v_obj - vec3(q, 0, 0)), sdf(v_obj + vec3(0, q, 0)) - sdf(v_obj - vec3(0, q, 0)),
                   sdf(v_obj + vec3(0, 0, q)) - sdf(v_obj - vec3(0, 0, q)));
    vec3 n = normalize(rot * gs);
    vec3 v_alb = alb;
    float e = 0.004, h0 = height(v_obj);
    vec3 grad = vec3(height(v_obj + vec3(e, 0, 0)) - h0, height(v_obj + vec3(0, e, 0)) - h0, height(v_obj + vec3(0, 0, e)) - h0) / e;
    vec3 gw = rot * grad;
    n = normalize(n - 0.035 * (gw - dot(gw, n) * n));
    // the colour: big light and dark blotches, the pits dark, their rims a little lighter, grit
    float rim, id, rim2, id2;
    float pit = bubbles(v_obj * scale, holes, rim, id), pit2 = bubbles(v_obj * scale * 2.3, holes, rim2, id2);
    float b = smoothstep(0.25, 0.75, fbm(v_obj * 1.1 + 3.1));   // (big blotches, light against dark)
    float tone = mix(1.0, 0.3 + 1.4 * b, blotch) * mix(1.0, 0.22, max(pit, pit2 * 0.8)) * (1.0 + 0.25 * max(rim, rim2))
                 * (0.85 + 0.3 * vnoise(v_obj * 40.0));
    float diff = max(dot(n, key), 0.0);
    float wrap = max(dot(n, normalize(vec3(0.6, -0.35, 0.5))), 0.0);   // a fill from the lower right
    vec3 hv = normalize(key + vec3(0.0, 0.0, 1.0));
    float spec = pow(max(dot(n, hv), 0.0), 30.0) * shine;
    vec3 c = v_alb * tone * (0.08 + 1.9 * diff + 0.35 * wrap) + vec3(spec);
    // glints: frozen metal catching the light, a fleck where a fine cell says so
    float gr, gid;
    bubbles(v_obj * 22.0, 0.0, gr, gid);
    if (gid > 1.0 - glint && diff > 0.25 && pit < 0.3)
        c += vec3(0.9, 0.92, 1.0) * (0.6 + 0.6 * fract(gid * 53.7)) * diff;
    frag = vec4(c, 1.0);
}
"""


def rotation(axis, angle):
    a = np.asarray(axis, float)
    a /= np.linalg.norm(a)
    k = np.array([[0, -a[2], a[1]], [a[2], 0, -a[0]], [-a[1], a[0], 0]])
    return np.eye(3) + math.sin(angle) * k + (1 - math.cos(angle)) * (k @ k)


# each kind's first pose (the stone as it sits before it spins: axis, angle); every one spins about the screen's
# vertical (the user, 2026-10-24: they spin horizontally)
POSES = [((0.4, 1.0, 0.2), 0.7), ((1.0, 0.2, 0.4), 1.9), ((0.2, 0.3, 1.0), 0.4), ((0.5, 0.8, 0.3), 2.6)]
SPIN = (0.0, 1.0, 0.0)


def render_kind(ctx, kind, n):
    """the kind's TUMBLE frames, n x n: linear RGB (premultiplied) and coverage"""
    import moderngl
    planes, ends, caves, bite_planes, bite_ends = solid(kind)
    fs = FS.replace('@CHUNKS@', str(CHUNKS)).replace('@FACES@', str(FACES)).replace('@CAVES@', str(CAVES)) \
        .replace('@BITES@', str(BITES))
    prog = ctx.program(vertex_shader=VS, fragment_shader=fs)
    prog['key'].value = tuple(KEY)
    _n, _ch, _f, _cv, _b, rough, colour, holes, scale, blotch, glint, shine = KINDS[kind]
    for name, v in (('holes', holes), ('scale', scale), ('blotch', blotch), ('glint', glint), ('shine', shine),
                    ('seed', 17.0 * kind + 3.0), ('rough', rough)):
        prog[name].value = v
    prog['alb'].value = tuple(colour)
    pad4 = lambda rows, k: [tuple(r) for r in rows] + [(0.0, 0.0, 0.0, 0.0)] * (k - len(rows))
    prog['planes'].value = pad4([(*nrm, d) for nrm, d in planes], CHUNKS * FACES)
    prog['chunk_end'].value = ends + [0] * (CHUNKS - len(ends))
    prog['nchunk'].value = len(ends)
    prog['caves'].value = pad4([(*c, r) for c, r in caves], CAVES)
    prog['ncave'].value = len(caves)
    prog['bite_planes'].value = pad4([(*nrm, d) for nrm, d in bite_planes], BITES * 6)
    prog['bite_end'].value = bite_ends + [0] * (BITES - len(bite_ends))
    prog['nbite'].value = len(bite_ends)
    quad = np.array([-1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, 1], dtype='f4')
    vao = ctx.vertex_array(prog, [(ctx.buffer(quad.tobytes()), '2f', 'in_xy')])
    color = ctx.texture((n, n), 4, dtype='f4')
    fbo = ctx.framebuffer(color_attachments=[color])
    pose_axis, pose_angle = POSES[kind]
    pose = rotation(pose_axis, pose_angle)
    frames = []
    for f in range(TUMBLE):
        rot = rotation(SPIN, 2 * math.pi * f / TUMBLE) @ pose
        prog['rot'].write(rot.T.astype('f4').tobytes())
        fbo.use()
        fbo.clear(0, 0, 0, 0)
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


RUNS = 4                    # a row's runs at most (the gaps between them clear)


def row_runs(on):
    """a row's runs of opaque pixels (on: bool per column, the box's width): gaps under 3 px filled, the smallest
    filled while there are more than RUNS, each made even (a pixel more on its fuller side, or at the box's edge
    past it); [(x0, x1)]"""
    d = len(on)
    xs = np.nonzero(on)[0]
    if not len(xs):
        return []
    runs = []
    start = prev = int(xs[0])
    for x in xs[1:]:
        x = int(x)
        if x - prev > 3:                # (a gap of 3 px or more stays clear)
            runs.append([start, prev + 1])
            start = x
        prev = x
    runs.append([start, prev + 1])
    while len(runs) > RUNS:
        k = min(range(len(runs) - 1), key=lambda i: runs[i + 1][0] - runs[i][1])
        runs[k][1] = runs[k + 1][1]
        del runs[k + 1]
    out = []
    for i, (x0, x1) in enumerate(runs):
        if (x1 - x0) & 1:
            lo = out[-1][1] if out else -1                                  # (the room each side)
            hi = runs[i + 1][0] if i + 1 < len(runs) else d + 1
            if x1 + 1 < hi:
                x1 += 1
            elif x0 - 1 > lo:
                x0 -= 1
            else:
                x1 += 1                                                     # (touching the next: they join below)
        if out and x0 <= out[-1][1]:
            out[-1][1] = x1
            if (out[-1][1] - out[-1][0]) & 1:
                out[-1][1] += 1
        else:
            out.append([x0, x1])
    return [(x0, x1) for x0, x1 in out]


def big_record(col, a, table):
    """one big sprite's record: u16 rows, u16 the run table's bytes, for each row a count and its runs (u8 left
    + 1, u8 length: even), then the runs' pixels in turn; and how many pixels"""
    d = a.shape[0]
    idx = sky.quantize(np.maximum(col, 6.0), table)
    heads, pix, px = bytearray(), bytearray(), 0
    for y in range(d):
        runs = row_runs(a[y] >= 0.5)
        heads.append(len(runs))
        for x0, x1 in runs:
            row = np.zeros(x1 - x0, np.uint8)
            lo, hi = max(x0, 0), min(x1, d)
            row[lo - x0:hi - x0] = idx[y, lo:hi]
            if x0 < 0:
                row[0] = row[1]
            if x1 > d:
                row[-1] = row[-2]
            heads += bytes([x0 + 1, x1 - x0])
            pix += bytes(row)
            px += x1 - x0
    if len(heads) & 1:
        heads.append(0)
    return struct.pack('<HH', d, len(heads)) + bytes(heads) + bytes(pix), px


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
    for kind in range(len(KINDS)):
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
        print('%-6s %2d frames x %d big sizes: %5.0f KB of runs' % (KINDS[kind][0], TUMBLE, len(sizes), kpx / 1024))
    nk = len(KINDS)
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
