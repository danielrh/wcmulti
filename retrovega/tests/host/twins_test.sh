#!/bin/sh
# The asm's C twins (src/twins.c) on the host against the DOS build.  RVP.EXE /twins (in DOSBox) checks every twin
# against its asm on seeded random inputs and logs a sum of each one's outputs; the same self-test built for the
# host as host/build.sh builds the game's code (32-bit, UBSan; rv_x is c_x there: host/asm_twins.c) must log the
# same sums.  Then the twins compute with int 32 bits what the asm computes on DOS.  Prints TWINS OK.
#   tests/host/twins_test.sh     NOBUILD=1: RVP.EXE as the last build left it; CC=clang (and its truncation check)
# 32-bit, as the host builds the game (gcc -m32, Emscripten's wasm32): ARCH=64 builds too, but there lasers.c's longs
# are 64 bits (its sums are not DOS's) and the structs' padding fails the self-test's comparisons.
set -e
cd "$(dirname "$0")/../.."
CC=${CC:-gcc}
ARCH=${ARCH:-32}
OUT=build/host-twins
mkdir -p "$OUT"
SAN="-fsanitize=undefined -fno-sanitize=alignment -fno-sanitize-recover=undefined"
case "$(basename "$CC")" in clang*) SAN="$SAN -fsanitize=implicit-integer-truncation";; esac
$CC -m$ARCH -std=gnu99 -g -O1 -funsigned-char -fno-strict-aliasing $SAN -include host/rvhost.h -Isrc -Ihost \
    -DRV_TESTS -Wall -Wno-unused-parameter -o "$OUT/twins_test" \
    host/twins_test.c src/twins.c src/lasers.c src/lightfield.c host/asm_twins.c
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 "$OUT/twins_test" > "$OUT/host.log" 2>&1 || true
grep '^twins:' "$OUT/host.log" > "$OUT/host.txt" || true
TIMEOUT=200 tests/run.sh /dev/null /twins > /dev/null 2>&1 || true
tr -d '\r' < build/dos/RVLOG.TXT | grep '^twins:' > "$OUT/dos.txt" || true
cat "$OUT/dos.txt"
if [ ! -s "$OUT/dos.txt" ] || grep -q 'DIFFERS\|DISAGREES' "$OUT/dos.txt"; then
    echo "TWINS FAIL: the DOS run (a twin against its asm)"
    exit 1
fi
if ! cmp -s "$OUT/dos.txt" "$OUT/host.txt"; then
    grep -v '^twins:' "$OUT/host.log" | head -20
    diff "$OUT/dos.txt" "$OUT/host.txt" || true
    echo "TWINS FAIL: the host's sums are not the DOS run's"
    exit 1
fi
echo "TWINS OK ($(grep -c ' ok,' "$OUT/dos.txt") twins: each agrees with its asm on DOS, and the host's sums are DOS's)"
