#!/usr/bin/env python3
"""wcport.py -- the hooks' tables for another build of a game they know.

SM2.EXE (The Secret Missions 2) is WC.EXE relinked with a little more in it,
and SO1.EXE and SO2.EXE (the Special Operations) are WC2.EXE's.  Every
address the multiplayer layer uses -- the data offsets of
src/cpu/wcnet_ds.def and the code places of load_wc1_code / load_wc2_code in
src/cpu/wcnet_game.cpp -- is carried over to the other build with
scripts/wcmap.py's pairing of functions (same shape, other addresses):

  scripts/wcport.py sm2            # report: every address, its twin, how sure
  scripts/wcport.py sm2 --write    # ... and write that build's column of
                                   # wcnet_ds.def and its part of wcnet_ports.h
  scripts/wcport.py all --write

A data offset is taken when the votes for it are clear (wcmap.py), found by
its contents when it is a text the hooks use as scratch space, and otherwise
comes from MANUAL below, where each was found by hand.  A code place is the
instruction aligned with the base build's; the report shows both
instructions, and anything that is not the same instruction is marked `??`
and must be looked at before it is trusted.

The executables are not in the repository: wc/ and wc2/ are the installed
games.
"""
import collections
import difflib
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wcexe
import wcmap

ROOT = wcmap.ROOT
DEF = os.path.join(ROOT, "src", "cpu", "wcnet_ds.def")
GAME_CPP = os.path.join(ROOT, "src", "cpu", "wcnet_game.cpp")
PORTS_H = os.path.join(ROOT, "src", "cpu", "wcnet_ports.h")
COLUMNS = ["wc1", "sm2", "wc2", "so1", "so2"]

BUILDS = collections.OrderedDict([
    ("sm2", dict(base="wc/WC.EXE", exe="wc/SM2.EXE", base_column="wc1", base_fn="load_wc1_code", title="The Secret Missions 2")),
    ("so1", dict(base="wc2/WC2.EXE", exe="wc2/SO1.EXE", base_column="wc2", base_fn="load_wc2_code", title="Special Operations 1")),
    ("so2", dict(base="wc2/WC2.EXE", exe="wc2/SO2.EXE", base_column="wc2", base_fn="load_wc2_code", title="Special Operations 2")),
])

# Texts in the data segment that the hooks use as scratch space
# (wcnet_vm.cpp), and tables of constants no instruction names by its
# address: found in the other build by their first bytes.  (WC2's two error
# messages begin alike; the steering tables are there twice, and the copy
# the game reads is the one at the base build's own offset.)
BY_CONTENT = {"aLoadingWingCom": 48, "aSorryAnErrorHasOccured": 48, "steerStepsX": 12, "steerStepsY": 12}
# A word an instruction sequence names: the bytes before it, anywhere in the code.
BY_CODE = {"randomSeed": "03c913c003cb13c203c3890e"}   # the generator: ... add ax,bx / mov [seed],cx

# Found by hand, where no aligned instruction names the address: the build,
# the name, the offset, and how it was found.
MANUAL = {
    ("sm2", "vduText"): (0x9138, "two votes only, and as far from commGlobalTxt as in WC.EXE"),
    ("sm2", "commTextTimer"): (0x9084, "two votes only; ovr131:0C27 cmp word [9084],0 is WC.EXE ovr134:0C27 cmp word [8D96],0"),
    ("sm2", "pilotLastName"): (0x9D2C, "ovr146:0044 mov word [C526],9D2C is WC.EXE ovr149:0020 mov word [C240],9A42"),
    ("sm2", "pilotCallsign"): (0x9D3A, "14 bytes after the last name, as in WC.EXE"),
    ("so1", "joyRaw"): (0xA758, "one vote only, and right after joyRange as in WC2.EXE"),
    ("so2", "joyRaw"): (0xA9EE, "one vote only, and right after joyRange as in WC2.EXE"),
}
# The same for code places: (build, name) -> "seg:off" in that build.
MANUAL_CODE = {
}


