# Making a new campaign on Wing Commander II's program

What was learned building SO3 (`so3/`), written down for the next one (an
"End Run" style SO4).  `docs/so3.md` has the file formats; this is the
legwork: what the program fixes, what is ours to change, and how to test.

## 1. The shape you inherit

A campaign is Special Operations 2's program (`SO2.EXE` patched to read the
number-3 files, see `so3/build.py`) with its own `MODULE.003` (missions),
`SERIES.S03` (scene texts), and copies of SO2's `CAMPAIGN`, `GAMEFLOW`,
`INCIDENT` scene files.  Until the CSCP bytecode is written by us, the
**campaign script and every scene's script are SO2's**.  That fixes:

* **20 mission slots** (series 1-5 x missions 0-3) and their order.  SO2's
  flow (from its texts; to be confirmed by a play-through) is a main line
  s1m0..s1m3, s2m0..s2m3, s3m0..s3m3, s4m0..s4m3, s5m3 (the last patrol)
  with s5m0, s5m1, s5m2 as "lost" exits: one mission each, then the ending.
  Blocks 15-19 of SERIES all end with the same Kilrathi epilogue scene.
* **Which outcomes the script tests.** The debriefing variants read the
  mission state: `24 nn 27 26 00` = "mission ship nn gone", kill counts, and
  objective results (GAMEFLOW's variables ACCOMPLISH(0-3), SHIP_GONE(0-15),
  OBJ_WON(0-7)).  So a new mission in a slot must keep **the same objective
  list (kind, parameter) and the same mission-ship indices for the ships the
  slot's scenes talk about** (SO2's are dumped in the session notes; e.g.
  s1m1 ship 10 the Dorkathi, s1m2 pickup(13), s1m3 ship 3 the freighter whose
  death earns the court-martial, s2m3 destroy(10), s3m0 meet(3), s3m2 meet(9)
  and landing ship 9, s4m2 destroy(10), s4m3 destroy(9) land(10), s5m2
  meet(0) land(14)).  Everything else about a mission (types, names, pilots,
  positions, how many wingmen) is free.
* **Scenes: the pictures, the rooms, who speaks each line, how many lines.**
  `scripts/wc2scene.py SERIES.S02 [block]` prints each block's skeleton:
  per Talk routine the room (`setupbackground n`), the medium shots, and
  each line with its **talker id** and string number.  A new text must give
  every string its replacement (same count), and a line keeps its speaker.
  Lines must stay under ~119 characters; `\x86` callsign, `\x85` surname,
  `\x87` the kill count.  Block N is mission N's (block = series*4+mission);
  20 is a stub, 21 the eject scenes, 22 the losing ending, 23 the carrier's
  loss.  Talk1 = the story scene before the briefing, Talk2 the briefing,
  Talk3 the debriefing (with variants), Talk4/5 in-flight radio scenes that
  nav-point steps trigger (type 1 steps: scene number, mode 4 = on arrival).
