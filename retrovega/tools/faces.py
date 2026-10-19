#!/usr/bin/env python3
"""Comm faces, painted from scratch: data/people.csv -> talking portraits.

    faces.py [KEY ...] [--out build/people] [--size 256] [--sheet docs/people.png]
             [--gif build/people-talk.gif]

Every face is made here (no third-party art, photographs or fonts), so the
portraits are ours to give away: CC0 1.0 (docs/people.md).  The look is
semi-realistic, a painted portrait rather than a cartoon: the face is a
height field (skull, brow ridge, eye sockets, cheekbones, nose, lips, chin)
lit by one soft key light from the upper left, with light wrapping round
the form, warm shadows (light through skin), occlusion in the creases, a
little sheen, and painted eyes, brows and regulation-cut hair over it.

For each character OUT/KEY/ holds, at --size (256: twice WC1's comm faces):

    base.png          the face at rest: mouth closed, eyes open
    mouth_0..8.png    the mouth box in each of lipsync.py's nine shapes
    eyes_0..2.png     the eye box open, half closed, closed (blinks)
    face.json         where the two boxes sit on base.png

The boxes never overlap: the engine lays a mouth frame and an eye frame
over the base independently.  OUT/KEY.gif is the character saying its line.
"""
import argparse
import csv
import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lipsync  # noqa: E402

N = 768               # drawing canvas
K = N / 512.0         # design units (0..512) to canvas pixels
YY, XX = np.mgrid[0:N, 0:N] / K
LIGHT = np.array([-0.5, -0.56, 0.66]) / np.linalg.norm([-0.5, -0.56, 0.66])
HALF = (LIGHT + np.array([0, 0, 1.0])) / np.linalg.norm(LIGHT + np.array([0, 0, 1.0]))
MOUTH_BOX = (-84, -46, 84, 62)       # around the mouth centre, design units
EYE_BOX = (-104, -48, 104, 30)       # around the eye line's centre
BG = (16, 30, 34)


# --- small tools --------------------------------------------------------------
def rgb(text):
    return tuple(int(v) for v in text.split())


def lin(c):
    return (np.asarray(c, dtype=np.float64) / 255.0) ** 2.2


def to_srgb(c):
    return np.clip(np.clip(c, 0, 1) ** (1 / 2.2) * 255, 0, 255)


def g(x0, y0, sx, sy, a):
    return a * np.exp(-0.5 * (((XX - x0) / sx) ** 2 + ((YY - y0) / sy) ** 2))


def ridge(x0, y0, x1, y1, w, a):
    dx, dy = x1 - x0, y1 - y0
    t = np.clip(((XX - x0) * dx + (YY - y0) * dy) / (dx * dx + dy * dy), 0, 1)
    d = np.hypot(XX - x0 - t * dx, YY - y0 - t * dy)
    return a * np.exp(-0.5 * (d / w) ** 2)


def _mask(fn):
    img = Image.new('L', (N, N), 0)
    fn(ImageDraw.Draw(img))
    return np.asarray(img) > 127


def ellipse(cx, cy, rx, ry):
    return _mask(lambda d: d.ellipse([(cx - rx) * K, (cy - ry) * K, (cx + rx) * K, (cy + ry) * K], fill=255))


def polygon(pts):
    return _mask(lambda d: d.polygon([(x * K, y * K) for x, y in pts], fill=255))


def stroke(pts, w):
    return _mask(lambda d: d.line([(x * K, y * K) for x, y in pts], fill=255, width=max(1, int(w * K)),
                                  joint='curve'))


def soft(m, r=1.0):
    """A bool mask as a feathered alpha (r design units)."""
    return np.clip(ndimage.gaussian_filter(m.astype(float), r * K * 0.6), 0, 1)


def noise(seed, sigma=(1.0, 1.0)):
    r = np.random.default_rng(seed).random((N, N))
    n = ndimage.gaussian_filter(r, (sigma[0] * K, sigma[1] * K))
    return (n - n.mean()) / (n.std() + 1e-9)


def dome(m, r):
    """A rounded height inside a mask: r design units to full height."""
    d = ndimage.distance_transform_edt(m) / K
    t = np.clip(d / r, 0, 1)
    return np.sqrt(t * (2 - t)) * r


def shade(H, base, ambient=0.26, wrap=0.35, spec=0.0, shin=30, warm=None, ao_k=0.03, rim=True):
    """Light a height field: base is linear colour (3,) or (N,N,3)."""
    gy, gx = np.gradient(H, 1.0 / K)
    n = np.dstack([-gx, -gy, np.ones_like(H)])
    n /= np.linalg.norm(n, axis=2, keepdims=True)
    ndl = n @ LIGHT
    w = np.clip((ndl + wrap) / (1 + wrap), 0, 1)
    ao = 1 - np.clip((ndimage.gaussian_filter(H, 9 * K) - H) * ao_k, 0, 0.5)
    col = np.broadcast_to(base, H.shape + (3,)) * (ambient + 0.95 * w)[..., None]
    if warm is not None:
        col = col * (1 + (np.array(warm) - 1) * (1 - w)[..., None] * 0.6)
    col = col * ao[..., None]
    if rim:
        col = col * (0.72 + 0.28 * np.sqrt(np.clip(n[..., 2], 0, 1)))[..., None]
    if spec:
        col = col + (spec * np.clip(n @ HALF, 0, 1) ** shin)[..., None]
    return col


