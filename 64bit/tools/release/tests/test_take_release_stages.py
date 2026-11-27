"""Hermetic tests of the candidate stages of tools/release/take_release.py (issue 3502): the real stage modules
against a fake fork (a git repository made here, with a release tag and two pin commits) and fake tools
(tests/fake_tool.py). No gate, lock, build or game is touched; nothing is written outside a temporary folder.

    python -I -X utf8 -m unittest discover -s tools/release/tests -p "test_take_release*.py" -v
"""
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]   # ticket 4968: deps/i0.lock
sys.path.insert(0, str(HERE.parent))
import take_release as tr  # noqa: E402
import descriptor  # noqa: E402

PY = sys.executable
FAKE = HERE / 'fake_tool.py'
STAGES = list(tr.CANDIDATE)  # testdata: a fake module here (the real one runs c5h; its own tests are 3501's)
FAKE_TESTDATA = '''
from pathlib import Path
from take_release import StageResult
def run(ctx):
    db = ctx.dir('testdata', 'db') / 'Civ5DebugDatabase.db'
    db.write_bytes(b'test db of the tag')
    return StageResult.ok(test_db=str(db))
'''
TOOL_NAMES = ['quietlock', 'src_fingerprint', 'build_server', 'census', 'model', 'purity', 'purity_test', 'pack',
              'test_proxy_rt',
              'hutest', 'run_proxy_game', 'run_d10', 'run_pair', 'fix0096', 'fix0084', 'fix0103', 'run_server',
              'branches', 'build_all']
FP = 'v1 ' + 'a' * 64 + ' 3 files'
FAKE_REFERENCE_COUNTS = {'tiny': 30666, 'standard': 351496, 'huge': 683123}   # fake_tool.py's default FAKE_ROWS

BUILD_VP_CLANG = '''
import os, sys
from pathlib import Path
SHARED_PREDEFS = ['FXS_IS_DLL', 'WIN32']
RELEASE_PREDEFS = SHARED_PREDEFS + ['NDEBUG']
DEBUG_PREDEFS = SHARED_PREDEFS + ['VPDEBUG']
PREDEFS = {
    'release': RELEASE_PREDEFS,
    'debug': DEBUG_PREDEFS,
}
here = Path.cwd()
(here / 'predefs.txt').write_text(' '.join('/D' + d for d in PREDEFS['release']))  # the compile line's defines
if os.environ.get('FAKE_I0_FAIL') and os.environ['FAKE_I0_FAIL'] in str(here):
    print('clang: error'); sys.exit(1)
d = here / 'clang-output' / 'Release'
d.mkdir(parents=True, exist_ok=True)
(d / 'CvGameCore_Expansion2.dll').write_bytes(('x86 dll of ' + str(here)).encode())
(d / 'CvGameCore_Expansion2.pdb').write_bytes(b'pdb')
with open(os.environ['FAKE_CALLS'], 'a') as f:
    f.write('build_vp_clang ' + str(here) + '\\n')
'''


HEADER_22 = ('#pragma once\n// For the default value of 22, just comment the line out\n'
             '// #define MOD_GLOBAL_MAX_MAJOR_CIVS (34)\n// #define MOD_GLOBAL_MAX_MAJOR_CIVS (43)\n'
             '// #define MOD_GLOBAL_MAX_MAJOR_CIVS (62)\n')


def git(repo, *a):
    r = subprocess.run(['git', '-C', str(repo)] + [str(x) for x in a], capture_output=True, text=True, check=True)
    return r.stdout.strip()


