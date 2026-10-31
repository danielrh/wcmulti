# Writing the game: the implementation guide

This guide is the road map for writing RetroVega's play yourself: the flight model, the guns, damage, deaths, the
AI, collisions, missiles and the mission, so that it plays like the space combat of the early nineties. It
covers:

- where your code goes, and where the game calls it;
- the order to build it in, nine milestones, each with what to write and what the tests expect;
- the memory: every piece of a ship's state, the mission's tables, and where victory and loss are kept;
- the mission format, from `data/missions.txt` to the slot arrays;
- the play tests, which fail now and pass as you write each piece, on your machine and in DOSBox;
- the Watcom and 16-bit traps, and how the tests catch them.

[docs/logic.md](logic.md) is the reference it leans on: the helpers' calls, the units, the multiplayer layer's
rules, the host harness. This guide says what to do with them, in order.

## Your loop: write on the host, check in DOSBox

The same `src/logic.c` builds three ways, and the play tests run on two of them.

| step | command | what you get |
|---|---|---|
| 1. test on the host | `tests/play/playtest.sh M1` | the tests of milestone 1 in a second, on Linux: UBSan on, the layer's rules checked at every call |
| 2. debug a test | `OPT=-O0 SDL=0 host/build.sh`, then `gdb --args build/host32-nosdl/playtest M1.4` | gdb at your code. `break step_motion`, `print gPositionVector[0]`, `print SHIELD(1, 0)` work (the build has the macros) |
| 3. fly it on the host | `host/build.sh && build/host32/rvsim` | your logic in a window, the ships as wire boxes; `--record` and `--replay` a flight; rr and valgrind on a replay (logic.md, "The host harness") |
| 4. the whole game on the host | `host/game.sh`, then `build/game32/rv /fly /nomouse` | the real game, sprites, cockpit and sound, compiled natively with UBSan: gdb on the full game. `/fly` starts in space (`/fly N`: RVGAME.DAT's mission N); `/nomouse` plays by the keys and leaves the pointer free for the debugger; `--help` lists the rest |
| 5. test in DOS | `tests/play/playtest.sh --dos` | the same tests in `RVP.EXE` (RV.EXE with the tests) under DOSBox, and each test's world compared with the host's |
| 6. find where DOS differs | `tests/play/playtest.sh --steps M5.3` | the first step, and the arrays, where the 16-bit build parts from the host's |
| 7. play it in DOS | `CODE=1 ./build.sh && tests/play.sh` | RV.EXE in a DOSBox window |

**Why step 5 matters.** On DOS `int` is 16 bits, and on the host 32. Code that is right on the host can wrap on
DOS, and no sanitizer on the host sees it when nothing is stored into a 16-bit variable. Every test ends with a hash
of the world, so the DOS build has to reach exactly the host's world, test by test. I checked this by planting such a
bug: `int16_t k = km / 8; if (k * 64 > 20000)`. All 61 tests passed on both sides and clang's check stayed quiet,
yet the hashes differed on the six tests where the AI's throttle mattered. `--steps M5.3` then named step 1 and the
speeds.

Setting up a machine for all this is in logic.md, "Building and running". The host side needs gcc with
gcc-multilib; the DOS side needs Open Watcom 2 and DOSBox (`DOSBOX=dosbox` uses a stock one).

## What you fill in, and where the game calls it

Everything you write is in `src/logic.c`. The line numbers are as of this guide; they move as you write.

| function | where | called from | when |
|---|---|---|---|
| `logic_start(m)` | logic.c:34 | `flight_startup`, flight.c:2264 | once, as the flight loads, before the ships' art is read: the types it spawns are the types that load |
| `logic_frame()` | logic.c:123 | `sim_step`, flight.c:212 | once a frame, after the controls are read (`g_pilot`) and `frameCounter++`. It runs the nine steps below |
| `step_timers` .. `step_mission` | logic.c:72-121 | `logic_frame` | the nine steps, in order (logic.md, "The main loop") |
| `logic_hud()` | logic.c:138 | `scene_draw`, flight.c:1158 | once per band of rows, after the world is drawn: marks over the view |
| `logic_end()` | logic.c:142 | `flight_fly`, flight.c:2730 | once, after the loop: the mission's final score |
| `logic_autopilot()` | logic.c:149 | `flight_autopilot`, flight.c:2651 (the A key; rvsim's A too) | between two steps: on to the next nav point, or not now |
| `do_damage_impl` | logic.c:160 | the `do_damage` thunk: your steps, rocks.c, the layer | every hurt |
| `fireGunFromShip_impl` | logic.c:174 | the `fireGunFromShip` thunk: your fire loop, the layer | one gun or launcher fires |
| `maybe_fire_all_guns_impl` | logic.c:182 | its thunk | every ready gun of a ship |
| `aiFighterThink_impl` | logic.c:189 | its thunk, from your `step_minds` | a fighter's mind, a step |
| `aiShipThink_impl` | logic.c:195 | its thunk, from your `step_minds` | a capital ship's mind |
| `despawn_impl` | logic.c:202 | its thunk: your `step_deaths`, the layer | a ship's end, after its explosion |
| `delayedDespawn_impl` | logic.c:208 | its thunk | a kill: the end, after the explosion's time |
| `enterNavPoint_impl` | logic.c:215 | its thunk, from your `step_mission` | the player reached a nav point: its ships come |

Already written, marked PLUMBING: `aiSetSpeed`, `setController`, `despawnQuietly`, `farShipTest` and
`outerSpawnShipEntity`. The ABI fixes their whole job.

**Always call a thunk by its name**, `do_damage(...)` and never `do_damage_impl(...)`. The multiplayer layer
hooks the thunk: it skips the call on a client and replays it from the host. On the host, the harness checks the
layer's rules there. The thunks are listed in `abi/rvabi.def`; new ones go at its end.

## The order: nine milestones

Each milestone is a set of play tests (`tests/play/playtest.sh M3` runs M3's). They are independent where they can
be: each builds its own scene, so M3's damage tests don't need M1's flight. Some need an earlier piece and say so
when it is missing (M5.8: "nothing to keep up with (M1.1 first)").

The numbers come from `data/ships.csv` and `data/weapons.csv`, and the tests read them there. Tuning a ship
changes what the tests expect, not whether they pass.

### M1: flight (`step_minds` for the player, `step_motion`)

What to write:

- **`step_minds`**: for the player (`CTL_PLAYER`), a turn for this step from `g_pilot`:
  - `steer_x` -9..9 yaws (+ right), `steer_y` pitches (+ up), `roll` rolls (+ lifts the right wing);
  - a step's turn is `dps × TURN × steer / (360 × KPS_HZ × 9)`, with the type's `yaw_dps`, `pitch_dps` and
    `roll_dps`. `TURN_STEP(dps, steer)` (world.h) gives it in 1/256ths of a unit without a divide: carry the
    fraction from step to step, or ships under 7 dps never turn;
  - keep it in a per-slot array of your own; the AI fills the same array (M5).
- **`step_motion`**, for each ship slot:
  - **The turn:** `vec_turn(front, right, yaw)`, `vec_turn(front, up, pitch)`, `vec_turn(right, up, roll)`, then
    `ship_orthonormalise(s)`.
  - **The speed** (`speedLo`) toward the throttle (`setSpeed`, at most `max_kps`) by the type's `accel` (kps a
    second; a step's share). While `rvAfterburner[s]` is lit, the aim is `ab_kps`.
  - **The move:** `gVelocityVector = front × KPS_STEP(speed) / 256`, then `gPositionVector += gVelocityVector`.
  - **Controllers:** `CTL_PUPPET` only coasts (position += velocity); `CTL_FROZEN` does nothing.

What the tests expect:

| test | expects |
|---|---|
| M1.1 coast | each step's move is exactly `gVelocityVector`, of length `KPS_STEP(speedLo)` ±3%, along the nose |
| M1.2 throttle | the speed climbs to the throttle (never past it, at most `accel` kps a step, not in one step), reaches it within 10 s, and falls to 0 within 10 s |
| M1.3 top-speed | a throttle of 5,000 gives `max_kps`, no more |
| M1.4-1.6 yaw, pitch, roll | 10 steps of full steer turn the type's rate, ±30% (`TURN_SLACK`), the right way, without disturbing the other axes |
| M1.7 steer-steps | a steer of 3 turns 20-50% as far as 9 |
| M1.8 aerobatics | 20 s of mixed turning keeps every frame unit-length and square |
| M1.9 afterburner | past `max_kps` (never past `ab_kps`) while lit; back to the throttle within 20 s after |
| M1.10 puppet | moves exactly by its velocity; its orientation untouched; no shots; no AI calls |
| M1.11 frozen | doesn't move (while the player does) |
| M1.12 turn-steps | each single step of a full yaw, pitch and roll turns the right way and at most twice its share (a turn measured only over many steps can wrap round); a second's sum within ±30% |
| M1.13 turn-back | a steer of -9 turns each axis the other way, as fast |
| M1.14 there-and-back | a second of each turn and a second back ends within 3 units (1°) of where it began |
| M1.15 slow-turns | under a unit a step still turns at its rate (±30%): a capital ship's yaw, and a steer of 1. Carry the fraction from step to step |
| M1.16 body-axes | rolled a quarter turn, a yaw takes the nose toward the ship's own right and a pitch toward its own up, not the world's |
| M1.17 two-axes | yaw and pitch at once each turn at their own rate |
| M1.18 steady | with the controls centred after a turn, the frame moves no more than 2/256 in 20 s |
| M1.19 turn-flight | turning hard at cruise, every step's move is within 14° of the nose (a slide, if any, belongs above the top speed) |
| M1.20 respawn | a ship spawned into a used slot turns exactly as one spawned into a fresh slot: reset what you keep for a slot when a ship is spawned in (`logic_prepare_spawn`) |
| M1.21 full-house | a ship spawned into every slot 1..`RV_MAX_SHIP_SLOT` (`ship_spawn` returns its slot), no slot left free, and all of them fly |
| M1.22 slot-limit | `ship_spawn` past the ship slots (32, 127, 128, -1) returns -1 and leaves the slot alone; with every ship slot full, `outerSpawnShipEntity` returns 0xFFFF and changes nothing; `logic_prepare_spawn` takes any slot number |

The flight's rate is your decision. `KPS_HZ` is 20 and the flight steps 30 times a second (`LOGIC_HZ`), so
everything moves 1.5 times its nominal rate (logic.md, "Units"). The tests compute through `KPS_STEP` and
`KPS_HZ`, so they follow whatever you set.

### M2: guns (`fireGunFromShip`, `maybe_fire_all_guns`, `step_fire`, `step_timers`)

What to write:

- **`fireGunFromShip_impl(ship, gun)`** for a gun or turret (`weapon(GUN_TYPE(ship, gun))->kind`):
  - the muzzle: position + right × `GUN_MOUNT_X` + up × `GUN_MOUNT_Y` (in 32nds of `collide_half(ship)`), and front
    × the nose;
  - `shot_add(ship, GUN_TYPE(ship, gun), &muzzle, &velocity)`, the velocity along the nose at
    `KPS_STEP(speed_kps)`, plus the ship's own if you want bolts to inherit it;
  - then the cooldown into `GUN_STATE`'s bits 0-6 (the weapon's `refire`, in steps; keep bit 7, the destroyed bit),
    and the energy off `gunEnergy` (never below 0);
  - **never refuse**: the caller decided, and the layer replays the call on the other machines (logic.md, "The
    contract of fireGunFromShip").
- **`maybe_fire_all_guns_impl(ship)`**: the guns and turrets in groups of one weapon type (a loadout's `3xLAS` is a
  group of three, linked). A group fires together, each gun through the `fireGunFromShip` thunk, when every gun in it is cooled
  (`GUN_STATE & 0x7F` is 0) and the energy covers them all; otherwise none of it fires, so linked guns never fire
  lopsided. A destroyed gun leaves its group, and the rest fire together without it. Not the launchers.
- **`step_fire`**: the player's trigger, `g_pilot.guns` with the master arm `g_sys.armed` on, through
  `maybe_fire_all_guns(0)`. The AI's choices come in M5, the missile key in M7.
- **`step_timers`**: the guns' cooldowns down a step (guns and turrets only: a launcher's low bits are its
  rounds), and `gunEnergy` back up toward 100.
- **`step_shots`**: `shots_move()`, which also ends a shot at its range. The hits come in M3.

What the tests expect:

| test | expects |
|---|---|
| M2.1 fire-call | one shot: owner, weapon, within the ship's size of it, along the nose, at the weapon's speed (plus the ship's at most); returns 0xFFFF |
| M2.2 never-refuses | a shot with no energy and the gun cooling; energy not wrapped |
| M2.3 cooldown | a cooldown set; it reaches 0 in 1/2 to 2 × `refire` steps; a destroyed gun's bit 7 kept |
| M2.4 energy | a shot costs energy; it recharges to exactly 100 within 20 s, never past |
| M2.5 trigger | the first step fires every gun; 2 s of trigger, at least two volleys, no faster than the refire; nothing after release; no missile |
| M2.6 safety | nothing with the master arm off (but something with it on) |
| M2.7 broken-gun | a destroyed gun fires nothing, the others do |
| M2.8 all-guns | `maybe_fire_all_guns` fires each ready gun once, no launcher; at once again, nothing |
| M2.9 range | shots are gone a few steps past their life |
| M2.10 launcher-rounds | a launcher's `GUN_STATE` doesn't change in 3 s of nobody firing: its low bits are rounds, and only guns' and turrets' count down |
| M2.11 destroyed-bit | firing a gun whose record says destroyed keeps bit 7, and sets a cooldown |
| M2.12 energy-gate | for the player's first group of linked guns (two or more of one type, whatever the count): with less energy than a shot nothing fires; with all but one shot's worth, still nothing; with the whole group's, all of it fires; recharged, it fires again |
| M2.13 muzzles | a ship turned off the world's axes fires each gun from its mount's side (`GUN_MOUNT_X`'s sign, on the ship's right), ahead of its middle and within its size, along its nose |
| M2.14 turrets | a capital ship's turrets all fire through `maybe_fire_all_guns`, from within its size (its big mounts' arithmetic), and cool within twice their refire |
| M2.15 shots-full | with the shot list full (`SHOTS_MAX`), a fire still sets the cooldown and spends the energy: every machine replaying the call changes the gun the same way |
| M2.16 volleys | the player's linked guns, from one shot short of a volley's energy, 10 s of held trigger: every step fires none or the whole group, never part of it; the group's cooldowns stay equal; at least three volleys as the energy comes back |
| M2.17 link-fire | the same for a group of four (the Redeemer's mass drivers), and for the three left with one of them destroyed |

### M3: damage (`do_damage`, `step_shots`, `step_timers`)

What to write:

- **`do_damage_impl(src, dst, qty, where)`**:
  - Read `where` with `RV_WHERE_VEC(where)`: the hit's place, in positions.
  - **Which side:** take `where` relative to the ship and dot it with its front and right (in km, not positions:
    the products overflow). The front or rear decides the shield, `SHIELD(dst, 0/1)`. The larger of front and side
    decides the armour, `ARMOR(dst, 0..3)`: front, rear, left, right.
  - **Spend** the shield on that side, then that armour, then the hull: `damagePoints` up to the type's `hull`.
    Nothing below 0.
  - **A death:** the hull gone sets `shipAiState[dst] = 9`.
  - **Leave alone:** slot 0 while `playerCanBeDamaged` is 0, a ship with `rvFlags` bit 1 (invulnerable), a ship
    already dying.
  - Return nonzero when it did something.
  - **The amount must not depend on `src`**, and any roll comes from `game_rand()` (logic.md, "Damage").
- **`step_shots`**: after `shots_move()`, for each shot from the last down, `shot_hit(i, &slot, &where)`. On a hit,
  `do_damage(shot_source(i), slot, shot(i)->damage, RV_WHERE(&where))`, then `shot_remove(i)`. `shot_source` is
  the stand-in bolt the layer reads to know who fired.
- **`step_timers`**: the shields back toward `SHIELD_MAX`, a little each step. Armour doesn't come back in flight.

What the tests expect:

| test | expects |
|---|---|
| M3.1-3.2 shield-front, shield-rear | a hit of 3 on the front (rear) takes only that shield, by at most 3; returns nonzero |
| M3.3 armour | shields down: a front hit takes only the front armour |
| M3.4 armour-sides | shields down: a hit on the left takes the left armour, the right the right |
| M3.5 hull | shields and front armour gone: the hull (`damagePoints`) |
| M3.6 big-hit | 200 on a fresh Nicander: `shipAiState` 9 within a step; nothing wrapped |
| M3.7 no-wrap | 100 hits of 3 all round: nothing wraps, the ship dies and stays dead |
| M3.8 player-safe | `playerCanBeDamaged` 0 protects slot 0 (which a hit otherwise hurts), returning 0 |
| M3.9 invulnerable | `rvFlags` 1 protects a ship |
| M3.10 any-source | a hit of 3 and of 30 does the same with src 0xFFFF, the player's ship and a bolt of the player's |
| M3.11 shots-hit | 3 s of fire at a ship 1,500 km ahead, side on, hurts it; on the host, the hits name a bolt of the player's |
| M3.12 recharge | in 20 s the shields come back (never past their most); the armour doesn't |
| M3.13 type-rates | a type's `gun_energy_steps` and `shield_full_shift` set the rates: with 6 and 4, 10 points of energy in 60 steps, and a shield full in 16 |

### M4: deaths (`step_deaths`, `despawn`, `delayedDespawn`, `logic_end`)

What to write:

- **`step_deaths`**: a ship at `shipAiState` 9 keeps its slot while it explodes (the layer learns of a death from
  the 9 in the slot), then ends through a thunk:
  - `despawn(s)`, or `delayedDespawn(killer, s)` at the kill, whose body ends it later;
  - `despawn_impl` is its explosion and debris (none in the engine yet), then `ship_clear(s)`.
- **The player's death:** after its explosion, `missionStatus = 4`. The flight loop ends at the next check.
- **The explosion and its sound come by themselves** (engine.md, "Explosions and sound effects"): the engine sees
  the 9 after the step and starts them, hiding the ship's sprite two steps later. Keep the 9 in the slot to the
  end of at least one step, or it is never seen; how long you keep the slot after that is yours (the explosion
  goes on without it). Shots, hits and missiles sound and spark by themselves too.
- **A dying ship** neither thinks nor fires.
- **`logic_end`**: a dead player's mission is lost (`victoryPoints` 0).

What the tests expect:

| test | expects |
|---|---|
| M4.1 kill | 9 a step after the hit; the slot kept 3 steps or more; gone within 10 s; on the host, ended through `despawn` or `delayedDespawn` |
| M4.2 dying-quiet | an enemy that has been firing fires nothing, guns or missiles, from the step after its death |
| M4.3 player-dies | `missionStatus` 4 within 10 s; `shipAiState[0]` 9 meanwhile; `victoryPoints` 0 after `logic_end` |

### M5: the AI (`aiFighterThink`, `aiShipThink`, `step_minds`, `step_fire`)

What to write:

- **`step_minds`**, for `CTL_AI`: the thunk, `aiFighterThink(s)`, or `aiShipThink(s)` for `ET_CAPSHIP`. Never for
  the player, a puppet or a frozen ship.
- **`aiFighterThink_impl(s)`**:
  - **A target:** the nearest ship of another side, say. Keep it in `rvTarget[s]`.
  - **The way to it:** `ship_rel(s, target, &v)` gives right, up and ahead in km. `lf_atan2` of right (or up)
    against ahead gives an angle, which becomes a steer of -9..9 into the same turn array the player's controls
    fill. Turning is then M1's code.
  - **The throttle**, through `aiSetSpeed(s, kps)`.
  - **Fire** when the target is in front (within 30°) and in range: a flag that `step_fire` acts on with
    `maybe_fire_all_guns(s)`.
  - **A wingman** with no enemy about keeps station off the player (`formationOffset` is there for it).
- Everything an AI decides goes through its type's limits, as the player's ship does.

What the tests expect (an AI Nicander against a still player):

| test | expects |
|---|---|
| M5.1 ai-thunks (host only) | `aiFighterThink` at least every 4 steps for each AI fighter, never for the player, a puppet, a frozen or a capital ship; `aiShipThink` for the capital ship |
| M5.2 ai-turns | an enemy 6,000 km ahead, facing away, points its nose at the player (cos > 0.9) within 10 s |
| M5.3 ai-closes | it comes within 2,500 km within 30 s |
| M5.4 ai-fires | within 45 s it fires and hurts the player |
| M5.5 ai-aims (host only) | started beside the player, in range but 90° off its nose: of its first 8 volleys, 70% (`AIM_SHARE`) with the player within 30° (`AIM_COS`) |
| M5.6 ai-limits | its nose turns no faster than 1.5 × its type's rate; its speed within `max_kps`, or `ab_kps` while lit; and it does turn and fly |
| M5.7 wingman-fights | a Terran AI hurts an enemy within 60 s while the player holds fire (on the host: by its gunfire) |
| M5.8 formation | flying straight at cruise for a minute, the wingman stays within 5,000 km of the player for the last 10 s |
| M5.9 no-left | an enemy whose profile may not turn left (byte 17 of its mission ship record names the profile) never yaws left, and still brings its nose onto the player on its left within 20 s |
| M5.10 waiter | an enemy whose profile lies in wait stays still and cloaked (`rvFlags` 16) while the player points at it, and comes, uncloaked, within 10 s of the player turning away |
| M5.11 old-straight | the old game's first mission measured (tests/wc1/README.md): a line fighter crossing 15,000 km ahead of a player flying straight at the 300 setting fires first from 1,500-6,000 km, a volley at a time (never two within 12 steps), comes within 2,500 km without a collision, and has its nose back on the player within 15 s of a pass (needs M2: the shot) |
| M5.12 old-hunt | the same fighter against a player whose nose is set onto it every 3 s: five passes or more in 90 s, one collision at most, the nose back in time after all but one, the volleys apart |

**The AI as written** (docs/ai.md): a mind per slot runs a tactic picked by weighted dice from its profile's table for
its situation; a steering turns the tactic's wanted direction into the player's own steer and roll, pulling only where
the profile allows; the profiles are rows of a table, one a character (interceptor, fighter, heavy, ace, bomber,
wingman, capital, waiter, no-left), a type's by `profile_of`, or a mission ship record's byte 17 (`P_*` + 1). The
numbers are measured against the old games' flights (`tests/wc1/`).

### M6: collisions (`step_collisions`)

What to write:

- For each pair of ships, `collide_coarse`, then `collide_ships(a, b, &push, &depth)`.
- **Part them:** `push` is the least move of b that frees them. Move b by it, or a by -push, or each by half; a
  frozen ship is not moved.
- **Hurt both** through `do_damage`, each with the other as `src`.

What the tests expect:

| test | expects |
|---|---|
| M6.1 ram | a player inside a frozen ship is out within 10 steps (or touching: less than 1/16 of its half box deep); both hurt |
| M6.2 no-ghosts | flown at top speed at a ship 1,500 km ahead, the player is never inside it more than 3 steps in a row, and the collision hurts the ship |

### M7: missiles (`fireGunFromShip`'s launchers, `step_missiles`, `step_fire`)

What to write:

- **`fireGunFromShip_impl`** for a launcher (weapon kind missile, torpedo or rocket):
  - a free ship slot (`ship_free_slot`), made `ET_MISSILE`, at the launcher's mount, facing the ship's way;
  - `parentShipForShip` the ship;
  - `missileTarget` its target (`g_sys.target` for the player, `rvTarget` for an AI), 0xFF for a dumbfire (lock
    none);
  - the rounds (`GUN_STATE`'s low bits) one fewer;
  - return the slot: the layer maps it across machines.
- Keep the missile's weapon and life in arrays of your own: a missile slot has no ship type.
- **`step_missiles`**, for each `ET_MISSILE` slot:
  - **Homing:** a seeker steers toward `missileTarget` like a ship, then `ship_orthonormalise`.
  - **The move:** at its weapon's `speed_kps`.
  - **A hit:** `collide_segment(t, &last, &now, &hit, 0)` against each ship but its parent. On a hit,
    `do_damage(missile_slot, t, damage, RV_WHERE(&hit))`, then the slot cleared (`ship_clear`).
  - **Its range:** a missile ends when its weapon's range runs out.
- **`step_fire`**: the missile key, `g_pilot.missile` (Enter), launches from a launcher with rounds left.

What the tests expect:

| test | expects |
|---|---|
| M7.1 launch | the Schroedinger's launcher gives an `ET_MISSILE` slot, parent 0, within the ship's size of it; rounds one fewer |
| M7.2 missile-key | Enter launches one; four presses with two rounds launch two |
| M7.3 missile-flies | a second after launch, a dumbfire moves 80-120% of `KPS_STEP(speed_kps)` a step, straight along the nose |
| M7.4 missile-hits | a dumbfire at a ship 2,500 km ahead, on its line, hurts it within 10 s, and its slot ends |
| M7.5 missile-homes | a friend-or-foe from the Gawain, `missileTarget` set at launch, hurts a ship 31° off its nose |
| M7.6 missile-ends | fired into empty space, it ends within twice its range's steps |

### M8: the mission (`logic_start`, `step_mission`, `enterNavPoint`, `logic_end`, `logic_autopilot`)

What to write:

- **`logic_start(m)`**:
  - the player into slot 0 (`ship_spawn(0, g_camp.player_ship, SIDE_TERRAN, CTL_PLAYER, ...)`);
  - the mission ships at nav 0 (the wingman) through `outerSpawnShipEntity(record, 0)`;
  - **`victoryPoints = 0`**: the launch's stand-in roll is there until you score over it (see "Victory and loss");
  - your own state reset.
- **`step_mission`**: when the player comes within reach of a nav point not yet visited, `enterNavPoint(n)` once.
  Then the mission's rule for winning, into `victoryPoints`.
- **`enterNavPoint_impl(nav)`**: `currentNavPoint = nav` (written), then that nav's mission ships:
  `outerSpawnShipEntity(record, nav)` for each record whose nav (+8) is it.
- **`logic_end`**: the final score. Landed with the mission done: won. Dead, ejected, or the mission undone: lost.
- **`logic_autopilot`**: with no enemy near, the player and the wing to the next nav point not yet visited (home,
  nav 0, when all are). Return 1, or 0 when it may not.

What the tests expect (a patrol: nav 1 at 20,000 km ahead with two Nicanders, nav 2 with an Agis, a wingman at
nav 0):

| test | expects |
|---|---|
| M8.1 start | slot 0 the player (`CTL_PLAYER`, the player's type); one Terran AI within 10,000 km, its pilot the mission's; each ship's `slotMissionShip` a record of its type and side; no enemies yet; `currentNavPoint` 0 |
| M8.2 nav1 | the player taken to nav 1: `currentNavPoint` 1, and nav 1's two Nicanders within 15,000 km of it, their records nav 1's |
| M8.3 nav-once | 5 s more there: still two |
| M8.4 won | `victoryPoints` 0 a second in; nav 1 flown and its ships killed, then nav 2's; home; landed: nonzero |
| M8.5 skipped | landed after 2 s: 0 |
| M8.6 enemies-left | both navs flown, nothing killed, landed: 0 |
| M8.7 layer-spawn | `outerSpawnShipEntity(1, 0)` puts the wingman's record into a free slot at nav 0 (my plumbing: it passes now) |
| M8.8 autopilot | from the start, to nav 1 (`currentNavPoint` 1); with enemies 4,000 km ahead, returns 0 and moves nothing; with them gone, to nav 2 |

The tests take the player to a nav point by moving it (as the autopilot would) and land by setting
`missionStatus = 1`, as the engine's C key does.

### M9: the HUD (`logic_hud`, host only)

- **A gunsight** at the view's middle: `flight_camera(&cam)`, then `hud_cross(cam.cx, cam.cy, ...)`.
- **Brackets round the target:** `flight_ent_box(g_sys.target, &cam, &b)`, then `hud_brackets` round `b.sx ±
  b.half`.

M9.1 wants a mark within 30 px of the middle; M9.2 wants marks within 10 px of the target's box. They read the
host harness's screen, so they don't run in DOS.

## The memory: where everything lives

### The fixed block

The world is a fixed block at the start of the game's data segment, DS:0000-7FFF. The multiplayer layer reads and
writes it by offset, so the offsets are fixed for good. `abi/rvabi.def` is the one source: `tools/rvabi.py` makes
`src/rvhooks.h` from it, and docs/spec.md section 4 has the full table.

- In C you use the names: `missionStatus`, `gPositionVector[s]`, `SHIELD(s, 0)`. On DOS each is a near pointer at
  its offset; on the host each is a place in `rv_dgroup` (host/fixed.c).
- Slot s's element of an array is at the array's offset + s × its size: slot 3's position is at
  0x0B00 + 3 × 12 = 0x0B24.
- Your own state goes in `logic.c` (below), never past the block's sections. New fixed data for the layer goes at
  a section's end in `rvabi.def`.

### Victory, loss and the flight's state (the scalars)

| DS: | name | bytes | meaning | written by |
|---|---|---|---|---|
| 0x0100 | `missionStatus` | 2 | 0 flying; 1 landed; 2 ejected; 3 on the carrier (before launch); 4 dead; 5 left (a test script) | flight.c 0 at the start; the engine 1 (C, the carrier call) and 2 (Ctrl+E twice); **you 4**; the layer may |
| 0x0102 | `playerCanBeDamaged` | 2 | 0: slot 0 takes no damage | the engine 1 at the start; the layer, cheats |
| 0x0104 | `frameCounter` | 2 | steps flown, +1 a step | the engine only |
| 0x0106 | `randomSeed` | 4 | the play's one random stream (`game_rand`) | the engine seeds it per mission; the layer re-seeds |
| 0x010A | `currentNavPoint` | 2 | the nav point the player is at | `enterNavPoint_impl` |
| 0x0110 | `playerMissionShip` | 2 | the player's record in `missionShipTable` | the campaign |
| 0x0112 | `playerTarget` | 2 | the player's target slot, 0xFFFF none | yours to keep equal to `g_sys.target`, for the layer |
| 0x0114 | `landedOn` | 2 | the slot landed on, 0xFFFF none | yours, at a landing |
| 0x0116 | `victoryPoints` | 2 | the mission's score: nonzero won | **you**; the stand-in roll before (see below) |
| 0x011E-0x0123 | `fieldCount`, `fieldRockTally`, `currentField` | 6 | the asteroid fields | rocks.c |
| 0x0128 | `steerInput` | 4 | the steering as the layer reads it: pitch, yaw | flight.c, each frame |
| 0x028C | `systemDamage` | 10 | a byte per system, 0..4 | free for your damage model |
| 0x02A0 | `tempVector` | 12 | a spare near vector (a `where`) | anyone |
| 0x02C0 | `navPointState` | 152 | 8 × 19 bytes of nav-point progress | free for your mission's progress |
| 0x035E | `rvCockpitDamage` | 4 | a bit per cracked cockpit overlay (logic.md, "Damage") | you |

### A ship: the slot arrays

128 slots. Slot 0 is the player; 1..31 (`RV_MAX_SHIP_SLOT`) are ships, capital ships and missiles; 32 is the shots'
stand-in bolt; 33..117 are kept free; 118..124 are the fields' hazards; 125 is the camera.

| DS: (slot 0) | name | bytes | meaning, units | written by |
|---|---|---|---|---|
| 0x0600 | `maybeEntityType` | 2 | `ET_SHIP` 12, `ET_CAPSHIP` 13, `ET_MISSILE` 11, `ET_NONE` 0 ... (world.h). What is drawn | `ship_spawn`, `ship_clear`, your missiles |
| 0x0700 | `gInstanceShipTypes` | 2 | the ship type, a row of ships.csv (`slot_type(s)` gives its numbers) | `ship_spawn` |
| 0x0800 | `parentShipForShip` | 1 | a missile's or bolt's launcher, 0xFF none | your missiles; `shot_source` |
| 0x0880 | `commAnimInfo` | 2 | the side: 0 Terran, 1 Aera, 2 Rlaan | `ship_spawn` |
| 0x0980 | `shipStateByte` | 1 | the pilot: the cast's row, 8 a human, 0xFF none | the spawn |
| 0x0A00 | `slotMissionShip` | 2 | its record in `missionShipTable`, 0xFFFF none | the spawn |
| 0x0B00 | `gPositionVector` | 12 | **where**: x, y, z, 256 a km (`int32_t`) | you (motion); the layer (puppets) |
| 0x1100 | `gVelocityVector` | 12 | the move a step, positions | you |
| 0x1700 | `gOrientationRightVector` | 12 | right, 256 = 1.0 | you (turns); the layer |
| 0x1D00 | `gOrientationUpVector` | 12 | up | the same |
| 0x2300 | `gOrientationFrontVector` | 12 | the nose: left-handed, x right, y up, z ahead | the same |
| 0x2900 | `setSpeed` | 4 | the throttle, kps | the engine's keys (slot 0), `aiSetSpeed` (AI) |
| 0x2B00 | `speedLo` | 4 | the speed now, kps | you |
| 0x2D00 | `curShield` | 4 | `SHIELD(s, 0)` front, `SHIELD(s, 1)` rear | `ship_spawn`; you |
| 0x2F00 | `shieldMax` | 4 | `SHIELD_MAX(s, k)` | `ship_spawn` |
| 0x3100 | `armorState` | 8 | `ARMOR(s, k)`: front, rear, left, right | `ship_spawn`; you |
| 0x3500 | `damagePoints` | 2 | **the hull's damage**, up to the type's `hull` | you |
| 0x3600 .. 0x3780 | `coreHp`, `hullCounter`, `gunDamage`, `engineFlag` | 1 each | free for your damage model (WC's: core hits, gun damage, engine out 0xFF) | you |
| 0x3800 | `gunEnergy` | 2 | gun energy, 0..100 | you |
| 0x3900 | `gunTable` | 51 | `GUN_COUNT(s)`, then 10 records: `GUN_TYPE`, `GUN_MOUNT_X/Y`, `GUN_STATE` (bit 7 destroyed; a gun's cooldown, a launcher's rounds) | `ship_spawn` from the loadout; you |
| 0x5280 | `missileTarget` | 1 | what a missile homes on, 0xFF none | your missiles |
| 0x5300 | `shipAiState` | 2 | **9 dying**; any other value your AI's | you |
| 0x5400 | `gMaybeShipRadius` | 2 | its half box, km | `ship_spawn` |
| 0x5500 | `formationOffset` | 6 | right, up, ahead of its leader, km | you |
| 0x5800, 0x5900 | `entityCullStatus`, `entityScreenY` | 2 each | the drawing's | the engine |
| 0x5A00 | `damageProperty` | 2 | spare per ship (a bolt's damage) | you |
| 0x5B00 | `rvController` | 1 | `CTL_AI`, `CTL_PLAYER`, `CTL_PUPPET`, `CTL_SCRIPT`, `CTL_FROZEN` | the spawn; `setController` (the layer) |
| 0x5B80 | `rvNetId` | 1 | the layer's | the layer only |
| 0x5C00 | `rvFlags` | 2 | 1 invulnerable, 2 a field's hazard, 4 docked, 8 jamming, 16 cloaked | you, rocks.c |
| 0x5D00 | `rvLastWrite` | 2 | the step the layer last wrote it | the layer |
| 0x5E00 | `rvTarget` | 1 | what it targets, 0xFF none | you |
| 0x5E80 | `rvAfterburner` | 1 | lit (the flames draw longer) | the engine (slot 0, Tab); you (AI) |
| 0x5F00 | `rvLivery` | 1 | its paint, a row of liveries.csv | nobody yet (the sprites carry their paint) |

`ship_spawn` sets a slot whole: its type's speeds, shields, armour, gun table, and the links at none. `ship_clear`
empties it. A ship may appear in a slot without your code spawning it: the layer does, through
`outerSpawnShipEntity`, and so do the play tests. So read what a slot is from the arrays each step.

### Your own state

Anything the arrays don't hold goes in `logic.c` as `static` arrays: the turns your minds decide, an AI's
mode, a missile's life, the nav points visited.

- **Make them `__far`:** `static int16_t __far g_turn[RV_MAX_SHIP_SLOT + 1][3];`. RV.EXE's DGROUP has 3.2 KB free,
  RVT.EXE's (the test build's) 608 bytes.
- **Reset them all in `logic_start`.** The play tests begin each scene there.
- **Rebuild from the arrays each step what can be.** The layer writes positions, orientations and health between
  steps (logic.md, "The rules").
- A missile's slot has no ship type, so its weapon has to be one of yours (or `gInstanceShipTypes` set to the
  weapon's type, which the engine never reads for a missile).

Outside the fixed block, you read and write:

| what | where | for |
|---|---|---|
| `g_pilot` | logic.h | the controls this step: steer, roll, trigger, missile key, afterburner (flight.c fills it) |
| `g_sys` | panel.h | the cockpit's systems: `armed` (master arm), `target` (the player's target slot, -1 none; T and Y pick), `lock`, `burning`, `fuel` (the engine keeps them) |
| the shots | shots.h | the gunfire list: `shot_add`, `shot(i)`, `shots_move`, `shot_hit` (logic.md, "Shots") |
| `g_camp` | campaign.h | the save: the player's ship type, the campaign's node and mission |

## Victory and loss

**The two words.** `missionStatus` (DS:0x0100) says how the flight ended. `victoryPoints` (DS:0x0116) says
whether the mission was won: nonzero won.

**The flight ends** when `missionStatus` is nonzero at one of the loop's checks (`rvflight.inc`). Who sets it:

| value | means | set by |
|---|---|---|
| 0 | flying | flight.c at the start |
| 1 | landed | the engine: C calls the carrier, and the flight ends when the call has been said. `/flight N` lands after N seconds (tests) |
| 2 | ejected | the engine: Ctrl+E twice within 2 s |
| 3 | on the carrier | `camp_load_units`, before the launch |
| 4 | dead | **your `step_deaths`**, after the player's explosion |
| 5 | left | a test script's Esc |

**Where `victoryPoints` comes from.** `launch()` in game.c:

1. **The roll.** Before the flight it rolls the stand-in, 80% won, into `victoryPoints`. The flight's music reads it
   during the flight: the way home is triumphant or sad by it.
2. **The flight:** `logic_start` .. `logic_end`. **You score over the roll here.** Set 0 in `logic_start`, nonzero
   in `step_mission` once the mission is done, and the final word in `logic_end`.
3. **`/winrate N`** (a test's switch) puts the roll back after the flight, so a script can steer the campaign
   whatever was flown (`tests/campaign.txt`). Without it, your score stands.
4. **`missionVictoryCalc`**, the layer's moment: in multiplayer the host's score becomes everyone's.
5. **The debriefing:** won or lost by `victoryPoints`, with the mission's `win` or `lose` lines, then
   `camp_resolve(won)`.
6. **The campaign:** each node has one to four missions. More won than lost takes the good way out, fewer the bad,
   an even split goes by the campaign's running total (a node of four: three or four won good, none or one bad, two by
   the total; engine.md, "The campaign").

**What the engine doesn't do yet** (mine to add when you want them):

- A death (`missionStatus` 4) is debriefed as a loss, and the campaign goes on: there is no funeral and no end of
  the game.
- Ejecting is debriefed like a landing.
- Landing doesn't check that the carrier is near: C works anywhere. The score is yours, though: `logic_end` can
  call a landing far from nav 0 a loss.

## The mission format

### `data/missions.txt`

One block per mission, a keyword and its value per line. `tools/rvdata.py` checks every block and compiles them
into `RVGAME.DAT` (run by `build.sh`, and by `host/build.sh` for the harness).

| keyword | form | meaning, units | limits |
|---|---|---|---|
| `mission` | `ID` | starts a block | 16 characters |
| `node` | `ID` | the campaign node it belongs to (campaign.csv) | |
| `title`, `briefing`, `win`, `lose` | text | the briefing's title and speech, the debriefing's lines (`briefing`, `win` and `lose` may repeat: joined) | 48, 640, 128, 128 characters |
| `type` | `patrol` | the mission's kind: `RvMission.type`, for your `step_mission` | only `patrol` exists |
| `music` | a cue | the briefing's music | |
| `nav` | `N x y z` | nav point N at x, y, z km from the launch point (nav 0) | N 1..15, each once; ±32,767 km; 8 a mission |
| `ship` | `TYPE side navN [count=N] [pilot=KEY]` | `count` ships of a type (a ships.csv key, `player` or `wingman`) of a side (0 Terran, 1 Aera, 2 Rlaan) at a nav point, with a cast pilot | 12 lines, 64 ships in all |
| `field` | `asteroid\|mine x z radius [density=N]` | an asteroid or mine field, km (rocks.c flies it) | 4 |
| `bar`, `lounge`, `talk` | | who sits in the rooms, and what they say | |

### From the file to the slots

1. **`RVGAME.DAT`**: one 1,424-byte `RvMission` record a mission (`src/rvdata.h`, which the same tool writes):
   `nav[8]` (id, x, y, z), `ship[12]` (type, side, nav, count, pilot), `field[4]`, the texts, the seats.
2. **At the briefing**, `camp_load_units` (campaign.c) fills the fixed block's mission tables:
   - **`missionShipTable`** (DS:0x68D0, 64 records of 42 bytes), a record per ship (a `count=3` line is three):

     | offset | bytes | meaning |
     |---|---|---|
     | +0 | 2 | the ship type (`player` and `wingman` become the player's) |
     | +2 | 2 | the side |
     | +4 | 2 | the leader (0xFFFF for the player) |
     | +6 | 2 | the behaviour: 6 the player, 4 a pilot in formation (the wingman), 0 the rest |
     | +8 | 2 | its nav point |
     | +10 | 6 | its place off the nav point, km (a group side by side, 1,500 km apart) |
     | +16 | 1 | its pilot |
     | +17 .. +41 | | free: AI skill, flags, the objective. Yours |

   - **`navPointTable`** (DS:0x6400, 16 records of 77 bytes), record N for nav N, nav 0 the launch point at the
     origin:

     | offset | bytes | meaning |
     |---|---|---|
     | +0 | 1 | its number |
     | +2 | 6 | x, y, z, km |
     | +8 .. +0x38 | | free (hazards, triggers) |
     | +0x39 | 20 | ten mission-ship words of the ships there, 0xFFFF none (the layer reads them) |

   - **`fieldTable`** (DS:0x7350, 7 records of 20 bytes): rocks.c's.
3. **At the launch**, `logic_start(m)` gets the `RvMission` itself (`m->nav`, `m->ship`, `m->field`) and puts the
   first ships in. `outerSpawnShipEntity(record, nav)` puts a record's ship at its nav point plus its place.

### Changing the format

- **A new mission type** (escort, strike, defend): add it to `TYPES` in `tools/rvdata.py`, then branch on
  `m->type` in your `step_mission`. Keep `m` (or what you need of it) in your own state in `logic_start`.
- **A new keyword** (an objective, a wave, a trigger): parse it in `tools/rvdata.py`, add the field to its record
  layout there (`RvMission`'s), and raise `VERSION`. The tool writes `src/rvdata.h` to match, and the engine
  refuses an `RVGAME.DAT` of another size. Or put it in the free bytes of a mission-ship or nav record.
- **The scale.** The placeholder missions put the nav points 1,000-5,000 km out, inside the radar's 8,000 km.
  Spacing like the old games' wants 15,000-30,000 km and the autopilot between; the tests use 20,000. The missions
  are `tools/campaign.py`'s output, yours to edit by hand.
- **The limits.** 31 ship slots, less the player, are shared by the wingmen, the enemies and the missiles in
  flight (`ship_spawn` refuses a slot past them, returning -1). A nav point's ships come in when the player gets
  there, and the dead leave room for the next nav's. The multiplayer layer still replicates only slots 0..9 until
  it reads the number from its `GameParams` (spec 4.2).

## The play tests

`tests/play/playtest.c` is the suite, in one file that builds for both sides; `tests/play/playtest.sh` runs it.

```
tests/play/playtest.sh              all 76 on the host
tests/play/playtest.sh M3 M5.2      milestone 3 and test M5.2
tests/play/playtest.sh --dos        on the host, then in DOSBox, and the hashes compared
tests/play/playtest.sh --steps M5.3 one test on both, a hash of each part of the world after every step
build/host32-nosdl/playtest --list  the tests and what each expects
```

**The output.** A line a test: PASS or FAIL, the world's hash, its id and what it checks. Under a failure, its
first three reasons:

```
playtest:      M2.3: firing set no cooldown in GUN_STATE's bits 0-6 (the layer copies gun records whole: the cooldown lives there)
playtest: FAIL D937F2C7 M2.3 cooldown (fireGunFromShip, step_timers): GUN_STATE counts the refire down
playtest: 1 of 61 pass  M1 0/11  M2 0/9  M3 0/12  M4 0/3  M5 0/8  M6 0/2  M7 0/6  M8 1/8  M9 0/2
```

**On the host**, the multiplayer layer's rules are checked at every thunk call, as in rvsim. A rule broken shows
as `step N broke a rule the multiplayer layer needs:` and the rule. `playtest.log`, beside the program, has the
game's own log. `--check-src` is always on, so every `do_damage` is tried again with src 0xFFFF, as M3.10 does by
hand.

**Host-only tests.** M5.1, M5.5, M9.1 and M9.2 ask the harness things the DOS build can't tell: which thunks were
called, what is on the screen. In DOS they show as SKIP.

**In DOS**, `RVP.EXE /playtest [IDS]` runs the same tests, its lines in `RVLOG.TXT`. `--dos` does the build and
the run, and then compares each test's hash with the host's:

```
hashes: 57 the same as the host's, 0 different
```

A test can pass on both sides and still differ: that is the 16-bit trap. Then `--steps ID` finds the step, and
names the arrays that differ first: kinds, positions, velocities, frames, speeds, hurts, guns, minds, mission.
From there, read your code for what touches those arrays at that moment, against logic.md's "Sized types" rules.

**The tests are yours too.** If your design differs from what a test expects, change the test:

- The tolerances are at the top: `TURN_SLACK`, `SPEED_SLACK`, `AIM_COS`, `AIM_SHARE`.
- The ships it flies are there too: `PT_PLAYER`, `PT_ENEMY`.
- Each test says in its message what it checks and why.

The file is written in the same C as the game (C89, the sized types, every string and table `__far`), because it
builds into RVP.EXE.

**How I checked them.** I wrote a quick logic of my own in the style of the old games, as a throwaway, and kept it
out of the repository so the writing stays yours. With it:

- all 61 tests pass on the host and the 57 DOS ones in DOSBox, every hash equal across 32 and 64 bits, gcc and
  clang, and DOS;
- every test fails against today's empty `logic.c`, except M8.7, which tests my plumbing;
- I broke it on purpose 13 ways, and each break was caught: a damage that depends on `src`, an unsquared frame, a
  gun that refuses, an AI that fires unaimed, an AI called without its thunk, armour that repairs itself, a puppet
  that thinks, instant speed, a roll the wrong way, a mission always won, nav ships that come again, `rand()`, and
  the 16-bit wrap above.

## Watcom and DOS traps

The build or the tests catch most of these; the last three you have to watch for.

- **Declarations come first in a block** (C89). Watcom refuses one after a statement; the host build warns about it
  (`-Wdeclaration-after-statement`).
- **`build.sh` doesn't stop on a C error.** `playtest.sh --dos` looks for `Error!` in its output. By hand, check
  `RV.EXE`'s time after building.
- **A string literal lands in DGROUP**, even in far code. Use `static const char __far s_x[] = "...";` and
  `rv_logf(s_x, ...)`, as logic.c does.
- **`__far` binds to one declarator.** `static const char __far a[] = "x", b[] = "y";` puts b in DGROUP. Write
  `__far` before each name.
- **A fixed-block address with a constant index, passed as a far pointer, points at segment 0.** Watcom makes a
  far pointer from a constant near address with segment 0, not DS: `_fmemcpy(&saved, &gPositionVector[0], 12)` reads
  the interrupt table, and `_fmemset(maybeEntityType, 0, 256)` would write it. An index in a variable is safe
  (`&gPositionVector[s]`), and near calls are safe: `memcpy`, `memset`, and every helper of world.h, shots.h and
  collide.h. The play tests' hash ran into this; nothing in the engine does it.
- **`%d` is 16 bits on DOS, `%ld` 32.** Pass an `int16_t` to `%d` and an `int32_t` to `%ld` in `rv_logf`.
- **`int` is 16 bits on DOS**, so a product of two `int16_t`s wraps there and not on the host. The rules are in
  logic.md, "Sized types", and the hash comparison is what catches the rest.

## What the engine still lacks

For play like the old games', beyond your logic (logic.md, "What the engine still lacks", has the full list):

- what follows a death or an ejection (above);
- a landing that needs the carrier;
- text from the logic on the cockpit's displays;
- shots against asteroids, and the AI's view of them;
- the multiplayer layer's support for RV.EXE beyond recognising it.

These are mine to build when you ask.
