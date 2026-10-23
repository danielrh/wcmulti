# The people: comm portraits that talk and blink

| piece | where |
|---|---|
| the cast (names, callsigns, birthplaces, backgrounds, looks) | [../data/people.csv](../data/people.csv) |
| the artwork | made by image synthesis from [portrait-prompts.md](portrait-prompts.md), run on the user's M1 Mac or in ChatGPT, dropped into `art/people/` |
| the talking and blinking | this repository: `tools/lipsync.py` turns each line's letters into mouth shapes (no speech needed), and blinks come on a timer |

## The cast

| key | name | callsign | born | role |
|---|---|---|---|---|
| TALLY | Wendell Greer | Tally | Mobile (Earth) | the barman, a Redeemer test pilot grounded by a launch accident; still holds the simulator record |
| COMET | Adaeze Nwosu | Comet | Ndele (Andolian) | pilot, ex-Homeland Security pirate hunter |
| FROST | Lin Mei | Frost | an Uln-border station | pilot; speaks some Rlaan, a medical-corps dropout |
| ECHO | Kasia Wren | Echo | Naxos (Purist) | pilot; her colony was lost to the Aera |
| TORCH | Rafael Ortega | Torch | Shmrn border space | pilot; got out of the Shmrn pocket and wants back in |
| KESTREL | Kenji Arai | Kestrel | Odin (Shaper) | pilot, Shaper-raised, uncanny reflexes |
| DRIFTER | Owen Price | Drifter | Alliance (Merchant Guild) | pilot, freighter kid, bets on everything (including which twin is on the comm) |
| PRIME | Julian Mercer | Prime | Alliance (Merchant Guild) | pilot, the original twin, a showman |
| ANCHOR | Julius Mercer | Anchor | Alliance (Merchant Guild) | pilot, Prime's clone, the careful one |
| HERO_M / HERO_F | the player (by default Cal Brody) | Wildcard | Earth (Sol) | fresh from the academy |
| XO | Cmdr. Mira Solberg | | Brahma (Andolian) | the XO; survived the first Aera raids, hard but fair |
| TECH | Chief Ana Kowalski | Kit | Venture (LIHW) | the flight-deck chief, an ex-racer who kitbashes ships from salvaged parts |

Everyone wears a short military regulation haircut; facial hair is a
moustache, a goatee or stubble at most. The portraits have no headsets
(they are the lounge and briefing faces); a headset set for in-flight comms
is optional and lives in `art/people/KEY/comm/`.

## The Mercer twins

Julian and Julius Mercer pass for identical twins. Their Merchant Guild
parents insured every cargo twice, so after Julian they had a spare son
cloned, and Julius was born eleven months later. Both joined up, and both
were assigned to the same carrier in the same war, so the redundancy came
to nothing.

| | Prime (Julian) | Anchor (Julius) |
|---|---|---|
| look | the same face; he wears it like a recruiting poster | the same face, a shade more tired |
| manner | flies like a man with a spare, loves an audience | flies by the checklist, owns his mistakes, gets blamed for Julian's |
| sore point | being told he is the copy | being "the spare"; the squadron calls him Carbon to needle him |

They are good for mix-ups: swapped seats in the simulator (Tally suspects
one of them set his record for the other), a reprimand delivered to the
wrong brother, Drifter's betting pool on which one is on the comm.

Their portraits began as the player's: the twin was drawn from HERO_M's
first portrait. The tool turns their slate-blue hero suits olive.

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

## tools/portraits.py

`tools/portraits.py` turns the generated art into the frames above. The
sources sit in `art/people/src/` (one folder per chat export, kept out of
git) and `art/people/manifest.csv` says which file is which frame.

1. **Fit.** Every frame is its own generation: some are framed differently
   (the original portrait is often zoomed), and all are redrawn a little.
   Each frame is scaled and shifted onto the base by matching the edges of
   the whole bust, coarse to fine.
