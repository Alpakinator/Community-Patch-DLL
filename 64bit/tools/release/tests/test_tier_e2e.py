"""Hermetic end-to-end take of the hotfix tier through take_tier.py's command line (epic 0630, hand-over item 3,
lead-0630 2026-11-21).

    python -I -X utf8 -m unittest tools/release/tests/test_tier_e2e.py -v

Every stage of the hotfix plan (plans.HOTFIX) is a fake module in a temporary tier-stage folder, except tierreport,
which is the real one (loaded through a shim), so the test walks the whole plan the way a release owner starts it:
take_tier.main() with --tier hotfix, --base-take, --work, --fork, --descriptor, --tools (a fake quietlock.py) and the
test hook --tier-stages-dir. It checks: every stage runs once in the plan's order; the state reaches the hotfix tier;
the report lists every stage that ran with its minutes and every carried result apart, with the take it came from,
never as a run; a resumed take runs nothing again; a stop in tierraise (the automatic raise) ends the take there with
exit 10 and the report says where; a carried stage's line names its hashes. No gate, lock, build, game or instance is
touched; take_release.py's own plans are not used.
"""
import json
import os
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE.parent / 'tiers'))
sys.path.insert(0, str(HERE))
import take_release as tr  # noqa: E402
import take_tier as tt  # noqa: E402
from test_take_release import FAKE_QL, write_desc  # noqa: E402

PY = sys.executable
REAL_REPORT = HERE.parent / 'tiers' / 'stages' / 'tierreport.py'

# One fake for every stage: what it returns is read from E2E_SCRIPT (a JSON file: {stage: {"status": ..., "outputs":
# {...}}}); every call is appended to E2E_CALLS, so the test sees order and count.
FAKE = '''
import json, os
from take_release import StageResult
NAME = {name!r}
DEPENDS = []
def run(ctx):
    with open(os.environ['E2E_CALLS'], 'a', encoding='utf-8') as f:
        f.write(NAME + '\\n')
    spec = json.load(open(os.environ['E2E_SCRIPT'], encoding='utf-8')).get(NAME) or {{}}
    out = spec.get('outputs') or {{'fake': NAME}}
    st = spec.get('status', 'ok')
    if st == 'stop':
        return StageResult.stop(spec['what'], spec.get('unblock', 'fix it'), **out)
    if st == 'failed':
        return StageResult.failed(spec['what'], **out)
    return StageResult.ok(**out)
'''

SHIM = f'''
import importlib.util
_s = importlib.util.spec_from_file_location('tier_report_real_e2e', {str(REAL_REPORT)!r})
_m = importlib.util.module_from_spec(_s)
_s.loader.exec_module(_m)
DEPENDS, run = _m.DEPENDS, _m.run
'''

BASE_LABEL = 'Release-9.9.8 (base work)'


def carried(what):
    import tierlib
    return tierlib.carried(BASE_LABEL, '2026-11-20 20:00:00', what)


