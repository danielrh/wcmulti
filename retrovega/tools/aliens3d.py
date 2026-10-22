"""The aliens' comm helmets (tools/helmet3d.py's renderer): Aera and Rlaan pilots, heavy gear, no face shown.

The user's brief (2026-10-22): Rlaan and Aera pilots, heavy gear, mostly anonymous to the player, with distinct
helmets.  Each pilot (data/aliens.csv) has a helmet of their own in their side's manner (docs/spec.md's looks):

  Aera, steel and teal with glowing trim, ships named for Sparta's kings: hard angular steel helms, an opaque face
  plate with a lit slit for eyes, a vocoder grille that lights as they speak, heavy gorgets.
  Rlaan, charcoal and violet, organic: smooth fluted shells like a seed or a beetle's, clusters of round lenses for
  eyes, soft ribbed tubes, the speech a row of lights that pulse.

What a human's eyes do (tools/comms.py's frames) these do with light: 'open' lit, 'half' dimmed, 'shut' dark (a
blink is a flicker), 'left' / 'right' the light's bright point aside (a glance); 'talk' 1 and 2 light the grille
or the speech lights partly and fully, as lipsync's shapes open.
"""
import numpy as np

from helmet3d import Model, capsule, cyl_z, ellipsoid, length, mirror_x, rbox, smax, smin

GLANCE = {'left': -1, 'right': 1}
EYE_LIGHT = {'open': 1.0, 'half': 0.45, 'shut': 0.05, 'left': 1.0, 'right': 1.0}


def band(v, c, half, soft=0.006):
    """1 inside |v - c| < half, falling off over soft"""
    return np.clip((half - np.abs(v - c)) / soft + 0.5, 0, 1)


# --- Aera ---

class Aera(Model):
    """an Aera helm: steel, an opaque face plate, the eyes a lit slit with a scanning point, a lit vocoder grille"""
    look_steel = (0.17, 0.26, 0.85)

    def __init__(self):
        self.order = ['steel', 'plate', 'trim', 'gorget', 'grille']
        self.look = {'steel': self.look_steel, 'plate': (0.035, 0.12, 1.0), 'trim': (0.20, 0.35, 0.6),
                     'gorget': (0.22, 0.4, 0.5), 'grille': (0.05, 0.5, 0.2)}
        self.points = {}

    def eye_glow(self, P, state):
        """the slit (Aera.slit(P): 1 on it) lit, a bright point along it: the middle, or aside in a glance"""
        on = self.slit(P)
        g = GLANCE.get(state.get('eyes'), 0) * self.scan
        spot = np.exp(-((P[:, 0] - g) / 0.07) ** 2)
        return on * EYE_LIGHT[state.get('eyes', 'open')] * (0.55 + 0.9 * spot)

    def glow(self, P, mat, state):
        return self.eye_glow(P, state) + self.speech(P, state.get('talk', 0))


