#!/usr/bin/env python3
"""The game's logic data: campaign, missions and cast, checked and compiled.

    rvdata.py [--out build/dos/RVGAME.DAT] [--header src/rvdata.h]

Reads data/campaign.csv, data/missions.txt, data/people.csv,
data/relations.csv, data/ships.csv and the seat art (art/places/*/place.json),
checks every reference and rule (docs/engine.md), and writes:

    RVGAME.DAT   fixed-size little-endian records the engine reads straight
                 into its tables: a header, the pilots, the campaign nodes,
                 the missions
    rvdata.h     the same records as packed C structs, the limits and the
                 special values, so the two can never disagree (the engine
                 also checks each record size against the header)

missions.txt is one block per mission, a keyword and its value per line,
'#' comments.  Keywords: mission, node, title, type, music, briefing, win,
lose, nav (id x y z, km), ship (TYPE|player|wingman side navN [count=N]
[pilot=KEY]), field (asteroid|mine x z radius, km), bar and lounge
(seat=PILOT|- ...), talk (room speaker listener "text").  briefing, win and lose may repeat: the lines are joined.
"""
import argparse
import csv
import json
import math
import os
import shlex
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DATA = os.path.join(ROOT, 'data')

VERSION = 3                             # 2: the ship types' engines; 3: their hit boxes and vitals
LIMITS = {'PILOTS': 16, 'NODES': 32, 'MISSIONS': 128, 'MISSIONS_PER_NODE': 4, 'NAVS': 8, 'SHIPS': 12,
          'SEATS': 3, 'TALKS': 4, 'FIELDS': 4, 'SHIPTYPES': 64}
TEXT = {'ID': 16, 'TITLE': 48, 'BRIEFING': 640, 'RESULT': 128, 'TALK': 60, 'KEY': 10, 'CALLSIGN': 12,
        'NAME': 24, 'NODE_ID': 8, 'LINE': 72}
FIELDS = {'asteroid': 9, 'mine': 10}                 # their EntityType, as fieldTable keeps them
NONE, GOOD, BAD = 0xFF, 0xFE, 0xFD                   # node edges: a node index, or an ending
SHIP_PLAYER, SHIP_WINGMAN = 0xFF, 0xFE               # mission ship types chosen at run time
ROOMS = ['bar', 'lounge']
SEAT_NAMES = ['left', 'middle', 'right']
FIXED_PLACES = {'TECH': 'the deck', 'TALLY': 'the bar', 'XO': 'the briefing room'}
TYPES = {'patrol': 0}
# the music's cues, in their files' order: the rooms' songs (tools/songs.py, tools/midi.py), then the flight's
# (tools/flightsongs.py; src/score.c picks them), the stings last (played once: music_sting)
MUSIC_FILES = ['BRIEF', 'BAR', 'LAUNCH', 'WIN', 'LOSE', 'QUARTERS', 'LOUNGE', 'CRUISE1', 'CRUISE2', 'CRUISE3',
               'CRUISE4', 'CRUISE5', 'CRUISE6', 'HOMEWARD', 'BATTLE1', 'BATTLE2', 'BATTLE3', 'ROCKS',
               'PANIC1', 'PANIC2', 'PANIC3', 'VICTORY1', 'VICTORY2', 'VICTORY3', 'TRAGEDY']
MUSIC = {'briefing': 0, 'bar': 1, 'launch': 2, 'win': 3, 'lose': 4, 'quarters': 5, 'lounge': 6}
MUSIC.update({f.lower(): i for i, f in enumerate(MUSIC_FILES) if i >= len(MUSIC)})
ROLES = {'pilot': 0, 'hero': 1, 'barman': 2, 'xo': 3, 'tech': 4}

# record layouts: (C type, field, struct format)
HEADER = [('char', 'magic[8]', '8s'), ('unsigned short', 'version', 'H'), ('unsigned short', 'header_size', 'H'),
          ('unsigned short', 'npilots', 'H'), ('unsigned short', 'pilot_size', 'H'), ('unsigned long', 'pilots_at', 'L'),
          ('unsigned short', 'nnodes', 'H'), ('unsigned short', 'node_size', 'H'), ('unsigned long', 'nodes_at', 'L'),
          ('unsigned short', 'nmissions', 'H'), ('unsigned short', 'mission_size', 'H'),
          ('unsigned long', 'missions_at', 'L'), ('unsigned short', 'start_node', 'H'),
          ('unsigned short', 'nshiptypes', 'H'), ('unsigned short', 'shiptype_size', 'H'),
          ('unsigned long', 'shiptypes_at', 'L'), ('unsigned char', 'reserved[22]', '22s')]
