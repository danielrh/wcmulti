#!/usr/bin/env python3
"""A WC.EXE data-segment dump (the layer's @tag key-script dump, 64 KB) read as the multiplayer layer reads it
(src/cpu/wcnet_ds.def's offsets): the mission's nav points and ships, the fields, and the slots as they stand.

    dsdump.py DUMP.bin [--slots]
"""
import struct, sys

# offsets from src/cpu/wcnet_ds.def (the WC.EXE column)
NAV_TABLE, NAV_SIZE, NAVS = 0x687C, 0x4D, 16
MS_TABLE, MS_SIZE, MSHIPS = 0x6E80, 0x2A, 64
FIELD_TABLE, FIELD_SIZE = 0xCDF6, 0x14
CUR_NAV, FIELD_COUNT, CUR_MISSION, CUR_SERIES = 0xC178, 0xBFF6, 0xC255, 0xC256
PLAYER_MS = 0x9560
# the slots (10 ship slots in WC.EXE)
ETYPE, TYPES, POS, FRONT, UP, RIGHT, VEL = 0xBD1A, 0xBFF8, 0xA9C2, 0xB4B6, 0xB1B6, 0xAEB6, 0xCAE2
SPEED, SETSPEED, AISTATE, AITIMER, MTARGET, PARENT, SLOTMS, SIDE, PILOT = 0xBE6C, 0xB9F6, 0xCA56, 0xBB8E, 0xC284, 0xC30E, 0xC974, 0xBE94, 0xD1A2
SHIELD, ARMOR, HULLDMG, GUNS, STATS_HULL = 0xC430, 0xC85E, 0xC17A, 0xC472, 0x49EC
STATS_SIZE = 0x6F

def main():
    path = sys.argv[1]
    d = open(path, 'rb').read()
    w = lambda o: struct.unpack_from('<h', d, o)[0]
    uw = lambda o: struct.unpack_from('<H', d, o)[0]
    dw = lambda o: struct.unpack_from('<i', d, o)[0]
    print('mission %d of series %d, at nav %d, player mission ship %d, fields %d' % (d[CUR_MISSION], d[CUR_SERIES], w(CUR_NAV), w(PLAYER_MS), w(FIELD_COUNT)))
    print('nav points:')
    for n in range(NAVS):
        r = NAV_TABLE + NAV_SIZE * n
        rec = d[r:r + NAV_SIZE]
        if not any(rec):
            continue
        name = rec[:16].split(b'\0')[0].decode('latin-1')
        ships = [uw(r + 0x39 + 2 * k) for k in range(10)]
        ships = [s for s in ships if s != 0xFFFF]
        print('  nav %2d %-12r words %s ships %s' % (n, name, ' '.join('%d' % w(r + k) for k in range(16, 0x39, 2)), ships))
    print('mission ships:')
    for m in range(MSHIPS):
        r = MS_TABLE + MS_SIZE * m
        rec = d[r:r + MS_SIZE]
        if not any(rec):
            continue
        print('  ms %2d type %2d side %d leader %5d behaviour %d nav %2d place %6d %6d %6d pilot %3d rest %s' % (
            m, w(r), w(r + 2), w(r + 4), w(r + 6), w(r + 8), w(r + 10), w(r + 12), w(r + 14), d[r + 16], rec[17:].hex()))
    print('fields:')
    for f in range(7):
        r = FIELD_TABLE + FIELD_SIZE * f
        rec = d[r:r + FIELD_SIZE]
        if not any(rec):
            continue
        print('  field %d type %d at %d %d %d radius %d density %d' % (f, w(r), dw(r + 2), dw(r + 6), dw(r + 10), dw(r + 14), w(r + 18)))
    if '--slots' in sys.argv:
        print('slots:')
        for s in range(10):
            et = w(ETYPE + 2 * s)
            if et == 0:
                continue
            p = [dw(POS + 12 * s + 4 * k) for k in range(3)]
            f = [dw(FRONT + 12 * s + 4 * k) for k in range(3)]
            print('  slot %d type %2d (ship type %2d) side %d ms %2d pilot %3d at %s front %s speed %d set %d ai %d timer %d target %d parent %d hull %d sh %d %d ar %s' % (
                s, et, w(TYPES + 2 * s), w(SIDE + 2 * s), w(SLOTMS + 2 * s), d[PILOT + s], p, f, dw(SPEED + 4 * s), dw(SETSPEED + 4 * s),
                w(AISTATE + 2 * s), w(AITIMER + 2 * s), d[MTARGET + s], d[PARENT + s], w(HULLDMG + 2 * s), w(SHIELD + 4 * s), w(SHIELD + 4 * s + 2),
                [w(ARMOR + 8 * s + 2 * k) for k in range(4)]))
    if '--stats' in sys.argv:
        print('ship stats (0x6f bytes a type, the hull word at +10):')
        base = STATS_HULL - 10
        for t in range(16):
            r = base + STATS_SIZE * t
            words = [w(r + 2 * k) for k in range(0, STATS_SIZE // 2)]
            print('  type %2d: %s' % (t, ' '.join('%d' % x for x in words)))

main()