class Hoplite(Aera):
    """the line pilot's helm (Nicander, Ariston): a Corinthian helm, a low crest along it, the cheek guards closing
    to a T, the face plate behind it; the slit across the T's bar, the grille down its stem"""
    scan = 0.16

    def solids(self, p):
        x, y, z = p[..., 0], p[..., 1], p[..., 2]
        C, R = np.array([0.0, 0.08, -0.04]), np.array([0.53, 0.68, 0.64])
        helm = ellipsoid(p, C, R)
        crest = np.maximum(np.abs(x) - 0.03, ellipsoid(p, C, R + 0.055))
        crest = np.maximum(crest, -(y - 0.2))
        steel = smin(helm, crest, 0.02)
        # the T: the eye band across, the stem down the middle, cut through the front
        tbar = rbox(p, np.array([0.0, 0.075, 0.55]), np.array([0.31, 0.065, 0.3]), 0.04)
        tstem = rbox(p, np.array([0.0, -0.18, 0.55]), np.array([0.085, 0.24, 0.3]), 0.04)
        cut = np.minimum(tbar, tstem)
        steel = smax(steel, -cut, 0.015)
        plate = ellipsoid(p, C + np.array([0, 0, -0.02]), R - np.array([0.04, 0.04, 0.05]))
        # a raised brow band over the T, the trim round the T's edge
        brow = rbox(p, np.array([0.0, 0.21, 0.52]), np.array([0.34, 0.035, 0.12]), 0.03)
        brow = np.maximum(brow, helm - 0.03)
        trim = np.maximum(np.abs(cut) - 0.012, np.abs(helm) - 0.02)
        gorget = np.minimum(ellipsoid(p, np.array([0, -0.62, -0.06]), np.array([0.62, 0.16, 0.58])),
                            ellipsoid(p, np.array([0, -0.80, -0.06]), np.array([0.78, 0.17, 0.66])))
        grille = rbox(p, np.array([0.0, -0.20, 0.47]), np.array([0.05, 0.17, 0.04]), 0.02)
        return {'steel': smin(steel, brow, 0.02), 'plate': plate, 'trim': trim, 'gorget': gorget, 'grille': grille}

    def albedo(self, P, mat, alb):
        # a chevron on the brow: the line pilot's mark
        x, y = P[:, 0], P[:, 1]
        chev = band(y - 0.36 + 0.55 * np.abs(x), 0.0, 0.022) * (np.abs(x) < 0.13) * (P[:, 2] > 0.3)
        return np.where(mat == 0, alb + 0.45 * chev, alb)

    def slit(self, P):
        return band(P[:, 1], 0.075, 0.016) * (np.abs(P[:, 0]) < 0.26) * (P[:, 2] > 0.3)

    def speech(self, P, level):
        y = P[:, 1]
        bars = sum(band(y, c, 0.012) for c in (-0.08, -0.15, -0.22, -0.29))
        on = bars * (np.abs(P[:, 0]) < 0.04) * (P[:, 2] > 0.45)
        lit = {0: 0.10, 1: 0.6, 2: 1.3}[level]
        inner = band(y, -0.185, 0.06 if level == 1 else 0.2, 0.01)
        return on * (0.10 + (lit - 0.10) * inner)


class Ephor(Aera):
    """the ace's helm (the Convolution): tall, a broad swept fin at each temple like a hawk's wing, the face a dark
    shield coming to a point at the chin, a V of two slanted slits across it for eyes, the grille at its point;
    three rank bars on the brow"""
    scan = 0.14
    view = {'span': 1.80, 'look': (0.0, 0.16, 0.0)}

    def solids(self, p):
        x, y, z = p[..., 0], p[..., 1], p[..., 2]
        pm = mirror_x(p)
        C, R = np.array([0.0, 0.12, -0.08]), np.array([0.48, 0.70, 0.66])
        helm = ellipsoid(p, C, R)
        # the jaw guard: a wedge down to a point at the chin
        jaw = np.maximum(ellipsoid(p, np.array([0.0, -0.22, 0.05]), np.array([0.40, 0.42, 0.50])),
                         np.abs(x) * 1.0 + (y + 0.62) * -0.62 - 0.02)
        steel = smin(helm, jaw, 0.06)
        # the fins: a flat swept blade at each temple, out and up to a point, thin front to back
        u, v = pm[..., 0] - 0.36, pm[..., 1] - 0.16
        tri = np.maximum.reduce([-u, (v - 0.95 * u) * 0.72 - 0.06, -(v - 0.55 * u) * 0.87 - 0.02, u - 0.48])
        fin = np.maximum(tri, np.abs(pm[..., 2] + 0.05 + 0.35 * u) - 0.03)
        steel = smin(steel, fin, 0.04)
        # the face: a shield from the brow to the jaw's point, set a little in
        shield = np.maximum.reduce([np.abs(x) - 0.27, y - 0.17, np.abs(x) * 1.3 - (y + 0.52)])
        cut = np.maximum(shield, -(z - 0.30))
        steel = smax(steel, -cut, 0.012)
        plate = ellipsoid(p, C + np.array([0, -0.04, -0.03]), R - np.array([0.03, 0.02, 0.035]))
        plate = smin(plate, jaw + 0.03, 0.06)
        trim = np.maximum(np.abs(shield) - 0.012, np.abs(helm) - 0.04)
        gorget = np.minimum(ellipsoid(p, np.array([0, -0.70, -0.08]), np.array([0.70, 0.20, 0.62])),
                            ellipsoid(p, np.array([0, -0.90, -0.08]), np.array([0.92, 0.20, 0.70])))
        grille = rbox(p, np.array([0.0, -0.33, 0.40]), np.array([0.07, 0.05, 0.05]), 0.02)
        return {'steel': steel, 'plate': plate, 'trim': trim, 'gorget': gorget, 'grille': grille}

    def albedo(self, P, mat, alb):
        x, y = P[:, 0], P[:, 1]
        bars = sum(band(x, c, 0.016) for c in (-0.06, 0.0, 0.06)) * band(y, 0.47, 0.06) * (P[:, 2] > 0.3)
        return np.where(mat == 0, alb + 0.5 * bars, alb)

    def slit(self, P):
        x, y = P[:, 0], P[:, 1]
        return band(y - (0.02 + 0.30 * np.abs(x)), 0.0, 0.016) * (np.abs(x) > 0.035) * (np.abs(x) < 0.22) * \
            (P[:, 2] > 0.3)

    def speech(self, P, level):
        y = P[:, 1]
        bars = sum(band(y, c, 0.011) for c in (-0.29, -0.33, -0.37))
        on = bars * (np.abs(P[:, 0]) < 0.06) * (P[:, 2] > 0.4)
        return on * {0: 0.10, 1: 0.6, 2: 1.3}[level] * (1 if level != 1 else band(y, -0.33, 0.025, 0.01) + 0.1)


