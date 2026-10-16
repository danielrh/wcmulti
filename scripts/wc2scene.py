#!/usr/bin/env python3
"""Skeleton of a WC2 scene block (SERIES.S0n): per Talk routine, the calls that
show a string (printit / closeup / talking ...), with the face set by settalker
and the background, in order.  wc2scene.py SERIES.S02 [block]"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wc2data

def forms(data, off, end, path=()):
    out = []
    while off + 8 <= end:
        cid = data[off:off+4]; size = struct.unpack('>I', data[off+4:off+8])[0]
        if cid == b'FORM':
            out += forms(data, off+12, off+8+size, path + (data[off+8:off+12].decode(),))
        else:
            out.append((path, cid.decode(), data[off+8:off+8+size]))
        off += 8 + size + (size & 1)
    return out

def skeleton(block):
    strs = [s.decode('latin1') for s in wc2data.scene_strings(block)]
    sequ = [(c, p) for path, c, p in forms(block, 0, len(block)) if path[-2:] == ('SEQU', 'SCRP')]
    names = [n.decode() for n in dict(sequ)['SYMB'].split(b'\0')[:-1]]
    ofst = dict(sequ)['OFST']
    n = struct.unpack_from('<H', ofst, 0)[0]
    offs = struct.unpack_from('<%dI' % n, ofst, 2)
    code = ofst[2 + 4 * n:]
    talks = sorted((o, names[i]) for i, o in enumerate(offs) if o != 0xFFFFFFFF)
    out = []
    for k, (start, name) in enumerate(talks):
        end = talks[k + 1][0] if k + 1 < len(talks) else len(code)
        out.append("  %s:" % name)
        i = start; last_str = None; last_const = None; consts = []; talker = '?'
        while i < end:
            op = code[i]
            if op == 0x24 and code[i+2:i+5] == b'\x28\xff\x15':
                talker = code[i + 1]; i += 5; consts = []; continue
            if op == 0x25 and code[i+3:i+6] == b'\x28\xff\x15':
                talker = struct.unpack_from('<H', code, i + 1)[0]; i += 6; consts = []; continue
            if op == 0x6f:
                last_str = struct.unpack_from('<H', code, i + 1)[0]; i += 3
            elif op == 0x24:
                last_const = code[i + 1]; consts.append(last_const); i += 2
            elif op == 0x25:
                last_const = struct.unpack_from('<H', code, i + 1)[0]; consts.append(last_const); i += 3
            elif op == 0x88:
                fn = names[code[i + 1]] if code[i + 1] < len(names) else '?%d' % code[i + 1]
                if fn in ('printit', 'closeup', 'talking', 'narrating', 'vidit', 'doshow', 'mediumshot') and last_str is not None:
                    out.append("    %-9s [%3s] %3d %s" % (fn, talker, last_str, strs[last_str][:70].replace('\n', ' ') if last_str < len(strs) else '?'))
                    last_str = None
                elif fn == 'settalker':
                    out.append("    settalker %s" % talker)
                elif fn in ('setupbackground', 'setupbackdrop'):
                    out.append("    %s %s" % (fn, consts[-1] if consts else '?'))
                elif fn == 'setupmediumslot':
                    out.append("    mediumslot %s" % consts[-3:])
                consts = []
                i += 2
            elif op == 0x89 and last_str is not None:
                out.append("    film      [%3s] %3d %s" % (talker, last_str, strs[last_str][:70].replace('\n', ' '))); last_str = None; i += 2
            elif op == 0x87:
                s = struct.unpack_from('<H', code, i + 1)[0]
                out.append("    film2     [%3s] %3d %s" % (talker, s, strs[s][:70].replace('\n', ' ') if s < len(strs) else '?')); i += 4
            elif op == 0x0f:
                rel = struct.unpack_from('<h', code, i + 1)[0]
                out.append("    IF-not -> +%d" % rel); i += 3
            elif op == 0x0e:
                rel = struct.unpack_from('<h', code, i + 1)[0]
                out.append("    JUMP -> +%d" % rel); i += 3
            else:
                i += 1
    return strs, out

if __name__ == '__main__':
    bl = wc2data.blocks_of(sys.argv[1])
    which = [int(a) for a in sys.argv[2:]] or range(len(bl))
    for b in which:
        strs, out = skeleton(bl[b])
        print("=== block %d (%d strings)" % (b, len(strs)))
        print("\n".join(out))