def read_def():
    """[(line, name or None, [values], rest)]"""
    out = []
    for line in open(DEF):
        line = line.rstrip("\n")
        m = re.match(r"^WC_DS\((\w+),\s*([^)]*)\)(.*)$", line)
        if not m:
            out.append((line, None, None, None))
            continue
        vals = [int(v, 16) for v in m.group(2).split(",")]
        assert len(vals) == len(COLUMNS), "wcnet_ds.def: %d columns in %s" % (len(vals), line)
        out.append((line, m.group(1), vals, m.group(3)))
    return out


def write_def(rows):
    with open(DEF, "w") as f:
        for line, name, vals, rest in rows:
            if name is None:
                f.write(line + "\n")
            else:
                f.write("WC_DS(%s, %s)%s\n" % (name, ", ".join("0" if v == 0 and False else "0x%04X" % v for v in vals), rest))


def parse_code_table(fn):
    """The base build's code places: [(kind, name, segment index, thunk, offset, nargs, pascal)]."""
    src = open(GAME_CPP).read()
    start = src.index("static void %s() {" % fn)
    body = src[start:src.index("\n}\n", start)]
    enum = dict((n, int(v, 16)) for n, v in re.findall(r"(\w+) = (0x[0-9A-Fa-f]+)", re.search(r"enum \{(.*?)\};", body, re.S).group(1)))
    out = []
    for m in re.finditer(r"^\s*(ovr|root)\((\w+), (\w+), ([^;]*)\);", body, re.M):
        kind, name, segname = m.group(1), m.group(2), m.group(3)
        args = [a.strip() for a in m.group(4).split(",")]
        nums = [int(a, 16) if a.startswith("0x") else (1 if a == "true" else 0 if a == "false" else int(a)) for a in args]
        idx = int(re.sub(r"\D", "", segname))
        if kind == "ovr":
            thunk, off = nums[0], nums[1]
            rest = nums[2:]
        else:
            thunk, off = 0, nums[0]
            rest = nums[1:]
        nargs = rest[0] if rest else 0
        pascal = bool(rest[1]) if len(rest) > 1 else False
        out.append(dict(kind=kind, name=name, idx=idx, para=enum[segname], thunk=thunk, off=off, nargs=nargs, pascal=pascal))
    return out


