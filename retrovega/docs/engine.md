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

**Escape never quits** (the user's rule, 2026-10-20: "that key is for
interacting with the browser" — leaving full screen must not end the
game). Esc closes what is open (a close-up, a dialogue, a briefing page)
and does nothing else; in flight the player's Esc is ignored (a test
script's still ends the flight: `script_active()`). The game is not meant
to be quit from the browser at all — the player reloads the page — so the
page runs `RV.EXE /noquit`; a DOS install, run without it, has Q in the
bunk menu ("Q quits to DOS"), the one deliberate way out. A test script's
`quit` works everywhere. If a flight hotkey is ever wanted, the user named
Alt-X, "so it's rare".

- **Mouse:** the pointer moves freely; anything under it lights up.
- **Arrow keys:** they steer the pointer like a mouse, slowly at first
  (150 pixels a second) and faster the longer they are held (up to 700),
  diagonally with two. The game hooks int 9 to know which keys are down,
  and chains to the BIOS, which still queues the keystrokes; those are read
  with the enhanced functions of int 16h (11h to look, 10h to take: the
  grey keys and F12 too), and "no key" is the zero flag, which Watcom
  gives in `REGPACK`'s flags (`REGS` has only the carry).
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
- **Clicking a pilot** opens their talking close-up. The words appear as
  the mouth says them (lipsync.c: a shape a step, vowels two, pauses at
  commas and stops), timed on the 240 Hz music clock. The pace is the
  bunk menu's *talk speed*: slow (the first pace, 100 ms a step, about 8
  letters a second), normal (twice that: the default, since the user found
  the first pace painful to read), fast, very fast (four times). T in the
  bunk menu steps through them, a click picks one; the choice is kept in
  `RVSETUP.CFG` (the browser page keeps it with the bunks). The bar's and
  the lounge's table talk stays up for a second plus its reading time at
  that pace.
- **Clicking the XO** at the briefing opens her talking close-up, the same
  as a pilot's (her face over the room blurred, her name and rank); the
  briefing goes on there, and a speech too long for the column shows the
  sentence being said.
- **The tally board:** the bar's chalkboard shows chalk scribbles from
  across the room; a click shows it up close, every pilot, most splashes
  first, in a chalk hand with tally marks (gfx.c's `gfx_chalk`: a stroke
  whose pixels are mixed toward white by a hash of where they are, so the
  slate shows through). The hand's font (13 KB) is loaded only while the
  board shows: after a flight the largest free block can be under 32 KB.

## Music: interactive MIDI on the OPL3

Built (src/music.c, src/opl.c):

- **Songs:** Standard MIDI Files (type 0 or 1), one per cue beside RV.EXE
  (`BRIEF.MID BAR.MID LAUNCH.MID WIN.MID LOSE.MID QUARTERS.MID LOUNGE.MID`). They are
  read at start-up, each track into a block of its own (so a track may be
  up to 64 KB and a song as long as its tracks allow), the tracks merged
  as they are read and timed through their tempo maps in ticks of the
  music clock, and written to XMS (resident) 256 events at a time. Each
  loops, padded to a whole bar.
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
- **Cues:** the game cues the quarters (and the locker), the lounge, the
  bar, the briefing, the launch, and a win or a loss at the debrief. All
  use 250 ms. A pilot's close-up keeps the room's song.
