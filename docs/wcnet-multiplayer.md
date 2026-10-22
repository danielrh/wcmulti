# Wing Commander cooperative multiplayer (wcnet)

This DOSBox fork hooks the Wing Commander 1 executable (`WC.EXE`) so that two
pilots can fly a mission together: one machine is the server (the wing
leader, slot 0 of its own game) and the other flies the wingman.  This
document is the design reference for the code in `src/cpu/wcnet_*.cpp` and
records how the damage desync that motivated the rewrite was diagnosed.

Every game address below is written the way the IDA database (`wc.idb`,
exported as `WcMulti/wcmulti.idc`) names it, and can be reproduced with
`scripts/wcdis.py` (see "Reading the game" at the end).

## 1. Diagnosis: why damage was not coordinated

### 1.1 The game has two damage models, selected by slot

`do_damage(src, dst, quantity, vec)` is `ovr143:0A99` (reached through the
stub thunk `stub143:0084`, DOS `12D7:0084`).  Its flow:

1. `dseg:00BA` zero and `dst == 0` → the player is invulnerable, return.
2. Subtract the front or rear shield (`dseg:C430`, chosen by the sign of
   `dot(vec, front[dst])`, `ovr140:0924 vector_dot`).
3. Subtract the armor quadrant (`dseg:C85E`, chosen by `dot(vec, right[dst])`).
4. If armor is penetrated: with probability 1/100 destroy the ship outright
   (`delayedDespawn`, `ovr143:1F15`), otherwise apply *internal damage*
   (`ovr143:0EDB`) and if it returns 1 credit the kill (`ovr143:0A08`).

`ovr143:0EDB` starts with `or si,si / jnz`: **slot 0 takes the player path
`ovr143:1273`, every other slot takes the NPC path.**

| | Player model `ovr143:1273` | NPC model `ovr143:0EDB` |
|---|---|---|
| damage points per bolt hit | `max(quantity >> 4, 1)` | `max(quantity / 6, 1)` |
| per point | random system from a table, comm messages, HUD damage | random system 0..8 |
| instant death | system 2 from the front with probability 1/3, or 1/100 critical | system 2 from the front, always |
| hull | core hit points `dseg:C8B8` (4) → `missionStatus = 4` | `dseg:D22C` counter > ship max, or `delayedDespawn` inside `do_damage` |

So the same hit does very different things to "the ship in slot 0" and "the
same ship in slot 1".  In a two-player game each human's ship is slot 0 on
their own machine and an NPC slot on the other, and the old code replayed the
same `do_damage` call on both.  The server's copy of the wingman took NPC
damage and could be destroyed (from inside a replayed call, where the hooks
were inert, so the "ignore wingman death" logic never ran), while the client's
own copy took player damage.  Symmetrically the client's copy of the hero died
under the NPC model while the real hero was fine.  That is the "one player has
extra health" and "wingman explodes" behaviour.

### 1.2 Secondary problems found in the same pass

* The replay passed the shooter's *parent ship* as `src` even on the server,
  so the `entityType[src] == 8` (bolt) checks in `do_damage` never fired: no
  hit sounds, different taunt behaviour, and kill credit through a ship
  instead of a bolt.  (`ovr143:0BC6..0C1D`)
* Hooks were disabled for the whole duration of a replayed call, so nested
  `delayedDespawn`/spawn calls inside replayed damage were neither filtered
  nor synced.  Enemy deaths only stayed in sync because the RNG seed was
  replicated.
* The server still ran the wingman AI (`ovr163:160E`, dispatched from
  `ovr141:28D0`) for a human-flown slot: it fired guns (bolts that only
  existed on the server) and changed AI state on damage.
* Shield recharge (`ovr143:340E`) ticks on `frameCounter % rate`, so two
  machines with different frame phases drift by a point; nothing corrected
  that.
* The forwarded mission status was read after it had been zeroed, so a client
  landing told the server `Proceed`, and a client death reached the server
  while the client's own game was held in space by the status suppression.
* **Wingmen going KIA.**  The only write to the pilot-status array
  (`dseg:C260`) is `ovr144:02E9`, in a routine called solely from
  `outerSpawnExplosion` (`ovr143:1C37`): a pilot is KIA exactly when a
  piloted ship explodes.  The NPC-model damage above exploded the other
  human's ship copy, which is why wingmen came home KIA.  With ownership
  rules no machine explodes a remote player's ship, so the path is closed.
* **Missiles not locking for the wingman.**  A missile inherits the shooter's
  locked target from `dseg:C284[shooter]` at fire time
  (`ovr143:2C1F`, `fireGunFromShip`).  That byte is written by the shooter's
  own targeting code, so it only exists on the shooter's machine; the old
  code fired the client's missiles *on the server* using the server's byte
  for that slot (no lock), and the server's copy drives the missile.
  `WeaponFire.target` now carries the lock and the replaying side sets the
  byte before calling `fireGunFromShip`.
