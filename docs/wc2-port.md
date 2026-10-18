# Wing Commander II in the multiplayer layer: working notes

Status and findings of the port of `src/cpu/wcnet_*` to `WC2.EXE` (and later
`SO1.EXE`, `SO2.EXE`: section 8).  `docs/wcnet-multiplayer.md` is the design reference for
the Wing Commander 1 implementation; this file records what is different in
WC2 and how far the port is.  Addresses are in `scripts/wcexe.py` notation
(`-e wc2/WC2.EXE`); names collected so far are in `scripts/wc2.names`.

## 1. The executables

All five programs (WC.EXE, SM2.EXE, WC2.EXE, SO1.EXE, SO2.EXE) are Borland
C++ 1991 overlay programs with the same layout: MZ image, then an `FBOV`
block (overlay code, and the offset and length of the linker's segment table
inside the image).  `scripts/wcexe.py` reads that table, so no IDA export is
needed: `segNNN` is segment-table index NNN, `stubNNN`/`ovrNNN` an overlay,
`dseg` DGROUP (the paragraph the startup code loads into DX).

| | WC.EXE | WC2.EXE | SO1.EXE | SO2.EXE |
|---|---|---|---|---|
| segments | 178 | 240 | 240 | 207 |
| overlays | 40 (132..171) | 124 (105..228) | 124 | 93 |
| DGROUP paragraph | 1231 | 1976 | | |

