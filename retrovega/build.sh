#!/bin/sh
# Build RV.EXE (and the M0 check, HELLO.EXE) with Open Watcom 2 and NASM.
#   ./build.sh            -> build/dos/RV.EXE, RV.DAT, RVGAME.DAT, HELLO.EXE (DEBUG=1: unoptimised, -d2;
#                            RV_COMMS=face: the comm faces without breathing gear, their mouths speaking;
#                            CODE=1: the code and RVGAME.DAT only, everything else as the last build left it;
#                            LOGIC=path: that file in src/logic.c's place)
# The toolchain lives in ~/opt/watcom (docs/engine.md); WATCOM overrides it.
set -e
cd "$(dirname "$0")"
WATCOM=${WATCOM:-$HOME/opt/watcom}
export WATCOM PATH="$WATCOM/binl64:$PATH" INCLUDE="$WATCOM/h"
OUT=build/dos
mkdir -p "$OUT"
python3 tools/rvabi.py --c-header > src/rvhooks.h
python3 tools/rvabi.py --asm > src/rvhook.asm
python3 tools/rvdata.py --out "$OUT/RVGAME.DAT" --header src/rvdata.h
# the art tools' venv (tools/art-requirements.txt), made with uv on the system python when missing; packages no newer
# than 30 days (UV_EXCLUDE_NEWER overrides), so a release has had time to be caught if it was bad
VENV=$HOME/.venvs/retrovega-art
if [ -z "${CODE:-}" ] && [ ! -x "$VENV/bin/python" ]; then
    command -v uv > /dev/null || { echo "no uv: https://docs.astral.sh/uv/ (it makes $VENV)"; exit 1; }
    uv venv --python python3 "$VENV"
    uv pip install --python "$VENV/bin/python" --exclude-newer "${UV_EXCLUDE_NEWER:-30 days}" \
        -r tools/art-requirements.txt
fi
if [ -z "${CODE:-}" ]; then
python3 tools/midi.py --out "$OUT" > /dev/null
python3 tools/oplbank.py --out "$OUT/RVOPL.BNK" > /dev/null
fi
# optimised (the flight benchmark found -ox worth half again as many sprites a
# frame as -od); DEBUG=1 builds -od -d2 for the debugger
if [ -n "${DEBUG:-}" ]; then OPT="-od -d2"; else OPT="-ox -d1"; fi
CFLAGS="-q -bt=dos -mm -3 -s -w4 -zq $OPT -i=src"
nasm -f obj -i src/ -o "$OUT/rvhook.obj" src/rvhook.asm
nasm -f obj -o "$OUT/lowlevel.obj" src/lowlevel.asm
nasm -f obj -o "$OUT/lasers_a.obj" src/lasers.asm
nasm -f obj -o "$OUT/rocks_a.obj" src/rocks.asm
if [ -z "${CODE:-}" ]; then
# the comm link's faces (one helmet render, every pilot's face behind its visor, each alien's helmet: ~40 s), when
# their sources change.  RV_COMMS=face builds the pilots without their breathing gear, the bare face's mouth
# speaking (the user's option, to weigh its cost); the default, mask, is the user's ruling
RV_COMMS=${RV_COMMS:-mask}
case "$RV_COMMS" in mask) FACE="" ;; face) FACE="--face" ;; *) echo "RV_COMMS is mask or face"; exit 1 ;; esac
COMMS=build/comms-$RV_COMMS
[ $COMMS/comms.json -nt tools/comms.py ] && [ $COMMS/comms.json -nt tools/helmet3d.py ] \
    && [ $COMMS/comms.json -nt tools/aliens3d.py ] && [ $COMMS/comms.json -nt data/aliens.csv ] \
    && [ $COMMS/comms.json -nt data/people.csv ] \
    && [ -z "$(find art/people/faces -newer $COMMS/comms.json -print -quit)" ] \
    || python3 -W ignore tools/comms.py --out $COMMS --cols 56 $FACE > /dev/null   # (56: 2 x 2 blocks, the coarse link)
# the cockpits' panels (every control's states; the power knobs repainted as radio dials: ~15 s), when they change
# (and the pilot's legs under the dash: tools/legs.py)
[ build/panels/.made -nt tools/panels.py ] && [ build/panels/.made -nt data/panels.json ] \
    && [ build/panels/.made -nt tools/legs.py ] && [ build/panels/.made -nt data/legs.json ] \
    && [ -z "$(find art/cockpits -newer build/panels/.made -print -quit)" ] \
    || { python3 -W ignore tools/panels.py --out build/panels > /dev/null && touch build/panels/.made; }
