"""Issue 3501: the test-data stage (tools/release/stages/testdata.py). Hermetic: a fake fork (a git repository made in a
temporary folder), a fake game folder and a fake c5h (a runner that writes the files c5h would); git is real."""
from __future__ import annotations

import contextlib
import hashlib
import io
import json
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'stages'))
sys.path.insert(0, str(HERE.parent))
import descriptor  # noqa: E402
import testdata  # noqa: E402

CP = 'd1b6328c-ff44-4b0d-aad7-c657f83610cd'
VP = '8411a7a8-dad3-4622-a18e-fcc18324c799'
EUI = 'aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee'
MPM = 'ffffffff-bbbb-cccc-dddd-eeeeeeeeeeee'
TAG = 'Release-9.1.0'


def modinfo(guid, ver):
    return f'<?xml version="1.0"?>\n<Mod id="{guid}" version="{ver}">\n  <Properties/>\n</Mod>\n'


CP_FILES = {'Core Files/a.lua': b'-- a', 'Core Files/b.lua': b'-- b', 'Gamecore/GamecoreInit.lua': b'-- init'}
CP_PROJECT = ('<?xml version="1.0" encoding="utf-8"?>\r\n<Project>\r\n  <ItemGroup>\r\n'
              '    <Content Include="Core Files\\a.lua">\r\n      <ImportIntoVFS>True</ImportIntoVFS>\r\n    </Content>\r\n'
              '    <Content Include="Core Files\\b.lua">\r\n      <ImportIntoVFS>False</ImportIntoVFS>\r\n    </Content>\r\n'
              '    <Content Include="Gamecore\\GamecoreInit.lua">\r\n      <SubType>Lua</SubType>\r\n'
              '      <ImportIntoVFS>True</ImportIntoVFS>\r\n    </Content>\r\n  </ItemGroup>\r\n</Project>\r\n')


def cp_modinfo(with_gamecore: bool) -> bytes:
    """A CP modinfo as ModBuddy writes it (BOM, CRLF, forward slashes); master's committed one lacks the Gamecore line."""
    def line(rel, imp):
        return f'    <File md5="{hashlib.md5(CP_FILES[rel]).hexdigest().upper()}" import="{imp}">{rel}</File>'
    lines = ['<?xml version="1.0" encoding="utf-8"?>', f'<Mod id="{CP}" version="151">', '  <Properties/>', '  <Files>',
             line('Core Files/a.lua', 1), line('Core Files/b.lua', 0)]
    if with_gamecore:
        lines.append(line('Gamecore/GamecoreInit.lua', 1))
    lines += ['  </Files>', '</Mod>', '']
    return b'\xef\xbb\xbf' + '\r\n'.join(lines).encode('utf-8')


def git(repo, *args):
    subprocess.run(['git', '-C', str(repo), *args], check=True, capture_output=True)


def make_db(path: Path, logging=0, strategies=3, extra_rows=()):
    con = sqlite3.connect(path)
    con.execute('CREATE TABLE CustomModOptions (Name TEXT, Value INTEGER, Class INTEGER, DbUpdates INTEGER)')
    con.execute("INSERT INTO CustomModOptions VALUES ('SQLITE_LOGGING', ?, 6, 0)", (logging,))
    con.execute('CREATE TABLE AIEconomicStrategies (ID INTEGER, Type TEXT)')
    con.executemany('INSERT INTO AIEconomicStrategies VALUES (?, ?)', [(i, f'S{i}') for i in range(strategies)])
    for r in extra_rows:
        con.execute(r)
    con.commit()
    con.close()


