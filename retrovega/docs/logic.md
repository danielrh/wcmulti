# The game's logic: the guide to src/logic.c

`src/logic.c` is where the game's play is written by hand: ships moving,
guns firing, damage, the AI, the mission. Everything else in the flight
(drawing, the cockpit, the music, the asteroid fields) is already there
and shows whatever state the logic leaves in the world.

The file starts empty. Its main loop is a row of named steps with nothing in
them, and its thunk bodies return at once. Two things are already done:

- `logic_start` puts the mission's ships into their slots.
- The thunks whose whole job the ABI fixes are written; they are marked
  PLUMBING.

The first flight's stand-in code is kept in git, for reference: the player's
steering and throttle, the stand-in AI, and the first cut of the damage
model. `git show 65bc2a7b:retrovega/src/flight.c` has them, in `ship_physics`
and `do_damage_impl`.

[docs/gameplay.md](gameplay.md) is the road map to go with this reference: the
order to write the play in, nine milestones from the flight model to the
mission; the memory map; where victory and loss are kept; the mission format;
and the play tests (`tests/play/`), which fail now and pass as each piece is
written, on the host and in DOSBox.

This guide covers:

- the frame: where your code runs and what runs around it;
- the world: the slot arrays, their units, and the helpers for them;
- each step of the loop, and the helpers it needs;
- the shots: the bolt list kept outside the slot table, and why it is safe;
- the rules the multiplayer layer needs kept;
- memory and time: what is tight;
- asteroid fields;
- what the engine still lacks;
- the sized types, and the host harness (rvsim): the logic on Linux and in a browser, under gdb, rr, valgrind and
  the sanitizers.

## Files

| file | what | whose |
|---|---|---|
| `src/logic.c`, `src/logic.h` | the main loop, the HUD hook, the autopilot, the thunk bodies, `g_pilot` (the controls) | **yours** |
| `tests/play/` | the play tests: what the logic has to do, a check at a time, on the host and in RVP.EXE (docs/gameplay.md) | tests, yours to change |
| `src/world.c`, `src/world.h` | names for the slot arrays' values; vector, orientation and slot helpers; spawning; the gameplay random stream | helpers |
| `src/shots.c`, `src/shots.h` | the gunfire list: add, move, hit test, the do_damage stand-in, and the drawing | helpers |
| `src/hud.c`, `src/hud.h` | drawing over the view, clipped to the canopy opening | helpers |
| `src/collide.h` | collisions: ship against ship, point and segment against a ship's hull | helpers |
| `src/flight.h` | the camera, and where a ship is on the screen (`flight_ent_box`) | helpers |
| `src/rvhooks.h` | the slot arrays and scalars (generated from `abi/rvabi.def`), and the thunk prototypes | the ABI |
| `src/rvtypes.h` | `int16_t`, `uint32_t` and the rest: the types to write in (see "Sized types") | helpers |
| `host/` | rvsim, the host harness: the logic built for Linux or a browser (see "The host harness") | the harness |
| `src/campaign.h` | `ship_type(type)`, `weapon(type)`, the mission being flown (`g_camp`) | data |
| `src/panel.h` | `g_sys`, the cockpit's systems: target, lock, master arm, afterburner, decoys, fuel | the cockpit's |
| `data/weapons.csv`, `data/ships.csv` | weapon numbers and looks; each ship type's guns, turrets and missiles | data |

## The frame

The flight loop is `src/rvflight.inc`, at RVHOOK's fixed sites, with its C
side in `flight.c`. Each time round it runs these, in this order:

| when | what | whose |
|---|---|---|
| `flight_keys` | the keys and the pointer into `g_pilot` (steering, trigger, missile key, afterburner), the throttle keys into `setSpeed[0]`, the cockpit's own keys | flight.c |
| `flight_frame`: `frameCounter++`, then **`logic_frame()`** | **one step of play** | **logic.c** |
| | the test scenes' scripted ships (only with `RVT.EXE`'s switches) | flight.c |
| `rocks_frame` | the asteroid field: rocks flown, the player's hits on them (`do_damage`, then a toss aside) | rocks.c |
| `plan_draws`, `missiles_frame`, `shots_place` | what will be drawn this frame, through the player's camera | flight.c |
| `gather` | the cockpit's state from the slots: shields, armour, speed, radar contacts, target, lamps (`systems_tick` keeps `g_sys`) | flight.c |
| `scene_draw` | the picture, a band of rows at a time: sky, stars, far shots, rocks, ships, near shots, missiles, then **`logic_hud()`**, then the cockpit | flight.c |
| `rvFrameDone`, the wait | the frame is flipped; it then waits out the rest of 1/`LOGIC_HZ` s (30 a second) | flight.c |

`logic_autopilot()` runs between two frames when the A key is pressed (the
autopilot site, which the layer steps over on a client): it returns 1 when it
took the player on to the next nav point.

`logic_start(m)` runs once, while the flight loads. It runs before the
ships' art is read, so the types it spawns are the types that get loaded.
`logic_end()` runs once, after the loop.

**One step is one frame.** Everything moves per step, as in WC1. There is no
variable timestep anywhere (spec §6.1). With the multiplayer layer, the
host's frame pacing sets the rate instead of the 30 fps cap.

## The world: slots

The world is the fixed block's slot arrays (`rvhooks.h`): 128 slots, each
array one element per slot.

