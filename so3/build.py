#!/usr/bin/env python3
"""so3/build.py -- Special Operations 3 from an installed Wing Commander II.

  so3/build.py --hellcat DIR [--game wc2] [--out build-so3] [--test-game]

Writes OUT/plain/: GAMEDAT/ with every file the program names by its number
3 (SO3.EXE is SO2.EXE with its program number set to 3, so that it reads the
.S03 / .003 / .V03 files and savegame.so3: that patch is a list in
OUT/plain.json which the page applies to the player's own SO2.EXE; the
program is never shipped) (copies of Special Operations 2's,
MODULE.003 and SERIES.S03 with our missions and texts in them), the Hellcat
as ship type 45 (HCl's and Howard Day's conversion for Academy, repacked
as a WC2 fighter) and its AI file.  --test-game makes OUT/game, a whole
game directory to fly it from natively.  (A third player's seat is the
multiplayer layer's doing, a second copy of the wingman spawned by the
host's game: no second data set; tools/wc2wing can still write one into a
MODULE file for a mission authored with two wing fighters.)

Everything in OUT comes from the game's own files and must not be
committed or published in the clear: so3/pack.mjs encrypts it for the page
with a key only an owner of the game has (see so3/README.md).
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
sys.path.insert(0, os.path.join(ROOT, "so3"))
import wc2data   # noqa: E402
import campaign  # noqa: E402
import doorplates  # noqa: E402

# GAMEDAT files whose names the program builds from its number (seen in a
# traced run of SO2.EXE through the barracks, a briefing, a flight and a
# landing, plus the ones its code names).  Written as the "2" name.
NUMBERED = [
    "ALTSHIPS.002", "CAMPAIGN.S02", "GAMEFLOW.S02", "INCIDENT.S02", "MODULE.002", "PLANETS.V02",
    "SECTOR.002", "SERIES.S02", "STARTGLB.002", "TEMPGLOB.002",
]
SO2_SHA256 = "f9b53260a85340416febc68dd10dc0efb5570a5f97c872f3425e6f87e65207ae"   # GOG's SO2.EXE


def renumber(name, n):
    """ALTSHIPS.002 -> ALTSHIPS.003, SERIES.S02 -> SERIES.S03, PLANETS.V02 -> PLANETS.V03."""
    stem, ext = name.rsplit(".", 1)
    return "%s.%s%d" % (stem, ext[:-1], n)


# The scene files name each other by literal file names in their scripts
# ("series.s02" in GAMEFLOW.S02 and CAMPAIGN.S02, ...): in the copies those
# point at our set, the same length each.
SCENE_REFERENCES = [b"series.s02", b"gameflow.s02", b"incident.s02", b"campaign.s02"]
# The briefing's comm scene draws the cockpit of Special Operations 2's first
# mission, the Rapier's (pcship.v01, seen in a traced run): in our copies that
# name is the Hellcat's cockpit file.
SCENE_RENAMES = [(b"pcship.v01", b"pcship.v%02d" % campaign.HELLCAT)]


def rename_references(data, frm=2, to=3):
    """The scene files' references to data set `frm` made references to `to`
    (and, from the game's own set, the Rapier's cockpit made the Hellcat's)."""
    for ref in SCENE_REFERENCES:
        for form in (ref, ref.upper(), ref[:1].upper() + ref[1:]):
            data = data.replace(form[:-1] + str(frm).encode(), form[:-1] + str(to).encode())
    if frm == 2:
        for old, new in SCENE_RENAMES:
            assert len(old) == len(new)
            data = data.replace(old, new)
    return data


# Faces.  Special Operations 2's scenes draw Tolwyn where our Captain speaks
# and Angel where our wing commander does: CAMPAIGN.S03's shape list names
# recoloured copies of their portraits instead (Banbridge: brown hair, a
# blue collar; Bergstrom: red hair, a grey uniform), and Doomsday's own
# portrait (WC2's people.v09) where SO2 drew Talon.  Palette indices from
# the briefing palette: 112-127 skin, 160-175 greys, 64-79 yellow to red,
# 40-46 blues, 136-142 auburn, 228-238 browns.
BANBRIDGE = {160 + i: 232 + min(6, i) for i in range(10)}
BANBRIDGE.update({71 + i: 40 + i for i in range(7)})
BERGSTROM = {121 + i: 136 + i for i in range(5)}
BERGSTROM.update({52 + i: 164 + i for i in range(7)})

# Launches and landings.  Special Operations 2's scenes play Wing Commander
# II's Concordia films (INCIDENT.S00: the carrier's hull and hangar around
# the player's fighter, one launch and one landing per fighter type, and
# stills of her hull: head-on for a red alert, abeam before a first launch);
# the Culloden is a
# Waterloo-class cruiser.  Special Operations 2's own launch, landing and
# still for the missions it flies from a cruiser are INCIDENT.S02 blocks 5,
# 4 and 1 (the .V39 films, our INCIDENT.S03): the still is a Waterloo, the
# launch and landing draw the Concordia again, which our copies replace
# (carrier_stills below).  Every film table entry naming one of WC2's
# Concordia films names those instead (the user, 2026-10-18:
# "the so3 campaign talks about the Waterloo cruiser but the scenes don't
# take from that"); the scripts play films by number, so they are untouched.
CONCORDIA_LAUNCHES = [2, 10, 11, 12, 13, 17, 23, 98, 99, 100, 101, 102]
CONCORDIA_LANDINGS = [3, 5, 6, 7, 8, 9, 24]
CONCORDIA_STILLS = [0, 4]
WATERLOO_LAUNCH, WATERLOO_LANDING, WATERLOO_STILL = 5, 4, 1


# ... but those "Waterloo" launch and landing films draw the Concordia (the
# user, 2026-10-18: only the red alert's still, laungame.v39, is a
# Waterloo): each is one still of the carrier (launch.v39, land.v39), drawn
# where it stands while the fighters (launch.v30, land.v30) move.  Our
# INCIDENT.S03 names stills of our own instead: the Waterloo's in-flight
# sprite (SHIP.V17) fitted into the box the Concordia filled, in the game's
# palette (WC2PAL; the films' palette is the same for every colour the
# sprite uses, dumped from a running film).  And the user asked for her
# from different angles (2026-10-18): the launch and landing films are
# copied, appended to INCIDENT.S03 each naming a still of its own, and each
# scene block sends its launch and landing to the copy its number picks, so
# mission after mission she is seen from another side.  The two late scenes
# that show her with another ship (INCIDENT.S02 blocks 15 and 21) get views
# of their own.
INCIDENT_BLOCKS = 26                     # INCIDENT.S02's; the copies follow
LANDING_VIEWS = [10, 12, 3, 24, 31]      # LAND.V45.. (block 4, then its copies)
LAUNCH_VIEWS = [10, 11, 26, 12, 27]      # LAUNCH.V45.. (block 5, then its copies)
LATE_LANDING_STILLS = [(15, "LAND.V50", 25), (21, "LAND.V51", 32)]
LANDING_BLOCKS = [WATERLOO_LANDING] + list(range(INCIDENT_BLOCKS, INCIDENT_BLOCKS + len(LANDING_VIEWS) - 1))
LAUNCH_BLOCKS = [WATERLOO_LAUNCH] + list(range(LANDING_BLOCKS[-1] + 1, LANDING_BLOCKS[-1] + len(LAUNCH_VIEWS)))
# Wing Commander II's films of the Concordia head-on in front of a planet
# (INCIDENT.S00 blocks 38, 62, 64: wormhole.v01 with planets, the last with
# the repair bay): copied to the end of our INCIDENT.S03 naming a Waterloo
# head-on instead (view 14, from a little above; view 28, from below).
HEADON_FILMS = [(38, "WORMHOLE.V45", 14), (62, "WORMHOLE.V45", 14), (64, "WORMHOLE.V46", 28)]
HEADON_BLOCKS = list(range(LAUNCH_BLOCKS[-1] + 1, LAUNCH_BLOCKS[-1] + 1 + len(HEADON_FILMS)))
# Wing Commander II's door films (INCIDENT.S00 blocks 81-86, 88: doors.v00)
# show the Concordia's plates; copied to the end of our INCIDENT.S03 naming
# DOORS.V45, the same doors with the Culloden's plates (so3/doorplates.py).
DOOR_FILMS = [81, 82, 83, 84, 85, 86, 88]
DOOR_BLOCKS = list(range(HEADON_BLOCKS[-1] + 1, HEADON_BLOCKS[-1] + 1 + len(DOOR_FILMS)))
# Wing Commander II's flight deck and repair deck films print the place
# themselves ("Flight Deck, TCS Concordia": INCIDENT.S00 blocks 89-94;
# "Repair Deck, TCS Concordia": 108-112): copied to the end of our
# INCIDENT.S03 with the Culloden's name in that one string.
CAPTION_FILMS = [(b, "Flight Deck,\nTCS Culloden") for b in range(89, 95)] + [(b, "Repair Deck,\nTCS Culloden") for b in range(108, 113)]
CAPTION_BLOCKS = list(range(DOOR_BLOCKS[-1] + 1, DOOR_BLOCKS[-1] + 1 + len(CAPTION_FILMS)))


def carrier_stills():
    """[(the Concordia still, our file, the Waterloo's view)] for every still our INCIDENT.S03 names."""
    return ([("LAND.V39", "LAND.V%02d" % (45 + i), v) for i, v in enumerate(LANDING_VIEWS)] +
            [("LAUNCH.V39", "LAUNCH.V%02d" % (45 + i), v) for i, v in enumerate(LAUNCH_VIEWS)] +
            [("LAND.V39", name, v) for _, name, v in LATE_LANDING_STILLS] +
            sorted(set(("WORMHOLE.V01", name, v) for _, name, v in HEADON_FILMS)))


def waterloo_incidents(data, gamedat):
    """INCIDENT.S03 (a copy of INCIDENT.S02) naming our stills, with the
    copies of its launch and landing films and of WC2's head-on films
    appended."""
    parts = wc2data.read_container(data)
    assert len(parts) == INCIDENT_BLOCKS, len(parts)
    blocks, flags = [b for _, b in parts], [f for f, _ in parts]

    def named(b, old, new):
        assert b.count(old) == 1, old
        return b.replace(old, new)
    land, launch = blocks[WATERLOO_LANDING], blocks[WATERLOO_LAUNCH]
    blocks[3] = named(blocks[3], b"land.v39", b"land.v45")
    for b, name, _ in LATE_LANDING_STILLS:
        blocks[b] = named(blocks[b], b"land.v39", name.lower().encode())
    for i, b in enumerate(LANDING_BLOCKS):
        copy = named(land, b"land.v39", b"land.v%02d" % (45 + i))
        if b < len(blocks):
            blocks[b] = copy
        else:
            blocks.append(copy)
            flags.append(flags[WATERLOO_LANDING])
    for i, b in enumerate(LAUNCH_BLOCKS):
        copy = named(launch, b"launch.v39", b"launch.v%02d" % (45 + i))
        if b < len(blocks):
            blocks[b] = copy
        else:
            blocks.append(copy)
            flags.append(flags[WATERLOO_LAUNCH])
    assert len(blocks) == LAUNCH_BLOCKS[-1] + 1
    wc2 = wc2data.read_container(os.path.join(gamedat, "INCIDENT.S00"))
    for (b, name, _), at in zip(HEADON_FILMS, HEADON_BLOCKS):
        assert len(blocks) == at
        blocks.append(named(wc2[b][1], b"wormhole.v01", name.lower().encode()))
        flags.append(wc2[b][0])
    for b, at in zip(DOOR_FILMS, DOOR_BLOCKS):
        assert len(blocks) == at
        blocks.append(named(wc2[b][1], b"doors.v00", b"doors.v45"))
        flags.append(wc2[b][0])
    for (b, caption), at in zip(CAPTION_FILMS, CAPTION_BLOCKS):
        assert len(blocks) == at
        strings = wc2data.scene_strings(wc2[b][1])
        assert b"TCS Concordia" in strings[0], (b, strings[0])
        blocks.append(wc2data.series_set_strings(wc2[b][1], [caption.encode("latin1")] + strings[1:], drop_speech=False))
        flags.append(wc2[b][0])
    return wc2data.compose_container(blocks, flags)


def waterloo_still(gamedat, name, view_number):
    """The carrier still `name` (a one-sprite table) with the Waterloo seen from `view_number` in the Concordia's place."""
    (flag, block), = wc2data.read_container(os.path.join(gamedat, name))
    first = struct.unpack_from("<I", block, 4)[0] & 0xFFFFFF
    x2, x1, y1, y2, rows = wc2data.decode_sprite(block[first:])
    xs = [x for r in rows.values() for x in r]
    ys = list(rows)
    box = (min(xs), min(ys), max(xs) - min(xs) + 1, max(ys) - min(ys) + 1)
    ship = wc2data.read_container(os.path.join(gamedat, "SHIP.V17"))
    colours = set()
    for _, b in ship[:37]:
        colours |= {c for r in wc2data.decode_sprite(b[struct.unpack_from("<I", b, 4)[0] & 0xFFFFFF:])[4].values() for c in r.values()}
    pal = open(os.path.join(gamedat, "WC2PAL"), "rb").read()
    palette = [tuple(pal[3 * i:3 * i + 3]) for i in range(256)]
    view = ship[view_number][1]
    px, w, h = wc2data.sprite_fitted(view[struct.unpack_from("<I", view, 4)[0] & 0xFFFFFF:], box[2], box[3], palette, sorted(colours))
    ox, oy = box[0] + (box[2] - w) // 2, box[1] + (box[3] - h) // 2
    new = {}
    for (x, y), c in px.items():
        new.setdefault(oy + y, {})[ox + x] = c
    return wc2data.compose_container([wc2data._tree([wc2data.encode_sprite(x2, x1, y1, y2, new)])], [flag])


def waterloo_films(blocks):
    """The scene blocks with their Concordia films made the Waterloo's (block
    k's launch and landing the copies k picks), and how many entries changed."""
    out, changed = [], 0
    for k, b in enumerate(blocks):
        mapping = dict([(("incident.s00", c), ("incident.s03", LAUNCH_BLOCKS[(k + 2) % len(LAUNCH_BLOCKS)])) for c in CONCORDIA_LAUNCHES] +
                       [(("incident.s00", c), ("incident.s03", LANDING_BLOCKS[k % len(LANDING_BLOCKS)])) for c in CONCORDIA_LANDINGS] +
                       [(("incident.s00", c), ("incident.s03", WATERLOO_STILL)) for c in CONCORDIA_STILLS] +
                       # (SO2's own scenes for its cruiser missions name the launch and landing directly)
                       [(("incident.s03", WATERLOO_LAUNCH), ("incident.s03", LAUNCH_BLOCKS[(k + 2) % len(LAUNCH_BLOCKS)])),
                        (("incident.s03", WATERLOO_LANDING), ("incident.s03", LANDING_BLOCKS[k % len(LANDING_BLOCKS)]))] +
                       [(("incident.s00", b), ("incident.s03", at)) for (b, _, _), at in zip(HEADON_FILMS, HEADON_BLOCKS)] +
                       [(("incident.s00", b), ("incident.s03", at)) for b, at in zip(DOOR_FILMS, DOOR_BLOCKS)] +
                       [(("incident.s00", b), ("incident.s03", at)) for (b, _), at in zip(CAPTION_FILMS, CAPTION_BLOCKS)])
        b, n = wc2data.retarget_films(b, mapping)
        out.append(b)
        changed += n
    return out, changed


RECOLOURED_FACES = [("PEOPLE.V02", "PEOPLE.V46", BANBRIDGE), ("PEOPLE.V01", "PEOPLE.V47", BERGSTROM)]
FACE_FILES = [(b"people.v02", b"people.v46"), (b"people.v01", b"people.v47"), (b"people.v32", b"people.v09")]

# SO3.EXE is SO2.EXE with these byte sequences replaced, each found the given
# number of times.  The page applies the same list to the player's own copy
# (web/so3.js); the program itself is never shipped.
def exe_patch(n, target):
    return {
        "source": "SO2.EXE",
        "target": target,
        "sha256": SO2_SHA256,
        "patches": [
            {"what": "the data-file number, mov word [cb16], 2", "find": "c70616cb0200", "replace": "c70616cb0%d00" % n, "count": 5},
            {"what": "the saved game's name", "find": b"savegame.so2".hex(), "replace": (b"savegame.so%d" % n).hex(), "count": 1},
            # The start screen's second button resumes the story from the saved
            # game's current record and then names the game whose files to play
            # with a constant of its own (ovr118:0427, mov word [bp-6], 2): left
            # at 2 it resumed SO3's record in SO2's missions.
            {"what": "the game number of a resumed story", "find": "c746fa02009a2f0088048b46fa5ec9cb", "replace": "c746fa0%d009a2f0088048b46fa5ec9cb" % n, "count": 1},
        ],
    }


EXE_PATCH = exe_patch(3, "SO3.EXE")


def apply_patch(data, spec):
    """`data` patched as `spec` says; raises if a sequence is not found the
    expected number of times (another build of the program)."""
    d = bytes(data)
    for p in spec["patches"]:
        find, repl = bytes.fromhex(p["find"]), bytes.fromhex(p["replace"])
        n = d.count(find)
        if n != p["count"]:
            raise ValueError("%s: expected %d of %s, found %d" % (spec["source"], p["count"], p["what"], n))
        d = d.replace(find, repl)
    return d


def hellcat_record(rec, sabre):
    """The Hellcat's record with campaign.py's numbers and weapons: the guns
    where HELLCAT_GUNS puts them, the missile and decoy mounts where the
    conversion left them (the Rapier's), with the codes changed; the words
    nobody has named (32-41, 46-53) the Sabre's rather than the Rapier's
    that HCl's record carried (the user's pick, 2026-10-17: a sturdier
    template for a medium fighter)."""
    rec = bytearray(rec)
    rec[32:42] = sabre[32:42]
    rec[46:54] = sabre[46:54]
    rec = wc2data.set_record_stats(bytes(rec), **campaign.HELLCAT_STATS)
    old = wc2data.hardpoints(rec)
    others = [p for p in old if p[0] >= 15]
    chaff = [p for p in others if p[0] == wc2data.WEAPON["chaff"]]
    missiles = [p for p in others if p[0] != wc2data.WEAPON["chaff"]]
    assert len(missiles) >= len(campaign.HELLCAT_MISSILES) and len(chaff) >= campaign.HELLCAT_DECOYS
    new = [(wc2data.WEAPON[name], x, y, z, armed, 0) for name, x, y, z, armed in campaign.HELLCAT_GUNS]
    for name, p in zip(campaign.HELLCAT_MISSILES, missiles):
        new.append((wc2data.WEAPON[name],) + tuple(p[1:]))
    new += chaff[:campaign.HELLCAT_DECOYS]
    return wc2data.set_hardpoints(rec, new)


def patch_exe(src, dst, spec=EXE_PATCH):
    d = open(src, "rb").read()
    digest = hashlib.sha256(d).hexdigest()
    if digest != SO2_SHA256:
        print("warning: SO2.EXE is not the build this was made with (sha256 %s)" % digest)
    open(dst, "wb").write(apply_patch(d, spec))


def type_info(gamedat, shiptype, extra_dir):
    """(kind, class) of a ship type from its SHIP file (ours in extra_dir first)."""
    if shiptype in campaign.SPECIAL_TYPES:
        return campaign.SPECIAL_TYPES[shiptype]
    for d in (extra_dir, gamedat):
        p = os.path.join(d, "SHIP.V%02d" % shiptype)
        if os.path.exists(p):
            name, kind, t, cls = wc2data.ship_record_of(p)
            return kind, cls
    sys.exit("SO3: no SHIP.V%02d" % shiptype)


def build_mission(mod, mis, gamedat, extra_dir):
    ships = []
    for s in mis["ships"]:
        kw = dict(s)
        kind, cls = type_info(gamedat, kw["shiptype"], extra_dir)
        kw["cls"] = cls
        ships.append(wc2data.ship_record(**kw))
    navs = []
    for n in mis["navs"]:
        types = [(t,) + type_info(gamedat, t, extra_dir) for t in n["types"]]
        navs.append(wc2data.nav_record(n["name"], n["pos"], visible=n.get("visible", 1), radius=n.get("radius", 15000),
                                       reveals=n.get("reveals", ()), steps=n.get("steps", ()), types=types, ships=n["ships"]))
    objectives = [wc2data.objective_record(k, p, label) for k, p, label in mis["objectives"]]
    if len(objectives) < 8:
        objectives.append(wc2data.objective_record(-1, -1, ""))
    h = mis["header"]
    header = wc2data.header_record(h["launch"], h["carrier"], h["player"], h["wing"])
    mod.set_mission(mis["series"], mis["mission"], header, navs, objectives, ships, mis["name"], mis.get("series_name"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default=os.path.join(ROOT, "wc2"))
    ap.add_argument("--hellcat", required=True, help="directory with HCl's XSHIP.V50 and PCSHIP.V50")
    ap.add_argument("--out", default=os.path.join(ROOT, "build-so3"))
    ap.add_argument("--test-game", action="store_true", help="also OUT/game: the whole game directory with SO3 in it")
    ap.add_argument("--so2-slots", default="", help="comma-separated s-m slots to fill with SO2's own mission records instead of ours (bisecting the campaign script's behaviour)")
    ap.add_argument("--hellcat-scale", type=float, default=0.75,
                    help="the Hellcat's sprites at this size (the conversion's 142 KB file is near twice a WC2 fighter's and starves the heap; 0.75 makes it 81 KB)")
    args = ap.parse_args()
    gamedat = os.path.join(args.game, "GAMEDAT")
    plain = os.path.join(args.out, "plain")
    pgd = os.path.join(plain, "GAMEDAT")
    shutil.rmtree(plain, ignore_errors=True)
    os.makedirs(pgd)

    # (The program is not part of the package: the page patches the player's
    # SO2.EXE by the manifest's list; so3.exe is written for --test-game only.)

    for n in NUMBERED:
        data = open(os.path.join(gamedat, n), "rb").read()
        if n.endswith(".S02"):
            data = rename_references(data)
        if n == "CAMPAIGN.S02":
            for old, new in FACE_FILES:
                assert len(old) == len(new) and data.count(old) == 1, old
                data = data.replace(old, new)
        if n == "INCIDENT.S02":
            data = waterloo_incidents(data, gamedat)
        if n == "GAMEFLOW.S02":
            parts = wc2data.read_container(data)
            blocks, changed = waterloo_films([b for _, b in parts])
            data = wc2data.compose_container(blocks, [f for f, _ in parts])
            print("GAMEFLOW.S03: %d Concordia film entries made the Waterloo's" % changed)
        open(os.path.join(pgd, renumber(n, 3)), "wb").write(data)

    for old, new, view in carrier_stills():
        open(os.path.join(pgd, new), "wb").write(waterloo_still(gamedat, old, view))
        print("%s: %s with the Waterloo's view %d in the Concordia's place" % (new, old, view))
    open(os.path.join(pgd, "DOORS.V45"), "wb").write(doorplates.culloden_doors(open(os.path.join(gamedat, "DOORS.V00"), "rb").read()))
    print("DOORS.V45: DOORS.V00 with the Culloden's plates")

    # the faces: the scenes draw a person from the portrait file CAMPAIGN's
    # shape list names at that person's place (docs/newcampaign.md), so our
    # copy of the list names our recoloured copies and WC2's own Doomsday
    for src, dst, mapping in RECOLOURED_FACES:
        wc2data.recolour_people(os.path.join(gamedat, src), os.path.join(pgd, dst), mapping)
        print("%s: %s recoloured" % (dst, src))

    # the Hellcat
    ext = wc2data.blocks_of(os.path.join(args.hellcat, "XSHIP.V50"))
    parts, flags = wc2data.academy_fighter_to_wc2(ext, campaign.HELLCAT)
    parts[3] = hellcat_record(parts[3], wc2data.blocks_of(os.path.join(gamedat, "SHIP.V04"))[3])
    if args.hellcat_scale != 1.0:
        parts = wc2data.scale_fighter(parts, args.hellcat_scale)
    wc2data.write_container(os.path.join(pgd, "SHIP.V%02d" % campaign.HELLCAT), parts, flags)
    cp = wc2data.blocks_of(os.path.join(args.hellcat, "PCSHIP.V50"))
    parts, flags = wc2data.academy_cockpit_to_wc2(cp)
    # The rear view: Academy's cockpit carries the Sabre's blocks for the
    # side and rear views, and the Sabre's rear view is its turret.  The
    # Hellcat has none: the Rapier's rear view (the user, 2026-10-17).
    parts[3] = wc2data.blocks_of(os.path.join(gamedat, "PCSHIP.V01"))[3]
    # Academy's front view keeps every pixel plain (no runs: 36 KB, near
    # twice the Rapier's); packed as the format allows, the same pixels take
    # half that.  The game holds the views outside the heap ships load into
    # (its free count did not move), so this is the package's size, not room
    # in flight (the user, 2026-10-18).
    saved = 0
    for i, fl in enumerate(flags):
        if fl == 0xC1:
            parts[i], n = wc2data.compact_sprite_table(parts[i])
            saved += n
    wc2data.write_container(os.path.join(pgd, "PCSHIP.V%02d" % campaign.HELLCAT), parts, flags)
    print("Hellcat cockpit: %d bytes (%d saved by packing its views)" % (os.path.getsize(os.path.join(pgd, "PCSHIP.V%02d" % campaign.HELLCAT)), saved))
    shutil.copyfile(os.path.join(gamedat, "INTELSHP.041"), os.path.join(pgd, "INTELSHP.%03d" % campaign.HELLCAT))
    # A type flown by a pilot without an INTEL file of his own needs the type's
    # INTELSHP; the game has none for the Rapier (never AI-flown in SO2).
    if not os.path.exists(os.path.join(gamedat, "INTELSHP.001")):
        shutil.copyfile(os.path.join(gamedat, "INTELSHP.003"), os.path.join(pgd, "INTELSHP.001"))
    print("Hellcat: %s, %d bytes" % (wc2data.ship_record_of(os.path.join(pgd, "SHIP.V%02d" % campaign.HELLCAT)), os.path.getsize(os.path.join(pgd, "SHIP.V%02d" % campaign.HELLCAT))))

    # the range's hulks: the Dorkathi's files as a type of our own, with no weapons
    hulk = wc2data.read_container(os.path.join(gamedat, "SHIP.V13"))
    parts, flags = [b for _, b in hulk], [f for _, f in [(0, f) for f, _ in hulk]]
    rec = bytearray(parts[38])
    struct.pack_into("<H", rec, wc2data.REC["type"], campaign.HULK)
    rec[:20] = b"Hulk".ljust(20, b"\0")
    # A target, not a transport: a hit sphere nearer its drawn size (the
    # Dorkathi's 200 is a Sabre's, around a sprite the renderer draws at
    # less than half a fighter's scale, so bolts burst and hulls bumped well
    # clear of it), a core a few passes of guns take, and a death blast that
    # does not take the Hellcat shooting it at close range with it (the
    # Dorkathi's 5000 is a corvette's; the user's report, 2026-10-17).
    parts[38] = wc2data.set_record_stats(wc2data.set_hardpoints(bytes(rec), []), radius=110, core=1200, blast=800)
    wc2data.write_container(os.path.join(pgd, "SHIP.V%02d" % campaign.HULK), parts, flags)
    print("Hulk: %s" % (wc2data.ship_record_of(os.path.join(pgd, "SHIP.V%02d" % campaign.HULK)),))

    # the missions
    mod = wc2data.Module(os.path.join(gamedat, "MODULE.002"))
    so2_slots = {tuple(int(x) for x in slot.split("-")) for slot in args.so2_slots.split(",") if slot}
    for mis in campaign.MISSIONS:
        if (mis["series"], mis["mission"]) in so2_slots:
            print("mission s%d m%d: SO2's own records kept (--so2-slots)" % (mis["series"], mis["mission"]))
            continue
        build_mission(mod, mis, gamedat, pgd)
        print("mission s%d m%d %s: %d ships, %d nav points" % (mis["series"], mis["mission"], mis["name"], len(mis["ships"]), len(mis["navs"])))
    mod.save(os.path.join(pgd, "MODULE.003"))

    # the scene texts
    series = wc2data.read_container(os.path.join(gamedat, "SERIES.S02"))
    blocks = [rename_references(b) for _, b in series]
    for i, strings in campaign.SERIES_TEXT.items():
        old = wc2data.scene_strings(blocks[i])
        if len(strings) != len(old):
            sys.exit("SO3: block %d has %d strings, campaign.py gives %d" % (i, len(old), len(strings)))
        blocks[i] = wc2data.series_set_strings(blocks[i], [s.encode("latin1") for s in strings])
    blocks, changed = waterloo_films(blocks)
    print("SERIES.S03: %d Concordia film entries made the Waterloo's" % changed)
    wc2data.write_container(os.path.join(pgd, "SERIES.S03"), blocks, [f for f, _ in series])

    files = sorted(os.path.relpath(os.path.join(dp, f), plain) for dp, _, fs in os.walk(plain) for f in fs)
    json.dump({"files": files, "anchor": "GAMEDAT/SERIES.S02", "exe": EXE_PATCH, "exes": [EXE_PATCH]},
              open(os.path.join(args.out, "plain.json"), "w"), indent=1)
    print("%d files in %s" % (len(files), plain))

    if args.test_game:
        game = os.path.join(args.out, "game")
        shutil.rmtree(game, ignore_errors=True)
        shutil.copytree(args.game, game)
        for f in files:
            shutil.copyfile(os.path.join(plain, f), os.path.join(game, f))
        patch_exe(os.path.join(args.game, "SO2.EXE"), os.path.join(game, "SO3.EXE"))
        print("test game in %s (SO3.EXE patched there)" % game)


if __name__ == "__main__":
    main()