2. **Boxes.** The eye box comes from where the blink frames differ from the
   base (the two eye centres also set the framing). The mouth box is only
   the lips and what moves with them. It is centred on the row between the
   nose and the chin where the mouth frames change most, 1.24 eye spans
   wide and 0.76 tall. Every frame also redraws the chin, neck and collar a
   little. While the box took all of that in, the XO's neck swelled and
   shrank as she talked (the user, 2026-10-23), and the others' less so.
3. **Blend.** Each box frame is the base with the edit blended in towards
   the middle, tone-matched to the base, so its border is the base's own
   pixels and no seam shows when the engine pastes it. The mouth blends in
   through the ellipse inside its box.
4. **Frame.** Everyone is scaled and placed the same way: the eyes 16% of
   the width apart, at 44% of the height.
5. **Fixes.** `olive` recolours a slate-blue hero suit and its gold patch
   to the pilots' olive drab and orange (the Mercer twins began as hero
   portraits).

For each character `art/people/faces/KEY/` (in git, about 200 KB each)
holds `base.png` (RGBA), `mouth_0..8.png`, `eyes_0..2.png`, `comm.png` (the
headset look, where there is one) and `face.json` (the boxes, the fits, the
sources); `build/people/KEY.gif` is the character saying their line.
`--gallery docs/people.png` draws the cast sheet and `build/people/cast.gif`,
everyone talking.

`tools/faces.py`, the procedural painter from before the art arrived, is
kept as a placeholder generator; its boxes follow the same rules.

## The aliens on the comm

The user's brief (2026-10-22): Rlaan and Aera pilots, heavy gear, mostly
anonymous to the player, with distinct helmets. Nobody on our side has
seen one's face, and they give no names: Terran pilots call each after
their helmet. They are comm voices only (`data/aliens.csv`; their helmets
are `tools/aliens3d.py`'s, docs/engine.md "The comm link"); a hostile
fighter's pilot hails the player once a flight.

| key | side | flies | the helmet | says |
|---|---|---|---|---|
| HOPLITE | Aera | Nicander, Ariston, Agis | a Corinthian helm, a low crest, the cheek guards closing to a T; the T's bar glows for eyes, its stem lights as they speak; a chevron on the brow | "Terran pilot. Your carrier burns tonight, and you will watch it." |
| EPHOR | Aera | Convolution (the aces) | tall, a swept blade at each temple, a dark shield face with a V of lit slits, three rank bars | "I have counted your kills, Terran. Now count mine." |
| PHALANX | Aera | Areus, Cleomenes (heavy) | a blend of the two above (the user's pick over a boxy one): the Hoplite's helm, broader, its crest raised; short swept fins; a wide dark shield of a face with a shallow V of light and a lit grille down its middle; two chevrons | "Hold your formation, Terran. It makes you easier to break." |
| LANTERN | Rlaan | Shizu, Gaozong (scouts, probes) | a smooth pear-shaped shell fluted at the crown, a pale band, seven lenses in a hexagon, pods at the jaw; speech lights down the throat | "This lane is under Rlaan contract. Keep your guns cold." |
| ABBOT | Rlaan | Shizong, Xuanzong (disablers, jammers) | a tall ringed crown like an onion dome, a hood flaring wide, one great lens and two small; speech lights down the hood's edges | "We watch. We record. Your war is noted, Terran." |
| BELLOWS | Rlaan | Zhuangzong, Taizong (line, bombers) | a wide flat spotted carapace, a beak of three lapping plates, a long lens either side, four ribbed breathing tubes | "Your war is bad for trade, little pilot. Go home." |

## Still to do

- **HERO_M's mouth and blink frames** (Cal Brody has only his portrait; he doesn't move yet).
- **A face palette:** in flight a comm face shares the flight palette (spec §9). In the bar and briefing rooms the scene palette is free.
- **Small sizes:** 128 and 64 px versions for the cockpit's comm VDU (`--size`).
- **The headset set:** only the portraits so far (`comm.png`), no talking frames.
- **A capital ship's officer** on the comm (the user's brief, 2026-10-22): not made yet.
