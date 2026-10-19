#!/usr/bin/env python3
"""New hulls built from Vega Strike meshes by recipe (data/ships.csv mesh 'hull:NAME').

A recipe starts from a base ship (its mesh, maps and exhaust points) and
applies operations in order:

    ('scale', sx, sy, sz)        stretch the whole hull
    ('clip', x0, f)              beyond x0 of the half-span, the span shrinks by f (stubby wings)
    ('sweep', x0, dz)            beyond x0 of the half-span, the wing bends forward by dz of the
                                 length at the tip (negative: back), growing as a power 1.5
    ('lift', x0, dy)             beyond x0 of the half-span, the wing rises by dy of the height
                                 at the tip (gull or anhedral wings)
    ('cut', box)                 remove the hull's triangles whose centres lie in box (fractions
                                 of the reference box below, -1..1)
    ('graft', unit, box, place)  copy the triangles of a Vega Strike mesh (this hull's own base, or
                                 another) whose centres lie in box (x0 x1 y0 y1 z0 z1, fractions
                                 of that mesh's bounding box, -1..1) and set them at place: (scale,
                                 x, y, z, mirror, roll) with x y z fractions of this hull's
                                 half-extent from its centre, mirror 'x' or 'y' (also add the mirror
                                 image across that axis) or None, roll in degrees about the nose
                                 axis, turning the piece about its own box centre first

Placement and cuts are measured against the reference box: the base hull's
bounding box, carried through the deformations but not changed by cuts or
grafts.  A recipe marked asymmetric gets all its headings rendered (62
views) instead of mirroring half of them (37).

Normals follow each deformation through its Jacobian, so the lighting stays
right on bent and stretched panels.

    hulls.py NAME     print the recipe's parts and extents
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bfxm  # noqa: E402

RECIPES = {
    # Agis: a light swarm fighter.  Nicander cut short and stubby, its wings
    # swept sharply forward and lifted into a gull W.
    'AGIS': ('Nicander/nicander.bfxm', [
        ('scale', 1.1, 1.2, 0.62),
        ('sweep', 0.3, 0.30),
        ('lift', 0.35, 0.35),
    ]),
    # Cleomenes: a heavy assault ship.  Areus thickened, with a copy of its
    # own wing set upright above and below: a cross seen from the front,
    # Areus still at a glance.
    'CLEOMEN': ('Areus/areus.bfxm', [
        ('scale', 1.1, 1.15, 1.15),
        ('graft', 'Areus/areus.bfxm', (0.3, 1, -1, 1, -1, 0.6), (0.85, 0.0, 0.62, -0.1, 'y', 90)),
    ]),
    # Archidamus: the aces' fighter, one of a kind.  Ariston's wings come
    # off; its big rear wing goes back on the right only, half again as
    # large, and two copies of its small front wing go on the left, one
    # angled 30 degrees up, one 30 degrees down.
    'ARCHIDM': ('Ariston/ariston.bfxm', [
        ('cut', (0.28, 1, -1, 1, -1, 1)),
        ('cut', (-1, -0.28, -1, 1, -1, 1)),
        ('graft', 'Ariston/ariston.bfxm', (0.28, 1, -1, 1, -1, 0.15), (1.6, 0.86, 0.0, -0.43, None)),
        ('graft', 'Ariston/ariston.bfxm', (0.28, 1, -1, 1, 0.2, 1), (0.8, -0.5, 0.5, 0.5, None, 150)),
        ('graft', 'Ariston/ariston.bfxm', (0.28, 1, -1, 1, 0.2, 1), (0.8, -0.5, -0.5, 0.35, None, 210)),
    ], {'asym': True}),
    # The first Archidamus (forward-swept, raised wing tips), kept as an
    # alternative.
    'ARCHIDM_CLAW': ('Ariston/ariston.bfxm', [
        ('scale', 1.05, 1.0, 1.12),
        ('sweep', 0.5, 0.24),
        ('lift', 0.6, 0.25),
    ]),
}


def _bbox(parts):
    allp = np.concatenate([m.positions for m in parts])
    return allp.min(0), allp.max(0)


def _deform(parts, f):
    """Move every vertex by f and carry the normals through f's Jacobian."""
    lo, hi = _bbox(parts)
    eps = 1e-3 * float(np.max(hi - lo))
    for m in parts:
        p = m.positions
        cols = []
        for i in range(3):
            d = np.zeros(3)
            d[i] = eps
            cols.append((f(p + d) - f(p - d)) / (2 * eps))
        jac = np.stack(cols, axis=2)                           # (V,3,3): J[:, :, i] = df/dx_i
        n = np.linalg.solve(np.transpose(jac, (0, 2, 1)), m.normals[..., None])[..., 0]
        m.normals = n / np.maximum(np.linalg.norm(n, axis=1), 1e-9)[:, None]
        m.positions = f(p)


def _point_ops(op, lo, hi):
    """The vertex function of one operation, given the hull's bounding box."""
    hs = float(max(abs(lo[0]), abs(hi[0])))
    length, height = float(hi[2] - lo[2]), float(hi[1] - lo[1])
    kind = op[0]
    if kind == 'scale':
        s = np.array(op[1:4], dtype=float)
        return lambda p: p * s
    if kind == 'clip':
        x0, k = op[1] * hs, op[2]

        def f(p):
            q = p.copy()
            ax = np.abs(p[:, 0])
            out = ax > x0
            q[out, 0] = np.sign(p[out, 0]) * (x0 + (ax[out] - x0) * k)
            return q
        return f
    if kind in ('sweep', 'lift'):
        x0, amount = op[1] * hs, op[2]
        axis, scale = (2, length) if kind == 'sweep' else (1, height)

        def f(p):
            q = p.copy()
            t = np.clip((np.abs(p[:, 0]) - x0) / max(hs - x0, 1e-9), 0, None)
            q[:, axis] = p[:, axis] + amount * scale * t ** 1.5
            return q
        return f
    raise ValueError('unknown operation %r' % (kind,))