"$VENV/bin/python" -P tools/rvpack.py --out "$OUT/RV.DAT" --header src/rvasset.h --comms $COMMS \
    --panels build/panels
# the balls' sprites (the spherical projectiles: every kind, frame and diameter)
[ "$OUT/BALLS.DAT" -nt tools/balls.py ] && [ "$OUT/BALLS.DAT" -nt data/palette.json ] \
    || "$VENV/bin/python" -P tools/balls.py --out "$OUT/BALLS.DAT" --preview docs/balls.png > /dev/null
# the asteroid fields' rocks (every kind, tumble frame and size: ~30 s; tools/rocks.py), when it changes
[ "$OUT/ROCKS.DAT" -nt tools/rocks.py ] && [ "$OUT/ROCKS.DAT" -nt data/palette.json ] \
    || "$VENV/bin/python" -P tools/rocks.py --out "$OUT/ROCKS.DAT" --preview docs/rocks.png > /dev/null
# the ships' silhouettes (tools/hitboxes.py made them from the meshes; the build has no Vega Strike assets)
cp data/shapes.dat "$OUT/SHAPES.DAT"
# the sky's far things (nebulae, galaxies, planets: every turn of each)
[ "$OUT/SKY.DAT" -nt tools/sky.py ] && [ "$OUT/SKY.DAT" -nt data/palette.json ] \
    || "$VENV/bin/python" -P tools/sky.py --out "$OUT/SKY.DAT" --preview docs/sky.png > /dev/null
# the fighters' views (tools/pack.py, copied into art/ships): beside RV.EXE, and
# their pre-rendered sets (tools/rvsprites.c, the host build of lightfield.c)
cp art/ships/SHIP*.RV "$OUT/" 2>/dev/null || true
mkdir -p build/host
gcc -std=gnu99 -O2 -I src -o build/host/rvsprites src/lightfield.c tools/rvsprites.c
for f in art/ships/SHIP*.RV; do
    s="$OUT/$(basename "${f%.RV}").RVS"
    # (the nearest ships are drawn exactly in flight: rolls at every size from half down, so a far
    # ship turns with the player's roll - the user's ruling, 2026-10-20)
    [ "$s" -nt "$f" ] && [ "$s" -nt tools/rvsprites.c ] && [ "$s" -nt src/lightfield.c ] || build/host/rvsprites "$f" "$s" 0 4 9 > /dev/null
    # and the block mode's (RV.EXE /res 2): the ladder only, on every view, from half size, drawn as 2 x 2 blocks
    h="${s%.RVS}.RVH"
    [ "$h" -nt "$f" ] && [ "$h" -nt tools/rvsprites.c ] && [ "$h" -nt src/lightfield.c ] || build/host/rvsprites "$f" "$h" 0 0 0 1 2 > /dev/null
done
# the carrier (a test aid, RV.EXE /capital): from half size down, rolls at two sizes
if [ -f build/rvdat/SHIP20.RV ]; then
    s="$OUT/SHIP20.RVS"
    [ "$s" -nt build/rvdat/SHIP20.RV ] && [ "$s" -nt tools/rvsprites.c ] || build/host/rvsprites build/rvdat/SHIP20.RV "$s" 3 3 2 > /dev/null
    h="$OUT/SHIP20.RVH"             # (the block mode: a capital at a quarter, drawn as 4 x 4 blocks)
    [ "$h" -nt build/rvdat/SHIP20.RV ] && [ "$h" -nt tools/rvsprites.c ] || build/host/rvsprites build/rvdat/SHIP20.RV "$h" 0 0 0 1 4 > /dev/null
fi
fi
# the explosions' heat cels and the flight's sound effects (tools/explode.py, tools/sfx.py: seconds), when they
# change; made with CODE=1 too (the code reads them)
[ "$OUT/EXPLODE.DAT" -nt tools/explode.py ] \
    || ~/.venvs/retrovega-art/bin/python -P tools/explode.py --out "$OUT/EXPLODE.DAT" > /dev/null
[ "$OUT/SFX.DAT" -nt tools/sfx.py ] && [ "$OUT/SFX.DAT" -nt data/sounds.csv ] \
    || ~/.venvs/retrovega-art/bin/python -P tools/sfx.py --out "$OUT/SFX.DAT" > /dev/null
