/* rvhost.h: the DOS words the shared sources use, for gcc and clang (host/build.sh puts it ahead of every file with
 * -include).  The shared sources are the game's own (src/), built unchanged: what is DOS in them reads as plain C
 * here, and rvhooks.h puts the fixed block in rv_dgroup (host.c) because RV_HOST is defined. */
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
#define _fmalloc malloc
#define _ffree free
#define _fstrlen strlen
#define _fstrcpy strcpy
#define _fstrncpy strncpy
#define _fstrcmp strcmp
#define _fstrchr strchr

#endif
