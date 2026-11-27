"""Compare two stats.db files (the DLL's SQLITE_LOGGING output) under decision D24 parts 2 and 3: every table, every
column, nothing excluded but the process measurements below.

  python tools/d10/statscmp.py A.db|RUNDIR B.db|RUNDIR [--from-turn T] [--expect-turns LO HI] [--json OUT]

Skipped (process measurements and run identities, never game output; the set tools/i0/ab.py defines, listed in
every report):
  tables  Mem* (MemoryDiagnostics, MemoryHooks, MemoryImports: the process's memory), run_dictionary (one row per
          process), sqlite_* (SQLite bookkeeping, e.g. sqlite_sequence)
  columns RunId (every table: time(NULL) at the logger's start), uuid_dictionary.uuid_hex (the game's GUID, not drawn
          from the seeded RNG: differs between two runs of the same DLL, ab.py's A/A check of 2026-11-10)
Everything else is compared, MilitarySummary.MostEndangeredCity and ReligionBeliefYields.IsReligionOwner included.

Rows are compared in rowid (insertion) order. --from-turn T keeps only rows with Turn >= T in tables that have a Turn
column (tables without one are compared whole): the window used to compare a continuation from a save of turn T with
a direct play. For every differing column the report gives the number of differing rows, the first differing turn and
the first pair of values; it also reports row-count differences and whether the rows differ only in order. Cells are
compared with their storage type: an INTEGER 5 and a REAL 5.0 differ.

Plausibility (issue 9011): two equal databases prove nothing when they are empty. The comparison is accepted only if
at least one row was compared and every per-turn table (PER_TURN_TABLES) is present with rows (in the --from-turn
window) on both sides; with --expect-turns LO HI each of them must also have rows for every turn LO+1..HI on both
sides (a game that stopped early or logged little fails).

Exit 0 when identical and plausible, 1 otherwise. Standard-library Python 3.11. Written 2026-11-10 by team-2400
worker B (issue 2403); plausibility and typed cells by one-off 9011 (2026-11-10).
"""
from __future__ import annotations

import argparse
import json
import sqlite3
import sys
from collections import Counter
from pathlib import Path

SKIP_TABLE_PREFIXES = ('Mem', 'sqlite_')
SKIP_TABLES = {'run_dictionary': 'one row per process (instrumented builds)'}
SKIP_COLUMNS = {'RunId': 'time(NULL) at the logger start, every table (instrumented builds)'}
SKIP_TABLE_COLUMNS = {('uuid_dictionary', 'uuid_hex'): "the game's GUID, not seeded; differs A/A (tools/i0/ab.py)"}


def skipped_rules() -> dict:
    return {'tablePrefixes': {'Mem': "the process's memory instruments", 'sqlite_': 'SQLite bookkeeping'},
            'tables': SKIP_TABLES, 'columns': SKIP_COLUMNS,
            'tableColumns': {f'{t}.{c}': r for (t, c), r in SKIP_TABLE_COLUMNS.items()}}


def resolve(p) -> Path:
    p = Path(p)
    if p.is_dir():
        p = p / 'cache' / 'stats.db'
    if not p.is_file():
        raise FileNotFoundError(f'no stats.db: {p}')
    return p


def _q(n: str) -> str:
    return '"' + n.replace('"', '""') + '"'


def _tables(con) -> dict[str, list[str]]:
    out = {}
    for (n,) in con.execute("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name"):
        out[n] = [r[1] for r in con.execute(f'PRAGMA table_info({_q(n)})')]
    return out


def _skip_table(n: str) -> bool:
    return n in SKIP_TABLES or n.startswith(SKIP_TABLE_PREFIXES)


def _cell(v):
    """A REAL cell is compared with its storage type: 5 (INTEGER) and 5.0 (REAL) differ (Python's 5 == 5.0)."""
    return ('REAL', v) if isinstance(v, float) else v


def _rows(con, table, cols, from_turn):
    sel = ', '.join(_q(c) for c in cols) or "''"  # a table whose only columns are skipped: count rows
    where = f' WHERE "Turn" >= {int(from_turn)}' if from_turn is not None and 'Turn' in cols else ''
    return [tuple(_cell(v) for v in r)
            for r in con.execute(f'SELECT {sel} FROM {_q(table)}{where} ORDER BY rowid').fetchall()]


# Tables the DLL writes on every turn of a game (measured 2026-11-10 on the tiny, standard and huge D10 games: one or
# more rows for every turn of the game, of a continuation and of the huge save's 50 turns). Each must be present and
# non-empty in both databases; with --expect-turns LO HI it must have rows for every turn LO+1..HI in both.
PER_TURN_TABLES = ('WorldStateLog', 'civ_turn_era', 'MapPlotsState', 'MapUnitsState', 'MilitarySummary',
                   'BuildingYields', 'BuildingsOverview')


