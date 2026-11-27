"""Hermetic tests of the intermediate tier's plan and of its tierraise (epic 0630, ticket 5022): the plan is column I
of the study's section 6 with the minutes of rule 3; take_tier accepts the tier; tierraise records changes in the
intermediate tier and never stops there, while the hotfix tier still raises; the export gate's minute-1 preflight
passes, stops, and is off with --no-export-preflight (negative tests).

    PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q tools/release/tests/test_tier_intermediate.py
"""
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE.parent / 'tiers'))
import take_release as tr  # noqa: E402
import take_tier as tt  # noqa: E402

TR = tr.load_stage_module('tierraise', HERE.parent / 'tiers' / 'stages')
COMP = {'clang-cl': 'clang version 19.1.0', 'lld-link': 'LLD 19.1.0'}


class Ctx:
    """The parts of the driver's Ctx that tierraise and its preflight use; ctx.run runs the command for real (the test
    passes a fake check script) and records the call."""

    def __init__(self, config, outs, desc_file, tmp):
        self.config = config
        self._outs = outs
        self.desc = type('D', (), {'file': desc_file})()
        self.tmp = tmp
        self.said, self.partials, self.runs = [], {}, []

    def outputs(self, n):
        return self._outs[n]

    def outputs_if_ok(self, n):
        return self._outs.get(n)

    def say(self, *a):
        self.said.append(' '.join(str(x) for x in a))

    def partial(self, **o):
        self.partials.update(o)

    def git(self, *args, cwd=None, check=True, binary=False):
        return 'f' * 40 if args[:2] == ('rev-parse', 'HEAD') else None

    def run(self, cmd, *, name=None, heavy=True, cwd=None, **kw):
        import subprocess
        import time
        self.runs.append((name, cmd))
        log = self.tmp / f'{name}.log'
        t0 = time.time()
        p = subprocess.run(cmd, capture_output=True, text=True)
        log.write_text(p.stdout + p.stderr)
        return tr.RunResult(p.returncode, log, time.time() - t0, p.stdout + p.stderr)


class Plan(unittest.TestCase):
    def test_tier_known(self):
        self.assertIn('intermediate', tt.plans.TIERS)
        self.assertNotIn('full', tt.plans.TIERS)

    def test_column_i(self):
        names = [s[0] for s in tt.plans.stages('intermediate')]
        self.assertEqual(names[:4], ['survey', 'branches', 'testdata', 'lock'])
        self.assertEqual(names[-1], 'tierreport')
        for need in ('tierraise', 'build', 'suites', 'scenes', 'savetest', 'fixchecks', 'wholegames', 'instance',
                     'hi_journey', 'oldexe', 'modpack', 'sweep'):
            self.assertIn(need, names)
        # the study's R1, R2, E3: never in the intermediate tier
        for gone in ('drift', 'refsave', 'report'):
            self.assertNotIn(gone, names)
        self.assertEqual(len(names), len(set(names)))

    def test_every_stage_names_minutes_and_what_it_displaces(self):
        # rule 3 of the epic
        names = [s[0] for s in tt.plans.stages('intermediate')]
        self.assertEqual(sorted(names), sorted(tt.plans.INTERMEDIATE_MINUTES))
        for n, (minutes, displaces, src) in tt.plans.INTERMEDIATE_MINUTES.items():
            self.assertIsInstance(minutes, int, n)
            self.assertTrue(displaces and src.startswith('['), n)

    def test_hotfix_plan_unchanged(self):
        self.assertEqual([s[0] for s in tt.plans.stages('hotfix')],
                         ['survey', 'branches', 'testdata', 'lock', 'tierraise', 'build', 'narrowing', 'census',
                          'model', 'purity', 'pack', 'offline', 'suites', 'savetest', 'wholegames', 'hi_journey',
                          'tierreport'])

    def test_holds_of_the_intermediate_tier(self):
        # 5022 on 5017: the four whole games side by side hold four headless places in tier I only
        self.assertEqual(tt.plans.holds('intermediate', 'wholegames'), {'headless': 4})
        self.assertEqual(tt.plans.holds('hotfix', 'wholegames'), {})
        for n in ('scenes', 'instance', 'oldexe', 'modpack', 'sweep', 'fixchecks'):
            self.assertTrue(tt.plans.holds('intermediate', n), n)
        self.assertIsNone(tt.plans.holds('intermediate', 'tierreport'))   # a barrier

    def test_suites_are_a_barrier_right_after_the_build_in_tier_i(self):
        # ticket 5024, MC #10778: no game stage of the take beside the suites (they ran 6-10x slower beside them)
        names = [s[0] for s in tt.plans.stages('intermediate')]
        self.assertEqual(names[names.index('build') + 1], 'suites')
        self.assertIsNone(tt.plans.holds('intermediate', 'suites'))
        P = tt.parallel
        w = P.waits_for(names, names, lambda n: [], {n: tt.plans.holds('intermediate', n) for n in names
                                                     if tt.plans.holds('intermediate', n) is not None}, {})
        for n in ('scenes', 'savetest', 'wholegames', 'instance', 'hi_journey', 'oldexe', 'modpack', 'sweep'):
            self.assertIn('suites', w[n], n)           # every game stage starts after the suites end
        self.assertIn('build', w['suites'])
        # the hotfix tier keeps them side by side, after offline
        self.assertEqual(tt.plans.holds('hotfix', 'suites'), {})
        hot = [s[0] for s in tt.plans.stages('hotfix')]
        self.assertEqual(hot[hot.index('offline') + 1], 'suites')

    def test_wholegames_does_not_wait_for_savetest(self):
        mod = tr.load_stage_module('wholegames', HERE.parent / 'tiers' / 'stages')
        self.assertNotIn('savetest', mod.USES)

    def test_take_tier_accepts_intermediate(self):
        own, rest = tt.parse_args(['Release-9.9.9', '--tier', 'intermediate'])
        self.assertEqual(own.tier, 'intermediate')
        self.assertFalse(own.no_export_preflight)

    def test_take_tier_refuses_full(self):
        with self.assertRaises(SystemExit):
            tt.parse_args(['Release-9.9.9', '--tier', 'full'])


