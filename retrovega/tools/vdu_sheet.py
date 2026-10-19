#!/usr/bin/env python3
"""Contact sheet of the roster's Vega Strike VDU images, from data/ships.csv.

    vdu_sheet.py out.png [assets-dir]

assets-dir defaults to ~/dev/vegastrike-assets (a copy of Vega Strike's
Assets-Production; its VDU images are DDS files whatever their extension,
which Pillow reads).  Ships without a VDU image (the new designs) get a
labelled empty frame.
"""
import csv
import os
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
CELL, COLS, LABEL = 150, 7, 26
GROUPS = [
    ('TERRAN FIGHTERS', lambda s: s['faction'] == 'terran' and s['type'] < '0x10'),
    ('AERA FIGHTERS', lambda s: s['faction'] == 'aera' and s['type'] < '0x10'),
    ('RLAAN FIGHTERS', lambda s: s['faction'] == 'rlaan' and s['type'] < '0x20'),
    ('TERRAN CAPITAL SHIPS', lambda s: s['faction'] == 'terran' and s['type'] >= '0x20'),
    ('AERA CAPITAL SHIPS', lambda s: s['faction'] == 'aera' and s['type'] >= '0x20'),
    ('RLAAN CAPITAL SHIPS', lambda s: s['faction'] == 'rlaan' and s['type'] >= '0x20'),
]


def main():
    out = sys.argv[1]
    assets = sys.argv[2] if len(sys.argv) > 2 else os.path.expanduser('~/dev/vegastrike-assets')
    ships = list(csv.DictReader(open(os.path.join(HERE, '..', 'data', 'ships.csv'))))
    groups = [(t, [s for s in ships if f(s)]) for t, f in GROUPS]
    rows = sum((len(g) + COLS - 1) // COLS for _, g in groups)
    sheet = Image.new('RGB', (COLS * CELL, rows * (CELL + LABEL) + 18 * len(groups)), (20, 20, 28))
    d = ImageDraw.Draw(sheet)
    y = 0
    for title, group in groups:
        d.text((4, y + 3), title, fill=(255, 210, 110))
        y += 18
        for i, s in enumerate(group):
            if i and i % COLS == 0:
                y += CELL + LABEL
            x = (i % COLS) * CELL
            d.rectangle((x + 3, y + 1, x + CELL - 4, y + CELL - 4), outline=(50, 50, 64))
            if s['vdu']:
                im = Image.open(os.path.join(assets, 'units', 'vessels', s['vdu'])).convert('RGBA')
                im.thumbnail((CELL - 10, CELL - 10))
                bg = Image.new('RGBA', im.size, (0, 0, 0, 255))
                bg.alpha_composite(im)
                sheet.paste(bg.convert('RGB'), (x + 5 + (CELL - 10 - im.width) // 2, y + 3 + (CELL - 10 - im.height) // 2))
            else:
                d.text((x + 50, y + 60), 'NEW', fill=(255, 120, 120))
                d.text((x + 12, y + 78), s['notes'].split(':')[0].replace('NEW ', '')[:22], fill=(160, 160, 170))
            alias = ' (%s)' % s['vs_alias'] if s['vs_alias'] else ''
            d.text((x + 4, y + CELL - 2), '%s %s%s' % (s['type'], s['name'], alias), fill=(230, 230, 230))
            d.text((x + 4, y + CELL + 10), '%s, %s' % (s['class'], s['status']), fill=(140, 150, 170))
        y += CELL + LABEL
    sheet.save(out)


if __name__ == '__main__':
    main()
