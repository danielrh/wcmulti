#!/bin/sh
# The flight modules' unit tests on the host: gcc builds src/lightfield.c and
# src/lasers.c as the DOS build does (no DOS in them) with their tests and
# runs them.  LZ_SHEET=FILE.ppm also writes the lasers' preview sheet, and with it
# LZ_SHAPES=FILE.ppm the fat bolts at every size and angle (FILE-capsule.ppm and so on: one a way).
set -e
cd "$(dirname "$0")/../.."
mkdir -p build/host
gcc -std=gnu99 -Wall -Wextra -O2 -I src -o build/host/lf_test src/lightfield.c tests/host/lf_test.c -lm
build/host/lf_test
# lasers.c once for each way of drawing fat bolts (lasers.h's LZ_NEAR); the sheets get the way's name
for v in 1:capsule 2:octagon 3:rect; do
    n=${v%%:*} name=${v#*:}
    gcc -std=gnu99 -Wall -Wextra -O2 -I src -DLZ_NEAR=$n -o build/host/lz_test_$name src/lasers.c tests/host/lz_test.c -lm
    build/host/lz_test_$name ${LZ_SHEET:+${LZ_SHEET%.ppm}-$name.ppm} ${LZ_SHAPES:+${LZ_SHAPES%.ppm}-$name.ppm}
done
