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

## The songs (tools/flightsongs.py, CC0)

| cue | title | key, tempo | what |
|---|---|---|---|
| CRUISE1 | Far Shore | B minor, 96 | glass bell eighths, wide pad, sub bass, the soft lead; the vibraphone's bridge |
| CRUISE2 | Cartographer | D dorian, 84 | harp in 3+3+2, strings, the clarinet, celesta answers |
| CRUISE3 | Long Watch | E minor (lydian IV), 100 | electric piano eighths, ride, vibraphone, flute |
| CRUISE4 | Quiet Instruments | C minor drone, 92 | bell ostinato, ticking hats, slow lead, celesta flurries |
| CRUISE5 | Beacon | F# minor into A, 76 | choir, harp, horn, celesta pings, far timpani |
| CRUISE6 | Nav Point | G dorian, 108 | saw pluck sixteenths, pizzicato, a light groove, the oboe |
| RETURN1 | Homeward | E flat, 66 | pad, harp rolled up, the horn, strings; no drums |
| RETURN2 | Running Lights | B flat, 72 | electric piano, flute, a shaker |
| BATTLE1 | Contact | C minor sus pedal, 132 | synth bass eighths, pluck sixteenths, brass hits, the horn's call, timpani |
| BATTLE2 | Break and Attack | D minor, 144 | four on the floor, saw arpeggios, synth lead, brass |
| BATTLE3 | Furball | E phrygian, 152 | overdriven fifths, toms, the lead's riff |
| BATTLE4 | Crossfire | G minor, 120 | orchestral: pizzicato ostinato, horn, trumpet, snare, timpani |
| PANIC1 | Hull Breach | 96 | a low cluster, a heartbeat, ticks |
| PANIC2 | Systems Failing | 116 | a bass climbing by half steps, brass alarms in seconds |
| PANIC3 | Abandon Ship | 152 | sixteenths, tritones, a crash every bar |
| VICTORY1..3 | | C, D mixolydian, B flat | brass fanfares, 3.6-4 s |
| TRAGEDY | | A minor, 60 | the horn falling E D C, B A over Am F Dm E7b9 Am, 12 s |

There are 31 minutes in all: cruise 15.8, home 4.6, battle 7.0, panic 3.8.

- **How they are written:** each song is a harmony of bars and layers over
  it (`Piece`: pad, arpeggio, bass, drums). The A sections' tunes are
  written out note by note; the later ones are written by
  `tools/compose.py` from the chords, seeded.
- **Loops:** each is a loop, its last bar leading back into its first.
  Everyone else's level is evened out by `level=` (dB, half on the
  volumes, half on the velocities): the cruise songs sit at about -27 dBFS
  RMS, the battles at -24.
- **Voices:** none steals a voice: at most 18 sound at once.
- **New patches in the bank** (on programs no room song uses): saw pluck
  (81) and short pluck (84) for the arpeggios, kept short so sixteenths
  stay within the 18 voices; sub bass (39); glass bell (98); the soft lead
  on 85; noise swell (119).

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
   calling the carrier to land (C).
5. **Calm:** otherwise, the cruise songs.

On top of these:

- **Stings:** an enemy that was within 12,000 km last frame and is gone
  now was splashed, and gets a victory sting, the three in turn. A friend
  gone the same way gets the tragedy.
- **Rotation:** each mood's song plays through once (`music_loops`), and
  then the next in its list takes over on the bar. The place in each list
  is kept from flight to flight.
- **Cross-fades:** calm and home fade over 2.5-3 s on the bar, a fight
  and a panic over 0.5-0.7 s on the beat.

**A sting** (`music_sting`, cues from `RV_MUSIC_STINGS` on, played once):

1. The engine notes where the song it cuts short was: its next event, from
   the ring's place.
2. It plays the sting.
3. When the sting ends, it goes on with the song from there. First it plays
   the channels' programs, volumes, pans and expressions as they stood
   there (the chase: the song read from its start to that event).
4. The song fades back in over 300 ms.

A sting over a sting goes back to the first one's song. A cue that comes
during a sting waits for it to end.

**Test:** `RV.EXE /scoretest` drives the moods by the clock instead of
the flight: calm, a fight at 8 s, a victory sting at 16, the tragedy at
24, the three panics at 34, 42 and 50, home at 58, dead at 70.
`tests/score.txt` flies it (`tests/run.sh tests/score.txt /scoretest
/flight 85 /midilog`).

**Memory:**

- **Loading:** the 26 songs load at start-up into XMS, 70,000 events.
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
