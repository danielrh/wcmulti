#!/usr/bin/env python3
"""wc2data.py -- Wing Commander II's data files, read and written.

What is here was found by reading the game (SO2.EXE, see docs/so3.md):

* The block container (every GAMEDAT file): a dword of the file's size, a
  table of dword entries up to the first block (the low 24 bits an offset,
  the high byte a flag: 0xE0 plain data, 0xC1 a table of shapes, 0xFF an
  empty block, 0x20 LZW-compressed with a dword of plain size in front),
  then the blocks.  `read_container` / `write_container`.
* The LZW of the compressed blocks: codes of 9 to 12 bits, LSB first, 256
  resets the dictionary, 257 ends.  `lzw_decode`.
* The IFF scene files (CAMPAIGN.S0n, GAMEFLOW.S0n, SERIES.S0n, INCIDENT.S0n):
  `FORM CSCP` with big-endian chunk sizes; `FORM TEXT` / `SYMB` is the
  scene's strings, NUL separated, which the script refers to by index.
  `iff_chunks`, `series_set_strings`.
* MODULE.00n, the missions: seven tables indexed by series * 4 + mission
  (16 series of 4), as the loader (SO2.EXE ovr117:0000) reads them:
  header 24, nav points 1010 (10 x 101), objectives 512 (8 x 64), mission
  ships 960 (16 x 60), mission name 40, series name 40 (per series), and
  222 bytes nobody has seen used.  `Module`, `nav_record`, `ship_record`,
  `objective_record`, `header_record`.
* SHIP.Vnn: a fighter is five packets (a tree of 37 view sprites, a 3-child
  table, the per-view hardpoint words, the 243-byte record, an 11-child
  table); capital ships and bases are 41 blocks (37 one-child trees, the
  3-child table, the record, the 11-child table, the words).  The record:
  20 bytes of name, word 20 the kind (0x0C fighter, 0x0D capital ship, 0x0E
  base), word 22 the type number, word 24 the class the missions quote.
  `ship_record_of`, `academy_fighter_to_wc2`, `academy_cockpit_to_wc2`.
"""
import os
import math
import struct

# ---------------------------------------------------------------------------
# The block container


def lzw_decode(src, expect=None):
    out = bytearray()
    dict_ = []
    nbits = 9
    prev = None

    def reset():
        nonlocal dict_, nbits, prev
        dict_ = [bytes([i]) for i in range(256)] + [b"", b""]
        nbits = 9
        prev = None

    reset()
    bitpos = 0
    total_bits = len(src) * 8
    while bitpos + nbits <= total_bits:
        code = 0
        for i in range(nbits):
            b = bitpos + i
            code |= ((src[b >> 3] >> (b & 7)) & 1) << i
        bitpos += nbits
        if code == 256:
            reset()
            continue
        if code == 257:
            break
        if code < len(dict_):
            entry = dict_[code]
        elif code == len(dict_) and prev is not None:
            entry = prev + prev[:1]
        else:
            raise ValueError("bad LZW code %d at bit %d" % (code, bitpos))
        out += entry
        if prev is not None:
            dict_.append(prev + entry[:1])
        prev = entry
        if len(dict_) == (1 << nbits) and nbits < 12:
            nbits += 1
        if expect is not None and len(out) >= expect:
            break
    return bytes(out)


def read_container(path):
    """[(flag, bytes)] with compressed blocks decompressed (their flag kept)."""
    d = open(path, "rb").read() if isinstance(path, str) else path
    first = struct.unpack("<I", d[4:8])[0] & 0xFFFFFF
    n = (first - 4) // 4
    entries = [struct.unpack("<I", d[4 + 4 * i:8 + 4 * i])[0] for i in range(n)]
    out = []
    for i in range(n):
        off, flag = entries[i] & 0xFFFFFF, entries[i] >> 24
        nxt = (entries[i + 1] & 0xFFFFFF) if i + 1 < n else len(d)
        block = d[off:nxt]
        if flag == 0x20:
            expect = struct.unpack("<I", block[:4])[0]
            block = lzw_decode(block[4:], expect)
        out.append((flag, block))
    return out


