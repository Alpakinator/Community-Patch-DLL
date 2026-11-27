"""Hermetic tests of the CARRIED tier stages (epic 0630, ticket 5011): tools/release/tiers/stages/{savetest, wholegames,
narrowing, census, model, purity}.py on the results store (tools/release/tiers/store.py), run by the tier's driver.
Fake run_d10 (written here) and the fake testbed of fake_validated_tool.py; the build and lock stages are recorded as
ok in each take's state beforehand. No gate, lock, build, game, testbed or instance is touched.

    python -I -X utf8 tools/release/tests/test_tier_carried.py
"""
import hashlib
import json
import os
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE.parent / 'tiers'))
import descriptor  # noqa: E402
import take_release as tr  # noqa: E402
import take_tier as tt  # noqa: E402
import store  # noqa: E402

PY = sys.executable
FAKE = HERE / 'fake_validated_tool.py'
TAG = 'Release-9.9.9'
DLL = 'CvGameCore_Expansion2.dll'
COUNTS = {'tiny': 30666, 'standard': 351496, 'huge': 683123}

FAKE_RUN_D10 = '''
import json, os, sys
from pathlib import Path
a = sys.argv[1:]
def opt(f, d=None):
    return a[a.index(f) + 1] if f in a else d
with open(os.environ['FAKE_D10_CALLS'], 'a', encoding='utf-8') as f:
    f.write(' '.join(a) + '\\n')
games = opt('--games', 'tiny,standard,huge').split(',')
rows = json.loads(os.environ.get('FAKE_D10_ROWS') or '{}')
out = Path(opt('--out')); out.mkdir(parents=True)
rep = {'pass': True, 'wallSeconds': 77.0, 'failures': [], 'games': {
    g: {'parts': {'2': {'comparisons': [{'label': 'x86 direct vs x64 direct', 'ok': True, 'rows': rows.get(g, 1)}]}}}
    for g in games}}
(out / 'report.json').write_text(json.dumps(rep)); (out / 'report.txt').write_text('PASS')
'''


