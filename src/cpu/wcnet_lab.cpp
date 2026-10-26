// The walkthrough video's recorder: see wcnet_lab.h for the file it writes.
// Nothing here runs unless WCLAB names a directory.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include "wc_net.h"
#include "wcnet_lab.h"
#include "wcnet_memory.h"
#include "wcnet_code.h"
#include "wcnet_game.h"
#include "wcnet_vm.h"
#include "wcnet_log.h"
#include "cpu.h"
#include "regs.h"
#include "pic.h"
#include "mem.h"

bool wclab_on = false;

namespace {

FILE *g_rec = NULL;

void chunk(char type, const void *a, size_t alen, const void *b = NULL, size_t blen = 0) {
    if (!g_rec) return;
    Bit32u len = (Bit32u)(alen + blen);
    double now = PIC_FullIndex();
    fputc(type, g_rec);
    fwrite(&len, 4, 1, g_rec);
    fwrite(&now, 8, 1, g_rec);
    if (alen) fwrite(a, 1, alen, g_rec);
    if (blen) fwrite(b, 1, blen, g_rec);
}

void event(const std::string &json) {
    chunk('E', json.data(), json.size());
}

std::string hex(const Bit8u *p, size_t n) {
    std::string s;
    char b[4];
    for (size_t i = 0; i < n; i++) {
        snprintf(b, sizeof(b), i ? " %02X" : "%02X", p[i]);
        s += b;
    }
    return s;
}

struct Recorder {
    Recorder() {
        const char *dir = getenv("WCLAB");
        if (!dir || !dir[0]) return;
        std::string path = std::string(dir) + "/rec.bin";
        g_rec = fopen(path.c_str(), "wb");
        if (!g_rec) {
            fprintf(stderr, "WCLAB: cannot write %s\n", path.c_str());
            return;
        }
        setvbuf(g_rec, NULL, _IOFBF, 1 << 22);
        fwrite("WCLAB1\n", 1, 7, g_rec);
        wclab_on = true;
        event("{\"t\":\"start\"}");
    }
    ~Recorder() {
        if (g_rec) fclose(g_rec);
        g_rec = NULL;
    }
} g_recorder;

std::vector<Bit8u> g_lastPicture;

// One instruction as the debugger would show it, before it runs.
struct Insn {
    Bit16u cs, ip, ax, bx, cx, dx, si, di, bp, sp, ds, es, ss, flags;
    Bit8u bytes[8];
    Bit16u stack[6];
};

Insn snap() {
    Insn s;
    s.cs = (Bit16u)SegValue(cs);
    s.ip = (Bit16u)reg_eip;
    s.ax = reg_ax; s.bx = reg_bx; s.cx = reg_cx; s.dx = reg_dx;
    s.si = reg_si; s.di = reg_di; s.bp = reg_bp; s.sp = reg_sp;
    s.ds = (Bit16u)SegValue(::ds); s.es = (Bit16u)SegValue(es); s.ss = (Bit16u)SegValue(ss);
    s.flags = (Bit16u)reg_flags;
    for (int i = 0; i < 8; i++) s.bytes[i] = mem_readb(SegPhys(cs) + ((s.ip + i) & 0xffff));
    for (int i = 0; i < 6; i++) s.stack[i] = mem_readw(SegPhys(ss) + ((s.sp + 2 * i) & 0xffff));
    return s;
}

std::string insn_json(const Insn &s) {
    char buf[400];
    snprintf(buf, sizeof(buf),
             "\"cs\":\"%04X\",\"ip\":\"%04X\",\"bytes\":\"%s\",\"ax\":\"%04X\",\"bx\":\"%04X\",\"cx\":\"%04X\",\"dx\":\"%04X\","
             "\"si\":\"%04X\",\"di\":\"%04X\",\"bp\":\"%04X\",\"sp\":\"%04X\",\"ds\":\"%04X\",\"es\":\"%04X\",\"ss\":\"%04X\","
             "\"flags\":\"%04X\",\"stack\":\"%04X %04X %04X %04X %04X %04X\"",
             s.cs, s.ip, hex(s.bytes, 8).c_str(), s.ax, s.bx, s.cx, s.dx, s.si, s.di, s.bp, s.sp, s.ds, s.es, s.ss, s.flags,
             s.stack[0], s.stack[1], s.stack[2], s.stack[3], s.stack[4], s.stack[5]);
    return buf;
}

// Far return addresses up the chain of saved BPs (Borland frames).
std::string callers(Bit16u bp) {
    std::string chain;
    for (int i = 0; i < 10 && bp; i++) {
        char b[24];
        snprintf(b, sizeof(b), "%s\"%04X:%04X\"", i ? "," : "", mem_readw(SegPhys(ss) + ((bp + 4) & 0xffff)),
                 mem_readw(SegPhys(ss) + ((bp + 2) & 0xffff)));
        chain += b;
        Bit16u next = mem_readw(SegPhys(ss) + bp);
        if (next <= bp) break;
        bp = next;
    }
    return "[" + chain + "]";
}

// A trace: every instruction until a count runs out or the program is back
// at a place with the stack as it was (the end of an injected call).
long g_traceLeft = 0;
bool g_traceUntil = false;
Bit16u g_untilCs = 0, g_untilIp = 0, g_untilSp = 0;
long g_traceN = 0;
std::string g_traceName;

// A watch: writes to part of the data segment, with the instruction that
// made them and who called it.
int g_watchLen = 0, g_watchHits = 0;
Bit16u g_watchOff = 0;
Bit8u g_watchSeen[64];
Insn g_prev;
bool g_havePrev = false;

bool g_injectPending = false;
Bit16u g_lastDs = 0;
int g_lastFrame = -1;

void start_trace(const std::string &name, long count) {
    g_traceName = name;
    g_traceLeft = count;
    g_traceN = 0;
    wc_net_countdown = 0;
}

void inject_2016() {
    using namespace wc;
    const Loc &fn = code::maybe_fire_all_guns;
    if (!fn.known() || !DS) {
        event("{\"t\":\"inject2016\",\"error\":\"not known\"}");
        return;
    }
    Bit16u seg = fn.seg, off = fn.overlay ? fn.stubOff : fn.off;
    // NUL · pushf · pusha · xor si,si · push si · call far seg:off · pop si · popa · popf · retf
    const Bit8u thunk[15] = { 0x00, 0x9C, 0x60, 0x33, 0xF6, 0x56, 0x9A, (Bit8u)(off & 0xff), (Bit8u)(off >> 8),
                              (Bit8u)(seg & 0xff), (Bit8u)(seg >> 8), 0x5E, 0x61, 0x9D, 0xCB };
    Bit16u at = ds::aLoadingWingCom;
    Bit8u before[15];
    for (int i = 0; i < 15; i++) before[i] = rd8((Bit16u)(at + i));
    g_untilCs = (Bit16u)SegValue(cs);
    g_untilIp = (Bit16u)reg_eip;
    g_untilSp = reg_sp;
    CPU_Push16(g_untilCs);
    CPU_Push16(g_untilIp);
    for (int i = 0; i < 15; i++) mem_writeb(DS_OFF + at + i, thunk[i]);
    char buf[400];
    snprintf(buf, sizeof(buf),
             "{\"t\":\"inject2016\",\"at\":\"%04X:%04X\",\"before\":\"%s\",\"after\":\"%s\",\"return\":\"%04X:%04X\",\"sp\":\"%04X\","
             "\"call\":\"%04X:%04X\",\"overlay\":\"%04X\"}",
             DS, at, hex(before, 15).c_str(), hex(thunk, 15).c_str(), g_untilCs, g_untilIp, g_untilSp, seg, off,
             fn.overlay ? overlay_segment(fn.seg, fn.stubOff) : 0);
    event(buf);
    SegSet16(cs, DS);
    reg_eip = at + 1;
    g_traceUntil = true;
    start_trace("inject2016", 200000);
}

}  // namespace

