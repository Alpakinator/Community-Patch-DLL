"""Hermetic tests of issue 0372: the wholegames stage with --major-civs above 22 (a game of more than 22 majors on both
arms, the major count in the report, the refusal of a run that played only 22-slot games), the `local` host and
C5_RELEASE_WHOLEGAMES_GAMES. The fixture of test_validated_tier.py: fake testbed, fake tools, a temporary folder; c5h's
resolver is replaced by a stand-in. Nothing starts a game, a server or a testbed job.

    python -I -X utf8 -m unittest discover -s tools/release/tests -p "test_validated_tier*.py" -v
"""
import json
import os
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
import take_release as tr  # noqa: E402
from test_validated_tier import FAKE, PY, TAG, Fixture, sha  # noqa: E402

SPEC = 'server/tests/specs/continents-large-30majors-fixed-seed.json'
# The plan of every run before 0372 (two hosts, round robin): kept for 22 byte for byte, so that no ok stage reruns.
PLAN_22 = {'oldpc': [['tc', 'new:TINY:Continents:5051:5253'], ['sp', 'new:STANDARD:Pangaea:3333:4444']],
           'laptop': [['sf', 'new:STANDARD:Fractal:5757:6868'], ['mp', 'new:SMALL:Pangaea:1819:2021']]}


