#!/usr/bin/env python3
"""The old game's campaign ported to the engine: its missions' shapes (the nav points, who waits at each, the
fields) from the data-segment dumps dump-all.sh makes, written as data/missions.txt and data/campaign.csv in our
own setting (the Aera war: the ships by the author's map, the systems renamed, the words ours).

    tests/wc1/port.py DUMPDIR [--write]        print the campaign; --write replaces data/missions.txt and
                                               data/campaign.csv (keep a copy of the placeholders)

The scale: the old game's nav points sit up to 70,000 km out and the engine keeps a nav point in 16 bits of km,
so every distance is scaled by SCALE (0.45; its speed setting moves 2 km/s a point, ours 1.5, so a leg takes
some 60% of its time).  Its ship types are read as the author's map has them (TYPES); the
carrier and its circling escorts are left out (the flight begins at the launch point, as the engine has it); the
wingman is one of our cast.  The branching between the systems is the published tree of the old campaign, as
recalled: edit campaign.csv where it is wrong.  The briefings are written from the mission's shape, to be
rewritten by hand.
"""
import csv, os, random, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
DATA = os.path.join(ROOT, 'data')
sys.path.insert(0, os.path.join(ROOT, 'tools'))

SCALE = 0.45                            # (its farthest nav points sit 70,000 km out)
# the old game's ship types, by what the dumps showed at each nav point (tests/wc1/README.md), to the author's map
TYPES = {0: 'SCHROED', 1: 'FRANKLN', 2: 'GAWAIN', 3: 'LANCELT', 8: 'VIGILNC', 9: 'AGIS', 10: 'NICANDR', 11: 'ARISTON',
         12: 'CONVOLN', 13: 'CLEOMEN', 6: 'OX', 4: 'KAHAN', 5: 'AGASICL', 7: 'ANAXIDM', 14: 'CHARILS', 15: 'AGASICL',
         16: 'AGASICL', 17: 'AGASICL', 18: 'ANAXIDM', 19: 'CHARILS'}
CAPITALS = {4, 5, 6, 7, 8, 14, 15, 16, 17, 18, 19}
FIELDS = {22: 'asteroid', 23: 'mine'}
# the systems, in the old campaign's order, as places of the Aera war: (node id, name, good next, bad next)
SERIES = [
    ('ENIGMA', 'the Enigma shipyards', 'CEPHID', 'CEPHID'),
    ('CEPHID', 'Cephid 17', 'SHELTON', 'CRUCIBLE'),
    ('SHELTON', 'Shelton', 'LIXIS', 'OLDZIEY'),
    ('CRUCIBLE', 'the Crucible picket', 'OLDZIEY', 'DEFIANCE'),
    ('OLDZIEY', 'Oldziey', 'LIXIS', 'ATARAXIA'),
    ('DEFIANCE', 'Defiance', 'ATARAXIA', 'BARNARD'),
    ('LIXIS', 'the Lixis beacon', 'SHMRN', 'RIGEL'),
    ('ATARAXIA', 'Ataraxia', 'RIGEL', 'BARNARD'),
    ('RIGEL', 'the Rigel relay', 'SHMRN', 'BARNARD'),
    ('BARNARD', "Barnard's Star", 'SHMRN', 'WOLF359'),
    ('WOLF359', 'Wolf 359', 'SHMRN', 'LUYTEN'),
    ('SHMRN', 'the Shmrn pocket', 'GOOD', 'BAD'),
    ('LUYTEN', "Luyten's Star", 'BAD', 'BAD'),
]
WINGMEN = ['TORCH', 'ECHO', 'COMET', 'FROST', 'KESTREL', 'DRIFTER', 'PRIME', 'ANCHOR']
NAMES = {'SCHROED': 'Schroedinger', 'FRANKLN': 'Franklin', 'GAWAIN': 'Gawain', 'LANCELT': 'Lancelot', 'AGIS': 'Agis',
         'NICANDR': 'Nicander', 'ARISTON': 'Ariston', 'CONVOLN': 'Convolution', 'CLEOMEN': 'Cleomenes', 'OX': 'Ox',
         'KAHAN': 'Kahan', 'AGASICL': 'Agasicles', 'ANAXIDM': 'Anaxidamus', 'CHARILS': 'Charillus', 'VIGILNC': 'Vigilance'}

