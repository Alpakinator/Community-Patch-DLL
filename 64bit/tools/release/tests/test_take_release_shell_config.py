"""Hermetic tests of the release's shell configuration (issue 2602, MC #3197): the descriptor's optional `shell_config`,
`pack.py --shell-config` (the manifest key and the refusal of a build that disagrees) and the pack stage's call.

    python -I -X utf8 -m unittest discover -s tools/release/tests -p "test_take_release_shell_config.py" -v

The byte-for-byte test runs the pack.py of the merge base with origin/main (git show, into a temporary folder) and this
branch's pack.py on the same fake layout and compares every file of both packs. Nothing is written outside a
temporary folder.
"""
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REL = HERE.parent
REPO = REL.parent.parent
sys.path.insert(0, str(REL))
sys.path.insert(0, str(HERE))
import descriptor  # noqa: E402
import pack  # noqa: E402
from test_take_release_stages import StageFixture  # noqa: E402

PY = sys.executable


def pe(machine, filler=b''):
    """A minimal PE image head with the given machine field (0x14C x86, 0x8664 x64)."""
    b = bytearray(0x200)
    b[:2] = b'MZ'
    struct.pack_into('<I', b, 0x3C, 0x80)
    b[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<H', b, 0x84, machine)
    return bytes(b) + filler


def sha(b):
    return hashlib.sha256(b).hexdigest()


def git(*a):
    r = subprocess.run(['git', '-C', str(REPO)] + list(a), capture_output=True)
    if r.returncode != 0:
        raise AssertionError('git %s: %s' % (' '.join(a), r.stderr.decode('utf-8', 'replace')))
    return r.stdout


class Layout:
    """A fake build layout: bin/ (five x64 files), proxy/bin/ (the proxy), optionally a compositor."""

    def __init__(self, root, composite=None):
        self.root = Path(root)
        (self.root / 'bin').mkdir(parents=True)
        for f in pack.X64_FILES:
            (self.root / 'bin' / f).write_bytes(pe(0x8664, f.encode()))
        (self.root / 'proxy' / 'bin').mkdir(parents=True)
        self.proxy = self.root / 'proxy' / 'bin' / 'CvGameCore_Expansion2.dll'
        self.proxy.write_bytes(pe(0x14C, b'proxy'))
        self.x86 = self.root / 'x86.dll'
        self.x86.write_bytes(pe(0x14C, b'the tag dll'))
        self.composite = None
        if composite:
            d = self.root.joinpath(*composite)
            d.mkdir(parents=True, exist_ok=True)
            self.composite = d / 'shell_composite.dll'
            self.composite.write_bytes(pe(0x14C, b'composite'))

    def args(self, out, *extra):
        return ['--layout', str(self.root), '--out', str(out), '--x86-dll', str(self.x86), '--release', 'Release-9.9.9',
                '--fork-commit', 'f' * 40, '--tools-commit', 't' * 40] + list(extra)


class TestDescriptor(unittest.TestCase):
    def test_existing_descriptors_have_no_shell_config_and_read_as_thin(self):
        for p in sorted((REL / 'releases').glob('Release-*.json')):
            raw = json.loads(p.read_text(encoding='utf-8'))
            self.assertNotIn('shell_config', raw, p)
            d = descriptor.load(p.stem)
            self.assertIsNone(d.shell_config)
            self.assertEqual(d.shell_configuration, 'thin')

    def test_existing_descriptor_files_unchanged_byte_for_byte(self):
        base = git('merge-base', 'HEAD', 'origin/main').decode().strip()
        names = git('ls-tree', '--name-only', base, 'tools/release/releases/').decode().split()
        self.assertTrue(names)
        for n in names:
            old = git('show', '%s:%s' % (base, n))
            new = (REPO / n).read_bytes().replace(b'\r\n', b'\n')
            self.assertEqual(old.replace(b'\r\n', b'\n'), new, n)

    def _parse(self, **over):
        d = json.loads((REL / 'releases' / 'Release-5.4.8.json').read_text(encoding='utf-8'))
        d.update(over)
        return descriptor.parse(d, 'test', expect_tag='Release-5.4.8')

    def test_values(self):
        self.assertEqual(self._parse(shell_config='i2').shell_configuration, 'i2')
        self.assertEqual(self._parse(shell_config='thin').shell_config, 'thin')
        self.assertIsNone(self._parse(shell_config=None).shell_config)

    def test_bad_values_refused(self):
        for v in ('I2', 'full', '', 2, ['i2']):
            with self.assertRaises(descriptor.DescriptorError) as cm:
                self._parse(shell_config=v)
            self.assertEqual(cm.exception.field, 'shell_config')


class TestPackShellConfig(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = Path(self._td.name)

    def tearDown(self):
        self._td.cleanup()

    def run_pack(self, script, args):
        r = subprocess.run([PY, '-I', str(script)] + args, capture_output=True, text=True)
        return r.returncode, r.stdout + r.stderr

    def manifest(self, out):
        return json.loads((out / 'vp64pack' / 'manifest.json').read_text(encoding='utf-8'))

    def test_without_the_option_the_pack_is_byte_for_byte_the_old_one(self):
        base = git('merge-base', 'HEAD', 'origin/main').decode().strip()
        old = self.tmp / 'old' / 'tools' / 'release' / 'pack.py'
        old.parent.mkdir(parents=True)
        old.write_bytes(git('show', '%s:tools/release/pack.py' % base))
        for name, comp, extra in (('plain', None, []), ('m43', None, ['--major-civs', '43']),
                                  ('withcomp', ('proxy', 'bin'), [])):
            lay = Layout(self.tmp / name / 'layout', comp)
            outs = []
            for which, script in (('old', old), ('new', REL / 'pack.py')):
                out = self.tmp / name / which
                rc, txt = self.run_pack(script, lay.args(out, *extra))
                self.assertEqual(rc, 0, txt)
                outs.append(out / 'vp64pack')
            a = {p.name: p.read_bytes() for p in outs[0].iterdir()}
            b = {p.name: p.read_bytes() for p in outs[1].iterdir()}
            if pack.X64_DLL_PACK in b and pack.X64_DLL_PACK not in a:
                # issue 2606: a merge base from before the rename: the old pack, its x64 DLL renamed, is the new one
                a[pack.X64_DLL_PACK] = a.pop(pack.X64_DLL)
                a['manifest.json'] = a['manifest.json'].replace(b'"file": "%s"' % pack.X64_DLL.encode(),
                                                                b'"file": "%s"' % pack.X64_DLL_PACK.encode())
            self.assertEqual(a, b, name)
            self.assertNotIn(b'shell_config', b['manifest.json'])

    def test_thin_build_thin_config(self):
        lay = Layout(self.tmp / 'l')
        rc, txt = self.run_pack(REL / 'pack.py', lay.args(self.tmp / 'o', '--shell-config', 'thin'))
        self.assertEqual(rc, 0, txt)
        self.assertEqual(self.manifest(self.tmp / 'o')['shell_config'], 'thin')
        self.assertEqual(pack.build_shell_config(lay.root), 'thin')

    def test_i2_on_a_thin_build_refused_nothing_written(self):
        lay = Layout(self.tmp / 'l')
        rc, txt = self.run_pack(REL / 'pack.py', lay.args(self.tmp / 'o', '--shell-config', 'i2'))
        self.assertEqual(rc, 1, txt)
        self.assertIn('pack: FAILED: --shell-config i2 disagrees with the build', txt)
        self.assertIn('a thin build', txt)
        self.assertFalse((self.tmp / 'o').exists())

    def test_thin_on_an_i2_build_refused(self):
        lay = Layout(self.tmp / 'l', ('composite', 'bin'))
        rc, txt = self.run_pack(REL / 'pack.py', lay.args(self.tmp / 'o', '--shell-config', 'thin'))
        self.assertEqual(rc, 1, txt)
        self.assertIn('an i2 build', txt)
        self.assertFalse((self.tmp / 'o').exists())

    def test_i2_build_i2_config_both_places(self):
        for i, where in enumerate((('proxy', 'bin'), ('composite', 'bin'))):
            lay = Layout(self.tmp / ('l%d' % i), where)
            out = self.tmp / ('o%d' % i)
            rc, txt = self.run_pack(REL / 'pack.py', lay.args(out, '--shell-config', 'i2'))
            self.assertEqual(rc, 0, txt)
            m = self.manifest(out)
            self.assertEqual(m['shell_config'], 'i2')
            # the compositor is not a pack file: the installer puts it beside the proxy
            self.assertNotIn('shell_composite.dll', [p.name for p in (out / 'vp64pack').iterdir()])

    def test_i2_with_a_manifest_naming_another_proxy_refused(self):
        lay = Layout(self.tmp / 'l', ('proxy', 'bin'))
        (lay.composite.parent / 'shell_composite.sha256').write_text(
            '# x\nproxy %s\ncomposite %s\n' % ('0' * 64, sha(lay.composite.read_bytes())))
        rc, txt = self.run_pack(REL / 'pack.py', lay.args(self.tmp / 'o', '--shell-config', 'i2'))
        self.assertEqual(rc, 1, txt)
        self.assertIn("does not name the build's proxy", txt)
        (lay.composite.parent / 'shell_composite.sha256').write_text(
            'proxy %s\ncomposite %s\n' % (sha(lay.proxy.read_bytes()), sha(lay.composite.read_bytes())))
        rc, txt = self.run_pack(REL / 'pack.py', lay.args(self.tmp / 'o2', '--shell-config', 'i2'))
        self.assertEqual(rc, 0, txt)

    def test_two_compositors_or_an_x64_compositor_refused(self):
        lay = Layout(self.tmp / 'l', ('proxy', 'bin'))
        (lay.root / 'composite' / 'bin').mkdir(parents=True)
        (lay.root / 'composite' / 'bin' / 'shell_composite.dll').write_bytes(pe(0x14C, b'other'))
        rc, txt = self.run_pack(REL / 'pack.py', lay.args(self.tmp / 'o', '--shell-config', 'i2'))
        self.assertEqual(rc, 1, txt)
        self.assertIn('two compositors', txt)
        lay2 = Layout(self.tmp / 'l2', ('proxy', 'bin'))
        lay2.composite.write_bytes(pe(0x8664, b'x64'))
        rc, txt = self.run_pack(REL / 'pack.py', lay2.args(self.tmp / 'o2', '--shell-config', 'i2'))
        self.assertEqual(rc, 1, txt)
        self.assertIn('not a 32-bit PE image', txt)

    def test_unknown_value_is_a_usage_error(self):
        lay = Layout(self.tmp / 'l')
        rc, txt = self.run_pack(REL / 'pack.py', lay.args(self.tmp / 'o', '--shell-config', 'full'))
        self.assertEqual(rc, 2, txt)

    # ticket 0574: i3 = the i2 build and an option set
    def write_options(self, obj, name='opts.json'):
        p = self.tmp / name
        p.write_text(json.dumps(obj) if not isinstance(obj, str) else obj, encoding='utf-8')
        return str(p)

    def test_i3_build_with_options(self):
        lay = Layout(self.tmp / 'l', ('proxy', 'bin'))
        opts = {'Renderer': {'ServerUnits': 1}, 'Performance': {'AiUpdateTickMs': 0}}
        rc, txt = self.run_pack(REL / 'pack.py', lay.args(self.tmp / 'o', '--shell-config', 'i3', '--options',
                                                          self.write_options(opts)))
        self.assertEqual(rc, 0, txt)
        m = self.manifest(self.tmp / 'o')
        self.assertEqual(m['shell_config'], 'i3')
        self.assertEqual(m['options'], opts)
        self.assertEqual(pack.build_shell_config(lay.root), 'i2')   # the build itself is the i2 layout
        self.assertNotIn('shell_composite.dll', [p.name for p in (self.tmp / 'o' / 'vp64pack').iterdir()])

    def test_i3_without_options_records_an_empty_set(self):
        lay = Layout(self.tmp / 'l', ('composite', 'bin'))
        rc, txt = self.run_pack(REL / 'pack.py', lay.args(self.tmp / 'o', '--shell-config', 'i3'))
        self.assertEqual(rc, 0, txt)
        self.assertEqual(self.manifest(self.tmp / 'o')['options'], {})

    def test_i3_on_a_thin_build_refused_nothing_written(self):
        lay = Layout(self.tmp / 'l')
        rc, txt = self.run_pack(REL / 'pack.py', lay.args(self.tmp / 'o', '--shell-config', 'i3'))
        self.assertEqual(rc, 1, txt)
        self.assertIn('pack: FAILED: --shell-config i3 disagrees with the build', txt)
        self.assertFalse((self.tmp / 'o').exists())

    def test_options_only_with_i3(self):
        f = self.write_options({'Renderer': {'ServerUnits': 1}})
        for cfg, comp in ((None, None), ('thin', None), ('i2', ('proxy', 'bin'))):
            lay = Layout(self.tmp / ('l-%s' % cfg), comp)
            extra = (['--shell-config', cfg] if cfg else []) + ['--options', f]
            rc, txt = self.run_pack(REL / 'pack.py', lay.args(self.tmp / ('o-%s' % cfg), *extra))
            with self.subTest(cfg=cfg):
                self.assertEqual(rc, 2, txt)
                self.assertIn('--options needs --shell-config i3', txt)
                self.assertFalse((self.tmp / ('o-%s' % cfg)).exists())
        with self.assertRaises(pack.PackError):   # the function refuses it too
            pack.check_options('i2', {'Renderer': {'ServerUnits': 1}})

    def test_bad_option_sets_refused_nothing_written(self):
        for i, (obj, text) in enumerate((
                ({'Renderer': {'ServerRenderer': 1}}, 'vp64helper on'),
                ({'Renderer': {'ServerUnits': '1'}}, 'integer'),
                ({'Renderer': {'ServerUnits': True}}, 'integer'),
                ([1], 'object'),
                ('{not json', '--options'))):
            lay = Layout(self.tmp / ('l%d' % i), ('proxy', 'bin'))
            out = self.tmp / ('o%d' % i)
            rc, txt = self.run_pack(REL / 'pack.py', lay.args(out, '--shell-config', 'i3', '--options',
                                                              self.write_options(obj, 'o%d.json' % i)))
            with self.subTest(obj=obj):
                self.assertEqual(rc, 1, txt)
                self.assertIn('pack: FAILED:', txt)
                self.assertIn(text, txt)
                self.assertFalse(out.exists())
        rc, txt = self.run_pack(REL / 'pack.py', Layout(self.tmp / 'lm', ('proxy', 'bin')).args(
            self.tmp / 'om', '--shell-config', 'i3', '--options', str(self.tmp / 'missing.json')))
        self.assertEqual(rc, 1, txt)
        self.assertIn('missing.json', txt)


class TestPackStage(StageFixture):
    def test_descriptor_without_shell_config_calls_pack_as_before(self):
        self.assertEqual(self.driver().run(), 0, self.lines[-3:])
        self.assertNotIn('--shell-config', self.called('pack')[0])
        st = self.state()['pack']
        self.assertNotIn('shell_config', st['outputs'])
        self.assertNotIn('shell_config', st['inputs']['own'])

    def test_descriptor_shell_config_goes_to_pack(self):
        self.desc = self.make_desc(shell_config='i2')
        self.assertEqual(self.driver().run(), 0, self.lines[-3:])
        self.assertIn('--shell-config i2', self.called('pack')[0])
        st = self.state()['pack']
        self.assertEqual(st['outputs']['shell_config'], 'i2')
        self.assertEqual(st['inputs']['own']['shell_config'], 'i2')
        self.assertNotIn('--options', self.called('pack')[0])
        self.assertNotIn('options', st['outputs'])
        self.assertNotIn('options', st['inputs']['own'])

    def test_descriptor_i3_and_its_options_go_to_pack(self):   # ticket 0574
        opts = {'Renderer': {'ServerUnits': 1}, 'Performance': {'AiUpdateTickMs': 0}}
        self.desc = self.make_desc(shell_config='i3', options=opts)
        self.assertEqual(self.driver().run(), 0, self.lines[-3:])
        call = self.called('pack')[0]
        self.assertIn('--shell-config i3', call)
        self.assertIn('--options', call)
        st = self.state()
        self.assertEqual(st['pack']['outputs']['shell_config'], 'i3')
        self.assertEqual(st['pack']['outputs']['options'], opts)
        self.assertEqual(st['pack']['inputs']['own']['options'], opts)
        self.assertEqual(st['build']['outputs']['shell_config'], 'i3')       # the i2 layout, named i3
        self.assertTrue(st['build']['outputs']['composite_dll'].endswith('shell_composite.dll'))
        written = [p for p in self.tmp.rglob('options.json') if p.parent.name == 'inputs']
        self.assertEqual(len(written), 1, written)
        self.assertEqual(json.loads(written[0].read_text(encoding='utf-8')), opts)


if __name__ == '__main__':
    unittest.main()
