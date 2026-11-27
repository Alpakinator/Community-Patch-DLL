"""Hermetic tests of tools/release/tiers/linkwrap.py (epic 0630 M2, ticket 5018): the fork's build_vp_clang.py run
unchanged with the whole-program link place (D121) taken around its lld-link call alone.

    PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q tools/release/tests/test_tier_linkwrap.py

A fake build_vp_clang.py 'compiles' by sleeping and 'links' with one subprocess.run of a string command naming a fake
lld-link (lld-link.py), as the fork's link_dll does; a fake linkplace (--linkplace, a module file) takes one place
through an exclusive lock file and records every take, add_pid and release with its time. Never the real lock folder:
every wrapped run here passes --linkplace.
"""
import json
import os
import subprocess
import sys
import tempfile
import textwrap
import time
import unittest
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent
LINKWRAP = HERE.parent / 'tiers' / 'linkwrap.py'
sys.path.insert(0, str(HERE.parent / 'tiers'))
import linkwrap  # noqa: E402

PY = sys.executable

FAKE_LINKPLACE = '''
import json, os, time

class LinkPlaceTimeout(Exception):
    pass

EV = os.environ['FAKE_LP_EVENTS']
LOCK = EV + '.lock'

def _ev(kind, **kw):
    with open(os.path.join(EV, '%d.jsonl' % os.getpid()), 'a', encoding='utf-8') as f:
        f.write(json.dumps({'t': time.time(), 'kind': kind, 'pid': os.getpid(), **kw}) + '\\n')

class Place:
    taken = True
    memory_waited = False
    def __init__(self, what, waited):
        self.what, self.waited_s = what, waited
    def add_pid(self, pid):
        _ev('add_pid', what=self.what, linker=pid)
    def release(self):
        _ev('release', what=self.what)
        if not os.environ.get('FAKE_LP_NOLOCK'):
            os.remove(LOCK)

def acquire(what, out=None, limit_min=None, say=None, cmd=None, **kw):
    if os.environ.get('FAKE_LP_TIMEOUT'):
        _ev('timeout', what=what)
        raise LinkPlaceTimeout('gave up after 60 min waiting for a whole-program link place: 1 of 1 place(s) taken '
                               'by:\\n  pid 4242 owner other-owner: x64 DLL release link (build.py) since 15:00\\n'
                               '  nothing was linked; ask the holder\\'s owner, or link later')
    t0 = time.time()
    if not os.environ.get('FAKE_LP_NOLOCK'):
        while True:
            try:
                os.close(os.open(LOCK, os.O_CREAT | os.O_EXCL | os.O_WRONLY))
                break
            except FileExistsError:
                time.sleep(0.02)
    _ev('take', what=what, out=str(out), cmd=cmd, limit_min=limit_min)
    return Place(what, time.time() - t0)
'''

FAKE_LLD = '''
import json, os, sys, time
def ev(kind, **kw):
    with open(os.path.join(os.environ['FAKE_LP_EVENTS'], '%d.jsonl' % os.getpid()), 'a', encoding='utf-8') as f:
        f.write(json.dumps({'t': time.time(), 'kind': kind, 'pid': os.getpid(),
                            'name': os.environ.get('FAKE_BUILD_NAME'), **kw}) + '\\n')
ev('link-start', args=sys.argv[1:])
time.sleep(float(os.environ.get('FAKE_LINK_S', '0.6')))
ev('link-end')
print('lld-link: linked ' + os.environ.get('FAKE_BUILD_NAME', ''))
sys.exit(int(os.environ.get('FAKE_LINK_RC', '0')))
'''

