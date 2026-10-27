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

/* --- the keys: SDL's scan codes as the PC keyboard's (set 1; 0x100 | code for the grey keys, sent after E0) --- */

static uint16_t pc_scan(SDL_Scancode s)
{
    static const uint8_t letters[26] = { 0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
                                         0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C };
    static const uint8_t keypad[10] = { 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49, 0x52 };   /* 1 .. 9, 0 */
    if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) return letters[s - SDL_SCANCODE_A];
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_0) return (uint16_t)(0x02 + (s - SDL_SCANCODE_1));
    if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F10) return (uint16_t)(0x3B + (s - SDL_SCANCODE_F1));
    if (s >= SDL_SCANCODE_KP_1 && s <= SDL_SCANCODE_KP_0) return keypad[s - SDL_SCANCODE_KP_1];
    switch (s) {
    case SDL_SCANCODE_RETURN: return 0x1C;
    case SDL_SCANCODE_ESCAPE: return 0x01;
    case SDL_SCANCODE_BACKSPACE: return 0x0E;
    case SDL_SCANCODE_TAB: return 0x0F;
    case SDL_SCANCODE_SPACE: return 0x39;
    case SDL_SCANCODE_MINUS: return 0x0C;
    case SDL_SCANCODE_EQUALS: return 0x0D;
    case SDL_SCANCODE_LEFTBRACKET: return 0x1A;
    case SDL_SCANCODE_RIGHTBRACKET: return 0x1B;
    case SDL_SCANCODE_BACKSLASH: case SDL_SCANCODE_NONUSHASH: return 0x2B;
    case SDL_SCANCODE_SEMICOLON: return 0x27;
    case SDL_SCANCODE_APOSTROPHE: return 0x28;
    case SDL_SCANCODE_GRAVE: return 0x29;
    case SDL_SCANCODE_COMMA: return 0x33;
    case SDL_SCANCODE_PERIOD: return 0x34;
    case SDL_SCANCODE_SLASH: return 0x35;
#ifdef __EMSCRIPTEN__
    case SDL_SCANCODE_CAPSLOCK: return 0x01;   /* (the game's Esc: the browser keeps Esc for leaving full screen) */
#else
    case SDL_SCANCODE_CAPSLOCK: return 0x3A;
#endif
    case SDL_SCANCODE_F11: return 0x57;
    case SDL_SCANCODE_F12: return 0x58;
    case SDL_SCANCODE_SCROLLLOCK: return 0x46;
    case SDL_SCANCODE_NUMLOCKCLEAR: return 0x45;
    case SDL_SCANCODE_KP_MULTIPLY: return 0x37;
    case SDL_SCANCODE_KP_MINUS: return 0x4A;
    case SDL_SCANCODE_KP_PLUS: return 0x4E;
    case SDL_SCANCODE_KP_PERIOD: return 0x53;
    case SDL_SCANCODE_NONUSBACKSLASH: return 0x56;
    case SDL_SCANCODE_LCTRL: return 0x1D;
    case SDL_SCANCODE_LSHIFT: return 0x2A;
    case SDL_SCANCODE_LALT: return 0x38;
    case SDL_SCANCODE_RSHIFT: return 0x36;
    case SDL_SCANCODE_RCTRL: return 0x11D;
    case SDL_SCANCODE_RALT: return 0x138;
    case SDL_SCANCODE_INSERT: return 0x152;
    case SDL_SCANCODE_HOME: return 0x147;
    case SDL_SCANCODE_PAGEUP: return 0x149;
    case SDL_SCANCODE_DELETE: return 0x153;
    case SDL_SCANCODE_END: return 0x14F;
    case SDL_SCANCODE_PAGEDOWN: return 0x151;
    case SDL_SCANCODE_RIGHT: return 0x14D;
    case SDL_SCANCODE_LEFT: return 0x14B;
    case SDL_SCANCODE_DOWN: return 0x150;
    case SDL_SCANCODE_UP: return 0x148;
    case SDL_SCANCODE_KP_DIVIDE: return 0x135;
    case SDL_SCANCODE_KP_ENTER: return 0x11C;
    default: return 0;
    }
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

static void audio_cb(void *user, Uint8 *stream, int len)
{
    (void)user;
    opl3_render((int16_t *)stream, len / 4, plat_now_us());
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
    rate = opl3_init(100000);
#else
    rate = opl3_init(50000);
#endif
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
