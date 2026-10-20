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
| Music | Standard MIDI files, our own sequencer, the OPL3 with our own bank (MPU-401 on request) | below |

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
edges. The shape is the user's sketch, a triangular lattice from the start
(s) to victory (v) and loss (l), and **every node has exactly two ways
out**: one towards v (`good`), one towards l (`bad`).

```
             s              layer 1: the start
            / \
           a   b            layer 2: two nodes
          / \ / \
         c _ d _ e          layers 3-6: three nodes each
         |\     /|           d moves sideways, outwards, either way;
         | \   / |           c and e go down, straight or to the middle
         c _ d _ e
            ...
         c   d   e          layer 6: c reaches v or slides to d,
          \  |\  |           e reaches l or slides to d,
           v    l           d reaches either
```

- **A node is 4 missions.**
- **Which way:** after its four missions the node trends towards v (3 or
  4 wins, or 2 with the campaign's running total ahead) or towards l, and
  the engine takes that way out.
- **Sideways or down:** that depends on where the node sits. In layers
  3-5 the middle of a row of three moves sideways (outwards) and the
  outer nodes move down; the first two layers move down. In layer 6, as
  the sketch has it, v is reached only from the left two nodes and l only
  from the right two: an outer node's other way slides into the middle.
  So from deep on one side it takes two nodes in a row to swing the end.
- **Why it is a DAG:** sideways moves go outwards in layers 3-5 and
  inwards in layer 6, whose middle only goes down.
  The tools check that there is no cycle, that every node is reachable,
  and that a node's two ways out differ.
- **Size:** 15 nodes, 60 missions.

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

Built so far:

- **The base archive:** base 0 is `RV.DAT`, base n is `RVBASEn.DAT`, with
  the same room records and its own pictures. The save keeps the base.
- **Changing base:** loading a save from another base unloads the archive
  (`dat_close`: its XMS level, tables and fonts), loads the other, and
  calls `baseChanged`. `tests/base.txt` checks it with a copy of the
  carrier as base 1.
- **The mission level:** a launch takes the mission level and the end
  of the flight gives it back. It is empty until flight brings ships and
  sounds; with the carrier loaded about 23 MB of XMS are free for it.

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
- **Arrow keys:** they steer the pointer like a mouse, slowly at first
  (150 pixels a second) and faster the longer they are held (up to 700),
  diagonally with two. The game hooks int 9 to know which keys are down,
  and chains to the BIOS, which still queues the keystrokes.
- **Tab and Shift+Tab:** the pointer jumps to the next hotspot to the
  right (or left), round to the first again; Enter acts on it. Menus
  that take the arrows (the bunks, name entry) turn the steering off.
- **Switching back:** while in keyboard mode, mouse motion under 6
  mickeys in total is ignored. A click, or more motion than that, gives
  control back to the mouse.
- **Pilots turn:** when the pointer is on a seated pilot, they turn
  towards the player. The turning animation is a two-frame dissolve
  (ordered dither) from the talk pose to the face pose, and back when
  the pointer leaves.
- **Clicking a pilot** opens their talking close-up.

## Music: interactive MIDI on the OPL3

Built (src/music.c, src/opl.c):

- **Songs:** Standard MIDI Files (type 0 or 1), one per cue beside RV.EXE
  (`BRIEF.MID BAR.MID LAUNCH.MID WIN.MID LOSE.MID QUARTERS.MID`). They are
  read at start-up, their tracks merged and timed through their tempo maps
  in ticks of the music clock, and kept in XMS (resident). Each loops,
  padded to a whole bar.
- **Streaming:** the interrupt plays from a ring of 1024 events in
  conventional memory (some 40 s of the busiest song), which the main loop
  (`music_poll`) tops up from XMS; the next song's ring is filled by
  `music_cue` before the interrupt switches to it. Six songs cost 14 KB of
  conventional memory, however long they are.
- **The clock:** the PIT reprogrammed to 240 Hz. The BIOS's 18.2 Hz tick
  is still chained, so the game's clock keeps counting.
- **Output: the Sound Blaster's OPL3** (port 388h, found by the timer test;
  an OPL2 is not enough), with our own instrument bank `RVOPL.BNK`
  (`tools/oplbank.py`, CC0): 38 patches for every General MIDI program
  (by family where no patch is written) and the drum keys of channel 10.
  `src/opl.c` plays MIDI channel messages on the chip's 18 two-operator
  voices: a patch is one or two layers (a wide pad is two, detuned a few
  cents and put left and right; the electric piano is a soft body under a
  short metallic tine); velocity, volume (7), expression (11), pan (10)
  and the sustain pedal (64) are honoured. A note takes a quiet voice
  first, then one whose sound has died away by itself (the bank says how
  long each layer is heard), then the oldest releasing, and only then the
  oldest sounding. `RV.EXE /mpu` sends the same songs to a General MIDI
  synthesizer on the MPU-401 instead (UART mode, port 330h); with no OPL3
  that is the fallback. `/nomusic` turns it off.
