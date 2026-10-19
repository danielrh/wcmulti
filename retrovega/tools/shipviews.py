#!/usr/bin/env python3
"""Render a Vega Strike ship into RetroVega's 37 sprite views (WC1's layout).

    shipviews.py KEY OUTDIR [--size PX] [--livery NAME] [--assets DIR]

KEY is a row of data/ships.csv (key or name).  data/art.csv may give the
ship a stretch, an exposure, a whitening of its hull, paint rules and a
default livery; data/liveries.csv names the liveries.  Writes, with
TAG = KEY-LIVERY:

    OUTDIR/TAG_NN.png         one RGBA sprite per view (hard alpha)
    OUTDIR/TAG_paint_NN.png   where the livery is: 1 primary, 2 secondary
                              (those pixels go to the palette's livery ramps),
                              3 canopy glass
    OUTDIR/TAG_sheet.png      the views as a grid: rows top, +60, +30, 0,
                              -30, -60, bottom; columns headings 0..180
    OUTDIR/TAG_sheet_256.png  the same posterised (a preview of the palette)

The minimal renderer, headless on Mesa (EGL) through moderngl (docs/art.md):
the ship's own Vega Strike maps (diffuse, specular, glow); lights fixed to
the screen like a 1990s sprite set (a warm point light up and to the left,
so flat panels shade across, a dim fill, a rim); hemisphere ambient; an optional banded key light for a cel
look; livery paint from rules; engine glows at the exhaust points of Vega
Strike's units.csv (its Light column); a darkened silhouette edge and a
little depth-based cavity shading.
"""
import argparse
import csv
import math
import os
import sys

import moderngl
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bfxm  # noqa: E402

SS = 4  # supersampling per axis

# The 37 views: (elevation, heading) in degrees.  Elevation is the camera's
# height above the ship's wing plane; heading 0 = nose toward the viewer,
# 90 = profile with the nose to the LEFT, 180 = tail toward the viewer.  The
# headings 210..330 are drawn mirrored.  Top and bottom are drawn nose left.
BANDS = (60, 30, 0, -30, -60)
HEADINGS = (0, 30, 60, 90, 120, 150, 180)
VIEWS = [(90, 90)] + [(e, h) for e in BANDS for h in HEADINGS] + [(-90, 90)]

GLOW = {'terran': (255, 168, 72), 'aera': (96, 240, 200), 'rlaan': (240, 90, 220)}
GLASS = (14, 22, 52)  # canopy glass, dark navy, before its highlight and reflection
# The key light, the same for every ship so the sprites match: a point light
# well off the view axis (mostly from the left, somewhat above, a little in
# front),
# at three ship radii, so faces turned toward it are bright, faces turned
# away fall into shadow, and flat panels shade across.
KEY_DIR = (-0.85, 0.45, 0.27)
KEY_DISTANCE = 3.0
AMBIENT = 0.30     # how much light the shadow side keeps
DECAL_SIZE = 128   # each decal's cell in the atlas
MAX_DECALS = 4

VS = '''
#version 330
uniform mat4 mvp;
uniform mat3 rot;
in vec3 in_pos;
in vec3 in_norm;
in vec2 in_uv;
out vec3 v_n;
out vec2 v_uv;
out vec3 v_obj;
out vec3 v_view;
out vec3 v_on;
void main() {
    gl_Position = mvp * vec4(in_pos, 1.0);
    v_view = rot * in_pos;
    v_on = in_norm;
    v_n = rot * in_norm;
    v_uv = in_uv;
    v_obj = in_pos;
}
'''

