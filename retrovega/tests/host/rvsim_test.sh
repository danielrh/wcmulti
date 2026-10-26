#!/bin/sh
# The host harness's self-test (docs/logic.md, "The host harness"):
#   1. rvsim built 32- and 64-bit flies the game's logic (src/logic.c) to the same hash, and the spawns' log lines
#      are RV.EXE's (build/dos/RVLOG.TXT, when a DOS flight of the same mission is there to compare with);
#   2. a recording flies again to the same hash;
#   3. a logic that breaks the layer's rules on purpose (tests/host/rules_logic.c) has each break reported, and
#      UBSan, RV_CHECK and (with clang) the 16-bit truncation check each stop it where they should.
# VALGRIND=1 also runs the game's logic under valgrind (32- and 64-bit, SAN=none builds).
set -e
cd "$(dirname "$0")/../.."
T=build/host-test
mkdir -p $T
fails=0
ok() { echo "ok   $*"; }
bad() { echo "FAIL $*"; fails=$((fails + 1)); }
build() { env "$@" host/build.sh > $T/build.log 2>&1 || { cat $T/build.log; exit 1; }; }

# 1. 32 and 64 bits alike
build ARCH=32
build ARCH=64
h32=$(build/host32/rvsim --headless --frames 900 2> $T/log32 | sed -n 's/.*hash \([0-9a-f]*\).*/\1/p')
h64=$(build/host64/rvsim --headless --frames 900 2> $T/log64 | sed -n 's/.*hash \([0-9a-f]*\).*/\1/p')
[ -n "$h32" ] && [ "$h32" = "$h64" ] && ok "32 and 64 bits: the same world after 900 steps ($h32)" \
    || bad "32-bit hash $h32, 64-bit $h64"
if [ -f build/dos/RVLOG.TXT ] && grep -q "mission L1c0_m1" build/dos/RVLOG.TXT; then
    sed -n 's/^ *[0-9]*  \(logic: slot .*\)/\1/p' $T/log32 > $T/spawn-host
    sed -n '/mission L1c0_m1/,/^flight: L1c0_m1/p' build/dos/RVLOG.TXT | tr -d '\r' | grep "^logic: slot" > $T/spawn-dos || true
    if [ ! -s $T/spawn-dos ]; then
        echo "--   (build/dos/RVLOG.TXT has no spawn lines for L1c0_m1 to compare)"
    elif cmp -s $T/spawn-host $T/spawn-dos; then
        ok "the spawns are RV.EXE's ($(wc -l < $T/spawn-dos) ships)"
    else
        bad "the spawns differ from RV.EXE's:"; diff $T/spawn-dos $T/spawn-host || true
    fi
fi

# 2. a recording replays
r1=$(build/host32/rvsim --headless --frames 300 --record $T/rec.rvin 2> /dev/null | sed -n 's/.*hash \([0-9a-f]*\).*/\1/p')
r2=$(build/host32/rvsim --headless --replay $T/rec.rvin 2> /dev/null | sed -n 's/.*hash \([0-9a-f]*\).*/\1/p')
[ -n "$r1" ] && [ "$r1" = "$r2" ] && ok "a recording replays to the same world ($r1)" || bad "recorded $r1, replayed $r2"

# 3. the rules
build ARCH=32 LOGIC=tests/host/rules_logic.c
R=build/host32-rules_logic/rvsim
set +e
$R --headless --frames 100 --check-src > /dev/null 2> $T/rules.log
code=$?
set -e
[ $code = 3 ] && ok "a broken rule makes the exit code 3" || bad "exit code $code, not 3"
for want in "10  RULE: fireGunFromShip(0, 0) put no shot" "20  RULE: do_damage(1, 0, 1): curShield comes out different" \
            "30  RULE: rand() called" "40  RULE: slot 1: its orientation vectors are no longer square"; do
    grep -q "$want" $T/rules.log && ok "reported: $want" || bad "not reported: $want"
done
expect_stop() {                          # (env var, what the report says, how)
    set +e
    env "$1=1" $R --headless --frames 100 > /dev/null 2> $T/stop.log
    code=$?
    set -e
    grep -q "$2" $T/stop.log && [ $code -ne 0 ] && ok "$3 stops the run (exit $code)" || bad "$3 did not stop the run"
}
expect_stop RV_TEST_UB "signed integer overflow" "UBSan's signed overflow"
expect_stop RV_TEST_BOUNDS "rv_fail: src/shots.c" "RV_CHECK past the shot list"
if command -v clang > /dev/null; then
    build ARCH=32 CC=clang LOGIC=tests/host/rules_logic.c
    R=build/host32-clang-rules_logic/rvsim
    expect_stop RV_TEST_TRUNC "changed the value to -5536" "clang's 16-bit truncation check"
else
    echo "--   (no clang: the truncation check not tried)"
fi

if [ -n "${VALGRIND:-}" ]; then
    for a in 32 64; do
        build ARCH=$a SAN=none
        if valgrind --error-exitcode=9 --leak-check=full -q build/host$a-none/rvsim --headless --frames 300 \
            > /dev/null 2> $T/vg$a.log; then
            ok "valgrind, $a-bit: no errors, no leaks"
        else
            bad "valgrind, $a-bit:"; grep -v "^ *[0-9]*  " $T/vg$a.log | head -20
        fi
    done
fi

[ $fails = 0 ] && echo "RVSIM OK" || { echo "RVSIM: $fails failed"; exit 1; }