# a ship type: data/ships.csv's row, its numbers as the simulation wants them
SHIPTYPE = [('char', 'key[RV_KEY]', '%ds' % TEXT['KEY']), ('char', 'name[RV_NAME]', '%ds' % TEXT['NAME']),
            ('unsigned char', 'type', 'B'), ('unsigned char', 'side', 'B'), ('unsigned char', 'flags', 'B'),
            ('unsigned char', 'cockpit', 'B'),
            ('unsigned short', 'cruise_kps', 'H'), ('unsigned short', 'max_kps', 'H'), ('unsigned short', 'ab_kps', 'H'),
            ('unsigned short', 'accel', 'H'), ('unsigned char', 'yaw_dps', 'B'), ('unsigned char', 'pitch_dps', 'B'),
            ('unsigned char', 'roll_dps', 'B'), ('unsigned char', 'guns', 'B'),
            ('unsigned short', 'shield_f', 'H'), ('unsigned short', 'shield_r', 'H'),
            ('unsigned short', 'armor_f', 'H'), ('unsigned short', 'armor_r', 'H'), ('unsigned short', 'armor_l', 'H'),
            ('unsigned short', 'armor_rt', 'H'), ('unsigned short', 'hull', 'H'), ('unsigned short', 'lod0_px', 'H'),
            ('unsigned short', 'missiles', 'H'), ('unsigned char', 'nengines', 'B'), ('unsigned char', 'pad', 'B'),
            ('signed char', 'engine[RV_ENGINES][4]', '%db' % (4 * 4)),
            ('unsigned char', 'hit[3]', '3B'), ('unsigned char', 'core', 'B'), ('unsigned char', 'nvitals', 'B'),
            ('signed char', 'vital[RV_VITALS][3]', '%db' % (3 * 8))]
SHIPTYPE_FLAG_PLAYER = 1               # the player may fly it (a cockpit kit exists)
# a ship type's engines (data/engines.csv, tools/engines.py): x, y, z in its sprites' box (127 = the half box; +x
# the ship's left, +z the nose) and the glow's radius, min(0.3 size, 0.09) of the half box, in 1/1024ths
ENGINES = 4
# its hit box and vital points (data/hitboxes.csv, tools/hitboxes.py): the box's half extents across, up and along
# (127 = the sprites' half box), its core's radius (the biggest sphere about its middle inside its sprite at every
# view), and points (mounts, nose, tail, wingtips) in the engines' frame
VITALS = 8
PILOT = [('char', 'key[RV_KEY]', '%ds' % TEXT['KEY']), ('char', 'callsign[RV_CALLSIGN]', '%ds' % TEXT['CALLSIGN']),
         ('char', 'name[RV_NAME]', '%ds' % TEXT['NAME']), ('unsigned char', 'role', 'B'),
         ('unsigned char', 'pad', 'B'), ('char', 'line[RV_LINE]', '%ds' % TEXT['LINE'])]
NODE = [('char', 'id[RV_NODE_ID]', '%ds' % TEXT['NODE_ID']), ('unsigned char', 'layer', 'B'),
        ('signed char', 'col', 'b'), ('unsigned short', 'missions[RV_MISSIONS_PER_NODE]', '4H'),
        ('unsigned char', 'good', 'B'), ('unsigned char', 'bad', 'B'), ('unsigned char', 'pad[2]', '2s')]
NAV = [('unsigned char', 'id', 'B'), ('unsigned char', 'pad', 'B'), ('short', 'x', 'h'), ('short', 'y', 'h'),
       ('short', 'z', 'h')]
SHIP = [('unsigned char', 'type', 'B'), ('unsigned char', 'side', 'B'), ('unsigned char', 'nav', 'B'),
        ('unsigned char', 'count', 'B'), ('unsigned char', 'pilot', 'B'), ('unsigned char', 'pad[3]', '3s')]
FIELD = [('unsigned char', 'type', 'B'), ('unsigned char', 'pad', 'B'), ('short', 'x', 'h'), ('short', 'z', 'h'),
         ('short', 'radius', 'h')]
