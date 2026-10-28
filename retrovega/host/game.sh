#!/bin/sh
# The native build: the whole game, src/ as RV.EXE has it, for a 32-bit host or the browser (docs/engine.md, "The
# native build").  What RV.EXE asks of DOS, the BIOS and the PC is answered by host/hle.c; the assembly's place is
# taken by its C twins (src/twins.c, host/asm_twins.c: the test build checks them against the assembly); the
# screen, the keys, the mouse and the OPL3 (ymfm, BSD) are SDL2's (host/play.c).
#   host/game.sh              build/game32/rv: 32-bit gcc, UBSan; run it with --data build/game32/run (made here:
#                             the DOS build's files linked in) and RV.EXE's options (/script FILE ...)
#   SAN=ubsan|asan|none       the checks built in (the default UBSan)
#   CC=clang                  clang (and clang++)
#   OPT=-O1                   the optimisation (always -g3)
#   WEB=1                     the browser's (build/gameweb/: rv.js, rv.wasm; Emscripten, ASYNCIFY)
#   TESTS=1                   the test build's aids in, as RVT.EXE has them (RV_TESTS: the demos, the benchmarks, the
#                             self-tests; build/game32-tests)
# 32-bit only: the archive's records and the far pointers (seg:off, the host pointer's halves) are 32-bit layouts.
# Needs the DOS build's data (./build.sh), gcc-multilib and g++-multilib, libsdl2-dev:i386; WEB=1 the Emscripten SDK.
set -e
cd "$(dirname "$0")/.."
SAN=${SAN:-ubsan}
CC=${CC:-gcc}
OPT=${OPT:--O1}
case "$SAN" in ubsan|asan|none) ;; *) echo "SAN is ubsan, asan or none"; exit 1 ;; esac
CCNAME=$(basename "$CC")
case "$CCNAME" in clang*) CXX=${CXX:-clang++} ;; *) CXX=${CXX:-g++} ;; esac
if [ -n "${WEB:-}" ]; then
    OUT=build/gameweb
    if ! command -v emcc > /dev/null; then
        EMSDK=${EMSDK:-$HOME/emsdk}
        PATH="$EMSDK/upstream/emscripten:$PATH"
        export EM_CONFIG="$EMSDK/.emscripten"
    fi
    CC=emcc
    CXX=em++
    AR=emar
    ARCHF=""
    OPT=-O2
    SANF=""
    SDLF="-sUSE_SDL=2"
    SDLL="-sUSE_SDL=2 -sASYNCIFY -sASYNCIFY_STACK_SIZE=65536 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=134217728
          -sSTACK_SIZE=1048576 -sEXPORTED_RUNTIME_METHODS=FS,callMain -sINVOKE_RUN=0 -sEXIT_RUNTIME=1 -lidbfs.js
          -sMODULARIZE=1 -sEXPORT_NAME=RetroVega"
    EXE=$OUT/rv.js
    LINKG=""
else
    OUT=build/game32
    [ "$SAN" = ubsan ] || OUT=$OUT-$SAN
    case "$CCNAME" in gcc|cc) ;; *) OUT=$OUT-$CCNAME ;; esac
    AR=ar
    ARCHF=-m32
    case "$SAN" in
        ubsan) SANF="-fsanitize=undefined -fno-sanitize=alignment" ;;
        asan) SANF="-fsanitize=address,undefined -fno-sanitize=alignment -fno-omit-frame-pointer" ;;
        none) SANF="" ;;
    esac
    PC="env PKG_CONFIG_LIBDIR=/usr/lib/i386-linux-gnu/pkgconfig:/usr/lib32/pkgconfig pkg-config"
    SDLF=$($PC --cflags sdl2)
    SDLL=$($PC --libs sdl2)
    EXE=$OUT/rv
    LINKG=-g
fi
TESTF=""
if [ -n "${TESTS:-}" ]; then
    OUT=$OUT-tests
    EXE=$OUT/$(basename "$EXE")
    TESTF=-DRV_TESTS
fi
mkdir -p "$OUT/obj" "$OUT/stubs"
[ -f build/dos/RV.DAT ] || { echo "no build/dos/RV.DAT: ./build.sh first"; exit 1; }

# the generated sources, as build.sh makes them
python3 tools/rvabi.py --c-header > src/rvhooks.h
python3 tools/rvabi.py --host-thunks > "$OUT/thunks.c"
rm -f "$OUT"/stubs/*.c
python3 tools/rvabi.py --stubs "$OUT/stubs"

# the game's modules (build.sh's list), as Watcom builds them: char unsigned, no type-based aliasing; the DOS words
# (host/rvhost.h, host/dos/: dos.h, i86.h, conio.h, io.h, malloc.h); main.c's main is rv_main there (play.c's calls it)
ENGINE=$(sed -n 's/^ENGINE="\(.*\)"$/\1/p' build.sh)
[ -n "$ENGINE" ] || { echo "build.sh's ENGINE list not found"; exit 1; }
COMMON="-std=gnu99 -g3 -funsigned-char -fno-strict-aliasing -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -DRV_HOST_GAME
        -include host/rvhost.h -Ihost/dos -Isrc -Ihost"
WARN="-Wall -Wno-unused-parameter -Wno-sign-compare -Wno-unused-result -Wno-format -Wno-unused-but-set-variable
      -Wno-pointer-sign -Wno-char-subscripts"
CFLAGS="$ARCHF $OPT $COMMON $WARN $SANF $TESTF"
rm -f "$OUT"/obj/*.o
for c in $ENGINE; do
    $CC $CFLAGS -c -o "$OUT/obj/$c.o" "src/$c.c" &
done
wait
for c in hle fixed flightloop asm_twins fsguard pcm; do
    $CC $CFLAGS -c -o "$OUT/obj/h_$c.o" "host/$c.c"
done
$CC $CFLAGS -c -o "$OUT/obj/twins.o" src/twins.c
$CC $CFLAGS $SDLF -c -o "$OUT/obj/h_play.o" host/play.c
$CC $ARCHF $OPT -g3 -c -o "$OUT/obj/h_fmt.o" host/fmt.c              # (without rvhost.h: its sprintf is this)
$CC $CFLAGS -c -o "$OUT/obj/thunks.o" "$OUT/thunks.c"
CXXF="$ARCHF $OPT -g3 -std=c++14 -Ihost $SANF"
$CXX $CXXF -c -o "$OUT/obj/h_opl3.o" host/opl3.cpp
for y in ymfm_opl ymfm_adpcm ymfm_pcm; do
    $CXX $CXXF -w -c -o "$OUT/obj/$y.o" "host/ymfm/$y.cpp" &
done
wait
rm -f "$OUT/libstubs.a"
for c in "$OUT"/stubs/*.c; do
    $CC $CFLAGS -c -o "${c%.c}.o" "$c"
done
$AR rcs "$OUT/libstubs.a" "$OUT"/stubs/*.o
$CXX $ARCHF $OPT $SANF $LINKG -o "$EXE" "$OUT"/obj/*.o "$OUT/libstubs.a" $SDLL -lm
if [ -z "${WEB:-}" ]; then
    # the run's directory: the DOS build's data linked in (its saves and its log its own)
    mkdir -p "$OUT/run"
    for f in build/dos/*.DAT build/dos/*.MID build/dos/*.BNK build/dos/SHIP*.RV build/dos/SHIP*.RVS build/dos/SHIP*.RVH; do
        [ -e "$f" ] && ln -sf "$PWD/$f" "$OUT/run/"
    done
    echo "built $EXE (run: $EXE --data $OUT/run)"
else
    echo "built $EXE"
fi
