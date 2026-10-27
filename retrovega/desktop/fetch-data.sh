#!/bin/bash
# desktop/fetch-data.sh DIR: the game's data files from the published site (fireflyk64.github.io/rv), each download
# checked against the SHA-256 below before anything is unpacked: a different file is refused.  The site's files are
# made reproducibly (host/web/site.sh, tools/artweb.py): these hashes hold until the data itself changes.  For a
# machine without the DOS build (../build.sh); then
#   make data DATA=<the game's directory> FROM=DIR
# which adds the ships' sprite sets, made here from art/ships.  This is the only step that uses the network, and
# it only downloads: nothing fetched is run.  SITE=url fetches from another copy of the site (same hashes).
set -euo pipefail
[ $# = 1 ] || { echo "usage: desktop/fetch-data.sh DIR"; exit 2; }
dir=$1
site=${SITE:-https://fireflyk64.github.io/rv}
GAME_SHA256=b4d4eae15d1494dfb3c3dedffe08a6bad6b1339b7e628613a148062ec6fc034b    # game.tar.gz
ROCKS_SHA256=c2e51d50e103fd745bae8dfbe666eefce5d2837a6438a427cd66516745bb8762   # art/ROCKS.DAT.gz
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
curl -fsSL --proto =https -o "$tmp/game.tar.gz" "$site/game.tar.gz"
curl -fsSL --proto =https -o "$tmp/ROCKS.DAT.gz" "$site/art/ROCKS.DAT.gz"
echo "$GAME_SHA256  $tmp/game.tar.gz" | sha256sum -c --quiet \
    || { echo "fetch-data.sh: game.tar.gz is not the checked-in one (its SHA-256 differs): not used"; exit 1; }
echo "$ROCKS_SHA256  $tmp/ROCKS.DAT.gz" | sha256sum -c --quiet \
    || { echo "fetch-data.sh: ROCKS.DAT.gz is not the checked-in one (its SHA-256 differs): not used"; exit 1; }
# (only plain file names in the tarball: nothing that could land outside DIR)
if tar -tzf "$tmp/game.tar.gz" | grep -qv '^[A-Za-z0-9_.-]*$'; then
    echo "fetch-data.sh: game.tar.gz names something other than plain files: not used"
    exit 1
fi
mkdir -p "$dir"
tar -xzf "$tmp/game.tar.gz" -C "$dir" --no-same-owner --no-same-permissions
gunzip -c "$tmp/ROCKS.DAT.gz" > "$dir/ROCKS.DAT"
echo "fetch-data.sh: the game's data files in $dir (checked); now: make data DATA=... FROM=$dir"