def _coverage(con, table: str, lo: int, hi: int) -> list[int]:
    """Turns in lo+1..hi without a row in table."""
    have = {r[0] for r in con.execute(f'SELECT DISTINCT "Turn" FROM {_q(table)} WHERE "Turn" > ? AND "Turn" <= ?',
                                      (lo, hi))}
    return [t for t in range(lo + 1, hi + 1) if t not in have]


def compare(pa, pb, from_turn: int | None = None, expect_turns: tuple[int, int] | None = None) -> dict:
    pa, pb = resolve(pa), resolve(pb)
    ca = sqlite3.connect(f'file:{pa.as_posix()}?mode=ro', uri=True)
    cb = sqlite3.connect(f'file:{pb.as_posix()}?mode=ro', uri=True)
    ta, tb = _tables(ca), _tables(cb)
    rep = {'a': str(pa), 'b': str(pb), 'fromTurn': from_turn, 'skippedRules': skipped_rules(),
           'skippedTables': sorted(t for t in set(ta) | set(tb) if _skip_table(t)),
           'skippedColumns': [], 'onlyInA': sorted(t for t in ta if t not in tb and not _skip_table(t)),
           'onlyInB': sorted(t for t in tb if t not in ta and not _skip_table(t)), 'tables': []}
    for name in sorted(set(ta) & set(tb)):
        if _skip_table(name):
            continue
        keep = lambda c: c not in SKIP_COLUMNS and (name, c) not in SKIP_TABLE_COLUMNS  # noqa: E731
        for c in ta[name]:
            if not keep(c):
                rep['skippedColumns'].append(f'{name}.{c}')
        cols_a = [c for c in ta[name] if keep(c)]
        cols_b = [c for c in tb[name] if keep(c)]
        e = {'table': name, 'columns': len(cols_a)}
        if cols_a != cols_b:
            e['schemaDiffers'] = {'a': cols_a, 'b': cols_b}
            cols = [c for c in cols_a if c in cols_b]
        else:
            cols = cols_a
        ra, rb = _rows(ca, name, cols, from_turn), _rows(cb, name, cols, from_turn)
        e['rowsA'], e['rowsB'] = len(ra), len(rb)
        ti = cols.index('Turn') if 'Turn' in cols else None
        if ti is not None:
            e['turnsA'] = [min((r[ti] for r in ra), default=None), max((r[ti] for r in ra), default=None)]
            e['turnsB'] = [min((r[ti] for r in rb), default=None), max((r[ti] for r in rb), default=None)]
        if ra != rb:
            # per-column differences over the rows both have (rowid order)
            percol: dict[str, dict] = {}
            first_row = None
            for idx, (x, y) in enumerate(zip(ra, rb)):
                if x == y:
                    continue
                if first_row is None:
                    first_row = {'index': idx, 'a': dict(zip(cols, x)), 'b': dict(zip(cols, y))}
                for ci, c in enumerate(cols):
                    if x[ci] != y[ci]:
                        d = percol.get(c)
                        if d is None:
                            percol[c] = {'rows': 1, 'firstIndex': idx, 'firstTurn': None if ti is None else x[ti],
                                         'a': x[ci], 'b': y[ci]}
                        else:
                            d['rows'] += 1
            e['columnsDiffering'] = percol
            e['firstDifferingRow'] = first_row
            ma, mb = Counter(ra), Counter(rb)
            e['rowsOnlyInA'] = sum((ma - mb).values())
            e['rowsOnlyInB'] = sum((mb - ma).values())
            e['sameRowsOtherOrder'] = ma == mb
            if len(ra) != len(rb) and ti is not None:
                pa_, pb_ = Counter(r[ti] for r in ra), Counter(r[ti] for r in rb)
                bad = sorted(t for t in set(pa_) | set(pb_) if pa_[t] != pb_[t])
                e['firstTurnRowCountDiffers'] = bad[0] if bad else None
                if bad:
                    e['rowCountAtThatTurn'] = [pa_[bad[0]], pb_[bad[0]]]
            if ti is not None:
                diff_turns = [r[ti] for r in (ma - mb)] + [r[ti] for r in (mb - ma)]
                e['firstTurnDiffering'] = min(diff_turns) if diff_turns else None
        e['identical'] = ra == rb and 'schemaDiffers' not in e
        rep['tables'].append(e)
    # plausibility: identical empty databases are no evidence (issue 9011)
    prob = rep['plausibility'] = []
    byname = {t['table']: t for t in rep['tables']}
    for name in PER_TURN_TABLES:
        t = byname.get(name)
        if t is None:
            prob.append(f'{name}: missing (a table the DLL writes every turn)')
            continue
        if t['rowsA'] == 0 or t['rowsB'] == 0:
            prob.append(f"{name}: {t['rowsA']} / {t['rowsB']} rows" +
                        (f' with Turn >= {from_turn}' if from_turn is not None else ''))
            continue
        if expect_turns is not None:
            lo, hi = expect_turns
            for side, con in (('A', ca), ('B', cb)):
                miss = _coverage(con, name, lo, hi)
                if miss:
                    prob.append(f'{name}: {side} has no row for {len(miss)} of the {hi - lo} turns {lo + 1}..{hi} '
                                f'(first {miss[:5]})')
    rep['expectTurns'] = list(expect_turns) if expect_turns is not None else None
    ca.close()
    cb.close()
    rep['tablesCompared'] = len(rep['tables'])
    rep['rowsCompared'] = sum(min(t['rowsA'], t['rowsB']) for t in rep['tables'])
    rep['identical'] = (not rep['onlyInA'] and not rep['onlyInB'] and all(t['identical'] for t in rep['tables'])
                        and rep['tablesCompared'] > 0 and rep['rowsCompared'] > 0 and not prob)
    return rep


