"""Hermetic tests of the driver tools/release/take_release.py (issue 3502): the state file, resume, rerun on changed
inputs, the stop line and exit 10, a failing tool (exit 1), a gate refusal passed through (exit 3), the STOP file and
every argument check. Fake stages and a fake quietlock.py; no gate, lock, build or game is touched.

    python -I -X utf8 -m unittest discover -s tools/release/tests -p "test_take_release*.py" -v
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
import take_release as tr  # noqa: E402

PY = sys.executable

FAKE_QL = r'''
import os, subprocess, sys
a = sys.argv[1:]
held = os.environ.get('FAKE_QL_HELD')
if a and a[0] == 'run':
    if held and os.path.exists(held):
        print('refused, command not started (x: y); lock held by 7003 timing')
        sys.exit(2)
    cmd = a[a.index('--') + 1:]
    print('FAKE-QL run', ' '.join(a[1:a.index('--')]), flush=True)
    sys.exit(subprocess.call(cmd))
if a and a[0] == 'wait':
    if held and os.path.exists(held):
        os.remove(held)
    print('quiet-machine lock free')
    sys.exit(0)
sys.exit(0)
'''

STAGE_A = '''
from pathlib import Path
from take_release import StageResult
DEPENDS = []
def inputs(ctx):
    return {'a': Path(ctx.config['tmp'], 'input-a.txt').read_text()}
def run(ctx):
    c = Path(ctx.config['tmp'], 'count-a.txt'); c.write_text(str(int(c.read_text() if c.exists() else 0) + 1))
    mode = Path(ctx.config['tmp'], 'mode-a.txt')
    if mode.exists() and mode.read_text() == 'stopfile':
        (ctx.work / 'STOP').write_text('operator')
    return StageResult.ok(value=Path(ctx.config['tmp'], 'input-a.txt').read_text())
'''

STAGE_B = '''
import sys
from pathlib import Path
from take_release import StageResult
DEPENDS = ['a']
def run(ctx):
    c = Path(ctx.config['tmp'], 'count-b.txt'); c.write_text(str(int(c.read_text() if c.exists() else 0) + 1))
    m = Path(ctx.config['tmp'], 'mode-b.txt')
    mode = m.read_text() if m.exists() else 'ok'
    if mode == 'stop':
        return StageResult.stop('a new narrowing site', 'triage it and run again')
    if mode == 'fail':
        r = ctx.run([sys.executable, '-c', 'import sys; print("boom"); sys.exit(1)'], name='tool', heavy=True)
        if r.rc != 0:
            return StageResult.failed(f'tool exit {r.rc}')
    if mode == 'refuse':
        ctx.run([sys.executable, '-c', 'import sys; print("launch gate: REFUSED: held by 7003", file=sys.stderr); sys.exit(3)'],
                name='gated', gated=True)
    if mode == 'raise':
        raise ValueError('a bug in the stage')
    if mode == 'heavy':
        r = ctx.run([sys.executable, '-c', 'print("inner ran")'], name='heavy')
        return StageResult.ok(rc=r.rc, wrapped='FAKE-QL run' in r.text, inner='inner ran' in r.text)
    if mode == 'heavy-two':
        r1 = ctx.run([sys.executable, '-c', 'print(1)'], name='one')
        r2 = ctx.run([sys.executable, '-c', 'print(2)'], name='two')
        return StageResult.ok()
    if mode == 'dict-stop':
        return {'status': 'stop', 'outputs': {}, 'stop': {'what': 'w', 'cl_step': 2, 'unblock': 'u'}}
    if mode == 'dict-compat':
        p = ctx.run([sys.executable, '-c', 'print("compat")'], capture_output=True, text=True)
        return {'status': 'ok', 'outputs': {'out': p.stdout.strip(), 'rc': p.returncode}}
    if mode == 'part':
        prev = ctx.previous_outputs()
        if 'part1' not in prev:
            ctx.partial(part1='done')
            return StageResult.failed('second part failed')
        return StageResult.ok(reused=True)
    return StageResult.ok(b=Path(ctx.config['tmp'], 'count-b.txt').read_text() and 'x')
'''

STAGE_C = '''
from pathlib import Path
from take_release import StageResult
DEPENDS = ['b']
def run(ctx):
    c = Path(ctx.config['tmp'], 'count-c.txt'); c.write_text(str(int(c.read_text() if c.exists() else 0) + 1))
    return StageResult.ok()
'''

def write_desc(path, **over):
    """A valid descriptor (3501's format) for Release-9.9.9, from Release-5.4.8.json; None removes a field."""
    d = json.loads((tr.HERE / 'releases' / 'Release-5.4.8.json').read_text(encoding='utf-8'))
    d['tag'] = 'Release-9.9.9'
    for k, v in over.items():
        if v is None:
            d.pop(k, None)
        else:
            d[k] = v
    Path(path).write_text(json.dumps(d), encoding='utf-8')
    return d


STAGES = [('a', '1', '3502'), ('b', '6', '3502'), ('c', '7', '3502'), ('d', '10', '3503')]


class Fixture(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = Path(self._td.name)
        self.work = self.tmp / 'work'
        self.sd = self.tmp / 'fakestages'
        self.sd.mkdir()
        for n, src in (('a', STAGE_A), ('b', STAGE_B), ('c', STAGE_C)):
            (self.sd / f'{n}.py').write_text(textwrap.dedent(src))
        (self.tmp / 'input-a.txt').write_text('one')
        ql = self.tmp / 'fake_quietlock.py'
        ql.write_text(FAKE_QL)
        self.tools = {**tr.default_tools(), 'quietlock': [PY, '-I', str(ql)]}
        self.held = self.tmp / 'held'
        os.environ['FAKE_QL_HELD'] = str(self.held)
        self.lines = []
        # Ticket 4711: the guards' folders are settings (tools/release/settings.py); here temporary ones, and no
        # settings file, so the tests do not depend on this machine's deps/local-settings.ini.
        self.pins = self.tmp / 'pins'
        self.clones = self.tmp / 'clones'
        self._env = {k: os.environ.get(k) for k in ('C5_PINS_DIR', 'C5_CLONES_DIR', 'C5_SETTINGS_FILE')}
        os.environ.update(C5_PINS_DIR=str(self.pins), C5_CLONES_DIR=str(self.clones),
                          C5_SETTINGS_FILE=str(self.tmp / 'no-settings.ini'))

    def tearDown(self):
        os.environ.pop('FAKE_QL_HELD', None)
        for k, v in self._env.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
        self._td.cleanup()

    def driver(self, stages=None, **cfg):
        config = {'tmp': str(self.tmp), 'tier': 'candidate', 'who': 'test3502', 'gate_wait': 0, **cfg}
        return tr.Driver('Release-9.9.9', self.work, object(), config, stages or STAGES[:3], tools=self.tools,
                         stages_dir=self.sd, out=self.lines.append)

    def count(self, n):
        p = self.tmp / f'count-{n}.txt'
        return int(p.read_text()) if p.exists() else 0

    def mode(self, stage, m):
        (self.tmp / f'mode-{stage}.txt').write_text(m)

    def state(self):
        return json.loads((self.work / 'state.json').read_text())

    def status(self, name):
        return next(e for e in self.state()['stages'] if e['name'] == name)['status']


class TestDriver(Fixture):
    def test_full_run_and_state_file(self):
        self.assertEqual(self.driver().run(), 0)
        st = self.state()
        self.assertEqual([e['name'] for e in st['stages']], ['a', 'b', 'c'])
        for e in st['stages']:
            self.assertEqual(e['status'], 'ok')
            for k in ('name', 'cl_step', 'status', 'started', 'ended', 'outputs', 'stop', 'inputs', 'inputs_sha'):
                self.assertIn(k, e)
        self.assertEqual(st['stages'][0]['outputs'], {'value': 'one'})
        self.assertEqual(st['runs'][-1]['exit'], 0)
        self.assertTrue(any('tier candidate reached' in ln for ln in self.lines))

    def test_rerun_unchanged_runs_nothing(self):
        self.driver().run()
        self.assertEqual(self.driver().run(), 0)
        self.assertEqual((self.count('a'), self.count('b'), self.count('c')), (1, 1, 1))

    def test_stop_line_exit_10_and_resume(self):
        self.mode('b', 'stop')
        self.assertEqual(self.driver().run(), 10)
        self.assertEqual(self.lines[-1], 'take_release: STOP b (CL step 6): a new narrowing site; to unblock: '
                                         'triage it and run again')
        e = next(e for e in self.state()['stages'] if e['name'] == 'b')
        self.assertEqual(e['status'], 'stop')
        self.assertEqual(e['stop'], {'what': 'a new narrowing site', 'cl_step': '6', 'unblock': 'triage it and run again'})
        self.assertEqual(self.status('c'), 'pending')
        self.assertEqual(self.count('c'), 0)
        self.mode('b', 'ok')
        self.assertEqual(self.driver().run(), 0)
        self.assertEqual((self.count('a'), self.count('b'), self.count('c')), (1, 2, 1))

    def test_resume_after_each_stage(self):
        # a stop at each stage in turn; each rerun starts at the stopped stage and never repeats an ok one
        self.mode('b', 'stop')
        self.driver().run()
        self.mode('b', 'ok')
        (self.sd / 'c.py').write_text(textwrap.dedent(STAGE_C).replace('return StageResult.ok()',
                                                                          'return StageResult.stop("x", "y")'))
        self.assertEqual(self.driver().run(), 10)
        (self.sd / 'c.py').write_text(textwrap.dedent(STAGE_C))
        self.assertEqual(self.driver().run(), 0)
        self.assertEqual((self.count('a'), self.count('b'), self.count('c')), (1, 2, 2))

    def test_changed_inputs_rerun_ok_stage_and_dependents(self):
        self.driver().run()
        (self.tmp / 'input-a.txt').write_text('two')
        self.assertEqual(self.driver().run(), 0)
        # b depends on a's outputs, which changed: it runs again; its own outputs do not change, so c does not
        self.assertEqual((self.count('a'), self.count('b'), self.count('c')), (2, 2, 1))
        self.assertTrue(any('inputs changed' in ln for ln in self.lines))
        self.assertEqual(self.state()['stages'][0]['outputs']['value'], 'two')

    def test_changed_inputs_same_outputs_stop_the_cascade(self):
        self.driver().run()
        # a's inputs change but its outputs do not: b and c are not run again
        (self.sd / 'a.py').write_text(textwrap.dedent(STAGE_A).replace(
            "return StageResult.ok(value=Path(ctx.config['tmp'], 'input-a.txt').read_text())",
            "return StageResult.ok(value='one')"))
        (self.tmp / 'input-a.txt').write_text('two')
        self.assertEqual(self.driver().run(), 0)
        self.assertEqual((self.count('a'), self.count('b'), self.count('c')), (2, 1, 1))

    def test_failing_tool_exit_1(self):
        self.mode('b', 'fail')
        self.assertEqual(self.driver().run(), 1)
        self.assertEqual(self.status('b'), 'failed')
        self.assertIn('FAILED b: tool exit 1', self.lines[-1])
        log = (self.work / 'logs' / 'b-tool.log').read_text()
        self.assertIn('boom', log)
        self.assertIn('FAKE-QL run', log)  # a heavy command goes through quietlock.py run
        self.assertEqual(self.count('c'), 0)

    def test_exception_in_stage_exit_1_with_traceback(self):
        self.mode('b', 'raise')
        self.assertEqual(self.driver().run(), 1)
        self.assertIn('ValueError: a bug in the stage', self.lines[-1])
        self.assertIn('Traceback', (self.work / 'logs' / 'b-traceback.log').read_text())

    def test_gate_refusal_exit_3_passed_through(self):
        self.mode('b', 'refuse')
        self.assertEqual(self.driver().run(), 3)
        self.assertEqual(self.status('b'), 'pending')
        self.assertTrue(self.lines[-1].startswith('take_release: REFUSED b (CL step 6)'))
        self.assertIn('launch gate: REFUSED', self.lines[-1])
        self.assertEqual(self.count('c'), 0)

    def test_lock_refusal_of_quietlock_run_is_a_refusal(self):
        self.mode('b', 'heavy')
        self.held.write_text('held')
        self.assertEqual(self.driver().run(), 3)
        self.assertIn('refused, command not started', self.lines[-1])

    def test_gate_wait_retries_after_the_lock_is_free(self):
        self.mode('b', 'heavy')
        self.held.write_text('held')
        self.assertEqual(self.driver(gate_wait=1).run(), 0)
        out = self.state()['stages'][1]['outputs']
        self.assertEqual(out, {'rc': 0, 'wrapped': True, 'inner': True})

    def test_stop_file_before_a_stage(self):
        self.mode('a', 'stopfile')
        self.assertEqual(self.driver().run(), 10)
        self.assertTrue(self.lines[-1].startswith('take_release: STOP b (CL step 6): stop file '))
        self.assertIn('to unblock: remove', self.lines[-1])
        self.assertEqual(self.status('a'), 'ok')
        self.assertEqual(self.status('b'), 'pending')
        self.assertEqual(self.count('b'), 0)
        (self.tmp / 'mode-a.txt').write_text('')
        (self.work / 'STOP').unlink()
        self.assertEqual(self.driver().run(), 0)
        self.assertEqual((self.count('a'), self.count('b'), self.count('c')), (1, 1, 1))

    def test_stop_file_inside_a_stage_before_a_step(self):
        self.mode('b', 'heavy-two')
        stage_b_first = textwrap.dedent(STAGE_B).replace(
            "r1 = ctx.run([sys.executable, '-c', 'print(1)'], name='one')",
            "r1 = ctx.run([sys.executable, '-c', 'print(1)'], name='one'); (ctx.work / 'STOP').write_text('x')")
        (self.sd / 'b.py').write_text(stage_b_first)
        self.assertEqual(self.driver().run(), 10)
        self.assertEqual(self.status('b'), 'pending')
        self.assertTrue((self.work / 'logs' / 'b-one.log').exists())
        self.assertFalse((self.work / 'logs' / 'b-two.log').exists())

    def test_parts_of_an_unfinished_stage_are_offered_on_resume(self):
        self.mode('b', 'part')
        self.assertEqual(self.driver().run(), 1)
        self.assertEqual(self.driver().run(), 0)
        self.assertEqual(self.state()['stages'][1]['outputs'], {'part1': 'done', 'reused': True})

    def test_dict_result_of_3501s_form(self):
        self.mode('b', 'dict-stop')
        self.assertEqual(self.driver().run(), 10)
        self.assertEqual(self.lines[-1], 'take_release: STOP b (CL step 2): w; to unblock: u')
        self.mode('b', 'dict-compat')
        self.assertEqual(self.driver().run(), 0)
        self.assertEqual(self.state()['stages'][1]['outputs'], {'out': 'compat', 'rc': 0})

    def test_compat_runner_checks_the_stop_file(self):
        self.work.mkdir(parents=True)
        (self.work / 'STOP').write_text('x')
        d = self.driver()
        ctx = tr.Ctx(d, 'b')
        with self.assertRaises(tr.StopRequested):
            ctx.run([sys.executable, '-c', 'pass'], capture_output=True)

    def test_from_reruns_that_stage_and_later(self):
        self.driver().run()
        self.assertEqual(self.driver().run(frm='b'), 0)
        self.assertEqual((self.count('a'), self.count('b'), self.count('c')), (1, 2, 2))

    def test_from_past_a_stopped_stage_does_not_claim_the_tier(self):
        self.mode('b', 'stop')
        self.driver().run()
        self.assertEqual(self.driver().run(frm='c'), 2)  # c needs b
        (self.sd / 'c.py').write_text(textwrap.dedent(STAGE_C).replace("DEPENDS = ['b']", "DEPENDS = ['a']"))
        self.assertEqual(self.driver().run(frm='c'), 10)
        self.assertEqual(self.count('c'), 1)
        self.assertTrue(self.lines[-1].startswith('take_release: STOP b (CL step 6): the candidate tier is not '
                                                  'reached: b is stop (a new narrowing site)'))
        self.assertFalse(any('tier candidate reached' in ln for ln in self.lines))

    def test_only_runs_one_stage(self):
        self.driver().run()
        self.assertEqual(self.driver().run(only='b'), 0)
        self.assertEqual((self.count('a'), self.count('b'), self.count('c')), (1, 2, 1))

    def test_only_refuses_when_a_dependency_is_not_ok(self):
        self.mode('b', 'stop')
        self.driver().run()
        self.assertEqual(self.driver().run(only='c'), 2)
        self.assertIn('needs b', self.lines[-1])
        self.assertEqual(self.count('c'), 0)

    def test_missing_stage_module_stops_not_built_yet(self):
        self.assertEqual(self.driver(stages=STAGES).run(), 10)
        self.assertTrue(self.lines[-1].startswith('take_release: STOP d (CL step 10): not built yet (issue 3503)'))
        self.assertEqual(self.status('d'), 'stop')

    def test_validated_tier_placeholder_while_3506s_registry_is_missing(self):
        st = tr.tier_stages('validated', self.sd)
        self.assertEqual(st[:len(tr.CANDIDATE)], tr.CANDIDATE)
        self.assertEqual(st[len(tr.CANDIDATE):], [('validated', '14-17', '3506')])
        self.assertEqual(tr.tier_stages('candidate', self.sd), tr.CANDIDATE)

    def test_validated_tier_from_the_registry_hook(self):
        (self.sd / 'validated.py').write_text("STAGES = [('refsave', '16', '3506'), ('wholegames', '14', '3506')]\n")
        st = tr.tier_stages('validated', self.sd)
        self.assertEqual(st[len(tr.CANDIDATE):], [('refsave', '16', '3506'), ('wholegames', '14', '3506')])

    def test_second_driver_in_the_same_work_folder_is_refused(self):
        self.work.mkdir(parents=True)
        (self.work / 'driver.pid').write_text(f'{os.getppid()} x\n')  # a live process that is not this one
        with self.assertRaises(tr.ArgError):
            self.driver().run()
        self.assertEqual(self.count('a'), 0)

    def test_stale_driver_pid_is_taken_over(self):
        self.work.mkdir(parents=True)
        p = subprocess.Popen([PY, '-c', 'pass'])
        p.wait()
        (self.work / 'driver.pid').write_text(f'{p.pid} x\n')
        self.assertEqual(self.driver().run(), 0)
        self.assertFalse((self.work / 'driver.pid').exists())


class TestArguments(Fixture):
    def setUp(self):
        super().setUp()
        self.fork = self.tmp / 'fork'
        self.fork.mkdir()
        subprocess.run(['git', 'init', '-q', str(self.fork)], check=True)
        self.desc = self.tmp / 'desc.json'
        write_desc(self.desc)
        self.tj = self.tmp / 'tools.json'
        self.tj.write_text(json.dumps({'quietlock': self.tools['quietlock']}))

    def main(self, *extra, tag='Release-9.9.9', work=None, fork=True, desc=True):
        argv = [tag, '--work', str(work or self.work), '--stages-dir', str(self.sd), '--tools', str(self.tj)]
        if fork:
            argv += ['--fork', str(self.fork)]
        if desc:
            argv += ['--descriptor', str(self.desc)]
        lines = []
        rc = tr.main(argv + list(extra), out=lines.append)
        self.lines = lines
        return rc

    def assertArgError(self, rc, text):
        self.assertEqual(rc, 2, self.lines)
        self.assertIn(text, '\n'.join(self.lines))
        self.assertFalse((self.work / 'state.json').exists())

    def test_bad_tag(self):
        self.assertArgError(self.main(tag='5.4.8'), "expected Release-<major>.<minor>.<patch>")
        self.assertIn('needs --base-commit', '\n'.join(self.lines))

    def test_unknown_from_stage(self):
        self.assertArgError(self.main('--from', 'nope'), '--from nope: not a stage of the candidate tier')

    def test_unknown_only_stage(self):
        self.assertArgError(self.main('--only', 'validated'), '--only validated: not a stage of the candidate tier')

    def test_from_and_only_together(self):
        self.assertEqual(self.main('--from', 'build', '--only', 'build'), 2)

    def test_unknown_tier(self):
        self.assertEqual(self.main('--tier', 'gold'), 2)

    def test_work_inside_the_repository(self):
        self.assertArgError(self.main(work=tr.REPO / 'tmp-work'), 'is under')
        self.assertFalse((tr.REPO / 'tmp-work').exists())

    def test_scratch_inside_the_repository(self):
        self.assertArgError(self.main('--scratch', str(tr.REPO / 'tmp-scratch')), '--scratch')

    def test_work_under_the_pins_folder(self):
        w = self.pins / 'dlls' / 'x'
        self.assertArgError(self.main(work=w), 'is under')
        self.assertFalse(self.pins.exists())

    def test_scratch_under_the_pins_folder(self):
        self.assertArgError(self.main('--scratch', str(self.pins / 's')), '--scratch')
        self.assertFalse(self.pins.exists())

    def test_no_pins_setting_refuses(self):
        """Fail-closed (ticket 4711): without the pins_dir setting the driver refuses (exit 2) and writes nothing."""
        del os.environ['C5_PINS_DIR']
        self.assertArgError(self.main(), 'setting pins_dir is not set')
        self.assertFalse(self.work.exists())

    def test_no_clones_setting_refuses(self):
        del os.environ['C5_CLONES_DIR']
        if tr.REPO.parent.name == 'GitHub':
            self.skipTest('the repository sits in a folder named GitHub: that folder is the default')
        self.assertArgError(self.main(), 'setting clones_dir is not set')
        self.assertFalse(self.work.exists())

    def test_pins_setting_from_the_settings_file(self):
        del os.environ['C5_PINS_DIR']
        f = self.tmp / 'settings.ini'
        f.write_text(f'# a comment\npins_dir = {self.pins.as_posix()}\n', encoding='utf-8')
        os.environ['C5_SETTINGS_FILE'] = str(f)
        self.assertArgError(self.main(work=self.pins / 'w'), 'is under')

    def test_bad_settings_file_refuses(self):
        del os.environ['C5_PINS_DIR']
        f = self.tmp / 'settings.ini'
        f.write_text('pins_dir: nope\n', encoding='utf-8')
        os.environ['C5_SETTINGS_FILE'] = str(f)
        self.assertArgError(self.main(), 'not a `key = value` line')
        self.assertFalse(self.work.exists())

    def test_no_fork_on_first_run(self):
        self.assertArgError(self.main(fork=False), '--fork <clone of the DLL fork> is needed')

    def test_fork_not_a_git_clone(self):
        self.fork = self.tmp / 'notgit'
        self.fork.mkdir()
        self.assertArgError(self.main(), 'not a git clone')

    def test_fork_missing(self):
        self.fork = self.tmp / 'nothere'
        self.assertArgError(self.main(), 'no such folder')

    def test_fork_under_your_own_clones(self):
        self.fork = self.clones / 'Community-Patch-DLL'
        self.assertArgError(self.main(), 'your own clones')

    def test_jobs_out_of_range(self):
        self.assertArgError(self.main('--jobs', '0'), '--jobs 0')

    def test_negative_gate_wait(self):
        self.assertArgError(self.main('--gate-wait', '-1'), '--gate-wait')

    def test_bad_who(self):
        self.assertArgError(self.main('--who', 'a b'), '--who')

    def test_missing_ia_patch(self):
        self.assertArgError(self.main('--ia-patch', str(self.tmp / 'none.patch')), '--ia-patch')

    def test_descriptor_missing_file(self):
        self.desc = self.tmp / 'none.json'
        self.assertArgError(self.main(), 'descriptor:')

    def test_descriptor_for_another_tag(self):
        write_desc(self.desc, tag='Release-1.0.0')
        self.assertArgError(self.main(), "is 'Release-1.0.0' but the file is for 'Release-9.9.9'")

    def test_descriptor_missing_required_field(self):
        write_desc(self.desc, upstream_commit=None)
        self.assertArgError(self.main(), 'upstream_commit: missing (required)')

    def test_descriptor_not_json(self):
        self.desc.write_text('{nope')
        self.assertArgError(self.main(), 'descriptor:')

    def test_state_of_another_tag(self):
        self.work.mkdir(parents=True)
        (self.work / 'state.json').write_text(json.dumps({'format': 1, 'tag': 'Release-1.0.0', 'stages': []}))
        rc = self.main()
        self.assertEqual(rc, 2)
        self.assertIn("state of 'Release-1.0.0'", '\n'.join(self.lines))

    def test_state_unreadable(self):
        self.work.mkdir(parents=True)
        (self.work / 'state.json').write_text('{')
        self.assertEqual(self.main(), 2)
        self.assertIn('not readable JSON', '\n'.join(self.lines))

    def test_config_is_recorded_and_reused(self):
        # the first stage of the real tier is missing in the fake stages dir: a not-built-yet stop, config recorded
        rc = self.main('--jobs', '4', '--carry', 'pins')
        self.assertEqual(rc, 10, self.lines)
        cfg = json.loads((self.work / 'state.json').read_text())['config']
        self.assertEqual((cfg['jobs'], cfg['carry'], cfg['fork']), (4, 'pins', str(self.fork.resolve())))
        rc = self.main(fork=False, desc=False)  # fork and descriptor come from the state
        self.assertEqual(rc, 10, self.lines)
        self.assertIn('not built yet (issue 3502)', self.lines[-1])

    def config(self):
        return json.loads((self.work / 'state.json').read_text())['config']

    def test_series_is_recorded_and_reused(self):
        # issue 3509: --series FILE goes into the state's config (the branches stage reads config['series'])
        series = self.tmp / 'carry-copy.tsv'
        series.write_text('# a release copy\n')
        self.assertEqual(self.main('--series', str(series)), 10, self.lines)
        self.assertEqual(self.config()['series'], str(series.resolve()))
        self.assertEqual(self.main(fork=False, desc=False), 10, self.lines)
        self.assertEqual(self.config()['series'], str(series.resolve()))

    def test_series_default_is_none(self):
        self.assertEqual(self.main(), 10, self.lines)
        self.assertIsNone(self.config()['series'])

    def test_series_missing_file(self):
        self.assertArgError(self.main('--series', str(self.tmp / 'none.tsv')), '--series')

    def test_release_name_with_a_base_commit(self):
        # issue 3509: a name that is not a Release tag, with the base named by its commit; recorded and reused
        write_desc(self.desc, tag='master-ba8aafdc')
        rc = self.main('--base-commit', 'BA8AAFDCA', tag='master-ba8aafdc')
        self.assertEqual(rc, 10, self.lines)
        self.assertIn('not built yet (issue 3502)', self.lines[-1])
        st = json.loads((self.work / 'state.json').read_text())
        self.assertEqual((st['tag'], st['config']['base_commit']), ('master-ba8aafdc', 'ba8aafdca'))
        self.assertEqual(self.main(tag='master-ba8aafdc', fork=False, desc=False), 10, self.lines)
        self.assertEqual(self.config()['base_commit'], 'ba8aafdca')

    def test_release_tag_with_a_base_commit(self):
        # the dry run's case without a local tag: a Release-x.y.z name that is not (yet) a tag in the fork
        self.assertEqual(self.main('--base-commit', 'ba8aafdca'), 10, self.lines)
        self.assertEqual(self.config()['base_commit'], 'ba8aafdca')

    def test_release_name_without_a_base_commit(self):
        write_desc(self.desc, tag='master-ba8aafdc')
        self.assertArgError(self.main(tag='master-ba8aafdc'), 'needs --base-commit')

    def test_bad_release_name(self):
        self.assertArgError(self.main('--base-commit', 'ba8aafdca', tag='a b'), "expected Release-")

    def test_bad_base_commit(self):
        self.assertArgError(self.main('--base-commit', 'master'), "--base-commit 'master': 7-40 hex digits")

    def test_new_tag_without_a_descriptor_names_base_release(self):
        # issue 3512: no releases/<tag>.json and no --descriptor: exit 2, the message names the flow
        self.assertArgError(self.main(tag='Release-99.0.0', desc=False), 'no release descriptor')
        self.assertIn('--base-release', '\n'.join(self.lines))

    def test_base_release_is_recorded_and_reused(self):
        rc = self.main('--base-release', 'Release-5.4.8', tag='Release-99.0.0', desc=False)
        self.assertEqual(rc, 10, self.lines)
        self.assertEqual((self.config()['base_release'], self.config()['descriptor']), ('Release-5.4.8', None))
        self.assertEqual(self.main(tag='Release-99.0.0', fork=False, desc=False), 10, self.lines)
        self.assertEqual(self.config()['base_release'], 'Release-5.4.8')

    def test_base_release_current(self):
        rc = self.main('--base-release', 'current', tag='Release-99.0.0', desc=False)
        self.assertEqual(rc, 10, self.lines)
        self.assertEqual(self.config()['base_release'], 'current')

    def test_unknown_base_release(self):
        self.assertArgError(self.main('--base-release', 'Release-0.0.1', tag='Release-99.0.0', desc=False),
                            "unknown release 'Release-0.0.1'")
        self.assertIn('--base-release names the previous release', '\n'.join(self.lines))

    def test_base_release_and_descriptor_exclude_each_other(self):
        self.assertArgError(self.main('--base-release', 'Release-5.4.8'), '--descriptor and --base-release')

    def test_descriptor_given_later_replaces_a_recorded_base_release(self):
        self.assertEqual(self.main('--base-release', 'Release-5.4.8', desc=False), 10, self.lines)
        self.assertEqual(self.main(fork=False), 10, self.lines)
        self.assertEqual((self.config()['base_release'], self.config()['descriptor']),
                         (None, str(self.desc.resolve())))

    def test_skip_ia_is_recorded_and_with_ia_undoes_it(self):
        def ia():
            return json.loads((self.work / 'state.json').read_text())['config']['ia']
        self.main()
        self.assertIs(ia(), True)
        self.main('--skip-ia')
        self.assertIs(ia(), False)
        self.main(fork=False, desc=False)
        self.assertIs(ia(), False)
        self.main('--with-ia')
        self.assertIs(ia(), True)


class OldOptionNames(unittest.TestCase):
    """Ticket 0708: the options that named the owner were renamed (--for-owner, --not-for-owner, --owner-check); the old
    names are hidden aliases that set the same thing and print one note each."""

    def test_each_old_name_sets_what_its_new_name_sets(self):
        for old, new in (('--for-art', '--for-owner'), ('--not-for-art', '--not-for-owner'),
                         ('--art-check', '--owner-check')):
            a, b = tr.parse_args(['Release-9.9.9', old]), tr.parse_args(['Release-9.9.9', new])
            self.assertEqual((a.for_owner, a.owner_check), (b.for_owner, b.owner_check), old)
            self.assertEqual(tr.old_option_notes(a), [f'take_release: note: {old} is the old name of {new}; it still '
                                                      f'works, use {new}'])
            self.assertEqual(tr.old_option_notes(b), [])

    def test_defaults_unchanged(self):
        a = tr.parse_args(['Release-9.9.9'])
        self.assertIsNone(a.for_owner)
        self.assertIs(a.owner_check, False)

    def test_last_marker_wins_across_old_and_new_names(self):
        self.assertIs(tr.parse_args(['Release-9.9.9', '--for-art', '--not-for-owner']).for_owner, False)
        self.assertIs(tr.parse_args(['Release-9.9.9', '--not-for-owner', '--for-art']).for_owner, True)

    def test_the_note_is_printed_once(self):
        out = []
        with tempfile.TemporaryDirectory() as t:
            rc = tr.main(['Release-9.9.9', '--art-check', '--art-check', '--work', str(Path(t) / 'none')],
                         out=out.append)
        self.assertEqual(rc, tr.EXIT_ARGS)                  # no state there: the check itself ran (exit 2)
        notes = [ln for ln in out if 'old name' in ln]
        self.assertEqual(notes, ['take_release: note: --art-check is the old name of --owner-check; it still works, '
                                 'use --owner-check'], out)
        self.assertTrue(any('--owner-check: no state' in ln for ln in out), out)

    def test_old_names_hidden_from_help(self):
        r = subprocess.run([PY, '-I', str(HERE.parent / 'take_release.py'), '--help'], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0)
        usage = r.stdout.split('\n\n')[0]                                # argparse's usage block
        tail = r.stdout.split('Old option names')[-1]                     # the option list after the docstring
        for old in ('--for-art', '--not-for-art', '--art-check'):
            self.assertNotIn(old + ' ', usage, old)
            self.assertNotIn(old + ']', usage, old)
        self.assertIn('--for-owner', usage)
        self.assertNotRegex(tail.split('The contract')[-1], r'--(not-)?for-art\b|--art-check\b')

    def test_a_state_written_before_the_rename_keeps_its_marker(self):
        with tempfile.TemporaryDirectory() as t:
            work = Path(t)
            (work / tr.STATE_NAME).write_text(json.dumps({'config': {'for_art': True}}), encoding='utf-8')
            self.assertIs(tr.build_config(tr.parse_args(['Release-9.9.9']), work)['for_owner'], True)
            self.assertIs(tr.build_config(tr.parse_args(['Release-9.9.9', '--not-for-owner']), work)['for_owner'],
                          False)
            self.assertNotIn('for_art', tr.build_config(tr.parse_args(['Release-9.9.9']), work))


if __name__ == '__main__':
    unittest.main()