class StageFixture(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = Path(self._td.name)
        self.work = self.tmp / 'work'
        self.calls = self.tmp / 'calls.txt'
        self.env_keys = []
        self.setenv(FAKE_CALLS=str(self.calls), FAKE_QL_HELD=str(self.tmp / 'held'))
        self.make_fork()
        self.make_deps()
        self.desc = self.make_desc()
        self.tools = {**tr.default_tools(), **{n: [PY, '-I', str(FAKE), n] for n in TOOL_NAMES}}
        self.lines = []

    def tearDown(self):
        for k in self.env_keys:
            os.environ.pop(k, None)
        self._td.cleanup()

    def setenv(self, **kv):
        for k, v in kv.items():
            os.environ[k] = v
            self.env_keys.append(k)

    def make_fork(self):
        f = self.fork = self.tmp / 'fork'
        f.mkdir()
        git(f, 'init', '-q')
        git(f, 'config', 'user.email', 't@example.invalid')
        git(f, 'config', 'user.name', 'test')
        git(f, 'config', 'core.autocrlf', 'false')
        (f / 'build_vp_clang.py').write_text(textwrap.dedent(BUILD_VP_CLANG))
        src = f / 'CvGameCoreDLL_Expansion2'
        src.mkdir()
        (src / 'a.cpp').write_text('int a() { return gDLL->GetEngineThing(); }\n')
        (src / 'Lua').mkdir()
        (src / 'Lua' / 'CvLuaGame.cpp').write_text('Method(GetX)\nMethod(GetY)\n')
        cp = f / '(1) Community Patch'
        cp.mkdir()
        self.player_dll = b'the player dll'
        (cp / 'CvGameCore_Expansion2.dll').write_bytes(self.player_dll)
        # issue 4252: the switch's header (lines commented out, as upstream's) and the tag's 43-civ player DLL
        hdr = f / 'CvGameCoreDLLUtil' / 'include'
        hdr.mkdir(parents=True)
        (hdr / 'CustomModsGlobal.h').write_text(HEADER_22)
        c43 = f / '(3b) 43 Civs Community Patch'
        c43.mkdir()
        self.player_dll_43 = b'the 43-civ player dll'
        (c43 / 'CvGameCore_Expansion2.dll').write_bytes(self.player_dll_43)
        git(f, 'add', '-A')
        git(f, 'commit', '-q', '-m', 'release')
        git(f, 'tag', 'Release-9.9.9')
        self.tag_commit = git(f, 'rev-parse', 'HEAD')
        (src / 'b.cpp').write_text('int b() { return 1; }\n')
        git(f, 'add', '-A')
        git(f, 'commit', '-q', '-m', 'i0 pin')
        self.i0 = git(f, 'rev-parse', 'HEAD')
        (src / 'a.cpp').write_text('int a() { return gDLL->GetEngineThing() + 0; }\n')
        git(f, 'commit', '-q', '-am', 'x64 pin')
        self.x64 = git(f, 'rev-parse', 'HEAD')
        # the instruments patch: applies to the i0 pin
        (src / 'b.cpp').write_text('int b() { return 2; }\n')
        self.ia_patch = self.tmp / 'ia-test-440878983.patch'
        self.ia_patch.write_bytes(subprocess.run(['git', '-C', str(f), 'diff'], capture_output=True, check=True).stdout)
        git(f, 'checkout', '-q', '--', '.')

    def make_deps(self):
        d = self.deps = self.tmp / 'deps'
        d.mkdir()
        (d / 'dll.lock').write_text(f'repository = x\nbranch = x64-pin\ncommit = {self.x64}\n'
                                    f'base = Release-9.9.9 ({self.tag_commit[:9]})\nfingerprint = {FP}\n')
        (d / 'i0.lock').write_text(f'repository = x\nbranch = i0-pin\ncommit = {self.i0}\n'
                                   f'base = Release-9.9.9 ({self.tag_commit[:9]})\n')

    def make_desc(self, tag='Release-9.9.9', **over):
        save = self.tmp / 'saves' / 'REF-HUGE_0350.Civ5Save'
        save.parent.mkdir(exist_ok=True)
        save.write_bytes(b'huge save bytes')
        d = json.loads((tr.HERE / 'releases' / 'Release-5.4.8.json').read_text(encoding='utf-8'))
        d.update(tag=tag, upstream_commit=self.tag_commit,
                 player_dll_sha256=hashlib.sha256(self.player_dll).hexdigest(),
                 player_dll_43_sha256=hashlib.sha256(self.player_dll_43).hexdigest(),
                 reference_saves={'huge': {'path': save.as_posix(),'sha256': hashlib.sha256(b'huge save bytes').hexdigest(),
                                           'turn': 350}},
                 # ticket 0574: the fake run_d10's rows (fake_tool.py), not the real descriptor's: those follow the pins
                 # (0463 and 0517 changed them), which stopped every fixture take at savetest since 0463
                 reference_counts=dict(FAKE_REFERENCE_COUNTS))
        d.update(over)
        return descriptor.parse(d, 'test', expect_tag=tag)

    def driver(self, name='Release-9.9.9', **cfg):
        fake_td = self.tmp / 'fakestages' / 'testdata.py'
        if not fake_td.exists():
            fake_td.parent.mkdir(exist_ok=True)
            fake_td.write_text(textwrap.dedent(FAKE_TESTDATA))
        cfg.setdefault('stage_files', {'testdata': str(fake_td)})
        # issue 0480: the fake derivation drops a row, so the full purity test runs on the audited source; the fake
        # fork has no audited_at commit, so the source is this folder (TestPurityFullTest covers the real copy)
        if 'purity_audited_src' not in cfg:
            cfg['purity_audited_src'] = str(self.tmp / 'audited-src')
        # ticket 0774: deps/dll.lock says release-config = release-pgo; the fake fork's commits have no profile, so the
        # plain build path is pinned here (test_take_release_pgo.py tests the release-pgo path with its own lock)
        config = {'tier': 'candidate', 'who': 'test3502', 'gate_wait': 0, 'fork': str(self.fork), 'carry': 'pins',
                  'jobs': 2, 'ia_patch': str(self.ia_patch), 'deps_dir': str(self.deps), 'llvm_bin': 'C:\\nowhere',
                  'x64_config': 'release', **cfg}
        return tr.Driver(name, self.work, self.desc, config, STAGES, tools=self.tools,
                         out=self.lines.append)

    def state(self):
        return {e['name']: e for e in json.loads((self.work / 'state.json').read_text())['stages']}

    def called(self, name):
        if not self.calls.exists():
            return []
        return [ln for ln in self.calls.read_text().splitlines() if ln.split(' ', 1)[0] == name]


class TestStages(StageFixture):
    def test_full_candidate_run_on_the_pins(self):
        rc = self.driver().run()
        self.assertEqual(rc, 0, self.lines[-3:])
        st = self.state()
        self.assertEqual(st['branches']['status'], 'skipped')
        for n, _, _ in STAGES:
            if n != 'branches':
                self.assertEqual(st[n]['status'], 'ok', n)
        sv = st['survey']['outputs']
        self.assertEqual(sv['tag_commit'], self.tag_commit)
        self.assertFalse(any(sv['flags'].values()))
        b = st['build']['outputs']
        self.assertEqual((b['x64_commit'], b['i0_commit']), (self.x64, self.i0))
        self.assertEqual(b['x64_config'], 'release')
        lay = Path(b['x64_layout'])
        self.assertTrue(lay.name.startswith(f'x64-{self.x64[:8]}-p'))
        for rel in ('bin/CvGameCore_Expansion2.dll', 'bin/server.exe', 'proxy/bin/CvGameCore_Expansion2.dll',
                    'proxy/build-proxy.json', 'SHA256.txt', 'README.txt'):
            self.assertTrue((lay / rel).is_file(), rel)
        self.assertTrue((self.work / 'layout' / f'i0ship-{self.i0[:8]}' / 'CvGameCore_Expansion2.dll').is_file())
        ia = self.work / 'layout' / f'i0ship-{self.i0[:8]}-ia'
        self.assertTrue((ia / self.ia_patch.name).is_file())
        # the -ia source has the patch and the version stub; the plain 32-bit source is a worktree
        src_ia = Path(st['build']['outputs']['i0ia_layout'])
        self.assertTrue(src_ia.is_dir())
        stub = next((self.work / 'src').glob('i0ia-*')) / 'update_commit_id.bat'
        self.assertIn('+ia440878983 Copy', stub.read_text())
        self.assertIn('return 2', (stub.parent / 'CvGameCoreDLL_Expansion2' / 'b.cpp').read_text())
        self.assertTrue((Path(b['i0_src']) / '.git').exists())
        # the x64 source is a plain copy (fingerprint = deps/dll.lock's), built without --unlocked
        bs = self.called('build_server')
        self.assertEqual(len(bs), 1)
        self.assertNotIn('--unlocked', bs[0])
        self.assertIn('--offline', bs[0])
        # MC #414: the smoke run's database is the test-data stage's (build_all.default_db reads the variable)
        self.assertIn('C5_RELEASE_TEST_DB=' + str(self.work / 'testdata' / 'db' / 'Civ5DebugDatabase.db'), bs[0])
        self.assertFalse((Path(b['x64_src']) / '.git').exists())
        # candidate locks under <work>/deps, with the build's keys; nothing else written in deps/
        dl = (self.work / 'deps' / 'dll.lock').read_text()
        self.assertIn(f'commit      = {self.x64}', dl)
        self.assertIn('release', dl)
        self.assertEqual(sorted(p.name for p in self.deps.iterdir()), ['dll.lock', 'i0.lock'])
        # pack got the tag's player DLL
        self.assertEqual((self.work / 'inputs' / 'player-x86' / 'CvGameCore_Expansion2.dll').read_bytes(),
                         self.player_dll)
        self.assertEqual(st['purity']['outputs']['methods'], 2)
        self.assertEqual(st['purity']['outputs']['dropped'], 1)
        self.assertEqual(st['savetest']['outputs']['rows'], {'tiny': 30666, 'standard': 351496, 'huge': 683123})
        # the save test used a copy of the reference save, never the original
        d10 = self.called('run_d10')[0]
        self.assertIn(str(self.work / 'inputs'), d10)
        self.assertIn('--order-exception none', d10)
        self.assertEqual(set(st['fixchecks']['outputs']['checks']), {'0096', '0084', '0103', 'oldsave'})
        self.assertEqual(st['drift']['outputs']['rows'], {'x86': 30666, 'x64': 30666})
        self.assertIn('--turns 352', self.called('run_server')[0])
        self.assertTrue(any('tier candidate reached' in ln for ln in self.lines))

    def test_rerun_after_success_runs_nothing(self):
        self.assertEqual(self.driver().run(), 0)
        n = len(self.calls.read_text().splitlines())
        self.assertEqual(self.driver().run(), 0)
        self.assertEqual(len(self.calls.read_text().splitlines()), n)

    def test_series_carry_through_the_branches_tool(self):
        self.setenv(FAKE_BRANCH_HEADS=json.dumps({'i0': self.i0, 'x64': self.x64}))
        series = self.tmp / 'carry.tsv'
        series.write_text('# test series\n')
        self.assertEqual(self.driver(carry='series', series=str(series)).run(), 0, self.lines[-3:])
        st = self.state()
        self.assertEqual(st['branches']['status'], 'ok')
        name = st['branches']['outputs']['x64_branch']
        self.assertRegex(name, r'^x64-release-9\.9\.9-c[0-9a-f]{8}$')
        call = self.called('branches')[0]
        self.assertIn('--replace', call)
        self.assertNotIn('--push', call)
        self.assertEqual(st['lock']['outputs']['x64']['branch'], name)

    def test_branches_stop_passed_through(self):
        self.setenv(FAKE_BRANCHES_STOP='1')
        series = self.tmp / 'carry.tsv'
        series.write_text('')
        self.assertEqual(self.driver(carry='series', series=str(series)).run(), 10)
        self.assertEqual(self.lines[-1], 'take_release: STOP branches (CL step 3): port row b25fd45c0 conflicts; '
                                         'to unblock: resolve it')

    def test_branches_conflict_stop_names_the_drivers_series_option(self):
        # issue 3509: branches.py's "rerun with --series <copy>" is passed on as take_release.py's own option
        self.setenv(FAKE_BRANCHES_STOP='series')
        series = self.tmp / 'carry.tsv'
        series.write_text('')
        self.assertEqual(self.driver(carry='series', series=str(series)).run(), 10)
        line = self.lines[-1]
        self.assertTrue(line.startswith('take_release: STOP branches (CL step 3): identity row 5201480f1 conflicts'),
                        line)
        self.assertIn('(then rerun take_release.py Release-9.9.9 --series <copy> (recorded in the state', line)
        self.assertNotIn('rerun with --series', line)
        self.assertEqual(self.state()['branches']['stop']['unblock'], line.split('; to unblock: ', 1)[1])

    def test_release_name_with_a_base_commit_and_no_tag(self):
        # issue 3509: a base that is not a tag (upstream master at a commit), named by --base-commit; the series a
        # release copy (--series): branches get the commit as --base and the copy as --series
        git(self.fork, 'tag', '-d', 'Release-9.9.9')
        self.desc = self.make_desc(tag='master-test')
        self.setenv(FAKE_BRANCH_HEADS=json.dumps({'i0': self.i0, 'x64': self.x64}))
        series = self.tmp / 'carry-master-test.tsv'
        series.write_text('# release copy\n')
        rc = self.driver('master-test', carry='series', series=str(series), base_commit=self.tag_commit[:9]).run()
        self.assertEqual(rc, 0, self.lines[-3:])
        st = self.state()
        self.assertEqual(st['survey']['outputs']['tag_commit'], self.tag_commit)
        self.assertEqual(st['survey']['inputs']['own']['base_commit'], self.tag_commit[:9])
        self.assertRegex(st['branches']['outputs']['x64_branch'], r'^x64-release-master-test-c[0-9a-f]{8}$')
        call = self.called('branches')[0]
        self.assertIn(f'--base {self.tag_commit} ', call)
        self.assertIn(f'--series {series} ', call)
        self.assertEqual((self.work / 'inputs' / 'player-x86' / 'CvGameCore_Expansion2.dll').read_bytes(),
                         self.player_dll)

    def test_survey_stops_on_a_base_commit_missing_in_the_fork(self):
        self.desc = self.make_desc(tag='master-test')
        self.assertEqual(self.driver('master-test', base_commit='0' * 40).run(), 10)
        self.assertIn('STOP survey (CL step 1): base commit ' + '0' * 40 + ' of master-test (--base-commit) is not '
                      'in the fork clone', self.lines[-1])

    def test_survey_stops_when_the_base_commit_is_not_the_descriptors(self):
        self.desc = self.make_desc(tag='master-test')
        self.assertEqual(self.driver('master-test', base_commit=self.i0).run(), 10)
        self.assertIn(f'base commit {self.i0} of master-test is {self.i0[:12]} in the fork, the descriptor says '
                      f'{self.tag_commit}', self.lines[-1])

    def test_changed_series_reruns_branches_and_builds(self):
        self.setenv(FAKE_BRANCH_HEADS=json.dumps({'i0': self.i0, 'x64': self.x64}))
        series = self.tmp / 'carry.tsv'
        series.write_text('# one\n')
        self.assertEqual(self.driver(carry='series', series=str(series)).run(), 0)
        series.write_text('# two\n')
        self.assertEqual(self.driver(carry='series', series=str(series)).run(), 0)
        names = {ln.split('--name ')[1].split()[0] for ln in self.called('branches')}
        self.assertEqual(len(names), 2)  # a new, content-addressed branch name
        # same commits: lock outputs change (the branch name), so the build runs again
        self.assertEqual(len(self.called('build_server')), 2)

    def test_narrowing_gate_cut_is_a_stop_of_cl7(self):
        self.setenv(FAKE_NARROW='1')
        self.assertEqual(self.driver().run(), 10)
        self.assertTrue(self.lines[-1].startswith('take_release: STOP build (CL step 7): the narrowing gate found 1 '
                                                  'new -Wshorten-64-to-32 site(s) (CvGameCoreDLL_Expansion2/a.cpp:12:5)'))
        self.assertEqual(self.state()['narrowing']['status'], 'pending')

    def test_build_without_gate_result_stops_narrowing(self):
        self.setenv(FAKE_NO_GATE='1')
        self.assertEqual(self.driver().run(), 10)
        self.assertTrue(self.lines[-1].startswith('take_release: STOP narrowing (CL step 7): the x64 build has no '
                                                  'narrowing gate result'))

    def test_failing_x64_build_exit_1(self):
        self.setenv(FAKE_BUILD_SERVER_FAIL='1')
        self.assertEqual(self.driver().run(), 1)
        self.assertIn('FAILED build: server/build_server.py exit 1', self.lines[-1])

    def test_32bit_failure_resumes_without_a_second_x64_build(self):
        self.setenv(FAKE_I0_FAIL='i0ia-')
        self.assertEqual(self.driver().run(), 1)
        self.assertIn('build_vp_clang.py (i0ia) exit 1', self.lines[-1])
        os.environ.pop('FAKE_I0_FAIL')
        self.assertEqual(self.driver().run(), 0, self.lines[-3:])
        self.assertEqual(len(self.called('build_server')), 1)
        self.assertEqual(len([c for c in self.called('build_vp_clang') if 'i0wt-' in c]), 1)

    def test_skip_ia_builds_no_instance_aware_dll(self):
        self.assertEqual(self.driver(ia=False).run(), 0, self.lines[-3:])
        b = self.state()['build']['outputs']
        self.assertIsNone(b['i0ia_layout'])
        self.assertEqual(len(self.called('build_vp_clang')), 1)
        self.assertFalse(list((self.work / 'src').glob('i0ia-*')))
        self.assertIn('not built (--skip-ia)', (self.work / 'deps' / 'i0.lock').read_text())

    def test_ia_patch_that_does_not_apply_is_a_stop(self):
        bad = self.tmp / 'ia-bad-1.patch'
        bad.write_text('--- a/nothere.cpp\n+++ b/nothere.cpp\n@@ -1 +1 @@\n-x\n+y\n')
        self.assertEqual(self.driver(ia_patch=str(bad)).run(), 10)
        self.assertTrue(self.lines[-1].startswith('take_release: STOP build (CL step 3): the instruments patch '
                                                  'ia-bad-1.patch does not apply'))
        self.assertIn('--ia-patch', self.lines[-1])

    # issue 0310: with no --ia-patch the -ia patch is chosen by the i0 commit's base (build.py IA_PATCHES). Here the
    # "5.4.8" row's base is the release tag and the "master" row's base is a later commit; the i0 pin contains the
    # tag always and the later commit only when it is the pin itself (a master-cut i0 tree) or an ancestor of it.
    def by_base_rows(self, master_base):
        master_patch = self.tmp / 'ia-master-440878983.patch'
        master_patch.write_bytes(self.ia_patch.read_bytes())
        return [{'name': '9045', 'base': self.tag_commit, 'base_name': 'Release-9.9.9', 'patch': str(self.ia_patch)},
                {'name': '3515', 'base': master_base, 'base_name': 'master', 'patch': str(master_patch)}]

    def test_ia_patch_by_base_takes_the_release_patch_for_a_release_based_i0(self):
        rows = self.by_base_rows(self.x64)            # the x64 pin is not in the i0 pin's history
        self.assertEqual(self.driver(ia_patch='by-base', ia_patches=rows).run(), 0, self.lines[-3:])
        b = self.state()['build']['outputs']
        self.assertEqual(Path(b['ia_patch']).name, self.ia_patch.name)
        self.assertEqual(b['ia_patch_choice'], f"by base: Release-9.9.9 ({self.tag_commit[:9]}), 9045's patch")
        self.assertTrue((Path(b['i0ia_layout']) / self.ia_patch.name).is_file())

    def test_ia_patch_by_base_takes_the_master_patch_for_a_master_based_i0(self):
        rows = self.by_base_rows(self.i0)             # the newest base the i0 commit contains
        self.assertEqual(self.driver(ia_patch='by-base', ia_patches=rows).run(), 0, self.lines[-3:])
        b = self.state()['build']['outputs']
        self.assertEqual(Path(b['ia_patch']).name, 'ia-master-440878983.patch')
        self.assertEqual(b['ia_patch_choice'], f"by base: master ({self.i0[:9]}), 3515's patch")
        self.assertTrue((Path(b['i0ia_layout']) / 'ia-master-440878983.patch').is_file())
        self.assertTrue(any('-ia instruments patch: ia-master-440878983.patch' in ln for ln in self.lines))

    def test_ia_patch_by_base_with_no_known_base_is_a_stop(self):
        rows = [{'name': 'x', 'base': '0123456789' * 4, 'base_name': 'unknown', 'patch': str(self.ia_patch)}]
        self.assertEqual(self.driver(ia_patch='by-base', ia_patches=rows).run(), 10)
        self.assertTrue(self.lines[-1].startswith('take_release: STOP build (CL step 3): no instruments patch for the '
                                                  f'-ia build: the i0 commit {self.i0[:9]} contains none'))

    def test_ia_patch_given_wins_over_the_base(self):
        rows = self.by_base_rows(self.i0)
        self.assertEqual(self.driver(ia_patches=rows).run(), 0, self.lines[-3:])   # ia_patch = the fixture's patch
        b = self.state()['build']['outputs']
        self.assertEqual((Path(b['ia_patch']).name, b['ia_patch_choice']), (self.ia_patch.name, 'given (--ia-patch)'))

    def test_ia_patch_default_rows_pick_by_real_bases(self):
        # the shipped rows: Release-5.4.8 (7307aefdf), upstream master ba8aafdca, the i0 pin batch 3 39f17f754
        # (ticket 0463), the i0 pin batch 5 5d18d3d6c (ticket 4968) and Release-5.5 b0bce8c2f (ticket 0766), none in
        # this fake fork
        from stages import build
        self.assertEqual([r['base'][:9] for r in build.IA_PATCHES],
                         ['7307aefdf', 'ba8aafdca', '39f17f754', '5d18d3d6c', 'b0bce8c2f'])
        for r in build.IA_PATCHES:
            self.assertTrue(Path(r['patch']).is_file(), r['patch'])
        self.assertEqual(self.driver(ia_patch='by-base').run(), 10)
        self.assertIn('contains none of the instruments patches\' bases (Release-5.4.8 (7307aefdf), upstream master '
                      '(ba8aafdca), i0-ship-batch3 (39f17f754), i0-ship-batch5 (5d18d3d6c), Release-5.5 (b0bce8c2f))',
                      self.lines[-1])

    def test_ia_patch_0766_row_is_release_5_5_and_runs_without_loadstring(self):
        # ticket 0766: the Release-5.5 row carries 4968's runner (no _G.loadstring, build 26577625) and no
        # take_release 43-civ define block (the take injects that itself)
        from stages import build
        row = [r for r in build.IA_PATCHES if r['name'] == '0766']
        self.assertEqual(len(row), 1)
        self.assertEqual(row[0]['base'], 'b0bce8c2f23c865cf5b0779986654366c88cef62')
        text = Path(row[0]['patch']).read_text(encoding='utf-8', errors='replace')
        self.assertIn('lua_pushcfunction(L, LuaExecCompile)', text)
        self.assertNotIn('# take_release (issue 4252)', text)
        self.assertNotIn('m_iCacheStamp', text)
        added = [ln for ln in text.splitlines() if ln.startswith('+') and 'loadstring(' in ln and 'G.loadstring' in ln]
        self.assertEqual(added, [], 'the runner must not call _G.loadstring')

    def test_ia_patch_row_covers_the_current_pin_and_runs_without_loadstring(self):
        # tickets 4968 and 0801: the instruments row that serves deps/i0.lock's pin is the one whose base the pin is
        # (or is built on: the lock's 'base = <name> (<commit>)' line, or a Re-pinning line '<pin> = ... <base>'), and
        # its runner compiles without _G.loadstring (Civ V build 26577625 sets it to nil). Since the 5.5 pin move
        # (0765) that row is 0766's (Release-5.5 b0bce8c2f); this test follows the lock instead of naming a row.
        from stages import build
        lock = (REPO / 'deps' / 'i0.lock').read_text(encoding='utf-8')
        pin = re.search(r'(?m)^commit\s*=\s*([0-9a-f]{40})\s*$', lock).group(1)
        m = re.search(r'(?m)^base\s*=\s*.*\(([0-9a-f]{7,40})\)\s*$', lock)
        lock_base = m.group(1) if m else None

        def covers(row):
            b = row['base']
            if b == pin or (lock_base and b.startswith(lock_base)):
                return True
            return bool(re.search(r'(?m)^#.*%s = \S+ %s' % (pin[:9], b[:9]), lock))

        rows = [r for r in build.IA_PATCHES if covers(r)]
        self.assertEqual(len(rows), 1, ('exactly one instruments row serves the i0 pin of deps/i0.lock', pin, lock_base))
        text = Path(rows[0]['patch']).read_text(encoding='utf-8', errors='replace')
        self.assertIn('lua_pushcfunction(L, LuaExecCompile)', text)
        added = [ln for ln in text.splitlines() if ln.startswith('+') and 'loadstring(' in ln and 'G.loadstring' in ln]
        self.assertEqual(added, [], 'the runner must not call _G.loadstring')

    def test_ia_patch_4968_row_runs_without_loadstring(self):
        # ticket 4968: its row (the 5.4.8 pin batch 5) keeps a runner that compiles without _G.loadstring
        from stages import build
        row = [r for r in build.IA_PATCHES if r['name'] == '4968']
        self.assertEqual(len(row), 1)
        text = Path(row[0]['patch']).read_text(encoding='utf-8', errors='replace')
        self.assertIn('lua_pushcfunction(L, LuaExecCompile)', text)
        self.assertIn('local _, _, compileC = ...', text)
        added = [ln for ln in text.splitlines() if ln.startswith('+') and 'loadstring(' in ln and 'G.loadstring' in ln]
        self.assertEqual(added, [], 'the runner must not call _G.loadstring')

    def test_ia_patch_by_base_the_pin_row_wins_over_an_older_pin_row(self):
        # ticket 4968: rows for two i0 pins, the older base an ancestor of the newer: the i0 commit that contains the
        # newer pin takes its row; one that contains only the older base takes the older row (a base no row fits:
        # test_ia_patch_by_base_with_no_known_base_is_a_stop)
        new_patch = self.tmp / 'ia-pin5-4968.patch'
        new_patch.write_bytes(self.ia_patch.read_bytes())
        rows = [{'name': '0463', 'base': self.tag_commit, 'base_name': 'i0-ship-batch3', 'patch': str(self.ia_patch)},
                {'name': '4968', 'base': self.i0, 'base_name': 'i0-ship-batch5', 'patch': str(new_patch)}]
        self.assertEqual(self.driver(ia_patch='by-base', ia_patches=rows).run(), 0, self.lines[-3:])
        b = self.state()['build']['outputs']
        self.assertEqual(Path(b['ia_patch']).name, 'ia-pin5-4968.patch')
        self.assertEqual(b['ia_patch_choice'], f"by base: i0-ship-batch5 ({self.i0[:9]}), 4968's patch")

    def test_ia_patch_by_base_an_i0_without_the_newer_pin_keeps_the_older_row(self):
        new_patch = self.tmp / 'ia-pin5-4968.patch'
        new_patch.write_bytes(self.ia_patch.read_bytes())
        rows = [{'name': '0463', 'base': self.tag_commit, 'base_name': 'i0-ship-batch3', 'patch': str(self.ia_patch)},
                {'name': '4968', 'base': self.x64, 'base_name': 'i0-ship-batch5', 'patch': str(new_patch)}]
        self.assertEqual(self.driver(ia_patch='by-base', ia_patches=rows).run(), 0, self.lines[-3:])
        b = self.state()['build']['outputs']
        self.assertEqual(b['ia_patch_choice'], f"by base: i0-ship-batch3 ({self.tag_commit[:9]}), 0463's patch")

    def test_ia_patch_config_reads_the_old_default_as_by_base(self):
        self.assertEqual(tr._ia_patch_config(None, None), 'by-base')
        self.assertEqual(tr._ia_patch_config(None, str(tr.IA_OLD_DEFAULT)), 'by-base')   # a pre-0310 work folder
        self.assertEqual(tr._ia_patch_config('by-base', str(self.ia_patch)), 'by-base')
        self.assertEqual(tr._ia_patch_config(None, str(self.ia_patch)), str(self.ia_patch))   # chosen before: kept
        self.assertEqual(tr._ia_patch_config(str(self.ia_patch), None), str(self.ia_patch.resolve()))

    def test_census_site_is_a_stop(self):
        self.setenv(FAKE_CENSUS_SITE='CvFoo.cpp:10 size_t m_x\n')
        self.assertEqual(self.driver().run(), 10)
        self.assertTrue(self.lines[-1].startswith('take_release: STOP census (CL step 8): 1 x64-only save-width'))

    def test_model_difference_is_a_stop_and_refusal_a_failure(self):
        self.setenv(FAKE_MODEL_RC='1')
        self.assertEqual(self.driver().run(), 10)
        self.assertIn('STOP model (CL step 10): the model differs from proxy/model', self.lines[-1])
        os.environ['FAKE_MODEL_RC'] = '2'
        self.assertEqual(self.driver().run(), 1)
        self.assertIn('FAILED model: regen_model.py exit 2', self.lines[-1])

    def test_missing_entry_point_is_not_built_yet(self):
        self.tools['pack'] = [PY, '-I', str(self.tmp / 'no' / 'pack.py')]
        self.assertEqual(self.driver().run(), 10)
        self.assertTrue(self.lines[-1].startswith('take_release: STOP pack (CL step -): not built yet (issue 3504)'))

    def test_pack_exit_10_is_a_stop(self):
        self.setenv(FAKE_PACK_STOP='1')
        self.assertEqual(self.driver().run(), 10)
        self.assertEqual(self.lines[-1], 'take_release: STOP pack (CL step -): the accepted DLL x cannot be fetched; '
                                         'to unblock: fetch it')

    def test_player_dll_mismatch_is_a_stop(self):
        self.desc = self.make_desc(player_dll_sha256='0' * 64)
        self.assertEqual(self.driver().run(), 10)
        self.assertIn('STOP pack', self.lines[-1])
        self.assertIn('player_dll_sha256', self.lines[-1])

    def test_offline_failure_and_resume_of_passed_checks(self):
        self.setenv(FAKE_HUTEST_FAIL='1')
        self.assertEqual(self.driver().run(), 1)
        self.assertIn('offline check hutest exit 1', self.lines[-1])
        os.environ.pop('FAKE_HUTEST_FAIL')
        self.assertEqual(self.driver().run(), 0)
        self.assertEqual(len(self.called('test_proxy_rt')), 1)  # passed before: not run again
        self.assertEqual(len(self.called('hutest')), 3)  # failed once, then hutest and scenarios

    def test_scratch_folder_takes_the_large_outputs(self):
        scratch = self.tmp / 'scratch'
        self.assertEqual(self.driver(scratch=str(scratch)).run(), 0, self.lines[-3:])
        for d in ('census', 'offline', 'drift', 'fixchecks', 'savetest-1'):
            self.assertTrue((scratch / d).is_dir(), d)
            self.assertFalse((self.work / d).exists(), d)

    def test_savetest_stops_when_the_scratch_drive_is_too_full(self):
        self.assertEqual(self.driver(keep_free_gb=10 ** 9).run(), 10)
        self.assertIn('STOP savetest (CL step 13)', self.lines[-1])
        self.assertIn('GB must stay free', self.lines[-1])
        self.assertEqual(self.called('run_d10'), [])

    def test_savetest_rows_differ_is_a_stop(self):
        self.setenv(FAKE_ROWS=json.dumps({'tiny': 30000, 'standard': 351496, 'huge': 683123}))
        self.assertEqual(self.driver().run(), 10)
        self.assertIn('STOP savetest (CL step 13)', self.lines[-1])
        self.assertIn('tiny 30000 (reference 30666)', self.lines[-1])

    def test_savetest_reference_save_with_wrong_hash_is_a_stop(self):
        d = self.make_desc()
        (Path(d.reference_saves['huge'].path)).write_bytes(b'changed')
        self.desc = d
        self.assertEqual(self.driver().run(), 10)
        self.assertIn('STOP savetest', self.lines[-1])
        self.assertIn('the descriptor says', self.lines[-1])

    def test_drift_difference_is_a_stop(self):
        self.setenv(FAKE_DRIFT="(53, 'TechChoices')")
        self.assertEqual(self.driver().run(), 10)
        self.assertIn('STOP drift (CL step 14): x64 != x86 on Tiny 0-100', self.lines[-1])

    def test_fixcheck_failure_exit_1(self):
        self.setenv(FAKE_0096_FAIL='1')
        self.assertEqual(self.driver().run(), 1)
        self.assertIn('FAILED fixchecks: fix check 0096 did not pass', self.lines[-1])

    def test_old_save_failure_exit_1(self):
        self.setenv(FAKE_RUN_SERVER_FAIL='1')
        self.assertEqual(self.driver().run(), 1)
        self.assertIn('fix check oldsave did not pass', self.lines[-1])

    def test_survey_stops_on_a_tag_missing_in_the_fork(self):
        git(self.fork, 'tag', '-d', 'Release-9.9.9')
        self.assertEqual(self.driver().run(), 10)
        self.assertIn('STOP survey (CL step 1): tag Release-9.9.9 is not in the fork clone', self.lines[-1])

    def test_survey_stops_when_the_tag_is_not_the_descriptors_commit(self):
        self.desc = self.make_desc(upstream_commit='b' * 40)
        self.assertEqual(self.driver().run(), 10)
        self.assertIn('the descriptor says ' + 'b' * 40, self.lines[-1])

    def test_survey_flags_a_changed_release(self):
        # base = the i0 pin's parent's parent is not possible here; use an older base: a commit before the tag
        f = self.fork
        git(f, 'checkout', '-q', '-b', 'newrel', self.tag_commit)
        (f / 'CvGameCoreDLL_Expansion2' / 'c.cpp').write_text('void c() { gDLL->NewCall(); }\n')
        (f / 'CvGameCoreDLL_Expansion2' / 'a.cpp').write_text('changed\n')
        git(f, 'add', '-A')
        git(f, 'commit', '-q', '-m', 'next release')
        git(f, 'tag', '-f', 'Release-9.9.9')
        new = git(f, 'rev-parse', 'HEAD')
        self.desc = self.make_desc(upstream_commit=new)
        self.driver().run(only='survey')
        out = self.state()['survey']['outputs']
        self.assertTrue(out['flags']['new_engine_call'])
        self.assertTrue(out['flags']['new_source_file'])
        self.assertTrue(out['flags']['carry_overlap'])  # a.cpp: changed by the x64 pin and by the release
        rep = json.loads((self.work / 'survey' / 'survey.json').read_text())
        self.assertEqual(rep['engine_calls']['added'], ['NewCall'])
        self.assertIn('CvGameCoreDLL_Expansion2/a.cpp', rep['carry_overlap'])



class TestMajorCivs(StageFixture):
    """Issue 4252 (epic 0334): --major-civs 43 through the candidate stages; the default path unchanged."""

    def test_43_candidate_run_builds_and_names_the_43_variant(self):
        rc = self.driver(major_civs=43).run()
        self.assertEqual(rc, 0, self.lines[-3:])
        st = self.state()
        b = st['build']['outputs']
        self.assertEqual(b['major_civs'], 43)
        # x64: build_all.py --major-civs 43 first, then build_server.py --deps of it (no define for the server)
        ba = self.called('build_all')
        self.assertEqual(len(ba), 1)
        self.assertIn('--major-civs 43', ba[0])
        self.assertIn('--config release', ba[0])
        bs = self.called('build_server')
        self.assertEqual(len(bs), 1)
        self.assertIn('--deps ' + str(self.work / 'build' / f'x64-{self.x64[:8]}-m43' / 'deps'), bs[0])
        self.assertNotIn('--major-civs', bs[0])
        lay = Path(b['x64_layout'])
        self.assertTrue(lay.name.startswith(f'x64-{self.x64[:8]}-m43-p'), lay.name)
        self.assertIn(b'm43', (lay / 'bin' / 'CvGameCore_Expansion2.dll').read_bytes())
        self.assertIn('Major civilizations: 43', (lay / 'README.txt').read_text())
        # 32-bit plain and -ia: archives (no worktree), the define on the compile line, the +m43 version stubs
        self.assertEqual(Path(b['i0_layout']).name, f'i0ship-{self.i0[:8]}-m43')
        self.assertEqual(Path(b['i0ia_layout']).name, f'i0ship-{self.i0[:8]}-m43-ia')
        src = Path(b['i0_src'])
        self.assertFalse((src / '.git').exists())
        self.assertEqual((src / 'predefs.txt').read_text(), '/DFXS_IS_DLL /DWIN32 /DNDEBUG /DMOD_GLOBAL_MAX_MAJOR_CIVS=43')
        self.assertIn('"Release-9.9.9-1-g', (src / 'update_commit_id.bat').read_text())
        self.assertIn('+m43 Copy"', (src / 'update_commit_id.bat').read_text())
        ia = next((self.work / 'src').glob('i0iam43-*'))
        self.assertIn('+ia440878983+m43 Copy', (ia / 'update_commit_id.bat').read_text())
        self.assertIn('MOD_GLOBAL_MAX_MAJOR_CIVS=43', (ia / 'predefs.txt').read_text())
        self.assertIn('return 2', (ia / 'CvGameCoreDLL_Expansion2' / 'b.cpp').read_text())   # the patch is applied
        self.assertFalse(list((self.work / 'src').glob('i0wt-*')))
        # the candidate locks carry the count's keys, not the 22 ones
        dl = (self.work / 'deps' / 'dll.lock').read_text()
        il = (self.work / 'deps' / 'i0.lock').read_text()
        for key in ('release-43', 'verified-43'):
            self.assertRegex(dl, rf'(?m)^{key} +=')
        for key in ('release-43', 'instance-43'):
            self.assertRegex(il, rf'(?m)^{key} +=')
        self.assertNotRegex(dl, r'(?m)^(release|verified) +=')
        self.assertNotRegex(il, r'(?m)^(release|instance) +=')
        self.assertIn('--major-civs 43', dl)
        # pack: the (3b) player DLL of the tag, and --major-civs 43 for pack.py
        self.assertEqual((self.work / 'inputs' / 'player-x86' / 'CvGameCore_Expansion2.dll').read_bytes(),
                         self.player_dll_43)
        self.assertIn('--major-civs 43', self.called('pack')[0])
        self.assertEqual(st['pack']['outputs']['player_dll'], '(3b) 43 Civs Community Patch/CvGameCore_Expansion2.dll')

    def test_22_run_calls_and_names_as_before(self):
        self.assertEqual(self.driver().run(), 0, self.lines[-3:])
        self.assertEqual(self.called('build_all'), [])
        self.assertNotIn('--deps', self.called('build_server')[0])
        self.assertNotIn('--major-civs', self.called('pack')[0])
        b = self.state()['build']['outputs']
        self.assertNotIn('major_civs', b)
        self.assertEqual(Path(b['i0_layout']).name, f'i0ship-{self.i0[:8]}')
        self.assertTrue((Path(b['i0_src']) / '.git').exists())   # the plain 32-bit build stays a worktree
        self.assertNotIn('MOD_GLOBAL_MAX_MAJOR_CIVS', (Path(b['i0_src']) / 'predefs.txt').read_text())
        self.assertRegex((self.work / 'deps' / 'dll.lock').read_text(), r'(?m)^release +=')
        self.assertNotIn('major_civs', self.state()['build']['inputs'])

    def test_a_43_rerun_after_a_22_run_builds_again(self):
        self.assertEqual(self.driver().run(), 0, self.lines[-3:])
        self.assertEqual(self.driver(major_civs=43).run(), 0, self.lines[-3:])
        self.assertEqual(len(self.called('build_server')), 2)
        self.assertEqual(len(self.called('build_all')), 1)
        self.assertEqual(self.state()['build']['outputs']['major_civs'], 43)

    def _pin_with_live_line(self, value):
        hdr = self.fork / 'CvGameCoreDLLUtil' / 'include' / 'CustomModsGlobal.h'
        hdr.write_text(HEADER_22.replace(f'// #define MOD_GLOBAL_MAX_MAJOR_CIVS ({value})',
                                         f'#define MOD_GLOBAL_MAX_MAJOR_CIVS ({value})'))
        git(self.fork, 'commit', '-q', '-am', f'switch on {value}')
        c = git(self.fork, 'rev-parse', 'HEAD')
        for name, key in (('dll.lock', 'x64'), ('i0.lock', 'i0')):
            lock = self.deps / name
            old = self.x64 if key == 'x64' else self.i0
            lock.write_text(lock.read_text().replace(old, c))
        return c

    def test_a_live_header_line_of_another_count_is_a_stop_never_a_silent_22(self):
        c = self._pin_with_live_line(43)
        self.assertEqual(self.driver().run(), 10, self.lines[-3:])
        self.assertTrue(self.lines[-1].startswith('take_release: STOP build (CL step 6): the x64 source '
                                                  f'{c[:9]}: the source sets MOD_GLOBAL_MAX_MAJOR_CIVS to 43'),
                        self.lines[-1])
        self.assertEqual(self.called('build_server'), [])

    def test_a_live_header_line_equal_to_the_count_builds_without_a_second_define(self):
        self._pin_with_live_line(43)
        self.assertEqual(self.driver(major_civs=43).run(), 0, self.lines[-3:])
        src = Path(self.state()['build']['outputs']['i0_src'])
        self.assertEqual((src / 'predefs.txt').read_text(), '/DFXS_IS_DLL /DWIN32 /DNDEBUG /DMOD_GLOBAL_MAX_MAJOR_CIVS=43')

    def test_43_player_dll_against_the_descriptors_43_hash(self):
        self.desc = self.make_desc(player_dll_43_sha256='0' * 64)
        self.assertEqual(self.driver(major_civs=43).run(), 10)
        self.assertIn("STOP pack (CL step -): the tag's (3b) 43 Civs Community Patch/CvGameCore_Expansion2.dll is",
                      self.lines[-1])
        self.assertIn("fix the descriptor's player_dll_43_sha256", self.lines[-1])

    def test_failing_43_x64_dll_build_exit_1(self):
        self.setenv(FAKE_BUILD_ALL_FAIL='1')
        self.assertEqual(self.driver(major_civs=43).run(), 1)
        self.assertIn('FAILED build: tools/x64-dll/build_all.py --major-civs 43 exit 1', self.lines[-1])
        self.assertEqual(self.called('build_server'), [])


if __name__ == '__main__':
    unittest.main()
