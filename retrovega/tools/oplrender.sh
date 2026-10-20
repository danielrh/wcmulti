#!/bin/sh
# Builds build/oplrender (tools/oplrender.cpp): src/opl.c on the host, through
# DOSBox's own OPL3 emulator (../src/hardware/dbopl.cpp).
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
DB=..
gcc -O2 -Wall -c src/opl.c -o build/host/opl.o
g++ -O2 -w -I$DB/include -I$DB/build-native -c $DB/src/hardware/dbopl.cpp -o build/host/dbopl.o
g++ -O2 -Wall -I$DB/include -I$DB/build-native -c tools/oplrender.cpp -o build/host/oplrender.o
g++ -o build/oplrender build/host/oplrender.o build/host/dbopl.o build/host/opl.o
