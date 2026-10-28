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

extern char host_last_rule[];           /* the last RULE line's text */
extern uint32_t host_bolt_hits[];       /* do_damage calls whose src was a bolt of this slot's (a hit by its gunfire) */
void host_counts_reset(void);           /* the thunk call counts below, and host_bolt_hits, back to 0 */
/* calls of thunk fn (RV_FN_*) since the reset: all of them (arg -1), or those whose argument arg (0 or 1) was value */
uint32_t host_calls(uint16_t fn, int16_t arg, int16_t value);

void host_rule(const char *fmt, ...) __attribute__((format(printf, 1, 2)));   /* a rule the layer needs broken: logged with the step (--strict: abort) */
void host_check_world(void);            /* after a step: the slot table's invariants */
uint32_t host_hash(void);               /* FNV-1a of the fixed block and the shot list: equal runs, equal hashes */

#endif