# Paint rules arrive as uniforms: span (outer wings beyond a fraction of the
# half-span), spine (a stripe along the centre line narrower than a
# fraction), lum (the brightest panels: a percentile of the texels the mesh
# uses), hazard (alternating bands beyond a fraction of the half-span), fin
# (vertical surfaces beyond a fraction of the half-span: wingtip fins), band
# (a stripe across the wings between two fractions of the half-span), above
# (higher than a fraction of the ship's height), blue (texels of the
# diffuse map bluer than their red and green by a margin: a canopy the
# Vega Strike artist painted navy); each may be limited to the surfaces
# facing up (1) or down (-1) on the ship.  The glass rules pick the canopy,
# drawn dark blue and glossy whatever the livery.
FS = '''
#version 330
uniform sampler2D diffuse_map;
uniform sampler2D spec_map;
uniform sampler2D glow_map;
uniform sampler2D lum_map;
uniform vec3 primary;
uniform vec3 secondary;
uniform float paint_on;
uniform float half_span;
uniform float length_;
uniform float y_lo;
uniform float y_hi;
uniform vec3 glass_col;
uniform vec4 rule1;     // span, spine, lum, hazard for the primary colour
uniform vec4 rule1b;    // fin, band from, band to, above
uniform vec4 rule2;     // the same two for the secondary colour
uniform vec4 rule2b;
uniform vec4 side1;     // per term: 0 anywhere, 1 only facing up, -1 only facing down
uniform vec4 side1b;
uniform vec4 side2;
uniform vec4 side2b;
uniform vec4 rule3;     // the same for the glass
uniform vec4 rule3b;
uniform vec4 side3;
uniform vec4 side3b;
uniform vec4 rule1c;    // blue, unused x3, for each of the three
uniform vec4 rule2c;
uniform vec4 rule3c;
uniform vec4 side1c;
uniform vec4 side2c;
uniform vec4 side3c;
uniform float whiten;
uniform float detail_k;   // how much of the texture's fine detail shows
uniform float bump_k;     // how much that detail bends the light (bevels)
uniform float shine;      // the hull's least specular strength
uniform sampler2D decal_atlas;
uniform vec4 decal_pos[4];  // centre x, centre z, size (ship units), rotation (radians)
uniform vec4 decal_opt[4];  // atlas cell, side (0, 1 top, -1 bottom), mirrored, in use
uniform float exposure;
uniform float toon;
uniform vec3 key_pos;     // a point light, view space, ship units
uniform float ambient;
uniform vec3 fill_dir;
in vec3 v_n;
in vec2 v_uv;
in vec3 v_obj;
in vec3 v_view;
in vec3 v_on;
layout(location = 0) out vec4 frag;
layout(location = 1) out vec4 paint;
vec3 lin(vec3 c) { return pow(c, vec3(2.2)); }
float facing(float side, float ny) { return side == 0.0 ? 1.0 : step(0.2, side * ny); }
vec4 decal(float ny) {
    vec4 acc = vec4(0.0);
    for (int i = 0; i < 4; i++) {
        if (decal_opt[i].w < 0.5 || facing(decal_opt[i].y, ny) < 0.5) continue;
        float x = decal_opt[i].z > 0.5 ? abs(v_obj.x) : v_obj.x;
        vec2 d = vec2(x - decal_pos[i].x, v_obj.z - decal_pos[i].y) / decal_pos[i].z;
        float c = cos(decal_pos[i].w), s = sin(decal_pos[i].w);
        d = vec2(c * d.x - s * d.y, s * d.x + c * d.y);
        if (abs(d.x) > 0.5 || abs(d.y) > 0.5) continue;
        vec4 t = texture(decal_atlas, vec2((decal_opt[i].x + d.x + 0.5) / 4.0, 0.5 - d.y));
        acc = vec4(mix(acc.rgb, t.rgb, t.a), max(acc.a, t.a));
    }
    return acc;
}
float area(vec4 r, vec4 rb, vec4 rc, vec4 sd, vec4 sdb, vec4 sdc, float ny, float nx) {
    float x = abs(v_obj.x) / half_span;
    float p = 0.0;
    if (rc.x > 0.0) p = max(p, step(rc.x, texture(lum_map, v_uv).g) * facing(sdc.x, ny));
    if (rb.x > 0.0) p = max(p, step(rb.x, x) * step(0.5, nx) * facing(sdb.x, ny));
    if (rb.z > 0.0) p = max(p, step(rb.y, x) * step(x, rb.z) * facing(sdb.y, ny));
    if (rb.w > 0.0) p = max(p, step(rb.w, (v_obj.y - y_lo) / (y_hi - y_lo)) * facing(sdb.w, ny));
    if (r.x > 0.0) p = max(p, step(r.x, x) * facing(sd.x, ny));
    if (r.y > 0.0) p = max(p, step(x, r.y) * facing(sd.y, ny));
    if (r.z > 0.0) p = max(p, step(r.z, texture(lum_map, v_uv).r) * facing(sd.z, ny));
    if (r.w > 0.0) p = max(p, step(r.w, x) * step(0.5, fract(v_obj.z / length_ * 9.0)) * facing(sd.w, ny));
    return p;
}
void main() {
    vec3 n = normalize(v_n);
    if (!gl_FrontFacing) n = -n;
    vec3 v = vec3(0.0, 0.0, 1.0);
    // the texture's fine detail (panel lines, vents, grime) as a height around 0
    float hd = texture(lum_map, v_uv).b - 0.5;
    // ... bending the normal, so panel edges catch the light like bevels
    n = normalize(n - vec3(dFdx(hd), dFdy(hd), 0.0) * bump_k * 3.0);
    float detail = 1.0 + detail_k * 1.4 * hd;
    vec3 albedo = lin(texture(diffuse_map, v_uv).rgb);
    albedo = mix(albedo, vec3(0.62), whiten);
    vec3 on = normalize(v_on) * (gl_FrontFacing ? 1.0 : -1.0);
    vec4 dec = decal(on.y);
    float p1 = area(rule1, rule1b, rule1c, side1, side1b, side1c, on.y, abs(on.x)) * paint_on * (1.0 - dec.a);
    float p2 = area(rule2, rule2b, rule2c, side2, side2b, side2c, on.y, abs(on.x)) * paint_on * (1.0 - dec.a);
    albedo = mix(albedo, primary, p1);
    albedo = mix(albedo, secondary, p2);
    albedo = mix(albedo, lin(dec.rgb), dec.a);
    albedo *= detail;
    float g = area(rule3, rule3b, rule3c, side3, side3b, side3c, on.y, abs(on.x));
    albedo = mix(albedo, glass_col, g);
    vec3 specc = lin(texture(spec_map, v_uv).rgb);
    specc = max(specc, vec3(shine));
    float shin = mix(7.0, 60.0, clamp(dot(specc, vec3(0.3, 0.6, 0.1)) * 1.5 - shine, 0.0, 1.0));
    vec3 sky = vec3(0.60, 0.62, 0.68), ground = vec3(0.10, 0.10, 0.11);
    vec3 amb = mix(ground, sky, n.y * 0.5 + 0.5) * ambient;
    vec3 key_dir = normalize(key_pos - v_view);
    float kd = max(dot(n, key_dir), 0.0);
    if (toon > 0.0) {
        float b = kd * toon;
        kd = (floor(b) + smoothstep(0.35, 0.65, fract(b))) / toon;
    }
    float fd = max(dot(n, fill_dir), 0.0);
    vec3 col = albedo * (amb + kd * vec3(1.06, 1.0, 0.90) * 1.9 + fd * vec3(0.45, 0.48, 0.58) * 0.3);
    float matte = (1.0 - 0.85 * max(p1, p2)) * (1.0 - g);
    vec3 h = normalize(key_dir + v);
    col += specc * pow(max(dot(n, h), 0.0), shin) * 2.2 * matte;
    vec3 r = reflect(-v, n);
    col += specc * mix(ground, sky, r.y * 0.5 + 0.5) * 0.3 * matte;
    col += vec3(0.40, 0.44, 0.52) * pow(1.0 - max(n.z, 0.0), 3.0) * 0.3 * matte;
    // glass: a sharp highlight and a dark sky reflected in it
    col += g * (vec3(0.9) * pow(max(dot(n, h), 0.0), 60.0)
                + mix(vec3(0.01, 0.015, 0.03), vec3(0.16, 0.24, 0.42), smoothstep(-0.2, 0.8, r.y)) * 0.4);
    col += lin(texture(glow_map, v_uv).rgb);
    col *= exposure;
    col = col / (1.0 + 0.12 * col);
    frag = vec4(pow(clamp(col, 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);
    paint = vec4(p1 > 0.5 ? 1.0 : 0.0, p2 > 0.5 ? 1.0 : 0.0, g > 0.5 ? 1.0 : 0.0, 1.0);
}
'''


