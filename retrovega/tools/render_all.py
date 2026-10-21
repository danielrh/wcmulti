#!/usr/bin/env python3
"""Render every ship of data/ships.csv that has a mesh, and a gallery.

    render_all.py [--out build/sprites] [--size PX] [--only KEY,KEY|faction]
                  [--livery NAME] [--view N] [--gallery FILE] [--step 30|15]

Each ship goes to OUT/KEY/ (shipviews.py's files); --size overrides every
ship's lod0_px (a quick survey); --livery overrides each ship's default;
the gallery shows view N (default 10: 30 degrees above, heading 60) of
every ship rendered, labelled, on black.
"""
import argparse
import csv
import os
import sys
import time

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import shipviews  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', default=os.path.join(HERE, '..', 'build', 'sprites'))
    ap.add_argument('--size', type=int, default=0)
    ap.add_argument('--only', default='')
    ap.add_argument('--livery', default=None)
    ap.add_argument('--view', type=int, default=10)
    ap.add_argument('--gallery', default='')
    ap.add_argument('--cell', type=int, default=200, help='gallery cell size')
    ap.add_argument('--assets', default=os.path.expanduser('~/dev/vegastrike-assets'))
    ap.add_argument('--step', type=int, default=30, help='the views\' angular step: 30 (37 views) or 15 (145)')
    args = ap.parse_args()
    only = set(filter(None, args.only.split(',')))
    rows = [r for r in csv.DictReader(open(os.path.join(HERE, '..', 'data', 'ships.csv')))
            if r['mesh'] and (not only or r['key'] in only or r['faction'] in only)]
    done = []
    for row in rows:
        t = time.time()
        size = args.size or int(row['lod0_px'])
        outdir = os.path.join(args.out, row['key'])
        try:
            shipviews.render(row, outdir, size, args.assets, args.livery, step=args.step)
        except Exception as e:  # keep going: the survey wants every failure
            print('%-10s FAILED %s' % (row['key'], e))
            continue
        art = shipviews.table_row('art.csv', row['key']) or {}
        livery = args.livery or art.get('livery') or 'none'
        done.append((row, os.path.join(outdir, '%s-%s_%02d.png' % (row['key'], livery, args.view))))
        print('%-10s %4d px %5.1f s' % (row['key'], size, time.time() - t))
    if args.gallery and done:
        cell, cols = args.cell, 6
        rows_n = (len(done) + cols - 1) // cols
        g = Image.new('RGB', (cols * cell, rows_n * (cell + 16)), (0, 0, 0))
        d = ImageDraw.Draw(g)
        for i, (row, path) in enumerate(done):
            x, y = (i % cols) * cell, (i // cols) * (cell + 16)
            im = Image.open(path)
            im.thumbnail((cell, cell), Image.LANCZOS)
            g.paste(im, (x + (cell - im.width) // 2, y + (cell - im.height) // 2), im)
            d.text((x + 4, y + cell + 2), '%s %s' % (row['type'], row['name']), fill=(220, 220, 220))
        g.save(args.gallery)
        print('gallery', args.gallery)


if __name__ == '__main__':
    main()
