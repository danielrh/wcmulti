#!/usr/bin/env python3
"""so3/build.py -- Special Operations 3 from an installed Wing Commander II.

  so3/build.py --hellcat DIR [--game wc2] [--out build-so3] [--test-game]

Writes OUT/plain/: GAMEDAT/ with every file the program names by its number
3 (SO3.EXE is SO2.EXE with its program number set to 3, so that it reads the
.S03 / .003 / .V03 files and savegame.so3: that patch is a list in
OUT/plain.json which the page applies to the player's own SO2.EXE; the
program is never shipped) (copies of Special Operations 2's,
MODULE.003 and SERIES.S03 with our missions and texts in them), the Hellcat
as ship type 49 (HCl's and Howard Day's conversion for Academy, repacked
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


def rename_references(data):
    for ref in SCENE_REFERENCES:
        for form in (ref, ref.upper(), ref[:1].upper() + ref[1:]):
            data = data.replace(form, form[:-1] + b"3")
    return data


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


def patch_exe(src, dst):
    d = open(src, "rb").read()
    digest = hashlib.sha256(d).hexdigest()
    if digest != SO2_SHA256:
        print("warning: SO2.EXE is not the build this was made with (sha256 %s)" % digest)
    open(dst, "wb").write(apply_patch(d, EXE_PATCH))


def type_info(gamedat, shiptype, extra_dir):
    """(kind, class) of a ship type from its SHIP file (ours in extra_dir first)."""
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
    objectives = [wc2data.objective_record(k, p, label) for k, p, label in mis["objectives"]] + [wc2data.objective_record(-1, -1, "")]
    h = mis["header"]
    header = wc2data.header_record(h["launch"], h["carrier"], h["player"], h["wing"])
    mod.set_mission(mis["series"], mis["mission"], header, navs, objectives, ships, mis["name"], mis.get("series_name"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default=os.path.join(ROOT, "wc2"))
    ap.add_argument("--hellcat", required=True, help="directory with HCl's XSHIP.V50 and PCSHIP.V50")
    ap.add_argument("--out", default=os.path.join(ROOT, "build-so3"))
    ap.add_argument("--test-game", action="store_true", help="also OUT/game: the whole game directory with SO3 in it")
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
        open(os.path.join(pgd, renumber(n, 3)), "wb").write(data)

    # the Hellcat
    ext = wc2data.blocks_of(os.path.join(args.hellcat, "XSHIP.V50"))
    parts, flags = wc2data.academy_fighter_to_wc2(ext, campaign.HELLCAT)
    wc2data.write_container(os.path.join(pgd, "SHIP.V%02d" % campaign.HELLCAT), parts, flags)
    cp = wc2data.blocks_of(os.path.join(args.hellcat, "PCSHIP.V50"))
    parts, flags = wc2data.academy_cockpit_to_wc2(cp)
    wc2data.write_container(os.path.join(pgd, "PCSHIP.V%02d" % campaign.HELLCAT), parts, flags)
    shutil.copyfile(os.path.join(gamedat, "INTELSHP.041"), os.path.join(pgd, "INTELSHP.%03d" % campaign.HELLCAT))
    print("Hellcat: %s" % (wc2data.ship_record_of(os.path.join(pgd, "SHIP.V%02d" % campaign.HELLCAT)),))

    # the missions
    mod = wc2data.Module(os.path.join(gamedat, "MODULE.002"))
    for mis in campaign.MISSIONS:
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
