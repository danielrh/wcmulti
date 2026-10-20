/* M0 check: RV.EXE's image layout, the thunks and 32 MB of XMS.
 * Writes RVLOG.TXT and exits 0 when everything is where it should be. */
#include <stdio.h>
#include <string.h>
#include <dos.h>
#include <i86.h>
#include "rv.h"
#include "rvhooks.h"

extern unsigned char rv_fixed_block[];

void __far __cdecl rvHello_impl(void) { rv_log("rvHello: the layer can take DS here"); }

static unsigned short get_ds(void);
#pragma aux get_ds = "mov ax,ds" value [ax];
static unsigned short get_ss(void);
#pragma aux get_ss = "mov ax,ss" value [ax];

static unsigned char __far buf_a[4096], __far buf_b[4096];

int main(void)
{
    unsigned short ds = get_ds(), hookseg, dg;
    unsigned i;
    int ok = 1;
    XmsBlock b;
    rvHello();
    hookseg = FP_SEG((void __far *)rvHello);
    dg = *(unsigned short __far *)MK_FP(hookseg, RVHOOK_DGROUP_OFF);
    rv_log("DS=%04X SS=%04X RVHOOK=%04X, its DGROUP word=%04X, rvHello at %04X:%04X",
           ds, get_ss(), hookseg, dg, hookseg, FP_OFF((void __far *)rvHello));
    rv_log("fixed block at DS:%04X, puppet header '%.8s', signature '%s'",
           (unsigned)rv_fixed_block, (char *)rvPuppetHeader, (char *)aLoadingWingCom);
    if ((unsigned)rv_fixed_block != 0) { rv_log("fixed block not at DS:0"); ok = 0; }
    if (dg != ds) { rv_log("RVHOOK's DGROUP word is not DS"); ok = 0; }
    if (get_ss() != ds) { rv_log("SS != DS"); ok = 0; }
    if (FP_OFF((void __far *)rvHello) != RV_FN_rvHello) { rv_log("rvHello thunk offset"); ok = 0; }
    if (memcmp(rvPuppetHeader, "RVPUPPET", 8) != 0) { rv_log("puppet header"); ok = 0; }
    if (strcmp((char *)aLoadingWingCom, RV_SIGNATURE) != 0) { rv_log("signature"); ok = 0; }
    if (!xms_init(32768UL)) {
        rv_log("no 32 MB of XMS");
        ok = 0;
    } else {
        for (i = 0; i < sizeof buf_a; i++)
            buf_a[i] = (unsigned char)(i * 7 + 3);
        b = xms_alloc(32UL * 1024 * 1024 - 64, XMS_RESIDENT);   /* nearly all of it */
        xms_put(b, 32UL * 1024 * 1024 - 64 - sizeof buf_a, buf_a, sizeof buf_a);
        xms_get(b, 32UL * 1024 * 1024 - 64 - sizeof buf_a, buf_b, sizeof buf_b);
        if (_fmemcmp(buf_a, buf_b, sizeof buf_a) != 0) { rv_log("XMS round trip at the top"); ok = 0; }
        _fmemset(buf_b, 0, sizeof buf_b);
        xms_copy_conv(buf_b, buf_a, sizeof buf_a);
        if (_fmemcmp(buf_a, buf_b, sizeof buf_a) != 0) { rv_log("conventional move"); ok = 0; }
        rv_log("XMS: 32 MB arena, a 4 KB round trip at its top end, a conventional move: %s", ok ? "ok" : "FAILED");
        xms_shutdown();
    }
    rv_log("%s", ok ? "LAYOUT OK" : "LAYOUT WRONG");
    return ok ? 0 : 1;
}

/* RVHOOK's flight loop (rvflight.inc) names the engine's flight calls; HELLO
 * has no flight, so these stand in and are never called */
void __far __cdecl flight_startup(void) {}
unsigned short __far __cdecl flight_keys(void) { return 5; }
void __far __cdecl flight_frame(void) {}
void __far __cdecl flight_music(void) {}
void __far __cdecl flight_autopilot(void) {}
void __far __cdecl autoAnimation_c(unsigned short a, unsigned short b, unsigned short c) { (void)a; (void)b; (void)c; }