ENGINE="util xms video dat gfx input room lipsync opl music score campaign lightfield ships lasers bolts balls rocks missiles sky collide flames comms panel cockpit world shots hud explode sfx fx logic flight game main"
LOGIC=${LOGIC:-src/logic.c}           # (LOGIC=path: another logic.c in its place, an experiment's; the play tests' check)
for c in $ENGINE hello; do
    if [ "$c" = logic ]; then src=$LOGIC; else src=src/$c.c; fi
    wcc $CFLAGS -fo="$OUT/$c.obj" "$src"
done
# the test build, RVT.EXE (docs/engine.md, "The test build"): the modules with test code (the demos, the
# benchmarks, the self-tests, under RV_TESTS) compiled again with it on; RV.EXE, the game's, goes without
TESTED=$(cd src && grep -l 'RV_TESTS' $(for c in $ENGINE; do printf '%s.c ' "$c"; done) | sed 's/\.c$//' | tr '\n' ' ')
mkdir -p "$OUT/t"
for c in $TESTED; do
    if [ "$c" = logic ]; then src=$LOGIC; else src=src/$c.c; fi
    wcc $CFLAGS -dRV_TESTS -dRV_NO_TWINS -fo="$OUT/t/$c.obj" "$src"   # (the twins are RVP.EXE's)
done
# and the asm's C twins (twins.c: the host build draws with them), checked against it (RVP.EXE /twins)
wcc $CFLAGS -dRV_TESTS -fo="$OUT/t/twins.obj" src/twins.c
# the self-tests' build, RVP.EXE: RV.EXE with the play tests (tests/play/playtest.c: RVP.EXE /playtest,
# docs/gameplay.md) and the asm's C twins (src/twins.c: RVP.EXE /twins), and main's switches for them; nothing
# else (in RVT.EXE they took 62 KB its flights need: both exit before any flight)
mkdir -p "$OUT/p"
wcc $CFLAGS -dRV_PLAYTEST -fo="$OUT/p/main.obj" src/main.c
wcc $CFLAGS -fo="$OUT/p/playtest.obj" tests/play/playtest.c
# RVHOOK first (image paragraph 0), the C code, then DGROUP last as Watcom's
# startup needs it, with the fixed block (RVFIXED) ahead of Watcom's _NULL so
# it starts at DS:0000 (docs/engine.md).
# the thunks' default bodies, one module each in a library (see rvabi.py --stubs)
python3 tools/rvabi.py --stubs "$OUT/stubs"
rm -f "$OUT/rvstubs.lib"
for c in "$OUT"/stubs/*.c; do
    wcc $CFLAGS -fo="${c%.c}.obj" "$c"
    wlib -q -b "$OUT/rvstubs.lib" +"${c%.c}.obj"
done
link() {
    name=$1; shift
    wlink system dos name "$OUT/$name" option quiet option map="$OUT/${name%.EXE}.MAP" \
        option stack=8192 \
        order clname RVHOOK clname CODE clname FAR_DATA \
              clname BEGDATA segment RVFIXED segment _NULL segment _AFTERNULL \
              clname DATA clname BSS clname STACK \
        file "$OUT/rvhook.obj" "$@" library "$OUT/rvstubs.lib"
}
link HELLO.EXE file "$OUT/lowlevel.obj" file "$OUT/util.obj" file "$OUT/xms.obj" file "$OUT/hello.obj"
link RV.EXE file "$OUT/lowlevel.obj" file "$OUT/lasers_a.obj" file "$OUT/rocks_a.obj" $(for c in $ENGINE; do printf ' file %s/%s.obj' "$OUT" "$c"; done)
link RVP.EXE file "$OUT/lowlevel.obj" file "$OUT/lasers_a.obj" file "$OUT/rocks_a.obj" file "$OUT/p/playtest.obj" \
    file "$OUT/t/twins.obj" \
    $(for c in $ENGINE; do
    case "$c" in main) printf ' file %s/p/main.obj' "$OUT";; *) printf ' file %s/%s.obj' "$OUT" "$c";; esac; done)
link RVT.EXE file "$OUT/lowlevel.obj" file "$OUT/lasers_a.obj" file "$OUT/rocks_a.obj" $(for c in $ENGINE; do
    case " $TESTED " in *" $c "*) printf ' file %s/t/%s.obj' "$OUT" "$c";; *) printf ' file %s/%s.obj' "$OUT" "$c";; esac; done)
echo "built $OUT/RV.EXE, $OUT/RVT.EXE (the test build), $OUT/RVP.EXE (the self-tests) and $OUT/HELLO.EXE"
