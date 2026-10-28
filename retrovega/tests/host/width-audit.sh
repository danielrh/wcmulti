#!/bin/sh
# The width audit: where the native build computes what DOS would have wrapped.  int and unsigned are 16 bits on DOS
# (Watcom) and 32 on the native build; the two agree wherever a value fits in 16 bits.  tools/width16.py rewrites the
# game's sources (and the host's) with its own int and unsigned 16 bits and its casts to them checked; built with
# clang's -fsanitize=implicit-integer-truncation and run on test scripts, it reports each place a value that does
# not fit is stored, passed, returned or cast: each is a place where DOS truncates and the native build does not.
# A report is a candidate: look at it, and where the 16 bits matter make them explicit in the real source (a
# uint16_t, an int16_t cast), keeping RV.EXE's code the same (docs/engine.md, "The native build").
#   tests/host/width-audit.sh [SCRIPT [RV.EXE options]]   default: the walk, a whole campaign, and the test build's
#                                                         scenes (built as RVT.EXE is: RV_TESTS, its demos in)
# Writes build/width16/report.txt: each place once, with its first value.  The audit build is for finding them only:
# its unsigned arithmetic is not DOS's (an unsigned short is promoted to a signed int).
set -e
cd "$(dirname "$0")/../.."
OUT=build/width16
CC=${CC:-clang}
CXX=${CXX:-clang++}
[ -f build/dos/RV.DAT ] || { echo "no build/dos/RV.DAT: ./build.sh first"; exit 1; }
rm -rf "$OUT/src" "$OUT/host" "$OUT/obj"
mkdir -p "$OUT/src" "$OUT/host/dos" "$OUT/obj" "$OUT/stubs"
python3 tools/rvabi.py --c-header > src/rvhooks.h
for f in src/*.c src/*.h host/*.c host/*.h host/dos/*.h; do
    case "$f" in host/fmt.c|host/play.c|host/rvhost.h|host/width16.h|host/twins_test.c|host/host.c|host/sim.c) continue ;; esac
    case "$f" in host/fsguard.c|host/pcm.c|host/pcm.h) cp "$f" "$OUT/$f"; continue ;; esac   # (the host's own, as they are)
    python3 tools/width16.py "$f" > "$OUT/$f"
done
python3 tools/rvabi.py --host-thunks > "$OUT/thunks.c"
rm -f "$OUT"/stubs/*.c
python3 tools/rvabi.py --stubs "$OUT/stubs"
ENGINE=$(sed -n 's/^ENGINE="\(.*\)"$/\1/p' build.sh)
PC="env PKG_CONFIG_LIBDIR=/usr/lib/i386-linux-gnu/pkgconfig:/usr/lib32/pkgconfig pkg-config"
SAN="-fsanitize=implicit-integer-truncation -fsanitize-recover=all"
CFLAGS="-m32 -O1 -g -std=gnu99 -funsigned-char -fno-strict-aliasing -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -DRV_HOST_GAME -DRV_TESTS
        -include host/rvhost.h -I$OUT/host/dos -I$OUT/src -I$OUT/host -Ihost -w $SAN"
for c in $ENGINE; do
    $CC $CFLAGS -c -o "$OUT/obj/$c.o" "$OUT/src/$c.c" &
done
wait
for c in hle fixed flightloop asm_twins fsguard pcm; do $CC $CFLAGS -c -o "$OUT/obj/h_$c.o" "$OUT/host/$c.c"; done
$CC $CFLAGS -c -o "$OUT/obj/twins.o" "$OUT/src/twins.c"
$CC $CFLAGS $($PC --cflags sdl2) -c -o "$OUT/obj/h_play.o" host/play.c   # (the front end as it is: SDL's callbacks take ints)
$CC -m32 -O1 -c -o "$OUT/obj/h_fmt.o" host/fmt.c
$CC $CFLAGS -c -o "$OUT/obj/thunks.o" "$OUT/thunks.c"
cat > "$OUT/report.c" <<'EOF'
/* the checked casts' reports: each place once */
#include <stdio.h>
#include <string.h>
void rv16_report(const char *file, int line, long value, int is_unsigned)
{
    static struct { const char *file; int line; } seen[4096];
    static int n;
    int i;
    for (i = 0; i < n; i++)
        if (seen[i].line == line && !strcmp(seen[i].file, file))
            return;
    if (n < 4096) { seen[n].file = file; seen[n].line = line; n++; }
    fprintf(stderr, "%s:%d: runtime error: cast of %ld to 16-bit %s\n", file, line, value, is_unsigned ? "unsigned" : "int");
}
EOF
$CC -m32 -O1 -c -o "$OUT/obj/report.o" "$OUT/report.c"
CXXF="-m32 -O1 -std=c++14 -Ihost -w"
$CXX $CXXF -c -o "$OUT/obj/h_opl3.o" host/opl3.cpp
for y in ymfm_opl ymfm_adpcm ymfm_pcm; do $CXX $CXXF -c -o "$OUT/obj/$y.o" "host/ymfm/$y.cpp"; done
for c in "$OUT"/stubs/*.c; do $CC $CFLAGS -c -o "${c%.c}.o" "$c"; done
rm -f "$OUT/libstubs.a"
ar rcs "$OUT/libstubs.a" "$OUT"/stubs/*.o
$CXX -m32 $SAN -o "$OUT/rv" "$OUT"/obj/*.o "$OUT/libstubs.a" $($PC --libs sdl2) -lm
mkdir -p "$OUT/run"
for f in build/dos/*.DAT build/dos/*.MID build/dos/*.BNK build/dos/SHIP*.RVS build/dos/SHIP*.RVH; do
    [ -e "$f" ] && ln -sf "$PWD/$f" "$OUT/run/"
done
# the runs: each script with its options, every report kept
: > "$OUT/reports.log"
run() {
    script=$1; shift
    cp "$script" "$OUT/run/WALK.TXT"
    rm -f "$OUT"/run/*.BMP "$OUT"/run/BUNK*.SAV "$OUT"/run/RVSETUP.CFG
    echo "width audit: $script $*"
    (cd "$OUT/run" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy UBSAN_OPTIONS=halt_on_error=0:print_summary=0 \
        timeout 600 ../rv /script WALK.TXT "$@" > run.log 2>&1) || echo "  (it ended with $?: $OUT/run-$(basename "$script" .txt).log)"
    cp "$OUT/run/run.log" "$OUT/run-$(basename "$script" .txt).log"
    cp "$OUT/run/RVLOG.TXT" "$OUT/RVLOG-$(basename "$script" .txt).TXT" 2> /dev/null || true
    # (only the stores into DOS's int and unsigned, rv16_*: a char or a short truncates on both)
    grep "runtime error" "$OUT/run/run.log" | grep "rv16_\|cast of" >> "$OUT/reports.log" || true
}
if [ $# -gt 0 ]; then
    run "$@"
else
    run tests/walk.txt
    run tests/campaign.txt /flight 1 /winrate 80
    run tests/board.txt
    run tests/talk.txt
    run tests/keys.txt
    run tests/flight.txt /flight 20
    run tests/lasers.txt /lasers 200 /balls 100 /flight 30
    run tests/flames.txt /flames /flight 40
    run tests/rocks.txt /field 40 /flight 60
    run tests/sky.txt /sky /bench 0 /flight 26
    run tests/zoom.txt /zoom 1 /bench 0 /flight 26
    run tests/roll.txt /bench 0 /capital /flight 30
    run tests/panel.txt /ship 2 /flight 60
    run tests/stick.txt /ship 4 /flight 60
fi
# each place once, its first report, in file and line order
sed -n 's/^\(src\/[^:]*:[0-9]*\):[0-9]*: runtime error: \(.*\)$/\1 \2/p; s/^\(src\/[^:]*:[0-9]*\): runtime error: \(.*\)$/\1 \2/p' \
    "$OUT/reports.log" | sort -t: -k1,1 -k2,2n -u | awk '!seen[$1]++' > "$OUT/report.txt"
echo "width audit: $(wc -l < "$OUT/report.txt") places ($OUT/report.txt)"
