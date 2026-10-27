#!/bin/bash
# desktop/fetch-data.sh DIR: the game's data files from the published site (fireflyk64.github.io/rv, the deploy of
# commit 47fae9d5), each download checked against the SHA-256 below before anything is unpacked: a different file
# is refused.  For a machine without the DOS build (../build.sh); then
#   make data DATA=<the game's directory> FROM=DIR
# which adds the ships' sprite sets, made here from art/ships.  This is the only step that uses the network, and
# it only downloads: nothing fetched is run.  SITE=url fetches from another copy of the site (same hashes).
set -euo pipefail
[ $# = 1 ] || { echo "usage: desktop/fetch-data.sh DIR"; exit 2; }
dir=$1
site=${SITE:-https://fireflyk64.github.io/rv}
GAME_SHA256=7ede30c0d4c0b5633f82f7c147ef997ef22c981d67761c2e804cbec9d2950e28    # game.tar.gz
ROCKS_SHA256=510fe74fda6da50ef19b02127d3474bf2e91ff0289944f4a1ea8ac39c8d5c222   # art/ROCKS.DAT.gz
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
