#!/usr/bin/env python3
"""The carrier's rooms with people in them: painted shots -> layers the
game composes.  Called by places.py for every room in data/places.json
that has no fighter.

Every shot of a room is an edit of the same picture, so they line up
exactly; each drifts a little in colour.  What a shot adds to the room's
empty plate is cut out where it differs from the plate.  The cut is
tone-matched to the plate (the low-frequency colour difference measured
around it is added back), so its border shows no seam.

A room can have:

    seats      places at a table, each with the pilots drawn sitting there
               ("talk": turned to the others; "face": turned to the player,
               shown while the mouse is over them).  A mission puts at most
               one pilot in each seat.  Neighbouring seats are split by a
               seam that runs where the shots have the fewest people, so
               any mix of pilots composes.
    actors     someone who animates on their own (the barman): a rectangle
               replaced by one frame after another, frames from whole shots
    stages     the whole crowd in later poses (the briefing's pilots rising),
               cut out against a reference shot
    speaker    a talking figure from a sheet of gestures x mouths, placed
               where a reference shot has them; the mouth follows lipsync.py
               and the gesture changes with each sentence.  Also drawn
               large over a blurred crop of the room for the close-up.
    hotspots   rectangles the mouse can click, with what they do
    boards     rectangles the engine writes or draws on (the chalkboard, the
               briefing screen, the medal case)
    zoom       a board seen up close: a 16:10 crop of the plate around it,
               scaled to the screen (zoom-BOARD.png), and where the board's
               rectangle lands in it

Source pixels throughout data/places.json; the outputs are in screen pixels.
"""
import json
import os
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lipsync  # noqa: E402
import portraits  # noqa: E402

CHANGED = 12.0     # Lab distance at which a pixel counts as changed
TONE_SIGMA = 48    # source pixels: the reach of the colour drift correction
MIN_PART = 3000    # source pixels: smaller changed areas are noise
# the speaker's three mouths: lipsync.py's nine shapes folded onto them
MOUTH_OF = {lipsync.REST: 0, lipsync.MBP: 0, lipsync.FV: 1, lipsync.TH: 1, lipsync.S: 1,
            lipsync.AH: 1, lipsync.EH: 1, lipsync.OH: 2, lipsync.OO: 2}


def load(path):
    return np.asarray(Image.open(path).convert('RGB')).astype(np.float32) / 255


def lab_distance(a, b):
    la = portraits.srgb_to_lab(ndimage.gaussian_filter(a, (1.5, 1.5, 0)))
    lb = portraits.srgb_to_lab(ndimage.gaussian_filter(b, (1.5, 1.5, 0)))
    return np.sqrt(((la - lb) ** 2).sum(-1))


def toned(plate, img, keep):
    """img with the plate's low-frequency colour where they should agree:
    the difference over keep, spread by a normalised blur (the mean
    difference where keep is far away)."""
    keep = keep.astype(np.float32)
    diff = (plate - img) * keep[..., None]
    mean = diff.sum((0, 1)) / max(keep.sum(), 1.0)
    eps = 0.02
    num = np.stack([ndimage.gaussian_filter(diff[..., c], TONE_SIGMA) for c in range(3)], -1)
    den = ndimage.gaussian_filter(keep, TONE_SIGMA)[..., None]
    return np.clip(img + (num + eps * mean) / (den + eps), 0, 1)


def clean(mask, min_part=MIN_PART):
    m = ndimage.binary_opening(mask, iterations=2)
    m = ndimage.binary_closing(m, iterations=4)
    m = ndimage.binary_fill_holes(m)
    lab, n = ndimage.label(m)
    if not n:
        return m
    sizes = ndimage.sum(m, lab, range(1, n + 1))
    return np.isin(lab, 1 + np.nonzero(sizes >= min_part)[0])


def changes(plate, img, limit=None):
    """img toned to the plate, and where it differs from it (inside limit)."""
    first = lab_distance(plate, img) < CHANGED
    img = toned(plate, img, first)
    mask = clean(lab_distance(plate, img) >= CHANGED)
    if limit is not None:
        mask &= limit
    img = toned(plate, img, ~ndimage.binary_dilation(mask, iterations=10))
    return img, mask


def box_mask(shape, box):
    m = np.zeros(shape, bool)
    if box:
        x0, y0, x1, y1 = box
        m[y0:y1, x0:x1] = True
    else:
        m[:] = True
    return m


