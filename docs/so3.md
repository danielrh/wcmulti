# Wing Commander II's data files, as learned for SO3

Working notes behind `scripts/wc2data.py` and `so3/`.  Addresses are
SO2.EXE's unless marked WC2.EXE (its overlays are numbered one higher);
`scripts/wcexe.py -e wc2/SO2.EXE dis ...` reads them.

## 1. The program and its number

Special Operations 1 and 2 are WC2.EXE's program with a data-file number:
`mov word [dseg:CB16], 2` at five places (`seg001:02AB`, `02F6`, `0339`,
`ovr118:1462`, `ovr127:0003`), and a sixth place that sets it by another
road: the start screen's second button (shown once a saved game exists)
resumes the story from the saved game's current record in `ovr118:0357`,
which ends with `mov word [bp-6], 2` (`ovr118:0427`) and whose caller
stores that in CB16 (`seg001:0315`).  Left at 2, SO3's resumed record ran
in SO2's missions ("a weird SO2").  Names built from the number: `module.00%d`, `campaign.s0%d`,
`gameflow.s0%d`, `series.s0%d`, `planets.v0%d` (beside `planets.v00`),
`startglb.00%d`, `altships.00%d`, and `savegame.so2` as a literal.  A traced
run (`WCNET_WATCH` logs the game's `int 21h`) through the barracks, a
briefing, a flight and a landing opened exactly those with a 2; `incident`,
`vdu`, `medium`, `screens`, `tempglob` and the rest were opened with other
numbers.  `so3/build.py` describes SO3.EXE as byte patches of SO2.EXE (a 3 at the
five places and in the resume constant, `savegame.so3`), which the page applies to the player's copy
(the user's rule: the program is patched by the system, never shipped),
and copies the numbered files to their 3 names.  The scene files name each
other by literal names inside their scripts (`series.s02` in GAMEFLOW and
CAMPAIGN), so the copies have those renamed too.

The multiplayer hooks know a program by its file name and a text at a fixed
place in its data segment (`kGames` in `src/cpu/wcnet_game.cpp`); SO3.EXE is
listed there with SO2.EXE's tables.

## 2. The block container

Every GAMEDAT file: a dword of the file's size, a table of dword entries up
to the first block (low 24 bits the offset, the high byte a flag), the
blocks.  Flags seen: `E0` plain data, `C1` a table of shapes, `FF` an empty
block, `20` LZW-compressed (a dword of plain size, then codes of 9..12 bits,
LSB first, 256 resets, 257 ends; MODULE.001 and most fighters' SHIP files
are stored so).  Nested tables (a block that is itself size + offsets +
children) have plain offsets in WC2's files; Academy's tools write the E0
flag on them too, and WC2 does not mind the flag on top-level entries but
the nested ones are made plain to be safe.

## 3. MODULE.00n: the missions

Seven tables, each indexed by `series * 4 + mission` (16 series of 4),
read by `ovr117:0000(series, mission)` into the data segment:

| block | bytes | what | to |
|---|---|---|---|
| 0 | 24 | header: 0, launch ship, landing ship, the player's ship, 8 wing ships (-1 ends) | `C340` (playerMissionShip `C346`, playerWing `C348`) |
| 1 | 1010 | 10 nav points of 101 bytes | navPointTable `7482` |
| 2 | 512 | 8 objectives of 64 bytes | `C05E` |
| 3 | 960 | 16 mission ships of 60 bytes | missionShipTable `7874` |
| 4 | 40 | the mission's name | `C034` |
| 5 | 40 | the series' name (one per series) | `C358` |
| 6 | 222 | unused in every mission looked at | `C262` |

**Nav point** (WC2.EXE `ovr116:1511` sets one up): name[20]; +0x1E
visible; +0x1F x, +0x23 y, +0x27 z (int32, km x 256); +0x2B a word
(15000 or 20000 everywhere); +0x2D a byte kept in `C5F8`; +0x2F four
(value, nav) pairs written to that nav's visible byte on arrival (FF
none); +0x37 steps of 3 bytes (`ovr116:1395`: type 0 "slot p1 flies with
ALTINT.p2", type 1 "scene p1, how p2" (0 pending and armed, 4 play now), FF
none; the third step's last byte is the first type word, so two steps);
+0x3F three ship type words to load here, +0x45 their kinds, +0x4B their
classes (copied from the SHIP record); +0x51 ten mission ship indices.
Up to three new types per nav point; the player's stays loaded.  **Memory:**
a fighter type (five-packet file) keeps all 37 views in memory, 70-120 KB
each; capital ships and bases (41-block files) load views on demand.  Two
new fighter types beside the player's Hellcat at one nav point ran the game
out of memory ("Error: ship.v07, packet 1 ... FREE: 9276"), so a mission
puts one new fighter type at a nav point and spends the other slots on
capital ships, fields and pods.

**Mission ship** (`ovr116:1CED` spawns, `19C3` sets up): name[19], +0x13
alternate ship (ALTSHIPS); +0x14 type byte, +0x15 class word (5 asteroid
field, 6 mine field, 0x2C and 0x3D fixed objects, else the SHIP record's
class); +0x17 side (0 Confederation, 1 Kilrathi, 2 scenery); +0x19 a flag
kept per slot (wingmen, escortees, carriers); +0x1B behaviour (2 a leader
placed here, 3 patrol, 4 in formation, 6 escortee, -1 stationary); +0x1D
scratch (the nav it spawned at); +0x1E/+0x22/+0x26 offset from the nav
point (int32, km x 256); +0x2A yaw, +0x2C pitch, +0x2E roll (degrees);
+0x30 formation index; +0x31 word x 256 into a per-slot dword (a field's
size minus 3000 km; ships 7..25); +0x33 a byte (4 fighters and capitals,
2..3 freighters); +0x35 orders (6 the player, 5 the player's wingman -- the
first Confederation ship with orders above 4 becomes the wingman slot --
4 usual, 2 and 3 on attackers); +0x37 nonzero: do not spawn again; +0x38
formation leader (mission ship, FF none); +0x39 group; +0x3A the ship the
wing is about (attacked or escorted); +0x3B comm personality
(COMMUNIC.Tnn: 0x24 Maniac, 0x11 a Confederation bridge, 0x19 the Imperial
Guard, 0x1A "Die, Terran coward!", 0x25 the Mandarins; 0 none).  A name
starting with `?` is shown as unknown.  A type above 56 is special-cased.

**Objective**: word kind, word parameter, 60 bytes of label (a label
starting with `.` is a designer's note).  Kinds: 0 reach nav, 1 land on
ship, 2 keep ship alive, 3 meet ship, 4 destroy ship, 5 clear nav of
enemies, 6 pick up ship (pods, pilots); -1 ends the list.

## 4. SHIP.Vnn and PCSHIP.Vnn

The fighter loader (`ovr115:07ED(type, class, kind, slot)`) reads
`ship.v%02d` as five packets: 0 a tree of the 37 view sprites, 1 a 3-child
table, 2 the per-view hardpoint words, 3 the 243-byte record (into
`dseg:157C + 0xF3 * slot`), 4 an 11-child table.  Kind 0xD and 0xE files
(capital ships, bases) are 41 blocks: 37 one-child trees, the 3-child table,
the record, the 11-child table, the words.  Academy stores fighters the
41-block way (and has an `xship.v` variant WC2 also knows); a fighter given
to WC2 in that layout reads a sprite as its record and asks for
`pcship.v-9`.  The record: name[20], word 20 kind (0xC/0xD/0xE), word 22
the type number (the cockpit file is `pcship.v%02d` of it), word 24 the
class.  An AI-flown type needs `intelshp.%03d`.  **Type numbers are not all free:** the
renderer's view choice (`seg006:25B1`: pitch band and yaw step from the
ship's orientation, then tables at `dseg:10C4` / `11BC` for the sprite and
its mirror flag) treats the record's type 0x30 and 0x31 (48 and 49, the
asteroid bases) by other rules, skipping the 90-degree roll correction of
the top and bottom views; a fighter numbered 49 flew with its top forward.
Other numbers the code compares against: 9, 12-16, 18-21, 28, 31, 32, 37,
44, 47, 51, 57-65.  The Hellcat is 45.  Cockpit files have 14
blocks (fighters), 16 (one turret) or 17 (Broadsword); the Hellcat's 16 are
kept.

**The record's numbers** (SO2.EXE `ovr115:003B` copies them into a new
ship; HCl's editor names the rest; `scripts/wc2data.py` REC): word 38 the
shield recharge rate, 42 the top speed in tens of kps, 44 the acceleration,
54/56/58 yaw, pitch, roll; words 223/225 the front and rear shields,
227/229/231/233 the front, rear, left and right armour.  At 62 a count byte, then up to
sixteen weapon mounts of ten bytes from 63 (the game copies the list as it
is into its per-ship gun table, `dseg:6A7A + 0xA1 * slot`): a word with the
weapon's code, then x (right), y (up) and z (forward) as signed words in the
ship's own units, a byte that is 0 for a weapon armed at launch (the guns
with 0 form the selected gun group, the first missile with 0 is the selected
missile) and a byte the loader fills with the weapon's type index.  The
bolt leaves from ship position + x * right + y * up + z * forward
(`seg006:188F`), so a pair wants x and -x: the Rapier's nose lasers sit at
(+-24, 36, 30), its wing cannons at |x| 104 and 152, the Sabre's at 192.
(An earlier reading of this list, a word of code << 8 | flags followed by
x, y, z, was off by a byte and put the Hellcat's guns on one side each.)
The code is the weapon's slot in the
type table (7 laser, 8 neutron, 9 mass driver, 10 particle, 15 Dart DF, 16
Javelin HS, 17 Pilum FF, 18 Spiculum IR, 19 torpedo, 20 chaff pod, 21
Porcupine).  The game's own: Rapier recharge 6, speed 45, turn 10/10/10,
shields 115/115, armour 75/75/65/65, 2 lasers + 2 particle, 2 DF + 2 HS +
2 FF, 2 chaff; Sabre recharge 9, speed 40, turn 8/8/8, shields 80/80,
armour 200/200/180/180; Morningstar shields 150/150, armour 200 all round.
The Hellcat's are `HELLCAT_STATS` in so3/campaign.py.

**Scenes and the cockpit picture.**  The comm scenes of a briefing draw a
cockpit from a `pcship.vNN` named in the scene file's shape list
(CAMPAIGN.S0n lists all seven flyable ones); the first mission's draws
`pcship.v01`, the Rapier's.  In SO3's copies that name is the Hellcat's.

**View sprites.**  Header four int16 x2, x1, y1, y2 (the sprite spans
[-x1-1, x2] by [-y1-1, y2] around its origin); records of int16 key, vx,
vy: key even = key/2 raw pixels, key odd = key/2 pixels of sub-runs (a
byte k: even = k/2 raw pixels, odd = one colour k/2 times); key 0 ends.
`wc2data.decode_sprite` / `encode_sprite` (lossless round trip on every
view the game has) and `scale_fighter` (the Hellcat at 0.75: 142 KB to 81
KB; the per-view hardpoint pixel positions in packet 2 and the eleven
debris sprites in packet 4 scale with it).  Portrait files (PEOPLE.Vnn)
are one table block of 39 such sprites.

## 5. The scene files (CSCP)

CAMPAIGN.S0n, GAMEFLOW.S0n, SERIES.S0n, INCIDENT.S0n: IFF `FORM CSCP`
blocks (big-endian sizes), one per scene set.  `FORM TEXT / SYMB` is the
strings, NUL separated, referred to by number from the bytecode (`6F nn 00`
pushes string nn); `FORM SPCH` maps lines to digitised speech (optional:
drop it and the lines are silent); `FORM SEQU` is the scene's own routines
(`Talk1..Talk6`) and the engine calls it imports (`talking`, `printit`,
`setupbackdrop`, ...); `FORM SCNE / Runner` dispatches a phase 0..5 to a
Talk routine.  `scripts/wc2scene.py SERIES.S02 [block]` prints each block's
skeleton: per Talk routine the room, who speaks each line (the talker id
set in field 0x15 before `closeup` / `film2`) and the branches.  Faces are
named in CAMPAIGN.S0n's SHAP list (`people.vNN`); renaming an entry in our
copy recasts a part.  SERIES.S02 block 0 is Special Operations 2's new-game
introduction (Talk1, strings 0-22), the first briefing (Talk2, 23-30) and
debriefing (Talk3, 31-38, with 39-41 a recording) and three radio lines
(42-44).  `\x86` in a string is the callsign, `\x85` the surname.  SO3's
first block keeps the script and replaces the strings; writing new scenes
means writing the bytecode, which has not been decoded.

## 6. Test rigs

`WCNET=0` flies a program alone (no server).  Scratch `w2solo.sh` runs one
instance headless with a key script (`!shot=name` for screenshots); the
mission test is `loadfix -34 so3 Origin -k l s1 m0`, the campaign from the
barracks `Origin -k s1 m0` with "Fly mission" at pointer 0.48, 0.55.
