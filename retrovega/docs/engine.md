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
| Big memory | **XMS, all there is (at least 32 MB)** through the XMS 3.0 driver (functions 88h/89h query and allocate-any, 0Bh move) | DOSBox's own XMS; `memsize=63`, its most, leaves 62 MB (since 2026-10-22: the user's worry about room; it was 40 and a fixed 32 MB arena). EMS is no longer needed. |
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

DGROUP is full: the fixed block, ~10.5 KB of constant strings, ~12 KB of BSS
and the 8 KB stack leave a few hundred bytes, and the C runtime's near heap
(its FILE structures) lives in what is left. With under ~200 bytes free
RV.EXE cannot open its files ("RV.DAT or RVGAME.DAT is missing", no
RVLOG.TXT) or prints "Not enough memory to allocate file structures". New
tables go `__far` (the people's labels moved out on 2026-10-21: 624 bytes
free since); `grep ^DGROUP build/dos/RV.MAP` gives its size.

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
- **Touch:** every key of the menu has a button that does the same (SAVE,
  LOAD, QUIT, BACK or CLOSE; SAVE and CANCEL at the name), a close box at
  the top right, and a click outside the panel closes it: a phone, with no
  keys, works it all (the user's report, 2026-10-26: stuck in it on a
  phone). `tests/bunk-touch.txt` works it by clicks alone.
- **Touch, below the game:** the emulator puts the pointer where a finger
  is and lets a tap click (src/gui/sdlmain.cpp: SDL's mouse events made of
  touches, absolute, never a request for Pointer Lock, which phones lack;
  in full screen the finger is measured over the letterboxed picture), and
  input.c counts the presses between two polls (int 33h function 5), as a
  tap can be down and up again between them. `scripts/web-rv-phone.mjs`
  walks the base on an emulated phone by taps alone.
- **The touch controls** (web/touch.js, on a screen with touch while a game
  runs): **Keys** raises the device's own keyboard (a hidden field: what it
  types goes to the game as key presses through the emulator's queue,
  wc_web_key; a phone keyboard's composing and correcting is followed by the
  field's value, as backspaces and letters), **Esc** is the game's Esc, and
  **Fire** holds Space. A row under the picture, or its corners in full
  screen (the page puts the canvas's wrapper, #screen, in full screen so they
  come along). For RetroVega the layer had never taken the page's keys at
  all (its hooks have no periodic call): the page's input now asks for the
  hook check, which drains the keys and buttons (wcnet_hooks.cpp,
  web_keys_and_buttons). The phone test types a bunk's name with Keys.
- **Files:** a bunk's save is `BUNK01.SAV` .. `BUNK08.SAV`; the player
  names it (24 characters), or leaves it empty for "Bunk N" (the name
  prompt says so: a phone cannot type).
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
| base | the archive's palettes, fonts and small tables (134 KB) | on a base change |
| room | the room's pictures and sprites, read from the archive as drawn | on leaving the room, and at a launch |
| mission | the mission's ships; the cockpit, faces and pictures a flight draws | when the mission ends |
| hazard | an asteroid field's rocks (`ROCKS.DAT`), and the archive's assets read meanwhile (`dat_level`) | when the player is away from the field, and when the mission ends |

Music is resident and keeps playing across every load.

Built so far:

- **The base archive:** base 0 is `RV.DAT`, base n is `RVBASEn.DAT`, with
  the same room records and its own pictures. The save keeps the base.
  Since 2026-10-22 it is not read whole into XMS (it was 11 MB of a 32 MB
  arena: every room's pictures and all eight cockpits, held through every
  flight): `dat_open` reads its directory and tables, and the palettes,
  fonts and tables (134 KB, the base level); any other asset is read from
  the file the first time `dat_read` wants it, into XMS at the level of the
  moment (`dat_enter`: `scene_enter` gives the last room's back and takes
  the room level; a launch gives the room's back and takes the mission's),
  small ones carved from 64 KB slabs, and kept until that level is given
  back (`xms_freed`, a hook in xms.c, clears them). DOSBox serves the reads
  at host speed. The walk test reads 133 assets (3.3 MB) over its rooms;
  its screens are the old build's but for animation phase.
- **Changing base:** loading a save from another base unloads the archive
  (`dat_close`: its XMS level, tables and fonts), loads the other, and
  calls `baseChanged`. `tests/base.txt` checks it with a copy of the
  carrier as base 1.
- **The mission level:** a launch takes the mission level and the end
  of the flight gives it back. With the archive read as wanted and 62 MB
  of XMS, 58 MB are free for it (17 before): the flames scene's seven ship
  types take 26 MB, every roll block in, 31 MB to spare.

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

**Trading memory for speed in flight.** With 32 MB of XMS (62 since 2026-10-22) and moves that
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
roll. It is made at launch into `victoryPoints` (nonzero won), the word
the layer reads and carries to the other players as WC1's mission score,
so the way home's music knows it (music.md); `missionVictoryCalc` at the
end is the layer's moment to put the host's in, and the debrief follows it.

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
`logic_frame()`, then the picture, then a wait to 30 frames a second.

`logic_frame()` is the game's logic, in `src/logic.c`, written by hand.
Since 2026-10-26 its main loop and its fire loop are empty steps for the
user to fill; the first flight's steering, stand-in AI and damage model
are in git (`git show 65bc2a7b:retrovega/src/flight.c`). Its helpers are:

- `world.h`: vectors, a slot's ship, spawning, the gameplay random stream;
- `shots.h`: the gunfire, kept as a list outside the slot table, with a stand-in bolt slot for `do_damage`;
- `hud.h`: marks over the view;
- the weapons and loadouts, now in RVGAME.DAT v4;
- the trigger, in `g_pilot`.