void WCLAB_VideoFrame(Bitu width, Bitu height, Bitu bpp, Bitu pitch, const Bit8u *data, const Bit8u *pal) {
    if (!g_rec || bpp != 8 || width > 1024 || height > 1024) return;
    std::vector<Bit8u> pic(768 + width * height);
    for (int i = 0; i < 256; i++) {  // render.pal.rgb: r, g, b, unused
        pic[3 * i] = pal[4 * i];
        pic[3 * i + 1] = pal[4 * i + 1];
        pic[3 * i + 2] = pal[4 * i + 2];
    }
    for (Bitu y = 0; y < height; y++) memcpy(&pic[768 + y * width], data + y * pitch, width);
    if (pic == g_lastPicture) {
        chunk('S', NULL, 0);
        return;
    }
    g_lastPicture.swap(pic);
    Bit8u head[5] = { (Bit8u)(width & 0xff), (Bit8u)(width >> 8), (Bit8u)(height & 0xff), (Bit8u)(height >> 8), (Bit8u)bpp };
    chunk('V', head, 5, g_lastPicture.data(), g_lastPicture.size());
}

void WCLAB_Audio(Bit32u rate, Bitu frames, const Bit16s *stereo) {
    if (!g_rec || !frames) return;
    Bit32u head[2] = { rate, (Bit32u)frames };
    chunk('A', head, 8, stereo, frames * 4);
}