def seam(cost, x0, x1):
    """A top-to-bottom path between columns x0 and x1 of least total cost,
    stepping at most one column a row: its x on every row."""
    c = cost[:, x0:x1].astype(np.float64)
    h, w = c.shape
    acc = c.copy()
    back = np.zeros((h, w), np.int8)
    for y in range(1, h):
        prev = acc[y - 1]
        left = np.r_[np.inf, prev[:-1]]
        right = np.r_[prev[1:], np.inf]
        stack = np.stack([left, prev, right])
        k = np.argmin(stack, axis=0)
        acc[y] += stack[k, np.arange(w)]
        back[y] = k - 1
    x = int(np.argmin(acc[-1]))
    path = np.zeros(h, int)
    for y in range(h - 1, -1, -1):
        path[y] = x + x0
        x = min(max(x + int(back[y, x]), 0), w - 1)
    return path


def owners(img, mask, centres, f=2, edge_cost=10.0):
    """Which seat each changed pixel of a shot belongs to: the seat whose
    column of people it is best connected to through the changed pixels,
    a step costing more across a colour edge (the ink line between two
    people), so a reaching arm stays with its owner.  -1 where unchanged."""
    from scipy.sparse import coo_matrix
    from scipy.sparse.csgraph import dijkstra
    m = mask[::f, ::f]
    lab = portraits.srgb_to_lab(ndimage.gaussian_filter(img, (1, 1, 0))[::f, ::f])
    h, w = m.shape
    index = -np.ones((h, w), int)
    index[m] = np.arange(m.sum())
    rows, cols, cost = [], [], []
    for dy, dx in ((0, 1), (1, 0), (1, 1), (1, -1)):
        a = m[max(0, -dy):h - max(0, dy), max(0, -dx):w - max(0, dx)]
        ys, xs = np.nonzero(a)
        ys, xs = ys + max(0, -dy), xs + max(0, -dx)
        ok = m[ys + dy, xs + dx]
        ys, xs = ys[ok], xs[ok]
        d = np.linalg.norm(lab[ys, xs] - lab[ys + dy, xs + dx], axis=1)
        rows.append(index[ys, xs])
        cols.append(index[ys + dy, xs + dx])
        cost.append(np.hypot(dy, dx) * (1.0 + edge_cost * d / 10.0))
    n = int(m.sum())
    g = coo_matrix((np.concatenate(cost), (np.concatenate(rows), np.concatenate(cols))), shape=(n, n)).tocsr()
    seat_of = np.full(n, -1)
    seeds = []
    yy, xx = np.nonzero(m)
    for k, c in enumerate(centres):
        near = np.nonzero(np.abs(xx * f - c) < 15)[0]
        seat_of[near] = k
        seeds.append(near)
    seeds = np.concatenate(seeds)
    _d, _p, src = dijkstra(g, directed=False, indices=seeds, min_only=True, return_predecessors=True)
    own = np.where(src >= 0, seat_of[np.maximum(src, 0)], -1)
    # unreachable parts: the nearest seat
    lost = own < 0
    own[lost] = np.argmin(np.abs(xx[lost, None] * f - np.array(centres)[None, :]), axis=1)
    small = -np.ones((h, w), int)
    small[m] = own
    big = np.repeat(np.repeat(small, f, axis=0), f, axis=1)[:mask.shape[0], :mask.shape[1]]
    # fill the full-size mask's pixels the small one missed from their neighbours
    missing = mask & (big < 0)
    if missing.any():
        _dist, (iy, ix) = ndimage.distance_transform_edt(big < 0, return_indices=True)
        big[missing] = big[iy[missing], ix[missing]]
    return np.where(mask, big, -1)


