#!/bin/sh
# Run RV.EXE headless in the native DOSBox with a test script; screenshots
# (NAME.BMP) and RVLOG.TXT land in build/dos.
#   tests/run.sh [script]        default tests/walk.txt
set -e
cd "$(dirname "$0")/.."
./build.sh > /dev/null
cp "${1:-tests/walk.txt}" build/dos/WALK.TXT
rm -f build/dos/*.BMP build/dos/RVLOG.TXT build/dos/BUNK*.SAV
mkdir -p build/dosrun
sed 's#mount c ../dos#mount c '"$PWD"'/build/dos#' tests/rv.conf > build/dosrun/run.conf
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 300 ~/dev/dosbox3d/build-native/src/dosbox \
    -conf build/dosrun/run.conf > build/dosrun/dosbox.log 2>&1 || true
grep -v "^stub" build/dos/RVLOG.TXT
