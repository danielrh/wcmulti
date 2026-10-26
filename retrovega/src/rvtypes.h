/* rvtypes.h: the sized integer types (int16_t, uint32_t, ...) the game's logic and the code it shares with the host
 * harness (host/, docs/logic.md "The host harness") are written in, so a value keeps its size on every compiler: on
 * DOS int16_t is Watcom's int and int32_t its long; on a 32- or 64-bit host they stay 16 and 32 bits where int and
 * long would not.  The arithmetic still differs (C works in int, 32 bits on a host: docs/logic.md says where).
 *
 * Open Watcom and every C99 compiler have <stdint.h>; the DOS compilers before it (Watcom 11 and older, Turbo and
 * Borland C, Microsoft C before 2010), or RV_NO_STDINT, take these, written for a 16-bit int. */
#ifndef RVTYPES_H
#define RVTYPES_H

#if defined(RV_NO_STDINT) || (defined(__WATCOMC__) && __WATCOMC__ < 1200) || defined(__TURBOC__) \
    || (defined(_MSC_VER) && _MSC_VER < 1600)
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef int int16_t;
typedef unsigned int uint16_t;
typedef long int32_t;
typedef unsigned long uint32_t;
#define INT16_MIN (-32767 - 1)
#define INT16_MAX 32767
#define UINT16_MAX 65535U
#define INT32_MIN (-2147483647L - 1)
#define INT32_MAX 2147483647L
#define UINT32_MAX 4294967295UL
#define INT32_C(x) x##L
#define UINT32_C(x) x##UL
#else
#include <stdint.h>
#endif

#endif
