/* host.h: the host harness's own declarations (host.c, sim.c; docs/logic.md "The host harness") */
#ifndef RVSIM_HOST_H
#define RVSIM_HOST_H

#include <stdio.h>
#include "rvtypes.h"

#define HOST_W 640                      /* the screen, as the game's (SCR_W x SCR_H): a byte a pixel, the palette's */
#define HOST_H 400
extern uint8_t host_screen[HOST_H * HOST_W];

extern FILE *host_log;                  /* where rv_log writes: stderr unless --log */
extern int host_trace;                  /* --trace: every thunk call logged */
extern int host_check_src;              /* --check-src: each do_damage also tried with src 0xFFFF, the results compared */
extern int host_strict;                 /* --strict: a broken rule stops the run (abort: a debugger stops there) */
extern uint32_t host_ticks;             /* the BIOS clock (18.2 a second), counted from the steps: the same each run */
extern uint32_t host_rules_broken;

void host_rule(const char *fmt, ...) __attribute__((format(printf, 1, 2)));   /* a rule the layer needs broken: logged with the step (--strict: abort) */
void host_check_world(void);            /* after a step: the slot table's invariants */
uint32_t host_hash(void);               /* FNV-1a of the fixed block and the shot list: equal runs, equal hashes */

#endif
