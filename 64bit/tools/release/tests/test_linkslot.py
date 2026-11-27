"""tools/release/linkslot.py: the slot's default is on C: (ticket 0193, D133: no lock or slot file on D:, never on F:)."""
import importlib
import os
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))


def load(env_value=None):
    saved = os.environ.pop('C5_3500_LINKSLOT', None)
    try:
        if env_value is not None:
            os.environ['C5_3500_LINKSLOT'] = env_value
        import linkslot
        return importlib.reload(linkslot)
    finally:
        os.environ.pop('C5_3500_LINKSLOT', None)
        if saved is not None:
            os.environ['C5_3500_LINKSLOT'] = saved


class LinkSlot(unittest.TestCase):
    def test_default_on_c(self):
        ls = load()
        self.assertEqual(ls.SLOT, Path('C:/c5-64bit-work/3500/release-link.slot'))

    def test_take_release_in_named_slot(self):
        with tempfile.TemporaryDirectory() as t:
            ls = load(str(Path(t) / 'sub' / 'release-link.slot'))
            self.assertEqual(ls.main(['take', '--who', 'a']), 0)
            self.assertEqual(ls.main(['take', '--who', 'b']), 4)
            self.assertEqual(ls.main(['release', '--who', 'b']), 4)
            self.assertEqual(ls.main(['release', '--who', 'a']), 0)
            self.assertFalse(ls.SLOT.exists())


if __name__ == '__main__':
    unittest.main()
