/* xms.c: one XMS arena (32 MB) carved into blocks by level (docs/engine.md).
 *
 * The arena is a single XMS block taken at start-up; blocks are handed out
 * from it bump-style, each tagged with a level, and freeing a level drops
 * every block of that level and above.  All copies go through the driver's
 * move (function 0Bh): DOSBox does it as one host-side copy, so even
 * conventional-to-video copies (handle 0 both ends) cost a few emulated
 * instructions.  The move wants an even length: xms_get splits an odd one;
 * xms_put and xms_copy_conv round it up (blocks are 16-byte rounded). */
#include <dos.h>
#include <i86.h>
#include "rv.h"

void (__far *rv_xms_entry)(void);

static unsigned short g_handle;
static unsigned long g_size, g_top;
static struct { unsigned long off, size; unsigned char level, used; } g_block[XMS_MAX_BLOCKS + 1];
static unsigned g_nblocks;

#pragma pack(1)
static struct {
    unsigned long length;
    unsigned short src_handle;
    unsigned long src_off;
    unsigned short dst_handle;
    unsigned long dst_off;
} g_move;
#pragma pack()

int xms_init(unsigned long want_kb)
{
    union REGS r;
    struct SREGS s;
    RvRegs x;
    r.x.ax = 0x4300;
    int86(0x2f, &r, &r);
    if (r.h.al != 0x80)
        return 0;
    segread(&s);
    r.x.ax = 0x4310;
    int86x(0x2f, &r, &r, &s);
    rv_xms_entry = (void (__far *)(void))MK_FP(s.es, r.x.bx);
    x.eax = 0x8800; x.ebx = x.ecx = x.edx = 0;
    rv_xms(&x);
    rv_log("XMS: %lu KB largest free, %lu KB in all", x.eax, x.edx);
    if (x.eax < want_kb)
        return 0;
    x.eax = 0x8900; x.edx = want_kb;
    rv_xms(&x);
    if ((x.eax & 0xffff) != 1) {
        rv_log("XMS: allocating %lu KB failed, error %02lx", want_kb, x.ebx & 0xff);
        return 0;
    }
    g_handle = (unsigned short)x.edx;
    g_size = want_kb * 1024UL;
    g_top = 0;
    g_nblocks = 0;
    rv_log("XMS: arena of %lu KB, handle %u", want_kb, g_handle);
    return 1;
}

void xms_shutdown(void)
{
    RvRegs x;
    if (!g_handle)
        return;
    x.eax = 0x0A00; x.edx = g_handle; x.ebx = x.ecx = 0;
    rv_xms(&x);
    g_handle = 0;
}

unsigned long xms_free_kb(void) { return (g_size - g_top) / 1024; }

XmsBlock xms_alloc(unsigned long bytes, int level)
{
    unsigned i;
    RV_CHECK(level, XMS_LEVELS);
    bytes = (bytes + 15) & ~15UL;
    if (g_top + bytes > g_size || g_nblocks >= XMS_MAX_BLOCKS) {
        rv_log("XMS: out of room for %lu bytes (level %d, %u blocks, %lu KB free)",
               bytes, level, g_nblocks, xms_free_kb());
        return 0;
    }
    /* levels nest: a block may not be taken below the deepest level in use */
    if (g_nblocks && g_block[g_nblocks].level > level)
        rv_fail(__FILE__, __LINE__, "XMS level order", level, g_block[g_nblocks].level);
    i = ++g_nblocks;
    g_block[i].off = g_top;
    g_block[i].size = bytes;
    g_block[i].level = (unsigned char)level;
    g_block[i].used = 1;
    g_top += bytes;
    return (XmsBlock)i;
}

void xms_free_level(int level)
{
    while (g_nblocks && g_block[g_nblocks].level >= level) {
        g_top = g_block[g_nblocks].off;
        g_block[g_nblocks].used = 0;
        g_nblocks--;
    }
}

static int move(void)
{
    RvRegs x;
    if (g_move.length & 1)
        g_move.length++;
    x.eax = 0x0B00; x.ebx = x.ecx = x.edx = 0;
    x.si = FP_OFF((void __far *)&g_move);
    rv_xms(&x);
    return (x.eax & 0xffff) == 1;
}

static void check(XmsBlock b, unsigned long off, unsigned long n)
{
    RV_CHECK(b - 1, g_nblocks);
    RV_ASSERT(g_block[b].used);
    if (off + n > g_block[b].size)
        rv_fail(__FILE__, __LINE__, "XMS range", (long)(off + n), (long)g_block[b].size);
}

int xms_put(XmsBlock b, unsigned long off, const void __far *src, unsigned long n)
{
    check(b, off, n);
    g_move.length = n;
    g_move.src_handle = 0;
    g_move.src_off = ((unsigned long)FP_SEG(src) << 16) | FP_OFF(src);
    g_move.dst_handle = g_handle;
    g_move.dst_off = g_block[b].off + off;
    return move();
}

int xms_get(XmsBlock b, unsigned long off, void __far *dst, unsigned long n)
{
    check(b, off, n);
    if (n & 1) {
        /* an odd length: never write past the caller's buffer */
        static unsigned char two[2];
        unsigned long last = off + n - 1;
        int ok = n > 1 ? xms_get(b, off, dst, n - 1) : 1;
        if (last + 2 <= g_block[b].size)
            ok &= xms_get(b, last, two, 2);
        else
            ok &= xms_get(b, last - 1, two, 2), two[0] = two[1];
        ((unsigned char __far *)dst)[n - 1] = two[0];
        return ok;
    }
    g_move.length = n;
    g_move.src_handle = g_handle;
    g_move.src_off = g_block[b].off + off;
    g_move.dst_handle = 0;
    g_move.dst_off = ((unsigned long)FP_SEG(dst) << 16) | FP_OFF(dst);
    return move();
}

int xms_copy_conv(void __far *dst, const void __far *src, unsigned long n)
{
    g_move.length = n;
    g_move.src_handle = 0;
    g_move.src_off = ((unsigned long)FP_SEG(src) << 16) | FP_OFF(src);
    g_move.dst_handle = 0;
    g_move.dst_off = ((unsigned long)FP_SEG(dst) << 16) | FP_OFF(dst);
    return move();
}
