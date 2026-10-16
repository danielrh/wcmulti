# SO3: Special Operations 3, *Culloden*

A fan campaign for Wing Commander II, flown in the Hellcat V, running on
Special Operations 2's program: twenty missions in SO2's twenty slots, every
scene between them rewritten, two-player throughout (the second player
flies the named wingman).  `story.md` is the story, `campaign.py` the
missions, `texts.py` the scene texts, `build.py` makes the game files,
`pack.mjs` seals them for the page.  `docs/so3.md` has what was learned of
the game's data files to make this possible; `docs/newcampaign.md` how to
make the next campaign.

## How it works

Special Operations 2 is Wing Commander II's program with its data files
numbered 2 (`MODULE.002`, `SERIES.S02`, ...).  The number is set at five
places in the code; `SO3.EXE` is `SO2.EXE` with a 3 there and
`savegame.so3` as its saved game, so it reads a third set of files, which
are copies of the second with our missions (`MODULE.003`), our texts
(`SERIES.S03`) and the Hellcat (ship type 45: `SHIP.V45`, `PCSHIP.V45`,
`INTELSHP.045`) added.  The program is never shipped: the patch is a list
of byte sequences in the manifest (`so3.json`, `exe`), and the page makes
`SO3.EXE` from the player's own `SO2.EXE` when the game starts
(`web/so3.js`); `build.py` applies the same list for a native test.

## Building

    so3/build.py --hellcat <dir with XSHIP.V50 and PCSHIP.V50> [--test-game]
    node so3/pack.mjs pack build-so3/plain wc2/GAMEDAT/SERIES.S02 web/so3

`build-so3/plain/` is the SO3 file set; `--test-game` also makes
`build-so3/game/`, the whole game directory, for a native run:

    env WCNET=0 build-native/src/dosbox -c "mount c build-so3/game" -c "c:" \
        -c "loadfix -34 so3 Origin -k l s1 m0"

`build-so3/` is ignored by git: it is made from the game's own files.

## What may be published

Nothing derived from the game's files goes into the repository or onto the
site in the clear.  `pack.mjs` puts the whole file set into one AES-256-GCM
blob whose key is the SHA-256 of `GAMEDAT/SERIES.S02`, a file every owner of
Special Operations 2 has and no patch has changed; the page derives the same
key from the player's own copy, so only an owner can open it.  The blob
(`web/so3/so3.enc`) and its manifest (`so3.json`: the anchor's name, the
key's hash, the IV, the file list, the executable's patch list) are what is
committed; the executable itself is in neither.  Our own sources
(`campaign.py`, `story.md`, the tools) contain no bytes of the game.

The Hellcat V art is Mario "HCl" Brito's and Howard Day's conversion for
Wing Commander Academy (the CIC thread "Hellcat cockpit in the WC2 style"),
repacked here in WC2's fighter layout; it travels inside the same blob.
