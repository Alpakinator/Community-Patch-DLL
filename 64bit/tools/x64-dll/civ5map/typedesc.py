"""Write the game's world builder type lists from a gameplay database, in the DLL's order (ticket 0343).

    python -I typedesc.py --db Civ5DebugDatabase.db --out types.txt

The DLL's InitTypeDesc (CvWorldBuilderMapLoader.cpp) adds, for every ID from 0 to the info count, the row's Type or
NULL when the row is missing: terrains, features (also the natural-wonder list), resources, improvements, units,
techs, policies, buildings, promotions. Here "-" stands for NULL. The database is opened read only.
"""
import argparse
import sqlite3
import sys

SECTIONS = [('terrain', 'Terrains'), ('features', 'Features'), ('resources', 'Resources'),
            ('improvements', 'Improvements'), ('units', 'Units'), ('techs', 'Technologies'),
            ('policies', 'Policies'), ('buildings', 'Buildings'), ('promotions', 'UnitPromotions')]


def write(db, out):
    con = sqlite3.connect('file:%s?mode=ro' % db.replace('\\', '/'), uri=True)
    lines = []
    counts = {}
    for tag, table in SECTIONS:
        rows = dict(con.execute('select ID, Type from %s' % table).fetchall())
        n = (max(rows) + 1) if rows else 0
        lines.append('[%s]' % tag)
        lines += [rows.get(i) or '-' for i in range(n)]
        counts[tag] = n
    con.close()
    with open(out, 'w', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    return counts


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--db', required=True)
    ap.add_argument('--out', required=True)
    a = ap.parse_args(argv)
    try:
        counts = write(a.db, a.out)
    except (sqlite3.Error, OSError) as e:
        print('typedesc: FAILED: %s' % e, file=sys.stderr)
        return 2
    print('typedesc: %s' % ' '.join('%s=%d' % kv for kv in counts.items()))
    return 0


if __name__ == '__main__':
    sys.exit(main())
