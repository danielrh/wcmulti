#!/bin/sh
# Play RV.EXE by hand in a DOSBox window (no script): the game's own options after it, e.g.
#   tests/play.sh /field 120          an asteroid field in every flight
#   EXE=RVT.EXE tests/play.sh /zoom 1 the test build, one Gawain dead ahead
# NOBUILD=1 skips the build; CYCLES=N (40000; the page runs 50000); DOSBOX=path another DOSBox.
# The log is build/dos/RVLOG.TXT; F12 saves a screenshot (RVSHOT.BMP); Q in the bunk menu quits.
set -e
cd "$(dirname "$0")/.."
[ -n "$NOBUILD" ] || ./build.sh > /dev/null
mkdir -p build/dosrun
sed -e 's#mount c ../dos#mount c '"$PWD"'/build/dos#' -e "s#RV.EXE /script WALK.TXT#${EXE:-RV.EXE} $*#" \
    -e "s#^cycles=.*#cycles=${CYCLES:-40000}#" -e 's#^nosound=true#nosound=false#' -e 's#^exit$##' \
    tests/rv.conf > build/dosrun/play.conf
exec ${DOSBOX:-$HOME/dev/dosbox3d/build-native/src/dosbox} -conf build/dosrun/play.conf
