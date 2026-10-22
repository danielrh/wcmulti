"""Comm helmets as 3D shapes (signed distance fields), ray-marched in numpy: one render a helmet.

The renderer is shared: a Model gives its solids (name: distance), how each looks (albedo, roughness, clear coat),
an optional clear visor, the points the link needs (where the eyes, the callsign, a light land) and, for an alien's
lit parts, what glows in each state; render() marches it once, and the states are lit afterwards from the hit
points (emissive()), so a blink costs no second march.

Human, the pilots' helmet, after the user's reference (a clear-domed hood): a dark satin hard shell round the head,
its front open from brow to chin behind a clear visor in a black gasket, light air pads at the cheeks inside it, a
lamp and camera module on the brow, the shell sealing down into a neck collar, and (mask=True, the build's default)
the breathing gear over nose and mouth.  Lit by a key light (upper left, in front), a fill (right) and a rim
(behind); soft shadows, occlusion; the visor and the shell reflect a studio (a bright soft-box above and to the
left, dark below), more at a glancing angle.  The aliens' helmets are tools/aliens3d.py's.
"""
import numpy as np


def length(p):
    return np.sqrt(np.sum(p * p, axis=-1))


def ellipsoid(p, c, r):
    q = (p - c) / r
    k0 = length(q)
    k1 = length(q / r)
    return k0 * (k0 - 1.0) / np.maximum(k1, 1e-9)


def rbox(p, c, b, rad):
    q = np.abs(p - c) - (np.asarray(b) - rad)
    return length(np.maximum(q, 0)) + np.minimum(np.max(q, axis=-1), 0) - rad


def capsule(p, a, b, r):
    pa, ba = p - a, np.asarray(b) - a
    h = np.clip(np.sum(pa * ba, -1) / np.dot(ba, ba), 0, 1)
    return length(pa - ba * h[..., None]) - r


def cyl_y(p, c, r, half):
    q = p - c
    d = np.stack([np.hypot(q[..., 0], q[..., 2]) - r, np.abs(q[..., 1]) - half], -1)
    return np.minimum(np.max(d, -1), 0) + length(np.maximum(d, 0))


def cyl_z(p, c, r, half):
    q = p - c
    d = np.stack([np.hypot(q[..., 0], q[..., 1]) - r, np.abs(q[..., 2]) - half], -1)
    return np.minimum(np.max(d, -1), 0) + length(np.maximum(d, 0))


def smin(a, b, k):
    h = np.clip(0.5 + 0.5 * (b - a) / k, 0, 1)
    return b * (1 - h) + a * h - k * h * (1 - h)


def smax(a, b, k):
    return -smin(-a, -b, k)


def mirror_x(p):
    pm = p.copy()
    pm[..., 0] = np.abs(pm[..., 0])
    return pm


class Model:
    """a helmet: its solids, how each looks, its points; no visor, nothing glows"""
    order = []
    look = {}                           # solid: (albedo, roughness, clear coat)
    points = {}                         # name: a 3D point, projected onto the picture
    view = {}                           # the camera, if not the pilots' framing (camera()'s span, eye, look)

    def solids(self, p):
        raise NotImplementedError

    def visor(self, p):                 # a clear visor's distance, or None
        return None

    def albedo(self, P, mat, alb):      # paint: the albedo at hit points P (mat: their solids' indices)
        return alb

    def glow(self, P, mat, state):      # what glows at hit points P in a state (a dict), 0 .. 1 and more
        return np.zeros(len(P))

    def sdf(self, p):
        d = self.solids(p)
        return np.minimum.reduce([d[k] for k in self.order])

    def material(self, p):
        d = self.solids(p)
        return np.argmin(np.stack([d[n] for n in self.order], 0), 0)


# --- the pilots' helmet ---

HEAD_C = np.array([0.0, 0.0, 0.0])
HEAD_R = np.array([0.40, 0.52, 0.46])
SHELL_C = np.array([0.0, 0.06, -0.06])
SHELL_R = np.array([0.60, 0.70, 0.70])
# the face's opening, as a rounded region on the shell's front
OPEN_C = np.array([0.0, -0.06, 0.6])
VISOR_C = np.array([0.0, -0.04, 0.02])
VISOR_R = np.array([0.56, 0.62, 0.66])
# the eyes, on the head's front, for fitting the portraits
EYES = [np.array([-0.152, 0.075, 0.43]), np.array([0.152, 0.075, 0.43])]


def opening(p):
    """negative inside the face's opening (seen from the front)"""
    q = p - OPEN_C
    return np.hypot(q[..., 0] / 0.43, (q[..., 1] + 0.0) / 0.50) - 1.0


