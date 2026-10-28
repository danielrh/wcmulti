/* play.c: the native build's front end (docs/engine.md, "The native build"): an SDL2 window showing the VESA
 * screen through the DAC, the keys as the PC's scan codes, the mouse as the driver's pointer, the OPL3 (opl3.cpp)
 * on the audio device; then the game's own main (src/main.c, built as rv_main).
 *
 *   rv [--data DIR] [--scale N] [--mute] [RV.EXE's options: /script FILE, /noquit, ...]
 *
 * In the browser (Emscripten, ASYNCIFY) the same: the host's idling (plat_sleep_us) is the page's turn. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include "rvhooks.h"
#include "hle.h"
#include "opl3.h"
#include "pcm.h"
#include "pcscan.h"

int rv_main(int argc, char **argv);

static SDL_Window *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture *g_tex;
static SDL_AudioDeviceID g_audio;
static int g_mute;

uint64_t plat_now_us(void)
{
#ifdef __EMSCRIPTEN__
    return (uint64_t)(emscripten_get_now() * 1000.0);
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
#endif
}

void plat_sleep_us(uint32_t us)
{
#ifdef __EMSCRIPTEN__
    emscripten_sleep(us / 1000);        /* (0: a turn for the page, and back) */
#else
    struct timespec t;
    if (!us)
        return;
    t.tv_sec = us / 1000000u;
    t.tv_nsec = (long)(us % 1000000u) * 1000;
    nanosleep(&t, 0);
#endif
}

/* --- the screen --- */

void plat_present(void)
{
    uint32_t pal[256];
    void *pixels;
    int pitch, i, y;
    hle_fb_changed = 0;
    if (!g_tex)
        return;
    for (i = 0; i < 256; i++) {
        const uint8_t *c = hle_dac + 3 * i;
        pal[i] = 0xFF000000u | (uint32_t)((c[0] << 2 | c[0] >> 4) << 16) | (uint32_t)((c[1] << 2 | c[1] >> 4) << 8)
                 | (uint32_t)(c[2] << 2 | c[2] >> 4);
    }
    if (SDL_LockTexture(g_tex, 0, &pixels, &pitch))
        return;
    for (y = 0; y < HLE_H; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)pixels + y * pitch);
        const uint8_t *src = hle_fb + y * HLE_W;
        int x;
        for (x = 0; x < HLE_W; x++)
            row[x] = pal[src[x]];
    }
    SDL_UnlockTexture(g_tex);
    SDL_RenderClear(g_ren);
    SDL_RenderCopy(g_ren, g_tex, 0, 0);
    SDL_RenderPresent(g_ren);
}

static uint8_t g_held[SDL_NUM_SCANCODES];

static void key(SDL_Scancode s, int down)
{
    uint16_t pc = pc_scan(s);
    if (!pc)
        return;
    if (!down && !g_held[s])
        return;
    g_held[s] = (uint8_t)down;
    hle_key(pc, down);
}

static void release_all(void)
{
    int s;
    for (s = 0; s < SDL_NUM_SCANCODES; s++)
        if (g_held[s])
            key((SDL_Scancode)s, 0);
}

#ifdef __EMSCRIPTEN__
/* the page's own keys (its touch controls): a PC scan code (0x100 | code: a grey key) down or up */
EMSCRIPTEN_KEEPALIVE void rv_page_key(int pc, int down) { hle_key(pc, down); }
/* (for the page's tests) the mouse driver's buttons (bits 0, 1), its x at bit 8 and y at bit 20 */
EMSCRIPTEN_KEEPALIVE int rv_page_mouse(void) { int x, y, b = hle_mouse_state(&x, &y); return b | x << 8 | y << 20; }
#endif

void plat_events(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            exit(0);
        case SDL_KEYDOWN:
            key(e.key.keysym.scancode, 1);      /* (its repeats too: the keyboard's typematic) */
            break;
        case SDL_KEYUP:
            key(e.key.keysym.scancode, 0);
            break;
        case SDL_MOUSEMOTION:
            hle_mouse_at(e.motion.x, e.motion.y);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            hle_mouse_at(e.button.x, e.button.y);
            if (e.button.which == SDL_TOUCH_MOUSEID) {      /* (a finger: a click, never the held button) */
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
                    hle_mouse_tap(0);
                break;
            }
            hle_mouse_button(e.button.button == SDL_BUTTON_LEFT ? 0 : e.button.button == SDL_BUTTON_RIGHT ? 1 : -1,
                             e.type == SDL_MOUSEBUTTONDOWN);
            break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                release_all();
            if (e.window.event == SDL_WINDOWEVENT_EXPOSED || e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                hle_fb_changed = 1;
            break;
        }
    }
}

