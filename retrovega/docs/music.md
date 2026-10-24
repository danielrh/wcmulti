# The flight's music

What plays in space, how it is written, and how the game picks it. The
rooms' songs are in [engine.md](engine.md) ("Music"); the engine that plays
them all is `src/music.c` on the OPL3 (`src/opl.c`, our bank
`tools/oplbank.py`).

## The user's brief (2026-10-23)

- **Space:** many songs, pensive but curious, like the Vega Strike pieces
  they played us (terranpeace, terranpeace2, peace4, peace5_p).
- **Home:** a very calm one for the way home, once the mission is done and
  the carrier is targeted.
- **Battle:** like terranbattle.
- **Stings:** three short victory songs for an enemy splashed, and a short
  tragic one anyone would know again.
- **Panic:** three songs for a ship falling apart, from tense to full panic.
- **Length:** not long songs, but some 15 to 30 minutes of music in all.
- **Reference:** the songs are OPL MIDI, so the references were inspiration
  only.

**What the references have in common** (librosa, chords every two beats):

- **Harmony:** minor and modal (B minor, C minor), sus chords and open
  fifths, pedals and drones (peace5_p is C minor throughout), i-VI-VII and
  i-v-VI.
- **Texture:** arpeggios turning under a sparse tune, and layers coming in
  one by one.
- **Tempo:** 95-110, or half time.
- **Battle:** terranbattle is a C sus pedal with hits on every beat, Fm,
  Gm and B flat passing over it.

The songs are our own, in that language. They are not copies.

## The user's notes on the first draft (2026-10-24)

They listened through a preview page and wrote notes at times. In short:

- **Cruise:** too casual in places ("Pokemon", "bar music", "cheap"), and
  they wanted an "orchestral feeling of wonder". Builds had no payoff.
  Intros were too quiet for loops.
- **The way home:** one song, looping, starting at once, never sleepy,
  "dopamine and relief".
- **Battle:** Contact perfect, Furball nearly. All the combat songs should
  be "lego pieces" that loop perfectly or combine. Break and Attack was
  repetitive, "racing music". Crossfire was lethargic, but would suit
  asteroids.
- **Panic:** Hull Breach must keep the fight's pace, with the damage as
  dissonance over a fight. Systems Failing's builds need payoffs. Abandon
  Ship should run panic, hope, dashed, hope, dashed; its speed-up was
  "brilliant".
- **Stings:** too happy, they want longing. The loss was not tragic
  enough: it should be a recognisable dirge.
- **Their question:** intro tracks and loop tracks? One file each, with a
  loop marker the engine follows (below), does it at the tick.

## The third draft (2026-10-24)

The user's verdict on the second: the cruise songs "sound like elevator
music--not memorable". They sent two cues from another score for the
feeling of excitement and wonder, and ruled that ours stay our own: the
cues set the feel, nothing is taken from them note for note. Their program
numbers are MT-32 tones, not General MIDI (the user spotted it): synth
brass, French horns, strings, timpani, piccolo, harp and an orchestra hit.

Measured against ours, the cues were fast (165 in 4/4, 123 a dotted beat
in 12/8, against our 72 to 108), lean (three parts sounding against six
to eight), restless (a note in every fifth of a second), chromatic (15%
of their note time outside one key: lifts of a major third, minor-third
cycles), sparkling (one to three fast notes a second: runs, sweeps), hit
where we had a beat or nothing, and short (30 to 40 s loops ending in a
riser). The plan: a new set of cruise pieces in that style and the way
home in two versions, step 1 first for the user's verdict.

**Step 1** (commit 744050f0) built the orchestra, Outbound and the way
home. The user: Outbound "hits on all counts... fix all the flight music
to have that same style but their own unique character and be remixable
and loopable so the orchestral score is complete". **Step 2** did that:
every flight piece rewritten for the orchestra, and the score's remixes.

- **The orchestra** (`tools/orch.py`) on programs no room song uses:
  spiccato strings (44) for the motor, an orchestra hit (55), a piccolo
  (72), a synth brass that bites (62, 63); with the horns, strings,
  timpani and harp. The crash is 6 dB down, the user's note: it stood 4 dB
  over the mix (A-weighted).