A program loaded by DOSBox's shell with nothing else resident starts at
segment 01A2 (WC1's fixed `DS = 13D3` is 01A2 + 1231).  GOG starts WC2 with
`loadfix -34`, which moves it (to 0A6E here); the hooks therefore take the
load segment from `DOS_Execute` instead of assuming it.

**WC2 has to be started that way.**  Loaded at 01A2 it calls through a null
far pointer in some in-flight scenes and the CPU ends up running the
interrupt table: the scene after the first autopilot of series 2 mission 1
(a letterboxed picture of the sun, for ever), the start of 1-2, 5-2, 5-3
and 10-1 (a black screen).  It happens alone and without the network layer
as well.  With `loadfix -34 wc2 ...` all of them play.  The page's registry
entry runs it so; headless runs must too.

WC2 is the same engine grown: `scripts/wcexe.py -e wc2/WC2.EXE match wc/WC.EXE
<address>` finds the descendants of WC1's functions by instruction shape.
What moved:

| WC1 | WC2 | |
|---|---|---|
| `ovr143` ships, damage, weapons | `ovr114` | |
| `ovr143:0A99 do_damage(src, dst, qty, vec)` cdecl | `ovr114:1128`, **pascal** (arguments pushed left to right, callee pops) | |
| `ovr143:2978 fireGunFromShip` | `ovr114:3847` | |
| `ovr143:1F15 delayedDespawn` | `ovr114:2B69` (and `2B10`?) | |
| `ovr143:1273` player damage model | `ovr114:1CA9` | |
| `ovr145:115D outerSpawnShipEntity` | `ovr116:1CED` | |
| `ovr140:1C16 despawn` | `seg006:1AB7` (root image) | |
| `ovr168` asteroid and mine fields | `ovr134` (`0B2B` = in-field test) | |

Data seen in `do_damage`: entity type word array `dseg:64AA`, parent byte
array `dseg:6392`, shields `dseg:6680` (two words per slot), AI state
`dseg:6752` (9 = dying), damage points `dseg:6306`, ship type **byte** array
`dseg:4B3C`, ship statistics records of 0xF3 bytes (WC1: 0x6F), "player can
be damaged" `dseg:00C0`.

## 2. Running it headless

`loadfix -34 wc2 Origin -k l s<series> m<mission>` is the developers' mission test: the
cockpit within seconds, no story, and "You have landed." on the DOS screen
at the end (`seg001:0330` and `0349` are its mission start and end; the
hooks there do what the campaign loop's do, and a client loads the mission
the server names by writing it to `dseg:00D0` and the series to `00D2`).
Without the `l`, `wc2 Origin -k s<series> m<mission>` is the mission jump
(`ovr127:09A0` parses the command line: `Origin` enables the rest, `-k` makes
the player invulnerable, `s`/`m` pick the mission and start in the barracks).
The barracks is a different room from base to base, and the doors change
places: the game names the one under the pointer.  In series 1 (Caernarvon
station) a left click on the round door at the middle of the screen (pointer
at 0.48, 0.55 of the mouse range) is "Fly mission" and the airlock on the
left is "Exit to DOS"; on the Concordia (series 9) "Fly mission" is the
airlock on the left (0.12, 0.5), the terminal beside it "Save/Load game",
the middle door "Exit to DOS" and the hatch on the right "View storyline".
`-k` only makes the player invulnerable; the jump works without it.  The briefing conversation plays for about 110
emulated seconds at `cycles=8000` before the cockpit appears; Esc during the
opening sequence of a new game drops back to the title.

The key script works without the WC1 hooks when `WCNET_KEYSCRIPT_FROM=boot`
starts its clock with the program; `!shot=` screenshots work in any mode 13h
screen.

## 3. What works (two native instances, `wc2 Origin -k s1 m0`)

Stage 1 to 4 of the plan in section 10, for a mission the story flies with a wingman:

* **Tables.**  `src/cpu/wcnet_ds.def` has a WC2 column; `scripts/wcmap.py
  wc/WC.EXE wc2/WC2.EXE data` produced most of it by pairing functions and
  voting on the addresses in aligned instructions (`funcs` and `where` do the
  same for code), checked against memory dumps taken in flight.  70 entity
  slots (WC1: 64): right, up, front, position and velocity vectors are five
  consecutive arrays at `dseg:4D26`, `506E`, `53B6`, `56FE`, `5A46`.  Ship
  slots are still 0..9, the player is slot 0 and his wingman slot 1.
* **Hooks** (`load_wc2_code` in `wcnet_game.cpp`): frame loop `seg001:1BED`
  (top of the loop `1CA3`, status tests `1CF2`/`1D07`), the campaign loop
  around a flight `ovr128:02CE` (mission about to load `0486`, flight over
  `04C3`), `do_damage ovr114:1128`, destroy `ovr114:2B10`, fire
  `ovr114:3847`, spawn `ovr116:1CED`, nav point setup `ovr116:1511`, despawn
  `seg006:1AB7`, the per-entity AI dispatcher `ovr141:2CD3`, AI set speed
  `ovr114:0E5A`, autopilot `ovr107` (`0003`, `0667`, key at `seg001:1079`).
  The dispatcher, the session and the event code are shared with WC1; the
  differences are data (`GameParams`, `Loc.pascal`).
* **What was different.**
  * Many WC2 functions are pascal: `call_arg16(fn, n)`, `return_from_call(fn,
    ax)` and `GameCall(fn)` take the convention from the table.
  * WC2 loads a nav point's ship types when the player gets there
    (`ovr116:1511`); a spawn of a type that is not loaded returns a slot and
    creates nothing.  The server's spawns after an autopilot reach a client
    before its own nav check has run, so the client runs the nav point setup
    first (`NavSetupJob`), with the spawns and despawns nested in it answered
    as a client's own.
  * WC2 places a wingman by formation AI, not by a start offset.  The AI is
    off for a human's ship, so the client takes the position the server
    holds for its slot from the start state (this also fixed WC1, where a
    client used to start inside the host's ship).
  * Fighters get their AI from `ovr129:150E`, capital ships from `1F13`:
    the hook is on the dispatcher above both.  On a WC2 client the AI of the
    host's ship is off as well (it would fly formation on the client).
  * Mission ship records are 0x3C bytes (name 20 bytes, type byte at +0x14,
    class word at +0x15: 5 and 6 are asteroid and mine fields); nav point
    records 0x65 bytes with their ten ship words at +0x51.
* **Tested**: start in formation, positions and health in step, three Sartha
  at Nav 1 firing, damaging each other and dying on both machines, autopilot
  to Nav 1, a landing (both play the landing sequence and return to the
  barracks), a wingman's death (both get "You have died... Replay Mission").

## 4. Seats: missions the story flies alone

WC.EXE gives every mission a wingman, and the client flies his ship.  A third
of WC2's missions have none (the table below): slot 1 is then the carrier, a
transport to escort, or empty, and a client that took it for its ship would
"fly" a capital ship.  So the server decides, with the mission start state
(`Game.seat`, `ServerSession::seat_for`), what a client is:

* **Wingman** when the client's slot holds a fighter (entity type 12) whose
  mission ship is in the player's wing list: eight words at `dseg:C1B6`,
  after the player's own mission ship at `dseg:C1B4` (`ovr116:1C5C` tests
  both; `dseg:6E1E` has the mission ship of every slot).  "Recon", the Sabre
  that leads the last mission's escort, counts: the second player flies it.
  The client's slot is its network id (1 for the first client, 3 for the
  second), and since 2026-10-17 a client whose slot holds no such fighter
  takes the first slot that holds one no human flies
  (`ServerSession::reseat`, with the mission start state: the id goes to
  the client in it, and the other machines know the ship by the server's
  slot number as before).  Most of Special Operations' missions list the
  carrier first at the launch nav point, so their wingman is slot 2 and the
  second player used to be a drone or the gunner there; now it flies the
  wingman's ship.  A mission with two wing fighters (SO3P, `tools/wc2wing`,
  docs/so3.md section 6) seats two clients; every frame says which network
  ids humans fly (`Frame.humans`), and a client holds the AI off and
  computes no damage to any of them, the host's body and the other
  wingmen's alike (`ClientSession::is_remote_player_slot`).  The first
  wingman that spawns inside the leader is put on the left wing, the second
  on the right (`take_station`).
* **Gunner** when there is no such wingman and the leader's ship has turrets
  (the Broadsword's three, the Sabre's one); see below.
