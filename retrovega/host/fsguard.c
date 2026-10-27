/* fsguard.c: every file the game opens on the host goes through here (rvhost.h makes the game's fopen and open
 * these; hle.c's _dos_open calls them).  The game only ever opens files in its own directory by plain DOS names
 * (RV.DAT, SHIP0B.RVS, BUNK01.SAV, RVLOG.TXT, ...), so any other name is refused: a path, "..", a hidden or long
 * name, a device.  Each file opened to write is told to rv_fs_written once (desktop/core.c reports it, so what
 * the game writes can be watched while it runs; desktop/README.md). */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#undef fopen
#undef open

void (*rv_fs_written)(const char *name);

/* a DOS 8.3 name: 1 to 8 of letters, digits, _ and -, then optionally a dot and 1 to 3 of them */
int rv_fs_name_ok(const char *name)
{
    int n = 0, ext = -1;
    const char *p;
    for (p = name; *p; p++) {
        char c = *p;
        if (c == '.') {
            if (ext >= 0 || n == 0)
                return 0;
            ext = 0;
            continue;
        }
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return 0;
        if (ext >= 0) {
            if (++ext > 3)
                return 0;
        } else if (++n > 8) {
            return 0;
        }
    }
    return n > 0 && ext != 0;
}

static void written(const char *name)
{
    static char seen[64][13];
    static int nseen;
    int i;
    for (i = 0; i < nseen; i++)
        if (!strcmp(seen[i], name))
            return;
    if (nseen < 64)
        strcpy(seen[nseen++], name);
    if (rv_fs_written)
        rv_fs_written(name);
}

static int refuse(const char *name)
{
    fprintf(stderr, "fsguard: refused to open \"%.40s\" (not a plain DOS name in the game's directory)\n", name);
    errno = EACCES;
    return 0;
}

FILE *rv_host_fopen(const char *name, const char *mode)
{
    if (!rv_fs_name_ok(name))
        return refuse(name), (FILE *)0;
    if (strpbrk(mode, "wa+"))
        written(name);
    return fopen(name, mode);
}

int rv_host_open(const char *name, int flags, ...)
{
    if (!rv_fs_name_ok(name))
        return refuse(name), -1;
    if ((flags & O_ACCMODE) != O_RDONLY || (flags & (O_CREAT | O_TRUNC)))
        return refuse(name), -1;        /* (the game opens with open() only to read: dat.c, rocks.c) */
    return open(name, flags);
}