* **Talker ids** (SO2): 0 the player, 2 Angel, 4 Tolwyn, 8 Stingray, 10
  Minx, 12 Jazz, 14 Sparks, 16 Paladin, 18 Talon, 22 Crossbones, 24
  Thrakhath, 26 the Emperor, 28 Maniac, 30/36/38 radio and Flight Ops
  voices, 46 Minx on the radio, 58 the Kilrathi admiral, 200 the narrator.
  The face files are **named only in CAMPAIGN.S0n's SHAP list** (`people.v00
  people.v38 people.v01 ...`); the program resolves talker id -> file by a
  table that was not found in the EXE.  Traced so far: 0 -> people.v00, 2 ->
  v01, 4 -> v02, 10 -> v30, 16 -> v08, 18 -> v32, 28 -> v33.  **To recast a
  face, rename the file in our CAMPAIGN.S03 copy's list** (same length: e.g.
  `people.v32` -> `people.v09` puts Doomsday where Talon was; WC2's own
  `PEOPLE.V04/V05/V09` are Stingray, Downtown, Doomsday) or ship a recoloured
  copy under a new number.  Verify by screenshot: run the scene and look.

## 2. Memory: the rule that shapes every mission

The program keeps about **278 KB** for ships in flight (228 KB at the launch
nav, the carrier costs ~50 KB); a fighter type costs its whole SHIP file
(WC2's: 75-89 KB; HCl's Hellcat as converted 142 KB), capital ships and
bases load views lazily (cheap).  A type that would leave **less than ~60 KB**
fails to load: silently for a wingman (the slot stays empty: "spawn of
mission ship n left slot 1 empty" in the host log), with the loader's error
screen ("Error: ship.vNN, packet ...") for others.  A reinforcement wave
(nav record with visible=2 after its nav) loads its type when it spawns,
into a heap already fragmented by the fight ("largest block" smaller than
the file): list the wave's type on the parent nav too, or reuse the nav's
type.  An asteroid or mine field costs about as much as a fighter type (its rocks).
Hence: **one new fighter type per nav point beside the player's, never
two; a field counts as one; at the launch nav the carrier plus the player's
type plus at most one other light type**; SO2's own missions obey this and
are the safe templates.  Measured with `!mem` in flight (so3, 2026-10-16):
Hellcat (81 KB) + hulks + Gothri at a nav 81 KB free; Sabre + Grikath +
field + a friendly Sabre 49 KB (works, but nothing more fits there);
Morningstar + Jalkehi 151 KB.
The Hellcat is brought to WC2 size with `wc2data.scale_fighter(parts, 0.75)`
(sprite codec `decode_sprite` / `encode_sprite`, lossless round trip; it
also scales the per-view hardpoint pixel positions and the debris sprites):
142 KB -> 81 KB.

## 3. Mission data facts

* Nav lists hold ten mission-ship **words**; up to three types per nav.
* Give wing ships explicit positions (SO2: player 3000,0,0; wingman 1500,0,0
  formation 1; third 1000,0,1000 formation 2; fourth 2000,0,2000 formation 3):
  a ship left at 0,0,0 sits inside the carrier and explodes at launch.
* The two-player seat is the first Confederation ship with orders 5 in nav
  0's list after the player, and **slots are given in list order: the player
  first, the wingman second, the carrier last** (a carrier listed first takes
  slot 1 and the second player sits in it); SO2's lists put the carrier
  first, so reorder them.  SO2 slots without a wingman need one added.
* **A wingman (orders 5) needs an AI file for its pilot id: `INTEL.0nn`, nn
  the id in decimal** (the game has 001 003 004 006 009 011 012 014 022-025
  028 029 036; 0x24 Maniac, 0x04 Stingray, 0x09 Doomsday, 0x0B Shadow's);
  an id without one ("Error: intel.021, packet 0") kills the loader at
  launch.  Pilots with orders 4 fly by the ship type's `INTELSHP.0nn`.
* `alt` (byte 0x13) picks an ALTSHIPS record: SO2 uses 13/14 Mandarin
  Sabres, 15 Jazz's Morningstar, 16 Mandarin Broadswords, 17/18 the friendly
  Dorkathi you can land on.  Keep them where the slot had them.
* Reveals `(value, nav)` set another nav's visible byte on arrival (SO2
  s1m3 hides nav 0 and shows the landing carrier at nav 5).
* Fleeing capital ship: behaviour 6 toward a far ship.  Pickups: type 53
  class 61 (pod) or 54/44 (pilot), objective kind 6; only Sabres and
  Broadswords have the tractor beam.

## 4. Test aids (key script, `WCNET_KEYSCRIPT`)

`!kill=<slot>` (server only), `!killfoes[=<mission ship>,...]` (every enemy
slot, sparing the listed mission ships, e.g. a surrendering freighter),
`!status=1` (land now), `!mem` (free heap), `!shot=<name>`, `skip` (Esc
through scenes until the next flight), `clickskip[=x:y]` (click the pointer
spot through scenes and the barracks' "fly" button, default 0.48:0.55, until
the next flight; the clock restarts in the next flight).  A campaign
play-through is `clickskip` + per mission a repeated `a` / `!killfoes` cycle;
run it solo first (`WCNET=0`), then host + client (`w2duo.sh`).  Check each
mission's host log for "left slot", "screen: Error" and the spawn list.

## 5. Order of work for a new campaign

1. Choose a slot-by-slot template from SO2 (ship indices, objectives, nav
   count) and reskin: `campaign.py` data, `build.py`, pack, test spawns.
2. Write the 24 text blocks against `wc2scene.py`'s skeletons.
3. Recast faces in CAMPAIGN.S03's SHAP list; screenshot each room once.
4. Play the campaign through with the rig; fix the order and the variants.
5. Menu entries (`web/gamefiles.js`), build-web, deploy, live check.