class Port(object):
    def __init__(self, build):
        b = BUILDS[build]
        self.build, self.b = build, b
        self.base_path, self.new_path = os.path.join(ROOT, b["base"]), os.path.join(ROOT, b["exe"])
        m = wcmap.load(self.base_path, self.new_path)
        self.old, self.new, self.pairs, self.votes = m["old"], m["new"], m["pairs"], m["votes"]
        self.xo, self.xn = wcexe.Exe(self.base_path), wcexe.Exe(self.new_path)
        self.index = dict(((f[0], f[1]), i) for i, f in enumerate(self.old))

    # -- data -------------------------------------------------------------
    def dgroup(self, x):
        return x.data[x.hdr + x.dgroup * 16:x.hdr + x.dgroup * 16 + 0x10000]

    def data(self, name, base):
        """-> (offset, how)"""
        if (self.build, name) in MANUAL:
            off, how = MANUAL[(self.build, name)]
            return off, "by hand: " + how
        if name in BY_CONTENT:
            text = self.dgroup(self.xo)[base:base + BY_CONTENT[name]]
            d = self.dgroup(self.xn)
            hits = [m.start() for m in re.finditer(re.escape(text), d)]
            shown = repr(text.decode("latin-1")) if all(32 <= c < 127 or c == 10 for c in text) else text.hex()
            if len(hits) == 1 or base in hits:
                return (base if base in hits else hits[0]), "its contents %s" % shown
            return 0, "?? its contents %s are %s" % (shown, "not there" if not hits else "there %d times" % len(hits))
        if name in BY_CODE:
            pat = bytes.fromhex(BY_CODE[name])
            hits = []
            for seg in self.xn.code_segments():
                code, _ = self.xn.seg_bytes(seg)
                i = code.find(pat)
                while i >= 0:
                    hits.append((seg, i, code[i + len(pat)] | code[i + len(pat) + 1] << 8))
                    i = code.find(pat, i + 1)
            if len(hits) == 1:
                return hits[0][2], "named at %s" % self.xn.fmt(hits[0][0], hits[0][1] + len(pat))
            return 0, "?? its code is there %d times" % len(hits)
        c = collections.Counter(self.votes.get(base, {})).most_common(2)
        best = c[0] if c else (0, 0)
        second = c[1][1] if len(c) > 1 else 0
        if best[1] >= 3 and best[1] >= 2 * second:
            return best[0], "%d votes to %d" % (best[1], second)
        return 0, "?? %d votes for %04X, %d for another" % (best[1], best[0], second)

    # -- code -------------------------------------------------------------
    def twin(self, seg, off):
        """The instruction of the new build aligned with seg:off of the base
        build -> (seg, off, score, base text, new text, exact)."""
        start = self.xo.func_bounds(seg, off)[0]
        i = self.index.get((seg, start))
        if i is None or i not in self.pairs:
            return None
        j, score = self.pairs[i]
        fa, fb = self.old[i], self.new[j]
        if off not in fa[5]:
            return None
        k = fa[5].index(off)
        sm = difflib.SequenceMatcher(None, fa[3], fb[3], autojunk=False)
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if i1 <= k < i2:
                if tag == "equal":
                    kk = j1 + (k - i1)
                    return fb[0], fb[5][kk], score, fa[4][k], fb[4][kk], True
                kk = min(j1, len(fb[5]) - 1)
                return fb[0], fb[5][kk], score, fa[4][k], fb[4][kk], False
        return None

    def place(self, e):
        """A code place of the base table -> dict for the new build, or None."""
        seg = ("ovr%d" if e["kind"] == "ovr" else "seg%03d") % e["idx"]
        base_addr = self.xo.fmt(seg, e["off"])
        assert self.xo.segs[e["idx"]]["para"] == e["para"], "%s: the table says paragraph %04X for %s, the executable %04X" % (
            e["name"], e["para"], seg, self.xo.segs[e["idx"]]["para"])
        manual = MANUAL_CODE.get((self.build, e["name"]))
        if manual:
            nseg, noff = self.xn.parse(manual)
            t = (nseg, noff, 1.0, "", "(by hand)", True)
        else:
            t = self.twin(seg, e["off"])
        if t is None:
            return dict(e, base_addr=base_addr, ok=False, why="its function has no twin")
        nseg, noff, score, told, tnew, exact = t
        out = dict(e, base_addr=base_addr, addr=self.xn.fmt(nseg, noff), score=score, told=told, tnew=tnew, ok=exact and score >= 0.6)
        nidx = int(re.sub(r"\D", "", nseg))
        if e["kind"] == "root":
            if not nseg.startswith("seg"):
                return dict(out, ok=False, why="a root place went to an overlay")
            out.update(npara=self.xn.segs[nidx]["para"], nthunk=0, noff=noff)
            return out
        if not nseg.startswith("ovr"):
            return dict(out, ok=False, why="an overlay place went to the root")
        # The thunk: the twin of the function the base table's thunk enters.
        target = self.xo.stub_target(e["idx"], e["thunk"])
        nthunk = None
        if target is not None:
            tt = self.twin(seg, target)
            if tt is not None and tt[0] == nseg:
                hits = [so for so, tg in self.xn.ovr[nidx]["entries"] if tg == tt[1]]
                nthunk = hits[0] if hits else None
        if nthunk is None:
            return dict(out, ok=False, why="no thunk found for it (the base's enters %s)" % (self.xo.fmt(seg, target) if target is not None else "nothing"))
        out.update(npara=self.xn.by_name["stub%d" % nidx]["para"], nthunk=nthunk, noff=noff)
        return out


