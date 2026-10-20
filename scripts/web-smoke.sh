#!/bin/bash
# Runs the browser build end to end in two headless Chrome pages against a
# local lobby server (see scripts/web-smoke.mjs).  Needs web/dist from
# scripts/build-web.sh, the Go toolchain (for the lobby server), Node and
# Google Chrome.
#
#   scripts/web-smoke.sh [seconds] [mission] [series]
#   NATIVE_HOST=1 scripts/web-smoke.sh [seconds] [mission] [series]
#   scripts/web-smoke.sh pad      # a controller's stick in a Hornet and a Rapier (scripts/web-pad.mjs)
#   scripts/web-smoke.sh voice    # voice between two pages (scripts/web-voice.mjs)
#   scripts/web-smoke.sh hall     # the public lobby and room codes (scripts/web-hall.mjs; scripts/web-chatfilter-test.mjs first)
#   scripts/web-smoke.sh rv [s]   # RetroVega with no game files: lobby, a flight alone, a host and a wingman (scripts/web-rv.mjs)
#
# With NATIVE_HOST=1 the native DOSBox (NATIVE_DOSBOX, default
# build-native/src/dosbox or src/dosbox) hosts through the same lobby with
# the Rust transport and only the wingman is a browser page: the cross-play
# check between the two wclobby implementations.
# GAME_SOURCE=zip GAME_FILE=/path/to/game.zip (or GAME_SOURCE=gog with the
# GOG installer .exe) makes the pages bring their own game files through
# the drop zone instead of the server's wc.tar.gz.
# Logs and screenshots land in $OUT (default /tmp/web-smoke).
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${OUT:-/tmp/web-smoke}"
PORT="${PORT:-8765}"
LOBBY_PORT="${LOBBY_PORT:-8787}"
DURATION="${1:-90}"
mkdir -p "$OUT"
export PATH="$PATH:/usr/local/go/bin"

LOBBY="$ROOT/build-web/p2p-lobby-server"
if [ ! -x "$LOBBY" ]; then
    echo "building the lobby server"
    (cd "$ROOT/lobbylink" && CGO_ENABLED=0 go build -trimpath -o "$LOBBY" ./cmd/p2p-lobby-server) || exit 1
fi
PW="${PLAYWRIGHT_DIR:-$ROOT/build-web/playwright}"
if [ ! -d "$PW/node_modules/playwright" ]; then
    echo "installing playwright into $PW"
    mkdir -p "$PW" && (cd "$PW" && npm init -y >/dev/null && npm i --silent playwright) || exit 1
fi

# (The public lobby asks for 256 seats: a server has to be told to give a room that many.)
printf '[rooms]\nmax_players_hard = 256\n' > "$OUT/lobby.toml"
"$LOBBY" --config "$OUT/lobby.toml" --listen-http "127.0.0.1:$LOBBY_PORT" --allowed-origin "http://localhost:$PORT,http://127.0.0.1:$PORT" \
    --public-url "http://127.0.0.1:$LOBBY_PORT" --allow-no-origin > "$OUT/lobby.log" 2>&1 &