class Major(Fixture):
    def setUp(self):
        super().setUp()
        self.resolved = []
        self.major = None
        # a stand-in c5h kit (bin/civ5headless.exe) for the `local` host
        self.c5h = self.tmp / 'c5h'
        (self.c5h / 'bin').mkdir(parents=True)
        (self.c5h / 'bin' / 'civ5headless.exe').write_bytes(b'host')

    def driver(self, only=None):
        stages = tr.tier_stages('validated')[len(tr.CANDIDATE):]
        cfg = {'tier': 'validated', 'who': 'test0372', 'gate_wait': 0}
        if self.major is not None:
            cfg['major_civs'] = self.major
        d = tr.Driver(TAG, self.work, self.desc, cfg, stages, tools=self.tools, out=self.lines.append)
        inst = d.module('instance')
        inst.TINY_SAVE, inst.TINY_SHA256 = self.tiny, sha(b'tiny save')
        wg = d.module('wholegames')
        wg.LONGGAMES = [PY, '-I', str(FAKE), 'longgames']
        wg.C5H = self.c5h

        def fake_resolve(ctx, spec_rel, majors, n, out):
            if os.environ.get('FAKE_RESOLVE_FAIL'):
                raise ValueError('resolved spec has majors 21 and 22 slots')
            self.resolved.append((spec_rel, majors, n))
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_text(json.dumps({'specVersion': 1, 'majors': majors, 'slots': [{}] * n}))
            return out
        wg.resolve_x86_spec = fake_resolve
        self.wg = wg
        return d

    def go_wg(self, major=None):
        self.major = major
        return self.go(only='wholegames')

    def wg_state(self):
        return self.state()['wholegames']

    def submits(self):
        return [c for c in self.called('tb') if ' submit ' in c]

    def ctx(self, major=None):
        d = self.driver()
        if major is not None:
            d.config['major_civs'] = major
        return tr.Ctx(d, 'wholegames')

    # --- the default path did not move
    def test_22_inputs_and_plan_unchanged(self):
        inp = self.driver().module('wholegames').inputs(self.ctx())
        self.assertEqual(json.loads(json.dumps(inp)), {
            'plan': PLAN_22, 'to': 1000, 'exceptions': ['ReligionBeliefYields.IsReligionOwner']})
        self.assertEqual(json.loads(json.dumps(self.wg.inputs(self.ctx(22))))['plan'], PLAN_22)

    def test_22_run_records_majors_and_passes(self):
        self.assertEqual(self.go_wg(), 0, self.text())
        o = self.wg_state()['outputs']
        self.assertEqual(o['parity'], 'passed')
        self.assertEqual(o['major_civs'], 22)
        self.assertEqual(o['majors_played'], 8)
        self.assertNotIn('spec:', ' '.join(self.submits()))
        self.assertEqual(self.resolved, [])
        self.assertIn('majors_played: 8', (self.work / 'validated-report.md').read_text(encoding='utf-8'))

    # --- 43: the 30-major game on both arms
    def test_43_plays_the_30_major_game_on_both_arms(self):
        self.assertEqual(self.go_wg(43), 0, self.text())
        o = self.wg_state()['outputs']
        self.assertEqual(o['parity'], 'passed')
        self.assertEqual((o['major_civs'], o['majors_played']), (43, 30))
        l30 = next(g for g in o['games'] if g['name'] == 'l30')
        self.assertEqual((l30['majors'], l30['majors_planned'], l30['x64']['majors'], l30['x86']['majors']),
                         (30, 30, 30, 30))
        self.assertEqual((l30['rows_x64'], l30['rows_x86']), (100, 100))
        self.assertEqual(len(o['games']), 5)
        self.assertEqual(self.resolved, [(SPEC, 30, 43)])
        sub = next(c for c in self.submits() if 'l30-x64' in c)
        self.assertIn(f'l30-x64,x64,spec:{SPEC},1000', sub)
        self.assertIn('l30-x86,x86,spec:%TESTBED_BIN%/rel/specs/l30-x86.json,1000', sub)
        self.assertIn('=rel/specs/l30-x86.json', sub)
        self.assertIn('l30-x86.json', o['x86_specs']['l30']['path'])
        other = next(c for c in self.submits() if 'l30-x64' not in c)
        self.assertNotIn('rel/specs', other)
        md = (self.work / 'validated-report.md').read_text(encoding='utf-8')
        self.assertIn('majors_played: 30', md)
        self.assertIn('"majors": 30', md)

    def test_43_inputs_carry_the_count_and_the_major_game_first(self):
        ctx = self.ctx(43)
        inp = self.wg.inputs(ctx)
        self.assertEqual(inp['major_civs'], 43)
        self.assertEqual(inp['plan']['oldpc'][0], ('l30', f'spec:{SPEC}', 30))

    def test_43_subset_without_a_major_game_is_refused_before_playing(self):
        self.setenv(C5_RELEASE_WHOLEGAMES_GAMES='tc,sf')
        self.assertEqual(self.go_wg(43), 10, self.text())
        e = self.wg_state()
        self.assertIn('no whole game of the plan seats more than 22 majors', e['stop']['what'])
        self.assertEqual(self.submits(), [])
        self.assertNotEqual(self.report()['verdict'], 'validated')

    def test_major_count_with_no_game_that_fits_is_refused(self):
        # l30 needs 31 slots or more (autoplay keeps a major slot for the observer)
        self.assertEqual(self.go_wg(30), 10, self.text())
        self.assertIn('no whole game of the plan', self.wg_state()['stop']['what'])
        self.assertEqual(self.submits(), [])

    def test_43_that_played_only_22_slot_games_is_refused(self):
        self.setenv(FAKE_TB_MAJORS='l30-x64=22,l30-x86=22')
        self.assertEqual(self.go_wg(43), 10, self.text())
        e = self.wg_state()
        self.assertIn('every whole game played seated 22 majors or fewer', e['stop']['what'])
        self.assertEqual(e['outputs']['parity'], 'incomplete')
        self.assertNotEqual(self.report()['verdict'], 'validated')

    def test_one_arm_seated_22_is_refused(self):
        self.setenv(FAKE_TB_MAJORS='l30-x86=22')
        self.assertEqual(self.go_wg(43), 10, self.text())
        self.assertIn('every whole game played seated 22 majors or fewer', self.wg_state()['stop']['what'])

    def test_wrong_count_is_a_stop(self):
        self.setenv(FAKE_TB_MAJORS='l30-x64=29,l30-x86=29')
        self.assertEqual(self.go_wg(43), 10, self.text())
        what = self.wg_state()['stop']['what']
        self.assertIn('another number of majors than its spec', what)
        self.assertIn('x64 29, x86 29, spec 30', what)

    def test_difference_in_the_30_major_game_is_parity_failed(self):
        self.setenv(FAKE_TB_DIFF='l30')
        self.assertEqual(self.go_wg(43), 10, self.text())
        self.assertEqual(self.wg_state()['outputs']['parity'], 'failed')
        self.assertEqual(self.report()['verdict'], 'candidate, parity failed')

    def test_failed_30_major_run_is_a_stop(self):
        self.setenv(FAKE_TB_RUNFAIL='l30-x86')
        self.assertEqual(self.go_wg(43), 10, self.text())
        self.assertIn('did not finish', self.wg_state()['stop']['what'])

    def test_unresolvable_x86_spec_fails_before_submitting(self):
        self.setenv(FAKE_RESOLVE_FAIL='1')
        self.assertEqual(self.go_wg(43), 1, self.text())
        self.assertEqual(self.submits(), [])
        self.assertIn('could not be resolved', self.text())

    # --- C5_RELEASE_WHOLEGAMES_GAMES
    def test_subset_is_a_stop_never_validated(self):
        self.setenv(C5_RELEASE_WHOLEGAMES_GAMES='l30')
        self.assertEqual(self.go_wg(43), 10, self.text())
        e = self.wg_state()
        self.assertEqual(e['outputs']['parity'], 'passed on a subset')
        self.assertEqual(e['outputs']['subset'], ['l30'])
        self.assertIn('a subset is never validated', e['stop']['what'])
        self.assertEqual(len(self.submits()), 1)
        self.assertNotIn('tc-x64', self.submits()[0])
        rep = self.report()
        self.assertIn('not reached', rep['verdict'])
        self.assertEqual(rep['stages']['wholegames']['status'], 'stop')
        self.assertIn('a subset is never validated', rep['stages']['wholegames']['what'])
        md = (self.work / 'validated-report.md').read_text(encoding='utf-8')
        self.assertIn('| wholegames | 14 | stop | only the games', md)
        self.assertIn('parity: passed on a subset', md)

    def test_whole_tier_with_a_subset_names_it_in_the_verdict(self):
        self.setenv(C5_RELEASE_WHOLEGAMES_GAMES='l30')
        self.major = 43
        self.assertEqual(self.go(), 10, self.text())
        self.assertEqual(self.state()['refsave']['status'], 'ok')
        v = self.report()['verdict']
        self.assertTrue(v.startswith('candidate, validated tier not reached (wholegames stop: only the games'), v)
        self.assertIn('a subset is never validated', v)

    def test_whole_tier_43_is_validated(self):
        self.major = 43
        self.assertEqual(self.go(), 0, self.text())
        self.assertEqual(self.report()['verdict'], 'validated')
        self.assertEqual(self.report()['stages']['wholegames']['outputs']['majors_played'], 30)

    def test_unknown_subset_name_fails(self):
        self.setenv(C5_RELEASE_WHOLEGAMES_GAMES='tc,zz')
        self.assertEqual(self.go_wg(), 1, self.text())
        self.assertIn("unknown or no games ['zz']", self.text())
        self.setenv(C5_RELEASE_WHOLEGAMES_GAMES='l30')   # l30 is not a game of a 22 run
        self.assertEqual(self.go_wg(), 1, self.text())

    # --- the `local` host
    def test_local_host_runs_longgames_here(self):
        self.setenv(C5_RELEASE_TESTBEDS='local', C5_RELEASE_WHOLEGAMES_GAMES='l30')
        self.assertEqual(self.go_wg(43), 10, self.text())
        self.assertEqual(self.submits(), [])
        calls = self.called('longgames')
        self.assertEqual(len(calls), 1)
        spec86 = (self.work / 'wholegames' / 'specs' / 'l30-x86.json').as_posix()
        self.assertIn(f'l30-x86,x86,spec:{spec86},1000', calls[0])
        self.assertIn(f'--x64-bin {self.build["x64_bin"]}', calls[0])
        self.assertIn(f'--x86-dll {self.build["i0_dll"]}', calls[0])
        envs = [json.loads(x) for x in (self.tmp / 'longgames-env.jsonl').read_text().splitlines()]
        self.assertEqual(envs[0]['TESTBED_DB'], str(self.desc.test_db))
        self.assertEqual(envs[0]['TESTBED_HOST'], 'local')
        o = self.wg_state()['outputs']
        self.assertEqual(o['parity'], 'passed on a subset')
        self.assertEqual(o['majors_played'], 30)
        self.assertIn('local', o['local'])

    def test_local_job_that_ended_is_reused_by_a_rerun(self):
        self.setenv(C5_RELEASE_TESTBEDS='local', C5_RELEASE_WHOLEGAMES_GAMES='l30')
        self.assertEqual(self.go_wg(43), 10, self.text())
        self.assertEqual(self.go_wg(43), 10, self.text())
        self.assertEqual(len(self.called('longgames')), 1)
        self.assertIn('ended; reading it', self.text())

    def test_local_job_with_a_failed_run_is_played_again(self):
        self.setenv(C5_RELEASE_TESTBEDS='local', C5_RELEASE_WHOLEGAMES_GAMES='l30', FAKE_TB_RUNFAIL='l30-x86',
                    FAKE_TB_NOSTATS='l30')
        self.assertEqual(self.go_wg(43), 10, self.text())
        os.environ.pop('FAKE_TB_RUNFAIL')
        os.environ.pop('FAKE_TB_NOSTATS')
        self.assertEqual(self.go_wg(43), 10, self.text())
        self.assertEqual(len(self.called('longgames')), 2)
        self.assertEqual(self.wg_state()['outputs']['parity'], 'passed on a subset')

    def test_local_job_without_results_fails(self):
        self.setenv(C5_RELEASE_TESTBEDS='local', FAKE_LOCAL_CRASH='1')
        self.assertEqual(self.go_wg(), 1, self.text())
        self.assertIn('local longgames.py exit 3', self.text())

    def test_two_local_jobs_and_a_testbed(self):
        self.setenv(C5_RELEASE_TESTBEDS='local1,local2,laptop')
        self.assertEqual(self.go_wg(43), 0, self.text())
        self.assertEqual(len(self.called('longgames')), 2)
        self.assertEqual(len(self.submits()), 1)
        o = self.wg_state()['outputs']
        self.assertEqual({g['host'] for g in o['games']}, {'local1', 'local2', 'laptop'})
        self.assertEqual(next(g['host'] for g in o['games'] if g['name'] == 'l30'), 'local1')
        self.assertEqual(o['parity'], 'passed')

    def test_local_host_gives_c5h_its_host_exe(self):
        self.setenv(C5_RELEASE_TESTBEDS='local')
        self.assertEqual(self.go_wg(), 0, self.text())
        envs = [json.loads(x) for x in (self.tmp / 'longgames-env.jsonl').read_text().splitlines()]
        self.assertEqual(envs[0]['TESTBED_C5H_EXE'], str(self.c5h / 'bin' / 'civ5headless.exe'))

    def test_local_without_a_c5h_host_fails_before_running(self):
        self.setenv(C5_RELEASE_TESTBEDS='local')
        (self.c5h / 'bin' / 'civ5headless.exe').unlink()
        self.assertEqual(self.go_wg(), 1, self.text())
        self.assertIn('no civ5headless.exe', self.text())
        self.assertEqual(self.called('longgames'), [])

    def test_c5h_exe_repository_then_kit(self):
        self.driver()
        repo = self.tmp / 'c5hrepo'
        build = repo / 'out' / 'Release' / '20261027-120431'
        build.mkdir(parents=True)
        (build / 'civ5headless.exe').write_bytes(b'host')
        (repo / 'out' / 'latest.txt').write_text(str(build) + '\n')
        self.wg.C5H = repo
        self.assertEqual(self.wg.c5h_exe(), build / 'civ5headless.exe')    # the workstation: a repository
        self.wg.C5H = self.c5h
        self.assertEqual(self.wg.c5h_exe(), self.c5h / 'bin' / 'civ5headless.exe')   # a testbed: a kit
        self.wg.C5H = self.tmp / 'nothing'
        self.assertIsNone(self.wg.c5h_exe())

    def test_failed_run_without_stats_is_incomplete_not_parity_failed(self):
        # the 2026-11-15 live run: the x86 host did not start, so no stats.db and longgames' compare failed
        self.setenv(FAKE_TB_RUNFAIL='l30-x86', FAKE_TB_NOSTATS='l30')
        self.assertEqual(self.go_wg(43), 10, self.text())
        e = self.wg_state()
        self.assertEqual(e['outputs']['parity'], 'incomplete')
        self.assertIn('did not finish or were not compared', e['stop']['what'])
        self.assertNotEqual(self.report()['verdict'], 'candidate, parity failed')

    def test_compare_error_with_both_runs_passed_stays_a_difference(self):
        self.setenv(FAKE_TB_NOSTATS='l30')
        self.assertEqual(self.go_wg(43), 10, self.text())
        self.assertEqual(self.wg_state()['outputs']['parity'], 'failed')

    def test_local_host_name_rule(self):
        self.driver()
        self.assertTrue(all(self.wg.is_local(h) for h in ('local', 'local1', 'local12')))
        self.assertFalse(any(self.wg.is_local(h) for h in ('oldpc', 'laptop', 'locale', 'mylocal')))


if __name__ == '__main__':
    unittest.main()
