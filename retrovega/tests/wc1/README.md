# The AI against the old game's: how it is measured

The AI's tables (docs/ai.md) are meant to reproduce the behaviour of the space combat of the early nineties, so
this directory flies the old game itself, as a black box, and ours in the same scene, and compares what the
ships did. Nothing here reads the old program's code: the multiplayer layer already knows where its ships' slots
are (`src/cpu/wcnet_ds.def`), and the trace is what any spectator could see.

## The old game, traced

`run-old.sh OUT SERIES MIS "KEYSCRIPT" [SECONDS]` runs it alone under the layer (`WCNET=0`: no network, the mission
picked by `SERIES` and `MIS`, the barracks and the briefing passed by the layer's own keys), with:

- `WCNET_SHIPTRACE=file`: a line a ship slot a frame (`S frame slot entity type side pilot x y z fx fy fz ux uy uz
  vx vy vz speed setspeed aistate aitimer missiletarget parent hull shf shr arf arr arl arrt`), a line a gun
  fired (`F frame ship gun`), a line a hit (`D frame src dst qty`);
- the key script in space (the items are listed in `src/cpu/wcnet_hooks.cpp` above `key_script()`): `a` the
  autopilot, `!rocks=0` no asteroids, `!face=npc` the nose onto the nearest enemy, `+space`/`-space` the trigger,
  `@tag` a dump of the data segment (`dsdump.py tag.bin --slots --stats` reads the nav points, the mission ships,
  the slots and the ship stats from it), `!shot=name` a screenshot;
- a callsign with CHEAT in it: the player takes no damage (else the game restarts the mission when it dies, and
  the trace holds two flights).

`trace.py TRACE [--fps N] [--timeline] [--slot S]` splits a trace into flights (the frame count restarts) and
each slot's lives (a slot changes ships at a nav point), and for each AI ship against the player prints its
speeds, turn rates, distances, how much of the time its nose is on the player, when and from how far it fired,
its closest passes, and its AI state words. The old game steps 20 times a second; ours 30 (`--fps 30`).

`dump-all.sh OUT` dumps every mission of the campaign at its first second in space: the nav points and the
ships of each (for porting them).

## Ours, in the same scene

`compare.sh` builds rvsim and flies `enyo1.scene` (the first mission's nav 1 as the old game's trace had it at
the arrival: three Dralthi, here Nicanders, 15,000 km ahead and to the left in a loose line, crossing; the wingman
750 km to the right; the player at the 300 setting) twice for four minutes: the player flying straight, and the
test pilot hunting (`--hunt 3`: the nose onto the nearest enemy every 3 s, the trigger held), both invulnerable
(`--nodamage`), each traced (`--shiptrace`) and summarised the same way. `rvsim --scene FILE` takes `ship TYPE
SIDE X Y Z YAW [PROFILE] [KPS]` lines (km from the player, who starts at the origin facing +z; the profile byte
17 of a mission ship record, docs/ai.md) and `player KPS`.

## What the old game did (Enyo 1, nav 1: three Dralthi against a Hornet, 2026-10-30)

Two runs: the player flying straight at the 300 setting for four minutes, and the test pilot hunting.

| what | the old game, player straight | the old game, player hunting |
|---|---|---|
| speed (km/s of its positions, median / 90% / most) | 465-800 / 805-950 / 2400 (afterburner) | 620-785 / 810-1430 / 2425 |
| turn rate, degrees a second (90% / most) | 108-126 / 640 | 86-138 / 336 |
| nose on the player (share of the time within 25 degrees) | 22-96% | 38-67% |
| first shot | 25-28 s in, at 2,455-5,129 km | 30-32 s in, at 2,748-4,225 km |
| shots, their range (median) and their aim off the player (median) | 850-5,200 km (1,200-3,400), 0.0 degrees | 290-5,500 km (1,900-2,300), 0-21 degrees |
| closest passes | 400-1,200 km | 250-800 km |
| rams | none (the hits with a ship as source are its missiles') | none |
| AI state words seen | -1 always, 1 and 8 now and then, 9 dying | the same |

Two facts of its world that ours does not share:

- **Its ships are small.** Its ship radius is 100 km for the Hornet and 160 for the Dralthi (`gMaybeShipRadius`),
  so two fighters pass 250 km apart without touching. Ours have half boxes of 400-450 km (a sprite full size at
  2,500 km, `collide_half`): a pass must be 1,000 km wide, and a wing flying together into one target flies into
  itself. The AI's lanes and its pass beside the target (docs/ai.md, `pass_side`) are there for that; whether
  the world's scale should change is a decision for the game's author.
- **Its speed setting is twice its km a second**: a setting of 200 moves 25.6 positions a frame at 20 frames a
  second, 2 km/s a point. Ours moves 1.5 (`KPS_HZ` 20 at 30 steps a second, world.h). The real speeds of the two
  Dralthi-class fighters still come out alike (the old one's 400 setting at 2, our Nicander's 500 at 1.5).

## Ours, after the first tuning (the same scene)

| what | ours, player straight | ours, player hunting |
|---|---|---|
| speed (median / 90% / most) | 540-720 / 750-790 / 1425 | 730-750 / 750 / 1020 |
| turn rate (90% / most) | 200-215 / 285 | 215-225 / 265 |
| nose on the player | 25-38% | 19-35% |
| first shot | 12-23 s in, at 2,970-3,090 km | 10-19 s in, at 2,940-3,640 km |
| shots' range (median) and aim | 720-3,230 km (2,400-2,500), 8-9 degrees | 1,530-3,640 km (2,470-2,830), 6-12 degrees |
| closest passes | 1,190-2,400 km | 230-1,250 km (the test pilot rams them) |

What the tuning changed: no sit-and-fire for fighters (the old game's never stop), the break distance 1,400 km
(its passes at 1-3 radii, in our scale), attacks spread into lanes and passing beside the target, a bump sending a
fighter breaking away, and a bump's damage a dozen points at most. Still apart: ours turn faster (the Nicander's
105 degrees a second at 1.5 is 157, and a yaw with a pitch more), aim looser (its shots go at 0 degrees: it fires
on the lead point exactly; ours within a 7-degree cone), and open fire from 3,000 km where its lasers reach 5,000
(`data/weapons.csv`'s ranges).

## What is still to measure

The other types (the Salthi at nav 3; Krant, Gratha, Jalthi, the capital ships and their turrets in the later
missions: `dump-all.sh` lists which missions have which), the wingman's own behaviour (it flies in formation at 750
km, breaks to attack, chases far), and the AI state words 1 and 8 (what the ship does while they stand).