class Human(Model):
    """the pilots' helmet; mask: the breathing gear over nose and mouth (the user's ruling), else the face bare
    behind the visor (the build's RV_COMMS=face, to weigh what a speaking mouth costs)"""

    def __init__(self, mask=True):
        self.mask = mask
        self.order = ['shell', 'gasket', 'pad', 'metal', 'lens', 'seal', 'head'] + (['mask', 'hose'] if mask else [])
        self.look = {'shell': (0.16, 0.30, 0.9), 'gasket': (0.035, 0.6, 0.05), 'pad': (0.82, 0.85, 0.05),
                     'metal': (0.30, 0.25, 0.6), 'lens': (0.05, 0.05, 1.0), 'seal': (0.07, 0.9, 0.05),
                     'head': (0.5, 0.7, 0.05), 'mask': (0.10, 0.55, 0.05), 'hose': (0.09, 0.6, 0.05)}
        self.points = {'eye_l': EYES[0], 'eye_r': EYES[1],
                       'brow': np.array([0.0, 0.535, 0.56]),     # the callsign's place on the shell, above the gasket
                       'led': np.array([0.0, -0.265, 0.59])}     # the regulator's light: lit while the pilot speaks

    def solids(self, p):
        x = p[..., 0]
        y = p[..., 1]
        z = p[..., 2]
        pm = mirror_x(p)
        # the shell: hollow, its front open round the face, its edge rolled, a crest band over the top
        outer = ellipsoid(p, SHELL_C, SHELL_R)
        shell = np.abs(outer + 0.03) - 0.03
        cut = np.maximum(opening(p) * 0.45, -(z - 0.05))
        shell = smax(shell, -cut, 0.03)
        # seams: one along the shell's side, one round the opening
        seam = np.abs(np.abs(x) - 0.43) - 0.004
        seam = np.maximum(seam, -(y + 0.05))
        shell = smax(shell, -np.maximum(seam, -outer - 0.02), 0.003)
        # the gasket: a black rim round the opening, the visor's frame
        gasket = np.maximum(np.abs(opening(p) * 0.45 + 0.01) - 0.022, np.abs(outer + 0.005) - 0.035)
        gasket = np.maximum(gasket, -(z - 0.02))
        # the air pads at the cheeks, inside the opening
        pad = rbox(pm, np.array([0.335, -0.12, 0.30]), np.array([0.055, 0.12, 0.10]), 0.03)
        pad = np.maximum(pad, -(ellipsoid(p, SHELL_C, SHELL_R - 0.07)))
        # the brow's lamp and camera: a housing, two lenses
        module = rbox(p, np.array([0.0, 0.69, 0.22]), np.array([0.11, 0.035, 0.07]), 0.025)
        lens = np.minimum(cyl_z(p, np.array([-0.05, 0.69, 0.29]), 0.022, 0.012),
                          cyl_z(p, np.array([0.05, 0.69, 0.29]), 0.015, 0.012))
        # the neck seal: soft, down from the shell's lower rim to the collar; the collar ring; the suit's shoulders
        # (a fabric tube, wider as it goes down, with a few soft folds round it)
        ty = np.clip((-0.45 - y) / 0.75, 0, 1)
        rad = 0.30 + 0.12 * ty * ty
        ang = np.arctan2(x, z + 0.06)
        folds = 0.012 * np.sin(ang * 7 + y * 9) * (1 - np.abs(np.cos(ang)) * 0.5)
        seal = np.maximum(np.hypot(x, z + 0.06) - rad - folds, np.maximum(y + 0.40, -1.20 - y))
        collar = cyl_y(p, np.array([0, -1.02, -0.06]), 0.46, 0.04) - 0.02
        suit = ellipsoid(p, np.array([0, -1.55, -0.05]), np.array([1.15, 0.58, 0.64]))
        seal = smin(smin(seal, collar, 0.04), suit, 0.10)
        head = ellipsoid(p, HEAD_C, HEAD_R)
        d = {'shell': shell, 'gasket': gasket, 'pad': pad, 'metal': module, 'lens': lens, 'seal': seal, 'head': head}
        if not self.mask:
            return d
        # the breathing gear (the user: the mouth covered, the eyes all shown): a rubber mask over nose and mouth,
        # its regulator in front, straps back across the cheeks, a ribbed hose down to the neck seal
        mask = ellipsoid(p, np.array([0.0, -0.215, 0.385]), np.array([0.215, 0.205, 0.16]))
        mask = smax(mask, -(p[..., 2] - 0.30), 0.02)
        mask = smin(mask, rbox(p, np.array([0.0, -0.10, 0.47]), np.array([0.07, 0.07, 0.05]), 0.04), 0.05)   # (nose)
        reg = cyl_z(p, np.array([0.0, -0.265, 0.535]), 0.06, 0.035)
        reg = np.minimum(reg, cyl_z(p, np.array([0.0, -0.265, 0.575]), 0.035, 0.012))
        straps = np.minimum(capsule(pm, np.array([0.18, -0.13, 0.38]), np.array([0.36, -0.02, 0.12]), 0.014),
                            capsule(pm, np.array([0.19, -0.30, 0.34]), np.array([0.36, -0.24, 0.08]), 0.014))
        t = np.clip((-0.33 - y) / 0.36, 0, 1)
        hx = 0.10 + 0.16 * t
        hz = 0.47 - 0.14 * t
        hose = np.where((y < -0.30) & (y > -0.72), np.hypot(x - hx, z - hz) - 0.032 - 0.004 * np.sin(y * 160), 1e3)
        d['metal'] = np.minimum(module, reg)
        d['mask'] = np.minimum(mask, straps)
        d['hose'] = hose
        return d

    def visor(self, p):
        """the visor: a thin clear dome over the opening"""
        v = np.abs(ellipsoid(p, VISOR_C, VISOR_R)) - 0.006
        return np.maximum(v, np.maximum(opening(p) * 0.45 + 0.005, -(p[..., 2] - 0.15)))


