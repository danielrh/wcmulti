/* conio.h on the host: the I/O ports the game reads and writes (host/hle.c: the VGA's DAC and retrace, the OPL3,
 * the PIT, the PIC, the keyboard's data port; an MPU-401 that is not there). */
#ifndef RV_HOST_CONIO_H
#define RV_HOST_CONIO_H

unsigned inp(unsigned port);
unsigned outp(unsigned port, unsigned value);

#endif