* **Drone** otherwise (`WCSEAT=drone` or `WCSEAT=gunner` on the server
  overrides, for tests).  A drone is a ship that exists on the client's
  machine only:
  * the server has no entity for it, so nothing sees, targets or hits it and
    the leader does not see it; `client_for_slot` answers only for wingmen,
    so slot 1 stays the ship the mission put there, with its AI and health;
  * the client flies the mission's own player ship (the cockpit the mission
    was built for), cannot fire (`on_fire_entry`) and takes no damage
    (`on_do_damage_entry`); it sends empty frames, and its own ending
    (ejecting, quitting) ends nothing but its own flight;
  * the leader's ship has to exist on the drone's machine, and no spawn of
    the server's makes one (a wingman gets it from the spawn of his own
    ship, `EntityMap::record_spawn`).  `HostBodyJob` spawns the player's
    mission ship a second time.  Two things in the record are in the way
    and are changed for the call: the game will not spawn a mission ship
    that is already in a slot (`ovr132:054A`; slot 0's entry in `dseg:6E1E`
    is hidden), and the AI word (+0x35) says 6, "the player", which is error
    024 for any slot but 0 (`ovr116:200D`; it becomes 4, an ordinary pilot);
  * it starts close behind the leader and rides there every frame
    (`ClientSession::follow_leader`: 420 lengths behind, 45 below, which
    puts the leader just above the gun sight), as the leader's copilot
    (below); `/chase` in the comms prompt lets it fly free, and back.
    Flying free it can go and look for whatever the leader cannot find.

  * it has the comms (`0`) and, flying free, another pair of eyes; it no
    longer sees through cloaks (below).

Still to come for this: a way for a drone to point something out (its target
shown to the leader).

### Cloaking

A ship with a cloaking device has it as the last "gun" of its gun table (type
0x3C; `ovr114:002F` tests for one, and the spawn hides it from the count).
Its state is a word per ship at `dseg:70A8`: 2 no device, 0 visible, 1
cloaked (not drawn, not on the radar, missiles locked on it give up:
`ovr141:16FA`), with a timer for coming back at `dseg:70BC`.  The AI cloaks
with `ovr133:0034(ship)` and uncloaks, to shoot, with `ovr133:0085(ship)`.

Every machine runs the AI, so every machine would cloak its own copies in
its own time.  The server's two calls are events (`Event.cloak`, sent when
the state really changes; the ships cloaked right now go into a start
state), a client's own calls are dropped at the function's entry, and the
client runs the function when the event comes (`CloakJob`).  A drone used
to skip the cloaking half and see the stealth fighters all along; since
2026-10-05 it sees what everybody sees (the user: with the leader's energy
in its hands, the drone was powerful enough).

Checked on series 5 mission 2 (three Strakha at Nav 1): the host's log and
the client's show the same cloak and uncloak sequence, and the state words
agree within a few frames.

### The gunner

Turrets in WC2 (`ovr136`):

* A ship's guns are a table at `dseg:67D4 + 0xA1 * slot`: a count, then ten
  bytes per gun (type word, mount x, y, z, a flag, the type again).  Type
  0x0B is a turret; `ovr136:11F9(ship)` counts them (`turret_count`).  The
  fire key's "all guns" (`ovr114:3EC3`) skips them.
* F2, F3, F4 put the player in a turret: `dseg:9389` (camera mode) is 4,
  `dseg:CD6A` the turret (Broadsword: 0 rear, 1 left, 2 right), `dseg:0B24`
  the view (3, 2, 1), and the stick moves the camera entity (slot 0x43)
  within limits.  The fire key is then `ovr136:0510` (`seg001:1490`): with
  turret energy (`dseg:CD5C`, 100 full, 24 a shot) and no cooldown
  (`dseg:9022`) it makes two bolts (type 8, parent ship 0) with the
  camera's orientation, from the camera's place plus a barrel offset
  (`dseg:90AC`, pair `dseg:CDA8`).
* Unmanned turrets fire by themselves: `ovr136:08A3(ship)` from the AI
  dispatcher, for the player's ship and for every other ship with turrets,
  a turret at a time (loop head `ovr136:08D8`, next turret `ovr136:11BF`);
  it leaves out the turret the player sits in.  It makes its bolts itself,
  not through `fireGunFromShip`, so it is not replicated: every machine
  runs it for the ships it sees and gets its own turret fire, which hits
  its own player's ship there and nobody else's.

The gunner's seat is built on that:

* On the gunner's machine the server's network id 0 is slot 0: its "own
  ship" is the leader's.  Position, flight and health of slot 0 come from
  the server every frame like any other ship's (its own steering is
  overwritten; hits on it are the server's to decide), the leader's shots
  are replayed from slot 0, and its own fire key does nothing in the
  pilot's view.  Nothing is spawned for it and the server has no entity for
  it.
* Its game is still WC2 with a turret ship: F2/F3/F4 work, the turret
  sights move, the fire key calls `ovr136:0510`.  The hook at
  `ovr136:0536` (the shot goes out) sends the camera's three vectors, its
  offset from the ship and the barrel pair (`Event.turret`); the server
  runs `ovr136:0510` itself with its camera standing there for the call
  (`TurretFireJob`; its own camera, energy and cooldown are put back).
  The bolts are the server's, from its ship: hits and kills are the
  leader's ship's.
* The gunner's frames say which turret it sits in (`Frame.manned_turret`),
  and the server's automatic fire skips that one (hook at `ovr136:08D8`);
  the others keep firing by themselves on both machines.
