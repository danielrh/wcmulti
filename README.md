DOSBox ported to Emscripten
===========================

This fork also carries a cooperative multiplayer layer for Wing Commander 1;
see [docs/wcnet-multiplayer.md](docs/wcnet-multiplayer.md) for the design,
the diagnosis of the damage desync, and `scripts/wcdis.py` for reading the
game's code with symbols.  It runs natively (TCP or a lobbylink room code,
see [wclobby/README.md](wclobby/README.md)) and **in a web browser**, where
the same room codes connect players over WebRTC: see
"Wing Commander in the browser" below.

Wing Commander in the browser
-----------------------------

```
git submodule update --init          # lobbylink (signaling server + browser client)
scripts/build-web.sh                 # deps, emconfigure, emmake, web/dist
web/serve.py                         # http://localhost:8000/
```

`scripts/build-web.sh` needs the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)
(`emcc` on `PATH`, or `EMSDK=/path/to/emsdk`), cmake, curl, autotools,
protoc, node/npm; Go is optional (only the smoke test builds a lobby
server).  It builds protobuf, liblzma, a few Boost libraries and
[innoextract](https://constexpr.org/innoextract/) for wasm once
(`build-web/deps`), configures DOSBox out of tree in `build-web/`, compiles
the lobbylink TypeScript client and assembles `web/dist/`: `dosbox.js` +
`dosbox.wasm`, `innoextract.js` + `innoextract.wasm`, `p2p-client.js` and
the page (`index.html`, `wc.js`, `gamefiles.js`, `inno-worker.js`).  If a
game directory exists (`./wc`, or `WCDIR=/path/to/game`) its executables,
`.CFG` files and `GAMEDAT/` are also packaged as `wc.tar.gz`, which the page
offers as "this server's copy"; that is for private hosting only.

**Bring your own game.**  The emulator has nothing to do with the game
binary: players drop either a `.zip` of their Wing Commander folder or the
GOG installer (`setup_wing_commander_*.exe`) onto the page.  Nothing is
uploaded: a zip is unpacked with the browser's `DecompressionStream`, an
installer is unpacked by innoextract running in a worker (any Inno Setup /
GOG installer, so this extends to other games), the game is recognised by
its executable (`web/gamefiles.js` keeps that registry: Wing Commander and
Wing Commander II with multiplayer on, and the programs that come in their
directories, The Secret Missions 2 and the two Special Operations, in a
"Play" menu; anything else offers a picker of the DOS executables found and
runs single-player), only the game directory is kept,
and the files are saved in the browser's IndexedDB so the next visit starts
right away ("Forget the saved copy" removes them).  If unpacking an
installer fails, the page says so and suggests `innoextract` + zip.

**The room.**  Enter a room code and a callsign and press Join: the page
itself joins the lobbylink room (the URL then carries the code, so send the
link and your wingmen land in the same room), shows who is in it and
whether their links are up, and has a chat.  Chat and presence travel over
the same WebRTC links the game will use, as reliable messages tagged with
a 4-byte prefix that the game protocol never produces, so the transport
(`src/wclobby_web.js`) filters them out.  The first pilot in is the host;
its Fly starts the game for everyone in the room (wingmen who already have
their game files start automatically, the others get a Fly button); a
wingman can also press Fly on its own, e.g. when the host is already
flying.  Starting goes full screen (Esc leaves it; the Full screen button
brings it back; Caps Lock is the game's Esc, for skipping cutscenes without
leaving full screen), and DOSBox adopts the page's room connection instead of
joining twice (`Module.lobbyGame`).  Only the canvas receives the
keyboard, so the chat box stays usable.  Native and browser players can
share a room, exactly like `runwc.sh DOSPATH room CODE`.

**Debugging voice.** Everyone in the room must opt in. Voice uses a separate
WebRTC audio connection, so working game sound and multiplayer do not prove
that voice works. Choose **Microphone** beside Voice to select the input;
Chrome reveals device names after microphone permission is granted. The choice
is saved in this browser. Changing microphones replaces the audio track while
keeping the established voice connection. An unavailable saved microphone is
shown explicitly; **System default** returns to Chrome's default input.
The microphone indicator names the selected input and shows
whether sound is detected. Open **Voice diagnostics** on both computers, speak,
and compare two reports a few seconds apart:

- The speaking computer needs a live, enabled microphone track and increasing
  `inputEnergy`. Otherwise check the selected input and microphone permissions
  in Chrome and macOS. A built-in MacBook microphone may be disconnected when
  the lid is closed; open the lid or use an external microphone.
- Increasing `sentBytes` on the speaker but no `receivedBytes` on the listener
  points to the voice connection/network path. `directions` should negotiate
  `sendrecv` when both choose Always. `route` identifies direct versus relay.
- Increasing `receivedEnergy` with blocked or paused `playback` calls for
  **Enable voice playback**. If playback is playing, check Chrome's site Sound
  permission, tab mute, and the computer's output device and volume.

Copy reports from both sides with **Copy voice report**; they omit recordings,
SDP, candidate addresses and TURN credentials. For deeper inspection, open
`chrome://webrtc-internals` before joining and look for audio RTP statistics.
The current **Force TURN relay** option applies to gameplay only, not voice.

`PLAYWRIGHT_DIR=build-web/playwright node scripts/web-voice-test.mjs` runs
voice-only tests with two Chrome contexts and a generated microphone tone;
no game files or lobby server are needed. Set `CHROME` to the browser executable
if needed. The tests retain normal autoplay policy and disable microphone
processing only for synthetic tones so echo cancellation cannot suppress them.
`scripts/web-smoke.sh voice` also checks voice alongside the running game.

**The public lobby.**  Pilots with nobody to fly with meet in a chat room
on the page (the lobbylink room `WC-LOBBY`, and `WC-LOBBY0`, `WC-LOBBY1`
and so on when it is full).  A pilot who opens the page is in it, unless a
link to a room brought the page or the pilot left the lobby before.  A
room's code starts with its game, `WC1-4821`, `WC2-`, `SM2-`, `SO1-`,
`SO2-`, and a code said in the lobby is a link: a click joins that room, if
the game loaded on the page is that game.  "Advertise" sends the pilot's
room (code, mission, free seats), and `/room` in a line is its code.
Nobody runs the lobby, so each page keeps its
rules for its own player and for what the others send: lines of 60
characters, two to start with and then one every ten seconds (one a second
while fewer than eight pilots are there), no profanity
(but hell: there is a Hellcat), no links.  `web/hall.js`,
`web/chatfilter.js`; docs/wcnet-multiplayer.md, section 6a.

**Which mission.**  The host picks it in the room: a system and mission
of the Vega campaign (everyone then skips the barracks and flies that
mission from a fresh start, with the callsign and last name entered on the
page, so no save game is involved and nothing can disagree), or "Campaign",
the barracks, where the host's save game and walk into the briefing room
decide and wingmen walk into their own briefing room to receive the host's
mission.  The choice is shown to the wingmen and travels with the start.
Natively the same thing is `MIS=<mission> SERIES=<series>` (the game's own
indices: series 1.., mission 0..).  A room is for two pilots unless the host
asks for a third seat.  When anyone dies, everyone flies that same mission
again, wherever the campaign had got to.

**Controllers.**  Pick a game controller in the room form (or press one of its
buttons while the window has the focus).  It drives the game's mouse and
keyboard: left stick turns and pitches, right stick pitches and rolls, A
guns, B missile, X target, Y weapon, L1/R1 speed up/down, Back the
navigation map, Start the autopilot, and the left trigger, pressed nearly
all the way, the afterburner.  Pitch is inverted by default (pull back to
climb); every button and axis can be changed under "Controller buttons and
axes".  Each browser window has its own controller, so two windows side by
side make a split screen, with a controller each or a controller in one and
the keyboard and mouse in the other.  With a controller chosen the game's
mouse crosshair is not drawn in flight (a box under the controller's
settings brings it back).

**Save games.**  Saving in a bunk writes the game's save file (all eight
bunks).  The page keeps a copy in the browser's local storage and puts it
back at the next visit; "Download save games" gives you the file, and
"Restore from a file" takes it back, in any browser.

