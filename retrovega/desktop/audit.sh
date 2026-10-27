#!/bin/bash
# desktop/audit.sh: what the desktop build and the game do, held against what the checked-in version did
# (desktop/baseline/, recorded with --record; desktop/README.md says how to read each part).
#
#   audit.sh build [--record]   the build (make) in a sandbox of its own (no network; the source read-only; only
#                               the output directory writable), traced: every program it ran, every file it
#                               wrote or changed, any network call.  Against baseline/build-*.txt.
#   audit.sh static [--record]  the library functions each program calls (its dynamic imports), against
#                               baseline/imports-*.txt; and the ones rv-core must never call, whatever the baseline.
#   audit.sh run [--record]     rv-core in its sandbox (policy.sh), headless, on the walk (rooms, a save, a flight)
#                               and a 20 s flight, traced: every system call it made once the game started, every
#                               file it wrote.  Against baseline/core-*.txt.
#   audit.sh forbidden          the sandboxes refusing: rv-core's filter stops a socket, starting a program and
#                               executable memory; its sandbox, a file written outside its directory; the front
#                               end's and the build's sandboxes, the network and the home directory.
#   audit.sh watch DATA [RV.EXE's options]
#                               play (run.sh) with both programs traced, and say as it happens anything that is not
#                               the game writing its own files: a write elsewhere, the network, a program started,
#                               a system call rv-core's baseline does not have.
#   audit.sh all                build, static, run and forbidden; exits non-zero if any part found a difference.
#
# DATA=dir: the game's files for `run` (default ../build/desktop-data, else ../build/dos).  Needs bash, bwrap,
# strace, make, nm, sha256sum.
set -uo pipefail
export LC_ALL=C                         # (sorting and comparing byte by byte)
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
BIN=${BIN:-$root/build/desktop}
BASE=$here/baseline
LOGS=$root/build/audit
mkdir -p "$LOGS" "$BASE"
. "$here/policy.sh"
fail=0

say() { printf '%s\n' "$*"; }
bad() { printf 'DIFFERS  %s\n' "$*"; fail=1; }
good() { printf 'ok       %s\n' "$*"; }

# a list held against its baseline: lines added are differences, lines gone are only said
against() {   # NAME FILE (the list now) [--record]
    local name=$1 now=$2 base=$BASE/$1 added gone
    if [ "${3:-}" = --record ]; then
        cp "$now" "$base"
        good "$name: recorded ($(wc -l < "$base") lines)"
        return
    fi
    if [ ! -f "$base" ]; then
        bad "$name: no baseline (audit.sh ${name%%-*} --record, on the version you trust)"
        return
    fi
    added=$(comm -13 "$base" "$now")
    gone=$(comm -23 "$base" "$now")
    if [ -n "$added" ]; then
        bad "$name: new since the baseline:"
        printf '           + %s\n' $added
    else
        good "$name: nothing new ($(wc -l < "$now") lines)"
    fi
    [ -z "$gone" ] || printf '         (in the baseline, not seen now: %s)\n' "$(echo $gone)"
}

# the paths a strace line names (its quoted strings)
paths_of() { grep -o '"[^"]*"' | tr -d '"'; }

# --- the build ---

