#!/bin/sh
# Build RV.EXE (and the M0 check, HELLO.EXE) with Open Watcom 2 and NASM.
#   ./build.sh            -> build/dos/RV.EXE, RV.DAT, RVGAME.DAT, HELLO.EXE
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
CFLAGS="-q -bt=dos -mm -3 -s -w4 -zq -od -d2 -i=src"
nasm -f obj -o "$OUT/rvhook.obj" src/rvhook.asm
nasm -f obj -o "$OUT/lowlevel.obj" src/lowlevel.asm
~/.venvs/retrovega-art/bin/python -P tools/rvpack.py --out "$OUT/RV.DAT" --header src/rvasset.h
ENGINE="util xms video dat gfx input room lipsync music campaign game main"
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
link RV.EXE file "$OUT/lowlevel.obj" $(for c in $ENGINE; do printf ' file %s/%s.obj' "$OUT" "$c"; done)
echo "built $OUT/RV.EXE and $OUT/HELLO.EXE"
