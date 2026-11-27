"""Hermetic tests of the hotfix tier's build stage (epic 0630, ticket 5012): tools/release/tiers/stages/build.py.

    python -I -X utf8 tools/release/tests/test_tier_build.py

A base take runs today's build stage (tools/release/stages/build.py) on the fake fork, pins and tools of
test_take_release_stages.StageFixture; the tier take then runs the tier's build stage against it. The fake build_server
writes no build_all output of its own DLL, so after the base take the test lays out what the real build_all.py leaves
in <x64_out>/deps (dll/CvGameCore_Expansion2.dll and stubs/lua51_x64.dll, the bytes build_server.py copies into bin/).
No gate, lock, build or game is touched.
"""
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE.parent / 'tiers'))
import take_release as tr  # noqa: E402
import take_tier as tt  # noqa: E402
import tierlib  # noqa: E402
from test_take_release_stages import StageFixture  # noqa: E402

PLAN = list(tr.CANDIDATE[:5])          # survey, branches, testdata, lock, build
OFFLINE = next(s for s in tr.CANDIDATE if s[0] == 'offline')
TIER_STAGES = HERE.parent / 'tiers' / 'stages'
DLL = 'CvGameCore_Expansion2.dll'
EXTRA = {'adopted', 'adopted_unrecorded', 'adoption', 'compiler'}
RT_SRC = HERE.parents[2] / 'proxy' / 'tools' / 'test_proxy_rt.py'
EXEGEN = 'gen-exebuild/vp_exe_tables.gen.h'

# Ticket 0664: a fake of the offline stage's rt check that reads from --build exactly what proxy/tools/test_proxy_rt.py
# reads from it (the paths below, taken from its source by rt_build_reads()); exit 1 naming each one missing.
FAKE_RT = '''
import json, sys
from pathlib import Path
args = sys.argv[1:]
build = Path(args[args.index('--build') + 1])
reads = json.loads(Path(__file__).with_suffix('.json').read_text())
missing = [r for r in reads if not (any(build.glob(r)) if '*' in r else (build / r).exists())]
if missing:
    print('rt: FAIL: the build folder lacks ' + ', '.join(missing))
    sys.exit(1)
print('rt: PASS (%d build-folder reads present)' % len(reads))
'''


def rt_build_reads():
    """Every path test_proxy_rt.py reads under its --build folder (`build_dir / 'a' / 'b'`, with `.glob('x')`), from
    its source: a new read there is picked up here without editing this test."""
    text = RT_SRC.read_text(encoding='utf-8')
    reads = set()
    for m in re.finditer(r"build_dir((?:\s*/\s*'[^']+')+)\)?(\.glob\('([^']+)'\))?", text):
        parts = re.findall(r"'([^']+)'", m.group(1)) + ([m.group(3)] if m.group(3) else [])
        reads.add('/'.join(parts))
    return sorted(reads)


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest().upper()


class TierBuild(StageFixture):
    # the tier side's repository commit and tree state (stages/build.py _tools_commit; MC #9800 adopts the server and
    # the proxy only on the same commit with clean trees). Dirty by default: the DLL-only adoption of ticket 5012.
    TOOLS = ('e' * 40, True)

    def setUp(self):
        super().setUp()
        self.base = self.tmp / 'base'
        self.tier_work = self.tmp / 'tier'
        self._orig_build = tierlib.original('build')
        self._tools_commit = self._orig_build._tools_commit
        self._orig_build._tools_commit = lambda ctx: self.TOOLS

    def tearDown(self):
        self._orig_build._tools_commit = self._tools_commit
        super().tearDown()

    def base_tools(self, commit, dirty=False):
        def fn(e):
            e['outputs'].update(tools_commit=commit, tools_dirty=dirty)
        self.edit_base(fn)

    def cfg(self, **cfg):
        fake_td = self.tmp / 'fakestages' / 'testdata.py'
        self.driver()   # writes the fake testdata stage
        return {'who': 'test5012', 'gate_wait': 0, 'fork': str(self.fork), 'carry': 'pins', 'jobs': 2,
                'ia_patch': str(self.ia_patch), 'deps_dir': str(self.deps), 'llvm_bin': 'C:\\nowhere',
                'purity_audited_src': str(self.tmp / 'audited-src'), 'stage_files': {'testdata': str(fake_td)},
                'x64_config': 'release', **cfg}   # ticket 0774: the lock's release-pgo needs a profile of a real pin

    def take_base(self):
        d = tr.Driver('Release-9.9.9', self.base, self.desc, {'tier': 'candidate', **self.cfg()}, PLAN,
                      tools=self.tools, out=self.lines.append)
        self.assertEqual(d.run(), 0, self.lines[-3:])
        bo = self.base_outputs()
        deps = Path(bo['x64_out']) / 'deps'
        lay = Path(bo['x64_layout'])
        (deps / 'dll' / DLL).write_bytes((lay / 'bin' / DLL).read_bytes())
        (deps / 'stubs').mkdir(exist_ok=True)
        (deps / 'stubs' / 'lua51_x64.dll').write_bytes((lay / 'bin' / 'lua51_x64.dll').read_bytes())
        (deps / 'stubs' / 'lua51_x64.lib').write_bytes(b'import lib')
        # ticket 0664: the generated code build_server.py leaves in its --out (the fake writes none): every path the
        # offline stage's rt check reads outside bin/ and proxy/bin/ (those the fake build has), and the EXE table
        out = Path(bo['x64_out'])
        for rel in rt_build_reads() + [EXEGEN]:
            if rel.split('/')[0] == 'bin' or rel.startswith('proxy/bin'):
                continue
            p = out / rel.replace('*', '0')
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(f'// generated {rel} of the base build\n')
        return bo

    def base_state(self):
        return json.loads((self.base / 'state.json').read_text())

    def base_outputs(self):
        return next(e for e in self.base_state()['stages'] if e['name'] == 'build')['outputs']

    def edit_base(self, fn):
        st = self.base_state()
        fn(next(e for e in st['stages'] if e['name'] == 'build'))
        (self.base / 'state.json').write_text(json.dumps(st))

    def take_tier(self, plan=None):
        self.n0 = {n: len(self.called(n)) for n in ('build_server', 'build_vp_clang', 'build_all')}
        d = tt.TierDriver('Release-9.9.9', self.tier_work, self.desc,
                          {'tier': 'hotfix', 'base_take': str(self.base), 'tier_stages_dir': str(TIER_STAGES),
                           **self.cfg()}, plan or PLAN, tools=self.tools, out=self.lines.append)
        return d.run()

    def new_calls(self, name):
        return self.called(name)[self.n0[name]:]

    def tier_entry(self):
        st = json.loads((self.tier_work / 'state.json').read_text())
        return next(e for e in st['stages'] if e['name'] == 'build')


