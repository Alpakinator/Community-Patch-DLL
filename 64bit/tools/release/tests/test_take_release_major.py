"""Issue 4252 (epic 0334): the release driver's --major-civs switch outside the stages: the command line and its
refusals, the work folder of a count, pack.py's 32-bit DLL of the count and its manifest, the 32-bit define injection.

    python -I -X utf8 -m unittest discover -s tools/release/tests -p "test_take_release*.py" -v

Hermetic: a temporary git repository stands in for the fork; nothing is built; the driver's pins and clones folders
are temporary settings (C5_PINS_DIR, C5_CLONES_DIR, no settings file), so no deps/local-settings.ini is needed.
"""
import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import take_release as tr  # noqa: E402
import pack  # noqa: E402
from stages import build as sb  # noqa: E402


def git(repo, *a):
    return subprocess.run(['git', '-C', str(repo)] + [str(x) for x in a], capture_output=True, text=True,
                          check=True).stdout.strip()


def pe(machine, tail=b''):
    """A minimal PE image head of the given machine (pack.py reads only the machine field)."""
    return b'MZ' + b'\0' * 0x3A + struct.pack('<I', 0x40) + b'PE\0\0' + struct.pack('<H', machine) + b'\0' * 64 + tail


SETTING_VARS = ('C5_PINS_DIR', 'C5_CLONES_DIR', 'C5_SETTINGS_FILE')


