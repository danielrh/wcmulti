#!/usr/bin/env python3
"""The game's pictures for DOS: RV.DAT and its C header (docs/engine.md).

    rvpack.py [--out build/dos/RV.DAT] [--header src/rvasset.h] [--preview DIR]

Takes the built rooms (art/places/KEY/, tools/places.py), the room data
(data/places.json: hotspots and doors), the cast's talking faces
(art/people/faces/KEY/) and the Roboto font, and writes one archive of
8-bit assets for the engine, with the records that tie them together:

    palettes   (6-bit VGA values, as the DAC takes them) index 0-15 are the UI's own colours in every palette (text,
               panels, the pointer); a room's view uses 16-255 for itself and
               its sprites.  A close-up is a room's dimmed backdrop in 16-63
               plus a person's face in 64-255, put together at run time from
               two palette parts.  Each palette carries the tables DOS cannot
               afford to compute: a 50% shade of every colour, and every
               colour mixed with the three text colours at three strengths.
    images     opaque, one byte a pixel, rows top down
    sprites    cut-outs as run-length rows: per row (skip, count, count
               pixels)... ending (0, 0); a row table of 32-bit offsets first
    fonts      glyphs as 0-3 coverage per pixel, from Orbitron (art/fonts)
    rooms      a fixed record per room: its palette and pictures, hotspots
               with what they do, the seat poses, the actor's frames, the
               briefing's speaker and crowd, the deck's fighters and Kit
    people     a record per person: palette part, face, mouths, eyes

Things the engine would otherwise compose at run time are composed here:
the deck as one plate per fighter (shadow, reflection, legs and ship are
soft-edged), the faces scaled to their close-up size.  XMS has room.
"""
import argparse
import csv
import json
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import places  # noqa: E402

VERSION = 1
SCREEN = (640, 400)
ROOMS = ['bar', 'lounge', 'briefing', 'quarters', 'locker', 'deck']
FACE = 352                      # close-up face size (the 256 px faces scaled 1.375)
# Orbitron (SIL OFL 1.1, art/fonts/), the user's pick for the futuristic look:
# bold for body text, black for titles.  (file, pixels, weight)
FONT_FILES = {'body': ('Orbitron.ttf', 13, 700), 'title': ('Orbitron.ttf', 22, 900)}
FONT_DIR = os.path.join(ROOT, 'art', 'fonts')

UI = [(0, 0, 0), (28, 28, 30), (60, 60, 64), (100, 100, 104), (140, 140, 144), (180, 180, 184),
      (220, 220, 222), (255, 255, 255), (96, 58, 12), (168, 108, 28), (222, 160, 60), (252, 214, 124),
      (12, 18, 30), (52, 70, 98), (120, 216, 255), (210, 44, 32)]
UI_NAMES = ['BLACK', 'GREY1', 'GREY2', 'GREY3', 'GREY4', 'GREY5', 'GREY6', 'WHITE', 'AMBER1', 'AMBER2',
            'AMBER3', 'AMBER4', 'PANEL', 'PANEL_EDGE', 'HILITE', 'RED']
TEXT_COLOURS = [7, 11, 5]       # white, amber, grey: the mix tables' order
KIND = {'PAL': 1, 'PALPART': 2, 'IMG': 3, 'SPR': 4, 'FONT': 5}
ACTIONS = ['NONE', 'GOTO', 'BUNK', 'LOG', 'SCORES', 'SIMULATOR', 'CLOSEUP', 'CUSTOMIZE', 'LAUNCH', 'MEDALS',
           'LETTERS', 'PHOTOS', 'KILLS']
LIMITS = {'HOTS': 8, 'POSES': 12, 'IDLE': 16, 'DECK_SHIPS': 8, 'CLIPS': 3, 'PEOPLE': 16, 'ACTOR_FRAMES': 12}


class Pack:
    def __init__(self):
        self.assets = []         # (name, kind, w, h, x, y, payload)

    def add(self, name, kind, payload, w=0, h=0, x=0, y=0):
        if len(name) >= 22:
            sys.exit('asset name too long: %s' % name)
        self.assets.append((name, KIND[kind], w, h, x, y, payload))
        return len(self.assets) - 1


# --- colour ----------------------------------------------------------------

