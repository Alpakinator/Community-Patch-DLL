"""Hermetic tests of the purity stage's full test (issue 0480): stage `purity` runs proxy/tools/test_purity.py in full
after derive_purity.py, on the release's x64 source when derive dropped no row, else on the audited source; a failed
check fails the stage. Fake tools only (tests/fake_tool.py `purity`, `purity_test`); nothing outside a temporary folder.

    python -I -X utf8 -m unittest discover -s tools/release/tests -p "test_take_release_purity.py" -v
"""
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE))
import take_release as tr  # noqa: E402
from test_take_release_stages import StageFixture  # noqa: E402


def opt(line, flag):
    a = line.split()
    return a[a.index(flag) + 1]


class TestPurityFullTest(StageFixture):
    def test_default_tool_is_the_repository_test(self):
        self.assertTrue(tr.default_tools()['purity_test'][-1].replace('\\', '/').endswith('proxy/tools/test_purity.py'))

    def test_dropped_rows_test_runs_on_the_audited_source(self):
        self.assertEqual(self.driver().run(), 0, self.lines[-3:])
        st = self.state()['purity']
        self.assertEqual(st['outputs']['dropped'], 1)
        self.assertEqual(st['outputs']['test_checks'], '66/66')
        self.assertEqual(st['outputs']['test_src'], 'audited')
        calls = self.called('purity_test')
        self.assertEqual(len(calls), 1)
        self.assertEqual(Path(opt(calls[0], '--src')), self.tmp / 'audited-src')
        b = self.state()['build']['outputs']
        self.assertEqual(Path(opt(calls[0], '--dll')), Path(b['x64_bin']) / 'CvGameCore_Expansion2.dll')
        self.assertEqual(Path(opt(calls[0], '--scratch')), self.work / 'purity-test')
        # the test ran after the derivation, through the quiet-machine lock (a heavy job)
        lines = self.calls.read_text().splitlines()
        i_purity = next(i for i, ln in enumerate(lines) if ln.startswith('purity '))
        i_test = next(i for i, ln in enumerate(lines) if ln.startswith('purity_test '))
        self.assertLess(i_purity, i_test)
        self.assertTrue(any(ln.startswith('quietlock run') and 'purity_test --src' in ln for ln in lines))

    def test_no_drop_test_runs_on_the_release_source(self):
        self.setenv(FAKE_PURITY_NODROP='1')
        self.assertEqual(self.driver().run(), 0, self.lines[-3:])
        st = self.state()
        self.assertEqual(st['purity']['outputs']['dropped'], 0)
        self.assertEqual(st['purity']['outputs']['test_src'], 'release')
        self.assertEqual(opt(self.called('purity_test')[0], '--src'), st['build']['outputs']['x64_src'])

    def test_a_failed_check_fails_the_stage(self):
        """The negative control of the stage: the 0478 failure (a derive check against grown lists) is a failure."""
        self.setenv(FAKE_PURITY_TEST='fail')
        self.assertEqual(self.driver().run(), tr.EXIT_FAILED, self.lines[-3:])
        st = self.state()
        self.assertEqual(st['purity']['status'], 'failed')
        self.assertNotEqual(st['pack']['status'] if 'pack' in st else 'pending', 'ok')
        text = '\n'.join(self.lines)
        self.assertIn('the full purity test failed', text)
        self.assertIn('FAIL derive: every listed row kept at the pin', text)

    def test_exit_0_without_a_passing_summary_fails(self):
        self.setenv(FAKE_PURITY_TEST='nosummary')
        self.assertEqual(self.driver().run(), tr.EXIT_FAILED, self.lines[-3:])
        self.assertEqual(self.state()['purity']['status'], 'failed')

    def test_audited_commit_missing_from_the_fork_fails_naming_it(self):
        """Without the test hook the stage copies the baseline's audited_at commit from the fork (git archive); the
        fake fork lacks it, so the stage fails and says why (it never runs the test on a source that drops rows)."""
        self.assertEqual(self.driver(purity_audited_src=None).run(), tr.EXIT_FAILED, self.lines[-3:])
        self.assertEqual(self.state()['purity']['status'], 'failed')
        self.assertIn('could not be copied from the fork', '\n'.join(self.lines))
        self.assertEqual(self.called('purity_test'), [])


if __name__ == '__main__':
    unittest.main()
