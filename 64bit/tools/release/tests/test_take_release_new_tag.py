"""Issue 3512: a new release with no tools/release/releases/<tag>.json, taken with --base-release. The real testdata
stage (on a fake c5h and a fake game folder) writes the candidate descriptor; every stage after it reads that
candidate (the built database, the tag's game folder) and every tool the driver starts gets C5_RELEASE naming it.
Hermetic: the fake fork, deps and tools of test_take_release_stages.py; nothing outside a temporary folder.

    python -I -X utf8 -m unittest discover -s tools/release/tests -p "test_take_release*.py" -v
"""
import json
import os
import sqlite3
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
import take_release as tr  # noqa: E402
import descriptor  # noqa: E402
from test_take_release_stages import PY, STAGES, StageFixture, git  # noqa: E402

NEW = 'Release-10.0.0'
BASE = 'Release-9.9.9'
CP = 'd1b6328c-ff44-4b0d-aad7-c657f83610cd'
VP = '8411a7a8-dad3-4622-a18e-fcc18324c799'
STRATEGIES = 41   # the built database's AIEconomicStrategies rows (5.4.8's descriptor says 37)

FAKE_C5H = r'''
import os, sqlite3, sys
from pathlib import Path
a = sys.argv[1:]
def opt(f):
    return a[a.index(f) + 1]
with open(os.environ['FAKE_CALLS'], 'a', encoding='utf-8') as f:
    f.write('c5h ' + ' '.join(a) + '\n')
out = Path(opt('--out'))
if a[0] == 'snapshot':
    out.write_bytes(b'snapshot')
    sys.exit(0)
if a[0] == 'dbbuild':
    con = sqlite3.connect(out)
    con.execute('CREATE TABLE CustomModOptions (Name TEXT, Value INTEGER)')
    con.execute("INSERT INTO CustomModOptions VALUES ('SQLITE_LOGGING', 0)")
    con.execute('CREATE TABLE AIEconomicStrategies (ID INTEGER)')
    con.executemany('INSERT INTO AIEconomicStrategies VALUES (?)', [(i,) for i in range(%d)])
    con.execute('CREATE TABLE BuiltMods (Arg TEXT)')
    con.executemany('INSERT INTO BuiltMods VALUES (?)', [(a[i + 1],) for i, x in enumerate(a) if x == '--mod'])
    con.commit()
    con.close()
    sys.exit(0)
sys.exit(9)
''' % STRATEGIES


def modinfo(guid, ver):
    return f'<?xml version="1.0"?>\n<Mod id="{{{guid.upper()}}}" version="{ver}">\n  <Properties/>\n</Mod>\n'