class Adoption(TierBuild):
    def test_adopts_and_builds_only_server_and_proxy(self):
        bo = self.take_base()
        self.assertEqual(self.take_tier(), 0, self.lines[-5:])
        e = self.tier_entry()
        self.assertEqual(e['status'], 'ok')
        o = e['outputs']
        # no DLL built: one build_server --deps, no 32-bit build, no build_all
        self.assertEqual(self.new_calls('build_vp_clang'), [])
        self.assertEqual(self.new_calls('build_all'), [])
        bs = self.new_calls('build_server')
        self.assertEqual(len(bs), 1, bs)
        self.assertIn('--deps', bs[0])
        # adopted files named with their hashes, each in this take's work folder
        x64dll = f'build/x64-{self.x64[:8]}/deps/dll/{DLL}'
        self.assertEqual(o['adopted'][x64dll], bo['x64_hashes'][f'bin/{DLL}']['sha256'])
        self.assertEqual(o['adopted'][f'build/i0-{self.i0[:8]}/{DLL}'], bo['i0_dll_sha256'])
        self.assertEqual(o['adopted'][f'build/i0ia-{self.i0[:8]}/{DLL}'], bo['i0ia_dll_sha256'])
        self.assertIn(f'build/x64-{self.x64[:8]}/deps/stubs/lua51_x64.dll', o['adopted'])
        for rel in o['adopted']:
            self.assertTrue((self.tier_work / rel).is_file(), rel)
        self.assertIn('DLLs adopted from Release-9.9.9', o['adoption']['line'])
        # hand-over 2(e): what is checked against a record and what is only copied are named apart
        lib = f'build/x64-{self.x64[:8]}/deps/stubs/lua51_x64.lib'
        self.assertNotIn(lib, o['adopted'])
        self.assertEqual(o['adopted_unrecorded'][lib], hashlib.sha256(b'import lib').hexdigest().upper())
        self.assertIn(f'build/x64-{self.x64[:8]}/deps/build_all.json', o['adopted_unrecorded'])
        self.assertFalse(set(o['adopted']) & set(o['adopted_unrecorded']))
        self.assertIn('checked against its SHA-256 records', o['adoption']['line'])
        self.assertIn('copied with no record in the base take', o['adoption']['line'])
        self.assertIn('.lib', o['adoption']['line'])
        # the layout's DLLs are the base's, byte for byte; the server is this take's build
        self.assertEqual((Path(o['x64_layout']) / 'bin' / DLL).read_bytes(),
                         (Path(bo['x64_layout']) / 'bin' / DLL).read_bytes())
        self.assertEqual(o['i0_dll_sha256'], bo['i0_dll_sha256'])
        self.assertEqual(sha(o['i0_dll']), bo['i0_dll_sha256'])
        self.assertEqual(sha(o['i0ia_dll']), bo['i0ia_dll_sha256'])
        self.assertTrue(str(o['x64_layout']).startswith(str(self.tier_work)))
        self.assertTrue((Path(o['x64_layout']) / 'bin' / 'server.exe').is_file())

    def test_output_keys_equal_the_originals(self):
        bo = self.take_base()
        self.assertEqual(self.take_tier(), 0, self.lines[-5:])
        o = self.tier_entry()['outputs']
        self.assertEqual(set(o) - EXTRA, set(bo))
        self.assertTrue({'adopted', 'adoption'} <= set(o))

    def test_hash_mismatch_refuses(self):
        bo = self.take_base()
        (Path(bo['x64_out']) / 'deps' / 'dll' / DLL).write_bytes(b'not the DLL the base laid out')
        self.assertEqual(self.take_tier(), tr.EXIT_STOP, self.lines[-5:])
        e = self.tier_entry()
        self.assertEqual(e['status'], 'stop')
        what = e['stop']['what']
        self.assertIn('adoption refused', what)
        self.assertIn(bo['x64_hashes'][f'bin/{DLL}']['sha256'], what)                       # expected
        self.assertIn(hashlib.sha256(b'not the DLL the base laid out').hexdigest().upper(), what)   # found
        self.assertIn('validated tier', e['stop']['unblock'])
        # nothing built, nothing laid out
        self.assertEqual(self.new_calls('build_server'), [])
        self.assertEqual(self.new_calls('build_vp_clang'), [])
        self.assertFalse((self.tier_work / 'layout').exists())

    def test_i0_hash_mismatch_refuses(self):
        bo = self.take_base()
        Path(bo['i0_dll']).write_bytes(b'changed')
        self.assertEqual(self.take_tier(), tr.EXIT_STOP)
        self.assertIn('the 32-bit DLL', self.tier_entry()['stop']['what'])
        self.assertEqual(self.new_calls('build_server'), [])
        self.assertEqual(self.new_calls('build_vp_clang'), [])

    def test_missing_file_refuses(self):
        bo = self.take_base()
        Path(bo['i0ia_dll']).unlink()
        self.assertEqual(self.take_tier(), tr.EXIT_STOP)
        what = self.tier_entry()['stop']['what']
        self.assertIn('is missing', what)
        self.assertIn(bo['i0ia_dll_sha256'], what)
        self.assertEqual(self.new_calls('build_server'), [])

    def test_missing_deps_refuses(self):
        bo = self.take_base()
        shutil.rmtree(Path(bo['x64_out']) / 'deps')
        self.assertEqual(self.take_tier(), tr.EXIT_STOP)
        self.assertIn('is missing', self.tier_entry()['stop']['what'])
        self.assertEqual(self.new_calls('build_server'), [])

    def test_stubs_library_mismatch_refuses(self):
        bo = self.take_base()
        (Path(bo['x64_out']) / 'deps' / 'stubs' / 'lua51_x64.dll').write_bytes(b'other lua')
        self.assertEqual(self.take_tier(), tr.EXIT_STOP)
        self.assertIn('lua51_x64.dll', self.tier_entry()['stop']['what'])

    def test_compiler_of_the_base_carried(self):
        self.take_base()
        self.edit_base(lambda e: e['outputs'].update(compiler='clang-cl: 19.1.7; lld-link: 19.1.7'))
        self.assertEqual(self.take_tier(), 0, self.lines[-5:])
        self.assertEqual(self.tier_entry()['outputs']['compiler'], 'clang-cl: 19.1.7; lld-link: 19.1.7')


class FullBuild(TierBuild):
    def check_full(self, needle):
        self.assertEqual(self.take_tier(), 0, self.lines[-5:])
        o = self.tier_entry()['outputs']
        self.assertEqual(o['adopted'], {})
        self.assertIn(needle, o['adoption']['why'])
        self.assertEqual(len(self.new_calls('build_vp_clang')), 2)   # the 32-bit plain and -ia builds
        bs = self.new_calls('build_server')
        self.assertEqual(len(bs), 1)
        self.assertNotIn('--deps', bs[0])                             # the x64 DLL built too
        return o

    def test_changed_x64_commit_runs_the_full_build(self):
        self.take_base()
        self.edit_base(lambda e: e['outputs'].update(x64_commit='0' * 40))
        o = self.check_full('x64_commit changed')
        self.assertEqual(set(o) - EXTRA, set(self.base_outputs()))

    def test_changed_i0_commit_runs_the_full_build(self):
        self.take_base()
        self.edit_base(lambda e: e['outputs'].update(i0_commit='1' * 40))
        self.check_full('i0_commit changed')

    def test_changed_ia_patch_runs_the_full_build(self):
        self.take_base()
        self.edit_base(lambda e: e['inputs']['own'].update(ia_patch_sha256='f' * 64))
        self.check_full('ia_patch_sha256')

    def test_failed_base_build_runs_the_full_build(self):
        self.take_base()
        self.edit_base(lambda e: e.update(status='failed'))
        self.check_full('no ok build stage')


