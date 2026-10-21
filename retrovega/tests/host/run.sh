#!/bin/sh
# The flight modules' unit tests on the host: gcc builds src/lightfield.c and
# src/lasers.c as the DOS build does (no DOS in them) with their tests and
# runs them.  LZ_SHEET=FILE.ppm also writes the lasers' preview sheet.
set -e
cd "$(dirname "$0")/../.."
mkdir -p build/host
gcc -std=gnu99 -Wall -Wextra -O2 -I src -o build/host/lf_test src/lightfield.c tests/host/lf_test.c -lm
build/host/lf_test
gcc -std=gnu99 -Wall -Wextra -O2 -I src -o build/host/lz_test src/lasers.c tests/host/lz_test.c -lm
build/host/lz_test $LZ_SHEET
