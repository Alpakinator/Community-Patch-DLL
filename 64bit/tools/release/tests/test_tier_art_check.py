"""take_release.py --owner-check on a validation tier's take (epic 0630): the tier's own hi_journey answers D162 (it knows
a carried stock route); a take of the candidate or validated tier reads the --stages-dir module exactly as before.

    python -I -X utf8 tools/release/tests/test_tier_art_check.py
"""
import json
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import take_release as tr  # noqa: E402

STUB = '''
def owner_check(entry, build):
    return {'allowed': True, 'why': [], 'notes': []}
def owner_line(chk, tag):
    return 'STUB-ORIGINAL for the owner: yes'
'''


class OwnerCheck(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = Path(self._td.name)
        self.sd = self.tmp / 'stages'
        self.sd.mkdir()
        (self.sd / 'hi_journey.py').write_text(textwrap.dedent(STUB))
        self.lines = []

    def tearDown(self):
        self._td.cleanup()

    def state(self, tier):
        w = self.tmp / tier
        w.mkdir()
        (w / 'state.json').write_text(json.dumps({'tag': 'Release-9.9.9', 'config': {'tier': tier, 'for_owner': True},
                                                  'stages': []}))
        return w

    def test_validated_take_reads_stages_dir_as_before(self):
        rc = tr.owner_check_main('Release-9.9.9', self.state('validated'), None, self.lines.append, self.sd)
        self.assertEqual(rc, tr.EXIT_OK)
        self.assertTrue(any('STUB-ORIGINAL' in ln for ln in self.lines), self.lines)

    def test_hotfix_take_reads_the_tier_module(self):
        rc = tr.owner_check_main('Release-9.9.9', self.state('hotfix'), None, self.lines.append, self.sd)
        self.assertEqual(rc, tr.EXIT_STOP)   # no hi_journey entry: the tier module refuses
        self.assertFalse(any('STUB-ORIGINAL' in ln for ln in self.lines), self.lines)
        self.assertTrue(any('for the owner: NO' in ln for ln in self.lines), self.lines)


if __name__ == '__main__':
    unittest.main()