class NewTag(StageFixture):
    def setUp(self):
        super().setUp()
        self._old_release = os.environ.pop('C5_RELEASE', None)
        self.env_calls = self.tmp / 'env-calls.txt'
        self.setenv(FAKE_ENV_CALLS=str(self.env_calls))
        # the new release in the fork: the pins' tree plus the tag's mod folders with new versions
        f = self.fork
        (f / '(1) Community Patch' / '(1) Community Patch (v 152).modinfo').write_text(modinfo(CP, 152))
        (f / '(2) Vox Populi').mkdir()
        (f / '(2) Vox Populi' / '(2) Vox Populi (v 18).modinfo').write_text(modinfo(VP, 18))
        git(f, 'add', '-A')
        git(f, 'commit', '-q', '-m', 'the new release')
        git(f, 'tag', NEW)
        self.new_commit = git(f, 'rev-parse', 'HEAD')
        # the previous release's game folder (with its modpack), core database and pinned test database
        game = self.tmp / 'base' / "Sid Meier's Civilization V"
        (game / 'Assets' / 'DLC' / 'DLC_01').mkdir(parents=True)
        (game / 'Assets' / 'DLC' / 'DLC_01' / 'a.Civ5Pkg').write_text('<Civ5Package/>')
        old = game / 'Assets' / 'DLC' / 'VP_MODPACK_9_9_9'
        (old / 'Mods' / 'Old').mkdir(parents=True)
        (old / 'MPModsPack.Civ5Pkg').write_text('<Civ5Package><Key>k</Key></Civ5Package>')
        self.core = self.tmp / 'Civ5CoreDatabase.db'
        self.core.write_bytes(b'core')
        self.base_db = self.tmp / 'base-test.db'
        self.base_db.write_bytes(b'the previous release database')
        self.desc = self.make_desc(BASE, game_dir=game.as_posix(), test_db=self.base_db.as_posix(),
                                   test_db_sha256=descriptor.sha256(self.base_db),
                                   loc_db=(self.tmp / 'loc.db').as_posix())
        # c5h as a tool: c5h.cmd in a folder without spaces (cmd /c)
        c5h = self.c5h = self.tmp / 'c5h'
        c5h.mkdir()
        (c5h / 'fake_c5h.py').write_text(FAKE_C5H)
        (c5h / 'c5h.cmd').write_text(f'@"{PY}" -I "{c5h / "fake_c5h.py"}" %*\r\n')

    def tearDown(self):
        if self._old_release is not None:
            os.environ['C5_RELEASE'] = self._old_release
        super().tearDown()

    def new_driver(self, **cfg):
        return self.driver(NEW, stage_files={}, base_release=BASE, c5h=str(self.c5h), core_db=str(self.core),
                           core_db_pins={descriptor.sha256(self.core): 'the test core database'}, **cfg)   # ticket 0612

    def env_lines(self):
        if not self.env_calls.exists():
            return []
        return [ln.split('\t') for ln in self.env_calls.read_text(encoding='utf-8').splitlines()]

    def test_new_tag_runs_testdata_to_offline_on_the_built_database(self):
        self.assertFalse((tr.HERE / 'releases' / f'{NEW}.json').exists())
        rc = self.new_driver().run()
        self.assertEqual(rc, 0, self.lines[-5:])
        st = self.state()
        for n, _, _ in STAGES:
            self.assertIn(st[n]['status'], ('ok', 'skipped'), n)
        # survey: no descriptor of the tag yet, so its commit is not compared with the base's
        self.assertIn('not made', json.loads((self.work / 'survey' / 'survey.json').read_text())['descriptor_check'])
        # testdata wrote the candidate from the tag, on the previous release's descriptor
        cand_path = Path(st['testdata']['outputs']['descriptor'])
        self.assertEqual(cand_path, self.work / 'testdata' / f'{NEW}.json')
        cand = descriptor.load_file(cand_path, expect_tag=NEW)
        built = self.work / 'testdata' / 'db' / 'Civ5DebugDatabase.db'
        self.assertEqual(cand.upstream_commit, self.new_commit)
        self.assertEqual(cand.mod_args, [f'{CP}:152', f'{VP}:18'])
        self.assertEqual(cand.test_db, built)
        self.assertTrue(cand.test_db_source.startswith('built'))
        self.assertEqual(cand.game_dir, self.work / 'testdata' / 'game' / "Sid Meier's Civilization V")
        self.assertEqual(cand.reference_counts, {})
        self.assertEqual(cand.reference_saves['huge'].sha256, self.desc.reference_saves['huge'].sha256)
        self.assertEqual(cand.economic_strategies, STRATEGIES)
        self.assertEqual(cand.player_dll_43_sha256, self.desc.player_dll_43_sha256)  # the tag's (3b) DLL
        con = sqlite3.connect(built)
        try:
            mods = sorted(r[0] for r in con.execute('SELECT Arg FROM BuiltMods'))
        finally:
            con.close()
        self.assertEqual(mods, sorted([f'{CP}:152', f'{VP}:18']))
        # every stage after testdata used the built database, never the previous release's
        self.assertIn('C5_RELEASE_TEST_DB=' + str(built), self.called('build_server')[0])
        verify = self.called('run_proxy_game')[0]
        self.assertIn(f'--db {built}', verify)
        self.assertIn(f'--loc-db {self.tmp / "loc.db"}', verify)
        self.assertIn(f'--db {built}', self.called('run_server')[0])
        self.assertNotIn(str(self.base_db), '\n'.join(self.calls.read_text().splitlines()))
        self.assertIn('no reference_counts', st['savetest']['outputs']['note'])
        self.assertEqual(st['pack']['status'], 'ok')
        # every tool the driver started was pointed at the candidate, and descriptor.load() there reads it
        lines = self.env_lines()
        self.assertTrue({'run_proxy_game', 'run_d10', 'run_pair', 'run_server', 'build_server', 'census', 'pack'}
                        <= {n for n, _ in lines}, lines)
        self.assertEqual({v for _, v in lines}, {str(cand_path.resolve())})
        os.environ['C5_RELEASE'] = lines[0][1]
        try:
            self.assertEqual(descriptor.load().test_db, built)
        finally:
            os.environ.pop('C5_RELEASE', None)

    def test_rerun_runs_nothing_and_a_changed_candidate_reruns_the_stages_after_it(self):
        self.assertEqual(self.new_driver().run(), 0, self.lines[-5:])
        n = len(self.calls.read_text().splitlines())
        self.assertEqual(self.new_driver().run(), 0)
        self.assertEqual(len(self.calls.read_text().splitlines()), n)
        # a person points the candidate at another database: offline (its inputs name it) runs again
        p = self.work / 'testdata' / f'{NEW}.json'
        d = json.loads(p.read_text(encoding='utf-8'))
        other = self.tmp / 'other.db'
        other.write_bytes(b'x')
        d.update(test_db=other.as_posix(), test_db_sha256=descriptor.sha256(other))
        p.write_text(json.dumps(d), encoding='utf-8')
        self.assertEqual(self.new_driver().run(), 0, self.lines[-5:])
        self.assertIn(f'--db {other}', self.called('run_proxy_game')[-1])
        self.assertEqual(len(self.called('run_proxy_game')), 2)

    def test_stages_after_testdata_need_its_candidate(self):
        drv = self.new_driver()
        self.assertIs(drv.desc_for('survey'), self.desc)
        self.assertIs(drv.desc_for('testdata'), self.desc)   # testdata's base
        with self.assertRaises(descriptor.DescriptorError) as e:
            drv.desc_for('offline')
        self.assertIn(f'take_release.py {NEW} --from testdata', str(e.exception))
        cand = self.work / 'testdata' / f'{NEW}.json'
        cand.parent.mkdir(parents=True)
        raw = dict(self.desc.raw, tag=NEW)
        cand.write_text(json.dumps(raw), encoding='utf-8')
        drv.state.entry('testdata', '2').update(status='ok', outputs={'descriptor': str(cand)})
        self.assertEqual(drv.desc_for('offline').tag, NEW)
        self.assertEqual(tr.release_env(drv.desc_for('drift')), str(cand.resolve()))
        cand.write_text(json.dumps(dict(raw, economic_strategies=99)), encoding='utf-8')   # read again when changed
        self.assertEqual(drv.desc_for('fixchecks').economic_strategies, 99)
        cand.write_text(json.dumps(dict(raw, tag='Release-10.0.1')), encoding='utf-8')
        with self.assertRaises(descriptor.DescriptorError) as e:
            drv.desc_for('offline')
        self.assertEqual(e.exception.field, 'tag')
        cand.unlink()
        with self.assertRaises(descriptor.DescriptorError) as e:
            drv.desc_for('offline')
        self.assertIn('missing', str(e.exception))

    def test_offline_without_testdata_fails_and_names_it(self):
        # a --base-release run where testdata never ran: a stage after it fails with the reason, it does not run on
        # the previous release's descriptor
        drv = self.new_driver()
        for n in ('survey', 'branches', 'lock', 'build'):
            drv.state.entry(n, '-').update(status='ok', outputs={'x64_out': str(self.tmp), 'x64_bin': 'b',
                                                                 'proxy_dll': 'p', 'x64_src': 's'})
        self.assertEqual(drv.run(only='offline'), 1)
        self.assertIn('--from testdata', self.lines[-1])
        self.assertEqual(self.called('run_proxy_game'), [])

    def test_release_env(self):
        rel = descriptor.load('Release-5.4.8')
        self.assertEqual(tr.release_env(rel), 'Release-5.4.8')   # the repository's file: by its tag
        self.assertIsNone(tr.release_env(self.desc))              # parsed from data, no repository file
        self.assertIsNone(tr.release_env(object()))


if __name__ == '__main__':
    unittest.main()
