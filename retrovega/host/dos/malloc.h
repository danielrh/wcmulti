/* malloc.h on the host: the C library's, and Watcom's far heap calls (host/hle.c: the heap is the host's, with
 * nothing of DOS's to give back or walk). */
#ifndef RV_HOST_MALLOC_H
#define RV_HOST_MALLOC_H

#include <stdlib.h>

struct _heapinfo { void *_pentry; size_t _size; int _useflag; };
#define _HEAPOK 0
#define _HEAPEND 5
#define _USEDENTRY 0
#define _FREEENTRY 1
int _fheapwalk(struct _heapinfo *hi);
int _fheapshrink(void);

#endif
