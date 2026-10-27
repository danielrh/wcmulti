# desktop/policy.sh: the two sandboxes (bubblewrap), read by run.sh and audit.sh: everything either program can
# see or reach is on these lines (desktop/README.md).  Bash; sets the arrays CORE_SANDBOX and FRONT_SANDBOX.
#   core_sandbox DATA PIPES [FILES]   rv-core: the game (FILES: where its data files are, if not in DATA)
#   front_sandbox PIPES               rv: its window, its sound and its input
# CORE_EXTRA, FRONT_EXTRA: mounts added before the root goes read-only (the audit's trace directory, its test
# program); empty when playing.
# Both: their own empty network namespace (no network at all), their own PID and IPC namespaces, no capabilities,
# nothing of the home directory, the system's programs and libraries read-only, the sandbox's own root read-only.

# the system's libraries, read-only (/lib and the like are links into /usr on most systems, directories on some)
system_ro() {
    local d
    SYS=(--ro-bind /usr /usr)
    for d in /lib /lib32 /lib64 /libx32 /bin /sbin; do
        if [ -L "$d" ]; then
            SYS+=(--symlink "$(readlink "$d")" "$d")
        elif [ -d "$d" ]; then
            SYS+=(--ro-bind "$d" "$d")
        fi
    done
    SYS+=(--ro-bind /etc/ld.so.cache /etc/ld.so.cache)
}

# rv-core: the game.  Its directory (DATA) at /data, where it may write (its log, its saves, screenshots); the
# game's own data files in it bound read-only on top (it cannot change them); the two pipes; nothing else: no /dev
# but bubblewrap's minimal one, no /proc, no /tmp, no environment.
core_sandbox() {
    local data=$1 pipes=$2 files=${3:-$1} f
    system_ro
    CORE_SANDBOX=(bwrap --unshare-all --die-with-parent --new-session --cap-drop ALL --clearenv
        "${SYS[@]}" --dev /dev
        --ro-bind "$BIN/rv-core" /rv-core
        --bind "$data" /data)
    for f in "$files"/*.DAT "$files"/*.MID "$files"/*.BNK "$files"/SHIP*.RVS "$files"/SHIP*.RVH; do
        [ -e "$f" ] && CORE_SANDBOX+=(--ro-bind "$f" "/data/$(basename "$f")")
    done
    [ -n "$pipes" ] && CORE_SANDBOX+=(--bind "$pipes" /pipes)
    CORE_SANDBOX+=("${CORE_EXTRA[@]}")
    CORE_SANDBOX+=(--remount-ro / --chdir /data)   # (the sandbox's own root read-only too: only /data is writable)
    return 0
}

# a /proc of the sandbox's own (its PID namespace's), where bubblewrap may mount one (a desktop: yes; some
# containers: no, and then none: never the host's, whose processes it would show)
proc_own() {
    PROC=()
    if bwrap --unshare-all --ro-bind /usr /usr --proc /proc /usr/bin/true 2> /dev/null; then
        PROC=(--proc /proc)
    else
        echo "policy.sh: bubblewrap cannot mount a /proc here: the front end runs without one" >&2
    fi
}

# rv: the window, the sound, the input.  The display's socket (X11 or Wayland) and the sound server's (PulseAudio
# or PipeWire), the GPU (/dev/dri) and the sound devices (/dev/snd), /sys and /proc read-only (the GPU's driver
# looks there), a /tmp of its own (thrown away), the two pipes; nothing it can write that outlives it, no home.
front_sandbox() {
    local pipes=$1 rt=${XDG_RUNTIME_DIR:-/run/user/$(id -u)} n
    system_ro
    proc_own
    FRONT_SANDBOX=(bwrap --unshare-all --die-with-parent --new-session --cap-drop ALL --clearenv
        "${SYS[@]}" --ro-bind-try /etc/alsa /etc/alsa --ro-bind-try /etc/pulse /etc/pulse
        --dev /dev --dev-bind-try /dev/dri /dev/dri --dev-bind-try /dev/snd /dev/snd
        "${PROC[@]}" --ro-bind /sys /sys --tmpfs /tmp --dir /run/rv
        --setenv HOME /tmp --setenv XDG_RUNTIME_DIR /run/rv --setenv PATH /usr/bin
        --setenv MESA_SHADER_CACHE_DISABLE true
        --ro-bind "$BIN/rv" /rv)
    if [ -n "${DISPLAY:-}" ]; then          # X11: its socket (the file: the abstract one is in the network namespace)
        n=${DISPLAY#*:}
        n=${n%%.*}
        FRONT_SANDBOX+=(--bind "/tmp/.X11-unix/X$n" "/tmp/.X11-unix/X$n" --setenv DISPLAY ":$n")
        if [ -n "${XAUTHORITY:-}" ] && [ -f "$XAUTHORITY" ]; then
            FRONT_SANDBOX+=(--ro-bind "$XAUTHORITY" /run/rv/Xauthority --setenv XAUTHORITY /run/rv/Xauthority)
        elif [ -f "$HOME/.Xauthority" ]; then
            FRONT_SANDBOX+=(--ro-bind "$HOME/.Xauthority" /run/rv/Xauthority --setenv XAUTHORITY /run/rv/Xauthority)
        fi
    fi
    if [ -n "${WAYLAND_DISPLAY:-}" ] && [ -S "$rt/$WAYLAND_DISPLAY" ]; then
        FRONT_SANDBOX+=(--bind "$rt/$WAYLAND_DISPLAY" "/run/rv/$WAYLAND_DISPLAY" --setenv WAYLAND_DISPLAY "$WAYLAND_DISPLAY")
    fi
    [ -S "$rt/pulse/native" ] && FRONT_SANDBOX+=(--bind "$rt/pulse/native" /run/rv/pulse/native)
    [ -S "$rt/pipewire-0" ] && FRONT_SANDBOX+=(--bind "$rt/pipewire-0" /run/rv/pipewire-0)
    [ -n "${SDL_VIDEODRIVER:-}" ] && FRONT_SANDBOX+=(--setenv SDL_VIDEODRIVER "$SDL_VIDEODRIVER")
    [ -n "${SDL_AUDIODRIVER:-}" ] && FRONT_SANDBOX+=(--setenv SDL_AUDIODRIVER "$SDL_AUDIODRIVER")
    [ -n "$pipes" ] && FRONT_SANDBOX+=(--bind "$pipes" /pipes)
    FRONT_SANDBOX+=("${FRONT_EXTRA[@]}")
    FRONT_SANDBOX+=(--remount-ro /)     # (the sandbox's own root read-only too: only its /tmp is writable)
    return 0
}

CORE_EXTRA=()
FRONT_EXTRA=()