def table_row(name, key, column='key'):
    for row in csv.DictReader(open(os.path.join(HERE, '..', 'data', name))):
        if key.lower() == row[column].lower():
            return row
    return None


def ship_row(key):
    for row in csv.DictReader(open(os.path.join(HERE, '..', 'data', 'ships.csv'))):
        if key.lower() in (row['key'].lower(), row['name'].lower()):
            return row
    sys.exit('no ship %r in data/ships.csv' % key)


def linear(text):
    if not text.strip():
        return (0.0, 0.0, 0.0)
    return tuple((int(v) / 255.0) ** 2.2 for v in text.split())


def unit_lights(assets, unit):
    """Exhaust points (x, y, z, size) from Vega Strike's units.csv Light column."""
    rows = csv.reader(open(os.path.join(assets, 'units', 'units.csv'), newline='', encoding='utf-8', errors='replace'))
    header = next(rows)
    col = header.index('Light')
    for r in rows:
        if r and r[0] == unit:
            out = []
            for item in r[col].split('}'):
                f = item.strip('{').split(';')
                try:
                    out.append(tuple(float(v) for v in f[1:5]))
                except (ValueError, IndexError):
                    pass
            return [o for o in out if len(o) == 4]
    return []


def load_image(path, fallback):
    if path and os.path.exists(path):
        return Image.open(path).convert('RGB')
    return Image.new('RGB', (4, 4), fallback)