class World:
    """A fork with TAG, a base game folder with a VP modpack, a core database and a base descriptor."""

    def __init__(self, cp_project=None):
        """cp_project: None (no .civ5proj), 'master' (the committed CP modinfo lacks a VFS import of its project, as
        upstream master's at ba8aafdca, issue 0310) or 'release' (modinfo and project agree, as Release-5.4.8's)."""
        self.tmp = tempfile.TemporaryDirectory()
        t = self.root = Path(self.tmp.name)
        self.fork = t / 'fork'
        for folder, guid, ver in (('(1) Community Patch', CP, 151), ('(2) Vox Populi', VP, 17),
                                  ('(3a) VP - EUI Compatibility Files', EUI, 1), ('(5) Modpack Maker for VP', MPM, 1)):
            d = self.fork / folder
            d.mkdir(parents=True)
            (d / f'{folder} (v {ver}).modinfo').write_text(modinfo('{' + guid.upper() + '}', ver), encoding='utf-8')
        (self.fork / '(1) Community Patch' / 'CvGameCore_Expansion2.dll').write_bytes(b'MZ tag dll')
        if cp_project:
            cp = self.fork / '(1) Community Patch'
            (cp / '(1) Community Patch (v 151).modinfo').write_bytes(cp_modinfo(cp_project == 'release'))
            (cp / 'Community Patch.civ5proj').write_bytes(CP_PROJECT.encode('utf-8'))
            for rel, data in CP_FILES.items():
                (cp / rel).parent.mkdir(parents=True, exist_ok=True)
                (cp / rel).write_bytes(data)
        (self.fork / 'README.md').write_text('x', encoding='utf-8')
        git(self.fork, 'init', '-q')
        git(self.fork, 'config', 'core.autocrlf', 'false')
        git(self.fork, '-c', 'user.name=t', '-c', 'user.email=t@t', 'add', '-A')
        git(self.fork, '-c', 'user.name=t', '-c', 'user.email=t@t', 'commit', '-q', '-m', 'release')
        git(self.fork, 'tag', TAG)
        self.game = t / 'base' / "Sid Meier's Civilization V"
        (self.game / 'Assets' / 'DLC' / 'DLC_01').mkdir(parents=True)
        (self.game / 'Assets' / 'DLC' / 'DLC_01' / 'a.Civ5Pkg').write_text('<Civ5Package/>', encoding='utf-8')
        pack = self.game / 'Assets' / 'DLC' / 'VP_MODPACK_OLD'
        (pack / 'Mods' / 'Old').mkdir(parents=True)
        (pack / 'MPModsPack.Civ5Pkg').write_text('<Civ5Package><Key>k</Key></Civ5Package>', encoding='utf-8')
        (self.game / 'lua51_Win32.dll').write_bytes(b'MZ')
        self.core = t / 'Civ5CoreDatabase.db'
        self.core.write_bytes(b'core')
        self.base_db = t / 'base.db'
        make_db(self.base_db, logging=1)
        self.save = t / 'huge.Civ5Save'
        self.save.write_bytes(b'save')
        self.calls = []
        self.snapshot_report = None      # steps of a failing c5h snapshot (exit 1), or None
        self.db_logging_row = True

    def base(self, tag='Release-9.0.0'):
        return descriptor.parse({
            'format': 1, 'tag': tag, 'upstream_commit': 'a' * 40,
            'mods': [{'folder': '(1) Community Patch', 'guid': CP, 'version': 151},
                     {'folder': '(2) Vox Populi', 'guid': VP, 'version': 17}],
            'game_dir': self.game.as_posix(), 'test_db': self.base_db.as_posix(),
            'test_db_sha256': descriptor.sha256(self.base_db), 'loc_db': (self.root / 'loc.db').as_posix(),
            'reference_saves': {'huge': {'path': self.save.as_posix(), 'sha256': descriptor.sha256(self.save), 'turn': 350}},
            'reference_counts': {'tiny': 1}, 'economic_strategies': 3}, 'base.json', expect_tag=tag)

    def runner(self, cmd, **kw):
        """git for real; c5h snapshot / dbbuild faked."""
        self.calls.append(cmd)
        if cmd[0] == 'git':
            return subprocess.run(cmd, **kw)
        out = Path(cmd[cmd.index('--out') + 1])
        if 'snapshot' in cmd:
            out.write_bytes(b'snapshot')
            if self.snapshot_report is not None:
                b = out.with_name(out.name + '.build')
                b.mkdir()
                (b / 'builddb-report.json').write_text(json.dumps({'status': 'built', 'steps': self.snapshot_report}),
                                                       encoding='utf-8')
                return subprocess.CompletedProcess(cmd, 1, 'snapshot build errors', '')
            return subprocess.CompletedProcess(cmd, 0, str(out), '')
        if 'dbbuild' in cmd:
            if self.db_logging_row:
                make_db(out, logging=0)
            else:
                con = sqlite3.connect(out)
                con.execute('CREATE TABLE CustomModOptions (Name TEXT, Value INTEGER)')
                con.commit()
                con.close()
            return subprocess.CompletedProcess(cmd, 0, 'built', '')
        raise AssertionError(f'unexpected command {cmd}')

    def stage(self, tag=TAG, base=None, **kw):
        kw.setdefault('core', self.core)
        kw.setdefault('core_pins', {descriptor.sha256(self.core): 'the test core database'})   # ticket 0612
        return testdata.Stage(tag, self.root / 'out', self.fork, base or self.base(),
                              runner=self.runner, log=lambda m: None, **kw)

    def close(self):
        self.tmp.cleanup()


