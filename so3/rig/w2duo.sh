#!/bin/bash
# Two native WC2 instances (host + wingman).
#   w2duo.sh <outdir> <duration_s> "<series> <mission>" "<host keyscript>" "<client keyscript>" [extra env for both]
# DIRECT=" " for the campaign mode (barracks), default "l" (mission test).
# HOSTENV / CLIENTENV: extra "VAR=val VAR2=val" for one side only.
set -u
ROOT=/home/sandbox/dev/dosbox3d
OUT="$1"; DURATION="$2"; MISSION="$3"; HOSTKEYS="$4"; CLIENTKEYS="$5"; shift 5
PORT="${WCPORT:-13461}"
GAME="${GAME:-wc2}"
GAMEDIR="${GAMEDIR:-$ROOT/wc2}"
mkdir -p "$OUT/host" "$OUT/client"
cat > "$OUT/dosbox.conf" <<EOC
[cpu]
cycles=${CYCLES:-8000}
[sblaster]
irq=5
EOC
launch() {  # name keyscript extra-env...
    local name="$1" keys="$2"; shift 2
    local dir="$GAMEDIR"; [ "$name" = client ] && [ -n "${CLIENTGAMEDIR:-}" ] && dir="$CLIENTGAMEDIR"
    ( cd "$OUT/$name" && exec env "$@" WCPORT="$PORT" WCNET_LOG="${WCNET_LOG:-2}" WCNET_KEYSCRIPT="$keys" WCNET_DUMP_DIR="$OUT/$name" \
        SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        timeout -s KILL "$DURATION" "$ROOT/build-native/src/dosbox" -conf "$OUT/dosbox.conf" \
        -c "mount c $dir" -c "c:" -c "loadfix -34 $GAME Origin ${KFLAG--k} ${DIRECT:-l} s${MISSION% *} m${MISSION#* }" -c "exit" \
        > "$OUT/$name.out" 2> "$OUT/$name.err" ) &
}
launch host "$HOSTKEYS" WCHOST= WCCALLSIGN=${HOSTCALL:-HOST} ${HOSTENV:-} "$@"
sleep 4
launch client "$CLIENTKEYS" WCHOST=127.0.0.1 WCCALLSIGN=${CLIENTCALL:-WINGMAN} ${CLIENTENV:-} "$@"
wait
echo "done: $OUT"
