#!/bin/bash
# Three native WC2-family instances: a host and two wingmen (SO3P, or any
# mission tools/wc2wing gave a second wing fighter).
#   w2trio.sh <outdir> <duration_s> "<series> <mission>" "<host keys>" "<client keys>" "<client2 keys>" [extra env for all]
# DIRECT=" " for the campaign mode (barracks), default "l" (mission test).
# GAME (so3p), GAMEDIR (the host's game directory); CLIENTGAMEDIR and
# CLIENT2GAMEDIR give the wingmen directories of their own (two programs on
# one directory corrupt each other's save files).
# HOSTENV / CLIENTENV / CLIENT2ENV: extra "VAR=val" for one side only.
set -u
ROOT=/home/sandbox/dev/dosbox3d
OUT="$1"; DURATION="$2"; MISSION="$3"; HOSTKEYS="$4"; CLIENTKEYS="$5"; CLIENT2KEYS="$6"; shift 6
PORT="${WCPORT:-13481}"
GAME="${GAME:-so3p}"
GAMEDIR="${GAMEDIR:-$ROOT/build-so3/game}"
mkdir -p "$OUT/host" "$OUT/client" "$OUT/client2"
cat > "$OUT/dosbox.conf" <<EOC
[cpu]
cycles=${CYCLES:-8000}
[sblaster]
irq=5
EOC
launch() {  # name keyscript gamedir extra-env...
    local name="$1" keys="$2" dir="$3"; shift 3
    ( cd "$OUT/$name" && exec env "$@" WCPORT="$PORT" WCNET_LOG="${WCNET_LOG:-2}" WCNET_KEYSCRIPT="$keys" WCNET_DUMP_DIR="$OUT/$name" \
        SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        timeout -s KILL "$DURATION" "$ROOT/build-native/src/dosbox" -conf "$OUT/dosbox.conf" \
        -c "mount c $dir" -c "c:" -c "loadfix -34 $GAME Origin ${KFLAG--k} ${DIRECT:-l} s${MISSION% *} m${MISSION#* }" -c "exit" \
        > "$OUT/$name.out" 2> "$OUT/$name.err" ) &
}
launch host "$HOSTKEYS" "$GAMEDIR" WCHOST= WCCALLSIGN=${HOSTCALL:-HOST} ${HOSTENV:-} "$@"
sleep 4
launch client "$CLIENTKEYS" "${CLIENTGAMEDIR:-${GAMEDIR}-client}" WCHOST=127.0.0.1 WCCALLSIGN=${CLIENTCALL:-WINGMAN} ${CLIENTENV:-} "$@"
sleep 4
launch client2 "$CLIENT2KEYS" "${CLIENT2GAMEDIR:-${GAMEDIR}-client2}" WCHOST=127.0.0.1 WCCALLSIGN=${CLIENT2CALL:-THIRD} ${CLIENT2ENV:-} "$@"
wait
echo "done: $OUT"