class Helpers(unittest.TestCase):
    def test_modpack_name(self):
        self.assertEqual(testdata.modpack_name('Release-5.4.8'), 'VP_MODPACK_5_4_8')
        self.assertEqual(testdata.modpack_name('Release-5.5.0-rc.1'), 'VP_MODPACK_5_5_0_RC_1')

    def test_read_modinfo(self):
        with tempfile.TemporaryDirectory() as t:
            d = Path(t)
            with self.assertRaises(testdata.StageError):
                testdata.read_modinfo(d)  # none
            (d / 'a.modinfo').write_text(modinfo('{D1B6328C-FF44-4B0D-AAD7-C657F83610CD}', 151), encoding='utf-8')
            self.assertEqual(testdata.read_modinfo(d), (CP, 151))
            (d / 'b.modinfo').write_text(modinfo(CP, 1), encoding='utf-8')
            with self.assertRaises(testdata.StageError):
                testdata.read_modinfo(d)  # two
            (d / 'b.modinfo').unlink()
            (d / 'a.modinfo').write_text('<Mod id="x"/>', encoding='utf-8')
            with self.assertRaises(testdata.StageError):
                testdata.read_modinfo(d)  # no version

    def test_compare_dbs(self):
        with tempfile.TemporaryDirectory() as t:
            a, b = Path(t) / 'a.db', Path(t) / 'b.db'
            make_db(a)
            make_db(b)
            self.assertTrue(testdata.compare_dbs(a, b)['identical'])
            con = sqlite3.connect(b)
            con.execute("UPDATE AIEconomicStrategies SET Type = 'X' WHERE ID = 1")
            con.execute('CREATE TABLE Extra (a INTEGER)')
            con.execute('ALTER TABLE CustomModOptions ADD COLUMN More TEXT')
            con.commit()
            con.close()
            r = testdata.compare_dbs(a, b)
            self.assertFalse(r['identical'])
            self.assertEqual(r['only_in_b'], ['Extra'])
            by = {d['table']: d for d in r['differ']}
            self.assertEqual(by['AIEconomicStrategies']['rows_only_in_a'], 1)
            self.assertEqual(by['AIEconomicStrategies']['examples_b'], [['1', 'X']])
            self.assertEqual(by['CustomModOptions']['columns_only_in_b'], ['More'])


