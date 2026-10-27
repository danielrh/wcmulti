/* opl3.h: the native build's OPL3 (opl3.cpp: ymfm's YMF262), for the front end (play.c). */
#ifndef RV_OPL3_H
#define RV_OPL3_H

#include <stdint.h>

double opl3_init(uint32_t lag_us);      /* the chip made; its sample rate returned */
void opl3_write(uint64_t at_us, uint16_t reg, uint8_t val);   /* a register, heard at at_us + the lag */
void opl3_render(int16_t *out, int n, uint64_t now_us);       /* n stereo frames, as of now_us */
unsigned long opl3_resyncs(void);

#endif