/* --- the sound --- */

void plat_opl_write(uint64_t at_us, uint16_t reg, uint8_t val) { opl3_write(at_us, reg, val); }

static double g_audio_rate;
static uint32_t g_lag_us;

#undef fopen
static FILE *rv_host_fopen_any(const char *path, const char *mode) { return fopen(path, mode); }
#define fopen rv_host_fopen

void plat_pcm(uint64_t at_us, uint16_t rate, const uint8_t *s, uint16_t n)
{
    static FILE *dump;
    static int tried;
    if (!tried++ && getenv("RV_PCM_DUMP"))   /* (a test aid: the effects' stream as it came, unsigned 8-bit; the
                                           host's own file, so not through fsguard's fopen, which is the game's) */
        dump = rv_host_fopen_any(getenv("RV_PCM_DUMP"), "wb");
    if (dump) {
        fwrite(s, 1, n, dump);
        fflush(dump);
    }
    if (g_audio)
        SDL_LockAudioDevice(g_audio);
    pcm_put(at_us, rate, s, n);
    if (g_audio)
        SDL_UnlockAudioDevice(g_audio);
}

static void audio_cb(void *user, Uint8 *stream, int len)
{
    uint64_t now = plat_now_us();
    (void)user;
    opl3_render((int16_t *)stream, len / 4, now);
    pcm_mix((int16_t *)stream, len / 4, g_audio_rate, (double)now - g_lag_us);   /* (the effects at the music's lag) */
    if (g_mute)
        memset(stream, 0, (size_t)len);
}

/* --- the layer's hooks: the thunks' (rvabi.py --host-thunks) --- */

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
    else if (fn == RV_FN_gameSaved)
        plat_saved();
}

void plat_saved(void)
{
#ifdef __EMSCRIPTEN__
    EM_ASM({ if (Module['rvSaved']) Module['rvSaved'](); });
#endif
}

int main(int argc, char **argv)
{
    static char *gargv[64];
    int gargc = 0, i, scale = 2, r;
    const char *data = 0;
    SDL_AudioSpec want, have;
    double rate;
    gargv[gargc++] = argv[0];
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc)
            data = argv[++i];
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc)
            scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mute"))
            g_mute = 1;
        else if (gargc < 63)
            gargv[gargc++] = argv[i];
    }
    if (data && chdir(data)) {
        perror(data);
        return 1;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER)) {
        fprintf(stderr, "rv: SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
#ifdef __EMSCRIPTEN__
    scale = 1;                          /* (the page's CSS sizes the canvas: its pixels are the game's) */
    g_win = SDL_CreateWindow("RetroVega", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, HLE_W, HLE_H, 0);
#else
    g_win = SDL_CreateWindow("RetroVega", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, HLE_W * scale, HLE_H * scale,
                             SDL_WINDOW_RESIZABLE);
#endif
    if (g_win)
        g_ren = SDL_CreateRenderer(g_win, -1, 0);
    if (g_ren) {
        SDL_RenderSetLogicalSize(g_ren, HLE_W, HLE_H);
        g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, HLE_W, HLE_H);
    }
    if (!g_tex)
        fprintf(stderr, "rv: no screen (%s): running without one\n", SDL_GetError());
    SDL_ShowCursor(SDL_DISABLE);        /* (the game draws its own pointer) */
#ifdef __EMSCRIPTEN__
    g_lag_us = 100000;
#else
    g_lag_us = 50000;
#endif
    rate = opl3_init(g_lag_us);
    g_audio_rate = rate;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        memset(&want, 0, sizeof want);
        want.freq = (int)(rate + 0.5);
        want.format = AUDIO_S16SYS;
        want.channels = 2;
#ifdef __EMSCRIPTEN__
        want.samples = 4096;
#else
        want.samples = 1024;
#endif
        want.callback = audio_cb;
        g_audio = SDL_OpenAudioDevice(0, 0, &want, &have, 0);
        if (!g_audio)
            fprintf(stderr, "rv: no sound (%s)\n", SDL_GetError());
    }
    hle_init();
    if (g_audio)
        SDL_PauseAudioDevice(g_audio, 0);
    gargv[gargc] = 0;
    r = rv_main(gargc, gargv);
    if (g_audio)
        SDL_CloseAudioDevice(g_audio);
    SDL_Quit();
    return r;
}
