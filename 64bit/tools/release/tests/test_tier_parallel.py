"""Hermetic tests of a tier's stages side by side (epic 0630, ticket 5017): tools/release/tiers/parallel.py through
take_tier.py's command line with --places.

    PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q tools/release/tests/test_tier_parallel.py

Every stage of the hotfix plan is a fake module in a temporary tier-stage folder (tierreport is the real one, through a
shim), as in test_tier_e2e.py. Each fake reads what to do from a JSON script (sleep, set or read an environment
variable, record a part, wait for a file, fail, stop, refuse, crash) and writes one event file per run (pid, start, end,
what it saw), so the tests see overlap in time, processes and order. No gate, lock, build, game or instance is touched.
"""
import json
import os
import subprocess
import sys
import tempfile
import textwrap
import time
import unittest
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE.parent / 'tiers'))
sys.path.insert(0, str(HERE))
import take_release as tr  # noqa: E402
import take_tier as tt  # noqa: E402
from test_take_release import FAKE_QL, write_desc  # noqa: E402

PY = sys.executable
TAKE_TIER = HERE.parent / 'tiers' / 'take_tier.py'
REAL_REPORT = HERE.parent / 'tiers' / 'stages' / 'tierreport.py'

FAKE = '''
import json, os, time
from pathlib import Path
from take_release import StageResult, GateRefused
NAME = {name!r}
DEPENDS = {depends!r}
USES = {uses!r}
def run(ctx):
    spec = json.load(open(os.environ['P5017_SCRIPT'], encoding='utf-8')).get(NAME) or {{}}
    ev = {{'stage': NAME, 'pid': os.getpid(), 'start': time.time(), 'previous': ctx.previous_outputs()}}
    try:
        for k, v in (spec.get('env_set') or {{}}).items():
            os.environ[k] = v
        if spec.get('partial'):
            ctx.partial(**spec['partial'])
        if spec.get('wait_file'):
            end = time.time() + 60
            while not os.path.exists(spec['wait_file']) and time.time() < end:
                time.sleep(0.05)
        time.sleep(spec.get('sleep', 0))
        ev['env'] = {{k: os.environ.get(k) for k in spec.get('env_read') or []}}
        if spec.get('make_stop'):
            Path(ctx.work, 'STOP').write_text('stop')
        if spec.get('check_stop'):
            ctx.check_stop()
    finally:
        ev['end'] = time.time()
        Path(os.environ['P5017_CALLS'], f'{{NAME}}-{{os.getpid()}}-{{time.time_ns()}}.json').write_text(json.dumps(ev))
    if spec.get('crash'):
        os._exit(5)
    st = spec.get('status', 'ok')
    if st == 'refused':
        raise GateRefused('fake: refused, command not started; lock held by 7003 timing')
    if st == 'stop':
        return StageResult.stop(spec['what'], 'fix it')
    if st == 'failed':
        return StageResult.failed(spec['what'])
    return StageResult.ok(**(spec.get('outputs') or {{'fake': NAME}}))
'''

SHIM = f'''
import importlib.util
_s = importlib.util.spec_from_file_location('tier_report_real_5017', {str(REAL_REPORT)!r})
_m = importlib.util.module_from_spec(_s)
_s.loader.exec_module(_m)
DEPENDS, run = _m.DEPENDS, _m.run
'''

ENV_KEYS = ('C5_PINS_DIR', 'C5_CLONES_DIR', 'C5_SETTINGS_FILE', 'P5017_CALLS', 'P5017_SCRIPT', 'FAKE_QL_HELD',
            'T5017_LEAK')
PLACES = 'headless=1,game=1,gpu=1'