audit_build() {
    local rec=${1:-} out=$BIN tr=$LOGS/build-trace d   # (the programs run.sh runs: the audited build is the one played)
    rm -rf "$out" "$tr"
    mkdir -p "$out" "$tr"
    system_ro
    proc_own
    say "== the build: make, in a sandbox (no network, the source read-only, $out writable), traced"
    bwrap --unshare-all --die-with-parent --new-session --cap-drop ALL --clearenv \
        "${SYS[@]}" --ro-bind-try /etc/alternatives /etc/alternatives --dev /dev "${PROC[@]}" --tmpfs /tmp \
        --ro-bind "$root" "$root" --bind "$out" "$out" --bind "$tr" "$tr" \
        --setenv PATH /usr/bin:/bin --setenv HOME /tmp --setenv LC_ALL C --remount-ro / --chdir "$here" \
        /usr/bin/strace -ff -qq -s 400 -o "$tr/t" \
        -e trace=execve,execveat,open,openat,creat,mkdir,mkdirat,rmdir,unlink,unlinkat,rename,renameat,renameat2,link,linkat,symlink,symlinkat,chmod,fchmodat,truncate,%network \
        make OUT="$out" -j"$(nproc)" > "$LOGS/build.log" 2>&1
    local st=$?
    [ $st = 0 ] && good "make finished" || bad "make failed ($st): $LOGS/build.log"
    # the programs it ran (that started: a failed execve is a search along PATH)
    cat "$tr"/t.* | grep -E '^execve(at)?\(' | grep -E '= 0$' | sed -E 's/^execve(at)?\("([^"]*)".*/\2/' \
        | sort -u > "$LOGS/build-programs.now"
    # what it wrote or changed: inside the output directory, its own /tmp (thrown away), or elsewhere (refused: the
    # rest is read-only, but trying is a difference)
    cat "$tr"/t.* | grep -E '^(open|openat|creat)\(' | grep -E 'O_WRONLY|O_RDWR|O_CREAT|O_TRUNC|O_APPEND|^creat' \
        | paths_of > "$LOGS/build-writes.raw"
    cat "$tr"/t.* | grep -E '^(mkdir|mkdirat|rmdir|unlink|unlinkat|rename|renameat|renameat2|link|linkat|symlink|symlinkat|chmod|fchmodat|truncate)\(' \
        | paths_of >> "$LOGS/build-writes.raw"
    grep -v "^$out\(/\|$\)" "$LOGS/build-writes.raw" | grep -v '^/tmp/' | grep -v '^/dev/null$' | sort -u > "$LOGS/build-elsewhere.now"
    grep "^$out/" "$LOGS/build-writes.raw" | sed "s#^$out/##" | sort -u > "$LOGS/build-outputs.now"
    cat "$tr"/t.* | grep -E '^(socket|socketpair|connect|bind|listen|accept|accept4|sendto|sendmsg|recvfrom|recvmsg|getsockopt|setsockopt)\(' \
        | grep -v '^socketpair(AF_UNIX' > "$LOGS/build-network.now" || true
    say "   programs run: $(echo $(sed 's#.*/##' "$LOGS/build-programs.now" | sort -u))"
    against build-programs.txt "$LOGS/build-programs.now" $rec
    against build-outputs.txt "$LOGS/build-outputs.now" $rec
    if [ -s "$LOGS/build-elsewhere.now" ]; then
        bad "the build tried to write outside its output directory:"
        sed 's/^/           /' "$LOGS/build-elsewhere.now"
    else
        good "nothing written outside the output directory (and its own /tmp)"
    fi
    if [ -s "$LOGS/build-network.now" ]; then
        bad "the build made network calls:"
        sed 's/^/           /' "$LOGS/build-network.now" | head -20
    else
        good "no network calls"
    fi
    (cd "$out" && sha256sum rv rv-core rvsprites) > "$LOGS/build-sha256.now"
    if [ "$rec" = --record ]; then
        cp "$LOGS/build-sha256.now" "$BASE/build-sha256.txt"
    elif [ -f "$BASE/build-sha256.txt" ] && ! cmp -s "$BASE/build-sha256.txt" "$LOGS/build-sha256.now"; then
        say "note     the programs' bytes differ from the recorded build's (expected when the compiler or its"
        say "         libraries differ from the recording machine's; on the same machine and source, a difference)"
        sed 's/^/           /' "$LOGS/build-sha256.now"
    else
        good "the programs are byte for byte the recorded build's"
    fi
}

# --- the imports ---