[logic.md](logic.md) is its guide: the order of a step, the helpers, the
rules the multiplayer layer needs kept, the budgets, and what the engine
still lacks.

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
  The throttle slides up to 14 pixels with `setSpeed`. The joystick is
  the pilot's hand on it (the user's renders, arm2.zip, 2026-10-23;
  `tools/joysticks.py` into `art/cockpits/KEY/stick/`): nine poses,
  pushed forward (nose down), upright, pulled back (nose up: the pointer
  below the middle), each leaning left, upright or right, picked by the
  steering (the arrows at full rate, else the pointer's distance from the
  middle of the opening). Each set has a right arm whose sleeve the user
  cut off with a horizontal line, and the same poses bare, at the
  cockpit's scale and place (exactly the kit's old stick: its size, its
  mount). Each handed pose is scaled and set by its bare one (their bases
  matched by overlap), so the stick stands where and as big as the
  cockpit's was; the whole set is lowered only as far as puts every
  pose's cut edge off the bottom of the screen, the arm never drawn in
  (the Redeemer 25 pixels, the Progeny 21, the Franklin 18, the Pacifier
  9, the Goddard 3, the rest not at all; the base then cropped by the
  screen's edge). The kit's socket is painted out of the base (its hole
  and collar filled with the plate round them, speckled like it): the new
  stick has its own base. Then (the user's, 2026-10-24) the hands read far
  too small beside the legs: an arm is not much thinner than a leg, and
  nearer. So each set except the Franklin's (its side stick at the
  screen's edge; the user named the other seven) is grown about its fist,
  where the kit's grip was, until the fist is 0.9 of a knee's width
  (data/legs.json): 1.8 to 2.4 times. It is then lowered (nearer, as it
  is) until its upright pose hides none of the displays or the panel's
  controls, by at most 35 pixels: the Schroedinger 32, the Goddard 30,
  the Lancelot 21, the Gawain 15. Where that is not enough, a display
  sits right above the stick, and the set goes only as far as hides no
  more than 35 would (`joysticks.py` names what is left; stick.json
  `hides`). For two of those the panel moved instead (the user's pick,
  data/legs.json `moves`): the Redeemer's SHIELD, ARMOR and HULL bars
  swap places with the grille left of them, and the Progeny's E/M box
  (the EMCON knob and lamps) with its EJECT box, which now sits behind
  the hand. The Pacifier's SYSTEMS display stays partly behind its stick
  head (the user's: as it is).
- **The pilot's legs** (the user's, 2026-10-24: the footpads either side
  of the stick read as knees with nothing after them, as if the legs were
  curled back; in the Goddard and the Redeemer, legs against the
  panel). `tools/legs.py` paints them into the base before the panel's
  controls are made (data/legs.json): each footwell goes dark, black
  from just under the dash and lighter towards the bottom edge, and a leg
  in the sleeve's olive and creases comes up from below the screen. Each
  leg's knee rounds off into the dark and its shin carries on under the
  dash, so no shoes are drawn and most of each footpad is covered. Where
  only a slot between two boxes is free (the Gawain's right vent), the
  leg is glimpsed there in their shade. The Gawain's and the Lancelot's
  EJECT boxes are gone (the user's, 2026-10-24: they blocked the legs),
  their left legs in their place. The Schroedinger is lopsided: its right
  leg is in the hollow under the radar and its left takes the EJECT box's
  place (the user's: the wing may block an ejection anyway, and a one-legged
  pilot looked wrong; Ctrl+E still ejects). The Pacifier's lower-left
  panel is cut away below its lamp row, which stays as the dash's lip, its
  afterburner and target lamps with it. The Lancelot's COMM FREQ and E/M panels lose their
  inner ends (the user's pick): its NAV readout moves out over E/M's and
  its legs take their old place. The 1-knob goes, so its
  radio shows the hundreds and tens only, and so do the EMCON lamps (K
  still works). A control, a label or a damage overlay inside a moved box
  moves with it; one left under the legs is dropped, an overlay's pixels
  there cleared.
- **Damage:** every overlay is a sprite with a fixed bit
  (`tools/cockpits.py` BITS: 0 weapons MFD, 1 targeting MFD, 2 radar, ...
  23 canopy, 26 survival VDU), drawn while its bit of `rvCockpitDamage`
  is set. The kits' broken throttle and broken stick are not packed (the
  user: they are unlikely to break; those systems' damage shows on the
  panel's lamps).
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

### The panel: every control does something

The user's ruling (2026-10-22): every dial, display and light of each
cockpit is hooked to a game function, or blanked; the power-distribution
knobs, which the game has no use for, repainted as a radio's frequency
dials that turn to whoever is talking (wing, the carrier for landing, an
enemy). The table is `data/panels.json`: for each cockpit, each control's
place, kind and function (213 in all; the functions and their meanings at
its head). Unlabelled, unlit buttons and knobs stay as art: with no light
or needle there is nothing for them to show.

**The art** (`tools/panels.py`, made in the build into build/panels). From
the kit's base: a lamp's other state (a lit one darkened, a dark one lit in
its colour, a plain button given a glow), a knob turned to each of its
positions (rendered 4x, rotated, back down), the throttle's ticks lit to
each level, the frequency knobs' scales recoloured one green (dim, and a
lit state), the gauges' painted needles painted out (the engine draws
moving ones), lettering painted out and relettered (Roboto Condensed Bold,
the kits' light-with-shadow look, at the panel's slant): POWER DISTRIB /
ENG / SHD / WPN become COMM FREQ / 100 / 10 / 1 on the Schroedinger,
Gawain and Lancelot, the Franklin's POWER buttons CHAN, the Redeemer's
PWR / ENG knobs CHAN / VOL. Every other lit lamp of the cockpit (found by
colour, away from the controls and the glass) is one more state: the
running lights off. A state keeps only the pixels it changes; `rvpack.py`
packs each as a sprite and a table of `RvPanelControl` records a cockpit,
the cockpit's palette taking the states' colours in.

**The survival display, heat and signature** (the user's design,
2026-10-23). `tools/panels.py` draws each cockpit's survival display as a
map, a byte a pixel: the front and rear shields two rounded trapezoids, the
armour four trapezoids inside them (a box's frame cut along its diagonals,
one a side), the ship's silhouette in the middle (its sprites' top view,
art/ships, so the wings are there), a pixel's gap between them; a wide
display lies sideways, nose to the right. Each shield or armour pixel
carries its rank, the order it goes in as that side weakens: the shields
ring by ring from the outside in, like a tree's, the armour straight
inward (front to back, left to right...). The engine lights a region's
pixels whose rank is under its strength (shields light blue, armour
green) and blacks the rest: a 256-entry colour table made when the
reading changes, then a lookup a pixel. The HEAT display is the ship's
silhouette in thermal colours (navy through the reds to white), its
engines hottest (data/engines.csv), as hot as the ship runs; SIGNATURE a
segmented bar; both take their value every 5 s. Fuel is a backlit amber
bar that draws shorter as it goes, never a number.

**The engine** (`src/panel.c`). Each frame every lamp's, knob's and
switch's state comes from the ship's systems (`ShipSystems`, kept by
flight.c); one that changed marks its rows, and the band's redraw paints
its sprite over the restored base (the base shows the kit's own state).
The displays (needles, bars, readouts, maps) take turns: each frame one
takes a fresh reading (what it shows: a few numbers or two short lines),
and only a changed reading marks its rows, so a display lags the ship by
a beat, as a scanning instrument would (the user's). A display is drawn
from its reading alone, so when other things wipe its rows (the base is
restored by the band) it is drawn again unchanged without being worked
out again. Needles (speed dials, the Progeny's two-hand mission clock),
bars (shields, armour, hull, fuel, power plant, engine, speed, target
range) and readouts (the nav point, the target: the body font when it
fits the glass, else a 3 x 5 pixel font, cut to the box) are drawn. A
click below the canopy works the control under the pointer.

**The systems.** EMCON (K): ACTIVE (the radar emitting: every contact
identified), PASSIVE (the scope unidentified), OFF (a dark scope, the
running lights off, the smallest signature). The sensor mode (V): the
lock's seeker, radar (needs EMCON ACTIVE and the IFF) or infrared (needs a
hot target: its engines toward us, or burning). IFF (I): off, every
contact one colour. Master arm (M), decoys (X), hold the lock (L), the
radar range (G: 4,000 / 8,000 / 16,000 km), the survival display's page
(H: shields and armour, hull and damage, the nav point), the nav point
(N), the target (T next, Y the nearest enemy; the nearest at launch;
AUTO keeps it so), comms muted (C: no comm faces, no hails), the
afterburner (Tab held: the ship's afterburner speed, fuel for about 11 s,
heat), eject (Ctrl+E twice within two seconds, or the handle: the mission
ends ejected). The LOCK lamp is the target held in an 11-degree cone
within 6,000 km by a seeker the switches allow; MASTER WARN blinks for a
damaged system or a shield under a quarter. The radio tunes itself to
whoever starts talking (WING 267 MHz for a pilot, GUARD 243 for an
alien's hail: the real military distress frequency; CARRIER 315 for the
carrier), its digit knobs stepping there a notch every third frame. The
switches are flags combat will read: the lock rules already do.

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
Since 2026-10-23 the benchmark is in the test build only (below).

At `cycles=40000` (tests/rv.conf) that is about two emulated cycles a
pixel, the XMS fetch of each sprite's rows and the per-row call into
`rv_blit_row` (lowlevel.asm: a span is one `rep movsd`) included, with
the rest of the frame (the restore, the instruments, the flip, the
simulation) already paid for. The browser's row was measured by driving
the live page through the rooms with Tab and Enter (the deck's launch
hotspot sits at 478,176) and reading RVLOG.TXT; a desktop browser on a
faster machine will sit between the two. The number is live in
`rvBenchCount` while the flight runs.

### The ships' views fetched when a flight asks

The page (2026-10-23, the user: "lets do all these things. The 6 degrees is
where it is at!"). At 6 degrees a fighter's views are 4.4 MB and the
eighteen some 80 MB, too much to fetch before the game starts as the 29 MB
at 10 degrees were, so a flight fetches its own. The engine's side is
`ships_want` (ships.c): given the flight's types it looks for each one's
set file beside RV.EXE (SHIPnn.RVH, or .RVS under /res 1) and, when any is
missing, writes the whole list to RVWANT.TXT as `want GEN SHIP02.RVH
SHIP08.RVH ...` (GEN the clock, so the page acts on a new line once) and
either returns (`start_briefing` in game.c: the mission's types, the
player's for the player and wingman entries, 08 when no enemy is listed,
so the fetching runs while the briefing is read) or waits (`flight_startup`,
before the loads: every quarter second a look for the files, `rv_halt`
between, three minutes at most, then on without what is missing, which
`ships_load` logs as "no set for type"). On DOS the files are beside RV.EXE
and nothing is written; the native tests pass with every set in place and
with one held back until the file appears (a shell loop playing the page:
"the sets came in 500 ms").

The page's side (web/wc.js, "RetroVega's ship sets"): build-web.sh leaves
SHIPnn.RV out of retrovega.tar.gz (6.5 MB now, from 25.5) and puts each
beside it gzipped (rv/SHIPnn.RV.gz, a third smaller) with rv/ships.json
(the build, and each ship's name, size and SHA-1). While RetroVega runs the
page reads RVWANT.TXT every 300 ms; a new generation is served: sets no
longer listed are removed from the game's files, and each listed one that
is missing is taken from the browser's Cache API (`rv-sets-BUILD`, keyed by
the views' hash and the recipe), else made from the views (the `rv-art`
cache by hash, else fetched and gunzipped through DecompressionStream) in a
worker (web/rvsprites-worker.js, so the emulator and the music run on;
1.3-1.9 s a 10-degree fighter in headless Chrome) and kept in both caches,
then written with `FS.writeFile`; DOSBox opens a file created after the
mount by its name (the directory cache expands an unknown name unchanged).
The status line says what is being fetched or made. Without the Cache API
(an http page other than localhost, a private window) it all still works,
fetched each time. scripts/web-rv-flight.mjs flies it: the briefing asked
for two sets, both were made in 3.8 s, and the launch found them in place.

### The test build: RVT.EXE

The demos, the benchmarks and the self-tests (`/bench`, `/record`, `/zoom`,
`/capital`, `/sky`, `/midilog`, `/scoretest`, `/ship`, `/lasers` and its
scene's knobs, `/boltdemo`, `/flames`, `/flametest`, `/balls`, `/balldemo`,
`/missiledemo`, `/skydemo`, `/boxes`, `/collidedemo`, `/hulldemo`,
`/commdemo`; the kernel check and the path timings every flight began with)
sit under `#ifdef RV_TESTS`: regions of flight.c (the whole demo and
benchmark half of it), the checks and timings at the ends of bolts.c,
balls.c, ships.c and comms.c, and their switches in main.c. build.sh finds
the modules that mention it, compiles them again with `-dRV_TESTS` into
build/dos/t/, and links **RVT.EXE** from those; **RV.EXE**, the game's and
the page's, goes without: 56 KB smaller on disk (304 KB from 360), 48 KB
more conventional memory in the flight. The switches themselves stay
variables in both builds (never set in the game's), so the flight's code
reads the same either way and a dead branch costs a compare, not a warning.
`tests/run.sh` runs RV.EXE unless a test switch is given (anything but
`/script`, `/flight`, `/winrate`, `/res`, `/onfly`, `/skyrows`, `/mpu`,
`/nomusic`, `/noquit`), then RVT.EXE; `EXE=` picks one regardless. The log's
first line says `RVT.EXE: the test build` when it is. The game's flight log
ends `flight: over with status N after F frames`; the test build's adds
the benchmark as before. Not gated, since each is a few hundred bytes: the
music log (`/midilog`'s recorder in music.c), score.c's clock test, the
flames scene's spawning and the carrier's.

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
  the recipe `0 4 9`), at roll steps of 11.25 degrees (22.5 until
  2026-10-22, when the user spent the new XMS on finer turning in the mid
  field: +1.6 MB a fighter type, 85 blocks a set), all in the engine's
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
- **Nine stored turns give 32** (five gave sixteen before). An upside-down copy (the rows in the
  other order, the same cost as a copy) turns a sprite by 180 − θ and
  mirrors its heading; a mirrored copy (`rv_blit_row_rev`, four pixels a
  `bswap`, 0.7 of a copy) turns it by −θ and mirrors the heading; both
  together turn it by 180. So a symmetric ship stores only the arc 0..90
  for every view and the flight draws the other 23 steps as those
  copies of the stored nine, of the view or of the opposite heading's
  (`find_sprite`); the top and bottom views, their own mirror, have their
  own table; the asymmetric Convolution stores eight 45-degree steps.
- **Loading** (`ships_load_blocks`): the dense copies' cache (eight 64 KB
  copies, below), then the ladder for every type, then each type's
  full-size block (the exact draw's views: 0.2 to 1.5 MB a fighter), then
  the rolls size by size (a size's rolls lie together in the file and
  come in as one XMS block, each at its offset: a fighter takes 22 of the
  arena's 512 blocks, not 85; `ShipSet.rolls_in` marks the sizes whose
  rolls are all in, for `turn_wanted`, which had looked for roll steps the
  asymmetric Convolution never stores and drew it exactly at every size),
  while the mission budget lasts (1 MB of XMS kept
  back); a draw whose block is missing takes the unturned sprite at that
  size, and the plan draws such a ship exactly (below). Nothing is read
  from the disk in flight.
- **The hybrid** (flight.c `plan_draws`, once a frame): the ships on
  screen are sorted back to front; the biggest, while their pixels fit a
  budget (`g_onfly_px`, 32,000 a frame; `RV.EXE /onfly N`) and they are
  above 40% size, are drawn **exactly** — the full-size view turned by the
  true roll and scaled to the true size by inverse mapping from a dense
  copy (`ships_draw_exact`, two dense buffers kept between frames); the
  rest are copies from the sets, where 11.25-degree and 16% steps are
  invisible. A ship too big for what is left of the budget is drawn at
  half or quarter detail instead (every other row computed and the row
  above copied by `rep movsd`; then 2 × 1 blocks, `rv_rotate_row2`): the
  user's rule that a ship in your face gets blockier, never slower. And
  every other ship the sets cannot show turned (no roll steps at its size:
  the four biggest, nearer than ~4,100 km; or rolls that did not fit) is
  drawn exactly too, at half height (below, "No stream").

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

**No stream; every near ship turned** (2026-10-21). The engine flames'
self-test (`/flametest`, "Engine flames") showed that the sets hold no
turned sprites at their four biggest sizes, so every near ship but the two
drawn exactly showed unturned (the user: "unrolled local fighters ...
could look really strange"). The user's design: the ships beyond the
first two drawn exactly as well, at half height, each dropped row the one
above copied down; and with that, no stream: every full-size view
resident (the 15-degree lattice), the disk kept for comm images and the
like. So `ships_load_blocks` loads block 0 (after the ladder, before the
rolls; `load_full` points the views at it, `loaded` stays 2 so the sets'
lookup never takes its dense-lattice views), and the slots, reads,
touches, prefetches and the next-view fallback are gone. The plan marks
its first two exact as before (stable, `EXACT_HOLD`), then every ship
`ships_turn_wanted` at detail 2 (4 once the budget is spent). More than two
turned ships thrashed the two 64 KB dense buffers (25.6 ms with three):
`dense_from` now keeps the last eight dense copies in XMS too
(`g_dc`, mission level), and a ship whose copy was taken gets it back by
one XMS move (host speed in DOSBox) instead of a re-expansion (the flames
scene: 471 expansions and 10,258 moves in 20 s). A dropped row's copy is a
`rep movsd` (DOSBox charges a cycle per four bytes; an XMS move, a few
dozen instructions whatever its length, was tried for the wide spans and
gained nothing once the row was one call). **Exact edges** (the user's next
idea: the copied rows made stair steps of the outline): the ship taken as
convex, a left and a right edge a row, a jagged inside fine. A row drawn
in full also has its edges found (`rv_turn_drawn`, lowlevel.asm: the
kernel's walk, reading only, in from both ends of the span, then the
kernel between them, so the walk costs no more than the kernel did over
the margins); a dropped row's edges are probed from the row above's (a
step or two each, outward while the next pixel is ship, else inward to
the first that is), the part the row above covers too is copied, and the
rest, out to its own edges, sampled (`rv_turn_dropped`): one far call a
row. Costs at 40,000 cycles in the flames scene (six ships 3,000-3,500
km, all near: two at full detail, four at half height): exact draws 17.6
ms with the plain (jagged) row copy, 19.5 with walked edges in C, 18.6
with probed ones and four calls a row, 17.8 with one asm call a row; the
whole frame 31.8 ms (593 frames in 20 s, the 30 fps cap held).

**The stream** (2026-10-20 to 21, since removed; the user's design then: "it doesn't impact the
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
2.5 → 3.4 MB, 38 s to render. The engine kept `SHIP_VIEWS` (330) records
and resolves a symmetric ship's mirrored headings by index
(`mirrored_index`: past `2 + nb * nh`) rather than by reading a record, so
their indices may run past the table (since 2026-10-23 the records are
read from the set's index block in XMS, `full_view`, and there is no cap:
an asymmetric set's 614 would fit). In the zoom scene the target
switched view 137 times against 92 at 15 degrees (each a smaller step);
whether the pop still reads was put to the user as a blind pair of
videos. Recipe: `render_all.py --only GAWAIN --step 10 --out DIR`,
`pack.py --sprites DIR --out OUT` (it skips ships not rendered there),
`build/host/rvsprites OUT/SHIP02.RV OUT/SHIP02.RVS 0 4 3`, the two files
copied over build/dos's after `build.sh`.

**A 6-degree lattice** (tried 2026-10-23 on the Gawain, after the memory
work; the user: "maybe the magic is 6 degrees ... it also divides 360"):
`lattice(6)` is 29 bands x 31 headings, 901 views (1,742 with the mirrored
headings), 2.77 x the 10-degree lattice; the 30-degree sub-lattice nests
in it every fifth. The tools take it (pack.py knows 901 / 1742, rvsprites.c
MAXV 1750, `render_all.py --step 6`) and the engine needed nothing: the
view records live in XMS and the mirrored headings resolve by index (the
zoom scene drew views 441, 623, 1417). The Gawain: 104 s to render (1,805
files), its .RV 4.42 MB (1.58 at 10 degrees), its block set .RVH 5.82 MB
(2.09), 5.7 MB of XMS when loaded — so six fighters at 6 degrees and three
capitals at 10 (4 x 4 blocks) fit the ~58 MB a mission has. The cost is
the page's: the views would be some 80 MB where the 19 ships' were 29, and
makeSet takes 1.6 s a fighter in node (0.95 s for the 10-degree sharp
set), so the switch waited on fetching a flight's ship files when it is
flown, with a cache (the next section), rather than everything at the
start; and the renders are committed art (the build box has no meshes),
2.8 x the repository growth a re-render.

**The eighteen at 6 degrees** (the user, later that day: "The 6 degrees is
where it is at!"): every symmetric fighter re-rendered with `--step 6`
(39 s for the Gaozong to 327 s for the Pacifier, 46 minutes in all at low
priority beside other work), the Convolution left at 15 (asymmetric, 266
views). art/ships is 80.6 MB (1.2-8.4 MB a ship; 50.8 MB gzipped
for the page), build.sh makes the 38 sets in 42 s (the block sets 110 MB,
the sharp ones 163 MB), the page's twin makes the same bytes (the check),
and the first mission's flight loads its two types in 9.3 MB of XMS (3.3 at
10 degrees) with 48 MB left. The exact draw costs the same per frame (4.6 ms
either way for the test flight's two ships) though a turning ship changes
view more often: 305 dense copies expanded into the slab against 190.
Capitals, when there are any, stay at 10 degrees.

**The sky** (2026-10-20, the user's ask: "a generic function to blit a
sprite at infinity at a vector ... so it will rotate with the player; that
way we can make a starfield if we want"). `lf_sky(dir, up, cam, cx, cy,
focal, &place)` (lightfield.c, unit-tested on the host) puts a direction
of the sky — any length, the world frame — through the camera exactly as
`lf_place` puts a ship: where it shows, and the turn on screen of a sprite
painted there whose up in the world is `up` (the world's own for most), so
it turns with the sky as the player rolls. `ships_draw_sky(set, v, dir,
up, cam, scale, ...)` paints a set's full-size view there through the
exact path (turned by the sky's roll, scaled). The stars themselves are now 2,000
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
both). Test scene: `RV.EXE /sky`
with tests/sky.txt (three Gawain views painted ahead, high left and low
right; the player rolls, yaws, pitches).

**The block mode, `RV.EXE /res 2` — the game's mode since 2026-10-23**
(the user, on the recordings: "Left is best — no blend. I think here is
where we need to be"; `/res 1`, `?res=1`, keeps the sharp sets for
comparison). **A dithered blend at cell crossings was tried and
rejected**: the view across the nearest boundary dithered in over the
current one in 2 x 2 blocks (a quarter, then half of them, the same
pattern either side of the switch so it was a crossfade); on the zoom
scene at 50 % of the cell it kept the ship speckled most of the time, at
75 % in the last quarter of each cell, and the user preferred the plain
switch to either. (The design after
the pops: "reduce zoomed-in sprite size by 4x (2 in each axis) and draw
blotches ... does that buy us enough memory to store the dense map without
a cache and with maximal predictability?"). Two modes from the same
10-degree views, a runtime switch, the page's `?res=2`:
- `/res 1`, the sets as they are: SHIPnn.RVS, the hybrid above (the near
  field turned exactly from the full-size block at 10 degrees, the mid
  field copied from the 30-degree sub-lattice at 11.25-degree roll steps),
  ~5.3 MB a fighter type.
- `/res 2`, SHIPnn.RVH (`rvsprites IN OUT 0 0 0 1 2`: lattice 1 = every
  view of the ship's lattice, factor 2 = the ladder from half size, the
  mirrored headings not stored): the ladder only, 13 sizes of the lattice's
  own views, **~2 MB a fighter type at 10 degrees**; no roll blocks at all.
  Every ship is drawn exactly (`plan_draws` marks them all; `turn_wanted`
  is true throughout, the sets having no turns): `ships_draw_exact` picks
  the ladder's smallest sprite still at least the ship's size over the
  factor, and `draw_turned_at` turns and scales it from a dense copy keyed
  by size and view (`dense_from`, key size x 1024 + view) as 2 x 2 blocks
  (detail 4: 2 x 1 blocks on the drawn rows, the rows between copied). So
  every ship rolls continuously and the only remaining steps are the
  lattice's 10 degrees; the ships are 320 x 200 pixels under a 640 x 400
  cockpit, which is WC1's pixel size. Cost: ~1.3 ms a big ship, 4 ms for
  one filling the opening (the zoom scene's frame-time line: "ships drawn
  exactly 4.25"); the sets load in 100 ms. Capital ships take factor 4
  (`0 0 0 1 4`, the ladder from a quarter, drawn as 4 x 4 blocks: detail
  8, a C row kernel copying three rows of four) — the user: "it's ok if
  they are pretty blocky way up close if it gives us space to store those
  10 degree views"; ~2 MB a capital at 10 degrees, no turns stored. The
  factor is in the header (byte 9, bits 1-2), `ShipSet.factor`; a set's
  blocks hold only the lattice's own views, so `set_view` is bounded by
  the block's view count (`SetBlock.nv`) and `find_sprite` maps a mirrored
  heading to its pair with a mirror flag for such sets. The page makes the
  matching sets (`makeSet(rv, 0, 0, 0, 1, 2)`, 0.3-0.7 s a type under
  node) and passes `/res 2`; the check covers both recipes (38 sets). An
  action sphere of two capitals and four fighter types is ~12 MB of the 62.
  The zoom scene at 10 degrees: 110 view switches against 74 at 15, each a
  third smaller; the next remedy is the dithered blend at a crossing.

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
2 km in radius and 24 km long (the user doubled the length on 2026-10-21,
the same width), and the near plane is at 200 km, so the largest is 8 px
across and 64 px long side on (bolts.h).

**Two drawings, chosen by the radius on screen.** Under 3/4 px a bolt is a
*line*: each row it crosses gets the pixels whose centres the segment passes
over in that row (an x-major line's run), and a steep row with none gets the
pixel under the row's middle, so every row shows. Above, it is *fat*: a red
(`UI_RED`) capsule, and from 32 x16 of radius a white (`UI_WHITE`) core
capsule 3/8 as wide painted over it, below that a white core line (until
2026-10-21 there was also a dark red rim, fire ramp 130, as a third colour). Each capsule row is one span between its
left and right boundary: a's arc, the straight stretch, b's arc, met at the
tangent rows (rounded so the stretch is never used past them: on a nearly
level bolt a sixteenth of a row there is pixels of x).

**The pipeline** (src/bolts.c over src/lasers.c and src/lasers.asm):

- once a frame, `rv_bolts_place`: every bolt through the camera (eighteen
  32-bit products, four divisions, the near-plane cut, the screen box), out
  as a line set up for the draw (`LzLine`, 20 bytes) or a fat bolt (`LzFat`);
  then `rv_fat_setup` makes each fat bolt's passes (`LzPass`), or
  `rv_slab_setup` its polygons (`LzPoly`);
- per band, `rv_lines_draw` draws every line, one call for all, and
  `rv_pass_draw` or `rv_poly_draw` each fat bolt, smallest first; every row
  is clipped to the cockpit opening's extents as the ships are;
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
µs at `cycles=40000`; the bolts 24 km long since 2026-10-21, the near ones
drawn as capsules):

| | place | set up | draw | in all |
|---|---|---|---|---|
| a far bolt (a line, 2.2 rows) | 5.6 | — | 2.7 | 8.3 (+0.7 flown and re-fired) |
| a near bolt (fat, 2-4 px radius) | 5.2 | 13.2 | 30.0 | 48.4 |
| a far bolt drawn fat (one algorithm for all) | | | | 25.4 |

So one algorithm for every bolt would cost three times the line for the far
ones: the split pays. The near bolts' set-up was 87 µs in C (Watcom's 64-bit
helpers) before `rv_fat_setup`.

**Capsules, the user's pick** (2026-10-21). The fat bolts are capsules in
two colours; the other drawings tried that day were deleted once the user
had seen them (for a day they were a build-time choice, `NEAR=` in build.sh,
measured below). How we got there, the user's questions in turn:

- *Simpler near bolts?* A bolt as two convex polygons painted over each
  other, a red outline and a white middle half as wide (the "step up then
  down"), set up by `rv_slab_setup` and filled by `rv_poly_draw` (one span a
  row between two chains of edges walked down from the top vertex), up to
  3 px across three lines side by side (red, white, red). Four shapes were
  tried: the user's level/upright ends, the rectangle square to the flight,
  the rectangle with pointed ends, and (below) the swept octagon.
- *Own bolts are seen end on* (`/boltdemo`, below): the rectangle then had
  its ends at the segment's ends and was a sliver; its ends are pushed out
  by the half width now (the same four corners).
- *A square back is a nonstarter: round, at the polygons' speed?* An octagon
  swept along the bolt: the segment's sum with an octagon of flats at the
  half width, each octagon edge going to the end it faces (the sign of an
  addition: (1,0), (1,1), (0,1), ... dotted with b - a), the long sides
  joining where the ends change; no root, no division, at most ten corners,
  end on an octagon, which at a few pixels covers a circle's pixels; under
  1.5 px of radius the outline and a white core line (three lines would be a
  dash end on). It is round, and the slowest: every corner is an edge to set
  up as the chains walk down (~30 instructions), and a near bolt is only a
  few rows, so ten corners cost more than the rows. The capsules' arcs come
  from the root table at a row's cost; their dark rim, a third colour, was
  their extra pass, and in two colours they are as cheap as the rectangle.
- *Do the capsules point where the bolt flies?* Measured on the host (the C
  reference, random bolts of 1.7-4 px radius): the drawn pixels' principal
  axis is within 1° of the bolt's for every shape once a bolt is 5 px long
  (0.9° the capsules and the rectangle, 1.3° the octagon); on 2-5 px bolts
  2.5°, 2.4° and 3.4°, and the white middle's 3.7° (the old three-colour
  capsule's thin core 5.0°), 3.1° and 3.3°. No shape shows the direction
  better than another to speak of.
- *Which uses the least memory under 1 MB?* All of it is conventional
  memory (none of it is XMS): the code, the far buffers, the capsules' root
  table. Sized for the radius the flight draws at most (4 px,
  `LZ_RMAX16`) the table is 1,025 bytes (it was 4,097, for 8 px). Bytes of
  RV.EXE's image (the linker's "Memory size") and of the bolts' modules:

| build | RV.EXE | lasers code (asm and C) | bolts.c code | far data | root table | bolts in all | near bolt, set up + draw |
|---|---|---|---|---|---|---|---|
| `capsule` | 261,856 | 8,608 | 7,579 | 5,370 | 1,025 | 22,582 | 13.2 + 30.0 = 43.2 µs |
| `octagon` | 261,696 | 8,928 | 7,684 | 5,850 | — | 22,462 | 13.3 + 46.9 = 60.2 µs |
| `rect` | 261,776 | 9,274 | 7,662 | 5,594 | — | 22,530 | 7.2 + 37.2 = 44.4 µs |

The three are within 160 bytes of each other: the capsules' code is the
smallest and their table the difference, so counted as shared state (a
square root by table that the AI could use too) the capsules take the
least. The user kept the capsules (RV.EXE 261,824 bytes with them alone);
until 2026-10-21 every shape was in the build at once (280,608 bytes,
3-colour capsules the default).

A pre-rendered sprite a bolt (`rv_blit_row`, as the ships are) is no
faster at these sizes: 31 µs a bolt to copy (the runs' bookkeeping) before
finding and fetching it. In the frame the twelve near bolts take 0.1-0.2 ms
of 33 whatever the drawing, and the far ones are lines in every build.

**The player's own bolts** (`RV.EXE /boltdemo`, the user's question of
2026-10-21: most near bolts a player sees are their own). Eleven bolts fly
straight ahead from the eye's depth: one at the eye, one just right of it (a
10 px gap between the two where they appear), one half a metre left, four
1.5 m left and right and 1 m up and down, and four at those corners. A metre
is taken as 32 km of the flight's units (a fighter's 128 px view is full size
at 2,500 km: 800 km for a 25-30 m hull), so a bolt is 12 cm thick and 75 cm
long and the near plane is 6 m out. They fly 6.75 km a frame, the eye to the
line drawing (1,067 km) in about five seconds; the round is ten seconds,
then the flight lands.
`tests/boltdemo.txt` with `/boltdemo /record 1 /flight 900` saves every
frame (`CYCLES=max`, a few minutes; the motion is per frame, so the
recording is exact however slowly it runs; stop DOSBox when RVLOG.TXT says
the flight is over, as the script then idles). What it shows: own bolts are
seen nearly end on, so they appear at the near plane 0.9 s out as 8 px blobs
(the corners' ±96, ±64 px from the middle), are 4 px across by 2 s, dots by
3 s and lines from 5 s; the shape shows only in that first second or two.

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

**Memory again, 2026-10-23** (the user: "lets do 1-4", trading conventional
memory for XMS). Measured by a startup log that walks DOS's arena chain
below A000 (the DOS largest-block call did not move when a table under a
later block went) and the far heap (`_fheapwalk`, after `_fheapshrink`
gives whole free segments back): free in DOS after the screen and the
fonts, 120 KB before, 216 after, the largest block 163 KB from 115. The
steps: (1) the exact
draw's dense copies in **one 64 KB slab** (`g_slab`, `SLAB_MAX` 32 entries,
ring allocation, an overlapped entry evicted) in upper memory, the two
64 KB buffers and the eight XMS copies (`g_dc`) gone — a copy evicted is
re-expanded from its rows, and the flight test shows 199 expansions to
3,047 hits in 20 s; and video.c's 64,000-byte band buffer in upper memory
too (+65 KB). (2) RV.DAT's 49 KB directory in resident XMS, read a record
at a time through a rotating pair of far buffers (`dir_at`, `dat_asset`),
and the sets' view records no longer copied into `ShipSet` (`full_view`
reads the record from the set's own index block; the .RV fallback writes
a 12-byte record a view): +48 KB. (3) The test build above: +48 KB. (4)
The 32 KB C runtime (stdio) left as is: a last resort. Code is 206 KB in 62
segments (flight 41 KB, the runtime 32, ships 17, lasers 14) and static far
data 85 KB (the stars 28): overlays would have saved ~35 KB for a slower
room change, so the test build was the trade.

**A Watcom pitfall** found on the way: `-ox` compiled
`if (s == 0) A; else { B; C; }` in `ship_physics` so that slot 0 ran the
else arm (the player spun); `wdis` showed the missing test. When C does the
impossible, read the disassembly.

### Engine flames

The user's brief (2026-10-21, with four WC1 screenshots): animating
purplish-blue flames from each engine where Vega Strike puts it, full on
normally, the afterburner's longer (or reddish), pixelated is fine since they
are bright, static first (perhaps a two-frame flicker later), sharing the
laser bolts' capsule code.

**Where the engines are.** `tools/engines.py` (run once; it reads the Vega
Strike meshes as `shipviews.py` does) writes `data/engines.csv`: each ship
type's exhaust points from units.csv's Light column, the largest four, in
the frame its sprites are rendered in: (point - the mesh's centre) / the
view box's radius, +x the ship's left, +z the nose, 1.0 the box's half
(`lod0_px / 2` px at full size), and the light's size. Nine third-party
types have no exhaust in Vega Strike: they get one engine at the middle of
the hull's tail. `rvdata.py` packs them into each `RvShipType` (RVGAME.DAT
version 2: `nengines`, `engine[4][4]`: x, y, z x 127 and the radius of the
glow the sprites carry, `min(0.3 size, 0.09)` of the half box, in 1/1024ths).

**On the sprite.** The flames must sit on the engines the picture shows, and
the picture is a lattice view (30 or 15 degrees apart), turned on screen by
the roll and scaled. So an engine is placed through the drawn sprite's own
frame, not the ship's true orientation: `lf_view_rot` is the tool's
`view_matrix` for view v of a lattice (rows: the picture's x, its y, the way
to the viewer), and `lf_sprite_point` takes a point of the ship through it,
turned clockwise by the roll as `lf_rotate_setup` turns the sprite (output =
R (source - origin), R = [c -s; s c] on a y-down screen), scaled by the
ship's size: the nozzle's offset from the ship's origin on screen, and the
tail's way (the renderer's -z) and whether it points toward the viewer.
`tests/host/lf_test.c` checks eight views at three rolls against numpy's
`view_matrix` (within 1/2 %: the views' angles are 1024ths of a turn, 30
degrees is 85 1/3).

**The flame** (src/flames.c). The first version (2026-10-21) was three
flat capsules (a thin dark tail, a wide blue root, a white core); the user
found it cartoonish ("not nearly as fiery and hot") and asked for the
pictures' full gradient and noise, with a stipple where the palette falls
short. Now a flame is shaded pixel by pixel: `lz_flame_setup` /
`rv_flame_setup` make two numbers linear on the screen, s along the flame
(0 the nozzle, 1 the tip) and e across it (in root radii), and
`lz_flame_draw` / `rv_flame_draw` walk a capsule's rows and spans round it
(the bolts' row loop, `pass_body`, shared) and shade each pair of pixels:
r = |e| (or the round root's distance, max + 3/8 min), the profile's
intensity at (s, r + the noise's jitter) (`tools/flamelut.py`: a teardrop
widest a third of the way along, a white root, a lavender middle, a blue
body, violet edges), plus the noise, through nine stipple levels, each a
pair of palette colours on alternate pixels (the palette has no saturated
violet-blue: (63, 57, 222) is made of (76, 105, 250) and (113, 32, 170);
the pairs were searched against the user's WC1 flames). The afterburner's
look is whiter with magenta-violet edges, and twice as long. The asm is
checked against the C at every laser flight's start, set-up and pixels.

**Baked** (the user's idea: pre-baked noisy frames rather than shading every
frame). Shaded live, six near ships' 18 flames cost 16.6 ms a frame at
40,000 cycles; shading two pixels at once and the set-up in asm brought it
to 9.4. So every flame is baked, once, when the game starts (the user's
wish: not at every launch; `flames_bake` after the fonts): 2 looks x 6
foreshortenings (the tail's length over the root's radius: 0, end on, to 5,
or 10 for the afterburner) x 6 sizes (1 .. 7 px of radius) x the ways (0 ..
90 degrees down-right, the rest mirrored and flipped as the ships' sprites
are; how many below) x 2 frames of noise: 3,114 run-length sprites in the
ships' format, 1,356 KB of XMS (the resident level, reserved before RV.DAT),
1.9 s at 40,000 cycles (each strip of the scratch cleared only over the
flame's own columns: clearing all 640 had cost a second). A frame takes the
nearest, its nozzle at the sprite's origin, and draws it with
`ships_blit_mix`: each row from one of the two frames at random (the user's
idea again: two frames mixed row by row make a new flame every frame). The
size snaps to steps of 1.5. Cost at 40,000 cycles: the six near ships' 18 flames
3.45 ms a frame (1.80 of it the set-up in C: the sprite's frame, two sprite
points and a header from XMS an engine; the blits 1.65), the laser
benchmark's two big ships 0.52, a normal mission ~0.5-0.9.

**Lined up exactly** (the user, 2026-10-21: in play the flames looked bent
left or right; they are a strong clue to a ship's heading, so they must
point exactly along the ship as drawn, by the same means for the ships drawn
exactly and from the sets). Three errors were found, by `/flametest` below:

- *The picture's turn.* The flames took the ship's true roll, but only the
  ships drawn exactly are turned by it. A set's sprite is turned to a
  22.5-degree step (or a coarser one, when that step is not in), and the
  sets hold no turned sprites at all at their four biggest sizes (scale 152
  and up, nearer than ~4,100 km), nor at the far sizes whose roll blocks did
  not fit the mission's XMS: those ships are drawn unturned. Their flames
  pointed up to 140 px from where the picture's tail points. Now
  `ships_drawn` makes the draw's own choices (one `set_pick` shared with
  `ships_draw` and `ships_box`; for an exact ship, whether its view or a
  neighbour's can be turned now, else the sets as the flight falls back) and
  gives the flames the view and turn the picture will have.
- *The ways baked.* Every 11.25 degrees: a 30 px flame's tail up to 3 px off
  its axis. Now each foreshortening and size has k steps to the quarter
  turn, k >= pi L / 2 for a flame L px long (at least 8, up to 110 for the
  afterburner's longest), so the tail is within half a pixel of the way
  asked for.
- *The angle's grain.* `lf_atan2` floors to 1024ths of a turn (0.35
  degrees: half a pixel on a 70 px flame). The ways are now 16384ths: baked
  from the sine table interpolated, and a flame's taken from a 257-entry
  arctangent table interpolated (one division; a binary search by cross
  products was as exact but cost 0.9 ms a frame more in the scene), the way
  asked for from the whole axis (`lf_sprite_point` of 16384 units), its
  length apart.

**The self-test.** `RV.EXE /flametest` (`tests/run.sh tests/flames.txt
/flametest /flight 200`), before the flames scene's first frame: the six
types, at ten distances (1,250 to 28,000 km), in 40 orientations each (every
eighth a yaw only, dead ahead: an exact ship's unturned copy at 2,500 km),
drawn as the flight draws them (into an empty band: the choices only),
exactly (its view read first, but every third time not: the neighbour's or
the sets' fallback) and from the sets. It checks that the view and turn
`ships_drawn` gives are those the draw used (`ships_last_drawn`), and that
every flame's baked way ends within half a pixel (and the ways' grain: 0.53)
of where the drawn picture's tail points, worked out from the drawn view and
turn, not by flames.c; and logs the old way's miss beside it. The result
(4,800 draws, 14,716 flames): no pose differs, the worst flame 0.512 px off
at its tail, the mean 0.15; before, 1,100 of every 1,470 flames were more
than a pixel off near, ~800 far, the worst 140 px; `flametest: OK`. It also
found `scale_of` wrapping past 16,384 km (a ship there drawn too big: the
squares' sum is now kept in 32 bits).

**The test scene.** `RV.EXE /flames` (`tests/flames.txt`): six ships in two
rows of three 3,000-3,500 km ahead, standing, each yawing at its own rate
and the odd ones pitching too, so every view of their engines comes round;
ships 2, 4 and 6 on the afterburner. With `/record 1` the flight lands
after 450 frames (15 s at 30 fps), however slowly they are made.

**Steadier exact draws** (found in that scene: the user saw its Gawain
"wobbling back and forth"). Its full-size view, streamed for the exact
draw, was often not in, so it was drawn from the sets that frame: their
coarser lattice's view (30 degrees, against the exact draw's 15), every
other frame (233 times in 450). The stream had 4 slots, one read a frame,
and as many ships drawn exactly as slots: a read always evicted a view a
ship needed. Now: 6 slots, 2 reads a frame, at most one fewer ship drawn
exactly than there are slots; the plan marks every exact ship's view (and
its last one in) as wanted before it reads any (`ships_touch`); a view not
in yet is drawn from its last one, a lattice neighbour (`resident_neighbour`
in `ships_draw_exact`); and a ship demoted from the exact draw waits 1.5 s
before it may be promoted (the user's rule: on the budget's line two ships
had gone back and forth), the ships drawn exactly last frame taken first.
In the scene, with 6 slots or the old 4, no ship changes path after its
first frame.

**At most two ships drawn exactly** (the user: the scene ran at 20 fps with
five; cap them, with hysteresis, to meet the frame). A frame's time, now
logged for every flight (`flight: ms a frame: the plan ...; the picture ...
(ships drawn exactly ..., from the sets ...); the whole frame ...`), showed
three exact ships costing 25.6 ms of a 43.8 ms frame at 40,000 cycles: the
turned draw works from a dense copy of the view, there are two dense buffers
(64 KB each, all of upper memory), and with three ships each evicted
another's copy, so every one re-expanded its view every frame. So the exact
ships are at most `SHIPS_DENSE_BUFS` (2): the exact draws 11.2 ms, the frame
28 ms, 28.7 fps (the 30 fps cap's waits aside); in the recording ships 3 and
6 are exact all 15 s and the other four from the sets, no switch. More
exact ships would need the dense copies sized to their views (a fighter's is
~16 KB of the 64) rather than more memory. (Since 2026-10-21 the views are resident and
the dense copies kept in XMS, and every near ship beyond the two is drawn
exactly at half height: "No stream; every near ship turned", under the
sets.)

### The comm link: faces on the VDUs

The user's brief (2026-10-22, from a WC1 screenshot): a pilot's face on
either VDU over a grainy green-and-white link; every human pilot in the
same helmet, styled for this universe, "futuristic but functional" (their
reference, after a racing helmet and a SpaceX one: a clear-domed hood, head
on), the callsign on it, the pilot's own eyes from their portrait,
animating a bit; the helmets shared and only the differences animated; the
mouth behind breathing gear, so only the eyes differ; a low-bandwidth,
encrypted link: each row drawn twice, the picture deliberately coarse;
little memory; good on most VDUs, cropped for the rest. Then: the bare face
again as a compile-time option, to weigh its cost; and Aera and Rlaan
pilots, in heavy gear, mostly anonymous, each in a helmet of their own.

**The art** (`tools/helmet3d.py`, `tools/aliens3d.py`, `tools/comms.py`,
made in the build into build/comms-mask or build/comms-face). The helmets
are signed distance fields ray-marched in numpy, one render a helmet
(helmet3d.py's renderer: a model gives its solids, how each looks, an
optional clear visor and, for an alien, what glows in each state, lit
afterwards from the hit points so a state costs no second march). The
pilots' helmet: a dark satin shell, a clear visor in a black gasket, air
pads at the cheeks, a lamp and camera module, an oxygen mask with its
regulator, straps and a ribbed hose, a fabric neck seal; key, fill and rim
lights, soft shadows, occlusion, a studio's reflections (the visor's kept
apart, laid over the face). Each pilot's portrait goes behind the visor,
its eyes onto the helmet's eye points, its local contrast raised so the
eyes survive the reduction. The aliens (data/aliens.csv, docs/people.md)
are six helmets, three a side: the Aera's steel helms with an opaque face
plate, a lit slit for eyes and a vocoder grille that lights as they speak
(the heavy pilot's a blend of the other two: the user's pick over a boxy
one); the Rlaan's charcoal shells, smooth and fluted, clusters of
round lenses, speech lights that pulse. Then the link's look: luminance, a
dark ground, the range stretched, 56 x 40 kept (shown as 2 x 2 blocks: the
user's pick of the coarse look; `--cols 112` keeps the full width), eight
shades that are flight palette entries every cockpit shares (black, the
carrier-green livery ramp 64..71, a pale green-white), a light ordered
dither (an alien's lights bloomed first).

**Voices.** RV.DAT's `comm_cast` holds a record a speaker
(`RvCommVoice`): its base picture (the pilots' shared helmet, or an
alien's own), a window over it (what differs from pilot to pilot: the eyes
and brows; none for an alien), then its patches (eyes half, shut, glance
left, right; then its mouths), each patch only the pixels that change (the
rest 255, clear); how many mouths (0: the regulator's light shows the
speaking; 8: lipsync's shapes, a bare face; 2: an alien's speech lights,
lit partly for the closed shapes, fully for the open ones); where the
callsign is stencilled and the light lit (none for an alien: they are
anonymous); its own line; the ships it flies. `RvPerson.comm` is a
person's voice. An alien's eyes do what a pilot's do with light: dimmed,
dark (a blink is a flicker), the bright point aside (a glance).
`comm_ramp` is the shades' entries and the same two steps down for the
scan line.

**The engine** (`src/comms.c`). `comms_say(voice, vdu, text)` (text NULL:
the voice's own line) puts the picture together once in XMS as the VDU
shows it (112 x 80, each kept row twice, the second through the darker
ramp); a blink, a glance or a mouth shape rebuilds only the rows its
patches cover. Each frame `comms_draw` moves the visible rows to the screen
(an XMS move a row), stencils the callsign at the VDU's own resolution in
a 4 x 6 pixel font (at the link's it could not be read; ink light on a dark
shell, dark on a light one, a shadow), lights the regulator while a mouth
shape is open (`lip_steps` on the text), and sprinkles three grains a row
(a shade up or down). Centred on the VDU; a shorter one loses three
quarters of its crop at the bottom (the mask, the hose), keeping the
callsign. The eyes blink every 1.3-4 s (half, shut, half) or, one time in
four, glance aside for half a second. In a flight the launch's comm comes
three seconds out; after it, the first Aera or Rlaan fighter within 3,000
km hails the player on the right VDU, once (`comms_voice_for`: the side's
voice for that ship type, else the side's first).

**The bare face** (`RV_COMMS=face ./build.sh`): the pilots' helmet without
its breathing gear, the face's mouth speaking in lipsync's eight shapes
(the portraits' mouth frames), no regulator's light; the engine is the
same (a voice says how many mouths it has).

**What it costs**, from `RV.EXE /commdemo` (it times a comm before the
voices say their lines, the aliens first; DOSBox at 40,000 cycles):

| | mask (the default) | bare face |
|---|---|---|
| a frame's draw (a 71-row VDU) | 1.30 ms | 1.34 ms |
| a comm's start (the picture put together) | 5.0 ms, once | 5.0 ms, once |
| a blink's step (4 a blink, every 1.3-4 s) | 1.38 ms | 2.31 ms |
| a mouth's step (speaking, about every third frame: lipsync's 100 ms) | none (the light: in the draw) | 2.0 ms (0.67 ms a frame on average) |
| the pilots' art in RV.DAT (ten) | 24 KB | 38 KB (RV.DAT 17.5 KB bigger with the directory) |
| XMS while a comm shows | the 9 KB picture | the same |

An alien's speech step is 1.6 ms, its eyes' 0.8 ms; the aliens' art is 18
KB for six. Nearly all of it is XMS calls: a row rebuilt is four reads and
a write, a row drawn one move. The picture held in conventional memory
(9 KB) would make the draw a `rep movsd` a row, about 0.1 ms.

### Balls: the spherical projectiles

The user's brief (2026-10-21): very fast spherical projectiles, far more
common than the bolts, animating (1-4 frames), round so they need only a
scale; a single pixel of their colour when far; all of it from XMS; four
kinds: a silver mass driver slug (still), a glowing swirling blue ball, a
searing hot ball with energy crackling out of it, a shining green ball with
four points. Then, on the first sprites: the fire pinker with more
discharges, the green one a continuous star rather than a lit ball with
spikes, the blue one fiery plasma with random white highlights and clear
sections.

**The sprites** (`tools/balls.py` -> `BALLS.DAT`, built by `build.sh`, in
the page's `retrovega.tar.gz`): for each kind and frame (a set: 1 + 4 + 4 +
4 = 13), a sprite at every diameter 1 .. 32, d x d bytes, 0 clear; 145 KB.
Each is drawn procedurally 8 x 8 times supersampled: the slug a shaded
metal sphere; the plasma three swirling arms round a white core, its edge
ragged, white flecks and clear rents from noise new every frame; the fire
white through pale pink to hot pink and crimson, thirteen jagged
discharges out of its rim, most forking; the star the curve |x|^½ + |y|^½ =
1, white-green at its heart, turning an eighth of its points' spacing a
frame (four frames loop). A pixel mostly covered is opaque, a partly
covered one (a glow) a stipple, the rest clear; colours are the flight
palette's fixed ones (1 .. 207), or a checker of two where their mix is
much nearer than any one (the palette has no pinks: salmon and lavender
make them). At 1 and 2 px a ball is one colour, its inner part's.

**The engine** (`balls.c`, `balls.h`; the asm in `lasers.asm`, its
reference in `lasers.c`). The file goes into resident XMS when the game
starts. A flight with balls copies every set's sprites up to 8 px (204
bytes a set, 2.6 KB) into conventional memory; bigger ones are fetched from
XMS once a frame into an 8 KB buffer, each set and size once (a frame with
more falls back to the 8 px one). A ball is 20 bytes (position, a velocity
in positions a frame, its kind and animation phase); `balls_move` flies
them. Once a frame `rv_balls_place` puts them through the camera (the
bolts' `LzCam`, its near plane 100 km): diameter = 2 x the radius on
screen, rounded, 1 .. 32; a kind past `z1` = rf / 12 is one pixel without
the division; the set is the kind's first + (anim + phase) & mask (the
animation steps every other frame, 15 a second). Small ones (8 px at most)
go to the front of the list with their sprite pointers, big ones to its
back; the small ones are drawn before the ships, the big ones after
(`rv_balls_draw`: a ball not in the band costs five to nine instructions,
one of one pixel goes straight in, the rest two pixels a step with 0
clear, clipped to the cockpit's extents). Radii: slug 1.5 km, plasma and
star 2.5, fire 3: 12-24 px at the near plane, one pixel past ~1,000-2,000
km. The asm is checked against the C (records and pixels) at the start of
every flight with balls; `balls_time` logs the costs.

**Costs** at 40,000 cycles: a ball placed 2.69 us; drawn 0.78 us at 1 px
(the band's whole visit), 5.4 at 4, 13.2 at 8, 40 at 16. The benchmark
(`RV.EXE /balls -1 /bigships 1`, `tests/lasers.txt`: far balls fired from
the far ships every way at 60 km a frame, living 1-3 s, 12 near ones):
1,687 far balls and 12 near held ~30 fps beside a big ship drawn exactly,
16 far ones and the cockpit; the balls 7 ms of the frame (placed 4.0,
drawn 1.4, flown and fired 1.6 in the benchmark's C). `/balls N` fixes the
count, `/nearballs N` the near ones; with no `/lasers` the scene has one
bolt. `RV.EXE /balldemo` (`tests/boltdemo.txt`, `/record 1`): ten of each
kind in a column off each corner of the view, 300 km apart from 100 km to
3,000, flying at the eye 10 km a frame: every size at once, ten seconds.
The module's state and its log formats are far (`rv_logf` takes a far
format): DGROUP is nearly full (Watcom's `__far` binds to one declarator:
each in a list needs its own).

### Missiles

The user's brief (2026-10-21): missiles drawn with the bolts' code as far
as possible, long and round with a flame out of the back like a fighter's,
bigger than a bolt, a line when far; four in flight at a time expected, six
at most. On the first recording: thicker, red toward the engine rather than
at the front (the tip may be bright red too), fins if they cost nothing;
not as exact as the ships, since a missile is seen for moments.

`missiles.c`, `missiles.h`: a pool of `MISSILE_MAX` (6), each a position, a
way (256 = 1.0) and whether it flies and its motor burns. Once a frame
(`missiles_place`, after the plan has added the ships' flames) each is put
through the camera as bolts are: `lz_project`, split out of `lz_place` (so
the bolts' asm check still covers it), gives a bolt's ends on screen with
its head kept, their depths and its radius. A missile is 60 km long and 4
km in radius (a bolt is 24 and 2): under the bolts' line radius it is a
gray line with the flame's pale violet on its tail's pixel; over it, three
parts, each the bolts' two nested capsules (`rv_fat_setup`, the whole's
radius): the rear quarter dark red with a red middle, the body dark gray
with a lighter middle, the 4 km tip red with the brightest red middle,
drawn farthest first (from behind, the rear hides the rest). Two fins are
lines (`rv_lines_draw`) swept back from the body's edge 30% of the way from
the tail to 2.3 radii out near it, a pair a fin from 4 px thick, none end
on. The flame is a baked one (`flames_add_at`, split out of `flames_add`:
the nearest of the afterburner's look to a nozzle, a tail's way and length
on screen and a root's radius, the body's), 40 km, its rows mixed anew every
frame; one pointing toward the camera is drawn last, one pointing away
first. All in C (six at most): at 40,000 cycles the demo's six cost 0.48 ms
a frame to place and 1.09 to draw. `RV.EXE /missiledemo`
(`tests/boltdemo.txt`, `/record 1`): two fired from under the wings and
seen from behind as they speed away, one crossing 700 km out side on,
another 2,500 km out turned a little toward the camera, one coming nose
first from 6,000 km, one far (9,000 km) climbing across as a line; ten
seconds.

### The sky's far things: nebulae, galaxies, planets

The user's brief (2026-10-21): nebulae, distant galaxies and planets, all at
infinity: a sprite at one scale that turns as the camera rolls ("we may be
able to demand symmetry, but let's see what we can afford"). Then, on the
first frames: half the rows, doubled or interlaced, as a cheaper way, and
interlacing as the look for translucent gas, an option.

**The art** (`tools/sky.py` -> `SKY.DAT`, built by `build.sh`, in the page's
`retrovega.tar.gz`): each thing is drawn procedurally once, three times finer
than shown, in a box of odd size (its middle a pixel's), then for every one
of 128 turns (2.8 degrees each) turned, box-filtered down, put in the flight
palette (a 32^3 table: the nearest colour, or a checker of two where their
mix is much nearer) and run-length coded in the ships' format. Light over
black space: dimmer than 9 of 255 is clear (a stipple below it to half), a
planet's body is solid, its night side on the palette's near-blacks (16 up).
Four things: an emission nebula (hydrogen's pink heart, oxygen's teal outer
gas, dust and rents, young stars), a spiral galaxy at a slant (bulge, two
arms with knots, dust lanes), a banded gas giant lit from the upper left
(turbulent bands, a storm, a blue haze on its lit limb), a ringed planet lit
from above.

**Symmetry for nothing.** The ships' blit mirrors and flips sprites, and a
half turn is both, so every thing keeps only the first 64 turns; a thing
that is its own mirror image (the ringed planet: lit from above, bands and
rings level) keeps 0 .. 32 and draws the rest flipped (n/2 - k), both (k -
n/2) or mirrored (n - k).

**Rows.** A thing may keep every other row (each the mean of a pair: half
the bytes) and is drawn every row, doubled (each kept row on two screen
rows; of a full thing, its even rows twice) or interlaced (only every other
screen row from its top: what is behind shows between the lines, a gas's
translucence; brightened by half to make up for the black lines).
`ships_blit_rows` draws all of these; `RV.EXE /skyrows N` makes every thing
so (0 each its own way, 1 every row, 2 doubled, 3 interlaced). The nebula is
kept at half height and drawn interlaced.

**The engine** (`sky.c`, `sky.h`). `SKY.DAT` goes into resident XMS when
the game starts (2.8 MB; 1.0 MB more of the page's download, gzipped). A flight
adds things with `sky_add` (the kind, a direction and an up, world axes;
the up is scaled to 256: `lf_atan2` overflows past 2^17). Once a frame each
is put through `lf_sky` (where its direction falls, how its up is turned on
the screen); the nearest of the 128 turns picks a kept turn and its flips,
whose header (12 bytes) comes from XMS. Per band, nebulae and galaxies are
drawn before the stars and planets after them.

| thing | box | turns kept | KB | KB a turn | px drawn |
|---|---|---|---|---|---|
| nebula (half the rows) | 241 | 64 | 997 | 15.6 | 13,849 lines |
| galaxy | 201 | 64 | 708 | 11.1 | 8,315 |
| gas giant | 131 | 64 | 761 | 11.9 | 11,206 |
| ringed planet | 181 | 33 | 303 | 9.2 | 8,315 |

Cost at 40,000 cycles in `RV.EXE /skydemo` (the four round the nose, the
camera rolling a whole turn every twelve seconds and yawing and pitching;
three or four on screen at a time): drawn every row 3.70 ms a frame, as
built (the nebula interlaced) 2.79, all interlaced 2.32; placing them
nothing measurable. Interlacing saves less than half: the rows are still
fetched and walked.

### Asteroid fields

The user's brief (2026-10-23): a nav point has up to eight ship types (two
or three capital ships, two or three friendly fighters, three enemy
fighters); some have a hazard instead, an asteroid field. Asteroids tumble
endlessly and quickly, come in about four kinds and only ever scale (they
stay upright through the player's roll); they are plentiful and gather
round the player: a fixed set near the player, those flown past and left
behind taken away and put back ahead; a density says how thick the field
is. Each player's rocks are their own in multiplayer, as the hook layer
has WC1's. A rock is a plain XMS copy into the frame, and the rocks' art is
in memory only near a field. Then the questions: can the field make the
player dodge, and can the CPU draw many rocks at 30 fps?

The user's second round (2026-10-24), on the first field: a crash breaks
the rock apart and tosses the player to the side, their speed kept; the
rocks too round: jagged, bright grey, much smaller ("shattered planet crust
rather than Ceres... a sharp rock found on a playground"), roughly round,
spinning horizontally, a quarter to a half of a ship; 120 is probably a good
density; spawned with a velocity toward the player, or a bit off it (ahead
of where the player will be, left, right, up or down at random). Then, on
the faceted stones: the facets are good, but big noise, rich noisy textures
("flash frozen metal possibly"), shown with pictures of a black glinting
slag and a heap of grey and rose lava rock. Then: "the texture is much
better but the shape needs to be more jagged and less convex... some
overhangs etc, very sharp", with a sheet of lumpy, holed rocks.

**The art** (`tools/rocks.py` -> `ROCKS.DAT`, 7.1 MB, 3.9 MB gzipped; `docs/rocks.png`).
Four rocks, each a solid the fragment shader marches (a signed distance,
no mesh: overhangs and caves come for nothing and every crease is exact):
the union of three to five faceted chunks (each a convex polytope of 10-14
planes spread round its own middle, the chunks offset from one another, so
where two meet there is a sharp re-entrant crease or an overhang), less
seven to fourteen caves (spheres just under the surface: round holes with
overhanging rims) and two or three bites (small polytopes: sharp notches),
its faces a little rough; centred on its own middle and scaled to the unit
sphere. Its surface: large light and dark blotches, two sizes of bubble
pits (dark, their rims a little lighter) that also tip the faces' normals
as bumps, fine grit, and on the "frozen metal" glints that catch the light.
The kinds: a grey scoria, a dark slag full of glints, a lava rock (the
palette has no rose: it reads peach), a light pumice. Lit as the balls are
(the key light up and to the left, a fill from the lower right), 32 frames
of a turn about the screen's vertical, three times finer than the biggest
size, then box-filtered to every size: each diameter 1 .. 8 (eight of the
frames: at that size the spin hardly shows), then a ladder of 40 sizes to
128 px (every pixel to 16, then 7% steps). Colours are the flight palette's
fixed ones or a checker of two (sky.py's table). **A row of a big sprite is
a few runs of opaque pixels** (at most four; a gap under 3 px is drawn,
the rest stay clear: the rocks have overhangs), each of an even length
(the XMS driver moves even lengths; an odd run takes a pixel on its fuller
side), so a run is one XMS move. A record (ROCKS.DAT version 2) is its rows,
its run table's bytes, the run table (for each row a count, then each run's
left + 1 and length) and then the runs' pixels, one after another.

**The engine** (`rocks.c`, `rocks.h`; the per-rock work in `rocks.asm`).

- *The bubble.* While the player is near a field, a fixed set of rocks is
  kept in a sphere of 9,000 km about them: density x 3.05 rocks (density =
  rocks per (10,000 km)^3; 120 when the mission says nothing: 366 rocks),
  1,200 at most. A rock's radius is 110 .. 240 km (a fighter's half box
  is about 450 km: a quarter to a half of the ship), its kind any of the
  four, its spin a turn in one to four seconds either way from a random
  phase.
- *Velocities.* Every rock flies (`rv_rocks_place` moves it as it places
  it): 3-8 km a frame toward where the player will be when it gets there
  (their position plus their velocity times its time to them, head on),
  off that point by up to 4,000 km across and up at random: most fly close
  by, now and then one comes straight in.
- *Once a frame* every rock goes through the camera (`rv_rocks_place`, its
  C reference `lz_rocks_place`, checked to the bit by RVT.EXE when its
  first field is entered): one that has left the sphere (the player flew on
  past it) goes to a reap list and is put back on the sphere's far side,
  cosine-weighted about the way the player looks (as an endless field's
  rocks would come in), inside the field or not at all; one within reach
  on every axis goes to a hit list; one before the near plane is not
  drawn; else its diameter (radius x focal / depth), past 60% of the
  sphere's radius shrunk to nothing at its edge (by distance squared, no
  root: a rock grows in from a point wherever it comes in, nothing blinks
  into being), its middle on the screen, its frame (phase + frame counter
  x spin / 4), its depth.
- *Dodging.* Now and then (every 720 / density seconds on average: 6 s at
  120) the first rock to be brought round goes on the player's course
  instead: at the sphere's edge dead ahead, off the line by less than its
  radius, flying straight at where the player will be. Flying on straight
  is flying into it; it grows in from a point as it comes.
- *Hits.* Each rock on the hit list against the player's hit box (a
  sphere against collide.c's box, in the ship's frame). The player takes
  speed x how head-on (a fifth at least) x the rock's radius / 4,000 + 1
  (a head-on 170 km rock at 180 kps: 8; the fighters' shields are 4-20 a
  side) through `do_damage`, the field's slot its source, the place hit in
  `tempVector`, and is tossed aside: across their way, away from the rock
  (dead ahead: a side at random), at their speed / 20 + 30 km a frame
  dying away by an eighth a frame (about 300 km at 180 kps), their own
  speed kept. The rock breaks: seven pieces a quarter to a half its size
  (`g_frag`, 32 at most) fly out every way from where it was, going on with
  three quarters of the player's velocity so they are seen spreading ahead,
  spinning fast, gone in one to two seconds, flown and placed by the same
  asm, never hit; the rock itself is brought round ahead.
  `do_damage_impl` (flight.c) is the first damage model: the shield on the
  side hit (front or rear), then that side's armour (front, rear, left,
  right), then the hull; nothing dies of it yet.
- *Drawing.* The small ones (8 px at most) are drawn as balls are
  (`rv_balls_draw`, from every kind's small sprites copied to conventional
  memory, 6.5 KB), before the ships. The big ones are sorted by depth (a
  counting sort, 64 km a key) and drawn in among the ships by it
  (`rocks_draw_to(depth)` before each ship, the rest after). A big one up
  to 128 px is its ladder size's record: its head and run table fetched from XMS,
  then `rv_rocks_xms` moves each row's runs, clipped to the cockpit's
  extents, by the XMS driver straight into the band buffer (a run cut to
  an odd length loses its pixel at the cut); `/rockxms 0` moves the whole
  record into the scratch buffer instead and `rv_rocks_copy` copies the
  rows. Past 128 px a ladder sprite is drawn as 2 x 2, 3 x 3 or 4 x 4
  blocks (up to 512 px), the run widened once in a line buffer and copied
  to its rows: the block mode's look.
- *Memory.* `ROCKS.DAT` goes into XMS at its own level, `XMS_HAZARD` (above
  the mission's), when the player comes within the sphere's reach of an
  asteroid field, and is given back 2,000 km farther out; the archive's
  assets read meanwhile go to that level too (`dat_level`). So does the
  conventional memory: the pool (24 bytes a rock), the screen lists and the
  small sprites, about 40 KB at 1,200 rocks. Read in, it takes 170 ms.
- *Multiplayer.* The rocks are no slots: nothing about them is sent, and
  they come from a random stream of the module's own (never `randomSeed`,
  the play's shared one). Only the player's own ship hits them. The
  fields are where the layer looks (`rvabi.def`): `fieldTable` (type,
  position, radius in km, density), `fieldCount`, `currentField` (the
  record the player is in), `fieldRockTally` (the rocks kept); each field
  has a slot of its own above the ships (`RV_CAMERA_SLOT - 1 - i`:
  EntityType 9, `rvFlags` bit 2, a local-only hazard) that a rock's hit
  names in `do_damage`, so the layer can soften rocks or report a hit as it
  does WC1's; `clearFields` (the layer's "rocks off") takes every field and
  rock away. `entitiesToDespawn` stays empty: no rock is a slot.
- *Missions.* `data/missions.txt` has `field asteroid x z radius
  [density=N]` (the record's spare byte); the nav points do not carry their
  hazards into the flight yet, so `RV.EXE /field D` (the page's
  `?field=D`) starts every flight in a field of density D, 200,000 km
  across, centred where the player starts. The page fetches `ROCKS.DAT`
  (rv/ROCKS.DAT.gz, listed in rv/ships.json's `files`) when the flight's
  list in `RVWANT.TXT` names it, as it fetches the ships' views.

The second round's rocks (110-240 km, flying at the player; the same
40 s straight on at cruise, natively at 40,000 cycles):

| density | rocks kept | on screen: small, big (most) | hits in 40 s | the whole frame |
|---|---|---|---|---|
| none | | | | 13.2 ms |
| 120 (the default) | 366 | 3, 10 (22) | 3 | 14.9 ms |
| 240 | 732 | 6, 18 (46) | 3 | 17.1 ms |
| 400 (capped at 1,200) | 1,200 | 10, 34 (81) | 10 | 19.1 ms |

Small rocks cost little to draw: more of them is cheap (1,200 kept and 81
on screen at 19 ms of the 33). What follows was measured with the first
rocks (80-1,000 km, convex, standing still).

**Costs** at 40,000 cycles (DOSBox charges an instruction a cycle; an XMS
move is host code, a few instructions here whatever its length). Measured
alone, in a loop (RVT.EXE logs them when its first field is entered):
placing a rock 2.84 us (366 rocks: 1.0 ms; 1,200: 3.4 ms); a big rock's
row moved by XMS 1.1 us whatever its width (32, 64 or 128 px), copied
after one move of the whole rock 1.9-2.2 us; a near rock as 2 x 2 blocks
2.5-3.6 us a screen row (its run widened, then copied). A rock of 8 px or
less costs what a ball does (a few us). In flight
(`tests/rocks-straight.txt`: the first mission's launch, then 40 s
straight on at cruise; the whole frame is elapsed time less the 30 fps
cap's wait, the part-by-part figures alias with the cap and are rough):

| density | rocks kept | on screen: small, big (most) | hits in 40 s | the whole frame |
|---|---|---|---|---|
| none | | | | 13.2 ms |
| 60 | 183 | 3, 13 (21) | 3 | 15.4 ms |
| 120 (the default) | 366 | 4, 21 (33) | 5 | 17.8 ms |
| 240 | 732 | 11, 44 (68) | 11 | 21.8 ms |
| 400 (capped at 1,200) | 1,200 | 21, 68 (97) | 48 | 27.6 ms |

So 30 fps (33 ms) holds natively at 40,000 cycles at every density up to
the cap. The copying way (`/rockxms 0`) cost more at each density in a
first sweep (120: 19.1 against 18.4 ms; 240: 24.4 against 22.5; 400: 30.7
against 27.1). Most of a dense field's drawing is its few nearest rocks
(past 128 px, drawn as blocks: at 120, about 2.2 of 4.4 ms of big rocks;
at 400, 5.5 of 8.3). The hit list's reach was at first the biggest rock's:
at 400 its 16 places filled with rocks that could not touch the ship, and
the ones that could went untested (1 hit in that run, 48 since); it is
now each rock's own radius and the player's half box.

In the page (headless Chrome on this box's i7-6800K core, 50,000 cycles,
`?field=D&env.WCNET_PERF=4`, the same 40 s flown by
scripts/web-rv-flight.mjs): at 120, 30.0 fps, the emulator at 100% of
real time, the worst frame 35-43 ms (13.4 emulated ms a frame); at 400
(1,200 rocks, 69 big on screen, 98 at most), 30.0 fps, the emulator at
97-100% (21.2 ms a frame). The XMS moves' host work (a callback a row) does
not show. `ROCKS.DAT` came with the ships' sets the flight asked for (the
three in 6.9 s, the rocks a 3.8 MB download).

The look's weak point: past 128 px a rock is 2 x 2 .. 4 x 4 blocks of its
128 px sprite, and its checker dithering becomes a coarse checkerboard on
the nearest ones. A bigger ladder (sizes to 192 or 256, half the rows kept
past 128) would cost 8-40 MB more of XMS and as much again of download.

### Where a ship is on a screen: boxes for targeting

The user's brief (2026-10-21): functions that take an entity's index and a
camera and give its axis-aligned box on the screen, for targeting, the lock
and the cockpit's animations.

`flight.h`: an `RvCamera` (a position, a basis, the projection's middle and
focal length, and the part of the screen it shows); `flight_camera` fills
one with the player's view. `flight_ent_box(slot, camera, &box)` (0 when no
ship is in the slot) gives an `EntBox`:

- `x0, y0, x1, y1`: the box of the ship's sprite as it is (or would be)
  drawn, `ships_box` (`ships.c`) following `ships_draw`'s choice: from the
  sets, the record of the size step, view and roll step it takes, placed
  as `ships_blit_view` places it (mirrored or upside down); drawn exactly
  (the plan's choice, when the camera is the one the frame was planned
  for), the turned image's box (`lf_rotate_setup`). Engines' flames are
  not in it. When there is no sprite (too small, not in), the square
  below.
- `sx, sy` its middle on the screen; `half` half its size there (the
  type's `lod0_px` at the flight's scale for its distance and the camera's
  focal length): a square that does not change as the ship turns, for
  lock brackets.
- `dir`: the way to it on the screen, 0..1023 clockwise from up, also when
  it is behind the camera (an arrow at the view's edge); `X, Y, Z`: it
  from the camera (right, up, ahead, sixteenths of a km); `ahead`;
  `visible` (ahead and its box touching the camera's part of the screen);
  `exact`.

`RV.EXE /boxes` brackets every ship's two boxes in flight (the sprite's
cyan, the steady square amber) and logs them now and then; with `/flames
/record 1` the six turning ships' brackets follow their sprites.

### Ship-to-ship collisions

The user's brief (2026-10-21): collision functions, coarse then fine; the
fine one the smaller ship's hit box against the other's vitals (engine
centres, hardpoints, a small core sphere in the middle), one axis-aligned
compare each; a vector back that rectifies the intersection and shoves the
ships apart, or 0. Then: the smallest box with the biggest ship's vitals,
the core conservative, a sphere against the box by growing the box by its
radius (a rounded, "squircle" box is fine); the core a sphere that fits
inside the sprite at every view angle, to 95% coverage.

**Data** (`tools/hitboxes.py` -> `data/hitboxes.csv`, packed by
`rvdata.py` into `RvShipType`, RVGAME.DAT version 3): in the sprites' frame
(127 = the half box, +x the ship's left), the mesh's half extents (`hit`),
the core's radius (`core`: in each of the ship's views the biggest disc
about its middle that is 95% opaque, the least of them: 13% of the half box
for the Gawain, 24% for the Mule; 0 for ten types whose middle shows space
in some view), and up to eight vitals (`vital`): the mesh's nose, tail and
wingtips and the weapon mounts of Vega Strike's units.csv nearest the box's
edges. The engines are `engine`'s. A ship's half box in the world is
`lod0_px` x 3.125 km (the flight's scale: full size at 2,500 km, focal
400).

**`collide.c`.** `collide_coarse(a, b)`: the half boxes' spheres, per axis
first. `collide_ships(a, b, &push, &depth)`: coarse, then the smaller
ship's box (by volume) against the bigger's vitals, each taken into the
box's frame by one 3 x 3 product (the axes dotted, once a pair) and the
middles' difference: a point is in when inside on all three axes; the core
is a sphere against the box (the box grown by its radius with rounded edges
and corners: inside, or within its radius of the box's nearest point). Only
when that finds nothing, the bigger ship's box against the smaller's
vitals (a fighter in the middle of a capital ship's hull, between its
sparse vitals). The push frees the deepest vital: in the box, out the
nearer face of the axis it is shallowest on; the core off a corner,
straight away from the box's nearest point. It is b's move (a's is the
opposite; the demo shares it, half each). `collide_shape` gives a ship's
box corners and vitals in the world, for drawing.

**The hull: three silhouettes** (the user's pick, "useful especially for
point blaster shots"). `tools/hitboxes.py` also rasterizes each mesh seen
from above, the side and the front (orthographic, over its box) into 64 x
64 bit masks, a cell solid when the mesh covers 40% of it: `data/shapes.dat`
(54 KB for 35 types; the build copies it to `SHAPES.DAT`, resident XMS; a
flight keeps the types it meets in an 8-slot cache, 1.5 KB each). A point in
a ship's frame is in its hull when it is in the box and its cell is solid in
all three (the cell of v on an axis of half extent h is (v + h) / 2h x 64;
masks: top columns X rows Z, side columns Z rows Y, front columns X rows Y;
X the ship's right, the mesh's -x). The way out of the hull for a push: the
shortest of twelve, along each mask's two axes either way to its first
clear cell (out of one silhouette is out of the hull). The vitals tests
above are against the hull (the core sphere as its middle and six points a
radius off it); a type without silhouettes is its box. `collide_point`
(a blaster shot) and `collide_segment` (a bolt from where it was to where
it is): the segment's box against the ship's, both ends into the ship's
frame, clipped to its box (in 1024ths of the way, 32-bit: Watcom's long
long divisions cost thousands of cycles), then walked in cell units, at
most half a cell an axis a step, until a cell is solid in all three: the
first point in, and how far along. At 40,000 cycles a bolt's 75 km move
through a fighter's box costs 81 us (most of it the walk: a fighter's box
is thin, 4 km cells up and down); a ray from the eye through it 153 us.

`RV.EXE /hulldemo [/hulltype N]` (`tests/boltdemo.txt`, `/record 1`): a
fighter turning every way 1,800 km ahead, a 24 x 16 grid of rays from the
eye through its box on the screen tested each frame (red in, grey missed;
recorded, every other frame clean, the grid logged): over the 150 clean
frames 81% of the hits are on the ship's sprite and 85% of its points are
hit (the hull is fuller than the ship where it is hollow from every side;
the engines' glow is not hull; the sprite is a lattice view a few degrees
off). Which side is the ship's right could not be checked: the only
strongly asymmetric type (the Convolution, 0x0D) fails its set's load
check (`ships.c`'s bound on blocks, views and rolls), and the others are
too nearly symmetric to tell (mirrored masks score the same).

`RV.EXE /collidedemo` (`tests/boltdemo.txt`, `/record 1`): two fighters
2,600 km ahead flown at each other at 14 km a frame each, three rounds:
level (with the boxes: hits in 44 of the 46 frames their spheres touch,
pushed back along the line of flight, nose to nose; with the hulls: 22,
pushed apart up and down, the thin hull's shortest way out, so they slip
past), 120 km apart vertically (boxes: 11 hits; hulls: none, they do not
meet), 700 km apart (inside each other's spheres 35 frames, no hit). Their boxes (red while they hit) and vitals are
drawn over them.

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

## The native build: the game without DOS

`host/game.sh` builds the whole game, `src/` as RV.EXE has it, for a 32-bit
host (`build/game32/rv`: gcc `-m32`, UBSan) or, with `WEB=1`, for the browser
(`build/gameweb/rv.js`, `rv.wasm`: Emscripten, ASYNCIFY). No emulator runs
it. https://fireflyk64.github.io/rv/ is this build (`host/web/site.sh` puts
the page together, `host/web/deploy.sh` publishes it with rvsim at `rv/sim/`).
It is one player: the multiplayer layer lives in DOSBox, so flying with
others stays on the DOSBox page (`dosbox/?retrovega`).

- **DOS answered on the host:** `host/dos/` has the headers the game includes
  (`dos.h`, `i86.h`, `conio.h`, `io.h`, `malloc.h`), and `host/hle.c` answers
  what they declare, as far as the game uses it:
  - int 10h: VESA mode 100h and its 64 KB bank window;
  - int 16h: the enhanced keystroke calls, from a BIOS buffer that int 9 fills;
  - int 21h: the upper memory blocks (DOSBox's 128 KB), the largest free block (288 KB less what the far heap holds), DOS's list of lists for the memory walk;
  - int 2Fh and the XMS driver: one 63,424 KB arena, as the page's DOSBox gives;
  - int 33h: the mouse, presses counted between polls;
  - the ports: the DAC, the retrace, the OPL3 (its status faked for the detection), the PIT's rate, the keyboard's data.

  A far pointer is the host's own pointer (the build is 32-bit): FP_SEG and
  FP_OFF are its two halves, so xms.c's moves carry host addresses; MK_FP of a
  segment the game knows (40h, A000h, the UMBs, DOS's lists) gives the memory
  it means.
- **Interrupts:** the timer's (int 8, at the rate the game sets on the PIT:
  240 a second with the music) and the keyboard's (int 9) are delivered on the
  host clock where the game lets them:
  - at `_enable`;
  - at `rv_halt` (its sti/hlt), which waits for the next one;
  - wherever it reads the BIOS clock or asks the mouse or keys.

  The game's own handlers (music.c's `isr`, input.c's `key_isr`) run as they
  are and chain to hle.c's BIOS ones.
- **The screen:** the bank window's moves land in a 640×400 frame, shown
  through the DAC's palette with SDL2. It is shown at `rv_halt` (at most 60
  times a second), at each flight frame (`rvFrameDone`) and at a retrace wait.
  In the browser those are also where the page gets its turns. A game busy
  for long still gives the page one every 40 ms at its frames and polls.
- **The music:** each OPL3 register write is stamped with the time its
  interrupt was due. `host/opl3.cpp` plays them through ymfm's YMF262 at that
  time on the audio's own clock, 50 ms (in the browser 100 ms) behind the
  host's. So the notes keep the music clock's time to the sample, though the
  interrupts come in bursts.
  ymfm (`host/ymfm/`, Aaron Giles) is BSD 3-Clause. The user's ruling
  (2026-10-26) is that what ships in the game is BSD-compatible and the dev
  tools may be GPL, so `tools/oplrender` keeps DOSBox's dbopl.
- **The assembly:** its place is taken by its C twins (`src/twins.c`, one a
  routine, the asm's own parameters; `host/asm_twins.c` makes each `rv_x` a
  call to its `c_x`).
  - **The proof:** `RVT.EXE /twins` checks every twin against its assembly on
    seeded random inputs in DOSBox. `tests/host/twins_test.sh` runs the same
    test on the host and requires DOSBox's sums, so the twins compute DOS's
    answers with a 32-bit `int` too.
  - **The flight loop's skeleton:** `src/rvflight.inc` is in
    `host/flightloop.c`, the same calls in the same order without the sites
    only the layer hooks.
- **Sixteen bits against 32:** `int` and `unsigned` are 16 bits on DOS and 32
  on the host. The two agree wherever a value fits in 16 bits, and differ
  where DOS wraps one. Fixed so far:
  - the music interrupt's count of PIT ticks, which advances the BIOS clock (`unsigned short`);
  - the 16-bit hashes and generators: the chalk's grain, the hand's wobble, the flames' noise, the comm faces' looks, the ships' dither;
  - the flight's two-word edges (the low word as `int16_t`).

  RV.EXE's code is unchanged by these: every module's disassembly was compared
  before and after.
- **The same pictures:** `tests/host/shotdiff.py` compares two runs'
  screenshots pixel by pixel. For `tests/walk.txt` run by RV.EXE in DOSBox and
  by `build/game32/rv /script`, they are the same except where something moves
  with the clock: a line typing out, a lamp's blink, the comm link's flicker.
- **The page** (`host/web/game.html`, `game.js`):
  - the game's files from `game.tar.gz`;
  - the bunk's saves kept in IndexedDB after each save and put back at the next visit;
  - the ships' sets made as a flight asks (`web/rvsets.js`, the DOSBox page's too);
  - the touch controls (`web/touch.js`, shared too);
  - Caps Lock is the game's Esc.

  `host/web/game-check.mjs` plays it in headless Chrome: the base, a save, the
  briefing, a flight, the debrief, and the saves after a reload. With
  `PHONE=1` it does all of that by taps on a phone's screen, with the Keys
  button's typing and Fire.
- **A bug found on the way, DOS's too:** `rv_rotate_row2` writes one pixel
  past its span when the span is odd (the twin does what the asm does).

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
