#!/usr/bin/env python3
"""Read Vega Strike .bfxm meshes into triangle lists.

The layout follows Vega Strike's own converter (engine/objconv/mesher/
from_BFXM.cpp): a superheader, then records of meshes; each mesh has a
header (scale, material), a block of arbitrary-length attributes (detail
texture, textures, logos, LODs, animations), then geometry: vertices
(x y z, normal i j k, s t), and lines, triangles, quads, line strips,
triangle strips, triangle fans and quad strips whose corners carry their
own s t.  Only the first record (the full-detail mesh) is wanted here;
LOD and shield meshes are other records or files.

    bfxm.py file.bfxm      print a summary
"""
import struct
import sys

import numpy as np


class Mesh(object):
    """One mesh: positions (V,3), normals (V,3), triangles as corner arrays.

    tri_index (T,3) indexes positions/normals; tri_uv (T,3,2) are the
    corners' texture coordinates; textures maps the texture index (0 the
    diffuse map, 1 specular, 2 damage, 3 glow...) to its file name."""

    def __init__(self):
        self.scale = 1.0
        self.positions = None
        self.normals = None
        self.tri_index = []
        self.tri_uv = []
        self.textures = {}
        self.material = {}


def _words(data):
    return memoryview(data).cast('I'), memoryview(data).cast('f')


def _string(ints, at, length):
    raw = struct.pack('<%dI' % ((length + 3) // 4), *ints[at:at + (length + 3) // 4])
    return raw[:length].split(b'\0')[0].decode('latin-1')


def read(path, record=0):
    """The meshes of one record (a record's meshes are its levels of detail, finest first)."""
    recs = records(path)
    return recs[record] if record < len(recs) else []


def parts(path):
    """The finest mesh of every record that has a real texture: the ship's whole hull.

    A record whose texture is an animation (`.ani`, type 1) is a running light."""
    out = []
    for rec in records(path):
        if rec and any(t == 2 for ts in rec[0].textures.values() for t, _ in ts):
            out.append(rec[0])
    return out


def records(path):
    """Every record's meshes."""
    data = open(path, 'rb').read()
    if data[:4] != b'BFXM':
        raise ValueError('%s: not a BFXM file' % path)
    data = data[:len(data) // 4 * 4]
    I, F = _words(data)
    w = 3
    superlen = I[w]
    nvf, npf, nrf, _nraf, nrec = I[w + 1], I[w + 2], I[w + 3], I[w + 4], I[w + 5]
    nadf = I[w + 6]
    w = superlen // 4
    recs = []
    for rec in range(nrec):
        meshes = []
        rbegin = w
        rheader, rlength, nmesh = I[w], I[w + 1], I[w + 2]
        w = rbegin + rheader // 4
        for _ in range(nmesh):
            m = Mesh()
            mbegin = w
            mheader, mlength = I[w], I[w + 1]
            m.scale = F[w + 2]
            m.material = {
                'power': F[w + 9], 'ambient': tuple(F[w + 10:w + 14]), 'diffuse': tuple(F[w + 14:w + 18]),
                'emissive': tuple(F[w + 18:w + 22]), 'specular': tuple(F[w + 22:w + 26]),
                'blend': (I[w + 7], I[w + 8]), 'cullface': I[w + 26], 'lighting': I[w + 27],
            }
            w = mbegin + mheader // 4
            vsa = w
            vsalen = I[w]; w += 1
            dlen = I[w]; w += 1 + (dlen + 3) // 4            # detail texture name
            ndp = I[w]; w += 1 + 3 * ndp                    # detail planes
            ntex = I[w]; w += 1
            for _t in range(ntex):
                ttype, tindex, tlen = I[w], I[w + 1], I[w + 2]
                w += 3
                m.textures.setdefault(tindex, []).append((ttype, _string(I, w, tlen)))
                w += (tlen + 3) // 4
            w = vsa + vsalen // 4                           # skip logos, LODs, animations
            nv = I[w]; w += 1
            verts = np.frombuffer(data, dtype='<f4', count=nv * nvf, offset=w * 4).reshape(nv, nvf)
            w += nv * nvf
            m.positions = verts[:, 0:3].astype(np.float64) * m.scale
            # the stored normals point inward (opposite the outward winding
            # on every ship checked); Vega Strike flips them, and so do we
            m.normals = -verts[:, 3:6].astype(np.float64)
            tris, uvs = [], []

            def corners(n):
                nonlocal w
                out = []
                for _c in range(n):
                    out.append((I[w], F[w + 1], F[w + 2]))
                    w += nrf
                return out

            def emit(a, b, c):
                tris.append((a[0], b[0], c[0]))
                uvs.append(((a[1], a[2]), (b[1], b[2]), (c[1], c[2])))

            nl = I[w]; w += 1
            for _l in range(nl):
                w += npf; corners(2)
            nt = I[w]; w += 1
            for _l in range(nt):
                w += npf; emit(*corners(3))
            nq = I[w]; w += 1
            for _l in range(nq):
                w += npf; a, b, c, d = corners(4); emit(a, b, c); emit(a, c, d)
            for kind in ('linestrip', 'tristrip', 'trifan', 'quadstrip'):
                ns = I[w]; w += 1
                for _s in range(ns):
                    ne = I[w]; w += 1 + npf
                    cs = corners(ne)
                    if kind == 'tristrip':
                        for k in range(ne - 2):
                            if k % 2 == 0:
                                emit(cs[k], cs[k + 1], cs[k + 2])
                            else:
                                emit(cs[k + 1], cs[k], cs[k + 2])
                    elif kind == 'trifan':
                        for k in range(1, ne - 1):
                            emit(cs[0], cs[k], cs[k + 1])
                    elif kind == 'quadstrip':
                        for k in range(0, ne - 2, 2):
                            emit(cs[k], cs[k + 1], cs[k + 3]); emit(cs[k], cs[k + 3], cs[k + 2])
            m.tri_index = np.array(tris, dtype=np.int64).reshape(-1, 3)
            m.tri_uv = np.array(uvs, dtype=np.float64).reshape(-1, 3, 2)
            meshes.append(m)
            w = mbegin + mlength // 4
        recs.append(meshes)
        w = rbegin + rlength // 4
    return recs


def main():
    for m in parts(sys.argv[1]):
        lo, hi = m.positions.min(0), m.positions.max(0)
        print('verts %d tris %d scale %.3f extent %s..%s textures %s' % (
            len(m.positions), len(m.tri_index), m.scale, np.round(lo, 2), np.round(hi, 2), m.textures))


if __name__ == '__main__':
    main()