* The game starts every mission in the pilot's seat.  Eight frames into
  the flight the gunner's machine makes the call the F4 key makes
  (`enqueue_gunner_seat`): the gunner begins in the rear turret.
* An autopilot puts every view back to the pilot's.  The gunner's machine
  remembers the view it had when the autopilot's camera began
  (`dseg:0B24`) and, when the camera is back, makes the call the F2..F4
  keys make (`ovr141:0AC1(view, 0)`, after clearing `dseg:00DC` as they
  do): the gunner sits in the same turret again (`ViewJob`).
* Its ending (ejecting, quitting) ends nothing; the leader's death or
  landing ends its flight.

Checked (two native instances, `Origin -k l s2 m0` Broadsword and `s8 m2`
Sabre): the gunner's rear and left turret shots appear on the host as the
same bolts from the host's ship while the host sits in the pilot's view;
the host's own turret energy is untouched; the landing ends both; the
gunner ejecting leaves the host flying.

Missions by seat (the direct mode, 25 s in; the ship is the one the player
really flies: the mission's label for it can say otherwise, 9-0's
"Broadsword" is a Ferret).  Pilot ids up to 14 are the story's wingmen: 1
Angel, 3 Hobbes, 4 Stingray, 6 Jazz, 9 Doomsday, 11 Shadow, 14 Spirit.

| series | ship | wingman missions | gunner | drone |
|---|---|---|---|---|
| 1 | Ferret | 0, 1, 2, 3 (Shadow) | | |
| 2 | Broadsword (0), Ferret | | 0 | 1, 2, 3 |
| 3 | Rapier | 0, 1, 2, 3 (Hobbes) | | |
| 4 | Broadsword | 0, 1, 2 (Doomsday) | 3 | |
| 5 | Epee, Sabre (3) | 0, 1, 3 (Spirit) | | 2 |
| 6 | Rapier | 0, 1, 2, 3 (Stingray) | | |
| 7 | Broadsword, Ferret (3) | 0, 1, 2 (Angel) | | 3 |
| 8 | Sabre | 0, 1 (Jazz) | 2, 3 | |
| 9 | Ferret, Broadsword (2) | | 2 | 0, 1, 3 |
| 10 | Broadsword | 0, 1, 2 (Doomsday) | 3 | |
| 11 | Rapier | 0, 1, 2, 3 (Stingray) | | |
| 12 | Sabre | 0, 1 (Jazz), 3 (Recon's Sabre) | 2 | |

Series 13 and 14 do not exist.  In the wingman missions of series 4, 5 (3),
7, 8, 10 and 12 the second player flies the wingman's own Broadsword or
Sabre.

### The copilot

A drone riding behind the leader (the default since 2026-10-05; `0` then
`/chase` lets it fly free, and back) is the leader's copilot, the user's
idea for keeping that player busy: energy management the game itself has
no keys for, and the cruising speed.  Its keys never reach its own game
(`wc_net_key_filter`, called by `KEYBOARD_AddKey`; the chat prompt takes
its keys before that, and the gunner's keys are left alone) but go to the
server as `Event.copilot` with the next frame:

| key | action |
|---|---|
| Up / Down | shields to the rear / to the front |
| Space (Fire guns) | shields into the guns, half from each side |
| Enter (Fire missile) | guns into whichever shield is lower |
| + / - | the leader's cruising speed: the server taps its own keys (`tap_key`) so the game does what it would for the leader |

The server moves the energy on its own ship's words (`curShield`,
`shieldMax`, `gunEnergy`; `apply_copilot`): a fifth of a shield's full
charge a press (12 of an Epee's 60, 23 of a Ferret's 115; the guns go to
100), capped by what the source has and the destination can take, and
logs what it did ("copilot BRAVO: shields to guns: guns 40, shields
50/38"); nothing is drawn on either screen, the gauges show it (the user
wants the game to look as it always did).  The game's own recharge goes
on underneath, so the moves matter when the shields are down, which is
when they are wanted.  The drone sees the leader's readouts: riding along,
its own slot 0 (the mission's player ship, the same cockpit) gets the
leader's shields, armour, guns and damage every frame (`write_health`
from the leader's copy, in `populate_own_update`), and its place and speed
are the leader's already (`follow_leader`).  On a controller the D-pad's
up and down are the two arrows; A, B, L1 and R1 are the rest.

Checked natively (`Origin -k l s5 m2`, two instances) with the front
shield poked down and the guns fired empty: shields to the front 33/60 ->
53/40, shields to guns 20 -> 40, guns to the rear shield, + + - one speed
step up on the host and on the drone's gauge; and in two browser pages
(`scripts/web-wc2.sh 5/2 drone`).

With this the drone lost its view through cloaks (the user's call:
powerful enough).

Riding along, the drone's dashboard is dark: the emulator dims the browns
and greys of the lines from the dashboard's top down (the game's 3D view
runs on behind the dashboard, to line 135 in a Ferret and an Epee alike,
so the view's bottom is no guide: the top is measured per cockpit from
the video memory, `!vga`, as the first line from which most pixels are
the dashboard's -- Ferret 95, Epee 80, by the player's mission ship type
byte; another cockpit is dimmed from the view's bottom) and leaves the
saturated colours, so that only the gauges' lights stand out and the view
is the leader's ship (`wc_net_cockpit_dim`, `DimCockpit` in
src/gui/sdlmain.cpp: on the lines the renderer repainted that frame, which
keeps a line dimmed once; true-colour output only; `WCDRONE_COCKPIT=1`
keeps the game's picture).  The window's arch above the dashboard and the
pilot's legs (blue, a "light" by the rule) stay.

## 5. The story path: barracks, briefing, campaign record

The campaign loop is `ovr128:02CE`.  Between flights the game holds a
campaign record on the far heap (far pointer at `dseg:CF3A`; first word its
length in words; +4 series, +6 mission, +8/+0xA the scene, +0x12 the phase,
+0x9E a count and from +0xA0 the story's flags, e.g. who is alive): it is
what a saved game holds.  `ovr120:0793` is the barracks; its choices are 1
"Fly mission" (`ovr120:0800`), 2 the story scenes, 3 save/load, 4 exit.
Flying plays the briefing scene (`ovr146:02A4`, the series' script, which
picks the scene from the record), copies the record's series and mission to
`dseg:467F`/`4681` (`ovr146:0217`), saves and frees the record
(`ovr146:0414`) and loads the mission (`ovr128:0486`).

So the two machines meet at `ovr120:0800` (`code::flyMission`), before the
briefing: the server's briefing message carries its whole campaign record
(`ServerSendBriefingStart.campaign_state`) and the client overwrites its own
with it.  A client that came from another mission, or another saved game,
then watches the server's briefing, flies the server's mission and finds
the server's story in it (checked: host started with `s3 m1`, client with
`s1 m0`; both got Hobbes's and Angel's briefing and the Rapier mission).
The hook at `ovr128:0486` then only starts the flight's bookkeeping.  The
mission test (`l`) has no record and no barracks: the handshake stays at
`seg001:0330` there.

## 6. The browser page

`web/gamefiles.js` has both games' campaigns; the page's mission menu, its
hints and the way a picked mission reaches the game come from the registry
entry of the host's game (every hello carries the sender's game, and a
player with another game loaded is told).  A WC2 mission picked by the host
is `loadfix -34 wc2 Origin s<series> m<mission>` on every machine: everybody starts in
the barracks with the story at that mission and clicks "Fly mission".
Saved games are `GAMEDAT/SAVEGAME.WC2`; the speed is GOG's `cycles=8000`.

WC2's own comms display is not hooked, so chat lines (and the answers to
the `/chase` and `/rocks` commands) are drawn on the emulator's overlay in
flight too (`wc_net_overlay_chat`), for a time that grows with their
length.  Nothing else is: the seats and their keys were announced on the
screen at the mission's start until 2026-10-05, when the user asked for
the game to look as vanilla as possible; the page's lobby hint has them.
The game's one line of help, "To transmit comms use the '0' key", stays
out of flight and goes for good once a message has been sent from that
browser (the hooks log the sending, the page remembers it in local storage
and starts the game with `WCNET_NOHINT`), and the room form has a box to
turn it off before that.  The
overlay is drawn on the game's picture, of which the renderer only repaints
changed lines: a change of text asks for one full repaint
(`render.scale.clearCache`), or old text stays where the picture is still.

