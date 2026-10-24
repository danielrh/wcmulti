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

**Step 1 (this commit):**

- **The orchestra** on programs no song used: spiccato strings (44) for
  the eighth-note motor, an orchestra hit (55), a piccolo (72), a synth
  brass that bites (62, 63). The crash is 6 dB down, the user's note: it
  stood 4 dB over the mix (A-weighted).
- **The balance** of the parts is the reference render's (the user liked
  it), measured K-weighted against the motor: the brass 8 dB over it, the
  horns 5, the piccolo 4, the hits 7, the crash 5. `ORCH` in
  flightsongs.py sets each part's channel, program and level.
- **Outbound** (CRUISE1) is the proof piece. **HOMEWARD** is rewritten in
  the style. **HOMELOSS** is new: the user asked for a way home for a
  mission lost, "mostly just a bit slower and sadder but otherwise same".
- **The guardrail:** `tools/songcheck.py` measures the cruise and home
  songs against bands from the two cues (notes a second, motor, voices,
  off-key share, fast notes, drum hits, bars without a change) and names
  every band a song falls outside. Outbound and the way home pass; the
  second draft's five other cruise songs do not, and are next.

## The songs (tools/flightsongs.py, CC0)

The second draft, with the third's first step:

| cue | title | key, tempo | what |
|---|---|---|---|
| CRUISE1 | Outbound | E minor, G# minor, C, E; 160 | third draft: the motor in octaves, a synth-brass call and horn answers, hits on 3+3+2, a break into C, the horn's tune, home to E major, a timpani riser; the second pass rescored |
| CRUISE2 | Deep Field | E lydian, 72 | new: strings, horn's long tune, flute, the minor turn, build, violins high with choir |
| CRUISE3 | Long Watch | E minor, 100 | the draft's 0:58 flute passage, piano eighths, oboe, a lift into G, the passage again full |
| CRUISE4 | Quiet Instruments | C minor drone, 92 | ticking that quickens into a payoff (horns, violins, choir), flurries, an oboe passage |
| CRUISE5 | Beacon | F# minor into A, 76 | a string pulse, horn's tune, violins' passage, climax, lift into A |
| CRUISE6 | Nav Point | G dorian, 108 | as the draft, looping past its intro |
| HOMEWARD | Homeward, won | B flat, 12/8 at 108 | third draft: harp eighths, the horn's tune, a lift into D for a brass chorale, home through B flat |
| HOMELOSS | Homeward, lost | B flat minor, 12/8 at 92 | the same, slower and sadder: horns for the brass, no hits, sinking a major third to G flat |
| BATTLE1 | Contact | C minor sus pedal, 140 | as the draft (132), looping past its intro |
| BATTLE2 | Break and Attack | C minor, 140 | half-time fight, riff, brass calls, a Neapolitan horn tune, a lift into A flat |
| BATTLE3 | Furball | C phrygian, 140 | the draft's (E, 152) with a surprise: the riff up a third, then a tutti stop |
| ROCKS | Rock Field | G minor, 120 | asteroid fields: the draft's Crossfire, a new drift in B flat, a descant |
| PANIC1 | Hull Breach | C, 140 | Contact's drive under a cluster, alarms, the call a tritone off |
| PANIC2 | Systems Failing | C rising, 140 | each build pays off in two wild bars |
| PANIC3 | Abandon Ship | 152 to 196 | panic, hope, dashed, hope, dashed harder, a speed-up |
| VICTORY1..3 | Splash One..Three | C, D, B flat | bittersweet: bVI-bVII-I, the minor plagal, a deceptive turn |
| TRAGEDY | Wingman Lost | B flat minor, 52 | a funeral march, 14 s |

The pieces run 31 minutes once through, each going round from its loop.
`Piece.loop_at(bar)` writes the marker. `Song.tempo(beat, bpm)` writes a
tempo change. Abandon Ship's speed-up is timed so that its eight bars take
seven, so the song stays whole bars at its first tempo.

- **How they are written:** each song is a harmony of bars and layers over
  it (`Piece`: pad, arpeggio, bass, drums). The A sections' tunes are
  written out note by note; the later ones are written by
  `tools/compose.py` from the chords, seeded.
- **Loops:** a "loop" marker meta event (`FF 06 "loop"`) after a song's
  intro is where it goes round again from. The engine plays the intro once
  (`music.c`: the marker becomes a do-nothing event, `Song.loop` its index;
  the ring wraps there and that event's delta is zeroed, since the last
  bar's rest has brought it to the bar line). `oplrender` follows it too.
  `music_cue_body` starts a song at its marker, chased, for the battles'
  chain.
- **Levels:** evened out by `level=` (dB, half on the volumes, half on the
  velocities): the cruise songs sit at -25 to -27 dBFS RMS, battle and
  panic at -23 to -25, the stings at -20.
- **Voices:** none steals a voice: at most 18 sound at once.
- **New patches in the bank** (on programs no room song uses): saw pluck
  (81) and short pluck (84) for the arpeggios, kept short so sixteenths
  stay within the 18 voices; sub bass (39); glass bell (98); the soft lead
  on 85; noise swell (119). The third draft's: spiccato (44), orchestra
  hit (55), piccolo (72), synth brass (62, 63).
- **Whole bars:** a loop's length is its last event's bar, so nothing may
  sound past the last bar line: a swell's last expression step and a
  sweep's last note end inside it.

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
- **Rotation:** a calm song plays through once (`music_loops`), and then
  the next in its list takes over on the bar.
- **Battle chain:** the battles are all at 140 in C. When one has gone
  round, the next goes on from its loop marker (`music_cue_body`) on the
  bar, with a 150 ms fade, as one piece. A panic comes in on the bar too.
  The place in each list is kept from flight to flight.
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
/flight 105 /midilog`). The music log's lines carry the music clock
(`@ticks`, 240 a second), so a log rendered with `oplrender --log` can be
cut and labelled.

**Memory:**

- **Loading:** the 26 songs load at start-up into XMS, some 75,000 events.
  The far heap grows to read each song's tracks, and `_fheapshrink` gives
  that back to DOS afterwards: 268 KB is free after the music, where it
  was 180 KB with the seven room songs.
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
