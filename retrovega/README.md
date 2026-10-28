# RetroVega (working title)

A new DOS space-combat game: 640×400, EMS 4.0, a 128-slot entity table. It
is built from the ground up so that the multiplayer DOSBox in this
repository can puppeteer it like WC.EXE: same named memory, same calls, same
hook contracts. It is set in Vega Strike's universe: the Terran
Confederation against the Aera, with the Rlaan meddling.

- [docs/spec.md](docs/spec.md): the engine specification. It covers the platform, the fixed memory layout, the code ABI the hook layer drives, the simulation rules, video, the EMS budget, art direction and milestones.
- [data/ships.csv](data/ships.csv): the roster. Ship type numbers, roles, retro stats, and the source VDU image and mesh in Vega Strike's assets.
- [data/weapons.csv](data/weapons.csv): guns, turrets and missiles, numbered as they appear in the gun table.
- [abi/rvabi.def](abi/rvabi.def): the single source of every offset the hook layer uses; `tools/rvabi.py` lays it out.
- [docs/roster.png](docs/roster.png): contact sheet of the roster's VDU images (`tools/vdu_sheet.py docs/roster.png`).
- [docs/gallery.png](docs/gallery.png): every ship of the roster as rendered for the game, two views each (`tools/render_all.py`, then `tools/gallery.py docs/gallery.png`).
- [docs/people.md](docs/people.md): the cast (data/people.csv: names, callsigns, birthplaces, backgrounds, looks). Their portraits come from the image-synthesis prompts in [docs/portrait-prompts.md](docs/portrait-prompts.md); mouth frames are driven by the letters of their lines (`tools/lipsync.py`), plus blinks. `tools/portraits.py` lines the generated frames up into talking faces (art/people/faces/); [docs/people.png](docs/people.png) is the cast sheet.
- [docs/location-prompts.md](docs/location-prompts.md): image-synthesis prompts for the carrier's rooms (bar, bunks and locker, briefing, launch deck and canopy, debrief); the cast is placed in them as busts behind counters, tables and lecterns.
- [docs/engine.md](docs/engine.md): the DOS game's engine design for its first slice (rooms, campaign, briefings, save and load), the toolchain (Open Watcom 2, NASM, `build.sh`), 32 MB of XMS, and how it stays compatible with the multiplayer layer's trampolines. `src/rvhooks.h` lists every place the layer can hook.
- [docs/gameplay.md](docs/gameplay.md): the implementation guide for writing the play by hand, like the space combat of the early nineties. It covers:
  - nine milestones, from the flight model through guns, damage, deaths, the AI, collisions and missiles to the mission;
  - every function to fill in, and where the game calls it;
  - the memory map: each ship's state and the mission's tables, and where victory and loss are kept;
  - the mission format.
  - The play tests (`tests/play/`), which fail now and pass as each piece is written, check it on the host under gdb and the sanitizers, and in DOSBox, where each test's world must equal the host's.
- [docs/logic.md](docs/logic.md): the guide to the game's logic in flight (`src/logic.c`, written by hand). It covers:
  - the main loop's steps, and the helpers for the world, shots, collisions and the HUD;
  - the rules the multiplayer layer needs kept;
  - memory and time budgets, asteroid-field combat;
  - what the engine still lacks;
  - the sized types (`src/rvtypes.h`) and the host harness, rvsim (`host/`): the logic built for 32- or 64-bit Linux, under gdb, rr, valgrind and the sanitizers, with the multiplayer rules checked, and for a browser at https://fireflyk64.github.io/rv/sim/.
- The native build (`host/game.sh`, [docs/engine.md](docs/engine.md) "The native build"): the whole game compiled for a 32-bit host or the browser, no emulator, DOS's services answered by `host/hle.c` and the assembly replaced by its tested C twins: https://fireflyk64.github.io/rv/ is this build.
- [desktop/](desktop/README.md): the game for a Linux desktop, built to be audited. It has a plain Makefile with no generated code or downloads. The game (`rv-core`) runs in a seccomp filter and a bubblewrap sandbox with only its own directory, apart from its window (`rv`). `desktop/audit.sh` checks the build's programs and writes, and the game's system calls and writes, against a baseline recorded from the reviewed version, and it can watch a game as it is played.
- [docs/places.md](docs/places.md): the rooms built for the game by `tools/places.py` (data/places.json):
  - **Bar and lounge:** pilots a mission seats at the tables, turning to the player when pointed at, and Tally polishing.
  - **Briefing:** the XO talking in lip sync, then the pilots rising.
  - **Quarters and locker.**
  - **Launch deck:** Kit's loops in front of every player fighter, at Vega Strike's own scale, on landing legs.

  The pictures are [docs/rooms.png](docs/rooms.png) and [docs/deck.png](docs/deck.png). `tools/roompreview.py` writes a page that plays the rooms.
