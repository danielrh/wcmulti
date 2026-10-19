# Portrait prompts: the cast, by image synthesis

These prompts produce the eleven comm portraits (data/people.csv) and their
talking and blinking frames. Run them on an M1 Mac (Draw Things, ComfyUI,
DiffusionBee) or in ChatGPT's image tools.

**Three steps:**

1. Generate the 11 portraits: section 2, each with the style block of section 1.
2. For each portrait, make 10 edits: 8 mouth shapes and 2 blink states (section 3). Each edit changes only its region.
3. Drop the results into `retrovega/art/people/` as section 5 describes. A tool there will align them, cut the mouth and eye boxes, and build the frames the engine uses.

**Licences.** The portraits must stay open (CC0 or CC-BY):

- **ChatGPT:** OpenAI's terms give you the outputs, so we can release them CC0.
- **Local models, fine:** FLUX.1-schnell (Apache-2.0), SDXL base 1.0 (CreativeML OpenRAIL++-M) and SD 1.5 (OpenRAIL-M); outputs from these are ours to release.
- **Local models, avoid:** FLUX.1-dev (non-commercial licence) and fine-tunes whose licence restricts outputs.

## 1. The style block (every portrait prompt)

Prepend this to each prompt in section 2. In ChatGPT, it is enough to send
it once at the start of the conversation and say "use this style for every
portrait".

```
Hand-drawn digital painting in the style of a 1990s PC space-combat game's comm-screen
character portrait: confident ink linework, painterly shading with soft gradients and a
little visible brushwork, realistic adult proportions with slight stylisation (not a
cartoon, not a photograph, not a 3D render). Muted military palette. One warm key light
from the upper left, a soft cool rim light from the right. Front-facing head-and-shoulders
bust, looking straight at the viewer, neutral relaxed expression, mouth gently closed,
eyes open. Centred: the head fills the middle 40% of the frame's width, the eyes sit at
48% of the image height, the chin at about 75%, the shoulders run off the bottom edge.
Plain flat dark slate background (#1C2830), or transparent if available. No text, logos,
frames or borders. Square, 1024x1024.
```

**Negative prompt** (local models only):

```
photo, photorealistic, 3d render, cgi, plastic skin, doll, chibi, anime eyes, caricature,
exaggerated features, oversized head, long hair, ponytail, afro, full beard, hat, helmet,
headset, headphones, microphone, sunglasses, hands, text, watermark, signature, frame,
border, busy background, tilted head, three-quarter view, open mouth, smile with teeth
```

Remove "open mouth" from the negative prompt for the mouth edits in section 3.

**Shared clothing phrases** (already written into each prompt below):

| who | wears |
|---|---|
| pilots | an olive-drab Terran Confederation flight suit with a high zipped collar, an orange squadron patch on the left shoulder and a pale name tape on the chest |
| the hero | the same flight suit in slate blue, with a gold squadron patch |

## 2. The portraits

All hair is short military regulation. Facial hair is small: a moustache, a
goatee or stubble at most. No one wears a headset or helmet: these are the
faces of the lounge, the bar and the briefing room. Pilots in a headset are
a second, optional set for in-flight comms (section 3, "Two looks").

**RAY: Augustin "Ray" Reyes, the barman (a retired pilot)**
```
A 61-year-old man, a retired fighter pilot who now runs the squadron bar. Olive skin,
weathered face with crow's feet and faint forehead lines, short salt-and-pepper hair in a
neat side-parted crop, a neat short grey moustache and no beard, brown eyes, square jaw,
long straight nose, calm and knowing look. Charcoal work shirt under a brown leather bar
apron; a small faded squadron pin on the apron strap.
```

**COMET: Adaeze Nwosu, pilot**
```
A 29-year-old woman fighter pilot. Deep brown skin, very short black crew cut, dark brown
eyes, rounded face, broad nose, small silver ear studs, steady, slightly sardonic look.
Olive-drab Terran Confederation flight suit with a high zipped collar, orange squadron
patch on the left shoulder, pale name tape on the chest.
```