- **The balance** of the parts is the reference render's (the user liked
  it), measured K-weighted against the motor: the brass 8 dB over it, the
  horns 5, the piccolo 4, the hits 7, the crash 5. `ORCH` sets each
  part's channel, program and level; `orchestra(s)` puts them on.
- **The guardrail:** `tools/songcheck.py` measures every flight song
  against bands from the two cues (notes a second, motor, voices, off-key
  share, fast notes, drum hits, bars without a change), the fights and the
  rock field in bands of their own, and checks the markers: a loop marker,
  blocks on bar lines, at least four blocks in a fight, nothing just past
  a bar line. Every piece passes.
- **The user's notes on the first full set** (2026-10-24, timestamps on
  the listening page): Sensor Sweep's scan of note pairs with the piccolo
  asking and the muted trumpet answering was "too childish like random
  buttons", "too kings questy"; Deep Lane's D flat block (second pass)
  "loses the thread... should tighten up a lot and be moodier"; Waypoint's
  horn answers "too repetitive and happy", its stabs and its way home "too
  happy and jumpy", "too much like a video game", while its lift "gets good
  again"; Starwake "feels somehow repetitive with the rest... we need
  something special here". Each of those passages is rewritten; what they
  liked (Sensor Sweep's G major, Waypoint's lift) is kept. Starwake is in
  3/4 now (`Piece(..., beats=3)`: the figures take the bar's length from
  the piece).

## The songs (CC0, ours)

| cue | title | key, tempo | what |
|---|---|---|---|
| CRUISE1 | Outbound | E minor, G# minor, C, E; 160 | launch: the motor in octaves, a synth-brass call and horn answers, hits on 3+3+2, a break into C, the horn's tune, home to E major, a timpani riser |
| CRUISE2 | Wide Orbit | G minor, 12/8 at 116 | wonder: harp rolling two octaves over a turning spiccato, the horn singing, round the minor-third cycle G, B flat, D flat, E minor |
| CRUISE3 | Sensor Sweep | B minor, 152 | searching: the spiccato's beam (the bass, and a line up the chord and back every two bars) under the horn's rising fourths, then G major opens for the horn's tune with the hits |
| CRUISE4 | Far Light | D, 12/8 at 124 | heroic wonder, the brightest: strings galloping under harp sixteenths, the synth-brass fanfare lifted D, F#, B flat |
| CRUISE5 | Deep Lane | F minor, 144 | dark wonder: low strings in 3+3+2, the horn's tune falling through D flat and G flat, a darkest block (B flat minor to C7) of one falling figure on the 3+3+2 with the timpani, diminished piccolo flourishes |
| CRUISE6 | Long Burn | A minor, 12/8 at 112 | anticipation: eight bars of repeated A's burning under turning chords, then the release into C# minor, the strings' tune |
| CRUISE7 | Waypoint | C minor, 168 | brisk travel: the strings at a gallop over a pedal C, the brass's call in long notes over the horns' falling line, a lift through A flat and B flat, home through A flat and B flat to C |
| CRUISE8 | Starwake | G lydian, 3/4 at 156 | the cycle's crown, the one piece in three and in a single pass: the harp's five-note figure turning over the bar, a tune that leaps a sixth, hemiolas, a hush, the last climb |
| HOMEWARD | Homeward, won | B flat, 12/8 at 108 | harp eighths, the horn's tune, a lift into D for a brass chorale, home through B flat |
| HOMELOSS | Homeward, lost | B flat minor, 12/8 at 92 | the same, slower and sadder: horns for the brass, no hits, sinking a major third to G flat |
| BATTLE1 | Contact | C sus pedal into C minor, 140 | the call on the horn answered by synth brass over spiccato sixteenths, low strings and timpani |
| BATTLE2 | Break and Attack | C minor, 140 | half-time hits over the motor at full speed, a riff of its own on strings, a Neapolitan horn tune, a lift into A flat |
| BATTLE3 | Furball | C phrygian, 140 | the riff on synth brass and horn over driving fifths, up a minor third the second time, a tutti stop |
| ROCKS | Rock Field | G minor, 120 | a march through the rocks (snare, timpani, low strings), the horn's theme, a drift through B flat with the harp and piccolo glinting, a brass descant |
| PANIC1 | Hull Breach | C, 140 | the fight going on under the damage: a low cluster, alarms in seconds, the call a tritone off |
| PANIC2 | Systems Failing | C rising, 140 | the bass climbing a half step a bar; every climb pays off in two wild bars |
| PANIC3 | Abandon Ship | 152 to 196 | panic, hope (the call in major), dashed, hope higher, dashed harder, a speed-up |
| VICTORY1..3 | Splash One..Three | C, D, B flat | bittersweet: bVI-bVII-I, the minor plagal, a deceptive turn |
| TRAGEDY | Wingman Lost | B flat minor, 52 | a funeral march, 14 s |

