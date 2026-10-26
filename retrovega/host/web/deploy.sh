#!/bin/sh
# The browser rvsim onto GitHub Pages, at https://fireflyk64.github.io/rv/sim/ (docs/logic.md, "In a browser"), with
# rv/ itself RetroVega's short address (web/rv/index.html: to the DOSBox page's built-in game): built from the
# committed tree, checked locally, copied into the Pages repository, pushed, and checked live.
#   host/web/deploy.sh "what changed"
# PAGES=~/dev/fireflyk64.github.io (the Pages repository's clone), PLAYWRIGHT_DIR=(Playwright's node_modules' parent:
# default the dosbox3d browser build's), RUN=(a prefix for the browser checks, such as a process reaper), PORT=8795
# (the local check's).
set -e
cd "$(dirname "$0")/../.."
[ -n "$1" ] || { echo "usage: host/web/deploy.sh \"what changed\""; exit 2; }
PAGES=${PAGES:-$HOME/dev/fireflyk64.github.io}
PLAYWRIGHT_DIR=${PLAYWRIGHT_DIR:-$PWD/../build-web/playwright}
export PLAYWRIGHT_DIR
PORT=${PORT:-8795}
[ -z "$(git status --porcelain src host tools data ../web/rv | grep -v '^??')" ] \
    || { echo "uncommitted changes in src, host, tools or data: commit them first (the page names its commit)"; exit 1; }
H=$(git rev-parse --short HEAD)
WEB=1 host/build.sh > build/hostweb-build.log 2>&1 || { tail -20 build/hostweb-build.log; exit 1; }
W=build/hostweb
python3 -m http.server -d $W --bind 127.0.0.1 $PORT > /dev/null 2>&1 &
SERVER=$!
trap 'kill $SERVER 2> /dev/null' EXIT
sleep 1
$RUN node host/web/check.mjs http://127.0.0.1:$PORT/ | tee build/hostweb-check.log
grep -q "RVSIM WEB OK" build/hostweb-check.log || { echo "LOCAL CHECK FAILED"; exit 1; }
(cd "$PAGES" && git pull -q --ff-only)
mkdir -p "$PAGES/rv/sim"
cp $W/index.html $W/rvsim.js $W/rvsim.wasm $W/rvsim.data "$PAGES/rv/sim/"
cp ../web/rv/index.html "$PAGES/rv/index.html"
rm -f "$PAGES/rv/rvsim.js" "$PAGES/rv/rvsim.wasm" "$PAGES/rv/rvsim.data"   # (rvsim's first place, rv/ itself)
cd "$PAGES"
git add -A rv
if git diff --cached --quiet; then
    echo "the live page is already this build"
else
    git commit -q -m "rv: $1 (from dosbox3d retrovega $H)"
    git push -q origin main
    echo "pages: $(git log --oneline -1)"
fi
cd - > /dev/null
for i in $(seq 1 40); do                # (Pages publishes in about a minute)
    if curl -sf "https://fireflyk64.github.io/rv/sim/rvsim.wasm?t=$(date +%s)" | cmp -s - $W/rvsim.wasm \
        && curl -sf "https://fireflyk64.github.io/rv/sim/index.html?t=$(date +%s)" | cmp -s - $W/index.html \
        && curl -sf "https://fireflyk64.github.io/rv/index.html?t=$(date +%s)" | cmp -s - ../web/rv/index.html; then
        echo "live files match"
        break
    fi
    [ $i = 40 ] && { echo "THE LIVE FILES NEVER MATCHED"; exit 1; }
    sleep 10
done
$RUN node host/web/check.mjs "https://fireflyk64.github.io/rv/sim/?t=$(date +%s)" | tee build/hostweb-live.log
grep -q "RVSIM WEB OK" build/hostweb-live.log && echo "DEPLOY OK" || { echo "LIVE CHECK FAILED"; exit 1; }
