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
   | `SIT_PASSED` | within 2 × `break_km` and off the nose: just passed, or about to |
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

- **Straight** (`bank` 0, or within 20° of the way): a yaw and a pitch at once, each a full steer beyond
  `FULL_AT` (10°) off and eased in within it. So a target 10° up and right gets both.
- **Bank and pull** (`bank` 1, farther than 20° off): the target's bearing in the right-up plane is taken, the
  nearest allowed **pull** (right, left, up or down) is chosen, the ship rolls toward it (a roll of +t, the right
  wing up, turns a bearing by -t), and pulls along it, full once the bearing is within a quarter turn of the pull.
  This is how the fighters of the old games turned: a roll, then the nose comes round.
- **The pulls** (`pulls`, `PULL_*` bits): a profile that may not pull one way never does. A ship that cannot
  turn left (`P_NOLEFT`) given a target on its left rolls until the target is above or below it and pulls there.
  A profile that may pull nowhere flies straight.

## The profiles

`k_profiles[P_*]`, one row each; a ship's comes from its type and side (`profile_of`: a capital ship
`P_CAPITAL`; the player's side `P_WINGMAN`; Agis, Shizu, Gaozong `P_INTERCEPTOR`; Areus, Taizong `P_BOMBER`;
Cleomenes, Zhuangzong `P_HEAVY`; Convolution `P_ACE`; the rest `P_FIGHTER`), or from byte 17 of its mission ship
record (`P_*` + 1; 0 the type's own), which a mission may set.

| field | meaning |
|---|---|
| `engage_km` | an enemy this near is fought |
| `fire_km` | the guns fire within this (and their own range) |
| `break_km` | a closing attacker breaks off this near; 0 never (it rams) |
| `pounce_km` | `T_WAIT`'s chance distance |
| `aim_cos` | the fire cone (cos × 256) |
| `pulls`, `bank` | the steering's allowed pulls and style |
| `ab_pct` | the chance a tactic that closes or flees uses the afterburner |
| `flee_pct` | the hull percent below which it runs (0 never) |
| `missile_pct` | the chance a completed lock is a launch |
| `wait_s` | `T_WAIT`'s patience |
| `idle` | its tactic with nobody to fight (`T_CRUISE`, `T_FORMATION`, `T_ESCORT`, `T_WAIT`) |
| `pick[SIT_*][4]` | the dice: up to four (tactic, weight) a situation |

The rows as they stand:

| profile | character |
|---|---|
| `P_INTERCEPTOR` | light and fast: head-on passes and pursuits, strafes near, evades and loops when tailed, runs at 30% hull |
| `P_FIGHTER` | the line fighter: head on, round again, loops; runs at 20% |
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

The tactics' numbers are meant to be measured, not guessed: `tests/wc1/` (to come) flies the first mission of
the old game under the multiplayer layer with its ships' positions, headings, speeds and fire traced each frame,
the same scene flown here with the matching types, and the two traces compared: when the enemies turn in, how
close they come before they break, how often they pass and loop, how far out they open fire. The results and
the rows they changed are recorded there.