class Raise(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = Path(self._td.name)
        self.desc = self.tmp / 'desc.json'
        self.desc.write_text(json.dumps({'tag': 'Release-9.9.9', 'upstream_commit': 'a' * 40,
                                         'mods': [{'folder': 'x', 'version': 1}]}))
        self.base = self.tmp / 'base'
        self.base.mkdir()
        self.lock = {'x64': {'commit': '1' * 40}, 'i0': {'commit': '2' * 40}}
        self.survey = {'tag_commit': '3' * 40}
        self.write_base()
        self._env = {k: os.environ.get(k) for k in ('C5_TIER_EXE_BUILD', 'C5_TIER_BASE_EXE_BUILD')}
        for k in self._env:
            os.environ.pop(k, None)

    def tearDown(self):
        for k, v in self._env.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
        self._td.cleanup()

    def write_base(self, tools_commit='e' * 40):
        st = {'tag': 'Release-9.9.9', 'config': {'carry': 'pins'}, 'stages': [
            {'name': 'survey', 'status': 'ok', 'outputs': self.survey},
            {'name': 'testdata', 'status': 'ok', 'outputs': {'descriptor': str(self.desc)}},
            {'name': 'lock', 'status': 'ok', 'outputs': self.lock},
            {'name': 'build', 'status': 'ok', 'outputs': {'tools_commit': tools_commit}},
            {'name': 'tierraise', 'status': 'ok', 'outputs': {'now': {'compiler': COMP, 'exe_build': '26577625'}}}]}
        (self.base / 'state.json').write_text(json.dumps(st))

    def check_script(self, rc):
        p = self.tmp / f'check{rc}.py'
        p.write_text(f'import sys\nprint("export check:", sys.argv[1:])\nsys.exit({rc})\n')
        return p

    def ctx(self, tier, lock=None, preflight=None):
        cfg = {'base_take': str(self.base), 'carry': 'pins', 'compiler_lines': COMP, 'tier': tier}
        if preflight is not None:
            cfg.update(export_preflight=True, export_check=str(self.check_script(preflight)))
        return Ctx(cfg, {'survey': self.survey, 'lock': lock or self.lock,
                         'testdata': {'descriptor': str(self.desc)}}, str(self.desc), self.tmp)

    def test_intermediate_records_and_goes_on(self):
        c = self.ctx('intermediate', lock={'x64': {'commit': '9' * 40}, 'i0': {'commit': '2' * 40}})
        r = TR.run(c)
        self.assertEqual(r.status, 'ok', r.what)
        self.assertEqual(r.outputs['raised'], [])
        self.assertTrue(any(x.startswith('dll_pin changed') for x in r.outputs['changed']))
        self.assertNotIn('ask_full', r.outputs)

    def test_intermediate_exe_change_asks_for_full(self):
        os.environ['C5_TIER_EXE_BUILD'] = '4390913'
        r = TR.run(self.ctx('intermediate'))
        self.assertEqual(r.status, 'ok', r.what)
        self.assertIn('full tier', r.outputs['ask_full'])

    def test_hotfix_still_raises(self):
        # negative control of the intermediate rule: the same change in the hotfix tier is the M1 stop
        r = TR.run(self.ctx('hotfix', lock={'x64': {'commit': '9' * 40}, 'i0': {'commit': '2' * 40}}))
        self.assertEqual(r.status, 'stop')
        self.assertIn('raised to the intermediate tier', r.what)
        self.assertIn('--tier intermediate', r.unblock)

    def test_preflight_passes(self):
        c = self.ctx('intermediate', preflight=0)
        r = TR.run(c)
        self.assertEqual(r.status, 'ok', r.what)
        self.assertEqual(c.runs[0][0], 'export-preflight')
        cmd = c.runs[0][1]
        self.assertEqual(cmd[cmd.index('--since') + 1], 'e' * 40)
        self.assertEqual(cmd[cmd.index('--rev') + 1], 'f' * 40)
        self.assertTrue(c.partials['export_preflight']['ran'])

    def test_preflight_failure_stops_hotfix_and_intermediate(self):
        for tier in ('hotfix', 'intermediate'):
            r = TR.run(self.ctx(tier, preflight=1))
            self.assertEqual(r.status, 'stop', tier)
            self.assertIn('export preflight FAILED', r.what)
            self.assertEqual(r.outputs['export_preflight']['rc'], 1)

    def test_preflight_off_runs_nothing(self):
        c = self.ctx('hotfix')
        r = TR.run(c)
        self.assertEqual(r.status, 'ok', r.what)
        self.assertEqual(c.runs, [])

    def test_preflight_without_base_commit_is_said_not_run(self):
        self.write_base(tools_commit=None)
        c = self.ctx('intermediate', preflight=1)
        r = TR.run(c)
        self.assertEqual(r.status, 'ok', r.what)
        self.assertEqual(c.runs, [])
        self.assertFalse(c.partials['export_preflight']['ran'])
        self.assertTrue(any('NOT RUN' in s for s in c.said))


# --------------------------------------------------------------------------------------------------- fixchecks

import test_tier_carried as TC  # noqa: E402

FAKE_FIX = r"""
import os, sys
name = sys.argv[1]
with open(os.environ['FAKE_FIX_CALLS'], 'a', encoding='utf-8') as f:
    f.write(name + ' ' + ' '.join(sys.argv[2:]) + '\n')
if os.environ.get('FAKE_FIX_FAIL') == name:
    print('broken on purpose'); sys.exit(3)
print({'0096': 'fix: rc 0: 0096-RESULT PASS', '0084': 'VERDICT PASS',
       '0103': 'CMP new-off vs new-on: IDENTICAL'}[name])
"""

FAKE_QL = r"""
import subprocess, sys
a = sys.argv[1:]
sys.exit(subprocess.call(a[a.index('--') + 1:]) if a and a[0] == 'run' else 0)
"""


class FixChecks(TC.Takes):
    def setUp(self):
        super().setUp()
        fx = self.tmp / 'fake_fix.py'
        fx.write_text(FAKE_FIX)
        ql = self.tmp / 'fake_ql.py'
        ql.write_text(FAKE_QL)
        self.fix_calls = self.tmp / 'fixcalls.txt'
        self.setenv(FAKE_FIX_CALLS=str(self.fix_calls))
        self.tools.update({f'fix{n}': [TC.PY, '-I', str(fx), n] for n in ('0096', '0084', '0103')},
                          quietlock=[TC.PY, '-I', str(ql)])
        os.environ.pop('FAKE_FIX_FAIL', None)

    def fix_runs(self):
        return [ln.split()[0] for ln in self.fix_calls.read_text().splitlines()] if self.fix_calls.is_file() else []

    def stage(self, work, tier='intermediate'):
        return self.run_stage(work, 'fixchecks', tier=tier)

    def test_fresh_then_carried_then_one_server_byte_fresh(self):
        w1 = self.take()
        self.assertEqual(self.stage(w1), tr.EXIT_OK, self.text())
        self.assertEqual(self.fix_runs(), ['0096', '0084', '0103'])      # oldsave dropped (R2)
        e = self.entry(w1, 'fixchecks')
        self.assertIn('oldsave', e['outputs']['dropped'])
        self.assertTrue(e['outputs']['fresh_why'])
        w2 = self.take()
        self.assertEqual(self.stage(w2), tr.EXIT_OK, self.text())
        self.assertEqual(len(self.fix_runs()), 3)                         # carried: nothing ran
        self.assertIn('carried from', self.entry(w2, 'fixchecks')['outputs']['carried']['line'])
        w3 = self.take(server=b'server exe!')                             # one byte of server.exe
        self.assertEqual(self.stage(w3), tr.EXIT_OK, self.text())
        self.assertEqual(len(self.fix_runs()), 6)

    def test_failed_check_fails_and_is_not_recorded(self):
        os.environ['FAKE_FIX_FAIL'] = '0084'
        w1 = self.take()
        self.assertEqual(self.stage(w1), tr.EXIT_FAILED, self.text())
        self.assertEqual(self.fix_runs(), ['0096', '0084'])
        os.environ.pop('FAKE_FIX_FAIL')
        w2 = self.take()
        self.assertEqual(self.stage(w2), tr.EXIT_OK, self.text())
        self.assertEqual(self.fix_runs()[2:], ['0096', '0084', '0103'])   # nothing was carried from the failure

    def test_not_in_the_hotfix_tier(self):
        w1 = self.take()
        self.assertEqual(self.stage(w1, tier='hotfix'), tr.EXIT_OK, self.text())
        self.assertEqual(self.entry(w1, 'fixchecks')['status'], 'skipped')
        self.assertEqual(self.fix_runs(), [])


# ------------------------------------------------------------------------------------------------ ticket 5023 (W3)
# The 43-civ variant of the intermediate tier, the EXE-4390913 journey (oldexe), the Tiny-only instance stage and the
# tier report's section of accepted scene differences. Offline: the fixtures of test_tier_hi_journey.py (fake
# instance, fake tools, a stand-in for the original's play), test_validated_tier.py (fake instance, fake arm) and
# test_tier_carried.py (the results store).

import shutil  # noqa: E402
import store  # noqa: E402
import tierlib  # noqa: E402
import test_tier_hi_journey as THJ  # noqa: E402
import test_validated_tier as TV  # noqa: E402

TIER_STAGES = HERE.parent / 'tiers' / 'stages'


class M43Plan(unittest.TestCase):
    def test_the_43_civ_plan_is_column_i_s_row(self):
        names = [s[0] for s in tt.plans.stages('intermediate', 43)]
        self.assertEqual(names, ['survey', 'branches', 'testdata', 'lock', 'tierraise', 'build', 'narrowing', 'model',
                                 'purity', 'pack', 'wholegames', 'hi_journey', 'tierreport'])
        for gone in ('scenes', 'savetest', 'suites', 'oldexe', 'sweep', 'modpack', 'instance', 'fixchecks', 'offline'):
            self.assertNotIn(gone, names)
        # every stage of the plan names its minutes (rule 3); the three of the study's 43-civ row are its own
        self.assertEqual(sorted(names), sorted(tt.plans.INTERMEDIATE_M43_MINUTES))
        self.assertEqual(tt.plans.INTERMEDIATE_M43_MINUTES['wholegames'][0], 127)
        self.assertEqual(tt.plans.INTERMEDIATE_M43_MINUTES['hi_journey'][0], 42)

    def test_standard_and_hotfix_plans_unchanged_by_the_count(self):
        self.assertEqual(tt.plans.stages('intermediate'), tt.plans.stages('intermediate', 22))
        self.assertEqual(tt.plans.stages('hotfix', 43), tt.plans.stages('hotfix'))
        self.assertIn('oldexe', [s[0] for s in tt.plans.stages('intermediate', 22)])


class M43Arguments(unittest.TestCase):
    """take_tier.py with --major-civs 43: the 43-civ plan's names, and the base take of the same count only."""

    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = Path(self._td.name)
        keys = ('C5_PINS_DIR', 'C5_CLONES_DIR', 'C5_SETTINGS_FILE')
        self._env = {k: os.environ.get(k) for k in keys}
        os.environ.update(C5_PINS_DIR=str(self.tmp / 'pins'), C5_CLONES_DIR=str(self.tmp / 'clones'),
                          C5_SETTINGS_FILE=str(self.tmp / 'no-settings.ini'))
        self.lines = []

    def tearDown(self):
        for k, v in self._env.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
        self._td.cleanup()

    def base(self, major=None):
        b = self.tmp / f'base{major or 22}'
        b.mkdir(exist_ok=True)
        (b / 'state.json').write_text(json.dumps({'format': 1, 'tag': 'Release-9.9.8', 'stages': [], 'runs': [],
                                                  'config': {'major_civs': major} if major else {}}))
        return b

    def main(self, *argv):
        self.lines.clear()
        return tt.main(['Release-5.4.8', '--tier', 'intermediate', '--work', str(self.tmp / 'w'), *argv],
                       out=self.lines.append)

    def text(self):
        return '\n'.join(self.lines)

    def test_base_of_another_count_refused(self):
        rc = self.main('--major-civs', '43', '--base-take', str(self.base()))
        self.assertEqual(rc, tr.EXIT_ARGS)
        self.assertIn('a take of 22 major civilizations, this take has 43', self.text())
        # NEGATIVE of the check: a 43-civ base passes it (and is refused next as not accepted)
        rc = self.main('--major-civs', '43', '--base-take', str(self.base(43)))
        self.assertEqual(rc, tr.EXIT_ARGS)
        self.assertNotIn('major civilizations, this take has', self.text())
        self.assertIn('not an accepted release take', self.text())

    def test_a_standard_take_refuses_a_43_civ_base(self):
        rc = self.main('--base-take', str(self.base(43)))
        self.assertEqual(rc, tr.EXIT_ARGS)
        self.assertIn('a take of 43 major civilizations, this take has 22', self.text())

    def test_stage_names_of_the_43_civ_plan(self):
        rc = self.main('--major-civs', '43', '--only', 'scenes', '--base-take', str(self.base(43)))
        self.assertEqual(rc, tr.EXIT_ARGS)
        self.assertIn('--only scenes: not a stage of the intermediate tier', self.text())
        rc = self.main('--major-civs', '43', '--only', 'wholegames', '--base-take', str(self.base(43)))
        self.assertNotIn('not a stage', self.text())


class WholeGames43(unittest.TestCase):
    """The tier's wholegames in a 43-civ intermediate take: only l30 (the original's MAJOR_GAMES), passed as a subset."""

    def setUp(self):
        self.mod = tr.load_stage_module('wholegames', TIER_STAGES)
        self.orig = tierlib.original('wholegames')
        self._run = self.orig.run
        self.seen = []

    def tearDown(self):
        self.orig.run = self._run

    def ctx(self, tier, major):
        said = []
        return type('C', (), {'config': {'tier': tier, 'major_civs': major}, 'say': lambda s, *a: said.append(a)})()

    def test_subsets(self):
        self.assertEqual(self.mod.subset_of(self.ctx('intermediate', 43))[0], ('l30',))
        self.assertEqual(self.mod.subset_of(self.ctx('intermediate', 22)), (None, None))
        self.assertEqual(self.mod.subset_of(self.ctx('hotfix', 43))[0], ('tc',))
        self.assertEqual(self.mod.games(self.ctx('intermediate', 43)), ('l30',))
        self.assertEqual(self.mod.games(self.ctx('intermediate', 22)), ('tc', 'sf', 'sp', 'mp'))

    def fake(self, subset, parity='identical'):
        def run(ctx):
            self.seen.append(os.environ.get(self.orig.GAMES_SETTING))
            games = [{'name': g, 'parity': parity, 'runs_pass': True, 'majors': 30} for g in subset]
            return tr.StageResult.stop('subset', 'x', parity='passed on a subset', subset=list(subset), games=games)
        self.orig.run = run

    def test_l30_alone_is_the_43_civ_take_s_pass(self):
        self.fake(['l30'])
        res = self.mod.fresh(self.ctx('intermediate', 43))
        self.assertEqual(self.seen, ['l30'])
        self.assertIsNone(os.environ.get(self.orig.GAMES_SETTING))   # put back
        self.assertEqual(res.status, 'ok')
        self.assertEqual(res.outputs['tier_subset']['games'], ['l30'])
        self.assertTrue(res.outputs['tier_pass'])

    def test_negatives_stay_stops(self):
        self.fake(['l30', 'tc'])                         # not exactly the tier's subset
        self.assertEqual(self.mod.fresh(self.ctx('intermediate', 43)).status, 'stop')
        self.fake(['l30'], parity='different')           # x64 != x86
        self.assertEqual(self.mod.fresh(self.ctx('intermediate', 43)).status, 'stop')

    def test_standard_intermediate_runs_the_original_as_it_is(self):
        self.orig.run = lambda ctx: self.seen.append(os.environ.get(self.orig.GAMES_SETTING)) or tr.StageResult.ok()
        self.assertEqual(self.mod.fresh(self.ctx('intermediate', 22)).status, 'ok')
        self.assertEqual(self.seen, [None])


class OldExe(THJ.Base):
    """oldexe: the tier's hi_journey with release-basic, product only, on an EXE-4390913 instance, in <work>/oldexe."""

    def setUp(self):
        super().setUp()
        self.set_build('4390913')
        self._oenv = {k: os.environ.get(k) for k in ('C5_RELEASE_HI_JOURNEYS', 'C5_RELEASE_HI_BY',
                                                      'C5_RELEASE_OLDEXE_INSTANCE', 'C5_RELEASE_OLDEXE_BY')}
        for k in self._oenv:
            os.environ.pop(k, None)

    def tearDown(self):
        for k, v in self._oenv.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
        super().tearDown()

    def set_build(self, build):
        p = self.inst / 'instance.json'
        m = json.loads(p.read_text())
        m['exe_build'] = {'build': build, 'stamp': '0x1', 'size': 1}
        p.write_text(json.dumps(m))

    def fake_play(self, ctx):
        """The original's play, as THJ's stand-in, for the journeys and routes the environment names."""
        routes, inst = os.environ.get('C5_RELEASE_HI_ROUTES'), os.environ.get('C5_RELEASE_HI_INSTALLED')
        names = [n for n in (os.environ.get('C5_RELEASE_HI_JOURNEYS') or 'release-basic,release-large').split(',')]
        self.plays.append((routes, inst, ','.join(names), os.environ.get('C5_RELEASE_HI_EXE_BUILD')))
        js = {}
        for n in names:
            res = ctx.dir('hi_journey', 'runs') / f'{n}-{routes}.result.json'
            res.write_text('{}')
            rows = {routes: {'route': routes, 'journey': n, 'ran': True, 'exit': self.product_exit, 'took_s': 100,
                             'checkpoints': [{'name': 'c', 'ok': self.product_exit == 0}], 'result': str(res)}}
            js[n] = {'routes': rows, 'verdict': O_HI.journey_verdict(rows, [routes])}
        out = {'journeys': js, 'verdict': O_HI.overall(js), 'index': str(self.index), 'index_committed': True,
               'candidate': O_HI.candidate_of(ctx), 'layout': {'kind': O_HI.INSTALLED, 'instance': inst},
               'restore': {'ok': True}, 'claim_released': True}
        return tr.StageResult.ok(**O_HI.write_report(ctx, out))

    def go(self, for_owner=False, major=None):
        cfg = {'tier': 'intermediate', 'who': 'test5023', 'gate_wait': 0, 'base_take': str(self.base),
               'tier_stages_dir': str(TIER_STAGES)}
        if for_owner:
            cfg['for_owner'] = True
        if major:
            cfg['major_civs'] = major
        stages = [('build', '6', '5012'), ('pack', '-', '3504'), ('oldexe', '-', '5023')]
        d = tt.TierDriver(THJ.TAG, self.work, object(), cfg, stages, tools=self.tools, out=self.lines.append)
        return d.run(only='oldexe')

    def entry(self):
        return {e['name']: e for e in json.loads((self.work / 'state.json').read_text())['stages']}['oldexe']

    def test_release_basic_product_only_on_4390913_in_its_own_folder(self):
        self.assertEqual(self.go(), 0, self.lines)
        e = self.entry()
        self.assertEqual(e['status'], 'ok', e)
        o = e['outputs']
        self.assertEqual(o['verdict'], 'passed')
        self.assertEqual(self.plays, [('product', 'c5h-t', 'release-basic', '4390913')])   # no stock route at all
        self.assertEqual(o['stock']['mode'], 'not played')
        self.assertEqual(list(o['journeys']), ['release-basic'])
        self.assertNotIn('carried_parts', o)
        self.assertTrue(o['exe_build'].startswith('4390913'))
        claim = next(c for c in self.called('pool') if c.startswith('claim'))
        self.assertIn('--exe-build 4390913', claim)
        self.assertIn('--owner test5023-release-oldexe', claim)
        self.assertTrue(any('--action remove' in c for c in self.called('run_installer')))   # the removal ran
        self.assertTrue((self.work / 'oldexe' / 'report.json').is_file())
        self.assertTrue((self.work / 'oldexe' / 'installer').is_dir())
        self.assertTrue((self.work / 'oldexe' / 'runs' / 'release-basic-product.result.json').is_file())
        self.assertFalse((self.work / 'hi_journey').exists())                     # hi_journey's folder untouched
        for k in ('C5_RELEASE_HI_EXE_BUILD', 'C5_RELEASE_HI_JOURNEYS', 'C5_RELEASE_HI_BY'):
            self.assertIsNone(os.environ.get(k), k)                               # put back after the stage

    def test_a_26577625_instance_is_skipped_and_released(self):
        self.set_build('26577625')
        self.assertEqual(self.go(), 0, self.lines)
        e = self.entry()
        self.assertEqual(e['status'], 'skipped')
        self.assertIn('not 4390913', e['outputs']['not_played'])
        self.assertEqual(self.plays, [])
        self.assertTrue(any(c.startswith('release c5h-t') for c in self.called('pool')))

    def drop_build(self):
        p = self.inst / 'instance.json'
        m = json.loads(p.read_text())
        m.pop('exe_build', None)
        p.write_text(json.dumps(m))

    def test_an_instance_without_a_recorded_build_is_4390913_as_the_pool_says(self):
        # 5024's live check (2026-11-22): c5h-b (made before epic 0590) records no exe_build; the pool calls it 4390913
        self.drop_build()
        self.assertEqual(self.go(), 0, self.lines)
        e = self.entry()
        self.assertEqual(e['status'], 'ok', e)
        self.assertIn('none recorded', e['outputs']['exe_build_assumed'])
        self.assertEqual(self.plays, [('product', 'c5h-t', 'release-basic', '4390913')])
        # the constant is the pool's own
        import importlib.util
        spec = importlib.util.spec_from_file_location('pool_for_5024_test',
                                                      str(HERE.parents[2] / 'tools' / 'harness' / 'pool.py'))
        pool = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(pool)
        self.assertEqual(self.mod().POOL_DEFAULT_EXE_BUILD, pool.DEFAULT_EXE_BUILD)

    def test_a_26577625_stage_still_refuses_an_unrecorded_build(self):
        # negative control: a stage that wants 26577625 (hi_journey's default) never takes an unrecorded instance
        self.drop_build()
        self.go()   # loads oldexe's own copy of the tier hi_journey module (sys.modules 'tier_hi_journey_for_oldexe')
        self.calls.unlink()
        self.plays.clear()
        shutil.rmtree(self.work / 'oldexe', ignore_errors=True)
        mod = sys.modules['tier_hi_journey_for_oldexe']
        saved = mod.POOL_DEFAULT_EXE_BUILD
        mod.POOL_DEFAULT_EXE_BUILD = '26577625'   # the assumption no longer names the wanted build
        try:
            self.assertEqual(self.go(), 0, self.lines)
        finally:
            mod.POOL_DEFAULT_EXE_BUILD = saved
        e = self.entry()
        self.assertEqual(e['status'], 'skipped')
        self.assertIn('runs EXE build unknown, not 4390913', e['outputs']['not_played'])

    def test_the_release_instance_settings_are_not_used(self):
        os.environ.update(C5_RELEASE_INSTANCE='c5h-standard', C5_RELEASE_HI_INSTANCE='c5h-new-a')
        try:
            self.assertEqual(self.go(), 0, self.lines)
        finally:
            os.environ.pop('C5_RELEASE_INSTANCE', None)
            os.environ.pop('C5_RELEASE_HI_INSTANCE', None)
        claim = next(c for c in self.called('pool') if c.startswith('claim'))
        self.assertNotIn('--name', claim)
        os.environ['C5_RELEASE_OLDEXE_INSTANCE'] = 'c5h-t'
        self.calls.unlink()
        self.assertEqual(self.go(), 0, self.lines)
        claim = next(c for c in self.called('pool') if c.startswith('claim'))
        self.assertIn('--name c5h-t', claim)

    def test_failed_journey_reported_and_a_stop_on_a_take_for_owner(self):
        self.product_exit = 1
        self.assertEqual(self.go(), 0, self.lines)
        self.assertEqual(self.entry()['status'], 'ok')
        self.assertEqual(self.entry()['outputs']['verdict'], 'failed')
        self.calls.unlink()
        self.plays.clear()
        self.assertEqual(self.go(for_owner=True), tr.EXIT_STOP, self.lines)
        self.assertIn('did not pass', self.entry()['stop']['what'])

    def test_a_43_civ_take_skips_it(self):
        self.assertEqual(self.go(major=43), 0, self.lines)
        e = self.entry()
        self.assertEqual(e['status'], 'skipped')
        self.assertIn('standard take', e['outputs'].get('skipped_why') or e.get('error') or json.dumps(e))
        self.assertEqual(self.called('build_installer'), [])


O_HI = tierlib.original('hi_journey')


class InstanceTiny(TV.Fixture):
    """The intermediate tier's instance stage: the Tiny arm only, no refsave, carried from the store."""

    def setUp(self):
        super().setUp()
        self.orig = tierlib.original('instance')
        self._tiny = (self.orig.TINY_SAVE, self.orig.TINY_SHA256)
        self.orig.TINY_SAVE, self.orig.TINY_SHA256 = self.tiny, TV.sha(b'tiny save')
        self.results = self.tmp / 'results'
        self.repo = self.tmp / 'repo'
        for pat in store.TOOL_SOURCES['instance'] + ['tools/release/stages/instance.py']:
            p = self.repo / pat
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(f'source of {pat}\n'.encode())
        self.n = 0

    def tearDown(self):
        self.orig.TINY_SAVE, self.orig.TINY_SHA256 = self._tiny
        super().tearDown()

    def take(self, proxy=b'proxy dll', purity=b'# vp64-purity 1\n'):
        """A new work folder whose state has build, purity and pack ok (no refsave)."""
        self.n += 1
        w = self.tmp / f'take{self.n}'
        (w / 'inputs' / 'player-x86').mkdir(parents=True)
        (w / 'inputs' / 'player-x86' / 'CvGameCore_Expansion2.dll').write_bytes(b'player dll')
        pt = w / 'purity.tsv'
        pt.write_bytes(purity)
        b = dict(self.build, x64_hashes={
            'bin/CvGameCore_Expansion2.dll': {'sha256': TV.sha(b'x64 dll')},
            'bin/server.exe': {'sha256': TV.sha(b'server')},
            'proxy/bin/CvGameCore_Expansion2.dll': {'sha256': TV.sha(proxy)}},
            i0ia_dll_sha256=TV.sha(b'x86 instance-aware'))
        st = {'format': tr.FORMAT, 'tag': TV.TAG, 'created': 'x', 'config': {}, 'runs': [], 'stages': [
            {'name': n, 'cl_step': '-', 'status': 'ok', 'outputs': o, 'inputs_sha': 'seed'}
            for n, o in (('build', b), ('purity', {'purity_tsv': str(pt)}), ('pack', {'pack': 'p'}))]}
        (w / 'state.json').write_text(json.dumps(st))
        return w

    def stage(self, work):
        self.lines.clear()
        cfg = {'tier': 'intermediate', 'who': 'test5023', 'gate_wait': 0, 'results': str(self.results),
               'tools_repo': str(self.repo), 'base_take': str(work)}
        stages = [('build', '6', 'x'), ('purity', '11', 'x'), ('pack', '-', 'x'), ('instance', '16-17', '5023')]
        d = tt.TierDriver(TV.TAG, work, self.desc, cfg, stages, tools=self.tools, out=self.lines.append)
        return d.run(only='instance')

    def entry(self, w):
        return {e['name']: e for e in json.loads((w / 'state.json').read_text())['stages']}['instance']

    def arms(self):
        return [c.split()[1] for c in self.called('arm')]

    def test_tiny_arm_only_without_refsave_then_carried_then_fresh(self):
        w1 = self.take()
        self.assertEqual(self.stage(w1), 0, self.text())
        o = self.entry(w1)['outputs']
        self.assertEqual(self.arms(), ['rel-tiny'])                       # the huge arm is not played (R8)
        self.assertEqual(list(o['arms']), ['tiny'])
        self.assertTrue(o['arms']['tiny']['ok'])
        self.assertTrue(o['restore']['ok'])
        self.assertTrue(o['claim_released'])
        self.assertIn('R8', o['dropped'])
        rec = json.loads(Path(o['store']).read_text())
        self.assertEqual(rec['scope'], {'games': ['tiny']})
        self.assertEqual(rec['parts']['proxy_dll'], TV.sha(b'proxy dll'))
        self.assertEqual(rec['parts']['purity_list'], TV.sha(b'# vp64-purity 1\n'))
        # the same bytes: carried, nothing played
        w2 = self.take()
        self.assertEqual(self.stage(w2), 0, self.text())
        self.assertIn('carried', self.entry(w2)['outputs'])
        self.assertEqual(self.arms(), ['rel-tiny'])
        # NEGATIVE: one byte of the proxy DLL, then of the purity list: fresh, and said why
        w3 = self.take(proxy=b'proxy dlL')
        self.assertEqual(self.stage(w3), 0, self.text())
        self.assertIn('proxy_dll', self.entry(w3)['outputs']['fresh_why'])
        w4 = self.take(purity=b'# vp64-purity 1\nx\n')
        self.assertEqual(self.stage(w4), 0, self.text())
        self.assertIn('purity_list', self.entry(w4)['outputs']['fresh_why'])
        self.assertEqual(self.arms(), ['rel-tiny'] * 3)

    def test_a_validated_take_with_both_arms_covers_the_tiny_arm(self):
        w1 = self.take()
        self.assertEqual(self.stage(w1), 0, self.text())
        rp = next((self.results / 'instance').glob('*.json'))
        rec = json.loads(rp.read_text())
        rec['scope'] = {'games': ['huge', 'tiny']}
        rp.write_text(json.dumps(rec))
        w2 = self.take()
        self.assertEqual(self.stage(w2), 0, self.text())
        self.assertIn('carried', self.entry(w2)['outputs'])
        rec['scope'] = {'games': ['huge']}                                 # NEGATIVE: a record without the Tiny arm
        rp.write_text(json.dumps(rec))
        w3 = self.take()
        self.assertEqual(self.stage(w3), 0, self.text())
        self.assertIn("needs ['tiny']", self.entry(w3)['outputs']['fresh_why'])

    def test_failed_arm_stops_and_is_not_recorded(self):
        self.setenv(FAKE_ARM_MISMATCH='tiny')
        w = self.take()
        self.assertEqual(self.stage(w), tr.EXIT_STOP, self.text())
        e = self.entry(w)
        self.assertIn('the live route on c5h-t failed: tiny', e['stop']['what'])
        self.assertTrue(e['outputs']['restore']['ok'])                    # restored all the same
        self.assertFalse((self.results / 'instance').exists())

    def test_changed_tiny_save_stops_before_a_claim(self):
        self.tiny.write_bytes(b'other')
        w = self.take()
        self.assertEqual(self.stage(w), tr.EXIT_STOP, self.text())
        self.assertIn('Tiny live save', self.entry(w)['stop']['what'])
        self.assertEqual(self.called('pool'), [])

    def test_no_refsave_dependency(self):
        mod = tr.load_stage_module('instance', TIER_STAGES)
        self.assertEqual(mod.DEPENDS, ['build'])
        self.assertNotIn('refsave', mod.USES)
        self.assertEqual(store.KEYS['instance'][:6], ['x64_dll', 'server_exe', 'x64_bin_other', 'proxy_dll', 'i0ia_dll',
                                                      'purity_list'])


class AcceptedScenesReport(unittest.TestCase):
    """tierreport's section "Accepted scene differences" (ticket 5023, the lead's ruling on 5019's question)."""

    def setUp(self):
        self.rep = tr.load_stage_module('tierreport', TIER_STAGES)

    def state(self, scenes):
        return {'tag': 'Release-9.9.9', 'config': {'tier': 'intermediate'}, 'runs': [], 'stages': [
            {'name': 'build', 'status': 'ok', 'outputs': {}}, {'name': 'scenes', **scenes}]}

    def test_listed_with_reason_who_and_date(self):
        acc = [{'bundle': 'B1', 'shot': '1002', 'pixels': '12 pixels differ, largest 40', 'reason': 'river | edge',
                'who': 'owner', 'date': '2026-11-22'}]
        md = self.rep.markdown(self.rep.build(self.state({'status': 'ok', 'outputs': {
            'accepted': acc, 'accept_file': 'w/scenes/accept.json'}}), ['build', 'scenes']))
        self.assertIn('## Accepted scene differences', md)
        self.assertIn('| B1 | 1002 | 12 pixels differ, largest 40 | river / edge | owner | 2026-11-22 |', md)
        self.assertIn('Accept file: w/scenes/accept.json', md)

    def test_none_said_and_no_section_without_the_stage(self):
        md = self.rep.markdown(self.rep.build(self.state({'status': 'ok', 'outputs': {'accepted': []}}),
                                              ['build', 'scenes']))
        self.assertIn('None: no scene difference was accepted in this take.', md)
        carried = self.state({'status': 'ok', 'outputs': {'carried': {'line': 'c'}, 'accepted': [{'bundle': 'X'}]}})
        md = self.rep.markdown(self.rep.build(carried, ['build', 'scenes']))
        self.assertIn('None: no scene difference was accepted', md)       # a carried result accepted nothing here
        md = self.rep.markdown(self.rep.build(self.state({'status': 'ok', 'outputs': {}}), ['build']))
        self.assertNotIn('Accepted scene differences', md)


if __name__ == '__main__':
    unittest.main()
