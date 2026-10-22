#!/bin/sh
# Build RV.EXE (and the M0 check, HELLO.EXE) with Open Watcom 2 and NASM.
#   ./build.sh            -> build/dos/RV.EXE, RV.DAT, RVGAME.DAT, HELLO.EXE (DEBUG=1: unoptimised, -d2;
#                            RV_COMMS=face: the comm faces without breathing gear, their mouths speaking)
# The toolchain lives in ~/opt/watcom (docs/engine.md); WATCOM overrides it.
set -e
cd "$(dirname "$0")"
WATCOM=${WATCOM:-$HOME/opt/watcom}
export WATCOM PATH="$WATCOM/binl64:$PATH" INCLUDE="$WATCOM/h"
OUT=build/dos
mkdir -p "$OUT"
python3 tools/rvabi.py --c-header > src/rvhooks.h
python3 tools/rvabi.py --asm > src/rvhook.asm
python3 tools/rvdata.py --out "$OUT/RVGAME.DAT" --header src/rvdata.h
python3 tools/midi.py --out "$OUT" > /dev/null
python3 tools/oplbank.py --out "$OUT/RVOPL.BNK" > /dev/null
# optimised (the flight benchmark found -ox worth half again as many sprites a
# frame as -od); DEBUG=1 builds -od -d2 for the debugger
if [ -n "${DEBUG:-}" ]; then OPT="-od -d2"; else OPT="-ox -d1"; fi
CFLAGS="-q -bt=dos -mm -3 -s -w4 -zq $OPT -i=src"
nasm -f obj -i src/ -o "$OUT/rvhook.obj" src/rvhook.asm
nasm -f obj -o "$OUT/lowlevel.obj" src/lowlevel.asm
nasm -f obj -o "$OUT/lasers_a.obj" src/lasers.asm
# the comm link's faces (one helmet render, every pilot's face behind its visor, each alien's helmet: ~40 s), when
# their sources change.  RV_COMMS=face builds the pilots without their breathing gear, the bare face's mouth
# speaking (the user's option, to weigh its cost); the default, mask, is the user's ruling
RV_COMMS=${RV_COMMS:-mask}
case "$RV_COMMS" in mask) FACE="" ;; face) FACE="--face" ;; *) echo "RV_COMMS is mask or face"; exit 1 ;; esac
COMMS=build/comms-$RV_COMMS
[ $COMMS/comms.json -nt tools/comms.py ] && [ $COMMS/comms.json -nt tools/helmet3d.py ] \
    && [ $COMMS/comms.json -nt tools/aliens3d.py ] && [ $COMMS/comms.json -nt data/aliens.csv ] \
    && [ $COMMS/comms.json -nt data/people.csv ] \
    && [ -z "$(find art/people/faces -newer $COMMS/comms.json -print -quit)" ] \
    || python3 -W ignore tools/comms.py --out $COMMS --cols 56 $FACE > /dev/null   # (56: 2 x 2 blocks, the coarse link)
~/.venvs/retrovega-art/bin/python -P tools/rvpack.py --out "$OUT/RV.DAT" --header src/rvasset.h --comms $COMMS
# the balls' sprites (the spherical projectiles: every kind, frame and diameter)
[ "$OUT/BALLS.DAT" -nt tools/balls.py ] && [ "$OUT/BALLS.DAT" -nt data/palette.json ] \
    || ~/.venvs/retrovega-art/bin/python -P tools/balls.py --out "$OUT/BALLS.DAT" --preview docs/balls.png > /dev/null
# the ships' silhouettes (tools/hitboxes.py made them from the meshes; the build has no Vega Strike assets)
cp data/shapes.dat "$OUT/SHAPES.DAT"
# the sky's far things (nebulae, galaxies, planets: every turn of each)
[ "$OUT/SKY.DAT" -nt tools/sky.py ] && [ "$OUT/SKY.DAT" -nt data/palette.json ] \
    || ~/.venvs/retrovega-art/bin/python -P tools/sky.py --out "$OUT/SKY.DAT" --preview docs/sky.png > /dev/null
# the fighters' views (tools/pack.py, copied into art/ships): beside RV.EXE, and
# their pre-rendered sets (tools/rvsprites.c, the host build of lightfield.c)
cp art/ships/SHIP*.RV "$OUT/" 2>/dev/null || true
mkdir -p build/host
gcc -std=gnu99 -O2 -I src -o build/host/rvsprites src/lightfield.c tools/rvsprites.c
for f in art/ships/SHIP*.RV; do
    s="$OUT/$(basename "${f%.RV}").RVS"
    # (the nearest ships are drawn exactly in flight: rolls at every size from half down, so a far
    # ship turns with the player's roll - the user's ruling, 2026-10-20)
    [ "$s" -nt "$f" ] && [ "$s" -nt tools/rvsprites.c ] && [ "$s" -nt src/lightfield.c ] || build/host/rvsprites "$f" "$s" 0 4 9 > /dev/null
done
# the carrier (a test aid, RV.EXE /capital): from half size down, rolls at two sizes
if [ -f build/rvdat/SHIP20.RV ]; then
    s="$OUT/SHIP20.RVS"
    [ "$s" -nt build/rvdat/SHIP20.RV ] && [ "$s" -nt tools/rvsprites.c ] || build/host/rvsprites build/rvdat/SHIP20.RV "$s" 3 3 2 > /dev/null
fi
ENGINE="util xms video dat gfx input room lipsync opl music campaign lightfield ships lasers bolts balls missiles sky collide flames comms cockpit flight game main"
for c in $ENGINE hello; do
    wcc $CFLAGS -fo="$OUT/$c.obj" "src/$c.c"
done
# RVHOOK first (image paragraph 0), the C code, then DGROUP last as Watcom's
# startup needs it, with the fixed block (RVFIXED) ahead of Watcom's _NULL so
# it starts at DS:0000 (docs/engine.md).
# the thunks' default bodies, one module each in a library (see rvabi.py --stubs)
python3 tools/rvabi.py --stubs "$OUT/stubs"
rm -f "$OUT/rvstubs.lib"
for c in "$OUT"/stubs/*.c; do
    wcc $CFLAGS -fo="${c%.c}.obj" "$c"
    wlib -q -b "$OUT/rvstubs.lib" +"${c%.c}.obj"
done
link() {
    name=$1; shift
    wlink system dos name "$OUT/$name" option quiet option map="$OUT/${name%.EXE}.MAP" \
        option stack=8192 \
        order clname RVHOOK clname CODE clname FAR_DATA \
              clname BEGDATA segment RVFIXED segment _NULL segment _AFTERNULL \
              clname DATA clname BSS clname STACK \
        file "$OUT/rvhook.obj" "$@" library "$OUT/rvstubs.lib"
}
link HELLO.EXE file "$OUT/lowlevel.obj" file "$OUT/util.obj" file "$OUT/xms.obj" file "$OUT/hello.obj"
link RV.EXE file "$OUT/lowlevel.obj" file "$OUT/lasers_a.obj" $(for c in $ENGINE; do printf ' file %s/%s.obj' "$OUT" "$c"; done)
echo "built $OUT/RV.EXE and $OUT/HELLO.EXE"