# --- the renderer ---

def normal_of(f, p, e=0.0012):
    ex, ey, ez = np.array([e, 0, 0]), np.array([0, e, 0]), np.array([0, 0, e])
    n = np.stack([f(p + ex) - f(p - ex), f(p + ey) - f(p - ey), f(p + ez) - f(p - ez)], -1)
    return n / np.maximum(length(n)[..., None], 1e-9)


def march(f, ro, rd, tmax=8.0, steps=180):
    t = np.zeros(rd.shape[:-1])
    hit = np.zeros(t.shape, bool)
    alive = np.ones(t.shape, bool)
    for _ in range(steps):
        idx = np.nonzero(alive)
        if len(idx[0]) == 0:
            break
        p = ro + rd[idx] * t[idx][..., None]
        d = f(p)
        t[idx] += d * 0.9
        done = d < 0.0006
        far = t[idx] > tmax
        hit[tuple(i[done] for i in idx)] = True
        alive[tuple(i[done | far] for i in idx)] = False
    return t, hit


def soft_shadow(sdf, p, ld, k=10.0, steps=48):
    res = np.ones(p.shape[:-1])
    t = np.full(res.shape, 0.01)
    for _ in range(steps):
        d = sdf(p + ld * t[..., None])
        res = np.minimum(res, np.clip(k * d / t, 0, 1))
        t += np.clip(d, 0.008, 0.1)
    return np.clip(res, 0, 1)


def occlusion(sdf, p, n):
    occ = np.zeros(p.shape[:-1])
    for i, h in enumerate((0.02, 0.05, 0.09, 0.14, 0.2)):
        occ += (h - sdf(p + n * h)) * (0.85 ** i)
    return np.clip(1 - 2.0 * occ, 0, 1)


KEY = np.array([-0.55, 0.62, 0.56]) / np.linalg.norm([-0.55, 0.62, 0.56])
FILL = np.array([0.8, 0.1, 0.6]) / np.linalg.norm([0.8, 0.1, 0.6])
BACK = np.array([0.3, 0.5, -0.8]) / np.linalg.norm([0.3, 0.5, -0.8])


def studio(R):
    """what a clear coat reflects: a soft-box above and to the left, a dimmer strip right, a dark floor"""
    box = np.clip((R[..., 1] - 0.10) / 0.5, 0, 1) ** 1.2 * np.clip(1 - np.abs(R[..., 0] + 0.25) * 1.1, 0, 1)
    strip = 0.35 * np.exp(-((R[..., 0] - 0.75) / 0.12) ** 2) * np.clip(R[..., 1] + 0.3, 0, 1)
    return 0.03 + 0.95 * box + strip


