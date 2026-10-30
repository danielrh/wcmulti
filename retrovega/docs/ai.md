# The AI: minds, tactics, profiles

The ships of `CTL_AI` are flown by minds in `src/logic.c` (`aiFighterThink`, `aiShipThink`), built so that
the space combat of the early nineties can be reproduced number for number, and other behaviours written as
new table rows. This page is the reference: what a mind does each step, the tables, how to add a behaviour,
and how the behaviours are checked against the old games' flights.

## A step of a mind

Each step, for each AI fighter (through the `aiFighterThink` thunk, which the multiplayer layer skips where the
ship is a puppet):

1. **The target.** Its current one is kept while it lives, is an enemy and stays within 1.5 × the profile's
   `engage_km`; else the nearest enemy within `engage_km` (never a cloaked one, `rvFlags` 16). `rvTarget[s]`
   says which.
2. **The view** of the target (`View`): where it is in the ship's own frame (right, up, ahead, km), its lead
   point (where the guns' shots would meet it: the target's move over the shot's time of flight, twice refined),
   how straight the nose points at it (`cos_nose`) and at the lead point (`cos_aim`), and how straight *its*
   nose points at the ship (`cos_its`: is it attacking me).
3. **The situation** (`SIT_*`), from the view and the profile:

   | situation | when |
   |---|---|
   | `SIT_IDLE` | no target |
   | `SIT_HURT` | the hull below the profile's `flee_pct` |
   | `SIT_TAILED` | the target behind, within `TAIL_KM` (3,500 km), and pointing at the ship |
   | `SIT_PASSED` | within four hull sums (`room_of` × 4) and behind or off the nose: just passed, or about to |
   | `SIT_NEAR` | ahead, within 4/3 × `fire_km` |
   | `SIT_FAR` | the rest |

4. **The tactic.** A tactic runs for its time (`tactic_steps`) or until its own end; then the dice are rolled
   over the profile's row for the situation (`pick[sit]`: up to four tactics with weights, from `game_rand`,
   the play's one random stream). A tactic that ends early (a pursuit that has passed its target, a head-on run at
   its break distance) picks the next at once.
5. **The tactic's step:** where the nose should go (a direction in the ship's frame), the throttle (through the
   `aiSetSpeed` thunk), the afterburner (`rvAfterburner`), and whether the guns bear and a missile is locked
   (`g_mind[s].fire`, which `step_fire` acts on through `maybe_fire_all_guns` and `fireGunFromShip`).
6. **The steering** (`steer_to`) turns the wanted direction into the same `steer_x`, `steer_y` and `roll`
   the player's controls give (`g_turn[s]`, -9..9); `step_motion` then flies it at the type's rates. So an AI
   can do nothing a player could not.