TALK = [('unsigned char', 'room', 'B'), ('unsigned char', 'speaker', 'B'), ('unsigned char', 'listener', 'B'),
        ('unsigned char', 'pad', 'B'), ('char', 'text[RV_TALK]', '%ds' % TEXT['TALK'])]
MISSION = [('char', 'id[RV_ID]', '%ds' % TEXT['ID']), ('unsigned char', 'node', 'B'), ('unsigned char', 'type', 'B'),
           ('unsigned char', 'music', 'B'), ('unsigned char', 'nnav', 'B'), ('unsigned char', 'nship', 'B'),
           ('unsigned char', 'ntalk', 'B'), ('unsigned char', 'nfield', 'B'), ('unsigned char', 'pad', 'B'),
           ('char', 'title[RV_TITLE]', '%ds' % TEXT['TITLE']), ('char', 'briefing[RV_BRIEFING]', '%ds' % TEXT['BRIEFING']),
           ('char', 'win[RV_RESULT]', '%ds' % TEXT['RESULT']), ('char', 'lose[RV_RESULT]', '%ds' % TEXT['RESULT']),
           ('RvNav', 'nav[RV_NAVS]', None), ('RvShip', 'ship[RV_SHIPS]', None),
           ('unsigned char', 'seat[RV_ROOMS][RV_SEATS]', '6B'), ('unsigned char', 'pad2[2]', '2s'),
           ('RvTalk', 'talk[RV_TALKS]', None), ('RvField', 'field[RV_FIELDS]', None)]


def fmt(fields):
    return '<' + ''.join(f for _t, _n, f in fields if f)


class Fail(Exception):
    pass


def text(s, n, where):
    b = s.encode('cp437')
    if len(b) >= n:
        raise Fail('%s: %d characters, at most %d' % (where, len(b), n - 1))
    return b


def load_csv(name):
    with open(os.path.join(DATA, name)) as f:
        return list(csv.DictReader(f))


def parse_missions(path):
    missions, cur = [], None
    for lineno, raw in enumerate(open(path), 1):
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        key, _, val = line.partition(' ')
        val = val.strip()
        where = '%s:%d' % (os.path.basename(path), lineno)
        if key == 'mission':
            cur = {'id': val, 'where': where, 'nav': [], 'ship': [], 'talk': [], 'seats': {}, 'field': [],
                   'briefing': [], 'win': [], 'lose': []}
            missions.append(cur)
        elif cur is None:
            raise Fail('%s: %s before any mission' % (where, key))
        elif key in ('briefing', 'win', 'lose'):
            cur[key].append(val)
        elif key in ('node', 'title', 'type', 'music'):
            cur[key] = val
        elif key == 'nav':
            cur['nav'].append((where, val.split()))
        elif key == 'ship':
            cur['ship'].append((where, val.split()))
        elif key == 'field':
            cur['field'].append((where, val.split()))
        elif key in ROOMS:
            cur['seats'][key] = (where, dict(kv.split('=', 1) for kv in val.split()))
        elif key == 'talk':
            cur['talk'].append((where, shlex.split(val)))
        else:
            raise Fail('%s: unknown keyword %r' % (where, key))
    return missions


