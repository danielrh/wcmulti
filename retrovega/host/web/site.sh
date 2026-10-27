#!/bin/sh
# The native build's page, put together in build/gamesite/ (what deploy.sh puts at fireflyk64.github.io/rv/):
# index.html and game.js (host/web), rv.js and rv.wasm (host/game.sh WEB=1), game.tar.gz (the DOS build's data:
# what RV.EXE reads at the start), the shared page modules (web/rvsets.js, rvsprites.js and its worker, touch.js)
# and art/ (the ships' views and the rocks a flight asks for: tools/artweb.py).
#   host/web/site.sh            then serve build/gamesite (python3 -m http.server -d build/gamesite)
#   NOBUILD=1                   the browser build as it is (build/gameweb)
set -e
cd "$(dirname "$0")/../.."
OUT=build/gamesite
mkdir -p "$OUT/art"
[ -n "${NOBUILD:-}" ] || WEB=1 host/game.sh > build/gameweb-build.log 2>&1 || { tail -20 build/gameweb-build.log; exit 1; }
REV=$(git rev-parse --short HEAD)$(git diff --quiet HEAD -- src host tools data || echo M)
sed "s/@REV@/$REV/" host/web/game.html > "$OUT/index.html"
cp host/web/game.js build/gameweb/rv.js build/gameweb/rv.wasm "$OUT/"
for f in rvsets.js rvsprites.js rvsprites-worker.js touch.js; do cp "../web/$f" "$OUT/"; done
# (reproducible: the names sorted, the dates and owners fixed, gzip without its time stamp: the same data, the same
# bytes, so desktop/fetch-data.sh's pinned SHA-256 holds until the data itself changes)
(cd build/dos && tar --format=ustar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner --mode=a+r,u+w \
    -cf - RV.DAT RVGAME.DAT BALLS.DAT SKY.DAT SHAPES.DAT RVOPL.BNK *.MID) | gzip -9 -n > "$OUT/game.tar.gz.part" \
    && mv "$OUT/game.tar.gz.part" "$OUT/game.tar.gz"
python3 tools/artweb.py --out "$OUT/art" --build "$REV" > /dev/null
echo "built $OUT ($REV): $(du -sh "$OUT" | cut -f1)"
