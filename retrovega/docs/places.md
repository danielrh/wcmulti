# Places: the carrier's rooms, built for the game

`tools/places.py` turns a room's painted layers into the game's 640x400
layers, puts the player's fighter in the room where it has one, and writes
a preview GIF of the room's script. data/places.json describes each room.
The painted sources (git-ignored) sit in `art/places/src/KEY/`; the built
layers (committed) go to `art/places/KEY/`.

    ~/.venvs/retrovega-art/bin/python -P tools/places.py [KEY ...] [--ship REDEEMR] [--still docs/deck.png]

The preview goes to `build/places/KEY.gif`. It uses one 256-colour palette
for the whole GIF, with no dithering, as the game will. `--rerender`
renders the fighters again; otherwise they are cached in
`build/places/ships/`.

## The launch deck

![every player fighter on the pad](deck.png)

The deck is a side view of a hangar. Kit stands at her cart in front,
and the fighter sits on the pad behind her, nose right. Kit and her rig
come from an animation pack made for the scene (the user's image
synthesis; CC0): `background.png`, `foreground-base.png` (Kit and the
cart, full canvas, transparent elsewhere) and three clips. Each clip is an
atlas of 32 frames of one rectangle, the patch over her head and
shoulders, listed in `animations.json`:

| clip | what | frames |
|---|---|---|
| idle | breathing and blinking | 32 at 100 ms |
| console-work | gaze down, a small head dip, taps on the console | 32 at 80 ms |
| ready-nod | a double nod towards the fighter | 32 at 80 ms |

The source canvas is 1536x1024. It is cropped to 1536x960 (32 pixels off
the top and the bottom) and scaled to 640x400.

**Draw order:** the room, then the fighter's under layer (its shadow and
its reflection in the polished deck), then its sleds, its exhaust, the
fighter itself, and last the front layer with the current frame's patch
copied in. The copy includes transparent pixels: the patch replaces that
rectangle of the front layer. The tool checks that no frame changes the
front outside the patch.

**The fighters** are rendered for the deck by `tools/shipviews.py`
(`--views E,H`), and the tool picks them up on its own:

- **Camera:** 10 degrees above, heading 270 (nose right).
- **Light:** a key from overhead, then warmed to the deck's lamps (`light`).
  Matching the room's light is what makes them sit in it.
- **Which ships:** every ship in data/ships.csv whose status starts with
  `player`. A new fighter needs only its row there and a re-run.
- **Size:** they share one scale, Vega Strike's own sizes: the mesh times
  the unit's `Unit_Scale` from units.csv, at `metre` source pixels a metre.

| ship | length | on the deck |
|---|---|---|
| Redeemer | 18 m | whole, 401 px long |
| Gawain | 26 m | whole, its tail behind Kit |
| Lancelot | 28 m | its top off the screen |
| Schroedinger | 30 m | its fins off the top |
| Pacifier | 38 m | the nose and the belly |
| Franklin | 41 m | the front two thirds, canopy dome and all |

**Placement:** the nose is at `nose_x`. The rest runs behind Kit and off
the left. A heavy fighter shows only its front and goes off the top.

**The landing sleds:** every ship's lowest point rides 12 px above the
deck, on standard landing sleds.

- **Shape:** a sled is a foot on the deck, a strut repeated up to the
  height it needs, and a saddle under the hull (`sled-foot.png`,
  `sled-strut.png`, `sled-saddle.png`). The engine stacks them.
- **Where they go:** one stands under the front of the longest stretch of
  belly that can be seen, and one under its rear when that stretch is over
  300 px. Each reaches from the deck to the lowest point of the hull above
  its saddle.
- **Placeholder:** the parts are drawn shapes until a painted sled exists.
  Put one at `art/places/src/deck/sled.png` (prompt below) and re-run; the
  tool slices it itself. The strut is the longest run of rows at its
  narrowest width.

**The launch:** after the ready nod, the fighter launches and the sleds
stay on the deck.

1. The engines spool up (600 ms).
2. It rises 4 px (400 ms).
3. It accelerates out to the right (900 ms), with the exhaust growing
   from the nozzles: the hull's last pixel at each engine's height.
   Vega Strike puts its engine lights inside the hull, so their own x is
   no use.

## place.json

Everything the engine needs, in screen pixels:

- **back** and **front**.
- **actor:** the patch rectangle, and each clip's atlas (8 frames to a
  row) and frame times.
- **fighter:** the deck's line, the sled parts, and per ship:
  - **sprite:** the picture and its corner;
  - **under:** the under layer (RGBA: colour, and how much of it to blend
    over the room) and its corner;
  - **engines:** the nozzles, relative to the sprite;
  - **sleds:** each sled's middle and top.
- **launch** timings.
- **script:** the order the clips and the launch play in.

## Painted sled prompt

Send with the style block of [location-prompts.md](location-prompts.md):

```
A single piece of carrier deck equipment, isolated on a plain white background, seen
exactly side on at eye level: a heavy landing sled that holds a parked space fighter off
the deck. A low, wide steel foot with a yellow-and-black hazard band and two small amber
lights, standing flat; from its middle one thick polished hydraulic ram rising straight
up, the same width along its whole length; on top a padded saddle cradle, olive rubber in
a steel frame, slightly concave to take a hull. Worn gunmetal paint, ink linework, warm
amber hangar light from above. No fighter, no people, no shadow, no text.
```

## Still to do

- **Palette:** one for each room, shared with the faces and the fighter's
  colours.
- **The other rooms:** bar, bunks, locker, briefing and debrief, from
  [location-prompts.md](location-prompts.md).
