"""Ticket 0513 (D184): the build stage's release-pgo path. With deps/dll.lock `release-config release-pgo` (the MC's
switch at a pin batch) or config x64_config, the stage checks the locked profile (file present, the lock's SHA-256,
trained on the commit it builds) before anything is built, builds the DLL with tools/x64-dll/build_all.py --config
release-pgo --profile <file>, lays it out through server/build_server.py --config release --deps, checks the DLL's own
.vppgo section and the retrain verdict, and names the layout `...-pgo`. Without the switch nothing changes.

    python -I -X utf8 -m pytest -q -p no:cacheprovider tools/release/tests/test_take_release_pgo.py

Hermetic: a stand-in context records the commands and writes the files the builds would write; nothing is built.
"""
import hashlib
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import take_release  # noqa: E402,F401  (stages import it)
from stages import build as sb  # noqa: E402

PIN = '1b87dd1857e2b4e4ba87c76c7b481161b33e1368'
DLL = 'CvGameCore_Expansion2.dll'
PROFDATA_MAGIC = b'\xfflprofi\x81'


def make_pe(path: Path, sections: dict) -> Path:
    """A minimal PE file with the given sections (build.py's pe_sections reads only the section table)."""
    n = len(sections)
    table_off = 0x40 + 24
    off = table_off + 40 * n
    head = bytearray(b'MZ' + b'\0' * 0x3A + struct.pack('<I', 0x40))
    head += b'PE\0\0' + struct.pack('<HHIIIHH', 0x8664, n, 0, 0, 0, 0, 0x2022)
    table, blobs = bytearray(), bytearray()
    for name, data in sections.items():
        table += name.encode().ljust(8, b'\0') + struct.pack('<IIIIIIHHI', len(data), 0x1000, len(data), off, 0, 0, 0,
                                                             0, 0x40000040)
        blobs += data
        off += len(data)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bytes(head + table + blobs))
    return path


class Ctx:
    """The build stage's view of the driver: config, desc, work, tool, outputs, run (recorded, files written)."""

    def __init__(self, tmp: Path, config: dict, dll_sha=None, retrain=False, summary_sha=None):
        self.tmp, self.config, self.work = tmp, config, tmp / 'work'
        self.desc = SimpleNamespace()
        self.runs = []
        self.dll_sha, self.retrain, self.summary_sha = dll_sha, retrain, summary_sha

    def tool(self, name):
        return ['python', name]

    def outputs(self, name):
        return {'test_db': str(self.tmp / 'test.db')}

    def say(self, *a):
        pass

    def run(self, cmd, name=None, gated=False, env=None, **kw):
        cmd = [str(c) for c in cmd]
        self.runs.append((name, cmd))
        out = Path(cmd[cmd.index('--out') + 1])
        if cmd[1] == 'build_all':
            d = out / 'dll'
            d.mkdir(parents=True, exist_ok=True)
            sha = self.summary_sha or self.dll_sha
            (d / 'build-summary.json').write_text(json.dumps({
                'config': cmd[cmd.index('--config') + 1], 'profile': {'sha256': sha}, 'dll_profile': sha,
                'pgo_diagnostics': {'hash_mismatch': 0, 'missing_function': 0},
                'pgo_retrain': {'needed': self.retrain, 'reasons': ['600 of 49286 functions unmatched'] if self.retrain
                                else []}}))
            make_pe(d / DLL, {'.vppgo': b'VP-BUILD-PROFILE=' + (self.dll_sha or '').encode() + b' (x)\0'})
            (out / 'build_all.json').write_text(json.dumps({'config': cmd[cmd.index('--config') + 1], 'steps': []}))
        else:   # build_server: the DLL of --deps (or a plain one) into bin/
            deps = Path(cmd[cmd.index('--deps') + 1]) if '--deps' in cmd else None
            (out / 'bin').mkdir(parents=True, exist_ok=True)
            (out / 'proxy' / 'bin').mkdir(parents=True, exist_ok=True)
            src = deps / 'dll' / DLL if deps else None
            (out / 'bin' / DLL).write_bytes(src.read_bytes() if src else b'MZplain')
            (out / 'bin' / 'server.exe').write_bytes(b'MZ')
            (out / 'proxy' / 'bin' / DLL).write_bytes(b'MZ')
        return SimpleNamespace(rc=0, log=self.tmp / f'{name}.log', seconds=1.0, tail=lambda n=10: '')