def s24(b, o):
    v = b[o] | b[o + 1] << 8 | b[o + 2] << 16
    return v - (1 << 24) if v & 0x800000 else v

def read_dump(path):
    d = open(path, 'rb').read()
    w = lambda o: struct.unpack_from('<h', d, o)[0]
    uw = lambda o: struct.unpack_from('<H', d, o)[0]
    dw = lambda o: struct.unpack_from('<i', d, o)[0]
    navs = []
    for n in range(16):
        r = 0x687C + 0x4D * n
        rec = d[r:r + 0x4D]
        if not any(rec):
            continue
        name = rec[:16].split(b'\0')[0].decode('latin-1')
        ships = [uw(r + 0x39 + 2 * k) for k in range(10)]
        if not name:
            continue
        navs.append(dict(n=n, name=name, x=s24(d, r + 32), y=s24(d, r + 36), z=s24(d, r + 40),
                         ships=[s for s in ships if s != 0xFFFF]))
    ships = []
    for m in range(32):
        r = 0x6E80 + 0x2A * m
        rec = d[r:r + 0x2A]
        if not any(rec) or w(r) < 0:
            continue
        ships.append(dict(m=m, type=w(r), side=w(r + 2), leader=w(r + 4), behaviour=w(r + 6), nav=w(r + 8),
                          x=s24(d, r + 10), y=s24(d, r + 14), pilot=d[r + 16]))
    fields = []
    for f in range(7):
        r = 0xCDF6 + 0x14 * f
        rec = d[r:r + 0x14]
        if not any(rec):
            continue
        fields.append(dict(type=w(r), x=dw(r + 2), y=dw(r + 6), z=dw(r + 10), radius=dw(r + 14), density=w(r + 18)))
    return dict(navs=navs, ships=ships, fields=fields, mission=d[0xC255], series=d[0xC256], player=uw(0x9560))

def plural(n, name):
    if n == 1:
        return 'a%s %s' % ('n' if name[0] in 'AEIOU' else '', name)
    return '%d %s%s' % (n, name, '' if name.endswith('s') else 's')

def cap(text):
    return text[0].upper() + text[1:]

def km(v):
    return max(-32000, min(32000, round(v * SCALE)))

