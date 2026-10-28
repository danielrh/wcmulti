# RetroVega on a Linux desktop, built and run so you can audit it

This directory builds RetroVega for a Linux desktop as two programs. Each runs in a sandbox of its own. A tool
checks what the build and the game actually do against a record made from a version you have reviewed.

| Program | What it is | What it can reach |
| --- | --- | --- |
| `rv-core` | The whole game: `src/` as RV.EXE has it, `host/hle.c` in DOS's place, and the assembly's C twins. 32-bit, linked against the C library only. | Its own directory and two pipes. Nothing else. |
| `rv` | Its window, sound and input: SDL2, ymfm's OPL3 (BSD, `host/ymfm`), and the sound effects the core mixes (`host/pcm.c`). About 450 lines of our own. | The display's socket and the sound server's. No files that outlive it. |

All of the game runs in `rv-core`: every rule, every data file it reads, every save. That's why it gets the
tightest box. `rv` never opens a game file; it shows what the core sends and passes the keys and the mouse back.

## Building, getting the data, playing

You need gcc with gcc-multilib (for `-m32`), g++, make, pkg-config, libsdl2-dev, bubblewrap and strace.

```sh
sudo apt install gcc gcc-multilib g++ make pkg-config libsdl2-dev bubblewrap strace   # Debian, Ubuntu
cd retrovega/desktop
./audit.sh build                    # the build, in its sandbox and traced (or plain `make`)
./fetch-data.sh ~/rv-files          # the game's data files, checked against pinned SHA-256s
make data DATA=~/rv-game FROM=~/rv-files   # those, and the ships' sprite sets made here from art/ships
./run.sh ~/rv-game                  # play
```

With a DOS build at hand (`../build.sh`), use `make data DATA=~/rv-game` on its own; `FROM` defaults to
`../build/dos`.

## What to read, in order

1. **`Makefile`**
   - Every source file is listed.
   - The only programs the build runs are the compiler, the assembler, the linker, pkg-config and mkdir.
   - Nothing is generated or downloaded while building. The two generated sources are checked in under `gen/`, and
     `make check-generated` shows they are exactly what `tools/rvabi.py` makes.
   - `host/ymfm/` is ymfm's source, unchanged; its README names the upstream commit.
2. **The sandboxes**, `policy.sh`: every directory, device and socket either program can see, one line each.
   Both programs get:
   - their own empty network namespace;
   - their own process and IPC namespaces;
   - no capabilities and nothing of your home directory;
   - the system read-only, and the sandbox's own root read-only too.
3. **The core's filter**:
   - `seccomp.c` and `syscalls.h` are the system calls the game may make once it starts. Any other call stops it.
   - `mmap2` is allowed only without `PROT_EXEC`, so the game can never make memory it could run code from.
   - `host/fsguard.c` is the file names it may open: plain DOS 8.3 names in its own directory. It also reports each
     file the game opens for writing.
4. **The boundary between the two programs**: `proto.h`, then `core.c` and `front.c`. The front end checks
   every message it reads from the core: its kind, its length and its bounds.
5. **`run.sh`**: how the two programs are started and connected.
6. **`audit.sh`**: what is checked, and how.

## The audit

`audit.sh` holds what happens now against `baseline/`. The baseline was recorded from the version checked in on
2026-10-27, on this repository's build machine. Each part prints `ok` or `DIFFERS`, and exits non-zero on any
difference.

