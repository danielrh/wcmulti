#!/usr/bin/env python3
"""width16.py: a game source with DOS's integer widths made visible, for the width audit (tests/host/width-audit.sh).

On DOS (Watcom, 16-bit) int and unsigned are 16 bits; on the native build (32-bit host, wasm32) they are 32.  The
two agree wherever a value fits in 16 bits, and differ where DOS wraps or truncates one: a 16-bit hash or random
number, a counter that wraps, a product cut to 16 bits on its way into an int.  This rewrites a source so its own
int and unsigned are 16 bits (the typedefs rv16_int, rv16_uint: host/width16.h), and every cast to them a call
that checks the value (rv16i, rv16u).  Built with clang's -fsanitize=implicit-integer-truncation, each store of a
value that does not fit (an assignment, an argument, a return) is reported with its file and line, and each cast
by the checks: a report is a place where the native build computes what DOS would have wrapped.

The audit build only finds those places (its unsigned arithmetic is not DOS's: an unsigned short is promoted to a
signed int); the fix goes in the real source (a uint16_t, a cast), and RV.EXE stays as it was.

    tools/width16.py SRC.c > OUT.c      (with #line directives: the reports name SRC.c's lines)
"""
import re
import sys

TOKEN = re.compile(r'''
    (?P<ws>\s+)
  | (?P<comment>/\*.*?\*/|//[^\n]*)
  | (?P<str>"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*')
  | (?P<pp>\#[^\n]*(?:\\\n[^\n]*)*)
  | (?P<id>[A-Za-z_]\w*)
  | (?P<num>\.?\d[\w.]*)
  | (?P<op>->|\+\+|--|<<=|>>=|<<|>>|<=|>=|==|!=|&&|\|\||[-+*/%&|^!~<>=?:;,.(){}\[\]])
''', re.S | re.X)

INTS = {'int', 'unsigned', 'signed', 'short', 'long', 'char'}


def tokens(src):
    out, at = [], 0
    while at < len(src):
        m = TOKEN.match(src, at)
        if not m:
            out.append(('op', src[at]))
            at += 1
            continue
        out.append((m.lastgroup, m.group()))
        at = m.end()
    return out


def sig(toks, i, step):
    """the index of the next significant token from i (step +1 or -1), or None"""
    i += step
    while 0 <= i < len(toks) and toks[i][0] in ('ws', 'comment'):
        i += step
    return i if 0 <= i < len(toks) else None


def operand_end(toks, i):
    """the end (exclusive) of the unary operand that starts at significant token i: a name or number with its
    postfix chain (. -> [] () ++ --), or a parenthesised expression with its chain"""
    def balanced(j, open_, close):
        depth = 0
        while j < len(toks):
            if toks[j][1] == open_:
                depth += 1
            elif toks[j][1] == close:
                depth -= 1
                if depth == 0:
                    return j + 1
            j += 1
        return j
    kind, text = toks[i]
    if text == 'sizeof':                # (sizeof x, sizeof (t): its operand with it)
        k = sig(toks, i, 1)
        if k is not None and toks[k][1] == '(':
            return balanced(k, '(', ')')
        return operand_end(toks, k) if k is not None else i + 1
    if text == '(':
        j = balanced(i, '(', ')')
        # a cast inside: (type)operand, the type a name or two (int16_t, unsigned char, LzBolt *) then an operand
        inner = [t for t in toks[i + 1:j - 1] if t[0] not in ('ws', 'comment')]
        nxt = sig(toks, j - 1, 1)
        if inner and all(t[0] == 'id' or t[1] == '*' for t in inner) and nxt is not None \
                and (toks[nxt][0] in ('id', 'num') or toks[nxt][1] in ('(', '-', '~', '!', '&', '*')) \
                and (toks[nxt][1] != '(' or any(t[1] == '*' or t[1] in INTS or t[1].endswith('_t') or t[1][0].isupper()
                                                 for t in inner)):   # ((f)(x): a call, unless f is a type's name)
            return operand_end(toks, nxt)
    elif kind in ('id', 'num'):
        j = i + 1
    elif text in ('-', '+', '~', '!', '*', '&'):
        k = sig(toks, i, 1)
        return operand_end(toks, k) if k is not None else i + 1
    else:
        return None
    while True:
        k = sig(toks, j - 1, 1)
        if k is None:
            return j
        t = toks[k][1]
        if t in ('.', '->'):
            n = sig(toks, k, 1)
            j = n + 1
        elif t == '[':
            j = balanced(k, '[', ']')
        elif t == '(' and kind == 'id':
            j = balanced(k, '(', ')')
        elif t in ('++', '--'):
            j = k + 1
        else:
            return j


def rewrite(src, path):
    toks = tokens(src)
    out = []
    i = 0
    n = len(toks)
    scanf_next = False
    while i < n:
        kind, text = toks[i]
        # a cast to int or unsigned (alone, or unsigned int): a checked conversion of its operand
        if text == '(':
            j = sig(toks, i, 1)
            words, k = [], j
            while k is not None and toks[k][0] == 'id' and toks[k][1] in INTS:
                words.append(toks[k][1])
                k = sig(toks, k, 1)
            if words and k is not None and toks[k][1] == ')' and words in (['int'], ['unsigned'], ['unsigned', 'int'], ['signed'], ['signed', 'int']):
                start = sig(toks, k, 1)
                end = operand_end(toks, start) if start is not None else None
                prev = sig(toks, i, -1)
                # (not sizeof (int), nor a declaration's parenthesis)
                if end is not None and not (prev is not None and toks[prev][1] == 'sizeof'):
                    fn = 'rv16u' if words[0] == 'unsigned' else 'rv16i'
                    out.append(fn + '(')
                    out.append(''.join(t for _, t in toks[start:end]))
                    out.append(')')
                    i = end
                    continue
        # a scanf's format: %d and %u store 16 bits now (%hd, %hu)
        if kind == 'id' and text in ('sscanf', 'scanf', 'fscanf'):
            scanf_next = True
        if kind == 'str' and scanf_next:
            text = re.sub(r'%(\d*)([du])', r'%\1h\2', text)
            out.append(text)
            scanf_next = False
            i += 1
            continue
        if kind == 'id' and text in ('int', 'unsigned', 'signed'):
            nxt = sig(toks, i, 1)
            if text == 'int' and nxt is not None and toks[nxt][1] == 'main':
                out.append(text)                                     # (main returns an int)
                i += 1
                continue
            prv = sig(toks, i, -1)
            nt = toks[nxt][1] if nxt is not None else ''
            pt = toks[prv][1] if prv is not None else ''
            if text == 'int' and pt in ('long', 'short', 'unsigned', 'signed', 'char'):
                out.append('' if pt in ('long', 'short') else text)   # (long int, short int: the other word says it)
                if pt in ('unsigned', 'signed'):
                    out[-1] = ''                                     # (unsigned int: said at the unsigned)
                i += 1
                continue
            if text == 'unsigned' and nt in ('char', 'short', 'long'):
                out.append(text)
                i += 1
                continue
            if text == 'signed' and nt in ('char', 'short', 'long'):
                out.append(text)
                i += 1
                continue
            out.append('rv16_uint' if text == 'unsigned' else 'rv16_int')
            i += 1
            continue
        out.append(text)
        i += 1
    return '#include "width16.h"\n#line 1 "%s"\n' % path + ''.join(out)


def main():
    path = sys.argv[1]
    src = open(path).read()
    sys.stdout.write(rewrite(src, path))


if __name__ == '__main__':
    main()