class Canvas(object):
    def __init__(self):
        self.c = np.zeros((N, N, 3))
        self.a = np.zeros((N, N))

    def put(self, alpha, col):
        al = np.clip(alpha, 0, 1)[..., None]
        self.c = self.c * (1 - al) + np.broadcast_to(col, self.c.shape) * al
        self.a = np.maximum(self.a, al[..., 0])

    def image(self):
        out = np.dstack([to_srgb(self.c), self.a * 255]).astype(np.uint8)
        return Image.fromarray(out, 'RGBA')


# --- a face -------------------------------------------------------------------
class Face(object):
    def __init__(self, row):
        self.row = row
        self.f = row['sex'] == 'f'
        self.age = int(row['age'])
        self.skin = lin(rgb(row['skin']))
        self.hair = lin(rgb(row['hair_color']))
        self.iris = lin(rgb(row['eye_color']))
        self.suit, self.accent = lin(rgb(row['outfit_color'])), lin(rgb(row['accent_color']))
        self.extras = set(filter(None, row['extras'].split('|')))
        self.cx, self.cy = 256, 250
        self.rx, self.ry = (98, 136) if self.f else (106, 142)
        self.ey, self.edx, self.ew, self.eh = 252, (42 if self.f else 44), 20, (9.5 if self.f else 8.5)
        if self.age >= 50:
            self.eh -= 1.2
        self.ny, self.my = 304, 342
        self.seed = sum(map(ord, row['key']))
        lipred = lin((176, 84, 84))
        k = 0.42 if self.f else 0.22
        self.lip = self.skin * (1 - k) + lipred * k

    # geometry ---------------------------------------------------------------
    def head_mask(self):
        jaw, pts = self.row['jaw'], []
        for i in range(240):
            t = 2 * math.pi * i / 240
            x, y = math.cos(t), math.sin(t)
            if y > 0:
                if jaw == 'square':
                    x = math.copysign(abs(x) ** 0.78, x)
                elif jaw == 'pointed':
                    x *= 1 - 0.24 * y * y
                else:
                    x *= 1 - 0.1 * y * y
            pts.append((self.cx + self.rx * x, self.cy + self.ry * y))
        return polygon(pts)

    def viseme_shape(self, v):
        """(how far the lips part, how wide the mouth is) in design units / ratio."""
        return {lipsync.REST: (0, 1.0), lipsync.MBP: (0, 0.96), lipsync.FV: (8, 0.95), lipsync.TH: (11, 0.95),
                lipsync.S: (8, 1.08), lipsync.AH: (26, 0.98), lipsync.EH: (15, 1.1), lipsync.OH: (20, 0.78),
                lipsync.OO: (11, 0.6)}[v]

    def face_height(self, head, v):
        cx, ey, edx = self.cx, self.ey, self.edx
        u, w = (XX - cx) / self.rx, (YY - self.cy) / self.ry
        edge = 1 - np.exp(-ndimage.distance_transform_edt(head) / K / 9)
        H = 95 * np.sqrt(np.clip(1 - u * u - w * w, 0.02, 1)) * edge
        H += g(cx, 180, 60, 40, 5) - g(cx - 92, 212, 18, 30, 5) - g(cx + 92, 212, 18, 30, 5)
        for s in (-1, 1):
            ex = cx + s * edx
            H += g(ex, ey - 24, 30, 9, 8) - g(ex, ey, 27, 15, 14) + g(ex, ey + 1, 17, 8, 6)
            H += g(cx + s * 62, 290, 24, 18, 8) - g(cx + s * 30, 322, 6, 20, 3 + (3 if self.age >= 50 else 0))
            H -= g(cx + s * 70, 330, 20, 26, 5)
            H += g(cx + s * 13, self.ny + 2, 8, 7, 9)
            if self.age >= 50:
                H += g(ex, ey + 18, 15, 4, 2.5)
        H += g(cx, ey - 16, 10, 8, 4) + ridge(cx, ey - 2, cx, self.ny - 6, 6.5, 16) + g(cx, self.ny - 2, 11, 10, 20)
        if self.row['nose'] == 'broad':
            H += g(cx, self.ny, 20, 8, 5)
        elif self.row['nose'] == 'long':
            H += ridge(cx, ey - 2, cx, self.ny - 6, 5, 4)
        open_h, wide = self.viseme_shape(v)
        H += g(cx, self.my - 7 - open_h * 0.35, 25 * wide, 6, 5) + g(cx, self.my + 7 + open_h * 0.65, 21 * wide, 6.5, 6)
        H += g(cx, 378 + open_h * 0.3, 22, 14, 8)
        if v == lipsync.OO:
            H += g(cx, self.my, 16, 14, 6)
        return H

    def hairline(self, top, side):
        return YY < top + (side - top) * ((XX - self.cx) / self.rx) ** 2

    # parts ------------------------------------------------------------------
    def body(self, c, head):
        cx = self.cx
        nw = 34 if self.f else 40
        neck = polygon([(cx - nw, 300), (cx + nw, 300), (cx + nw + 4, 440), (cx - nw - 4, 440)])
        Hn = np.sqrt(np.clip(1 - ((XX - cx) / (nw + 4)) ** 2, 0, 1)) * nw * 0.6
        col = shade(Hn, self.skin * 0.95, warm=(1.08, 0.84, 0.74))
        jaw_shadow = soft(ndimage.binary_dilation(head, iterations=int(10 * K)) & ~head, 6)
        col = col * (1 - 0.55 * jaw_shadow)[..., None]
        c.put(soft(neck, 0.8), col)
        body = polygon([(28, 512), (58, 468), (150, 434), (cx - 58, 420), (cx + 58, 420), (362, 434), (454, 468),
                        (484, 512)])
        Hb = dome(body, 40) + noise(self.seed + 3, (14, 6)) * 2.0
        outfit = self.row['outfit']
        base = self.suit
        c.put(soft(body, 0.8), shade(Hb, base, ambient=0.3))
        if outfit == 'flightsuit':
            for s in (-1, 1):
                flap = polygon([(cx + s * 58, 420), (cx + s * 8, 470), (cx + s * 26, 512), (cx + s * 92, 450)])
                c.put(soft(flap, 0.6), shade(dome(flap, 6) + Hb * 0.3, base * 1.15, ambient=0.32))
            c.put(soft(stroke([(cx, 470), (cx, 512)], 2.2), 0.4), base * 0.35)
            patch = ellipse(110, 482, 20, 20)
            c.put(soft(patch, 0.5), shade(dome(patch, 6), self.accent, ambient=0.35))
            tag = polygon([(318, 478), (378, 474), (380, 490), (320, 494)])
            c.put(soft(tag, 0.4), shade(dome(tag, 3), lin((200, 196, 180)), ambient=0.4))
        elif outfit == 'dress':
            collar = polygon([(cx - 54, 402), (cx + 54, 402), (cx + 60, 442), (cx - 60, 442)])
            c.put(soft(collar, 0.6), shade(dome(collar, 8), self.suit * 1.1))
            c.put(soft(stroke([(cx - 54, 404), (cx + 54, 404)], 2.5), 0.4), self.accent)
            for i in range(3):
                pip = ellipse(cx + 26 + i * 9, 426, 3.2, 3.2)
                c.put(soft(pip, 0.3), shade(dome(pip, 2), self.accent, spec=0.5, shin=20, ambient=0.5))
            for s in (-1, 1):
                board = polygon([(cx + s * 118, 444), (cx + s * 188, 466), (cx + s * 184, 480), (cx + s * 116, 456)])
                c.put(soft(board, 0.4), shade(dome(board, 4), self.accent, spec=0.3, ambient=0.4))
        elif outfit == 'overalls':
            shirt = polygon([(cx - 58, 420), (cx + 58, 420), (cx + 30, 474), (cx - 30, 474)])
            c.put(soft(shirt, 0.6), shade(dome(shirt, 10), lin((60, 62, 70))))
            for s in (-1, 1):
                strap = polygon([(cx + s * 68, 432), (cx + s * 92, 436), (cx + s * 72, 512), (cx + s * 48, 512)])
                c.put(soft(strap, 0.5), shade(dome(strap, 5), self.accent, ambient=0.32))
                stud = ellipse(cx + s * 70, 484, 4.5, 4.5)
                c.put(soft(stud, 0.3), shade(dome(stud, 3), lin((200, 200, 196)), spec=0.6, ambient=0.5))
        elif outfit == 'apron':
            c.put(soft(stroke([(cx - 30, 420), (cx, 452), (cx + 30, 420)], 3), 0.5), base * 0.5)
            for s in (-1, 1):
                strap = polygon([(cx + s * 64, 428), (cx + s * 84, 430), (cx + s * 70, 512), (cx + s * 50, 512)])
                c.put(soft(strap, 0.5), shade(dome(strap, 5), self.accent, spec=0.15, shin=12, ambient=0.3))

    def ears(self, c):
        for s in (-1, 1):
            ex = self.cx + s * (self.rx + 2)
            ear = ellipse(ex, 270, 13, 27)
            He = dome(ear, 8) - g(ex + s * 1, 272, 5, 13, 4)
            c.put(soft(ear, 0.7), shade(He, self.skin * np.array([0.82, 0.74, 0.72]), ambient=0.2, wrap=0.2,
                                        warm=(1.08, 0.82, 0.74)))
            if 'studs' in self.extras:
                st = ellipse(ex + s * 2, 293, 2.6, 2.6)
                c.put(soft(st, 0.3), shade(dome(st, 2), lin((210, 200, 180)), spec=0.8, shin=20, ambient=0.5))

    def hair_layer(self, c, front=True):
        st, cx = self.row['hair_style'], self.cx
        top = ellipse(cx, self.cy - 2, self.rx + 5, self.ry + 6)
        if not front:
            if st == 'bun':
                bun = ellipse(cx + 6, 116, 40, 26)
                Hb = dome(bun, 14) + noise(self.seed, (0.8, 3)) * 1.5
                c.put(soft(bun, 0.8), shade(Hb, self.hair, spec=0.12, shin=14))
            return
        if st in ('crew', 'hightight', 'crop', 'bun'):
            hl = {'crew': (166, 212), 'hightight': (168, 214), 'crop': (162, 214), 'bun': (166, 222)}[st]
            hair = top & self.hairline(*hl)
            thick = {'crew': 5, 'hightight': 6, 'crop': 9, 'bun': 4}[st]
            if st == 'crop':
                hair |= ellipse(cx - 6, 146, self.rx - 4, 44) & self.hairline(170, 214)
            dens = np.ones((N, N))
            if st == 'hightight':
                dens = np.where(abs(XX - cx) < self.rx * 0.62, 1.0, 0.45)
            elif st in ('crew', 'crop'):
                dens = np.clip(1.25 - np.clip((YY - 196) / 40, 0, 1) * 0.6, 0, 1)
            flow = (1.2, 4.0) if st == 'bun' else ((4.0, 1.2) if st == 'crop' else (1.6, 1.6))
            tex = noise(self.seed + 11, flow)
            u, w = (XX - cx) / (self.rx + 5), (YY - self.cy) / (self.ry + 6)
            H = 95 * np.sqrt(np.clip(1 - u * u - w * w, 0.02, 1)) + dome(hair, thick) + tex * 0.3
            col = shade(H, self.hair, ambient=0.24, spec=0.16, shin=10, ao_k=0.02)
            col = col * (0.9 + 0.1 * np.clip(tex * 0.5 + 0.5, 0, 1))[..., None]
            alpha = soft(hair, 1.4) + 0.14 * noise(self.seed + 5, (1.0, 1.0))
            alpha = np.clip((alpha - 0.3) / 0.4, 0, 1)
            c.put(alpha * dens, col)
            if st == 'crop':
                c.put(soft(stroke([(cx - 36, 118), (cx - 40, 150)], 1.5), 0.6) * hair, self.hair * 0.4)
        elif st == 'buzz':
            hair = top & self.hairline(168, 222)
            tex = np.random.default_rng(self.seed).random((N, N))
            c.put(soft(hair, 2.0) * 0.55, self.hair * 0.9)
            c.put(soft(hair, 2.0) * (tex < 0.25) * 0.35, self.hair * 0.5)

    def eyes(self, c, state):
        open_f = (1.0, 0.42, 0.0)[state]
        for s in (-1, 1):
            ex, ey, ew, eh = self.cx + s * self.edx, self.ey, self.ew, self.eh
            t = np.clip((XX - ex) / ew, -1, 1)
            inside = abs(XX - ex) < ew
            tu = np.clip((XX - (ex - s * 3)) / ew, -1, 1)
            y_up = ey - eh * (1 - tu * tu) ** 0.8
            y_lo = ey + eh * 0.72 * (1 - t * t) ** 0.9
            y_top = y_lo - (y_lo - y_up) * open_f
            eye = inside & (YY > y_top) & (YY < y_lo)
            if open_f > 0:
                depth = np.clip((YY - y_top) / (eh * 1.1), 0, 1)
                sclera = lin((236, 230, 224)) * (0.62 + 0.3 * depth - 0.25 * t * t)[..., None]
                c.put(soft(eye, 0.35), sclera)
                r = np.hypot(XX - ex, YY - (ey + 1))
                ir = 9.2
                k = np.clip(r / ir, 0, 1)
                streak = 1 + 0.12 * noise(self.seed + s, (0.4, 0.4))
                iris = self.iris * (0.55 + 0.6 * (1 - k ** 3) * (0.8 + 0.2 * k))[..., None] * streak[..., None]
                iris = iris * (0.55 + 0.45 * depth)[..., None]
                c.put(soft(eye & (r < ir), 0.25), iris)
                c.put(soft(eye & (r < 3.4), 0.25), lin((14, 12, 14)))
                c.put(soft(eye & (np.hypot(XX - ex + 3, YY - ey + 3) < 1.7), 0.2), lin((250, 250, 250)))
            lash = inside & (abs(YY - y_top) < (1.6 if self.f else 1.1) + 0.5 * (1 - abs(t)))
            c.put(soft(lash, 0.3) * 0.9, lin((26, 18, 16)))
            if self.f:
                flick = stroke([(ex + s * ew, ey - 1), (ex + s * (ew + 5), ey - 4)], 1.3)
                c.put(soft(flick, 0.3) * 0.8, lin((26, 18, 16)))
            if open_f > 0:
                crease = inside & (abs(YY - (y_up - 7)) < 1.0) & (abs(t) < 0.92)
                c.put(soft(crease, 0.8) * 0.28, self.skin * 0.55)
            low = inside & (abs(YY - y_lo) < 0.7) & (abs(t) < 0.9)
            c.put(soft(low, 0.5) * 0.25, self.skin * 0.6)

    def brows(self, c):
        n = {'thin': 70, 'medium': 110, 'bushy': 170}[self.row['brows']]
        band = {'thin': 3.5, 'medium': 5.0, 'bushy': 7.0}[self.row['brows']]
        col = self.hair * 0.75
        rng = np.random.default_rng(self.seed + 7)
        img = Image.new('L', (N, N), 0)
        d = ImageDraw.Draw(img)
        for s in (-1, 1):
            ex = self.cx + s * self.edx
            for _ in range(n):
                tt = rng.random()
                x = ex - s * 22 + s * 48 * tt
                y = self.ey - 25 - 6 * math.sin(math.pi * min(tt * 1.4, 1.0)) + rng.normal(0, band * 0.35)
                ang = math.radians(-70 + 60 * tt) if s > 0 else math.radians(-110 - 60 * tt)
                ln = rng.uniform(5, 8)
                d.line([(x * K, y * K), ((x + ln * math.cos(ang) * 0.6) * K, (y + ln * math.sin(ang) * 0.35) * K)],
                       fill=int(rng.uniform(140, 230)), width=max(1, int(1.1 * K)))
        a = ndimage.gaussian_filter(np.asarray(img) / 255.0, 0.5 * K)
        c.put(np.clip(a * 1.2, 0, 1), col)

    def nose_details(self, c):
        cx, ny = self.cx, self.ny
        for s in (-1, 1):
            c.put(soft(ellipse(cx + s * 8, ny + 6, 4.5, 2.6), 0.9) * 0.75, self.skin * 0.28)
            crease = stroke([(cx + s * 17, ny - 2), (cx + s * 20, ny + 6), (cx + s * 14, ny + 10)], 1.0)
            c.put(soft(crease, 1.0) * 0.25, self.skin * 0.5)

    def mouth(self, c, v):
        cx, my = self.cx, self.my
        open_h, wide = self.viseme_shape(v)
        w = (24 if self.f else 26) * wide
        t = np.clip((XX - cx) / w, -1, 1)
        inside = abs(XX - cx) < w
        bow = 2.2 * np.exp(-((XX - cx) / 7.0) ** 2)
        y_a = my - open_h * 0.35 * (1 - t ** 2) ** 0.6 - 0.5
        y_b = my + open_h * 0.65 * (1 - t ** 2) ** 0.7 + 0.5
        round_ = v in (lipsync.OO, lipsync.OH)
        if round_:
            ry = max(open_h * 0.5, 1)
            gap = np.hypot((XX - cx) / w, (YY - (my + open_h * 0.15)) / ry) < 1
            ring = ellipse(cx, my + open_h * 0.15, w + 7, ry + 8) & ~gap
            upper = ring & (YY < my + open_h * 0.15)
            lower = ring & (YY >= my + open_h * 0.15)
        else:
            gap = inside & (YY > y_a) & (YY < y_b) & (open_h > 0)
            up_th = (7.5 if self.f else 6.5) * (1 - t ** 2) ** 0.5
            lo_th = (9.0 if self.f else 8.0) * (1 - t ** 2) ** 0.55
            upper = inside & (YY < y_a + 0.5) & (YY > y_a - up_th - bow + 2.2)
            lower = inside & (YY > y_b - 0.5) & (YY < y_b + lo_th)
        Hl = g(cx, my - 7, 22, 5, 3) + g(cx, my + 8, 20, 6, 4)
        lip_c = shade(Hl, self.lip, ambient=0.3, spec=0.12, shin=26, warm=(1.06, 0.86, 0.84))
        c.put(soft(upper, 0.45) * 0.85, lip_c * 0.92)
        c.put(soft(lower, 0.45) * 0.85, lip_c)
        if open_h <= 0:
            line_ = inside & (abs(YY - (my + 0.2 * (1 - t * t))) < (1.0 if v == lipsync.REST else 1.4))
            c.put(soft(line_, 0.5) * 0.8, self.lip * 0.32)
            corners = (abs(abs(XX - cx) - w) < 2.5) & (abs(YY - my) < 2.0)
            c.put(soft(corners, 1.0) * 0.4, self.skin * 0.45)
            return
        depth = np.clip((YY - (my - open_h * 0.35)) / max(open_h, 1), 0, 1)
        c.put(soft(gap, 0.35), lin((40, 14, 18)) * (0.5 + 0.5 * depth)[..., None])
        top_edge = my - open_h * 0.35
        if v in (lipsync.FV, lipsync.TH, lipsync.AH, lipsync.EH, lipsync.S):
            tooth_h = {lipsync.FV: 8, lipsync.S: 6, lipsync.AH: 5.5, lipsync.EH: 5.5, lipsync.TH: 4}[v]
            teeth = gap & (YY < y_a + tooth_h) & (abs(t) < 0.82)
            tcol = lin((232, 226, 212)) * (0.55 + 0.45 * np.clip((YY - y_a) / tooth_h, 0, 1))[..., None]
            tcol = tcol * (1 - 0.35 * np.abs(t))[..., None]
            c.put(soft(teeth, 0.3), tcol)
        if v == lipsync.S:
            c.put(soft(gap & (YY > y_b - 4.5) & (abs(t) < 0.75), 0.3), lin((214, 208, 194)) * 0.8)
        if v in (lipsync.AH, lipsync.OH, lipsync.EH):
            tongue = gap & ellipse(cx, my + open_h * 0.65, w * 0.6, open_h * 0.4)
            c.put(soft(tongue, 0.5) * 0.9, lin((170, 70, 76)))
        if v == lipsync.TH:
            c.put(soft(gap & ellipse(cx, top_edge + 4, w * 0.4, 3.5), 0.4), lin((180, 80, 86)))

    def facial_hair(self, c, head, part):
        fh, cx, my = self.row['facial_hair'], self.cx, self.my
        if not fh:
            return
        tex = noise(self.seed + 21, (0.5, 1.8))
        if part == 'beard':
            if fh == 'stubble':
                region = head & (YY > 300 - 26 * ((XX - cx) / self.rx) ** 2) & ~ellipse(cx, my + 1, 30, 13)
                dots = np.random.default_rng(self.seed).random((N, N)) < 0.22
                c.put(soft(region, 4) * 0.22, self.hair * 0.6)
                c.put(soft(region, 4) * dots * 0.25, self.hair * 0.4)
            elif fh in ('trimmed', 'fullbeard'):
                full = fh == 'fullbeard'
                drop = 10 if full else 2
                top_y = (306 if full else 320) - (34 if full else 26) * ((XX - cx) / self.rx) ** 2
                beard = head & (YY > top_y) & ~ellipse(cx, my + 2, 32, 16)
                beard |= head & (abs(XX - cx) > self.rx - 14) & (YY > 262)
                beard |= ellipse(cx, self.cy + self.ry - 8 + drop, 50 + drop, 24 + drop) & (YY > 340)
                tex = noise(self.seed + 21, (0.8, 2.0))
                Hb = dome(beard, 4 + drop * 0.4) + tex * 0.4 + 60
                col = shade(Hb, self.hair, ambient=0.24, spec=0.08, shin=8, ao_k=0.02)
                col = col * (0.88 + 0.12 * np.clip(tex * 0.5 + 0.5, 0, 1))[..., None]
                alpha = np.clip((soft(beard, 2.0) + 0.16 * noise(self.seed + 9, (0.9, 0.9)) - 0.3) / 0.4, 0, 1)
                c.put(alpha, col)
        elif part == 'moustache' and fh in ('trimmed', 'fullbeard', 'moustache'):
            mo = polygon([(cx - 34, my - 4), (cx - 16, my - 15), (cx, my - 13), (cx + 16, my - 15), (cx + 34, my - 4),
                          (cx + 24, my - 4), (cx, my - 7), (cx - 24, my - 4)])
            Hm = dome(mo, 4) + tex * 0.3 + 60
            c.put(soft(mo, 1.4), shade(Hm, self.hair, ambient=0.24, spec=0.08, shin=8, ao_k=0.02))

    def skin_marks(self, c):
        cx = self.cx
        rng = np.random.default_rng(self.seed + 31)
        if 'freckles' in self.extras:
            for _ in range(46):
                s = rng.choice((-1, 1))
                x, y = cx + s * rng.uniform(10, 72), rng.uniform(270, 314)
                c.put(soft(ellipse(x, y, 1.4, 1.4), 0.6) * 0.45, self.skin * 0.62)
        if 'wrinkles' in self.extras:
            for s in (-1, 1):
                ex = cx + s * (self.edx + self.ew + 4)
                for dy in (-4, 1, 6):
                    c.put(soft(stroke([(ex, self.ey + dy), (ex + s * 9, self.ey + dy * 1.7)], 0.9), 0.8) * 0.3,
                          self.skin * 0.55)
            for y in (176, 188):
                c.put(soft(stroke([(cx - 34, y + 2), (cx, y), (cx + 34, y + 2)], 0.9), 1.0) * 0.22, self.skin * 0.6)
        if 'scar' in self.extras:
            c.put(soft(stroke([(cx + 52, 212), (cx + 66, 244)], 1.6), 0.5) * 0.7, np.minimum(self.skin * 1.25, 1))
            c.put(soft(stroke([(cx + 54, 212), (cx + 68, 244)], 0.8), 0.5) * 0.4, self.skin * 0.6)
        if 'smudge' in self.extras:
            c.put(soft(ellipse(cx + 60, 318, 14, 6), 4) * 0.35, lin((50, 46, 44)))

    def accessories(self, c):
        cx = self.cx
        if 'goggles' in self.extras:
            strap = polygon([(cx - self.rx - 6, 150), (cx + self.rx + 6, 150), (cx + self.rx + 6, 160),
                             (cx - self.rx - 6, 160)]) & ellipse(cx, self.cy, self.rx + 8, self.ry + 8)
            c.put(soft(strap, 0.5), shade(dome(strap, 3), lin((52, 50, 54))))
            for s in (-1, 1):
                rim = ellipse(cx + s * 38, 152, 22, 17)
                c.put(soft(rim, 0.5), shade(dome(rim, 8), lin((116, 106, 92)), spec=0.4, shin=16))
                glass = ellipse(cx + s * 38, 152, 15, 11)
                c.put(soft(glass, 0.4), shade(dome(glass, 6), lin((70, 130, 140)), spec=0.9, shin=40, ambient=0.4))
        if 'headset' in self.extras:
            band = ellipse(cx, self.cy - 4, self.rx + 14, self.ry + 12) & ~ellipse(cx, self.cy - 4, self.rx + 6,
                                                                                   self.ry + 4)
            band &= YY < 262
            dark = lin((46, 46, 52))
            c.put(soft(band, 0.6), shade(dome(band, 4) + 80, dark, spec=0.3, shin=20))
            ex = cx - self.rx - 6
            c.put(soft(ellipse(ex, 270, 17, 22), 0.6), shade(dome(ellipse(ex, 270, 17, 22), 10), dark, spec=0.3, shin=20))
            boom = stroke([(ex + 6, 288), (cx - 62, 330), (cx - 42, 340)], 3.2)
            c.put(soft(boom, 0.5), shade(dome(boom, 2) + 80, dark, spec=0.2))
            mic = ellipse(cx - 40, 340, 6, 5)
            c.put(soft(mic, 0.5), shade(dome(mic, 4) + 80, lin((30, 30, 32))))

    # the whole face ---------------------------------------------------------
    def draw(self, viseme=lipsync.REST, eyes=0):
        c = Canvas()
        head = self.head_mask()
        self.hair_layer(c, front=False)
        self.body(c, head)
        self.ears(c)
        H = self.face_height(head, viseme)
        skin = self.skin * (1 + 0.035 * noise(self.seed + 1, (5, 5)))[..., None]
        skin = skin * (1 + 0.02 * noise(self.seed + 2, (0.4, 0.4)))[..., None]
        flush = g(self.cx - 62, 296, 22, 16, 1) + g(self.cx + 62, 296, 22, 16, 1) + g(self.cx, self.ny - 2, 12, 10, 0.8)
        skin = skin * (1 + flush[..., None] * np.array([0.06, -0.03, -0.03]))
        oil = 0.06 + 0.10 * (g(self.cx, 175, 50, 25, 1) + g(self.cx, self.ny - 4, 10, 10, 1.4))
        lit = shade(H, skin, ambient=0.2, wrap=0.25, warm=(1.08, 0.8, 0.7))
        shiny = shade(H, skin, ambient=0.2, wrap=0.25, spec=1.0, shin=24, warm=(1.08, 0.8, 0.7))
        c.put(soft(head, 0.8), lit + (shiny - lit) * oil[..., None])
        self.skin_marks(c)
        self.facial_hair(c, head, 'beard')
        self.eyes(c, eyes)
        self.brows(c)
        self.nose_details(c)
        self.mouth(c, viseme)
        self.facial_hair(c, head, 'moustache')
        self.hair_layer(c, front=True)
        self.accessories(c)
        return c.image()

    def boxes(self, size):
        f = size / 512.0
        mb = [int((self.cx + MOUTH_BOX[0]) * f), int((self.my + MOUTH_BOX[1]) * f),
              int((self.cx + MOUTH_BOX[2]) * f), int((self.my + MOUTH_BOX[3]) * f)]
        eb = [int((self.cx + EYE_BOX[0]) * f), int((self.ey + EYE_BOX[1]) * f),
              int((self.cx + EYE_BOX[2]) * f), int((self.ey + EYE_BOX[3]) * f)]
        return mb, eb