def compose_container(parts, flags=None):
    """The file for blocks `parts` (bytes) with `flags` (0xE0 when None)."""
    n = len(parts)
    off = 4 + 4 * n
    table = []
    for i, p in enumerate(parts):
        fl = 0xE0 if flags is None else flags[i]
        if fl == 0x20:
            fl = 0xE0   # we never write compressed blocks
        table.append(off | (fl << 24))
        off += len(p)
    return struct.pack("<I", off) + b"".join(struct.pack("<I", e) for e in table) + b"".join(parts)


def write_container(path, parts, flags=None):
    with open(path, "wb") as f:
        f.write(compose_container(parts, flags))


def blocks_of(path):
    """The blocks alone."""
    return [b for _, b in read_container(path)]


# ---------------------------------------------------------------------------
# IFF (the scene files)


def iff_chunks(data, off, end):
    """[(id, start, end)] of the chunks between off and end; a FORM's id is
    b'FORM' + its type and its range is its contents after the type."""
    out = []
    while off + 8 <= end:
        cid = data[off:off + 4]
        size = struct.unpack(">I", data[off + 4:off + 8])[0]
        if cid == b"FORM":
            out.append((cid + data[off + 8:off + 12], off + 12, off + 8 + size))
        else:
            out.append((cid, off + 8, off + 8 + size))
        off += 8 + size + (size & 1)
    return out


def iff_form(ftype, children):
    """A FORM of type `ftype` (4 bytes) around `children` (raw chunk bytes)."""
    body = ftype + b"".join(children)
    return b"FORM" + struct.pack(">I", len(body)) + body + (b"\0" if len(body) & 1 else b"")


def iff_chunk(cid, payload):
    return cid + struct.pack(">I", len(payload)) + payload + (b"\0" if len(payload) & 1 else b"")


def iff_raw(data, start, end, is_form):
    """The bytes of a chunk as listed by iff_chunks, header included."""
    head = start - (12 if is_form else 8)
    size = struct.unpack(">I", data[head + 4:head + 8])[0]
    return data[head:head + 8 + size + (size & 1)]


def scene_strings(block):
    """The strings of a scene block (FORM CSCP), by index."""
    top = iff_chunks(block, 0, len(block))[0]
    for cid, s, e in iff_chunks(block, top[1], top[2]):
        if cid == b"FORMTEXT":
            symb = iff_chunks(block, s, e)[0]
            return block[symb[1]:symb[2]].split(b"\0")[:-1]
    return []


def series_set_strings(block, strings, drop_speech=True):
    """A scene block with its FORM TEXT replaced by `strings` (bytes, by
    index: the script names them by number, so keep the count) and, with
    drop_speech, without its FORM SPCH (the digitised speech of the old
    lines, which a scene without the form does without)."""
    top = iff_chunks(block, 0, len(block))[0]
    assert top[0] == b"FORMCSCP", top[0]
    children = []
    for cid, s, e in iff_chunks(block, top[1], top[2]):
        if cid == b"FORMTEXT":
            children.append(iff_form(b"TEXT", [iff_chunk(b"SYMB", b"\0".join(strings) + b"\0")]))
        elif cid == b"FORMSPCH" and drop_speech:
            continue
        else:
            children.append(iff_raw(block, s, e, cid.startswith(b"FORM")))
    return iff_form(b"CSCP", children)


# ---------------------------------------------------------------------------
# MODULE.00n


class Module(object):
    STRIDES = [24, 1010, 512, 960, 40, 40, 222]
    NAV_SIZE, SHIP_SIZE, OBJ_SIZE = 101, 60, 64

    def __init__(self, path):
        self.flags_blocks = read_container(path)
        self.tables = [bytearray(b) for _, b in self.flags_blocks]
        assert len(self.tables) == 7, len(self.tables)

    def save(self, path):
        write_container(path, [bytes(t) for t in self.tables])

    def mission_parts(self, s, m):
        idx = s * 4 + m
        parts = [bytes(self.tables[i][idx * self.STRIDES[i]:(idx + 1) * self.STRIDES[i]]) for i in range(7)]
        parts[5] = bytes(self.tables[5][s * 40:(s + 1) * 40])
        return parts

    def set_mission(self, s, m, header, navs, objectives, ships, name, series_name=None, extra=None):
        idx = s * 4 + m

        def put(table, stride, data):
            data = data.ljust(stride, b"\0")[:stride]
            self.tables[table][idx * stride:(idx + 1) * stride] = data

        put(0, 24, header)
        put(1, 1010, b"".join(navs))
        put(2, 512, b"".join(objectives))
        put(3, 960, b"".join(ships))
        put(4, 40, name.encode("latin1"))
        if series_name is not None:
            self.tables[5][s * 40:(s + 1) * 40] = series_name.encode("latin1").ljust(40, b"\0")[:40]
        put(6, 222, extra or b"")

    def clear_mission(self, s, m):
        self.set_mission(s, m, header_record(0, 0, -1, []), [], [objective_record(-1, -1, "")], [], "")


