# Sourced by game.sh and build.sh (32-bit, SDL on): sets PC, the pkg-config that finds the i386 SDL2.
# Where it looks: SDL32_PC (a pkgconfig directory), then the system's i386 libsdl2-dev, then the one DEPS=1 unpacks
# into $SDL32_HOME (default ~/.local/sdl2-i386) from the libsdl2-dev:i386 .deb without installing it: installing it
# makes apt remove the amd64 dev packages' dependents (gdm3, gnome-shell, python3-*...).  That one needs the i386
# runtime (sudo apt install libsdl2-2.0-0:i386) and gcc-multilib.
SDL32_HOME=${SDL32_HOME:-$HOME/.local/sdl2-i386}
SDL32_SYS=/usr/lib/i386-linux-gnu/pkgconfig:/usr/lib32/pkgconfig
SDL32_LOCAL=$SDL32_HOME/usr/lib/i386-linux-gnu/pkgconfig
sdl32_has() { env PKG_CONFIG_LIBDIR="$1" pkg-config --exists sdl2 2> /dev/null; }
sdl32_unpack() {
    [ -e /usr/lib/i386-linux-gnu/libSDL2-2.0.so.0 ] || {
        echo "no i386 SDL2 runtime: sudo apt install libsdl2-2.0-0:i386 (dpkg --add-architecture i386 first if need be)"
        return 1; }
    T=$(mktemp -d)
    (cd "$T" && apt-get download libsdl2-dev:i386) || { rm -rf "$T"; return 1; }
    mkdir -p "$SDL32_HOME"
    dpkg -x "$T"/libsdl2-dev_*_i386.deb "$SDL32_HOME"
    rm -rf "$T"
    L=$SDL32_HOME/usr/lib/i386-linux-gnu
    # the package's links point at the runtime beside them; the runtime is the system's
    rm -f "$L/libSDL2.so" "$L/libSDL2-2.0.so"
    ln -s /usr/lib/i386-linux-gnu/libSDL2-2.0.so.0 "$L/libSDL2.so"
    sed -i "s|^prefix=.*|prefix=$SDL32_HOME/usr|; s|^libdir=.*|libdir=$L|; s|^Cflags:.*|Cflags: -I\${includedir}/SDL2 -I$SDL32_HOME/usr/include/i386-linux-gnu -D_REENTRANT|" "$L/pkgconfig/sdl2.pc"
}
if [ -n "${SDL32_PC:-}" ]; then
    :
elif sdl32_has "$SDL32_SYS"; then
    SDL32_PC=$SDL32_SYS
else
    if ! sdl32_has "$SDL32_LOCAL" && [ -n "${DEPS:-}" ]; then
        sdl32_unpack || exit 1
    fi
    sdl32_has "$SDL32_LOCAL" || {
        echo "no 32-bit SDL2 (libsdl2-dev:i386) found.  DEPS=1 $0 fetches it into $SDL32_HOME without"
        echo "installing it; or SDL32_PC=/dir/of/sdl2.pc, or (headless, host/build.sh only) SDL=0"
        exit 1; }
    SDL32_PC=$SDL32_LOCAL
fi
PC="env PKG_CONFIG_LIBDIR=$SDL32_PC pkg-config"
