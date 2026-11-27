"""Ticket 0629: pack.py never writes a read-only file into a pack. A layout built from the shared read-only pins folder
(C:/c5-64bit-work/keep/pins-0.1b) before 0629 holds read-only dependency DLLs and PDBs; shutil.copy2 would carry the
attribute into the pack. The layout's files keep theirs.

    PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q tools/release/tests/test_pack_writable.py
"""
import os
import shutil
import stat
import struct
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import pack  # noqa: E402


def pe(machine, tail=b''):
    return b'MZ' + b'\0' * 0x3A + struct.pack('<I', 0x40) + b'PE\0\0' + struct.pack('<H', machine) + b'\0' * 64 + tail


def read_only(p):
    return not os.stat(p).st_mode & stat.S_IWRITE


class PackWritable(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = Path(self._td.name)
        self.bin = self.tmp / 'layout' / 'bin'
        self.bin.mkdir(parents=True)
        for f in pack.X64_FILES:
            for p, b in ((self.bin / f, pe(0x8664, f.encode())), ((self.bin / f).with_suffix('.pdb'), b'pdb')):
                p.write_bytes(b)
                os.chmod(p, stat.S_IREAD)          # as copied from the shared folder before 0629
        self.x86 = self.tmp / 'x86.dll'
        self.x86.write_bytes(pe(0x14C, b'the tag dll'))

    def tearDown(self):
        for p in self.tmp.rglob('*'):
            if p.is_file():
                os.chmod(p, stat.S_IREAD | stat.S_IWRITE)
        self._td.cleanup()

    def test_negative_copy2_keeps_the_attribute(self):
        shutil.copy2(self.bin / pack.X64_DLL, self.tmp / 'plain-copy.dll')
        self.assertTrue(read_only(self.tmp / 'plain-copy.dll'))

    def test_pack_files_are_writable_and_the_layout_keeps_its_attribute(self):
        out = self.tmp / 'out'
        rc = pack.main(['--layout', str(self.bin.parent), '--x86-dll', str(self.x86), '--release', 'Release-9.9.9',
                        '--fork-commit', 'f' * 40, '--tools-commit', 't' * 40, '--out', str(out), '--pdb'])
        self.assertEqual(rc, 0)
        files = [p for p in (out / 'vp64pack').rglob('*') if p.is_file()]
        self.assertGreaterEqual(len(files), 2 * len(pack.X64_FILES))
        self.assertEqual([p.name for p in files if read_only(p)], [])
        self.assertTrue(all(read_only(self.bin / f) for f in pack.X64_FILES))
        shutil.rmtree(out)                          # a later take can remove or replace the pack

    def test_copy_writable_keeps_contents_and_time(self):
        src = self.bin / pack.X64_DLL
        dst = self.tmp / 'c.dll'
        pack.copy_writable(src, dst)
        self.assertEqual(dst.read_bytes(), src.read_bytes())
        self.assertEqual(int(dst.stat().st_mtime), int(src.stat().st_mtime))
        self.assertFalse(read_only(dst))
        self.assertTrue(read_only(src))


if __name__ == '__main__':
    unittest.main()