def make_texture(ctx, img, components=3):
    tex = ctx.texture(img.size, components, img.tobytes())
    tex.build_mipmaps()
    tex.filter = (moderngl.LINEAR_MIPMAP_LINEAR, moderngl.LINEAR)
    tex.repeat_x = tex.repeat_y = True
    return tex


def used_texels(size, tri_uv):
    """Mask of the texels the mesh's triangles cover (texture space)."""
    cov = Image.new('L', size, 0)
    d = ImageDraw.Draw(cov)
    for t in tri_uv:
        d.polygon([(u * size[0], v * size[1]) for u, v in t], fill=255)
    return np.asarray(cov) > 0


def lum_map(diffuse, tri_uv):
    """Two masks of the diffuse map for the paint rules, as an RGB image: in
    red its luminance, smoothed, as percentiles of the texels the mesh uses
    (0..1), so 'lum>0.8' means its brightest fifth of panels; in green its
    blueness, blue minus the larger of red and green (0..1); in blue its fine
    detail (panel lines, vents, grime) as a height around 0.5.  Also the
    median linear luminance."""
    a = np.asarray(diffuse, dtype=np.float32) / 255
    lum = a @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    used = used_texels(diffuse.size, tri_uv)
    smooth = ndimage.gaussian_filter(lum, diffuse.size[0] / 200.0)
    ranks = np.searchsorted(np.sort(smooth[used]), smooth) / max(used.sum(), 1)
    ref = float(np.median((a ** 2.2)[used] @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)))
    blue = ndimage.gaussian_filter(a[..., 2] - np.maximum(a[..., 0], a[..., 1]), 1.0)
    # fine detail: the luminance against its own blur, +-0.15 mapped to 0..1
    fine = lum - ndimage.gaussian_filter(lum, diffuse.size[0] / 128.0)
    height = 0.5 + 0.5 * np.clip(fine / 0.15, -1, 1)
    rgb = np.dstack([np.clip(ranks, 0, 1), np.clip(blue, 0, 1), height])
    return Image.fromarray((rgb * 255).astype(np.uint8), 'RGB'), max(ref, 0.02)


def rule_vec(rule):
    """'fin>0.95|band:0.55-0.7|spine<0.07@bottom' -> the shader's three
    parameter vectors and three side vectors.  Terms: span>F, spine<F,
    lum>P, hazard>F, fin>F, band:A-B, above>F, blue>D; one ending in @top or
    @bottom paints only the surfaces facing that way on the ship."""
    v, side = [0.0] * 12, [0.0] * 12
    for r in filter(None, (rule or '').split('|')):
        r, _, where = r.partition('@')
        w = {'': 0.0, 'top': 1.0, 'bottom': -1.0}[where]
        if r.startswith('band:'):
            lo, hi = r[5:].split('-')
            v[5], v[6], side[5] = float(lo), float(hi), w
            continue
        for name, i in (('span>', 0), ('spine<', 1), ('lum>', 2), ('hazard>', 3), ('fin>', 4), ('above>', 7), ('blue>', 8)):
            if r.startswith(name):
                v[i], side[i] = float(r[len(name):]), w
                break
        else:
            sys.exit('unknown paint term %r' % r)
    # side[5] is the band's; the shader reads it as the second side of the b vector
    sb = (side[4], side[5], 0.0, side[7])
    return tuple(v[:4]), tuple(v[4:8]), tuple(v[8:]), tuple(side[:4]), sb, tuple(side[8:])


