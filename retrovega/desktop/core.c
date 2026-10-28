/* core.c: rv-core, RetroVega's game without a screen of its own (desktop/README.md).
 *
 * The game (src/ as RV.EXE has it, host/hle.c in DOS's place, the assembly's C twins) runs here; its screen,
 * its OPL3 and its input go over two pipes to the front end (rv, front.c), which has the window and the sound.
 * Without the pipes it runs headless (a test script drives it: /script FILE).  It reads and writes nothing but
 * those pipes and the files of its own directory (by plain DOS names: host/fsguard.c), and before the game
 * starts it shuts itself into a seccomp filter: the few system calls below, and any other one stops it with a
 * line saying which (SIGSYS).
 *
 *   rv-core [--data DIR] [--in FIFO --out FIFO] [--no-sandbox] [RV.EXE's options: /script FILE, /noquit, ...]
 *
 * --no-sandbox leaves the filter off (for a debugger, which the filter would stop).  The filter is seccomp.c's;
 * sandboxtest.c (rv-sandbox-test) shows it, and the bubblewrap sandbox, refusing. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include "rvhooks.h"
#include "hle.h"
#include "proto.h"

int rv_main(int argc, char **argv);
void rv_seccomp(const char *who);       /* (seccomp.c) */
extern void (*rv_fs_written)(const char *name);

static int g_in = -1, g_out = -1;      /* the pipes, from the front end and to it; -1: headless */

uint64_t plat_now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
}

/* a message to the front end, whole (a front end gone, its window closed, ends the game) */
static void put(int type, const void *data, uint32_t n)
{
    RvpHead h;
    const uint8_t *p = (const uint8_t *)&h;
    size_t left = sizeof h;
    int part;
    if (g_out < 0)
        return;
    memset(&h, 0, sizeof h);
    h.type = (uint8_t)type;
    h.len = n;
    for (part = 0; part < 2; part++) {
        while (left) {
            ssize_t k = write(g_out, p, left);
            if (k < 0 && errno == EINTR)
                continue;
            if (k <= 0)
                _exit(0);
            p += k;
            left -= (size_t)k;
        }
        p = (const uint8_t *)data;
        left = n;
    }
}

static RvpOpl g_opl[256];
static int g_nopl;

static void flush_opl(void)
{
    if (g_nopl)
        put(RVP_OPL, g_opl, (uint32_t)(g_nopl * sizeof g_opl[0]));
    g_nopl = 0;
}

void plat_opl_write(uint64_t at_us, uint16_t reg, uint8_t val)
{
    if (g_out < 0)
        return;
    memset(&g_opl[g_nopl], 0, sizeof g_opl[0]);
    g_opl[g_nopl].at_us = at_us;
    g_opl[g_nopl].reg = reg;
    g_opl[g_nopl].val = val;
    if (++g_nopl == 256)
        flush_opl();
}

void plat_pcm(uint64_t at_us, uint16_t rate, const uint8_t *s, uint16_t n)
{
    uint8_t m[sizeof(RvpPcm) + RVP_PCM_MAX];
    RvpPcm h;
    if (g_out < 0 || n > RVP_PCM_MAX)
        return;
    memset(&h, 0, sizeof h);
    h.at_us = at_us;
    h.rate = rate;
    h.n = n;
    memcpy(m, &h, sizeof h);
    memcpy(m + sizeof h, s, n);
    flush_opl();                        /* (in order with the music's) */
    put(RVP_PCM, m, (uint32_t)(sizeof h + n));
}

void plat_sleep_us(uint32_t us)
{
    struct timespec t;
    flush_opl();
    if (!us)
        return;
    t.tv_sec = us / 1000000u;
    t.tv_nsec = (long)(us % 1000000u) * 1000;
    nanosleep(&t, 0);
}

void plat_present(void)
{
    static uint8_t pal[768];
    static int sent;
    hle_fb_changed = 0;
    if (g_out < 0)
        return;
    flush_opl();
    if (!sent || memcmp(pal, hle_dac, sizeof pal)) {
        memcpy(pal, hle_dac, sizeof pal);
        put(RVP_PALETTE, pal, sizeof pal);
        sent = 1;
    }
    put(RVP_FRAME, hle_fb, RVP_MAX);
}