def port(dumpdir):
    rng = random.Random(1990)
    from campaign import relations, seat_room, TALK   # (the placeholders' seats and table talk)
    rel = relations()
    nodes, missions = [], []
    wing = 0
    for si, (node, system, good, bad) in enumerate(SERIES):
        ids = []
        for mi in range(4):
            path = os.path.join(dumpdir, 's%d-m%d' % (si + 1, mi), 't1.bin')
            if not os.path.exists(path):
                continue
            dmp = read_dump(path)
            mid = '%s%d' % (node[:7], mi + 1)
            ids.append(mid)
            # the nav points, scaled, nav 0 the launch point at the origin
            by_nav = {}
            lines = ['mission  %s' % mid, 'node     %s' % node, 'type     patrol', 'music    briefing']
            navs = [n for n in dmp['navs'] if n['n'] > 0]
            for n in navs:
                lines.append('nav      %d %d %d %d' % (n['n'], km(n['x']), km(n['y']), km(n['z'])))
            # who is where: the enemies and the escort targets by nav, grouped by type; the carrier, its escorts
            # (behaviour 3 and their followers at nav 0) and the player's own records left out
            pilot = WINGMEN[wing % len(WINGMEN)]
            wing += 1
            lines.append('ship     player 0 nav0')
            lines.append('ship     wingman 0 nav0 pilot=%s' % pilot)
            groups = {}
            player = dmp['player']
            carrier = {s['m'] for s in dmp['ships'] if s['type'] == 8}
            escorts = {s['m'] for s in dmp['ships'] if s['behaviour'] == 3 and s['leader'] in carrier}
            escorts |= {s['m'] for s in dmp['ships'] if s['leader'] in escorts}
            followers = [s['m'] for s in dmp['ships'] if s['leader'] == player]
            for s in dmp['ships']:
                t = s['type']
                if t in FIELDS or t == 8 or s['m'] == player or s['m'] in escorts or (followers and s['m'] == followers[0]):
                    continue
                nav = 0
                for n in dmp['navs']:
                    if s['m'] in n['ships']:
                        nav = n['n']
                key = (nav, t, s['side'])
                groups[key] = groups.get(key, 0) + 1
            enemies_at = {}
            for (nav, t, side), count in sorted(groups.items()):
                kind = TYPES.get(t)
                if not kind:
                    print('  %s: unknown old type %d at nav %d (x%d), left out' % (mid, t, nav, count), file=sys.stderr)
                    continue
                lines.append('ship     %s %d nav%d%s' % (kind, 1 if side else 0, nav, ' count=%d' % count if count > 1 else ''))
                if side:
                    enemies_at.setdefault(nav, []).append(plural(count, NAMES.get(kind, kind)))
                else:
                    enemies_at.setdefault(nav, []).append('our %s' % NAMES.get(kind, kind))
            for f in dmp['fields'][:4]:
                kind = FIELDS.get(f['type'])
                if kind:
                    lines.append('field    %s %d %d %d' % (kind, km(f['x'] / 256), km(f['z'] / 256), max(300, km(f['radius']))))
            # the words: the patrol as the shape says, in the war's own voice
            waypoints = ', '.join('Nav %d' % n['n'] for n in navs) or 'the carrier'
            expect = []
            for nav in sorted(enemies_at):
                expect.append('%s at Nav %d' % (' and '.join(enemies_at[nav]), nav))
            fields_txt = ''
            if dmp['fields']:
                fields_txt = ' Mind the %s along the way.' % ' and '.join(sorted({FIELDS.get(f['type'], 'field') + ' field' for f in dmp['fields']}))
            title = 'Patrol: %s, %s' % (system, 'one nav point' if len(navs) == 1 else '%d nav points' % len(navs))
            briefing = ('Pilots, listen up. The Vigilance holds station in %s. You and %s will fly %s and come home. '
                        'Intelligence expects %s.%s Keep your wingman close and bring the ship back.'
                        % (system, pilot.capitalize(), waypoints, '; '.join(expect) if expect else 'a quiet run', fields_txt))
            lines.insert(2, 'title    %s' % title[:48])
            lines.insert(5, 'briefing %s' % briefing[:640])
            lines.insert(6, 'win      %s holds. Good flying; get some rest.' % cap(system)[:40])
            lines.insert(7, 'lose     We lost ground in %s. Debrief, and we go again tomorrow.' % system[:40])
            # the rooms
            used = set()
            for room in ('bar', 'lounge'):
                seats = seat_room(rng, room, used, rel)
                lines.append('%-8s %s' % (room, ' '.join('%s=%s' % (k, v or '-') for k, v in seats.items())))
                people = [v for v in seats.values() if v]
                if len(people) >= 2:
                    a, b = people[0], people[1]
                    talk = TALK[rel.get(frozenset((a, b)), (True, 'none'))[1]]
                    lines.append('talk     %s %s %s "%s"' % (room, a, b, talk[0]))
                    lines.append('talk     %s %s %s "%s"' % (room, b, a, talk[1]))
            missions.append('\n'.join(lines))
        nodes.append((node, si, ids, good, bad))
    return nodes, missions

def main():
    dumpdir = sys.argv[1]
    nodes, missions = port(dumpdir)
    header = ('# The campaign: the old game\'s missions\' shapes (tests/wc1/port.py from its dumps), in the Aera war.\n'
              '# Edit freely: tools/rvdata.py checks every block.\n\n')
    text = header + '\n\n'.join(missions) + '\n'
    camp = 'node,layer,col,missions,good,bad\n' + ''.join('%s,%d,0,%s,%s,%s\n' % (n, i + 1, ' '.join(ids), g, b) for n, i, ids, g, b in nodes)
    if '--write' in sys.argv:
        open(os.path.join(DATA, 'missions.txt'), 'w').write(text)
        open(os.path.join(DATA, 'campaign.csv'), 'w').write(camp)
        print('wrote %d missions in %d nodes' % (len(missions), len(nodes)))
    else:
        print(camp)
        print(text)

main()
