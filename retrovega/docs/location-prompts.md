# Location prompts: the carrier's rooms, by image synthesis

These prompts produce the backdrops the game happens in. They are drawn
the same way as the portraits (docs/portrait-prompts.md), so the people and
the rooms match. Licences are the same as for the portraits: CC0.

**How the people get into the rooms.** The rooms are painted empty. The
game puts the cast's portraits (head-and-shoulders busts) behind something
that hides their lower half: the bar counter, the far edge of a table, a
lectern. These props are cut out of the same painting as a separate layer
(the tool does that). The busts face us, so each prompt keeps the chairs
facing us and the spots behind the props clear.

**Crowds** (the rows in the briefing room, crew far off on the deck) are
painted into the room itself as dark, sketchy figures, and need no art of
their own.

**Screens and boards are blank.** The game draws the mission map, the high
scores and the splash count on them.

## 1. The style block (every location prompt)

```
Hand-drawn digital painting in the style of a 1990s PC space-combat game's background art:
confident ink linework, painterly shading with soft gradients and a little visible
brushwork, believable proportions with slight stylisation (not a cartoon, not a
photograph, not a 3D render). Muted military palette: gunmetal greys, olive drab, worn
paint, warm practical lights. The interior of a large Terran Confederation space carrier,
built for function: riveted bulkheads, conduits, ribbed ceilings, hatches with
hazard-striped frames. A wide view at eye level with a slightly wide-angle lens, everything
in focus. No text, letters, numbers, logos or signs anywhere: every board and screen is
blank. No frame or border. Landscape, 1536x1024. Keep everything important away from the
top and bottom 5% of the picture.
```

**Negative prompt** (local models only):

```
text, letters, numbers, logo, signage, watermark, signature, frame, border, photo,
photorealistic, 3d render, cgi, fisheye, tilted horizon, blurry, cartoon, anime
```

The game's screen is 640x400 (16:10). The 1536x1024 picture is cropped to
1536x960 (a strip off the top and bottom) and scaled down.

## 2. The rooms

**BAR: the pilots' lounge**
```
The pilots' lounge and bar, warm and lived-in. On the left, a long bar counter runs away
from us at a gentle angle, with stools in front of it and shelves of bottles and glasses
behind; the space behind the counter is clear (the bartender will stand there). In the
middle distance, a round table under a hanging lamp, with three empty chairs on its far
side facing us and nothing on the table but a few glasses. On the right, a flight
simulator: an enclosed cockpit capsule on a hydraulic base, its hatch open, a small
boarding step, its screens dark. On the back wall, a large blank chalkboard in a worn
wooden frame, nothing written on it, and a wide viewport beside it showing stars. Two
off-duty crew far in the back corner, small and in shadow. Warm amber lamps, cool starlight
from the viewport. No one at the counter or the table.
```

**BUNKS: crew quarters (where the game is saved)**
```
A pilot's crew quarters: a narrow cabin with two-high bunks along the left wall, neatly
made with grey blankets; a small fold-down desk with a lamp in the middle; on the right a
tall steel personal locker, its door closed, a blank name plate on it; a round porthole
with stars. Cool blue night lighting with the warm pool of the desk lamp. Quiet and
tidy. No people.
```

**LOCKER: the open locker, close up**
```
Straight on, close up: an open steel personal locker filling the picture. A flight jacket
on a hanger, a flight helmet on the top shelf, a duffel bag on the floor of the locker. On
the inside of the door, a few photographs taped up, painted softly and slightly blurred so
no faces or details can be made out. On a shelf, an open medal case lined with dark blue
velvet with empty slots (the medals will be added), and a bundle of letters tied with
string. Warm light from the room falling into the locker. No people.
```

**BRIEFING: the briefing room, wide**
```
A large, dark briefing auditorium. We look from the back rows towards a raised stage. Rows
of seated pilots fill the foreground and the middle, seen from behind: only the backs of
heads and shoulders in flight suits, as dark silhouettes with a faint cool rim of light
from the screen, drawn loosely like quick sketches. On the stage, on the left, an empty
lectern (an officer will be added behind it). Behind the stage, a huge blank wall screen
glowing a faint blue-grey; it is the main light in the room. Dark, quiet, expectant.
```

**DECK: the launch deck.** Built differently in the end: a side-on hangar
with Kit at her cart in front and the fighter on the pad behind her, from
an animation pack (docs/places.md). The prompt below is the first idea.
```
The carrier's launch deck, from just behind and above a fighter's launch position. A long,
dim launch bay stretches away from us to an open space door full of stars, lit by rows of
guide lights converging on the exit. Hazard-striped deck markings, catapult rails, a few
deck crew small and far off, in silhouette, one waving light wands. The launch position in
the foreground, on the rails, is empty (the fighter will be added). Cold blue-white
floodlights, amber warning lights.
```

**DEBRIEF: the debriefing room**
```
A small debriefing room, harshly lit by overhead fluorescent panels, cooler than the bar. A
long grey metal table seen from its near end, running away from us. Along its far side,
empty chairs facing us; at the head of the table, at the back, a clear space in front of a
blank wall screen (the commanding officer will stand there). Clipboards, data pads and
coffee mugs on the table. Bulkheads with a map rack and a hatch. No people.
```

## 3. The canopy closing (the launch)

Optional since the side-on deck (docs/places.md) shows the launch itself.

Three frames: one picture and two edits of it, in one conversation. The
pilot wears a full helmet with the visor down, so it is everyone (the player
is never seen here).

**CANOPY_1** (send with the style block):
```
Close up, from just in front of a fighter's nose, looking at its cockpit: a pilot in a
full flight helmet with a dark tinted visor pulled down (no face visible) and an oxygen
mask, strapped into an ejection seat, gloved hands on the controls. The bubble canopy is
fully raised. Cockpit instruments glow green and amber. The launch bay's floodlights behind.
```

**CANOPY_2:**
```
Edit this exact picture. Change ONLY the canopy: it is halfway down, swinging closed.
Keep everything else identical: the pilot, the cockpit, the lighting, the framing.
```

**CANOPY_3:**
```
Edit the original picture. Change ONLY the canopy: it is fully closed and sealed, with the
floodlights reflecting on the glass. Keep everything else identical: the pilot, the
cockpit, the lighting, the framing.
```

The ship itself is ours: the deck shot uses the fighter's own rendered
sprite (tools/shipviews.py), so every fighter launches as itself.

## 4. Where the results go

Drop each room's file into `retrovega/art/places/src/` named by its key:
`bar.png`, `bunks.png`, `locker.png`, `briefing.png`, `deck.png`,
`debrief.png`, `canopy_1.png` .. `canopy_3.png`. If a room comes out with a
person in it who shouldn't be there, re-roll it rather than edit: the
props get cut along their painted edges.
