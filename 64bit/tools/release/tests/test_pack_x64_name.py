"""Issue 2606 (MC #3403, 2026-11-17): pack.py writes the x64 DLL as CvGameCore_Expansion2_x64.dll.

In the modpack form (VP inside <game>\\Assets\\DLC) the EXE's DLC scan took VP64\\CvGameCore_Expansion2.dll for the DLC's
game DLL and loaded it into the 32-bit process (error 193, then 0xC0000005; installer-notes section 15). The pack names
the x64 DLL so that no scan takes it, and the manifest's x64_dll.file says so (the proxy reads it with pack=1). A pack
written before 2606 (the old name in its manifest) stays readable: the installer's pack check reads the names from the
manifest.

    python -I -m pytest -q tools/release/tests/test_pack_x64_name.py
"""
import hashlib
import json
import struct
import sys
import tempfile
import types
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import pack  # noqa: E402

sys.path.insert(0, str(HERE.parent.parent / 'installer'))
import build_installer  # noqa: E402


def pe(machine, tail=b''):
    return b'MZ' + b'\0' * 0x3A + struct.pack('<I', 0x40) + b'PE\0\0' + struct.pack('<H', machine) + b'\0' * 64 + tail


def sha(b):
    return hashlib.sha256(b).hexdigest()


class PackX64Name(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = Path(self._td.name)
        self.bin = self.tmp / 'layout' / 'bin'
        self.bin.mkdir(parents=True)
        for f in pack.X64_FILES:
            (self.bin / f).write_bytes(pe(0x8664, f.encode()))
            (self.bin / f).with_suffix('.pdb').write_bytes(b'pdb ' + f.encode())
        self.x86 = self.tmp / 'x86.dll'
        self.x86.write_bytes(pe(0x14C, b'the tag dll'))

    def tearDown(self):
        self._td.cleanup()

    def run_pack(self, *extra):
        out = self.tmp / 'out'
        rc = pack.main(['--layout', str(self.bin.parent), '--x86-dll', str(self.x86), '--release', 'Release-9.9.9',
                        '--fork-commit', 'f' * 40, '--tools-commit', 't' * 40, '--out', str(out)] + list(extra))
        return rc, out / 'vp64pack'

    def test_names(self):
        self.assertEqual(pack.X64_DLL, 'CvGameCore_Expansion2.dll')            # the build's name
        self.assertEqual(pack.X64_DLL_PACK, 'CvGameCore_Expansion2_x64.dll')   # MC #3403
        self.assertEqual(pack.PACK_FILES, ['server.exe', 'CvGameCore_Expansion2_x64.dll', 'lua51_x64.dll',
                                           'CvGameDatabase_x64.dll', 'CvLocalization_x64.dll'])

    def test_pack_holds_the_x64_dll_as_x64_and_the_manifest_names_it(self):
        rc, pk = self.run_pack()
        self.assertEqual(rc, 0)
        names = sorted(p.name for p in pk.iterdir())
        self.assertEqual(names, sorted(pack.PACK_FILES + ['purity.tsv', 'manifest.json']))
        self.assertNotIn('CvGameCore_Expansion2.dll', names)    # nothing a DLC scan takes for a game DLL
        m = json.loads((pk / 'manifest.json').read_text(encoding='utf-8'))
        self.assertEqual(m['x64_dll'], {'file': 'CvGameCore_Expansion2_x64.dll',
                                        'sha256': sha((self.bin / 'CvGameCore_Expansion2.dll').read_bytes())})
        self.assertEqual((pk / 'CvGameCore_Expansion2_x64.dll').read_bytes(),
                         (self.bin / 'CvGameCore_Expansion2.dll').read_bytes())
        self.assertNotIn('CvGameCore_Expansion2_x64.dll', m['files'])
        self.assertNotIn('CvGameCore_Expansion2.dll', m['files'])
        self.assertEqual(sorted(m['files']), ['CvGameDatabase_x64.dll', 'CvLocalization_x64.dll', 'lua51_x64.dll',
                                              'server.exe'])
        # the empty purity file is bound to the same DLL
        self.assertIn(m['x64_dll']['sha256'], (pk / 'purity.tsv').read_text(encoding='utf-8'))

    def test_pdb_keeps_the_builds_name(self):
        rc, pk = self.run_pack('--pdb')
        self.assertEqual(rc, 0)
        self.assertTrue((pk / 'CvGameCore_Expansion2.pdb').is_file())   # the DLL's debug directory names it
        self.assertFalse((pk / 'CvGameCore_Expansion2_x64.pdb').exists())

    def test_purity_bound_to_another_dll_refused(self):
        p = self.tmp / 'purity.tsv'
        p.write_text('# vp64-purity 1\ndll_sha256\t%s\n' % ('0' * 64), encoding='utf-8')
        rc, pk = self.run_pack('--purity', str(p))
        self.assertEqual(rc, 1)
        self.assertFalse((pk / 'manifest.json').exists())

    def _check(self, pk):
        m = json.loads((pk / 'manifest.json').read_text(encoding='utf-8'))
        w = types.SimpleNamespace(manifest=m, pack=pk, path=pk / 'manifest.json')
        build_installer.Work.check_pack(w)

    def test_installer_pack_check_reads_the_new_name(self):
        rc, pk = self.run_pack()
        self.assertEqual(rc, 0)
        self._check(pk)                                               # no Refused
        (pk / 'CvGameCore_Expansion2_x64.dll').write_bytes(pe(0x8664, b'changed'))
        with self.assertRaises(build_installer.Refused):
            self._check(pk)

    def test_a_pack_from_before_2606_still_verifies(self):
        """A pack written with the old name (manifest x64_dll.file = CvGameCore_Expansion2.dll) stays readable."""
        rc, pk = self.run_pack()
        self.assertEqual(rc, 0)
        (pk / 'CvGameCore_Expansion2_x64.dll').rename(pk / 'CvGameCore_Expansion2.dll')
        mp = pk / 'manifest.json'
        m = json.loads(mp.read_text(encoding='utf-8'))
        m['x64_dll']['file'] = 'CvGameCore_Expansion2.dll'
        mp.write_text(json.dumps(m, indent=1) + '\n', encoding='utf-8')
        self._check(pk)
        # negative: the manifest names the new file but only the old one is there
        m['x64_dll']['file'] = 'CvGameCore_Expansion2_x64.dll'
        mp.write_text(json.dumps(m, indent=1) + '\n', encoding='utf-8')
        with self.assertRaises(build_installer.Refused):
            self._check(pk)


if __name__ == '__main__':
    unittest.main()
