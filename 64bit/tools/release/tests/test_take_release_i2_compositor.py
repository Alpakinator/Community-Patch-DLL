"""Hermetic tests of issue 0464: the build stage lays out the increment 2 compositor for a release whose descriptor says
shell_config "i2" (shell_composite.dll, .sha256, .ini and MinHook-LICENSE.txt beside the proxy), so an i2 take
passes pack.py's check; a thin take is byte for byte the take of before; testdata's candidate descriptor carries
shell_config; build and pack fail when the candidate's shell_config differs from the run's (MC #3827).

    python -I -X utf8 -m unittest discover -s tools/release/tests -p "test_take_release_i2_compositor.py" -v

The byte-for-byte test runs the build stage of commit BASE (git show, into a temporary folder; the stage before 0464)
and this branch's on the same fake fork, pins and tools, in the same work folder, and compares every file the stage
writes (layout, candidate locks, build outputs) and the stage's recorded inputs and outputs. Nothing is written outside
a temporary folder; no gate, lock, build or game is touched (tests/fake_tool.py stands in for every tool).
"""
import json
import os
import re
import subprocess
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REL = HERE.parent
REPO = REL.parent.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(REL))
import descriptor  # noqa: E402
import pack  # noqa: E402
from test_take_release_stages import FAKE, PY, StageFixture, git  # noqa: E402
import test_take_release_new_tag as new_tag  # noqa: E402  (a module: its NewTag tests are not collected here again)

NEW = new_tag.NEW

# The last commit before issue 0464 (main, 2026-11-17): its stages/build.py is the thin take's reference.
BASE = '63da63dc7443ab55248e31eaefa8b65805fe857d'
STAMP_RE = re.compile(rb'\d{4}-\d{2}-\d{2} \d{2}:\d{2}')   # the layout READMEs' and the locks' minute stamps
MINHOOK = REPO / 'shell' / 'third_party' / 'minhook' / 'LICENSE.txt'
SHELL_FILES = ('shell_composite.dll', 'shell_composite.pdb', 'shell_composite.sha256', 'shell_composite.ini',
               'MinHook-LICENSE.txt')


def shell_manifest():
    import importlib.util
    spec = importlib.util.spec_from_file_location('t0464_shell_manifest', str(REPO / 'proxy/tools/shell_manifest.py'))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def no_seconds(v):
    """v without its 'seconds' keys (the build records' wall time, which differs run to run)."""
    if isinstance(v, dict):
        return {k: no_seconds(x) for k, x in v.items() if k != 'seconds'}
    if isinstance(v, list):
        return [no_seconds(x) for x in v]
    return v


def tree(root: Path) -> dict:
    """{relative path: bytes with minute stamps masked} of every file under root."""
    out = {}
    for p in sorted(root.rglob('*')):
        if p.is_file():
            out[p.relative_to(root).as_posix()] = STAMP_RE.sub(b'<stamp>', p.read_bytes())
    return out


class I2Fixture(StageFixture):
    def setUp(self):
        super().setUp()
        self.tools['build_shell'] = [PY, '-I', str(FAKE), 'build_shell']

    def layout(self):
        return Path(self.state()['build']['outputs']['x64_layout'])


class TestThinUnchanged(I2Fixture):
    """A thin take (no shell_config, or "thin") writes what the build stage of before wrote, byte for byte."""

    def base_build(self):
        f = self.tmp / 'basestages' / 'build.py'
        f.parent.mkdir()
        f.write_bytes(subprocess.run(['git', '-C', str(REPO), 'show', f'{BASE}:tools/release/stages/build.py'],
                                     capture_output=True, check=True).stdout)
        self.assertNotIn(b'0464', f.read_bytes())
        return f

    def run_build(self, build_file=None):
        fake_td = self.tmp / 'fakestages' / 'testdata.py'
        self.driver()   # writes the fake testdata stage
        files = {'testdata': str(fake_td)}
        if build_file:
            files['build'] = str(build_file)
        rc = self.driver(stage_files=files).run()
        self.assertEqual(rc, 0, self.lines[-3:])
        st = self.state()['build']
        return {'inputs': st['inputs'], 'outputs': st['outputs'],
                'layout': tree(self.work / 'layout'), 'deps': tree(self.work / 'deps'),
                'build': tree(self.work / 'build')}

    def compare(self, **desc_over):
        self.desc = self.make_desc(**desc_over)
        before = self.run_build(self.base_build())
        self.work.rename(self.tmp / 'work-before')
        git(self.fork, 'worktree', 'prune')
        n_before = len(self.called('build_shell'))
        self.desc = self.make_desc(**desc_over)
        after = self.run_build()
        self.assertEqual(len(self.called('build_shell')), n_before)   # no compositor build for thin
        self.assertEqual(n_before, 0)
        self.assertEqual(sorted(before['layout']), sorted(after['layout']))
        for k in ('layout', 'deps', 'build'):
            self.assertEqual(before[k], after[k], k)
        self.assertEqual(before['inputs'], after['inputs'])
        self.assertEqual(no_seconds(before['outputs']), no_seconds(after['outputs']))
        self.assertFalse(any(n.startswith('shell') or 'MinHook' in n for n in after['layout'].keys()
                             if '/proxy/bin/' in n))

    def test_descriptor_without_shell_config(self):
        self.compare()

    def test_descriptor_with_thin(self):
        self.compare(shell_config='thin')


