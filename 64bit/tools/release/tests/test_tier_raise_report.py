"""Hermetic tests of the tier stages tierraise and tierreport (epic 0630, ticket 5010): the automatic raise on each of
its four causes and on an unreadable base (negative tests), and the report's carried section (a carried result is never
called a run).

    python -I -X utf8 tools/release/tests/test_tier_raise_report.py
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

TR = tr.load_stage_module('tierraise', HERE.parent / 'tiers' / 'stages')
REP = tr.load_stage_module('tierreport', HERE.parent / 'tiers' / 'stages')
COMP = {'clang-cl': 'clang version 19.1.0', 'lld-link': 'LLD 19.1.0'}


class Ctx:
    def __init__(self, config, outs, desc_file):
        self.config = config
        self._outs = outs
        self.desc = type('D', (), {'file': desc_file})()

    def outputs(self, n):
        return self._outs[n]

    def outputs_if_ok(self, n):
        return self._outs.get(n)


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

    def write_base(self, carry='pins', lock=None, compiler=COMP, exe='26577625', desc=None):
        st = {'tag': 'Release-9.9.9', 'config': {'carry': carry}, 'stages': [
            {'name': 'survey', 'status': 'ok', 'outputs': self.survey},
            {'name': 'testdata', 'status': 'ok', 'outputs': {'descriptor': str(desc or self.desc)}},
            {'name': 'lock', 'status': 'ok', 'outputs': lock or self.lock},
            {'name': 'tierraise', 'status': 'ok', 'outputs': {'now': {'compiler': compiler, 'exe_build': exe}}}]}
        (self.base / 'state.json').write_text(json.dumps(st))

    def ctx(self, carry='pins', lock=None, compiler=COMP):
        return Ctx({'base_take': str(self.base), 'carry': carry, 'compiler_lines': compiler},
                   {'survey': self.survey, 'lock': lock or self.lock, 'testdata': {'descriptor': str(self.desc)}},
                   str(self.desc))

    def test_all_equal_stays_hotfix(self):
        r = TR.run(self.ctx())
        self.assertEqual(r.status, 'ok', r.what)
        self.assertEqual(r.outputs['raised'], [])

    def test_dll_pin_changed_raises(self):
        r = TR.run(self.ctx(lock={'x64': {'commit': '9' * 40}, 'i0': {'commit': '2' * 40}}))
        self.assertEqual(r.status, 'stop')
        self.assertIn('raised to the intermediate tier: dll_pin changed', r.what)

    def test_series_take_dll_change_is_not_a_pin_change(self):
        self.write_base(carry='series')
        r = TR.run(self.ctx(carry='series', lock={'x64': {'commit': '9' * 40}, 'i0': {'commit': '8' * 40}}))
        self.assertEqual(r.status, 'ok', r.what)

    def test_carry_mode_changed_raises(self):
        self.write_base(carry='series')
        r = TR.run(self.ctx(carry='pins'))
        self.assertEqual(r.status, 'stop')
        self.assertIn('dll_pin', r.what)

    def test_compiler_changed_raises(self):
        r = TR.run(self.ctx(compiler={'clang-cl': 'clang version 20.1.0', 'lld-link': 'LLD 19.1.0'}))
        self.assertEqual(r.status, 'stop')
        self.assertIn('compiler changed', r.what)

    def test_compiler_unreadable_raises(self):
        r = TR.run(self.ctx(compiler={'clang-cl': None, 'lld-link': 'LLD 19.1.0'}))
        self.assertEqual(r.status, 'stop')
        self.assertIn('compiler cannot be read on this take', r.what)

    def test_vp_release_changed_raises(self):
        other = self.tmp / 'other.json'
        other.write_text(json.dumps({'tag': 'Release-9.9.9', 'upstream_commit': 'b' * 40,
                                     'mods': [{'folder': 'x', 'version': 1}]}))
        self.write_base(desc=other)
        r = TR.run(self.ctx())
        self.assertEqual(r.status, 'stop')
        self.assertIn('vp_release changed', r.what)

    def test_exe_build_changed_raises(self):
        os.environ['C5_TIER_EXE_BUILD'] = '4390913'
        r = TR.run(self.ctx())
        self.assertEqual(r.status, 'stop')
        self.assertIn('exe_build changed', r.what)

    def test_base_without_records_raises_on_each(self):
        (self.base / 'state.json').write_text(json.dumps({'tag': 'Release-9.9.9', 'config': {}, 'stages': []}))
        r = TR.run(self.ctx())
        self.assertEqual(r.status, 'stop')
        for what in ('dll_pin cannot be read on the base take', 'compiler cannot be read on the base take',
                     'exe_build cannot be read on the base take'):
            self.assertIn(what, r.what)

    def test_base_exe_stated_by_operator_is_named(self):
        st = json.loads((self.base / 'state.json').read_text())
        st['stages'] = [s for s in st['stages'] if s['name'] != 'tierraise'] + [
            {'name': 'tierraise', 'status': 'ok', 'outputs': {'now': {'compiler': COMP}}}]
        (self.base / 'state.json').write_text(json.dumps(st))
        os.environ['C5_TIER_BASE_EXE_BUILD'] = '26577625'
        r = TR.run(self.ctx())
        self.assertEqual(r.status, 'ok', r.what)
        self.assertIn('stated by the operator', r.outputs['notes']['exe_build'])


class Report(unittest.TestCase):
    def state(self):
        return {'tag': 'Release-9.9.9', 'config': {'tier': 'hotfix', 'base_take': 'B'}, 'runs': [{}],
                'tier_stages': ['build', 'savetest', 'wholegames', 'hi_journey', 'tierreport'],
                'stages': [
                    {'name': 'build', 'status': 'ok', 'started': '2026-11-21 10:00:00', 'ended': '2026-11-21 10:12:00',
                     'outputs': {}},
                    {'name': 'savetest', 'status': 'ok', 'started': '2026-11-21 10:12:00',
                     'ended': '2026-11-21 10:12:01', 'outputs': {'carried': {
                         'from_take': 'Release-9.9.9 C:/w/base', 'line': 'carried from Release-9.9.9, x64 DLL abc equal'}}},
                    {'name': 'wholegames', 'status': 'ok', 'started': '2026-11-21 10:12:01',
                     'ended': '2026-11-21 10:40:01', 'outputs': {'fresh_why': 'x64 DLL differs',
                                                                  'tier_subset': {'games': ['tc'], 'why': 'D221'}}},
                    {'name': 'hi_journey', 'status': 'ok', 'started': '2026-11-21 10:40:01',
                     'ended': '2026-11-21 11:10:01', 'outputs': {'carried_parts': {'stock': {
                         'from_take': 'T', 'line': 'carried from T, trace x equal'}}}},
                ]}

    def test_carried_listed_apart_never_as_run(self):
        r = REP.build(self.state())
        self.assertEqual([c['stage'] for c in r['carried']], ['savetest', 'hi_journey/stock'])
        md = REP.markdown(r)
        run_part = md.split('## Carried results')[0]
        self.assertNotIn('| savetest |', run_part)
        self.assertIn('| wholegames | ok | 28.0 |', run_part)
        self.assertIn('carried from Release-9.9.9, x64 DLL abc equal', md)
        self.assertEqual(r['wall_minutes_first_start_to_last_end'], 70.0)
        self.assertIn('hotfix tier reached', r['verdict'])
        self.assertEqual(r['fresh_although_carriable'][0]['why'], 'x64 DLL differs')
        self.assertEqual(r['reduced'][0]['games'], ['tc'])

    def test_gate_line_pass(self):
        r = REP.build(self.state())
        self.assertEqual(r['gate'], 'GATE PASS (hotfix tier, Release-9.9.9): build PASS, savetest CARRIED, '
                                    'wholegames PASS, hi_journey PASS')
        self.assertIn('**GATE PASS', REP.markdown(r))

    def test_gate_line_fail_names_stage_and_first_checkpoint(self):
        st = self.state()
        st['stages'][3].update(status='failed', error='a journey failed')
        st['stages'][3]['outputs']['journeys'] = {
            'release-basic': {'routes': {'product': {'ran': True, 'exit': 0, 'checkpoints': [{'name': 'a', 'ok': True}]},
                                         'stock': {'carried': {'from_take': 'T'}}}},
            'release-large': {'routes': {'product': {'ran': True, 'exit': 1, 'checkpoints': [
                {'name': 'city-founded', 'ok': True}, {'name': 'turn-ended', 'ok': False}]}}}}
        g = REP.build(st)['gate']
        self.assertTrue(g.startswith('GATE FAIL (hotfix tier, Release-9.9.9) at hi_journey (failed): a journey failed; '
                                     'first failing checkpoint: release-large product: checkpoint turn-ended.'), g)
        self.assertIn('Nothing is handed over', g)
        self.assertIn('hi_journey FAIL', g)

    def test_gate_line_fails_when_d162_says_no(self):
        st = self.state()
        st['stages'][3]['outputs']['owner'] = {'allowed': False, 'why': ['release-basic was not played']}
        g = REP.build(st)['gate']
        self.assertIn('at hi_journey (ok): D162 for the owner: no (release-basic was not played)', g)

    def test_gate_line_reads_the_answer_of_a_state_written_before_0708(self):
        st = self.state()
        st['stages'][3]['outputs']['art'] = {'allowed': False, 'why': ['release-basic was not played']}
        g = REP.build(st)['gate']
        self.assertIn('at hi_journey (ok): D162 for the owner: no (release-basic was not played)', g)

    def test_stop_is_the_verdict(self):
        st = self.state()
        st['stages'][2].update(status='stop', stop={'what': 'parity failed'})
        r = REP.build(st)
        self.assertEqual(r['verdict'], 'NOT reached: wholegames is stop (parity failed)')

    def test_no_carry_says_so(self):
        st = self.state()
        st['stages'][1]['outputs'] = {}
        st['stages'][3]['outputs'] = {}
        self.assertIn('None: every check of this take was run.', REP.markdown(REP.build(st)))


if __name__ == '__main__':
    unittest.main()
