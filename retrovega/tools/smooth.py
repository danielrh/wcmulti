#!/usr/bin/env python3
"""Smoothing a low-polygon mesh: bevelled creases, Loop subdivision that
keeps them, normals that keep them too, and the creases as polylines (for
light strips).

    smooth(mesh, levels, crease_deg, bevel=0, sharp=inf) -> new Mesh, crease polylines

**Bevel** (bevel > 0, a fraction of the mesh's size): every crease edge is
cut away by a narrow face of that width, as a chamfer on a machined edge,
before subdividing.  Each vertex on a crease splits into one vertex per
wedge of faces between its creases, moved into the wedge (on the bisector
of its two crease edges, at the width from both); the gap along each
crease becomes the chamfer, and where three or more creases meet the
hole between the wedges is filled.  The chamfer's two long edges are the
creases from then on, `sharp` levels sharp (semi-sharp creases, DeRose et
al. 1998): sharp for ever keeps a flat chamfer, 1 rounds it into a fillet.

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
import math

import numpy as np
from scipy.spatial import cKDTree

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
    """One level.  crease maps edges to their sharpness: an edge counts as
    a crease while it is above 0, and its halves are one level less sharp."""
    edges = _edges(t)
    nv = len(p)
    index = {}
    pts = []
    for k, (e, fs) in enumerate(edges.items()):
        a, b = e
        if crease.get(e, 0) > 0 or len(fs) != 2:
            pts.append((p[a] + p[b]) / 2)
        else:
            pts.append(0.375 * (p[a] + p[b]) + 0.125 * (p[fs[0][1]] + p[fs[1][1]]))
        index[e] = nv + k
    nbrs = [set() for _ in range(nv)]
    hard = [[] for _ in range(nv)]
    for a, b in edges:
        nbrs[a].add(b)
        nbrs[b].add(a)
        if crease.get((a, b), 0) > 0:
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
    ncrease = {}
    for (a, b), sharp in crease.items():
        if sharp <= 0:
            continue
        m = index[(a, b)]
        ncrease[(min(a, m), max(a, m))] = sharp - 1
        ncrease[(min(m, b), max(m, b))] = sharp - 1
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


def _fan(t, v, faces):
    """The faces round vertex v in order (each the next across its edge v -> next)."""
    nxt, by_prev = {}, {}
    for f in faces:
        k = list(t[f]).index(v)
        nxt[f] = t[f][(k + 1) % 3]
        by_prev[t[f][(k + 2) % 3]] = f
    order = [faces[0]]
    while True:
        f = by_prev.get(nxt[order[-1]])
        if f is None or f == order[0]:
            break
        order.append(f)
    return order, nxt


def _corner_angle(p, tri, v):
    k = list(tri).index(v)
    a, b = p[tri[(k + 1) % 3]] - p[v], p[tri[(k + 2) % 3]] - p[v]
    return math.acos(max(-1.0, min(1.0, float(np.dot(a, b) / (np.linalg.norm(a) * np.linalg.norm(b) + 1e-12)))))


def bevel(p, t, uv, crease, width):
    """Chamfer the crease edges: (positions, triangles, uvs, the chamfers' long edges)."""
    around = [[] for _ in range(len(p))]
    for f, tri in enumerate(t):
        for v in tri:
            around[v].append(f)
    newp = []
    corner = {}                    # (face, vertex) -> new vertex
    for v in range(len(p)):
        order, nxt = _fan(t, v, around[v])
        cuts = [i for i, f in enumerate(order) if (min(v, nxt[f]), max(v, nxt[f])) in crease]
        if len(cuts) < 2:          # inside a smooth patch, or where a crease ends: stays put
            for f in order:
                corner[(f, v)] = len(newp)
            newp.append(p[v])
            continue
        # wedges: the faces after each crease edge up to the next
        for j, c in enumerate(cuts):
            end = cuts[(j + 1) % len(cuts)]
            wedge = []
            i = (c + 1) % len(order)
            while True:
                wedge.append(order[i])
                if i == end:
                    break
                i = (i + 1) % len(order)
            first, last = wedge[0], wedge[-1]
            k = list(t[first]).index(v)
            d1 = p[t[first][(k + 2) % 3]] - p[v]          # the crease edge the wedge starts from
            d2 = p[nxt[last]] - p[v]                       # the one it ends at
            near = min(np.linalg.norm(d1), np.linalg.norm(d2))
            d1, d2 = d1 / np.linalg.norm(d1), d2 / np.linalg.norm(d2)
            theta = sum(_corner_angle(p, t[f], v) for f in wedge)
            inward = np.mean([p[t[f]].mean(axis=0) for f in wedge], axis=0) - p[v]
            d = d1 + d2
            if np.linalg.norm(d) < 1e-3:                   # a straight crease: square off it
                d = inward - np.dot(inward, d1) * d1
            d /= np.linalg.norm(d) + 1e-12
            if np.dot(d, inward) < 0:
                d = -d
            dist = min(width / max(math.sin(theta / 2), 0.25), 0.4 * near)
            for f in wedge:
                corner[(f, v)] = len(newp)
            newp.append(p[v] + d * dist)
    nt = [[corner[(f, v)] for v in tri] for f, tri in enumerate(t)]
    nuv = [list(u) for u in uv]
    long_edges = set()
    hole = {}                      # vertex -> the hole's directed edges round it
    for (a, b), fs in _edges(t).items():
        if (a, b) not in crease or len(fs) != 2:
            continue
        f, g = fs[0][0], fs[1][0]
        if list(t[f]).index(b) != (list(t[f]).index(a) + 1) % 3:
            f, g = g, f                                    # f holds a -> b, g holds b -> a
        af, bf, ag, bg = corner[(f, a)], corner[(f, b)], corner[(g, a)], corner[(g, b)]
        ua = uv[f][list(t[f]).index(a)]
        ub = uv[f][list(t[f]).index(b)]
        for tri, tuv in (((af, ag, bg), (ua, ua, ub)), ((af, bg, bf), (ua, ub, ub))):
            if len(set(tri)) == 3:
                nt.append(list(tri))
                nuv.append(list(tuv))
        long_edges.add((min(af, bf), max(af, bf)))
        long_edges.add((min(ag, bg), max(ag, bg)))
        if af != ag:
            hole.setdefault(a, []).append((ag, af))        # the chamfer runs af -> ag at a
        if bf != bg:
            hole.setdefault(b, []).append((bf, bg))        # and bg -> bf at b
    for v, edges in hole.items():
        step = dict(edges)
        if len(step) < 3:
            continue
        start = next(iter(step))
        ring = [start]
        while step.get(ring[-1]) not in (None, start) and len(ring) <= len(step):
            ring.append(step[ring[-1]])
        u0 = uv[around[v][0]][list(t[around[v][0]]).index(v)]
        for i in range(1, len(ring) - 1):
            nt.append([ring[0], ring[i], ring[i + 1]])
            nuv.append([u0, u0, u0])
    return np.array(newp), np.array(nt), np.array(nuv, dtype=np.float64), long_edges


def project(points, p, t, k=12):
    """Each point moved to the nearest point of the surface (p, t)."""
    a, b, c = p[t[:, 0]], p[t[:, 1]], p[t[:, 2]]
    tree = cKDTree((a + b + c) / 3)
    _d, cand = tree.query(points, k=min(k, len(t)))
    best = points.copy()
    bestd = np.full(len(points), np.inf)
    for j in range(cand.shape[1]):
        f = cand[:, j]
        q = _closest_on_triangle(points, a[f], b[f], c[f])
        d = np.linalg.norm(q - points, axis=1)
        better = d < bestd
        best[better], bestd[better] = q[better], d[better]
    return best


def _closest_on_triangle(x, a, b, c):
    """Closest points on triangles (a, b, c) to points x, all (N, 3) (Ericson)."""
    ab, ac, ax = b - a, c - a, x - a
    d1, d2 = (ab * ax).sum(1), (ac * ax).sum(1)
    bx = x - b
    d3, d4 = (ab * bx).sum(1), (ac * bx).sum(1)
    cx = x - c
    d5, d6 = (ab * cx).sum(1), (ac * cx).sum(1)
    va = d3 * d6 - d5 * d4
    vb = d5 * d2 - d1 * d6
    vc = d1 * d4 - d3 * d2
    den = np.where(np.abs(va + vb + vc) < 1e-12, 1e-12, va + vb + vc)
    out = a + ab * (vb / den)[:, None] + ac * (vc / den)[:, None]        # inside the face
    with np.errstate(divide='ignore', invalid='ignore'):
        e_ab = a + ab * np.clip(d1 / (d1 - d3), 0, 1)[:, None]
        e_ac = a + ac * np.clip(d2 / (d2 - d6), 0, 1)[:, None]
        e_bc = b + (c - b) * np.clip((d4 - d3) / ((d4 - d3) + (d5 - d6)), 0, 1)[:, None]
    for cond, val in (((vc <= 0) & (d1 >= 0) & (d3 <= 0), e_ab), ((vb <= 0) & (d2 >= 0) & (d6 <= 0), e_ac),
                      ((va <= 0) & ((d4 - d3) >= 0) & ((d5 - d6) >= 0), e_bc),
                      ((d1 <= 0) & (d2 <= 0), a), ((d3 >= 0) & (d4 <= d3), b), ((d6 >= 0) & (d5 <= d6), c)):
        out = np.where(cond[:, None], np.nan_to_num(val), out)
    return out


def smooth(mesh, levels, crease_deg, bevel_width=0.0, sharp=math.inf):
    """A smoothed copy of mesh (one vertex per corner) and its original
    creases as polylines, laid on the smoothed surface."""
    p = mesh.positions.copy()
    t = np.asarray(mesh.tri_index).reshape(-1, 3)
    uv = np.asarray(mesh.tri_uv, dtype=np.float64).reshape(-1, 3, 2)
    hard = creases(p, t, crease_deg)
    lines = polylines(p, hard)
    if bevel_width > 0:
        size = float(np.max(p.max(0) - p.min(0)))
        p, t, uv, hard = bevel(p, t, uv, hard, bevel_width * size)
        crease = {e: sharp for e in hard}
    else:
        crease = {e: math.inf for e in hard}
    for _ in range(levels):
        p, t, uv, crease = loop_step(p, t, uv, crease)
    crease = {e for e, sharpness in crease.items() if sharpness > 0}
    out = bfxm.Mesh()
    out.__dict__.update({k: v for k, v in mesh.__dict__.items()
                         if k not in ('positions', 'normals', 'tri_index', 'tri_uv')})
    out.positions = p[t].reshape(-1, 3)
    out.normals = corner_normals(p, t, crease).reshape(-1, 3)
    out.tri_index = np.arange(len(t) * 3).reshape(-1, 3)
    out.tri_uv = uv
    if bevel_width > 0 or levels:
        # the creases' lines, sampled finely, on the new surface (a fillet's middle)
        step = float(np.max(p.max(0) - p.min(0))) / 400
        laid = []
        for line in lines:
            pts = [line[0]]
            for x, y in zip(line[:-1], line[1:]):
                n = max(1, int(np.linalg.norm(y - x) / step))
                pts += [x + (y - x) * (i / n) for i in range(1, n + 1)]
            laid.append(project(np.array(pts), p, t))
        lines = laid
    return out, lines