namespace wc {

void lab_game_frame() {
    if (!g_rec || !DS) return;
    int frame = rd16(ds::frameCounter);
    if (frame == g_lastFrame) return;
    g_lastFrame = frame;
    if (DS != g_lastDs) {
        g_lastDs = DS;
        char buf[160];
        snprintf(buf, sizeof(buf), "{\"t\":\"segments\",\"ds\":\"%04X\",\"load\":\"%04X\",\"mainLoopTop\":\"%04X:%04X\"}", DS, g_loadSeg,
                 code::mainLoopTop.seg, code::mainLoopTop.off);
        event(buf);
    }
    std::vector<Bit8u> seg(0x10000);
    for (Bit32u off = 0; off < 0x10000; off++) seg[off] = rd8((Bit16u)off);
    Bit16u head[3] = { (Bit16u)frame, DS, g_loadSeg };
    chunk('M', head, 6, seg.data(), seg.size());
}

void lab_after_frame_hooks() {
    if (!g_rec || !g_injectPending) return;
    if (g_trampoline.is_running() || !at_location(code::mainLoopTop)) return;
    g_injectPending = false;
    inject_2016();
}

bool lab_per_instruction() {
    return g_traceLeft > 0 || g_watchLen > 0;
}

void lab_instruction() {
    if (g_watchLen > 0) {
        for (int i = 0; i < g_watchLen; i++) {
            Bit8u now = rd8((Bit16u)(g_watchOff + i));
            if (now == g_watchSeen[i] || !g_havePrev) continue;
            Bit8u was[64], is[64];
            for (int j = 0; j < g_watchLen; j++) { was[j] = g_watchSeen[j]; is[j] = rd8((Bit16u)(g_watchOff + j)); }
            std::string json = "{\"t\":\"watch\",\"off\":\"" + std::to_string(g_watchOff) + "\",\"was\":\"" + hex(was, g_watchLen) +
                               "\",\"now\":\"" + hex(is, g_watchLen) + "\",\"writer\":{" + insn_json(g_prev) + "},\"callers\":" + callers(reg_bp) + "}";
            event(json);
            memcpy(g_watchSeen, is, g_watchLen);
            if (--g_watchHits <= 0) g_watchLen = 0;
            break;
        }
        g_prev = snap();
        g_havePrev = true;
    }
    if (g_traceLeft > 0) {
        Insn s = snap();
        if (g_traceUntil && s.cs == g_untilCs && s.ip == g_untilIp && s.sp == g_untilSp) {
            char buf[120];
            snprintf(buf, sizeof(buf), "{\"t\":\"trace-end\",\"name\":\"%s\",\"n\":%ld,\"back\":true}", g_traceName.c_str(), g_traceN);
            event(buf);
            g_traceLeft = 0;
            g_traceUntil = false;
            return;
        }
        event("{\"t\":\"insn\",\"name\":\"" + g_traceName + "\",\"n\":" + std::to_string(g_traceN) + "," + insn_json(s) + "}");
        g_traceN++;
        if (--g_traceLeft == 0) {
            event("{\"t\":\"trace-end\",\"name\":\"" + g_traceName + "\",\"n\":" + std::to_string(g_traceN) + "}");
            g_traceUntil = false;
        }
    }
}

bool lab_script_item(const std::string &item, double t) {
    if (!g_rec) return false;
    char buf[300];
    if (item.compare(0, 7, "!add32=") == 0 || item.compare(0, 7, "!set32=") == 0) {
        // !add32=<hex offset>:<decimal amount>, !set32=<hex offset>:<decimal value>
        Bit16u off = (Bit16u)strtol(item.c_str() + 7, NULL, 16);
        size_t colon = item.find(':');
        long v = colon == std::string::npos ? 0 : strtol(item.c_str() + colon + 1, NULL, 10);
        Bit32s was = (Bit32s)(rd16(off) | ((Bit32u)rd16((Bit16u)(off + 2)) << 16));
        Bit32s now = item[1] == 'a' ? was + (Bit32s)v : (Bit32s)v;
        for (int i = 0; i < 4; i++) wr8((Bit16u)(off + i), (Bit8u)((Bit32u)now >> (8 * i)));
        snprintf(buf, sizeof(buf), "{\"t\":\"poke32\",\"script\":%.3f,\"off\":%u,\"was\":%d,\"now\":%d}", t, off, was, now);
        event(buf);
        wclog(1, "lab %.1fs: dseg:%04X %d -> %d", t, off, was, now);
        return true;
    }
    if (item.compare(0, 7, "!pokes=") == 0) {
        // !pokes=<hex offset>:<hex bytes>, e.g. !pokes=B4B6:00010000
        Bit16u off = (Bit16u)strtol(item.c_str() + 7, NULL, 16);
        size_t colon = item.find(':');
        std::string bytes = colon == std::string::npos ? "" : item.substr(colon + 1);
        Bit8u was[64], now[64];
        int n = 0;
        for (size_t i = 0; i + 1 < bytes.size() && n < 64; i += 2, n++) {
            was[n] = rd8((Bit16u)(off + n));
            now[n] = (Bit8u)strtol(bytes.substr(i, 2).c_str(), NULL, 16);
            wr8((Bit16u)(off + n), now[n]);
        }
        snprintf(buf, sizeof(buf), "{\"t\":\"pokes\",\"script\":%.3f,\"off\":%u,\"was\":\"%s\",\"now\":\"%s\"}", t, off,
                 hex(was, n).c_str(), hex(now, n).c_str());
        event(buf);
        return true;
    }
    if (item.compare(0, 6, "!dump=") == 0) {
        std::string name = item.substr(6);
        std::vector<Bit8u> mem(0x100000);
        for (Bit32u a = 0; a < 0x100000; a++) mem[a] = mem_readb(a);
        chunk('X', name.c_str(), name.size() + 1, mem.data(), mem.size());
        event("{\"t\":\"dump\",\"name\":\"" + name + "\"}");
        return true;
    }
    if (item.compare(0, 6, "!mark=") == 0) {
        snprintf(buf, sizeof(buf), "{\"t\":\"mark\",\"script\":%.3f,\"name\":\"%s\"}", t, item.c_str() + 6);
        event(buf);
        wclog(1, "lab %.1fs: mark %s", t, item.c_str() + 6);
        return true;
    }
    if (item.compare(0, 7, "!trace=") == 0) {
        start_trace("trace", atol(item.c_str() + 7));
        return true;
    }
    if (item.compare(0, 7, "!watch=") == 0) {
        // !watch=<hex offset>:<length>[:<hits>]
        g_watchOff = (Bit16u)strtol(item.c_str() + 7, NULL, 16);
        size_t c1 = item.find(':');
        size_t c2 = c1 == std::string::npos ? c1 : item.find(':', c1 + 1);
        g_watchLen = c1 == std::string::npos ? 4 : atoi(item.c_str() + c1 + 1);
        if (g_watchLen > 64) g_watchLen = 64;
        g_watchHits = c2 == std::string::npos ? 1 : atoi(item.c_str() + c2 + 1);
        for (int i = 0; i < g_watchLen; i++) g_watchSeen[i] = rd8((Bit16u)(g_watchOff + i));
        g_havePrev = false;
        wc_net_countdown = 0;
        snprintf(buf, sizeof(buf), "{\"t\":\"watch-start\",\"script\":%.3f,\"off\":%u,\"len\":%d,\"hits\":%d}", t, g_watchOff, g_watchLen,
                 g_watchHits);
        event(buf);
        return true;
    }
    if (item == "!inject2016") {
        g_injectPending = true;
        return true;
    }
    return false;
}

}  // namespace wc
