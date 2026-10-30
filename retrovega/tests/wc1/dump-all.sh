#!/bin/bash
# Every mission of the old game's first campaign flown alone to its first second in space, its data segment dumped
# (the nav points, the mission ships, the fields): OUT/sSS-mMM/t1.bin, read by dsdump.py.
#   dump-all.sh OUT [series ...]      (default: all 13 series, 4 missions each; a missing mission just times out)
set -u
OUT=$1; shift
SERIES=${*:-1 2 3 4 5 6 7 8 9 10 11 12 13}
BIN=$HOME/dev/dosbox3d/build-native/src/dosbox
WC=$HOME/dev/dosbox3d/wc
for s in $SERIES; do
    for m in 0 1 2 3; do
        d=$OUT/s$s-m$m
        mkdir -p "$d"
        [ -f "$d/t1.bin" ] && continue
        ( cd "$d" && env WCNET=0 WCNET_AUTOKEYS=1 SERIES=$s MIS=$m WCNET_LOG=2 WCCALLSIGN=CHEATMAV \
            WCNET_KEYSCRIPT="1:@t1,1.5:!shot=t1" WCNET_DUMP_DIR="$d" SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
            timeout -s KILL 110 "$BIN" -c "mount c $WC" -c "c:" -c "wc" -c "exit" > out.txt 2> err.txt )
        if [ -f "$d/t1.bin" ]; then echo "s$s m$m: dumped"; else echo "s$s m$m: no flight (no such mission?)"; fi
    done
done