How it works: the browser build is the same DOSBox and the same `wcnet_*`
code, compiled with Emscripten and
[Asyncify](https://emscripten.org/docs/porting/asyncify.html), so the
multiplayer transport can block for network data from inside the CPU core
(the wasm stack is unwound while the page's event loop delivers WebRTC
messages).  `wclobby/include/wclobby.h`, the C API the transport uses, is
implemented for the browser by `src/wclobby_web.js` on top of lobbylink's
browser client (`lobbylink/clients/ts`); natively it is the Rust crate in
`wclobby/`.  The page passes the client class in as `Module.P2PGame` and the
WCNET settings as environment variables (`WCROOM`, `WCLOBBY`, `WCPLAYERS`,
`WCCALLSIGN`, ...; any `?env.NAME=value` in the URL is passed through too,
e.g. `?env.MIS=1&env.SERIES=1` to jump into a mission).

**Lobby server origin.**  The lobby server checks the page's `Origin`.  The
public server at `https://pqrstuvw.xyz/lobbylink` only accepts pages served
from its own host, so either host `web/dist` there (next to the lobby, e.g.
`https://pqrstuvw.xyz/wc/`) or run your own server with the page's origin
allowed:

```
p2p-lobby-server --listen-http 127.0.0.1:8787 --public-url http://127.0.0.1:8787 \
                 --allowed-origin http://localhost:8000
```

and open `http://localhost:8000/?server=http://127.0.0.1:8787`.
`scripts/web-smoke.sh` does exactly that with two headless Chrome pages
(host + wingman flying mission 1) and prints both logs;
`NATIVE_HOST=1 scripts/web-smoke.sh` lets the native build host and a
browser page join it, the cross-play check; `GAME_SOURCE=zip
GAME_FILE=game.zip` or `GAME_SOURCE=gog GAME_FILE=setup_wing_commander_*.exe`
make the pages bring their own game files through the drop zone.

Browser notes: game saves live in the page's memory file system and are
lost when the tab closes; click the screen to give it the keyboard;
`Ctrl`+`F10` releases the mouse; sound starts because the Fly button click
counts as the user gesture browsers require.  The `output=surface`
renderer and the `simple` CPU core are used (both defaults of the
Emscripten build; the multiplayer hooks are in that core too).
`?cmd=ver` (or any DOS command) runs that instead of `wc`, and the emulated
clock is readable as `DOSBox._wc_web_emulated_ms()` in the console.
Tested in headless Chrome; Firefox and Safari have the same APIs
(Asyncify needs no special browser support) but were not tried.

About
-----

[DOSBox](http://www.dosbox.com/) is an open source DOS emulator designed for
running old games. [Emscripten](https://github.com/kripken/emscripten)
compiles C/C++ code to JavaScript. This is a version of DOSBox which can be
compiled with Emscripten to run in a web browser. It allows running old DOS
games and other DOS programs in a web browser.

DOSBox is distributed under the GNU General Public License. See the
[COPYING file](https://github.com/dreamlayers/em-dosbox/blob/em-dosbox-0.74/COPYING)
for more information.

Status
------

Em-DOSBox runs most games successfully in web browsers. Although DOSBox has
not been fully re-structured for running as an Emscripten main loop, most
functionality is available thanks to emterpreter sync. A few programs can
still run into problems due to paging exceptions.

Other issues
------------

* Game save files are written into the Emscripten file system, which is by
  default an in-memory file system. Saved games will be lost when you close
  the web page.
* Compiling in Windows is not supported. The build process requires a
  Unix-like environment due to use of GNU Autotools. See Emscripten
  [issue 2208](https://github.com/kripken/emscripten/issues/2208).
* Emscripten [issue 1909](https://github.com/kripken/emscripten/issues/1909)
used to make large switch statements highly inefficient. It seems fixed now,
but V8 JavaScript Engine [issue
2275](http://code.google.com/p/v8/issues/detail?id=2275) prevents large switch
statements from being optimized. Because of this, the simple, normal and
prefetch cores are automatically transformed. Case
statements for x86 instructions become functions, and an array of function
pointers is used instead of the switch statements. The `--enable-funarray`
configure option controls this and defaults to yes.
* The same origin policy prevents access to data files when running via a
file:// URL in some browsers. Use a web server such as
`python -m SimpleHTTPServer` instead.
* In Firefox, ensure that
[dom.max\_script\_run\_time](http://kb.mozillazine.org/Dom.max_script_run_time)
 is set to a reasonable value that will allow you to regain control in case of
a hang.
* Firefox may use huge amounts of memory when starting asm.js builds which have
not been minified.
* The FPU code uses doubles and does not provide full 80 bit precision.
DOSBox can only give full precision when running on an x86 CPU.

Compiling
---------

Use the latest stable version of Emscripten (from the master branch). For
more information see the the
[Emscripten installation instructions](https://kripken.github.io/emscripten-site/docs/getting_started/downloads.html).
Em-DOSBox depends on bug fixes and new features found in recent versions of
Emscripten. Some Linux distributions have packages with old versions, which
should not be used.

First, create `./configure` by running `./autogen.sh`. Then
configure with `emconfigure ./configure` and build with `make`.
This will create `src/dosbox.js` which contains DOSBox and `src/dosbox.html`,
a web page for use as a template by the packager. These cannot be used as-is.
You need to provide DOSBox with files to run and command line arguments for
running them.

This branch supports SDL 2 and uses it by default. Emscripten will
automatically fetch SDL 2 from Emscripten Ports and build it. Use of `make -j`
to speed up compilation by running multiple Emscripten processes in parallel
[may break this](https://github.com/kripken/emscripten/issues/3033).
Once SDL 2 has been built by Emscripten, you can use `make -j`.
To use a different pre-built copy of Emscripten SDL 2, specify a path as in
`emconfigure ./configure --with-sdl2=/path/to/SDL-emscripten`. To use SDL 1,
give a `--with-sdl2=no` or `--without-sdl2` argument to `./configure`.

Emscripten emterpreter sync is used by default. This enables more DOSBox
features to work, but requires an Emscripten version after 1.29.4 and may
cause a small performance penalty. To disable use of emterpreter sync,
add the `--disable-sync` argument to `./configure`. When sync is used,
the Emscripten
[memory initialization file](https://kripken.github.io/emscripten-site/docs/optimizing/Optimizing-Code.html#memory-initialization)
is enabled, which means `dosbox.html.mem` needs to be in the same folder as
`dosbox.js`. The memory initialization file is large. Serve it in compressed
format to save bandwidth.

Running DOS Programs
--------------------

To run DOS programs, you need to provide a suitable web page, load files into
the Emscripten file system, and give command line arguments to DOSBox. The
simplest method is by using the included packager tools.

The normal packager tool is `src/packager.py`, which runs the Emscripten
packager. It requires `dosbox.html`, which is created when building Em-DOSBox.
If you do not have Emscripten installed, you need to use `src/repackager.py`
instead. Any packager or repackager HTML output file can be used as a template
for the repackager. Name it `template.html` and put it in the same directory
as `repackager.py`.

The following instructions assume use of the normal packager. If using
repackager, replace `packager.py` with `repackager.py`. You need
[Python 2](https://www.python.org/downloads/) to run either packager.

If you have a single DOS executable such as `Gwbasic.exe`, place
it in the same `src` directory as `packager.py` and package it using:

```./packager.py gwbasic Gwbasic.exe```

This creates `gwbasic.html` and `gwbasic.data`. Placing those in the same
directory as `dosbox.js` and viewing `gwbasic.html` will run the program in a
web browser:

Some browsers have a same origin policy that prevents access to the required
data files while using `file://` URLs. To get around this you can use Python's
built in Really Simple HTTP Server and point the browser to
[http://localhost:8000](http://localhost:8000).

```python -m SimpleHTTPServer 8000```

If you need to package a collection of DOS files. Place all the files in a
single directory and package that directory with the executable specified. For
example, if Major Stryker's files are in the subdirectory `src/major_stryker`
and it's launched using `STRYKER.EXE` you would package it using:

```./packager.py stryker major_stryker STRYKER.EXE```

Again, place the created `stryker.html` and `stryker.data` files in the same
directory as `dosbox.js` and view `stryker.html` to run the game in browser.

You can also include a [DOSBox
configuration](http://www.dosbox.com/wiki/Dosbox.conf) file that will be
acknowledged by the emulator to modify any speed, audio or graphic settings.
Simply include a `dosbox.conf` text file in the package directory before you
run `./packager.py`.

To attempt to run Major Stryker in CGA graphics mode, you would create the
configuration file `/src/major_stryker/dosbox.conf` and include this body of
text:

```
[dosbox]
machine=cga
```

Then package it using:

```./packager.py stryker-cga major_stryker STRYKER.EXE```

Credits
-------

Most of the credit belongs to the
[DOSBox crew](http://www.dosbox.com/crew.php).
They created DOSBox and made it compatible with a wide variety of DOS games.
[Ismail Khatib](https://github.com/CeRiAl) got DOSBox
to compile with Emscripten, but didn't get it to work.
[Boris Gjenero](https://github.com/dreamlayers)
started with that and got it to work. Then, Boris re-implemented
Ismail's changes a cleaner way, fixed issues and improved performance to make
many games usable in web browsers. Meanwhile,
[Alon Zakai](https://github.com/kripken/) quickly fixed Emscripten bugs which
were encountered and added helpful Emscripten features.