def down(img, size):
    """Canvas to the portrait's size, alpha premultiplied so edges stay clean."""
    a = np.asarray(img).astype(np.float64)
    pm = a.copy()
    pm[..., :3] *= a[..., 3:4] / 255
    small = np.asarray(Image.fromarray(pm.astype(np.uint8), 'RGBA').resize((size, size), Image.LANCZOS)).astype(float)
    al = np.maximum(small[..., 3:4], 1e-3)
    small[..., :3] = np.clip(small[..., :3] * 255 / al, 0, 255)
    small[..., 3] = np.where(small[..., 3] > 96, 255, 0)
    return Image.fromarray(small.astype(np.uint8), 'RGBA')


def render(row, outdir, size):
    face = Face(row)
    os.makedirs(outdir, exist_ok=True)
    base = down(face.draw(), size)
    base.save(os.path.join(outdir, 'base.png'))
    mb, eb = face.boxes(size)
    mouths = [down(face.draw(viseme=v), size).crop(mb) for v in range(9)]
    eyes = [down(face.draw(eyes=e), size).crop(eb) for e in range(3)]
    for v, im in enumerate(mouths):
        im.save(os.path.join(outdir, 'mouth_%d.png' % v))
    for e, im in enumerate(eyes):
        im.save(os.path.join(outdir, 'eyes_%d.png' % e))
    json.dump({'key': row['key'], 'name': row['name'], 'callsign': row['callsign'], 'size': size,
               'mouth_box': mb, 'eye_box': eb, 'visemes': list(lipsync.VISEMES),
               'eyes': ['open', 'half', 'closed']}, open(os.path.join(outdir, 'face.json'), 'w'), indent=1)
    return base, mouths, eyes, mb, eb