class StageRuns(unittest.TestCase):
    def setUp(self):
        self.w = World()

    def tearDown(self):
        self.w.close()

    def test_new_release(self):
        res = self.w.stage().go()
        self.assertEqual(res['status'], 'ok', res)
        out = self.w.root / 'out'
        d = descriptor.parse(json.loads((out / f'{TAG}.json').read_text(encoding='utf-8')), 'x', expect_tag=TAG)
        self.assertEqual(d.mod_args, [f'{CP}:151', f'{VP}:17'])
        self.assertEqual(d.test_db, out / 'db' / 'Civ5DebugDatabase.db')
        self.assertEqual(d.reference_counts, {})            # a new release: filled by the save test
        self.assertEqual(d.reference_saves['huge'].turn, 350)  # the base's save is tried first (P10)
        self.assertEqual(d.economic_strategies, 3)
        self.assertEqual(d.player_dll_sha256, descriptor.sha256(self.w.fork / '(1) Community Patch/CvGameCore_Expansion2.dll'))
        game = out / 'game' / "Sid Meier's Civilization V"
        self.assertEqual(d.game_dir, game)
        dlc = sorted(p.name for p in (game / 'Assets' / 'DLC').iterdir())
        self.assertEqual(dlc, ['DLC_01', 'VP_MODPACK_9_1_0'])   # the old modpack left out
        self.assertTrue((game / 'Assets/DLC/VP_MODPACK_9_1_0/MPModsPack.Civ5Pkg').is_file())
        self.assertTrue((game / 'Assets/DLC/VP_MODPACK_9_1_0/Mods/(2) Vox Populi').is_dir())
        con = sqlite3.connect(out / 'db' / 'Civ5DebugDatabase.db')
        self.assertEqual(con.execute("SELECT Value FROM CustomModOptions WHERE Name = 'SQLITE_LOGGING'").fetchone(), (1,))
        con.close()
        rec = json.loads((out / 'testdata.json').read_text(encoding='utf-8'))
        self.assertEqual(rec['status'], 'ok')
        self.assertNotIn('compare', rec['steps'])  # not the base release: nothing to compare with
        dbb = next(c for c in self.w.calls if 'dbbuild' in c)
        self.assertIn(f'{CP}:151', dbb)
        # MC ruling: the tag's EUI compatibility files go into the database (and the modpack), not into the descriptor
        self.assertIn(f'{EUI}:1', dbb)
        self.assertNotIn(f'{MPM}:1', dbb)
        self.assertNotIn('(3a) VP - EUI Compatibility Files', d.mod_folders)
        self.assertTrue((game / 'Assets/DLC/VP_MODPACK_9_1_0/Mods/(3a) VP - EUI Compatibility Files').is_dir())
        self.assertIn('(3a) VP - EUI Compatibility Files', d.test_db_source)
        self.assertIn('Assets\\DLC\\VP_MODPACK_9_1_0\\Mods', dbb)
        self.assertEqual(Path(dbb[dbb.index('--dll') + 1]).name, 'CvGameCore_Expansion2.dll')
        toml = (out / 'c5h' / 'civ5headless.toml').read_text(encoding='utf-8')
        self.assertIn(game.as_posix(), toml)
        self.assertEqual((out / 'c5h' / 'core' / 'Civ5CoreDatabase.db').read_bytes(), b'core')

    def test_master_modinfo_regenerated_from_its_project(self):
        # issue 0310: the committed CP modinfo lacks a VFS import of its .civ5proj (upstream master's Gamecore files);
        # the stage's mods, the modpack copy and so the database build get the line ModBuddy's release build writes
        w = World(cp_project='master')
        try:
            res = w.stage().go()
            self.assertEqual(res['status'], 'ok', res)
            self.assertEqual(res['outputs']['modinfo_regenerated'], ['(1) Community Patch'])
            out = w.root / 'out'
            name = '(1) Community Patch (v 151).modinfo'
            want = cp_modinfo(True)
            self.assertEqual((out / 'mods' / '(1) Community Patch' / name).read_bytes(), want)
            self.assertEqual((out / 'game' / "Sid Meier's Civilization V" / 'Assets/DLC/VP_MODPACK_9_1_0/Mods' /
                              '(1) Community Patch' / name).read_bytes(), want)
            rec = json.loads((out / 'testdata.json').read_text(encoding='utf-8'))['steps']['mods']
            cpr = rec['modinfo_regenerated']['(1) Community Patch']
            self.assertEqual((cpr['changed'], cpr['added'], cpr['import_set'], cpr['project_files_missing']),
                             (True, ['Gamecore/GamecoreInit.lua'], [], []))
            self.assertFalse(rec['modinfo_regenerated']['(2) Vox Populi']['changed'])   # no project: untouched
            self.assertEqual(testdata.read_modinfo(out / 'mods' / '(1) Community Patch'), (CP, 151))
        finally:
            w.close()

    def test_release_modinfo_unchanged(self):
        # a released tag (5.4.8): modinfo and project agree; the file stays byte for byte the tag's
        w = World(cp_project='release')
        try:
            res = w.stage().go()
            self.assertEqual(res['status'], 'ok', res)
            self.assertEqual(res['outputs']['modinfo_regenerated'], [])
            name = '(1) Community Patch (v 151).modinfo'
            self.assertEqual((w.root / 'out' / 'mods' / '(1) Community Patch' / name).read_bytes(), cp_modinfo(True))
        finally:
            w.close()

    def test_heavy_steps_go_through_the_heavy_runner(self):
        # 3502: the game-folder copy (its own process), c5h snapshot and dbbuild go through `heavy` (the driver's
        # ctx.run with a name, i.e. under quietlock.py run); git stays on the light runner
        names = []

        def heavy(name, cmd, cwd=None, env=None):
            names.append(name)
            if name == 'game-copy':
                return subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True)
            return self.w.runner(cmd, cwd=cwd, env=env)
        res = self.w.stage(heavy=heavy).go()
        self.assertEqual(res['status'], 'ok', res)
        self.assertEqual(names, ['game-copy', 'snapshot', 'dbbuild'])
        game = self.w.root / 'out' / 'game' / "Sid Meier's Civilization V"
        self.assertEqual(sorted(p.name for p in (game / 'Assets' / 'DLC').iterdir()), ['DLC_01', 'VP_MODPACK_9_1_0'])
        self.assertTrue(all(c[0] == 'git' for c in self.w.calls if 'snapshot' not in c and 'dbbuild' not in c))

    def test_heavy_game_copy_failure_fails(self):
        def heavy(name, cmd, cwd=None, env=None):
            return subprocess.CompletedProcess(cmd, 3, 'refused, command not started', '')
        res = self.w.stage(heavy=heavy).go()
        self.assertEqual(res['status'], 'failed')
        self.assertIn('game folder copy exit 3', res['error'])

    def test_base_release_not_reproduced_keeps_the_pinned_db(self):
        self.w.base_db.unlink()
        make_db(self.w.base_db, logging=1, extra_rows=["INSERT INTO AIEconomicStrategies VALUES (9, 'MOD_EXTRA')"])
        res = self.w.stage(tag=TAG, base=self.w.base(TAG)).go()
        self.assertEqual(res['status'], 'ok', res)
        self.assertFalse(res['outputs']['identical_to_base'])
        cmp = json.loads((self.w.root / 'out' / f'compare-{TAG}.json').read_text(encoding='utf-8'))
        self.assertEqual([d['table'] for d in cmp['differ']], ['AIEconomicStrategies'])
        d = json.loads((self.w.root / 'out' / f'{TAG}.json').read_text(encoding='utf-8'))
        self.assertEqual(d['test_db'], self.w.base_db.as_posix())
        self.assertTrue(d['test_db_source'].startswith('pinned'))
        self.assertEqual(d['reference_counts'], {'tiny': 1})

    def test_base_release_reproduced_uses_the_build(self):
        # the fake build equals the base database once the stage sets SQLITE_LOGGING = 1
        res = self.w.stage(tag=TAG, base=self.w.base(TAG)).go()
        self.assertEqual(res['status'], 'ok', res)
        self.assertTrue(res['outputs']['identical_to_base'])
        d = json.loads((self.w.root / 'out' / f'{TAG}.json').read_text(encoding='utf-8'))
        self.assertTrue(d['test_db'].endswith('/db/Civ5DebugDatabase.db'))

    def test_stop_file(self):
        out = self.w.root / 'out'
        out.mkdir()
        (out / 'STOP').write_text('', encoding='utf-8')
        res = self.w.stage().go()
        self.assertEqual(res['status'], 'stop')
        self.assertEqual(res['stop']['cl_step'], 2)
        self.assertIn('commit', res['stop']['what'])
        self.assertEqual(self.w.calls, [])
        self.assertEqual(json.loads((out / 'testdata.json').read_text(encoding='utf-8'))['status'], 'stop')

    def test_unknown_tag_fails(self):
        res = self.w.stage(tag='Release-0.0.0').go()
        self.assertEqual(res['status'], 'failed')
        self.assertIn('git rev-parse', res['error'])
        rec = json.loads((self.w.root / 'out' / 'testdata.json').read_text(encoding='utf-8'))
        self.assertEqual(rec['status'], 'failed')
        self.assertFalse((self.w.root / 'out' / f'Release-0.0.0.json').exists())

    def test_release_name_with_a_base_commit_reads_the_commit(self):
        # issue 3509 (take_release.py --base-commit): the name is not a tag; the stage reads the fork at the commit
        commit = subprocess.run(['git', '-C', str(self.w.fork), 'rev-parse', 'HEAD'], check=True, capture_output=True,
                                text=True).stdout.strip()
        git(self.w.fork, 'tag', '-d', TAG)
        res = self.w.stage(tag='master-test', ref=commit[:9]).go()
        self.assertEqual(res['status'], 'ok', res)
        out = self.w.root / 'out'
        d = descriptor.parse(json.loads((out / 'master-test.json').read_text(encoding='utf-8')), 'x',
                             expect_tag='master-test')
        self.assertEqual(d.upstream_commit, commit)
        game = out / 'game' / "Sid Meier's Civilization V"
        self.assertTrue((game / 'Assets/DLC/VP_MODPACK_MASTER_TEST/MPModsPack.Civ5Pkg').is_file())

    def test_missing_mod_folder_fails(self):
        base = self.w.base()
        raw = dict(base.raw, mods=base.raw['mods'] + [{'folder': '(9) Missing', 'guid': '0' * 8 + '-0000-0000-0000-' + '0' * 12,
                                                      'version': 1}])
        res = self.w.stage(base=descriptor.parse(raw, 'b.json')).go()
        self.assertEqual(res['status'], 'failed')
        self.assertIn('(9) Missing', res['error'])

    def test_snapshot_world_builder_failures_accepted(self):
        wb = 'C:\\g\\Assets\\DLC\\DLC_SP_Maps_2\\Maps\\WB_Script_Random_Earth.lua'
        self.w.snapshot_report = [{'kind': 'mapscript', 'path': wb, 'ok': False, 'note': 'no GetMapScriptInfo '},
                                  {'kind': 'xml', 'path': 'x.xml', 'ok': True}]
        res = self.w.stage().go()
        self.assertEqual(res['status'], 'ok', res)
        rec = json.loads((self.w.root / 'out' / 'testdata.json').read_text(encoding='utf-8'))
        self.assertEqual(rec['steps']['snapshot']['accepted_failures'], ['WB_Script_Random_Earth.lua'])

    def test_snapshot_other_failure_refused(self):
        self.w.snapshot_report = [
            {'kind': 'mapscript', 'path': 'C:\\g\\Maps\\WB_Script_A.lua', 'ok': False, 'note': 'no GetMapScriptInfo'},
            {'kind': 'xml', 'path': 'C:\\g\\x.xml', 'ok': False, 'note': 'duplicate key'}]
        res = self.w.stage().go()
        self.assertEqual(res['status'], 'failed')
        self.assertIn('c5h snapshot exit 1', res['error'])

    def test_database_without_logging_option_fails(self):
        self.w.db_logging_row = False
        res = self.w.stage().go()
        self.assertEqual(res['status'], 'failed')
        self.assertIn('SQLITE_LOGGING', res['error'])

    def test_extra_mod(self):
        extra = self.w.root / 'Squads'
        extra.mkdir()
        (extra / 'Squads (v 1).modinfo').write_text(modinfo('11111111-2222-3333-4444-555555555555', 1), encoding='utf-8')
        res = self.w.stage(extra_mods=[extra]).go()
        self.assertEqual(res['status'], 'ok', res)
        dbb = next(c for c in self.w.calls if 'dbbuild' in c)
        self.assertIn('11111111-2222-3333-4444-555555555555:1', dbb)
        d = json.loads((self.w.root / 'out' / f'{TAG}.json').read_text(encoding='utf-8'))
        self.assertEqual(len(d['mods']), 2)  # the database only, not the descriptor's mod set

    # ticket 0612: the core database is a pinned file; the stage refuses before any step when it is not
    def test_core_pinned_hash_recorded(self):
        res = self.w.stage().go()
        self.assertEqual(res['status'], 'ok', res)
        rec = json.loads((self.w.root / 'out' / 'testdata.json').read_text(encoding='utf-8'))
        self.assertEqual(rec['steps']['config']['core_sha256'], descriptor.sha256(self.w.core))

    def test_core_missing_refused(self):
        res = self.w.stage(core=self.w.root / 'gone.db').go()
        self.assertEqual(res['status'], 'failed')
        self.assertIn('is missing', res['error'])
        self.assertEqual(self.w.calls, [])

    def test_core_not_set_refused(self):
        res = self.w.stage(core=None).go()
        self.assertEqual(res['status'], 'failed')
        self.assertIn('no core database: set C5_CORE_DB', res['error'])
        self.assertEqual(self.w.calls, [])

    def test_core_wrong_hash_refused(self):
        res = self.w.stage(core_pins={'0' * 64: 'another file'}).go()
        self.assertEqual(res['status'], 'failed')
        self.assertIn('not a pinned one', res['error'])
        self.assertIn(descriptor.sha256(self.w.core), res['error'])
        self.assertEqual(self.w.calls, [])

    def test_default_pins(self):
        self.assertIn('dec462184517315c72753029bc3f84f86244528c31ba505eb7cba5cef45706f2', testdata.CORE_DB_PINS)
        with tempfile.TemporaryDirectory() as t:
            live = Path(t) / 'Civ5CoreDatabase.db'
            live.write_bytes(b'a rebuilt live cache')
            with self.assertRaisesRegex(testdata.StageError, 'not a pinned one'):
                testdata.check_core(live)

    def test_extra_mod_missing_fails(self):
        res = self.w.stage(extra_mods=[self.w.root / 'nope']).go()
        self.assertEqual(res['status'], 'failed')
        self.assertIn('no such folder', res['error'])


