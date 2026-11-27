"""Issue 0152: run_db_write_test.py's stable defaults. Offline, no build, under a few seconds:

    python -I -m unittest discover -s server/tests/db -p "test_run_db_write_test.py"

--bin is the pinned layout's bin (deps/dll.lock `release`); the database the test makes has the integer-ID tables the
C++ test needs; the import library's export list is read correctly from a PE32+ file (the pinned CvGameDatabase_x64.dll
when it is present, skipped otherwise)."""
import re
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(HERE))
import run_db_write_test as t  # noqa: E402


class Defaults(unittest.TestCase):
    def test_bin_is_the_dll_lock_release(self):
        text = (REPO / 'deps' / 'dll.lock').read_text(encoding='utf-8')
        m = re.search(r'^release\s*=\s*([A-Za-z]:/\S+)', text, re.M)
        self.assertIsNotNone(m, 'deps/dll.lock has no release path on its first line')
        self.assertEqual(t.DEFAULT_BIN, Path(m.group(1).rstrip('/')) / 'bin')

    def test_lock_release_dir_missing(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertIsNone(t.lock_release_dir(Path(d) / 'missing.lock'))


class MadeDatabase(unittest.TestCase):
    def test_shape(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / 'made.db'
            t.make_db(p, 512)
            con = sqlite3.connect(str(p))
            try:
                self.assertEqual(con.execute('PRAGMA page_size').fetchone()[0], 1024)
                self.assertEqual(con.execute('PRAGMA journal_mode').fetchone()[0], 'delete')
                tables = [r[0] for r in con.execute("SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT "
                                                    "LIKE 'sqlite_%' ORDER BY name")]
                rows = 0
                for name in tables:  # the rule of test_db_write_cost.cpp PickTargets
                    cols = con.execute(f'PRAGMA table_info("{name}")').fetchall()
                    if any(c[1] == 'ID' and c[2].lower() == 'integer' for c in cols):
                        rows += con.execute(f'SELECT count(*) FROM "{name}"').fetchone()[0]
                self.assertGreaterEqual(rows, 512 + t.DB_ROWS_PER_TABLE)
                self.assertGreater(p.stat().st_size, 5_000_000)
            finally:
                con.close()


class Exports(unittest.TestCase):
    def test_pinned_database_dll(self):
        dll = (t.DEFAULT_BIN or Path('-')) / (t.DB_LIB + '.dll')
        if not dll.exists():
            self.skipTest(f'{dll} not present')
        name, exports = t.pe_exports(dll)
        self.assertEqual(name.lower(), (t.DB_LIB + '.dll').lower())
        names = {n for n, _ in exports}
        self.assertTrue(any(n.startswith('?Open@Connection@Database@@') for n in names), sorted(names)[:5])

    def test_not_pe(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / 'x.dll'
            p.write_bytes(b'MZ' + b'\0' * 0x40)
            with self.assertRaises(ValueError):
                t.pe_exports(p)


if __name__ == '__main__':
    unittest.main()
