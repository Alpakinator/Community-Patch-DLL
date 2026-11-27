"""Negative and positive tests of tools/d10/statscmp.py (issue 9011): the verification's D01-D13 cases on small
synthetic stats.db files with the DLL's per-turn tables. On main, D06 (all tables empty), D09 (INTEGER 5 vs REAL 5.0)
and D12 (--from-turn past the last turn) were accepted; they fail here, as does a game with missing turns.

  python -m unittest discover -s tools/d10/tests -v
"""
from __future__ import annotations

import shutil
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import statscmp  # noqa: E402

TURNS = (0, 30)  # a game from turn 0 to 30
# the tables of PER_TURN (spelled out so that this file also runs against main's statscmp)
PER_TURN = getattr(statscmp, 'PER_TURN_TABLES', ('WorldStateLog', 'civ_turn_era', 'MapPlotsState', 'MapUnitsState',
                                                 'MilitarySummary', 'BuildingYields', 'BuildingsOverview'))


def compare(a, b, ft=None, ex=None):
    try:
        return statscmp.compare(a, b, ft, ex)
    except TypeError:  # main's statscmp: no expected turns
        return statscmp.compare(a, b, ft)


def make_db(path: Path, lo=TURNS[0], hi=TURNS[1], skip_turns=()):
    con = sqlite3.connect(path)
    for t in PER_TURN:
        con.execute(f'CREATE TABLE "{t}" (Turn INTEGER, Player INTEGER, Value, Share REAL)')
        for turn in range(lo, hi + 1):
            if turn in skip_turns:
                continue
            for pl in range(3):
                con.execute(f'INSERT INTO "{t}" VALUES (?, ?, ?, ?)', (turn, pl, turn * 10 + pl, pl / 4))
    con.execute('CREATE TABLE TechChoices (Turn INTEGER, Player INTEGER, Tech TEXT)')
    con.execute("INSERT INTO TechChoices VALUES (5, 1, 'TECH_POTTERY')")
    con.execute('CREATE TABLE uuid_dictionary (uuid_id INTEGER, uuid_hex TEXT)')
    con.execute("INSERT INTO uuid_dictionary VALUES (1, 'abc')")
    con.commit()
    con.close()
    return path


class StatsCmp(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='d10-statscmp-')
        cls.W = Path(cls.tmp.name)
        cls.a = make_db(cls.W / 'A.db')
        cls.b = make_db(cls.W / 'B.db')

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def mutate(self, name, sqls, src=None):
        p = self.W / name
        shutil.copy2(src or self.b, p)
        con = sqlite3.connect(p)
        for s in ([sqls] if isinstance(sqls, str) else sqls):
            con.execute(s)
        con.commit()
        con.close()
        return p

    def same(self, a, b, ft=None, ex=TURNS):
        r = compare(a, b, ft, ex)
        self.assertTrue(r['identical'], statscmp.text(r))
        return r

    def differ(self, a, b, ft=None, ex=None):
        r = compare(a, b, ft, ex)
        self.assertFalse(r['identical'], statscmp.text(r))
        return r

    def test_D01_baseline(self):
        self.same(self.a, self.b)

    def test_D02_D03_D04_cell_row_table(self):
        self.differ(self.a, self.mutate('D02.db', 'UPDATE MapUnitsState SET Value = Value + 1 WHERE rowid = 7'))
        self.differ(self.a, self.mutate('D03.db', 'DELETE FROM MapUnitsState WHERE rowid = (SELECT max(rowid) '
                                                  'FROM MapUnitsState)'))
        self.differ(self.a, self.mutate('D04.db', 'DROP TABLE TechChoices'))

    def test_D05_no_tables(self):
        e1, e2 = self.W / 'E1.db', self.W / 'E2.db'
        for p in (e1, e2):
            sqlite3.connect(p).close()
        self.differ(e1, e2)

    def test_D06_every_table_present_zero_rows(self):
        """Accepted on main."""
        dels = [f'DELETE FROM "{t}"' for t in PER_TURN + ('TechChoices', 'uuid_dictionary')]
        r = self.differ(self.mutate('Z1.db', dels, self.a), self.mutate('Z2.db', dels, self.b))
        self.assertTrue(r.get('plausibility'))

    def test_D07_one_side_zero_rows(self):
        dels = [f'DELETE FROM "{t}"' for t in PER_TURN]
        self.differ(self.a, self.mutate('D07.db', dels))

    def test_D08_uuid_hex_skipped(self):
        self.same(self.a, self.mutate('D08.db', "UPDATE uuid_dictionary SET uuid_hex = '00'"))

    def test_D09_integer_against_equal_real(self):
        """Accepted on main (5 == 5.0 in Python)."""
        self.differ(self.a, self.mutate('D09.db', 'UPDATE MapUnitsState SET Value = CAST(Value AS REAL) '
                                                  'WHERE rowid = 5'))

    def test_D10_rows_swapped(self):
        p = self.mutate('D10.db', [
            'CREATE TEMP TABLE x AS SELECT rowid AS r, * FROM MapUnitsState WHERE rowid IN (1, 2)',
            'DELETE FROM MapUnitsState WHERE rowid IN (1, 2)',
            'INSERT INTO MapUnitsState (rowid, Turn, Player, Value, Share) SELECT 3 - r, Turn, Player, Value, Share '
            'FROM x'])
        self.differ(self.a, p)

    def test_D11_extra_column(self):
        self.differ(self.a, self.mutate('D11.db', 'ALTER TABLE MapUnitsState ADD COLUMN Extra INTEGER'))

    def test_D12_from_turn_past_the_last_turn(self):
        """Accepted on main: nothing but tables without Turn was compared."""
        self.differ(self.a, self.b, ft=TURNS[1] + 1)

    def test_D13_missing_db(self):
        with self.assertRaises(FileNotFoundError):
            statscmp.compare(self.a, self.W / 'nonexistent.db')

    def test_far_fewer_turns_than_played(self):
        short_a = make_db(self.W / 'SA.db', hi=5)
        short_b = make_db(self.W / 'SB.db', hi=5)
        self.same(short_a, short_b, ex=(0, 5))
        r = self.differ(short_a, short_b, ex=TURNS)       # the game was to reach turn 30
        self.assertTrue(any('no row for 25 of the 30 turns' in p for p in r.get('plausibility', [])), r.get('plausibility'))
        gap_a = make_db(self.W / 'GA.db', skip_turns=(17,))
        gap_b = make_db(self.W / 'GB.db', skip_turns=(17,))
        self.differ(gap_a, gap_b, ex=TURNS)

    def test_missing_per_turn_table(self):
        a = self.mutate('MA.db', 'DROP TABLE WorldStateLog', self.a)
        b = self.mutate('MB.db', 'DROP TABLE WorldStateLog', self.b)
        self.differ(a, b)

    def test_window_with_expected_turns(self):
        self.same(self.a, self.b, ft=10, ex=(10, 30))


if __name__ == '__main__':
    unittest.main()