def emit_code(port, places):
    b = port.b
    lines = ["// %s (%s), from %s's places in %s." % (os.path.basename(b["exe"]), b["title"], os.path.basename(b["base"]), b["base_fn"]),
             "static void load_%s_code() {" % port.build, "    using namespace code;"]
    for p in places:
        if "noff" not in p:
            lines.append("    // %s: not found (%s)" % (p["name"], p.get("why", "")))
            continue
        tail = ""
        if p["nargs"] or p["pascal"]:
            tail = ", %d" % p["nargs"] + (", true" if p["pascal"] else "")
        if p["kind"] == "ovr":
            call = "ovr(%s, 0x%04X, 0x%04X, 0x%04X%s);" % (p["name"], p["npara"], p["nthunk"], p["noff"], tail)
        else:
            call = "root(%s, 0x%04X, 0x%04X%s);" % (p["name"], p["npara"], p["noff"], tail)
        lines.append("    %-62s // %s (%s)" % (call, p["addr"], p["base_addr"]))
    lines.append("}")
    return "\n".join(lines) + "\n"


def run(build, write):
    port = Port(build)
    b = port.b
    col, base_col = COLUMNS.index(build), COLUMNS.index(b["base_column"])
    print("== %s: %s from %s" % (build, b["exe"], b["base"]))
    print("   DGROUP paragraph %04X (base %04X), %d segments (base %d), %d overlays (base %d)" % (
        port.xn.dgroup, port.xo.dgroup, len(port.xn.segs), len(port.xo.segs), len(port.xn.ovr), len(port.xo.ovr)))
    rows = read_def()
    doubts = 0
    for n, (line, name, vals, rest) in enumerate(rows):
        if name is None or vals[base_col] == 0:
            continue
        off, how = port.data(name, vals[base_col])
        mark = "  " if off else "??"
        if not off:
            doubts += 1
        print("%s %-30s %04X -> %04X  %s" % (mark, name, vals[base_col], off, how))
        vals[col] = off
    places = [port.place(e) for e in parse_code_table(b["base_fn"])]
    for p in places:
        ok = p.get("ok")
        if not ok:
            doubts += 1
        print("%s %-22s %-13s -> %-13s %s  %s | %s%s" % ("  " if ok else "??", p["name"], p["base_addr"], p.get("addr", "?"),
              "%.2f" % p["score"] if "score" in p else "    ", p.get("told", ""), p.get("tnew", ""), "  [" + p["why"] + "]" if "why" in p else ""))
    print("   %d in doubt" % doubts)
    if write:
        write_def(rows)
    return emit_code(port, places)


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    write = "--write" in argv
    if not args or args[0] not in list(BUILDS) + ["all"]:
        print(__doc__)
        return 1
    builds = list(BUILDS) if args[0] == "all" else [args[0]]
    parts = {}
    if os.path.exists(PORTS_H):
        for m in re.finditer(r"// BEGIN (\w+)\n(.*?)// END \1\n", open(PORTS_H).read(), re.S):
            parts[m.group(1)] = m.group(2)
    for build in builds:
        parts[build] = run(build, write)
    if write:
        with open(PORTS_H, "w") as f:
            f.write("/*\n *  The code places of the other builds of the two games (wcnet_game.cpp),\n"
                    " *  carried over from WC.EXE's and WC2.EXE's by scripts/wcport.py: do not edit,\n"
                    " *  change the base table (or the script's MANUAL_CODE) and run it again.\n"
                    " *  Each line names the place in its own build and, in brackets, in the base.\n */\n")
            for build in BUILDS:
                if build in parts:
                    f.write("// BEGIN %s\n%s// END %s\n" % (build, parts[build], build))
        print("wrote %s and %s" % (os.path.relpath(DEF, ROOT), os.path.relpath(PORTS_H, ROOT)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