class Parallel(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = Path(self._td.name)
        self.plan = [s[0] for s in tt.plans.stages('hotfix')]
        self.tier = self.tmp / 'tierstages'
        self.tier.mkdir()
        self.fakes()
        self.nostages = self.tmp / 'nostages'
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
        self.calls = self.tmp / 'calls'
        self.calls.mkdir()
        self.script = self.tmp / 'script.json'
        self._env = {k: os.environ.get(k) for k in ENV_KEYS}
        os.environ.update(C5_PINS_DIR=str(self.tmp / 'pins'), C5_CLONES_DIR=str(self.tmp / 'clones'),
                          C5_SETTINGS_FILE=str(self.tmp / 'no-settings.ini'), P5017_CALLS=str(self.calls),
                          P5017_SCRIPT=str(self.script))
        os.environ.pop('FAKE_QL_HELD', None)
        os.environ.pop('T5017_LEAK', None)
        self.lines = []
        self.write_script({})
        self.procs = []

    def tearDown(self):
        for p in self.procs:
            if p.poll() is None:
                p.kill()
                p.wait()
        for k, v in self._env.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
        self.wait_children_gone()
        self._td.cleanup()

    def wait_children_gone(self):
        """Children of a killed parent may still run: wait (bounded) before the folder is removed."""
        import psutil
        pdir = self.work / 'parallel'
        end = time.time() + 30
        for p in (pdir.glob('*.pid') if pdir.is_dir() else []):
            try:
                pid = json.loads(p.read_text())['pid']
                while psutil.pid_exists(pid) and time.time() < end:
                    time.sleep(0.1)
            except (OSError, ValueError, KeyError):
                pass

    def fakes(self, depends=None, uses=None):
        depends, uses = depends or {}, uses or {}
        for n in self.plan:
            body = SHIM if n == 'tierreport' else textwrap.dedent(
                FAKE.format(name=n, depends=depends.get(n, []), uses=uses.get(n, [])))
            (self.tier / f'{n}.py').write_text(body, encoding='utf-8')

    def write_script(self, s):
        self.script.write_text(json.dumps(s), encoding='utf-8')

    def argv(self, *extra, base=True):
        a = ['Release-9.9.9', '--tier', 'hotfix', '--work', str(self.work), '--fork', str(self.fork),
             '--descriptor', str(self.desc), '--tools', str(self.tj), '--stages-dir', str(self.nostages),
             '--tier-stages-dir', str(self.tier), '--who', 'test5017', '--gate-wait', '0']
        if base:
            a += ['--base-take', str(self.base)]
        return a + list(extra)

    def main(self, *extra, base=True):
        return tt.main(self.argv(*extra, base=base), out=self.lines.append)

    def events(self):
        ev = [json.loads(p.read_text()) for p in self.calls.glob('*.json')]
        return sorted(ev, key=lambda e: e['start'])

    def ev(self, stage):
        got = [e for e in self.events() if e['stage'] == stage]
        self.assertEqual(len(got), 1, f'{stage}: {len(got)} runs')
        return got[0]

    def state(self):
        return json.loads((self.work / 'state.json').read_text(encoding='utf-8'))

    def entry(self, stage):
        return next(e for e in self.state()['stages'] if e['name'] == stage)

    @staticmethod
    def overlap(a, b):
        return a['start'] < b['end'] and b['start'] < a['end']

    def tail(self):
        return '\n'.join(self.lines[-12:])

    # --------------------------------------------------------------------------------------------- side by side

    def test_two_independent_stages_overlap_in_their_own_processes(self):
        # 4 s sleeps: under load a stage worker can start more than a second after its sibling (ticket 5024, 2026-11-23)
        self.write_script({'narrowing': {'sleep': 4}, 'model': {'sleep': 4}})
        self.assertEqual(self.main('--places', PLACES), tr.EXIT_OK, self.tail())
        a, b = self.ev('narrowing'), self.ev('model')
        self.assertTrue(self.overlap(a, b), (a, b))
        self.assertNotEqual(a['pid'], b['pid'])
        self.assertNotIn(os.getpid(), (a['pid'], b['pid']))
        # the barriers run alone in the driver's own process, in the plan's order
        for n in ('survey', 'branches', 'testdata', 'lock', 'tierraise'):
            self.assertEqual(self.ev(n)['pid'], os.getpid(), n)
        bar = [e['stage'] for e in self.events() if e['stage'] in ('survey', 'branches', 'testdata', 'lock', 'tierraise')]
        self.assertEqual(bar, ['survey', 'branches', 'testdata', 'lock', 'tierraise'])
        tierraise_end = self.ev('tierraise')['end']
        self.assertTrue(all(e['start'] >= tierraise_end for e in self.events() if e['pid'] != os.getpid()))
        st = self.state()
        self.assertEqual(st['reached']['tier'], 'hotfix')
        self.assertEqual(st['config']['places'], {'headless': 1, 'game': 1, 'gpu': 1, 'cpu_build': 1})
        self.assertEqual(self.entry('hi_journey')['held'], {'game': 1, 'gpu': 1})
        self.assertEqual(self.entry('survey')['held'], 'alone')
        self.assertTrue((self.work / 'logs' / 'narrowing-worker.log').is_file())
        self.assertTrue(any(ln.startswith('[narrowing] ') for ln in self.lines))
        self.assertEqual(list((self.work / 'parallel').glob('*')), [])       # every child's files merged and removed
        # tierreport: starts, ends, overlaps, places, the wall clock measured (not summed)
        r = json.loads((self.work / 'tier-report.json').read_text(encoding='utf-8'))
        p = r['side_by_side']
        self.assertIn(['narrowing', 'model'], [o['stages'] for o in p['overlaps']])
        self.assertEqual(p['wall_minutes'], r['wall_minutes_first_start_to_last_end'])
        self.assertEqual({s['stage']: s['held'] for s in p['stages']}['savetest'], {'headless': 1})
        md = (self.work / 'tier-report.md').read_text(encoding='utf-8')
        self.assertIn('## Stages side by side', md)
        self.assertIn('narrowing and model overlapped', md)

    def test_negative_control_without_places_nothing_overlaps_and_all_run_here(self):
        self.write_script({'narrowing': {'sleep': 1}, 'model': {'sleep': 1}})
        self.assertEqual(self.main(), tr.EXIT_OK, self.tail())
        ev = self.events()
        self.assertFalse(any(self.overlap(a, b) for i, a in enumerate(ev) for b in ev[i + 1:]))
        self.assertEqual({e['pid'] for e in ev}, {os.getpid()})
        self.assertIsNone(json.loads((self.work / 'tier-report.json').read_text())['side_by_side'])

    def test_a_hold_that_does_not_fit_waits(self):
        # cpu_build=1: build, census and purity hold it; headless=1: offline and savetest ('all') hold it
        # 4 s sleeps: under load a stage worker can start more than a second after its sibling (ticket 5024, 2026-11-23)
        self.write_script({**{n: {'sleep': 1} for n in ('census', 'purity', 'offline', 'savetest')},
                           'build': {'sleep': 4}, 'narrowing': {'sleep': 4}})
        self.assertEqual(self.main('--places', PLACES), tr.EXIT_OK, self.tail())
        for a, b in (('build', 'census'), ('build', 'purity'), ('census', 'purity'), ('offline', 'savetest')):
            self.assertFalse(self.overlap(self.ev(a), self.ev(b)), (a, b))
        # a stage that holds nothing still runs beside them
        self.assertTrue(self.overlap(self.ev('narrowing'), self.ev('build')))
        # the plan's order breaks the tie
        self.assertLess(self.ev('build')['start'], self.ev('census')['start'])

    def _flaky_reads(self, fails=2):
        """Path.read_text that refuses the first `fails` reads of each state.json and each stage result
        (parallel/<stage>.json) with a sharing violation, as Windows does while another process replaces the file."""
        real, seen = Path.read_text, {}

        def read_text(path, *a, **k):
            mine = str(path).startswith(str(self.work))      # the take's own files, not the base take's
            ours = path.name == tr.STATE_NAME or path.parent.name == tt.parallel.PAR_DIR and path.suffix == '.json'
            if mine and ours:
                seen[str(path)] = seen.get(str(path), 0) + 1
                if seen[str(path)] <= fails:
                    raise PermissionError(13, 'Permission denied', str(path))
            return real(path, *a, **k)
        return mock.patch.object(Path, 'read_text', read_text), seen

    def test_a_sharing_violation_on_a_result_or_the_state_is_waited_out(self):
        # 5024 (2026-11-23): release-intermediate's solo run and 2 of 12 copies under load failed a take with "its
        # stage worker exited 1 without a result" (a worker's read of state.json, Errno 13). The parent's reads of
        # the results, the driver's read of the recorded config and take_tier's own read retry like State's.
        self.write_script({'narrowing': {'sleep': 1}, 'model': {'sleep': 1}})
        patch, seen = self._flaky_reads()
        with patch:
            self.assertEqual(self.main('--places', PLACES), tr.EXIT_OK, self.tail())
            self.assertTrue(any(k.endswith(('narrowing.json', 'model.json')) for k in seen), seen)
            # a second run over the same work folder reads the recorded config (take_tier and build_config)
            self.assertEqual(self.main('--places', PLACES, '--only', 'narrowing'), tr.EXIT_OK, self.tail())
        self.assertEqual(self.entry('narrowing')['status'], 'ok')

    def test_negative_control_without_the_retry_a_sharing_violation_fails_the_take(self):
        self.write_script({'narrowing': {'sleep': 1}, 'model': {'sleep': 1}})
        patch, _ = self._flaky_reads()
        with patch, mock.patch.object(tr, '_retry_sharing', lambda fn, **k: fn()):
            try:
                rc = self.main('--places', PLACES)
            except PermissionError:
                rc = 'raised'
        self.assertNotEqual(rc, tr.EXIT_OK, self.tail())

    def test_negative_control_holds_that_fit_overlap(self):
        # 4 s sleeps: under load a stage worker can start more than a second after its sibling (ticket 5024, 2026-11-23)
        self.write_script({n: {'sleep': 4} for n in ('build', 'census', 'purity')})
        self.assertEqual(self.main('--places', PLACES + ',cpu_build=3'), tr.EXIT_OK, self.tail())
        self.assertTrue(self.overlap(self.ev('build'), self.ev('census')))
        self.assertTrue(self.overlap(self.ev('census'), self.ev('purity')))

    def test_a_dependency_waits(self):
        self.fakes(depends={'narrowing': ['build']}, uses={'wholegames': ['savetest']})
        self.write_script({'build': {'sleep': 1.5}, 'savetest': {'sleep': 1.5}})
        self.assertEqual(self.main('--places', PLACES), tr.EXIT_OK, self.tail())
        self.assertGreaterEqual(self.ev('narrowing')['start'], self.ev('build')['end'])
        self.assertGreaterEqual(self.ev('wholegames')['start'], self.ev('savetest')['end'])   # USES: its inputs
        self.assertIn('savetest', self.entry('wholegames')['inputs']['deps_sha'])

    def test_negative_control_without_the_dependency_it_overlaps(self):
        # 4 s sleeps: under load a stage worker can start more than a second after its sibling (ticket 5024, 2026-11-23)
        self.write_script({'build': {'sleep': 4}, 'narrowing': {'sleep': 4}})
        self.assertEqual(self.main('--places', PLACES), tr.EXIT_OK, self.tail())
        self.assertTrue(self.overlap(self.ev('narrowing'), self.ev('build')))

    def test_env_set_by_one_stage_is_not_seen_by_the_other(self):
        # 4 s sleeps: under load a stage worker can start more than a second after its sibling (ticket 5024, 2026-11-23)
        self.write_script({'narrowing': {'env_set': {'T5017_LEAK': 'narrowing'}, 'sleep': 4},
                           'model': {'sleep': 2, 'env_read': ['T5017_LEAK']}})
        self.assertEqual(self.main('--places', PLACES), tr.EXIT_OK, self.tail())
        self.assertTrue(self.overlap(self.ev('narrowing'), self.ev('model')))
        self.assertEqual(self.ev('model')['env'], {'T5017_LEAK': None})
        self.assertIsNone(os.environ.get('T5017_LEAK'))

    def test_negative_control_in_one_process_the_env_leaks(self):
        self.write_script({'narrowing': {'env_set': {'T5017_LEAK': 'narrowing'}},
                           'model': {'env_read': ['T5017_LEAK']}})
        self.assertEqual(self.main(), tr.EXIT_OK, self.tail())
        self.assertEqual(self.ev('model')['env'], {'T5017_LEAK': 'narrowing'})

    # --------------------------------------------------------------------------------------------- the ends

    def test_a_failure_lets_the_running_end_and_starts_nothing_new(self):
        self.write_script({'build': {'sleep': 2.5}, 'narrowing': {'sleep': 0.5, 'status': 'failed', 'what': 'boom'}})
        self.assertEqual(self.main('--places', PLACES), tr.EXIT_FAILED, self.tail())
        self.assertEqual(self.entry('build')['status'], 'ok')                 # it ran to its end
        self.assertEqual(self.entry('narrowing')['status'], 'failed')
        # nothing is LAUNCHED after the driver saw the failure: read from the driver's own ordered lines (5024: the
        # children's start stamps lag their launch by seconds on a loaded machine, so they cannot say this)
        ended = next(i for i, ln in enumerate(self.lines) if 'take_release: parallel: narrowing ended' in ln)
        launched_after = [ln for ln in self.lines[ended:] if ' started in its own process' in ln]
        self.assertEqual(launched_after, [])                                   # census, purity waited for cpu_build
        self.assertNotIn('census', [e['stage'] for e in self.events()])
        self.assertNotIn('tierreport', [e['stage'] for e in self.events()])
        self.assertEqual(self.lines[-1] if not self.lines[-1].startswith('take_release: D162') else self.lines[-2],
                         'take_release: FAILED narrowing: boom')
        self.assertNotIn('reached', self.state())

    def test_the_first_bad_stage_of_the_plan_decides_the_exit(self):
        self.write_script({'narrowing': {'sleep': 1.5, 'status': 'stop', 'what': 'new sites'},
                           'model': {'sleep': 0.2, 'status': 'failed', 'what': 'regen differs'}})
        self.assertEqual(self.main('--places', PLACES), tr.EXIT_STOP, self.tail())
        self.assertTrue(self.lines[-1].startswith('take_release: STOP narrowing'), self.tail())
        self.assertEqual(self.entry('model')['status'], 'failed')

    def test_a_stop_file_ends_the_take_with_the_running_finished(self):
        # narrowing makes the stop file 4 s into its run, while build, model and suites run (each child checks the
        # stop file when it starts: one that started after it would end pending at once)
        # 4 s sleeps: under load a stage worker can start more than a second after its sibling (ticket 5024, 2026-11-23)
        self.write_script({'build': {'sleep': 6}, 'narrowing': {'sleep': 4, 'make_stop': True},
                           'model': {'sleep': 6}, 'suites': {'sleep': 6, 'check_stop': True}})
        self.assertEqual(self.main('--places', PLACES), tr.EXIT_STOP, self.tail())
        self.assertEqual(self.entry('build')['status'], 'ok')
        self.assertEqual(self.entry('model')['status'], 'ok')
        self.assertEqual(self.entry('suites')['status'], 'pending')        # it saw the stop file itself
        stop_at = self.ev('narrowing')['end']
        self.assertEqual([e['stage'] for e in self.events() if e['start'] > stop_at + 0.5], [])
        self.assertTrue(any('stop file' in ln and ln.startswith('take_release: STOP') for ln in self.lines))
        # removing the stop file and running again finishes the take; the finished stages are not run again
        (self.work / 'STOP').unlink()
        n_build = len([e for e in self.events() if e['stage'] == 'build'])
        self.write_script({})
        self.assertEqual(self.main(), tr.EXIT_OK, self.tail())
        self.assertEqual(len([e for e in self.events() if e['stage'] == 'build']), n_build)
        self.assertEqual(self.state()['reached']['tier'], 'hotfix')

    def test_a_gate_refusal_passes_exit_3_through(self):
        self.write_script({'offline': {'status': 'refused'}, 'build': {'sleep': 1}})
        self.assertEqual(self.main('--places', PLACES), tr.EXIT_REFUSED, self.tail())
        e = self.entry('offline')
        self.assertEqual(e['status'], 'pending')
        self.assertTrue(e['error'].startswith('gate refusal: '))
        self.assertTrue(self.lines[-1].startswith('take_release: REFUSED offline'), self.tail())

    def test_a_crashed_child_fails_its_stage_and_its_parts_survive(self):
        self.write_script({'model': {'partial': {'part1': 'done'}, 'crash': True}})
        self.assertEqual(self.main('--places', PLACES), tr.EXIT_FAILED, self.tail())
        e = self.entry('model')
        self.assertEqual(e['status'], 'failed')
        self.assertIn('without a result', e['error'])
        self.assertEqual(e['outputs'].get('part1'), 'done')
        self.write_script({})
        self.assertEqual(self.main(), tr.EXIT_OK, self.tail())
        runs = [x for x in self.events() if x['stage'] == 'model']
        self.assertEqual(runs[-1]['previous'], {'part1': 'done'})          # the resume reuses the part

    def test_resume_after_the_driver_was_killed_mid_parallel(self):
        go_model = self.tmp / 'go-model'
        self.write_script({'narrowing': {'partial': {'part1': 'done'}, 'wait_file': str(self.tmp / 'never')},
                           'model': {'wait_file': str(go_model)}})
        p = subprocess.Popen([PY, '-I', str(TAKE_TIER)] + self.argv('--places', PLACES), stdout=subprocess.DEVNULL,
                             stderr=subprocess.STDOUT)
        self.procs.append(p)
        pdir = self.work / 'parallel'

        def wait_for(cond, what, t=60):
            end = time.time() + t
            while not cond():
                self.assertLess(time.time(), end, f'timed out waiting for {what}')
                time.sleep(0.1)

        def part_recorded():
            try:
                ents = json.loads((pdir / 'narrowing.state.json').read_text()).get('stages')
            except (OSError, ValueError):
                return False
            return any(x['name'] == 'narrowing' and (x.get('outputs') or {}).get('part1') for x in ents)
        wait_for(part_recorded, 'narrowing to record its part')
        wait_for(lambda: (pdir / 'model.pid').is_file(), 'model to start')
        p.kill()                                    # the driver dies; its children do not
        p.wait()
        nar_pid = json.loads((pdir / 'narrowing.pid').read_text())['pid']
        subprocess.run(['taskkill', '/F', '/PID', str(nar_pid)], capture_output=True)   # this one dies too
        import psutil
        wait_for(lambda: not psutil.pid_exists(nar_pid), 'narrowing to end')
        # a resume while a child of the dead driver still runs is refused (exit 2), nothing started
        self.lines.clear()
        self.assertEqual(self.main(base=False), tr.EXIT_ARGS, self.tail())
        self.assertTrue(any('model' in ln and 'still runs' in ln for ln in self.lines), self.tail())
        go_model.write_text('go')
        wait_for(lambda: (pdir / 'model.json').is_file(), 'model to finish without a driver')
        mpid = json.loads((pdir / 'model.pid').read_text())['pid']
        wait_for(lambda: not psutil.pid_exists(mpid), 'model process to exit')
        self.write_script({})
        self.lines.clear()
        self.assertEqual(self.main(base=False), tr.EXIT_OK, self.tail())     # places read from the state
        self.assertTrue(any('model ended (ok) while no driver ran' in ln for ln in self.lines))
        self.assertEqual(len([e for e in self.events() if e['stage'] == 'model']), 1)   # merged, not run again
        nar = [e for e in self.events() if e['stage'] == 'narrowing']
        self.assertEqual(nar[-1]['previous'], {'part1': 'done'})
        self.assertEqual(self.state()['reached']['tier'], 'hotfix')
        self.assertEqual(len(self.state()['runs']), 3)

    # --------------------------------------------------------------------------------------------- unchanged serial

    def test_without_places_the_serial_driver_runs_unchanged(self):
        self.write_script({})
        with mock.patch.object(tt.ParallelTierDriver, 'run', side_effect=AssertionError('parallel driver used')):
            self.assertEqual(self.main(), tr.EXIT_OK, self.tail())
        self.assertEqual([e['stage'] for e in self.events()], [n for n in self.plan if n != 'tierreport'])
        st = self.state()
        self.assertNotIn('places', st['config'])
        self.assertFalse((self.work / 'parallel').exists())
        self.assertFalse(any('parallel' in ln or ln.startswith('[') for ln in self.lines))

    def test_with_places_a_chain_runs_in_the_serial_order_with_the_same_results(self):
        chain = {n: [self.plan[i - 1]] for i, n in enumerate(self.plan) if 0 < i and n != 'tierreport'}
        self.fakes(depends=chain)
        self.write_script({})
        self.assertEqual(self.main(), tr.EXIT_OK, self.tail())
        serial = self.state()
        serial_order = [e['stage'] for e in self.events()]
        for p in self.calls.glob('*.json'):
            p.unlink()
        self.work = self.tmp / 'work2'
        self.assertEqual(self.main('--places', PLACES), tr.EXIT_OK, self.tail())
        par = self.state()
        self.assertEqual([e['stage'] for e in self.events()], serial_order)

        def strip(st):
            return [(e['name'], e['status'], {k: v for k, v in e['outputs'].items() if k not in ('report',)},
                     e['inputs_sha'] if e['name'] != 'tierreport' else None) for e in st['stages']]
        self.assertEqual(strip(par), strip(serial))

    def test_places_recorded_reused_and_taken_back(self):
        self.write_script({})
        self.assertEqual(self.main('--places', PLACES), tr.EXIT_OK, self.tail())
        self.assertIn('places', self.state()['config'])
        self.assertEqual(self.main('--only', 'model'), tr.EXIT_OK, self.tail())      # resume: places from the state
        self.assertNotEqual(self.events()[-1]['pid'], os.getpid())
        self.assertEqual(self.main('--places', 'off', '--only', 'model'), tr.EXIT_OK, self.tail())
        self.assertEqual(self.events()[-1]['pid'], os.getpid())
        self.assertNotIn('places', self.state()['config'])

    def test_from_keeps_its_meaning(self):
        self.write_script({})
        self.assertEqual(self.main('--places', PLACES), tr.EXIT_OK, self.tail())
        n = len(self.events())
        self.assertEqual(self.main('--from', 'savetest'), tr.EXIT_OK, self.tail())
        again = [e['stage'] for e in self.events()[n:]]
        self.assertEqual(sorted(again), sorted(['savetest', 'wholegames', 'hi_journey']))   # forced, as serially

    def test_only_with_a_dependency_not_ok_is_refused_as_serially(self):
        self.fakes(depends={'narrowing': ['build']})
        self.write_script({})
        self.assertEqual(self.main('--places', PLACES, '--only', 'narrowing'), tr.EXIT_ARGS, self.tail())
        self.assertTrue(any('--only narrowing needs build' in ln for ln in self.lines), self.tail())
        self.assertEqual(self.events(), [])

    # --------------------------------------------------------------------------------------------- refusals

    def test_places_that_cannot_run_the_take_are_refused_before_anything_runs(self):
        for places, words in (('headless=1,game=0,gpu=1', 'hi_journey holds game=1'),
                              ('headless=0,game=1,gpu=1', 'savetest holds every headless place'),
                              ('headless=1,game=1,gpu=1,cpu_build=0', 'build holds cpu_build=1')):
            with self.subTest(places):
                self.lines.clear()
                self.assertEqual(self.main('--places', places), tr.EXIT_ARGS, self.tail())
                self.assertTrue(any(words in ln for ln in self.lines), self.tail())
                self.assertEqual(self.events(), [])

    def test_bad_places_syntax_is_refused(self):
        for bad in ('headless', 'headless=-1', 'cores=4', 'headless=x'):
            with self.subTest(bad):
                self.lines.clear()
                self.assertEqual(self.main('--places', bad), tr.EXIT_ARGS)
                self.assertTrue(any('--places' in ln for ln in self.lines), self.tail())
        self.assertEqual(self.events(), [])

    def test_a_cycle_in_after_is_an_error_not_a_hang(self):
        with mock.patch.dict(tt.plans.AFTER, {'narrowing': ['model'], 'model': ['narrowing']}):
            self.assertEqual(self.main('--places', PLACES), tr.EXIT_ARGS, self.tail())
        self.assertTrue(any('no stage can start' in ln for ln in self.lines), self.tail())


class Units(unittest.TestCase):
    """The pure parts of parallel.py."""

    def test_waits_for_barriers_and_edges(self):
        P = tt.parallel
        names = ['a', 'b', 'c', 'd', 'e']
        holds = {'b': {}, 'c': {}, 'e': {}}
        deps = {'c': ['b'], 'e': ['a', 'zz']}
        w = P.waits_for(names, names, lambda n: deps.get(n, []), holds, {})
        self.assertEqual(w['a'], set())
        self.assertEqual(w['b'], {'a'})            # after the barrier a
        self.assertEqual(w['c'], {'a', 'b'})
        self.assertEqual(w['d'], {'a', 'b', 'c'})  # a barrier waits for everything before it
        self.assertEqual(w['e'], {'a', 'd'})       # and everything after it waits for it; unknown deps ignored
        w2 = P.waits_for(names, ['c', 'd', 'e'], lambda n: deps.get(n, []), holds, {})   # --from c
        self.assertEqual(w2['c'], set())
        self.assertEqual(w2['e'], {'d'})

    def test_state_io_retries_a_sharing_violation(self):
        # 5024: a worker reading state.json while the parent replaces it saw Errno 13 for a moment
        calls = []

        def flaky():
            calls.append(1)
            if len(calls) < 3:
                raise PermissionError(13, 'Permission denied')
            return 'read'
        self.assertEqual(tr._retry_sharing(flaky, wait_s=0.01), 'read')
        self.assertEqual(len(calls), 3)

        def never():
            raise PermissionError(13, 'Permission denied')
        with self.assertRaises(PermissionError):           # a lasting refusal still fails, after the tries
            tr._retry_sharing(never, tries=3, wait_s=0.01)

        def other():
            raise ValueError('not JSON')
        with self.assertRaises(ValueError):                # only sharing violations are retried
            tr._retry_sharing(other, wait_s=0.01)

    def _intermediate_game_stages(self):
        """5024's case: scenes holds the GPU place; hi_journey (game + gpu) waits; oldexe, modpack and sweep (game)."""
        P, pl = tt.parallel, tt.plans
        places = P.parse_places('headless=4,game=3,gpu=1')
        free = dict(places, gpu=0)                                   # scenes runs and holds gpu=1
        cands = [(n, P.resolve(pl.holds('intermediate', n), places))
                 for n in ('hi_journey', 'oldexe', 'modpack', 'sweep')]
        return P, free, cands

    def test_a_stage_waiting_for_one_kind_leaves_the_other_kinds_to_later_stages(self):
        P, free, cands = self._intermediate_game_stages()
        got = [n for n, _ in P.admit(cands, free)]
        # hi_journey reserves one game place for itself; two of the three later game stages start beside scenes
        self.assertEqual(got, ['oldexe', 'modpack'])

    def test_negative_control_the_kept_kinds_rule_idles_the_game_places(self):
        # the rule before 5024's fix: a waiting stage kept every kind it needs from the stages after it
        P, free, cands = self._intermediate_game_stages()
        kept, got = set(), []
        for n, need in cands:
            if any(need.get(k, 0) > free.get(k, 0) or (need.get(k, 0) and k in kept) for k in need):
                kept |= {k for k, v in need.items() if v}
                continue
            got.append(n)
        self.assertEqual(got, [])
        self.assertNotEqual(got, [n for n, _ in P.admit(cands, free)])

    def test_a_waiting_stage_is_never_starved_by_later_stages(self):
        P = tt.parallel
        # one game place: oldexe may not take the place hi_journey waits for beside the GPU (it needs game too)
        self.assertEqual(P.admit([('hi_journey', {'game': 1, 'gpu': 1}), ('oldexe', {'game': 1})],
                                 {'game': 1, 'gpu': 0}), [])
        # 'all' of a kind reserved: nothing later takes any of it; a stage that holds nothing still starts
        self.assertEqual(P.admit([('savetest', {'headless': 4}), ('fixchecks', {'headless': 1}), ('model', {})],
                                 {'headless': 3}), [('model', {})])
        # when the places come back the earlier stage starts first, in the plan's order
        self.assertEqual([n for n, _ in P.admit([('hi_journey', {'game': 1, 'gpu': 1}), ('oldexe', {'game': 1})],
                                                {'game': 1, 'gpu': 1})], ['hi_journey'])
        # stages that fit start, each taking its places
        self.assertEqual([n for n, _ in P.admit([('a', {'game': 1}), ('b', {'game': 1}), ('c', {'game': 1})],
                                                {'game': 2})], ['a', 'b'])

    def test_parse_places(self):
        P = tt.parallel
        self.assertEqual(P.parse_places('headless=4,game=1,gpu=1'),
                         {'headless': 4, 'game': 1, 'gpu': 1, 'cpu_build': 1})
        self.assertIsNone(P.parse_places('off'))
        self.assertEqual(P.resolve({'headless': 'all', 'gpu': 1}, {'headless': 4, 'gpu': 1}), {'headless': 4, 'gpu': 1})

    def test_every_declared_stage_is_in_the_plan_and_every_kind_known(self):
        names = [s[0] for t in tt.plans.TIERS for s in tt.plans.stages(t)]   # 5022: every tier's plan
        self.assertFalse(set(tt.plans.HOLDS) - set(names))
        self.assertEqual(tt.parallel.check(names, tt.plans.HOLDS, tt.parallel.parse_places(PLACES)), [])
        for n in ('survey', 'branches', 'testdata', 'lock', 'tierraise', 'tierreport'):
            self.assertNotIn(n, tt.plans.HOLDS)                      # barriers: they run alone


if __name__ == '__main__':
    unittest.main()