class Phalanx(Aera):
    """the heavy pilot's helm (Areus, Cleomenes): a broad rounded box under a jutting brow, a dark goggle band
    beneath it with two round optics lit in it, a respirator snout below with its vents, a canister at each cheek,
    a hazard stripe over the top"""
    view = {'span': 1.66, 'look': (0.0, 0.10, 0.0)}
    scan = 0.0
    optics = [np.array([-0.17, 0.10]), np.array([0.17, 0.10])]

    def solids(self, p):
        x, y, z = p[..., 0], p[..., 1], p[..., 2]
        pm = mirror_x(p)
        helm = rbox(p, np.array([0.0, 0.08, -0.04]), np.array([0.56, 0.56, 0.56]), 0.22)
        brow = rbox(p, np.array([0.0, 0.25, 0.50]), np.array([0.52, 0.05, 0.14]), 0.035)
        steel = smin(helm, brow, 0.04)
        # the goggle band: cut across under the brow, the dark plate behind; the optics' rims in it
        band_cut = rbox(p, np.array([0.0, 0.10, 0.62]), np.array([0.40, 0.085, 0.2]), 0.05)
        steel = smax(steel, -band_cut, 0.01)
        plate = rbox(p, np.array([0.0, 0.08, -0.08]), np.array([0.54, 0.54, 0.54]), 0.22)
        rim = np.maximum(np.abs(cyl_z(pm, np.array([0.17, 0.10, 0.53]), 0.085, 0.02)) - 0.012, -(z - 0.5))
        # the snout: a rounded block forward of the mouth, vents down its front; canisters at the cheeks
        snout = rbox(p, np.array([0.0, -0.24, 0.52]), np.array([0.17, 0.13, 0.12]), 0.07)
        can = cyl_z(pm, np.array([0.36, -0.26, 0.44]), 0.10, 0.11) - 0.008 * np.cos(z * 90) * (z > 0.36)
        gorget = np.minimum(ellipsoid(p, np.array([0, -0.62, -0.06]), np.array([0.78, 0.16, 0.66])),
                            ellipsoid(p, np.array([0, -0.80, -0.06]), np.array([0.92, 0.18, 0.74])))
        return {'steel': smin(steel, snout, 0.05), 'plate': plate, 'trim': rim,
                'gorget': smin(gorget, can, 0.03), 'grille': np.full(x.shape, 1e3)}

    def albedo(self, P, mat, alb):
        # a hazard stripe down the helm's middle; the snout's vents dark
        x, y, z = P[:, 0], P[:, 1], P[:, 2]
        stripe = band(x, 0.0, 0.05) * (y > 0.33) * (z > 0.0)
        vents = sum(band(x, c, 0.012) for c in (-0.09, -0.03, 0.03, 0.09)) * band(y, -0.25, 0.08) * (z > 0.6)
        return np.where(mat == 0, alb + 0.35 * stripe - 0.6 * alb * vents, alb)

    def eye_glow(self, P, state):
        e = state.get('eyes', 'open')
        g = GLANCE.get(e, 0)
        out = np.zeros(len(P))
        for c in self.optics:
            d = np.hypot(P[:, 0] - c[0], P[:, 1] - c[1])
            iris = band(d, 0.050, 0.022 if e != 'half' else 0.010, 0.01)
            pupil = np.exp(-((P[:, 0] - c[0] - 0.04 * g) ** 2 + (P[:, 1] - c[1] - 0.015) ** 2) / 0.02 ** 2)
            out += (P[:, 2] > 0.4) * (d < 0.08) * EYE_LIGHT[e] * (0.8 * iris + 1.2 * pupil)
        return out

    def speech(self, P, level):
        x, y = P[:, 0], P[:, 1]
        vents = sum(band(x, c, 0.012) for c in (-0.09, -0.03, 0.03, 0.09)) * band(y, -0.25, 0.08) * (P[:, 2] > 0.6)
        inner = np.abs(x) < 0.06
        return vents * {0: 0.0, 1: 0.7 * inner, 2: 1.2}[level]