def sha(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


class Takes(unittest.TestCase):
    """Takes that share one results store and one tools tree; each take has its own work folder and builds."""

    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = Path(self._td.name)
        self.results = self.tmp / 'results'
        self.repo = self.tmp / 'repo'
        for stage in store.KEYS:
            for pat in store.TOOL_SOURCES[stage] + [f'tools/release/stages/{stage}.py']:
                p = self.repo / pat.replace('*', 'x.py')
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(f'source of {pat}\r\n'.encode())
        self.d10 = self.tmp / 'fake_run_d10.py'
        self.d10.write_text(textwrap.dedent(FAKE_RUN_D10))
        self.d10_calls = self.tmp / 'd10calls.txt'
        self.calls = self.tmp / 'calls.txt'
        self._environ = dict(os.environ)
        for k in [k for k in os.environ if k.startswith(('FAKE_', 'C5_RELEASE_'))]:
            os.environ.pop(k)
        self.setenv(C5_PINS_DIR=str(self.tmp / 'pins'), C5_CLONES_DIR=str(self.tmp / 'clones'),
                    C5_SETTINGS_FILE=str(self.tmp / 'no-settings.ini'), FAKE_D10_CALLS=str(self.d10_calls),
                    FAKE_D10_ROWS=json.dumps(COUNTS), FAKE_CALLS=str(self.calls), FAKE_DIR=str(self.tmp),
                    C5_RELEASE_TESTBEDS='fakehost')
        save = self.tmp / 'saves' / 'REF-HUGE_0350.Civ5Save'
        save.parent.mkdir()
        save.write_bytes(b'huge save bytes')
        d = json.loads((tr.HERE / 'releases' / 'Release-5.4.8.json').read_text(encoding='utf-8'))
        d.update(tag=TAG, reference_saves={'huge': {'path': save.as_posix(), 'sha256': sha(b'huge save bytes'),
                                                    'turn': 350}}, reference_counts=dict(COUNTS))
        self.desc = descriptor.parse(d, 'test', expect_tag=TAG)
        self.tools = {**tr.default_tools(), 'run_d10': [PY, '-I', str(self.d10)], 'tb': [PY, '-I', str(FAKE), 'tb'],
                      'purity': [PY, '-I', str(self.tmp / 'no' / 'derive_purity.py')]}
        self.lines = []
        self.n = 0

    def tearDown(self):
        os.environ.clear()
        os.environ.update(self._environ)
        self._td.cleanup()

    def setenv(self, **kv):
        os.environ.update(kv)

    # --- a take
    def take(self, x64=b'x64 dll', server=b'server exe', i0=b'x86 plain', other=b'db dll', compiler='19.1.7',
             fingerprint='a' * 64):
        """A new work folder whose state has build and lock ok with these bytes."""
        self.n += 1
        w = self.tmp / f'take{self.n}'
        lay = w / 'layout'
        (lay / 'bin').mkdir(parents=True)
        files = {f'bin/{DLL}': x64, 'bin/server.exe': server, 'bin/CvGameDatabase_x64.dll': other}
        for rel, b in files.items():
            (lay / rel).write_bytes(b)
        (lay / 'i0').mkdir()
        (lay / 'i0' / DLL).write_bytes(i0)
        logs = w / 'build' / 'logs'
        logs.mkdir(parents=True)
        (logs / 'dll.log').write_text(f'Tool versions:\n  clang-cl: clang version {compiler}\n  lld-link: LLD 19.1.7\n'
                                      '  MSVC (VCToolsVersion): 14.43.34808\n')
        (w / 'build' / 'build_all.json').write_text(json.dumps({'steps': [{'step': 'dll', 'rc': 0,
                                                                          'log': str(logs / 'dll.log')}]}))
        build = {'x64_bin': str(lay / 'bin'), 'x64_layout': str(lay), 'x64_src': str(w / 'src'), 'x64_commit': 'c' * 40,
                 'x64_hashes': {rel: {'sha256': sha(b).upper(), 'bytes': len(b)} for rel, b in files.items()},
                 'i0_dll': str(lay / 'i0' / DLL), 'i0_dll_sha256': sha(i0).upper(),
                 'build_all': str(w / 'build' / 'build_all.json'), 'narrowing_sites': 794,
                 'narrowing_gate': {'ok': True, 'new': 0, 'gone': 4, 'baseline': 'b.tsv'}, 'tools_commit': 'd' * 40,
                 'tools_dirty': False}
        lock = {'x64': {'commit': 'c' * 40, 'fingerprint': f'v1 {fingerprint} 479 files'}, 'dll_lock': None}
        st = {'format': 1, 'tag': TAG, 'created': 'x', 'config': {}, 'runs': [], 'stages': [
            {'name': 'lock', 'cl_step': '5', 'status': 'ok', 'outputs': lock, 'inputs_sha': 'x'},
            {'name': 'build', 'cl_step': '6', 'status': 'ok', 'outputs': build, 'inputs_sha': 'x'}]}
        w.mkdir(exist_ok=True)
        (w / 'state.json').write_text(json.dumps(st))
        return w

    def run_stage(self, work, stage, tier='hotfix', **cfg):
        self.lines.clear()
        plan = [('lock', '5', 'x'), ('build', '6', 'x'), ('model', '10', 'x'), (stage, '-', '5011')] \
            if stage == 'purity' else [('lock', '5', 'x'), ('build', '6', 'x'), (stage, '-', '5011')]
        config = {'tier': tier, 'who': 'test5011', 'gate_wait': 0, 'results': str(self.results),
                  'tools_repo': str(self.repo), 'keep_free_gb': -10 ** 6, 'scratch': str(work / 'scratch'), 'jobs': 1,
                  **cfg}
        d = tt.TierDriver(TAG, work, self.desc, config, plan, tools=self.tools, stages_dir=tr.STAGES_DIR,
                          out=self.lines.append)
        return d.run(frm='model') if stage == 'purity' else d.run(only=stage)

    def entry(self, work, stage):
        return next(e for e in json.loads((work / 'state.json').read_text())['stages'] if e['name'] == stage)

    def d10_runs(self):
        return self.d10_calls.read_text().splitlines() if self.d10_calls.is_file() else []

    def submits(self):
        return [c for c in (self.calls.read_text().splitlines() if self.calls.is_file() else []) if ' submit ' in c]

    def text(self):
        return '\n'.join(self.lines)


class SaveTest(Takes):
    def test_fresh_then_carried_then_one_byte_fresh(self):
        w1 = self.take()
        self.assertEqual(self.run_stage(w1, 'savetest'), 0, self.text())
        o1 = self.entry(w1, 'savetest')['outputs']
        self.assertEqual(len(self.d10_runs()), 1)
        self.assertIn('--games tiny,standard', self.d10_runs()[0])
        self.assertIn('--allow-subset', self.d10_runs()[0])
        self.assertIn('no record of savetest', o1['fresh_why'])
        self.assertEqual(o1['tier_subset']['games'], ['tiny', 'standard'])
        self.assertEqual(sorted(o1['rows']), ['standard', 'tiny'])
        self.assertTrue(Path(o1['store']).is_file())
        # the same bytes in another take: carried, run_d10 not called, no seconds of its own
        w2 = self.take()
        self.assertEqual(self.run_stage(w2, 'savetest'), 0, self.text())
        o2 = self.entry(w2, 'savetest')['outputs']
        self.assertEqual(len(self.d10_runs()), 1)
        self.assertIn('carried', o2)
        self.assertNotIn('wall_seconds', o2)
        self.assertNotIn('seconds', o2)
        self.assertTrue(o2['carried']['line'].startswith(f'carried from {TAG} {w1}'), o2['carried']['line'])
        self.assertIn('x64_dll ', o2['carried']['line'])
        self.assertEqual(o2['carried']['what']['x64_dll'], sha(b'x64 dll'))
        self.assertTrue(any('carried from' in ln for ln in self.lines), self.text())
        self.assertEqual(o2['rows'], o1['rows'])   # the original's keys are kept
        # NEGATIVE: one changed byte in the x64 DLL runs fresh, and says why
        w3 = self.take(x64=b'x64 dlL')
        self.assertEqual(self.run_stage(w3, 'savetest'), 0, self.text())
        o3 = self.entry(w3, 'savetest')['outputs']
        self.assertEqual(len(self.d10_runs()), 2)
        self.assertNotIn('carried', o3)
        self.assertIn('x64_dll', o3['fresh_why'])

    def test_server_exe_or_i0_changed_runs_fresh(self):
        self.assertEqual(self.run_stage(self.take(), 'savetest'), 0, self.text())
        w = self.take(server=b'server exe 2')
        self.assertEqual(self.run_stage(w, 'savetest'), 0, self.text())
        self.assertIn('server_exe', self.entry(w, 'savetest')['outputs']['fresh_why'])
        w = self.take(i0=b'x86 plain 2')
        self.assertEqual(self.run_stage(w, 'savetest'), 0, self.text())
        self.assertIn('i0_dll', self.entry(w, 'savetest')['outputs']['fresh_why'])
        self.assertEqual(len(self.d10_runs()), 3)

    def test_changed_tool_source_runs_fresh(self):
        self.assertEqual(self.run_stage(self.take(), 'savetest'), 0, self.text())
        (self.repo / 'tools/d10/statscmp.py').write_bytes(b'changed\r\n')
        w = self.take()
        self.assertEqual(self.run_stage(w, 'savetest'), 0, self.text())
        self.assertIn('tool_sources', self.entry(w, 'savetest')['outputs']['fresh_why'])
        self.assertEqual(len(self.d10_runs()), 2)

    def test_line_endings_alone_do_not_change_a_tool_hash(self):
        self.assertEqual(self.run_stage(self.take(), 'savetest'), 0, self.text())
        p = self.repo / 'tools/d10/run_d10.py'
        p.write_bytes(p.read_bytes().replace(b'\r\n', b'\n'))
        w = self.take()
        self.assertEqual(self.run_stage(w, 'savetest'), 0, self.text())
        self.assertIn('carried', self.entry(w, 'savetest')['outputs'])

    def test_missing_tool_source_runs_fresh_and_is_not_recorded(self):
        (self.repo / 'tools/d10/savecmp.py').unlink()
        w = self.take()
        self.assertEqual(self.run_stage(w, 'savetest'), 0, self.text())
        o = self.entry(w, 'savetest')['outputs']
        self.assertIn('cannot prove its inputs', o['fresh_why'])
        self.assertIn('tools/d10/savecmp.py', o['fresh_why'])
        self.assertTrue(o['store'].startswith('not recorded'))
        self.assertFalse((self.results / 'savetest').exists())

    def test_missing_hash_in_base_record_runs_fresh(self):
        self.assertEqual(self.run_stage(self.take(), 'savetest'), 0, self.text())
        rp = next((self.results / 'savetest').glob('*.json'))
        rec = json.loads(rp.read_text())
        del rec['parts']['i0_dll']
        rp.write_text(json.dumps(rec))
        w = self.take()
        self.assertEqual(self.run_stage(w, 'savetest'), 0, self.text())
        o = self.entry(w, 'savetest')['outputs']
        self.assertNotIn('carried', o)
        self.assertIn('has no i0_dll hash', o['fresh_why'])
        self.assertEqual(len(self.d10_runs()), 2)

    def test_counts_of_the_played_games_only(self):
        self.setenv(FAKE_D10_ROWS=json.dumps({'tiny': 1, 'standard': COUNTS['standard']}))
        w = self.take()
        self.assertEqual(self.run_stage(w, 'savetest'), tr.EXIT_STOP, self.text())
        e = self.entry(w, 'savetest')
        self.assertIn('tiny 1 (reference 30666)', e['stop']['what'])
        self.assertNotIn('huge', e['stop']['what'])
        self.assertFalse((self.results / 'savetest').exists())   # a stop is never recorded

    def test_hotfix_record_does_not_cover_the_full_tier(self):
        self.assertEqual(self.run_stage(self.take(), 'savetest'), 0, self.text())
        w = self.take()
        self.assertEqual(self.run_stage(w, 'savetest', tier='intermediate'), 0, self.text())
        o = self.entry(w, 'savetest')['outputs']
        self.assertIn("needs ['huge', 'standard', 'tiny']", o['fresh_why'])
        self.assertNotIn('--games', self.d10_runs()[-1])

    def test_store_inside_repository_refused(self):
        w = self.take()
        rc = self.run_stage(w, 'savetest', results=str(tr.REPO / 'results-here'))
        self.assertEqual(rc, tr.EXIT_FAILED, self.text())
        self.assertIn('inside the repository', self.entry(w, 'savetest')['error'])
        self.assertFalse((tr.REPO / 'results-here').exists())
        self.assertEqual(self.d10_runs(), [])


class WholeGames(Takes):
    def test_hotfix_tc_then_carried_then_one_byte_fresh(self):
        w1 = self.take()
        self.assertEqual(self.run_stage(w1, 'wholegames'), 0, self.text())
        o1 = self.entry(w1, 'wholegames')['outputs']
        subs = self.submits()
        self.assertEqual(len(subs), 1)
        self.assertIn('--run tc-x64', subs[0])
        self.assertNotIn('sf-x64', subs[0])
        self.assertEqual(o1['tier_subset']['games'], ['tc'])
        self.assertIn('D221', o1['tier_subset']['why'])
        self.assertTrue(o1['tier_pass'])
        self.assertEqual(o1['parity'], 'passed on a subset')
        self.assertNotIn('C5_RELEASE_WHOLEGAMES_GAMES', os.environ)   # restored after the original's call
        # server.exe changed only: carried (store.WHOLEGAMES_KEY_SERVER is False)
        w2 = self.take(server=b'server exe 2')
        self.assertEqual(self.run_stage(w2, 'wholegames'), 0, self.text())
        o2 = self.entry(w2, 'wholegames')['outputs']
        self.assertIn('carried', o2, o2.get('fresh_why'))
        self.assertIn('covers tc', o2['carried']['line'])
        self.assertNotIn('server_exe', o2['carried']['what'])
        self.assertEqual(len(self.submits()), 1)
        # NEGATIVE: one changed byte in the x64 DLL runs fresh
        w3 = self.take(x64=b'x64 dlL')
        self.assertEqual(self.run_stage(w3, 'wholegames'), 0, self.text())
        o3 = self.entry(w3, 'wholegames')['outputs']
        self.assertNotIn('carried', o3)
        self.assertIn('x64_dll', o3['fresh_why'])
        self.assertEqual(len(self.submits()), 2)

    def test_other_subset_stop_stays_a_stop(self):
        self.setenv(FAKE_TB_DIFF='tc')
        w = self.take()
        self.assertEqual(self.run_stage(w, 'wholegames'), tr.EXIT_STOP, self.text())
        e = self.entry(w, 'wholegames')
        self.assertEqual(e['outputs']['parity'], 'failed')
        self.assertNotIn('tier_pass', e['outputs'])
        self.assertFalse((self.results / 'wholegames').exists())

    def test_record_of_another_stage_refused(self):
        self.assertEqual(self.run_stage(self.take(), 'savetest'), 0, self.text())
        # a savetest record put where the wholegames record of the next take's key would be
        w = self.take()
        drv_parts, unread = store.key_parts('wholegames', _FakeSrc(self, w))
        self.assertEqual(unread, {})
        key = store.content_key('wholegames', drv_parts)
        src = next((self.results / 'savetest').glob('*.json'))
        (self.results / 'wholegames').mkdir()
        (self.results / 'wholegames' / f'{key}.json').write_text(src.read_text())
        self.assertEqual(self.run_stage(w, 'wholegames'), 0, self.text())
        o = self.entry(w, 'wholegames')['outputs']
        self.assertNotIn('carried', o)
        self.assertIn("a record of the stage 'savetest'", o['fresh_why'])


class _FakeSrc(store.LiveSource):
    """The live key parts of a take's stage outside the driver (for a test that plants a record under that key)."""

    def __init__(self, t, work):
        self.stage, self.repo, self.t = 'wholegames', t.repo, t
        self.st = {e['name']: e['outputs'] for e in json.loads((work / 'state.json').read_text())['stages']}

    def build(self):
        return self.st['build']

    def lock(self):
        return self.st['lock']

    def _desc(self):
        return self.t.desc

    def major(self):
        return 22


class SourceStages(Takes):
    def test_narrowing_carried_and_compiler_change_fresh(self):
        self.assertEqual(self.run_stage(self.take(), 'narrowing'), 0, self.text())
        w = self.take()
        self.assertEqual(self.run_stage(w, 'narrowing'), 0, self.text())
        o = self.entry(w, 'narrowing')['outputs']
        self.assertIn('carried', o)
        self.assertEqual(o['sites'], 794)
        w = self.take(compiler='20.1.0')
        self.assertEqual(self.run_stage(w, 'narrowing'), 0, self.text())
        self.assertIn('compiler', self.entry(w, 'narrowing')['outputs']['fresh_why'])
        w = self.take(fingerprint='b' * 64)
        self.assertEqual(self.run_stage(w, 'narrowing'), 0, self.text())
        self.assertIn('x64_source', self.entry(w, 'narrowing')['outputs']['fresh_why'])

    def test_unreadable_compiler_runs_fresh_and_is_not_recorded(self):
        w = self.take()
        (w / 'build' / 'logs' / 'dll.log').write_text('no versions here\n')
        self.assertEqual(self.run_stage(w, 'narrowing'), 0, self.text())
        o = self.entry(w, 'narrowing')['outputs']
        self.assertIn('compiler (no clang-cl version line', o['fresh_why'])
        self.assertTrue(o['store'].startswith('not recorded'))

    def seed_purity(self, w, body=b'# vp64-purity 1\nmethod\tX\n'):
        """A purity record of these bytes (as a fresh run or an import writes it) and a model record."""
        src = _FakeSrc(self, w)
        for stage in ('model', 'purity'):
            src.stage = stage
            parts, unread = store.key_parts(stage, src)
            self.assertEqual(unread, {})
            files = {}
            outs = {'out': str(self.tmp / 'm'), 'equal': True}
            if stage == 'purity':
                tsv, rep = self.tmp / 'base' / 'purity.tsv', self.tmp / 'base' / 'report.tsv'
                tsv.parent.mkdir(exist_ok=True)
                tsv.write_bytes(body)
                rep.write_bytes(b'kind\tkey\tdecision\n')
                outs = {'purity_tsv': str(tsv), 'report': str(rep), 'methods': 1, 'lua': 0, 'dropped': 0,
                        'test_checks': '66/66', 'test_src': 'release'}
                files, why = store.out_files('purity', outs)
                self.assertIsNone(why)
            store.Store(self.results).put(store.make_record(stage, parts, outs, take=f'{TAG} base', tag=TAG,
                                                            work='base', ended='2026-11-20 22:32:14',
                                                            source='test', files=files))
        return self.tmp / 'base' / 'purity.tsv'

    def test_purity_carried_file_proved_and_copied(self):
        w = self.take()
        tsv = self.seed_purity(w)
        self.assertEqual(self.run_stage(w, 'purity'), 0, self.text())
        self.assertIn('carried', self.entry(w, 'model')['outputs'])
        o = self.entry(w, 'purity')['outputs']
        self.assertIn('carried', o)
        self.assertEqual(Path(o['purity_tsv']).parent, w / 'purity-carried')
        self.assertEqual(sha(Path(o['purity_tsv']).read_bytes()), sha(tsv.read_bytes()))
        self.assertEqual(o['purity_tsv_sha256'], sha(tsv.read_bytes()))
        self.assertEqual(o['test_checks'], '66/66')

    def test_purity_carried_file_deleted_runs_fresh(self):
        w = self.take()
        self.seed_purity(w).unlink()
        rc = self.run_stage(w, 'purity')
        e = self.entry(w, 'purity')
        self.assertEqual(rc, tr.EXIT_STOP, self.text())   # fresh: the fake derive_purity.py does not exist
        self.assertIn('not built yet', e['stop']['what'])
        self.assertIn('purity_tsv', e['outputs']['fresh_why'])
        self.assertIn('is gone', e['outputs']['fresh_why'])

    def test_purity_carried_file_changed_runs_fresh(self):
        w = self.take()
        self.seed_purity(w).write_bytes(b'edited\n')
        self.assertEqual(self.run_stage(w, 'purity'), tr.EXIT_STOP, self.text())
        self.assertIn('changed since it was recorded', self.entry(w, 'purity')['outputs']['fresh_why'])

    def test_purity_dll_byte_changed_runs_fresh(self):
        self.seed_purity(self.take())
        w = self.take(x64=b'x64 dlL')
        self.assertEqual(self.run_stage(w, 'purity'), tr.EXIT_STOP, self.text())
        self.assertIn('carried', self.entry(w, 'model')['outputs'])   # the model keys on the source, not the DLL
        self.assertIn('x64_dll', self.entry(w, 'purity')['outputs']['fresh_why'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