Checked with two headless Chrome pages and a local lobby (the scratch
`webwc2` test): the host picks 9/1 or 1/0 in the lobby, both click "Fly
mission", both reach the cockpit; in 9/1 the second page is a drone, types
`0`, `/chase`, Enter and rides behind the leader.

### The R key

`R` in flight plays the last seconds again from a recording
(`seg001:1036` calls `ovr137:0000`): this machine's game stands still for
it while the other's goes on, and the key sits among the ones a pilot uses.
The hook steps over the call.

### Speech

The game's sound setup (`wc2.cfg`: `v a904 c25 d1`) is a Sound Blaster at
220, IRQ 5, DMA 1, which is what GOG's DOSBox is set to (`irq=5`).  With
DOSBox's default IRQ 7 the first digitised line plays its first block and
the game waits for an interrupt that comes on another line: the
introduction stopped for ever a syllable into the Emperor's speech, and no
wingman ever spoke (`!where` in a headless run of "Start New Game": the
same two addresses of a wait loop from the Emperor's silhouette on, for as
long as one cares to look; with `irq=5` the scene plays through).  The
registry's `dosbox` text is written to a file the page starts DOSBox with
(`-conf`); a native run needs `irq=5` under `[sblaster]` in its
dosbox.conf.  (`config -set "sblaster irq=5"` as a startup command does not
do: re-initialising the card moves its `SET BLASTER` line to the end of
the startup commands, and the ones not yet run are skipped.)
`scripts/web-wc2.sh intro` starts a new game in a browser page and checks
that the audience goes on.

### Names

