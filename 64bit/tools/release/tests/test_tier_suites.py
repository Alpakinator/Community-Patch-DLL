"""Hermetic tests of the area-to-checks map, the suites runner and the hotfix tier's suites stage (epic 0630, ticket
5014): tools/suites/areas.json, tools/suites/run_suites.py, tools/release/tiers/stages/suites.py.

    python -I -X utf8 tools/release/tests/test_tier_suites.py

The runner is tested on small fake suites in a temporary git repository; the stage through the tier driver with a fake
build stage, a fake base take and a fake quietlock.py. No real suite, build, lock or game is run.
"""
import json
import os
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent
RELEASE = HERE.parent
REPO = RELEASE.parents[1]
sys.path.insert(0, str(RELEASE))
sys.path.insert(0, str(RELEASE / 'tiers'))
sys.path.insert(0, str(REPO / 'tools' / 'suites'))
import take_release as tr  # noqa: E402
import take_tier as tt  # noqa: E402
import run_suites as rs  # noqa: E402

PY = sys.executable
REAL_STAGE = RELEASE / 'tiers' / 'stages' / 'suites.py'

FAKE_QL = r'''
import os, subprocess, sys
a = sys.argv[1:]
if a and a[0] == 'run':
    cmd = a[a.index('--') + 1:]
    print('FAKE-QL run', ' '.join(a[1:a.index('--')]), flush=True)
    sys.exit(subprocess.call(cmd))
sys.exit(0)
'''

PASS_TEST = 'def test_one():\n    assert True\n\ndef test_two():\n    assert 1 + 1 == 2\n'
FAIL_TEST = 'def test_one():\n    assert False, "broken on purpose"\n'
SKIP_TEST = ('import pytest\n\ndef test_ok():\n    pass\n\n'  # split: the export gate reads n@pytest... as an e-mail
             '@pytest.mark.skip(reason="needs the moon")\ndef test_moon():\n    pass\n')
SLOW_TEST = 'import time\n\ndef test_slow():\n    time.sleep(30)\n'


def git(repo, *args):
    r = subprocess.run(['git', '-C', str(repo), '-c', 'user.name=t', '-c', 'user.email=t@example.com',
                        '-c', 'commit.gpgsign=false'] + list(args), capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f'git {args}: {r.stderr}')
    return r.stdout.strip()


def fake_map(**over):
    m = {
        'format': 1,
        'always': ['safe'],
        'suites': {
            'A': {'kind': 'pytest', 'needs': 'none', 'tier': 'hotfix', 'targets': ['a']},
            'B': {'kind': 'pytest', 'needs': 'none', 'tier': 'hotfix', 'targets': ['b']},
            'safe': {'kind': 'pytest', 'needs': 'none', 'tier': 'hotfix', 'targets': ['safe']},
            'N': {'needs': 'build', 'tier': 'intermediate', 'how': 'a native build of b'},
            'G': {'needs': 'game', 'tier': 'intermediate', 'how': 'an installed game'},
        },
        'areas': {
            'a': {'paths': ['a/**'], 'suites': ['A']},
            'b': {'paths': ['b/**'], 'suites': ['B', 'N'], 'live': ['savetest']},
            'g': {'paths': ['g/**'], 'suites': ['G'], 'live': ['hi_journey']},
            'notes': {'paths': ['*.md', 'docs/**'], 'suites': [], 'note': 'documents'},
        },
    }
    for k, v in over.items():
        m[k] = v
    return m


