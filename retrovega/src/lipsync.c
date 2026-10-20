/* lipsync.c: mouth shapes from text, tools/lipsync.py's rules in C.
 *
 * Nine shapes: 0 rest, 1 m b p, 2 f v, 3 th t d n l, 4 s z c j x sh ch,
 * 5 ah, 6 eh, 7 oh, 8 oo.  A vowel holds two 100 ms steps, a consonant
 * one; a space rests one step, a comma three, a full stop five.  Digraphs
 * are matched first.  lip_steps writes one shape per step. */
#include <string.h>
#include <ctype.h>
#include "rv.h"
#include "lipsync.h"

enum { REST, MBP, FV, TH, S, AH, EH, OH, OO };

static int single(int c)
{
    switch (c) {
    case 'a': return AH;
    case 'i': case 'e': case 'y': case 'k': case 'g': case 'h': return EH;
    case 'o': return OH;
    case 'u': case 'r': case 'w': case 'q': return OO;
    case 'b': case 'm': case 'p': return MBP;
    case 'f': case 'v': return FV;
    case 't': case 'd': case 'n': case 'l': return TH;
    case 's': case 'z': case 'c': case 'j': case 'x': return S;
    }
    return -1;
}

static int digraph(int a, int b)
{
    static const char *pairs[] = { "th", "sh", "ch", "ph", "oo", "ee", "qu", "wh", "ou", "ck" };
    static const unsigned char shape[] = { TH, S, S, FV, OO, EH, OO, OO, OH, EH };
    int i;
    for (i = 0; i < (int)RV_COUNT(pairs); i++)
        if (pairs[i][0] == a && pairs[i][1] == b)
            return shape[i];
    return -1;
}

static int pause_steps(int c)
{
    switch (c) {
    case ' ': return 1;
    case ',': case ';': case ':': return 3;
    case '-': return 2;
    case '.': case '!': case '?': return 5;
    }
    return 0;
}

static int is_vowel(int c) { return c && strchr("aeiouy", c) != 0; }

int lip_steps(const char __far *text, unsigned char __far *out, int max)
{
    int n = 0, i = 0, v, steps;
    while (text[i] && n < max) {
        int c = tolower((unsigned char)text[i]);
        int p = pause_steps(c);
        if (p) {
            while (p-- && n < max)
                out[n++] = REST;
            i++;
            continue;
        }
        v = text[i + 1] ? digraph(c, tolower((unsigned char)text[i + 1])) : -1;
        if (v >= 0) {
            steps = is_vowel(c) ? 2 : 1;
            i += 2;
        } else {
            v = single(c);
            steps = is_vowel(c) ? 2 : 1;
            i++;
            if (v < 0)
                continue;
        }
        while (steps-- && n < max)
            out[n++] = (unsigned char)v;
    }
    if (n < max)
        out[n++] = REST;
    return n;
}
