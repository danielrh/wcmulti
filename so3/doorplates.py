"""SO3's doors: Wing Commander II's door films (DOORS.V00) with the Culloden's
plates.  Four of the seven doors give the Concordia away: Angel's office
("COL. JEANNETTE DEVEREAUX"), Tolwyn's ("ADMIRAL SIR GEOFFREY TOLWYN,
COMMANDER 14TH FLEET"), and the bridge's and the observation deck's ("CVS 65
CONCORDIA").  Their lettering is one colour (156) on a two-tone dithered
plate, in three fonts of a few pixels; the letters are cut from the plates
themselves (the player's own copy, at build time), the few the plates lack
are drawn here in the same style, the old text is taken off by restoring
the dither, and the new lines are centred where the old ones stood.

A door block is a one-sprite table (the screen, 320 x 129) followed by the
door's palette (FORM PAL CMAP) where the table's size field ends."""
import struct

import wc2data

TEXT = 156
LETTER_GAP = 1
WORD_GAP = {"large": 4, "small": 3, "plate": 3, "round": 3}

# The plates: (door block, the box the text lines are found in).
NAMEPLATE_DEVEREAUX = (0, (185, 280, 40, 92))
NAMEPLATE_TOLWYN = (1, (205, 300, 40, 92))
SHIP_PLATE_BRIDGE = (5, (205, 300, 0, 24))
SHIP_PLATE_OBSERVATION = (6, (205, 300, 0, 24))

# Where each font's letters are cut from: (plate, line number, its text without spaces).
SOURCES = {
    "large": [(NAMEPLATE_TOLWYN, 0, "ADMIRAL"), (NAMEPLATE_TOLWYN, 1, "SIRGEOFFREY"), (NAMEPLATE_TOLWYN, 2, "TOLWYN"),
              (NAMEPLATE_DEVEREAUX, 0, "COL.JEANNETTE"), (NAMEPLATE_DEVEREAUX, 1, "DEVEREAUX")],
    "small": [(NAMEPLATE_TOLWYN, 3, "COMMANDER"), (NAMEPLATE_TOLWYN, 4, "14THFLEET"), (NAMEPLATE_DEVEREAUX, 3, "FLIGHTGROUP")],
    "plate": [(SHIP_PLATE_BRIDGE, 0, "CVS65")],
    "round": [(SHIP_PLATE_BRIDGE, 1, "CONCORDIA")],
}
# Letters the plates lack, drawn in their styles: B and P from the large R
# (its bowl) over D's foot or a bare stem; the plate font's T; the rounded
# font's U, L and E after its O and C.
DRAWN = {
    "large": {"B": ["####.", "#..#.", "#####", "##..#", "#####"],
              "P": ["####.", "#..#.", "#####", "##...", "##..."]},
    "plate": {"T": ["#####", "..#..", "..#..", "..#..", "..#.."]},
    "round": {"U": ["#.#", "#.#", "#.#", ".#."],
              "L": ["#..", "#..", "#..", "###"],
              "E": [".##", "##.", "#..", ".##"]},
}
# The new lines, plate by plate: (line number, font, text); lines not named keep theirs.
RELETTER = [
    (NAMEPLATE_TOLWYN, [(0, "large", "CAPTAIN"), (1, "large", "WAYNE"), (2, "large", "BANBRIDGE"),
                        (3, "small", "COMMANDING"), (4, "small", "OFFICER")]),
    (NAMEPLATE_DEVEREAUX, [(0, "large", "LT. COLONEL"), (1, "large", "BERGSTROM")]),
    (SHIP_PLATE_BRIDGE, [(0, "plate", "TCS"), (1, "round", "CULLODEN")]),
    (SHIP_PLATE_OBSERVATION, [(0, "plate", "TCS"), (1, "round", "CULLODEN")]),
]


def _door(block):
    """(header, rows, tail) of a door block: the sprite decoded, the palette after the table."""
    size, first = struct.unpack_from("<II", block, 0)
    sprite = block[first & 0xFFFFFF:size]
    x2, x1, y1, y2, rows = wc2data.decode_sprite(sprite[:wc2data.sprite_length(sprite)])
    return (x2, x1, y1, y2), rows, block[size:]