def _cut(parts, ref, box):
    lo, hi = ref
    b = np.array(box, dtype=float).reshape(3, 2)
    blo, bhi = lo + (b[:, 0] + 1) / 2 * (hi - lo), lo + (b[:, 1] + 1) / 2 * (hi - lo)
    for m in parts:
        c = m.positions[m.tri_index].mean(1)
        drop = np.all((c >= blo) & (c <= bhi), axis=1)
        m.tri_index, m.tri_uv = m.tri_index[~drop], m.tri_uv[~drop]


def _graft(parts, assets, op, ref):
    _kind, unit, box, place = op
    src = bfxm.parts(os.path.join(assets, 'units', 'vessels', unit))
    slo, shi = _bbox(src)
    lo, hi = ref
    centre, size = (lo + hi) / 2, hi - lo
    b = np.array(box, dtype=float).reshape(3, 2)
    blo = slo + (b[:, 0] + 1) / 2 * (shi - slo)
    bhi = slo + (b[:, 1] + 1) / 2 * (shi - slo)
    scale, px, py, pz, mirror = place[:5]
    roll = np.radians(place[5]) if len(place) > 5 else 0.0
    rz = np.array([[np.cos(roll), -np.sin(roll), 0.0], [np.sin(roll), np.cos(roll), 0.0], [0.0, 0.0, 1.0]])
    added = []
    for m in src:
        c = m.positions[m.tri_index].mean(1)
        keep = np.all((c >= blo) & (c <= bhi), axis=1)
        if not keep.any():
            continue
        used = np.unique(m.tri_index[keep])
        remap = -np.ones(len(m.positions), dtype=np.int64)
        remap[used] = np.arange(len(used))
        g = bfxm.Mesh()
        g.textures, g.material, g.unit_dir = m.textures, m.material, m.unit_dir
        bc = (blo + bhi) / 2
        g.positions = ((m.positions[used] - bc) @ rz.T) * scale * float(np.max(size)) / float(np.max(shi - slo)) \
            + centre + np.array([px, py, pz]) * size / 2
        g.normals = m.normals[used] @ rz.T
        g.tri_index = remap[m.tri_index[keep]]
        g.tri_uv = m.tri_uv[keep]
        added.append(g)
        if mirror:
            flip = np.array([-1.0, 1.0, 1.0]) if mirror in ('x', 'mirror') else np.array([1.0, -1.0, 1.0])
            h = bfxm.Mesh()
            h.textures, h.material, h.unit_dir = g.textures, g.material, g.unit_dir
            h.positions = (g.positions - centre) * flip + centre
            h.normals = g.normals * flip
            h.tri_index = g.tri_index[:, ::-1].copy()
            h.tri_uv = g.tri_uv[:, ::-1].copy()
            added.append(h)
    parts.extend(added)


def asymmetric(name):
    return len(RECIPES[name]) > 2 and RECIPES[name][2].get('asym', False)


def build(name, assets, lights):
    """(parts, lights, base unit) of the recipe NAME; lights are the base
    ship's exhaust points (x, y, z, size), moved with the hull (a light inside
    a cut is dropped)."""
    base, ops = RECIPES[name][:2]
    parts = bfxm.parts(os.path.join(assets, 'units', 'vessels', base))
    ref = _bbox(parts)
    pts = np.array([l[:3] for l in lights], dtype=float).reshape(-1, 3)
    sizes = [l[3] for l in lights]
    for op in ops:
        if op[0] == 'graft':
            _graft(parts, assets, op, ref)
            continue
        if op[0] == 'cut':
            _cut(parts, ref, op[1])
            lo, hi = ref
            b = np.array(op[1], dtype=float).reshape(3, 2)
            blo, bhi = lo + (b[:, 0] + 1) / 2 * (hi - lo), lo + (b[:, 1] + 1) / 2 * (hi - lo)
            keep = ~np.all((pts >= blo) & (pts <= bhi), axis=1) if len(pts) else np.zeros(0, bool)
            pts, sizes = pts[keep], [s for s, k in zip(sizes, keep) if k]
            continue
        f = _point_ops(op, *_bbox(parts))
        _deform(parts, f)
        corners = np.array([[x, y, z] for x in (ref[0][0], ref[1][0]) for y in (ref[0][1], ref[1][1])
                            for z in (ref[0][2], ref[1][2])])
        moved = f(corners)
        ref = (moved.min(0), moved.max(0))
        if len(pts):
            pts = f(pts)
    return parts, [(tuple(p), s) for p, s in zip(pts, sizes)], os.path.dirname(base)



def main():
    name = sys.argv[1]
    parts, lights, base = build(name, os.path.expanduser('~/dev/vegastrike-assets'), [])
    lo, hi = _bbox(parts)
    print('%s from %s: %d parts, %d triangles, extent %s .. %s' % (
        name, base, len(parts), sum(len(m.tri_index) for m in parts), lo.round(2), hi.round(2)))


if __name__ == '__main__':
    main()