audit_static() {
    local rec=${1:-} p never
    say "== the library functions each program calls (dynamic imports)"
    for p in rv-core rv; do
        nm -D --undefined-only "$BIN/$p" | awk '{print $NF}' | sed 's/@.*//' | sort -u > "$LOGS/imports-$p.now"
        against "imports-$p.txt" "$LOGS/imports-$p.now" $rec
    done
    # whatever the baseline: the game itself never opens a socket, starts a program or loads code
    never=$(grep -xE 'socket|connect|bind|listen|accept|accept4|sendto|sendmsg|recvfrom|recvmsg|getaddrinfo|gethostbyname|execve|execv|execvp|execl|execlp|system|popen|fork|vfork|clone|posix_spawn|dlopen|dlsym|ptrace|mprotect|syscall|mmap|mmap64' \
        "$LOGS/imports-rv-core.now" || true)
    if [ -n "$never" ]; then
        bad "rv-core imports functions it must never call: $(echo $never)"
    else
        good "rv-core imports no network, process, ptrace, dlopen, mmap, mprotect or syscall() function"
    fi
}

# --- the game ---

data_dir() {
    local d
    for d in "${DATA:-}" "$root/build/desktop-data" "$root/build/dos"; do
        [ -n "$d" ] && [ -f "$d/RV.DAT" ] && { echo "$d"; return; }
    done
    echo "audit.sh: no game files (make data DATA=..., or DATA=dir)" >&2
    exit 2
}

audit_run() {
    local rec=${1:-} src tr=$LOGS/run-trace name script w
    src=$(data_dir)
    rm -rf "$tr"
    mkdir -p "$tr"
    : > "$LOGS/core-syscalls.raw"
    : > "$LOGS/core-writes.raw"
    : > "$LOGS/core-trouble.now"
    : > "$LOGS/core-refused.now"
    say "== rv-core in its sandbox, headless, traced (the game's files from $src)"
    for name in walk flight; do
        case $name in
            walk) script=$root/tests/walk.txt; set -- ;;
            flight) script=$root/tests/flight.txt; set -- /flight 20 ;;
        esac
        w=$(mktemp -d)
        cp "$script" "$w/WALK.TXT"
        CORE_EXTRA=(--bind "$tr" /trace)
        core_sandbox "$w" "" "$src"
        CORE_EXTRA=()
        "${CORE_SANDBOX[@]}" /usr/bin/strace -f -qq -s 200 -o "/trace/$name" \
            /rv-core --data /data /script WALK.TXT "$@" > "$LOGS/run-$name.log" 2>&1
        if grep -q '^bye' "$w/RVLOG.TXT" 2> /dev/null; then
            good "$name: the game ran to its end"
        else
            bad "$name: the game did not end as it should ($LOGS/run-$name.log, $w/RVLOG.TXT)"
        fi
        # the system calls from the filter on (the game's), the files opened to write, and what must not be
        awk '/PR_SET_SECCOMP/ {on = 1; next} on' "$tr/$name" | sed -n 's/^[0-9]* *\([a-z_0-9]*\)(.*/\1/p' \
            >> "$LOGS/core-syscalls.raw"
        grep -E '^[0-9]+ +(open|openat|creat)\(' "$tr/$name" | grep -E 'O_WRONLY|O_RDWR|O_CREAT|O_TRUNC|O_APPEND' \
            > "$LOGS/core-opens.tmp"
        grep -v '= -1' "$LOGS/core-opens.tmp" | paths_of >> "$LOGS/core-writes.raw"
        grep '= -1' "$LOGS/core-opens.tmp" >> "$LOGS/core-refused.now"   # (a write tried and refused: to a data file)
        grep -E '^[0-9]+ +(socket|connect|bind|sendto|sendmsg|execve|execveat|ptrace|fork|vfork|clone)\(' "$tr/$name" \
            | grep -v 'execve("/rv-core"' >> "$LOGS/core-trouble.now"   # (but strace starting it)
        rm -rf "$w"
    done
    sort -u "$LOGS/core-syscalls.raw" > "$LOGS/core-syscalls.now"
    # the names the game wrote (the screenshots' are the scripts')
    sort -u "$LOGS/core-writes.raw" > "$LOGS/core-writes.now"
    say "   system calls: $(echo $(cat "$LOGS/core-syscalls.now"))"
    against core-syscalls.txt "$LOGS/core-syscalls.now" $rec
    against core-writes.txt "$LOGS/core-writes.now" $rec
    if grep -q '/' "$LOGS/core-writes.now"; then
        bad "the game opened a path to write (it only ever names files in its own directory):"
        grep '/' "$LOGS/core-writes.now" | sed 's/^/           /'
    else
        good "every file written is a plain name in the game's directory"
    fi
    if [ -s "$LOGS/core-refused.now" ]; then
        bad "the game tried to write files it may not (its data files are read-only):"
        sed 's/^/           /' "$LOGS/core-refused.now" | head
    else
        good "no write refused (it never tried to change its data files)"
    fi
    if [ -s "$LOGS/core-trouble.now" ]; then
        bad "network, process or ptrace calls:"
        sed 's/^/           /' "$LOGS/core-trouble.now" | head
    else
        good "no network, process or ptrace calls"
    fi
}