class Cli(unittest.TestCase):
    def main(self, argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = testdata.main(argv)
        return rc, out.getvalue(), err.getvalue()

    def test_stop_exits_10(self):
        with tempfile.TemporaryDirectory() as t:
            out = Path(t) / 'out'
            out.mkdir()
            (out / 'STOP').write_text('', encoding='utf-8')
            core = Path(t) / 'core.db'
            core.write_bytes(b'c')
            with mock.patch.dict(testdata.CORE_DB_PINS, {descriptor.sha256(core): 'test'}):   # ticket 0612
                rc, so, _ = self.main(['--tag', 'Release-5.4.8', '--out', str(out), '--core', str(core)])
        self.assertEqual(rc, 10)
        self.assertIn('testdata: STOP testdata (CL step 2)', so)

    def test_unknown_base_exits_2(self):
        with tempfile.TemporaryDirectory() as t:
            rc, _, err = self.main(['--tag', 'X', '--out', t, '--base', 'Release-0.0.0'])
        self.assertEqual(rc, 2)
        self.assertIn('unknown release', err)

    def test_missing_core_exits_2(self):
        with tempfile.TemporaryDirectory() as t:
            rc, _, err = self.main(['--tag', 'X', '--out', t, '--core', str(Path(t) / 'none.db')])
        self.assertEqual(rc, 2)
        self.assertIn('is missing', err)

    def test_unpinned_core_exits_2(self):
        with tempfile.TemporaryDirectory() as t:
            core = Path(t) / 'Civ5CoreDatabase.db'
            core.write_bytes(b'a live cache')
            rc, _, err = self.main(['--tag', 'X', '--out', t, '--core', str(core)])
        self.assertEqual(rc, 2)
        self.assertIn('not a pinned one', err)
        self.assertEqual(len(err.strip().splitlines()), 1)   # one clear line (ticket 0612)


if __name__ == '__main__':
    unittest.main()
