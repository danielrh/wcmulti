# desktop/Makefile: RetroVega for a Linux desktop (desktop/README.md).  Four programs:
#
#   rv-core    the game: src/ as RV.EXE has it, host/hle.c in DOS's place, the assembly's C twins.  32-bit
#              (-m32: the game's far pointers and its files' records are 32-bit layouts); the C library only.
#   rv         its window, sound and input: SDL2 and ymfm's OPL3 (BSD, host/ymfm).  64-bit.
#   rvsprites  the ships' sprite sets from their views (art/ships), for `make data`.
#   rv-sandbox-test  the audit's: rv-core's seccomp filter, then one thing it must refuse (audit.sh forbidden).
#
# Every source is listed below.  Nothing is generated or downloaded while building: the two generated files are
# checked in under gen/ (`make check-generated` compares them with tools/rvabi.py's output), and the only
# programs run are the compiler, the linker and pkg-config (for SDL2's flags).
#
#   make                          the programs, in $(OUT)
#   make data DATA=DIR [FROM=..]  the game's files into DIR: FROM's data files (default ../build/dos, the DOS
#                                 build's; or a directory fetch-data.sh filled) and the ships' sets from art/ships
#   make check-generated          gen/ against tools/rvabi.py (needs python3)
#   make clean
# Needs: gcc (and gcc-multilib for -m32), g++, make, pkg-config, libsdl2-dev.

OUT ?= ../build/desktop
CC ?= cc
CXX ?= c++
ROOT := $(abspath ..)

# --- rv-core: the game ---

CORE_SRC = \
    ../src/util.c ../src/xms.c ../src/video.c ../src/dat.c ../src/gfx.c ../src/input.c ../src/room.c \
    ../src/lipsync.c ../src/opl.c ../src/music.c ../src/score.c ../src/campaign.c ../src/lightfield.c \
    ../src/ships.c ../src/lasers.c ../src/bolts.c ../src/balls.c ../src/rocks.c ../src/missiles.c ../src/sky.c \
    ../src/collide.c ../src/flames.c ../src/comms.c ../src/panel.c ../src/cockpit.c ../src/world.c \
    ../src/shots.c ../src/hud.c ../src/logic.c ../src/flight.c ../src/game.c ../src/main.c ../src/twins.c \
    ../host/hle.c ../host/fixed.c ../host/flightloop.c ../host/asm_twins.c ../host/fsguard.c \
    gen/thunks.c gen/stubs.c core.c seccomp.c

# the game's sources as Watcom builds them (char unsigned, no type-based aliasing, signed overflow wrapping), with
# the DOS words answered by host/rvhost.h and host/dos/; hardened (stack protector, position independent, full
# RELRO, no executable stack)
CORE_CFLAGS = -m32 -std=gnu99 -O2 -g -fwrapv -funsigned-char -fno-strict-aliasing -fstack-protector-strong -fPIE \
    -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -DRV_HOST_GAME -include ../host/rvhost.h \
    -I../host/dos -I../src -I../host -I. -ffile-prefix-map=$(ROOT)=retrovega \
    -Wall -Wno-unused-parameter -Wno-sign-compare -Wno-unused-result -Wno-format -Wno-unused-but-set-variable \
    -Wno-pointer-sign -Wno-char-subscripts -Wno-stringop-truncation
CORE_LDFLAGS = -m32 -pie -Wl,-z,relro,-z,now,-z,noexecstack

# fmt.c without rvhost.h (its sprintf and vfprintf are what rvhost.h names them)
CORE_OBJ = $(patsubst %.c,$(OUT)/core/%.o,$(notdir $(CORE_SRC))) $(OUT)/core/fmt.o

# --- rv: the window, the sound, the input ---

FRONT_SRC = front.c
FRONT_CXX_SRC = ../host/opl3.cpp ../host/ymfm/ymfm_opl.cpp ../host/ymfm/ymfm_adpcm.cpp ../host/ymfm/ymfm_pcm.cpp
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS := $(shell pkg-config --libs sdl2)
FRONT_CFLAGS = -std=gnu99 -O2 -g -fstack-protector-strong -fPIE -D_FORTIFY_SOURCE=2 -I../host -I. \
    -ffile-prefix-map=$(ROOT)=retrovega -Wall -Wextra -Wno-unused-parameter $(SDL_CFLAGS)
FRONT_CXXFLAGS = -std=c++14 -O2 -g -fstack-protector-strong -fPIE -I../host -ffile-prefix-map=$(ROOT)=retrovega -w
FRONT_LDFLAGS = -pie -Wl,-z,relro,-z,now,-z,noexecstack
FRONT_OBJ = $(OUT)/front/front.o $(patsubst %.cpp,$(OUT)/front/%.o,$(notdir $(FRONT_CXX_SRC)))