class CopyEqual(unittest.TestCase):
    def test_a_copy_that_differs_from_the_base_file_is_refused(self):
        import importlib.util
        import tempfile
        spec = importlib.util.spec_from_file_location('tier_build_5012', TIER_STAGES / 'build.py')
        tb = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(tb)
        with tempfile.TemporaryDirectory() as td:
            a, b = Path(td, 'a.lib'), Path(td, 'b.lib')
            a.write_bytes(b'one')
            b.write_bytes(b'one')
            self.assertEqual(tb._copy_equal(a, b), sha(a))
            b.write_bytes(b'two')
            with self.assertRaises(tb.Refused):
                tb._copy_equal(a, b)


class WholeBinAdoption(TierBuild):
    """MC #9800 (D232): on the base build's repository commit with clean trees the tier adopts the base's whole x64
    bin/ and proxy/bin/ by SHA-256 and builds nothing; its laid-out layout hashes as the base's."""
    TOOLS = ('c' * 40, False)

    def test_same_commit_adopts_server_and_proxy_builds_nothing(self):
        bo = self.take_base()
        self.base_tools('c' * 40)
        self.assertEqual(self.take_tier(), 0, self.lines[-5:])
        o = self.tier_entry()['outputs']
        self.assertEqual(self.new_calls('build_server'), [])
        self.assertEqual(self.new_calls('build_all'), [])
        self.assertEqual(self.new_calls('build_vp_clang'), [])
        srv = f'build/x64-{self.x64[:8]}/bin/server.exe'
        self.assertEqual(o['adopted'][srv], bo['x64_hashes']['bin/server.exe']['sha256'].upper())
        self.assertIn(f'build/x64-{self.x64[:8]}/proxy/bin/{DLL}', o['adopted'])
        self.assertEqual({k: v['sha256'].upper() for k, v in o['x64_hashes'].items()},
                         {k: v['sha256'].upper() for k, v in bo['x64_hashes'].items()})
        self.assertIn('the whole x64 bin/', o['adoption']['bins'])
        self.assertTrue(o['adoption']['line'].startswith('Server, proxy, compositor and DLLs adopted'))

    def test_i2_compositor_adopted_not_built(self):
        from test_take_release_stages import FAKE, PY
        self.tools['build_shell'] = [PY, '-I', str(FAKE), 'build_shell']
        self.desc = self.make_desc(shell_config='i2')
        bo = self.take_base()
        self.base_tools('c' * 40)
        n_shell = len(self.called('build_shell'))
        self.assertEqual(self.take_tier(), 0, self.lines[-5:])
        o = self.tier_entry()['outputs']
        self.assertEqual(len(self.called('build_shell')), n_shell)
        self.assertEqual(o['composite_sha256'].upper(), bo['composite_sha256'].upper())
        self.assertIn(f'build/shell-{"c" * 7}/bin/shell_composite.dll', o['adopted'])
        self.assertEqual({k: v['sha256'].upper() for k, v in o['x64_hashes'].items()},
                         {k: v['sha256'].upper() for k, v in bo['x64_hashes'].items()})

    def take_tier_required(self):
        self.n0 = {n: len(self.called(n)) for n in ('build_server', 'build_vp_clang', 'build_all')}
        d = tt.TierDriver('Release-9.9.9', self.tier_work, self.desc,
                          {'tier': 'hotfix', 'base_take': str(self.base), 'tier_stages_dir': str(TIER_STAGES),
                           'require_adoption': True, **self.cfg()}, PLAN, tools=self.tools, out=self.lines.append)
        return d.run()

    def test_require_adoption_passes_on_whole_adoption_line_first(self):
        self.take_base()
        self.base_tools('c' * 40)
        self.assertEqual(self.take_tier_required(), 0, self.lines[-5:])
        self.assertTrue(any(ln.startswith('ADOPTION: WHOLE') or 'ADOPTION: WHOLE' in ln for ln in self.lines))

    def test_require_adoption_stops_a_build_here(self):
        self.take_base()
        self.base_tools('d' * 40)
        self.assertEqual(self.take_tier_required(), tr.EXIT_STOP, self.lines[-5:])
        e = self.tier_entry()
        self.assertEqual(e['status'], 'stop')
        self.assertIn('--require-adoption', e['stop']['what'])
        self.assertEqual(self.new_calls('build_server'), [])
        self.assertTrue(any('ADOPTION: DLLs only' in ln for ln in self.lines))

    def test_require_adoption_stops_when_no_dll_adoption(self):
        self.take_base()
        self.base_tools('c' * 40)
        self.edit_base(lambda e: e['outputs'].update(x64_commit='0' * 40))
        self.assertEqual(self.take_tier_required(), tr.EXIT_STOP, self.lines[-5:])
        self.assertIn('the DLLs cannot be adopted', self.tier_entry()['stop']['what'])
        self.assertEqual(self.new_calls('build_all'), [])

    def test_changed_base_server_is_a_stop(self):
        bo = self.take_base()
        self.base_tools('c' * 40)
        (Path(bo['x64_out']) / 'bin' / 'server.exe').write_bytes(b'not the recorded server')
        self.assertEqual(self.take_tier(), tr.EXIT_STOP, self.lines[-5:])
        e = self.tier_entry()
        self.assertEqual(e['status'], 'stop')
        self.assertIn('bin/server.exe', e['stop']['what'])
        self.assertEqual(self.new_calls('build_server'), [])

    def test_other_commit_builds_server_here(self):
        self.take_base()
        self.base_tools('d' * 40)
        self.assertEqual(self.take_tier(), 0, self.lines[-5:])
        o = self.tier_entry()['outputs']
        self.assertEqual(len(self.new_calls('build_server')), 1)
        self.assertIn('built here (repository commit changed', o['adoption']['bins'])

    # ---- ticket 0664: the generated code comes with a whole adoption, and the offline stage's rt reads find it

    def fake_rt(self):
        """The fake rt check (FAKE_RT) with test_proxy_rt.py's build-folder reads beside it; returns its command."""
        p = self.tmp / 'fake_rt.py'
        p.write_text(FAKE_RT)
        reads = rt_build_reads()
        self.assertIn('gen/common/vp_ids.gen.cpp', reads)     # the regex still finds test_proxy_rt.py's reads
        self.assertIn('gen/proxy/proxy_ifaces_*.gen.cpp', reads)
        p.with_suffix('.json').write_text(json.dumps(reads))
        return [sys.executable, '-I', str(p)]

    def test_generated_code_adopted_each_file_checked_equal(self):
        bo = self.take_base()
        self.base_tools('c' * 40)
        self.assertEqual(self.take_tier(), 0, self.lines[-5:])
        o = self.tier_entry()['outputs']
        base_out, out = Path(bo['x64_out']), Path(o['x64_out'])
        gen = sorted(f.relative_to(base_out).as_posix() for g in ('gen', 'gen-exebuild')
                     for f in (base_out / g).rglob('*') if f.is_file())
        self.assertIn(EXEGEN, gen)
        self.assertIn('gen/common/vp_ids.gen.h', gen)
        for rel in gen:
            self.assertEqual((out / rel).read_bytes(), (base_out / rel).read_bytes(), rel)
            key = f'build/x64-{self.x64[:8]}/{rel}'
            self.assertEqual(o['adopted_unrecorded'][key], sha(base_out / rel), rel)
            self.assertNotIn(key, o['adopted'])
        self.assertIn('generated code', o['adoption']['bins'])

    def test_offline_rt_reads_present_on_the_adopted_tree(self):
        """The offline stage's rt check runs on the tier's adopted build folder (the ticket's failure: 4 of 111 rt
        checks failed for want of gen/); the fake reads exactly test_proxy_rt.py's build-folder paths."""
        self.take_base()
        self.base_tools('c' * 40)
        self.tools['test_proxy_rt'] = self.fake_rt()
        self.assertEqual(self.take_tier(PLAN + [OFFLINE]), 0, self.lines[-5:])
        st = json.loads((self.tier_work / 'state.json').read_text())
        off = next(e for e in st['stages'] if e['name'] == 'offline')
        self.assertEqual(off['status'], 'ok', off)
        self.assertEqual(off['outputs']['checks']['rt']['rc'], 0)
        self.assertEqual(self.new_calls('build_server'), [])          # whole adoption: nothing built
        # negative control: the same check on the adopted tree without gen/ fails, naming what it lacks
        bare = self.tmp / 'bare'
        shutil.copytree(self.tier_entry()['outputs']['x64_out'], bare)
        shutil.rmtree(bare / 'gen')
        r = subprocess.run(self.tools['test_proxy_rt'] + ['--build', str(bare)], capture_output=True, text=True)
        self.assertEqual(r.returncode, 1, r.stdout)
        self.assertIn('gen/common/vp_ids.gen.cpp', r.stdout)

    def test_base_without_generated_code_is_a_stop(self):
        bo = self.take_base()
        self.base_tools('c' * 40)
        shutil.rmtree(Path(bo['x64_out']) / 'gen')
        self.assertEqual(self.take_tier(), tr.EXIT_STOP, self.lines[-5:])
        e = self.tier_entry()
        self.assertEqual(e['status'], 'stop')
        self.assertIn('generated code', e['stop']['what'])
        self.assertEqual(self.new_calls('build_server'), [])
        self.assertFalse((self.tier_work / 'layout').exists())

    def test_whole_adoption_recorded_without_gen_is_adopted_again(self):
        """A partial record of a whole adoption made before ticket 0664 (no gen/ in its folder) is not resumed."""
        self.take_base()
        self.base_tools('c' * 40)
        self.assertEqual(self.take_tier(), 0, self.lines[-5:])
        out = Path(self.tier_entry()['outputs']['x64_out'])
        shutil.rmtree(out / 'gen')
        st = json.loads((self.tier_work / 'state.json').read_text())
        for e in st['stages']:
            if e['name'] == 'build':
                e['status'] = 'failed'                      # make the driver run the stage again, its outputs kept
        (self.tier_work / 'state.json').write_text(json.dumps(st))
        self.assertEqual(self.take_tier(), 0, self.lines[-5:])
        self.assertTrue((out / 'gen' / 'common' / 'vp_ids.gen.cpp').is_file())
        self.assertEqual(self.new_calls('build_server'), [])

    def test_dirty_base_builds_server_here(self):
        self.take_base()
        self.base_tools('c' * 40, dirty=True)
        self.assertEqual(self.take_tier(), 0, self.lines[-5:])
        self.assertEqual(len(self.new_calls('build_server')), 1)
        self.assertIn('uncommitted changes', self.tier_entry()['outputs']['adoption']['bins'])


