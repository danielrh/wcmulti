#!/bin/sh
# RetroVega's own pages onto GitHub Pages: at https://fireflyk64.github.io/rv/ the game itself built for the browser
# (host/web/site.sh: the native build, docs/engine.md "The native build"), at rv/sim/ the browser rvsim (docs/logic.md,
# "In a browser"); built from the committed tree, checked locally, copied into the Pages repository, pushed, and
# checked live.
#   host/web/deploy.sh "what changed"
# PAGES=~/dev/fireflyk64.github.io (the Pages repository's clone), PLAYWRIGHT_DIR=(Playwright's node_modules' parent:
# default the dosbox3d browser build's), RUN=(a prefix for the browser checks, such as a process reaper), PORT=8795
# (the local checks' first port; rvsim's, the game's at PORT+1).
set -e
cd "$(dirname "$0")/../.."
[ -n "$1" ] || { echo "usage: host/web/deploy.sh \"what changed\""; exit 2; }
PAGES=${PAGES:-$HOME/dev/fireflyk64.github.io}
PLAYWRIGHT_DIR=${PLAYWRIGHT_DIR:-$PWD/../build-web/playwright}
export PLAYWRIGHT_DIR
PORT=${PORT:-8795}
GPORT=$((PORT + 1))
SHARED="../web/rvsets.js ../web/rvsprites.js ../web/rvsprites-worker.js ../web/touch.js"
[ -z "$(git status --porcelain src host tools data $SHARED | grep -v '^??')" ] \
    || { echo "uncommitted changes in src, host, tools, data or the shared page modules: commit them first (the page names its commit)"; exit 1; }
H=$(git rev-parse --short HEAD)
[ -f build/dos/RV.DAT ] || ./build.sh > build/dos-build.log 2>&1 || { tail -20 build/dos-build.log; exit 1; }
WEB=1 host/build.sh > build/hostweb-build.log 2>&1 || { tail -20 build/hostweb-build.log; exit 1; }
host/web/site.sh
W=build/hostweb
G=build/gamesite
python3 -m http.server -d $W --bind 127.0.0.1 $PORT > /dev/null 2>&1 &
SERVER=$!
python3 -m http.server -d $G --bind 127.0.0.1 $GPORT > /dev/null 2>&1 &
GSERVER=$!
trap 'kill $SERVER $GSERVER 2> /dev/null' EXIT
sleep 1
$RUN node host/web/check.mjs http://127.0.0.1:$PORT/ | tee build/hostweb-check.log
grep -q "RVSIM WEB OK" build/hostweb-check.log || { echo "LOCAL CHECK FAILED (rvsim)"; exit 1; }
SHOT_DIR=build $RUN node host/web/game-check.mjs http://127.0.0.1:$GPORT/ | tee build/gamesite-check.log
grep -q "GAME OK" build/gamesite-check.log || { echo "LOCAL CHECK FAILED (the game)"; exit 1; }
(cd "$PAGES" && git pull -q --ff-only)
mkdir -p "$PAGES/rv/sim" "$PAGES/rv/art"
cp $W/index.html $W/rvsim.js $W/rvsim.wasm $W/rvsim.data "$PAGES/rv/sim/"
cp $G/index.html $G/game.js $G/rv.js $G/rv.wasm $G/game.tar.gz $G/rvsets.js $G/rvsprites.js $G/rvsprites-worker.js \
    $G/touch.js "$PAGES/rv/"
rm -f "$PAGES"/rv/art/*
cp $G/art/* "$PAGES/rv/art/"
cd "$PAGES"
git add -A rv
if git diff --cached --quiet; then
    echo "the live pages are already this build"
else
    git commit -q -m "rv: $1 (from dosbox3d retrovega $H)"
    git push -q origin main
    echo "pages: $(git log --oneline -1)"
fi
cd - > /dev/null
for i in $(seq 1 40); do                # (Pages publishes in about a minute)
    if curl -sf "https://fireflyk64.github.io/rv/sim/rvsim.wasm?t=$(date +%s)" | cmp -s - $W/rvsim.wasm \
        && curl -sf "https://fireflyk64.github.io/rv/rv.wasm?t=$(date +%s)" | cmp -s - $G/rv.wasm \
        && curl -sf "https://fireflyk64.github.io/rv/index.html?t=$(date +%s)" | cmp -s - $G/index.html; then
        echo "live files match"
        break
    fi
    [ $i = 40 ] && { echo "THE LIVE FILES NEVER MATCHED"; exit 1; }
    sleep 10
done
$RUN node host/web/check.mjs "https://fireflyk64.github.io/rv/sim/?t=$(date +%s)" | tee build/hostweb-live.log
grep -q "RVSIM WEB OK" build/hostweb-live.log || { echo "LIVE CHECK FAILED (rvsim)"; exit 1; }
SHOT_DIR=build $RUN node host/web/game-check.mjs "https://fireflyk64.github.io/rv/?t=$(date +%s)" | tee build/gamesite-live.log
grep -q "GAME OK" build/gamesite-live.log && echo "DEPLOY OK" || { echo "LIVE CHECK FAILED (the game)"; exit 1; }
