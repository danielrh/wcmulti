/* front.c: rv, RetroVega's window, sound and input on a desktop (desktop/README.md).
 *
 * The game itself runs in rv-core (core.c), shut into its own directory; this shows what it sends over its pipe
 * (the screen, the palette, the OPL3's register writes: played through ymfm, host/opl3.cpp) and sends it the keys
 * and the mouse.  It opens no file of the game's and runs no program: its only other channels are SDL's (the
 * display, the audio device).  Each file the game says it opened to write is printed ("the game wrote ..."), so
 * what it writes can be watched while it runs.
 *
 *   rv --in FIFO --out FIFO [--scale N] [--mute]      (desktop/run.sh makes the pipes and starts both) */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <SDL.h>
#include "opl3.h"
#include "pcm.h"
#include "pcscan.h"
#include "proto.h"

static SDL_Window *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture *g_tex;
static SDL_AudioDeviceID g_audio;
static int g_mute, g_to = -1, g_from = -1;
static uint8_t g_dac[768];
static unsigned long g_frames;

static uint64_t now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);   /* (the core's clock too: the OPL writes' times are on it) */
    return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
}

/* --- to the core: its input, queued (the pipe never makes the window wait) --- */

static uint8_t g_q[16384];
static size_t g_qn;

static void flush_to_core(void)
{
    while (g_qn) {
        ssize_t k = write(g_to, g_q, g_qn);
        if (k <= 0)
            return;                     /* (EAGAIN: the rest at the next turn; a core gone shows on its pipe) */
        memmove(g_q, g_q + k, g_qn - (size_t)k);
        g_qn -= (size_t)k;
    }
}

static void send_core(int type, const void *data, uint32_t n)
{
    RvpHead h;
    if (g_qn + sizeof h + n > sizeof g_q)
        return;                         /* (the core has stopped reading: dropped) */
    memset(&h, 0, sizeof h);
    h.type = (uint8_t)type;
    h.len = n;
    memcpy(g_q + g_qn, &h, sizeof h);
    memcpy(g_q + g_qn + sizeof h, data, n);
    g_qn += sizeof h + n;
}

static uint8_t g_held[SDL_NUM_SCANCODES];

static void key(SDL_Scancode s, int down)
{
    RvpKey k;
    uint16_t pc = pc_scan(s);
    if (!pc || (!down && !g_held[s]))
        return;
    g_held[s] = (uint8_t)down;
    memset(&k, 0, sizeof k);
    k.scan = pc;
    k.down = (uint8_t)down;
    send_core(RVP_KEY, &k, sizeof k);
}

static void mouse(int kind, int x, int y, int button, int down)
{
    RvpMouse m;
    memset(&m, 0, sizeof m);
    m.x = (int16_t)(x < 0 ? 0 : x >= RVP_W ? RVP_W - 1 : x);
    m.y = (int16_t)(y < 0 ? 0 : y >= RVP_H ? RVP_H - 1 : y);
    m.kind = (uint8_t)kind;
    m.button = (uint8_t)button;
    m.down = (uint8_t)down;
    send_core(RVP_MOUSE, &m, sizeof m);
}

/* the window's events: the keys and the mouse to the core; 0 when the window is closed */
static int events(void)
{
    SDL_Event e;
    int s;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            return 0;
        case SDL_KEYDOWN:
            key(e.key.keysym.scancode, 1);      /* (its repeats too: the keyboard's typematic) */
            break;
        case SDL_KEYUP:
            key(e.key.keysym.scancode, 0);
            break;
        case SDL_MOUSEMOTION:
            mouse(RVPM_MOVE, e.motion.x, e.motion.y, 0, 0);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            if (e.button.which == SDL_TOUCH_MOUSEID) {      /* (a finger: a click, never the held button) */
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
                    mouse(RVPM_TAP, e.button.x, e.button.y, 0, 1);
                break;
            }
            if (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT)
                mouse(RVPM_BUTTON, e.button.x, e.button.y, e.button.button == SDL_BUTTON_LEFT ? 0 : 1,
                      e.type == SDL_MOUSEBUTTONDOWN);
            break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                for (s = 0; s < SDL_NUM_SCANCODES; s++)
                    if (g_held[s])
                        key((SDL_Scancode)s, 0);
            break;
        }
    }
    return 1;
}