# ------------------------------------------------------------------ ticket 5018: the three DLL builds side by side

import test_take_release_stages as trs  # noqa: E402
from test_tier_linkwrap import FAKE_LINKPLACE  # noqa: E402

# The fixture's fake build_vp_clang.py (same DLL bytes, same FAKE_I0_FAIL switch) plus a 'compile' that takes
# FAKE_COMPILE_S seconds and a link as the fork's: one subprocess.run of a string command naming lld-link.
# Ticket 0694: the side-by-side builders do not append to the shared FAKE_CALLS file: appends of processes running at
# once to one file lose lines on Windows (the C runtime's append is a seek to the end, then a write; 3 writers lost
# 255 and 1,425 of 9,000 lines on the workstation, 2026-11-23), which took 5024's takes 5 and 6 to "1 != 2" build_vp_clang
# calls. Each build writes its call into a file of its own under FAKE_CALLS + '.d' (named by start time, then pid);
# SideBySideFixture.called() reads them after the shared file's.
_SHARED_APPEND = '''with open(os.environ['FAKE_CALLS'], 'a') as f:
    f.write('build_vp_clang ' + str(here) + '\\n')
'''
_OWN_CALL_FILE = '''import time as _t
_cd = Path(os.environ['FAKE_CALLS'] + '.d')
_cd.mkdir(exist_ok=True)
(_cd / ('%020d-%d.txt' % (_t.time_ns(), os.getpid()))).write_text('build_vp_clang ' + str(here) + '\\n')
'''
assert trs.BUILD_VP_CLANG.count(_SHARED_APPEND) == 1, 'test_take_release_stages.BUILD_VP_CLANG changed its call log'
LINKING_VP_CLANG = trs.BUILD_VP_CLANG.replace(_SHARED_APPEND, _OWN_CALL_FILE) + '''
import json, subprocess, time
def _ev(kind):
    with open(os.path.join(os.environ['FAKE_LP_EVENTS'], '%d.jsonl' % os.getpid()), 'a') as f:
        f.write(json.dumps({'t': time.time(), 'kind': kind, 'name': here.name}) + '\\n')
_ev('compile-start')
time.sleep(float(os.environ.get('FAKE_COMPILE_S', '0')))
_ev('compile-end')
cp = subprocess.run('"%s" "%s" @"%s"' % (sys.executable, os.environ['FAKE_LLD'], here / 'link'), capture_output=True)
if cp.returncode != 0:
    print('linking dll failed - see build log')
    sys.exit(1)
'''
FAKE_LLD = '''
import json, os, sys, time
from pathlib import Path
def ev(kind):
    with open(os.path.join(os.environ['FAKE_LP_EVENTS'], '%d.jsonl' % os.getpid()), 'a') as f:
        f.write(json.dumps({'t': time.time(), 'kind': kind, 'name': Path.cwd().name}) + '\\n')
ev('link-start')
time.sleep(float(os.environ.get('FAKE_LINK_S', '0.3')))
ev('link-end')
'''
# server/build_server.py taking FAKE_X64_S seconds (then fake_tool.py's), with build.py's link record in its summary
SLOW_SERVER = '''
import json, os, subprocess, sys, time
from pathlib import Path
def ev(kind):
    with open(os.path.join(os.environ['FAKE_LP_EVENTS'], '%d.jsonl' % os.getpid()), 'a') as f:
        f.write(json.dumps({'t': time.time(), 'kind': kind, 'name': 'x64'}) + '\\n')
ev('x64-start')
time.sleep(float(os.environ.get('FAKE_X64_S', '0')))
rc = subprocess.call([sys.executable, '-I', sys.argv[1], 'build_server'] + sys.argv[2:])
args = sys.argv[2:]
s = Path(args[args.index('--out') + 1]) / 'deps' / 'dll' / 'build-summary.json'
if rc == 0 and s.is_file():
    d = json.loads(s.read_text())
    d.update(link_place={'taken': True, 'waited_s': 12.5, 'memory_waited': False}, timings_s={'link': 30.0})
    s.write_text(json.dumps(d))
# the compiler's version lines in the dll step's log, as build_all.json names it (store.compiler_of reads them)
ba = Path(args[args.index('--out') + 1]) / 'deps' / 'build_all.json'
if rc == 0 and os.environ.get('FAKE_COMPILER') and ba.is_file():
    log = ba.parent / 'dll' / 'build-dll.log'
    log.write_text('clang-cl: %s\\nlld-link: %s\\n' % (os.environ['FAKE_COMPILER'], os.environ['FAKE_COMPILER']))
    d = json.loads(ba.read_text())
    for st in d.get('steps', []):
        if st.get('step') == 'dll':
            st['log'] = str(log)
    ba.write_text(json.dumps(d))
ev('x64-end')
sys.exit(rc)
'''
VOLATILE = ('seconds', 'parallel_builds')


