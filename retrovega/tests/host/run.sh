#!/bin/sh
# The lightfield module's unit test on the host: gcc builds src/lightfield.c
# as the DOS build does (no DOS in it) with tests/host/lf_test.c and runs it.
set -e
cd "$(dirname "$0")/../.."
mkdir -p build/host
gcc -std=gnu99 -Wall -Wextra -O2 -I src -o build/host/lf_test src/lightfield.c tests/host/lf_test.c -lm
build/host/lf_test