def header_record(launch, carrier, player, wing):
    """Words: 0, the mission ship launched from, the one to land on, the
    player's mission ship, then up to eight mission ships of the player's
    wing (-1 ends it)."""
    words = [0, launch, carrier, player] + list(wing) + [-1] * (8 - len(wing))
    return struct.pack("<12h", *words)


def nav_record(name, pos, visible=1, radius=15000, flag2d=0, reveals=(), steps=(), types=(), ships=()):
    """A nav point.  pos in km; reveals: (value, nav) pairs written to that
    nav's visible byte on arrival; steps: up to two (type, p1, p2), type 1 a
    scene (p1 the scene, p2 how: 0 after forty frames); types: up to three
    (type, kind, class) ship types to load here; ships: up to ten mission
    ship indices."""
    r = bytearray(101)
    r[:20] = name.encode("latin1")[:19].ljust(20, b"\0")
    r[0x1e] = visible
    struct.pack_into("<iii", r, 0x1f, pos[0] * 256, pos[1] * 256, pos[2] * 256)
    struct.pack_into("<h", r, 0x2b, radius)
    r[0x2d] = flag2d & 0xFF
    for i in range(4):
        if i < len(reveals):
            r[0x2f + 2 * i], r[0x30 + 2 * i] = reveals[i][0] & 0xFF, reveals[i][1] & 0xFF
        else:
            r[0x2f + 2 * i], r[0x30 + 2 * i] = 0xFF, 0xFF
    assert len(steps) <= 2, "the third step overlaps the first ship type"
    for i in range(2):
        if i < len(steps):
            r[0x37 + 3 * i], r[0x38 + 3 * i], r[0x39 + 3 * i] = steps[i][0] & 0xFF, steps[i][1] & 0xFF, steps[i][2] & 0xFF
        else:
            r[0x37 + 3 * i], r[0x38 + 3 * i], r[0x39 + 3 * i] = 0xFF, 0, 0
    r[0x3d], r[0x3e] = 0xFF, 0
    assert len(types) <= 3
    for i in range(3):
        t, k, c = types[i] if i < len(types) else (-1, 0, 0)
        struct.pack_into("<h", r, 0x3f + 2 * i, t)
        struct.pack_into("<h", r, 0x45 + 2 * i, k)
        struct.pack_into("<h", r, 0x4b + 2 * i, c)
    assert len(ships) <= 10
    for i in range(10):
        struct.pack_into("<h", r, 0x51 + 2 * i, ships[i] if i < len(ships) else -1)
    return bytes(r)