def stable(o):
    """Outputs without what differs from run to run by nature (the measured seconds, the side-by-side record)."""
    if isinstance(o, dict):
        return {k: stable(v) for k, v in o.items() if k not in VOLATILE}
    if isinstance(o, list):
        return [stable(v) for v in o]
    return o


class SideBySideFixture(TierBuild):
    """Ticket 5018: a fresh build of a tier other than hotfix starts the x64, i0 and -ia builds at once; the 32-bit
    builds through tools/release/tiers/linkwrap.py, with a stand-in linkplace that allows one link at a time."""

    def setUp(self):
        saved = trs.BUILD_VP_CLANG
        trs.BUILD_VP_CLANG = LINKING_VP_CLANG
        try:
            super().setUp()
        finally:
            trs.BUILD_VP_CLANG = saved
        self.events = self.tmp / 'events'
        self.events.mkdir()
        self.lp = self.tmp / 'fake_linkplace.py'
        self.lp.write_text(FAKE_LINKPLACE)
        lld = self.tmp / 'llvm' / 'lld-link.py'
        lld.parent.mkdir()
        lld.write_text(FAKE_LLD)
        slow = self.tmp / 'slow_server.py'
        slow.write_text(SLOW_SERVER)
        self.tools['build_server'] = [trs.PY, '-I', str(slow), str(trs.FAKE)]
        self.setenv(FAKE_LP_EVENTS=str(self.events), FAKE_LLD=str(lld))

    def called(self, name):
        """The shared call log's lines, then the side-by-side builders' own call files in start order (0694)."""
        rows = super().called(name)
        own = Path(str(self.calls) + '.d')
        if own.is_dir():
            rows += [ln for f in sorted(own.glob('*.txt')) for ln in f.read_text().splitlines()
                     if ln.split(' ', 1)[0] == name]
        return rows

    def take(self, work=None, tier='intermediate', base=False, **cfg):
        self.n0 = {n: len(self.called(n)) for n in ('build_server', 'build_vp_clang', 'build_all', 'build_shell')}
        config = {'tier': tier, 'tier_stages_dir': str(TIER_STAGES),
                  **self.cfg(linkwrap_linkplace=str(self.lp), **cfg)}
        if base:
            config['base_take'] = str(self.base)
        d = tt.TierDriver('Release-9.9.9', work or self.tier_work, self.desc, config, PLAN, tools=self.tools,
                          out=self.lines.append)
        return d.run()

    def entry(self, work=None):
        st = json.loads(((work or self.tier_work) / 'state.json').read_text())
        return next(e for e in st['stages'] if e['name'] == 'build')

    def ev(self):
        evs = [json.loads(ln) for f in self.events.glob('*.jsonl') for ln in f.read_text().splitlines()]
        return sorted(evs, key=lambda e: e['t'])

    def span(self, evs, name, a, b):
        t0 = [e['t'] for e in evs if e['kind'] == a and e['name'].startswith(name)]
        t1 = [e['t'] for e in evs if e['kind'] == b and e['name'].startswith(name)]
        self.assertEqual((len(t0), len(t1)), (1, 1), (name, a, b, evs))
        return t0[0], t1[0]

    def spans(self):
        evs = self.ev()
        return {'x64': self.span(evs, 'x64', 'x64-start', 'x64-end'),
                'i0c': self.span(evs, 'i0wt-', 'compile-start', 'compile-end'),
                'iac': self.span(evs, 'i0ia-', 'compile-start', 'compile-end'),
                'i0l': self.span(evs, 'i0wt-', 'link-start', 'link-end'),
                'ial': self.span(evs, 'i0ia-', 'link-start', 'link-end')}

    def layout_hashes(self, work):
        return {f.relative_to(work).as_posix(): sha(f) for f in sorted((work / 'layout').rglob('*'))
                if f.is_file() and f.name != 'README.txt'}

    def clear(self, work):
        shutil.rmtree(work)
        trs.git(self.fork, 'worktree', 'prune')
        for f in self.events.glob('*'):
            f.unlink()


