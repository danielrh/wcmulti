#!/bin/sh
# Our AI in the old game's first-mission scene (enyo1.scene), flown by rvsim for four minutes twice: the player
# flying straight, and the test pilot hunting (facing the nearest enemy every 3 s, firing); each flight's trace
# summarised as the old game's is (README.md).  Builds rvsim (SDL=0 host/build.sh).
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
    python3 -I tests/wc1/trace.py "$OUT/rv-enyo1-$mode.txt" --fps 30 | sed -n '/--- slot/,$p'
done