def camera(w, h, span=1.52, eye=(0.0, 0.10, 5.0), look=(0.0, 0.135, 0.0)):
    eye = np.array(eye, dtype=np.float64)
    fwd = np.array(look) - eye
    dist = np.linalg.norm(fwd)
    fwd /= dist
    right = np.cross(fwd, [0, 1, 0])
    right /= np.linalg.norm(right)
    up = np.cross(right, fwd)
    ys, xs = np.mgrid[0:h, 0:w]
    sx = (xs + 0.5) / w - 0.5
    sy = 0.5 - (ys + 0.5) / h
    scale = span / dist
    rd = fwd + right * (sx * scale)[..., None] + up * (sy * scale * h / w)[..., None]
    rd /= length(rd)[..., None]

    def project(P):
        q = P - eye
        zz = q @ fwd
        return ((q @ right) / zz / scale + 0.5) * w, (0.5 - (q @ up) / zz / scale / (h / w)) * h

    return eye, rd, project


def render(w, h, model=None):
    """a helmet: its luminance (h, w); a material map (the model's order, -1 nothing) and its names; the light on
    each point (the face's, where a portrait goes); the visor's reflection to add over what is behind it and how
    much of that it lets through; the hit points (for emissive()); the model's points on the picture"""
    model = model or Human()
    sdf = model.sdf
    eye, rd, project = camera(w, h, **model.view)
    t, hit = march(sdf, eye, rd)
    p = eye + rd * t[..., None]
    lum = np.zeros((h, w))
    light = np.zeros((h, w))
    matmap = np.full((h, w), -1)
    P, V = p[hit], -rd[hit]
    N = normal_of(sdf, P)
    mat = model.material(P)
    matmap[hit] = mat
    sh = soft_shadow(sdf, P + N * 0.002, KEY)
    ao = occlusion(sdf, P, N)
    table = np.array([model.look[k] for k in model.order])
    alb, rough, coat = table[mat, 0], table[mat, 1], table[mat, 2]
    alb = model.albedo(P, mat, alb)
    nl = np.clip(N @ KEY, 0, 1)
    nv = np.clip(np.sum(N * V, -1), 0, 1)

    def spec(L, power):
        Hh = L + V
        Hh /= length(Hh)[..., None]
        return np.clip(np.sum(N * Hh, -1), 0, 1) ** power
    power = 2.0 / np.maximum(rough ** 2, 1e-3)
    diffuse = alb * (0.06 + 1.0 * nl * sh + 0.30 * np.clip(N @ FILL, 0, 1) + 0.10 * (0.5 + 0.5 * N[:, 1])) * ao
    specular = (1.1 * spec(KEY, power) * sh + 0.3 * spec(FILL, power)) * (1 - rough) ** 2
    R = -V + 2 * nv[..., None] * N
    fres = 0.04 + 0.96 * (1 - nv) ** 5
    refl = studio(R) * (0.06 + 0.94 * fres) * coat * ao
    rim = 0.25 * np.clip(N @ BACK, 0, 1) * (1 - nv) ** 2
    lum[hit] = diffuse + specular + refl + rim
    light[hit] = (0.22 + 0.9 * nl * sh + 0.25 * np.clip(N @ FILL, 0, 1)) * ao
    # a clear visor: where the ray crosses it before what it hits, its reflection and what it lets through
    vrefl = np.zeros((h, w))
    vpass = np.ones((h, w))
    if model.visor(eye[None]) is not None:
        tv, hv = march(model.visor, eye, rd, steps=120)
        front = hv & (~hit | (tv < t))
        if front.any():
            Pv = eye + rd[front] * tv[front][..., None]
            Nv = normal_of(model.visor, Pv)
            Vv = -rd[front]
            Nv = np.where((np.sum(Nv * Vv, -1) < 0)[..., None], -Nv, Nv)
            nvv = np.clip(np.sum(Nv * Vv, -1), 0, 1)
            Rv = -Vv + 2 * nvv[..., None] * Nv
            fres = 0.05 + 0.95 * (1 - nvv) ** 5
            vrefl[front] = studio(Rv) * (0.22 + 0.78 * fres) * 0.4
            vpass[front] = 0.82 * (1 - fres)
    pts = {k: project(v) for k, v in model.points.items()}
    out = {'lum': lum, 'mat': matmap, 'names': list(model.order), 'light': light, 'visor_refl': vrefl,
           'visor_pass': vpass, 'hit': hit, 'P': P, 'pmat': mat, 'points': pts, 'model': model}
    if 'eye_l' in pts:                  # (the pilots' helmet: where the portraits' eyes go, the callsign, the light)
        out['eyes'] = [pts['eye_l'], pts['eye_r']]
        out['brow'] = pts['brow']
        out['led'] = pts.get('led')
    return out


def emissive(r, state):
    """what the model's lit parts add in a state: its glow at each hit pixel (h, w), before any bloom"""
    g = np.zeros(r['lum'].shape)
    g[r['hit']] = r['model'].glow(r['P'], r['pmat'], state)
    return g