def body(mask):
    """A person's cut: the largest part and parts at least a fifth its size
    (an arm the outline split off), not scraps of a neighbour."""
    m = clean(mask, MIN_PART // 2)
    lab, n = ndimage.label(m)
    if n < 2:
        return m
    sizes = ndimage.sum(m, lab, range(1, n + 1))
    return np.isin(lab, 1 + np.nonzero(sizes >= sizes.max() / 5)[0])


def sprite(frame, img, mask, dilate=3):
    """A cut-out at screen size: (RGBA image, x, y), hard alpha, cropped."""
    import places
    if dilate:
        mask = ndimage.binary_dilation(mask, iterations=dilate)
    rgba = np.dstack([img, mask.astype(np.float32)])
    small = places.hard(frame.image(Image.fromarray((rgba * 255 + 0.5).astype(np.uint8), 'RGBA')))
    bb = small.getbbox()
    if not bb:
        return None
    return small.crop(bb), bb[0], bb[1]


def seats(cfg, src, plate, frame, od, log):
    """The seat sprites: {seat: {pilot: {pose: {file, x, y}}}}."""
    limit = box_mask(plate.shape[:2], cfg.get('table'))
    shots = {}
    for seat in cfg['seats'].values():
        for poses in seat['pilots'].values():
            for f in poses.values():
                if f not in shots:
                    shots[f] = changes(plate, load(os.path.join(src, f)), limit)
    people = sum(m.astype(np.float32) for _img, m in shots.values())
    names = list(cfg['seats'])
    centres = [cfg['seats'][n]['x'] for n in names]
    owner = {f: owners(img, m, centres) for f, (img, m) in shots.items()}
    out = {}
    for k, name in enumerate(names):
        out[name] = {}
        for pilot, poses in cfg['seats'][name]['pilots'].items():
            out[name][pilot] = {}
            for pose, f in poses.items():
                img, mask = shots[f]
                region = owner[f] == k
                cut = sprite(frame, img, body(mask & region) & region)
                if cut is None:
                    log('  %s %s %s: nothing there' % (name, pilot, pose))
                    continue
                pic, x, y = cut
                fn = 'seat-%s-%s.png' % (pilot, pose)
                pic.save(os.path.join(od, fn))
                out[name][pilot][pose] = {'file': fn, 'x': x, 'y': y}
            log('  %-6s %-8s %s' % (name, pilot, ' '.join(out[name][pilot])))
    return out


def actor(name, cfg, src, plate, frame, od):
    """An actor that animates on its own: a rectangle of the plate replaced
    by each frame, the frames' changes (inside search) feathered into the
    plate so the rectangle's border is the plate's own pixels."""
    import places
    limit = box_mask(plate.shape[:2], cfg['search'])
    shots = {k: changes(plate, load(os.path.join(src, f)), limit) for k, f in cfg['frames'].items()}
    union = ndimage.binary_dilation(np.any([m for _i, m in shots.values()], axis=0), iterations=12)
    ys, xs = np.nonzero(union)
    # the rectangle on screen, and the source rectangle that maps onto it
    sx0, sy0 = frame.xy(xs.min(), ys.min())
    sx1, sy1 = frame.xy(xs.max() + 1, ys.max() + 1)
    rect = [int(sx0), int(sy0), int(np.ceil(sx1)), int(np.ceil(sy1))]
    feather = ndimage.gaussian_filter(union.astype(np.float32), 6)
    pics = {}
    for k, (img, _m) in shots.items():
        mix = plate * (1 - feather[..., None]) + img * feather[..., None]
        full = frame.image(Image.fromarray((mix * 255 + 0.5).astype(np.uint8), 'RGB'))
        pics[k] = full.crop(rect)
    order = list(pics)
    fn = '%s.png' % name
    places.atlas([p.convert('RGBA') for p in pics.values()]).save(os.path.join(od, fn))
    return {'person': cfg.get('person'), 'rect': [rect[0], rect[1], rect[2] - rect[0], rect[3] - rect[1]],
            'atlas': fn, 'columns': 8, 'frames': order,
            'clips': cfg.get('clips', {}), 'hover': cfg.get('hover')}


def stages(cfg, src, frame, od):
    """The crowd's later poses: what each changes from the reference shot."""
    ref = load(os.path.join(src, cfg['reference']))
    out = {}
    for name, f in cfg['poses'].items():
        if f is None:
            out[name] = None
            continue
        img, mask = changes(ref, load(os.path.join(src, f)), box_mask(ref.shape[:2], cfg.get('box')))
        pic, x, y = sprite(frame, img, mask)
        fn = 'crowd-%s.png' % name
        pic.save(os.path.join(od, fn))
        out[name] = {'file': fn, 'x': x, 'y': y}
    return {'poses': out, 'order': list(cfg['poses']), 'ms': cfg.get('ms', 300)}


def sheet_cells(path, rows, cols):
    """The cells of a sheet of figures on transparency, by their parts:
    [row][col] -> RGBA image of that figure, all cropped to one size about
    their figures' bottom middles."""
    img = Image.open(path).convert('RGBA')
    a = np.asarray(img)[..., 3] > 16
    lab, n = ndimage.label(ndimage.binary_closing(a, iterations=2))
    sizes = ndimage.sum(a, lab, range(1, n + 1))
    big = sorted(1 + np.nonzero(sizes > sizes.max() * 0.2)[0], key=lambda i: -sizes[i - 1])[:rows * cols]
    if len(big) < rows * cols:
        raise SystemExit('%s: found %d figures, wanted %d' % (path, len(big), rows * cols))
    boxes = []
    for i in big:
        ys, xs = np.nonzero(lab == i)
        boxes.append((i, xs.min(), ys.min(), xs.max() + 1, ys.max() + 1))
    # rows by their tops, then left to right
    boxes.sort(key=lambda b: b[2])
    grid = []
    for r in range(rows):
        row = sorted(boxes[r * cols:(r + 1) * cols], key=lambda b: b[1])
        grid.append(row)
    # one cell size: the widest and tallest figure, anchored at the head's middle
    w = max(b[3] - b[1] for b in boxes)
    h = max(b[4] - b[2] for b in boxes)
    cells = []
    for row in grid:
        out = []
        for i, x0, y0, x1, y1 in row:
            only = np.asarray(img).copy()
            only[lab != i] = 0
            fig = Image.fromarray(only, 'RGBA').crop((x0, y0, x1, y1))
            # where the head is: the middle of the top fifth of the figure
            fa = np.asarray(fig)[..., 3] > 16
            top = fa[:max(1, fa.shape[0] // 5)]
            hx = int(np.nonzero(top.any(axis=0))[0].mean())
            cell = Image.new('RGBA', (2 * w, h), (0, 0, 0, 0))
            cell.paste(fig, (w - hx, 0))
            out.append(cell)
        cells.append(out)
    return cells


def fit_figure(ref, plate, fig, box, scales):
    """Where fig (RGBA) best matches ref when drawn over the plate, inside box:
    (scale, x, y of its corner) in source pixels."""
    x0, y0, x1, y1 = box
    win = ref[y0:y1, x0:x1]

    def edges(rgb):
        g = ndimage.gaussian_filter(rgb @ np.array([0.30, 0.59, 0.11], np.float32), 1.0)
        e = np.hypot(ndimage.sobel(g, 0), ndimage.sobel(g, 1))
        return (e - e.mean()) / (e.std() + 1e-6)
    er = edges(win)
    best = None
    for s in scales:
        small = fig.resize((max(1, round(fig.size[0] * s)), max(1, round(fig.size[1] * s))), Image.LANCZOS)
        a = np.asarray(small).astype(np.float32) / 255
        test = plate[y0:y1, x0:x1].copy()
        h, w = a.shape[:2]
        cy, cx = (test.shape[0] - h) // 2, (test.shape[1] - w) // 2
        if cy < 0 or cx < 0:
            continue
        test[cy:cy + h, cx:cx + w] = test[cy:cy + h, cx:cx + w] * (1 - a[..., 3:]) + a[..., :3] * a[..., 3:]
        dx, dy, peak = portraits.xcorr_shift(er, edges(test))
        if best is None or peak > best[0]:
            best = (peak, s, x0 + cx + dx, y0 + cy + dy)
    return best[1], best[2], best[3], best[0]


def speaker(cfg, src, plate, frame, od, log):
    """The talking figure at its place in the room, and large for its close-up."""
    import places
    cells = sheet_cells(os.path.join(src, cfg['sheet']), len(cfg['gestures']), len(cfg['mouths']))
    ref = load(os.path.join(src, cfg['reference']))
    s, fx, fy, score = fit_figure(ref, plate, cells[0][0], cfg['search'], np.geomspace(0.15, 0.6, 46))
    s2, fx, fy, score = fit_figure(ref, plate, cells[0][0], cfg['search'], s * np.linspace(0.96, 1.04, 9))
    log('  speaker fitted at scale %.3f, %d,%d (score %.2f)' % (s2, fx, fy, score))
    # in the room: scaled, clipped at the lectern's top
    W = round(cells[0][0].size[0] * s2)
    H = round(cells[0][0].size[1] * s2)
    clip = cfg['clip_y'] - fy
    frames = []
    for row in cells:
        for c in row:
            big = Image.new('RGBA', plate.shape[1::-1], (0, 0, 0, 0))
            small = c.resize((W, H), Image.LANCZOS)
            a = np.asarray(small).copy()
            a[max(0, int(clip)):, :, 3] = 0
            big.paste(Image.fromarray(a, 'RGBA'), (int(round(fx)), int(round(fy))))
            frames.append(places.hard(frame.image(big)))
    bb = union_box(frames)
    places.atlas([f.crop(bb) for f in frames], columns=len(cfg['mouths'])).save(os.path.join(od, 'speaker.png'))
    # the close-up: a blurred, enlarged crop of the room behind the figure, large
    cx0, cy0, cx1, cy1 = cfg['closeup']['crop']
    back = Image.fromarray((plate * 255 + 0.5).astype(np.uint8), 'RGB').crop((cx0, cy0, cx1, cy1))
    back = back.resize(places.SCREEN, Image.LANCZOS)
    back = Image.fromarray(ndimage.gaussian_filter(np.asarray(back).astype(np.float32),
                                                   (cfg['closeup'].get('blur', 3),) * 2 + (0,)).astype(np.uint8))
    back.save(os.path.join(od, 'closeup-back.png'))
    hc = cfg['closeup']['height']
    big = [places.hard(c.resize((round(c.size[0] * hc / c.size[1]), hc), Image.LANCZOS)) for row in cells for c in row]
    cb = union_box(big)
    big = [f.crop(cb) for f in big]
    places.atlas(big, columns=len(cfg['mouths'])).save(os.path.join(od, 'closeup.png'))
    cw, ch = big[0].size
    return {'person': cfg.get('person'), 'atlas': 'speaker.png', 'x': bb[0], 'y': bb[1],
            'size': [bb[2] - bb[0], bb[3] - bb[1]], 'columns': len(cfg['mouths']),
            'gestures': cfg['gestures'], 'mouths': cfg['mouths'],
            'mouth_of_viseme': [MOUTH_OF[v] for v in range(len(lipsync.VISEMES))],
            'closeup': {'back': 'closeup-back.png', 'atlas': 'closeup.png', 'columns': len(cfg['mouths']),
                        'size': [cw, ch], 'x': cfg['closeup']['x'] - cw // 2, 'y': places.SCREEN[1] - ch}}


def union_box(frames):
    bb = None
    for f in frames:
        b = f.getbbox()
        bb = b if bb is None else (min(bb[0], b[0]), min(bb[1], b[1]), max(bb[2], b[2]), max(bb[3], b[3]))
    return bb


def script(text, gestures):
    """A speech as steps for the speaker: [[gesture, mouth, ms]], the gesture
    changing with each sentence (the first, the others in turn, then back)."""
    out = []
    sentences = [t.strip() for t in text.replace('!', '.').replace('?', '.').split('.') if t.strip()]
    for i, sentence in enumerate(sentences):
        g = 0 if i == 0 else 1 + (i - 1) % (len(gestures) - 1)
        for v, steps in lipsync.visemes(sentence + '.'):
            m = MOUTH_OF[v]
            if out and out[-1][0] == g and out[-1][1] == m:
                out[-1][2] += steps * lipsync.STEP_MS
            else:
                out.append([g, m, steps * lipsync.STEP_MS])
    return out


def scale_rect(frame, r):
    x0, y0 = frame.xy(r[0], r[1])
    x1, y1 = frame.xy(r[2], r[3])
    return [round(x0), round(y0), round(x1 - x0), round(y1 - y0)]


def build(key, cfg, out, log=print):
    import places
    src = os.path.join(places.ROOT, cfg['src'])
    frame = places.Frame(cfg['crop'])
    plate = load(os.path.join(src, cfg['background']))
    od = os.path.join(out, key)
    os.makedirs(od, exist_ok=True)
    back = frame.image(Image.fromarray((plate * 255 + 0.5).astype(np.uint8), 'RGB'))
    back.save(os.path.join(od, 'back.png'))
    place = {'size': list(places.SCREEN), 'back': 'back.png', 'about': cfg.get('about', '')}
    if 'seats' in cfg:
        place['seats'] = seats(cfg, src, plate, frame, od, log)
    if 'actors' in cfg:
        place['actors'] = {n: actor(n, a, src, plate, frame, od) for n, a in cfg['actors'].items()}
        for n, a in place['actors'].items():
            log('  actor %s: %d frames in %s' % (n, len(a['frames']), a['rect']))
    if 'speaker' in cfg:
        place['speaker'] = speaker(cfg['speaker'], src, plate, frame, od, log)
        if cfg['speaker'].get('lines'):
            place['speaker']['lines'] = [{'text': t, 'steps': script(t, cfg['speaker']['gestures'])}
                                         for t in cfg['speaker']['lines']]
    if 'stages' in cfg:
        place['stages'] = stages(cfg['stages'], src, frame, od)
    place['hotspots'] = {n: dict(h, rect=scale_rect(frame, h['rect'])) for n, h in cfg.get('hotspots', {}).items()}
    place['boards'] = {n: scale_rect(frame, r) for n, r in cfg.get('boards', {}).items()}
    for n, crop in cfg.get('zoom', {}).items():
        zf = places.Frame(crop)
        name = 'zoom-%s.png' % n
        zf.image(Image.fromarray((plate * 255 + 0.5).astype(np.uint8), 'RGB')).save(os.path.join(od, name))
        place.setdefault('zoom', {})[n] = {'file': name, 'board': scale_rect(zf, cfg['boards'][n])}
    with open(os.path.join(od, 'place.json'), 'w') as f:
        json.dump(place, f, indent=1)
    return place
