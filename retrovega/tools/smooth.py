#!/usr/bin/env python3
"""Smoothing a low-polygon mesh: Loop subdivision that keeps its creases,
normals that keep them too, and the creases as polylines (for light strips).

    smooth(mesh, levels, crease_deg) -> new Mesh, crease polylines

An edge is a crease when the faces either side of it meet at more than
crease_deg (a wing's leading edge, a fin's root), and on an open border.
Loop's rules (Hoppe et al. 1994, with Warren's weights):

    edge point     smooth: 3/8 of each end + 1/8 of each opposite corner
                   crease: the midpoint
    vertex point   0 or 1 crease edges: (1 - n b) v + b * its n neighbours,
                       b = 3/16 for n = 3, else 3/(8n)
                   2 crease edges: 3/4 v + 1/8 of each crease neighbour
                   more: a corner, it stays

so flat panels stay flat and sharp edges stay sharp while the rest rounds
off.  Texture coordinates are interpolated linearly in each triangle.  The
result has one vertex per triangle corner: each corner's normal is the
area-weighted sum of the face normals around it that are joined to its
face across smooth edges.

The meshes' winding must be consistent and outward (every Vega Strike
mesh checked is).
"""
import numpy as np

import bfxm


def _face_normals(p, t):
    return np.cross(p[t[:, 1]] - p[t[:, 0]], p[t[:, 2]] - p[t[:, 0]])


def _edges(t):
    """{(a, b) with a < b: [(face, opposite corner)]}"""
    out = {}
    for f, (a, b, c) in enumerate(t):
        for u, v, w in ((a, b, c), (b, c, a), (c, a, b)):
            out.setdefault((min(u, v), max(u, v)), []).append((f, w))
    return out


def creases(p, t, crease_deg):
    fn = _face_normals(p, t)
    fn /= np.maximum(np.linalg.norm(fn, axis=1), 1e-12)[:, None]
    cos = np.cos(np.radians(crease_deg))
    out = set()
    for e, fs in _edges(t).items():
        if len(fs) != 2 or np.dot(fn[fs[0][0]], fn[fs[1][0]]) < cos:
            out.add(e)
    return out


def loop_step(p, t, uv, crease):
    edges = _edges(t)
    nv = len(p)
    index = {}
    pts = []
    for k, (e, fs) in enumerate(edges.items()):
        a, b = e
        if e in crease or len(fs) != 2:
            pts.append((p[a] + p[b]) / 2)
        else:
            pts.append(0.375 * (p[a] + p[b]) + 0.125 * (p[fs[0][1]] + p[fs[1][1]]))
        index[e] = nv + k
    nbrs = [set() for _ in range(nv)]
    hard = [[] for _ in range(nv)]
    for a, b in edges:
        nbrs[a].add(b)
        nbrs[b].add(a)
        if (a, b) in crease:
            hard[a].append(b)
            hard[b].append(a)
    newp = np.empty((nv + len(pts), 3))
    for v in range(nv):
        if len(hard[v]) > 2:
            newp[v] = p[v]
        elif len(hard[v]) == 2:
            newp[v] = 0.75 * p[v] + 0.125 * (p[hard[v][0]] + p[hard[v][1]])
        else:
            n = len(nbrs[v])
            beta = 3.0 / 16 if n == 3 else 3.0 / (8 * n)
            newp[v] = (1 - n * beta) * p[v] + beta * p[list(nbrs[v])].sum(axis=0)
    newp[nv:] = pts

    def mid(a, b):
        return index[(min(a, b), max(a, b))]
    nt, nuv = [], []
    for (a, b, c), (ua, ub, uc) in zip(t, uv):
        ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
        uab, ubc, uca = (ua + ub) / 2, (ub + uc) / 2, (uc + ua) / 2
        nt += [(a, ab, ca), (ab, b, bc), (ca, bc, c), (ab, bc, ca)]
        nuv += [(ua, uab, uca), (uab, ub, ubc), (uca, ubc, uc), (uab, ubc, uca)]
    ncrease = set()
    for a, b in crease:
        m = index[(a, b)]
        ncrease.add((min(a, m), max(a, m)))
        ncrease.add((min(m, b), max(m, b)))
    return newp, np.array(nt), np.array(nuv), ncrease


def corner_normals(p, t, crease):
    """(F, 3, 3): each corner's normal, smooth across smooth edges only."""
    fn = _face_normals(p, t)
    parent = list(range(len(t) * 3))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    slot = {(f, v): 3 * f + k for f, tri in enumerate(t) for k, v in enumerate(tri)}
    for e, fs in _edges(t).items():
        if e in crease or len(fs) != 2:
            continue
        f1, f2 = fs[0][0], fs[1][0]
        for v in e:
            r1, r2 = find(slot[(f1, v)]), find(slot[(f2, v)])
            if r1 != r2:
                parent[r1] = r2
    sums = np.zeros((len(t) * 3, 3))
    for f in range(len(t)):
        for k in range(3):
            sums[find(3 * f + k)] += fn[f]
    out = np.array([sums[find(i)] for i in range(len(t) * 3)])
    out /= np.maximum(np.linalg.norm(out, axis=1), 1e-12)[:, None]
    return out.reshape(-1, 3, 3)


def polylines(p, crease, turn_deg=50.0):
    """The crease edges chained into polylines, broken at corners (a vertex
    with other than two crease edges) and where the line turns by more than
    turn_deg."""
    adj = {}
    for a, b in crease:
        adj.setdefault(a, []).append(b)
        adj.setdefault(b, []).append(a)
    cos = np.cos(np.radians(turn_deg))

    def through(prev, v):
        """The next vertex after prev -> v, if the line runs on straight enough."""
        if len(adj[v]) != 2:
            return None
        nxt = adj[v][0] if adj[v][1] == prev else adj[v][1]
        d1, d2 = p[v] - p[prev], p[nxt] - p[v]
        if np.dot(d1, d2) < cos * np.linalg.norm(d1) * np.linalg.norm(d2):
            return None
        return nxt
    used = set()
    lines = []
    for a, b in sorted(crease):
        if (a, b) in used:
            continue
        chain = [a, b]
        used.add((a, b))
        for forward in (True, False):
            while True:
                prev, v = (chain[-2], chain[-1]) if forward else (chain[1], chain[0])
                nxt = through(prev, v)
                if nxt is None or (min(v, nxt), max(v, nxt)) in used:
                    break
                used.add((min(v, nxt), max(v, nxt)))
                if forward:
                    chain.append(nxt)
                else:
                    chain.insert(0, nxt)
        lines.append(p[chain])
    return lines


def smooth(mesh, levels, crease_deg):
    """A smoothed copy of mesh (one vertex per corner) and its creases as polylines."""
    p = mesh.positions.copy()
    t = np.asarray(mesh.tri_index).reshape(-1, 3)
    uv = np.asarray(mesh.tri_uv, dtype=np.float64).reshape(-1, 3, 2)
    crease = creases(p, t, crease_deg)
    for _ in range(levels):
        p, t, uv, crease = loop_step(p, t, uv, crease)
    out = bfxm.Mesh()
    out.__dict__.update({k: v for k, v in mesh.__dict__.items()
                         if k not in ('positions', 'normals', 'tri_index', 'tri_uv')})
    out.positions = p[t].reshape(-1, 3)
    out.normals = corner_normals(p, t, crease).reshape(-1, 3)
    out.tri_index = np.arange(len(t) * 3).reshape(-1, 3)
    out.tri_uv = uv
    return out, polylines(p, crease)
