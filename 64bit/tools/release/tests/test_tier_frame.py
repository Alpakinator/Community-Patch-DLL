"""Hermetic tests of the tier frame (epic 0630, ticket 5010): tools/release/tiers/take_tier.py and plans.py. Fake stages;
no gate, lock, build or game is touched.

    python -I -X utf8 -m unittest tools/release/tests/test_tier_frame.py -v
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
import take_release as tr  # noqa: E402
import take_tier as tt  # noqa: E402

ORIG_A = '''
from take_release import StageResult
DEPENDS = []
def run(ctx):
    return StageResult.ok(who='original-a')
'''
ORIG_B = '''
from take_release import StageResult
DEPENDS = ['a']
def run(ctx):
    return StageResult.ok(who='original-b', a=ctx.outputs('a')['who'])
'''
TIER_A = '''
from take_release import StageResult
DEPENDS = []
def run(ctx):
    return StageResult.ok(who='tier-a')
'''


class Frame(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = Path(self._td.name)
        self.orig = self.tmp / 'stages'
        self.tier = self.tmp / 'tierstages'
        self.orig.mkdir()
        self.tier.mkdir()
        (self.orig / 'a.py').write_text(textwrap.dedent(ORIG_A))
        (self.orig / 'b.py').write_text(textwrap.dedent(ORIG_B))
        (self.tier / 'a.py').write_text(textwrap.dedent(TIER_A))
        self.lines = []
        self._env = {k: os.environ.get(k) for k in ('C5_PINS_DIR', 'C5_CLONES_DIR', 'C5_SETTINGS_FILE')}
        os.environ.update(C5_PINS_DIR=str(self.tmp / 'pins'), C5_CLONES_DIR=str(self.tmp / 'clones'),
                          C5_SETTINGS_FILE=str(self.tmp / 'no-settings.ini'))

    def tearDown(self):
        for k, v in self._env.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
        self._td.cleanup()

    def driver(self, stages, **cfg):
        config = {'tier': 'hotfix', 'who': 'test0630', 'gate_wait': 0, 'tier_stages_dir': str(self.tier), **cfg}
        return tt.TierDriver('Release-9.9.9', self.tmp / 'work', object(), config, stages,
                             stages_dir=self.orig, out=self.lines.append)

    def state(self):
        return json.loads((self.tmp / 'work' / 'state.json').read_text())

    def test_tier_stage_found_first_original_otherwise(self):
        d = self.driver([('a', '1', 'x'), ('b', '2', 'x')])
        self.assertEqual(d.run(), 0)
        outs = {e['name']: e['outputs'] for e in self.state()['stages']}
        self.assertEqual(outs['a']['who'], 'tier-a')
        self.assertEqual(outs['b'], {'who': 'original-b', 'a': 'tier-a'})
        self.assertEqual(self.state()['reached']['tier'], 'hotfix')

    def test_missing_stage_is_not_built_yet_stop(self):
        d = self.driver([('a', '1', 'x'), ('nosuch', '-', '5099')])
        self.assertEqual(d.run(), tr.EXIT_STOP)
        self.assertTrue(any('not built yet (issue 5099)' in ln for ln in self.lines), self.lines)

    def test_hotfix_plan_names(self):
        names = [s[0] for s in tt.plans.stages('hotfix')]
        for gone in ('drift', 'fixchecks', 'refsave', 'instance'):   # study R1, R2, column H
            self.assertNotIn(gone, names)
        self.assertEqual(names[:4], ['survey', 'branches', 'testdata', 'lock'])
        self.assertEqual(names[-1], 'tierreport')
        self.assertLess(names.index('tierraise'), names.index('build'))
        self.assertEqual(len(set(names)), len(names))

    def test_take_release_plans_unchanged(self):
        # the tier is an addition: take_release's own plans are what they were (rule 1 of the epic)
        self.assertEqual(tr.TIERS, ('candidate', 'validated'))
        self.assertIn('drift', [s[0] for s in tr.CANDIDATE])

    def test_other_tier_refused(self):
        with self.assertRaises(ValueError):
            tt.plans.stages('full')   # M3; 'intermediate' exists since M2 (ticket 5022)


class Arguments(Frame):
    def main(self, *argv):
        return tt.main(list(argv), out=self.lines.append)

    def test_tier_required(self):
        self.assertEqual(self.main('Release-5.4.8', '--work', str(self.tmp / 'w')), tr.EXIT_ARGS)

    def test_unknown_tier_refused(self):
        self.assertEqual(self.main('Release-5.4.8', '--tier', 'full', '--work', str(self.tmp / 'w')), tr.EXIT_ARGS)

    def test_base_take_required_and_checked(self):
        rc = self.main('Release-5.4.8', '--tier', 'hotfix', '--work', str(self.tmp / 'w'))
        self.assertEqual(rc, tr.EXIT_ARGS)
        self.assertTrue(any('--base-take' in ln for ln in self.lines), self.lines)
        self.lines.clear()
        rc = self.main('Release-5.4.8', '--tier', 'hotfix', '--work', str(self.tmp / 'w'), '--base-take',
                       str(self.tmp / 'nostate'))
        self.assertEqual(rc, tr.EXIT_ARGS)
        self.assertTrue(any('no state.json' in ln for ln in self.lines), self.lines)

    def test_owner_check_refused(self):
        self.assertEqual(self.main('Release-5.4.8', '--tier', 'hotfix', '--owner-check'), tr.EXIT_ARGS)

    def test_other_tier_in_work_folder_refused(self):
        w = self.tmp / 'w'
        w.mkdir()
        (w / 'state.json').write_text(json.dumps({'format': 1, 'tag': 'Release-5.4.8', 'config': {'tier': 'validated'},
                                                  'stages': [], 'runs': []}))
        base = self.tmp / 'base'
        base.mkdir()
        (base / 'state.json').write_text('{}')
        rc = self.main('Release-5.4.8', '--tier', 'hotfix', '--work', str(w), '--base-take', str(base))
        self.assertEqual(rc, tr.EXIT_ARGS)
        self.assertTrue(any("tier 'validated'" in ln for ln in self.lines), self.lines)

    def test_help_exit_0(self):
        r = subprocess.run([sys.executable, '-I', str(HERE.parent / 'tiers' / 'take_tier.py'), '--help'],
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn('--base-take', r.stdout)


if __name__ == '__main__':
    unittest.main()
