#!/usr/bin/env python3
"""The campaign DAG and its placeholder missions (docs/engine.md).

    campaign.py            write data/campaign.csv and data/missions.txt

The campaign: a start node, six layers of nodes, then the two endings GOOD
and BAD.  Layer 1 is the start (column 0), layer 2 has columns -1..1,
layers 3-6 have -2..2; negative columns lean towards GOOD.  A node is four
missions.  Each node has up to four edges, and the engine picks one after
the node's four missions:

    down_good, down_bad    the next layer, one column towards that end
                           (from layer 6: the ending itself)
    side_good, side_bad    the same layer, one column towards that end,
                           only where that moves away from the centre
    down                   the next layer, the same column (from layer 6:
                           END, the ending the trend points at)

The engine's rule: 3-4 wins trend GOOD, 0-1 BAD, 2 follow the campaign's
running total; 4 or 0 wins go down towards the trend, a mixed result goes
sideways towards it if there is such a side edge, else straight down.  Side edges only ever move outwards,
so there is no cycle; this tool checks that anyway.

Every mission is a placeholder patrol for now: a system, nav points, an
Aera flight that grows with the layer, the player and a wingman, who sits
where in the bar and the lounge (only seats their art was drawn for,
never two pilots who will not talk at one table, each pilot in one place
at most), and a line or two of table talk.  Seeded, so it is the same
every run.  Edit missions.txt by hand later; tools/rvdata.py checks it.
"""
import csv
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, '..', 'data')
LAYERS = 6
WIDTH = {1: 0, 2: 1}            # half-width per layer, 2 beyond
MISSIONS_PER_NODE = 4

# who may sit where (docs/places.md: the seat art); Kit is always at the deck
SEATS = {
    'bar': {'left': ['FROST', 'TORCH'], 'middle': ['PRIME', 'KESTREL', 'ECHO'], 'right': ['DRIFTER', 'COMET', 'ANCHOR']},
    'lounge': {'left': ['KESTREL', 'COMET'], 'middle': ['ECHO', 'TORCH', 'DRIFTER'], 'right': ['ANCHOR', 'FROST', 'PRIME']},
}
WINGMEN = ['ECHO', 'TORCH', 'COMET', 'FROST', 'KESTREL', 'DRIFTER', 'PRIME', 'ANCHOR']
SYSTEMS = ['the Rigel relay', 'Cephid Gate', 'the Enigma shipyards', 'Shelton', 'the Crucible picket',
           'Vega Prime', 'the Sol frontier', 'Defiance', 'the Lixis beacon', 'Ataraxia']
ENEMIES = [['NICANDR'], ['NICANDR', 'AGIS'], ['NICANDR', 'ARISTON'], ['ARISTON', 'AREUS'],
           ['ARISTON', 'CLEOMEN', 'CONVOLN'], ['AREUS', 'CLEOMEN', 'CONVOLN']]
TALK = {
    'twins': ['Somebody has to fly the checklist.', 'You only say that because I am the spare.'],
    'banter': ['Ten credits says you come back first.', "You'd bet on your own funeral."],
    'friends': ['Watch my six out there.', 'Always.'],
    'rivals': ['You missed again, I counted.', 'Count the kills, not the misses.'],
    'none': ['Quiet out there lately.', 'Too quiet.'],
}


def width(layer):
    return WIDTH.get(layer, 2)


def node_id(layer, col):
    return 'L%d%s%d' % (layer, 'm' if col < 0 else 'p' if col > 0 else 'c', abs(col))


def build_nodes():
    nodes = []
    for layer in range(1, LAYERS + 1):
        w = width(layer)
        for col in range(-w, w + 1):
            n = {'node': node_id(layer, col), 'layer': layer, 'col': col}
            if layer == LAYERS:
                n['down_good'], n['down_bad'], n['down'] = 'GOOD', 'BAD', 'END'
            else:
                nw = width(layer + 1)
                n['down_good'] = node_id(layer + 1, max(col - 1, -nw))
                n['down_bad'] = node_id(layer + 1, min(col + 1, nw))
                n['down'] = node_id(layer + 1, col)
            n['side_good'] = node_id(layer, col - 1) if col <= 0 and col - 1 >= -w else ''
            n['side_bad'] = node_id(layer, col + 1) if col >= 0 and col + 1 <= w else ''
            n['missions'] = ' '.join('%s_m%d' % (n['node'], k + 1) for k in range(MISSIONS_PER_NODE))
            nodes.append(n)
    return nodes