def compile_all():
    people = load_csv('people.csv')
    pilot_index = {p['key']: i for i, p in enumerate(people)}
    if len(people) > LIMITS['PILOTS']:
        raise Fail('people.csv: %d pilots, at most %d' % (len(people), LIMITS['PILOTS']))
    ships_csv = load_csv('ships.csv')
    ships = {r['key']: int(r['type'], 16) for r in ships_csv}
    cockpit_keys = list(json.load(open(os.path.join(DATA, 'cockpits.json')))['ships'])
    if len(ships_csv) > LIMITS['SHIPTYPES']:
        raise Fail('ships.csv: %d types, at most %d' % (len(ships_csv), LIMITS['SHIPTYPES']))
    out_types = []
    engines_csv = {r['key']: r for r in load_csv('engines.csv')}
    hits_csv = {r['key']: r for r in load_csv('hitboxes.csv')}
    for r in ships_csv:
        n = lambda k: int(r[k] or 0)
        eng = []
        e = engines_csv.get(r['key'])
        for i in range(int(e['n']) if e else 0):
            x, y, z, size = (float(e['%s%d' % (c, i)]) for c in ('x', 'y', 'z', 'size'))
            clamp = lambda v: max(-127, min(127, int(round(v * 127))))
            eng += [clamp(x), clamp(y), clamp(z), max(1, int(round(min(0.3 * size, 0.09) * 1024)))]
        nengines = len(eng) // 4
        eng += [0] * (4 * ENGINES - len(eng))
        h = hits_csv.get(r['key'])
        clamp = lambda v: max(-127, min(127, int(round(v * 127))))
        hit = [max(1, clamp(float(h[c]))) for c in ('hx', 'hy', 'hz')] if h else [90, 40, 120]
        core = clamp(float(h['core'])) if h else 0
        vit = []
        for i in range(int(h['n']) if h else 0):
            vit += [clamp(float(h['%s%d' % (c, i)])) for c in ('x', 'y', 'z')]
        nvitals = len(vit) // 3
        vit += [0] * (3 * VITALS - len(vit))
        count = lambda k: int(r[k].split('x')[0]) if r[k] and 'x' in r[k] else 0
        for k in ('yaw_dps', 'pitch_dps', 'roll_dps'):
            if n(k) > 255:
                raise Fail('ships.csv %s: %s %d does not fit a byte' % (r['key'], k, n(k)))
        out_types.append(struct.pack(fmt(SHIPTYPE), text(r['key'], TEXT['KEY'], 'ships.csv'),
                                     text(r['name'], TEXT['NAME'], 'ships.csv'), int(r['type'], 16), n('side'),
                                     SHIPTYPE_FLAG_PLAYER if r['key'] in cockpit_keys else 0,
                                     cockpit_keys.index(r['key']) if r['key'] in cockpit_keys else 0xFF,
                                     n('cruise_kps'), n('max_kps'), n('ab_kps'), n('accel'), n('yaw_dps'),
                                     n('pitch_dps'), n('roll_dps'), count('guns'), n('shield_f'), n('shield_r'),
                                     n('armor_f'), n('armor_r'), n('armor_l'), n('armor_rt'), n('hull'),
                                     n('lod0_px'), count('missiles'), nengines, 0, *eng, *hit, core, nvitals, *vit))
    talk_ok = {frozenset((r['a'], r['b'])): r['talk'] == 'yes' for r in load_csv('relations.csv')}
    for pair in talk_ok:
        for k in pair:
            if k not in pilot_index:
                raise Fail('relations.csv: unknown pilot %s' % k)
    seat_art = {}
    for room in ROOMS:
        with open(os.path.join(ROOT, 'art', 'places', room, 'place.json')) as f:
            seat_art[room] = {s: set(v) for s, v in json.load(f)['seats'].items()}

    nodes = load_csv('campaign.csv')
    node_index = {n['node']: i for i, n in enumerate(nodes)}
    if len(nodes) > LIMITS['NODES']:
        raise Fail('campaign.csv: %d nodes, at most %d' % (len(nodes), LIMITS['NODES']))
    missions = parse_missions(os.path.join(DATA, 'missions.txt'))
    mission_index = {m['id']: i for i, m in enumerate(missions)}
    if len(missions) > LIMITS['MISSIONS'] or len(mission_index) != len(missions):
        raise Fail('missions.txt: %d missions (at most %d), ids must be unique' % (len(missions), LIMITS['MISSIONS']))

    def edge(v, where):
        if v in ('GOOD', 'BAD'):
            return {'GOOD': GOOD, 'BAD': BAD}[v]
        if v not in node_index:
            raise Fail('%s: edge to unknown node %s' % (where, v))
        return node_index[v]

    out_nodes = []
    for n in nodes:
        where = 'campaign.csv %s' % n['node']
        ms = n['missions'].split()
        if len(ms) != LIMITS['MISSIONS_PER_NODE']:
            raise Fail('%s: %d missions, need %d' % (where, len(ms), LIMITS['MISSIONS_PER_NODE']))
        for m in ms:
            if m not in mission_index:
                raise Fail('%s: unknown mission %s' % (where, m))
            if missions[mission_index[m]].get('node') != n['node']:
                raise Fail('%s: mission %s says it belongs to %s' % (where, m, missions[mission_index[m]].get('node')))
        out_nodes.append(struct.pack(fmt(NODE), text(n['node'], TEXT['NODE_ID'], where), int(n['layer']),
                                     int(n['col']), *[mission_index[m] for m in ms],
                                     edge(n['good'], where), edge(n['bad'], where), b''))
        if n['good'] == n['bad']:
            raise Fail('%s: both ways out go to %s' % (where, n['good']))
    check_dag(nodes)

    out_missions = []
    for m in missions:
        w = m['where']
        for k in ('node', 'title', 'type', 'music'):
            if k not in m:
                raise Fail('%s: mission %s has no %s' % (w, m['id'], k))
        if m['node'] not in node_index:
            raise Fail('%s: unknown node %s' % (w, m['node']))
        if m['type'] not in TYPES or m['music'] not in MUSIC:
            raise Fail('%s: type %r / music %r unknown' % (w, m['type'], m['music']))
        navs = []
        for where, f in m['nav']:
            navs.append(struct.pack(fmt(NAV), int(f[0]), 0, int(f[1]), int(f[2]), int(f[3])))
        if len(navs) > LIMITS['NAVS']:
            raise Fail('%s: %d nav points, at most %d' % (w, len(navs), LIMITS['NAVS']))
        nav_ids = {0} | {int(f[0]) for _w, f in m['nav']}
        shipr, wingman = [], None
        for where, f in m['ship']:
            opts = dict(o.split('=', 1) for o in f[3:])
            kind = f[0]
            tnum = SHIP_PLAYER if kind == 'player' else SHIP_WINGMAN if kind == 'wingman' else ships.get(kind)
            if tnum is None:
                raise Fail('%s: unknown ship %s' % (where, kind))
            nav = int(f[2][3:]) if f[2].startswith('nav') else -1
            if nav not in nav_ids:
                raise Fail('%s: unknown nav point %s' % (where, f[2]))
            pilot = NONE
            if 'pilot' in opts:
                if opts['pilot'] not in pilot_index or people[pilot_index[opts['pilot']]]['role'] != 'pilot':
                    raise Fail('%s: %s is not a pilot' % (where, opts['pilot']))
                pilot = pilot_index[opts['pilot']]
                if kind == 'wingman':
                    wingman = opts['pilot']
            shipr.append(struct.pack(fmt(SHIP), tnum, int(f[1]), nav, int(opts.get('count', 1)), pilot, b''))
        if len(shipr) > LIMITS['SHIPS']:
            raise Fail('%s: %d ships, at most %d' % (w, len(shipr), LIMITS['SHIPS']))
        seats, placed = [], {}
        for room in ROOMS:
            where, seat = m['seats'].get(room, (w, {}))
            people_here = []
            for s in SEAT_NAMES:
                who = seat.get(s, '-')
                if who == '-':
                    seats.append(NONE)
                    continue
                if who not in pilot_index:
                    raise Fail('%s: unknown pilot %s' % (where, who))
                if who in FIXED_PLACES:
                    raise Fail('%s: %s is always at %s' % (where, who, FIXED_PLACES[who]))
                if who not in seat_art[room].get(s, ()):
                    raise Fail('%s: no art of %s in the %s %s seat (only %s)'
                               % (where, who, room, s, ', '.join(sorted(seat_art[room].get(s, ())))))
                if who in placed:
                    raise Fail('%s: %s is already in the %s' % (where, who, placed[who]))
                placed[who] = room
                people_here.append(who)
                seats.append(pilot_index[who])
            for a in people_here:
                for b in people_here:
                    if a < b and not talk_ok.get(frozenset((a, b)), True):
                        raise Fail('%s: %s and %s will not sit at one table (relations.csv)' % (where, a, b))
        talks = []
        for where, f in m['talk']:
            room, a, b, line = f
            if room not in ROOMS or placed.get(a) != room or placed.get(b) != room:
                raise Fail('%s: %s and %s are not both in the %s' % (where, a, b, room))
            if not talk_ok.get(frozenset((a, b)), True):
                raise Fail('%s: %s and %s do not talk (relations.csv)' % (where, a, b))
            talks.append(struct.pack(fmt(TALK), ROOMS.index(room), pilot_index[a], pilot_index[b], 0,
                                     text(line, TEXT['TALK'], where)))
        if len(talks) > LIMITS['TALKS']:
            raise Fail('%s: %d talk lines, at most %d' % (w, len(talks), LIMITS['TALKS']))
        fields = []
        for where, f in m['field']:
            if f[0] not in FIELDS or len(f) != 4:
                raise Fail('%s: a field is asteroid|mine x z radius' % where)
            fields.append(struct.pack(fmt(FIELD), FIELDS[f[0]], 0, int(f[1]), int(f[2]), int(f[3])))
        if len(fields) > LIMITS['FIELDS']:
            raise Fail('%s: %d fields, at most %d' % (w, len(fields), LIMITS['FIELDS']))
        rec = struct.pack('<%ds8B' % TEXT['ID'], text(m['id'], TEXT['ID'], w), node_index[m['node']],
                          TYPES[m['type']], MUSIC[m['music']], len(navs), len(shipr), len(talks), len(fields), 0)
        rec += text(m['title'], TEXT['TITLE'], w).ljust(TEXT['TITLE'], b'\0')
        rec += text(' '.join(m['briefing']), TEXT['BRIEFING'], w).ljust(TEXT['BRIEFING'], b'\0')
        rec += text(' '.join(m['win']), TEXT['RESULT'], w).ljust(TEXT['RESULT'], b'\0')
        rec += text(' '.join(m['lose']), TEXT['RESULT'], w).ljust(TEXT['RESULT'], b'\0')
        rec += b''.join(navs).ljust(LIMITS['NAVS'] * struct.calcsize(fmt(NAV)), b'\0')
        rec += b''.join(shipr).ljust(LIMITS['SHIPS'] * struct.calcsize(fmt(SHIP)), b'\0')
        rec += struct.pack('<6B2s', *seats, b'')
        rec += b''.join(talks).ljust(LIMITS['TALKS'] * struct.calcsize(fmt(TALK)), b'\0')
        rec += b''.join(fields).ljust(LIMITS['FIELDS'] * struct.calcsize(fmt(FIELD)), b'\0')
        out_missions.append(rec)

    out_pilots = [struct.pack(fmt(PILOT), text(p['key'], TEXT['KEY'], 'people.csv'),
                              text(p['callsign'] or p['key'].title(), TEXT['CALLSIGN'], 'people.csv'),
                              text(p['name'], TEXT['NAME'], 'people.csv'), ROLES[p['role']], 0,
                              text(p['line'], TEXT['LINE'], 'people.csv %s line' % p['key'])) for p in people]
    return out_pilots, out_nodes, out_missions, out_types, node_index[nodes[0]['node']]