class FakeRepo(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = Path(self._td.name)
        self.repo = self.tmp / 'repo'
        for d, body in (('a', PASS_TEST), ('b', PASS_TEST), ('safe', PASS_TEST), ('g', PASS_TEST)):
            (self.repo / d).mkdir(parents=True)
            (self.repo / d / f'test_{d}.py').write_text(body)
        (self.repo / 'README.md').write_text('x\n')
        git(self.repo, 'init', '-q')
        git(self.repo, 'add', '-A')
        git(self.repo, 'commit', '-q', '-m', 'base')
        self.base = git(self.repo, 'rev-parse', 'HEAD')
        self.map = self.tmp / 'areas.json'
        self.write_map(fake_map())
        self.allow = self.tmp / 'allow.json'
        self.allow.write_text('[]')
        self.lines = []

    def tearDown(self):
        self._td.cleanup()

    def write_map(self, m):
        self.map.write_text(json.dumps(m))

    def commit(self, rel, body, msg='change'):
        p = self.repo / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(body)
        git(self.repo, 'add', '-A')
        git(self.repo, 'commit', '-q', '-m', msg)
        return git(self.repo, 'rev-parse', 'HEAD')

    def runner(self, *args, out='out'):
        self.out = self.tmp / out
        rc = rs.main(['--map', str(self.map), '--allowlist', str(self.allow), '--repo', str(self.repo),
                      '--out', str(self.out)] + list(args), out=self.lines.append)
        return rc

    def results(self):
        return json.loads((self.out / 'results.json').read_text())

    def by_name(self):
        return {s['name']: s for s in self.results()['suites']}


# --------------------------------------------------------------------------------------------------- the real map

class RealMap(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.m = rs.load_map(REPO / 'tools' / 'suites' / 'areas.json')
        r = subprocess.run(['git', '-C', str(REPO), 'ls-files'], capture_output=True, text=True, check=True)
        cls.files = [ln for ln in r.stdout.splitlines() if ln]

    def test_every_tracked_file_and_top_level_folder_is_mapped(self):
        unmapped = [f for f in self.files if not rs.areas_of(self.m, f)]
        self.assertEqual(unmapped, [], f'{len(unmapped)} tracked file(s) in no area, e.g. {unmapped[:5]}')
        tops = sorted({f.split('/')[0] for f in self.files if '/' in f})
        for t in tops:
            self.assertTrue(any(rs.areas_of(self.m, f) for f in self.files if f.startswith(t + '/')), t)

    def test_unmapped_folder_is_caught(self):
        # negative: the same check on a map without the area `ops` names the ops/ files
        m = json.loads((REPO / 'tools' / 'suites' / 'areas.json').read_text(encoding='utf-8'))
        del m['areas']['ops']
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / 'areas.json'
            p.write_text(json.dumps(m))
            m2 = rs.load_map(p)
        unmapped = [f for f in self.files if not rs.areas_of(m2, f)]
        self.assertTrue(unmapped)
        self.assertTrue(all(f.startswith('ops/') for f in unmapped), unmapped[:5])

    def test_runnable_suites_point_at_existing_paths(self):
        for n, s in self.m['suites'].items():
            if s['needs'] == 'none' and s['kind'] == 'pytest':
                for t in s['targets']:
                    self.assertTrue((REPO / t).exists(), f'{n}: {t}')

    def test_every_suite_names_its_tier_and_minutes(self):
        for n, s in self.m['suites'].items():
            self.assertIn(s['tier'], rs.TIERS, n)
            self.assertTrue(s.get('measured_s') or s.get('measured'), f'{n}: no measured_s and no measured note')
            if s.get('measured_s'):
                self.assertTrue(s.get('measured'), f'{n}: measured_s without when/where (rule 4)')

    def test_hotfix_tier_holds_only_short_suites(self):
        # lead-0630 2026-11-21: a needs-none suite measured over 8 min is intermediate (the map's tiers.rule)
        for n, s in self.m['suites'].items():
            if s['tier'] == 'hotfix' and s.get('measured_s'):
                self.assertLessEqual(s['measured_s'], 480, n)

    def test_perf_ledger_runs_with_the_ledger_venv_that_holds_duckdb(self):
        # MC #10921: with the runner's own Python the ledger's duckdb tests skip, and a skip fails the suite
        s = self.m['suites']['perf-ledger']
        py = rs.suite_python(s)
        self.assertTrue(os.path.normcase(py).endswith(os.path.normcase(os.path.join('venvs', 'ledger', 'Scripts',
                                                                                     'python.exe'))), py)
        if os.path.isfile(py):        # on a machine without the venv the runner fails the suite by name (no skip)
            r = subprocess.run([py, '-c', 'import duckdb'], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)

    def test_release_folder_is_full_with_a_hotfix_substitute(self):
        # MC #10917: the whole folder is out of the intermediate take (it runs at the merges that change tools/release)
        S = self.m['suites']
        self.assertEqual(S['release-tiers']['tier'], 'full')
        self.assertIn('MC #10917', S['release-tiers']['tier_note'])
        self.assertEqual(S['release-tiers']['hotfix_substitute'], ['release-hotfix', 'release-intermediate'])
        hot, mid = S['release-hotfix']['targets'], S['release-intermediate']['targets']
        self.assertEqual((S['release-hotfix']['tier'], S['release-intermediate']['tier']), ('hotfix', 'intermediate'))
        self.assertIn('tools/release/tests/test_take_release.py', hot)
        # every tier test file is in exactly one of the two (MC #10980: M2's six files are release-intermediate)
        on_disk = sorted(str(f.relative_to(REPO).as_posix()) for f in (REPO / 'tools/release/tests').glob('test_tier_*.py'))
        tier_files = [t for t in hot + mid if '/test_tier_' in t]
        self.assertEqual(sorted(tier_files), on_disk)
        self.assertEqual(len(tier_files), len(set(tier_files)))
        for f in ('intermediate', 'linkwrap', 'modpack', 'parallel', 'scenes', 'sweep'):
            self.assertIn(f'tools/release/tests/test_tier_{f}.py', mid)

    def test_harness_skips_safety_scan_and_allowlists_live_tests(self):
        self.assertIn('--ignore=tools/harness/tests/test_safety_static.py', self.m['suites']['harness']['pytest_args'])
        allow = rs.load_allowlist(REPO / 'tools' / 'suites' / 'skip_allowlist.json')
        self.assertTrue(any(a['suite'] == 'harness' and 'C5_HARNESS_LIVE' in a['match'] for a in allow))

    def test_suites_slow_under_load_have_tier_timeouts(self):
        # Ticket 5024, take 3 (2026-11-23 00:15): release-hotfix (474 tests since M2) timed out at its hotfix 900 s inside
        # the intermediate take's suites barrier with 56 % done. Like harness it has 1800 s above the hotfix tier.
        for n in ('harness', 'release-hotfix'):
            bt = self.m['suites'][n].get('timeout_s_by_tier') or {}
            for tier in ('intermediate', 'full'):
                self.assertGreaterEqual(bt.get(tier, 0), 1800, (n, tier))
            self.assertLessEqual(self.m['suites'][n]['timeout_s'], 900, n)   # the hotfix tier keeps its limit

    def test_perf_never_one_session(self):
        # 0627 section 2: two module-name collisions when perf/ runs in one pytest session
        for n, s in self.m['suites'].items():
            if s.get('kind') == 'pytest':
                perf = [t for t in s['targets'] if t.startswith('perf/') and not t.startswith('perf/ledger')]
                self.assertLessEqual(len(perf), 1, n)

    def test_always_has_safety_scan_and_export_gate(self):
        self.assertIn('safety-static', self.m['always'])
        self.assertIn('export-gate', self.m['always'])
        self.assertEqual(self.m['suites']['safety-static']['targets'], ['tools/harness/tests/test_safety_static.py'])
        self.assertIn('tools/export/check.py', self.m['suites']['export-gate']['cmd'])

    def test_some_real_paths(self):
        self.assertIn('release-driver', rs.areas_of(self.m, 'tools/release/take_release.py'))
        self.assertIn('server', rs.areas_of(self.m, 'server/src/main.cpp'))
        self.assertEqual(rs.areas_of(self.m, 'tracker/issues/5014-x.md'), ['tracker'])
        self.assertNotIn('tracker', rs.areas_of(self.m, 'tracker/server/app.py'))
        self.assertNotIn('perf', rs.areas_of(self.m, 'perf/quietlock/quietlock.py'))


class MapChecks(FakeRepo):
    def test_glob(self):
        self.assertTrue(rs.glob_re('a/**').match('a/x/y.py'))
        self.assertFalse(rs.glob_re('a/**').match('ab/x.py'))
        self.assertTrue(rs.glob_re('*.md').match('README.md'))
        self.assertFalse(rs.glob_re('*.md').match('docs/README.md'))
        self.assertTrue(rs.glob_re('**/evidence/**').match('server/evidence/1/x'))
        self.assertTrue(rs.glob_re('**/evidence/**').match('evidence/x'))

    def bad_map(self, m, needle):
        self.write_map(m)
        self.assertEqual(self.runner('--all'), rs.EXIT_USAGE)
        self.assertTrue(any(needle in ln for ln in self.lines), self.lines)
        self.assertFalse((self.out / 'results.json').exists())

    def test_unknown_suite_in_area_refused(self):
        m = fake_map()
        m['areas']['a']['suites'] = ['nosuch']
        self.bad_map(m, 'unknown suite nosuch')

    def test_area_without_checks_or_reason_refused(self):
        m = fake_map()
        m['areas']['notes'].pop('note')
        self.bad_map(m, 'no note saying why')

    def test_build_suite_without_how_refused(self):
        m = fake_map()
        m['suites']['N'].pop('how')
        self.bad_map(m, 'say `how`')

    def test_allowlist_entry_without_reason_refused(self):
        self.allow.write_text(json.dumps([{'suite': 'B', 'match': 'moon'}]))
        self.assertEqual(self.runner('--all'), rs.EXIT_USAGE)


# --------------------------------------------------------------------------------------------------- the runner

class Runner(FakeRepo):
    def test_diff_maps_to_the_right_suites(self):
        head = self.commit('b/extra.txt', 'x')
        self.assertEqual(self.runner('--diff', f'{self.base}..{head}'), 0, self.lines)
        r = self.results()
        self.assertEqual(r['selection']['areas'], ['b'])
        self.assertEqual(r['selection']['live'], ['savetest'])
        self.assertEqual([s['name'] for s in r['suites']], ['B', 'N', 'safe'])
        st = self.by_name()
        self.assertEqual(st['B']['status'], 'passed')
        self.assertEqual(st['N']['status'], 'not-run')
        self.assertIn('native build', st['N']['why'])
        self.assertEqual(st['B']['counts']['passed'], 2)
        self.assertEqual(r['verdict'], 'passed')

    def test_notes_change_runs_only_always(self):
        head = self.commit('docs/x.md', 'y')
        self.assertEqual(self.runner('--diff', f'{self.base}..{head}'), 0)
        self.assertEqual([s['name'] for s in self.results()['suites']], ['safe'])

    def test_unmapped_change_is_listed(self):
        head = self.commit('zzz/new.txt', 'y')
        self.assertEqual(self.runner('--diff', f'{self.base}..{head}'), 0)
        self.assertEqual(self.results()['selection']['unmapped'], ['zzz/new.txt'])

    def test_working_tree_diff(self):
        (self.repo / 'a' / 'more.txt').write_text('z')
        git(self.repo, 'add', 'a/more.txt')
        self.assertEqual(self.runner('--diff', self.base), 0)
        self.assertEqual(self.results()['selection']['areas'], ['a'])

    def test_times_and_commands_recorded(self):
        self.assertEqual(self.runner('--areas', 'a'), 0)
        st = self.by_name()['A']
        self.assertGreater(st['seconds'], 0)
        self.assertEqual(st['exit'], 0)
        self.assertIn('pytest', st['command'])
        self.assertEqual(st['env']['PYTEST_DISABLE_PLUGIN_AUTOLOAD'], '1')
        self.assertEqual(st['env']['PYTHONDONTWRITEBYTECODE'], '1')
        self.assertTrue(Path(st['log']).is_file())
        self.assertIsNotNone(self.results()['seconds'])

    def test_failing_suite_fails_the_run(self):
        head = self.commit('b/test_b.py', FAIL_TEST)
        self.assertEqual(self.runner('--diff', f'{self.base}..{head}'), rs.EXIT_FAILED)
        st = self.by_name()
        self.assertEqual(st['B']['status'], 'failed')
        self.assertEqual(st['B']['counts']['failed'], 1)
        self.assertNotEqual(st['B']['exit'], 0)
        self.assertEqual(st['safe']['status'], 'passed')   # the others still run
        self.assertEqual(self.results()['verdict'], 'failed')

    def test_skip_counts_as_failed(self):
        self.commit('b/test_b.py', SKIP_TEST)
        self.assertEqual(self.runner('--areas', 'b'), rs.EXIT_FAILED)
        st = self.by_name()['B']
        self.assertEqual(st['status'], 'failed')
        self.assertEqual(st['exit'], 0)   # pytest itself passed
        self.assertIn('skip', st['why'])
        self.assertIn('needs the moon', st['why'])

    def test_allowlisted_skip_passes(self):
        self.commit('b/test_b.py', SKIP_TEST)
        self.allow.write_text(json.dumps([{'suite': 'B', 'match': 'needs the moon', 'reason': 'no moon here'}]))
        self.assertEqual(self.runner('--areas', 'b'), 0, self.lines)
        self.assertEqual(self.by_name()['B']['counts']['skipped_allowed'], 1)

    def test_allowlisted_count_reads_folded_skip_lines(self):
        # pytest folds equal skips into one line ('SKIPPED [2] ...'): the allowed count is tests, not lines
        self.assertEqual(rs.skip_count(['SKIPPED [2] a.py:3: x', 'SKIPPED [1] b.py:4: y']), 3)
        self.assertEqual(rs.skip_count(['SKIPPED a.py: x']), 1)

    def test_allowlist_of_another_suite_does_not_count(self):
        self.commit('b/test_b.py', SKIP_TEST)
        self.allow.write_text(json.dumps([{'suite': 'A', 'match': 'needs the moon', 'reason': 'x'}]))
        self.assertEqual(self.runner('--areas', 'b'), rs.EXIT_FAILED)

    def test_no_tests_fails(self):
        self.commit('b/test_b.py', '# nothing\n')
        self.assertEqual(self.runner('--areas', 'b'), rs.EXIT_FAILED)
        self.assertIn('no test passed', self.by_name()['B']['why'])

    def test_timeout_fails(self):
        self.commit('b/test_b.py', SLOW_TEST)
        self.assertEqual(self.runner('--areas', 'b', '--timeout', '3'), rs.EXIT_FAILED)
        st = self.by_name()['B']
        self.assertEqual(st['status'], 'timeout')
        self.assertLess(st['seconds'], 25)

    def test_budget_precheck_refuses(self):
        m = fake_map()
        m['suites']['B']['measured_s'] = 1200
        self.write_map(m)
        self.assertEqual(self.runner('--areas', 'b', '--budget', '900'), rs.EXIT_BUDGET)
        r = self.results()
        self.assertEqual(r['verdict'], 'over-budget')
        self.assertIn('B 1200 s', r['why'])
        self.assertTrue(all(s['status'] == 'over-budget' for s in r['suites']))
        self.assertFalse((self.out / 'logs' / 'B.log').exists())   # nothing ran

    def test_budget_running_out(self):
        self.commit('b/test_b.py', SLOW_TEST)
        self.assertEqual(self.runner('--areas', 'b', '--budget', '3'), rs.EXIT_BUDGET)
        st = self.by_name()
        self.assertEqual(st['B']['status'], 'over-budget')
        self.assertEqual(st['safe']['status'], 'over-budget')
        self.assertIn('not started', st['safe']['why'])

    def test_game_and_build_suites_never_run(self):
        self.assertEqual(self.runner('--all'), 0)
        st = self.by_name()
        self.assertEqual(st['G']['status'], 'not-run')
        self.assertEqual(st['N']['status'], 'not-run')
        self.assertIsNone(st['G']['command'])

    def test_diff_placeholder_needs_a_range(self):
        m = fake_map()
        m['suites']['gate'] = {'kind': 'exit', 'needs': 'none', 'tier': 'hotfix',
                               'cmd': ['{python}', '-c', 'import sys; sys.exit(sys.argv[1] == sys.argv[2])',
                                       '{diff_base}', '{diff_head}']}
        m['always'] = ['safe', 'gate']
        self.write_map(m)
        self.assertEqual(self.runner('--areas', 'a'), 0)
        self.assertEqual(self.by_name()['gate']['status'], 'not-run')
        head = self.commit('a/x.txt', 'q')
        self.assertEqual(self.runner('--diff', f'{self.base}..{head}', out='o2'), 0)
        g = self.by_name()['gate']
        self.assertEqual(g['status'], 'passed')
        self.assertEqual(g['command'][-2:], [self.base, head])

    def test_exit_suite_failure(self):
        m = fake_map()
        m['suites']['gate'] = {'kind': 'exit', 'needs': 'none', 'tier': 'hotfix', 'cmd': ['{python}', '-c', 'raise SystemExit(1)']}
        m['always'] = ['gate']
        self.write_map(m)
        self.assertEqual(self.runner('--areas', 'a'), rs.EXIT_FAILED)
        self.assertEqual(self.by_name()['gate']['why'], 'exit 1')

    def tiered_map(self):
        m = fake_map()
        m['suites']['Bbig'] = {'kind': 'pytest', 'needs': 'none', 'tier': 'intermediate', 'targets': ['b'],
                               'hotfix_substitute': 'B'}
        m['suites']['Abig'] = {'kind': 'pytest', 'needs': 'none', 'tier': 'intermediate', 'targets': ['a']}
        m['areas']['b']['suites'] = ['Bbig', 'N']
        m['areas']['a']['suites'] = ['A', 'Abig']
        self.write_map(m)

    def test_tier_runs_the_substitute(self):
        self.tiered_map()
        self.assertEqual(self.runner('--areas', 'b', '--tier', 'hotfix'), 0, self.lines)
        st = self.by_name()
        self.assertEqual(st['Bbig']['status'], 'replaced')
        self.assertIn('B runs instead', st['Bbig']['why'])
        self.assertEqual(st['B']['status'], 'passed')
        self.assertEqual(self.results()['selection']['replaced'], {'Bbig': 'B'})

    def test_tier_without_substitute_is_over_tier(self):
        self.tiered_map()
        self.assertEqual(self.runner('--areas', 'a', '--tier', 'hotfix'), rs.EXIT_BUDGET)
        st = self.by_name()
        self.assertEqual(st['Abig']['status'], 'over-tier')
        self.assertIn('--suites Abig', st['Abig']['why'])
        self.assertEqual(st['A']['status'], 'passed')      # the rest still runs
        self.assertEqual(self.results()['verdict'], 'over-budget')

    def test_no_tier_runs_everything(self):
        self.tiered_map()
        self.assertEqual(self.runner('--areas', 'a,b'), 0, self.lines)
        st = self.by_name()
        self.assertEqual(st['Abig']['status'], 'passed')
        self.assertEqual(st['Bbig']['status'], 'passed')
        self.assertNotIn('B', st)

    def test_intermediate_tier_runs_intermediate_suites(self):
        self.tiered_map()
        self.assertEqual(self.runner('--areas', 'a', '--tier', 'intermediate'), 0, self.lines)
        self.assertEqual(self.by_name()['Abig']['status'], 'passed')

    def test_above_hotfix_the_substitute_runs_within_its_suite_not_twice(self):
        # ticket 5024: release-hotfix ran beside release-tiers (which holds its tests) in the measured take
        self.tiered_map()
        self.assertEqual(self.runner('--all', '--tier', 'intermediate'), 0, self.lines)
        st = self.by_name()
        self.assertEqual(st['Bbig']['status'], 'passed')
        self.assertEqual(st['B']['status'], 'replaced')
        self.assertIn('within Bbig', st['B']['why'])
        self.assertFalse((self.out / 'logs' / 'B.log').exists())
        self.assertEqual(self.results()['selection']['within'], {'B': 'Bbig'})

    def test_negative_control_without_a_tier_both_run(self):
        self.tiered_map()
        self.assertEqual(self.runner('--all'), 0, self.lines)
        st = self.by_name()
        self.assertEqual((st['Bbig']['status'], st['B']['status']), ('passed', 'passed'))

    def test_a_full_suite_is_replaced_by_its_substitute_in_the_intermediate_tier(self):
        # MC #10917: release-tiers (full) out of the intermediate take, release-hotfix in its place
        self.tiered_map()
        m = json.loads(self.map.read_text())
        m['suites']['Bbig']['tier'] = 'full'
        self.write_map(m)
        self.assertEqual(self.runner('--all', '--tier', 'intermediate'), 0, self.lines)
        st = self.by_name()
        self.assertEqual(st['Bbig']['status'], 'replaced')
        self.assertIn('in the intermediate tier B runs instead', st['Bbig']['why'])
        self.assertEqual(st['B']['status'], 'passed')
        self.assertFalse((self.out / 'logs' / 'Bbig.log').exists())
        # the full tier runs it, and its substitute runs within it, not twice
        self.assertEqual(self.runner('--all', '--tier', 'full', out='o2'), 0, self.lines)
        st = self.by_name()
        self.assertEqual((st['Bbig']['status'], st['B']['status']), ('passed', 'replaced'))

    def list_substitute_map(self):
        # MC #10980: release-tiers (full) stands in as release-hotfix in the hotfix tier and as release-hotfix +
        # release-intermediate in the intermediate tier
        self.tiered_map()
        m = json.loads(self.map.read_text())
        m['suites']['Bbig']['tier'] = 'full'
        m['suites']['I'] = {'kind': 'pytest', 'needs': 'none', 'tier': 'intermediate', 'targets': ['a']}
        m['suites']['Bbig']['hotfix_substitute'] = ['B', 'I']
        self.write_map(m)

    def test_a_list_substitute_stands_in_by_tier(self):
        self.list_substitute_map()
        self.assertEqual(self.runner('--areas', 'b', '--tier', 'hotfix'), 0, self.lines)
        st = self.by_name()
        self.assertEqual((st['Bbig']['status'], st['B']['status']), ('replaced', 'passed'))
        self.assertNotIn('I', st)                                   # an intermediate member stays out of the hotfix tier
        self.assertEqual(self.results()['selection']['replaced'], {'Bbig': 'B'})
        self.assertEqual(self.runner('--areas', 'b', '--tier', 'intermediate', out='o2'), 0, self.lines)
        st = self.by_name()
        self.assertEqual((st['Bbig']['status'], st['B']['status'], st['I']['status']), ('replaced', 'passed', 'passed'))
        self.assertIn('B, I run instead', st['Bbig']['why'])
        # the full tier runs the whole suite; both members run within it, not twice
        self.assertEqual(self.runner('--all', '--tier', 'full', out='o3'), 0, self.lines)
        st = self.by_name()
        self.assertEqual((st['Bbig']['status'], st['B']['status'], st['I']['status']), ('passed', 'replaced', 'replaced'))
        self.assertEqual(self.results()['selection']['within'], {'B': 'Bbig', 'I': 'Bbig'})

    def test_map_refuses_a_bad_list_substitute(self):
        for bad in (['I'], ['B', 'Bbig'], ['B', 'nosuch'], [], 'I'):
            self.list_substitute_map()
            m = json.loads(self.map.read_text())
            m['suites']['Bbig']['hotfix_substitute'] = bad       # no hotfix member / own tier / unknown / empty
            self.write_map(m)
            self.lines.clear()
            self.assertEqual(self.runner('--all', out='bad'), rs.EXIT_USAGE, bad)
            self.assertTrue(any('hotfix_substitute' in ln for ln in self.lines), (bad, self.lines))

    def test_negative_control_a_full_suite_without_substitute_is_over_tier(self):
        self.tiered_map()
        m = json.loads(self.map.read_text())
        m['suites']['Abig']['tier'] = 'full'
        self.write_map(m)
        self.assertEqual(self.runner('--areas', 'a', '--tier', 'intermediate'), rs.EXIT_BUDGET)
        self.assertEqual(self.by_name()['Abig']['status'], 'over-tier')

    def test_a_suite_runs_with_its_own_interpreter(self):
        # MC #10921: perf-ledger runs with the ledger venv (duckdb); here a named interpreter, checked from inside
        self.commit('b/test_b.py', 'import os, sys\n\ndef test_which_python():\n'
                    '    assert os.path.normcase(sys.executable) == os.path.normcase(os.environ["T5024_PY"])\n')
        m = fake_map()
        m['suites']['B']['python'] = '%T5024_PY%'
        self.write_map(m)
        old = os.environ.get('T5024_PY')
        os.environ['T5024_PY'] = sys.executable
        try:
            self.assertEqual(self.runner('--areas', 'b'), 0, self.lines)
        finally:
            if old is None:
                os.environ.pop('T5024_PY', None)
            else:
                os.environ['T5024_PY'] = old
        st = self.by_name()['B']
        self.assertEqual(st['status'], 'passed')
        self.assertEqual(st['command'][0], sys.executable)

    FLAKY = ('import os\nfrom pathlib import Path\n\n'
             'def test_ok():\n    pass\n\n'
             'def test_flaky_once():\n'
             '    mark = Path(os.environ["T5024_MARK"])\n'
             '    if not mark.exists():\n'
             '        mark.write_text("x")\n'
             '        assert False, "first try fails"\n')

    def test_a_failed_suite_reruns_once_and_a_pass_counts_as_a_recorded_flake(self):
        # MC #11081 (2026-11-23): in the intermediate barrier a failed suite runs once more; a pass counts, and the
        # record names the failing test and its output
        self.commit('b/test_b.py', self.FLAKY)
        self.write_map(fake_map())
        with mock.patch.dict(os.environ, {'T5024_MARK': str(self.tmp / 'mark')}):
            self.assertEqual(self.runner('--areas', 'b', '--rerun-failed', '1'), 0, self.lines)
        st = self.by_name()['B']
        self.assertEqual(st['status'], 'passed')
        self.assertIn('FLAKE', st['why'])
        self.assertEqual(st['flake']['tests'], ['test_flaky_once'])
        self.assertIn('first try fails', st['flake']['output'])
        self.assertTrue(Path(st['flake']['log']).is_file())          # the first try's log is kept beside the rerun's
        self.assertTrue(Path(st['log']).is_file())
        flakes = self.results()['flakes']
        self.assertEqual([(f['suite'], f['tests']) for f in flakes], [('B', ['test_flaky_once'])])
        self.assertTrue(any(ln.startswith('run_suites: RERUN B once') for ln in self.lines), self.lines)

    def test_negative_control_without_the_rerun_the_flaky_suite_fails(self):
        self.commit('b/test_b.py', self.FLAKY)
        self.write_map(fake_map())
        with mock.patch.dict(os.environ, {'T5024_MARK': str(self.tmp / 'mark')}):
            self.assertEqual(self.runner('--areas', 'b'), rs.EXIT_FAILED, self.lines)
        self.assertEqual(self.by_name()['B']['status'], 'failed')
        self.assertNotIn('flakes', self.results())

    def test_a_suite_that_fails_twice_fails_and_is_never_run_a_third_time(self):
        self.commit('b/test_b.py', 'import os\nfrom pathlib import Path\n\ndef test_always():\n'
                    '    p = Path(os.environ["T5024_MARK"])\n'
                    '    p.write_text(p.read_text() + "x" if p.exists() else "x")\n'
                    '    assert False, "always"\n')
        self.write_map(fake_map())
        mark = self.tmp / 'mark'
        with mock.patch.dict(os.environ, {'T5024_MARK': str(mark)}):
            self.assertEqual(self.runner('--areas', 'b', '--rerun-failed', '1'), rs.EXIT_FAILED, self.lines)
        st = self.by_name()['B']
        self.assertEqual(st['status'], 'failed')
        self.assertIn('failed twice', st['why'])
        self.assertEqual(st['first_try']['tests'], ['test_always'])
        self.assertEqual(mark.read_text(), 'xx')                      # two runs, not three
        self.assertNotIn('flakes', self.results())

    def test_a_rerun_cut_by_the_budget_is_not_called_a_second_failure(self):
        # take 5 (2026-11-23): release-hotfix failed once and its rerun was cut by the budget; the record said
        # "failed twice"
        # take 6 (2026-11-23): with one 1.5 s sleep in both tries the near-quiet barrier finished the rerun inside the
        # 4 s budget and it honestly failed twice; now the first try fails at once and only the rerun sleeps past the
        # budget, so the rerun is the one cut (budget 20 s: the first try alone took over 8 s once beside other
        # agents' loads)
        self.commit('b/test_b.py', 'import os\nimport time\nfrom pathlib import Path\n\ndef test_slow_fail():\n'
                    '    p = Path(os.environ["T5024_MARK"])\n'
                    '    if p.exists():\n'
                    '        time.sleep(120)\n'
                    '    p.write_text("x")\n'
                    '    assert False\n')
        self.write_map(fake_map())
        with mock.patch.dict(os.environ, {'T5024_MARK': str(self.tmp / 'mark')}):
            self.assertNotEqual(self.runner('--areas', 'b', '--rerun-failed', '1', '--budget', '20'), 0, self.lines)
        st = self.by_name()['B']
        self.assertNotEqual(st['status'], 'passed')
        self.assertNotIn('failed twice', st['why'])
        if 'first_try' not in st:       # the first try itself was cut: over-budget, never rerun; not this test's path
            self.assertEqual(st['status'], 'over-budget', st['why'])
            self.skipTest('the first try alone used the 20 s budget (machine too loaded): ' + st['why'])
        # the rerun was cut (over-budget), or the first try used the whole budget and there was no rerun
        self.assertIn('no rerun' if st['status'] == 'failed' else 'failed once', st['why'])
        self.assertEqual(st['first_try']['tests'], ['test_slow_fail'])

    def test_the_rerun_is_never_widened(self):
        self.write_map(fake_map())
        self.assertEqual(self.runner('--areas', 'b', '--rerun-failed', '2'), rs.EXIT_USAGE)

    def test_a_missing_interpreter_fails_the_suite_never_falls_back(self):
        m = fake_map()
        m['suites']['B']['python'] = str(self.tmp / 'no-venv' / 'python.exe')
        self.write_map(m)
        self.assertEqual(self.runner('--areas', 'b'), rs.EXIT_FAILED)
        st = self.by_name()['B']
        self.assertEqual(st['status'], 'failed')
        self.assertIn('interpreter', st['why'])
        self.assertFalse((self.out / 'logs' / 'B.log').exists())

    def test_map_refuses_an_empty_python(self):
        m = fake_map()
        m['suites']['B']['python'] = ' '
        self.write_map(m)
        self.assertEqual(self.runner('--all'), rs.EXIT_USAGE)

    def test_hotfix_tier_keeps_its_substitute(self):
        self.tiered_map()
        self.assertEqual(self.runner('--all', '--tier', 'hotfix'), rs.EXIT_BUDGET, self.lines)   # Abig over-tier
        st = self.by_name()
        self.assertEqual(st['B']['status'], 'passed')
        self.assertEqual(st['Bbig']['status'], 'replaced')

    def test_timeout_by_tier(self):
        self.commit('b/test_b.py', SLOW_TEST)
        m = fake_map()
        m['suites']['B'].update(timeout_s=3, timeout_s_by_tier={'intermediate': 4})
        self.write_map(m)
        self.assertEqual(self.runner('--areas', 'b', '--tier', 'intermediate'), rs.EXIT_FAILED)
        self.assertEqual(self.by_name()['B']['timeout_s'], 4)
        self.assertEqual(self.by_name()['B']['status'], 'timeout')
        # the hotfix tier and no tier keep timeout_s
        self.assertEqual(self.runner('--areas', 'b', '--tier', 'hotfix', out='o2'), rs.EXIT_FAILED)
        self.assertEqual(self.by_name()['B']['timeout_s'], 3)
        self.assertEqual(self.runner('--areas', 'b', out='o3'), rs.EXIT_FAILED)
        self.assertEqual(self.by_name()['B']['timeout_s'], 3)

    def test_map_refuses_a_bad_timeout_by_tier(self):
        for bad in ({'quick': 5}, {'intermediate': 0}, {'intermediate': '900'}, [900]):
            m = fake_map()
            m['suites']['B']['timeout_s_by_tier'] = bad
            self.write_map(m)
            self.lines.clear()
            self.assertEqual(self.runner('--all', out='bad'), rs.EXIT_USAGE, bad)
            self.assertTrue(any('timeout_s_by_tier' in ln for ln in self.lines), (bad, self.lines))

    def test_map_refuses_a_suite_without_tier_or_a_bad_substitute(self):
        m = fake_map()
        del m['suites']['A']['tier']
        self.write_map(m)
        self.assertEqual(self.runner('--all'), rs.EXIT_USAGE)
        self.assertTrue(any('tier' in ln for ln in self.lines))
        m = fake_map()
        m['suites']['N']['hotfix_substitute'] = 'G'      # G is not a hotfix suite
        self.write_map(m)
        self.assertEqual(self.runner('--all', out='o2'), rs.EXIT_USAGE)

    def test_game_files_suite_runs_only_with_the_flag(self):
        m = fake_map()
        m['suites']['GF'] = {'kind': 'pytest', 'needs': 'game-files', 'tier': 'hotfix', 'targets': ['g'],
                             'how': 'the golden copies'}
        m['areas']['g']['suites'] = ['GF']
        self.write_map(m)
        self.assertEqual(self.runner('--areas', 'g'), 0)
        self.assertEqual(self.by_name()['GF']['status'], 'not-run')
        self.assertIn('--game-files', self.by_name()['GF']['why'])
        self.assertEqual(self.runner('--areas', 'g', '--game-files', out='o2'), 0)
        self.assertEqual(self.by_name()['GF']['status'], 'passed')

    def test_pytest_args_are_passed(self):
        self.commit('a/test_bad.py', FAIL_TEST)
        m = fake_map()
        m['suites']['A']['pytest_args'] = ['--ignore=a/test_bad.py']
        self.write_map(m)
        self.assertEqual(self.runner('--areas', 'a'), 0, self.lines)
        self.assertIn('--ignore=a/test_bad.py', self.by_name()['A']['command'])

    def test_usage(self):
        self.assertEqual(self.runner(), rs.EXIT_USAGE)
        self.assertEqual(self.runner('--areas', 'nosuch'), rs.EXIT_USAGE)
        self.assertEqual(self.runner('--diff', 'nosuchcommit..HEAD'), rs.EXIT_USAGE)
        self.assertEqual(self.runner('--diff', 'a...b'), rs.EXIT_USAGE)


# --------------------------------------------------------------------------------------------------- the stage

FAKE_BUILD = '''
import json
from pathlib import Path
from take_release import StageResult
DEPENDS = []
def run(ctx):
    return StageResult.ok(**json.loads(Path(ctx.config['fake_build']).read_text()))
'''

SHIM = f'''
import importlib.util
_s = importlib.util.spec_from_file_location('tier_stage_suites_real', {str(REAL_STAGE)!r})
_m = importlib.util.module_from_spec(_s)
_s.loader.exec_module(_m)
DEPENDS, inputs, run = _m.DEPENDS, _m.inputs, _m.run
'''


class Stage(FakeRepo):
    def setUp(self):
        super().setUp()
        self.tier = self.tmp / 'tierstages'
        self.tier.mkdir()
        (self.tier / 'build.py').write_text(textwrap.dedent(FAKE_BUILD))
        (self.tier / 'suites.py').write_text(SHIM)
        ql = self.tmp / 'fake_quietlock.py'
        ql.write_text(FAKE_QL)
        self.tools = {**tr.default_tools(), 'quietlock': [PY, '-I', str(ql)]}
        self.basework = self.tmp / 'base'
        self.basework.mkdir()
        self.set_base(self.base)
        self.fb = self.tmp / 'fake_build.json'
        self._env = {k: os.environ.get(k) for k in ('C5_PINS_DIR', 'C5_CLONES_DIR', 'C5_SETTINGS_FILE')}
        os.environ.update(C5_PINS_DIR=str(self.tmp / 'pins'), C5_CLONES_DIR=str(self.tmp / 'clones'),
                          C5_SETTINGS_FILE=str(self.tmp / 'no-settings.ini'))

    def tearDown(self):
        for k, v in self._env.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
        super().tearDown()

    def set_base(self, commit):
        outs = {'tools_commit': commit} if commit else {}
        (self.basework / 'state.json').write_text(json.dumps(
            {'format': 1, 'tag': 'Release-9.9.8', 'stages': [{'name': 'build', 'status': 'ok', 'outputs': outs}]}))

    def drive(self, head, dirty=False, **cfg):
        self.fb.write_text(json.dumps({'tools_commit': head, 'tools_dirty': dirty}))
        config = {'tier': 'hotfix', 'who': 'test5014', 'gate_wait': 0, 'tier_stages_dir': str(self.tier),
                  'base_take': str(self.basework), 'fake_build': str(self.fb), 'suites_map': str(self.map),
                  'suites_repo': str(self.repo), 'scratch': str(self.tmp / 'scratch'), **cfg}
        self.work = self.tmp / 'work'
        d = tt.TierDriver('Release-9.9.9', self.work, object(), config, [('build', '6', '5012'), ('suites', '-', '5014')],
                          tools=self.tools, stages_dir=self.tmp / 'nostages', out=self.lines.append)
        return d.run()

    def entry(self):
        st = json.loads((self.work / 'state.json').read_text())
        return next(e for e in st['stages'] if e['name'] == 'suites')

    def test_pass_runs_changed_areas_through_quietlock(self):
        head = self.commit('a/x.txt', 'q')
        self.assertEqual(self.drive(head), 0, self.lines)
        e = self.entry()
        self.assertEqual(e['status'], 'ok')
        o = e['outputs']
        self.assertEqual(o['areas'], ['a'])
        self.assertEqual(o['diff'], f'{self.base}..{head}')
        self.assertEqual(set(o['suites']), {'A', 'safe'})
        self.assertGreater(o['suites']['A']['seconds'], 0)
        self.assertGreater(o['wall_seconds'], 0)
        self.assertEqual(o['base_tools_commit'], self.base)
        log = Path(o['log']).read_text()
        self.assertIn('FAKE-QL run', log)   # heavy: through quietlock.py run
        self.assertIn('run_suites.py', log)

    def test_failing_suite_fails_the_stage(self):
        head = self.commit('b/test_b.py', FAIL_TEST)
        self.assertEqual(self.drive(head), tr.EXIT_FAILED)
        e = self.entry()
        self.assertEqual(e['status'], 'failed')
        self.assertEqual(e['outputs']['suites']['B']['status'], 'failed')
        self.assertTrue(any('suite(s) failed: B' in ln for ln in self.lines), self.lines)

    def test_not_run_listed(self):
        head = self.commit('b/x.txt', 'q')
        self.assertEqual(self.drive(head), 0, self.lines)
        self.assertIn('N', self.entry()['outputs']['not_run'])

    def test_dirty_build_compares_with_working_tree(self):
        head = self.commit('a/x.txt', 'q')
        self.assertEqual(self.drive(head, dirty=True), 0, self.lines)
        self.assertEqual(self.entry()['outputs']['diff'], self.base)

    def test_base_without_tools_commit_stops(self):
        self.set_base(None)
        head = self.commit('a/x.txt', 'q')
        self.assertEqual(self.drive(head), tr.EXIT_STOP)
        e = self.entry()
        self.assertEqual(e['status'], 'stop')
        self.assertIn('no tools_commit', e['stop']['what'])
        self.assertFalse((self.tmp / 'scratch' / 'suites').exists())   # nothing ran

    def test_unknown_base_commit_stops(self):
        self.set_base('0' * 40)
        head = self.commit('a/x.txt', 'q')
        self.assertEqual(self.drive(head), tr.EXIT_STOP)
        self.assertIn('is not in the repository', self.entry()['stop']['what'])

    def test_stage_runs_the_hotfix_tier_with_game_files(self):
        head = self.commit('a/x.txt', 'q')
        self.assertEqual(self.drive(head), 0, self.lines)
        log = Path(self.entry()['outputs']['log']).read_text()
        self.assertIn('--tier hotfix', log)
        self.assertIn('--game-files', log)

    def test_over_tier_stops_naming_the_suite(self):
        m = fake_map()
        m['suites']['Abig'] = {'kind': 'pytest', 'needs': 'none', 'tier': 'intermediate', 'targets': ['a']}
        m['areas']['a']['suites'] = ['Abig']
        self.write_map(m)
        head = self.commit('a/x.txt', 'q')
        self.assertEqual(self.drive(head), tr.EXIT_STOP)
        e = self.entry()
        self.assertIn('higher tier', e['stop']['what'])
        self.assertIn('Abig', e['stop']['what'])
        self.assertEqual(e['outputs']['over_tier'], ['Abig'])

    def test_over_budget_stops_naming_the_suite(self):
        m = fake_map()
        m['suites']['A']['measured_s'] = 2000
        self.write_map(m)
        head = self.commit('a/x.txt', 'q')
        self.assertEqual(self.drive(head), tr.EXIT_STOP)
        e = self.entry()
        self.assertIn('budget', e['stop']['what'])
        self.assertIn('A 2000 s', e['stop']['what'])


# --------------------------------------------------------------------------------------------------- lanes (5022)

SLEEP_TEST = 'import time\n\ndef test_sleep():\n    time.sleep(4)\n'


class Lanes(FakeRepo):
    """Ticket 5022: --lanes N runs suites side by side; --range gives the export gate its range under --all."""

    def sleepy_map(self):
        for d in ('s1', 's2'):
            (self.repo / d).mkdir(parents=True, exist_ok=True)
            (self.repo / d / f'test_{d}.py').write_text(SLEEP_TEST)
        m = fake_map(always=[])
        m['suites'] = {'S1': {'kind': 'pytest', 'needs': 'none', 'tier': 'hotfix', 'targets': ['s1'], 'measured_s': 4},
                       'S2': {'kind': 'pytest', 'needs': 'none', 'tier': 'hotfix', 'targets': ['s2'], 'measured_s': 4}}
        m['areas'] = {'s': {'paths': ['s1/**', 's2/**'], 'suites': ['S1', 'S2']},
                      'rest': {'paths': ['**'], 'suites': [], 'note': 'the rest'}}
        self.write_map(m)

    def overlap(self):
        r = self.results()
        self.assertEqual([s['name'] for s in r['suites']], ['S1', 'S2'])   # the selection's order
        return sum(s['seconds'] for s in r['suites']) - r['seconds']

    def test_two_lanes_overlap(self):
        self.sleepy_map()
        self.assertEqual(self.runner('--all', '--lanes', '2'), 0, self.lines)
        self.assertEqual(self.results()['lanes'], 2)
        self.assertGreater(self.overlap(), 2.0)      # both slept at once: the wall clock is well below the sum

    def test_one_lane_is_serial(self):
        # negative control of the one above
        self.sleepy_map()
        self.assertEqual(self.runner('--all'), 0, self.lines)
        self.assertLess(self.overlap(), 1.0)

    def test_budget_counts_the_longest_lane(self):
        # recorded 7 s each against a 12 s budget: one lane is over by the estimate (14 s), two lanes are not (7 s);
        # the real sleep (4 s) leaves the two-lane run 5 s of slack on a busy machine (lead-0630, 2026-11-22: with a
        # 6 s budget and 4 s recorded the run went over by 0.17 s beside a live game)
        self.sleepy_map()
        m = json.loads(self.map.read_text())
        for s in m['suites'].values():
            s['measured_s'] = 7
        self.write_map(m)
        self.assertEqual(self.runner('--all', '--budget', '12'), rs.EXIT_BUDGET)            # 14 s in one lane
        self.assertEqual(self.runner('--all', '--budget', '12', '--lanes', '2', out='o2'), 0, self.lines)  # 7 s each

    def test_lane_loads(self):
        self.assertEqual(rs.lane_loads([5, 4, 3, 3], 2), [8, 7])
        self.assertEqual(rs.lane_loads([5, 4], 1), [9])

    def test_range_feeds_the_gate_under_all(self):
        m = fake_map()
        m['suites']['gate'] = {'kind': 'exit', 'needs': 'none', 'tier': 'hotfix',
                               'cmd': ['{python}', '-c', 'import sys; sys.exit(sys.argv[1] == sys.argv[2])',
                                       '{diff_base}', '{diff_head}']}
        m['always'] = ['safe', 'gate']
        self.write_map(m)
        head = self.commit('a/x.txt', 'q')
        self.assertEqual(self.runner('--all', '--range', f'{self.base}..{head}'), 0, self.lines)
        g = self.by_name()['gate']
        self.assertEqual(g['status'], 'passed')
        self.assertEqual(g['command'][-2:], [self.base, head])
        self.assertEqual(self.results()['selection']['mode'], 'all')

    def test_range_alone_selects_nothing(self):
        self.assertEqual(self.runner('--range', f'{self.base}..{self.base}'), rs.EXIT_USAGE)

    def test_range_and_diff_refused(self):
        self.assertEqual(self.runner('--diff', self.base, '--range', f'{self.base}..{self.base}'), rs.EXIT_USAGE)


class IntermediateStage(unittest.TestCase):
    """The suites stage's command in the intermediate tier (ticket 5022), read from a faked run_suites."""

    def run_stage(self, build_extra=None, results=None):
        mod = tr.load_stage_module('suites', REAL_STAGE.parent)
        seen = {'said': []}
        build = {'tools_commit': 'b' * 40, 'tools_dirty': False, **(build_extra or {})}

        class C:
            config = {'tier': 'intermediate'}
            who, tag = 'w', 'Release-9.9.9'

            def outputs(self, n):
                return build

            def git(self, *a, **k):
                return 'x'

            def scratch(self, *p):
                return Path(tempfile.mkdtemp())

            def tool(self, n):
                raise KeyError(n)

            def say(self, *a):
                seen['said'].append(' '.join(str(x) for x in a))

            def run(self, cmd, **kw):
                seen['cmd'] = cmd
                seen['env'] = kw.get('env')
                out = Path(cmd[cmd.index('--out') + 1])
                out.mkdir(parents=True, exist_ok=True)
                (out / 'results.json').write_text(json.dumps(results or {'verdict': 'passed', 'suites': [],
                                                                         'selection': {}}))
                return tr.RunResult(0, out / 'log', 1.0, '')

        orig = mod.tierlib.base_entry
        mod.tierlib.base_entry = lambda ctx, n: {'outputs': {'tools_commit': 'a' * 40}}
        orig_tool = mod._common.tool_cmd
        mod._common.tool_cmd = lambda ctx, name, default: list(default)
        try:
            r = mod.run(C())
        finally:
            mod.tierlib.base_entry = orig
            mod._common.tool_cmd = orig_tool
        return r, seen

    def test_command(self):
        r, seen = self.run_stage()
        self.assertEqual(r.status, 'ok', r.what)
        cmd = seen['cmd']
        self.assertIn('--all', cmd)
        self.assertEqual(cmd[cmd.index('--range') + 1], 'a' * 40 + '..' + 'b' * 40)
        self.assertEqual(cmd[cmd.index('--lanes') + 1], '3')
        self.assertEqual(cmd[cmd.index('--tier') + 1], 'intermediate')
        self.assertEqual(cmd[cmd.index('--budget') + 1], '3600')   # MC #10980: 60 min (was 45)
        self.assertEqual(cmd[cmd.index('--rerun-failed') + 1], '1')   # MC #11081: one rerun in the barrier
        self.assertNotIn('--diff', cmd)

    def test_a_flake_is_listed_in_the_take_record(self):
        # MC #11081: a suite that passed on its rerun passes the stage, and the take's record names the failing test
        res = {'verdict': 'passed', 'selection': {},
               'suites': [{'name': 'B', 'status': 'passed', 'seconds': 3, 'why': 'passed on its one rerun; FLAKE',
                           'flake': {'tests': ['T.test_x'], 'output': 'E  AssertionError', 'log': 'B.try1.log'}}],
               'flakes': [{'suite': 'B', 'tests': ['T.test_x'], 'why': '1 failed', 'log': 'B.try1.log'}]}
        r, seen = self.run_stage(results=res)
        self.assertEqual(r.status, 'ok', r.what)
        self.assertEqual(r.outputs['flakes'], [{'suite': 'B', 'tests': ['T.test_x'], 'why': '1 failed',
                                                'log': 'B.try1.log', 'output': 'E  AssertionError'}])
        self.assertTrue(any('FLAKE B' in s and 'T.test_x' in s for s in seen['said']), seen['said'])

    def test_the_take_gives_its_own_x64_lua_to_the_suites(self):
        # ticket 5024: the scenario suite's live-terrain Lua tests skip without C5_LUA51_DLL, and a skip fails
        with tempfile.TemporaryDirectory() as td:
            lua = Path(td) / 'deps' / 'stubs' / 'lua51_x64.dll'
            lua.parent.mkdir(parents=True)
            lua.write_bytes(b'MZ')
            with mock.patch.dict(os.environ, {'C5_LUA51_DLL': 'C:/somewhere/else/lua51_x64.dll'}):
                r, seen = self.run_stage({'x64_out': td})
            self.assertEqual(r.status, 'ok', r.what)
            self.assertEqual(seen['env']['C5_LUA51_DLL'], str(lua))           # the take's own, not the shell's
            self.assertEqual(r.outputs['lua51_dll'], str(lua))

    def test_negative_control_a_build_without_the_lua_dll_sets_nothing(self):
        with tempfile.TemporaryDirectory() as td, mock.patch.dict(os.environ, {}):
            os.environ.pop('C5_LUA51_DLL', None)
            r, seen = self.run_stage({'x64_out': td})                       # no deps/stubs/lua51_x64.dll there
        self.assertNotIn('C5_LUA51_DLL', seen['env'])
        self.assertIsNone(r.outputs['lua51_dll'])


if __name__ == '__main__':
    unittest.main()