/* --- from the core: the screen, the palette, the sound, what it wrote --- */

static void show(const uint8_t *fb)
{
    uint32_t pal[256];
    void *pixels;
    int pitch, i, y;
    g_frames++;
    if (!g_tex)
        return;
    for (i = 0; i < 256; i++) {
        const uint8_t *c = g_dac + 3 * i;
        pal[i] = 0xFF000000u | (uint32_t)((c[0] << 2 | c[0] >> 4) << 16) | (uint32_t)((c[1] << 2 | c[1] >> 4) << 8)
                 | (uint32_t)(c[2] << 2 | c[2] >> 4);
    }
    if (SDL_LockTexture(g_tex, 0, &pixels, &pitch))
        return;
    for (y = 0; y < RVP_H; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)pixels + y * pitch);
        const uint8_t *src = fb + y * RVP_W;
        int x;
        for (x = 0; x < RVP_W; x++)
            row[x] = pal[src[x]];
    }
    SDL_UnlockTexture(g_tex);
    SDL_RenderClear(g_ren);
    SDL_RenderCopy(g_ren, g_tex, 0, 0);
    SDL_RenderPresent(g_ren);
}

/* a name as fsguard.c lets the game open: letters, digits, _ - and one dot */
static int plain_name(const uint8_t *p, uint32_t n)
{
    uint32_t i, dots = 0;
    for (i = 0; i < n; i++) {
        uint8_t c = p[i];
        if (c == '.')
            dots++;
        else if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return 0;
    }
    return dots <= 1;
}

static uint8_t g_in[sizeof(RvpHead) + RVP_MAX + 4096];
static size_t g_inn;
static int g_ended = -1;                /* the game's exit status, once it has said it */

/* what the core has sent, as far as it has come; 0 when its pipe has closed */
static int from_core(void)
{
    size_t at = 0;
    int open = 1;
    for (;;) {
        ssize_t k = read(g_from, g_in + g_inn, sizeof g_in - g_inn);
        if (k == 0) {
            open = 0;                   /* (the core has ended: what it sent last is still to read) */
            break;
        }
        if (k < 0)
            break;                      /* (EAGAIN: no more now) */
        g_inn += (size_t)k;
        if (g_inn == sizeof g_in)
            break;
    }
    while (g_inn - at >= sizeof(RvpHead)) {
        RvpHead h;
        const uint8_t *p;
        memcpy(&h, g_in + at, sizeof h);
        if (h.len > RVP_MAX) {
            fprintf(stderr, "rv: a message of %u bytes from the game: stopping\n", (unsigned)h.len);
            return 0;
        }
        if (g_inn - at < sizeof h + h.len)
            break;
        p = g_in + at + sizeof h;
        if (h.type == RVP_PALETTE && h.len == sizeof g_dac) {
            memcpy(g_dac, p, sizeof g_dac);
        } else if (h.type == RVP_FRAME && h.len == RVP_MAX) {
            show(p);
        } else if (h.type == RVP_OPL && h.len % sizeof(RvpOpl) == 0) {
            uint32_t i;
            for (i = 0; i < h.len / sizeof(RvpOpl); i++) {
                RvpOpl o;
                memcpy(&o, p + i * sizeof o, sizeof o);
                opl3_write(o.at_us, o.reg, o.val);
            }
        } else if (h.type == RVP_PCM && h.len >= sizeof(RvpPcm) && h.len <= sizeof(RvpPcm) + RVP_PCM_MAX) {
            RvpPcm a;
            memcpy(&a, p, sizeof a);
            if (a.n == h.len - sizeof a && a.rate >= 8000 && a.rate <= 48000) {   /* (its own length, a sane rate) */
                if (g_audio)
                    SDL_LockAudioDevice(g_audio);
                pcm_put(a.at_us, a.rate, p + sizeof a, a.n);
                if (g_audio)
                    SDL_UnlockAudioDevice(g_audio);
            }
        } else if (h.type == RVP_WROTE && h.len > 0 && h.len <= 12) {
            if (plain_name(p, h.len))           /* (only a plain DOS name to the terminal: never what else it sent) */
                printf("rv: the game wrote %.*s\n", (int)h.len, (const char *)p);
            else
                printf("rv: the game says it wrote a file whose name is not a plain DOS name\n");
            fflush(stdout);
        } else if (h.type == RVP_END && h.len == 4) {
            int32_t r;
            memcpy(&r, p, sizeof r);
            g_ended = r;
        } else {
            fprintf(stderr, "rv: a message the game should not send ('%c', %u bytes): stopping\n", h.type,
                    (unsigned)h.len);
            return 0;
        }
        at += sizeof h + h.len;
    }
    memmove(g_in, g_in + at, g_inn - at);
    g_inn -= at;
    return open;
}