| Command | What it does |
| --- | --- |
| `./audit.sh build` | Runs `make` in a sandbox: no network, the source read-only, only the output directory writable. Traced with strace, it lists every program the build ran (`baseline/build-programs.txt`), every file it wrote or changed (`build-outputs.txt`) and any network call. A write outside the output directory, even one the sandbox refused, is a difference. The programs' SHA-256s are compared with the recorded ones. Rebuilt on this machine, they are identical, because the build is reproducible. |
| `./audit.sh static` | Lists the library functions each program imports (`imports-*.txt`). Whatever the baseline says, `rv-core` must import nothing that opens a network connection, starts a process, uses ptrace, loads code (`dlopen`), changes memory protection (`mprotect`, `mmap`) or makes raw system calls (`syscall`). |
| `./audit.sh run` | Runs `rv-core` in its sandbox, headless and traced, through the walk (every room, a save, a launch and a flight) and a 20 s flight. It records every system call made once the game started (`core-syscalls.txt`: 11 calls) and every file written (`core-writes.txt`: its log, the save, the scripts' screenshots). It also checks that each written file is a plain name in its own directory, that no write was refused (its data files are read-only), and that there were no network, process or ptrace calls. |
| `./audit.sh forbidden` | Shows the sandboxes refusing. `rv-sandbox-test` (`sandboxtest.c`, the same filter as the core) inside the core's sandbox tries a network socket, starting `/bin/sh` and executable memory. The filter stops each one. It then writes to `/etc`, `/usr`, `/tmp` and `..` directly, past fsguard, and the sandbox refuses each one. A shell inside the front end's sandbox and inside the build's sandbox tries the internet, the machine's own ports, your home directory and writing to `/`, `/usr` and `/etc`, and can do none of it. |
| `./audit.sh watch ~/rv-game` | Plays the game with both programs traced. As it happens, it reports anything that isn't the game writing its own files: a write, or a refused attempt to write, anywhere else; any network call other than the display and sound sockets; a program being started; a system call the core's baseline doesn't have. Alerts are also kept in `../build/audit/watch/alerts.txt`. |
| `./audit.sh all` | Runs build, static, run and forbidden. |

**When the code changes.** Review the change, run `./audit.sh all`, and read every `DIFFERS` line. A new system
call or a new library function is something the change asks for. Once you accept it, record a new baseline:
`./audit.sh build --record`, `static --record`, `run --record`. Then commit the `baseline/` change along with the
code, so the record and the code it describes stay together.

**On your machine.** The program list, the imports and the hashes depend on your compiler and libraries, so a
different distribution shows differences on first use. Read them once, then record your own baseline from the
version you reviewed. The core's system calls hardly vary between glibc versions; `syscalls.h` already allows
both forms where they differ, such as `statx` and `fstat64`. `--record` refuses a part that went wrong (a failed
build, a game that didn't run to its end); `git checkout baseline/` brings back the checked-in record.

## What this guarantees, and what it doesn't

- **`rv-core`**
  - It can't use the network. Socket calls aren't on its list, and its network namespace is empty anyway.
  - It can't start a program or create executable memory, and it can't trace or touch another process.
  - It can write only in its own directory. Its data files there are read-only.
  - It sees nothing of your home directory.
  - These limits hold even if a bug in the game, or a crafted save or data file, took it over. The filter and the
    sandbox are the kernel's to enforce, not the game's.
- **`rv`, the front end**
  - It runs SDL, a display library and a GPU driver, so its system calls aren't filtered. Its limits are
    bubblewrap's: no network apart from the display and sound sockets, nothing it can write that outlives it, and
    no home directory.
  - Under X11, any program with the display's socket can read other windows and keystrokes on that display. That
    is how X11 works, and the front end needs the socket to show a window. Wayland keeps programs apart.
- **The build sandbox** keeps the build from touching your machine beyond its output directory. It can't tell
  you whether the compiler itself is honest; the toolchain, glibc, SDL2, bubblewrap and the kernel are the base
  you trust.
- **The traces** see only what a run exercised. What bounds everything else is the filter and the sandboxes,
  which `forbidden` tests.
- **Some containers** don't let bubblewrap mount a fresh `/proc`. The front end then runs without one, and says
  so. It is never given the host's.

## The files

| File | What it is |
| --- | --- |
| `Makefile` | The build. |
| `core.c` | rv-core's platform layer: the pipes, or headless. |
| `front.c` | rv. |
| `proto.h` | What the two programs say to each other. |
| `seccomp.c`, `syscalls.h` | The core's filter. |
| `sandboxtest.c` | The audit's test program: rv-core's filter, then one forbidden act. |
| `policy.sh` | The two sandboxes. |
| `run.sh` | Starts the two programs. |
| `audit.sh` | The audit. |
| `baseline/` | The audit's record. |
| `fetch-data.sh` | The game's data files, hash-checked. |
| `gen/` | `tools/rvabi.py`'s output, checked in. |

The shared code lives in `../host/`: `hle.c`, `fsguard.c`, `pcscan.h`, `opl3.cpp`, `ymfm/`. The design is in
`../docs/engine.md`, "The native build".