def ship_record(name, shiptype, cls, side, pos=(0, 0, 0), yaw=0, pitch=0, roll=0, behave=-1, important=0,
                formation=0, radius=0, b33=4, ai=4, leader=-1, group=-1, objective=-1, pilot=0, alt=0):
    """A mission ship.  side 0 Confederation, 1 Kilrathi, 2 the scenery;
    behave (word 0x1b): 2 a leader placed where it stands, 3 a patrol, 4 a
    wingman in formation, 6 an escortee, -1 stationary; important: the
    byte the game keeps per slot (set on wingmen, escortees, carriers);
    radius: for a field its size minus 3000 km, for a ship a small number
    the game scales up; ai (word 0x35): 6 the player, 5 the player's
    wingman, 4 the usual, 2 and 3 seen on attackers; leader / objective:
    mission ship indices (-1 none): a follower's formation leader, and the
    ship a wing is about (what it attacks or protects); group: a small
    number shared by a wing, -1 for scenery; pilot: the comm personality
    (COMMUNIC.Snn), 0 none."""
    r = bytearray(60)
    r[:19] = name.encode("latin1")[:19].ljust(19, b"\0")
    r[0x13] = alt
    r[0x14] = shiptype & 0xFF
    struct.pack_into("<h", r, 0x15, cls)
    struct.pack_into("<h", r, 0x17, side)
    struct.pack_into("<h", r, 0x19, important)
    struct.pack_into("<h", r, 0x1b, behave)
    struct.pack_into("<iii", r, 0x1e, pos[0] * 256, pos[1] * 256, pos[2] * 256)
    struct.pack_into("<hhh", r, 0x2a, yaw, pitch, roll)
    r[0x30] = formation
    struct.pack_into("<h", r, 0x31, radius)
    r[0x33] = b33
    struct.pack_into("<h", r, 0x35, ai)
    r[0x38] = leader & 0xFF
    r[0x39] = group & 0xFF
    r[0x3a] = objective & 0xFF
    r[0x3b] = pilot & 0xFF
    return bytes(r)


def objective_record(kind, param, label):
    """kind: 0 reach nav `param`, 1 land on mission ship `param`, 2 keep
    mission ship `param` alive, 3 meet mission ship `param`, 4 destroy
    mission ship `param`, 5 clear nav `param` of enemies, 6 pick up mission
    ship `param` (a pod, a pilot); -1 ends the list.  A label starting with
    '.' is a note, not shown."""
    return struct.pack("<hh", kind, param) + label.encode("latin1")[:59].ljust(60, b"\0")


# ---------------------------------------------------------------------------
# SHIP.Vnn


def ship_record_of(path):
    """(name, kind, type, class) of a ship file."""
    bl = blocks_of(path)
    rec = bl[3] if len(bl) == 5 else bl[38]
    name = rec[:20].split(b"\0")[0].decode("latin1")
    kind, shiptype, cls = struct.unpack_from("<HHH", rec, 20)
    return name, kind, shiptype, cls


# The 243-byte record, as the game reads it (SO2.EXE ovr115:003B copies the
# shields and armour into a new ship, ovr115:1C2C the recharge rate, HCl's
# editor names the rest): word offsets.
REC = dict(kind=20, type=22, cls=24, shield_recharge=38, max_speed=42, acceleration=44, yaw=54, pitch=56, roll=58,
           shield_front=223, shield_rear=225, armor_front=227, armor_rear=229, armor_left=231, armor_right=233)
HARDPOINTS_AT, HARDPOINT_SIZE, HARDPOINTS_MAX = 62, 10, 16   # (code << 8 | flags, x, y, z, w) each, zero ends the list
# Weapon codes: the slot of the weapon's own record in the game's type table.
WEAPON = dict(laser=7, neutron=8, mass_driver=9, particle=10, dart_df=15, javelin_hs=16, pilum_ff=17, spiculum_ir=18,
              torpedo=19, chaff=20, porcupine=21)


def record_stats(rec):
    """The named fields of a ship record, as a dict."""
    return dict((k, struct.unpack_from("<h", rec, off)[0]) for k, off in REC.items())


def set_record_stats(rec, **fields):
    """A copy of the record with the named fields (REC) set; max_speed in kps (the record keeps kps / 10)."""
    r = bytearray(rec)
    for k, v in fields.items():
        if k == "max_speed":
            v = v // 10
        struct.pack_into("<h", r, REC[k], v)
    return bytes(r)


def hardpoints(rec):
    """[(code, flags, x, y, z, w)] of a record's weapon list."""
    out = []
    for i in range(HARDPOINTS_MAX):
        off = HARDPOINTS_AT + HARDPOINT_SIZE * i
        cf, x, y, z, w = struct.unpack_from("<Hhhhh", rec, off)
        if cf == 0:
            break
        out.append((cf >> 8, cf & 0xFF, x, y, z, w))
    return out


