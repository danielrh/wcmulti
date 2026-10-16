#!/bin/bash
# The whole SO3 campaign, host + wingman, from the barracks of s1 m0: every
# mission flown by autopilot with the host killing the enemies, the scenes
# clicked through.  campaignrun.sh <outdir> [duration_s] [start "s m"]
S=${S:-$(cd "$(dirname "$0")" && pwd)/out}
OUT=${1:-$S/crun}; DUR=${2:-5400}; START=${3:-"1 0"}
launch="6:!mouse=0.48:0.55,8:!button=0:1,8.3:!button=0:0,12:!mouse=0.48:0.55,13:!button=0:1,13.3:!button=0:0,16:clickskip"
host="$launch"; client="$launch"
# one segment per mission (the clock restarts in each flight): eight nav cycles, then click on to the next mission
seg() {  # $1 = spared mission ships for !killfoes (may be empty); $2 = a step before the third autopilot (1-3: away from the tender); $3 = the cycle length
  local sp="$1" extra="$2" cyc="${3:-16}" t=5 out=",2:!rocks=0"      # (the autopilot refuses inside a field: the rig cannot fly out of one)
  for i in 1 2 3 4 5 6 7 8; do
    out="$out,$t:a,$((t+7)):!killfoes${sp:+=$sp},$((t+10)):!killfoes${sp:+=$sp},$((t+13)):!killfoes${sp:+=$sp}"
    [ $i = 2 ] && [ -n "$extra" ] && out="$out,$((t+cyc-2)):$extra"     # after the second nav's arrival and scene, before the third autopilot
    t=$((t+cyc))
  done
  # the autopilot does not land by itself: the mission ends as landed, then the scenes are clicked through
  echo "$out,$((t+2)):!mem,$((t+4)):!status=1,$((t+8)):clickskip"
}
cseg() { local cyc="${1:-16}" t=5 out=",2:!rocks=0"; for i in 1 2 3 4 5 6 7 8; do out="$out,$t:a"; t=$((t+cyc)); done; echo "$out,$((t+4)):!status=1,$((t+8)):clickskip"; }
for m in $(seq 1 20); do
  if [ $m = 4 ]; then
    # 1-3: the picket fight, a cruise back to the Nav 1 point, the jump to the tender and her hail scene
    # (most of a minute), a cruise to the Nav 2 point, then Nav 3 and home.  The tender is on our side now,
    # so nothing is spared and the autopilot is free.
    # (after Nav 3: a cruise to its point, then the jump to the Culloden at nav 5, where the game lands by itself:
    # "land on ship 11" is credited only by that landing, not by the forced one)
    host="$host,2:!rocks=0,10:a,20:!killfoes,23:!killfoes,26:!killfoes,32:a,62:a,124:a,152:a,164:!killfoes,167:!killfoes,170:!killfoes,174:!killfoes,180:a,210:a,240:!status=1,244:clickskip"
    client="$client,2:!rocks=0,10:a,32:a,62:a,124:a,152:a,180:a,210:a,240:!status=1,244:clickskip"
    continue
  fi
  host="$host$(seg "" "" 16)"; client="$client$(cseg 16)"
done
rm -rf "$OUT"
if [ -n "${SOLO:-}" ]; then   # one program alone (WCNET=0): the kill aids work as the server's
  mkdir -p "$OUT"
  CALL=${HOSTCALL:-CHEATHOST} GAME=so3 GAMEDIR=/home/sandbox/dev/dosbox3d/build-so3/game WCNET=0 WCPORT=${WCPORT:-13600} WCNET_KEYSCRIPT_FROM=boot DIRECT=" " \
    $(dirname "$0")/w2solo.sh "$OUT" "$DUR" "$START" "$host" WCNET_WATCH=0000:1 > /dev/null 2>&1
  cp "$OUT/solo.err" "$OUT/host.err" 2>/dev/null
  echo "== campaignrun done" >> "$OUT/host.err"
  exit 0
fi
# host + a wingman that is started again whenever its program drops out
mkdir -p "$OUT/host" "$OUT/client"
cat > "$OUT/dosbox.conf" <<EOC
[cpu]
cycles=8000
[sblaster]
irq=5
EOC
PORT=${WCPORT:-13600}
GAMEDIR=/home/sandbox/dev/dosbox3d/build-so3/game
run_one() {  # name keyscript callsign hostvar extra-env...  (the wingman has a game directory of its own: the save files)
  local name="$1" keys="$2" call="$3" hostv="$4"; shift 4
  local dir="$GAMEDIR"; [ "$name" = client ] && dir="${GAMEDIR}-client"
  ( cd "$OUT/$name" && exec env "$@" WCPORT="$PORT" WCHOST="$hostv" WCCALLSIGN="$call" WCNET_LOG=2 WCNET_KEYSCRIPT="$keys" WCNET_DUMP_DIR="$OUT/$name" \
      WCNET_KEYSCRIPT_FROM=boot SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
      timeout -s KILL "$DUR" /home/sandbox/dev/dosbox3d/build-native/src/dosbox -conf "$OUT/dosbox.conf" \
      -c "mount c $dir" -c "c:" -c "loadfix -34 so3 Origin -k s${START% *} m${START#* }" -c "exit" >> "$OUT/$name.out" 2>> "$OUT/$name.err" )
}
run_one host "$host" "${HOSTCALL:-CHEATHOST}" "" WCNET_WATCH=0000:1 &
hostpid=$!
sleep 4
n=0
while kill -0 $hostpid 2>/dev/null; do
  n=$((n+1)); echo "== wingman start $n $(date +%T)" >> "$OUT/client.err"
  run_one client "$client" "${CLIENTCALL:-CHEATWING}" 127.0.0.1
  sleep 3
done
wait
echo "== campaignrun done" >> "$OUT/host.err"
