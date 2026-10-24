#!/bin/bash
# Builds the browser version of DOSBox with the Wing Commander multiplayer
# layer and assembles a ready-to-serve page in web/dist:
#
#   scripts/build-web.sh            # everything: deps, configure, make, page
#   web/serve.py                    # then open http://localhost:8000/
#
# Needs: the Emscripten SDK (emcc on PATH, or EMSDK=/path/to/emsdk), cmake,
# curl, autotools, protoc, node/npm (for the lobbylink TypeScript client)
# and the lobbylink submodule (git submodule update --init).
#
# Environment:
#   WCDIR      directory with the game files (default: ./wc); only *.EXE,
#              *.CFG, *.BAT, *.COM, *.DAT, *.OVL and GAMEDAT/ are packaged
#   WCFILES    explicit list of files/dirs inside WCDIR to package instead
#   WEB_OUT    output directory (default: web/dist)
#   JOBS       parallel make jobs (default: nproc)
#   CONFIGURE_FLAGS  extra ./configure flags
#   SKIP_INNOEXTRACT=1  do not build innoextract for the browser (the page
#              then only accepts .zip files, not GOG installers)
#   SKIP_RETROVEGA=1    do not build RetroVega (retrovega/build.sh: Open Watcom
#              and the art venv); the page then has no built-in game
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${BUILD_WEB_DIR:-$ROOT/build-web}
DEPS=$BUILD/deps
WCDIR=${WCDIR:-$ROOT/wc}
OUT=${WEB_OUT:-$ROOT/web/dist}
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 2)}
# The generated protobuf code must match the runtime linked into the wasm,
# so protoc has to be this version too (built below if the system one differs).
PROTOBUF_VERSION=3.21.12

say() { echo "== $*"; }

# -- Emscripten ---------------------------------------------------------------
if ! command -v emcc >/dev/null 2>&1; then
    EMSDK=${EMSDK:-$HOME/emsdk}
    if [ -f "$EMSDK/emsdk_env.sh" ]; then
        # shellcheck disable=SC1091
        source "$EMSDK/emsdk_env.sh" >/dev/null 2>&1
    else
        echo "emcc not found: install the Emscripten SDK (https://emscripten.org/docs/getting_started/downloads.html)" >&2
        echo "and put it on PATH or set EMSDK=/path/to/emsdk" >&2
        exit 1
    fi
fi
say "using $(emcc --version | head -1)"

# -- protobuf for wasm (and a matching protoc) --------------------------------
mkdir -p "$DEPS"
tarball=protobuf-cpp-$PROTOBUF_VERSION.tar.gz
fetch_protobuf() {
    cd "$DEPS"
    [ -f "$tarball" ] || curl -sSL -o "$tarball" "https://github.com/protocolbuffers/protobuf/releases/download/v${PROTOBUF_VERSION#3.}/$tarball"
    [ -d "protobuf-$PROTOBUF_VERSION" ] || tar xzf "$tarball"
}
if [ ! -f "$DEPS/protobuf/lib/libprotobuf.a" ]; then
    say "building protobuf $PROTOBUF_VERSION for wasm"
    fetch_protobuf
    emcmake cmake -S "protobuf-$PROTOBUF_VERSION" -B protobuf-build -DCMAKE_BUILD_TYPE=Release \
        -Dprotobuf_BUILD_TESTS=OFF -Dprotobuf_BUILD_PROTOC_BINARIES=OFF -Dprotobuf_BUILD_SHARED_LIBS=OFF \
        -Dprotobuf_WITH_ZLIB=OFF -DCMAKE_INSTALL_PREFIX="$DEPS/protobuf" > protobuf-cmake.log
    cmake --build protobuf-build -j"$JOBS" > protobuf-build.log
    cmake --install protobuf-build > protobuf-install.log
fi
PROTOC_FLAG=
if ! protoc --version 2>/dev/null | grep -q " $PROTOBUF_VERSION\$"; then
    if [ ! -x "$DEPS/protoc/bin/protoc" ]; then
        say "system protoc is not $PROTOBUF_VERSION; building a matching native protoc"
        fetch_protobuf
        cmake -S "protobuf-$PROTOBUF_VERSION" -B protoc-build -DCMAKE_BUILD_TYPE=Release \
            -Dprotobuf_BUILD_TESTS=OFF -Dprotobuf_BUILD_SHARED_LIBS=OFF -Dprotobuf_WITH_ZLIB=OFF \
            -DCMAKE_INSTALL_PREFIX="$DEPS/protoc" > protoc-cmake.log
        cmake --build protoc-build -j"$JOBS" --target protoc > protoc-build.log
        cmake --install protoc-build > protoc-install.log
    fi
    PROTOC_FLAG="--with-protoc=$DEPS/protoc/bin/protoc"
