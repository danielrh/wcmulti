AM_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include -I$(builddir) $(PROTOBUF_CFLAGS)

SUBDIRS = cpu debug dos fpu gui hardware libs ints misc shell platform

bin_PROGRAMS = dosbox

if HAVE_WINDRES
ico_stuff = winres.rc
endif

# The generated protobuf code lands in the build directory (also for VPATH
# builds: src/cpu finds it through -I$(top_builddir)/src).
wc.pb.cc wc.pb.h: $(srcdir)/wc.proto
	$(PROTOC) --proto_path=$(srcdir) --cpp_out=$(builddir) $(srcdir)/wc.proto

.rc.o:
	$(WINDRES) -o $@ $<

dosbox_SOURCES = dosbox.cpp wc.pb.cc $(ico_stuff)
dosbox_LDADD = cpu/libcpu.a debug/libdebug.a dos/libdos.a fpu/libfpu.a  hardware/libhardware.a gui/libgui.a \
               ints/libints.a misc/libmisc.a shell/libshell.a hardware/serialport/libserial.a libs/gui_tk/libgui_tk.a $(PROTOBUF_LIBS)
EXTRA_dosbox_DEPENDENCIES =

EXTRA_DIST = winres.rc dosbox.ico wc.proto pre.js web_runtime.js wclobby_web.js

# https://stackoverflow.com/questions/1779984/header-dependency-in-automake
BUILT_SOURCES=version.stamp wc.pb.h
CLEANFILES = wc.pb.cc wc.pb.h
version.stamp:
	$(MKDIR_P) $(top_builddir)/include
	$(PYTHON) $(srcdir)/version.py $(top_builddir)/include/version.h $(top_srcdir) $(CXX)

if LOBBYLINK
# The lobbylink (room code) transport is a Rust static library, see
# ../wclobby.  cargo rebuilds it when its sources change and leaves the
# archive untouched otherwise, so dosbox only relinks when it changed.
WCLOBBY_LIB = $(top_srcdir)/wclobby/target/release/libwclobby.a
BUILT_SOURCES += wclobby-lib
wclobby-lib:
	+cd $(top_srcdir)/wclobby && $(CARGO) build --release
$(WCLOBBY_LIB): | wclobby-lib
.PHONY: wclobby-lib
dosbox_LDADD += $(WCLOBBY_LIB) $(WCLOBBY_LIBS)
endif

if EMSCRIPTEN_BUILD
# The browser build: src/dosbox.js + src/dosbox.wasm, a modularized ES5
# module (createDOSBox) that web/wc.js drives.  main() is not run
# automatically: the page fills the file system and the environment first
# and then calls Module.callMain().
dosbox_LDFLAGS = -sINITIAL_MEMORY=134217728 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=4194304 \
                 -sFORCE_FILESYSTEM=1 -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createDOSBox -sENVIRONMENT=web \
                 -sINVOKE_RUN=0 -sEXIT_RUNTIME=0 \
                 -sEXPORTED_FUNCTIONS=_main,_malloc,_free,_wc_web_emulated_ms,_wc_web_perf,_wc_web_ds_byte,_wc_web_tap_escape,_wc_web_key,_wc_web_pointer,_wc_web_mouse_button,_wc_web_in_flight,_wc_web_set_rocks,_wc_web_rocks,_wc_web_set_wing_bumps --profiling-funcs \
                 -sEXPORTED_RUNTIME_METHODS=FS,ENV,callMain,UTF8ToString \
                 --pre-js $(srcdir)/pre.js --js-library $(srcdir)/web_runtime.js
EXTRA_dosbox_DEPENDENCIES += $(srcdir)/pre.js $(srcdir)/web_runtime.js
CLEANFILES += dosbox.js dosbox.wasm
if ASYNCIFY
# Every function that can be on the stack when the emulator sleeps or the
# multiplayer transport waits for the network is instrumented (no
# ASYNCIFY_ONLY list: the wcnet hooks run inside the CPU core, reached
# through function pointers and virtual calls).  The asyncify stack must
# hold the locals of that whole call chain.
dosbox_LDFLAGS += -sASYNCIFY=1 -sASYNCIFY_STACK_SIZE=1048576
endif
if LOBBYLINK_WEB
dosbox_LDFLAGS += --js-library $(srcdir)/wclobby_web.js
EXTRA_dosbox_DEPENDENCIES += $(srcdir)/wclobby_web.js
endif
endif