# --- rvsprites: the ships' sets (the same tool and arguments as ../build.sh) ---

SPRITES_SRC = ../tools/rvsprites.c ../src/lightfield.c

all: $(OUT)/rv-core $(OUT)/rv $(OUT)/rvsprites $(OUT)/rv-sandbox-test

$(OUT)/rv-core: $(CORE_OBJ)
	$(CC) $(CORE_LDFLAGS) -o $@ $(CORE_OBJ) -lm

$(OUT)/core/%.o: ../src/%.c | $(OUT)/core
	$(CC) $(CORE_CFLAGS) -c -o $@ $<
$(OUT)/core/%.o: ../host/%.c | $(OUT)/core
	$(CC) $(CORE_CFLAGS) -c -o $@ $<
$(OUT)/core/%.o: gen/%.c | $(OUT)/core
	$(CC) $(CORE_CFLAGS) -c -o $@ $<
$(OUT)/core/core.o: core.c proto.h | $(OUT)/core
	$(CC) $(CORE_CFLAGS) -c -o $@ core.c
$(OUT)/core/seccomp.o: seccomp.c syscalls.h | $(OUT)/core
	$(CC) $(CORE_CFLAGS) -c -o $@ seccomp.c
$(OUT)/core/fmt.o: ../host/fmt.c | $(OUT)/core
	$(CC) -m32 -std=gnu99 -O2 -g -fstack-protector-strong -fPIE -ffile-prefix-map=$(ROOT)=retrovega -c -o $@ $<

$(OUT)/rv: $(FRONT_OBJ)
	$(CXX) $(FRONT_LDFLAGS) -o $@ $(FRONT_OBJ) $(SDL_LIBS) -lm

$(OUT)/front/front.o: front.c proto.h | $(OUT)/front
	$(CC) $(FRONT_CFLAGS) -c -o $@ front.c
$(OUT)/front/%.o: ../host/%.cpp | $(OUT)/front
	$(CXX) $(FRONT_CXXFLAGS) -c -o $@ $<
$(OUT)/front/%.o: ../host/ymfm/%.cpp | $(OUT)/front
	$(CXX) $(FRONT_CXXFLAGS) -c -o $@ $<

$(OUT)/rv-sandbox-test: sandboxtest.c $(OUT)/core/seccomp.o
	$(CC) -m32 -std=gnu99 -O2 -g -fstack-protector-strong -fPIE -Wall -c -o $(OUT)/core/sandboxtest.o sandboxtest.c
	$(CC) $(CORE_LDFLAGS) -o $@ $(OUT)/core/sandboxtest.o $(OUT)/core/seccomp.o

$(OUT)/rvsprites: $(SPRITES_SRC) | $(OUT)
	$(CC) -std=gnu99 -O2 -Wno-unused-result -I../src -o $@ $(SPRITES_SRC)

$(OUT) $(OUT)/core $(OUT)/front:
	mkdir -p $@

# the game's files: the data the DOS build makes (or fetch-data.sh fetched), and the ships' sets made here (the
# sharp sets, SHIPnn.RVS, and the block mode's, SHIPnn.RVH: ../build.sh's recipes)
FROM ?= ../build/dos
DATA_FILES = RV.DAT RVGAME.DAT BALLS.DAT SKY.DAT SHAPES.DAT ROCKS.DAT RVOPL.BNK
data: $(OUT)/rvsprites
	@test -n "$(DATA)" || { echo "make data DATA=dir (where the game's files go)"; exit 1; }
	mkdir -p $(DATA)
	cd $(FROM) && cp $(DATA_FILES) *.MID $(abspath $(DATA))/
	for f in ../art/ships/SHIP*.RV; do \
	    s=$(DATA)/$$(basename $$f .RV); \
	    $(OUT)/rvsprites $$f $$s.RVS 0 4 9 > /dev/null && $(OUT)/rvsprites $$f $$s.RVH 0 0 0 1 2 > /dev/null || exit 1; \
	done

check-generated:
	python3 ../tools/rvabi.py --weak-stubs | diff -u gen/stubs.c -
	python3 ../tools/rvabi.py --host-thunks | diff -u gen/thunks.c -
	python3 ../tools/rvabi.py --c-header | diff -u ../src/rvhooks.h -
	@echo "gen/ and src/rvhooks.h are what tools/rvabi.py makes"

clean:
	rm -rf $(OUT)

.PHONY: all data check-generated clean