WC2 asks a new pilot for a first name, a last name and a callsign
(`ovr119:2340`) and prints them in its conversations from `dseg:9670`,
`dseg:9658` and `dseg:9640`; the saved record has its own copies
(`dseg:4629`, `:4642`, `:465B`).  A mission chosen on the command line never
asks, and the placeholders the executable ships with -- "FIRSTNAME",
"PCNAME", "CALLSIGN" -- were what people said.  The page has a first-name
field for this game alone (`firstName` in the registry), and the hooks keep
`WCFIRSTNAME`, `WCLASTNAME` and `WCCALLSIGN` in all six places
(`apply_pilot_names`, twelve characters as the game's own screen takes).

## 7. Finding out why the game quit or hangs

WC2 leaves through `ovr145:0038(message)` (thunk `stub145:0052`, 150 call
sites): it formats "Sorry, an error has occurred... %03d" and exits.  The
text is on the DOS screen only, so `game_program_ended` logs the text screen
when the game ends, and with `WCNET_EXIT_STACK=1` the stack as well: the
near pointer of the message (`push word 0x7f34`) is on it, and
`scripts/wcexe.py find` on that push finds the check that failed.

There are two more exits.  The resource loader's (`seg002:00D0`: a file's
packet could not be read, or there was no memory for it) prints "Error:
<file>, packet N", the packet's size, the largest free block and the free
memory when the game runs with the `Origin` switch, and otherwise formats
the long message at `dseg:0380` ("Sorry, an error has occurred while
%s...").  That message and the general one at `dseg:9FC0` are where the
hooks keep their call thunks and their trampoline (docs/wcnet-multiplayer.md,
"Running game code"): with a thunk's NUL in front, the game leaves without
a word once the hooks have run.  The page shows what text the game did
leave in its status line.

Error 007 at "Fly mission" (seen for series 9 mission 1, at once on the
click) is a game directory without `GAMEDAT/TEMPGLOB.000`: GOG's install
ships one, and a test archive made without the game's "temporary" files
does not start that mission (series 1 does).

`!where` in a key script logs CS:IP, the bytes there and the far return
addresses up the BP chain; a few of them a second apart show what a hang is
(the loadfix crash above showed as `0000:0000` with the interrupt table for
code).

### "Error: altships.25855, packet 1" on the wingman (2026-10-15)

Series 1 mission 4 (`Origin s1 m3`, the page's mission picker) killed the
wingman's game as the start state was applied, every time, with that line,
`packet size: -1` and about 139 KB free; the host flew on.  `WCNET_WATCH`
on the word the file name is made from found the writer: a DOS read
(`WCNET_WATCH` now logs the game's `int 21h` file calls too).

* `altships.%03d` is built from the game number at `dseg:C9AE` (0 for
  WC2.EXE, 1 and 2 for the Special Operations; set at the title and by the
  mission jump, used in every numbered file name: `module.000`,
  `planets.v00`, `startglb.000`, ...).  25855 is 0x64FF, two bytes of a
  file.
* The nav point setup (`ovr116:1511`) runs the nav point's three scripted
  steps (`ovr116:1395`, 3 bytes each from +0x37 of the record): type 0 is
  "the ship in slot N flies with the intelligence of file M": the 81-byte
  packet 0 of `ALTINT.00M` goes into the slot's intelligence record, 81
  bytes each from `dseg:C9EE`, the record's number from
  `[dseg:6E46 + 2*N]` (`ds::slotIntelRecord`; an empty slot has -1 there,
  `seg006:1B7A`).  Mission 4's step is slot 1 (Shadow) and file 1.  The
  game does not check the number: with -1 the packet lands at
  `C9EE - 0x51 = C99D`, over the startup variables `C99D..C9ED`, the game
  number among them.
* On the host slot 1 is Shadow's by then.  On the wingman's machine every
  slot but its own is empty when its nav point setup runs (its own spawns
  return -1, the server's ships come with the start state afterwards), so
  the record number is -1 and the packet trashes the data segment; the first
  spawn that needs an alternate ship (mission ship 0, `+0x13` set) asks for
  `altships.25855` and the loader exits.  The second caller of the steps,
  `ovr116:17F6` (a nav point's later phases: the "Meanwhile..." scenes and
  reinforcement waves), has the same hole for a slot that is empty when the
  phase fires.

The fix is a hook at `ovr116:1428` (`code::navStepAltIntel`): when the step's
slot is empty here (or not a ship slot), the step is skipped (`reg_eip` to
the block's `jmp`).  The intelligence record matters to the AI, which the
server runs; a wingman's copy of the ship flies by the server's positions.

### The mission's in-flight scenes, on both machines (2026-10-15)

With the crash gone, mission 4 showed the next thing: the host played the
Caernarvon sequence ("Stay in a parked orbit", Shadow's farewell,
"Meanwhile..." on the Concordia, the Admiral's new orders) right after its
autopilot, while the wingman's screen stood still; when the host came
back, the wingman's game played the same sequence, from its first line
("Well, Shadow, I guess it's back to life at Caernarvon Station..."),
while the host stood still -- the user's earlier "the wingman's game
takes the plain landing" was this scene, seen from its start.  How WC2
runs them:

* A nav point record's step of type 1 (`01 72 00` at nav 1 here: scene
  0x72, case 0) leaves the scene pending in `dseg:7D9A` (`ds::pendingScene`)
  and arms it (`7D9F`); the flight loop plays an armed scene forty frames
  later (`seg001:0580`, counter `7DA3`), and the autopilot's end plays it
  at once (`ovr107:01CA`).  `ovr114:2196` (called when the nav point's
  fighting is over) arms a pending scene too, plays it when `7DA1` says
  so, runs the nav point's later phases (`ovr116:17F6`: a record whose
  `+0x1E` is the phase number: its steps, its ten ships) and the "return
  home" scene `[4685] + 0x12C` when `9180`/`9182` are set.  The scene
  player `ovr148:00D3(scene, 1)` sets `missionStatus` from the scene's
  record (0x75 = landed), which is how mission 4 ends at the Concordia.
* The host's autopilot arms and plays the scene in one go.  The wingman's
  nav point is set up when the autopilot event reaches it (its scene
  armed, its count at zero), it runs the one or two frames its window
  allows after the host's last frame, and stands until the host's frames
  return; then its count ends and its scene plays.

So the host tells the wingmen: `code::playScene`, hooked at its entry when
called from the flight loop (second argument 1), sends `InFlightScene`
(wc.proto) out of band -- polled like chat, so a wingman waiting for a
frame still sees it (`ServerSession::on_in_flight_scene`,
`ClientSession::host_scene`).  A wingman with the same scene pending arms
it and sets its count to the last step, and its wait for the host's frame
lets one frame run without one (`scene_due`): both play the scene
together, each waits a moment for the other at the end, and the host's
own scene triggers stay the game's.  A comm answer is a scene as well; a
wingman has none pending and lets it pass.  A wingman whose own count
ends first (the host's and its own nav point setups a frame apart) plays
first and the host waits as before.

### A crash that was seen once (2026-10-05)

Reported from two browsers: the second mission of a session (series 3,
mission 2: the briefing in which Stingray scoffs at the freighter), the
whole briefing played with its speech, and one of the two games "crashed"
before the launch bay; the other flew on.  Ten more attempts did not show
it, and nothing here has: the same path (first mission, landing, barracks,
"Fly mission", that briefing unskipped, launch, cockpit) ran through in 5
two-instance native runs and in 6 runs of two browser pages, the two
clicks up to four seconds apart, and the first mission's
briefing-to-launch step in 4 solo runs at 7500 to 8500 cycles.  What that
step does: the briefing script ends, `ovr146:0217` copies series and
mission, `ovr146:0414` writes the campaign record to `GAMEDAT/TEMPGLOB.000`
and frees it, and the launch scene is loaded.  Checked and put aside:

* memory: the game's free heap (`!mem`, the loader's "MAIN") is 143067
  bytes at the first mission's start, 83315 in a fight at Nav 1 and 145311
  at the second mission's start, the same on host and wingman: nothing is
  lost between missions on either side;
* the campaign record the wingman takes over (same length or it is not
  taken), the names written into the game (inside their buffers), the
  hooks' periodic call (it runs no game code outside the flight loop);
* DOSBox's settings: `-conf` with only `[sblaster] irq=5` changes nothing
  else.

What stands out is that the speech was new that day (the IRQ had just been
put right), that the game's own messages were being wiped by the hooks, and
that the game is known for crashes that depend on how memory lies (the
reason for `loadfix`) and for trouble with its speech pack on real
machines.  The next time, the page's status line and its log say what the
game said.

## 8. The other builds: SM2.EXE, SO1.EXE, SO2.EXE

The Secret Missions 2 is WC.EXE built again (SM2.EXE: the same 40 overlays,
three segments fewer, the data segment's upper half 0x2E6 to 0x2EE further
up), and the two Special Operations are WC2.EXE's (SO1.EXE: the same 240
segments, most data ten bytes further up; SO2.EXE: 207 segments and 91
overlays, the game's overlays one number down).  Nothing in the multiplayer
layer is written for them: `g_game` is still one of two games, and
`g_build` picks the addresses.

* `src/cpu/wcnet_ds.def` has a column per build, and
  `src/cpu/wcnet_ports.h` their code places (`load_sm2_code`, ...).  Both
  are written by `scripts/wcport.py sm2|so1|so2|all --write` from the base
  build's column and table: a data offset by `wcmap.py`'s votes (every one
  unanimous but for the names, 5 or 8 votes to 1), the texts used as scratch
  space and the steering tables by their contents, the random generator's
  seed by the generator's code, and SM2.EXE's pilot record and VDU text by
  hand (`MANUAL` in the script says how); a code place is the instruction
  aligned with the base build's, and the script's report shows both
  instructions side by side.  All of them were the same instruction.
* `kGames` in `wcnet_game.cpp` has the five programs with their DGROUP
  paragraph and a text that tells the build ("Loading WC2 - SPECIAL
  OPERATIONS 1").
* The mission test's words `dseg:00D0`/`00D2` are in the table now
  (`directMission`, `directSeries`: the same in all three).

**Campaigns (WC1).**  WC.EXE and SM2.EXE know three campaigns: 0 Vega, 1
The Secret Missions, 2 Crusade.  A word holds the campaign to fly next
(`pendingCampaign`; the game's own `Z<digit>` switch sets it, and so does a
loaded game) and `runHangarMission` takes it over; the campaign's file,
CAMP.00n, has the table of series (`campaignTable`), which the program
loads once when it starts -- WC.EXE Vega's, SM2.EXE Crusade's -- and again
with a new game or a loaded one (`ovr161:03A3`, `ovr150:08FE`).  SM2.EXE
left alone, as the mission jump (`MIS`/`SERIES`) leaves it, would fly
Vega's missions by Crusade's table: the jump now sets the next campaign to
the one whose table is there (`GameParams.campaign`; `CAMPAIGN` in the
environment names another, without its table).  Not done: the campaign
does not travel with the briefing, so from the barracks everybody has to
be in the same campaign already (a new game in SM2.EXE is Crusade, in
WC.EXE Vega), and The Secret Missions' own missions have no entry in the
page's menu.

Crusade is nine series of two missions (8 and 9 are its two endings; 10
does not exist): Firekka (1, 2, 3, 5), Corsair (4, 6, 7), Charon (8, 9),
read from each mission's own record in the running game (the system's name
is at `dseg:985E` in SM2.EXE, `dseg:9574` in WC.EXE).  Every mission has a
wingman.  Series 4 and 5-1 are flown in a Dralthi.

Each Special Operations is five series of four missions (`Origin s<1..5>
m<0..3>`; series 6 gives the loader's error).  Seats, from the host's log
of every mission flown by two in the direct mode:

| | series 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| SO1 | Stingray, drone, gunner, gunner | drone, drone, gunner, gunner | gunner, gunner, Hobbes, Hobbes | gunner x 4 | gunner x 4 |
| SO2 | drone, drone, gunner, gunner | gunner, Stingray, gunner, gunner | gunner, drone, drone, drone | drone x 4 | gunner, gunner, Maniac, drone |

(The Crossbow has a turret, the Morningstar has none.)

Checked with two native instances each: SM2.EXE, Crusade 1/0 (briefing,
launch, both ships in step, the host's 107 shots on the wingman's machine,
autopilot to Nav 1 and the Dralthi there, the comms' "ICEMAN: Ready to
engage"); SO1.EXE 1/0 (the wingman's seat, guns both ways, autopilot,
landing ends both) and SO2.EXE 1/0 (the drone's seat, its copilot keys,
landing); and WC.EXE's and WC2.EXE's own checks again.  Running several
pairs at once in one game directory gives the game's error 017 now and
then (they share its temporary files).

In the browser: the room's code names the program (`SO1-4821`), and the
page picks it from the Wing Commander II directory.  Checked in two pages
by the story path, both as the drone's copilot: `GAME=so1 scripts/web-wc2.sh
1/1 drone 0.48,0.55` and `GAME=so2 scripts/web-wc2.sh 1/0 drone 0.48,0.55`
(in the Special Operations' barracks the middle door is "Fly mission"; the
airlock, which flies in the Concordia's room of the game, is "Exit to
DOS", and the terminal on the left "Save/Load"), and The Secret Missions 2
with `ROOM=SM2-7001 scripts/web-smoke.sh 100 0 1` (the server's copy of
Wing Commander's directory; the code picks SM2.EXE).

**Special Operations 3.**  A fan campaign on SO2.EXE's program with its
data-file number changed (SO3.EXE), the Hellcat V as ship type 45 and our
own missions: `so3/README.md`, `so3/story.md`, and `docs/so3.md` for what
was learned of the data files (MODULE, SHIP, the scene scripts) to do it.
The hooks list SO3.EXE in `kGames` with SO2.EXE's tables, and SO3P.EXE, the
same campaign as data set 4 with a seat for a third player in every mission
(`tools/wc2wing`; three native instances: `so3/rig/w2trio.sh`; three
browser pages: `scripts/web-wc2.sh trio`).

## 9. Not done yet

Chat on the game's own comms display, the leader's own manual turret shots on a gunner's screen,
the automatic turret fire of other ships (every machine makes its own), the mission's
outcome for the debriefing (each machine scores its own; the next "Fly
mission" brings the client back to the server's story), cloaking, turrets,
torpedoes, tractor beams, in-flight conversations (the mission's own
in-flight scenes play on every machine together since 2026-10-15, section
7; a comm answer still holds the others while it plays).  In the
Special Operations only the first mission of each was flown by two; their
story scenes and the transfer of a pilot from WC2 were not looked at.

## 10. Plan

1. Game detection and per-game tables (this is also what Secret Missions 2
   needs): `DOS_Execute` reports the program and its load segment; `ds::` and
   `code::` become tables loaded for the game that is running; calls know
   their calling convention.  WC1 must keep passing its tests.  *(done)*
2. Map WC2's data segment and frame loop; mission start and end.  *(done)*
3. Two players in one WC2 mission: handshake, spawns, positions.  *(done)*
4. Fire, damage, despawn, health, autopilot, mission endings.  *(done)*
5. WC2's own features: missions the story flies alone (the drone and the
   gunner: done), in-flight scenes.
6. The browser build: game registry entry, mission menu.  *(done)*
