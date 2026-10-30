#!/bin/bash
# The old game flown alone by the multiplayer layer (WCNET=0: no network, the mission picked by SERIES / MIS), its
# flight traced (WCNET_SHIPTRACE) and a key script run in space: for comparing its AI with ours (README.md).
#   tests/wc1/run-old.sh OUTDIR SERIES MIS "KEYSCRIPT" [SECONDS]
# e.g. the first mission, the player flown to nav 1 by the autopilot and left there, invulnerable, for two minutes:
#   tests/wc1/run-old.sh /tmp/enyo1 1 0 "1:!rocks=0,3:a,10:@nav1,130:@end" 260
# OUTDIR gets trace.txt, the key script's dumps (@tag -> tag.bin, read by dsdump.py) and screenshots (!shot=name).
# Needs build-native/src/dosbox (BIN=) and the game in wc/ (WC=).
set -u
OUT=$1; SERIES=$2; MIS=$3; KEYS=$4; SECS=${5:-260}
BIN=${BIN:-$HOME/dev/dosbox3d/build-native/src/dosbox}
WC=${WC:-$HOME/dev/dosbox3d/wc}
mkdir -p "$OUT"
cd "$OUT" && env WCNET=0 WCNET_AUTOKEYS=1 SERIES=$SERIES MIS=$MIS WCNET_LOG=2 WCCALLSIGN=CHEATMAV \
    WCNET_SHIPTRACE="$OUT/trace.txt" WCNET_KEYSCRIPT="$KEYS" WCNET_DUMP_DIR="$OUT" \
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout -s KILL "$SECS" "$BIN" \
    -c "mount c $WC" -c "c:" -c "wc" -c "exit" > out.txt 2> err.txt
echo "trace: $(grep -c '^S' "$OUT/trace.txt" 2>/dev/null || echo 0) ship lines in $OUT/trace.txt"
