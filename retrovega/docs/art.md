# Ship sprites from Vega Strike meshes

`tools/shipviews.py` renders a ship from Vega Strike's assets into the 37
views of WC1's layout (spec §8) at twice WC1's resolution, in a livery
(62 views for an asymmetric ship). `tools/render_all.py` renders the whole
roster into `build/sprites/` (not committed) and a gallery.

## Two looks

| `mode` (data/art.csv) | used for | lighting |
|---|---|---|
| `vs` (the default) | the canon Vega Strike ships | Vega Strike's own uber shader (`programs/uber.fp`), ported |
| `retro` | ships repainted from scratch, so far only the Franklin | the custom WC1-style look below: whitened hull, point light, cel bands |

The `vs` port keeps uber.fp's material typing (matte, metal, dielectric,
glass from the diffuse and specular colours), gloss from the specular
map's intensity, and environment reflections blurred by gloss from a Vega
Strike background cube (`textures/backgrounds/white_light.cube`, read by
`tools/ddscube.py`). It is lit by the same off-axis key as `retro`.

Each change from uber.fp was made after looking at the result:

- **Metal, not plastic.** The reflected light (the key's highlight plus the environment) is multiplied by the specular map's colour and added. uber's dielectric Fresnel coat put a white reflection over every surface, which looked like cling film on a sprite; it is kept for glass only. The Rlaan's imperial purple comes from their violet specular maps through uber's metal diffuse.
- **Matte surfaces take the diffuse once linearised.** uber squares it twice over, which only Vega Strike's bright HDR lights make up for.
- **Reflections are never mirror-sharp.** The environment is sampled at mip 4 or blurrier; a sharp one reads as crumpled foil.
- **Self-shadow term:** uber's own alternative `clamp(4 N.L)`. The current one caps every light at a quarter.
- **Diffuse alpha is ignored.** Some Rlaan maps keep other data in alpha.
- **Divisions are guarded.**

Liveries in `vs` mode replace the texture only where the paint rules hit
(the texture's fine detail goes back on top of the paint). `tint`
recolours a whole hull and keeps its brightness: the Aera capital ships
whose `AeraHull` map is missing are teal, as on their VDUs.
[franklin-sheet.png](franklin-sheet.png) shows all 37 views of the
Franklin; [franklin-liveries.png](franklin-liveries.png) shows nine of them
in four liveries.

## Setup

It is a minimal headless OpenGL renderer: moderngl on Mesa's EGL. llvmpipe
is enough; no GPU or display is needed. System Python refuses pip
installs, so it lives in a virtual environment that reuses the system's
numpy, scipy and Pillow:

    python3 -m venv --system-site-packages ~/.venvs/retrovega-art
    ~/.venvs/retrovega-art/bin/pip install moderngl

The assets are a copy of Vega Strike's Assets-Production at
`~/dev/vegastrike-assets` (`--assets` to change it).

## Use

    ~/.venvs/retrovega-art/bin/python -P tools/shipviews.py FRANKLN out/ [--livery carrier-green] [--size 160]

A ship takes about 4 seconds at 160 px. Outputs, with TAG = KEY-LIVERY:

| file | contents |
|---|---|
| `TAG_NN.png` | view NN (0 top, 1-35 the bands, 36 bottom), RGBA, hard alpha, centred on the ship |
| `TAG_paint_NN.png` | the paint mask: 1 primary, 2 secondary, 3 canopy glass, 0 hull. It sends those pixels to the livery ramps and the glass ramp of the palette. |
| `TAG_sheet.png` | the 37 views as a grid. Rows: top, +60, +30, 0, −30, −60, bottom. Columns: headings 0 (nose toward you), 30, ..., 90 (nose left), ..., 180 (tail toward you). |
| `TAG_sheet_256.png` | a 48-colour posterised preview, until the real palette exists |

## Per-ship settings: data/art.csv

| column | meaning |
|---|---|
| `stretch` | x y z scale before rendering. The Franklin is `1 1.8 1`, because its mesh is a 0.6 m-thick dart. |
| `exposure` | overall brightness |
| `toon` | key-light bands (3 gives a cel look; 0 is smooth) |
| `mode` | `vs` or `retro` (above) |
| `tint` | r g b: the whole hull's colour multiplied in, its brightness kept |
| `sheen` | r g b: the same for the metal's reflection (canon ships), its brightness kept: the Aera's blue-silver on the Convolution |
| `reflect` | how strongly the metal mirrors the sky (canon ships). Default 1. |
| `whiten` | 0..1: how far the hull goes toward light grey |
| `detail` | 0..1: how much of the texture's fine detail (panel lines, vents, grime) shows on top of the whitened hull and the paint. Default 0.6. |
| `bump` | how much that detail bends the light, so panel edges catch it like bevels and greebles. Default 1. |
| `shine` | the hull's least specular strength: higher gives broader, whiter sheen, like WC1's shiny hulls. Default 0.3. |
| `paint`, `paint2` | rules for the primary and secondary livery colours, terms joined with `\|` |
| `glass` | the rule for the canopy: dark navy glass with a sharp highlight and a reflected sky, whatever the livery |
| `decals` | up to four decals, joined with `\|` (below) |
| `livery` | the default livery |

Paint terms:

| term | paints |
|---|---|
| `span>F` | everything beyond F of the half-span (outer wings) |
| `fin>F` | vertical surfaces beyond F of the half-span (wingtip fins) |
| `band:A-B` | a stripe across the wings between A and B of the half-span |
| `spine<F` | a stripe along the centre line, F of the half-span wide |
| `hazard>F` | alternating bands beyond F of the half-span |
| `lum>P` | the brightest panels: above the P-th fraction of the texture's used texels, smoothed |
| `above>F` | everything higher than F of the ship's height |
| `blue>D` | texels of the diffuse map bluer than their red and green by D (0..1): a canopy the Vega Strike artist painted navy |

Any term can end in `@top` or `@bottom` to paint only the surfaces facing
that way on the ship. The Franklin uses `fin>0.95|band:0.55-0.7` in the
primary colour and `spine<0.07@bottom` in the secondary. Its glass is
`blue>0.12`: the oval dome on the nose taper, painted navy in its texture.
The hull's own texels are never more than 0.03 bluer; the dome's are 0.26.

Decals are written `name:x z size rot`, with optional suffixes:

- **x:** a signed fraction of the half-span.
- **z:** a fraction of the length from the tail.
- **size:** a fraction of the length.
- **rot:** degrees.
- **`@top` or `@bottom`:** puts the decal on one side only.
- **`~m`:** mirrors it onto both wings.

They are projected straight down (or up) onto the hull. They keep their own
colours, so a livery never recolours them, and the texture's detail shows
through them. The decals are drawn by the renderer itself:

| decal | what |
|---|---|
| `arrows` | two red chevrons pointing forward |
| `roundel` | a navy disc with a white ring and a gold star |
| `hazard` | yellow and black diagonal bands |

The Franklin carries `arrows:0 0.56 0.13 0@top|roundel:0.82 0.13 0.09 0@top~m`:
the chevrons just behind its canopy, and a roundel near each wingtip.

## The light

Every ship gets the same light, so the sprites match one another
(`KEY_DIR`, `KEY_DISTANCE` in `shipviews.py`). It is a point light mostly
from the left, somewhat above and only a little in front, well off the view
axis as in WC1's renders, at three ship radii; the shadow side keeps 0.30
ambient.  `--key x,y,z`, `--ambient` and `--shine` try other settings
(`--tag` names the trial's files). Faces turned toward it are bright, faces
turned away fall into shadow, and flat panels shade across. A dim cool
fill comes from the lower right.

## Liveries: data/liveries.csv

A livery is two colours, primary and secondary. No paint is near
black: against space a black stripe reads as a hole in the hull, so the
darkest accents are deep navy (26 36 84) or dark brown (70 42 26). The sprites keep paint in
placeholder ramps, so a livery costs nothing at run time: the engine
recolours when it decodes a ship type for a mission (spec §8).

## New hulls by recipe: tools/hulls.py

A ship whose mesh in `ships.csv` is `hull:NAME` is built from Vega Strike
meshes by the recipe NAME. It starts from a base ship (its mesh, maps and
exhaust points) and applies these operations:

| operation | does |
|---|---|
| `scale` | stretches the hull |
| `clip` | shortens the wings |
| `sweep` | bends the wingtips forward or back |
| `lift` | gull or anhedral wings |
| `cut` | removes a box of triangles |
| `graft` | copies a box of triangles from any Vega Strike mesh, scaled, placed, rolled about the nose axis and optionally mirrored |

Normals follow each deformation through its Jacobian. The new Aera
fighters, compared with their bases in
[aera-new.png](aera-new.png):

| ship | built from |
|---|---|
| Agis | Nicander cut short, its wings swept forward into a gull W: a swarm fighter |
| Cleomenes | Areus with a copy of its own wing upright above and below: a cross seen from the front |
| Archidamus | Ariston rebuilt asymmetric: its big rear wing, half again as large, on the right only, and two small front wings angled up and down on the left. **Retired from the roster**: the user chose Vega Strike's own asymmetric Aera fighter, the Convolution, for the aces. The recipe (and `ARCHIDM_CLAW`, its first forward-swept version) stays in hulls.py. |

An asymmetric ship cannot use WC1's mirrored headings. It renders 62
views: WC1's 37, then headings 210, 240, 270, 300 and 330 of each band
(37..61). A recipe says so (`asym`); a canon mesh is found asymmetric by
itself when its left-right mirror image misses it: the median distance
from a mirrored vertex to the nearest vertex over 2% of the radius. Every
other canon ship is under 0.5%; the Convolution is 15%.

**Sheen** (data/art.csv) is `tint` for the metal's reflection: the
specular map's colour multiplied by it, luminance kept. The Convolution's
texture is plain grey with a white specular map over its big wing, which
mirrored the sky like chrome. With `tint 200 205 225`, `sheen 120 150 240`
and `reflect 0.6` it takes the blue-silver of the Aera fighters' VDUs.
(`tint` and `sheen` both `70 160 150` would make it teal, like the Aera
capital ships.)

## Facts about the Vega Strike data

- **Shared textures:** maps a ship names but does not hold are found in the shared `textures/` folder, matched without case, then without extension, then by prefix (`AeraHull.png` becomes `AeraHull2.bmp`, the only one there).
- **Shared-vertex meshes** (Shizong, Shenzong) keep their texture coordinates on the vertices; their polygon corners are all zero.

- **Textures:** files named `.png` or `.jpg` are often DDS (DXT1/3/5); Pillow reads them. Texture `t` runs top-down, so images go to OpenGL unflipped.
- **Meshes:** `.bfxm` records are a ship's separate parts; the meshes inside a record are its levels of detail, finest first. A record whose texture is an `.ani` is a running light. `bfxm.parts()` takes the finest mesh of each real part.
- **Normals:** the stored vertex normals point inward, the opposite of the triangles' outward winding, on every ship checked. `bfxm.py` flips them, as Vega Strike does.
- **units.csv:** `Light` gives the exhaust points (x;y;z;size), from which the renderer draws engine glows. `Mounts` gives the gun mounts, for the per-view hardpoints (M3).

## WC1's layout, for reference

WC1's ship files (`SHIPTYPE.Vnn`) hold the 37 views as WC run-length
sprites. `scripts/wc2data.py` reads them: `read_container`, then the first
block's offset table, then `decode_sprite`. `GAME.PAL` is an ILBM; its
palette is the CMAP at offset 0x30. `SHIPTYPE.V00` is the Hornet. Its views
are 60 to 100 px wide, its hull white with green panels, a red emblem and
hazard-striped wing edges. That is the look the liveries aim for.