def set_hardpoints(rec, points):
    """A copy of the record with the weapon list replaced (same tuples as hardpoints gives)."""
    assert len(points) <= HARDPOINTS_MAX
    r = bytearray(rec)
    for i in range(HARDPOINTS_MAX):
        off = HARDPOINTS_AT + HARDPOINT_SIZE * i
        if i < len(points):
            code, flags, x, y, z, w = points[i]
            struct.pack_into("<Hhhhh", r, off, (code << 8) | flags, x, y, z, w)
        else:
            struct.pack_into("<Hhhhh", r, off, 0, 0, 0, 0, 0)
    return bytes(r)


def _strip_nested_flags(block):
    """A table block whose child offsets carry Academy's 0xE0 flag bytes ->
    plain offsets (what WC2's own files have)."""
    if len(block) < 8:
        return block
    size, first = struct.unpack("<II", block[:8])
    if size != len(block) or (first >> 24) != 0xE0:
        return block
    n = ((first & 0xFFFFFF) - 4) // 4
    out = bytearray(block)
    for i in range(n):
        v = struct.unpack_from("<I", block, 4 + 4 * i)[0]
        if (v >> 24) != 0xE0:
            return block
        struct.pack_into("<I", out, 4 + 4 * i, v & 0xFFFFFF)
    return bytes(out)


def _tree(children):
    off = 4 + 4 * len(children)
    offs = []
    for c in children:
        offs.append(off)
        off += len(c)
    return struct.pack("<I", off) + b"".join(struct.pack("<I", o) for o in offs) + b"".join(children)


def _unwrap_view(block):
    size, first = struct.unpack("<II", block[:8])
    assert size == len(block) and (first & 0xFFFFFF) == 8, "not a one-child tree"
    return block[8:]


FIGHTER_FLAGS = [0xC1, 0xC1, 0xE0, 0xE0, 0xC1]
COCKPIT_FLAGS = [0xC1, 0xC1, 0xC1, 0xC1, 0xE0, 0xE0, 0xE0, 0xE0, 0xC1, 0xFF, 0xC1, 0xFF, 0xC1, 0xE0, 0xC1, 0xE0]


def academy_fighter_to_wc2(ext_blocks, shiptype):
    """Academy's 41-block fighter (the capital-ship layout) as WC2's
    five-packet fighter file: (parts, flags)."""
    assert len(ext_blocks) == 41, len(ext_blocks)
    views = [_unwrap_view(b) for b in ext_blocks[:37]]
    rec = bytearray(ext_blocks[38])
    struct.pack_into("<H", rec, 22, shiptype)
    parts = [_tree(views), _strip_nested_flags(ext_blocks[37]), ext_blocks[40], bytes(rec), _strip_nested_flags(ext_blocks[39])]
    return parts, FIGHTER_FLAGS


def academy_cockpit_to_wc2(cp_blocks):
    """Academy's 16-block cockpit file (the layout of WC2's Sabre) with its
    tables' child offsets made plain: (parts, flags)."""
    assert len(cp_blocks) == 16, len(cp_blocks)
    return [_strip_nested_flags(b) for b in cp_blocks], COCKPIT_FLAGS


if __name__ == "__main__":
    import sys
    for p in sys.argv[1:]:
        if os.path.basename(p).upper().startswith("SHIP.V"):
            print(p, ship_record_of(p))
        else:
            for i, (fl, b) in enumerate(read_container(p)):
                print("%s block %d flag %02x %d bytes" % (p, i, fl, len(b)))


# ---- view sprites (WC2's row-run format, as HCl's WC2VIEW reads it) ----
#
# Header: four int16 x2, x1, y1, y2: the sprite spans x in [-x1-1, x2] and y in
# [-y1-1, y2] around its origin, so it is x1+x2+2 wide and y1+y2+2 tall.  Then
# records of int16 key, vx, vy: key even = key/2 raw pixels from (vx, vy); key
# odd = key/2 pixels made of sub-runs, a byte k each: k even = k/2 raw pixels,
# k odd = one colour k/2 times; key 0 ends.  Pixels not covered are clear.