class SideBySide(SideBySideFixture):
    # ---- the contract

    def test_same_records_and_layout_as_the_serial_build(self):
        self.assertEqual(self.take(build_side_by_side=False), 0, self.lines[-5:])
        serial = self.entry()
        self.assertNotIn('parallel_builds', serial['outputs'])
        lay = self.layout_hashes(self.tier_work)
        self.assertTrue(any(k.endswith(f'/{DLL}') for k in lay), lay)
        locks = {n: (self.tier_work / 'deps' / n).read_text() for n in ('dll.lock', 'i0.lock')}
        self.clear(self.tier_work)
        self.assertEqual(self.take(), 0, self.lines[-5:])            # intermediate: side by side by default
        sbs = self.entry()
        self.assertEqual(stable(sbs['outputs']), stable(serial['outputs']))
        for k in ('x64_build', 'i0_build', 'i0ia_build'):              # the records, key by key
            self.assertEqual(stable(sbs['outputs'][k]), stable(serial['outputs'][k]), k)
        self.assertEqual(self.layout_hashes(self.tier_work), lay)
        for n, text in locks.items():                                  # the candidate locks, the build time aside
            strip = lambda s: re.sub(r'\d{4}-\d\d-\d\d \d\d:\d\d', 'T', s)  # noqa: E731
            self.assertEqual(strip((self.tier_work / 'deps' / n).read_text()), strip(text), n)
        self.assertEqual(sbs['inputs'], serial['inputs'])
        pb = sbs['outputs']['parallel_builds']
        self.assertTrue(pb['used'])
        self.assertEqual(set(pb['builds']), {'x64', 'i0', 'i0ia'})
        self.assertEqual({b['status'] for b in pb['builds'].values()}, {'ok'})
        self.assertEqual((pb['builds']['x64']['link_wait_s'], pb['builds']['x64']['link_s']), (12.5, 30.0))
        for n in ('i0', 'i0ia'):
            b = pb['builds'][n]
            self.assertEqual((b['links'], b['linkwrap_rc'], b['link_place_taken']), (1, 0, True), b)
            self.assertGreaterEqual(b['link_wait_s'], 0)
            self.assertGreaterEqual(b['link_s'], 0.3)
            self.assertIsInstance(b['seconds'], int)
            self.assertTrue(Path(b['from']).is_file())
        self.assertEqual(len(self.new_calls('build_vp_clang')), 2)      # built once each, not again by the original
        self.assertEqual(len(self.new_calls('build_server')), 1)
        log = (self.tier_work / 'logs' / 'build-i0.log').read_text()
        self.assertIn('linkwrap.py', log)
        self.assertIn('linkwrap: link: place taken', log)

    def test_builds_overlap_and_the_links_never(self):
        self.setenv(FAKE_X64_S='2.5', FAKE_COMPILE_S='1.2', FAKE_LINK_S='0.6')
        self.assertEqual(self.take(), 0, self.lines[-5:])
        s = self.spans()
        self.assertTrue(overlaps(s['x64'], s['i0c']), s)
        self.assertTrue(overlaps(s['x64'], s['iac']), s)
        self.assertTrue(overlaps(s['i0c'], s['iac']), s)               # the two 32-bit compiles side by side
        self.assertFalse(overlaps(s['i0l'], s['ial']), s)              # their links never
        pb = self.entry()['outputs']['parallel_builds']
        self.assertLess(pb['wall_s'], 2.5 + 2 * 1.2 + 2 * 0.6)         # under the sum of the parts

    def test_negative_control_serial_build_does_not_overlap(self):
        """The overlap check above can fail: the same fakes built one after another never overlap."""
        self.setenv(FAKE_X64_S='0.5', FAKE_COMPILE_S='0.5')
        self.assertEqual(self.take(build_side_by_side=False), 0, self.lines[-5:])
        s = self.spans()
        self.assertFalse(overlaps(s['x64'], s['i0c']), s)
        self.assertFalse(overlaps(s['i0c'], s['iac']), s)

    def test_one_failed_build_fails_the_stage_after_the_others_end(self):
        self.setenv(FAKE_X64_S='2.0', FAKE_I0_FAIL='i0ia')
        self.assertNotEqual(self.take(), 0)
        e = self.entry()
        self.assertEqual(e['status'], 'failed')
        self.assertIn('build_vp_clang.py (i0ia) exit 1', e['error'])
        self.assertIn('build_vp_clang.py (i0ia) exit 1', self.lines[-1])
        evs = self.ev()
        x64_end = next(ev['t'] for ev in evs if ev['kind'] == 'x64-end')
        i0_link = next(ev['t'] for ev in evs if ev['kind'] == 'link-end' and ev['name'].startswith('i0wt-'))
        self.assertTrue(x64_end and i0_link)                           # the others ended ...
        o = e['outputs']
        self.assertIn('x64_build', o)                                  # ... and are recorded for a rerun
        self.assertIn('i0_build', o)
        self.assertNotIn('i0ia_build', o)
        self.assertEqual(o['parallel_builds']['builds']['i0ia']['status'], 'failed')
        self.assertFalse((self.tier_work / 'layout').exists())         # nothing laid out
        ends = re.findall(r'^=== .* exit -?\d+ in', (self.tier_work / 'logs' / 'build-i0ia.log').read_text(), re.M)
        self.assertEqual(len(ends), 1, ends)                           # the failed build is not run again serially
        # the rerun builds only what failed
        os.environ.pop('FAKE_I0_FAIL')
        n_server = len(self.called('build_server'))
        self.assertEqual(self.take(), 0, self.lines[-5:])
        self.assertEqual(len(self.called('build_server')), n_server)
        self.assertEqual(len(self.new_calls('build_vp_clang')), 1)
        pb = self.entry()['outputs']['parallel_builds']
        self.assertEqual((set(pb['builds']), set(pb['resumed'])), ({'i0ia'}, {'x64', 'i0'}))

    def test_two_failures_name_both(self):
        self.setenv(FAKE_I0_FAIL='i0', FAKE_BUILD_SERVER_FAIL='1')
        self.assertNotEqual(self.take(), 0)
        err = self.entry()['error']
        self.assertIn('server/build_server.py exit 1', err)            # the first in the original's order
        self.assertIn('also failed: i0: build_vp_clang.py (i0) exit 1', err)
        self.assertIn('i0ia: build_vp_clang.py (i0ia) exit 1', err)

    def test_x86_one_after_another_beside_x64(self):
        self.setenv(FAKE_X64_S='2.0', FAKE_COMPILE_S='0.5')
        self.assertEqual(self.take(build_x86_one_after_another=True), 0, self.lines[-5:])
        s = self.spans()
        self.assertFalse(overlaps(s['i0c'], s['iac']), s)
        self.assertTrue(overlaps(s['x64'], s['i0c']), s)
        self.assertEqual(self.entry()['outputs']['parallel_builds']['x86'], 'one after another')

    def test_compositor_in_the_side_by_side_set(self):
        self.tools['build_shell'] = [trs.PY, '-I', str(trs.FAKE), 'build_shell']
        self.desc = self.make_desc(shell_config='i2')
        self.assertEqual(self.take(), 0, self.lines[-5:])
        o = self.entry()['outputs']
        self.assertEqual(set(o['parallel_builds']['builds']), {'x64', 'composite', 'i0', 'i0ia'})
        self.assertEqual(len(self.new_calls('build_shell')), 1)
        self.assertEqual(o['composite_sha256'].upper(), sha(o['composite_dll']))

    def test_a_tool_that_cannot_be_wrapped_builds_one_after_another(self):
        self.tools['build_vp_clang'] = [trs.PY, '-c', 'exec(open("build_vp_clang.py").read())']
        self.assertEqual(self.take(), 0, self.lines[-5:])
        pb = self.entry()['outputs']['parallel_builds']
        self.assertFalse(pb['used'])
        self.assertIn('cannot take the link place', pb['why'])
        self.assertFalse(list((self.tier_work / 'logs').glob('*linkwrap.json')))

    def test_link_place_timeout_fails_the_build_loudly(self):
        self.setenv(FAKE_LP_TIMEOUT='1')
        self.assertNotEqual(self.take(), 0)
        e = self.entry()
        self.assertIn('build_vp_clang.py (i0) exit 75', e['error'])
        self.assertIn('LINK PLACE TIMEOUT', e['error'])
        self.assertEqual(e['outputs']['parallel_builds']['builds']['i0']['linkwrap_error'], 'link place timeout')

    # ---- the hotfix tier is unchanged

    def test_hotfix_full_build_stays_serial(self):
        self.take_base()
        self.edit_base(lambda e: e['outputs'].update(x64_commit='0' * 40))
        self.assertEqual(self.take(tier='hotfix', base=True), 0, self.lines[-5:])
        o = self.entry()['outputs']
        self.assertNotIn('parallel_builds', o)
        self.assertFalse(list((self.tier_work / 'logs').glob('*linkwrap.json')))
        self.assertTrue(any('the original build stage runs' in ln for ln in self.lines))
        self.assertIn('x64_commit changed', o['adoption']['why'])

    def test_hotfix_adoption_never_wraps(self):
        bo = self.take_base()
        self.assertEqual(self.take(tier='hotfix', base=True, build_side_by_side=True), 0, self.lines[-5:])
        o = self.entry()['outputs']
        self.assertNotIn('parallel_builds', o)
        self.assertEqual(self.new_calls('build_vp_clang'), [])
        self.assertEqual(o['i0_dll_sha256'], bo['i0_dll_sha256'])
        self.assertFalse(list((self.tier_work / 'logs').glob('*linkwrap.json')))

    def test_hotfix_full_build_side_by_side_on_request(self):
        self.take_base()
        self.edit_base(lambda e: e['outputs'].update(i0_commit='1' * 40))
        self.assertEqual(self.take(tier='hotfix', base=True, build_side_by_side=True), 0, self.lines[-5:])
        o = self.entry()['outputs']
        self.assertTrue(o['parallel_builds']['used'])
        self.assertIn('i0_commit changed', o['adoption']['why'])
        self.assertEqual(set(o) - EXTRA - {'parallel_builds'}, set(self.base_outputs()))