# --- Rlaan ---

class Rlaan(Model):
    """a Rlaan shell: charcoal, waxy, fluted; round lenses for eyes (a lit iris each, a glint that looks aside); the
    speech a row of small lights"""
    lenses = []                         # (x, y, z, radius) on the face

    def __init__(self):
        self.order = ['shell', 'lens', 'soft', 'cowl']
        self.look = {'shell': (0.11, 0.33, 0.65), 'lens': (0.03, 0.08, 1.0), 'soft': (0.08, 0.6, 0.2),
                     'cowl': (0.16, 0.45, 0.5)}
        self.points = {}

    def lens_solid(self, p):
        return np.minimum.reduce([length(p - np.array(c[:3])) - c[3] for c in self.lenses])

    def eye_glow(self, P, state):
        e = state.get('eyes', 'open')
        g = GLANCE.get(e, 0)
        out = np.zeros(len(P))
        for cx, cy, cz, r in self.lenses:
            d = np.hypot(P[:, 0] - cx, P[:, 1] - cy)
            near = (d < r) & (P[:, 2] > cz)
            iris = band(d, r * 0.55, r * (0.22 if e != 'half' else 0.1), r * 0.15)
            glint = np.exp(-((P[:, 0] - cx - 0.45 * r * g) ** 2 + (P[:, 1] - cy - 0.2 * r) ** 2) / (0.3 * r) ** 2)
            out += near * EYE_LIGHT[e] * (0.7 * iris + 1.1 * glint)
        return out

    lights_mat = None                   # (the speech lights only on this solid)

    def speech(self, P, level, mat=None):
        out = np.zeros(len(P))
        n = len(self.lights)
        for i, (cx, cy) in enumerate(self.lights):
            d = np.hypot(P[:, 0] - cx, P[:, 1] - cy)
            k = abs(i - (n - 1) / 2.0) / max(1, (n - 1) / 2.0)        # (0 the middle, 1 the ends)
            lit = 0.12 if level == 0 else (1.2 if level == 2 or k < 0.5 else 0.12)
            on = (P[:, 2] > self.lights_z) if self.lights_mat is None else (mat == self.order.index(self.lights_mat))
            out += np.exp(-(d / 0.022) ** 2) * on * lit
        return out

    def glow(self, P, mat, state):
        return self.eye_glow(P, state) + self.speech(P, state.get('talk', 0), mat)


