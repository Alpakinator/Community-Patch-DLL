"""Issue 0310: tools/release/stages/_modinfo.py, a mod's .modinfo given the VFS imports of its .civ5proj (as ModBuddy's
release build). Hermetic: mod folders made in a temporary folder.

    python -I -X utf8 -m unittest discover -s tools/release/tests -p "test_modinfo.py" -v
"""
from __future__ import annotations

import contextlib
import hashlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from stages import _modinfo as M  # noqa: E402


def content(rel, vfs):
    return (f'    <Content Include="{rel}">\n      <SubType>Lua</SubType>\n'
            f'      <ImportIntoVFS>{"True" if vfs else "False"}</ImportIntoVFS>\n    </Content>\n')


def h(data: bytes) -> str:
    return hashlib.md5(data).hexdigest().upper()


class Folder:
    def __init__(self, files: dict, project: list, modinfo_lines: list, nl='\n', bom=False, sep='/'):
        self.tmp = tempfile.TemporaryDirectory()
        d = self.dir = Path(self.tmp.name) / '(1) Community Patch'
        for rel, data in files.items():
            (d / rel).parent.mkdir(parents=True, exist_ok=True)
            (d / rel).write_bytes(data)
        (d / 'Community Patch.civ5proj').write_text(
            '<Project>\n  <ItemGroup>\n' + ''.join(content(r, v) for r, v in project) +
            '    <Content Include="Gamecore\\Lone.lua" />\n  </ItemGroup>\n</Project>\n', encoding='utf-8')
        lines = ['<?xml version="1.0" encoding="utf-8"?>', '<Mod id="x" version="1">', '  <Files>']
        lines += [f'    <File md5="{h(files[r])}" import="{i}">{r.replace("/", sep)}</File>' for r, i in modinfo_lines]
        lines += ['  </Files>', '  <Actions/>', '</Mod>', '']
        self.info = d / '(1) Community Patch (v 1).modinfo'
        self.info.write_bytes((b'\xef\xbb\xbf' if bom else b'') + nl.join(lines).encode('utf-8'))

    def close(self):
        self.tmp.cleanup()


FILES = {'Core Files/a.lua': b'a', 'Core Files/b.lua': b'b', 'Gamecore/GamecoreInit.lua': b'init',
         'Gamecore/GamecorePostProcess.lua': b'post', 'Assets/Yields/YieldAtlas_128_Culture.dds': b'dds'}


class TestModinfo(unittest.TestCase):
    def make(self, **kw):
        kw.setdefault('files', FILES)
        f = Folder(**kw)
        self.addCleanup(f.close)
        return f

    def test_master_gap_filled_with_crlf_bom_and_case(self):
        # the project names the file in another case than on disk (the VFS ignores case); the line gets the disk's
        f = self.make(project=[('Core Files\\a.lua', True), ('Gamecore\\GamecoreInit.lua', True),
                               ('Gamecore\\GamecorePostProcess.lua', True),
                               ('Assets\\Yields\\yieldatlas_128_culture.dds', True), ('Core Files\\b.lua', False)],
                      modinfo_lines=[('Core Files/a.lua', 1), ('Core Files/b.lua', 0)], nl='\r\n', bom=True)
        p = M.fix(f.dir)
        self.assertTrue(p['changed'])
        self.assertEqual([r for r, _ in p['add']], ['Gamecore/GamecoreInit.lua', 'Gamecore/GamecorePostProcess.lua',
                                                    'Assets/Yields/YieldAtlas_128_Culture.dds'])
        raw = f.info.read_bytes()
        self.assertTrue(raw.startswith(b'\xef\xbb\xbf'))
        t = raw.decode('utf-8-sig')
        self.assertNotIn('\n', t.replace('\r\n', ''))            # CRLF kept everywhere
        self.assertIn(f'    <File md5="{h(b"init")}" import="1">Gamecore/GamecoreInit.lua</File>\r\n'
                      f'    <File md5="{h(b"post")}" import="1">Gamecore/GamecorePostProcess.lua</File>\r\n'
                      f'    <File md5="{h(b"dds")}" import="1">Assets/Yields/YieldAtlas_128_Culture.dds</File>\r\n'
                      '  </Files>\r\n  <Actions/>', t)
        self.assertIn('import="0">Core Files/b.lua', t)           # not imported by the project: stays as it is
        self.assertFalse(M.changes(M.plan(f.dir)))                # a second fix changes nothing

    def test_matching_modinfo_not_rewritten(self):
        f = self.make(project=[('Core Files\\a.lua', True), ('Core Files\\b.lua', False)],
                      modinfo_lines=[('Core Files/a.lua', 1), ('Core Files/b.lua', 0)], nl='\r\n', bom=True)
        before = f.info.read_bytes()
        mtime = f.info.stat().st_mtime_ns
        p = M.fix(f.dir)
        self.assertFalse(p['changed'])
        self.assertEqual((f.info.read_bytes(), f.info.stat().st_mtime_ns), (before, mtime))

    def test_import_flag_set_and_missing_file_reported(self):
        f = self.make(project=[('Core Files\\b.lua', True), ('Gamecore\\Gone.lua', True)],
                      modinfo_lines=[('Core Files/b.lua', 0)])
        p = M.fix(f.dir)
        self.assertEqual((p['flip'], p['add'], p['missing']), (['Core Files/b.lua'], [], ['Gamecore\\Gone.lua']))
        self.assertIn('import="1">Core Files/b.lua', f.info.read_text(encoding='utf-8'))

    def test_backslash_modinfo_keeps_its_separator(self):
        f = self.make(project=[('Core Files\\a.lua', True), ('Gamecore\\GamecoreInit.lua', True)],
                      modinfo_lines=[('Core Files/a.lua', 1)], sep='\\')
        M.fix(f.dir)
        self.assertIn('import="1">Gamecore\\GamecoreInit.lua</File>\n  </Files>', f.info.read_text(encoding='utf-8'))

    def test_no_project_nothing_to_compare(self):
        f = self.make(project=[], modinfo_lines=[('Core Files/a.lua', 1)])
        (f.dir / 'Community Patch.civ5proj').unlink()
        p = M.fix(f.dir)
        self.assertEqual((p['project'], p['changed']), (None, False))

    def test_project_named_from_elsewhere(self):
        # an install without its .civ5proj, compared with the fork's copy of the project
        f = self.make(project=[('Gamecore\\GamecoreInit.lua', True)], modinfo_lines=[('Core Files/a.lua', 1)])
        proj = f.dir.parent / 'elsewhere.civ5proj'
        (f.dir / 'Community Patch.civ5proj').rename(proj)
        self.assertEqual([r for r, _ in M.fix(f.dir, proj)['add']], ['Gamecore/GamecoreInit.lua'])

    def test_cli_check_exit_codes(self):
        f = self.make(project=[('Gamecore\\GamecoreInit.lua', True)], modinfo_lines=[('Core Files/a.lua', 1)])
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(M.main(['check', str(f.dir)]), 1)
            self.assertEqual(M.main(['fix', str(f.dir)]), 0)
            self.assertEqual(M.main(['check', str(f.dir)]), 0)
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(M.main(['check', str(f.dir.parent)]), 2)   # no .modinfo there


if __name__ == '__main__':
    unittest.main()
