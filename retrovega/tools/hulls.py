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
    ('graft', unit, box, place)  copy the triangles of another Vega Strike mesh whose centres lie
                                 in box (x0 x1 y0 y1 z0 z1, fractions of that mesh's bounding box)
                                 and set them at place (scale, x y z as fractions of this hull's
                                 bounding box, from its centre), with their own maps; 'mirror' in
                                 place's last slot adds the mirror image on the other side

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
    # Cleomenes: a heavy assault ship.  Areus thickened, its wings swept
    # back, with a Nicander hull grafted onto each side as an outboard pod.
    'CLEOMEN': ('Areus/areus.bfxm', [
        ('scale', 1.15, 1.25, 1.2),
        ('sweep', 0.3, -0.25),
        ('graft', 'Nicander/nicander.bfxm', (-1, 1, -1, 1, -1, 1), (0.42, 0.62, -0.15, -0.15, 'mirror')),
    ]),
    # Archidamus: the aces' superiority fighter.  Ariston with forward-swept,
    # slightly raised wing tips and a longer nose.
    'ARCHIDM': ('Ariston/ariston.bfxm', [
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


def _graft(parts, assets, op):
    _kind, unit, box, place = op
    src = bfxm.parts(os.path.join(assets, 'units', 'vessels', unit))
    slo, shi = _bbox(src)
    lo, hi = _bbox(parts)
    centre, size = (lo + hi) / 2, hi - lo
    b = np.array(box, dtype=float).reshape(3, 2)
    blo = slo + (b[:, 0] + 1) / 2 * (shi - slo)
    bhi = slo + (b[:, 1] + 1) / 2 * (shi - slo)
    scale, px, py, pz, mirror = place
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
        g.positions = (m.positions[used] - bc) * scale * float(np.max(size)) / float(np.max(shi - slo)) \
            + centre + np.array([px, py, pz]) * size / 2
        g.normals = m.normals[used]
        g.tri_index = remap[m.tri_index[keep]]
        g.tri_uv = m.tri_uv[keep]
        added.append(g)
        if mirror:
            h = bfxm.Mesh()
            h.textures, h.material, h.unit_dir = g.textures, g.material, g.unit_dir
            h.positions = g.positions * np.array([-1.0, 1.0, 1.0])
            h.normals = g.normals * np.array([-1.0, 1.0, 1.0])
            h.tri_index = g.tri_index[:, ::-1].copy()
            h.tri_uv = g.tri_uv[:, ::-1].copy()
            added.append(h)
    parts.extend(added)


def build(name, assets, lights):
    """(parts, lights, base unit) of the recipe NAME; lights are the base
    ship's exhaust points (x, y, z, size), moved with the hull."""
    base, ops = RECIPES[name]
    parts = bfxm.parts(os.path.join(assets, 'units', 'vessels', base))
    pts = np.array([l[:3] for l in lights], dtype=float).reshape(-1, 3)
    for op in ops:
        if op[0] == 'graft':
            _graft(parts, assets, op)
            continue
        lo, hi = _bbox(parts)
        f = _point_ops(op, lo, hi)
        _deform(parts, f)
        if len(pts):
            pts = f(pts)
    return parts, [(tuple(p), l[3]) for p, l in zip(pts, lights)], os.path.dirname(base)



def main():
    name = sys.argv[1]
    parts, lights, base = build(name, os.path.expanduser('~/dev/vegastrike-assets'), [])
    lo, hi = _bbox(parts)
    print('%s from %s: %d parts, %d triangles, extent %s .. %s' % (
        name, base, len(parts), sum(len(m.tri_index) for m in parts), lo.round(2), hi.round(2)))


if __name__ == '__main__':
    main()