# --- the sandboxes refusing ---

audit_forbidden() {
    local t out st w
    say "== the sandboxes, refusing"
    # rv-core's filter (seccomp.c) and its sandbox (policy.sh), with rv-sandbox-test in rv-core's place
    w=$(mktemp -d)
    CORE_EXTRA=(--ro-bind "$BIN/rv-sandbox-test" /rv-sandbox-test)
    core_sandbox "$w" ""
    CORE_EXTRA=()
    for t in socket exec execmem; do
        out=$("${CORE_SANDBOX[@]}" /rv-sandbox-test "$t" 2>&1)
        st=$?
        [ $st = 70 ] && good "rv-core's filter: $t stopped ($out)" || bad "rv-core's filter: $t was not stopped ($st: $out)"
    done
    out=$("${CORE_SANDBOX[@]}" /rv-sandbox-test outside 2>&1)
    st=$?
    [ $st = 71 ] && good "rv-core's sandbox: files outside its directory refused ($(echo "$out" | sed 's/rv-sandbox-test: writing //' | tr '\n' ';'))" \
        || bad "rv-core's sandbox: a file outside its directory was written ($st: $out)"
    rm -rf "$w"
    # the front end's sandbox, and the build's: a shell in each tries the network and the home directory
    front_sandbox ""
    probe "the front end's sandbox" "${FRONT_SANDBOX[@]}"
    system_ro
    probe "the build's sandbox" bwrap --unshare-all --die-with-parent --cap-drop ALL --clearenv "${SYS[@]}" --dev /dev \
        --tmpfs /tmp --ro-bind "$root" "$root" --remount-ro /
}