- [docs/cockpits.png](docs/cockpits.png): the eight cockpits (the user's kits, cut by `tools/cockpits.py` into art/cockpits/) with their displays outlined; [docs/cockpits-packed.png](docs/cockpits-packed.png) is how the game shows them. The flight that uses them, its loop at the fixed hook sites and the drawing benchmark are in docs/engine.md, "Flight".
- [docs/art.md](docs/art.md): the sprite renderer. It turns a Vega Strike mesh into WC1's 37 views at twice WC1's resolution, in a livery. [docs/franklin-liveries.png](docs/franklin-liveries.png) shows the Franklin in four liveries.

The art comes from Vega Strike's Assets-Production, copied to
`~/dev/vegastrike-assets` (GPL-2.0, © the Vega Strike contributors; see
its `vega-license.txt`). Anything derived from it here is GPL as well.

## Title ideas

| title | why |
|---|---|
| **Coreward** (my pick) | Vega Strike's history says the Aera are "determined to punch through a corridor to core-ward expansion". That is the war in one word. |
| The New Jungle | The Aera homeworld was a hellish jungle, and "space became the new jungle, a realm to be watched and controlled". |
| Shmrn Pocket | The campaign's goal: the Andolian task force cut off defending the Shmrn. |
| Vega Frontier | Plain and retro. |
| Corridor | Short, and fits a box. |

The folder stays `retrovega/` until a title is chosen. Renaming is a
`git mv` plus the executable name and signature in the layer's `kGames`
row.

## Story backdrop

From Vega Strike's own history ("A brief history in time and space",
section "More or Less Now"):

- The Rlaan and the Aera drift toward an unofficial cease-fire on their border. That frees the Aera to turn on the human border.
- The Confederation has already lost several new border worlds.
- An Andolian task force sent to defend the Shmrn was surrounded when both neighbouring systems were overrun, and its fate is unknown.
- The Rlaan will not help in earnest. They fear an Aera victory, but they are happy to see the Confederation bled white, and to expand while it is busy.

The player flies from the TCS Vigilance with the relief group pushing toward
the Shmrn pocket.  In Vega Strike the Vigilance is a Purist-built corvette,
mass-produced, slow, tough and cheap, of a class "trickling into the frontier
territories to thwart rampant piracy and Aeran incursions"; it is also the one
capital ship in Vega Strike's data with an internal docking bay, and on this
one the bay has been refitted as the wing's flight deck.  The Archimedes, an
aging battleship still formidable at long range, is the group's heavy hitter;
the Watson, Vega Strike's mostly automated refuelling and rearming tender, its
support (and the ship most often in need of an escort).

The Rlaan are the third side throughout (`commAnimInfo` side 2, the "others"
the AI already knows):

- Gaozong probes shadow every battle.
- Xuanzong jammers break missile locks and fill the comms with static.
- Shizong disablers cripple whoever is winning.
- Shenzong haulers strip the wrecks.
- Now and then they help, when the Aera go after civilians.

The finale is the Aera dreadnought Leonidas.

Possible series:

1. Border picket: Aera probing raids after the cease-fire, and the first Rlaan probe.
2. Convoys (Mule, Ox) to the front, with Redeemer militia wingmen.
3. Breaking the Agasicles screen with Osprey bombers.
4. Into the pocket: reaching the Andolian Kahans, and escorting a Shaper Mongoose envoy to the Rlaan.
5. The Rlaan double game.
6. Hunting the carrier Agesipolis.
7. Leonidas.

## Roster

| | Terran Confederation | Aera | Rlaan |
|---|---|---|---|
| fighters | Schroedinger (light, the starter), Gawain (interceptor), Redeemer (militia assault), Pacifier/Osprey (fighter-bomber with a rear gunner seat), Lancelot/Destiny (heavy, if the art budget allows); Progeny/Mongoose rare, mostly escorted | Nicander, Ariston, Areus, Convolution (the aces' asymmetric strike fighter, in the Aera's blue-silver sheen), plus two new hulls built from them (tools/hulls.py): Agis (swarm fighter, gull-winged), Cleomenes (heavy assault, cross-winged) | Shizu (scout), Zhuangzong (line), Taizong (assault bomber), Shizong (disabler), Xuanzong (jammer), Gaozong (probe) |
| small craft | Franklin, the personnel transport: usually escorted, flown by the player on one or two missions where nothing else can get through; Goddard, the Andolian heavy bomber, escorted to its target from time to time | | |
| capital ships | Vigilance (home: a Purist corvette refitted as a light carrier), Kahan (Andolian destroyer), Archimedes (aging battleship), Watson (tender), Clydesdale (Merchant-built workhorse), Mule and Ox (transports) | Agasicles (destroyer), Anaxidamus (cruiser), Agesipolis (carrier), Charillus (transport), Leonidas (dreadnought) | Ruizong (destroyer), Shundi (cruiser and launch ship), Shenzong (transport) |

The factions differ in defence:

- Terran ships are balanced.
- Aera ships have strong shields.
- Rlaan ships have thin shields and very thick organic armour, as their Vega Strike stats have it.

Vega Strike has only three Aera fighters, so the other three are built
from those meshes by recipe and rendered through the same pipeline so
that they match.

## Open questions

1. The title.
2. In-game names: current Vega Strike names (Pacifier, Lancelot, Progeny, Leonidas) or the older ones the VDU files still carry (Osprey, Destiny, Mongoose, Yavok)?
3. Is the Lancelot/Destiny in?
4. The Aera riffs: are these names and roles good? Another idea is an older-generation Aera fighter in the flat-teal style of their capital ships, as a contrast to the glowing modern hulls.