def check_dag(nodes):
    edges = {n['node']: [n[k] for k in ('good', 'bad') if n[k] not in ('GOOD', 'BAD')] for n in nodes}
    state = {}

    def visit(v):
        if state.get(v) == 1:
            raise Fail('campaign.csv: a cycle through %s' % v)
        if state.get(v) == 2:
            return
        state[v] = 1
        for w in edges[v]:
            visit(w)
        state[v] = 2
    visit(nodes[0]['node'])
    left = [n['node'] for n in nodes if n['node'] not in state]
    if left:
        raise Fail('campaign.csv: unreachable %s' % left)


def c_struct(name, fields):
    lines = ['typedef struct {']
    for ctype, field, _f in fields:
        lines.append('    %s %s;' % (ctype, field))
    lines.append('} %s;' % name)
    return '\n'.join(lines)


def header_text(sizes):
    out = ['/* rvdata.h: RVGAME.DAT\'s records.  Generated by tools/rvdata.py: do not edit. */',
           '#ifndef RVDATA_H', '#define RVDATA_H', '',
           '#define RVGAME_MAGIC "RVGAME"', '#define RVGAME_VERSION %d' % VERSION, '']
    for k, v in LIMITS.items():
        out.append('#define RV_%s %d' % (k, v))
    out.append('#define RV_ROOMS %d' % len(ROOMS))
    out.append('#define RV_ENGINES %d' % ENGINES)
    out.append('#define RV_VITALS %d' % VITALS)
    for k, v in TEXT.items():
        out.append('#define RV_%s %d' % (k, v))
    out += ['', '/* node edges */', '#define RV_NODE_NONE 0x%02X' % NONE, '#define RV_NODE_GOOD 0x%02X' % GOOD,
            '#define RV_NODE_BAD 0x%02X' % BAD,
            '', '/* mission ship types picked at run time */',
            '#define RV_SHIP_PLAYER 0x%02X' % SHIP_PLAYER, '#define RV_SHIP_WINGMAN 0x%02X' % SHIP_WINGMAN,
            '#define RV_PILOT_NONE 0xFF', '']
    for i, r in enumerate(ROOMS):
        out.append('#define RV_ROOM_%s %d' % (r.upper(), i))
    for k, v in FIELDS.items():
        out.append('#define RV_FIELD_%s %d' % (k.upper(), v))
    for k, v in MUSIC.items():
        out.append('#define RV_MUSIC_%s %d' % (k.upper(), v))
    out.append('#define RV_MUSIC_COUNT %d' % len(MUSIC_FILES))
    out.append('#define RV_MUSIC_STINGS %d     /* from here on the cues are played once */' % MUSIC_FILES.index('VICTORY1'))
    out.append('#define RV_MUSIC_FILES %s' % ', '.join('"%s.MID"' % f for f in MUSIC_FILES))
    for k, v in ROLES.items():
        out.append('#define RV_ROLE_%s %d' % (k.upper(), v))
    out.append('#define RV_SHIPTYPE_FLAG_PLAYER %d' % SHIPTYPE_FLAG_PLAYER)
    out += ['', '#pragma pack(1)', c_struct('RvGameHeader', HEADER), c_struct('RvPilot', PILOT),
            c_struct('RvNode', NODE), c_struct('RvNav', NAV), c_struct('RvShip', SHIP), c_struct('RvTalk', TALK),
            c_struct('RvField', FIELD),
            c_struct('RvMission', MISSION), c_struct('RvShipType', SHIPTYPE), '#pragma pack()', '']
    for k, v in sizes.items():
        out.append('#define RV_%s_SIZE %d' % (k, v))
    # the quarter sine wave for the simulation's integer trigonometry (flight.c):
    # sin(i / 256 * 90 degrees) * 16384, i = 0..256
    out += ['', '/* sin(i/256 x 90 degrees) x 16384, i = 0..256: the simulation\'s trigonometry (flight.c) */',
            '#ifdef RV_TABLES', '#ifdef __WATCOMC__', '#define RV_FAR __far', '#else', '#define RV_FAR', '#endif',
            'static const short RV_FAR rv_qsin[257] = {']
    vals = [int(round(math.sin(i / 256 * math.pi / 2) * 16384)) for i in range(257)]
    for i in range(0, 257, 12):
        out.append('    ' + ', '.join('%d' % v for v in vals[i:i + 12]) + ',')
    out += ['};', '#endif']
    out += ['', '#endif', '']
    return '\n'.join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'dos', 'RVGAME.DAT'))
    ap.add_argument('--header', default=os.path.join(ROOT, 'src', 'rvdata.h'))
    args = ap.parse_args()
    try:
        pilots, nodes, missions, types, start = compile_all()
    except Fail as e:
        sys.exit('rvdata: %s' % e)
    sizes = {'HEADER': struct.calcsize(fmt(HEADER)), 'PILOT': len(pilots[0]), 'NODE': len(nodes[0]),
             'MISSION': len(missions[0]), 'SHIPTYPE': len(types[0])}
    hsize = sizes['HEADER']
    pilots_at = hsize
    nodes_at = pilots_at + len(pilots) * sizes['PILOT']
    missions_at = nodes_at + len(nodes) * sizes['NODE']
    types_at = missions_at + len(missions) * sizes['MISSION']
    head = struct.pack(fmt(HEADER), b'RVGAME', VERSION, hsize, len(pilots), sizes['PILOT'], pilots_at,
                       len(nodes), sizes['NODE'], nodes_at, len(missions), sizes['MISSION'], missions_at, start,
                       len(types), sizes['SHIPTYPE'], types_at, b'')
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, 'wb') as f:
        f.write(head + b''.join(pilots) + b''.join(nodes) + b''.join(missions) + b''.join(types))
    with open(args.header, 'w') as f:
        f.write(header_text(sizes))
    print('%s: %d pilots, %d nodes, %d missions (%d bytes each), %d ship types, %d bytes'
          % (args.out, len(pilots), len(nodes), len(missions), sizes['MISSION'], len(types),
             os.path.getsize(args.out)))


if __name__ == '__main__':
    main()