def check_dag(nodes):
    """No cycles, every node reachable from the start, both endings reachable."""
    by = {n['node']: n for n in nodes}
    edges = {n['node']: [n[k] for k in ('down_good', 'down_bad', 'down', 'side_good', 'side_bad')
                         if n[k] and n[k] != 'END'] for n in nodes}
    state = {}

    def visit(v, path):
        if v in ('GOOD', 'BAD'):
            return
        if state.get(v) == 1:
            sys.exit('cycle: %s' % ' -> '.join(path + [v]))
        if state.get(v) == 2:
            return
        state[v] = 1
        for w in edges[v]:
            if w not in by and w not in ('GOOD', 'BAD'):
                sys.exit('%s: edge to unknown %s' % (v, w))
            visit(w, path + [v])
        state[v] = 2
    start = nodes[0]['node']
    visit(start, [])
    unreached = [n['node'] for n in nodes if n['node'] not in state]
    if unreached:
        sys.exit('unreachable: %s' % unreached)
    ends = {w for v in edges for w in edges[v] if w in ('GOOD', 'BAD')}
    if ends != {'GOOD', 'BAD'}:
        sys.exit('endings reached: %s' % ends)


def relations():
    talk = {}
    with open(os.path.join(DATA, 'relations.csv')) as f:
        for r in csv.DictReader(f):
            talk[frozenset((r['a'], r['b']))] = (r['talk'] == 'yes', r['relation'])
    return talk


def may_talk(rel, a, b):
    return rel.get(frozenset((a, b)), (True, 'none'))[0]


def seat_room(rng, room, used, rel):
    """A seating: seat -> pilot or None, everyone at the table on speaking terms."""
    for _ in range(200):
        out = {}
        for seat, who in SEATS[room].items():
            pick = rng.choice(who + [None])
            out[seat] = pick if pick not in used else None
        people = [p for p in out.values() if p]
        if len(set(people)) == len(people) and all(may_talk(rel, a, b) for a in people for b in people if a < b):
            return out
    return {seat: None for seat in SEATS[room]}


def mission_text(rng, n, k, rel):
    layer, mid = n['layer'], '%s_m%d' % (n['node'], k + 1)
    system = rng.choice(SYSTEMS)
    foes = ENEMIES[layer - 1]
    wing = rng.choice(WINGMEN)
    lines = ['mission  %s' % mid,
             'node     %s' % n['node'],
             'title    Patrol: %s' % system,
             'type     patrol',
             'music    briefing',
             'briefing Pilots, listen up. Aera fighters have been sighted near %s. '
             'You and %s will sweep Nav 1 and Nav 2, then return to the carrier. '
             'Stay in formation and keep the chatter down.' % (system, wing.title()),
             'win      We hold %s. Good flying.' % system,
             'lose     We lost %s. Get some rest; we go again tomorrow.' % system,
             'nav      1 %d 0 %d' % (rng.randint(-1500, 1500), rng.randint(800, 2500)),
             'nav      2 %d 0 %d' % (rng.randint(-3000, 3000), rng.randint(2500, 5000)),
             'ship     player 0 nav0',
             'ship     wingman 0 nav0 pilot=%s' % wing]
    for nav, foe in zip((1, 2), [rng.choice(foes) for _ in range(2)]):
        lines.append('ship     %s 1 nav%d count=%d' % (foe, nav, min(4, 1 + (layer + rng.randint(0, 2)) // 2)))
    used = {wing}
    talks = []
    for room in ('bar', 'lounge'):
        seats = seat_room(rng, room, used, rel)
        used |= {p for p in seats.values() if p}
        lines.append('%-8s %s' % (room, ' '.join('%s=%s' % (s, p or '-') for s, p in seats.items())))
        people = [p for p in seats.values() if p]
        if len(people) >= 2:
            a, b = rng.sample(people, 2)
            kind = rel.get(frozenset((a, b)), (True, 'none'))[1]
            first, second = TALK.get(kind, TALK['none'])
            talks.append('talk     %s %s %s "%s"' % (room, a, b, first))
            talks.append('talk     %s %s %s "%s"' % (room, b, a, second))
    return '\n'.join(lines + talks)


def main():
    nodes = build_nodes()
    check_dag(nodes)
    with open(os.path.join(DATA, 'campaign.csv'), 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=['node', 'layer', 'col', 'missions', 'down_good', 'down_bad', 'down',
                                          'side_good', 'side_bad'], lineterminator='\n')
        w.writeheader()
        w.writerows(nodes)
    rel = relations()
    rng = random.Random(1)
    blocks = []
    for n in nodes:
        for k in range(MISSIONS_PER_NODE):
            blocks.append(mission_text(rng, n, k, rel))
    with open(os.path.join(DATA, 'missions.txt'), 'w') as f:
        f.write('# The missions (docs/engine.md).  Placeholders written by tools/campaign.py;\n'
                '# edit freely: tools/rvdata.py checks every block.\n\n')
        f.write('\n\n'.join(blocks) + '\n')
    print('%d nodes, %d missions; the DAG is acyclic and reaches both endings' % (len(nodes), len(blocks)))


if __name__ == '__main__':
    main()