class Lantern(Rlaan):
    """the scout's shell (Shizu, Gaozong): a smooth pear, wide at the crown and fluted there; seven lenses in a
    hexagon in the middle of the face; a lit band round the crown; the speech lights down the throat"""
    view = {'span': 1.62, 'look': (0.0, 0.12, 0.0)}
    lenses = [(0.0, 0.10, 0.53, 0.062)] + [(0.13 * np.cos(a), 0.10 + 0.13 * np.sin(a), 0.50, 0.050)
                                           for a in np.radians([0, 60, 120, 180, 240, 300])]
    lights = [(0.0, -0.13), (0.0, -0.20), (0.0, -0.27), (0.0, -0.34)]
    lights_z = 0.35

    def solids(self, p):
        x, y, z = p[..., 0], p[..., 1], p[..., 2]
        crown = ellipsoid(p, np.array([0.0, 0.18, -0.06]), np.array([0.60, 0.56, 0.60]))
        jaw = ellipsoid(p, np.array([0.0, -0.22, 0.06]), np.array([0.34, 0.40, 0.42]))
        shell = smin(crown, jaw, 0.18)
        ang = np.arctan2(x, z + 0.06)
        flute = 0.010 * np.cos(ang * 12) * np.clip((y - 0.05) / 0.3, 0, 1)
        shell = shell + flute
        # the lens cluster sits in a shallow dish
        dish = length(p - np.array([0.0, 0.10, 0.92])) - 0.46
        shell = smax(shell, -dish, 0.05)
        lens = self.lens_solid(p)
        soft = np.minimum(ellipsoid(p, np.array([0, -0.66, -0.05]), np.array([0.66, 0.22, 0.60])),
                          np.hypot(x, z + 0.02) - 0.30 - 0.012 * np.cos(y * 70))
        soft = np.maximum(soft, y + 0.30)
        pm = mirror_x(p)
        pods = ellipsoid(pm, np.array([0.40, -0.28, 0.12]), np.array([0.13, 0.17, 0.15]))
        tubes = capsule(pm, np.array([0.36, -0.36, 0.22]), np.array([0.12, -0.50, 0.36]), 0.03) - \
            0.007 * np.abs(np.cos(pm[..., 0] * 80))
        return {'shell': shell, 'lens': lens, 'soft': np.minimum(soft, tubes), 'cowl': pods}

    def albedo(self, P, mat, alb):
        ring = band(P[:, 1], 0.40, 0.035)
        return np.where(mat == 0, alb + 0.25 * ring, alb)


class Abbot(Rlaan):
    """the elder's shell (Shizong, Xuanzong): a tall crown in rings, an onion's shape; a hood flaring wide at the
    sides; one great lens and two small below it; the speech an arc of lights under them"""
    view = {'span': 1.90, 'look': (0.0, 0.20, 0.0)}
    lenses = [(0.0, 0.14, 0.50, 0.105), (-0.16, -0.05, 0.48, 0.045), (0.16, -0.05, 0.48, 0.045)]
    lights = [(-0.53, 0.02), (-0.55, -0.08), (-0.56, -0.18), (0.56, -0.18), (0.55, -0.08), (0.53, 0.02)]
    lights_z = -1.0
    lights_mat = 'cowl'

    def solids(self, p):
        x, y, z = p[..., 0], p[..., 1], p[..., 2]
        # the crown: an onion, narrowing to a point, in rings
        q = p - np.array([0.0, 0.10, -0.04])
        r = np.hypot(q[..., 0], q[..., 2])
        prof = 0.50 * np.sqrt(np.clip(1 - ((q[..., 1] + 0.05) / 0.62) ** 2, 0, 1)) * \
            (1 - 0.55 * np.clip(q[..., 1] / 0.6, 0, 1) ** 2)
        crown = (r - prof) * 0.8
        crown = np.maximum(crown, np.abs(q[..., 1] + 0.05) - 0.62)
        rings = 0.012 * np.abs(np.sin(q[..., 1] * 26)) * (q[..., 1] > 0.12)
        crown = crown + rings
        face = ellipsoid(p, np.array([0.0, -0.02, 0.10]), np.array([0.38, 0.40, 0.42]))
        shell = smin(crown, face, 0.12)
        # the hood: wide flanges at the sides, curving forward
        hood = np.maximum(ellipsoid(p, np.array([0.0, -0.12, -0.10]), np.array([0.82, 0.52, 0.28])),
                          -ellipsoid(p, np.array([0.0, -0.10, 0.10]), np.array([0.66, 0.60, 0.40])))
        hood = np.maximum(hood, -(y + 0.55))
        lens = self.lens_solid(p)
        soft = np.minimum(ellipsoid(p, np.array([0, -0.72, -0.05]), np.array([0.70, 0.24, 0.60])),
                          np.hypot(x, z + 0.0) - 0.28 - 0.010 * np.cos(y * 70))
        soft = np.maximum(soft, y + 0.32)
        return {'shell': shell, 'lens': lens, 'soft': soft, 'cowl': hood}

    def albedo(self, P, mat, alb):
        rings = (np.sin(P[:, 1] * 26) > 0.6) * (P[:, 1] > 0.24)
        return np.where(mat == 0, alb + 0.18 * rings, alb)


