/* dos.h on the host: Watcom's <dos.h> as far as the game uses it (host/hle.c answers). */
#ifndef RV_HOST_DOS_H
#define RV_HOST_DOS_H

#include <fcntl.h>
#include "i86.h"

typedef void (*hle_isr)(void);
hle_isr _dos_getvect(int n);
void _dos_setvect(int n, hle_isr isr);
void _chain_intr(hle_isr isr);          /* the handler before: called (on DOS a jump that never comes back) */

unsigned _dos_open(const char *path, unsigned mode, int *fd);
unsigned _dos_read(int fd, void *buf, unsigned n, unsigned *got);
unsigned _dos_close(int fd);

#endif
