#!/bin/sh
# The play tests (tests/play/playtest.c, docs/gameplay.md): what src/logic.c has to do, a check at a time.
#   tests/play/playtest.sh [IDS]         the host's: host/build.sh (SDL=0), then build/host32-nosdl/playtest, where
#                                        gdb, rr, valgrind and the sanitizers work
#   tests/play/playtest.sh --dos [IDS]   and then the DOS build's in DOSBox (CODE=1 ./build.sh, RVP.EXE /playtest),
#                                        each test's world hash held against the host's: a difference is the logic
#                                        computing otherwise with DOS's 16-bit int (docs/logic.md, "Sized types")
#   tests/play/playtest.sh --steps ID    one test on both, the world's parts hashed after every step: the first step
#                                        where the DOS build parts from the host's, and in which arrays
# IDS: M2 runs M2.*, M2.3 that one.  ARCH, SAN, CC and LOGIC go to host/build.sh (LOGIC to build.sh too); DOSBOX
# to tests/run.sh (a stock DOSBox runs RVP.EXE).  The outputs: build/playtest-host.txt, build/playtest-dos.txt.
# Exit 0 only when every test passes and, with --dos, every hash matches.
set -e
cd "$(dirname "$0")/../.."
dos=
steps=
if [ "${1:-}" = --dos ]; then
    dos=1
    shift
elif [ "${1:-}" = --steps ]; then
    [ $# = 2 ] || { echo "usage: tests/play/playtest.sh --steps ID"; exit 2; }
    dos=1
    steps=1
    set -- "$2" steps
fi
mkdir -p build
SDL=0 host/build.sh > build/playtest-build.log 2>&1 || { cat build/playtest-build.log; exit 2; }
exe=$(sed -n 's/^built .* and \(.*\/playtest\)$/\1/p' build/playtest-build.log)
[ -x "$exe" ] || { cat build/playtest-build.log; exit 2; }
grep -i 'warning' build/playtest-build.log || true
status=0
"$exe" "$@" > build/playtest-host.txt || status=1
[ -n "$steps" ] || cat build/playtest-host.txt
[ -n "$dos" ] || exit $status

if [ -z "$steps" ]; then
    echo
    echo "the DOS build (RVP.EXE /playtest in DOSBox):"
fi
CODE=1 ./build.sh > build/playtest-dos-build.log 2>&1 || { cat build/playtest-dos-build.log; exit 2; }
if grep -q 'Error!' build/playtest-dos-build.log; then   # (build.sh does not stop on a C error)
    grep -B2 -A2 'Error!' build/playtest-dos-build.log
    exit 2
fi
NOBUILD=1 CYCLES=${CYCLES:-max} TIMEOUT=${TIMEOUT:-900} tests/run.sh tests/walk.txt /playtest $(echo "$@" | tr ' ' ',') \
    > /dev/null 2>&1 || true
tr -d '\r' < build/dos/RVLOG.TXT | grep '^playtest:' > build/playtest-dos.txt || true
[ -s build/playtest-dos.txt ] || { echo "RVP.EXE wrote no play test lines (build/dos/RVLOG.TXT)"; exit 2; }
if [ -n "$steps" ]; then
    # the first step whose world differs, and the parts that do
    grep ' step ' build/playtest-host.txt > build/playtest-host.steps || true
    grep ' step ' build/playtest-dos.txt > build/playtest-dos.steps || true
    echo "$(wc -l < build/playtest-host.steps) steps on the host, $(wc -l < build/playtest-dos.steps) in DOSBox"
    paste -d '\n' build/playtest-host.steps build/playtest-dos.steps | awk '
        NR % 2 == 1 { h = $0; next }
        h != $0 {
            split(h, a, " "); split($0, b, " ")
            printf "the first difference: %s %s %s; these parts differ:", a[2], a[3], a[4]
            for (i = 6; i <= length(a); i++) if (a[i] != b[i]) { split(a[i], p, "="); printf " %s", p[1] }
            print ""; print "  host: " h; print "  DOS:  " $0; found = 1; exit 1 }
        END { if (!found) print "every step the same" }' && exit 0 || exit 1
fi
cat build/playtest-dos.txt
grep -q '^playtest: [0-9]* of [0-9]* pass' build/playtest-dos.txt || { echo "RVP.EXE did not finish the tests"; exit 2; }
grep -q '^playtest: FAIL' build/playtest-dos.txt && status=1
# each test's world, DOS's against the host's
echo
pick() { awk '$2 == "PASS" || $2 == "FAIL" { print $4, $3 }' "$1" | sort; }
pick build/playtest-host.txt > build/playtest-host.hash
pick build/playtest-dos.txt > build/playtest-dos.hash
if join build/playtest-host.hash build/playtest-dos.hash | awk '
        $2 == $3 { same++ }
        $2 != $3 { diff++; print "DIFFERS " $1 ": the DOS build reached another world (" $3 ", the host " $2 ")" }
        END { print "hashes: " same + 0 " the same as the host'"'"'s, " diff + 0 " different"; exit diff > 0 }'; then
    :
else
    echo "(a different world: something in the logic computes otherwise with a 16-bit int: docs/gameplay.md,"
    echo " \"When DOS differs\")"
    status=1
fi
exit $status