# build_vp_clang.py's shape: a non-link subprocess.run (update_commit_id.bat), the compiles, one subprocess.run of a
# string command naming lld-link with capture_output (link_dll), sys.exit(1) on a failed link
FAKE_VP_CLANG = '''
import argparse, json, os, subprocess, sys, time
from pathlib import Path
def ev(kind, **kw):
    with open(os.path.join(os.environ['FAKE_LP_EVENTS'], '%d.jsonl' % os.getpid()), 'a', encoding='utf-8') as f:
        f.write(json.dumps({'t': time.time(), 'kind': kind, 'pid': os.getpid(),
                            'name': os.environ.get('FAKE_BUILD_NAME'), **kw}) + '\\n')
ap = argparse.ArgumentParser()
ap.add_argument('--config', default='debug', choices=['release', 'debug'])
args = ap.parse_args()
ev('argv', argv=sys.argv, config=args.config, cwd=str(Path.cwd()), main=__name__)
cp = subprocess.run([sys.executable, '-c', 'print("commit id")'], capture_output=True)
ev('nonlink', out=cp.stdout.decode().strip(), rc=cp.returncode, type=type(cp).__name__)
ev('compile-start')
time.sleep(float(os.environ.get('FAKE_COMPILE_S', '1.5')))
ev('compile-end')
if os.environ.get('FAKE_SCRIPT_EXIT'):
    sys.exit(int(os.environ['FAKE_SCRIPT_EXIT']))
if os.environ.get('FAKE_SCRIPT_RAISE'):
    raise RuntimeError('the fake script raised')
if os.environ.get('FAKE_NO_LINK'):
    sys.exit(0)
rsp = Path.cwd() / 'link'
rsp.write_text('/OUT:x.dll')
command = f'"{sys.executable}" "{os.environ["FAKE_LLD"]}" @"{rsp}"'
cp = subprocess.run(command, capture_output=True)
ev('link-returned', rc=cp.returncode, stdout=cp.stdout.decode().strip(), type=type(cp).__name__)
if cp.returncode != 0:
    print('linking dll failed - see build log')
    sys.exit(1)
print('linking dll finished')
'''


def overlap(a, b):
    return a[0] < b[1] and b[0] < a[1]


