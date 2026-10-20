/* rv.h: the engine's common declarations (docs/engine.md). */
#ifndef RV_H
#define RV_H

#include <stddef.h>

/* Bounds: every index into a fixed table goes through RV_CHECK; a failure
 * stops the game with where and what (rv_fail). */
void rv_fail(const char *file, int line, const char *what, long value, long limit);
#define RV_CHECK(i, n) do { if ((unsigned long)(i) >= (unsigned long)(n)) \
    rv_fail(__FILE__, __LINE__, #i " < " #n, (long)(i), (long)(n)); } while (0)
#define RV_ASSERT(c) do { if (!(c)) rv_fail(__FILE__, __LINE__, #c, 0, 0); } while (0)
#define RV_COUNT(a) (sizeof(a) / sizeof((a)[0]))

void rv_log(const char *fmt, ...);

/* 32-bit registers for the calls int86 cannot make (lowlevel.asm) */
typedef struct { unsigned long eax, ebx, ecx, edx; unsigned short si; } RvRegs;
void __far __cdecl rv_xms(RvRegs *r);

/* XMS (xms.c): blocks of extended memory, moved with the driver's function 0Bh.
 * A block belongs to a level and every level's blocks can be freed at once. */
enum { XMS_RESIDENT, XMS_BASE, XMS_ROOM, XMS_MISSION, XMS_LEVELS };
#define XMS_MAX_BLOCKS 48
typedef unsigned short XmsBlock;       /* index into the block table, 0 = none */
int xms_init(unsigned long want_kb);   /* allocates one arena of want_kb; 0 on failure */
unsigned long xms_free_kb(void);       /* free in the arena */
XmsBlock xms_alloc(unsigned long bytes, int level);
void xms_free_level(int level);        /* frees that level and all above it */
int xms_put(XmsBlock b, unsigned long off, const void __far *src, unsigned long n);
int xms_get(XmsBlock b, unsigned long off, void __far *dst, unsigned long n);
int xms_copy_conv(void __far *dst, const void __far *src, unsigned long n); /* host-speed copy */
void xms_shutdown(void);

#endif