def _lines(rows, box):
    """The text lines in a box: [(top, bottom, [(x0, x1)] the glyphs)]."""
    x0, x1, y0, y1 = box
    ys = [y for y in range(y0, y1) if any(rows.get(y, {}).get(x) == TEXT for x in range(x0, x1))]
    bands = []
    for y in ys:
        if bands and y == bands[-1][1] + 1:
            bands[-1][1] = y
        else:
            bands.append([y, y])
    out = []
    for top, bottom in bands:
        cols = [x for x in range(x0, x1) if any(rows.get(y, {}).get(x) == TEXT for y in range(top, bottom + 1))]
        glyphs = []
        for x in cols:
            if glyphs and x == glyphs[-1][1] + 1:
                glyphs[-1][1] = x
            else:
                glyphs.append([x, x])
        out.append((top, bottom, glyphs))
    return out


def _fonts(doors):
    fonts = {}
    for font, sources in SOURCES.items():
        glyphs = fonts.setdefault(font, {})
        for (door, box), line, text in sources:
            top, bottom, cols = _lines(doors[door][1], box)[line]
            assert len(cols) == len(text), (font, text, len(cols))
            for ch, (a, b) in zip(text, cols):
                glyphs.setdefault(ch, ["".join("#" if doors[door][1].get(y, {}).get(x) == TEXT else "." for x in range(a, b + 1))
                                       for y in range(top, bottom + 1)])
        glyphs.update(DRAWN.get(font, {}))
    return fonts


def _width(font, glyphs, text):
    w = 0
    for i, ch in enumerate(text):
        if ch == " ":
            w += WORD_GAP[font]
        else:
            w += len(glyphs[ch][0]) + (LETTER_GAP if i + 1 < len(text) and text[i + 1] != " " else 0)
    return w


def _reletter(rows, box, changes, fonts):
    x0, x1, y0, y1 = box
    lines = _lines(rows, box)
    for number, font, text in changes:
        top, bottom, cols = lines[number]
        left, right = cols[0][0], cols[-1][1]
        # off with the old text: each of its pixels takes the colour two columns
        # along (the plate's dither alternates by column) that is not text
        for y in range(top, bottom + 1):
            row = rows[y]
            for x in range(left, right + 1):
                if row.get(x) == TEXT:
                    for d in range(2, 40, 2):
                        c = row.get(x - d) if row.get(x - d, TEXT) != TEXT else row.get(x + d)
                        if c is not None and c != TEXT:
                            row[x] = c
                            break
        glyphs = fonts[font]
        assert all(len(glyphs[ch]) == bottom - top + 1 for ch in text if ch != " "), (font, text)
        width = _width(font, glyphs, text)
        x = (left + right + 1) // 2 - width // 2
        assert x0 <= x and x + width <= x1, (text, x, width)
        for ch in text:
            if ch == " ":
                x += WORD_GAP[font]
                continue
            for r, bits in enumerate(glyphs[ch]):
                for i, bit in enumerate(bits):
                    if bit == "#":
                        rows[top + r][x + i] = TEXT
            x += len(glyphs[ch][0]) + LETTER_GAP


def culloden_doors(doors_v00):
    """DOORS.V00's bytes -> our DOORS.V45's: the same doors with the Culloden's plates."""
    parts = wc2data.read_container(doors_v00)
    doors = [_door(b) for _, b in parts]
    fonts = _fonts(doors)
    blocks = [b for _, b in parts]
    for (door, box), changes in RELETTER:
        header, rows, tail = doors[door]
        _reletter(rows, box, changes, fonts)
        sprite = wc2data.encode_sprite(*header, rows)
        if len(sprite) % 2:
            sprite += b"\0"
        blocks[door] = struct.pack("<II", 8 + len(sprite), 8) + sprite + tail
    return wc2data.compose_container(blocks, [f for f, _ in parts])