def talk_frames(base, mouths, eyes, mb, eb, line_text, seed):
    """The engine's way: the base, then this frame's mouth and eye boxes."""
    seq = lipsync.frames(line_text) + [lipsync.REST] * 16
    blink = lipsync.blinks(len(seq), seed=seed)
    out = []
    for v, e in zip(seq, blink):
        f = Image.new('RGBA', base.size, BG + (255,))
        f.alpha_composite(base)
        f.alpha_composite(mouths[v], (mb[0], mb[1]))
        f.alpha_composite(eyes[e], (eb[0], eb[1]))
        out.append(f.convert('RGB'))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('keys', nargs='*')
    ap.add_argument('--out', default=os.path.join(HERE, '..', 'build', 'people'))
    ap.add_argument('--size', type=int, default=256)
    ap.add_argument('--sheet', default='')
    ap.add_argument('--gif', default='')
    args = ap.parse_args()
    rows = [r for r in csv.DictReader(open(os.path.join(HERE, '..', 'data', 'people.csv')))
            if not args.keys or r['key'] in args.keys]
    made = []
    for i, row in enumerate(rows):
        base, mouths, eyes, mb, eb = render(row, os.path.join(args.out, row['key']), args.size)
        frames = talk_frames(base, mouths, eyes, mb, eb, row['line'], i)
        frames[0].save(os.path.join(args.out, row['key'] + '.gif'), save_all=True, append_images=frames[1:],
                       duration=50, loop=0)
        made.append((row, base, mouths, eyes, frames))
        print('%-8s %s: %d frames of talk' % (row['key'], row['name'], len(frames)), flush=True)
    if args.sheet:
        cell = args.size // 2
        mw, ew = made[0][2][0].width // 2, made[0][3][0].width // 2
        sheet = Image.new('RGB', (cell + 9 * (mw + 4) + 3 * (ew + 4) + 20, len(made) * (cell + 16)), BG)
        d = ImageDraw.Draw(sheet)
        for i, (row, base, mouths, eyes, _f) in enumerate(made):
            y = i * (cell + 16)
            b = base.resize((cell, cell), Image.LANCZOS)
            sheet.paste(b, (0, y), b)
            x = cell + 8
            for im in mouths + eyes:
                sm = im.resize((im.width // 2, im.height // 2), Image.LANCZOS)
                sheet.paste(sm, (x, y + 20), sm)
                x += sm.width + 4
            label = '%s%s (%s)' % (row['name'], ', "%s"' % row['callsign'] if row['callsign'] else '', row['role'])
            d.text((4, y + cell + 2), label, fill=(230, 220, 180))
        sheet.save(args.sheet)
        print('sheet', args.sheet)
    if args.gif:
        cols, cell = 4, args.size // 2
        n = max(len(m[4]) for m in made)
        frames = []
        for k in range(n):
            gimg = Image.new('RGB', (cols * cell, ((len(made) + cols - 1) // cols) * cell), BG)
            for i, m in enumerate(made):
                f = m[4][k % len(m[4])]
                gimg.paste(f.resize((cell, cell), Image.LANCZOS), ((i % cols) * cell, (i // cols) * cell))
            frames.append(gimg)
        frames[0].save(args.gif, save_all=True, append_images=frames[1:], duration=50, loop=0)
        print('gif', args.gif)


if __name__ == '__main__':
    main()
