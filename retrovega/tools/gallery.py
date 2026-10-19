#!/usr/bin/env python3
"""A roster gallery from rendered sprites (render_all.py's build/sprites).

    gallery.py OUT.png [--sprites build/sprites] [--views 10,4] [--cell 150]

Ships are grouped by faction, fighters then capital ships, each shown in the
given views in its default livery, labelled, on black like space.
"""
import argparse
import csv
import os

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
GROUPS = [
    ('TERRAN FIGHTERS AND SMALL CRAFT', lambda r: r['faction'] == 'terran' and r['type'] < '0x10'),
    ('AERA FIGHTERS', lambda r: r['faction'] == 'aera' and r['type'] < '0x10'),
    ('RLAAN FIGHTERS', lambda r: r['faction'] == 'rlaan' and r['type'] < '0x20'),
    ('TERRAN CAPITAL SHIPS', lambda r: r['faction'] == 'terran' and r['type'] >= '0x20'),
    ('AERA CAPITAL SHIPS', lambda r: r['faction'] == 'aera' and r['type'] >= '0x20'),
    ('RLAAN CAPITAL SHIPS', lambda r: r['faction'] == 'rlaan' and r['type'] >= '0x20'),
]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('out')
    ap.add_argument('--sprites', default=os.path.join(HERE, '..', 'build', 'sprites'))
    ap.add_argument('--views', default='10,4')
    ap.add_argument('--cell', type=int, default=150)
    args = ap.parse_args()
    views = [int(v) for v in args.views.split(',')]
    ships = [r for r in csv.DictReader(open(os.path.join(HERE, '..', 'data', 'ships.csv'))) if r['mesh']]
    art = {r['key']: r for r in csv.DictReader(open(os.path.join(HERE, '..', 'data', 'art.csv')))}
    cell, per_row = args.cell, 4
    width = per_row * len(views) * cell
    blocks = []
    for title, keep in GROUPS:
        group = [r for r in ships if keep(r)]
        if group:
            blocks.append((title, group))
    height = sum(18 + ((len(g) + per_row - 1) // per_row) * (cell + 14) for _, g in blocks)
    img = Image.new('RGB', (width, height), (0, 0, 0))
    d = ImageDraw.Draw(img)
    y = 0
    for title, group in blocks:
        d.text((4, y + 3), title, fill=(255, 210, 110))
        y += 18
        for i, r in enumerate(group):
            x = (i % per_row) * len(views) * cell
            if i and i % per_row == 0:
                y += cell + 14
            livery = (art.get(r['key']) or {}).get('livery') or 'none'
            for j, v in enumerate(views):
                path = os.path.join(args.sprites, r['key'], '%s-%s_%02d.png' % (r['key'], livery, v))
                if not os.path.exists(path):
                    continue
                im = Image.open(path)
                im.thumbnail((cell, cell), Image.LANCZOS)
                img.paste(im, (x + j * cell + (cell - im.width) // 2, y + (cell - im.height) // 2), im)
            label = '%s %s' % (r['type'], r['name']) + (' (%s)' % livery if livery != 'none' else '')
            d.text((x + 4, y + cell), label, fill=(220, 220, 220))
        y += cell + 14
    img.save(args.out)
    print(args.out, img.size)


if __name__ == '__main__':
    main()
