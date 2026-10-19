# The people: comm portraits that talk and blink

| piece | where |
|---|---|
| the cast (names, callsigns, birthplaces, backgrounds, looks) | [../data/people.csv](../data/people.csv) |
| the artwork | made by image synthesis from [portrait-prompts.md](portrait-prompts.md), run on the user's M1 Mac or in ChatGPT, dropped into `art/people/` |
| the talking and blinking | this repository: `tools/lipsync.py` turns each line's letters into mouth shapes (no speech needed), and blinks come on a timer |

## The cast

| key | name | callsign | born | role |
|---|---|---|---|---|
| RAY | Augustin "Ray" Reyes | Ironside | Mars (Sol) | the barman, a Redeemer pilot of the Luddite war grounded by a launch accident |
| COMET | Adaeze Nwosu | Comet | Ndele (Andolian) | pilot, ex-Homeland Security pirate hunter |
| FROST | Lin Mei | Frost | an Uln-border station | pilot; speaks some Rlaan, a medical-corps dropout |
| ECHO | Kasia Wren | Echo | Naxos (Purist) | pilot; her colony was lost to the Aera |
| TORCH | Rafael Ortega | Torch | Shmrn border space | pilot; got out of the Shmrn pocket and wants back in |
| KESTREL | Kenji Arai | Kestrel | Odin (Shaper) | pilot, Shaper-raised, uncanny reflexes |
| DRIFTER | Owen Price | Drifter | Alliance (Merchant Guild) | pilot, freighter kid, bets on everything |
| HERO_M / HERO_F | the player | Wildcard | Earth (Sol) | fresh from the academy |
| XO | Cmdr. Mira Solberg | | Brahma (Andolian) | the XO; survived the first Aera raids, hard but fair |
| TECH | Chief Ana Kowalski | Redline | Venture (LIHW) | the flight-deck chief, an ex-racer |

Everyone wears a short military regulation haircut; facial hair is a
moustache, a goatee or stubble at most. The portraits have no headsets
(they are the lounge and briefing faces); a headset set for in-flight comms
is optional and lives in `art/people/KEY/comm/`.

## Licence

The portraits must stay open. Generated with ChatGPT or with an openly
licensed local model (portrait-prompts.md names which), they are ours to
release, and they are released under
[CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/).

## Frames

From each portrait the tool keeps:

- the portrait itself, the face at rest (mouth closed, eyes open);
- a mouth box in each of the nine shapes below;
- an eye box open, half closed and closed.

The two boxes never overlap. The engine copies the portrait, then the
mouth frame for this step, then the eye frame, so only those two
rectangles change while someone talks.

| frame | shape | letters |
|---|---|---|
| 0 | rest (closed) | spaces, punctuation |
| 1 | lips pressed | m b p |
| 2 | teeth on the lip | f v ph |
| 3 | tongue behind the teeth | th t d n l |
| 4 | teeth together | s z c j x sh ch |
| 5 | wide open | a |
| 6 | open, wide | e i y k g h ee |
| 7 | round | o ou |
| 8 | small round | u w q r oo qu wh |

## Lip sync and blinks

`lipsync.py` turns a line into mouth shapes:

1. Digraphs are matched first, then single letters.
2. Durations:

   | item | steps |
   |---|---|
   | a vowel | 2 |
   | a consonant | 1 |
   | a space | 1 rest |
   | a comma | 3 rest |
   | a full stop, `?` or `!` | 5 rest |

3. A step is 100 ms (two frames at 20 fps), and repeats merge into one hold.

The engine needs a 128-byte table from ASCII to shape, plus the digraph
pass.

A blink comes every 2.5 to 5 seconds: half closed, closed, closed, half
closed, one frame each.

## tools/faces.py

`tools/faces.py` paints placeholder faces from the same table: a
height-field face lit in software. They are not the art; the user found
them too much like a game console's avatars. They remain useful as stand-ins
until the portraits arrive, and as a layout guide for a local model's
img2img or ControlNet, since their eyes and mouth sit where the frame boxes
expect.

## Still to do

- **An alignment tool:** align the generated portraits and edits, cut the boxes, and build the frames and preview GIFs.
- **A face palette:** in flight a comm face shares the flight palette (spec §9). In the bar and briefing rooms the scene palette is free.
- **Small sizes:** 128 and 64 px versions for the cockpit's comm VDU.