def decal_image(name):
    """One decal, drawn here (RGBA, DECAL_SIZE square, its top toward the nose)."""
    n = DECAL_SIZE
    img = Image.new('RGBA', (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    if name == 'arrows':      # two red chevrons pointing forward
        for y0 in (int(n * 0.12), int(n * 0.46)):
            d.polygon([(n * 0.5, y0), (n * 0.92, y0 + n * 0.34), (n * 0.74, y0 + n * 0.42),
                       (n * 0.5, y0 + n * 0.2), (n * 0.26, y0 + n * 0.42), (n * 0.08, y0 + n * 0.34)],
                      fill=(214, 28, 24, 255))
    elif name == 'roundel':   # a navy disc, a white ring, a gold star
        d.ellipse((4, 4, n - 4, n - 4), fill=(24, 40, 120, 255))
        d.ellipse((n * 0.16, n * 0.16, n * 0.84, n * 0.84), fill=(236, 236, 236, 255))
        d.ellipse((n * 0.24, n * 0.24, n * 0.76, n * 0.76), fill=(24, 40, 120, 255))
        c, r1, r2 = n / 2, n * 0.24, n * 0.09
        pts = []
        for k in range(10):
            a = math.pi / 2 + k * math.pi / 5
            r = r1 if k % 2 == 0 else r2
            pts.append((c + r * math.cos(a), c - r * math.sin(a)))
        d.polygon(pts, fill=(240, 196, 40, 255))
    elif name == 'hazard':    # yellow and black diagonal bands
        for k in range(-4, 8):
            x = k * n / 4
            d.polygon([(x, n), (x + n / 8, n), (x + n / 8 + n, 0), (x + n, 0)], fill=(240, 200, 30, 255))
            d.polygon([(x + n / 8, n), (x + n / 4, n), (x + n / 4 + n, 0), (x + n / 8 + n, 0)], fill=(24, 24, 28, 255))
    else:
        sys.exit('unknown decal %r' % name)
    return img


def decal_uniforms(spec, pos):
    """The decal atlas and uniforms from a spec such as
    'arrows:0 0.55 0.15 0@top|roundel:0.8 0.15 0.1 0@top~m': name:x z size rot
    with x a signed fraction of the half-span, z a fraction of the length
    from the tail, size a fraction of the length, rot in degrees; @top or
    @bottom to put it on one side; ~m to mirror it onto both wings."""
    atlas = Image.new('RGBA', (DECAL_SIZE * MAX_DECALS, DECAL_SIZE), (0, 0, 0, 0))
    hs = float(np.max(np.abs(pos[:, 0])))
    zlo, length = float(pos[:, 2].min()), float(pos[:, 2].max() - pos[:, 2].min())
    upos, uopt = [(0.0, 0.0, 1.0, 0.0)] * MAX_DECALS, [(0.0, 0.0, 0.0, 0.0)] * MAX_DECALS
    for i, item in enumerate(filter(None, (spec or '').split('|'))):
        if i >= MAX_DECALS:
            sys.exit('at most %d decals' % MAX_DECALS)
        mirror = item.endswith('~m')
        item = item[:-2] if mirror else item
        item, _, where = item.partition('@')
        name, _, args = item.partition(':')
        x, z, size, rot = (float(v) for v in args.split())
        atlas.paste(decal_image(name), (i * DECAL_SIZE, 0))
        upos[i] = (x * hs, zlo + z * length, size * length, math.radians(rot))
        uopt[i] = (float(i), {'': 0.0, 'top': 1.0, 'bottom': -1.0}[where], 1.0 if mirror else 0.0, 1.0)
    return atlas, upos, uopt


def view_matrix(elev, head):
    """Rotation (3x3) from ship space to view space for a view, camera on +z."""
    e, h = math.radians(elev), math.radians(head)
    d = np.array([math.sin(h) * math.cos(e), math.sin(e), math.cos(h) * math.cos(e)])
    if abs(elev) == 90:
        up = np.array([-1.0, 0.0, 0.0]) if elev > 0 else np.array([1.0, 0.0, 0.0])
    else:
        up = np.array([0.0, 1.0, 0.0])
    f = -d
    s = np.cross(f, up); s /= np.linalg.norm(s)
    u = np.cross(s, f)
    return np.array([s, u, -f])


def render(row, outdir, size, assets, livery_name=None, key=KEY_DIR, ambient=AMBIENT, shine=None, tag_suffix=''):
    art = table_row('art.csv', row['key']) or {}
    livery_name = livery_name or art.get('livery') or 'none'
    livery = table_row('liveries.csv', livery_name, 'name')
    if livery is None:
        sys.exit('no livery %r in data/liveries.csv' % livery_name)
    stretch = np.array([float(v) for v in (art.get('stretch') or '1 1 1').split()])
    unit_dir = os.path.join(assets, 'units', 'vessels', os.path.dirname(row['mesh']))
    parts = bfxm.parts(os.path.join(assets, 'units', 'vessels', row['mesh']))
    for m in parts:
        m.positions = m.positions * stretch
        n = m.normals / stretch
        m.normals = n / np.maximum(np.linalg.norm(n, axis=1), 1e-9)[:, None]
    allpos = np.concatenate([m.positions for m in parts])
    center = (allpos.min(0) + allpos.max(0)) / 2
    radius = float(np.max(np.linalg.norm(allpos - center, axis=1))) * 1.04
    lights = [(np.array(l[:3]) * stretch, l[3]) for l in unit_lights(assets, row['vs_unit'])]

    ctx = moderngl.create_standalone_context(backend='egl')
    prog = ctx.program(vertex_shader=VS, fragment_shader=FS)
    for name, unit in (('diffuse_map', 0), ('spec_map', 1), ('glow_map', 2), ('lum_map', 3)):
        prog[name].value = unit
    # the key is a point light up and to the left, near enough that flat
    # panels shade from bright to dark across the hull
    kd = np.array(key) / np.linalg.norm(key)
    prog['key_pos'].value = tuple(kd * KEY_DISTANCE * radius)
    prog['ambient'].value = ambient
    fill = np.array([0.7, -0.35, 0.45]); fill /= np.linalg.norm(fill)
    prog['fill_dir'].value = tuple(fill)
    prog['exposure'].value = float(art.get('exposure') or 1)
    prog['toon'].value = float(art.get('toon') or 0)
    prog['whiten'].value = float(art.get('whiten') or 0)
    prog['detail_k'].value = float(art.get('detail') or 0.6)
    prog['bump_k'].value = float(art.get('bump') or 1.0)
    prog['shine'].value = shine if shine is not None else float(art.get('shine') or 0.3)
    prog['decal_atlas'].value = 4
    atlas, pos, opt = decal_uniforms(art.get('decals'), allpos - center)
    decal_tex = make_texture(ctx, atlas, 4)
    prog['decal_pos'].value = pos
    prog['decal_opt'].value = opt
    prog['primary'].value = linear(livery['primary'])
    prog['secondary'].value = linear(livery['secondary'])
    prog['paint_on'].value = 1.0 if livery['primary'].strip() else 0.0
    prog['half_span'].value = float(np.max(np.abs(allpos[:, 0] - center[0])))
    prog['length_'].value = float(allpos[:, 2].max() - allpos[:, 2].min())
    prog['y_lo'].value = float(allpos[:, 1].min() - center[1])
    prog['y_hi'].value = float(allpos[:, 1].max() - center[1])
    prog['glass_col'].value = tuple((c / 255.0) ** 2.2 for c in GLASS)
    for n, rule in (('1', art.get('paint')), ('2', art.get('paint2')), ('3', art.get('glass'))):
        a, b, c, sa, sb, sc = rule_vec(rule)
        prog['rule' + n].value, prog['rule%sb' % n].value, prog['rule%sc' % n].value = a, b, c
        prog['side' + n].value, prog['side%sb' % n].value, prog['side%sc' % n].value = sa, sb, sc

    drawables = []
    for m in parts:
        corners = m.tri_index.reshape(-1)
        data = np.hstack([m.positions[corners] - center, m.normals[corners],
                          m.tri_uv.reshape(-1, 2)]).astype('f4')
        vao = ctx.vertex_array(prog, [(ctx.buffer(data.tobytes()), '3f 3f 2f', 'in_pos', 'in_norm', 'in_uv')])
        imgs = {}
        for idx, fallback in ((0, (200, 200, 200)), (1, (40, 40, 40)), (3, (0, 0, 0))):
            names = [n for t, n in m.textures.get(idx, []) if t == 2]
            imgs[idx] = load_image(os.path.join(unit_dir, names[0]) if names else None, fallback)
        lum, ref = lum_map(imgs[0], m.tri_uv)
        tex = {i: make_texture(ctx, im) for i, im in imgs.items()}
        tex['lum'] = make_texture(ctx, lum)
        drawables.append((vao, tex, ref))

    big = size * SS
    color, paint = ctx.texture((big, big), 4), ctx.texture((big, big), 4)
    depth = ctx.depth_texture((big, big))
    fbo = ctx.framebuffer(color_attachments=[color, paint], depth_attachment=depth)

    def grab(tex):
        return np.frombuffer(tex.read(), dtype=np.uint8).reshape(big, big, 4)[::-1].astype(np.float32) / 255

    views, paints = [], []
    glow = GLOW.get(row['faction'], (255, 200, 120))
    for elev, head in VIEWS:
        rot = view_matrix(elev, head)
        proj = np.diag([1 / radius, 1 / radius, -1 / radius, 1.0])
        mvp = (proj @ np.vstack([np.hstack([rot, np.zeros((3, 1))]), [0, 0, 0, 1]])).astype('f4')
        prog['mvp'].write(mvp.T.tobytes())
        prog['rot'].write(rot.astype('f4').T.tobytes())
        fbo.use()
        fbo.clear(0, 0, 0, 0, depth=1.0)
        ctx.enable(moderngl.DEPTH_TEST)
        ctx.disable(moderngl.CULL_FACE)
        for vao, tex, ref in drawables:
            tex[0].use(0); tex[1].use(1); tex[3].use(2); tex['lum'].use(3); decal_tex.use(4)
            vao.render(moderngl.TRIANGLES)
        z = np.frombuffer(depth.read(), dtype=np.float32).reshape(big, big)[::-1]
        facing = (rot @ np.array([0.0, 0.0, -1.0]))[2]
        engines = [(rot @ (pos - center) / radius, sz / radius, facing) for pos, sz in lights]
        img, pm = post(grab(color), grab(paint), z, size, engines, glow)
        views.append(img)
        paints.append(pm)

    os.makedirs(outdir, exist_ok=True)
    tag = '%s-%s%s' % (row['key'], livery_name, tag_suffix)
    for vi, (img, pm) in enumerate(zip(views, paints)):
        img.save(os.path.join(outdir, '%s_%02d.png' % (tag, vi)))
        pm.save(os.path.join(outdir, '%s_paint_%02d.png' % (tag, vi)))
    sheet = make_sheet(views, size)
    sheet.save(os.path.join(outdir, '%s_sheet.png' % tag))
    flat = Image.new('RGB', sheet.size, (0, 0, 0))
    flat.paste(sheet, mask=sheet.split()[3])
    poster = flat.quantize(colors=48, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).convert('RGB')
    poster.putalpha(sheet.split()[3])
    poster.save(os.path.join(outdir, '%s_sheet_256.png' % tag))
    return radius


def post(rgba, paint, z, size, engines, glow):
    """Supersampled render -> one sprite view (RGBA, hard alpha) and its paint mask."""
    cover = rgba[..., 3] > 0
    zf = np.where(cover, z, 1.0)
    # cavity: darken a little where the surface lies behind its neighbourhood
    zb = ndimage.gaussian_filter(zf, sigma=SS * 2.0)
    rgb = rgba[..., :3] * (1.0 - np.clip((zf - zb) * 40.0, 0.0, 0.25))[..., None]
    h = w = size

    def down(a):
        return a.reshape(h, SS, w, SS, *a.shape[2:]).mean(axis=(1, 3))

    a = down(cover.astype(np.float32))
    rgb = down(rgb) / np.maximum(a, 1e-6)[..., None]
    p1 = down(paint[..., 0]) / np.maximum(a, 1e-6)
    p2 = down(paint[..., 1]) / np.maximum(a, 1e-6)
    p3 = down(paint[..., 2]) / np.maximum(a, 1e-6)
    zmin = zf.reshape(h, SS, w, SS).min(axis=(1, 3))
    solid = a >= 0.4
    # a darkened silhouette edge, in the hull's own colour
    edge = solid & ~ndimage.binary_erosion(solid)
    rgb = np.where(edge[..., None], rgb * 0.55, rgb)
    # engine glows where the exhaust faces the viewer and is not hidden
    yy, xx = np.mgrid[0:h, 0:w]
    for q, sz, facing in engines:
        if facing < -0.3:
            continue
        px, py = (q[0] + 1) * w / 2, (1 - q[1]) * h / 2
        ix, iy = int(round(px)), int(round(py))
        if 0 <= ix < w and 0 <= iy < h and solid[iy, ix] and (1 - q[2]) / 2 > zmin[iy, ix] + 0.03:
            continue
        r = max(1.5, min(sz * 0.3, 0.09) * w / 2) * (0.6 + 0.4 * max(facing, 0.0))
        d = np.hypot(xx - px, yy - py) / r
        g = np.clip(1.0 - d, 0.0, 1.0) ** 1.5 * min(1.0, 0.35 + 0.8 * max(facing, 0.0))
        core = np.clip(1.0 - d * 2.0, 0.0, 1.0)
        add = g[..., None] * np.array(glow) / 255 + core[..., None] * 0.6
        rgb = np.where(solid[..., None], rgb + add, add + rgb * solid[..., None])
        solid = solid | (g > 0.3)
    out = np.dstack([np.clip(rgb * 255, 0, 255), solid * 255.0]).astype(np.uint8)
    pm = np.where(solid & (p3 >= 0.5), 3, np.where(solid & (p2 >= 0.5), 2, np.where(solid & (p1 >= 0.5), 1, 0))).astype(np.uint8)
    return Image.fromarray(out, 'RGBA'), Image.fromarray(pm, 'L')


def make_sheet(views, size):
    rows = [[views[0]]] + [views[1 + 7 * b:8 + 7 * b] for b in range(5)] + [[views[36]]]
    pad = 4
    sheet = Image.new('RGBA', (7 * (size + pad), 7 * (size + pad)), (0, 0, 0, 0))
    for r, row in enumerate(rows):
        for c, img in enumerate(row):
            sheet.paste(img, ((3 if len(row) == 1 else c) * (size + pad), r * (size + pad)))
    return sheet


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('ship')
    ap.add_argument('outdir')
    ap.add_argument('--size', type=int, default=0, help='view size in pixels (default: lod0_px)')
    ap.add_argument('--livery', default=None, help='a name in data/liveries.csv (default: data/art.csv)')
    ap.add_argument('--assets', default=os.path.expanduser('~/dev/vegastrike-assets'))
    ap.add_argument('--key', default=None, help='try another key light direction: x,y,z in view space')
    ap.add_argument('--ambient', type=float, default=AMBIENT, help='try another ambient level')
    ap.add_argument('--shine', type=float, default=None, help="try another shine (overrides data/art.csv)")
    ap.add_argument('--tag', default='', help='a suffix for the output names (trials)')
    args = ap.parse_args()
    row = ship_row(args.ship)
    if not row['mesh']:
        sys.exit('%s has no mesh yet' % row['name'])
    size = args.size or int(row['lod0_px'])
    key = tuple(float(v) for v in args.key.split(',')) if args.key else KEY_DIR
    r = render(row, args.outdir, size, args.assets, args.livery, key, args.ambient, args.shine, args.tag)
    print('%s: 37 views at %d px, radius %.2f' % (row['name'], size, r))


if __name__ == '__main__':
    main()