Capital ships (`aiShipThink`) hold their course at cruise, and each step mark the turrets that bear (cooled,
the nearest enemy within the turret's range) for `step_fire`; a launcher fires at the nearest enemy capital
ship in range every 10 s. A turret's shot goes toward `rvTarget`'s lead point (the guns' code aims it).

## The tactics

| tactic | what it does | ends when |
|---|---|---|
| `T_CRUISE` | flies on at cruise | 2 s (then the dice again) |
| `T_FORMATION` | keeps station on its leader: `formationOffset` (right, up, ahead of the leader, km; taken from where the ship is when it has none, at least 800 km to a side), the leader's heading once there, the throttle closing the gap along the leader's way and then matching | an enemy within `engage_km` |
| `T_ESCORT` | circles its leader at 2,500 km | the same |
| `T_WAIT` | sits still and cloaked (`rvFlags` 16) for up to `wait_s`, until a target comes within `pounce_km` not pointing at it (or within half that), then pursues on the afterburner | the chance, or its patience |
| `T_PURSUE` | at the target's lead point; top speed while far, then the target's speed plus 60 kps; fires when the guns bear | 8 s; the target behind and near; the break distance (then `T_BREAK`) |
| `T_HEADON` | straight at the lead point at top speed, firing | the break distance (then `T_BREAK`) |
| `T_STRAFE` | the same, but through: it does not break, it passes | the target behind and past the break distance |
| `T_SIT` | throttle 0, turning onto the target, firing | 3 s |
| `T_RAM` | at the target, afterburner, never off | never |
| `T_TORPEDO` | at a capital ship until a torpedo is away (within 2,000 km, after the lock), then `T_BREAK` | the launch; no rounds |
| `T_BREAK` | a hard pull one way (chosen by the dice) with a roll, at top speed | 1.5 s |
| `T_EVADE` | a pull with a roll one way, then the other; then either a stop (throttle 0, the pursuer overshoots) and away on the afterburner, or a climb away | 4 s; the tail clear |
| `T_LOOP` | a whole loop, nose up, at cruise | the loop's time (360° at the type's pitch rate) |
| `T_FLEE` | away from the target at top speed | 6 s |

The guns fire when the target is within `fire_km` (and the first gun's own range) and the lead point within the
profile's `aim_cos` cone (the default `AIM_CONE`, about 7°); a missile when a launcher has rounds, the target
has sat within `LOCK_CONE` (26°) and in range for the weapon's lock time, the last launch is 4 s past, and the
dice say so (`missile_pct`).

## The steering

`steer_to(s, profile, dx, dy, dz)` wants the nose along (dx right, dy up, dz ahead):

- **Straight** (a profile with every pull and `bank` 0, or any banking profile within 20° of the way): a yaw and
  a pitch at once, each a full steer beyond `FULL_AT` (10°) off and eased in within it. So a target 10° up and
  right gets both: the fine aim, which the old games' fighters had (their shots at 0° off).
- **Bank and pull** (`bank` 1 farther than 20° off, or a profile without every pull and `bank` 0 always): the
  target's bearing in the right-up plane is taken, the nearest allowed **pull** (right, left, up or down) is
  chosen, the ship rolls toward it (a roll of +t, the right wing up, turns a bearing by -t), and pulls along it,
  full once the bearing is within a quarter turn of the pull. This is how the fighters of the old games turned: a
  roll, then the nose comes round, flat.
- **The pulls** (`pulls`, `PULL_*` bits): a profile that may not pull one way never does. A ship that cannot
  turn left (`P_NOLEFT`) given a target on its left rolls until the target is above or below it and pulls there.
  The line fighter pulls right and left only: it yaws and never pitches, as the old game's Dralthi did. A profile
  that may pull nowhere flies straight.
- **Avoidance** (`avoid`, within `steer_to`): every other ship within the profile's `avoid_pct` of the two half
  boxes and coming together with this one pushes the wanted direction sideways, the harder the nearer; and the
  attackers of one target take lanes a hull apart (`pass_side`).
- **A break** (`T_BREAK`) is not steered: a profile that yaws turns flat at its rate, away from a target still
  ahead and after one gone behind (over as soon as the nose is on a target ahead again: the pursuit takes it from
  there); a banking profile that may pull up or down pulls with a roll toward the target's side. The afterburner
  for the profile's `burst` at its start, and an AI's burner is at the type's `ab_kps` at once (the old games'
  dash), where a player's ramps up.

## The profiles

`k_profiles[P_*]`, one row each; a ship's comes from its type and side (`profile_of`: a capital ship
`P_CAPITAL`; the player's side `P_WINGMAN`; Agis, Shizu, Gaozong `P_INTERCEPTOR`; Areus, Taizong `P_BOMBER`;
Cleomenes, Zhuangzong `P_HEAVY`; Convolution `P_ACE`; the rest `P_FIGHTER`), or from byte 17 of its mission ship
record (`P_*` + 1; 0 the type's own), which a mission may set.

| field | meaning |
|---|---|
| `engage_km` | an enemy this near is fought |
| `fire_km` | the guns fire within this (and their own range) |
| `break_km` | a pursuit with a pass coming (`passing`: closing at a tenth of the top speed or more) breaks off this near; 0 never (it rams) |
| `pounce_km` | `T_WAIT`'s chance distance |
| `aim_cos` | the fire cone (cos × 256) |
| `pulls`, `bank` | the steering's allowed pulls and style (below) |
| `ab_pct` | the chance a tactic that closes or flees uses the afterburner |
| `flee_pct` | the hull percent below which it runs (0 never) |
| `missile_pct` | the chance a completed lock is a launch |
| `wait_s` | `T_WAIT`'s patience |
| `idle` | its tactic with nobody to fight (`T_CRUISE`, `T_FORMATION`, `T_ESCORT`, `T_WAIT`) |
| `pass_pct` | the attackers of one target spread across it this percent of the two half boxes apart (lanes), and a pass goes by its own side of the target by as much |
| `avoid_pct` | another ship (the target too) within this percent of the two half boxes, coming together at a quarter of the top speed or more, pushes the wanted direction sideways |
| `turn_pct` | it uses this percent of its type's turn rate at most |
| `burst` | steps of afterburner at the start of every break (the old games' dash); 0 none |
| `lead_pct` | how much of the lead it aims off: 100 the shots meet the target, 0 it aims at the target itself (the old games' fighters) |
| `tail_pct` | behind its target within the guns' range it flies this percent of its top speed |
| `fire_gap` | steps between its volleys with the target in the cone (the old game's fighters: one every 0.75 s) |
| `pick[SIT_*][4]` | the dice: up to four (tactic, weight) a situation |

The rows as they stand:

| profile | character |
|---|---|
| `P_INTERCEPTOR` | light and fast: head-on passes and pursuits, strafes near, evades when tailed, runs at 30% hull; yaws, no pitch |
| `P_FIGHTER` | the line fighter, as the old game's Dralthi measured (tests/wc1/README.md): straight at the target at full speed, a hard flat yaw away at 900 km with a dash, round again in a second or two, a tail at 60%, a volley every 0.75 s; runs at 20% |
| `P_HEAVY` | slow and armoured: head on mostly, breaks rather than jinks, rarely runs |
| `P_ACE` | strafing runs, hard evasion, never runs, uses the afterburner half the time |
| `P_BOMBER` | torpedo runs at capital ships, else a heavy's fight; runs at 25% |
| `P_WINGMAN` | the player's side: in formation until an enemy comes within 12,000 km, pursuits, breaks |
| `P_CAPITAL` | holds its course; turrets and launchers |
| `P_WAITER` | lies in wait up to 30 s, then an interceptor's fight on the afterburner |
| `P_NOLEFT` | a fighter whose controls cannot turn left: it rolls and pulls up or down instead |

## Adding a behaviour

- **A new character:** a row in `k_profiles` and a name in the `P_*` enum, then `profile_of` or a mission ship
  record's byte 17. Nothing else changes.
- **A new maneuver:** a `T_*` name, its length in `tactic_steps`, its step in `run_tactic` (the nose through
  `steer_to` or `g_turn[s]` directly, the throttle through `throttle`, the fire through `aim_and_fire`), and
  rows that use it.
- **A new control limit:** a `pulls` mask; or a new rule in `steer_to` for what the mask cannot say.
- **A new situation:** a `SIT_*` name, its test in `situation`, a column in every profile.

Everything an AI decides goes through the ship's type (its rates and speeds in `step_motion`), so a row cannot
make a ship fly faster or turn harder than its data allows; the play tests check it (M5.6).

## Checking against the old games

The tactics' numbers are measured, not guessed: `tests/wc1/` flies the first mission of the old game under the
multiplayer layer with its ships' positions, headings, speeds and fire traced each frame, the same scene flown here
with the matching types (`compare.sh`), and the two traces digested alike: speeds, turn rates, how long the nose is
on the player, when and from how far they fire, every pass and the time until the nose is back, collisions. The
results, the line fighter's row as set from them, and what is still apart are in `tests/wc1/README.md`; the play
tests M5.11 and M5.12 hold the row to that measure on every build. `RV_TACTICS=1` has rvsim log each tactic taken.