class Wrapped(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = Path(self._td.name)
        self.events = self.tmp / 'events'
        self.events.mkdir()
        self.lp = self.tmp / 'fake_linkplace.py'
        self.lp.write_text(FAKE_LINKPLACE)
        self.lld = self.tmp / 'llvm' / 'lld-link.py'
        self.lld.parent.mkdir()
        self.lld.write_text(FAKE_LLD)

    def tearDown(self):
        self._td.cleanup()

    def build_dir(self, name):
        d = self.tmp / name
        d.mkdir(exist_ok=True)
        (d / 'build_vp_clang.py').write_text(FAKE_VP_CLANG)
        return d

    def env(self, name, **kw):
        e = {k: v for k, v in os.environ.items() if not k.startswith('FAKE_')}
        e.update(FAKE_LP_EVENTS=str(self.events), FAKE_LLD=str(self.lld), FAKE_BUILD_NAME=name, **kw)
        return e

    def cmd(self, name, *extra):
        return [PY, '-I', str(LINKWRAP), '--what', f'test 32-bit DLL link {name}', '--out', str(self.tmp / name),
                '--record', str(self.tmp / f'{name}.json'), '--linkplace', str(self.lp), *extra, '--',
                'build_vp_clang.py', '--config', 'release']

    def start(self, name, *extra, **env):
        d = self.build_dir(name)
        return subprocess.Popen(self.cmd(name, *extra), cwd=d, env=self.env(name, **env), stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)

    def run1(self, name, *extra, **env):
        p = self.start(name, *extra, **env)
        out, _ = p.communicate(timeout=60)
        return p.returncode, out

    def ev(self):
        """Every event of every process (one file per process: appends of two processes to one file interleave on
        Windows), in time order."""
        evs = [json.loads(ln) for f in self.events.glob('*.jsonl') for ln in f.read_text(encoding='utf-8').splitlines()]
        return sorted(evs, key=lambda e: e['t'])

    def interval(self, evs, name, a, b):
        t0 = [e['t'] for e in evs if e['kind'] == a and e.get('name') == name]
        t1 = [e['t'] for e in evs if e['kind'] == b and e.get('name') == name]
        self.assertEqual((len(t0), len(t1)), (1, 1), (name, a, b, evs))
        return t0[0], t1[0]

    def two_at_once(self, **env):
        ps = [self.start(n, **env) for n in ('A', 'B')]
        outs = [p.communicate(timeout=120)[0] for p in ps]
        self.assertEqual([p.returncode for p in ps], [0, 0], outs)
        evs = self.ev()
        comp = [self.interval(evs, n, 'compile-start', 'compile-end') for n in ('A', 'B')]
        link = [self.interval(evs, n, 'link-start', 'link-end') for n in ('A', 'B')]
        return evs, comp, link, outs

    # ---- the contract

    def test_two_builds_overlap_in_compile_never_in_link(self):
        # 4 s compiles: the two processes may start more than a second apart on a loaded machine (ticket 5024 take 4)
        evs, comp, link, outs = self.two_at_once(FAKE_COMPILE_S='4')
        self.assertTrue(overlap(*comp), comp)                   # the compiles ran side by side
        self.assertFalse(overlap(*link), link)                  # the links one after the other
        takes = [e for e in evs if e['kind'] == 'take']
        self.assertEqual(len(takes), 2, evs)                    # one place per link, none for anything else
        for n in ('A', 'B'):
            take = next(e for e in takes if e['what'] == f'test 32-bit DLL link {n}')
            rel = next(e for e in evs if e['kind'] == 'release' and e['what'] == take['what'])
            ls, le = self.interval(evs, n, 'link-start', 'link-end')
            self.assertLess(take['t'], ls)                      # the place before the linker
            self.assertGreater(rel['t'], le)                    # released after it
            cs, ce = self.interval(evs, n, 'compile-start', 'compile-end')
            self.assertGreater(take['t'], ce)                   # not around the compile (4252's way)
            # add_pid names the linker process itself
            add = next(e for e in evs if e['kind'] == 'add_pid' and e['what'] == take['what'])
            lpid = next(e['pid'] for e in evs if e['kind'] == 'link-start' and e['name'] == n)
            self.assertEqual(add['linker'], lpid)
            self.assertEqual(take['out'], str(self.tmp / n))
            self.assertIn('lld-link', take['cmd'])
        for o in outs:
            self.assertIn('linkwrap: link: place taken', o)
            self.assertIn('linking dll finished', o)

    def test_negative_control_without_the_place_links_overlap(self):
        """The same two builds with a place that locks nothing: the links overlap, so the test above can see it."""
        # ticket 5024, take 4 (2026-11-23 01:3x): under the suites barrier's load B started 1.5 s after A, so 0.8 s
        # compiles did not overlap; 4 s compiles and 5 s links leave room for a slow start
        _, comp, link, _ = self.two_at_once(FAKE_LP_NOLOCK='1', FAKE_COMPILE_S='4', FAKE_LINK_S='5')
        self.assertTrue(overlap(*comp), comp)
        self.assertTrue(overlap(*link), link)

    def test_the_script_runs_as_main_with_its_arguments_and_folder(self):
        rc, out = self.run1('A', FAKE_COMPILE_S='0')
        self.assertEqual(rc, 0, out)
        a = next(e for e in self.ev() if e['kind'] == 'argv')
        self.assertEqual(a['argv'], ['build_vp_clang.py', '--config', 'release'])
        self.assertEqual(a['config'], 'release')
        self.assertEqual(a['main'], '__main__')
        self.assertEqual(Path(a['cwd']), self.tmp / 'A')

    def test_capture_output_honoured_and_a_completed_process_returned(self):
        rc, out = self.run1('A', FAKE_COMPILE_S='0')
        self.assertEqual(rc, 0, out)
        r = next(e for e in self.ev() if e['kind'] == 'link-returned')
        self.assertEqual(r['stdout'], 'lld-link: linked A')     # captured, not printed
        self.assertEqual(r['type'], 'CompletedProcess')
        self.assertNotIn('lld-link: linked', out)

    def test_a_non_link_call_is_untouched(self):
        rc, out = self.run1('A', FAKE_COMPILE_S='0')
        self.assertEqual(rc, 0, out)
        evs = self.ev()
        n = next(e for e in evs if e['kind'] == 'nonlink')
        self.assertEqual((n['out'], n['rc'], n['type']), ('commit id', 0, 'CompletedProcess'))
        self.assertEqual(len([e for e in evs if e['kind'] == 'take']), 1)
        self.assertLess(n['t'], next(e['t'] for e in evs if e['kind'] == 'take'))

    def test_exit_code_passed_through(self):
        rc, out = self.run1('A', FAKE_COMPILE_S='0', FAKE_SCRIPT_EXIT='5')
        self.assertEqual(rc, 5, out)
        rc, out = self.run1('B', FAKE_COMPILE_S='0', FAKE_LINK_RC='3')       # a failed link: the script's exit 1
        self.assertEqual(rc, 1, out)
        self.assertIn('linking dll failed', out)
        rel = [e for e in self.ev() if e['kind'] == 'release' and e['what'].endswith(' B')]
        self.assertEqual(len(rel), 1)                                       # released after a failed link too
        rc, out = self.run1('C', FAKE_COMPILE_S='0', FAKE_SCRIPT_RAISE='1')  # an exception: 1 and its traceback
        self.assertEqual(rc, 1, out)
        self.assertIn('RuntimeError: the fake script raised', out)

    def test_record_names_the_wait_and_the_link(self):
        rc, out = self.run1('A', FAKE_COMPILE_S='0')
        self.assertEqual(rc, 0, out)
        r = json.loads((self.tmp / 'A.json').read_text())
        self.assertEqual(r['rc'], 0)
        self.assertEqual(len(r['links']), 1)
        link = r['links'][0]
        self.assertEqual((link['rc'], link['place_taken']), (0, True))
        self.assertGreaterEqual(link['link_s'], 0.5)
        self.assertGreaterEqual(link['waited_s'], 0)

    def test_link_place_timeout_is_loud(self):
        rc, out = self.run1('A', FAKE_COMPILE_S='0', FAKE_LP_TIMEOUT='1')
        self.assertEqual(rc, linkwrap.EXIT_TIMEOUT, out)
        lines = [ln for ln in out.splitlines() if 'LINK PLACE TIMEOUT' in ln]
        self.assertEqual(len(lines), 1, out)                       # one line ...
        for needle in ('pid 4242', 'other-owner', 'x64 DLL release link', 'nothing was linked',
                       'linkplace.py status', 'run the build again'):
            self.assertIn(needle, lines[0])                        # ... naming the holders and what to do
        evs = self.ev()
        self.assertFalse([e for e in evs if e['kind'] in ('link-start', 'link-returned')])   # nothing linked
        r = json.loads((self.tmp / 'A.json').read_text())
        self.assertEqual((r['rc'], r['error']), (linkwrap.EXIT_TIMEOUT, 'link place timeout'))

    def test_a_script_that_never_links_is_loud(self):
        rc, out = self.run1('A', FAKE_COMPILE_S='0', FAKE_NO_LINK='1')
        self.assertEqual(rc, linkwrap.EXIT_NO_LINK, out)
        self.assertIn('NO LINK', out)
        rc, out = self.run1('B', '--allow-no-link', FAKE_COMPILE_S='0', FAKE_NO_LINK='1')
        self.assertEqual(rc, 0, out)


class InProcess(unittest.TestCase):
    def test_is_link(self):
        fork_cmd = ('"C:\\Program Files (x86)\\Microsoft Visual Studio 9.0\\Common7\\Tools\\vsvars32.bat">NUL && '
                    'lld-link.exe @"C:\\x\\clang-build\\Release\\link"')
        self.assertTrue(linkwrap.is_link(fork_cmd))
        self.assertTrue(linkwrap.is_link(['C:\\Program Files\\LLVM\\bin\\lld-link.exe', '@rsp']))
        self.assertTrue(linkwrap.is_link([Path('lld-link'), '@rsp']))
        self.assertTrue(linkwrap.is_link(b'lld-link /dll'))
        compile_cmd = ('"C:\\vs\\vsvars32.bat">NUL && clang-cl.exe "C:\\x\\CvGameCoreDLL_Expansion2\\CvCity.cpp" '
                       '/Fo"C:\\x\\clang-build\\Release\\CvCity.obj" /Yu"CvGameCoreDLLPCH.h" -flto')
        self.assertFalse(linkwrap.is_link(compile_cmd))
        self.assertFalse(linkwrap.is_link('update_commit_id.bat'))
        self.assertFalse(linkwrap.is_link('mylld-linker.exe'))
        self.assertFalse(linkwrap.is_link(['link.exe', '/dll']))

    def test_non_link_call_passes_through_with_its_arguments(self):
        real = mock.Mock(return_value='ran')
        hook = linkwrap.Hook('w', None, None, linkplace_path=Path('nowhere.py'), real_run=real)
        self.assertEqual(hook.run(['clang-cl.exe', 'a.cpp'], capture_output=True, cwd='x'), 'ran')
        real.assert_called_once_with(['clang-cl.exe', 'a.cpp'], capture_output=True, cwd='x')
        self.assertEqual(hook.links, [])
        self.assertIsNone(hook._lp)                       # linkplace not even loaded

    def test_check_raises_and_releases(self):
        with tempfile.TemporaryDirectory() as td:
            lp = Path(td, 'lp.py')
            lp.write_text(FAKE_LINKPLACE)
            ev = Path(td, 'ev')
            ev.mkdir()
            with mock.patch.dict(os.environ, {'FAKE_LP_EVENTS': str(ev)}):
                hook = linkwrap.Hook('w', td, None, linkplace_path=lp)
                with self.assertRaises(subprocess.CalledProcessError) as cm:
                    hook.run([PY, '-c', 'import sys; print("x"); sys.exit(2)', 'lld-link'], capture_output=True,
                             check=True)
                self.assertEqual(cm.exception.returncode, 2)
                self.assertEqual(cm.exception.output.strip(), b'x')
                kinds = [json.loads(ln)['kind'] for ln in (ev / f'{os.getpid()}.jsonl').read_text().splitlines()]
                self.assertEqual(kinds, ['take', 'add_pid', 'release'])
                self.assertFalse(Path(str(ev) + '.lock').exists())
                self.assertEqual(hook.links[0]['rc'], 2)

    def test_subprocess_run_and_argv_restored(self):
        real, argv, path = subprocess.run, list(sys.argv), list(sys.path)
        with tempfile.TemporaryDirectory() as td:
            s = Path(td, 'nolink.py')
            s.write_text('import subprocess, sys\nassert subprocess.run.__qualname__ == "Hook.run"\n'
                         'assert sys.argv[1:] == ["--x"]\n')
            self.assertEqual(linkwrap.main(['--allow-no-link', '--linkplace', str(Path(td, 'none.py')), str(s),
                                            '--x']), 0)
        self.assertIs(subprocess.run, real)
        self.assertEqual(sys.argv, argv)
        self.assertEqual(sys.path, path)

    def test_default_linkplace_is_perf_quietlock(self):
        mod = linkwrap.load_linkplace(None)
        self.assertEqual(Path(mod.__file__).resolve(), linkwrap.LINKPLACE.resolve())
        self.assertTrue(hasattr(mod, 'acquire') and hasattr(mod, 'LinkPlaceTimeout'))

    def test_usage(self):
        self.assertEqual(linkwrap.main(['--what']), linkwrap.EXIT_USAGE)
        self.assertEqual(linkwrap.main([]), linkwrap.EXIT_USAGE)
        o, s, a = linkwrap.parse(['--what', 'w', '--link-wait-min=5', '--', 'b.py', '--config', 'release'])
        self.assertEqual((o['what'], o['wait_min'], s, a), ('w', 5.0, 'b.py', ['--config', 'release']))
        o, s, a = linkwrap.parse(['b.py', '--what', 'x'])                 # options after the script are its own
        self.assertEqual((o['what'], s, a), (None, 'b.py', ['--what', 'x']))


if __name__ == '__main__':
    unittest.main()