class Tmp(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = Path(self._td.name)
        # Ticket 4702: the driver's guard folders are settings (tools/release/settings.py: C5_PINS_DIR / C5_CLONES_DIR,
        # else deps/local-settings.ini). Here temporary ones and no settings file, so the tests pass in a checkout
        # without this machine's deps/local-settings.ini (an exported one) and never read this machine's folders.
        self._old_settings = {k: os.environ.get(k) for k in SETTING_VARS}
        for name in ('pins', 'clones'):
            (self.tmp / name).mkdir()
        os.environ.update(C5_PINS_DIR=str(self.tmp / 'pins'), C5_CLONES_DIR=str(self.tmp / 'clones'),
                          C5_SETTINGS_FILE=str(self.tmp / 'no-settings.ini'))

    def tearDown(self):
        for k, v in self._old_settings.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
        self._td.cleanup()


class TestCommandLine(Tmp):
    def fork(self):
        f = self.tmp / 'fork'
        f.mkdir()
        git(f, 'init', '-q')
        return f

    def main(self, *args):
        lines = []
        rc = tr.main(['Release-5.4.8', '--work', str(self.tmp / 'work'), '--fork', str(self.fork())] + list(args),
                     out=lines.append)
        return rc, lines

    def test_63_is_refused_with_exit_2(self):
        rc, lines = self.main('--major-civs', '63', '--unvalidated-major-civs')
        self.assertEqual(rc, tr.EXIT_ARGS)
        self.assertIn('take_release: error: --major-civs 63: outside 2..62', lines[-1])

    def test_an_unvalidated_value_is_refused_with_exit_2(self):
        rc, lines = self.main('--major-civs', '34')
        self.assertEqual(rc, tr.EXIT_ARGS)
        self.assertIn('--major-civs 34: not validated', lines[-1])
        self.assertIn('--unvalidated-major-civs', lines[-1])

    def test_config_records_the_count_and_defaults_to_22(self):
        a = tr.parse_args(['Release-5.4.8'])
        cfg = tr.build_config(a, self.tmp / 'w')
        self.assertEqual((cfg['major_civs'], cfg['unvalidated_major_civs']), (22, False))
        a = tr.parse_args(['Release-5.4.8', '--major-civs', '43'])
        self.assertEqual(tr.build_config(a, self.tmp / 'w')['major_civs'], 43)

    def test_a_work_folder_of_one_count_refuses_another(self):
        w = self.tmp / 'w'
        w.mkdir()
        (w / tr.STATE_NAME).write_text(json.dumps({'config': {'major_civs': 22}}))
        with self.assertRaisesRegex(tr.ArgError, r'--major-civs 43: the work folder .* holds a run of 22'):
            tr.build_config(tr.parse_args(['Release-5.4.8', '--major-civs', '43']), w)
        # without the option the recorded count is kept
        self.assertEqual(tr.build_config(tr.parse_args(['Release-5.4.8']), w)['major_civs'], 22)

    def test_default_work_folder_of_43(self):
        self.assertEqual(tr.major_work_suffix(None), '')
        self.assertEqual(tr.major_work_suffix(22), '')
        self.assertEqual(tr.major_work_suffix(43), '-m43')

    def test_build_all_is_a_tool(self):
        self.assertTrue(tr.default_tools()['build_all'][-1].replace('\\', '/').endswith('tools/x64-dll/build_all.py'))


class TestPack(Tmp):
    def setUp(self):
        super().setUp()
        f = self.fork = self.tmp / 'fork'
        f.mkdir()
        git(f, 'init', '-q')
        git(f, 'config', 'user.email', 't@example.invalid')
        git(f, 'config', 'user.name', 't')
        self.dll22, self.dll43 = pe(0x14C, b'22'), pe(0x14C, b'43')
        for folder, data in (('(1) Community Patch', self.dll22), ('(3b) 43 Civs Community Patch', self.dll43)):
            (f / folder).mkdir()
            (f / folder / 'CvGameCore_Expansion2.dll').write_bytes(data)
        git(f, 'add', '-A')
        git(f, 'commit', '-q', '-m', 'r')
        git(f, 'tag', 'Release-9.9.9')
        lay = self.layout = self.tmp / 'layout' / 'bin'
        lay.mkdir(parents=True)
        for name in pack.X64_FILES:
            (lay / name).write_bytes(pe(0x8664, name.encode()))

    def run_pack(self, *extra):
        out = self.tmp / ('out' + str(len(list(self.tmp.glob('out*')))))
        rc = pack.main(['--layout', str(self.layout.parent), '--fork', str(self.fork), '--tag', 'Release-9.9.9',
                        '--out', str(out)] + list(extra))
        m = out / 'vp64pack' / 'manifest.json'
        return rc, json.loads(m.read_text(encoding='utf-8')) if m.is_file() else None

    def test_43_binds_the_3b_dll_and_records_the_count(self):
        rc, m = self.run_pack('--major-civs', '43')
        self.assertEqual(rc, 0)
        self.assertEqual(m['x86_dlls'][0]['sha256'], hashlib.sha256(self.dll43).hexdigest())
        self.assertIn('(3b) 43 Civs Community Patch/CvGameCore_Expansion2.dll', m['x86_dlls'][0]['origin'])
        self.assertEqual(m['major_civs'], 43)

    def test_22_manifest_is_unchanged(self):
        rc, m = self.run_pack()
        self.assertEqual(rc, 0)
        self.assertEqual(m['x86_dlls'][0]['sha256'], hashlib.sha256(self.dll22).hexdigest())
        self.assertNotIn('major_civs', m)
        self.assertEqual(set(m), {'format', 'release', 'x86_dlls', 'x64_dll', 'purity', 'built', 'files'})

    def test_a_count_upstream_ships_no_dll_of_fails(self):
        rc, m = self.run_pack('--major-civs', '34')
        self.assertEqual(rc, 1)
        self.assertIsNone(m)
        self.assertRaisesRegex(pack.PackError, 'upstream ships no 32-bit DLL of that count', pack.tag_dll, 34)

    def test_a_tag_without_the_3b_dll_is_a_stop(self):
        git(self.fork, 'rm', '-q', '-r', '(3b) 43 Civs Community Patch')
        git(self.fork, 'commit', '-q', '-m', 'no 3b')
        git(self.fork, 'tag', '-f', 'Release-9.9.9')
        rc, _ = self.run_pack('--major-civs', '43')
        self.assertEqual(rc, 10)


class TestX86Define(Tmp):
    SCRIPT = textwrap.dedent('''\
        SHARED_PREDEFS = [
            'FXS_IS_DLL',
        ]
        RELEASE_PREDEFS = SHARED_PREDEFS + ['NDEBUG']
        DEBUG_PREDEFS = SHARED_PREDEFS + ['VPDEBUG']
        PREDEFS = {
            'release': RELEASE_PREDEFS,
            'debug': DEBUG_PREDEFS,
        }
        ''')

    def script(self, text):
        (self.tmp / 'build_vp_clang.py').write_text(text, encoding='utf-8')
        return self.tmp / 'build_vp_clang.py'

    def predefs(self, p):
        ns = {}
        exec(compile(p.read_text(encoding='utf-8'), str(p), 'exec'), ns)
        return ns['PREDEFS']

    def test_the_define_reaches_both_lists_before_predefs_reads_them(self):
        p = self.script(self.SCRIPT)
        self.assertIsNone(sb.inject_x86_define(self.tmp, 43))
        d = self.predefs(p)
        self.assertEqual(d['release'], ['FXS_IS_DLL', 'NDEBUG', 'MOD_GLOBAL_MAX_MAJOR_CIVS=43'])
        self.assertEqual(d['debug'], ['FXS_IS_DLL', 'VPDEBUG', 'MOD_GLOBAL_MAX_MAJOR_CIVS=43'])

    def test_a_second_call_is_a_no_op_and_another_count_is_refused(self):
        p = self.script(self.SCRIPT)
        sb.inject_x86_define(self.tmp, 43)
        once = p.read_text(encoding='utf-8')
        self.assertIsNone(sb.inject_x86_define(self.tmp, 43))
        self.assertEqual(p.read_text(encoding='utf-8'), once)
        self.assertIn('already carries another major-civilisation define', sb.inject_x86_define(self.tmp, 34))

    def test_a_script_of_another_form_is_refused(self):
        self.script(self.SCRIPT.replace('DEBUG_PREDEFS = SHARED_PREDEFS', 'DBG = SHARED_PREDEFS'))
        self.assertIn('0 "DEBUG_PREDEFS = ..." lines', sb.inject_x86_define(self.tmp, 43))
        self.script(self.SCRIPT.replace('PREDEFS = {', 'TABLE = {'))
        self.assertIn('"PREDEFS = {" after the DEBUG_PREDEFS line expected', sb.inject_x86_define(self.tmp, 43))

    def test_names_and_keys(self):
        self.assertEqual((sb.major_suffix(22), sb.major_suffix(43)), ('', '-m43'))
        self.assertEqual((sb.major_key('release', 22), sb.major_key('release', 43)), ('release', 'release-43'))


if __name__ == '__main__':
    unittest.main()
