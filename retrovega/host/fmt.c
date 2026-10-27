/* fmt.c: the game's printf formats as the host's printf takes them (rvhost.h, the native build): Watcom's F and N
 * (a far, a near pointer: "%Fs", "%Np") dropped; l stays (a long: 32 bits on DOS and on the 32-bit host).  Built
 * without rvhost.h (its sprintf and vfprintf are these). */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static void host_format(char *out, size_t n, const char *fmt)
{
    size_t i = 0;
    while (*fmt && i + 1 < n) {
        if ((out[i++] = *fmt++) != '%')
            continue;
        while (*fmt && strchr("-+ #0123456789.*", *fmt) && i + 1 < n)
            out[i++] = *fmt++;
        while (*fmt == 'F' || *fmt == 'N')
            fmt++;
        if (*fmt && i + 1 < n)
            out[i++] = *fmt++;          /* (the conversion, or l, h: copied, the rest follows as text) */
    }
    out[i] = 0;
}

int rv_host_vfprintf(FILE *f, const char *fmt, va_list ap)
{
    char hf[1024];
    host_format(hf, sizeof hf, fmt);
    return vfprintf(f, hf, ap);
}

int rv_host_sprintf(char *s, const char *fmt, ...)
{
    char hf[1024];
    va_list ap;
    int n;
    host_format(hf, sizeof hf, fmt);
    va_start(ap, fmt);
    n = vsprintf(s, hf, ap);
    va_end(ap);
    return n;
}
