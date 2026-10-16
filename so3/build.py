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
game directory to fly it from natively.

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


def rename_references(data):
    for ref in SCENE_REFERENCES:
        for form in (ref, ref.upper(), ref[:1].upper() + ref[1:]):
            data = data.replace(form, form[:-1] + b"3")
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
RECOLOURED_FACES = [("PEOPLE.V02", "PEOPLE.V46", BANBRIDGE), ("PEOPLE.V01", "PEOPLE.V47", BERGSTROM)]
FACE_FILES = [(b"people.v02", b"people.v46"), (b"people.v01", b"people.v47"), (b"people.v32", b"people.v09")]

# SO3.EXE is SO2.EXE with these byte sequences replaced, each found the given
# number of times.  The page applies the same list to the player's own copy
# (web/so3.js); the program itself is never shipped.
EXE_PATCH = {
    "source": "SO2.EXE",
    "target": "SO3.EXE",
    "sha256": SO2_SHA256,
    "patches": [
        {"what": "the data-file number, mov word [cb16], 2", "find": "c70616cb0200", "replace": "c70616cb0300", "count": 5},
        {"what": "the saved game's name", "find": b"savegame.so2".hex(), "replace": b"savegame.so3".hex(), "count": 1},
    ],
}


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


def hellcat_record(rec):
    """The Hellcat's record with campaign.py's numbers and weapons: the weapon
    positions stay where the conversion put them, the codes change."""
    rec = wc2data.set_record_stats(rec, **campaign.HELLCAT_STATS)
    old = wc2data.hardpoints(rec)
    guns = [p for p in old if p[0] < 15]
    others = [p for p in old if p[0] >= 15]
    chaff = [p for p in others if p[0] == wc2data.WEAPON["chaff"]]
    missiles = [p for p in others if p[0] != wc2data.WEAPON["chaff"]]
    assert len(guns) >= len(campaign.HELLCAT_GUNS) and len(missiles) >= len(campaign.HELLCAT_MISSILES) and len(chaff) >= campaign.HELLCAT_DECOYS
    new = []
    # the mounts mirrored exactly: the conversion's left and right differed by a unit
    for k, (name, p) in enumerate(zip(campaign.HELLCAT_GUNS, guns)):
        code, flags, x, y, z, w = (wc2data.WEAPON[name],) + p[1:]
        mate = guns[k ^ 1]
        if k & 1:
            x, y, z = -mate[2], mate[3], mate[4]
        new.append((code, flags, x, y, z, w))
    for name, p in zip(campaign.HELLCAT_MISSILES, missiles):
        new.append((wc2data.WEAPON[name],) + p[1:])
    new += chaff[:campaign.HELLCAT_DECOYS]
    return wc2data.set_hardpoints(rec, new)


def patch_exe(src, dst):
    d = open(src, "rb").read()
    digest = hashlib.sha256(d).hexdigest()
    if digest != SO2_SHA256:
        print("warning: SO2.EXE is not the build this was made with (sha256 %s)" % digest)
    open(dst, "wb").write(apply_patch(d, EXE_PATCH))


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
        open(os.path.join(pgd, renumber(n, 3)), "wb").write(data)

    # the faces: the scenes draw a person from the portrait file CAMPAIGN's
    # shape list names at that person's place (docs/newcampaign.md), so our
    # copy of the list names our recoloured copies and WC2's own Doomsday
    for src, dst, mapping in RECOLOURED_FACES:
        wc2data.recolour_people(os.path.join(gamedat, src), os.path.join(pgd, dst), mapping)
        print("%s: %s recoloured" % (dst, src))

    # the Hellcat
    ext = wc2data.blocks_of(os.path.join(args.hellcat, "XSHIP.V50"))
    parts, flags = wc2data.academy_fighter_to_wc2(ext, campaign.HELLCAT)
    parts[3] = hellcat_record(parts[3])
    if args.hellcat_scale != 1.0:
        parts = wc2data.scale_fighter(parts, args.hellcat_scale)
    wc2data.write_container(os.path.join(pgd, "SHIP.V%02d" % campaign.HELLCAT), parts, flags)
    cp = wc2data.blocks_of(os.path.join(args.hellcat, "PCSHIP.V50"))
    parts, flags = wc2data.academy_cockpit_to_wc2(cp)
    wc2data.write_container(os.path.join(pgd, "PCSHIP.V%02d" % campaign.HELLCAT), parts, flags)
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
    parts[38] = wc2data.set_hardpoints(bytes(rec), [])
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
    wc2data.write_container(os.path.join(pgd, "SERIES.S03"), blocks, [f for f, _ in series])

    files = sorted(os.path.relpath(os.path.join(dp, f), plain) for dp, _, fs in os.walk(plain) for f in fs)
    json.dump({"files": files, "anchor": "GAMEDAT/SERIES.S02", "exe": EXE_PATCH}, open(os.path.join(args.out, "plain.json"), "w"), indent=1)
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
