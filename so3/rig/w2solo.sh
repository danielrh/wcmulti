#!/bin/bash
# One native WC2-family instance as a host with nobody joining.
#   w2solo.sh <outdir> <duration_s> "<series> <mission>" "<keyscript>" [extra env]
# GAME=so2 GAMEDIR=/path DIRECT=" " for the barracks (default "l": mission test).
set -u
ROOT=/home/sandbox/dev/dosbox3d
OUT="$1"; DURATION="$2"; MISSION="$3"; KEYS="$4"; shift 4
PORT="${WCPORT:-13471}"
GAME="${GAME:-wc2}"
GAMEDIR="${GAMEDIR:-$ROOT/wc2}"
mkdir -p "$OUT"
cat > "$OUT/dosbox.conf" <<EOC
[cpu]
cycles=${CYCLES:-8000}
[sblaster]
irq=5
EOC
( cd "$OUT" && exec env "$@" WCPORT="$PORT" WCHOST= WCCALLSIGN=${CALL:-SOLO} WCNET_LOG="${WCNET_LOG:-2}" WCNET_KEYSCRIPT="$KEYS" WCNET_DUMP_DIR="$OUT" \
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    timeout -s KILL "$DURATION" "$ROOT/build-native/src/dosbox" -conf "$OUT/dosbox.conf" \
    -c "mount c $GAMEDIR" -c "c:" -c "loadfix -34 $GAME Origin ${KFLAG--k} ${DIRECT:-l} s${MISSION% *} m${MISSION#* }" -c "exit" \
    > "$OUT/solo.out" 2> "$OUT/solo.err" )
echo "done: $OUT"