def nearest(pal, rgb, allowed):
    """Index in `allowed` of the palette colour nearest each rgb (N,3)."""
    p = np.array([pal[i] for i in allowed], np.float32)
    d = ((rgb[:, None, :].astype(np.float32) - p[None]) ** 2).sum(-1)
    return np.array(allowed)[np.argmin(d, axis=1)]


def mix_tables(pal, indices):
    """shade[256]: each colour at half brightness; mix[3][3][256]: each colour
    mixed 25/50/75% with the text colours.  Only `indices` are searched."""
    rgb = np.array(pal, np.float32)
    shade = nearest(pal, rgb * 0.5, indices)
    mix = []
    for t in TEXT_COLOURS:
        for a in (0.25, 0.5, 0.75):
            mix.append(nearest(pal, rgb * (1 - a) + np.array(pal[t], np.float32) * a, indices))
    return bytes(shade.astype(np.uint8)) + b''.join(bytes(m.astype(np.uint8)) for m in mix)


def vga(pal):
    return bytes(int(round(c * 63 / 255)) for rgb in pal for c in rgb)


def make_palette(images, count, first):
    """count colours for these RGB(A) images (median cut), as a list."""
    parts = []
    for im in images:
        a = np.asarray(im.convert('RGBA'))
        px = a[a[..., 3] >= 128][:, :3]
        if len(px):
            parts.append(px)
    px = np.concatenate(parts)
    side = int(np.ceil(np.sqrt(len(px))))
    flat = np.zeros((side * side, 3), np.uint8)
    flat[:len(px)] = px
    flat[len(px):] = px[0]
    q = Image.fromarray(flat.reshape(side, side, 3)).quantize(count, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = q.getpalette()[:count * 3]
    pal += pal[-3:] * (count - len(pal) // 3)
    return [tuple(pal[i * 3:i * 3 + 3]) for i in range(count)]


class Palette:
    def __init__(self, colours):
        self.colours = colours                      # 256 RGB
        self.img = Image.new('P', (1, 1))
        flat = [c for rgb in colours for c in rgb]
        self.img.putpalette(flat)

    def index(self, im, allowed=None):
        """Indices of an image's RGB in this palette (nearest, no dither)."""
        rgb = im.convert('RGB')
        if allowed is None:
            return np.asarray(rgb.quantize(palette=self.img, dither=Image.Dither.NONE))
        a = np.asarray(rgb).reshape(-1, 3)
        uniq, inv = np.unique(a, axis=0, return_inverse=True)
        return nearest(self.colours, uniq, allowed)[inv.reshape(-1)].reshape(rgb.size[1], rgb.size[0]).astype(np.uint8)


# --- encodings -----------------------------------------------------------------

def sprite_payload(idx, alpha):
    """Rows of (skip, count, pixels) ending (0, 0), after a table of h+1
    32-bit row offsets (from the start of the row data)."""
    h, w = idx.shape
    rows, offs, at = [], [], 0
    for y in range(h):
        out = bytearray()
        x = 0
        on = alpha[y]
        while x < w:
            skip = 0
            while x < w and not on[x]:
                x += 1
                skip += 1
            if x >= w:
                break
            n = 0
            while x + n < w and on[x + n] and n < 255:
                n += 1
            while skip > 255:
                out += bytes((255, 0))
                skip -= 255
            out += bytes((skip, n)) + bytes(idx[y, x:x + n])
            x += n
        out += b'\0\0'
        offs.append(at)
        rows.append(bytes(out))
        at += len(out)
    offs.append(at)
    return struct.pack('<%dL' % (h + 1), *offs) + b''.join(rows)


def add_sprite(pack, name, im, pal, x, y, allowed=None):
    a = np.asarray(im.convert('RGBA'))
    alpha = a[..., 3] >= 128
    ys, xs = np.nonzero(alpha)
    if not len(ys):
        sys.exit('%s: an empty sprite' % name)
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    crop = im.convert('RGBA').crop((x0, y0, x1, y1))
    idx = pal.index(crop, allowed)
    return pack.add(name, 'SPR', sprite_payload(idx, alpha[y0:y1, x0:x1]), x1 - x0, y1 - y0, x + x0, y + y0)


def add_image(pack, name, im, pal, x=0, y=0, allowed=None):
    idx = pal.index(im, allowed)
    return pack.add(name, 'IMG', bytes(idx.astype(np.uint8).reshape(-1)), im.size[0], im.size[1], x, y)


def add_palette(pack, name, pal, search):
    return pack.add(name, 'PAL', vga(pal.colours) + mix_tables(pal.colours, search))


# --- rooms -----------------------------------------------------------------

def room_palette(images):
    room = make_palette(images, 240, 16)
    return Palette(UI + room)


def hotspots(key, cfg, frame):
    out = []
    for name, h in cfg.get('hotspots', {}).items():
        x0, y0 = frame.xy(h['rect'][0], h['rect'][1])
        x1, y1 = frame.xy(h['rect'][2], h['rect'][3])
        x0, y0 = max(0, round(x0)), max(0, round(y0))
        x1, y1 = min(SCREEN[0], round(x1)), min(SCREEN[1], round(y1))
        does = h['does']
        act, arg = (does.split(':') + [''])[:2]
        action = ACTIONS.index(act.upper())
        target = ROOMS.index(arg) if act == 'goto' else 0
        out.append(struct.pack('<4hBB26s', x0, y0, x1 - x0, y1 - y0, action, target,
                               h.get('label', name).encode('cp437')[:25]))
    if len(out) > LIMITS['HOTS']:
        sys.exit('%s: %d hotspots, at most %d' % (key, len(out), LIMITS['HOTS']))
    return out


def closeup_backdrop(back):
    """The room as it shows behind a close-up: enlarged, blurred, dimmed."""
    big = back.convert('RGB').resize((680, 425), Image.LANCZOS)
    big = big.filter(ImageFilter.GaussianBlur(5))
    big = Image.eval(big, lambda v: int(v * 0.55))
    return big.crop((20, 12, 660, 412))


def pack_room(pack, key, people_idx, cfg, place, d):
    frame = places.Frame(cfg['crop'])
    back = Image.open(os.path.join(d, place['back']))
    sprites, seats = [], []
    for seat, pilots in (place.get('seats') or {}).items():
        for pilot, poses in pilots.items():
            for pose, s in poses.items():
                sprites.append((seat, pilot, pose, Image.open(os.path.join(d, s['file'])), s['x'], s['y']))
    actor_frames = []
    for name, a in (place.get('actors') or {}).items():
        at = Image.open(os.path.join(d, a['atlas']))
        x, y, w, h = a['rect']
        for k, f in enumerate(a['frames']):
            c = a['columns']
            actor_frames.append((name, f, at.crop(((k % c) * w, (k // c) * h, (k % c + 1) * w, (k // c + 1) * h))))
    extra = []
    if key == 'briefing':
        for st in place['stages']['poses'].values():
            if st:
                extra.append(Image.open(os.path.join(d, st['file'])))
        extra.append(Image.open(os.path.join(d, place['speaker']['atlas'])))
    pal = room_palette([back] + [s[3] for s in sprites] + [f[2] for f in actor_frames] + extra)
    rec = {'palette': add_palette(pack, 'pal_' + key, pal, list(range(256))),
           'back': add_image(pack, 'back_' + key, back, pal)}
    poses = []
    by_pilot = {}
    for seat, pilot, pose, im, x, y in sprites:
        i = add_sprite(pack, '%s_%s_%s' % (key[:3], pilot[:7], pose[:4]), im, pal, x, y)
        by_pilot.setdefault((seat, pilot), {})[pose] = i
    for (seat, pilot), ps in by_pilot.items():
        poses.append(struct.pack('<BBHH', ['left', 'middle', 'right'].index(seat), people_idx[pilot],
                                 ps['talk'], ps.get('face', ps['talk'])))
    if len(poses) > LIMITS['POSES']:
        sys.exit('%s: %d seat poses, at most %d' % (key, len(poses), LIMITS['POSES']))
    actor = struct.pack('<6B4hH%dB' % (2 * LIMITS['IDLE']), 0xFF, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                        *([0] * 2 * LIMITS['IDLE']))
    for name, a in (place.get('actors') or {}).items():
        frames = [f for f in actor_frames if f[0] == name]
        first = None
        for _n, fname, im in frames:
            i = add_image(pack, '%s_%s' % (name[:8], fname[:8]), im.convert('RGB'), pal, a['rect'][0], a['rect'][1])
            first = i if first is None else first
        names = [f[1] for f in frames]
        idle = []
        for fname, ms in a['clips']['idle']:
            idle += [names.index(fname), max(1, round(ms / 55))]          # BIOS ticks
        idle += [0] * (2 * LIMITS['IDLE'] - len(idle))
        glance = a['clips'].get('glance', [[names[0], 0]])[0][0]
        actor = struct.pack('<6B4hH%dB' % (2 * LIMITS['IDLE']), people_idx[a['person']], len(frames),
                            len(a['clips']['idle']), names.index(a['hover']), names.index(glance), 0,
                            *a['rect'], first, *idle)
    hot = hotspots(key, cfg, frame)
    # the close-up's backdrop: 48 colours in 16-63
    cb = closeup_backdrop(back)
    part = make_palette([cb], 48, 16)
    cpal = Palette(UI + part + [(0, 0, 0)] * 192)
    cp = pack.add('cpal_' + key, 'PALPART', struct.pack('<BB', 16, 48) + vga(part) + closeup_mix(cpal.colours))
    cback = add_image(pack, 'cback_' + key, cb, cpal, allowed=list(range(64)))
    rec.update(hot=hot, poses=poses, actor=actor, closeup_part=cp, closeup_back=cback)
    return rec, pal


def closeup_mix(colours):
    """The text tables for a close-up, over its first 64 colours only."""
    rgb = np.array(colours[:64], np.float32)
    out = b''
    for t in TEXT_COLOURS:
        for a in (0.25, 0.5, 0.75):
            out += bytes(nearest(colours, rgb * (1 - a) + np.array(colours[t], np.float32) * a,
                                 list(range(64))).astype(np.uint8))
    return out


def pack_briefing(pack, rec, pal, place, d):
    s = place['speaker']
    atlas = Image.open(os.path.join(d, s['atlas']))
    w, h = s['size']
    cells = []
    for k in range(len(s['gestures']) * s['columns']):
        c = atlas.crop(((k % s['columns']) * w, (k // s['columns']) * h,
                        (k % s['columns'] + 1) * w, (k // s['columns'] + 1) * h))
        cells.append(add_sprite(pack, 'xo_cell%d' % k, c, pal, s['x'], s['y']))
    stages = []
    for name in place['stages']['order']:
        st = place['stages']['poses'][name]
        if st:
            stages.append(add_sprite(pack, 'crowd_' + name, Image.open(os.path.join(d, st['file'])), pal,
                                     st['x'], st['y']))
    c = s['closeup']
    cback = Image.open(os.path.join(d, c['back']))
    big = Image.open(os.path.join(d, c['atlas']))
    cw, ch = c['size']
    bigcells = [big.crop(((k % c['columns']) * cw, (k // c['columns']) * ch,
                          (k % c['columns'] + 1) * cw, (k // c['columns'] + 1) * ch)) for k in range(len(cells))]
    cpal = Palette(UI + make_palette([cback], 48, 16) + make_palette(bigcells, 192, 64))
    cpal_i = pack.add('pal_xoclose', 'PAL', vga(cpal.colours) + mix_tables(cpal.colours, list(range(64))))
    cback_i = add_image(pack, 'xoclose_back', cback, cpal, allowed=list(range(64)))
    big0 = None
    for k, im in enumerate(bigcells):
        i = add_sprite(pack, 'xo_big%d' % k, im, cpal, max(0, c['x']), c['y'], allowed=[0] + list(range(64, 256)))
        big0 = i if big0 is None else big0
    mov = s['mouth_of_viseme']
    return struct.pack('<4BHHHH9BBHHH', len(s['gestures']), s['columns'], 0, 0, cells[0], cpal_i, cback_i, big0,
                       *mov, 0, stages[0] if stages else 0xFFFF, stages[1] if len(stages) > 1 else 0xFFFF,
                       place['stages']['ms'] // 55 or 1)


def pack_deck(pack, place, d, ships_csv):
    back = Image.open(os.path.join(d, 'back.png')).convert('RGBA')
    F = place['fighter']
    plates = []
    for ship, s in F['ships'].items():
        im = back.copy()
        im.alpha_composite(Image.open(os.path.join(d, s['under'])).convert('RGBA'), (s['ux'], s['uy']))
        L = F['legs'][s['legs']]
        foot, strut, saddle = (Image.open(os.path.join(d, L[k])).convert('RGBA') for k in ('foot', 'strut', 'saddle'))
        for sx, top in s['sleds']:
            H = max(F['deck'] - top + L['seat'], foot.size[1] + saddle.size[1])
            x, y = sx - L['axis'], F['deck'] - H
            for yy in range(saddle.size[1], H - foot.size[1] + 2):
                im.alpha_composite(strut, (x, y + yy))
            im.alpha_composite(foot, (x, y + H - foot.size[1]))
            im.alpha_composite(saddle, (x, y))
        sp = Image.open(os.path.join(d, s['sprite'])).convert('RGBA')
        canvas = Image.new('RGBA', SCREEN, (0, 0, 0, 0))
        canvas.paste(sp, (s['x'], s['y']))
        im.alpha_composite(canvas)
        plates.append((ship, im.convert('RGB')))
    front = Image.open(os.path.join(d, place['front'])).convert('RGBA')
    A = place['actor']
    px, py, pw, ph = A['patch']
    cut = front.copy()
    ImageDraw.Draw(cut).rectangle([px, py, px + pw - 1, py + ph - 1], fill=(0, 0, 0, 0))
    clips = []
    for name in ('idle', 'console-work', 'ready-nod'):
        c = A['clips'][name]
        at = Image.open(os.path.join(d, c['atlas']))
        cells = [at.crop(((k % c['columns']) * pw, (k // c['columns']) * ph,
                          (k % c['columns'] + 1) * pw, (k // c['columns'] + 1) * ph)) for k in range(c['frames'])]
        clips.append((name, cells, c['ms']))
    pal = room_palette([p for _s, p in plates[:3]] + [front] + clips[0][1][:4])
    rec = {'palette': add_palette(pack, 'pal_deck', pal, list(range(256))),
           'back': add_image(pack, 'back_deck', back.convert('RGB'), pal)}
    entries = []
    for ship, im in plates:
        entries.append((ship, add_image(pack, 'deck_' + ship, im, pal)))
    front_i = add_sprite(pack, 'kit_front', cut, pal, 0, 0)
    clip_first, clip_n, clip_ms = [], [], []
    for name, cells, ms in clips:
        first = None
        for k, im in enumerate(cells):
            a = np.asarray(im.convert('RGBA'))
            if not (a[..., 3] >= 128).any():
                im = im.copy()
                im.putpixel((0, 0), (0, 0, 0, 255))
            i = add_sprite(pack, 'kit_%s%d' % (name[:5], k), im, pal, px, py)
            first = i if first is None else first
        clip_first.append(first)
        clip_n.append(len(cells))
        clip_ms.append(ms[0])
    types = {r['key']: int(r['type'], 16) for r in ships_csv}
    ships = b''.join(struct.pack('<BBH', types[s], 0, i) for s, i in entries)
    ships += b'\0' * (4 * LIMITS['DECK_SHIPS'] - len(ships))
    deck = struct.pack('<BBH3H3B3B3H4h', len(entries), 0, front_i, *clip_first, *clip_n, 0, 0, 0,
                       *[max(1, m // 55) for m in clip_ms], px, py, pw, ph) + ships
    return rec, deck


# --- people ------------------------------------------------------------------

def pack_people(pack, keys):
    out = []
    for key in keys:
        d = os.path.join(ROOT, 'art', 'people', 'faces', key)
        fj = json.load(open(os.path.join(d, 'face.json')))
        k = FACE / fj['size']
        base = Image.open(os.path.join(d, 'base.png')).convert('RGBA').resize((FACE, FACE), Image.LANCZOS)
        mouths = [Image.open(os.path.join(d, 'mouth_%d.png' % i)).convert('RGBA') for i in range(9)]
        eyes = [Image.open(os.path.join(d, 'eyes_%d.png' % i)).convert('RGBA') for i in range(3)]
        mb, eb = fj['mouth_box'], fj['eye_box']
        mpos = (round(mb[0] * k), round(mb[1] * k), round((mb[2] - mb[0]) * k), round((mb[3] - mb[1]) * k))
        epos = (round(eb[0] * k), round(eb[1] * k), round((eb[2] - eb[0]) * k), round((eb[3] - eb[1]) * k))
        mouths = [m.resize(mpos[2:], Image.LANCZOS) for m in mouths]
        eyes = [e.resize(epos[2:], Image.LANCZOS) for e in eyes]
        part = make_palette([base] + mouths + eyes, 192, 64)
        pal = Palette(UI + [(0, 0, 0)] * 48 + part)
        allowed = [0] + list(range(64, 256))
        pp = pack.add('ppal_' + key[:12], 'PALPART', struct.pack('<BB', 64, 192) + vga(part))
        b = add_sprite(pack, 'face_' + key[:12], base, pal, 0, 0, allowed)
        m0 = None
        for i, m in enumerate(mouths):
            j = add_image(pack, 'mouth%d_%s' % (i, key[:10]), m.convert('RGB'), pal, mpos[0], mpos[1], allowed)
            m0 = j if m0 is None else m0
        e0 = None
        for i, e in enumerate(eyes):
            j = add_image(pack, 'eyes%d_%s' % (i, key[:10]), e.convert('RGB'), pal, epos[0], epos[1], allowed)
            e0 = j if e0 is None else e0
        bb = np.asarray(base)[..., 3] >= 128
        top = int(np.nonzero(bb.any(axis=1))[0][0])
        out.append(struct.pack('<10sHHHHh', key.encode()[:9], pp, b, m0, e0, top))
    return out


# --- font and pointer ------------------------------------------------------------

def pack_font(pack, name, file, px, weight):
    font = ImageFont.truetype(os.path.join(FONT_DIR, file), px)
    font.set_variation_by_axes([weight])
    ascent, descent = font.getmetrics()
    height = ascent + descent
    glyphs, table, at = [], [], 0
    for ch in range(32, 127):
        c = chr(ch)
        adv = int(round(font.getlength(c)))
        w = max(1, adv + 2)
        im = Image.new('L', (w, height), 0)
        ImageDraw.Draw(im).text((0, 0), c, font=font, fill=255)
        a = np.asarray(im)
        cov = np.clip((a.astype(np.int32) + 32) // 72, 0, 3).astype(np.uint8)
        cols = np.nonzero(cov.any(axis=0))[0]
        if len(cols):
            cov = cov[:, :cols[-1] + 1]
        else:
            cov = cov[:, :1] * 0
        table.append(struct.pack('<HBB', at, cov.shape[1], adv))
        glyphs.append(bytes(cov.reshape(-1)))
        at += cov.size
    payload = struct.pack('<BBBB', height, ascent, 32, 95) + b''.join(table) + b''.join(glyphs)
    return pack.add(name, 'FONT', payload, 0, height)


def pack_pointers(pack, pal):
    """The arrow and the door arrow, in UI colours only (black outline, white)."""
    arrow = ['X           ', 'XX          ', 'XWX         ', 'XWWX        ', 'XWWWX       ', 'XWWWWX      ',
             'XWWWWWX     ', 'XWWWWWWX    ', 'XWWWWWWWX   ', 'XWWWWWWWWX  ', 'XWWWWWXXXXX ', 'XWWXWWX     ',
             'XWX XWWX    ', 'XX  XWWX    ', 'X    XWWX   ', '     XWWX   ', '      XX    ']
    door = ['    XX    ', '   XWWX   ', '  XWWWWX  ', ' XWWWWWWX ', 'XWWWWWWWWX', 'XXXXWWXXXX', '   XWWX   ',
            '   XWWX   ', '   XWWX   ', '   XWWX   ', '   XXXX   ']
    out = []
    for name, rows in (('ptr_arrow', arrow), ('ptr_door', door)):
        w, h = max(len(r) for r in rows), len(rows)
        idx = np.zeros((h, w), np.uint8)
        alpha = np.zeros((h, w), bool)
        for y, r in enumerate(rows):
            for x, ch in enumerate(r):
                if ch in 'XW':
                    idx[y, x] = 0 if ch == 'X' else 7
                    alpha[y, x] = True
        hx, hy = (0, 0) if name == 'ptr_arrow' else (w // 2, 0)
        out.append(pack.add(name, 'SPR', sprite_payload(idx, alpha), w, h, -hx, -hy))
    return out


# --- the archive -----------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'dos', 'RV.DAT'))
    ap.add_argument('--header', default=os.path.join(ROOT, 'src', 'rvasset.h'))
    args = ap.parse_args()
    table = json.load(open(os.path.join(ROOT, 'data', 'places.json')))
    with open(os.path.join(ROOT, 'data', 'people.csv')) as f:
        people = [r['key'] for r in csv.DictReader(f)]
    with open(os.path.join(ROOT, 'data', 'ships.csv')) as f:
        ships_csv = list(csv.DictReader(f))
    people_idx = {k: i for i, k in enumerate(people)}
    pack = Pack()
    ui_pal = Palette(UI + [(0, 0, 0)] * 240)
    ui_i = add_palette(pack, 'pal_ui', ui_pal, list(range(16)))
    body = pack_font(pack, 'font_body', *FONT_FILES['body'])
    title = pack_font(pack, 'font_title', *FONT_FILES['title'])
    arrow, door = pack_pointers(pack, ui_pal)
    rooms = []
    for key in ROOMS:
        d = os.path.join(ROOT, 'art', 'places', key)
        place = json.load(open(os.path.join(d, 'place.json')))
        cfg = table[key]
        speaker = b'\0' * struct.calcsize('<4BHHHH9BBHHH')
        deck = b'\0' * (struct.calcsize('<BBH3H3B3B3H4h') + 4 * LIMITS['DECK_SHIPS'])
        if key == 'deck':
            rec, deck = pack_deck(pack, place, d, ships_csv)
            rec.update(hot=hotspots(key, cfg, places.Frame(cfg['crop'])), poses=[],
                       actor=struct.pack('<6B4hH%dB' % (2 * LIMITS['IDLE']), 0xFF, *([0] * 9), 0,
                                         *([0] * 2 * LIMITS['IDLE'])),
                       closeup_part=0xFFFF, closeup_back=0xFFFF)
        else:
            rec, pal = pack_room(pack, key, people_idx, cfg, place, d)
            if key == 'briefing':
                speaker = pack_briefing(pack, rec, pal, place, d)
        boards = b''
        for kind, r in (place.get('boards') or {}).items():
            boards += struct.pack('<BB4h', ['chalkboard', 'simulator_screen', 'screen', 'medal_case'].index(kind),
                                  0, *r)
        boards += b'\0' * (10 * 4 - len(boards))
        hot = b''.join(rec['hot']) + b'\0' * (36 * (LIMITS['HOTS'] - len(rec['hot'])))
        poses = b''.join(rec['poses']) + b'\0' * (6 * (LIMITS['POSES'] - len(rec['poses'])))
        rooms.append(struct.pack('<12sBBBB4H', key.encode(), ROOMS.index(key), len(rec['hot']), len(rec['poses']),
                                 0 if rec['actor'][0] == 0xFF else 1, rec['palette'], rec['back'],
                                 rec['closeup_part'], rec['closeup_back'])
                     + hot + poses + rec['actor'] + speaker + deck + boards)
    persons = pack_people(pack, people)
    # lay out: header, directory, rooms, people, payloads
    hdr_fmt = '<8sHHLHLHLHHHHHH20s'
    hsize = struct.calcsize(hdr_fmt)
    dir_at = hsize
    dsize = 40
    rooms_at = dir_at + dsize * len(pack.assets)
    people_at = rooms_at + sum(len(r) for r in rooms)
    data_at = people_at + sum(len(p) for p in persons)
    directory, blobs, at = [], [], data_at
    for name, kind, w, h, x, y, payload in pack.assets:
        directory.append(struct.pack('<22sBBHHhhLL', name.encode(), kind, 0, w, h, x, y, at, len(payload)))
        blobs.append(payload)
        at += len(payload) + (len(payload) & 1)
        if len(payload) & 1:
            blobs.append(b'\0')
    head = struct.pack(hdr_fmt, b'RVDAT', VERSION, len(pack.assets), dir_at, len(rooms), rooms_at, len(persons),
                       people_at, len(rooms[0]), len(persons[0]), body, title, arrow, door, ui_i.to_bytes(2, 'little')
                       + b'\0' * 18)
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, 'wb') as f:
        f.write(head + b''.join(directory) + b''.join(rooms) + b''.join(persons) + b''.join(blobs))
    with open(args.header, 'w') as f:
        f.write(header(len(rooms[0]), len(persons[0])))
    print('%s: %d assets, %d rooms (%d bytes each), %d people, %.1f MB'
          % (args.out, len(pack.assets), len(rooms), len(rooms[0]), len(persons), os.path.getsize(args.out) / 1e6))


def header(room_size, person_size):
    L = LIMITS
    lines = ['/* rvasset.h: RV.DAT\'s records.  Generated by tools/rvpack.py: do not edit. */',
             '#ifndef RVASSET_H', '#define RVASSET_H', '', '#define RVDAT_VERSION %d' % VERSION, '']
    for k, v in L.items():
        lines.append('#define RV_%s %d' % (k, v))
    lines += ['#define RV_FACE %d' % FACE, '']
    for k, v in KIND.items():
        lines.append('#define RV_KIND_%s %d' % (k, v))
    for i, a in enumerate(ACTIONS):
        lines.append('#define RV_ACT_%s %d' % (a, i))
    for i, r in enumerate(ROOMS):
        lines.append('#define RV_R_%s %d' % (r.upper(), i))
    lines.append('#define RV_NROOMS %d' % len(ROOMS))
    for i, n in enumerate(UI_NAMES):
        lines.append('#define UI_%s %d' % (n, i))
    lines += ['#define RV_TEXT_WHITE 0', '#define RV_TEXT_AMBER 1', '#define RV_TEXT_GREY 2',
              '/* a palette asset: rgb[768] (6-bit), shade[256], mix[3 colours][3 strengths][256] */',
              '#define RV_PAL_RGB 0', '#define RV_PAL_SHADE 768', '#define RV_PAL_MIX 1024', '#define RV_PAL_BYTES 3328',
              '']
    lines += ['#pragma pack(1)', '''typedef struct {
    char magic[8];
    unsigned short version, nassets;
    unsigned long dir_at;
    unsigned short nrooms;
    unsigned long rooms_at;
    unsigned short npeople;
    unsigned long people_at;
    unsigned short room_size, person_size;
    unsigned short font_body, font_title, ptr_arrow, ptr_door;
    unsigned short pal_ui;
    unsigned char reserved[18];
} RvDatHeader;
typedef struct {
    char name[22];
    unsigned char kind, flags;
    unsigned short w, h;
    short x, y;                 /* where it goes on screen (sprites, frames), or a pointer's hot spot */
    unsigned long at, size;
} RvAsset;
typedef struct {
    short x, y, w, h;
    unsigned char action, arg;  /* RV_ACT_*, and the room for RV_ACT_GOTO */
    char label[26];
} RvHot;
typedef struct {
    unsigned char seat, pilot;  /* 0 left, 1 middle, 2 right; people.csv row */
    unsigned short talk, face;  /* sprite assets */
} RvPose;
typedef struct {
    unsigned char person, nframes, nidle, hover, glance, pad;
    short x, y, w, h;
    unsigned short frame0;      /* nframes consecutive image assets */
    unsigned char idle[RV_IDLE][2];   /* frame, BIOS ticks */
} RvActor;
typedef struct {
    unsigned char ngestures, nmouths, pad0, pad1;
    unsigned short cell0, closeup_pal, closeup_back, big0;
    unsigned char mouth_of_viseme[9], pad2;
    unsigned short stage[2], stage_ticks;
} RvSpeaker;
typedef struct {
    unsigned char ship_type, pad;
    unsigned short plate;
} RvDeckShip;
typedef struct {
    unsigned char nships, pad;
    unsigned short front;
    unsigned short clip_first[RV_CLIPS];
    unsigned char clip_n[RV_CLIPS], pad2[3];
    unsigned short clip_ticks[RV_CLIPS];
    short patch_x, patch_y, patch_w, patch_h;
    RvDeckShip ship[RV_DECK_SHIPS];
} RvDeck;
typedef struct {
    unsigned char kind, pad;    /* 0 chalkboard, 1 simulator screen, 2 briefing screen, 3 medal case */
    short x, y, w, h;
} RvBoard;
typedef struct {
    char key[12];
    unsigned char id, nhot, nposes, has_actor;
    unsigned short palette, back, closeup_part, closeup_back;
    RvHot hot[RV_HOTS];
    RvPose pose[RV_POSES];
    RvActor actor;
    RvSpeaker speaker;
    RvDeck deck;
    RvBoard board[4];
} RvRoom;
typedef struct {
    char key[10];
    unsigned short palpart, face, mouth0, eyes0;   /* the face sprite, 9 mouth and 3 eye images */
    short top;                  /* the face's first opaque row */
} RvPerson;
#pragma pack()''', '', '#define RV_ROOM_SIZE %d' % room_size, '#define RV_PERSON_SIZE %d' % person_size, '',
              '#endif', '']
    return '\n'.join(lines)


if __name__ == '__main__':
    main()