**FROST: Lin Mei, pilot**
```
A 31-year-old woman fighter pilot of East Asian descent. Light skin, black hair pulled back
into a tight low regulation bun, dark eyes, fine features, small nose, calm and composed
look. Olive-drab Terran Confederation flight suit with a high zipped collar, orange
squadron patch on the left shoulder, pale name tape on the chest.
```

**ECHO: Kasia Wren, pilot**
```
A 26-year-old woman fighter pilot. Very fair skin with light freckles across the nose and
cheeks, short auburn hair in a side-parted crop, green eyes, narrow chin, small upturned
nose, determined look. Olive-drab Terran Confederation flight suit with a high zipped
collar, orange squadron patch on the left shoulder, pale name tape on the chest.
```

**TORCH: Rafael Ortega, pilot**
```
A 35-year-old man fighter pilot. Tan skin, short dark-brown hair in a side-parted crop,
brown eyes, square jaw, long nose, a small neat goatee with a thin moustache, intense,
driven look. Olive-drab Terran Confederation flight suit with a high zipped collar,
orange squadron patch on the left shoulder, pale name tape on the chest.
```

**KESTREL: Kenji Arai, pilot**
```
A 30-year-old man fighter pilot of East Asian descent. Light skin, black high-and-tight
haircut, dark eyes, narrow chin, a thin pale scar through his left eyebrow, calm and alert
look. Olive-drab Terran Confederation flight suit with a high zipped collar, orange
squadron patch on the left shoulder, pale name tape on the chest.
```

**DRIFTER: Owen Price, pilot**
```
A 33-year-old man fighter pilot. Fair skin, sandy-blond crew cut, blue-grey eyes, square
jaw, light stubble, an easy, faintly amused look. Olive-drab Terran Confederation flight
suit with a high zipped collar, orange squadron patch on the left shoulder, pale name tape
on the chest.
```

**HERO_M: "Wildcard", the player (male)**
```
A 27-year-old man fighter pilot, a fresh academy graduate. Brown skin, dark-brown
high-and-tight haircut, brown eyes, square jaw, clean-shaven, determined but open look.
Slate-blue Terran Confederation flight suit with a high zipped collar, gold squadron patch
on the left shoulder, pale name tape on the chest.
```

**HERO_F: "Wildcard", the player (female)**
```
A 27-year-old woman fighter pilot, a fresh academy graduate. Fair skin, short brown hair in
a side-parted crop, hazel-brown eyes, rounded face, small nose, determined but open look.
Slate-blue Terran Confederation flight suit with a high zipped collar, gold squadron patch
on the left shoulder, pale name tape on the chest.
```

**XO: Commander Mira Solberg**
```
A 54-year-old woman, the carrier's executive officer. Warm brown skin, silver-grey hair in
a tight low regulation bun, dark brown eyes with fine lines at the corners, long straight
nose, small gold ear studs, composed and authoritative, hard but fair. Dark navy dress
uniform with a high collar edged in gold, three small gold rank pips on the collar, gold
shoulder boards.
```

**TECH: Chief Ana Kowalski, the flight-deck chief**
```
A 38-year-old woman, the flight deck's chief engineer and a former racer. Light tan skin,
dark-brown buzz cut, green-hazel eyes, rounded face, small nose, a smear of engine grease
on her right cheek, scratched welding goggles pushed up on her forehead, small ear studs,
wry look. Grey work overalls with orange straps over a charcoal t-shirt.
```

**Optional: a line-up for checking consistency.** Use this before or after the portraits:

```
A character line-up sheet of eleven people in the same style, each a head-and-shoulders
bust on the same slate background, evenly spaced in two rows: [paste the eleven
descriptions above, one per person].
```

## 3. The talking and blinking frames