- **The songs** (`tools/songs.py`, ours, CC0). The user asked for the
  quarters and the bar to be five times as long as their first versions,
  so that they do not repeat: *Bunk Light* for the quarters runs 9.5
  minutes at 68 a minute in five sections (D major: an electric piano
  turning over in eighths, a wide pad, upright bass, the vibraphone's tune
  and a flute answering it; B minor; F major; A major, two instruments
  trading phrases; D major again, thinning out into the start).
  *Off-Watch* for the bar runs 12.5 minutes at 100 a minute, swung, in
  five (F: jazz guitar over electric-piano comping, walking bass, ride and
  cross-stick; B flat; an E flat bossa nova in straight eighths; a G minor
  blues with three solo choruses; F again, trading fours into the top).
  *Off Duty* for the crew lounge, where the other pilots sit (the user:
  less energetic than the bar but not sleepy, "notes of joy, notes of calm
  reflection", blending between the two), runs 9.2 minutes at 84 a minute
  in six sections that take turns, joyful (G, C, D: the flute's tune over
  fingerpicked nylon guitar, a shaker and a soft kick) and reflective
  (E minor, A minor: the pad up, the percussion gone, clarinet and horn,
  with a bright major IV and celesta pings in the minor), each turn a
  two-bar cross-fade of expression; the last section eases from E minor
  back into G with the celesta remembering the tune.
  *Ready Room* for the briefing stays 110 s (72 a minute in D minor: a
  harp ostinato, strings, a horn line, a clarinet against it, timpani
  rolls, a far celesta ping). The first section of each and the briefing
  are written out note by note; the later sections are written by
  `tools/compose.py` from their chords: rootless voicings that move as
  little as they can, a bass on the chord, and a seeded melody writer that
  works phrase by phrase (chord tones on the strong beats, scale steps
  between, an arch, fall or rise, answers in the question's rhythm).
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

- **Band buffer:** the screen is drawn a run of dirty rows at a time (at
  most 100) into one 64,000-byte buffer and flipped before the next run;
  every run is drawn whole, back to front, so nothing older is needed
  (screenshots read the screen back through the bank window). The four
  bands of the first slice cost 256 KB; the flight's dense sprite buffers
  have the 192 KB this gave back.
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
instead of computing. What the benchmark says about it, and the plan for
scales and rolls, is in "Flight" below.

**Optional, in the emulator: a hookless core for screens.** The cores
test every instruction against the hook bitmap (`wc_net_cpu_hook`, about
a seventh of the browser build's speed when it was a call; inline now).
While the game says it is in a screen without flight (`uiMode(1)`, a
write the layer sees), DOSBox could switch `cpudecoder` to a copy of the
normal core compiled without the hook test, and back on `uiMode(0)`. The
browser build has no dynamic core, so that copy is its "better core". It
needs a periodic check so the layer still gets its tick. With the XMS
flips above it may not be needed: measure first.

## Flight

Built (src/flight.c, cockpit.c, ships.c, rvflight.inc): the flight loop at
its fixed sites, the eight cockpits, one view of a ship drawn as many
times as the frame rate allows, and the mission's end. There is no combat
yet: after 20 seconds the flight lands and the outcome is still the 80%
roll.

### The loop and its sites

`src/rvflight.inc` is the loop the layer watches, hand-written and
included by the generated `rvhook.asm` inside RVHOOK, each site at the
offset `abi/rvabi.def` fixes with the instruction the WC.EXE place of the
same name has:

| site | what sits there |
|---|---|
| `mainLoopTop` 0x0400 | `sti`, then `call far flight_keys` (C): interrupts on, no live registers but SI DI BP |
| `statusSetByExitKey` 0x0438 | `mov [missionStatus], ax`, reached when the keys set a status |
| `statusCheckAfterKeys` 0x0440 | `cmp word [missionStatus], 0 / jne leave`, then `call far flight_frame` (C) and the orchestra |
| `statusCheckAfterFrame` 0x0480 | the same test, then `jmp mainLoopTop` |
| `skipOrchestra` 0x0500 | `call far flight_music` (music_poll), 5 bytes the layer may step over |
| `afterStartup` 0x0510 | the return point of `flight_startup`, called from `rv_flight_run` just before it |
| `afterHangarMission` 0x0520 | the return point of the `runHangarMission` thunk, which `rv_hangar` calls |
| `autopilotKey` 0x0540 | `call far flight_autopilot`, 5 bytes; stepped over it is a `retf` |
| `cockpitPointer` 0x0560 | `call far [bp+6]`, 4 bytes: the pointer's drawing function, passed by the caller |
| `autoAnimationBody` 0x0580 | the `autoAnimation` thunk's body after `push bp / mov bp, sp`; its arguments at [bp+6], [bp+8], [bp+0Ah] |

The game calls `rv_flight_run()`; it returns when `missionStatus` is set:
1 landed (the 20 seconds are up), 5 exit (Esc), or whatever the layer
writes. `flight_frame` is one simulation step: `frameCounter++`, then
`ship_physics` for every slot that holds a ship (the player's steering
or the stand-in AI's turn, acceleration toward `setSpeed`, velocity =
front x kps / 20 as spec §6 has it, position += velocity, the frame
re-orthonormalised), then the picture. The loop is free-running for the
benchmark; the 20 fps pacing of spec §6 comes with combat.

Three fixed-block scalars were added for it: `rvCockpitDamage` (a bit per
damage layer), `rvBenchCount` and `rvFps` (so the layer or the page can
read the benchmark). RVGAME.DAT carries `ships.csv` as `RvShipType`
records (speeds, turn rates, shields, armour, which have a cockpit), so
the simulation reads a type's numbers from data.

### The cockpits

The user's eight cockpit kits (Schroedinger, Franklin, Gawain, Redeemer,
Pacifier, Lancelot, Progeny, Goddard) go through `tools/cockpits.py`
into `art/cockpits/KEY/` (docs/cockpits.png) and `rvpack.py` into
RV.DAT, a record each (`RvCockpit`, src/rvasset.h):

- **Palette:** the flight palette (data/palette.json) with the UI's 16
  colours first and the cockpit's own 47 in 208-254, the mix and shade
  tables over all of it. The base is error-diffused into those 63 colours
  (docs/cockpits-packed.png).
- **The opening:** the base's transparent pixels. Its extent on every row
  (`extents`, a RAW asset) is what the world is clipped to, and the
  pixels the base paints inside those extents (struts, a crossbeam) are
  one small sprite drawn over the world.
- **Displays:** the two MFDs, the radar's circle, the survival display,
  speed, fuel, the lock and warning lamps, in one vocabulary whatever the
  kit called them. The Lancelot kit has no display map: its displays were
  found inside its damage regions by the glass colour.
- **Controls:** the throttle and the joystick as sprites at their place.
  The throttle slides up to 14 pixels with `setSpeed`; the joystick moves
  4 x 3 pixels to one of eight sides or back to the middle with the
  steering (the arrows at full rate, else the pointer's distance from the
  middle of the opening).
- **Damage:** every overlay is a sprite with a fixed bit
  (`tools/cockpits.py` BITS: 0 weapons MFD, 1 targeting MFD, 2 radar, ...
  23 canopy, 26 survival VDU), drawn while its bit of `rvCockpitDamage`
  is set; a layer attached to the throttle or the joystick moves with it.
  The benchmark sets one more bit every three seconds; D sets the next by
  hand, R clears them.

**Drawing a frame.** `cockpit_dirty` marks the opening's rows and the
radar's every frame and an instrument's rows only when its value moved;
`cockpit_draw` then paints each dirty band back to front: the base's rows
restored from XMS (one move a band), the world clipped to the extents,
the struts, the instruments, the controls, the damage. The back buffer
and the dirty-row flip already existed, so there is no flashing and no
damage-rectangle bookkeeping: a band is whole rows, restored and flipped
by XMS moves that cost the emulated CPU a few instructions. On a real
486 those moves would be the bottleneck (some 250 KB a frame through the
bus); in DOSBox the sprites are.

### The benchmark

Off by default since 2026-10-20 (`g_bench_wanted` 0): the copies tiled the
sky in the player's first flight ("many Aera fighters like a benchmark").
`RV.EXE /bench -1` is the adapting measurement the tests quote (tests
pass it to tests/flight.txt), `/bench N` a fixed count. Without it the
left MFD shows the controls instead of the readout — ARROWS TURN, Q E
ROLL, + - F S SPD — which is how the player learns to roll. Turning it off
exposed that nothing else capped the frame rate: the simulation is per
frame (a kps is 12.8 units a frame), and a bare flight ran at ~95 fps
natively, three times too fast; `flight_frame` now waits out the frame to
FPS_WANT (30) on the 240 Hz clock (the adapting benchmark runs uncapped,
since it measures the rate).

`tests/run.sh tests/flight.txt` flies the first mission for 20 seconds
(F01START..F05DEBR.BMP): the biggest band view of the first enemy type
(the Nicander's view 1, 82 x 121) is drawn N times inside the opening,
drifting about, with N raised while the frame rate over the last eight
frames stays above 31 and lowered under 29. N shows on the left MFD with
the rate, and the mean N over the second half is logged:

| build | sprites of 82 x 121 a frame at 30 fps | sprite pixels a second |
|---|---|---|
| `-od` (no optimisation), the row loop calling `vid_row` | 38 | 11.3 M |
| `-od`, the row pointer stepped | 46 | 13.7 M |
| `-ox` (now the default; `DEBUG=1` builds `-od -d2`) | 68 | 20.2 M |
| the same in the deployed page (20,000 cycles, headless Chrome on a 2-core box) | 25 | 7.4 M |
| `-ox`, one band buffer (video.c: 192 KB of conventional memory freed) | 74 | 22.0 M |
| `-ox`, the three real ships drawn scaled and turned beside the copies (native / the page) | 54 / 16 | 16 M / 4.8 M |

`RV.EXE /bench N` fixes the count (0: none, so `tests/ships.txt` can look
at the real ships, logged every 100 frames as `lightfield: frame ...`).

At `cycles=40000` (tests/rv.conf) that is about two emulated cycles a
pixel, the XMS fetch of each sprite's rows and the per-row call into
`rv_blit_row` (lowlevel.asm: a span is one `rep movsd`) included, with
the rest of the frame (the restore, the instruments, the flip, the
simulation) already paid for. The browser's row was measured by driving
the live page through the rooms with Tab and Enter (the deck's launch
hotspot sits at 478,176) and reading RVLOG.TXT; a desktop browser on a
faster machine will sit between the two. The number is live in
`rvBenchCount` while the flight runs.

### The lightfield: one function draws a ship

`ships_draw(set, rel, cam, ship, scale, ...)` is the draw the rest of the
flight goes through (src/ships.c): rel is the ship's position less the
camera's, cam and ship their bases (front, right, up), scale the size
wanted as a fraction of the view's full size. The pure part is
`src/lightfield.c`, plain C with no DOS in it, which
`tests/host/run.sh` builds with gcc and checks against reference results
(the trigonometry against libm, the view for a direction, the place and
turn on screen for a dozen cases, the scaler on a known picture, the
rotator on an L, the turned row's inside-span against the checked
kernel); the DOS build checks its asm kernels against the same C at every
flight's start (`lightfield: asm kernels agree with the C ones`).

- **Conventions**, fixed here and noted in abi/rvabi.def: the frame is
  left-handed like the screen (x right, y up, z into it), so the camera's
  up is up the screen; the renderer's +x is a ship's left (a right-handed
  mesh with its nose on +z), so the heading is taken toward −right. A band
  view's turn on screen is the angle of the ship's up seen through the
  camera; the top and bottom views, which lie nose left, use the nose.
- **lf_place** gives the screen point, the view (0..61) and the turn
  (1024 a turn); the flight takes the size from the distance: full size at
  2,500 km in the ABI's WC-scale units, where a kps is 12.8 units a frame
  and a dogfight spans thousands of km.
- **Three paths**, picked by what is wanted: a plain copy when the turn
  rounds to 0 and the size is full; a scaled copy (each output row expands
  its source row and maps columns, `rv_scale_row`); or turned and scaled by
  inverse mapping from a dense copy of the view (`rv_rotate_row`, fed only
  the row's inside-span so it needs no bounds test). Both kernels are
  lowlevel.asm with lightfield.c's C as the reference.

Measured on the Nicander's 82 x 121 view at `cycles=40000`:

| path | output pixels a ms | cycles a pixel |
|---|---|---|
| copy (`rv_blit_row`) | 24,100 | 1.7 |
| scaled to 3/4 (C `lf_scale_row` / asm `rv_scale_row`) | 1,930 / 2,550 | 21 / 16 |
| turned 45 degrees at 3/4 (C / asm) | 940 / 2,570 | 43 / 16 |
| the deployed page (20,000 cycles): copy / scaled / turned | 12,050 / 1,275 / 1,285 | the same ratio |

In DOSBox an instruction costs about a cycle whatever it is, so the
scaled and turned draws cost what their inner loops count: nine times a
copy. That settles the question of scales and rolls below: a sprite drawn
often must be a copy, so sizes and turns are rendered once (by these same
kernels) and copied after.

### Pre-rendered sets, and the hybrid

What the flight draws from now (src/ships.c, tools/rvsprites.c): the
user's design, in which the 37 or 62 full-size views (SHIPnn.RV) are the
*compressed* form of a ship and the decompressor runs on the host, not in
the emulator.

- **SHIPnn.RVS** holds every view at 13 size steps (each 0.84 of the last,
  full size down to 1/8) and, for every size from half down (steps 4..12:
  the recipe `0 4 9`), at roll steps of 22.5 degrees, all in the engine's
  own run-length row format: a block of the file goes into XMS by a
  straight copy (`file_to_xms`), the index (a view table per block) sits in
  XMS too and is fetched twelve bytes at a time. `tools/rvsprites.c` makes a
  set in 40 ms on the host with lightfield.c's kernels (the same C will
  build a DOS SETUP.EXE). The page makes them itself when RetroVega is
  flown: `web/rvsprites.js` is the generator in JavaScript, byte for byte
  the C tool's output (`scripts/rvsprites-check.mjs` compares every set
  under node), 0.1-0.4 s a ship, so the 1.2 MB-a-fighter files are never
  downloaded; a copy of RV.EXE without them falls back to the .RV views,
  scaled and turned at draw time.
- **Five stored turns give sixteen.** An upside-down copy (the rows in the
  other order, the same cost as a copy) turns a sprite by 180 − θ and
  mirrors its heading; a mirrored copy (`rv_blit_row_rev`, four pixels a
  `bswap`, 0.7 of a copy) turns it by −θ and mirrors the heading; both
  together turn it by 180. So a symmetric ship stores only the arc 0..90
  for every view and the flight draws the other eleven steps as those
  copies of the stored five, of the view or of the opposite heading's
  (`find_sprite`); the top and bottom views, their own mirror, have their
  own table; the asymmetric Convolution stores eight 45-degree steps.
- **Loading** (`ships_load_blocks`): the stream's slots (below), then the
  ladder for every type, then the rolls size by size, while the mission
  budget lasts (1 MB of XMS kept back); a draw whose block is missing takes
  the unturned sprite at that size. The full-size block is never loaded.
- **The hybrid** (flight.c `plan_draws`, once a frame): the ships on
  screen are sorted back to front; the biggest, while their pixels fit a
  budget (`g_onfly_px`, 32,000 a frame; `RV.EXE /onfly N`) and they are
  above 40% size, are drawn **exactly** — the full-size view turned by the
  true roll and scaled to the true size by inverse mapping from a dense
  copy (`ships_draw_exact`, two dense buffers kept between frames); the
  rest are copies from the sets, where 22.5-degree and 16% steps are
  invisible. A ship too big for what is left of the budget is drawn at
  half or quarter detail instead (every other row computed and the row
  above copied by `rep movsd`; then 2 × 1 blocks, `rv_rotate_row2`): the
  user's rule that a ship in your face gets blockier, never slower.

**The 15-degree lattice** (the user's choice after the comparison): the
lightfield is now any lattice of nb bands x nh headings (lightfield.h:
WC1's 37 views are 5 x 7; the fighters are rendered at 11 x 13, 145 views,
266 for the asymmetric Convolution, `render_all.py --step 15`), and
`lf_view_of` / `lf_pair_of` take the lattice. The exact path uses the
dense one, so a view switch pops half as far; the sets' smaller sizes keep
the 30-degree sub-lattice (every other band and heading), so the page's
memory does not quadruple. A symmetric ship's mirrored headings are no
longer stored anywhere: the generator makes the sets' from the pair, and
the engine makes a full-size one by expanding its pair backwards
(`make_dense`) or copying it mirrored (`ships_blit`). The .RV record
carries the lattice in its last two bytes (`pack.py`); capitals stay at
30 degrees.

**The stream** (2026-10-20, the user's design: "it doesn't impact the
drawing and you just get jumpiness until it streams in", a few cache slots
for the nearest ships, configurable). A set's full-size block (block 0, the
dense lattice, 0.77 MB a fighter at 15 degrees) stays in the file: the
engine keeps the .RVS open (`ShipSet.fd`) and each view's place and length
in its record (`ShipView.blk` 0, `at` and `len` in the file; the views lie
in index order, so a length is the gap to the next). `ships_load_blocks`
takes a few XMS **slots** of the longest view (`RV.EXE /stream N`, four by
default, up to eight; a fighter's biggest view is ~6.5 KB of run-length
rows). Reads happen only in the frame's plan: `plan_draws` calls
`ships_prefetch` for each ship it marks exact, nearest first, and
`stream_view` brings its view into the least recently used slot not asked
for this frame — at most `/reads N` reads a frame (one by default) — then
every draw of that frame finds it (the four bands agree). A ship whose
view is not in draws from the sets that frame (`ships_draw_exact` returns
0 and `draw_world` falls back), which is the one-frame jumpiness; the
tally is logged when the flight ends ("the stream read 104 views in 33 ms;
2 times a ship waited a frame"). The MFD's target picture and the
benchmark's sprite come from the ladder's first resident step (84%,
`ships_blit_size`) so they take no slot. DOS has no asynchronous read and
no DMA a program can drive, but DOSBox serves INT 21h at host speed with
the emulated clock standing still: 256 reads of 6 KB did not move the 240
Hz clock (under 16 µs a read), so the reads cost no frame time where this
game runs; on real hardware they would be a few ms each, which `/reads 0`
turns off (everything from the sets) and chunking would bound. What it
buys: a fighter type keeps ~2.2 MB of XMS at 15 degrees (ladder 0.8,
rolls at nine sizes 1.4) instead of 2.5 with the full-size block resident
and rolls at three, and a denser lattice for the exact draw now costs disk,
not memory.

**Shrinking by coverage** (2026-10-20): the user saw the fighters' wings
collapse into nothing at a distance ("less hand drawn and intentional and
more like a bug"): the generator made every smaller size by the flight's
own kernel, one source sample per output pixel, so anything thinner than
the sampling step dropped out. Both generators (`shrunk` in
tools/rvsprites.c and web/rvsprites.js, byte for byte) now shrink by
coverage: the picture is turned at full size first, then each small
pixel owns the box of source pixels whose centres fall in its footprint
(the boxes tile the source; the footprint is `lf_rotate_setup`'s at roll
0, so origins agree with the flight's), is drawn when enough of them are
ship — the user's ladder: half from half size up, a quarter below half,
an eighth below a quarter, any at all below an eighth — and takes the
colour of the ship pixel nearest the box's centre (no blending: the
palette is indexed, and a real pixel's colour reads as drawn). Full size
is untouched, so the exact path and block 0 are as before. A one-pixel
wing now survives as a line down to an eighth; the page makes its 19
sets in about 12 s under node (the box scan is a pass over the source
per size) — see scripts/rvsprites-check.mjs's timing.

**The player's roll on far ships** (2026-10-20): rolling the camera never
switches a view (the direction to the camera in the ship's frame does not
change), so a near ship turns smoothly on the exact path; a far one comes
from the sets' roll blocks, and with rolls only at sizes 4..6 a ship
beyond a third of full size was drawn upright while the stars turned (162
of 406 logged frames of `/zoom 3`, the far swing 3,000..15,000 km with
`hold rright`): the user's verdict "a massive bug, not shippable", against
"perfect" with rolls at every size from half down (`0 4 9`, 49 blocks a
set, +0.57 MB on disk and +0.54 MB of XMS a fighter type). The recipe is
`0 4 9` everywhere since (build.sh, web/wc.js, the check).

**A 10-degree lattice** (tried 2026-10-20, after the stream): `lattice(10)`
is 17 bands x 19 headings, 325 views (614 with an asymmetric ship's
mirrored headings); the 30-degree sub-lattice nests in it every third
(`sub_view`'s step is 3), so the sets' copies are unchanged and only block
0 — now disk, not XMS — grows: the Gawain's .RV 0.69 → 1.56 MB, its .RVS
2.5 → 3.4 MB, 38 s to render. The engine keeps `SHIP_VIEWS` (330) records
and resolves a symmetric ship's mirrored headings by index
(`mirrored_index`: past `2 + nb * nh`) rather than by reading a record, so
their indices may run past the table; an asymmetric set at 10 degrees
would need 614 records and stays at 15. In the zoom scene the target
switched view 137 times against 92 at 15 degrees (each a smaller step);
whether the pop still reads was put to the user as a blind pair of
videos. Recipe: `render_all.py --only GAWAIN --step 10 --out DIR`,
`pack.py --sprites DIR --out OUT` (it skips ships not rendered there),
`build/host/rvsprites OUT/SHIP02.RV OUT/SHIP02.RVS 0 4 3`, the two files
copied over build/dos's after `build.sh`.

**The sky** (2026-10-20, the user's ask: "a generic function to blit a
sprite at infinity at a vector ... so it will rotate with the player; that
way we can make a starfield if we want"). `lf_sky(dir, up, cam, cx, cy,
focal, &place)` (lightfield.c, unit-tested on the host) puts a direction
of the sky — any length, the world frame — through the camera exactly as
`lf_place` puts a ship: where it shows, and the turn on screen of a sprite
painted there whose up in the world is `up` (the world's own for most), so
it turns with the sky as the player rolls. `ships_draw_sky(set, v, dir,
up, cam, scale, ...)` paints a set's full-size view there through the
exact path (turned by the sky's roll, scaled); the view must be in the
frame's plan (`ships_prefetch_view`). The stars themselves are now 2,000
directions over the whole sky (the opening sees about a twentieth): each
frame `stars_place` puts them through the camera — three products first,
to drop the sky behind and beside — so they turn with pitch, yaw and roll
alike, and the ships sit still against them (the old field slid in two
dimensions against the steering). That loop is `rv_stars_place` in
lowlevel.asm: in C, Watcom's 16-bit code around three 32-bit products a
star cost 8 ms a frame (160 cycles a star; `lf_sky`'s `lf_atan2` for every
visible star had made it 15), which halved the drawing benchmark; the
assembly, 32-bit registers throughout, places the 2,000 in 1.1 ms and
draws the ~100 on screen in 0.35 (the flight's closing log line reports
both). Test scene: `RV.EXE /sky /stream 8`
with tests/sky.txt (three Gawain views painted ahead, high left and low
right; the player rolls, yaws, pitches): the sky sprites take stream
slots like any full-size view, so the scene asks for eight; real sky art
(suns, nebulae, planets) would live in its own small set, resident.

**Against WC1** (the user's recording, 2026-10-20): WC1 turned and scaled
its sprites on the fly, continuously, which the earlier note here got
wrong; at 320×200 and ~12 fps it had ten times our pixel-time budget per
sprite, which is why the hybrid reserves the exact path for the one or two
nearest ships. What still pops in ours is a **view switch**: when the
camera's direction in a ship's own frame crosses a 30-degree sector or
band, the next pre-rendered silhouette replaces the last (a rolling target
seen near nose-on switches eight times a turn; the player rolling switches
never, since that direction does not change). The remedies, in order: a
denser lightfield for the full-size views (15 degrees: 145 views, ~0.5 MB
of XMS a fighter, four times the .RV on disk), and a short checkerboard
crossfade at a switch. Test scenes: `tests/zoom.txt` with `/zoom 1`
(a Gawain ahead, rolling, 1,200..6,000 km on a twelve-second swing) and
`tests/zoom2.txt` with `/zoom 2` (it yaws slowly while the player rolls:
`hold rright N`), both with `/bench 0 /record 2`, turned into a video by
ffmpeg from the R*.BMP frames.

**In reserve: blocks plus a DOSBox hint.** The game could draw its nearest
ships at 2 × 2 or 4 × 4 block detail always and publish a draw list (type,
view, scale, roll, origin, detail) in the fixed block at `rvFrameDone`; the
browser's DOSBox, which already hooks RV.EXE, would composite its own
full-detail (even filtered, even 2×) rendering of those sprites over the
VGA frame at output time, host-side and outside the cycle budget, while
real DOS shows the blocks. A legitimate DOS game with an "HD" emulator;
about a day in the layer. Not taken yet.

### Laser bolts

The user's brief (2026-10-20): red bolts, bright white in the middle and red
toward the edges in two or three colours, an elongated pill (angular is
fine), visible from any angle and never less than a pixel, up to 32 px long
and 8 wide when just fired, integer lines once a little away, hundreds a
frame; quality may give way to speed.

**The shape.** A bolt is a capsule: a segment along its flight with a radius
round it (`LzBolt`, 18 bytes: its middle in positions, its half axis in
sixteenths of a km). A capsule projects to a capsule from any angle — end on
it is a disc — so it is visible however it flies. The flight's bolts are
2 km in radius and 12 km long, and the near plane is at 200 km, so the
largest is 8 px across and 32 px long side on (bolts.h).

**Two drawings, chosen by the radius on screen.** Under 3/4 px a bolt is a
*line*: each row it crosses gets the pixels whose centres the segment passes
over in that row (an x-major line's run), and a steep row with none gets the
pixel under the row's middle, so every row shows. Above, it is *fat*: up to
three nested capsules painted over each other — a dark red rim (fire ramp
130), a red body (`UI_RED`) and a white core (`UI_WHITE`) from 2 px of
radius, the body and a white core line below that. Each capsule row is one
span between its left and right boundary: a's arc, the straight stretch,
b's arc, met at the tangent rows (rounded so the stretch is never used past
them: on a nearly level bolt a sixteenth of a row there is pixels of x).
The flight palette has no pink, hence a dark rim rather than a light halo.

**The pipeline** (src/bolts.c over src/lasers.c and src/lasers.asm):

- once a frame, `rv_bolts_place`: every bolt through the camera (eighteen
  32-bit products, four divisions, the near-plane cut, the screen box), out
  as a line set up for the draw (`LzLine`, 20 bytes) or a fat bolt (`LzFat`);
  then `rv_fat_setup` makes each fat bolt's passes (`LzPass`);
- per band, `rv_lines_draw` draws every line, one call for all, and
  `rv_pass_draw` each fat bolt's passes, smallest bolt first; every row is
  clipped to the cockpit opening's extents as the ships are;
- `rv_bolts_move` flies them (positions += half axis x speed).

`lasers.c` is the reference, plain C: `tests/host/run.sh` checks it against
brute force (no capsule pixel off the true distance by more than 0.12 px; no
line row empty, no gap between rows; mirror images agree) and writes a
preview sheet with `LZ_SHEET=FILE.ppm`. The asm does the same to the bit:
`bolts_selftest` compares records and pixels when a laser flight starts.
DOSBox charges an instruction a cycle whatever it does (a `rep stos` a cycle
an iteration), so `imul` and `idiv` cost what a `mov` costs: the asm counts
instructions, not multiplies. Where C's 64-bit `muldiv` wraps, an `idiv`
would trap: `mdiv` divides in two steps.

**What each costs** (`bolts_time`, logged at the start of a laser flight;
µs at `cycles=40000`, twice that in the page at 20,000):

| | place | set up | draw | in all |
|---|---|---|---|---|
| a far bolt (a line, 1.6 rows) | 5.6 | — | 2.3 | 7.9 (+0.7 flown and re-fired) |
| a near bolt (fat, 2-4 px radius) | 5.2 | 13.8 | 24.0 | 43 |
| a far bolt drawn as a capsule (one algorithm for all) | | | | 23.5 |

So one algorithm for every bolt would cost three times the line for the far
ones: the split pays. The near bolts' set-up was 87 µs in C (Watcom's 64-bit
helpers) before `rv_fat_setup`.

**The benchmark.** `RV.EXE /lasers N` flies the first mission's launch into a
still scene: two big ships 1,500 and 2,100 km ahead, rolling and turning so
they are drawn exactly (`/bigships`, 2), small ships 5,000-15,000 km out
within 35 degrees of the nose drawn from the sets (`/farships`, 16), bolts
kept 220-900 km ahead (`/near`, 12, fat), and N far bolts fired every way
from the far ships, living one to three seconds (`/lasers -1`: N follows the
frame rate as the sprite benchmark's count does). `tests/lasers.txt` with
`/lasers -1 /flight 30`; `CYCLES=20000 tests/run.sh ...` gives the page's
budget, `NOBUILD=1` skips the build. Each frame's parts are timed on the
240 Hz clock and logged at the end (`lasers: ms a frame ...`):

| cycles | big ships | far ships | near bolts | far bolts held at 30 fps |
|---|---|---|---|---|
| 40,000 | 2 | 16 | 12 | ~230 (206-248 over three runs) |
| 40,000 | 2 | 16 | 0 | 269 |
| 40,000 | 1 | 8 | 12 | 1,690 |
| 40,000 | 2 | 0 | 12 | 1,271 |
| 40,000 | 0 | 16 | 12 | 1,973 |
| 40,000 | 0 | 0 | 12 | 2,388 and more (the pool full at 25 ms a frame) |
| 50,000 (the page since 2026-10-21, Chrome on a 2016 desktop core) | 2 | 16 | 12 | 1,129 |
| 20,000 (the page before) | 2 | 16 | 12 | none: 15 fps with no bolt |
| 20,000 | 1 | 8 | 12 | none: 24 fps with no bolt |
| 20,000 | 0 | 16 | 12 | none: 30 fps with no bolt |
| 20,000 | 1 | 0 | 12 | 159 |
| 20,000 | 0 | 4 | 12 | 829 |
| 20,000 | 0 | 0 | 12 | 1,029 |

**How the four share the frame** (40,000 cycles; double every figure for
the page):

- the frame's fixed part: the cockpit and the flip 5.2 ms, the stars 1.3,
  the simulation and the plan ~2: about 9 of the 33.3 ms;
- a big ship drawn exactly: ~7.8 ms at 1.2-1.6 times full size (~900 far
  bolts' worth) — the exact path's own budget, `g_onfly_px`, sets it;
- a far ship from the sets: ~0.6 ms (~70 far bolts), most of it not pixels:
  `lf_place` costs 120 µs in C and every ship's draw repeats it in each of
  the 3 bands the world is drawn in;
- a near bolt: ~0.04 ms; a far bolt: ~0.009 ms (~115 a millisecond).

So frame ≈ 9 + 7.8 B + 0.6 F + 0.04 n + 0.009 N ms. The bolts are not where
the frame goes: in the user's scene the ships take three quarters of it, and
in the page as it was (20,000 cycles) that scene ran at ~15 fps before a
single bolt.

**The page's speed** (2026-10-21). The page was slower than the native tests
for two reasons, both fixed: it asked DOSBox for 20,000 cycles where the
tests run 40,000, and its emulator delivered only ~41,000 cycles a
millisecond on a 2016 desktop core where the native one does ~123,000 (a
Chrome profile: 75% in the interpreter, which Emscripten's Asyncify
instrumented because the multiplayer hooks could wait for the network from
inside it). The hooks now run from the main loop in the browser and the core
is left uninstrumented (docs/wcnet-multiplayer.md in dosbox3d): ~70,000
cycles a millisecond. RV.EXE halts (`rv_halt`, `sti; hlt`) while it waits for
its next tick in the rooms' loop and at the flight's 30 fps cap instead of
spinning, and DOSBox drops the rest of its time slice at a `hlt`, so asking
for more cycles costs a machine nothing while the game waits (a third less
host time in a 20 s flight at 50,000). The page now asks for 50,000
(web/gamefiles.js): the scene above holds 30 fps with ~1,100 far bolts. A
machine that cannot keep up shows it in the page's performance line
("emulator at N% of real time"), which works for RetroVega now: the layer
counts a flight frame at each `rvFrameDone` (`WCNET_PERF=<s>` natively).
The levers, in order: place each ship once a frame instead of once a band
(and `lf_place` in asm, as the stars' placing went: ~10x), the exact path's
budget or its 2 x 1 detail for the big ones, then the bolts.

**Memory.** Conventional memory, not time, was the first wall: the flight
had about 27 KB to spare (the band buffer and the exact draw's two 64 KB
dense buffers), and the bolts' ~30 KB of code and tables pushed the second
dense buffer out (`RV_ASSERT` in ships.c). `rv_alloc_high` (util.c) asks DOS
for upper memory (DOSBox has 128 KB of UMBs at D000 with EMS off, as both
configs run it; 64 KB with EMS on) and `ships.c` puts the dense buffers
there first, else in conventional memory as before: the flight then has
~155 KB left, and the bolts' pool takes 39 bytes a bolt of it (2,400).

**A Watcom pitfall** found on the way: `-ox` compiled
`if (s == 0) A; else { B; C; }` in `ship_physics` so that slot 0 ran the
else arm (the player spun); `wdis` showed the missing test. When C does the
impossible, read the disassembly.

### Pre-rendering: what the numbers allow (the earlier plan, superseded above)

A mission has 2-4 capital ship types and 4-6 fighter types, known at the
briefing. Decoded, a fighter's 62 views (the 25 mirrored headings made at
load, so nothing is flipped when drawn) take 230-260 KB of XMS; a capital
ship's 37 views at 320-640 pixels 1.5-3 MB. The mission level has about
21 MB.

- **Scales.** A ladder of 1, 3/4, 1/2, 3/8, 1/4, 3/16, 1/8 adds up to
  2.07 times the full-size set: 0.5 MB a fighter type, 3-6 MB a capital
  type, 16 MB for a full mission. That fits. Scaling a view costs about 8
  cycles an output pixel (nearest neighbour through a column table), so a
  fighter type's whole ladder is 0.1 s and the six types under a second;
  a capital type's ladder is 5-6 s, so capitals get their small steps
  pre-rendered during the briefing and their big ones on demand (a
  capital ship is slow and seen from one side for a long time).
- **Rolls.** Sixteen roll steps multiply the ladder by 16: 50 MB a
  fighter. Not pre-rendered. A roll is rendered when first needed into a
  cache keyed (type, view, scale, roll) with the mission level's spare
  memory (some 1,000 fighter-sized entries), at most one or two renders
  a frame, the nearest cached roll drawn until the exact one is ready.
  Rotating at blit time instead would cost about 10 cycles a pixel: at
  20 M pixels a second that is a third of the frame for five sprites.
- **During the briefing.** Each briefing frame has a few milliseconds
  spare, enough for one scaled view: the fighters' ladders are done
  inside the briefing's first seconds, the capitals' small steps in the
  rest. The loading screen takes what is left.
- **Explosions, bolts, debris:** pre-rendered frames, under 1 MB.

The views of a type stay run-length rows (gfx.c's format) at every scale,
so drawing any of them is the same `rv_blit_row` the benchmark measured.
Measured next: the scaler's real cost, and whether the far scales shimmer
enough to want a half-size source set (spec §8).

## Fonts, pointer, driver

- **Fonts:** rasterised by `tools/rvpack.py` into our own bitmap format,
  2-bit coverage drawn through the palette's mix tables: Orbitron (SIL OFL)
  at 13 px for text and 22 px for titles, always in memory; the tally
  board's chalk hand, Caveat (SIL OFL) at 30 px, roughened a little and
  packed four pixels to a byte, in memory only while the board shows.
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
9. The flight loop at its sites, the cockpits, the drawing benchmark.
   **Done** ("Flight" above); the sets, the hybrid and the stream too.
   Next combat and the 20 fps pacing. The user's rule for the guns
   (2026-10-20): "it's necessary to lead the target properly with
   lasers" — bolts have a finite speed and a hit is the bolt meeting the
   target; no hitscan, no auto-aim, so a pilot must judge the lead from
   the target's heading, which is why the far ships' roll had to be right.
   The bolts' drawing is done ("Laser bolts" above: lines far, nested
   capsules near, the `/lasers` benchmark); their flight in the game, the
   hits and the guns are next.

Steps 1-6 are done too: `tests/run.sh` plays `tests/walk.txt` (every room,
a save, a close-up, the briefing, the deck, a launch and its debrief),
`tests/campaign.txt` (a whole campaign with `/flight 1`: `/winrate 20`
ends in a loss, 80 in a victory), `tests/keys.txt` (the arrow keys) and
`tests/flight.txt` (the flight benchmark, 20 seconds) headless in DOSBox,
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