LOBBY_PID=$!
if [ "${1:-}" = rv ]; then
    # (the site as deployed: no wc.tar.gz, so a fresh page has no game files at all)
    export WEB_ROOT="$OUT/rv-dist"
    rm -rf "$WEB_ROOT" && mkdir -p "$WEB_ROOT"
    for f in "$ROOT"/web/dist/*; do [ "$(basename "$f")" = wc.tar.gz ] || ln -s "$f" "$WEB_ROOT/"; done
fi
python3 "$ROOT/web/serve.py" "$PORT" > "$OUT/http.log" 2>&1 &
HTTP_PID=$!
trap 'kill $LOBBY_PID $HTTP_PID 2>/dev/null' EXIT
sleep 1

if [ "${1:-}" = pad ]; then
    PLAYWRIGHT_DIR="$PW" SHOT_DIR="$OUT" node "$ROOT/scripts/web-pad.mjs" "http://localhost:$PORT/" "http://127.0.0.1:$LOBBY_PORT" wc1 2>&1 | tee "$OUT/pad.log"
    exit "${PIPESTATUS[0]}"
fi
if [ "${1:-}" = voice ]; then
    PLAYWRIGHT_DIR="$PW" node "$ROOT/scripts/web-voice.mjs" "http://localhost:$PORT/" "http://127.0.0.1:$LOBBY_PORT" 2>&1 | tee "$OUT/voice.log"
    exit "${PIPESTATUS[0]}"
fi

if [ "${1:-}" = rv ]; then
    PLAYWRIGHT_DIR="$PW" SHOT_DIR="$OUT" node "$ROOT/scripts/web-rv.mjs" "http://localhost:$PORT/" "http://127.0.0.1:$LOBBY_PORT" "${2:-30}" 2>&1 | tee "$OUT/rv.log"
    exit "${PIPESTATUS[0]}"
fi

if [ "${1:-}" = hall ]; then
    node "$ROOT/scripts/web-chatfilter-test.mjs" || exit 1
    PLAYWRIGHT_DIR="$PW" node "$ROOT/scripts/web-hall.mjs" "http://localhost:$PORT/" "http://127.0.0.1:$LOBBY_PORT" 2>&1 | tee "$OUT/hall.log"
    exit "${PIPESTATUS[0]}"
fi

if [ -n "${NATIVE_HOST:-}" ]; then
    DOSBOX="${NATIVE_DOSBOX:-$ROOT/build-native/src/dosbox}"
    [ -x "$DOSBOX" ] || DOSBOX="$ROOT/src/dosbox"
    [ -x "$DOSBOX" ] || { echo "no native dosbox (build it, or set NATIVE_DOSBOX)"; exit 1; }
    ROOM="SMOKE-$(tr -dc A-Z0-9 < /dev/urandom | head -c 6)"
    echo "native host $DOSBOX in room $ROOM"
    ( cd "$OUT" && exec env WCROOM="$ROOM" WCLOBBY="http://127.0.0.1:$LOBBY_PORT" WCLOBBY_ORIGIN="" \
        MIS="${2:-1}" SERIES="${3:-1}" WCNET_LOG=2 WCNET_AUTOKEYS=1 WCCALLSIGN=HOST \
        SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        timeout -s KILL $((DURATION + 40)) "$DOSBOX" -c "mount c ${DOSPATH:-$ROOT/wc}" -c "c:" -c "wc" \
        > "$OUT/native-host.out" 2> "$OUT/native-host.err" ) &
    NATIVE_PID=$!
    trap 'kill $LOBBY_PID $HTTP_PID $NATIVE_PID 2>/dev/null' EXIT
    sleep 8
    ROOM="$ROOM" NO_HOST=1 PLAYWRIGHT_DIR="$PW" SHOT_DIR="$OUT" node "$ROOT/scripts/web-smoke.mjs" \
        "http://localhost:$PORT/" "http://127.0.0.1:$LOBBY_PORT" "$DURATION" "${2:-1}" "${3:-1}" 2>&1 | tee "$OUT/smoke.log"
    status="${PIPESTATUS[0]}"
    echo "===== native host: last 25 log lines ($OUT/native-host.err) ====="
    grep -v '^$' "$OUT/native-host.err" | tail -25
    exit "$status"
fi
# PAGE_QUERY="?env.WCNET_LAG=200&env.WCNET_PERF=2" gives both pages extra
# address parameters (a simulated link, a forced exchange mode, ...).
PLAYWRIGHT_DIR="$PW" SHOT_DIR="$OUT" node "$ROOT/scripts/web-smoke.mjs" "http://localhost:$PORT/${PAGE_QUERY:-}" "http://127.0.0.1:$LOBBY_PORT" "$DURATION" "${2:-1}" "${3:-1}" 2>&1 | tee "$OUT/smoke.log"
exit "${PIPESTATUS[0]}"