Make these from each finished portrait. The frames must line up with the
portrait exactly, because only the mouth box or the eye box is cut out and
laid over it.

| tool | how |
|---|---|
| ChatGPT | Upload the character's portrait and send the edit prompt. If the face drifts, say "keep every pixel outside the mouth (or eyes) identical". |
| M1 local | Inpaint with the portrait as the source. Mask only the region below, use the same seed and model as the portrait, and denoise 0.55-0.75. |

**Mask regions**, as a fraction of the 1024 square:

| region | x | y |
|---|---|---|
| mouth | 33-67% | 59-80% |
| eyes | 29-71% | 39-55% |

**The edit prompts.** Paste these in order, one message each, into the
conversation that holds the character's portrait. Save each result under the
file name above its prompt. The portrait itself is `mouth_0` and `eyes_0`.

First, with the portrait as the latest image:

```
I'm going to ask for ten small edits of this portrait, one per message. Always start each
edit from this original portrait, never from a previous edit. Keep the output the same
size, framing and style, and change only the part I name.
```

`mouth_1` (m b p)
```
Edit the original portrait. Change ONLY the mouth: the lips pressed firmly together in a
straight line, slightly compressed, as when saying "M", "B" or "P". The expression stays
neutral. Keep everything else identical: the same face, eyes, hair, lighting, colours, line
style, framing and background. Same character, same moment.
```

`mouth_2` (f v)
```
Edit the original portrait. Change ONLY the mouth: the upper front teeth resting lightly on
the lower lip, the lower lip tucked slightly under them, as when saying "F" or "V". The
expression stays neutral. Keep everything else identical: the same face, eyes, hair,
lighting, colours, line style, framing and background. Same character, same moment.
```

`mouth_3` (th t d n l)
```
Edit the original portrait. Change ONLY the mouth and the jaw just below it: the mouth
slightly open, the tip of the tongue just visible touching the back of the upper front
teeth, as when saying "TH" or "L". The expression stays neutral. Keep everything else
identical: the same face, eyes, hair, lighting, colours, line style, framing and
background. Same character, same moment.
```

`mouth_4` (s z sh ch)
```
Edit the original portrait. Change ONLY the mouth: the teeth closed together and the lips
parted and stretched slightly wide, showing both rows of teeth, as when saying "S". The
expression stays neutral, not a smile. Keep everything else identical: the same face,
eyes, hair, lighting, colours, line style, framing and background. Same character, same
moment.
```

`mouth_5` (a)
```
Edit the original portrait. Change ONLY the mouth and the jaw just below it: the mouth open
wide in a relaxed "AH", the jaw dropped, the upper teeth visible and the tongue resting
low. The expression stays neutral. Keep everything else identical: the same face, eyes,
hair, lighting, colours, line style, framing and background. Same character, same moment.
```

`mouth_6` (e i y k g h)
```
Edit the original portrait. Change ONLY the mouth and the jaw just below it: the mouth half
open and a little wide, as when saying "EH", the upper teeth visible. The expression stays
neutral, not a smile. Keep everything else identical: the same face, eyes, hair, lighting,
colours, line style, framing and background. Same character, same moment.
```

`mouth_7` (o)
```
Edit the original portrait. Change ONLY the mouth and the jaw just below it: the lips
rounded into an open oval, as when saying "OH", the jaw dropped a little. The expression
stays neutral. Keep everything else identical: the same face, eyes, hair, lighting,
colours, line style, framing and background. Same character, same moment.
```

`mouth_8` (u w q r)
```
Edit the original portrait. Change ONLY the mouth: the lips pushed forward into a small,
tight round pucker with a small dark opening, as when saying "OO" or "W". The expression
stays neutral. Keep everything else identical: the same face, eyes, hair, lighting,
colours, line style, framing and background. Same character, same moment.
```