class IntermediateAdoption(SideBySideFixture):
    """MC #10412 as revised by MC #10515 (THE x64 BUILD KEY in tiers/stages/build.py): a tier other than hotfix with a
    base take adopts the 32-bit DLLs when unchanged and builds them side by side when changed; it adopts the x64 DLL
    by hash when the build key (pin and build inputs, tools/x64-dll/'s tree at the commit that built the base DLL, the
    toolchain) equals the base's, and builds it fresh, counted as CHANGED, when a part changed or cannot be read. By
    default the base and this take name the fake commit 'c'*40 (its tools/x64-dll/ tree cannot be read: key unknown);
    the key tests use the checkout's real HEAD. This take's toolchain is the config hook x64_toolchain, the base's the
    fake compiler's log lines (FAKE_COMPILER)."""
    TOOLS = ('c' * 40, False)
    TOOLCHAIN = 'clang-cl: 19.1.7; lld-link: 19.1.7'

    def setUp(self):
        super().setUp()
        self.setenv(FAKE_COMPILER='19.1.7')

    def take_base(self):
        # the base take runs today's stages/build.py, whose _tools_commit reads the real checkout: record TOOLS
        bo = super().take_base()
        self.base_tools(*self.TOOLS)
        return bo

    def take_i(self, **cfg):
        for f in self.events.glob('*'):   # the base take's events
            f.unlink()
        cfg.setdefault('x64_toolchain', self.TOOLCHAIN)
        return self.take(tier='intermediate', base=True, **cfg)

    def real_head(self):
        self.TOOLS = (trs.git(HERE.parents[2], 'rev-parse', 'HEAD'), False)

    def key(self):
        return self.entry()['outputs']['x64_key']

    def assert_built_fresh(self):
        bs = self.new_calls('build_server')
        self.assertEqual(len(bs), 1)
        self.assertNotIn('--deps', bs[0])                                       # the x64 DLL built too
        self.assertFalse([k for k in self.entry()['outputs']['adopted'] if '/x64-' in k])

    def test_unchanged_x86_adopted_x64_built_fresh(self):
        bo = self.take_base()
        self.edit_base(lambda e: e['outputs'].update(x64_commit='0' * 40))     # x64 pin changed, i0 not
        self.assertEqual(self.take_i(), 0, self.lines[-5:])
        o = self.entry()['outputs']
        self.assertEqual(self.new_calls('build_vp_clang'), [])
        self.assert_built_fresh()
        self.assertEqual(self.new_calls('build_all'), [])
        self.assertEqual(sha(o['i0_dll']), bo['i0_dll_sha256'])
        self.assertEqual(sha(o['i0ia_dll']), bo['i0ia_dll_sha256'])
        self.assertEqual(o['adopted'][f'build/i0-{self.i0[:8]}/{DLL}'], bo['i0_dll_sha256'])
        ways = o['adoption']['ways']
        self.assertTrue(ways['i0'].startswith('adopted from Release-9.9.9'), ways)
        self.assertTrue(ways['i0ia'].startswith('adopted from Release-9.9.9'), ways)
        self.assertIn('the DLL pin changed', ways['x64'])
        self.assertIn('a different DLL is expected and it counts as CHANGED', ways['x64'])
        self.assertIs(self.key()['equal'], False)
        pb = o['parallel_builds']
        self.assertEqual((set(pb['builds']), set(pb['resumed'])), ({'x64'}, {'i0', 'i0ia'}))

    def test_changed_x86_built_side_by_side(self):
        self.take_base()
        self.edit_base(lambda e: e['outputs'].update(i0_commit='1' * 40))
        self.setenv(FAKE_COMPILE_S='0.8', FAKE_X64_S='1.5')
        self.assertEqual(self.take_i(), 0, self.lines[-5:])                    # key unknown: built, no stop
        o = self.entry()['outputs']
        self.assertEqual(len(self.new_calls('build_vp_clang')), 2)
        self.assertEqual(set(o['parallel_builds']['builds']), {'x64', 'i0', 'i0ia'})
        s = self.spans()
        self.assertTrue(overlaps(s['i0c'], s['iac']), s)
        self.assertFalse(overlaps(s['i0l'], s['ial']), s)

    def test_same_key_adopts_the_x64_dll(self):
        self.real_head()
        bo = self.take_base()
        self.assertEqual(self.take_i(), 0, self.lines[-5:])
        o = self.entry()['outputs']
        self.assertIs(self.key()['equal'], True, self.key()['why'])
        bs = self.new_calls('build_server')
        self.assertEqual(len(bs), 1)
        self.assertIn('--deps', bs[0])                                          # server and proxy only
        self.assertEqual(self.new_calls('build_all'), [])
        base = bo['x64_hashes'][f'bin/{DLL}']['sha256'].upper()
        self.assertEqual(sha(Path(o['x64_bin']) / DLL), base)
        self.assertEqual(o['adopted'][f'build/x64-{self.x64[:8]}/deps/dll/{DLL}'].upper(), base)
        x = o['adoption']['ways']['x64']
        self.assertTrue(x.startswith('adopted from Release-9.9.9'), x)
        self.assertIn(f'key {self.key()["key"][:16]}', x)
        self.assertIn('x64', set(o['parallel_builds']['resumed']))
        # the key's parts are recorded on both sides
        self.assertEqual(self.key()['mine']['tools'], self.key()['base']['tools'])
        self.assertEqual(self.key()['mine']['toolchain'], self.TOOLCHAIN)

    def test_changed_toolchain_builds_and_counts_as_changed(self):
        self.real_head()
        bo = self.take_base()
        self.assertEqual(self.take_i(x64_toolchain='clang-cl: 20.1.0; lld-link: 20.1.0'), 0, self.lines[-5:])
        self.assertIs(self.key()['equal'], False)
        self.assert_built_fresh()
        x = self.entry()['outputs']['adoption']['ways']['x64']
        self.assertIn('toolchain: base clang-cl: 19.1.7; lld-link: 19.1.7, this take clang-cl: 20.1.0', x)
        self.assertIn('counts as CHANGED', x)
        self.assertIn(bo['x64_hashes'][f'bin/{DLL}']['sha256'].upper()[:16], x)
        self.assertIn(x, self.entry()['outputs']['adoption']['line'])

    def test_changed_tool_tree_builds_and_counts_as_changed(self):
        # a real commit of this repository whose tools/x64-dll differs from HEAD's: the parent of the last commit
        # that changed it
        repo = HERE.parents[2]
        last = trs.git(repo, 'log', '-1', '--format=%H', '--', 'tools/x64-dll')
        parent = trs.git(repo, 'rev-parse', f'{last}^')
        self.real_head()
        self.take_base()
        self.base_tools(parent)
        self.assertEqual(self.take_i(), 0, self.lines[-5:])
        self.assertIs(self.key()['equal'], False)
        self.assert_built_fresh()
        self.assertIn('tools/x64-dll/ changed (tree', self.entry()['outputs']['adoption']['ways']['x64'])

    def test_unknown_toolchain_builds_and_says_so(self):
        os.environ.pop('FAKE_COMPILER')
        self.real_head()
        self.take_base()
        self.assertEqual(self.take_i(), 0, self.lines[-5:])
        self.assertIsNone(self.key()['equal'])
        self.assert_built_fresh()
        x = self.entry()['outputs']['adoption']['ways']['x64']
        self.assertIn('key unknown (toolchain cannot be compared', x)
        self.assertIn('counted as CHANGED', x)

    def test_dirty_base_tree_key_unknown(self):
        self.real_head()
        self.take_base()
        self.base_tools(self.TOOLS[0], dirty=True)
        self.assertEqual(self.take_i(), 0, self.lines[-5:])
        self.assertIsNone(self.key()['equal'])
        self.assertIn('had uncommitted changes when it built', self.key()['why'])
        self.assert_built_fresh()

    def test_an_adopted_base_dll_is_followed_to_the_take_that_built_it(self):
        # the second beta's case: cand2/tier3 adopted its DLL from cand1/run; the key's tools part is the tree of the
        # commit that BUILT the DLL (cand1/run's), not the hotfix take's own
        repo = HERE.parents[2]
        last = trs.git(repo, 'log', '-1', '--format=%H', '--', 'tools/x64-dll')
        parent = trs.git(repo, 'rev-parse', f'{last}^')
        self.real_head()
        bo = self.take_base()
        older = self.tmp / 'older'
        older.mkdir()
        older_bo = dict(bo, tools_commit=parent, tools_dirty=False, x64_build={'tools_commit': parent})
        (older / 'state.json').write_text(json.dumps({'stages': [{'name': 'build', 'status': 'ok',
                                                                  'outputs': older_bo}]}))
        st = self.base_state()
        st.setdefault('config', {})['base_take'] = str(older)
        be = next(e for e in st['stages'] if e['name'] == 'build')
        be['outputs']['x64_build']['adopted_from'] = f'Release-9.9.9 {older}'
        (self.base / 'state.json').write_text(json.dumps(st))
        self.assertEqual(self.take_i(), 0, self.lines[-5:])
        k = self.key()
        self.assertIs(k['equal'], False)                     # the older take's tools/x64-dll/ differs from HEAD's
        self.assertEqual((k['base']['built_in'], k['base']['tools_commit']), (str(older), parent))
        # negative: an adoption whose base names no base take: unknown
        st['config'].pop('base_take')
        (self.base / 'state.json').write_text(json.dumps(st))
        shutil.rmtree(self.tier_work)
        self.assertEqual(self.take_i(), 0, self.lines[-5:])
        self.assertIsNone(self.key()['equal'])
        self.assertIn('names no base take', self.key()['why'])

    def test_normalised_hash_masks_only_the_date(self):
        import importlib.util
        spec = importlib.util.spec_from_file_location('tier_build_normalised_test', str(TIER_STAGES / 'build.py'))
        tbuild = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(tbuild)
        p, q, r = (self.tmp / n for n in ('a.dll', 'b.dll', 'c.dll'))
        head = bytearray(b'MZ' + b'\0' * 0x3a + (0x40).to_bytes(4, 'little'))
        pe = lambda stamp: bytes(head) + b'PE\0\0' + b'\x64\x86\x08\x00' + stamp
        p.write_bytes(pe(b'\x1f\x71\xb5\x25') + b'Build Nov 22 2026 10:18:33 rest')
        q.write_bytes(pe(b'\x10\x70\x3a\x45') + b'Build Nov 22 2026 23:01:26 rest')
        r.write_bytes(pe(b'\x10\x70\x3a\x45') + b'Build Nov 22 2026 23:01:26 resT')
        self.assertEqual(tbuild.normalised_sha(p), tbuild.normalised_sha(q))
        self.assertNotEqual(tbuild.normalised_sha(q), tbuild.normalised_sha(r))      # any other byte still counts
        self.assertNotEqual(sha(p), sha(q))

    def test_x86_hash_mismatch_refuses(self):
        bo = self.take_base()
        Path(bo['i0_dll']).write_bytes(b'changed')
        self.assertEqual(self.take_i(), tr.EXIT_STOP)
        self.assertIn('the 32-bit DLL', self.entry()['stop']['what'])
        self.assertEqual(self.new_calls('build_server'), [])


def overlaps(a, b):
    return a[0] < b[1] and b[0] < a[1]


if __name__ == '__main__':
    unittest.main()