- **Volume:** the master fade (0..1024) is attenuation on every sounding
  voice's carriers (20 log10); on the MPU-401 it scales each channel's
  controller 7.
- **Changing song:** `music_cue(cue, fade_ms)` fades the playing song out
  over `fade_ms`, silences every voice (the fastest release), starts the
  cue's song, and fades it in over half the time.
  `music_cue_at(cue, fade_ms, MUSIC_BEAT or MUSIC_BAR)` starts the fade on
  the next beat or bar of the old song, so a cut lands in time.
- **Cues:** the game cues the quarters (and the locker), the bar and
  lounge, the briefing, the launch, and a win or a loss at the debrief.
  All use 250 ms.
- **The songs** (`tools/songs.py`, ours, CC0): *Bunk Light* for the
  quarters (68 a minute in D major: an electric piano turning over in
  eighths under a wide pad, upright bass, the vibraphone with the tune and
  a flute answering it); *Off-Watch* for the bar (100 a minute, swung, in
  F: jazz guitar over electric-piano comping, a walking bass, ride and
  cross-stick; the vibraphone takes the second chorus); *Ready Room* for
  the briefing (72 a minute in D minor: a harp ostinato, strings, a horn
  line, a clarinet against it, timpani rolls, a far celesta ping).
  LAUNCH, WIN and LOSE are still `tools/midi.py`'s short placeholders.
- **Hearing it without a sound card:** `tools/oplrender.sh` builds
  `build/oplrender`, which runs `src/opl.c` on the host through DOSBox's
  own OPL3 emulator (dbopl) and writes a WAV: a song
  (`--bank RVOPL.BNK --mid SONG.MID`, on the game's 240 Hz clock, with the
  voices stolen and the most sounding at once), or what the game itself
  wrote (`--log OPLLOG.BIN`). `RV.EXE /midilog` records every OPL3
  register write (or MPU-401 byte, `MIDILOG.BIN`) with its tick;
  `tests/music.txt` walks the quarters, the lounge, the bar and the
  briefing with it on. Played back, each room change is a fade of some
  25 dB over a quarter second, the silence, and the next song in over an
  eighth; no note had to take a sounding voice.

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
7. The music engine. **Done.**
8. The layer: RV.EXE recognised, DS from RVHOOK, the new thunks logged.
   **Done** (below).

Steps 1-6 are done too: `tests/run.sh` plays `tests/walk.txt` (every room,
a save, a close-up, the briefing, the deck, a launch and its debrief),
`tests/campaign.txt` (a whole campaign: `/winrate 20` ends in a loss,
80 in a victory) and `tests/keys.txt` (the arrow keys) headless in DOSBox,
with screenshots.

## The layer and RV.EXE today

`src/cpu/` knows RV.EXE (`GAME_RV`, `BUILD_RV`):

- **Recognition:** by its name, `"RVHOOK"` at the image's first
  paragraph, DS from the relocated word after it, and the signature at
  DS:0400.
- **Hooks:** the per-instruction dispatcher has its own branch for it.
  It watches the 36 RVHOOK thunks (`src/cpu/wcnet_rv.h`, written by
  `tools/rvabi.py --layer` from the same rvabi.def) and logs each call
  with its arguments:
  `RV.EXE: roomEntered(2)`, `campaignNode(0, 1)`, `missionResolved(1)`.
- **Nothing else:** none of WC.EXE's hooks or periodic work run, so
  between thunks the cores only test the bitmap. That is the cheapest a
  hooked program can be, short of the hookless core.
- **Next, for multiplayer:** RV's column of `wcnet_ds.def` (`rvabi.py
  --column`) and the flight loop's sites (spec 5.3).