`eyes_1`
```
Edit the original portrait. Change ONLY the eyes: both upper eyelids lowered halfway over
the eyes, mid-blink, the irises half covered. The eyebrows do not move. Keep everything
else identical: the same face, mouth, hair, lighting, colours, line style, framing and
background. Same character, same moment.
```

`eyes_2`
```
Edit the original portrait. Change ONLY the eyes: both eyes fully closed in a natural,
relaxed blink, the lashes resting on the cheeks. The eyebrows do not move. Keep everything
else identical: the same face, mouth, hair, lighting, colours, line style, framing and
background. Same character, same moment.
```

Locally, use the same prompts for inpainting, without the first sentence.

**If an edit won't come out.** Some models balk at `mouth_3`'s tongue. Try
this version instead:

```
Edit the original portrait. Change ONLY the mouth and the jaw just below it: the lips
parted a little and the teeth slightly apart, a small relaxed opening, as when saying "N"
or "D". The expression stays neutral. Keep everything else identical: the same face, eyes,
hair, lighting, colours, line style, framing and background. Same character, same moment.
```

Or copy the nearest shape under the missing name. At 100 ms a step nobody
sees the difference.

| missing | copy |
|---|---|
| `mouth_1` | the portrait (`base.png`) |
| `mouth_2` | `mouth_1` |
| `mouth_3` | `mouth_4` |
| `mouth_4` | `mouth_3` |
| `mouth_5` | `mouth_6` |
| `mouth_6` | `mouth_5` |
| `mouth_7` | `mouth_8` |
| `mouth_8` | `mouth_7` |
| `eyes_1` | `eyes_2` |

**Two looks: the lounge and the cockpit.** A portrait made with a headset
is the in-flight comm look; the lounge look has none. To get the lounge
look from a headset portrait, send this in the conversation that made it:

```
Edit this exact portrait. Remove the headset completely: the band over the head, the
earpiece and the boom microphone. Fill in what they covered with what belongs there: the
short regulation haircut, the ear, the cheek and the skin beside the mouth, matching the
rest of the face. Keep everything else identical: the same face, expression, mouth, eyes,
hair, lighting, colours, line style, framing and background. Same character, same moment.
```

Then make the mouth and blink edits again from the new portrait: the old
mouth frames still carry the microphone, and an edit may shift the face a
little. Do it in a fresh conversation: upload the new `base.png` and paste
the eleven messages above, so "the original portrait" can only mean that
one. To give a pilot without one a headset (for a full comm set), ask
for the reverse: "Add a slim black pilot headset: band over the head,
earpiece on the left ear, thin boom mic ending beside the mouth. Keep
everything else identical."

## 4. Tips

- **One conversation or seed per character.** In ChatGPT, keep each character's edits in the conversation where its portrait was made. Locally, keep the portrait's seed for its edits.
- **Do the portraits first, all eleven,** and look at them together before any edits. Re-roll any that break the style or the framing.
- **A drifting edit is fine** as long as the mouth (or eyes) came out right. Only its box is used, and the tool tells you if the box moved.
- **Transparent backgrounds help** (ChatGPT can make them). Otherwise keep the flat slate colour and the tool will key it out.
- **Locally:** about 25-30 steps for SDXL, 4 for FLUX.1-schnell. Keep 1024×1024 throughout.

## 5. Where the results go

```
retrovega/art/people/RAY/base.png
retrovega/art/people/RAY/mouth_1.png ... mouth_8.png
retrovega/art/people/RAY/eyes_1.png, eyes_2.png
... the same for COMET, FROST, ECHO, TORCH, KESTREL, DRIFTER, HERO_M, HERO_F, XO, TECH
```

`base.png` is the portrait and doubles as `mouth_0` and `eyes_0`; every
file is the full 1024 square. The keys are those of data/people.csv.

The headset look, where there is one, goes in a `comm/` folder beside it
with the same file names:

```
retrovega/art/people/FROST/base.png ...        the lounge (no headset)
retrovega/art/people/FROST/comm/base.png ...   in flight (headset)
```
