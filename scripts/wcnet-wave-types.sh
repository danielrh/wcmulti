#!/bin/bash
# Headless two-instance test of the ship types a wingman's machine has loaded
# when it reaches a nav point after the host brought on a wave there
# (ShipTypesJob and on_load_nav_types_entry in src/cpu/wcnet_events.cpp).
#
# The host jumps alone (the key script's "!pos") to a nav point whose second
# wave is of a type its first did not have, and has the first wave destroyed
# ("!killfoes"); the wingman, left behind, jumps there once the second wave
# is on, so that its own game sets the nav point up after the host's waves.
# Every ship in a slot on the wingman's machine must then be of a loaded type
# and drawn as that type:
#
#   wc1  Venice 2 (WC.EXE, SERIES=12 MIS=1), Nav 2: Salthi, then Krants; a
#        fighter's shapes (dseg:C754) must be there
#   wc2  WC2 series 5 mission 3 (Ch 5 mission 4), Enemy Base: Jalkehi, then
#        Drakhri, while the wingman waits at the Concordia (whose nav point
#        its game sets up again meanwhile); a slot's entry in the table of
#        loaded ships (dseg:4B3C into dseg:14D0) must hold its ship's type
#
#   scripts/wcnet-wave-types.sh [wc1|wc2|all]
#
# Needs build-native/src/dosbox (BIN=), WC1 in wc/ and WC2 in wc2/ (WC1DIR=,
# WC2DIR=).  Dumps, screenshots and logs land in $OUT (default
# /tmp/wcnet-wave-types); ports WCPORT (default 13921) and WCPORT+1.  A few
# minutes; prints WAVE TYPES OK or WAVE TYPES FAILED.  CHECK_ONLY=1 checks the
# dumps an earlier run left in $OUT again.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WHICH="${1:-all}"
BIN="${BIN:-$ROOT/build-native/src/dosbox}"
WC1DIR="${WC1DIR:-$ROOT/wc}"
WC2DIR="${WC2DIR:-$ROOT/wc2}"
OUT="${OUT:-/tmp/wcnet-wave-types}"
PORT="${WCPORT:-13921}"

launch() {  # dir, name, keyscript, duration, env... -- dosbox arguments...
    local dir="$1" name="$2" keys="$3" duration="$4" env=(); shift 4
    while [ "$1" != "--" ]; do env+=("$1"); shift; done; shift
    mkdir -p "$dir/$name"
    ( cd "$dir/$name" && exec env "${env[@]}" WCNET_LOG=2 WCNET_KEYSCRIPT="$keys" WCNET_DUMP_DIR="$dir/$name" \
        SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout -s KILL "$duration" "$BIN" "$@" \
        > "$dir/$name.out" 2> "$dir/$name.err" ) &
}

face="!face=npc"
for t in 76.5 77 77.5 78 78.5 79 79.5 80; do face="$face,$t:!face=npc"; done

run_wc1() {
    local dir="$OUT/wc1" cmd=(-c "mount c $WC1DIR" -c "c:" -c "wc" -c "exit")
    rm -rf "$dir"; mkdir -p "$dir"
    # The first autopilot stops at the Rendezvous; ms 11 is Nav 2's Fralthi.
    local host="2:!rocks=0,5:a,25:!pos=3819237:-2955275:-18219737,32:!killfoes=11,55:@wave,75:@after,76:$face,81:!shot=after"
    local client="55:@wave,60:!pos=3626937:-2916775:-18222187,75:@after,76:$face,81:!shot=after"
    local env=(WCPORT="$PORT" WCNET_AUTOKEYS=1 SERIES=12 MIS=1)
    launch "$dir" host "$host" 240 "${env[@]}" WCHOST= WCCALLSIGN=CHEATHOST -- "${cmd[@]}"
    sleep 3
    launch "$dir" client "$client" 240 "${env[@]}" WCHOST=127.0.0.1 WCCALLSIGN=CHEATWING -- "${cmd[@]}"
}

