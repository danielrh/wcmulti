#!/bin/sh
# Run RV.EXE headless in the native DOSBox with a test script; screenshots
# (NAME.BMP) and RVLOG.TXT land in build/dos.
#   tests/run.sh [script [RV.EXE options]]     default tests/walk.txt
# The game's RV.EXE runs unless a test switch is given (anything but /script,
# /flight, /winrate, /res, /onfly, /skyrows, /mpu, /nomusic, /noquit): then
# RVT.EXE, the test build with the demos, benchmarks and self-tests in.
# EXE=RVT.EXE (or RV.EXE) picks one regardless.
# NOBUILD=1 skips the build; CYCLES=N runs at N cycles (the page runs 50000); TIMEOUT=S (300).
set -e
cd "$(dirname "$0")/.."
[ -n "$NOBUILD" ] || ./build.sh > /dev/null
cp "${1:-tests/walk.txt}" build/dos/WALK.TXT
rm -f build/dos/*.BMP build/dos/RVLOG.TXT
[ -n "$KEEP_SAVES" ] || rm -f build/dos/BUNK*.SAV build/dos/RVSETUP.CFG
mkdir -p build/dosrun
shift 2>/dev/null || true
exe=${EXE:-RV.EXE}
if [ -z "$EXE" ]; then
    for a in "$@"; do
        case "$a" in
            /script|/flight|/winrate|/res|/onfly|/skyrows|/mpu|/nomusic|/noquit) ;;
            /*) exe=RVT.EXE;;
        esac
    done
fi
sed -e 's#mount c ../dos#mount c '"$PWD"'/build/dos#' -e "s#RV.EXE /script WALK.TXT#$exe /script WALK.TXT $*#" \
    -e "s#^cycles=.*#cycles=${CYCLES:-40000}#" tests/rv.conf > build/dosrun/run.conf
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout ${TIMEOUT:-300} ~/dev/dosbox3d/build-native/src/dosbox \
    -conf build/dosrun/run.conf > build/dosrun/dosbox.log 2>&1 || true
grep -v "^stub" build/dos/RVLOG.TXT