class PgoStage(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = Path(self._td.name)
        self.profile = self.tmp / 'keep' / 'vp64-1b87dd185.profdata'
        self.profile.parent.mkdir()
        self.profile.write_bytes(PROFDATA_MAGIC + b'profile')
        self.sha = hashlib.sha256(self.profile.read_bytes()).hexdigest()

    def tearDown(self):
        self._td.cleanup()

    def lock(self, release_config='release-pgo', sha=None, pin=PIN, file=None):
        p = self.tmp / 'dll.lock'
        lines = [f'commit      = {PIN}', 'describe    = Release-5.4.8-80-g1b87dd185']
        if release_config:
            lines.append(f'release-config = {release_config}')
        lines += [f'profile     = sha256 {sha or self.sha} bytes 15 pin {pin} name vp64-1b87dd185.profdata',
                  '              training: M 150->160, S 0->50, H 300->305',
                  f'profile-file = {file or self.profile}']
        p.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        return str(p)

    def ctx(self, **lock_kw):
        extra = lock_kw.pop('ctx', {})
        return Ctx(self.tmp, {'dll_lock': self.lock(**lock_kw), 'jobs': 6}, **extra)

    def build(self, ctx):
        return sb.build_x64(ctx, PIN, self.tmp / 'src', False, ctx.work / 'build' / 'x64-1b87dd18', 22)

    def test_without_the_switch_the_take_builds_plain_release_as_before(self):
        c = self.ctx(release_config=None)
        self.assertEqual(sb.pgo_plan(c, PIN), (None, None))
        res, rec = self.build(c)
        self.assertIsNone(res)
        self.assertEqual([n for n, _ in c.runs], ['x64'])
        self.assertNotIn('--deps', c.runs[0][1])
        self.assertNotIn('x64_config', rec)
        self.assertNotIn('x64_config', sb.inputs(c))   # the inputs hash of a plain take does not move

    def test_release_pgo_builds_the_dll_with_the_locked_profile_then_lays_it_out(self):
        c = self.ctx(ctx={'dll_sha': None})
        c.dll_sha = self.sha
        res, rec = self.build(c)
        self.assertIsNone(res, res and res.what)
        (n1, a1), (n2, a2) = c.runs
        self.assertEqual((n1, n2), ('x64-dll-pgo', 'x64'))
        self.assertEqual(a1[a1.index('--config') + 1], 'release-pgo')
        self.assertEqual(a1[a1.index('--profile') + 1], str(self.profile))
        self.assertNotIn('--major-civs', a1)
        self.assertEqual(a2[a2.index('--config') + 1], 'release')
        self.assertEqual(a2[a2.index('--deps') + 1], a1[a1.index('--out') + 1])
        self.assertEqual((rec['x64_config'], rec['config'], rec['profile_sha256'], rec['profile_pin']),
                         ('release-pgo', 'release-pgo', self.sha, PIN))
        inp = sb.inputs(c)
        self.assertEqual((inp['x64_config'], inp['pgo_profile_sha256']), ('release-pgo', self.sha))

    def test_a_profile_file_with_another_sha256_is_refused_before_any_build(self):
        c = self.ctx(sha='cd' * 32)
        res, rec = self.build(c)
        self.assertEqual(res.status, 'stop')
        self.assertIn(f'has SHA-256 {self.sha}, deps/dll.lock locks {"cd" * 32}: refused', res.what)
        self.assertEqual(c.runs, [])

    def test_a_missing_profile_file_or_line_is_a_stop(self):
        c = self.ctx(file=self.tmp / 'gone.profdata')
        res, _ = self.build(c)
        self.assertEqual(res.status, 'stop')
        self.assertIn('is missing', res.what)
        p = self.tmp / 'nolock'
        p.write_text(f'commit = {PIN}\nrelease-config = release-pgo\n', encoding='utf-8')
        res, _ = sb.pgo_plan(Ctx(self.tmp, {'dll_lock': str(p)}), PIN)
        self.assertIn('no profile line', res.what)
        self.assertEqual(c.runs, [])

    def test_a_profile_trained_on_another_commit_asks_for_a_retrain(self):
        c = self.ctx(pin='04b1211b8d5e4bc9465b581947ced284bfbc728f')
        res, _ = self.build(c)
        self.assertEqual(res.status, 'stop')
        self.assertIn('trained on 04b1211b8, this take builds 1b87dd185', res.what)
        self.assertIn('retrain', res.unblock)
        self.assertEqual(c.runs, [])

    def test_the_retrain_rule_of_the_built_dll_stops_the_take(self):
        c = self.ctx(ctx={'retrain': True})
        c.dll_sha = self.sha
        res, _ = self.build(c)
        self.assertEqual(res.status, 'stop')
        self.assertIn('the retrain rule asks for a new profile: 600 of 49286', res.what)
        self.assertEqual([n for n, _ in c.runs], ['x64-dll-pgo'])   # build_server never ran

    def test_a_dll_that_names_another_profile_fails(self):
        c = self.ctx()
        c.dll_sha, c.summary_sha = 'ef' * 32, self.sha   # the summary says the locked profile, the DLL another
        res, _ = self.build(c)
        self.assertEqual(res.status, 'failed')
        self.assertIn('.vppgo', res.what)

    def test_an_unknown_configuration_is_a_stop(self):
        res, _ = sb.pgo_plan(Ctx(self.tmp, {'dll_lock': self.lock(), 'x64_config': 'fast'}), PIN)
        self.assertEqual(res.status, 'stop')
        self.assertIn("'fast'", res.what)

    def test_the_layout_of_a_release_pgo_dll_is_named_pgo(self):
        c = self.ctx()
        c.dll_sha = self.sha
        res, rec = self.build(c)
        self.assertIsNone(res)
        c.tag = 'Release-5.4.8'
        rec.update(steps=[], unresolved=0, narrowing_sites=794, narrowing_gate=None)
        dst, hashes = sb.layout_x64(c, rec, PIN, 'abcdef1', False)
        self.assertEqual(dst.name, 'x64-1b87dd18-pabcdef1-pgo')
        self.assertIn('release-pgo', (dst / 'README.txt').read_text(encoding='utf-8'))
        rec2 = dict(rec)
        rec2.pop('x64_config')
        self.assertEqual(sb.layout_x64(c, rec2, PIN, 'abcdef1', False)[0].name, 'x64-1b87dd18-pabcdef1')


if __name__ == '__main__':
    unittest.main()
