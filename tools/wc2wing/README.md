# wc2wing: a third seat for Wing Commander II missions

The multiplayer hooks seat a second human in a fighter of the player's
wing.  A mission that launches two such fighters seats two: the host and two
wingmen.  `wc2wing` gives a mission a second wing fighter, a copy of the
first wingman's record (or of the player's ship, in a mission the story
flies alone) that the game's own AI flies when no human takes it.  It works
on any `MODULE.00n` of WC2.EXE, SO1.EXE, SO2.EXE or a campaign built on them
(SO3's `MODULE.003` becomes SO3P's `MODULE.004` this way, `so3/build.py`).

    cargo build --release --manifest-path tools/wc2wing/Cargo.toml
    tools/wc2wing/target/release/wc2wing list wc2/GAMEDAT/MODULE.002 -m s1m1 --gamedat wc2/GAMEDAT
    tools/wc2wing/target/release/wc2wing add  wc2/GAMEDAT/MODULE.002 -o MODULE.002 --gamedat wc2/GAMEDAT --make-room

`list` prints each mission's player, wing list, and the slot every ship of
the launch nav point takes, marking the seats a human can have.  `add`
writes a copy of the file with the new ship in every mission (or those of
`-m s1m0,s2m3`): in the header's wing list, in the launch nav point's list
so that it takes slot 3 where the list allows (the hooks seat the second
client in any free wing fighter, so another slot works too), in formation
behind the wingman, 500 km to one side and 1000 km back.  `--name` and
`--pilot` (a COMMUNIC personality with an INTEL file: 9 is Doomsday, 12
Downtown, 4 Stingray, 36 Maniac) name the pilot; the default is the
template's pilot and its name with " 2".  Missions that launch two wing
fighters already are left alone (`--force` adds one anyway).

A mission has sixteen ship records and ten places in a nav point's list.
Where both are full, `--make-room` takes out the enemy follower (a ship of
the Kilrathi side flying in formation behind a leader) with the highest
index that nothing else refers to, and says which; without it the mission is
reported and skipped.

No dependencies beyond Rust's standard library; the file format is
described in `docs/so3.md` and `scripts/wc2data.py`, and the slot order at
launch at the top of `src/main.rs`.
