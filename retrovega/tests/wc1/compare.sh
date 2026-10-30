#!/bin/sh
# Our AI in the old game's first-mission scene (enyo1.scene), flown by rvsim for four minutes twice: the player
# flying straight, and the test pilot hunting (facing the nearest enemy every 3 s, firing); each flight's trace
# digested as the old game's is (README.md; RV_TACTICS=1 logs the minds' tactics).  Builds rvsim (SDL=0 host/build.sh).
#   tests/wc1/compare.sh [OUTDIR]
set -e
cd "$(dirname "$0")/../.."
OUT=${1:-build/wc1}
mkdir -p "$OUT"
SDL=0 host/build.sh > "$OUT/build.log" 2>&1 || { cat "$OUT/build.log"; exit 2; }
for mode in passive hunt; do
    extra=""
    [ "$mode" = hunt ] && extra="--hunt 3"
    build/host32-nosdl/rvsim --headless --frames 7200 --ship 0 --nodamage --scene tests/wc1/enyo1.scene \
        --shiptrace "$OUT/rv-enyo1-$mode.txt" --log "$OUT/rv-enyo1-$mode.log" $extra > /dev/null 2>&1 || true
    echo "=== ours, the player $mode ($OUT/rv-enyo1-$mode.txt)"
    # (30 steps a second; the two hulls' sum 260 km: a Nicander's 160 and a Schroedinger's 100)
    python3 -I tests/wc1/digest.py tests/wc1 "$OUT/rv-enyo1-$mode.txt" 30 260
done