Some 21 minutes of music, every piece its own, each going round from
its loop. `tools/flightsongs.py` has Outbound and the way home and gathers
the rest from `cruise_a.py`, `cruise_b.py`, `battles.py` and `panics.py`,
all written with `tools/orch.py`.

- **The cruise cycle:** each cruise piece's riser lands on its own
  dominant, and the next in the cycle starts in its key: E minor, G minor,
  B minor, D, F minor, A minor, C, G, and round to E.
- **Blocks:** `Piece.sect(bar)` (or `sections`) writes a "sect" marker at
  each block. A fight's block stands alone, so it can follow any block of
  any fight: it starts on its downbeat with the motor and a hit, in the C
  minor family, and no note is held over its end.
- **Loops:** a "loop" marker meta event (`FF 06 "loop"`) after a song's
  intro is where it goes round again from. The engine plays the intro once
  (`music.c`: the marker becomes a do-nothing event, `Song.loop` its index;
  the ring wraps there and that event's delta is zeroed, since the last
  bar's rest has brought it to the bar line). `oplrender` follows it too.
- **Levels:** evened out by `level=`: the way home at -25.6 to -24.6 LUFS,
  the cruise at -24.5 to -23.4, the rock field -22.8, the fights -22.2 to
  -21.8, the stings -20.8 to -19.1.
- **Voices:** none steals a voice: at most 18 sound at once.
- **Whole bars:** a loop's length is its last event's bar, so nothing may
  sound past the last bar line: ramps, swells and sweeps end their last
  step inside it.

**Previews:** `build/oplrender --bank build/dos/RVOPL.BNK --mid
build/dos/CRUISE1.MID --out CRUISE1.wav` (tools/oplrender.sh builds it)
plays a song through the game's own driver. `RV.EXE /midilog` records what
the game sent, and `oplrender --log OPLLOG.BIN` plays that back.

## Picking them: src/score.c

The score only watches. Once a frame (`flight_music`) it reads the ships'
slots and the ship's systems (panel.h), so combat need not tell it
anything. The rules come in this order:

1. **Dead** (hull 0): the tragedy, once, and then quiet.
2. **Panic:** with the hull under 60%, 35% and 15%, the first, second and
   third panic song. A panic stays, since the hull does not mend in flight.
3. **Fight:** an enemy within 4,000 km starts a fight, a little beyond the
   3,000 km of its hail. Twelve seconds with none within 6,000 km end it.
   They spawn some 5,000 km out, so a flight begins calm.
4. **Home:** the mission is done (every enemy gone, after there were
   some), or a friendly capital ship is the target, or the player is
   calling the carrier to land (C). Won, Homeward; lost, Homeward's sadder
   twin. Which is the mission's `victoryPoints`, the fixed block's word
   the layer carries to the other players at the mission's end, as in WC1.
   Until combat scores a mission, game.c rolls it at launch (80% won), so
   the way home already knows; at the end `missionVictoryCalc` is the
   layer's moment to put the host's score in, and the debrief follows it.
5. **Rocks:** inside an asteroid or mine field of the mission (its centre
   within its radius), the rock field's song.
6. **Calm:** otherwise, the cruise songs.

On top of these:

- **Stings:** an enemy that was within 12,000 km last frame and is gone
  now was splashed, and gets a victory sting, the three in turn. A friend
  gone the same way gets the tragedy.
- **Remixes** (`music_then`, the engine's follow-ons): as each calm song
  comes in, the next of the cruise cycle is asked for after its pass,
  from its loop, so the riser of one lands on the downbeat of the next,
  spliced on the bar line with no fade. As each battle comes in, another
  is asked for at one of its own blocks, a block or two past what the
  ring holds: so 20-35 s (three to five blocks) of each before the next
  cuts in. The place in each list is kept from flight to flight. A panic
  comes in on the bar.
- **The splice** (`music.c`): music_poll keeps a ring some 20 s ahead of
  the interrupt. When its fill reaches a follow-on's point (the end of the
  pass, or a block's marker, the bar line reached), it puts every
  channel's notes off, a switch event, the new song's channels as they
  stand at its first event (the chase), then its events. The interrupt
  takes the switch on the bar line: the new song, its bar clock and its
  event count from there. `music_then_cue` says a follow-on is still to
  be heard until then.
- **Whole bars** (`load_song`): a rest after a song's last event runs to
  the end of its last bar, found in the file's ticks and timed by its
  tempo. It used to be counted on the music clock, a beat rounded to whole
  ticks: where the beat rounds down (116, 124, 108, 156: 156 is 92.3 ticks
  a beat, counted 92) the bars came short and the rest grew nearly a bar
  too long, some 2 s of silence at the loop and before a follow-on; where
  it rounds up, a hitch of 0.2 s. The bar grid that cues wait for is kept
  in 1/256 ticks now, so a cue on the bar lands on it however long the
  song has played.
- **Cross-fades:** calm, home and the rocks fade over 1.5-2.5 s on the
  bar; a fight starts over 0.7 s on the beat.

**A sting** (`music_sting`, cues from `RV_MUSIC_STINGS` on, played once):

1. The engine notes where the song it cuts short was: the interrupt keeps
   the song's event index as it takes events, chase events aside.
2. It plays the sting.
3. When the sting ends, it goes on with the song from there. First it plays
   the channels' programs, volumes, pans and expressions as they stood
   there (the chase: the song read from its start to that event).
4. The song fades back in over 300 ms.

A sting over a sting goes back to the first one's song. A cue that comes
during a sting waits for it to end.

**Test:** `RVT.EXE /scoretest` (the test build) drives the moods by the clock instead of
the flight. The schedule, in seconds:

| time | what |
|---|---|
| 0 | calm |
| 8 | a rock field |
| 14 | a fight |
| 22 | the next battle chained on |
| 30 | a victory sting |
| 38 | the tragedy |
| 52, 60, 68 | the three panics |
| 78 | home, won |
| 88 | home, lost |
| 98 | dead |

`tests/score.txt` flies it (`tests/run.sh tests/score.txt /scoretest
/flight 105 /midilog`). `/scoretest 2` is the remixes': calm for 96 s
(Outbound's pass, then the next cruise piece spliced on), a fight for
100 s (the battles cut together block by block), the way home
(`tests/remix.txt`, `/flight 220`). `/scoretest 3` is the cycle's end:
calm only, from Waypoint, Starwake (in 3/4) spliced on after it, then
Outbound (`tests/cycle.txt`, `/flight 160`). The music log's lines carry the music clock
(`@ticks`, 240 a second), so a log rendered with `oplrender --log` can be
cut and labelled.

**Memory:**

- **Loading:** the 28 songs load at start-up into XMS, some 95,000 events
  (570 KB). The far heap grows to read each song's tracks, and
  `_fheapshrink` gives that back to DOS afterwards: 283 KB is free after
  the music (the walk test, 2026-10-24), where it was 180 KB with the seven
  room songs.
- **`/midilog`:** its buffers used to be taken from the far heap before
  anything else, and then the fonts found no room. That was already so
  before the flight's music. Upper memory is the ships' two dense
  buffers', to the byte. So the log now begins at the first `music_poll`,
  with 1,024 entries written out every frame. It writes the chip's set-up
  into the log first, and `opl_uncache` has every voice's patch written
  again at its next note, so `oplrender --log` plays it back right.

**Still to do:** combat (hits, kills, the hull's damage), the carrier in
space to target, mission objectives. The score already listens for all
of them.
