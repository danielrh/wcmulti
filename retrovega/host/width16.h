/* width16.h: the width audit's (tools/width16.py, tests/host/width-audit.sh): DOS's int and unsigned as 16 bits,
 * and the checked casts to them.  A cast whose value does not fit is reported once a place (file, line, value):
 * there the native build keeps what DOS would have wrapped. */
#ifndef RV_WIDTH16_H
#define RV_WIDTH16_H

typedef short rv16_int;
typedef unsigned short rv16_uint;
void rv16_report(const char *file, int line, long value, int is_unsigned);
static inline rv16_int rv16i_at(long v, const char *f, int l)
{
    if (v < -32768L || v > 32767L)
        rv16_report(f, l, v, 0);
    return (rv16_int)v;
}
static inline rv16_uint rv16u_at(unsigned long v, const char *f, int l)
{
    if (v > 65535UL)
        rv16_report(f, l, (long)v, 1);
    return (rv16_uint)v;
}
#define rv16i(v) rv16i_at((long)(v), __FILE__, __LINE__)
#define rv16u(v) rv16u_at((unsigned long)(v), __FILE__, __LINE__)

#endif