static double g_audio_rate;

static void audio_cb(void *user, Uint8 *stream, int len)
{
    uint64_t now = now_us();
    (void)user;
    opl3_render((int16_t *)stream, len / 4, now);
    pcm_mix((int16_t *)stream, len / 4, g_audio_rate, (double)now - 50000);   /* (the effects at the music's lag) */
    if (g_mute)
        memset(stream, 0, (size_t)len);
}

int main(int argc, char **argv)
{
    const char *to = 0, *from = 0;
    int i, scale = 2;
    SDL_AudioSpec want, have;
    double rate;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--in") && i + 1 < argc)
            to = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc)
            from = argv[++i];
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc)
            scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mute"))
            g_mute = 1;
        else {
            fprintf(stderr, "usage: rv --in FIFO --out FIFO [--scale N] [--mute]\n");
            return 2;
        }
    }
    if (!to || !from) {
        fprintf(stderr, "rv: --in and --out (the game's pipes: desktop/run.sh starts both)\n");
        return 2;
    }
    /* the pipes, in the order the core opens them (its out, then its in) */
    if ((g_from = open(from, O_RDONLY)) < 0 || (g_to = open(to, O_WRONLY)) < 0
        || fcntl(g_from, F_SETFL, O_NONBLOCK) || fcntl(g_to, F_SETFL, O_NONBLOCK)) {
        perror("rv: the game's pipes");
        return 1;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        fprintf(stderr, "rv: SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    g_win = SDL_CreateWindow("RetroVega", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, RVP_W * scale, RVP_H * scale,
                             SDL_WINDOW_RESIZABLE);
    if (g_win)
        g_ren = SDL_CreateRenderer(g_win, -1, 0);
    if (g_ren) {
        SDL_RenderSetLogicalSize(g_ren, RVP_W, RVP_H);
        g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, RVP_W, RVP_H);
    }
    if (!g_tex)
        fprintf(stderr, "rv: no screen (%s): running without one\n", SDL_GetError());
    SDL_ShowCursor(SDL_DISABLE);        /* (the game draws its own pointer) */
    rate = opl3_init(50000);
    g_audio_rate = rate;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        memset(&want, 0, sizeof want);
        want.freq = (int)(rate + 0.5);
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 1024;
        want.callback = audio_cb;
        if ((g_audio = SDL_OpenAudioDevice(0, 0, &want, &have, 0)))
            SDL_PauseAudioDevice(g_audio, 0);
        else
            fprintf(stderr, "rv: no sound (%s)\n", SDL_GetError());
    }
    for (;;) {
        struct pollfd pf = { g_from, POLLIN, 0 };
        poll(&pf, 1, 4);
        if (!from_core())
            break;
        if (!events())
            break;
        flush_to_core();
    }
    if (g_audio)
        SDL_CloseAudioDevice(g_audio);
    SDL_Quit();
    close(g_to);                        /* (the core reads the end of its input and stops) */
    if (g_ended >= 0) {
        printf("rv: the game ended (%d) after %lu frames\n", g_ended, g_frames);
        return g_ended;
    }
    if (g_inn || g_frames)
        printf("rv: the window was closed, or the game stopped (its own messages say which)\n");
    return 0;
}