class TestI2Layout(I2Fixture):
    def setUp(self):
        super().setUp()
        self.desc = self.make_desc(shell_config='i2')

    def test_i2_take_lays_out_the_compositor_and_passes_pack(self):
        rc = self.driver().run()
        self.assertEqual(rc, 0, self.lines[-3:])
        st = self.state()
        b = st['build']['outputs']
        lay = self.layout()
        pb = lay / 'proxy' / 'bin'
        for n in SHELL_FILES:
            self.assertTrue((pb / n).is_file(), n)
        self.assertFalse((pb / 'shell_server.exe').exists())   # only the compositor is laid out
        self.assertFalse((lay / 'composite').exists())
        # built once from this repository with C5_SHELL_BUILD = <work>/build/shell-<tools7>
        bs = self.called('build_shell')
        self.assertEqual(len(bs), 1)
        env = [ln for ln in self.calls.read_text().splitlines() if ln.startswith('build_shell_env ')]
        self.assertEqual(len(env), 1)
        self.assertIn(str(self.work / 'build' / f'shell-{b["tools_commit"][:7]}').replace('/', '\\'), env[0])
        # the manifest names the laid-out proxy and compositor, as the proxy and pack.py read it
        self.assertEqual(shell_manifest().check(pb), [])
        self.assertEqual((pb / 'MinHook-LICENSE.txt').read_bytes(), MINHOOK.read_bytes())
        self.assertEqual(pack.check_shell_config(lay, 'i2'), pb / 'shell_composite.dll')
        self.assertEqual(pack.build_shell_config(lay), 'i2')
        self.assertEqual((pb / 'shell_composite.ini').read_bytes(),
                         (REPO / 'shell' / 'composite' / 'shell_composite.ini').read_bytes())
        # SHA256.txt lists the four shell files after the thin take's
        rows = [ln.split('  ')[1] for ln in (lay / 'SHA256.txt').read_text().splitlines()]
        self.assertEqual(rows[-4:], ['proxy/bin/shell_composite.dll', 'proxy/bin/shell_composite.sha256',
                                     'proxy/bin/shell_composite.ini', 'proxy/bin/MinHook-LICENSE.txt'])
        self.assertEqual(b['shell_config'], 'i2')
        self.assertEqual(b['composite_dll'], str(pb / 'shell_composite.dll'))
        self.assertEqual(b['composite_sha256'], descriptor.sha256(pb / 'shell_composite.dll').upper())
        self.assertEqual(st['build']['inputs']['own']['shell_config'], 'i2')
        self.assertIn('Shell configuration i2', (lay / 'README.txt').read_text())
        self.assertIn('compositor ' + b['composite_sha256'], (self.work / 'deps' / 'dll.lock').read_text())
        # the pack stage passed i2 to pack.py, and the later stages ran
        self.assertIn('--shell-config i2', self.called('pack')[0])
        for n in ('pack', 'offline', 'savetest', 'drift', 'fixchecks'):
            self.assertEqual(st[n]['status'], 'ok', n)

    def test_real_pack_py_accepts_the_i2_layout(self):
        """pack.py itself (not the stand-in) on the i2 layout: exit 0, manifest shell_config i2. The fake x64 files
        are replaced by x64 PE heads first (pack.py checks the machine field; the compositor check is the point)."""
        self.assertEqual(self.driver().run(), 0, self.lines[-3:])
        lay = self.layout()
        from test_take_release_shell_config import pe
        for f in pack.X64_FILES:
            (lay / 'bin' / f).write_bytes(pe(0x8664, f.encode()))
        x86 = self.tmp / 'x86.dll'
        x86.write_bytes(pe(0x14C, b'player'))
        out = self.tmp / 'packout'
        base = [PY, '-I', str(REL / 'pack.py'), '--layout', str(lay), '--x86-dll', str(x86), '--release', 'Release-9.9.9']
        r = subprocess.run(base + ['--out', str(out), '--shell-config', 'i2'], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(json.loads((out / 'vp64pack' / 'manifest.json').read_text())['shell_config'], 'i2')
        r = subprocess.run(base + ['--out', str(self.tmp / 'packthin'), '--shell-config', 'thin'], capture_output=True,
                           text=True)
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn('disagrees with the build', r.stdout + r.stderr)

    def test_rerun_runs_nothing_and_a_thin_rerun_drops_the_compositor(self):
        self.assertEqual(self.driver().run(), 0)
        n = len(self.calls.read_text().splitlines())
        self.assertEqual(self.driver().run(), 0)
        self.assertEqual(len(self.calls.read_text().splitlines()), n)
        self.desc = self.make_desc()
        self.assertEqual(self.driver().run(), 0, self.lines[-3:])
        self.assertEqual(len(self.called('build_shell')), 1)
        self.assertEqual(pack.build_shell_config(self.layout()), 'thin')
        self.assertNotIn('shell_config', self.state()['build']['outputs'])

    def test_failing_compositor_build_fails_and_the_rerun_keeps_the_x64_build(self):
        self.setenv(FAKE_BUILD_SHELL_FAIL='1')
        self.assertEqual(self.driver().run(), 1)
        self.assertIn('FAILED build: shell/build.cmd exit 1', self.lines[-1])
        os.environ.pop('FAKE_BUILD_SHELL_FAIL')
        self.assertEqual(self.driver().run(), 0, self.lines[-3:])
        self.assertEqual(len(self.called('build_server')), 1)
        self.assertEqual(len(self.called('build_shell')), 2)

    def test_compositor_missing_or_not_x86_fails(self):
        self.setenv(FAKE_BUILD_SHELL_NO_DLL='1')
        self.assertEqual(self.driver().run(), 1)
        self.assertIn('shell_composite.dll is missing', self.lines[-1])
        os.environ.pop('FAKE_BUILD_SHELL_NO_DLL')
        self.setenv(FAKE_BUILD_SHELL_X64='1')
        self.assertEqual(self.driver().run(), 1)
        self.assertIn('is not a 32-bit PE image', self.lines[-1])
        self.assertEqual(self.state()['build']['status'], 'failed')
        self.assertEqual(self.state()['pack']['status'], 'pending')


class TestCandidateShellConfig(new_tag.NewTag):
    """The real testdata stage: the candidate carries the base descriptor's shell_config; a candidate that lost it (or
    names another) fails build and pack loudly."""

    def setUp(self):
        super().setUp()
        self.tools['build_shell'] = [PY, '-I', str(FAKE), 'build_shell']
        raw = dict(self.desc.raw, shell_config='i2')
        self.desc = descriptor.parse(raw, 'test', expect_tag=self.desc.tag)

    def cand(self):
        return Path(self.state()['testdata']['outputs']['descriptor'])

    def test_candidate_carries_i2_and_the_take_lays_out_the_compositor(self):
        self.assertEqual(self.new_driver().run(), 0, self.lines[-5:])
        self.assertEqual(descriptor.load_file(self.cand(), expect_tag=NEW).shell_config, 'i2')
        lay = Path(self.state()['build']['outputs']['x64_layout'])
        self.assertEqual(pack.build_shell_config(lay), 'i2')
        self.assertIn('--shell-config i2', self.called('pack')[0])

    def test_a_candidate_without_the_runs_shell_config_fails_build(self):
        self.assertEqual(self.new_driver().run(), 0, self.lines[-5:])
        c = json.loads(self.cand().read_text(encoding='utf-8'))
        del c['shell_config']
        self.cand().write_text(json.dumps(c, indent=1) + '\n', encoding='utf-8')
        self.assertEqual(self.new_driver().run(), 1)
        self.assertIn("FAILED build: the run's descriptor (Release-9.9.9) says shell_config 'i2'", self.lines[-1])
        self.assertIn('says nothing (thin)', self.lines[-1])

    def test_a_candidate_with_another_shell_config_fails_pack_too(self):
        self.assertEqual(self.new_driver().run(), 0, self.lines[-5:])
        c = json.loads(self.cand().read_text(encoding='utf-8'))
        c['shell_config'] = 'thin'
        self.cand().write_text(json.dumps(c, indent=1) + '\n', encoding='utf-8')
        self.assertEqual(self.new_driver().run(only='pack'), 1)
        self.assertIn("FAILED pack: the run's descriptor (Release-9.9.9) says shell_config 'i2'", self.lines[-1])
        self.assertIn('says thin', self.lines[-1])

    def test_a_base_without_shell_config_gives_a_candidate_without_it(self):
        self.desc = descriptor.parse({k: v for k, v in self.desc.raw.items() if k != 'shell_config'}, 'test',
                                     expect_tag=self.desc.tag)
        self.assertEqual(self.new_driver().run(), 0, self.lines[-5:])
        self.assertNotIn('shell_config', json.loads(self.cand().read_text(encoding='utf-8')))
        self.assertEqual(self.called('build_shell'), [])


# NewTag's own tests run in test_take_release_new_tag.py; the subclass keeps only its fixture
for _n in [n for n in vars(new_tag.NewTag) if n.startswith('test')]:
    if _n not in vars(TestCandidateShellConfig):
        setattr(TestCandidateShellConfig, _n, None)


if __name__ == '__main__':
    unittest.main()