| slots | hold |
|---|---|
| 0 | the player (the camera and the cockpit are slot 0's) |
| 1 .. `RV_MAX_SHIP_SLOT` (31) | ships, capital ships, missiles (`ship_spawn` refuses any slot past them, returning -1) |
| 32 = `SHOT_PROXY_SLOT` | the shots' stand-in bolt for `do_damage` (below) |
| 33 .. 117 | temporaries, all free: keep them so (see the rules) |
| 124 down to 118 | the asteroid and mine fields' hazard slots, one a field (rocks.c) |
| 125 | the camera; 127 the scratch vector |

The arrays a ship uses:

| array | type | meaning |
|---|---|---|
| `maybeEntityType[s]` | word | `ET_SHIP`, `ET_CAPSHIP`, `ET_MISSILE`, `ET_NONE` ... (world.h). The drawing shows every slot of `ET_SHIP` and `ET_CAPSHIP`; missiles are drawn from `ET_MISSILE` slots |
| `gInstanceShipTypes[s]` | word | the ship type (`data/ships.csv`); `slot_type(s)` gives its numbers |
| `commAnimInfo[s]` | word | the side: `SIDE_TERRAN`, `SIDE_AERA`, `SIDE_RLAAN` |
| `rvController[s]` | byte | who flies it: `CTL_AI`, `CTL_PLAYER`, `CTL_PUPPET`, `CTL_SCRIPT`, `CTL_FROZEN` |
| `gPositionVector[s]` | RvVec3 | position, **256 = 1 km** |
| `gOrientationFrontVector`, `...RightVector`, `...UpVector` | RvVec3 | the ship's frame, **256 = 1.0**; left-handed: x right, y up, z ahead |
| `gVelocityVector[s]` | RvVec3 | **positions a step** |
| `setSpeed[s]`, `speedLo[s]` | dword | throttle and current speed, kps |
| `curShield` (`SHIELD(s,k)`), `shieldMax` (`SHIELD_MAX`) | words | k: 0 front, 1 rear |
| `armorState` (`ARMOR(s,k)`) | words | k: 0 front, 1 rear, 2 left, 3 right |
| `damagePoints[s]` | word | hull damage taken (the type's `hull` is the most) |
| `coreHp`, `gunDamage`, `engineFlag` | bytes | free for your damage model (WC's names: core hits, gun damage, engine disabled 0xff) |
| `gunTable` (`GUN_COUNT`, `GUN_TYPE`, `GUN_MOUNT_X/Y`, `GUN_STATE`) | 51 bytes | the guns (below) |
| `gunEnergy[s]` | word | gun energy, 0 .. 100 |
| `shipAiState[s]` | word | your AI's state; **9 = dying** (the layer reads it) |
| `damageProperty[s]` | word | spare per ship (WC's "AI timers") |
| `missileTarget[s]`, `rvTarget[s]` | byte | what a missile homes on, what a ship targets; 0xff none |
| `parentShipForShip[s]` | byte | a missile's or a bolt's launcher; 0xff none |
| `rvAfterburner[s]` | byte | lit (the flames draw longer; flight.c sets the player's from Tab) |
| `formationOffset[s]` | 3 words | right, up, ahead of the leader, km |
| `rvFlags[s]` | word | 1 invulnerable, 2 local-only hazard, 4 docked, 8 jamming, 16 cloaked |
| `slotMissionShip[s]`, `shipStateByte[s]` | | the mission ship record it came from; its pilot (0..15 the cast, 8 a human, 0xff none) |

Scalars you will use: `frameCounter` (steps, +1 each, nothing else
changes it), `missionStatus` (0 flying; set 1 landed, 2 ejected, 4 dead, 5
exit to end the flight), `playerCanBeDamaged`, `victoryPoints` (nonzero:
won, read after the flight; until your logic sets it, it is the 80% roll made
at launch in game.c), `randomSeed`, `currentNavPoint`, `tempVector` (a
spare near vector for `do_damage`'s `where`).

### Units and the integer maths

- **Positions:** 256 a km, in `int32_t`.
  - A ship's half box is its type's `radius_km` (`collide_half(s)`, in positions): the old game's sizes, 100 km for a light fighter, 160 for a Nicander, 700 for the carrier (ships.csv). The sprite is drawn full size where its width is that (625 km for a light fighter).
  - Ships spawn 4,000 to 12,000 km apart.
- **Directions:** 256 = 1.0.
  - `vec_turn(a, b, t)` turns a pair of axes; `ship_orthonormalise(s)` squares the frame up again. Do it every step a ship turns.
- **Angles:** 1024 a turn (`TURN`).
  - `lf_sin` and `lf_cos` return × 16384.
  - `lf_atan2(y, x)` returns 0..1023. Keep its arguments under 2^16: halve them until they are.
- **Speeds:** `KPS_STEP(kps)` is positions a step: kps × 256 / `KPS_HZ`.
- **Turn rates:** degrees a second × `TURN` / 360 / `KPS_HZ` × steer / 9 is a step's turn. The first flight's `turn_step` does exactly this.
  - `TURN_STEP(dps, steer)` (world.h) gives it in 1/256ths of a unit (`TURN_FRAC_SHIFT`) with a multiply and a shift. Carry the fraction from step to step: the slowest ships turn under a unit a step.
  - It is approximate: 18 of the 4,864 rates and steers come out 1/256 of a unit off the exact quotient, the same on every machine. Its comment in world.h says what changing it takes, and how to make it exact.
- **Dividing by a constant:** a 32-bit `/` is a runtime call on DOS (`__I4D`). `QCONST(num, den, q)` makes num / den a fixed-point constant at compile time, and `QMUL(x, k, q)` multiplies by it, rounded. Keep a plain `/` where the exact quotient matters.
- **`KPS_HZ` (20) against `LOGIC_HZ` (30): a decision for you.**
  - The spec converts a kps at 20 steps a second; the flight steps 30 times a second. Everything therefore moves and turns 1.5 times its nominal rate. The first flight did too, and its feel (rocks, dodging) was tuned at that.
  - Set `KPS_HZ` to `LOGIC_HZ` in world.h if you want kps to mean km a second.
- **An `int16_t` is DOS's `int`, an `int32_t` its `long`** (see "Sized types"): products of positions overflow fast.
  - Turn positions into km (/ 256) before squaring or dotting: `ship_rel` and `vec_norm` work in km for that reason.
  - `sin` × a distance in positions overflows; take the distance in km.
- **`game_rand()`** is the gameplay random stream: everything that changes play (spread, AI choices, damage rolls). It uses `randomSeed`, which the flight seeds per mission and save and the layer re-seeds before it replays a spawn or a hit. Anything that only looks different (sparks, flicker) must use its own generator, never `game_rand`.

### World helpers (world.h)

| call | does |
|---|---|
| `vec_dot`, `vec_norm`, `vec_cross`, `vec_turn` | vector maths on `RvVec3` (`vec_turn(&front, &right, t)` yaws right; `(&front, &up)` pitches up; `(&right, &up)` lifts the right wing) |
| `ship_orthonormalise(s)`, `ship_face(s, yaw, pitch)` | square a ship's frame; set it facing a way, level |
| `slot_is_ship(s)`, `slot_type(s)` | a ship or capital ship in 0..31; its type's numbers (speeds, turn rates, shields, armour, hull, `hit[]` box, `loadout`) |
| `ship_rel(from, s, &v)` | where s is as `from` sees it: right, up, ahead, in km (an AI's eyes) |
| `player_km(s)` | km from the player, roughly |
| `ship_spawn(s, type, side, controller, x, y, z, yaw)` | a ship into slot s: its type's speeds, shields, armour and gun table (`ship_guns`); everything else reset |
| `ship_clear(s)`, `ship_free_slot()` | empty a slot (nothing shown); the first empty ship slot |
| `game_rand()` | 0..32767 from the gameplay stream |

## Sized types: the same numbers on DOS and on a host

The logic and every module the host harness shares with RV.EXE are written in
`rvtypes.h`'s sized types, never plain `int`, `long` or `short`:

| write | on DOS (Watcom) | for |
|---|---|---|
| `int16_t`, `uint16_t` | `int`, `unsigned` | counters, slots, angles, the slot table's words |
| `int32_t`, `uint32_t` | `long`, `unsigned long` | positions, products, speeds |
| `int8_t`, `uint8_t` | `signed char`, `unsigned char` | the slot table's bytes, gun mounts |
| `INT32_C(256)`, `UINT32_C(1)` | `256L`, `1UL` | 32-bit constants (`256L` is 64 bits on a 64-bit host) |

- A `long` is 64 bits on a 64-bit host, so a product that overflows on DOS would not overflow there. As `int32_t` it is 32 bits everywhere.
- The switch changed nothing on DOS: RV.EXE built from them is byte for byte the one before. Watcom's `int16_t` is `short`, the same 16 bits as its `int`.
- `rvhooks.h` and `rvdata.h` are generated in these types (tools/rvabi.py, tools/rvdata.py).
- Watcom's `char` is unsigned; the host build says so too (`-funsigned-char`).
- Compilers without `<stdint.h>` (Watcom 11 and older, Turbo and Borland C, Microsoft C before 2010) get `rvtypes.h`'s own typedefs. `RV_NO_STDINT` forces them.

**What sized types cannot make the same: C does its arithmetic in `int`.** That
is 16 bits on DOS and 32 on a host, and an `int16_t` or `uint16_t` is widened to
`int` before any operator. So an expression differs whenever its value passes
16 bits before it is stored:

| a, b are `int16_t` or `uint16_t` | DOS | host |
|---|---|---|
| `a = 300, b = 200; int32_t p = a * b;` | -5536 (wrapped at 16 bits, then widened) | 60000 |
| `a = 1, b = 2` (`uint16_t`); `int32_t d = a - b;` | 65535 | -1 |
| `if (a + b < 0)`, a and b near 30,000 | true | false |
| `(a * b) >> 8` | the wrapped product, shifted | the whole one |

The rules that make them the same:

- **Store a 16-bit result before using it again:** `int16_t c = a * b;`, then use `c`. The store wraps the host's value as DOS wraps it.
- **Do wide arithmetic in `int32_t` on purpose:** `(int32_t)a * b`, never `a * b` in the hope that it fits.
- **Cast a `uint16_t` difference back:** `(uint16_t)(frameCounter - then)` for a timer that wraps.
- **`%d` takes 16 bits on DOS, `%ld` 32:** pass an `int16_t` to `%d` and an `int32_t` to `%ld`. The host's log reads both as an `int`, so it cannot catch a mismatch.

The checks catch what slips through:

- UBSan stops on a 32-bit signed overflow, which DOS would wrap silently.
- clang's truncation check (`CC=clang host/build.sh`) stops when a value is stored into an `int16_t` or `uint16_t` it does not fit: a 16-bit overflow, or a shield going below 0 to 65534. It catches what is stored, not the middle of an expression; the rules above cover those.

## The main loop, step by step

`logic_frame()` calls nine empty functions in order. The order matters: a
ship decides, then fires from where it is, then moves; then the shots move
and hit what is where it now is.

1. **`step_timers`**: what runs down or builds up by itself.
   - The guns' cooldowns: `GUN_STATE(s, g) & 0x7F` counts steps down to 0. Keep bit 7, which marks the gun destroyed.
   - `gunEnergy` back toward 100, and the shields back toward `SHIELD_MAX`.
2. **`step_minds`**: each ship slot, by `rvController`:
   - `CTL_PLAYER` flies by `g_pilot`: `steer_x` and `steer_y` (-9..9: + turns the nose right, + pulls it up), `roll` (+ lifts the right wing), `afterburner`.
   - `CTL_AI` calls the thunk: `aiFighterThink(s)`, or `aiShipThink(s)` for `ET_CAPSHIP`. Always the thunk, so the layer can skip it on clients.
   - A mind leaves its decision somewhere `step_motion` reads: a per-slot turn array of your own (the stand-in used `g_turn[s][3]`), and the throttle through `aiSetSpeed(s, kps)`.
   - `CTL_PUPPET`: nothing; the layer writes it. `CTL_FROZEN`: nothing. `CTL_SCRIPT`: the mission's moves.
3. **`step_fire`**: the fire loop. Who pulls a trigger this step: the player with `g_pilot.guns && g_sys.armed`, `g_pilot.missile`, and the AI's own choices. Each gun that may fire goes through the `fireGunFromShip(s, g)` thunk, or `maybe_fire_all_guns(s)` for all of a ship's (see "Firing").
4. **`step_motion`**: every ship flies its step.
   - The turn: `vec_turn` by the type's rates, then `ship_orthonormalise`.
   - The speed toward `setSpeed`, by the type's `accel`. The afterburner's speed is `ab_kps` while `rvAfterburner[s]` is lit.
   - `gVelocityVector` = front × speed (`KPS_STEP`), then position += velocity.
   - A `CTL_PUPPET` only coasts: position += velocity.
5. **`step_shots`**: `shots_move()`, then for each shot `shot_hit(i, &slot, &where)`. On a hit: `do_damage(shot_source(i), slot, shot(i)->damage, RV_WHERE(&where))`, then `shot_remove(i)`. Walk the list backwards when removing.
6. **`step_missiles`**: missiles are ship slots of type `ET_MISSILE`. They steer like ships, toward `missileTarget[s]`, and burst on a proximity test (`collide_point` or a distance), through `do_damage`.
7. **`step_collisions`**: `collide_coarse(a, b)`, then `collide_ships(a, b, &push, &depth)`. Move b by `push` (or each by half), and charge both through `do_damage`.
8. **`step_deaths`**:
   - A ship whose hull is gone gets `shipAiState[s] = 9` and keeps its slot while its explosion lasts. Then `despawn(s)`, or `delayedDespawn(killer, s)`.
   - The player's death sets `missionStatus = 4`.
9. **`step_mission`**: nav points reached (`enterNavPoint(nav)`), waves spawned (`outerSpawnShipEntity(record, nav)`), objectives, and `victoryPoints`.

`logic_hud()` is called after the world is drawn, once per band of rows. It
may only draw with hud.h's calls (below).

## Firing

### The data

- **`data/weapons.csv`** is packed into RVGAME.DAT; `weapon(type)` returns an `RvWeapon`:
  - `kind`: `RV_WK_GUN`, `TURRET`, `MISSILE`, `TORPEDO`, `ROCKET`, `MINE`, `SPECIAL`.
  - `look`: how its shot is drawn, below.
  - `lock`: none, heat, image, auto or proximity, and `lock_tenths`.
  - `damage`, `speed_kps`, `range_km` (the csv's thousands of km: 3.0 is 3,000 km), `refire` (steps), `energy`.
- **The `look` column is new.** My first pass at it, to change at will:
  - Terran: lasers, particle cannons and disruptors are bolts; mass drivers slugs; the ion cannon plasma.
  - Aera: green stars.
  - Rlaan: pink fire, the Ktek Beam a beam.
- **A ship type's gun table** (`RvShipType.loadout`, packed from ships.csv's `guns`, `turrets` and `missiles` columns):
  - one record per gun and per turret, and one launcher per missile kind with its rounds;
  - `ship_spawn` copies it into the slot's `gunTable`;
  - each record is `GUN_TYPE` (the weapon), `GUN_MOUNT_X/Y` (1/32 of the half box, across and up) and `GUN_STATE` (bit 7 destroyed; a gun's cooldown; a launcher's rounds).
- **The mounts are placeholders.** The guns spread over the middle 60% of the hit box's width, a little low; turrets go above, launchers under the belly. Vega Strike's real mounts are in its `units.csv`, as `tools/hitboxes.py` already reads them for the vitals.
- **Ten records at most a ship.** The Leonidas lists 12, so its table keeps the first 10.

### The contract of `fireGunFromShip(ship, gun)`

It fires that gun **unconditionally**: the decision is the caller's. The
layer replays this call on every other machine, with the gun record as the
shooter's machine had it, so the body must not refuse because of cooldown or
energy. It sets the cooldown and spends the energy itself.

- **A gun or a turret** puts a shot in the world: `shot_add(ship, weapon_type, &muzzle, &velocity)`.
  - The muzzle: the position + right × mount x + up × mount y (scaled by `collide_half(ship) / 32`) + front × the nose (`collide_half / 127 × slot_type(ship)->hit[2]`).
  - The velocity: front × `KPS_STEP(speed_kps)`, plus the ship's own velocity if bolts should inherit it.
  - The shot's life is the weapon's range at its speed.
- **A launcher** puts a missile into a free ship slot: `ET_MISSILE`, `parentShipForShip` the ship, `missileTarget` its target. It returns that slot, which the layer maps across machines. Otherwise it returns 0xFFFF.
- **Lead, don't aim** (the user's ruling, 2026-10-20): shots fly straight at a finite speed and hit only by meeting the target. No hitscan, no auto-aim.

`maybe_fire_all_guns(ship)` is the "every ready gun" decision, a group of one
weapon type at a time (the linked lasers): the group fires when all of it is
cooled down and the energy covers all of it, or none of it fires, so linked
guns never fire lopsided. It calls the thunk for each gun that fires.

## Shots: the bolt list outside the slot table

The slot table has 110 or so temporaries, and the layer never needed bolts
to be slots:

- It replicates **a shot** as the `fireGunFromShip` call that made it: each machine runs the call and flies its own copy.
- It replicates **a hit** as the `do_damage` call it led to.

So shots live in their own list, `SHOTS_MAX` (192) at once. That list is
the side channel for "more bolts than slots".

| call | does |
|---|---|
| `shot_add(owner, weapon, &pos, &vel)` | a shot of that weapon (look, damage and life from weapons.csv), its head at pos (positions), moving vel (positions a step). Returns its index, or -1 when full |
| `shot(i)` | the `Shot` record, to read or change: `x y z`, `vx vy vz`, `damage`, `life`, `owner`, `weapon`, `look`, `hx hy hz` (the drawn half length) |
| `shots_count()`, `shot_remove(i)` | the last shot takes the removed one's index, so walk the list backwards |
| `shots_move()` | every shot a step of its velocity and of its life; spent ones are removed |
| `shot_hit(i, &slot, &where)` | the first ship, other than its owner, that its last step went into (its head less its velocity, to its head). A coarse box test, then the silhouette hull (`collide_segment`). Gives the slot and the first point inside |
| `shot_source(i)` | the slot to name as `do_damage`'s source (below) |

**Drawing is automatic.** Each frame the list is copied into the bolts'
and balls' pools:

- bolt and beam looks are drawn as capsules near and lines far;
- slug, plasma, fire and star are the ball sprites;
- far shots go behind the ships and near ones in front.

The shot and hit counts are logged at the flight's end: `shots: N fired, M hits`.

**The stand-in bolt: `shot_source(i)`.** The layer reads `do_damage`'s
source slot to learn who fired:

- a bolt (`ET_BOLT`) whose `parentShipForShip` is the shooter means "gunfire from that ship";
- anything else is ramming or a hazard;
- between two human pilots, a ship slot as the source would even be taken for a harmless wing bump.

So `shot_source` writes `SHOT_PROXY_SLOT` (slot 32) as a bolt of the shot's
owner, weapon and damage, and returns 32. It stays valid until the next
`shots_move()`. One slot is enough, because the layer reads it inside the
`do_damage` call.

**What it costs** (natively at 40,000 cycles):

- Drawing: a far bolt about 8 µs, a near one about 43 µs; a ball 3.5 µs.
- The hit test: a coarse test per ship per shot, a few instructions; a segment through a ship's box (`collide_segment`) about 81 µs.
- Memory: 192 shots and their draw pools take about 30 KB of conventional memory while flying.

## Damage: `do_damage(src, dst, qty, where)`

Every hurt goes through this thunk: shots, rams, missiles, and the rocks
(rocks.c calls it, with the field's hazard slot as `src`). `where` is a near
pointer to an `RvVec3`: pass `RV_WHERE(&v)` for a vector of yours (a local
works: the stack is in DGROUP), and in `do_damage_impl` read it as
`RV_WHERE_VEC(where)`. On DOS these are plain casts; the host harness maps the
16-bit pointer to its own memory. Only read it there. It returns what it did,
0 for nothing.

The rules the layer needs:

- **It must not depend on `src` for the amount.** The layer replays a hit on the other machines with `src` set to a bolt of the shooter's, the shooter's ship, or 0xFFFF. Use `src` only for blame and effects.
- **Ignore slot 0 while `playerCanBeDamaged` is 0.**
- **Randomness only from `game_rand()`.** The layer re-seeds before it replays.
- **A kill:** set `shipAiState[dst] = 9` and keep the slot until the explosion is over. That is how the layer learns of a death.

What it can spend, all in the slot arrays:

- `SHIELD(s, 0/1)`: front and rear;
- `ARMOR(s, 0..3)`: front, rear, left, right;
- `damagePoints` against the type's `hull`;
- `coreHp`, `gunDamage`, `engineFlag`, and each gun's destroyed bit;
- the player's cockpit damage: `rvCockpitDamage`, a bit per overlay. Bits (tools/cockpits.py): 0 left MFD, 1 right MFD, 2 radar, 3 shields, 4 armour, 5 hull, 6 speed, 7 fuel, 8 propulsion, 9 power, 11 engine, 12 nav, 13 comms, 14 IFF, 17 weapon controls, 20 ejection, 23-25 canopy. A set bit shows the cracked overlay, and some panels go dark with it.

## Queries: is there a collision, where is it on screen

| call | answers |
|---|---|
| `collide_coarse(a, b)` | the two ships' bounding spheres touch |
| `collide_ships(a, b, &push, &depth)` | the user's fine test: the smaller ship's hull against the bigger one's vitals. `push` is the least move of b that frees them |
| `collide_point(s, &p, &push)` | a world point inside ship s's hull |
| `collide_segment(s, &a, &b, &hit, &t1024)` | a segment (a step of a shot or a missile) into s's hull: the first point, and how far along it is |
| `collide_half(s)` | s's half box in positions (its size for any coarse test) |
| `flight_camera(&cam)`, `flight_ent_box(s, &cam, &box)` | the player's camera; ship s on the screen: its sprite's box, its middle, a steady half size, the way to it when it is off the screen (`dir`), camera-space X Y Z, and whether it is ahead and visible |

## Drawing: you don't draw the world

Put things in the world and the flight draws them: ship slots, missile
slots, the shots, the rocks and the sky. Two things are yours:

- **`logic_hud()`**, over the view, with hud.h's calls:
  - `hud_run(xa, xb, y, colour)`, `hud_dot`, `hud_vline`, `hud_line`, `hud_cross(x, y, arm, colour)`, `hud_brackets(x0, y0, x1, y1, colour)`, `hud_diamond(x, y, r, colour)`;
  - each is clipped to the band being drawn and to the canopy opening. The cockpit is drawn over the view afterwards;
  - it is called once a band, about four times a frame, so keep it light. Work out the marks in the step and draw them here;
  - colours: `UI_WHITE`, `UI_GREY1`..`6`, `UI_AMBER1`..`4`, `UI_HILITE` (cyan), `UI_RED` (rvasset.h).
  - A gunsight: a cross at `flight_camera`'s `cx, cy`; the view's middle is where the nose points.
  - Target brackets: `flight_ent_box(g_sys.target, &cam, &b)`, then `hud_brackets` round `b.sx ± b.half`.
- **The cockpit's own displays** come from the slots by themselves:
  - the radar's contacts (ship slots, coloured by side and IFF);
  - shields, armour and speed (slot 0's);
  - the right MFD's target view (`g_sys.target`);
  - the lock lamp (`g_sys.lock`).

  The MFDs' text lines are flight.c's (`g_st.mfd_text`); there is no call yet for the logic to put text there.

## Talking and music

- **Comm faces:**
  - `comms_say(voice, vdu, text)`: `vdu` is 0 for the left MFD, 1 for the right; a NULL text says the voice's own line.
  - `comms_voice(person)` is a cast member's voice (people.csv order); `comms_voice_for(side, type)` an Aera or Rlaan pilot's.
  - `comms_on(vdu)` tells whether one is showing.
  - Nothing is said while `g_sys.muted`.
- **Music** follows the slots by itself (score.c), so the logic tells it nothing:
  - calm while all is quiet, a battle when an enemy is within 4,000 km;
  - a victory sting when an enemy that was near is gone (despawned), a tragic one when a friend is;
  - panic as the player's hull falls under 60%, 35% and 15%;
  - the way home once the enemies are all gone, or after the landing call: won or lost by `victoryPoints`.

## The rules the multiplayer layer needs kept

The layer (`src/cpu/wcnet_*`) runs WC1 and WC2 multiplayer by watching
their thunks and fixed data. **For RV.EXE it only recognises the program and
logs its thunk calls today.** Its RV column, the flight sites and the events
are still to be written (spec §5.3). Keeping these rules now is what will
let that work without rewriting your logic:

1. **State lives in the slot arrays.**
   - The layer writes positions, orientations, health and gun records between steps. Recompute anything derived from them each step, and square up orientations every step (`ship_orthonormalise`).
   - Private per-slot arrays of your own (an AI's memory, a turn array) are fine if they are reset when a slot is spawned, and if another machine never needs them. The AI runs only on the machine that owns the ship.
2. **Go through the thunks.**
   - Firing: `fireGunFromShip`, `maybe_fire_all_guns`. Damage: `do_damage`. The AI: `aiFighterThink`, `aiShipThink`, `aiSetSpeed`. Spawning: `outerSpawnShipEntity`. Endings: `despawn`, `delayedDespawn`, `despawnQuietly`. Nav points: `enterNavPoint`.
   - Call the thunk, never `_impl`. The layer hooks the thunk, skips calls on clients, and replays them from the host.
3. **`fireGunFromShip` never refuses**, and makes its shot only from the slot's state (above).
4. **`do_damage` must not depend on `src`** (above).
5. **A death is `shipAiState = 9`**, with the slot kept until the explosion ends. A missile's end may turn its slot into anything below `ET_MISSILE`.
6. **Missiles take ship slots** (1..`RV_MAX_SHIP_SLOT`). The layer finds a launched missile as the slot that became used during the `fireGunFromShip` call.
7. **Keep the temporaries free.** The layer drops a shot when fewer than five temporaries are free; the shots never use them.
8. **Honour `rvController`.**
   - `CTL_PUPPET` only coasts: no AI, no input, no motion of your own.
   - `setController` is how the layer turns a ship into a puppet and back.
9. **One gameplay random stream** (`game_rand`); visual randomness uses its own.
10. **`frameCounter` +1 a step and nothing else.** One step is one frame.
11. **Rocks and mines are each machine's own** (rocks.c): they are not slots, and nothing about them is sent. Their hits name the field's hazard slot (`rvFlags` 2) as `do_damage`'s source.
12. **New thunks** go at the end of `abi/rvabi.def`, never between: the thunk offsets are fixed for good. The build regenerates `rvhooks.h` and `rvhook.asm`, and you write `name_impl`. A thunk with no body gets a stub that logs "stub: name" once.

## Limits, memory and time: what is tight

**Fixed counts:**

| limit | value | where | note |
|---|---|---|---|
| ship slots | 9 + the player | `MAXSHIPSLOT` in rvabi.def | **The tightest.** Missiles count against it. The engine could take 47 (one line in rvabi.def), but the layer's own constant is 9 until its "phase 2" (spec §5.3.4). For single-player testing it can go up now |
| ship types a flight | 8 | `SHIP_SETS`, ships.h | the user's brief: up to 8 types at a nav point. Every type a flight may spawn must be loaded at its start: the mission's ship list is, waves included |
| ships drawn a frame | 16 | `PLAN_MAX`, flight.c | only matters past 9 slots |
| ships drawn exactly (turned and scaled) | 2 | `EXACT_MAX` | the rest are drawn from the pre-rendered sets |
| shots | 192 | `SHOTS_MAX`, shots.h | |
| missiles drawn | 6 | `MISSILE_MAX` | the first six missile slots |
| gun table | 10 records | spec §4.5 | |
| mission ships | 12 lines (`count=` each), 64 records | rvdata.py, missionShipTable | |
| nav points | 8 in a mission, 10 ships each in navPointTable | | the layer hard-codes 10 |

**Memory:**

- **DGROUP, the near data segment, has 3.2 KB free (of 64) in RV.EXE, and 608 bytes in RVT.EXE, the test build.** Every string literal in C lands there, even in `__far` code.
  - Use `static const char __far s_x[] = "...";` with `rv_logf(s_x, ...)`, as logic.c does.
  - Watcom binds `__far` to one declarator: write it before each name.
  - New tables and arrays: `__far`. Below about 200 bytes free, RV.EXE cannot open its files.
- **The stack is 8 KB:** no big local arrays.
- **Conventional memory:** a flight in an asteroid field has about 112 KB left after the rocks (137 KB before today's helpers).
  - Every KB of code is a KB of it: the executable is resident. Today's helpers cost 9 KB of code and their lists about 30 KB while flying (part of it from heap that was already free).
  - Hand-written logic will not be big. A few thousand lines of C is perhaps 30-40 KB, which fits, but it is the budget to watch.
  - The test-only code goes in `#ifdef RV_TESTS` (RVT.EXE), as flight.c's does.
- **XMS:** 62 MB in all. A flight uses 9-12 MB for two to four ship types' sets, 7 MB for the rocks near a field, 2.8 MB for the sky, plus the fonts and balls, and leaves 40+ MB.
  - The art, not the code, is what grows. New art (explosions, sounds) would go there too.

**Time** (a frame is 33 ms at 30 fps):

- Today's flight in a field takes 13 ms natively at 40,000 cycles, with 3 ships, 366 rocks and the cockpit. The page runs 50,000 cycles at about the same speed per cycle.
- So the logic has roughly 15 ms a frame, before the frame rate drops, natively and in the page.
- In DOSBox an instruction costs about a cycle, whatever it is (`imul` and `idiv` included), so count instructions, not their kinds.
- Watcom's 32-bit maths in 16-bit code costs about 10 instructions a product.
- Per-element 32-bit work over hundreds of things (rocks, stars) belongs in asm (see rocks.asm). For 10 ships and 200 shots, C is fine.

**Watcom pitfalls met before:**

- C89: declarations before statements.
- `near` and `far` are keywords: don't name variables that.
- `-ox` once miscompiled an `if (s == 0) ... else` in the physics. When C behaves impossibly, read `wdis` output.
- A fixed-block address with a constant index passed as a **far** pointer gets segment 0, not DS:
  `_fmemcpy(&v, &gPositionVector[0], 12)` reads the interrupt table. Near calls (`memcpy`, the helpers) and a
  variable index are safe (docs/gameplay.md, "Watcom and DOS traps").
- `build.sh` does not stop on a C error: check `RV.EXE`'s time after a build.

## Asteroid fields

What is there:

- A field (`missions.txt`'s `field asteroid x z r density=N`, or `RV.EXE /field D` for every flight) keeps a bubble of rocks round the **player** only, 9,000 km across. The rocks fly at the player's lead point.
- rocks.c finds the player's own hits itself and calls `do_damage` with the field's slot. With today's empty `do_damage`, rocks toss the player aside but do no harm until you write it.
- A hit breaks the rock into spinning pieces.

What combat in a field still needs (none of it exists yet):

- **Shots against rocks:** a call such as `rocks_segment_hit(&a, &b, &rock)` that breaks the rock the shot meets. The rocks are each machine's own, so this needs nothing from the layer.
  - It must be fast. One C loop over all the rocks per shot would be 366 × 192 tests. It wants the same per-row trick as the rocks' asm: rocks within reach of each shot found once a frame.
- **The AI near rocks:** a "nearest rocks to this point" query, so an AI ship can steer clear. Only rocks near the player exist, so AI ships elsewhere fly through empty space. That matches WC1, where each player's field was their own.
- **AI ships and rocks:** whether an AI ship near the player is hurt by the player's rocks.
  - In WC1 the layer has the client report such hits (`report_local_hit`). In single player, rocks.c would need the same hit test for ship slots, not only slot 0.

## What the engine still lacks

The scan for this guide, most needed first:

1. **Shots against rocks, and the AI's rock query** (above).
2. *(Done 2026-10-28: explosions and sound effects come by themselves from the world: a ship at `shipAiState` 9
   explodes, a shot sounds its weapon, a hit throws sparks, a missile launches and bursts. engine.md, "Explosions and
   sound effects"; keep a dying ship's 9 to a step's end.)*
3. *(see 2)*
4. **Capital ship art.** Only the Vigilance exists, as a test aid (`/capital`). The roster's other capital ships have no sprites, and turrets have no aiming code.
5. **Ship slots beyond 9:** rvabi.def plus the layer's phase 2.
6. **The layer's RV.EXE support:** its column of offsets, the flight sites and the replication. Today it only logs.
7. **Mission flow:**
   - nav points carry no hazards or waves into the flight (the placeholder spawns the whole mission at once);
   - the autopilot is the logic's to write (`logic_autopilot`);
   - landing is only the C key's call to the carrier;
   - ejection only sets `missionStatus`.
8. **Text on the MFDs from the logic**, such as a weapon or missile display: no call yet.
9. **Smaller items:**
   - every bolt is drawn in one colour (red), whatever its weapon;
   - a beam is drawn at most 4,000 km long;
   - mines have no art;
   - gun mounts are placeholders.

## Building and running

**On this machine** (the sandbox), everything is installed:

1. `cd ~/dev/dosbox3d/retrovega`, then `./build.sh`. It writes everything to `build/dos/`:
   - `RV.EXE`, the game, and `RVT.EXE`, the test build with the demos and self-tests;
   - the data: `RV.DAT`, `RVGAME.DAT`, `BALLS.DAT`, `ROCKS.DAT`, `SKY.DAT`, `SHAPES.DAT`, `RVOPL.BNK`, the songs, and the ships' views and sets.

   `DEBUG=1 ./build.sh` builds without optimising, with debug info. `CODE=1 ./build.sh` rebuilds only the code (below).
2. Check that it built. **`build.sh` does not stop on a C error**: read its output for `Error!`, and check that `build/dos/RV.EXE`'s time is now. `grep ^DGROUP build/dos/RV.MAP` gives DGROUP's size (0x10000 is full).
3. Test it headless: `tests/run.sh tests/guns.txt /zoom 1 /flight 20 /ship 0`, then read `build/dos/RVLOG.TXT` and the `.BMP` screenshots (see Testing).
4. Play it in a window: `tests/play.sh`, or `tests/play.sh /field 120`, or `EXE=RVT.EXE tests/play.sh /zoom 1`.
   - Through the rooms: the bar's hatch to the briefing; Esc through the briefing; the fighter on the deck.
   - In flight: arrows (or the mouse) steer, Q and E roll, `+` `-` F S set the throttle, Tab is the afterburner, Space or the left button fires, Enter fires a missile, T and Y pick a target, C calls the carrier to land.
   - The flight's Escape does nothing (the user's rule, it is the browser's key); Q in the bunk menu quits.

**On a fresh Ubuntu machine** (24.04 or later, x86-64):

1. `git clone git@github.com:fireflyk64/dosbox3d.git`, then `cd dosbox3d` and `git checkout retrovega`.
2. Install the packages: `sudo apt install build-essential nasm python3 python3-numpy python3-pil python3-scipy python3-venv dosbox libegl1 libgl1-mesa-dri`. The two GL packages are Mesa, which `moderngl` renders the rocks with, headless.
3. Install Open Watcom 2:
   - download `ow-snapshot.tar.xz` from the "Current-build" release of https://github.com/open-watcom/open-watcom-v2/releases (this one is the 2026-10-01 snapshot);
   - `mkdir -p ~/opt/watcom && tar -xJf ow-snapshot.tar.xz -C ~/opt/watcom`. `build.sh` uses `~/opt/watcom/binl64`; set `WATCOM=` for another place.
4. The art tools' venv (`~/.venvs/retrovega-art`) is made by `build.sh` when it is missing, with [uv](https://docs.astral.sh/uv/) (install it first) from `tools/art-requirements.txt`, taking only package releases at least 30 days old (`UV_EXCLUDE_NEWER` changes that).
5. Build: `cd retrovega && ./build.sh`.
   - The first build takes several minutes: it renders the balls, the rocks and the sky, makes the comm faces and the panels, packs `RV.DAT` and pre-renders the ships' sets.
   - Later builds redo only what changed, except `RV.DAT` and the code: `CODE=1` skips `RV.DAT` while only code has changed.
   - The art sources (`art/*/src`) are not needed: they are git-ignored, and their cut results are committed.
6. Run with the stock DOSBox: `DOSBOX=dosbox tests/run.sh tests/flight.txt /flight 14` (headless), or `DOSBOX=dosbox tests/play.sh` (a window). RV.EXE needs nothing but a standard DOSBox (`machine=svga_s3`, `memsize=63`, XMS on, EMS off: `tests/rv.conf`). This repository's own DOSBox (the multiplayer layer) is only needed for multiplayer.
7. For the host harness (below):
   - `sudo dpkg --add-architecture i386 && sudo apt update`
   - `sudo apt install gcc-multilib libsdl2-dev libsdl2-dev:i386 gdb valgrind libc6-dbg:i386 rr`
   - for `CC=clang`: `sudo apt install clang llvm libclang-rt-NN-dev`, NN being `clang --version`'s. **Not** `libclang-rt-NN-dev:i386`: it conflicts with gcc-multilib, and apt removes gcc-multilib (and rr) to install it. The amd64 package has the 32-bit runtimes too.
   - for `WEB=1`, the Emscripten SDK: `git clone https://github.com/emscripten-core/emsdk ~/emsdk && ~/emsdk/emsdk install latest && ~/emsdk/emsdk activate latest`.
   - Then `host/build.sh` and `build/host32/rvsim`. The harness needs neither Watcom nor DOSBox.

## The host harness (rvsim)

`rvsim` is a flight with the game's logic and nothing else, built for Linux
(32- or 64-bit) or a browser from the same sources as RV.EXE: `src/logic.c`,
world, shots, collide, lightfield, hud and campaign. Whatever in them is DOS,
`host/` provides: the fixed block is an array, XMS blocks are host memory with
every move bounds-checked, and the screen is a buffer.

**None of the assembly is in it, and nothing is written twice.** The
assembly is all drawing (bolts, balls, rocks' rows, the stars), memory moves
and DOS calls. The harness draws its own picture: each ship's hit box with a
nose line, the shots as lines, the stars, a radar, and `logic_hud` over them.

Not in it: the sprites, the cockpit and its panel, the sound, the rooms, the
asteroid fields, and the multiplayer layer. rocks.c is a field's simulation
and drawing in one; a host version of its simulation is the next step for
field combat.

### Build

`host/build.sh`. The options are environment variables:

| setting | what |
|---|---|
| (none) | `build/host32/rvsim`: 32-bit, UBSan, an SDL2 window |
| `ARCH=64` | 64-bit (`build/host64/`) |
| `SAN=asan` | AddressSanitizer with UBSan; `SAN=none` for neither (for valgrind) |
| `CC=clang` | clang, which adds its 16-bit truncation check |
| `SDL=0` | headless only |
| `OPT=-O0` | no optimisation, for stepping in gdb (the default is -O1; always -g3) |
| `LOGIC=path.c` | another file in `src/logic.c`'s place (an experiment), built into its own directory |
| `WEB=1` | the browser build: `build/hostweb/` (`index.html`, `rvsim.js`, `.wasm`, `.data`) |

The directory names the build: `build/host64-asan`, `build/host32-clang`,
`build/host32-none-nosdl`, `build/host32-mylogic`. RVGAME.DAT and SHAPES.DAT
are made beside the program, so rvsim needs nothing else.

### Run

`build/host32/rvsim` flies a new campaign's first mission in a window, at 30
steps a second. `--help` lists the options:

- `--mission N`, `--ship T`, `--seed S`: what to fly.
- `--headless --frames N`: no window, as fast as it goes (900 steps unless told).
- `--record FILE`, `--replay FILE`: the controls a step at a time, with the throttle (the engine's keys set it). A replay flies the same flight exactly; the mission, ship and seed come from the file.
- `--hash N`: the world's hash (the fixed block and the shot list) every N steps. Equal runs give equal hashes: compare a 32-bit build with a 64-bit one, or a run before and after a change that should not alter play.
- `--shot FILE.ppm`: the last picture.
- `--trace`: every thunk call in the log.
- `--check-src`: each `do_damage` runs twice, the second time with `src` 0xFFFF on a copy of the fixed block, and the shields, armour, hull, guns and death are compared (the layer's replay rule).
- `--strict`: abort at the first broken rule, so a debugger stops there.

Keys: arrows steer, Q and E roll, Space or the left button fires, Enter fires
a missile, Tab is the afterburner, `+` `-` F S set the throttle, A the
autopilot (`logic_autopilot`, recorded with the controls), M (or the
right button held) steers by the mouse, P pauses, N steps once while paused,
Esc ends.

### What it checks

At every thunk call and after every step (host/host.c). Each break is logged
as `RULE:` with its step, and the exit code is 3:

- `fireGunFromShip` that puts no shot in the world and returns no missile (it must never refuse); a returned slot that is not `ET_MISSILE`; a gun past the ship's guns.
- `do_damage` with a `src` that is not a bolt, ship, missile or field hazard, or a bolt whose parent is no ship; with `--check-src`, an amount that depends on `src`.
- `rand()` or `srand()` called: play's randomness is `game_rand`.
- A ship's orientation vectors no longer 256 long or no longer square (`ship_orthonormalise` after turning); a parent, target or missile target that is not a slot; an `rvController` outside `CTL_*`; a ship type with no data.

These stop the run where they happen, with an abort, so a debugger or rr is at
the line:

- UBSan: signed overflow, a negative or too-far shift, division by zero. Its first find was in collide.c, a negative number shifted left (now `* 256`).
- `RV_CHECK` and `RV_ASSERT` (an index past a table, a shot past the list), and an XMS move past its block.
- ASan (`SAN=asan`): out-of-bounds and use-after-free, heap and stack.
- clang's truncation check (`CC=clang`): a value stored into a narrower type it does not fit.

`UBSAN_OPTIONS=halt_on_error=0` goes on past UBSan's reports.

### Debugging

- **A bug seen in play:** fly with `--record bug.rvin`. The recording then replays the bug in gdb, rr or valgrind, and in the other builds.
- **gdb:** `OPT=-O0 host/build.sh`, then `gdb --args build/host32/rvsim --replay bug.rvin`. The build has the macros (-g3), so `print gPositionVector[1]`, `print SHIELD(1, 0)` and `print maybeEntityType[3]` work, and `break logic_frame if frameCounter == 300`.
- **rr:** `rr record build/host64-none/rvsim --replay bug.rvin`, then `rr replay`, and go backwards from the crash to where a value went wrong (`watch -l gPositionVector[2].x`, then `reverse-continue`). rr needs hardware performance counters: most Linux desktops have them, most VMs do not (this sandbox's does not).
- **valgrind:** `SAN=none host/build.sh`, then `valgrind --leak-check=full build/host32-none/rvsim --headless --frames 600`.

### 32 and 64 bits

The 32-bit build is the default because it is the closer: `int` and `long`
are both 32 bits. The 64-bit build differs where code uses `long` (64 bits
there) or a pointer's size. In the sized types the two compute the same
world, and `tests/host/rvsim_test.sh` checks that their hashes match. When
they differ for your code, run both with `--hash 1` and diff the outputs: the
first different step is where they part.

### In a browser

`WEB=1 host/build.sh` builds `build/hostweb/` with Emscripten (`~/emsdk`, or
`emcc` on PATH): wasm32, like the 32-bit build, without sanitizers. Serve the
directory (`python3 -m http.server -d build/hostweb 8000`) and open it.
`?mission=N&ship=T&seed=S` picks the flight, and `&trace` and `&checksrc`
turn those options on; the log is under the view.

`host/web/deploy.sh "what changed"` builds it, puts it at
https://fireflyk64.github.io/rv/sim/ (the Pages repository's `rv/sim/`), and
checks the live page with `host/web/check.mjs` (Playwright). It also puts the
whole game, built for the browser, at `rv/` itself (engine.md, "The native
build").

### The self-test

`tests/host/rvsim_test.sh` (`VALGRIND=1` adds valgrind):

- the 32- and 64-bit builds fly the game's logic to the same hash, and its spawns are RV.EXE's (checked against `build/dos/RVLOG.TXT`'s, when that log has the mission);
- a recording replays to the same hash;
- `tests/host/rules_logic.c`, a logic that breaks the rules on purpose, has each break reported, and UBSan, `RV_CHECK` and clang's check each stop it.

## Testing

- **The play tests**, `tests/play/playtest.sh` (docs/gameplay.md, "The play tests"): 61 checks of the play, by
  milestone, on the host in a second; `--dos` runs them in RVP.EXE under DOSBox too and compares each test's world
  with the host's.
- `tests/run.sh SCRIPT [switches]` plays a script headless in DOSBox. The log is `build/dos/RVLOG.TXT`, the screenshots `build/dos/*.BMP`.
  - `/flight N` ends the flight after N seconds.
  - Switches other than the game's own (`/zoom`, `/ship`, `/boxes`, ...) run the test build, `RVT.EXE`.
- **`tests/guns.txt`** is for the fire loop: `tests/run.sh tests/guns.txt /zoom 1 /flight 20 /ship 0`.
  - `/zoom 1` puts one Gawain dead ahead, swinging between 1,200 and 6,000 km. `/ship 0` flies the Schroedinger (lasers; the default Redeemer's slugs are pixels at range).
  - The script holds the trigger (`hold fire N`), takes `G01FIRE`..`G03HIT`, and the log ends with `shots: N fired, M hits`.
  - When I filled the loop with the first flight's stand-ins and a simple fire loop, it gave 82 shots and 32 hits, and the Gawain's shields and armour went to 0. That was the check of these helpers.
- **The host harness** runs the logic on Linux, under gdb, rr, valgrind and the sanitizers, with the multiplayer rules checked (see "The host harness (rvsim)"). `tests/host/rvsim_test.sh` is its self-test.
- `/field D` puts every flight in an asteroid field; `/boxes` brackets every ship (`flight_ent_box`'s check).
- `rv_logf(s_fmt, ...)` writes a line to RVLOG.TXT. It opens and closes the file each line, so log events, not every step.
- In the browser the log is `new TextDecoder().decode(DOSBox.FS.readFile("/game/RVLOG.TXT"))` in the console.
