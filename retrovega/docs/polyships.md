# Polygon ships: the nearest ship rendered, not looked up (a design note, parked)

Parked on 2026-10-22: the user's call ("not really the style I'm going for
and a bit far afield"). Kept here for a later discussion.

**The problem it would solve.** Every pre-rendered view is right at its
lattice angle and nothing in between; when the direction from a ship to
the camera crosses a cell, the picture swaps. For a fighter turning at its
110 degrees a second that is seven swaps a second at 15 degrees, each one
moving the tail and wingtips by a quarter of the ship's length. A denser
lattice shrinks the swaps and a dithered blend softens them; only drawing
the ship from its shape removes them.

**The shape.** Each ship's Vega Strike mesh decimated on the host to
150-300 triangles (tools/, from the .bfxm the sprites are rendered from),
each face given one palette index from the mean of its texture under the
sprite lighting, packed into a small per-type asset (a few KB) beside the
views.

**The draw.** For the nearest ship only (the hybrid's first slot): the
mesh turned by the ship's basis into the camera's, back faces culled, a
painter's sort, each face a flat fill at the 2 x 2 block resolution
(160 x 100 of them across the opening) with its index shaded by the key
light's angle through the palette's shade table. At that resolution a
ship filling the opening is ~3,000 blocks; 200 triangles' setup plus
~3,000 block writes is about 50,000 cycles, roughly a millisecond: the
exact path's cost today for the same ship.

**What it would look like.** Flat-shaded polygons in the palette's ramps,
no texture, no panel lines: Privateer 2 / WC3 near-field ships over the
sprite mid field. The hand-off is the catch: the same ship would change
style, not just resolution, where the hybrid passes it from the sprites to
the mesh.

**If it is ever taken up.** The decimation tool is a day; the rasterizer
in lowlevel.asm another; the hybrid already has the slot (plan_draws'
first exact ship). It could be judged on the zoom scene before anything
else changes: a /poly switch, a recording, the user's eye.
