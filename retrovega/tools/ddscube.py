#!/usr/bin/env python3
"""Read a DXT1 DDS cube map (Vega Strike's textures/backgrounds/*.cube).

    faces(path) -> six HxWx3 uint8 arrays, +X -X +Y -Y +Z -Z (OpenGL's order)

Only the top level of each face is decoded; the renderer builds its own
mipmaps (the uber shader samples the environment blurred by gloss).

    ddscube.py file.cube [out.png]   print each face's mean colour, or save a strip
"""
import struct
import sys

import numpy as np


def _dxt1(data, w, h):
    blocks = np.frombuffer(data, dtype=np.uint8).reshape(-1, 8)
    c0 = blocks[:, 0].astype(np.uint32) | (blocks[:, 1].astype(np.uint32) << 8)
    c1 = blocks[:, 2].astype(np.uint32) | (blocks[:, 3].astype(np.uint32) << 8)
    bits = (blocks[:, 4].astype(np.uint32) | (blocks[:, 5].astype(np.uint32) << 8)
            | (blocks[:, 6].astype(np.uint32) << 16) | (blocks[:, 7].astype(np.uint32) << 24))

    def rgb(c):
        return np.stack([((c >> 11) & 31) * 255 / 31, ((c >> 5) & 63) * 255 / 63, (c & 31) * 255 / 31], -1)

    a, b = rgb(c0), rgb(c1)
    four = (c0 > c1)[:, None]
    pal = np.stack([a, b,
                    np.where(four, (2 * a + b) / 3, (a + b) / 2),
                    np.where(four, (a + 2 * b) / 3, 0.0)], 1)
    idx = (bits[:, None] >> (2 * np.arange(16, dtype=np.uint32))) & 3
    px = np.take_along_axis(pal, idx[..., None].astype(np.int64), 1)       # (N,16,3)
    bw, bh = w // 4, h // 4
    return px.reshape(bh, bw, 4, 4, 3).transpose(0, 2, 1, 3, 4).reshape(h, w, 3).astype(np.uint8)


def faces(path):
    d = open(path, 'rb').read()
    if d[:4] != b'DDS ':
        raise ValueError('%s: not a DDS file' % path)
    h, w = struct.unpack_from('<II', d, 12)
    mips = max(1, struct.unpack_from('<I', d, 28)[0])
    if d[84:88] != b'DXT1':
        raise ValueError('%s: %r, only DXT1 is read' % (path, d[84:88]))
    face_bytes = sum(max(1, (w >> i) // 4) * max(1, (h >> i) // 4) * 8 for i in range(mips))
    out, off = [], 128
    for _f in range(6):
        out.append(_dxt1(d[off:off + (w // 4) * (h // 4) * 8], w, h))
        off += face_bytes
    return out


def main():
    fs = faces(sys.argv[1])
    for name, f in zip(('+X', '-X', '+Y', '-Y', '+Z', '-Z'), fs):
        print(name, f.reshape(-1, 3).mean(0).round(1))
    if len(sys.argv) > 2:
        from PIL import Image
        Image.fromarray(np.hstack([f[::4, ::4] for f in fs])).save(sys.argv[2])


if __name__ == '__main__':
    main()