/* the front end's messages, as many as have come (the pipe does not wait): keys and the mouse to hle.c */
void plat_events(void)
{
    static uint8_t buf[4096];
    static size_t have;
    for (;;) {
        ssize_t k;
        size_t at = 0;
        if (g_in < 0)
            return;
        k = read(g_in, buf + have, sizeof buf - have);
        if (k == 0)
            exit(0);                    /* (the front end is gone: its window was closed) */
        if (k < 0)
            return;                     /* (EAGAIN: nothing more now) */
        have += (size_t)k;
        while (have - at >= sizeof(RvpHead)) {
            RvpHead h;
            memcpy(&h, buf + at, sizeof h);
            if (h.len > sizeof buf - sizeof h) {
                fprintf(stderr, "rv-core: a message of %u bytes from the front end: stopping\n", (unsigned)h.len);
                exit(2);
            }
            if (have - at < sizeof h + h.len)
                break;
            if (h.type == RVP_KEY && h.len == sizeof(RvpKey)) {
                RvpKey m;
                memcpy(&m, buf + at + sizeof h, sizeof m);
                hle_key(m.scan, m.down);
            } else if (h.type == RVP_MOUSE && h.len == sizeof(RvpMouse)) {
                RvpMouse m;
                memcpy(&m, buf + at + sizeof h, sizeof m);
                hle_mouse_at(m.x, m.y);
                if (m.kind == RVPM_BUTTON)
                    hle_mouse_button(m.button, m.down);
                else if (m.kind == RVPM_TAP)
                    hle_mouse_tap(0);
            }
            at += sizeof h + h.len;
        }
        memmove(buf, buf + at, have - at);
        have -= at;
    }
}

void plat_saved(void) {}

/* the layer's hooks (the thunks: gen/thunks.c): a flight's frame is shown */
void host_thunk_enter(uint16_t fn, const char *name, int16_t nargs, const uint16_t *args);
void host_thunk_leave(uint16_t fn, const char *name, int16_t nargs, const uint16_t *args, uint16_t ret);
void host_thunk_enter(uint16_t fn, const char *name, int16_t nargs, const uint16_t *args)
{
    (void)fn; (void)name; (void)nargs; (void)args;
}
void host_thunk_leave(uint16_t fn, const char *name, int16_t nargs, const uint16_t *args, uint16_t ret)
{
    (void)name; (void)nargs; (void)args; (void)ret;
    if (fn == RV_FN_rvFrameDone)
        hle_frame_done();
}

/* a file the game opened to write (fsguard.c tells each once): said, so it can be watched */
static void wrote(const char *name)
{
    if (g_out >= 0)
        put(RVP_WROTE, name, (uint32_t)strlen(name));
    else
        fprintf(stderr, "rv-core: the game wrote %s\n", name);
}

int main(int argc, char **argv)
{
    static char *gargv[64];
    const char *data = 0, *in = 0, *out = 0;
    int gargc = 0, i, r, filter = 1;
    gargv[gargc++] = argv[0];
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc)
            data = argv[++i];
        else if (!strcmp(argv[i], "--in") && i + 1 < argc)
            in = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc)
            out = argv[++i];
        else if (!strcmp(argv[i], "--no-sandbox"))
            filter = 0;
        else if (gargc < 63)
            gargv[gargc++] = argv[i];
    }
    if (data && chdir(data)) {
        perror(data);
        return 1;
    }
    if (!in != !out) {
        fprintf(stderr, "rv-core: --in and --out go together (the front end's pipes)\n");
        return 1;
    }
    if (out) {                          /* (the front end opens them the other way round: out, then in) */
        if ((g_out = open(out, O_WRONLY)) < 0 || (g_in = open(in, O_RDONLY)) < 0
            || fcntl(g_in, F_SETFL, O_NONBLOCK)) {
            perror("rv-core: the front end's pipes");
            return 1;
        }
    }
    signal(SIGPIPE, SIG_DFL);
    if (filter)
        rv_seccomp("rv-core");
    else
        fprintf(stderr, "rv-core: --no-sandbox: the seccomp filter is off\n");
    hle_init();
    rv_fs_written = wrote;
    gargv[gargc] = 0;
    r = rv_main(gargc, gargv);
    flush_opl();
    put(RVP_END, &r, sizeof r);
    return r;
}