run_wc2() {
    local dir="$OUT/wc2"
    rm -rf "$dir"; mkdir -p "$dir"
    printf '[cpu]\ncycles=8000\n[sblaster]\nirq=5\n' > "$dir/dosbox.conf"
    local cmd=(-conf "$dir/dosbox.conf" -c "mount c $WC2DIR" -c "c:" -c "loadfix -34 wc2 Origin -k l s5 m3" -c "exit")
    # The nav point plays a scene when the host gets there; the Jalkehi come after it.
    local host="5:!pos=7680000:0:19600000,60:!killfoes,80:@wave,120:@after"
    local client="80:@wave,100:!pos=7600000:0:19500000,120:@after,121:!face=npc,122:!shot=after"
    launch "$dir" host "$host" 340 WCPORT=$((PORT + 1)) WCHOST= WCCALLSIGN=CHEATHOST -- "${cmd[@]}"
    sleep 4
    launch "$dir" client "$client" 340 WCPORT=$((PORT + 1)) WCHOST=127.0.0.1 WCCALLSIGN=CHEATWING -- "${cmd[@]}"
}

case "$WHICH" in
    wc1|wc2|all) ;;
    *) echo "usage: $0 [wc1|wc2|all]" >&2; exit 2 ;;
esac
if [ -z "${CHECK_ONLY:-}" ]; then
    [ "$WHICH" != wc2 ] && run_wc1
    [ "$WHICH" != wc1 ] && run_wc2
    echo "running (logs, dumps and screenshots in $OUT)"
    wait
fi

python3 -I - "$OUT" "$WHICH" <<'EOF'
import os, struct, sys
out, which = sys.argv[1], sys.argv[2]
# dseg offsets (WC.EXE, WC2.EXE): loaded types, slot entity type, slot mission ship, mission ships, current nav point
GAMES = {
    "wc1": dict(loaded=0xC13A, size=13, entries=4, etype=0xBD1A, slotms=0xC974, ms=0x6E80, mssize=0x2A, nav=0xC178,
                navpoint=4, wave_type=11),
    "wc2": dict(loaded=0x45BB, size=0x16, entries=5, etype=0x64AA, slotms=0x6E1E, ms=0x75CE, mssize=0x3C, nav=0x441C,
                navpoint=4, wave_type=6),
}
ok = True
def fail(msg):
    global ok
    ok = False
    print("FAIL", msg)
for game in (["wc1", "wc2"] if which == "all" else [which]):
    g = GAMES[game]
    for when in ("wave", "after"):
        path = os.path.join(out, game, "client", when + ".bin")
        if not os.path.exists(path):
            fail(f"{game}: no dump {path} (the flight did not get that far)")
            continue
        d = open(path, "rb").read()
        w = lambda o: struct.unpack_from("<h", d, o)[0]
        loaded = [(w(g["loaded"] + g["size"] * i) if game == "wc2" else struct.unpack_from("b", d, g["loaded"] + g["size"] * i)[0])
                  for i in range(g["entries"])]
        ships, wave = [], 0
        for s in range(10):
            if w(g["etype"] + 2 * s) not in (0xC, 0xD):
                continue
            ms = w(g["slotms"] + 2 * s)
            if ms < 0:
                continue
            rec = g["ms"] + g["mssize"] * ms
            t = d[rec + 0x14] if game == "wc2" else w(rec)
            wave += t == g["wave_type"]
            if game == "wc1":
                drawn = w(0xBD1A + 2 * s) == 0xD or struct.unpack_from("<I", d, 0xC754 + 4 * s)[0] != 0
                as_type = t if drawn else None
            else:
                as_type = w(0x14D2 + 0xF3 * d[0x4B3C + s])
            ships.append((s, ms, t, as_type))
            if t not in loaded or as_type != t:
                fail(f"{game} {when}: slot {s} (mission ship {ms}) is type {t}, shown as {as_type}; loaded {loaded}")
        print(f"{game} {when}: nav point {w(g['nav'])}, loaded types {loaded}, ships (slot, mission ship, type, shown as) {ships}")
        if not wave:
            fail(f"{game} {when}: no ship of the second wave (type {g['wave_type']}) on the wingman's machine")
        if when == "after" and w(g["nav"]) != g["navpoint"]:
            fail(f"{game} after: the wingman's game has not set nav point {g['navpoint']} up itself")
    log = os.path.join(out, game, "client.err")
    for line in (open(log, errors="replace") if os.path.exists(log) else []):
        if "not loaded here" in line or "would drop" in line or "left slot" in line:
            print("  " + line.strip())
print("WAVE TYPES OK" if ok else "WAVE TYPES FAILED")
sys.exit(0 if ok else 1)
EOF