class Bellows(Rlaan):
    """the hauler's shell (Zhuangzong, Taizong): a wide flat carapace like a beetle's, spotted; a beak of plates down
    the middle; a long lens either side of it; four ribbed breathing tubes hanging from under the beak; the speech
    lights along the beak's edge"""
    view = {'span': 1.80, 'look': (0.0, 0.10, 0.0)}
    lenses = []
    lights = [(-0.17, 0.04), (-0.15, -0.12), (-0.12, -0.27), (0.12, -0.27), (0.15, -0.12), (0.17, 0.04)]
    lights_z = 0.40
    slits = [(-0.30, 0.06), (0.30, 0.06)]

    def solids(self, p):
        x, y, z = p[..., 0], p[..., 1], p[..., 2]
        pm = mirror_x(p)
        cara = ellipsoid(p, np.array([0.0, 0.22, -0.06]), np.array([0.74, 0.42, 0.62]))
        head = ellipsoid(p, np.array([0.0, -0.06, 0.06]), np.array([0.48, 0.40, 0.44]))
        shell = smin(cara, head, 0.10)
        # the beak: three plates down the middle, each lapping the one below, to a point
        beak = np.minimum.reduce([
            rbox(p, np.array([0.0, 0.04, 0.44]), np.array([0.12, 0.10, 0.08]), 0.05),
            rbox(p, np.array([0.0, -0.12, 0.47]), np.array([0.10, 0.09, 0.08]), 0.05),
            np.maximum(rbox(p, np.array([0.0, -0.27, 0.48]), np.array([0.08, 0.09, 0.07]), 0.04),
                       np.abs(x) - 0.08 + (y + 0.36) * 0.4)])
        shell = smin(shell, beak, 0.02)
        # the lenses: long, set into the sides of the face
        lens = ellipsoid(pm, np.array([0.30, 0.06, 0.32]), np.array([0.13, 0.05, 0.08]))
        tubes = np.minimum.reduce([
            capsule(pm, np.array([0.13, -0.20, 0.42]), np.array([0.19, -0.62, 0.40]), 0.035),
            capsule(pm, np.array([0.26, -0.16, 0.36]), np.array([0.42, -0.56, 0.30]), 0.035)])
        tubes = tubes - 0.008 * np.abs(np.cos(y * 75))
        soft = np.minimum(tubes, ellipsoid(p, np.array([0, -0.76, -0.05]), np.array([0.80, 0.24, 0.62])))
        return {'shell': shell, 'lens': lens, 'soft': soft, 'cowl': np.full(x.shape, 1e3)}

    def albedo(self, P, mat, alb):
        # spots on the carapace
        x, y, z = P[:, 0], P[:, 1], P[:, 2]
        spots = np.zeros(len(P))
        for cx, cy in ((-0.48, 0.32), (0.48, 0.32), (-0.30, 0.46), (0.30, 0.46), (0.0, 0.52), (-0.58, 0.18),
                       (0.58, 0.18)):
            spots += np.exp(-((x - cx) ** 2 + (y - cy) ** 2) / 0.045 ** 2)
        return np.where(mat == 0, alb + 0.3 * np.clip(spots, 0, 1) * (z > 0.0), alb)

    def eye_glow(self, P, state):
        e = state.get('eyes', 'open')
        g = GLANCE.get(e, 0)
        out = np.zeros(len(P))
        for cx, cy in self.slits:
            u = (P[:, 0] - cx) / 0.12
            v = (P[:, 1] - cy) / 0.04
            inside = (u * u + v * v < 1) & (P[:, 2] > 0.3)
            line = band(v, 0, 0.35 if e != 'half' else 0.15, 0.2)
            glint = np.exp(-((u - 0.5 * g) ** 2) / 0.12)
            out += inside * EYE_LIGHT[e] * (0.6 * line + 1.0 * glint * line)
        return out


MODELS = {'hoplite': Hoplite, 'ephor': Ephor, 'phalanx': Phalanx, 'lantern': Lantern, 'abbot': Abbot,
          'bellows': Bellows}