def decode_sprite(data):
    """(x2, x1, y1, y2, rows) with rows a dict y -> {x: colour} in origin coordinates."""
    x2, x1, y1, y2 = struct.unpack_from("<hhhh", data, 0)
    rows = {}
    off = 8
    while off + 6 <= len(data):
        key, vx, vy = struct.unpack_from("<hhh", data, off)
        off += 6
        if key == 0:
            break
        row = rows.setdefault(vy, {})
        x = vx
        if key % 2 == 0:
            for _ in range(key // 2):
                row[x] = data[off]; off += 1; x += 1
        else:
            n = 0
            while n < key // 2 and off < len(data):
                k = data[off]; off += 1
                if k % 2 == 0:
                    for _ in range(k // 2):
                        row[x] = data[off]; off += 1; x += 1; n += 1
                else:
                    c = data[off]; off += 1
                    for _ in range(k // 2):
                        row[x] = c; x += 1; n += 1
    return x2, x1, y1, y2, rows


def encode_sprite(x2, x1, y1, y2, rows):
    """The inverse of decode_sprite: one packed record per run of set pixels."""
    out = bytearray(struct.pack("<hhhh", x2, x1, y1, y2))
    for y in sorted(rows):
        row = rows[y]
        xs = sorted(x for x in row if row[x] != 0)
        i = 0
        while i < len(xs):
            j = i
            while j + 1 < len(xs) and xs[j + 1] == xs[j] + 1:
                j += 1
            span = [row[x] for x in xs[i:j + 1]]
            out += struct.pack("<hhh", 2 * len(span) + 1, xs[i], y)
            k = 0
            while k < len(span):
                run = 1
                while k + run < len(span) and span[k + run] == span[k] and run < 127:
                    run += 1
                if run >= 3:
                    out += bytes((2 * run + 1, span[k])); k += run
                else:
                    lit = k
                    while lit < len(span) and lit - k < 127 and not (lit + 2 < len(span) and span[lit] == span[lit + 1] == span[lit + 2]):
                        lit += 1
                    out += bytes((2 * (lit - k),)) + bytes(span[k:lit]); k = lit
            i = j + 1
    out += struct.pack("<hhh", 0, 0, 0)
    return bytes(out)


def scale_sprite(data, s):
    """The sprite at s times its size (nearest pixel), its origin kept."""
    x2, x1, y1, y2, rows = decode_sprite(data)
    nx1 = int(round((x1 + 1) * s)) - 1; nx2 = int(round((x2 + 1) * s)) - 1
    ny1 = int(round((y1 + 1) * s)) - 1; ny2 = int(round((y2 + 1) * s)) - 1
    new = {}
    for y in range(-ny1 - 1, ny2 + 1):
        sy = int(math.floor((y + 0.5) / s))
        src = rows.get(sy)
        if not src:
            continue
        row = {}
        for x in range(-nx1 - 1, nx2 + 1):
            sx = int(math.floor((x + 0.5) / s))
            c = src.get(sx, 0)
            if c:
                row[x] = c
        if row:
            new[y] = row
    return encode_sprite(nx2, nx1, ny1, ny2, new)


def scale_fighter(parts, s):
    """A five-packet fighter with its 37 views and its 11 debris sprites at s
    times their size, and the per-view hardpoint pixel positions to match."""
    tree = parts[0]
    size, first = struct.unpack("<II", tree[:8])
    n = (first - 4) // 4
    offs = [struct.unpack_from("<I", tree, 4 + 4 * i)[0] & 0xFFFFFF for i in range(n)] + [len(tree)]
    views = [scale_sprite(tree[offs[i]:offs[i + 1]], s) for i in range(n)]
    parts[0] = _tree(views)
    hp = bytearray(parts[2])
    voffs = struct.unpack_from("<%dH" % n, hp, 0)
    for v in range(n):
        end = voffs[v + 1] if v + 1 < n else len(hp)
        o = voffs[v]
        while o + 12 <= end and struct.unpack_from("<h", hp, o)[0] != -1:
            x, y = struct.unpack_from("<hh", hp, o + 8)
            struct.pack_into("<hh", hp, o + 8, int(round(x * s)), int(round(y * s)))
            o += 12
    parts[2] = bytes(hp)
    deb = parts[4]
    size, first = struct.unpack("<II", deb[:8])
    m = (first - 4) // 4
    doffs = [struct.unpack_from("<I", deb, 4 + 4 * i)[0] & 0xFFFFFF for i in range(m)] + [len(deb)]
    parts[4] = _tree([scale_sprite(deb[doffs[i]:doffs[i + 1]], s) for i in range(m)])
    return parts