def text(rep: dict) -> str:
    L = [f"A {rep['a']}", f"B {rep['b']}"]
    if rep['fromTurn'] is not None:
        L.append(f"window: rows with Turn >= {rep['fromTurn']} (tables without Turn compared whole)")
    L.append(f"skipped tables: {', '.join(rep['skippedTables']) or 'none present'}; skipped columns: "
             f"{', '.join(sorted(set(rep['skippedColumns']))) or 'none present'} (process measurements, see rules)")
    if rep['onlyInA']:
        L.append(f"TABLES ONLY IN A: {rep['onlyInA']}")
    if rep['onlyInB']:
        L.append(f"TABLES ONLY IN B: {rep['onlyInB']}")
    for t in rep['tables']:
        tr = f" turns {t['turnsA']}/{t['turnsB']}" if 'turnsA' in t else ''
        L.append(f"  {t['table']:<24} {t['rowsA']:>8} {t['rowsB']:>8} {t['columns']:>3} cols{tr}  "
                 f"{'same' if t['identical'] else 'DIFFERENT'}")
    for t in rep['tables']:
        if t['identical']:
            continue
        L.append(f"--- {t['table']}: rows {t['rowsA']} vs {t['rowsB']}, {t.get('rowsOnlyInA')} rows only in A, "
                 f"{t.get('rowsOnlyInB')} only in B, first differing turn {t.get('firstTurnDiffering')}"
                 + (' (same rows, other order)' if t.get('sameRowsOtherOrder') else ''))
        if 'schemaDiffers' in t:
            L.append(f"  schema A {t['schemaDiffers']['a']}\n  schema B {t['schemaDiffers']['b']}")
        if t.get('firstTurnRowCountDiffers') is not None:
            L.append(f"  row count first differs at turn {t['firstTurnRowCountDiffers']}: {t['rowCountAtThatTurn']}")
        for c, d in t.get('columnsDiffering', {}).items():
            L.append(f"  column {c}: {d['rows']} rows (rowid order), first at row {d['firstIndex']} turn "
                     f"{d['firstTurn']}: A {d['a']!r} / B {d['b']!r}")
    for p in rep.get('plausibility', []):
        L.append(f"IMPLAUSIBLE: {p}")
    if rep.get('expectTurns'):
        L.append(f"expected turns {rep['expectTurns'][0] + 1}..{rep['expectTurns'][1]} in every per-turn table "
                 f"({', '.join(PER_TURN_TABLES)})")
    same = all(t['identical'] for t in rep['tables']) and not rep['onlyInA'] and not rep['onlyInB']
    verdict = 'IDENTICAL' if rep['identical'] else \
        'NOT ACCEPTED (tables equal but the plausibility check failed)' if same else 'DIFFERENT'
    L.append(f"{rep['tablesCompared']} tables, {rep['rowsCompared']} rows compared: {verdict}")
    return '\n'.join(L) + '\n'


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('a')
    ap.add_argument('b')
    ap.add_argument('--from-turn', type=int)
    ap.add_argument('--expect-turns', type=int, nargs=2, metavar=('LO', 'HI'),
                    help='the game ran from turn LO to HI: every per-turn table must have rows for LO+1..HI')
    ap.add_argument('--json')
    a = ap.parse_args(argv)
    rep = compare(a.a, a.b, a.from_turn, tuple(a.expect_turns) if a.expect_turns else None)
    if a.json:
        Path(a.json).write_text(json.dumps(rep, indent=1, default=str), encoding='utf-8')
    print(text(rep), end='')
    return 0 if rep['identical'] else 1


if __name__ == '__main__':
    sys.exit(main())