fi

# -- innoextract for the browser (unpacks GOG / Inno Setup installers) --------
# liblzma and the five Boost libraries innoextract links are compiled with
# em++ directly (Boost's own b2 emscripten toolset predates wasm objects),
# then innoextract is built as an ES module for a worker (web/inno-worker.js).
# innoextract and Boost report errors with C++ exceptions, hence
# -fwasm-exceptions everywhere (Emscripten turns throws into aborts otherwise).
INNO_VERSION=1.9
XZ_VERSION=5.4.6
BOOST_VERSION=1_83_0
SYSROOT=$(dirname "$(command -v emcc)")/cache/sysroot
if [ -z "${SKIP_INNOEXTRACT:-}" ] && [ ! -f "$DEPS/inno-build/innoextract.js" ]; then
    cd "$DEPS"
    embuilder build zlib bzip2 > embuilder.log 2>&1
    if [ ! -f "$DEPS/xz/lib/liblzma.a" ]; then
        say "building liblzma $XZ_VERSION for wasm"
        [ -f "xz-$XZ_VERSION.tar.gz" ] || curl -sSL -o "xz-$XZ_VERSION.tar.gz" "https://github.com/tukaani-project/xz/releases/download/v$XZ_VERSION/xz-$XZ_VERSION.tar.gz"
        [ -d "xz-$XZ_VERSION" ] || tar xzf "xz-$XZ_VERSION.tar.gz"
        emcmake cmake -S "xz-$XZ_VERSION" -B xz-build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
            -DXZ_TOOL_XZ=OFF -DXZ_TOOL_XZDEC=OFF -DXZ_TOOL_LZMADEC=OFF -DXZ_TOOL_LZMAINFO=OFF -DXZ_NLS=OFF \
            -DCMAKE_INSTALL_PREFIX="$DEPS/xz" > xz-cmake.log
        cmake --build xz-build -j"$JOBS" --target liblzma > xz-build.log
        cmake --install xz-build > xz-install.log
    fi
    if [ ! -f "$DEPS/boost/lib/libboost_iostreams.a" ] || [ ! -f "$DEPS/boost/lib/libboost_filesystem.a" ] || [ ! -f "$DEPS/boost/lib/libboost_program_options.a" ]; then
        say "building the Boost libraries innoextract needs for wasm"
        [ -f "boost_$BOOST_VERSION.tar.gz" ] || curl -sSL -o "boost_$BOOST_VERSION.tar.gz" "https://archives.boost.io/release/${BOOST_VERSION//_/.}/source/boost_$BOOST_VERSION.tar.gz"
        [ -d "boost_$BOOST_VERSION" ] || tar xzf "boost_$BOOST_VERSION.tar.gz"
        B="$DEPS/boost_$BOOST_VERSION"
        rm -rf "$DEPS/boost"; mkdir -p "$DEPS/boost/lib" "$DEPS/boost/include" "$DEPS/boost-obj"
        ln -sfn "$B/boost" "$DEPS/boost/include/boost"
        build_boost_lib() {
            local name=$1; shift; local objs=()
            for src in "$@"; do
                local o="$DEPS/boost-obj/${name}_$(basename "$src" .cpp).o"
                em++ -O2 -std=c++20 -fwasm-exceptions -I"$B" -DBOOST_ALL_NO_LIB -sUSE_ZLIB=1 -sUSE_BZIP2=1 -c "$B/$src" -o "$o"
                objs+=("$o")
            done
            emar rcs "$DEPS/boost/lib/libboost_$name.a" "${objs[@]}"
        }
        build_boost_lib system libs/system/src/error_code.cpp
        build_boost_lib date_time libs/date_time/src/gregorian/greg_month.cpp
        build_boost_lib filesystem $(cd "$B" && ls libs/filesystem/src/*.cpp | grep -v windows_file_codecvt)
        build_boost_lib program_options $(cd "$B" && ls libs/program_options/src/*.cpp | grep -v winmain)
        build_boost_lib iostreams libs/iostreams/src/file_descriptor.cpp libs/iostreams/src/mapped_file.cpp \
            libs/iostreams/src/zlib.cpp libs/iostreams/src/bzip2.cpp libs/iostreams/src/gzip.cpp
    fi
    say "building innoextract $INNO_VERSION for wasm"
    [ -f "innoextract-$INNO_VERSION.tar.gz" ] || curl -sSL -o "innoextract-$INNO_VERSION.tar.gz" "https://github.com/dscharrer/innoextract/releases/download/$INNO_VERSION/innoextract-$INNO_VERSION.tar.gz"
    [ -d "innoextract-$INNO_VERSION" ] || tar xzf "innoextract-$INNO_VERSION.tar.gz"
    INNO_LINK="-fwasm-exceptions -sUSE_ZLIB=1 -sUSE_BZIP2=1 -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createInnoextract -sENVIRONMENT=worker \
        -sINVOKE_RUN=0 -sEXIT_RUNTIME=0 -sEXPORTED_RUNTIME_METHODS=FS,callMain -sALLOW_MEMORY_GROWTH=1 \
        -sINITIAL_MEMORY=67108864 -sSTACK_SIZE=1048576 -sFORCE_FILESYSTEM=1"
    rm -rf inno-build
    # innoextract's build-time version script still says cmake_minimum_required 2.8
    export CMAKE_POLICY_VERSION_MINIMUM=3.5
    emcmake cmake -S "innoextract-$INNO_VERSION" -B inno-build -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=BOTH -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=BOTH \
        -DBOOST_ROOT="$DEPS/boost" -DBoost_INCLUDE_DIR="$DEPS/boost/include" -DBoost_LIBRARY_DIR="$DEPS/boost/lib" \
        -DBoost_NO_SYSTEM_PATHS=ON -DBoost_NO_BOOST_CMAKE=ON -DBoost_USE_STATIC_LIBS=ON \
        -DLZMA_INCLUDE_DIR="$DEPS/xz/include" -DLZMA_LIBRARY="$DEPS/xz/lib/liblzma.a" \
        -DZLIB_INCLUDE_DIR="$SYSROOT/include" -DZLIB_LIBRARY="$SYSROOT/lib/wasm32-emscripten/libz.a" \
        -DBZIP2_INCLUDE_DIR="$SYSROOT/include" -DBZIP2_LIBRARIES="$SYSROOT/lib/wasm32-emscripten/libbz2.a" \
        -DWITH_CONV=builtin -DSET_OPTIMIZATION_FLAGS=OFF -DUSE_LTO=OFF -DUSE_LDGOLD=OFF -DSET_WARNING_FLAGS=OFF \
        -DCMAKE_CXX_FLAGS="-O2 -fwasm-exceptions -sUSE_ZLIB=1 -sUSE_BZIP2=1 -DBOOST_ALL_NO_LIB" -DCMAKE_EXE_LINKER_FLAGS="$INNO_LINK" > inno-cmake.log
    cmake --build inno-build -j"$JOBS" > inno-build.log 2>&1 || { tail -30 inno-build.log; exit 1; }
fi

# -- DOSBox -------------------------------------------------------------------
if [ ! -x "$ROOT/configure" ] || [ "$ROOT/configure.ac" -nt "$ROOT/configure" ]; then
    say "generating configure"
    (cd "$ROOT" && ./autogen.sh > "$BUILD/autogen.log" 2>&1)
fi
mkdir -p "$BUILD"
cd "$BUILD"
if [ ! -f config.status ] || [ "$ROOT/configure" -nt config.status ]; then
    say "configuring"
    # shellcheck disable=SC2086
    emconfigure "$ROOT/configure" --with-protobuf="$DEPS/protobuf" $PROTOC_FLAG \
        --enable-funarray=no --disable-dynamic-core --disable-fpu-x86 --disable-opengl \
        --disable-screenshots ${CONFIGURE_FLAGS:-} > configure.log
fi
say "building dosbox (make -j$JOBS)"
emmake make -j"$JOBS" > make.log 2>&1 || { tail -40 make.log; exit 1; }

# -- lobbylink browser client -------------------------------------------------
TS=$ROOT/lobbylink/clients/ts
if [ ! -f "$TS/src/index.ts" ]; then
    echo "lobbylink submodule missing: run git submodule update --init" >&2
    exit 1
fi
if [ ! -f "$TS/dist/index.js" ] || [ "$TS/src/index.ts" -nt "$TS/dist/index.js" ]; then
    say "building the lobbylink TypeScript client"
    (cd "$TS" && { [ -d node_modules ] || npm ci --silent; } && npx tsc -p .)
fi

# -- the page -----------------------------------------------------------------
say "assembling $OUT"
mkdir -p "$OUT"
cp "$BUILD/src/dosbox.js" "$BUILD/src/dosbox.wasm" "$OUT/"
cp "$TS/dist/index.js" "$OUT/p2p-client.js"
cp "$ROOT/web/index.html" "$ROOT/web/wc.js" "$ROOT/web/gamefiles.js" "$ROOT/web/gamepad.js" "$ROOT/web/voice.js" "$ROOT/web/hall.js" "$ROOT/web/chatfilter.js" "$ROOT/web/inno-worker.js" "$ROOT/web/so3.js" "$ROOT/web/rvsprites.js" "$ROOT/web/rvsprites-worker.js" "$OUT/"
# Special Operations 3's sealed package (so3/README.md), as the page fetches it
mkdir -p "$OUT/so3" && cp "$ROOT/web/so3/so3.json" "$ROOT/web/so3/so3.enc" "$OUT/so3/"
if [ -f "$DEPS/inno-build/innoextract.js" ]; then
    cp "$DEPS/inno-build/innoextract.js" "$DEPS/inno-build/innoextract.wasm" "$OUT/"
else
    echo "note: no innoextract build; the page will only accept .zip game folders" >&2
fi

# RetroVega (retrovega/), our own game: built into the page, fetched when
# flown (the registry's `builtin` in web/gamefiles.js)
if [ -z "${SKIP_RETROVEGA:-}" ]; then
    say "building RetroVega"
    "$ROOT/retrovega/build.sh" > "$BUILD/retrovega.log" 2>&1 || { tail -20 "$BUILD/retrovega.log"; exit 1; }
    (cd "$ROOT/retrovega/build/dos" && tar --format=ustar -czf "$OUT/retrovega.tar.gz" \
        RV.EXE RV.DAT RVGAME.DAT BALLS.DAT SKY.DAT SHAPES.DAT RVOPL.BNK *.MID)
    # the ships' views (SHIPnn.RV, 1-9 MB each at 6 degrees) and the asteroid fields' rocks (ROCKS.DAT, 8.6 MB):
    # not in the tarball but beside it, gzipped, fetched by the page for the types a flight needs (and the rocks
    # when it has a field) when the game asks (RVWANT.TXT; web/wc.js rvWant),
    # with a manifest of what is there (the build, each file's size and hash: the page's cache keys)
    mkdir -p "$OUT/rv"
    for f in "$ROOT"/retrovega/art/ships/SHIP*.RV "$ROOT"/retrovega/build/dos/ROCKS.DAT; do
        g="$OUT/rv/$(basename "$f").gz"
        [ "$g" -nt "$f" ] || gzip -9 -c "$f" > "$g"
    done
    python3 - "$ROOT" "$OUT/rv" "$(git -C "$ROOT" rev-parse --short HEAD)" <<'PYEOF2'
import csv, hashlib, json, os, sys
root, out, build = sys.argv[1:4]
names = {r['type'].lower(): r['name'] for r in csv.DictReader(open(os.path.join(root, 'retrovega/data/ships.csv')))}
ships = {}
for f in sorted(os.listdir(os.path.join(root, 'retrovega/art/ships'))):
    if not f.startswith('SHIP') or not f.endswith('.RV'):
        continue
    data = open(os.path.join(root, 'retrovega/art/ships', f), 'rb').read()
    key = f[:-3]
    ships[key] = {'file': 'rv/' + f + '.gz', 'bytes': len(data), 'sha1': hashlib.sha1(data).hexdigest(),
                  'name': names.get('0x' + key[4:6].lower(), key)}
# the game's files a flight asks for as they are: an asteroid field's rocks (ROCKS.DAT, tools/rocks.py)
files = {}
for f in ['ROCKS.DAT']:
    data = open(os.path.join(root, 'retrovega/build/dos', f), 'rb').read()
    files[f] = {'file': 'rv/' + f + '.gz', 'bytes': len(data), 'sha1': hashlib.sha1(data).hexdigest()}
json.dump({'build': build, 'ships': ships, 'files': files}, open(os.path.join(out, 'ships.json'), 'w'), indent=1)
print('rv/ships.json: %d ships, %.1f MB of views (%.1f MB gzipped)' % (len(ships), sum(s['bytes'] for s in ships.values()) / 1e6,
      sum(os.path.getsize(os.path.join(out, s['file'][3:])) for s in ships.values()) / 1e6))
PYEOF2
fi

if [ -d "$WCDIR" ]; then
    if [ -n "${WCFILES:-}" ]; then
        # shellcheck disable=SC2206
        files=($WCFILES)
    else
        files=()
        while IFS= read -r f; do files+=("${f#./}"); done < <(
            cd "$WCDIR" && find . -maxdepth 1 -type f \( -iname '*.exe' -o -iname '*.cfg' -o -iname '*.bat' \
                -o -iname '*.com' -o -iname '*.dat' -o -iname '*.ovl' \) | sort)
        [ -d "$WCDIR/GAMEDAT" ] && files+=(GAMEDAT)
    fi
    if [ ${#files[@]} -eq 0 ]; then
        echo "no game files found in $WCDIR" >&2
        exit 1
    fi
    say "packaging ${#files[@]} entries from $WCDIR"
    tar -C "$WCDIR" --format=ustar -czf "$OUT/wc.tar.gz" "${files[@]}"
else
    echo "no game directory at $WCDIR (set WCDIR); web/dist has no wc.tar.gz" >&2
fi
ls -la "$OUT"
say "done. Serve it with: $ROOT/web/serve.py"