class EndToEnd(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = Path(self._td.name)
        self.plan = [s[0] for s in tt.plans.stages('hotfix')]
        self.tier = self.tmp / 'tierstages'
        self.tier.mkdir()
        for n in self.plan:
            body = SHIM if n == 'tierreport' else textwrap.dedent(FAKE.format(name=n))
            (self.tier / f'{n}.py').write_text(body, encoding='utf-8')
        self.nostages = self.tmp / 'nostages'     # the original stage folder: empty, so nothing real is ever loaded
        self.nostages.mkdir()
        ql = self.tmp / 'fake_quietlock.py'
        ql.write_text(FAKE_QL)
        self.tj = self.tmp / 'tools.json'
        self.tj.write_text(json.dumps({'quietlock': [PY, '-I', str(ql)]}))
        self.fork = self.tmp / 'fork'
        self.fork.mkdir()
        subprocess.run(['git', 'init', '-q', str(self.fork)], check=True)
        self.desc = self.tmp / 'desc.json'
        write_desc(self.desc)
        self.base = self.tmp / 'base'
        self.base.mkdir()
        (self.base / 'state.json').write_text(json.dumps(
            {'format': 1, 'tag': 'Release-9.9.8', 'config': {'tier': 'candidate'}, 'reached': {'tier': 'candidate'},
             'stages': [{'name': n, 'status': 'ok', 'outputs': {}} for n in ('survey', 'lock', 'build', 'report')],
             'd162': {'decision': 'D162', 'allowed': True, 'verdict': 'passed'}, 'runs': []}))
        self.work = self.tmp / 'work'
        self.calls = self.tmp / 'calls.txt'
        self.script = self.tmp / 'script.json'
        self._env = {k: os.environ.get(k) for k in ('C5_PINS_DIR', 'C5_CLONES_DIR', 'C5_SETTINGS_FILE', 'E2E_CALLS',
                                                     'E2E_SCRIPT', 'FAKE_QL_HELD')}
        os.environ.update(C5_PINS_DIR=str(self.tmp / 'pins'), C5_CLONES_DIR=str(self.tmp / 'clones'),
                          C5_SETTINGS_FILE=str(self.tmp / 'no-settings.ini'), E2E_CALLS=str(self.calls),
                          E2E_SCRIPT=str(self.script))
        os.environ.pop('FAKE_QL_HELD', None)
        self.lines = []
        self.write_script({})

    def tearDown(self):
        for k, v in self._env.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
        self._td.cleanup()

    def write_script(self, s):
        self.script.write_text(json.dumps(s), encoding='utf-8')

    def hotfix_script(self):
        """A hotfix with a server change and no DLL change: the DLL-only stages carried, savetest fresh with its why,
        wholegames carried (both DLLs equal), the stock route of the journeys carried (D221)."""
        dll = {'x64 DLL': 'A' * 64, 'i0 DLL': 'B' * 64}
        s = {
            'tierraise': {'outputs': {'raised': False, 'compared': ['dll_pin', 'compiler', 'vp_release', 'exe_build']}},
            'build': {'outputs': {'adopted': {'build/x64/deps/dll/CvGameCore_Expansion2.dll': 'A' * 64},
                                  'adopted_unrecorded': {'build/x64/deps/stubs/lua51_x64.lib': 'C' * 64},
                                  'tools_commit': '1' * 40}},
            'savetest': {'outputs': {'fresh_why': 'server.exe changed (sha differs from the base take)',
                                     'tier_subset': {'games': ['tiny', 'standard'], 'why': 'D221: hotfix'}}},
            'hi_journey': {'outputs': {'carried_parts': {'stock': carried({'stock EXE': 'D' * 64})}}},
        }
        for n in ('narrowing', 'census', 'model', 'purity', 'wholegames'):
            s[n] = {'outputs': {'carried': carried(dll)}}
        return s

    def main(self, *extra, base=True):
        argv = ['Release-9.9.9', '--tier', 'hotfix', '--work', str(self.work), '--fork', str(self.fork),
                '--descriptor', str(self.desc), '--tools', str(self.tj), '--stages-dir', str(self.nostages),
                '--tier-stages-dir', str(self.tier), '--who', 'teste2e', '--gate-wait', '0']
        if base:
            argv += ['--base-take', str(self.base)]
        return tt.main(argv + list(extra), out=self.lines.append)

    def called(self):
        return self.calls.read_text(encoding='utf-8').split() if self.calls.exists() else []

    def state(self):
        return json.loads((self.work / 'state.json').read_text(encoding='utf-8'))

    def report(self):
        return json.loads((self.work / 'tier-report.json').read_text(encoding='utf-8'))

    # ------------------------------------------------------------------------------------------------------------

    def test_whole_hotfix_plan_runs_once_in_order_and_reaches_the_tier(self):
        self.write_script(self.hotfix_script())
        self.assertEqual(self.main(), tr.EXIT_OK, self.lines[-6:])
        # every stage but the real report is a fake that logs its call: each once, in the plan's order
        self.assertEqual(self.called(), [n for n in self.plan if n != 'tierreport'])
        st = self.state()
        self.assertEqual([e['name'] for e in st['stages']], self.plan)
        self.assertTrue(all(e['status'] == 'ok' for e in st['stages']), [(e['name'], e['status']) for e in st['stages']])
        self.assertEqual(st['reached']['tier'], 'hotfix')
        self.assertEqual(st['config']['tier'], 'hotfix')
        self.assertEqual(Path(st['config']['base_take']), self.base.resolve())

    def test_report_lists_runs_and_carried_results_apart(self):
        self.write_script(self.hotfix_script())
        self.assertEqual(self.main(), tr.EXIT_OK, self.lines[-6:])
        r = self.report()
        self.assertEqual(r['tier'], 'hotfix')
        self.assertEqual(r['verdict'], 'hotfix tier reached')
        carried_stages = {'narrowing', 'census', 'model', 'purity', 'wholegames'}
        self.assertEqual({c['stage'] for c in r['carried']}, carried_stages | {'hi_journey/stock'})
        for c in r['carried']:
            self.assertEqual(c['from_take'], BASE_LABEL)
            self.assertIn('equal', c['line'])
        # a carried stage is never a run: not in the run count, not in the "run in this take" table
        ran = [s['stage'] for s in r['stages'] if s['kind'] != 'carried']
        self.assertEqual(r['run_count'], len(ran))
        self.assertFalse(carried_stages & set(ran))
        self.assertEqual(len(r['stages']), len(self.plan) - 1)          # every stage but the report itself
        for s in r['stages']:
            self.assertIsNotNone(s['minutes'], s['stage'])               # measured from the state's clock (rule 4)
        self.assertEqual(r['fresh_although_carriable'][0]['stage'], 'savetest')
        self.assertEqual(r['reduced'][0]['stage'], 'savetest')
        md = (self.work / 'tier-report.md').read_text(encoding='utf-8')
        table_run = md.split('## Carried results')[0]
        for n in carried_stages:
            self.assertNotIn(f'| {n} |', table_run)
            self.assertIn(f'| {n} | {BASE_LABEL} |', md)

    def test_resumed_take_runs_nothing_again(self):
        self.write_script(self.hotfix_script())
        self.assertEqual(self.main(), tr.EXIT_OK, self.lines[-6:])
        n = len(self.called())
        self.lines.clear()
        self.assertEqual(self.main(base=False), tr.EXIT_OK, self.lines[-6:])   # the base take is read from the state
        self.assertEqual(len(self.called()), n)
        self.assertEqual(len(self.state()['runs']), 2)

    def test_raise_stops_the_take_before_build(self):
        s = self.hotfix_script()
        s['tierraise'] = {'status': 'stop', 'what': 'raised to the intermediate tier: dll_pin x64 1b87dd18 -> 2c98ee29',
                          'unblock': 'take the release in the intermediate tier'}
        self.write_script(s)
        self.assertEqual(self.main(), tr.EXIT_STOP)
        self.assertEqual(self.called(), self.plan[:self.plan.index('tierraise') + 1])
        self.assertTrue(any('raised to the intermediate tier' in ln for ln in self.lines), self.lines[-4:])
        self.assertNotEqual((self.state().get('reached') or {}).get('tier'), 'hotfix')
        # the report command for a take that stopped before the report stage says where it stopped
        r = subprocess.run([PY, '-I', str(REAL_REPORT), str(self.work)], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn('NOT reached: tierraise is stop', r.stdout)

    def test_failed_stage_fails_the_take_and_is_named(self):
        s = self.hotfix_script()
        s['savetest'] = {'status': 'failed', 'what': 'Standard game: desync at turn 3'}
        self.write_script(s)
        self.assertEqual(self.main(), tr.EXIT_FAILED)
        self.assertEqual(self.called()[-1], 'savetest')
        self.assertNotIn('wholegames', self.called())

    # the base must be an accepted take (MC #9116, D118): reached a tier, report ok, D162 yes ----------------------

    def set_base(self, **change):
        st = json.loads((self.base / 'state.json').read_text(encoding='utf-8'))
        for k, v in change.items():
            if v is None:
                st.pop(k, None)
            else:
                st[k] = v
        (self.base / 'state.json').write_text(json.dumps(st))

    def test_unaccepted_bases_are_refused_loudly_and_nothing_runs(self):
        self.write_script(self.hotfix_script())
        report_pending = [{'name': n, 'status': 'ok', 'outputs': {}} for n in ('survey', 'lock', 'build')] + [
            {'name': 'report', 'status': 'pending', 'outputs': {}}]
        cases = {
            'D162 line is NO': {'d162': {'allowed': False, 'why': ['release-basic was not played']}},
            'no D162 line': {'d162': None},
            'report stage is pending': {'stages': report_pending},
            'reached no tier': {'reached': None},
        }
        for words, change in cases.items():
            with self.subTest(words):
                self.setUp_base_fresh()
                self.set_base(**change)
                self.lines.clear()
                self.assertEqual(self.main(), tr.EXIT_ARGS, self.lines[-3:])
                self.assertTrue(any('not an accepted release take' in ln and words in ln for ln in self.lines),
                                self.lines[-3:])
                self.assertEqual(self.called(), [])
                self.assertFalse((self.work / 'state.json').exists())

    def setUp_base_fresh(self):
        self.set_base(reached={'tier': 'candidate'}, d162={'decision': 'D162', 'allowed': True, 'verdict': 'passed'},
                      stages=[{'name': n, 'status': 'ok', 'outputs': {}} for n in ('survey', 'lock', 'build', 'report')])

    def test_base_unaccepted_takes_a_dry_run_says_so_and_records_it(self):
        self.write_script(self.hotfix_script())
        self.set_base(d162={'allowed': False, 'why': ['release-basic was not played']})
        self.assertEqual(self.main('--base-unaccepted'), tr.EXIT_OK, self.lines[-6:])
        self.assertTrue(any('LOUD' in ln and 'NOT accepted' in ln for ln in self.lines))
        why = self.state()['config']['base_unaccepted']
        self.assertTrue(any('D162 line is NO' in w for w in why), why)
        # a resume needs no second flag: the state carries the operator's choice
        self.lines.clear()
        self.assertEqual(self.main(), tr.EXIT_OK, self.lines[-6:])

    def test_a_tier_take_is_accepted_by_its_tierreport(self):
        # the second beta's hotfix take (cand2/tier3, 2026-11-22): config.tier, tierreport ok, D162 yes, no `report`
        tier_stages = [{'name': n, 'status': 'ok', 'outputs': {}} for n in ('survey', 'lock', 'build', 'tierreport')]
        self.write_script(self.hotfix_script())
        self.set_base(reached={'tier': 'hotfix'}, stages=tier_stages, config={'tier': 'hotfix'})
        self.assertEqual(tt.base_unaccepted(self.base), [])
        self.assertEqual(self.main(), tr.EXIT_OK, self.lines[-6:])
        self.assertNotIn('base_unaccepted', self.state()['config'])
        # negative: a tierreport that did not end ok; a tierreport in a take that is not a tier take
        self.set_base(stages=tier_stages[:-1] + [{'name': 'tierreport', 'status': 'failed', 'outputs': {}}])
        self.assertTrue(any('report stage is failed' in w for w in tt.base_unaccepted(self.base)))
        self.set_base(stages=tier_stages, config={})
        self.assertTrue(any('report stage is missing' in w for w in tt.base_unaccepted(self.base)))

    def test_accepted_base_records_no_unaccepted_reasons(self):
        self.write_script(self.hotfix_script())
        self.assertEqual(self.main(), tr.EXIT_OK, self.lines[-6:])
        self.assertNotIn('base_unaccepted', self.state()['config'])

    def test_require_adoption_is_recorded_and_kept_on_resume(self):
        self.write_script(self.hotfix_script())
        self.assertEqual(self.main('--require-adoption'), tr.EXIT_OK, self.lines[-6:])
        self.assertTrue(self.state()['config']['require_adoption'])
        self.assertEqual(self.main(), tr.EXIT_OK, self.lines[-6:])
        self.assertTrue(self.state()['config']['require_adoption'])

    def test_take_release_default_plans_untouched(self):
        # rule 1 of the epic: the tier is an addition beside take_release's own plans
        self.assertEqual(tr.TIERS, ('candidate', 'validated'))
        self.assertNotIn('tierraise', [s[0] for s in tr.CANDIDATE])
        self.assertNotIn('tierreport', [s[0] for s in tr.CANDIDATE])


if __name__ == '__main__':
    unittest.main()