* **A wingman's own missile chased its target for ever (SO3 play, 2026-10-16).**
  A client fires by prediction (its own copy flies at once, the server
  replays the shot and its copy decides the damage), and the client swallowed
  every despawn the game asked for, its own missile's explosion included: the
  copy was meant to go when the server's despawn of the server's copy
  arrived.  When that word never landed (the net id taken again by then, a
  replay that spawned nothing), the missile flew on behind its target with
  nothing to end it, and the next launch found no slot ("I can't launch a
  second one").  Now a slot the client's own predicted fire filled with a
  missile (`g_predictedMissiles`) runs the game's despawns natively, explosion
  and removal, and drops the net id mapped onto it so the server's later
  despawn is ignored rather than landing on whatever took the slot.
* **A wingman's guns did no damage in a big fight (Gimle 3, 2026-10-17).**
  Two causes, both in the rule that the server decides every hit on an NPC
  with its own copies of things.  The server's copy of a wingman's shot
  leaves the server's copy of the wingman's ship when the shot's event
  arrives, up to a round trip after the wingman saw it leave, at a target
  the server has moved on by then: with a simulated 180 ms round trip the
  rig's wingman landed a third of the hits the host did from the same
  aim.  And a target far from the host (the one far enemy the keep-near
  rule leaves to a wingman) is one the host's game does not collide bolts
  with at all: the wingman hit it again and again and it would not blow
  up.  Now a wingman's own machine reports the hits of its own bolts, as it
  already reported its own rocks and mines (`report_local_hit` with the
  shooter named; `DamageJob::REPORTED` applies them with the shooter's bolt
  or ship as the source, so the kill is the wingman's), and the server's
  own copy of a wingman's bolt no longer does damage (it is for the eye).
  A wingman hits what the wingman sees it hit, as the host always did.
* **From the second mission on, the host ignored the wingman's movements
  (Gimle 2 flown and won, then Gimle 3; 2026-10-17, the user's logs).**  A
  regression of 029ec859 (2026-10-09): a wingman's position is applied only when its
  frame number is newer than the last one taken (`RemoteClient::lastPosition`,
  for the best-effort channel), and the host never cleared that number
  between missions while a wingman's frames start again at 1 with each
  mission (its epoch).  All through the next mission, until its count passed
  the last mission's, every position the wingman sent was "behind the
  times": the host's copy of its ship was the AI's (the item below), its
  shots left from wherever that copy pointed and did no damage, and a fresh
  browser, whose counters start at zero, had no such trouble.  The start
  state now clears `lastFrame`, `lastPosition`, their times and the link
  monitor for that client.  Related: `leftThisMission` was cleared only with
  the score after a landing or a death, so a wingman that had ejected or
  quit stayed uncounted through the next mission; a mission's start and its
  start state clear it now.
* **The host saw the AI flying the wingman's ship (same play-test).**  The
  WC.EXE AI hook (`ovr163:160E`) is the capital ships' routine; the
  fighters' (`ovr163:13CF`, from the same dispatcher) still ran for a
  wingman's slot on the server, turning the host's copy up to 40 degrees
  off the heading the wingman had just reported, every frame, between the
  wingman's updates (`player N's copy turned up to ... deg from its
  reported heading and sat up to ... km from its reported position` in the
  host's log with `WCNET_LOG=2`, every 5 s while either is large; around a
  fly-by the km are the game's own moving of the ship, not the AI's).  Both
  routines are held off a human's slot now (`aiFighterThink`; SM2 at the
  same offsets).
* The per-instruction hook read ~15 words of guest memory for every emulated
  instruction (`isExecutingOverlay` per hook).

## 2. Authority model

* **The server owns the mission.**  Spawns, despawns, the AI and damage to
  NPC ships happen on the server and are replayed on clients as events
  (`Spawn`, `Despawn`, `WeaponFire`, `Damage` with the RNG seed).
* **Each human's ship is authoritative on that human's machine**, because
  only there is it slot 0.  Damage to it is computed *locally* from that
  machine's own collision detection (victim side), never replayed.  The owner
  replicates the resulting `ShipHealth` snapshot every frame it changes (and
  every 30 frames regardless); everyone else writes the snapshot into their
  copy.  No machine ever runs `do_damage` or a despawn against a remote
  player's ship, at any nesting depth (`wcnet_events.cpp`,
  `on_do_damage_entry` / `intercept_despawn`).
* **NPC health is also replicated** by the server, applied after the frame's
  events have been replayed, so replay drift (recharge phase, RNG) is
  corrected every frame while cosmetic effects still come from the replay.
* Mission setup despawns every ship slot before spawning the mission; those
  calls run locally on every machine (they reset the slot's pilot to 0xff, its
  comm flags and cull status).  Only despawns of ships that exist are
  broadcast or, on a client, left to the server.
* **A ship far from the host but near a wingman stays.**  The game removes a
  ship more than 16000 km from the player's own ship (km: position units /
  256): the AI's wingman that has fallen that far behind (WC.EXE
  `ovr163:003D`; WC2: a friendly, `ovr129:004E`) and a ship with no target
  that has wandered off (`ovr163:0317`, `ovr129:036A`), each a
  `cmp ax,3E80h / jng` on the distance just measured.  On the host that
  distance is from the host's ship alone, so with the two pilots far apart
  the host's game took units from under the wingman's nose (and said so to
  everyone).  The hooks `farShipTest1` / `farShipTest2` sit on those
  compares on the server, and one such ship stays, by the user's rule
  (2026-10-15, after the reverted attempt below): an enemy within a laser
  blast and a half of a wingman's ship (4800 km x 1.5 = 7200 km;
  `WCKEEPNEAR=<km>` changes it, 0 switches it off), and of the enemies the
  game would so remove, the one nearest to a wingman -- the jump is taken
  for it as for a ship that is near.  A wingman can chase one unit down far
  from the host; everything else goes by the host's distance as the game
  has it, so the number of ships about stays the game's own.  Friendlies go
  by the game's rule (its wingman catches up by the game's own warp).
  Missiles need no such rule: the 9000 km compares in the missile ticks
  (`ovr163:1870`, `ovr163:19A0`) are a missile's distance to its own
  target.  Ships that are leaving (a retreat, a jump) go by other code and
  still go.  Clients never remove ships of their own (`intercept_despawn`).
* **Tried and reverted (2026-10-15): both humans as "the player" to an
  enemy's AI.**  The AI approaches, engages and aims at slot 0, the player's
  ship, named by the constant 0 all over it, so with the two pilots apart the
  units left the wingman for the host.  For half a day the server ran an
  enemy's AI tick with slot 0's place, speed and bearing swapped for a chosen
  human's (`AiAsJob`, a trampoline job around the game's own AI call, the
  human drawn at random per ship every eight seconds).  The user's playtest
  (McAuliffe 1 and 2, Enyo 2) had units ramming, units targeting each other,
  the wingman's hits not registering, ships that exploded on the host but
  stayed on the wingman's machine as untargetable ghosts, and missiles that
  vanished at launch.  The mechanism: inside that job the trampoline is
  running, so every hook that puts a job in place of a game call
  (`run_instead_of_current_call`: an enemy's shot, its death, its removal,
  a hit) was refused -- "Trampoline re-entered while running; ignoring", 43
  times in the first minute of McAuliffe 1's first fight on the rig -- and
  the game's call ran on the host alone, unbroadcast, while the job queued
  for it ran later against a slot that was already free.  Reverted whole,
  the WC.EXE AI hook included: it is back on `ovr163:160E`, the capital-ship
  AI.  (The per-entity dispatcher `ovr141:28D0` sends fighters to
  `ovr163:13CF`, missiles to `ovr140:13DD` and the guidance routines
  `ovr163:1870` / `ovr163:19A0`; the server runs the fighter AI for a human
  wingman's slot as it always did, its shots and speed changes blocked by
  the fire and set-speed hooks.)  The AI engages slot 0 only; the keep-near
  rule above is what lets a wingman fight far from the host.
* **Asteroid and mine fields belong to each machine.**  The game never has
  more than twenty rocks or mines: overlay 168 keeps them around slot 0 while
  it is inside a field (`ovr168:0B86` tests every 16 frames whether the
  player is within a field's radius, `ovr168:0AA7` then adds rocks ahead and
  drops the ones left behind), so there are not enough for two pilots and
  nothing about them is replicated.  A field is a mission "ship" of type
  0x16 (asteroids) or 0x17 (mines): `outerSpawnShipEntity` only adds it to
  the nav point's field table (`ovr145:1183` -> `ovr168:0B40`, table at
  `dseg:CDF6`, count at `dseg:BFF6`).  Every machine sets a nav point up for
  itself when its own ship gets there (`seg001:2029`, by distance, calling
  `ovr145:098F`), which empties that table first, so the spawn hook lets
  field "ships" through on every machine instead of broadcasting them.
  Before, a client replayed the server's field spawn and then wiped it with
  its own nav point setup, whose spawn calls are suppressed: only the host
  ever saw a rock.  (The server also sent the garbage that call returns in
  AX as the field's "slot", and stamped a pilot byte from it.)
  What the rocks do is decided where they are:
  * a player's shots break that player's own rocks (bolt against rock never
    goes through `do_damage`: `ovr141:22CD` breaks it one time in three);
  * a rock or a mine blast hitting the machine's own ship is ordinary local
    damage to slot 0, replicated as that ship's health;
  * nothing a machine's rocks do touches another human's ship there;
  * a client's rock or mine hitting a ship the server owns is *reported*:
    the client's `do_damage` hook sends a `Damage` event (target, quantity,
    direction) when the source belongs to no ship, the server applies it
    with no source (`DamageJob::REPORTED`) and broadcasts it and any kill
    like its own.  The game only lets a rock it drew that frame hit a ship
    (`ovr141:2411` removes an unseen one quietly), so a report always is a
    collision that player watched.
  The server's AI runs where only the host's rocks exist, so an enemy near a
  wingman cannot steer around that wingman's rocks.
  A rock is as deadly as the game makes it, alone or not: a collision does
  `(closing speed)^2 / 2` (`ovr141:25AB`, speeds in units per frame, 250 kps
  being 30), a new rock is given 52 to 75 units (`ovr168:040F`, a table
  indexed by `dseg:BFF4`: 56 in the Hornet of Enyo) plus up to 15 less a
  random part, and a Hornet's front shield and armor are 85 together, so one
  head-on rock (1800 and 2592 were measured, the first with networking off)
  kills outright.  What multiplayer adds is that both pilots
  now fly through rocks and either one's death ends the mission for both.
* **Soft rocks** (`WCROCKS=soft`, `/rocks soft`, mode 2): a rock that hits
  the machine's own ship does a sixteenth of the game's damage and never
  more than 80 (the `do_damage` hook rewrites the quantity on the stack), so
  one rock takes the shield and most of the armor of a Hornet but cannot
  kill a fresh ship from any side; later hits get through a point at a time
  (a collision's damage points are `quantity >> 7`).  Mines and everything a
  rock does to other ships are unchanged.
* **Bumps between wingmen are harmless** (`WCWINGBUMPS=game` for the game's
  damage; the lobby's "Collisions" menu, which pairs it with the rocks;
  `/bumps harmless`, `/bumps game`; 2026-10-17, the user's wish: a brush
  with the wingman lost missions).  In `do_damage` every machine answers
  for its own ship: when the source is a ship (not a bolt, a missile or a
  rock) and both are humans' ships, the damage is not taken.  Guns,
  missiles, rocks and ramming anything else count as before.  The host's
  choice travels with the briefing start and the frames like the rocks.
* **The host can switch the fields off** (`WCROCKS=0`, the lobby's
  "Asteroids and mines" menu, `/rocks off` in the lobby chat or in the comms
  prompt; `Session::request_rocks`).  Off, the spawn hook answers a field
  "ship" with no slot on every machine, so a nav point has no field and the
  autopilot flies through.  The server's choice travels in the briefing
  message (before a client sets up its first nav point) and in `Frame.rocks`
  with the start state and whenever it changes in flight; at that frame each
  machine runs the game's own `ovr168:01AE` to drop its rocks, or registers
  the current nav point's fields again (`outerSpawnShipEntity` for each field
  ship of `dseg:C178`'s nav point).
* A hit or a kill names its shooter only when a ship is behind it; a rock's
  or a mine's slot number means nothing on another machine.
* **Cockpit damage is the player model's own and needs no replication.**
  Once a hit is through shield and armor, `ovr143:1273` turns what is left
  into damage points (`>> 4` for a bolt, `>> 5` for a missile or a blast,
  `>> 7` for a rock or a ship; at least one), rolls a system for each from a
  table chosen by the kind of hit and its side (`dseg:0CB8`), and, when the
  hit was worth more than one point, breaks one of the cockpit's four
  damage spots at random (`ovr134:325E`, flags at `dseg:8CB2`, sprites per
  cockpit: burn marks, a torn panel, sparking wires, cracks by the radar).
  The nine systems' levels are at `dseg:D24C`; a damaged computer turns
  both displays to static.  So a laser, which leaves under 32 after the
  armor, never marks the cockpit, and heavier guns, missiles and collisions
  do.  Checked alone, as host and as wingman with the same scripted hits
  (`!hit=` in the key script): the same flags and the same sprites.
* The server skips the AI think function for a human-flown slot and refuses
  AI fire from it; the set-speed AI is skipped as before.
* Positions of a player's ship come from its owner every frame; the server
  broadcasts all positions.
* **Pilot identity of a human's ship.**  The comms code names and draws a
  speaker from the ship's pilot byte (`dseg:D1A2`): 0-7 are the eight named
  wingmen (name, face, phrase table and the talk-to-wingman menu index), 8 is
  the player, 9-12 enemy aces.  A player's own ship carries 8, which names
  nobody on another machine, so health snapshots never overwrite that byte
  for a human's ship (`write_health(..., keepPilot)`): on the server the
  client's body keeps the pilot the mission setup gave the wingman slot (the
  mission's wingman).  The replayed spawn of an NPC on a client gets the
  pilot the server reports for it (`Spawn.pilot`, stamped when the spawn
  event leaves, after the mission setup has run), and so does the client's
  body for the host in Wing Commander II.  Before this, both machines saw
  "(null)" with a random face and a garbage orders menu for the other human.
* **The leader on a wingman's machine** (`wcnet_events.cpp`, under that
  heading).  What the server reports for the host's own spawn is the
  mission's wingman, the very pilot the client's player is, and the game
  takes any ship with a named pilot for its own wingman: Angel's targeting
  computer said "Target: ANGEL" of the leader, and the leader's ship said
  Angel's lines.  So in Wing Commander a client's body for the host wears
  Iceman (pilot 3; `WCHOSTPILOT=0..7` picks another), the game's own lines
  from that ship are dropped (the hook on `showCommMessage` lets only the
  chat's own call through), Iceman's name is the host's callsign for the
  flight (`WCHOSTCALLSIGN`, which the page takes from the room's roster;
  without it the callsign comes with the host's first typed line) and is put
  back when the mission ends, and the renderer paints the name off the
  helmet of the comm picture while a line of the host's is on
  (`WCHELMETNAME=1` leaves it).  Wing Commander II has the callsign only: it
  is written as the name of the ship's mission record, which the targeting
  computer prints.
* **Shared fate.**  Any player's ending (landed, died, ejected, quit) ends
  the mission for everyone with that same status.  A client reports its own
  ending with `PlayerEnd`; the server adopts the status, its main loop exits,
  and its `MissionEnd` frame carries the status to every other client.
  Nobody's ship is despawned for this, so no pilot is ever marked KIA by it.
* **After a death the same mission is flown again.**  `runHangarMission`
  returns nonzero after a landing or an ejection (the game's main loop then
  calls it again: rec room, barracks, next briefing) and zero after a death,
  the loss of the carrier or a quit, which sends the main loop back to its
  title call.  In a forced-mission game (`MIS` / `SERIES`, the lobby's
  mission menu) that title call is where the hook starts the mission
  (`afterStartup`), and it now starts the mission most recently flown on that
  machine rather than the one the session began with; `run_campaign` returns
  into the main loop's own test (`afterHangarMission`), so the first mission
  behaves like every later one.  Both machines get there (shared fate), the
  client asks for the briefing and the server answers when its own restart
  reaches the mission start.  Before, a death in the first mission dropped
  into the game's new-game routine, which begins in the simulator, and later
  deaths restarted the session's first mission.  A game started from the
  barracks (no forced mission) keeps the original behaviour: back to the
  title, and the save games.
* **Funerals.**  The scripts hold a funeral for each named pilot whose word in
  `dseg:C260` (eight words, `series * 4 + mission` of his death, 0 = alive)
  names the mission just flown.  Both sides zero all eight at every mission
  start and end (`liven_everyone`; it used to leave the last two and a half
  untouched), so nobody is mourned who is alive on another machine.
* Clients fire their own guns immediately (`FireJob::PREDICT`) and tag the
  event with `client_seq`; the server replays it, and the echo lets the client
  map the missile slot instead of firing twice.
* **A shot carries the gun it came from.**  A ship's guns are a table
  (`dseg:C472`, 0x33 bytes a slot: a count, then five bytes a gun), and the
  game takes entries out of it: a launched missile's (`ovr143:0110`), and a
  random one when a hit damages the weapons (`ovr143:10C6`).  The machines do
  not always lose the same one, and from then on gun 1 is a laser on one and
  a missile on the other: a wingman's machine launched a missile for every
  laser shot of a Dralthi, missiles nobody else had and nobody removed
  (the count went below zero and the launches went on from stale entries).
  `WeaponFire.gun` is the shooter's record for that gun and
  `WeaponFire.guns` its count; the replaying machine puts both in place
  before the call.
* **The autopilot's fly-by is not a replay of anything.**  The client runs
  it on the trampoline (`AutopilotJob`), and what the game does meanwhile
  used to be taken for part of a replayed event and left alone.  The nav
  point setup the client's game runs when it arrives was one such thing: its
  spawns went through natively, the server's spawns for the same ships then
  found the slots taken (`spawn produced slot 65535`), and nothing the
  server said about those ships afterwards reached them -- on Gimle 2 the
  host shot down four Dralthi that stayed on the wingman's radar and came
  along to the next nav point.  `g_cinematic` marks the fly-by, and inside
  it the client's game is answered as at any other time.
* **A campaign begun in the middle of a series.**  The game adds a series'
  victory points up (`dseg:C280`, `compute_victory` at `ovr161:01E1`) and
  takes the winning path when the sum reaches the series' threshold.
  Starting at Gimle 2 left Gimle 1 at nothing, and two won missions were a
  lost series.  The missions before the first one flown count as won: their
  objectives' points (`dseg:9A88`, the campaign table) are added when that
  first mission is scored (`credit_skipped_missions`).

## 3. Frame exchange

At `seg001:20E3` (top of `main_loop`) the client sends its `ShipUpdate`
(+ events, + health) and waits for the server's next frame; the server takes
in what the clients have sent, merges it, and sends one frame to each.
Events are replayed through the trampoline before the game simulates the
frame; the client applies received health snapshots when the trampoline
drains (`on_trampoline_idle`).

The original design was strict lockstep: the server blocked for one message
per client and the client blocked for the server's answer, so every frame
cost a round trip and both emulators stood still while they waited.
Measured with a simulated delay (`WCNET_LAG`, section 3a): a 50 ms round
trip gave 12 frames a second with the host's emulator at 85% of real time
(its music with it), 80 ms gave 9 and 61%.  Now:

* **The server does not wait for a client's frame.**  It drains what has
  arrived (`drain_client`) and goes on.  Every client frame carries
  `Frame.ack`, the number of the last server frame the client had applied;
  only when a client is more than `kWindow` (6) frames behind does the
  server wait for it (`ClientLagJob`), so a client that has stopped still
  stops the game instead of being left behind -- however long: a client
  in flight that is in a scene of its own (a comm answer, a mission's
  cutscene) comes back, and a host that ran ahead would part the games (a
  time limit was considered and refused on 2026-10-14).  What the server
  waits for is a client *in the flight loop*: a client whose loop has
  returned (a landing, a death, the debriefing, the barracks, the DOS
  prompt) says so at once on the reliable channel (`Frame.flying` =
  false, from `on_mission_ended`, with its ending if the hooks' status
  checks missed it) and is neither waited for nor sent frames; a client
  whose game ends closes its link (`on_program_ended`); and once a
  wingman has had the mission's start state `exchange` no longer waits
  for a connection, so the host flies on alone when the last one leaves.
  The 2026-10-14 play-test froze a host in its cockpit when the wingman's
  game landed and went on to the debriefing while the host waited.
* **A wingman whose host is lost in flight flies on alone** (2026-10-15).
  A wingman's game that loses the server in the middle of a mission (the
  link failed, the host's game went away) closes the session (`drop`) and
  flies the rest of the mission by itself: its own copy of the game has the
  other ships and their AI.  No connection is sought until that flight is
  over (`g_aloneThisFlight` in wcnet_hooks.cpp, cleared when the flight
  loop has returned), and a failed attempt after that waits twice as long
  as the one before, up to five minutes.  In the browser the page owns the
  room: `wclobby_connect` adopts the page's live connection to the room
  (`Module.lobbyGame`, the connection or a function giving the one the
  page is in now) and never joins a room behind the page's back; a page
  that has left the room, or whose connection is closed, is a failed
  attempt.  The user's log of 2026-10-15 (Firefox, Enyo 2) showed the
  previous behaviour: after "lobby: player 0: link lost (failed)" the game
  adopted the page's closed connection again every few seconds, failed to
  send at once ("game is closed"), left the room and adopted it again, for
  the rest of the flight.
* **A wingman's landing is its own** (2026-10-14).  Its ship leaves the
  host's space (`enqueue_wingman_lost`, quietly) and the host flies on
  until it lands too; the games meet again at the next briefing, where
  the wingman takes the host's campaign record as always.  A wingman's
  death, ejection or exit still ends the mission for everyone (a wing
  lives and dies together), and the host's own landing still lands every
  wingman: a wingman's game cannot fly on without the host's simulation.
* **The host's landing reaches the wingmen at once, with the ship it
  landed on** (2026-10-15).  The mission-end frame of a landing used to
  wait for the victory computation, to carry the score in the same
  message; that runs after the host's landing animation and debrief, so
  the wingmen sat in their cockpits for twelve seconds and landed while
  the host was being debriefed.  Now the frame goes out as the flight
  loop returns, for every ending, and the score follows on its own to
  every connected wingman (`on_mission_victory_calc` on both sides; a
  mission end without a status is the score alone).  WC.EXE's landing
  sequence draws the carrier from the game's "landed on" word
  (`dseg:662E`, which only the game's own landing check `ovr141:1841`
  writes): a landing by the host's word left it -1 and the Tiger's Claw
  was missing from the wingman's landing.  `MissionEnd.landed_on_plus_one`
  carries the host's ship as a network id and the wingman writes its own
  slot for it (`ds::landedOn`); the sequence then shows the bay from
  wherever the wingman is.
* **The client waits for the server's frame without blocking**
  (`ServerFrameJob`), and applies every frame that has arrived, in order.
* **Both waits pass in emulated time.**  A waiting job makes a call that
  returns at once with the emulated CPU idle to the next timer event
  (`idle_call`, what `HLT` does) and queues itself again: interrupts,
  music and the clock run on, and in a browser nothing is suspended.

With that the same test held 15 frames a second with both emulators at
100% of real time up to a round trip of about 300 ms (six frames: at 20
frames a second, the default since, that is 250 ms).  What the delay costs
now is what it must: the server sees a client's ship and shots that much
later.

## 3a. Frame rate

Wing Commander 1 does everything per frame and never waits for a clock: 5120
units of flight per frame at any frame rate.  At the page's 3630 cycles an
empty sky ran at 20 frames a second and the fight of Gimle 1 (four Raptors,
three Jalthi, four Salthi, the Exeter) at 10: half speed, with the host
keeping up effortlessly.  More cycles alone make the empty sky too fast
(8000 cycles: 48 frames a second).  WC2 waits for 1/15 s a frame by itself.

So in flight (`wcnet_perf.cpp`):

* the emulated CPU gets `WCFLIGHTCYCLES` (default 16000 for WC.EXE, whose
  heaviest frames are 400 to 660 thousand cycles of work and have 50 ms;
  12000 for WC2, which holds its 15 frames a second with GOG's 8000 cycles
  in an ordinary furball with little to spare) instead of the configured cycles, and its own back when no flight frame has begun
  for 300 ms (cutscenes, the autopilot's camera and the barracks take
  their speed from the CPU and keep the old one);
* a frame that is done early waits for its turn (`PaceJob`, in emulated
  time as above): `WCFPS` frames a second, default 30 for WC.EXE (it was
  15 at first, then 20; everything in the game moves per frame, so this is
  also how fast it plays, and the page's FPS box lets the host fly a
  mission made for a slower game at any whole number from 8 to 60; the
  box starts at 20), 0 for none.  Above 20
  the flight's cycles grow with it, 800 a frame (24000 at 30).  Only the server (or a lone player) paces; a client follows the
  server's frames.

The Gimle fight then holds 15.0 frames a second in every interval, alone
and with two players, natively and in the browser, with the game busy 35 to
50% of the time.

`WCNET_PERF=<seconds>` logs a line per interval; the page shows the same
under the game's picture (`wc_web_perf`):

    perf: 15.0 fps, worst frame 68 ms, load 41%, emulator at 100% of real time,
          0.1 ms/frame waiting for the network, 12000 cycles, 7 ships, 27 other entities

The hooks' own cost matters in the browser, which has only the interpreting
CPU cores: the test "is this instruction one of ours" is made in line by
the cores (`wc_net_cpu_hook`, a table lookup) and only a watched address or
every thousandth instruction calls out.  As a call per instruction it took
about a quarter of the wasm build's speed (measured unpaced at 40000 to
100000 cycles: 30 to 66 thousand cycles a millisecond delivered before, 40
to 83 thousand after, in headless Chrome on two cores).  The paced game
needs about 5000 on average in its heaviest fight.

A load near 100% means the game needs more cycles in flight; an emulator
below 100% means the host cannot deliver the cycles asked for; a wait on the
host means another player's machine or connection is the brake (a wingman
in the low-latency mode always waits: it follows the host's pace).

## 3b. Far-away players: the simulated link and the two exchange modes

Players on other continents were not satisfied, and what they had was not
measured.  So first a link can be made bad on purpose, on one machine, from
the environment (the page's `?env.NAME=value` sets them too, so a browser
can try it): `WCNET_LAG=<ms>` holds every message that comes in back that
long (one way: with it on both machines the round trip is twice that);
`WCNET_JITTER=<ms>` adds a random delay of up to that much to each message,
and a message cannot overtake the one before it (the link is ordered), so a
late one holds the next ones back; `WCNET_DROP=<pct>[:<ms>]` loses that
share of the messages in transit, and since the link is reliable a lost
message is sent again and it and everything behind it arrive `<ms>` later
(default: a round trip plus 150 ms, what the fast retransmit of SCTP or TCP
costs once the other side has noticed the gap from the three messages
behind it at twenty a second); `WCNET_BURST=<periodMs>:<lossMs>[:<spikeMs>]`
loses the messages of `<lossMs>` every period and holds those of the
`<spikeMs>` after them up that long, a queue draining, which is what a
satellite handover does (Starlink's come every 15 s, by the wall clock on
both machines at once, so the burst goes by the wall clock too);
`WCNET_SEED=<n>` picks the random sequence (fixed by default, so a run
repeats).  `wcnet_transport.cpp`, `Impairment`.

**What the low-latency exchange stood up to** (section 3; two native
machines in Enyo 1 with the enemies of its first nav point, both firing,
150 s, `WCNET_PERF=1` for a sample a second; the figures in the table below
are from those runs):

(the one-way delay is what each machine adds; a frame's round trip as the
host measures it is twice that plus some 50 ms of frame processing, so a
"100 ms each way" link reads 250):

| link | host fps (min) | wingman fps (min) | seconds under 15 (host / wingman) | dips under 10 per minute |
|---|---|---|---|---|
| none | 20.0 (20.0) | 20.0 (19.8) | 0 / 0 | 0.0 |
| 50 ms each way | 20.0 (20.0) | 20.0 (19.8) | 0 / 0 | 0.0 |
| 100 ms each way | 20.0 (20.0) | 20.0 (19.8) | 0 / 0 | 0.0 |
| 150 ms each way | 15.7 (14.5) | 15.7 (13.9) | 14 / 29 | 0.0 |
| 200 ms each way | 12.0 (10.4) | 12.1 (10.4) | 112 / 113 | 0.0 |
| 300 ms each way | 7.3 (6.9) | 7.6 (5.7) | 76 / 76 | 60.0 |
| 25 ms + jitter up to 50 | 20.0 (20.0) | 20.0 (19.1) | 0 / 0 | 0.0 |
| 25 ms + jitter up to 100 | 20.0 (20.0) | 17.5 (14.5) | 0 / 3 | 0.0 |
| 25 ms + jitter up to 200 | 15.1 (11.6) | 10.7 (5.6) | 50 / 106 | 22.9 |
| 25 ms + jitter up to 400 | 8.4 (4.8) | 5.7 (2.5) | 102 / 99 | 58.8 |
| 25 ms + 0.5% lost | 20.0 (20.0) | 19.5 (15.6) | 0 / 0 | 0.0 |
| 25 ms + 1% lost | 20.0 (20.0) | 19.2 (12.7) | 0 / 2 | 0.0 |
| 25 ms + 2% lost | 20.0 (16.3) | 18.5 (10.4) | 0 / 9 | 0.0 |
| 25 ms + 5% lost | 19.8 (16.0) | 16.7 (6.9) | 0 / 31 | 1.6 |
| 150 ms + jitter 100 + 1% lost | 10.5 (4.8) | 8.8 (3.5) | 106 / 104 | 40.0 |

So the exchange of section 3 keeps 20 frames a second to a round trip of
about 250 ms as the host measures it, and beyond that runs at six frames
per round trip (the window): 15.7 at 350, 12 at 450, 7 at 650.  Jitter
costs the wingman alone at first, a frame for every burst of host frames
(17.5 at 100 ms of jitter, with the host at 20), then both (200 ms: 15 and
10.7, with 23 seconds a minute under 10 on the wingman); and a lost message
freezes the wingman for the retransmit, about 200 ms here, which is within
the target to 2% and past it at 5% (a second under 10 frames a second more
than once a minute).  The three together as a far-away player might have
them (150 ms each way, jitter, 1% loss) gave 10.5 and 8.8.

**The two modes** (`ExchangeMode`, `wcnet_session.h`):

* *Low latency*, the exchange of section 3: a wingman's game waits for
  every one of the host's frames and the host runs at most six frames
  ahead of what a wingman has applied.  What a wingman sees is at most a
  frame old relative to what the host sent, and a wingman's shots reach
  the host a one-way trip later.
* *High latency*: a wingman's game paces itself (`pace_frame` with the
  host's `Game.fps`) and at the top of each frame takes in whatever host
  frames have arrived, none, one or several (`ClientSession::on_frame_top`,
  `MODE_HIGH`); it waits only when the host has been silent for longer
  than the round trip and its spikes can explain (`silence_ms`: the worst
  round trip of the last five seconds plus a second), so that a host that
  stopped stops the wingman too.  The host's window grows to what the
  round trip needs plus a second (`highWindow_`, between one and five
  seconds of frames), and the host takes a wingman's position and shots
  whenever they come.  Everybody sees the others a one-way trip late, and
  a burst of host frames after a spike moves the host's ships on a bit at
  once instead of freezing the wingman's game; the wingman's own ship
  answers its stick at once in both modes, as it always did.

The host decides which is in force and its frames carry it (`Frame.mode`,
also in the start state).  `WCNET_MODE=auto|low|high` on the host, the
page's "Connection" option and `/latency auto`, `/latency low`,
`/latency high` in the comms prompt or the page's chat set it; a wingman's
`/latency` says which is in force and the round trip.  `low` is the default
(until 2026-10-15 it was `auto`, whose flips to the high-latency mode in
the middle of a fight were a disturbance of their own in play, the user
found; it is now only had by asking).  `auto` goes by what the host
measures of each wingman's link (`LinkMonitor`,
`ServerSession::mode_tick`, once a second in flight):

* the round trip, from a frame's going out to the wingman's ack of it
  coming back, less the time the wingman held it (`Frame.ack_delay`; the
  host's frames ack the wingman's the same way, so the wingman has the
  figure from its own clock too, for the page's line and `/latency`);
* its spikes: a round trip more than 150 ms above the best of the last
  minute (a lost message sent again, a queue somewhere), as a share of
  the last ten seconds' round trips and counted per minute as they
  begin, for the log;
* the wingman's own frame rate, said in each of its frames (`Frame.fps`),
  and the host's, a sample a second each.

A pause is no measure of the link (2026-10-15): a tick after a gap of
more than 1.5 s of the host's own (an in-flight scene, the nav map, a
comm answer), or with a wingman whose last frame is more than a second
old (its loop stands still and the host waits for it), puts that
wingman's monitor in the quiet period the autopilot's fly-by already
used (`LinkMonitor::quiet`): no samples and no round trips for five
seconds, the pause and the catch-up after it.  Before, the host's rate
collapsing while it waited read as a slow link and the mode flipped.

The rules, with the thresholds the simulated link gave (`mode_tick`;
every figure is as the host measures it, processing included):

* to the high-latency mode when the smoothed round trip passes
  `kRttHigh` = 300 ms, the window's edge (six frames at 20 a second; the
  low-latency exchange was whole at a measured 250 and at 15.7 frames a
  second at 350, and 200 ms of jitter reads as 337 on an ordered link),
  or a ten-second average of either frame rate is under 15 (the host's
  only when it is waiting for the network: a slow computer is not the
  link's fault), or more than one second of the last minute was under 10
  frames a second on either side, the user's own definition of "not
  reliably".  The spikes are logged (`spiky`, the share of the last ten
  seconds' round trips more than 150 ms above the best of the minute,
  and how many a minute begin) but do not decide: a count a minute does
  not tell a Starlink-like link from a 5% one (10 to 40 on both), and the
  share reads 20 to 67% on 100 ms of jitter, a link the exchange was
  still within the target on;
* back to the low-latency mode after thirty seconds in which the worst
  round trip stayed under `kRttLow` = 150 ms and there was no spike.

The user's wish was 15 frames a second on average and never under 10 more
than once a minute; the first two rules act within seconds of the first
frames, the last two within a minute of play, so a link that is bad from
the start is caught before the play is.

**Positions by the best-effort channel.**  On a reliable, ordered link a
lost message holds everything behind it back until it is sent again, so in
the high-latency mode a wingman's view of the host's ships froze for the
retransmit at every loss and then jumped.  lobbylink's second DataChannel
is unordered and never retransmits (`wclobby_send_best_effort` /
`wclobby_recv_best_effort` in the Rust library, the C header and the
browser shim; TCP carries the same marked with the top bit of its length
prefix), and in the high-latency mode every frame's positions go a second
time by it, a frame number with each (`ServerSession::flush_outgoing_frame`,
`ClientSession::take_positions`, `drain_client_positions` on the host for
the wingman's own): the freshest wins, and a reliable frame older than the
last positions taken keeps its events and health and leaves its positions
alone.  `WCNET_BESTEFFORT=0` switches it off, and the perf line says how
old the other machine's positions were at each frame top.  Measured (high
mode, 100 ms each way, 5% lost; both machines at 20.0 frames a second
either way):

| | positions old on the host, average (worst) | seconds with a gap of 150 ms or more, of 117 | on the wingman, average (worst) | seconds, of 115 |
|---|---|---|---|---|
| reliable only | 59 ms (400) | 82 | 102 ms (450) | 80 |
| with the best-effort copies | 13 ms (200) | 1 | 54 ms (250) | 8 |

**The high-latency mode on the same links, and the auto mode** (the
round trip is the host's own figure, smoothed, with the worst of the run;
the vhigh300 run and the 200 ms auto run are from the first build, before
the best-effort positions, so they have no position figures):

| link and mode | host fps (min) | wingman fps (min) | dips under 10 per minute | round trip the host measures (worst) | positions old: host (worst) / wingman (worst) |
|---|---|---|---|---|---|
| high: 100 ms each way + 5% lost | 20.0 (20.0) | 20.0 (20.0) | 0.0 | 293 ms (worst 951) | host 13 (200) / wingman 54 (250) |
| high: 25 ms + jitter up to 400 | 20.0 (20.0) | 20.0 (20.0) | 0.0 | 581 ms (worst 900) | host 50 (300) / wingman 102 (400) |
| high: 150 ms + jitter 100 + 1% lost | 20.0 (20.0) | 20.0 (20.0) | 0.0 | 512 ms (worst 1049) | host 13 (100) / wingman 61 (150) |
| high: 300 ms each way | 20.0 (20.0) | 20.0 (20.0) | 0.0 | 674 ms (worst 704) |  |
| auto: no impairment (stays low) | 20.0 (20.0) | 20.0 (19.6) | 0.0 | 36 ms (worst 49) | host 0 (0) / wingman 15 (31) |
| auto: 200 ms each way | 20.0 (20.0) | 20.0 (20.0) | 0.0 | 477 ms (worst 553) |  |
| auto: 25 ms + jitter up to 400 | 20.0 (20.0) | 20.0 (20.0) | 0.0 | 619 ms (worst 898) | host 51 (588) / wingman 102 (351) |
| auto: 25 ms + 5% lost | 20.0 (20.0) | 20.0 (20.0) | 0.0 | 157 ms (worst 501) | host 9 (250) / wingman 49 (150) |
| auto: 150 ms + jitter 100 + 1% lost | 20.0 (20.0) | 20.0 (20.0) | 0.0 | 505 ms (worst 1151) | host 14 (408) / wingman 61 (150) |

Every one of them holds 20.0 frames a second on both machines with no dip,
where the low-latency exchange had 7 to 15.  The auto runs switched within
the first seconds of the flight, from the round trip alone where it was
long, from the spikes where it was the loss or the jitter; the clean link
stayed in the low-latency mode throughout.

**The borderline links, and a Starlink-shaped one.**  The thresholds
above were set from the runs below (two of the auto runs are from an
interim build whose rule counted spiky round trips and went high on links
the low-latency exchange was still within the target on; with the rules
as they stand those links read a round trip under 300 and stay low, as
the three handover-burst runs show).  A Starlink link, from the public
measurements (median ping 33 to 55 ms in the operator's own figures, 80 to
100 in the academic ones; about 1.4% loss, mostly in bursts at the
satellite handover every 15 s with some 80 to 100 ms of queueing after
it), is tried two ways: its loss at random, and as the handover bursts
(`WCNET_BURST=15000:100:100` with 0.3% at random).

| link and mode | host fps (min) | wingman fps (min) | seconds under 15 (host / wingman) | dips under 10 per minute | round trip the host measures (worst) |
|---|---|---|---|---|---|
| auto: 25 ms + jitter up to 100 (interim spike rule: went high) | 20.0 (20.0) | 20.0 (16.1) | 0 / 0 | 0.0 | 231 ms (worst 353) |
| auto: 25 ms + jitter up to 200 | 20.0 (20.0) | 20.0 (20.0) | 0 / 0 | 0.0 | 338 ms (worst 551) |
| auto: 25 ms + 1% lost | 20.0 (20.0) | 19.2 (10.6) | 0 / 3 | 0.0 | 90 ms (worst 269) |
| auto: 25 ms + 2% lost | 20.0 (16.3) | 18.4 (9.0) | 0 / 11 | 1.0 | 91 ms (worst 283) |
| low: Starlink-like, 60 ms ping, jitter 30, 1% lost at random | 19.8 (15.6) | 18.9 (10.4) | 0 / 9 | 0.0 | 132 ms (worst 353) |
| auto, the same (interim spike rule: went high) | 20.0 (18.3) | 19.9 (13.7) | 0 / 1 | 0.0 | 168 ms (worst 451) |
| low: Starlink-like, 100 ms ping, jitter 20, 1% lost at random | 19.0 (13.9) | 18.3 (6.5) | 2 / 19 | 0.5 | 186 ms (worst 407) |
| auto, the same (interim spike rule: went high) | 19.9 (15.4) | 19.8 (11.7) | 0 / 3 | 0.0 | 203 ms (worst 453) |
| low: Starlink-like, 60 ms ping, a handover burst every 15 s | 19.9 (17.8) | 19.5 (14.8) | 0 / 1 | 0.0 | 107 ms (worst 348) |
| auto, the same | 20.0 (18.8) | 19.4 (15.0) | 0 / 0 | 0.0 | 108 ms (worst 327) |
| low: Starlink-like, 100 ms ping, a handover burst every 15 s | 19.5 (15.7) | 19.1 (12.5) | 0 / 7 | 0.0 | 179 ms (worst 409) |
| auto, the same | 19.6 (15.6) | 19.3 (13.4) | 0 / 4 | 0.0 | 184 ms (worst 406) |

So a Starlink-like link is within the target in the low-latency mode:
the 60 ms ping with its loss at random gives the wingman 18.9 frames a
second with a minimum of 10.4, the handover shape 19.5 with 14.8, and the
100 ms ping 18.3 to 19.1 with an occasional second under 10 when its loss
is at random; the auto mode leaves it in the low-latency mode.  The
high-latency mode gave 20.0 on both machines on every one of them, so a
pilot who would rather have that than the stricter exchange can ask for it
(`/latency high`).

## 3c. The page's view

The page's line under the picture (`web/wc.js`, `linkState`) names the
mode in force and the round trip; the host's "Connection" option (and
`?latency=low|high` in its address) forces one.

## 4. Modules

| file | role |
|---|---|
| `wcnet_memory.h/.cpp` | data-segment map with IDA names, typed accessors, `ShipHealthState` |
| `wcnet_code.h` | stub/overlay/root addresses of every hooked function and location |
| `wcnet_vm.h/.cpp` | interception helpers (`call_arg16`, `return_from_call`), `GameCall` thunks, the `Trampoline` job queue |
| `wcnet_transport.h/.cpp` | TCP framing, `Connection` with per-category queues |
| `wcnet_entities.h/.cpp` | `NetworkShipId`, client `EntityMap`, server `SpawnRegistry` |
| `wcnet_events.h/.cpp` | interception policy for damage/fire/spawn/despawn/AI, replay jobs |
| `wcnet_session.h/.cpp` | `ServerSession` / `ClientSession`: lifecycle, handshake, frame exchange, health replication, chat |
| `wcnet_hooks.cpp` | the per-instruction dispatcher (IP bitmap prefilter) |
| `wc_net.h` | the public API used by the CPU core, the shell command and the GUI |

### Running game code

`GameCall(seg, off).arg(..).invoke()` writes a far-call thunk into an unused
error string in the data segment (`dseg:0395`) and jumps to it with the
current CS:IP pushed as the return address.  The `Trampoline` is a 6-byte stub
(`push bp / mov bp,sp / nop / pop bp / retf`) at `dseg:0187+101`; its `nop` is
a hook point.  `run_instead_of_current_call()` is used at a function entry so
the stub's final `retf` returns to the intercepted function's caller;
`run_before_current_instruction()` pushes CS:IP first.  Each `VmJob::start()`
issues one `GameCall`; `finish()` runs when it returns (this is how the slot a
spawn used is learned).  `return_from_call(ax)` skips an intercepted function
by popping its far return address, the Borland cdecl caller cleans up the
arguments.

Those two places are the games' error messages (WC2: `dseg:0380`, its
loader's "Sorry, an error has occurred while %s...", and `dseg:9FC0`, the
general one), and a thunk begins with a NUL: once the hooks have run, a game
that stops with an error of its own prints an empty message and leaves a
bare DOS prompt.  (For a day the trampoline kept what was there and put it
back when it had run out; an interrupt taken on the stub's last
instructions then returned into the game's text, and the flight stood still
on both machines.  It keeps nothing now.)  The page says so when the game
ends by itself, with the text the game left on the screen
(`game_program_ended` logs it, and the stack: the page sets
`WCNET_EXIT_STACK`), reports an abort or a trap of the emulator the same
way, and has a "Copy log" button for a report.

### Hook points (`wcnet_code.h`)

| hook | where | purpose |
|---|---|---|
| `do_damage` | `ovr143:0A99` | damage policy + broadcast |
| `fireGunFromShip` | `ovr143:2978` | fire policy, prediction, broadcast |
| `delayedDespawn` / `despawn` | `ovr143:1F15` / `ovr140:1C16` | despawn policy + broadcast |
| `outerSpawnShipEntity` | `ovr145:115D` | spawn broadcast, slot registry; asteroid and mine fields run locally |
| AI think / set speed | `ovr163:160E` (capital ships), `ovr163:13CF` (fighters) / `ovr143:0874` | suppress for human-flown slots |
| mission starting / ended / score | `ovr161:0470` / `04DD` / `0251` | briefing handshake, mission end sync |
| main loop top, status checks | `seg001:20E3`, `20F4`, `2108`, `20F2` | frame exchange, client mission-status policy |
| autopilot | `ovr133:0003`, `ovr133:05C9`, `seg001:1695` | camera replication, client key lockout |
| barracks, startup | `ovr150:105D`, `seg001:0512`, `seg001:04F2` | connect handshake, direct mission start |

## 5. Protocol additions (`src/wc.proto`)

* `ShipHealth` inside `ShipUpdate.health`: shields, shield max, armor
  quadrants, damage points, core HP, hull counter, state byte, gun damage,
  engine flag, gun energy (the arrays are listed in the proto comments).
* `WeaponFire.client_seq` for predicted client shots, `WeaponFire.target`
  for the shooter's missile lock, `WeaponFire.gun` and `.guns` for the
  shooter's own record of the gun and its count of guns.
* `Frame.player_end` (`PlayerEnd`) for a client leaving the mission.
* `Spawn.pilot`: the slot's pilot byte once the mission setup has run.
* `Frame.rocks` and `ServerSendBriefingStart.rocks`: asteroid and mine fields
  on (1), off (0) or soft (2), the server's choice.
* A client's frame may carry `Damage` events as well as `WeaponFire`: hits by
  that client's own rocks or mines on a ship the server owns (no shooter, no
  seed).  The server ignores one that names a human's ship, a missile or an
  empty slot.

Old peers are not wire-compatible with these semantics (a client that does
not replicate health would be trusted as authoritative for a ship it never
damages), so run the same build on both machines.

## 6. Running and testing

Server: `WCPORT=13255 src/dosbox -c "mount c wc" -c c: -c wc` (or
`runwc.sh <dospath>`).  Client: same with `WCHOST=<server>`.  The `WCNET`
shell command still starts/stops the server or connects.  Useful environment:

| variable | meaning |
|---|---|
| `WCNET_LOG=0..3` | log verbosity (1 default; 2 per event; 3 full protocol) |
| `MIS=<n> SERIES=<n>` | jump straight into a campaign mission (series 1.., mission 0.. within it; setting either variable is enough, so `MIS=0 SERIES=1` forces the first mission) |
| `WCCALLSIGN`, `WCLASTNAME` | override the pilot identity |
| `WCHOSTPILOT=0..7` | on a client: which of the eight named pilots the host's ship appears as (default: 3, Iceman; never the mission's wingman, who is the client's own player) |
| `WCHOSTCALLSIGN` | on a client: the host's callsign, the name his ship goes by in the targeting computer (the page sets it from the room's roster; without it the host's first typed line brings it) |
| `WCHELMETNAME=1` | on a client: leave the stand-in pilot's name on the helmet of the comm picture while the host's line is shown |
| `WCNET_LAG=<ms>`, `WCNET_JITTER=<ms>`, `WCNET_DROP=<pct>[:<ms>]`, `WCNET_BURST=<periodMs>:<lossMs>[:<spikeMs>]`, `WCNET_SEED=<n>` | test aid: a slow or a bad link made on this machine (section 3b): one-way delay, random extra delay per message, share of messages lost (and how long a lost one takes to be sent again, with everything behind it), a periodic burst of loss and queueing like a satellite handover, and the random sequence |
| `WCNET_MODE=auto\|low\|high` | on the host: the exchange mode (section 3b; low is the default since 2026-10-15; auto goes by the measured link; the page's "Connection" option and `/latency` do the same) |
| `WCNET_BESTEFFORT=0` | no positions by the best-effort channel in the high-latency mode (section 3b), to measure what it is worth |
| `WCKEEPNEAR=<km>` | on the host: how close a wingman's ship must be to an enemy that the game would remove for being more than 16000 km from the host's ship, for that enemy to stay; one at a time, the nearest to a wingman (default 7200, a laser blast and a half; 0: the game's own rule) |
| `WCWINGBUMPS=game` | on the host: two wingmen's ships touching do the game's collision damage (the default is none; the lobby's "Collisions" menu, `/bumps harmless`, `/bumps game`) |
| `WCROCKS=0` / `WCROCKS=soft` | on the host: fly without asteroid and mine fields, or with rocks that do a sixteenth of their damage to a player's own ship (everyone follows the host; `/rocks on`, `/rocks soft`, `/rocks off` in the comms prompt switch it in flight) |
| `WCNET_AUTOKEYS=1` | test aid: press Enter through the briefing, then `A` (autopilot) once in space |
| `WCNET=0` | fly alone: no server, no room, the game's own wingman (a single-player control for experiments) |
| `WCNET_KEYSCRIPT="8:c,9.5:1,10:@after"` | test aid: once in space, at each emulated second press that key (letter, digit, `enter`, `esc`, `space`, held 150 ms) or, for `@tag`, dump the data segment to `WCNET_DUMP_DIR/tag.bin` and log the comm line, VDU text, mission, pilot bytes and KIA words; `!status=N` ends the mission as the game would (1 landed, 4 died, ...), `!kill=SLOT` destroys a ship on the server through the broadcast path of a real kill, `!poke=HEXOFF:HEXBYTE` writes the data segment, `!mouse=X:Y` puts the mouse pointer at those fractions of its range, `!button=N:1` / `N:0` presses and releases a mouse button, `+key` / `-key` hold and release a key, `!pos=X:Y:Z` moves the machine's own ship, `!face=SLOT` turns it towards a slot (`npc`: the first ship no human flies, `rock`: one of its field's rocks), `!rock=SLOT` puts one of its rocks or mines on that ship so the game's collision code finds them touching (`!rock=0`: dead ahead of its own guns instead), `!rocks=1` / `!rocks=0` / `!rocks=2` switches the fields on, off or soft as the host's lobby option does, `!hit=QTY` (or `!hit=QTY:back`) damages the machine's own ship through the game's `do_damage` as a hit from ahead (or behind) would and logs shields, armor, systems and cockpit flags, `!shot=NAME` writes the game's screen to `WCNET_DUMP_DIR/NAME.ppm` (for runs with no display), and every `@tag` line also lists the field state (`fields N inside|outside rocks <slots>`), and `wait` holds the script until the next mission's first frame and restarts the clock there (`skip` does the same and taps Esc every two seconds on the way, through the scenes), `!steer` logs the view WC2 steers in, its steps of turn and the turn it is asked for, `!mem` logs the free bytes of WC2's own heap (its loader's "MAIN"), `!vga=NAME` writes the video memory (320x200 palette indices and the 768-byte palette) to `WCNET_DUMP_DIR/NAME.vga`, `!where` logs CS:IP and the far return addresses up the stack; `WCNET_AUTOKEYS` then only handles briefings and debriefings |
| `WCNET_SKIPBARRACKS=1` | test aid: never stop in the rec room or the barracks, so a scripted run goes from a debriefing straight to the next briefing |
| `WCNET_WATCH=C260:16` | debugging aid: log every change of that part of the data segment (hex offset, length) with the address of the instruction after the write (`F000:xxxx` is a DOSBox callback: a DOS call wrote it), and every DOS file call of the game (`int 21h` open, create, close, read, write, seek: `dos: ...` lines with handle, length, buffer and file name), so that a stray read can be told from its file |
| `WCNET_AUDIOLOG=1` | test aid: every 500 ms log each mixer channel's mean level and the number of OPL register writes (stderr, same clock as the key script); `WCNET_WAVE=1` also records the mixed output into DOSBox's capture directory |
| `WCNET_DUMP_FRAME=<n>`, `WCNET_DUMP_FILE` | dump the data segment at in-flight frame n (and on SIGUSR1) |

`scripts/wcnet-smoke.sh [seconds] [mission] [series]` starts a headless
server and client (dummy SDL drivers), lets them fly mission 1 with
A callsign with `CHEAT` in it (any case) flies a ship that takes no damage
on its own machine: the hooks hold the game's own developer switch at zero
every frame in flight (the word `do_damage` tests before touching slot 0,
WC.EXE `dseg:00BA`, WC2.EXE `dseg:00C0`, what WC2's `Origin -k` clears), so
that a mission can be flown alone with an idle second player.  Nothing else
changes: the other machines take that ship's health from its own reports,
as always.  The log says so once per mission.

`WCNET_AUTOKEYS`, and prints both logs.  It exercises the handshake, the
mission start state, spawn mapping, autopilot, enemy fire/damage replay and
the client's local damage model.  It does not exercise chat, landing, or a
client death (nothing shoots back headless for long enough).

The default build needs no special flags (`include/setup.h` no longer uses
dynamic exception specifications); `make -j8` after `./configure`.

## 6a. The browser build

The same code runs in a web browser (`scripts/build-web.sh`, see the
top-level README for the workflow).  What differs:

* **Blocking becomes suspension.**  Emscripten
  [Asyncify](https://emscripten.org/docs/porting/asyncify.html) instruments
  the program, so `Connection::recv`, `Listener::accept` and
  `LobbyHub::join` can wait for the network from inside the hooks exactly
  as they do natively: the wasm stack is unwound, the page's event loop runs
  (delivering WebRTC and lobby events, keyboard input, audio), and the stack
  is rewound when the awaited message arrives.  `Normal_Loop` also yields to
  the browser every ~10 ms (`emscripten_sleep`, `EM_ASYNCIFY` in
  `src/dosbox.cpp`).
* **...but not the CPU core.**  Instrumenting the interpreter cost the
  browser build a third of its speed (2026-10-21: ~41,000 emulated cycles a
  millisecond on a 2016 desktop core in Chrome against ~123,000 natively for
  the same core; the profile was 75% `CPU_Core_Simple_Run`).  So in the
  browser the hooks run from the main loop instead: when the in-line test
  hits, the core puts the cycle back, fills the flags and returns a callback
  number (`wc_net_hook_exit`, `src/cpu/wc_net.h`); `Normal_Loop` runs that
  callback, which runs `wc_net_check_cpu_hooks()`, and the core comes back to
  the same instruction without stopping for its hooks again (the
  instructions whose hooks have run are kept until reached, since a hardware
  interrupt may come between; one whose hooks moved CS:IP is not kept, so
  coming back to it later stops again, as the in-core call did).  With no
  hook on its stack, the core and the helpers it calls per instruction
  (memory, I/O ports, calls, returns, interrupts, string moves) are never on
  the stack when the program unwinds, and `-sASYNCIFY_REMOVE`
  (`src/asyncify-remove.json`) leaves them uninstrumented: ~70,000 cycles a
  millisecond, +70%.  What they call that runs code nested (a page fault,
  an I/O permission fault, `CALLBACK_Idle`) does it without sleeping
  (`DOSBOX_RunMachineNoSleep`); a network wait in a hook during a
  protected-mode page fault would break that, and none of the games pages.
  Natively the hooks are still called in place.  To measure: the page's
  performance line ("emulator at N% of real time") at `?cycles=N`, or
  `?env.WCNET_PERF=4` for the layer's log; a Chrome CPU profile shows the
  rest.
* **The transport is JavaScript.**  `src/wclobby_web.js` implements the
  `wclobby.h` C API (the one `wcnet_lobby.cpp` uses) on top of lobbylink's
  browser client (`lobbylink/clients/ts`), with the same generation /
  hangup semantics as the Rust crate, so browser and native players can
  share a room.  Blocking API calls use `Asyncify.handleSleep`; the state
  machine lives in JS and rewinds the wasm from event handlers only (never
  from inside a call made by compiled code).
* **No TCP.**  `init_network` refuses to start without `WCROOM`.  Only the
  room-code transport exists in a browser.
* **The simple CPU core runs the hooks too.**  The Emscripten build defaults
  to `core=simple` (`src/dosbox.cpp`; ~25% faster than `normal` natively
  too), so `CPU_Core_Simple_Run` makes the same in-line test before every
  instruction as the normal core; the hooks are core-independent.
* **Frames are pushed as whole-screen updates.**  The chat overlay in
  `GFX_EndUpdate` reports the entire frame as changed; the surface output
  (the browser's renderer) only pushes changed runs, so that table has to
  be right (it was inverted before, which the texture output natively never
  noticed).
* **The game files come from the player.**  `web/gamefiles.js` unpacks a
  `.zip` (browser `DecompressionStream`) or an Inno Setup / GOG installer
  (innoextract compiled to wasm, run in `web/inno-worker.js`), recognises
  the game by its executable, installs the game directory under `/game` in
  the memory file system and caches it in IndexedDB.  Only Wing Commander 1
  gets `WCROOM` (the hooks are for `WC.EXE`); other games run single-player.
* **The page joins the room first** (roster, chat, host-driven start) and
  DOSBox adopts that `P2PGame` (`Module.lobbyGame`) in `wclobby_connect`
  instead of connecting again, which would supersede the page's session.
  Joining is somebody's doing (the user's condition for leaving rooms
  open to pages without a game): "Join room", a code clicked in the lobby,
  "Advertise".  A link to a room (`?room=CODE`) joins by itself only on a
  page with a game to fly, when the files are there (`joinLinked` from
  `setSource`); without one the page says so and waits for the game or for
  "Join room", so that whatever merely opens a link says nothing to the
  lobby server.
  Lobby chat and presence are reliable messages prefixed with
  `57 43 4C 01`; a protobuf message never starts with 0x57 (wire type 7),
  so `wclobby_web.js` drops them before they reach the game.
* **Lobby traffic shares the data channels**: the page's roster, chat and
  "start" messages begin with `WCL\x01`, which no protobuf message can, and
  both the browser transport (`src/wclobby_web.js`) and the native one
  (`src/cpu/wcnet_lobby.cpp`) drop them before the game sees a byte, so a
  native DOSBox can join a room a browser hosts.
* **Configuration is the environment**, set by `web/wc.js` from the page's
  form (`Module.ENV` in `preRun`): `WCROOM`, `WCLOBBY`, `WCPLAYERS`,
  `WCCALLSIGN`, `WCLASTNAME`, `WCLOBBY_RELAY`, `WCNET_LOG`, plus anything
  given as `?env.NAME=value` (so `MIS`, `SERIES` and `WCNET_AUTOKEYS` work
  for testing, as in `scripts/web-smoke.sh`).
* **Asteroids and mines** are the host's lobby option (a menu beside the
  mission menu: on, soft, off; `/rocks on`, `/rocks soft` and `/rocks off`
  in the lobby chat; `?rocks=off` or `?rocks=soft` in the host's link).  The page tells the other pages (`rocks` in its `hello`
  and `start` messages), starts its game with `WCROCKS`, and while flying
  calls `wc_web_set_rocks`; the host's game then tells every wingman's game
  through the game protocol.  A page in full screen has no chat: leave full
  screen, or type `/rocks off` in the game's own comms prompt (`0`).
* **Save games.**  A game's registry entry (`web/gamefiles.js`) lists its
  save files (Wing Commander: `GAMEDAT/SAVEGAME.WLD`, all eight bunks).  The
  page keeps a copy in local storage, writes it back into the game directory
  at every start, re-reads the file every few seconds while the game runs and
  stores it when it changed, offers it as a download, and can restore it from
  a file of the same size.
* **Speed.**  The registry also gives the game's `cycles` (3630 for Wing
  Commander: DOSBox's default of 3000 raised twice with Ctrl+F12), passed as
  a `cycles=` command before the game starts; `?cycles=N` overrides it.
* **Caps Lock is Esc.**  Browsers keep Esc for leaving full screen, so the
  page swallows Caps Lock while the canvas has the keyboard and injects a
  150 ms Esc press instead (`wc_web_tap_escape`); a lone key-up counts as a
  tap, because a Mac reports Caps Lock as key-down when it locks and key-up
  when it unlocks.  Where the Keyboard Lock API exists (Chrome, Edge) the
  page also asks for Esc in full screen: a tap reaches the game, holding it
  leaves.
* **Controllers** (`web/gamepad.js`).  The lobby has a controller picker
  and a table of buttons and axes; a chosen controller is presented to the
  game as its mouse and keyboard.  Wing Commander steers by its mouse
  pointer: it parks the pointer at 318,52 of the 640x200 mouse range (the
  middle of the cockpit view) and turns towards it, right of the neutral
  point to turn right, above it to raise the nose (the registry's `pointer`
  entry).  In flight the sticks set the pointer around that point (left
  stick turn and pitch, right stick pitch and, through the roll keys, roll;
  each axis can be inverted and pitch is by default, so pulling back climbs);
  the pointer is re-sent every 200 ms because the game re-centres it itself.
  Outside flight the stick moves the pointer like a mouse and the Fire guns
  button is also the click.  Buttons hold keys: A Space (guns), B Enter
  (missile), X `T`, Y `W`, L1 `+`, R1 `-`, Back `N`, Start `A`, and the left
  trigger Tab (afterburner) only past 90% of its travel, releasing below
  80%.  The page queues keys, the pointer and the click through
  `wc_web_key`, `wc_web_pointer` and `wc_web_mouse_button`; the emulator
  applies them in its async tick, and `wc_web_in_flight` tells the page which
  of the two modes applies.  The choice of controller is per window (session
  storage, announced to other windows over a `BroadcastChannel` so a
  controller taken by one window is not picked up by the next); the mapping
  is shared (local storage).  The page reads the controller from a timer, so
  a window that is visible but not focused keeps flying: two windows side by
  side are a split screen.  The DOS game-port joystick is off in this build
  (`joysticktype=none`), or a gamepad would reach the game twice.
* **A pad "built in"** (all five programs, and SO3: the registry's
  `joystick`).  Each pad is in the picker a second time, "Built in: ...":
  the pad is then the game port's stick A, which the game reads and makes
  sense of itself (case 4 of the flight's event loop, `seg001:0FCF`): button
  1 the guns, both buttons a missile, button 2 with the stick rolls (across)
  and changes speed (forward and back), a double click of button 2 held is
  the afterburner (its press counting in `seg039:0007`, from the timer's
  poll at `seg001:087B`).  Wing Commander II has the same library and the
  same scheme (checked: the trigger, a Pilum on both buttons, the roll, the
  afterburner); its `joy_init(8, 8, 1, 1)` (`seg082:000A`, from `ovr190`)
  reads steps of -8..8, and its variables are WC1's in the same order
  (`dseg:A8D4` on; SO1's and SO2's carried over by `scripts/wcport.py`).  The axes are the table's turn and pitch (inverted
  as set, scaled by the sensitivity, after the dead zone), the two buttons
  its "Joystick button 1/2" rows (A and B by default); an action bound to
  either button is greyed and not pressed, and the other buttons press
  their keys as ever.  The game looks for a joystick once, at startup
  (`joy_init(9, 9, 1, 1)` from `ovr160:01D2`, which takes the stick's
  position then as the centre), so the page puts the port in only when a
  built-in pad is chosen at the launch: `[joystick] joysticktype=none
  timed=false` in that launch's configuration and nothing in any other.
  The port counts (`timed=false`): a timed port's readings grow with the
  CPU's speed, and a flight runs at several times the menus' cycles, past
  the centre taken at startup.  The type stays `none`: any other has
  DOSBox's own mapper make a stick of its own and write its rest onto the
  port every frame.  The page puts the stick on the port (`wc_web_joystick`)
  and moves it (`wc_web_joy`); the hooks know the game's joystick state
  (`joyStick` and on in `wcnet_ds.def`).  Taken away in flight (another
  controller picked, the pad unplugged), the stick is dropped for the game
  between two polls with the words below stick A's reading zeroed, then
  leaves the port: dropped the game's own way, by a reading that times
  out, the flight's poll (WC1 `seg001:087B`, WC2 `seg001:065A`) goes on to
  read "stick 0xffff" and flies a frame by the y range and the raw counts,
  a quarter turn at once.  In flight the drop waits for the top of the
  frame loop (`code::mainLoopTop`), where no poll is half done; elsewhere
  for WC1's flags of a running poll (WC2's other polls keep the stick they
  began with).  The mouse pointer
  is then parked in the middle of the view (the stick never parked it).
  Given back, the stick is handed to the game as its `joy_init` takes one
  at rest (centre 64 counts, 6 a step, 768 reads at most, the game's own
  range).  A pad picked
  built in after a launch without the port is the mouse and keyboard until
  the next.  Natively `WCJOY=1` puts the stick on from the start (with
  `[joystick] timed=false` in the configuration) and the key script has
  `!joystick=`, `!joy=` and `!joybutton=`.
* **No crosshair for a controller.**  In flight the games steer by their
  mouse pointer, a crosshair the cockpit code draws on every frame while
  the pointer steers (`cmp word [00C6],1` in WC.EXE's `ovr134:26FE`,
  `[00DC]` in WC2.EXE's `seg005:3384`): the picture under it is put by
  (`call far [7A4E]`), the pointer drawn (`call far [7A4A]`), and the next
  frame puts the picture back.  The page moves that pointer for a
  controller's stick, so a pilot with a controller had it wandering about
  the view.  With a controller chosen the page tells the emulator
  (`wc_web_pointer_off`; `WCNOPOINTER=1` natively), and the hook steps over
  the one call that draws it (`code::cockpitPointer`, in all five
  programs): the rest goes on, so nothing is left on the screen, and in
  the barracks, the menus and the navigation map, where other code draws
  the pointer, it is there to click with.  "Show the game's crosshair in
  flight" under the controller's settings brings it back.  Checked
  natively in the five programs (the screen with and without differs in
  the pointer's nine by nine pixels and nowhere else) and in
  `scripts/web-smoke.sh pad`.
* **Where the pointer rests is the cockpit's business.**  Both games read
  the pointer the same way (WC.EXE `seg001:0E52`, WC2 `seg001:0C5A`): its
  distance from the middle of the 3D view picks a step of turn from two
  tables (10, 37, 52, 57, 62 pixels across, 5, 18, 27, 35, 38 up and down:
  steps 1 to 5), within four pixels of the view's edge the step is 8, and
  the keys go to 9.  The view is the cockpit's window, another rectangle in
  every ship: 318,52 is the middle of the Hornet's (0,0..319,104), and
  the Scimitar's ends at 111, the Raptor's at 86, the Rapier's at 69, and
  WC2's Ferret has 0,0..319,135, its Broadsword ..115.  WC2 had no
  `pointer` entry, so the stick rested the pointer in the middle of the
  screen: 33 pixels below the Ferret's neutral point, a dive at step 3 with
  the stick let go, and a climb only near the end of its travel; and
  WC.EXE's fixed 318,52 had only ever been tried in the Hornet: in the
  Raptor and the Rapier the same resting pointer is a dive at step 1 (`!steer`
  measured it).  Both entries are now `fromGame`: the page asks the running
  game for the view and the tables (`wc_web_steer`, `steer_info`) and maps
  the stick onto the steps themselves (`stepped` in `web/gamepad.js`): at
  rest no turn in any cockpit or turret, and the same stick the same step
  whatever the size of the window; full stick is the edge's 8 at the
  default sensitivity of 100% (the stick is analog: the user's choice),
  and 70% stops at step 5.  A window too short for
  the later steps (the Rapier's 69: steps 4 and 5 begin at 35 and 38
  pixels, the edge at 30) has only the edge beyond step 3, and full stick
  goes there.  `scripts/web-wc2.sh pad` and `scripts/web-smoke.sh pad`
  check it with a simulated controller in a Ferret and a Broadsword, a
  Hornet and a Rapier.  In the Hornet the change from the fixed point is
  only that full stick pitches at step 5 instead of 4.
* **A room code may be said in public.**  The lobby server lets anyone
  with the code take a free seat, and would let anyone "claim" an occupied
  seat whose holder had been silent on the signaling socket for 40 s
  (`allowReplacement`, `claim-slot`) -- which is every player in flight,
  since the game's traffic goes over the data channel and nothing keeps
  the socket busy.  The page now creates rooms with `allowReplacement:
  false` and `reconnectPolicy: "token-only"`: a seat comes back only to the
  tab that held it (the hidden resume token in session storage, which a
  reload keeps), nobody can push a player out, and a tab that is gone
  leaves its seat taken until the room dies, so the host makes a new room.
  A stranger can still take a seat that is free; the host sees the
  callsign in the roster.  A kick or a lock for the host would be a lobby
  server change (`lobbylink`).
* **The game's screen is the game's.**  Nothing is drawn on it by the
  hooks but the chat lines and the answers to the chat commands; the
  seats and their keys are the lobby's hints.  The game's one line of help
  about the `0` key goes for good once a message has been sent from the
  browser, and the room form has a box to turn it off before that
  (`WCNET_NOHINT`).
* **Voice** (`web/voice.js`).  Each player chooses in the room form: off
  (the default), listen only, push to talk, or always on; the choice is
  kept in local storage and goes to the others with the hello.  Voice runs
  only when everybody in the room has opted in -- one player who has not
  wants no voice and gets none, and nobody else hears or is heard either
  (the user's rule: one open mic could easily be rude).  A player who has
  not opted in while another has sees the control light up with who did,
  and Fly puts a line in both chats when a voice is on one side only, so
  that nobody is surprised.  The sound is WebRTC audio, a second peer
  connection beside the game's data channel, negotiated with offer, answer
  and ICE candidates sent as the page's own lobby messages over that data
  channel (the lobby server's signal relay only passes the game's kinds)
  with the lobby's ICE servers; the lower player id offers.  The browser's
  audio processing does the quality: echo cancellation (it knows what the
  browser plays, the game's sound included), noise suppression, automatic
  gain, Opus.  Push to talk is the backquote key, held (it does not reach
  the game), or a controller's left stick button (`ptt` in
  `web/gamepad.js`); listen only sends no microphone (a `recvonly`
  transceiver).  Any change of choice renegotiates, and a player going off
  closes everybody's voice.  `scripts/web-smoke.sh voice` runs it in two
  headless pages with Chrome's fake microphone.
* **A room code names its game.**  Everyone in a room runs the same
  program, and a code said in public should tell who can join, so a code of
  the page's own making is the program's tag and four digits: `WC1-4821`
  (WC.EXE), `SM2-` (SM2.EXE), `WC2-`, `SO1-`, `SO2-` (the registry's `tag`;
  `WC-` while no game is loaded).  Digits only: no letters, so no words.
  The code follows the game until the player types another or joins
  (`retagRoom`).  A game directory holds more than one of these programs
  (Wing Commander's has SM2.EXE, Wing Commander II's the two Special
  Operations): the "Play" menu picks one (`programsIn`, `setSource`), and so
  does a room's code, when the page is asked to join `SO1-4821` with Wing
  Commander II loaded.  The code is advisory, though (2026-10-17: pilots
  found it hard to change games once a room had a WC1 code): a browser
  with no files that run the code's game is not let in (`fitsRoom`: the
  loaded files or a copy the browser saved must have the program), and
  once in, everyone plays the host's program, whatever the code says.  A
  wingman's page switches to the host's program on the host's hello
  (`followHost`): to the same directory's other program, or to a saved copy
  that has it; only when nothing here runs it is Fly off (`wrongGame`).
  The "Play" menu lists every program of every game the browser holds
  (`renderPrograms`: the loaded files and the saved copies, by game), so a
  host switches from Wing Commander II to Special Operations 3 or Wing
  Commander there and the room follows.  An advertised room whose program
  is not its code's names the program after the code ("WC2-4821 SO3 ...").
* **The public lobby** (`web/hall.js`, `web/chatfilter.js`) is a chat room
  where pilots without a wingman say which room they fly in.  It is a
  lobbylink room, `WC-LOBBY`, of 32 seats (the user's figure, after 256
  and 64: it is what the public server's `max_players_hard` gives a room,
  and 32 pilots at a line every ten seconds are three lines a second,
  which can still be read; a server that gives a room fewer says how many,
  and the page asks again for that),
  the first of a row: `WC-LOBBY0`, `WC-LOBBY1`, ... up to `WC-LOBBY30`.  A
  page takes a seat in the first of the row that has one, so pilots gather
  in the first and spill into the next only when it is full; a pilot in
  another lobby than the first sees its name, since the lobbies do not
  hear each other.  Its lines travel over data channels between the browsers like a
  game's; nobody is in charge, so every page keeps the rules, for what its
  own player types (who is told why a line did not go) and again for what
  arrives (a page that was tampered with gains nothing):
  * a line is 60 characters at most;
  * two lines to start with, then one every ten seconds, and one a second
    while fewer than eight pilots are in the lobby (a token bucket;
    the receiving side's, one per seat, refills a little faster and holds
    a sender to ten seconds only from ten pilots, since two pages do not
    count the same pilots at the same moment, so that an honest sender is
    never dropped; it is not filled up again by the seat changing hands);
  * no profanity: a word list of the usual kind, with the usual disguises
    (doubled and spaced letters, look-alike signs, a star inside).  "hell"
    is not on it: the Hellcat is a ship and Hell's Kitchen a system.  A
    number is never a word, which is why the page's own codes are digits;
  * no links or addresses, however the dot is spelled.  Nothing in the
    lobby is a link except a room code;
  * a callsign is held to the same list (16 characters): one that fails is
    shown as "Pilot N".
  A room code in a line (`WC1-4821`: a game's prefix, a hyphen and anything
  after it, however short, so that a code of a pilot's own making is one too)
  is shown as a link.  A
  click joins that room without making it (`joinRoom({ create: false })`:
  a room nobody is in is "not open any more"), after the game check above.
  Nobody has to type a code: "Advertise", beside Send (and in the room),
  sends the pilot's room as such a line -- its code, which names the game,
  the mission and the free seats, "WC1-4821 Gimle 2, 1 seat free" -- and
  "/room" in a typed line is the room's code.  A pilot who is in no room
  yet is put into the one of the room form first (`myRoom`).  The line is
  one of the pilot's lines: when they are used up for the moment it waits
  in the box.  A pilot who comes later is shown the last line of each
  pilot present (its author sends it again, marked with its age; one a
  minute is taken from a seat).  **"Join open game"** (the top button while
  the pilot is in no room; the user's design, 2026-10-22) finds a room
  without a line of chat: the page asks every pilot in the lobby, on the
  same data channels but as a message of its own kind that no chat window
  shows (`{t: "rooms?", n}`), and each page answers with the room it sits
  in when that has a free seat -- its code, the program it plays and the
  free seats (`{t: "room", n, code, game, free}`, `openRoom`) -- or with an
  empty answer.  A page answers three questions from a seat at once and
  then one every five seconds; the asking page waits for every answer or
  two and a half seconds, drops answers that break the rules (a code of
  the lobby's own, a malformed or profane one, a free count that is not
  1..255), keeps those it can fly in (the program played and the game the
  code names, `canFly`) and joins one at random, the next if that one is
  full by then.  It never makes a room (the user's: that is what "Join
  room" below is for): with none, "No open game found" shows beside the
  button for five seconds and fades, with how many pilots did not answer
  (a page older than the question does not; the user's first try met
  one).  A pilot who ticks **Private**
  (beside "Controller buttons and axes", remembered by the browser) gives
  empty answers, and so does everyone in the room of a host who ticked it
  (the host's hello says `private`): such a room is found only by its code
  or its link.  "Join room" in the room panel joins the code typed there.
  A pilot who opens the page is in the
  lobby (the user's choice of default; it connects the browser directly to
  strangers', which the panel says), unless a link to a room brought the
  page -- that pilot has somebody to fly with -- or the pilot left the
  lobby: "Leave" is remembered until "Enter the lobby" is pressed again
  (`hall.auto`; `?lobby=off` keeps a page out once).  **Only a page with a
  game to fly connects to the lobby** (the user's, for the lobby server's
  health: every page in a lobby is a socket to the server, a sign of life
  twice a minute and an introduction to every other pilot, and a visitor
  who only looks at the page should cost none of that).  The page waits
  until the game files are loaded (`hall.enterWhenReady`, then
  `hall.gameChanged` from `setSource`), "Enter the lobby" is off until then
  and the panel says what is missing; a pilot whose game goes (the saved
  copy forgotten, another program loaded) is taken out and comes back with
  the next game.  A game to fly is one the hooks know: a file called
  `WC.EXE` is not enough, the executable of the chosen program has to be
  the build of the hooks' own table (`knownBuild` in `web/gamefiles.js`,
  the same string at the same place of the data segment that
  `game_program_loaded` looks for, read from the file; the registry's
  `build`).  Rooms are not held to this: a link to a room joins it at
  once, so that the host sees the wingman and can say where the files go.
  Flying leaves the lobby
  (`hall.shut` in `start()`: a lobby of pilots who are away is no use to
  those looking for one), and when the game is over on the page, or the
  page is loaded again, a pilot who was in it is back (`flightOver`).
  The connection is `hall.js`'s own, on the lobby server's signaling
  protocol, not the lobbylink client's:
  * links are direct (STUN only) and go through the server's TURN relay only
    when that has failed: the relay has ports for some dozens of links, and
    a full lobby would take them from the games;
  * every pilot is linked to every other, so a newcomer to a full lobby is
    offered dozens of links at once.  The offers are spread over a moment
    (25 ms a pilot present): the server drops a socket with more than a
    hundred messages waiting;
  * a seat whose page went away without leaving stays taken as far as the
    server knows, and a room of strangers fills up with those.  The room
    is made with `allowReplacement` and a `claimAfterMs` of 150 s: a
    newcomer who finds it full claims a seat that has been silent that
    long, and every page sends the server something twice a minute so that
    its own is not (the server counts any message and has no ping, so the
    answer is "unknown message type" every time);
  * the server ends a room a day after it was made (or five minutes after
    the last pilot left): the pages take seats again, the first one making
    the room anew.
  Whoever makes a room decides its options, which a page that keeps no
  rules could abuse (a lobby of one seat), and seats can be filled; the
  server has no reserved rooms.  The row of lobbies is the answer to both
  (the user's): the others land in the next one.  A full lobby is first
  asked for a silent seat, 32 seats at a time (the server answers every
  question in order, so a batch costs one round trip), which is what
  keeps the first lobby from filling with pages that are gone.  `scripts/web-smoke.sh hall` runs the rules in Node
  (`scripts/web-chatfilter-test.mjs`) and the lobby in headless pages
  (`scripts/web-hall.mjs`: no socket to the server without a game or with
  a `WC.EXE` that is not the game, in with the game and out when it goes,
  a room's link that waits for the game, another lobby server by the
  address and by Options, talking, the rules on both sides, a room offered
  and joined by a click, codes for other games, a full lobby, the next
  lobby and a claimed seat).
* **The page's looks** (`web/index.html`) are a cockpit display's: green on
  black, amber for what matters, corner brackets on the panels; game files
  and room on the left, the lobby beside them (Send, Advertise and Leave in
  one row), the room's chat beside its roster, the game below.  Flat
  colours only: the emulator runs in the page's own thread, and whatever
  the browser paints around the game's picture is time the game does not
  get.  (Measured in a headless page, the game in view: the layout costs
  the same as the old one.  With the picture out of view the browser paints
  nothing at all, which is why a test window too small to show it reports
  a faster game.)
* **The lobby server checks the page's origin**; the public server accepts
  its own host and the deployed page's (`https://fireflyk64.github.io`).
  Serve the page from there or run a lobby server with `--allowed-origin`
  for the page's origin.
* **Any lobbylink server will do** (the user's, for the day the public one
  is down: the server only introduces the browsers, and pilots can agree on
  another).  `?server=URL` in the page's address, or "Lobby server" under
  Options, is the server of the public lobby, of the rooms and of the game
  (`serverUrl` in `web/wc.js`; an address without a scheme is https).  A
  server that is not the public one is named at the top of the page and is
  in every link the page makes -- its own address once a room is joined or
  the field changed, and the room codes of the lobby (`roomLink`) -- so
  that those who follow a link meet there; changing the field moves a
  pilot who is in the lobby, or could not get into it, to the new server's
  (`hall.serverChanged`).  A server that does not answer is said with the
  way to another (`OTHER_SERVER`).  It is not remembered from one visit to
  the next: the address says which server a page talks to, and a link
  cannot change that for good.  The other server needs `--allowed-origin`
  for the page's origin, https (a page served over https may not open a
  `ws:` socket), and a TURN relay of its own for the pilots who cannot be
  reached directly.  Pilots on different servers do not see each other,
  and the lobby passes no links, so the new address travels some other way.

## 7. Known limitations and follow-ups

* A client whose machine cannot hold the server's frame rate applies more
  than one server frame per frame of its own: the world keeps the server's
  pace on its screen, but its own ship flies slower than the others (WC1
  moves per frame).  In the high-latency mode (section 3b) the converse
  too: a wingman paces itself at the host's nominal rate, so with a host
  that cannot hold it the wingman's ship flies faster than the host's
  world.  There is no extrapolation of remote ships between frames beyond
  what the game itself does with a ship's velocity.
* A server with no connected client blocks at the start of every frame until
  one connects (original behaviour); the same happens if the only client
  drops mid-mission.
* A client that died cannot rejoin the mission in progress; it gets the next
  briefing.
* Two client slots (network ids 1 and 3 when they connect; the slot each
  flies is decided with the mission's start state): rooms have two
  seats (three when the host ticks "3 players", marked beta, on making one), and a
  third pilot's ship is made by the host's game when the wing has none
  free (docs/wc2-port.md section 4).
* Rocks and mines are per machine (section 2): two pilots in the same field
  do not see the same rocks, and one can watch the other fly through a rock
  unharmed.
* Bolts fired by a remote ship are spawned on the client one frame later
  from the replicated position, so a fast-turning ship's shots can appear
  slightly off.

## 8. Reading the game

`scripts/wcdis.py` resolves any of the address forms used in this document
and disassembles with symbols from the IDC export:

```
scripts/wcdis.py sym do_damage           # ovr143:0A99, stub143:0084, DOS 12D7:0084
scripts/wcdis.py dis do_damage           # symbolized listing until the next name
scripts/wcdis.py dis ovr143:0EDB 0x300   # explicit range
scripts/wcdis.py callers stub143:0084    # every far call site, root image and overlays
scripts/wcdis.py entries stub143         # the stub's thunk table
scripts/wcdis.py names dseg              # all named data
```

Facts the tool encodes that are easy to get wrong by hand: overlay code is at
`FBOV + 0x10 + stub.fileofs` in `WC.EXE`; far-call segment operands inside
overlay code are `8 * segment-index` (index N is IDA's `segNNN` or
`stubNNN`), patched by the overlay loader; IDA segments may start unaligned
(`seg001` starts at 0x3BE8 with paragraph 0x3BE), so offsets are relative to
`paragraph * 16`; DOS segment = IDA segment + 0x1A2.
