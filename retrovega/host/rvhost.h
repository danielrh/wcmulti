/* rvhost.h: the DOS words the shared sources use, for gcc and clang (host/build.sh puts it ahead of every file with
 * -include).  The shared sources are the game's own (src/), built unchanged: what is DOS in them reads as plain C
 * here, and rvhooks.h puts the fixed block in rv_dgroup (fixed.c) because RV_HOST is defined.  The native build (host/game.sh,
 * RV_HOST_GAME) also has the far heap counted and the Watcom formats (%Fs: a far string) taken as the host's. */
#ifndef RVHOST_H
#define RVHOST_H

#define RV_HOST 1

#include <string.h>
#include <stdlib.h>

/* the memory model: one flat space, every pointer near and far at once */
#define __far
#define __near
#define __huge
#define __cdecl
#define __interrupt
#define _fmemset memset
#define _fmemcpy memcpy
#define _fmemmove memmove
#define _fmemcmp memcmp
#ifdef RV_HOST_GAME
#define _fmalloc hle_fmalloc            /* (counted: DOS's largest free block shrinks as the far heap grows, hle.c) */
#define _ffree hle_ffree
void *hle_fmalloc(size_t n);
void hle_ffree(void *p);
#else
#define _fmalloc malloc
#define _ffree free
#endif
#define _fstrlen strlen
#define _fstrcpy strcpy
#define _fstrncpy strncpy
#define _fstrcmp strcmp
#define _fstrchr strchr

#ifdef RV_HOST_GAME
/* Watcom's printf takes F and N before a conversion (a far, a near pointer: "%Fs"); the host's would read %F as a
 * double.  The game's formats go through these, which drop them (fmt.c) */
#include <stdio.h>
#include <stdarg.h>
int rv_host_sprintf(char *s, const char *fmt, ...);
int rv_host_vfprintf(FILE *f, const char *fmt, va_list ap);
#define sprintf rv_host_sprintf
#define vfprintf rv_host_vfprintf
/* the game's files: plain DOS names in its own directory, nothing else (fsguard.c) */
FILE *rv_host_fopen(const char *name, const char *mode);
int rv_host_open(const char *name, int flags, ...);
#define fopen rv_host_fopen
#endif

#endif
