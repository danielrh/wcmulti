/* io.h on the host: POSIX's file calls, as Watcom's <io.h> has them. */
#ifndef RV_HOST_IO_H
#define RV_HOST_IO_H

#include <unistd.h>
#include <fcntl.h>
#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifdef RV_HOST_GAME
#define open rv_host_open               /* (the game's files: fsguard.c) */
#endif

#endif
