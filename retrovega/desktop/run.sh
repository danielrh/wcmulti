#!/bin/bash
# desktop/run.sh: RetroVega on a Linux desktop (desktop/README.md): the game (rv-core) and its window (rv) each in
# a bubblewrap sandbox of its own (policy.sh), talking over two pipes in a private directory.
#   desktop/run.sh DATA [RV.EXE's options]      DATA: the game's directory (`make data DATA=...` fills it)
# BIN=dir: where rv and rv-core are (../build/desktop).  TRACE=dir: each program run under strace, its trace
# written there (audit.sh watch reads them as they grow).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
BIN=${BIN:-$here/../build/desktop}
[ $# -ge 1 ] || { echo "usage: desktop/run.sh DATA [RV.EXE's options]"; exit 2; }
DATA=$(cd "$1" && pwd)
shift
[ -f "$DATA/RV.DAT" ] || { echo "run.sh: $DATA has no RV.DAT (make data DATA=$DATA)"; exit 1; }
command -v bwrap > /dev/null || { echo "run.sh: needs bubblewrap (bwrap)"; exit 1; }
. "$here/policy.sh"
pipes=$(mktemp -d)
trap 'rm -rf "$pipes"' EXIT
mkfifo -m 600 "$pipes/in" "$pipes/out"
core=(/rv-core --data /data --in /pipes/in --out /pipes/out "$@")
front=(/rv --in /pipes/in --out /pipes/out)
if [ -n "${TRACE:-}" ]; then
    mkdir -p "$TRACE"
    CORE_EXTRA=(--bind "$TRACE" /trace)
    FRONT_EXTRA=(--bind "$TRACE" /trace)
    core=(/usr/bin/strace -f -qq -s 200 -o /trace/core.strace "${core[@]}")
    front=(/usr/bin/strace -f -qq -s 200 -o /trace/front.strace "${front[@]}")
fi
core_sandbox "$DATA" "$pipes"
front_sandbox "$pipes"
"${CORE_SANDBOX[@]}" "${core[@]}" &
pid=$!
status=0
"${FRONT_SANDBOX[@]}" "${front[@]}" || status=$?
kill "$pid" 2> /dev/null || true        # (a front end that failed leaves the core waiting on its pipe)
wait "$pid" 2> /dev/null || true
exit "$status"
