# The SO3 test rig

Shell scripts that drive the native emulator (`build-native/src/dosbox`)
with its key script (`WCNET_KEYSCRIPT`, see `src/cpu/wcnet_hooks.cpp` for the
`!kill`, `!killfoes`, `!status`, `!rocks`, `!pos`, `!face` (a slot, `npc` or `target`), `!mouse`, `!button`, `!shot`, `@dump`,
`skip` and `clickskip` items).  Output goes under `$S` (default `out/` beside
the scripts); nothing here touches the game's own files.

* `w2solo.sh OUT SECONDS "s m" KEYS` -- one program alone (`WCNET=0`),
  mission test mode; `DIRECT=" "` for the barracks.
* `w2duo.sh OUT SECONDS "s m" HOSTKEYS CLIENTKEYS` -- host + wingman on one
  machine (`CLIENTGAMEDIR` gives the wingman its own game directory: two
  programs on one directory corrupt each other's save files).
* `spawntest.sh` -- every mission once with two programs: launch, autopilot
  to the first nav, free memory, a screenshot; `out/spawntest.log`.
* `campaignrun.sh OUT [SECONDS] ["s m"]` -- the campaign from the barracks,
  host + wingman (the wingman restarted if its program drops), both
  invulnerable (CHEAT callsigns), every mission flown by autopilot with the
  host's `!killfoes`, scenes clicked through; the host traces file opens.
* `sweep.sh "s m" ...` -- one mission slot at a time in campaign mode, the
  transition after it included; `out/sweep.log` has a line per slot.
* `facecheck.sh` -- screenshots of a briefing and a debriefing.

What the rig cannot do is fly: it autopilots and kills.  It loses the
player to SO2's bomb at 2-2 (the scripted death), and it needs the real
landing (the autopilot to the landing carrier) where a mission lands
anywhere but the launch carrier.  docs/newcampaign.md has the lessons.
