# The DOS game: engine design, first slice

This is the plan for RV.EXE's first playable slice: the carrier's rooms, a
branching campaign, briefings, the launch, save and load. There is no space
flight yet: a mission is won 80% of the time at random. It builds on the
puppet ABI (spec.md, abi/rvabi.def) so the multiplayer layer can hook it
from the start.

## Platform decisions

| | decision | why |
|---|---|---|
| CPU mode | 16-bit real mode, 386 instructions | the layer's trampolines and accessors are real-mode (spec §5) |
| Compiler | Open Watcom 2, `wcc -mm -3` (medium model: far code, near data, SS = DS) | the layer reads stack arguments through DS; Watcom's DOS startup keeps SS = DGROUP |
| Assembler | NASM `-f obj` for RVHOOK and the fixed block | |
| Big memory | **XMS, 32 MB** through the XMS 3.0 driver (functions 89h allocate-any, 0Bh move) | DOSBox's own XMS; `memsize=40` leaves 39 MB. EMS is no longer needed. |
| Video | VESA 100h, 640×400×256, 64 KB banks | as spec §7 |
| Mouse | int 33h | DOSBox has it built in; on real DOS, CuteMouse (GPL) |
| Music | Standard MIDI files, our own sequencer, MPU-401 first, OPL3 later | below |

Toolchain: `~/opt/watcom` (the 2026-10-01 build of Open Watcom 2, unpacked
from its `ow-snapshot.tar.xz`), with `WATCOM`, `INCLUDE` and `PATH` set by
`retrovega/build.sh`. DOSBox config: `machine=svga_s3`, `memsize=40`,
`xms=true`, `ems=false`.

## Staying compatible with the layer's trampolines

What the layer needs from a program (src/cpu/wcnet_vm.cpp, wcnet_game.cpp):

- **SS = DS.** It reads a far call's arguments at DS:SP+4. Watcom's medium
  model puts the stack in DGROUP.
- **Two scratch strings in DGROUP that it may overwrite with code** and
  run with CS = DS: `aLoadingWingCom` (6 bytes at +101) and
  `aSorryAnErrorHasOccured` (249 bytes). The fixed block reserves both
  (DS:0x0400, DS:0x0480).
- **Far cdecl calls through thunks**, AX the result, SI DI BP DS kept:
  RVHOOK's `jmp far` thunks (spec §5.1).
- **A way to find DS at load time.**

**One change to the spec's image layout.** Spec §3 put DGROUP at image
paragraph 0x100 with the C code after it. Watcom's DOS startup
(cstrt086.asm) frees all memory beyond the end of DGROUP's stack with int
21h/4Ah, so code after DGROUP would be given back to DOS and overwritten.
The image is therefore:

| image paragraph | contents |
|---|---|
| 0x0000 | RVHOOK, 4 KB: header, thunks, sites (unchanged) |
| 0x0100 | C code |
| after the code | DGROUP, last as Watcom wants: the fixed 32 KB block first (ordered ahead of Watcom's `_NULL`), then C data, BSS, stack |

DGROUP's paragraph now moves with the code size. RVHOOK's header carries
`dw seg DGROUP`, which DOS relocates at load, so the layer reads DS from
loadSeg:0008 instead of adding a fixed `dgroupPara`. That is a one-line
special case in `set_game()`. The fixed block keeps its DS offsets, so the
layer's accessors do not change.

## The hook header: rvhooks.h

`tools/rvabi.py --c-header` writes `src/rvhooks.h` from abi/rvabi.def. It
lists every place the layer can hook:

- **Data:** each fixed-block variable as a typed near pointer macro.
- **Thunks:** a far cdecl prototype each (the engine's callers call the
  thunk, the body is `name_impl`).
- **Sites:** each flight-loop site's RVHOOK offset.

`--asm` writes `src/rvhook.asm`, the thunks and the header.

The first slice adds these thunks, appended so no old thunk moves:

| thunk | when |
|---|---|
| `roomEntered(room)` | a room is shown |
| `campaignNode(node, mission)` | the campaign has picked the next mission |
| `missionLoaded(mission)` | the mission's ships are in the slot arrays, before the briefing |
| `missionResolved(result)` | the outcome is known (now: the 80% roll) |
| `gameSaved(bunk)` / `gameLoaded(bunk)` | after a save or a load |
| `baseChanged(base)` | the player's home ship changed |
| `uiMode(on)` | the engine enters (1) or leaves (0) screens without flight |

## The campaign: a DAG in layers

`tools/campaign.py` writes `data/campaign.csv`; the engine only follows its
edges.

- **Shape:** a start node, then 6 layers of nodes, then two endings, GOOD
  and BAD.
- **Layers:** layer 1 is the start node (column 0); layer 2 has columns
  −1..1; layers 3 to 6 have −2..2. Negative columns lean towards GOOD,
  positive towards BAD.
- **A node is 4 missions.**

**Where the next node is.** After its four missions:

- **Trend:** 3 or 4 wins trend GOOD, 0 or 1 trend BAD, and 2 follow the
  campaign's running total.
- **Strength:** a decisive result (4 or 0 wins) goes **down** a layer,
  one column towards the trend. A mixed result goes **sideways** in the
  same layer, one column towards the trend, if that moves away from the
  centre; otherwise it goes straight down (same column).
- **Why it is a DAG:** sideways moves only go outwards, so there are no
  cycles. The tool checks it anyway.
- **The endings:** going down from layer 6 reaches GOOD or BAD by the
  trend.
- **Edges in the data:** `down_good`, `down_bad`, `down`, `side_good`,
  `side_bad` per node (24 nodes, 96 missions).

Every node lists its four missions. For now they are all patrols,
generated by the tool.

## Missions: data/missions.txt

A text file with one block per mission. The tool checks every reference.
For example:

```
mission  L3m1_m2
node     L3m1
title    Patrol: the Rigel relay
type     patrol
music    briefing
briefing Pilots, listen up. Aera fighters have been sighted near the Rigel relay.
briefing You and Echo will sweep Nav 1 and Nav 2, then return to the carrier.
win      We hold the Rigel relay. Good flying.
lose     We lost the Rigel relay. Get some rest; we go again tomorrow.
nav      1 1200 0 300
nav      2 -800 0 2600
ship     player 0 nav0
ship     wingman 0 nav0 pilot=ECHO
ship     NICANDR 1 nav1 count=3
ship     ARISTON 1 nav2 count=2
bar      left=FROST middle=- right=DRIFTER
lounge   left=KESTREL middle=TORCH right=ANCHOR
talk     bar FROST DRIFTER "Ten credits says you come back first."
```

`tools/campaign.py` writes the campaign and these placeholder missions
(seeded, so the same every time); edit them by hand from here on.
`tools/rvdata.py` compiles campaign, missions and cast into
`RVGAME.DAT`: fixed-size records the C code reads straight into its
tables. The records' C structs are written by the same tool
(`src/rvdata.h`), so the two never disagree. At `missionLoaded` the
engine fills `missionShipTable` and `navPointTable` from the mission's
records, as WC.EXE does before a flight. The layer sees the same tables
it already reads.

## The cast: who is where

- **One place a mission:** each mission puts each pilot in at most one
  place: a bar seat, a lounge seat, or nowhere.
- **Fixed places:** Kit is always at the deck (she is painted into it),
  Tally always at the bar, the XO always at the briefing.
- **Art:** a pilot can only take a seat their art was drawn for
  (docs/places.md: three pilots per seat per room).
- **Who sits together:** `data/relations.csv` says who will share a
  table and talk (friends, rivals, the twins), and who will not. The tool
  only seats pilots at one table if every pair there may talk. A `talk`
  line must be between two pilots in the same room.
- **The checks run twice:** in the tool, and again when the engine loads
  a mission.

## Saving: a bunk is a slot

- **Slots:** the quarters' bunk hotspot opens the bunk list, 8 bunks.
- **Files:** a bunk's save is `BUNK01.SAV` .. `BUNK08.SAV`; the player
  names it (24 characters).
- **Contents:** a fixed-size record: magic, version, bunk, name, base,
  campaign node, mission within the node, wins in the node, the running
  trend, the random seed, and the player's fighter and livery choice.
  Then a record per pilot (the player and every wingman): missions flown,
  kills (the splash count), medals and ribbons as bits, and alive or
  dead.
- **Loading:** loading a bunk checks the magic, version and size, then
  the node.

## Rooms, bases and loading

**A base** is a ship the player lives on: the carrier now, another ship
later. It is a set of rooms with the same layout and its own pictures.

- **Data:** `data/bases.csv` names each base's rooms.
- **Changing base:** the engine frees everything it loaded for the old
  base and loads the new one's.

**XMS is used in levels.** Each level can be freed at once:

| level | holds | freed |
|---|---|---|
| resident | font, cursor, palettes, the music engine and its songs | never |
| base | the base's room plates, seat sprites, actor frames | on a base change |
| room | a room's decoded working set | on leaving the room |
| mission | the mission's ships (later: sprites, sounds) | when the mission ends |

Music is resident and keeps playing across every load.

**Getting around.** Every room has a door hotspot to its neighbours:

| from | to |
|---|---|
| quarters | lounge |
| lounge | quarters (the bunks), bar (the hatch) |
| bar | lounge, **briefing (the hazard-striped hatch on the back-left wall)** |
| briefing | the deck (after a briefing), the bar |
| deck | back to the briefing room for the debrief |

The bar's painted hatch, left of the chalkboard, is the door to the
briefing room: there is one, so no new art is needed. Coming back from a
mission lands in the briefing room for the debrief.

## Input: mouse and keys together

- **Mouse:** the pointer moves freely; anything under it lights up.
- **Arrow keys:** the moment one is pressed, the pointer jumps to the
  nearest hotspot in that direction, and Enter acts on it.
- **Switching back:** while in keyboard mode, mouse motion under 6
  mickeys in total is ignored. A click, or more motion than that, gives
  control back to the mouse.
- **Pilots turn:** when the pointer is on a seated pilot, they turn
  towards the player. The turning animation is a two-frame dissolve
  (ordered dither) from the talk pose to the face pose, and back when
  the pointer leaves.
- **Clicking a pilot** opens their talking close-up.

## Music: interactive MIDI

- **Songs:** Standard MIDI Files (type 0 or 1) are read once into
  resident memory.
- **Sequencer:** ours, run from the timer interrupt (PIT reprogrammed to
  480 Hz, the old 18.2 Hz tick chained).
- **Output:** a driver interface: MPU-401 UART first. OPL3 with an open
  General MIDI patch bank comes later, because the browser build has no
  General MIDI synth.
- **Fast transitions:** each channel's volume is the song's controller 7
  times a master fade, re-sent as the fade moves.
- **Changing song** is `music_play(song, fade_ms, at)`. The current song
  fades out over `fade_ms` (150-300 ms gives the quick cut-overs), the
  new one starts and fades in. `at` can wait for the next beat or bar of
  the old song, so a cut lands on the beat.
- **Cues** (room, briefing, launch, win, lose) map to songs in
  `data/music.csv`.
- **Placeholder songs** are written by `tools/midi.py`: our own short
  loops, CC0.

## Drawing 640×400 fast

**Done in the game:**

- **Back buffer:** the scene is composed in a back buffer in
  conventional memory, 4 segments of 100 rows.
- **Dirty rows only:** only the rows that changed are flipped.
- **XMS moves:** the flip, and every background restore, is an XMS move,
  not a CPU loop. DOSBox carries out an XMS move as one host-side copy
  through its memory handlers (`XMS_MoveMemory`, `mem_memcpy`), video
  memory included. Copying a whole 64 KB bank costs a few emulated
  instructions.
- **Sprites:** run-length spans, copied with `rep movsd`.
- **Palette fades:** ports 3C8h/3C9h.

**Trading memory for speed in flight.** With 32 MB of XMS and moves that
cost the emulated CPU almost nothing, the flight renderer can pre-render
instead of computing:

- **Rotations:** each ship view in its roll steps, decoded at mission load
  (spec §7 fallback 1 becomes the plan).
- **Scales:** each view at a ladder of scale steps.
- **Explosions, shields:** the same.

Drawing a ship is then picking a pre-rendered image and copying its
spans; nothing is rotated or scaled per frame. A mission loads only the
types it uses (spec §8's budget had about 11 MB spare).

**Optional, in the emulator: a hookless core for screens.** The cores
test every instruction against the hook bitmap (`wc_net_cpu_hook`, about
a seventh of the browser build's speed when it was a call; inline now).
While the game says it is in a screen without flight (`uiMode(1)`, a
write the layer sees), DOSBox could switch `cpudecoder` to a copy of the
normal core compiled without the hook test, and back on `uiMode(0)`. The
browser build has no dynamic core, so that copy is its "better core". It
needs a periodic check so the layer still gets its tick. With the XMS
flips above it may not be needed: measure first.

## Fonts, pointer, driver

- **Fonts:** rasterised from Roboto (Apache 2.0) by
  `tools/font.py` into our own bitmap format, 2-bit antialiased into
  palette ramps. Two sizes: 11 px for text, 16 px for titles.
- **Pointer:** our own art (CC0), an arrow and a highlight ring, drawn by
  the engine with save-under.
- **Mouse driver:** DOSBox's int 33h. Nothing to ship.

## Bounds

- **Fixed arrays:** every table is a fixed-size array with its count:
  rooms, hotspots, seats, pilots, missions, nodes, save slots, XMS
  allocations and the dirty list.
- **Checked access:** every index goes through `RV_CHECK(i, n)`. In the
  first builds a failure stops the game with the file, line and values on
  screen. The checks stay in until the slice is stable.

## Order of work

1. This document; rvhooks.h, rvhook.asm and the build: RV.EXE says
   hello in DOSBox with 32 MB of XMS. **Proves:** the layout and the
   toolchain.
2. Campaign, missions and cast data, with their compiler and checks.
3. Video, XMS, assets: the bar on screen with the pointer.
4. Rooms and doors, pilots turning, Tally polishing.
5. Briefing (the XO speaking, the text), deck (Kit, fighter choice),
   the 80% outcome, debrief, campaign moves, the two endings.
6. Bunks: save, load, names.
7. The music engine.
8. The layer: RV.EXE recognised, DS from RVHOOK, the new thunks logged.