probe() {   # NAME SANDBOX...
    local name=$1 out
    shift
    out=$("$@" /usr/bin/bash -c '
        (exec 3<> /dev/tcp/1.1.1.1/443) 2> /dev/null && echo "NET: reached 1.1.1.1:443"
        (exec 3<> /dev/tcp/127.0.0.1/22) 2> /dev/null && echo "NET: reached the machine'"'"'s own port 22"
        for f in .ssh .gnupg .config .local .bash_history .netrc .aws .mozilla; do
            [ -e '"$HOME"'/$f ] && echo "HOME: '"$HOME"'/$f is visible"
        done
        touch /usr/rv-probe 2> /dev/null && echo "WROTE: /usr"
        touch /etc/rv-probe 2> /dev/null && echo "WROTE: /etc"
        touch /rv-probe 2> /dev/null && echo "WROTE: /"
        true' 2>&1)
    if [ -n "$out" ]; then
        bad "$name: $(echo $out)"
    else
        good "$name: no network (not even the machine itself), nothing of the home directory, / /usr /etc unwritable"
    fi
}

# --- while playing ---

# one program's trace (stdin) to alerts: WHO is core or front
watch_filter() {
    awk -v who="$1" -v base="$BASE/core-syscalls.txt" -v alerts="$LOGS/watch/alerts.txt" '
        BEGIN { while ((getline s < base) > 0) known[s] = 1 }
        function say(m) { print m; print m >> alerts; fflush(); fflush(alerts) }
        function mine(p) { return p ~ /^\/(dev|tmp|proc|sys|run\/rv|pipes)\// || p == "/dev/null" }   # (the front end'"'"'s own)
        {
            line = $0; sub(/^[0-9]+ +/, "", line)
            if (!match(line, /^[a-z_0-9]+\(/)) next     # (a resumed call, a signal, an exit: no call starts)
            name = substr(line, 1, RLENGTH - 1)
            short = substr(line, 1, 160)
            if (who == "core" && line ~ /PR_SET_SECCOMP/) { started = 1; next }
            if (line ~ /^(socket|connect|bind|sendto|sendmsg|listen|accept)/ && !(who == "front" && line ~ /AF_UNIX|AF_LOCAL|AF_NETLINK|sun_path/))
                say("ALERT  " who ": network: " short)
            if (line ~ /^execve/ && seen_exec++)
                say("ALERT  " who ": started a program: " short)
            if (line ~ /^(open|openat|creat)\(/ && line ~ /O_WRONLY|O_RDWR|O_CREAT|O_TRUNC|O_APPEND/) {
                match(line, /"[^"]*"/); path = substr(line, RSTART + 1, RLENGTH - 2)
                refused = line ~ /= -1/
                if (who == "core") {
                    if (path == "/pipes/in" || path == "/pipes/out") { }   # (its pipes to the front end)
                    else if (refused) say("ALERT  core: tried to write " path " (refused)")
                    else if (path ~ /\//) say("ALERT  core: wrote a path: " path)
                    else if (!written[path]++) say("game   wrote " path)
                } else if (!mine(path)) {
                    say("ALERT  front: " (refused ? "tried to write " path " (refused)" : "wrote " path))
                }
            }
            if (who == "core" && started && !(name in known) && !newcall[name]++)
                say("ALERT  core: a system call not in the baseline: " short " (its filter stops it)")
        }'
}

audit_watch() {
    local data=$1 tr=$LOGS/watch
    shift
    rm -rf "$tr"
    mkdir -p "$tr"
    : > "$tr/core.strace"
    : > "$tr/front.strace"
    say "== playing, both programs traced ($tr): what is not the game writing its own files is said here"
    say "   (and kept in $tr/alerts.txt)"
    TRACE=$tr "$here/run.sh" "$data" "$@" &
    local game=$! t1 t2 w1 w2
    # each program's trace followed on its own (two files followed together would split a line half written)
    exec 3< <(tail -n +1 -F "$tr/core.strace" 2> /dev/null)
    t1=$!
    exec 4< <(tail -n +1 -F "$tr/front.strace" 2> /dev/null)
    t2=$!
    watch_filter core <&3 &
    w1=$!
    watch_filter front <&4 &
    w2=$!
    wait $game
    local st=$?
    sleep 1
    kill $t1 $t2 $w1 $w2 2> /dev/null
    exec 3<&- 4<&-
    say "== the game ended ($st): $(cat "$tr/alerts.txt" 2> /dev/null | grep -c '^ALERT') alerts"
}

case "${1:-}" in
    build) audit_build "${2:-}" ;;
    static) audit_static "${2:-}" ;;
    run) audit_run "${2:-}" ;;
    forbidden) audit_forbidden ;;
    watch) [ $# -ge 2 ] || { echo "usage: audit.sh watch DATA [RV.EXE's options]"; exit 2; }; shift; audit_watch "$@" ;;
    all) audit_build; audit_static; audit_run; audit_forbidden
         [ $fail = 0 ] && say "AUDIT OK: nothing differs from the baseline, and the sandboxes refuse" || say "AUDIT: differences above" ;;
    *) sed -n '2,/^set -uo/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'; exit 2 ;;
esac
exit $fail
