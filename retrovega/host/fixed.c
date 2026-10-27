/* fixed.c: DGROUP's stand-in on the host, rv_dgroup, with the fixed block at 0 (rvhooks.h's RV_AT), and the
 * near vectors handed out past it (world.h's RV_WHERE): shared by rvsim and the native build. */
#include "rvtypes.h"
#include "rvhooks.h"
#include "world.h"

/* DGROUP: the fixed block at 0 .. 0x7FFF as on DOS, then room for the near vectors rv_where hands out */
#define NEAR_RING 0x8000
#define NEAR_RING_N 64
uint8_t rv_dgroup[0x10000] __attribute__((aligned(65536)));   /* (on a 64 KB boundary: a pointer's low 16 bits are its
                                           DGROUP offset, as a near pointer is on DOS: `(unsigned short)(unsigned)p`) */

/* do_damage's `where` (world.h's RV_WHERE): a vector in the fixed block is its offset there, as on DOS; any other is
 * copied into DGROUP past the fixed block (a ring of them) and that copy's offset given: read it, never write it */
uint16_t rv_where(const RvVec3 *v)
{
    static int16_t next;
    RvVec3 *at;
    if ((const uint8_t *)v >= rv_dgroup && (const uint8_t *)v + sizeof *v <= rv_dgroup + RV_FIXED_BYTES)
        return (uint16_t)((const uint8_t *)v - rv_dgroup);
    at = (RvVec3 *)(rv_dgroup + NEAR_RING + next * sizeof(RvVec3));
    next = (int16_t)((next + 1) % NEAR_RING_N);
    *at = *v;
    return (uint16_t)((uint8_t *)at - rv_dgroup);
}

