"""Build (and with --run, run) the tests of the offline scene harness's bundle library and sources (epic 0301).
Written 2026-11-15 for this repository (GPL-3.0-or-later). Owner: task 3902; task 3903 adds its own test files here and
does not edit this script.

    python -I perf/quietlock/quietlock.py run --who 3902 --what "scenebundle tests" -- \
        python -I server/tests/render/scenebundle/build_scenebundle_tests.py --out DIR [--asan] [--run]

What it builds:
1. c5render (render/build.py --target c5render, its own gate) into DIR/c5render (DIR/c5render-asan with --asan), unless
   --c5render names a folder holding a c5render.lib of the same variant.
2. The common objects: every server/src/render/*.cpp, shell/frame/ring_sink.cpp, server/src/feed/srv_feed.cpp and
   options/src/vp64_options.cpp (the units render_host_test.exe links, build_render_tests.py), and since issue 0440 every
   server/src/units/*.cpp (the server's unit scene; c5r_models.lib beside c5render.lib), compiled once.
3. One test program per `*.cpp` in this folder (glob): DIR/bin[-asan]/<stem>.exe = <stem>.cpp + the common objects +
   c5render + the D3D libraries. A header beside the tests (*.h) is shared by them and is not a test.
   The support library DIR/obj[-asan]/support/scenebundle_support.lib holds server.exe's `SrvOptions()` (every option at
   its default, as server/src/srv_options.cpp before a load): the render units call it. Being a library member, it is
   linked only when the test does not define `SrvOptions()` itself.
Normal: clang-cl x64 (the server's toolchain, tools/x64-dll/build.py) /std:c++14 /W3 /WX /O2 /MD. --asan: MSVC cl
/fsanitize=address /Zi /Oy- (the clang-cl ASan runtime crashes here, render/README.md), with the ASan runtime copied
beside the binaries (issue 0149).

--run: runs every built test program in name order, each as `<exe> --work DIR/work[-asan]/<stem>` (the folder is
emptied first; a test writes only there), with a timeout of --timeout s each (default 600), except the programs in
TIMEOUTS, which get at least their own value: sceneshot_test 7200 s (ticket 0800; was 1500 s under ticket 0482: it took
10 min 43 s alone under load on 2026-11-17, and ran past 3600 s beside nine heavy jobs four times on 2026-11-25/26).
--timeout-scale F (default 1, ticket 0800) multiplies every program's limit, TIMEOUTS' values included, for a loaded
machine. THE CONVENTION every test program here
follows: exit 0, and a last output line exactly `RESULT: <n> passed, 0 failed, 0 skipped` with n > 0. Anything else (a
non-zero exit, a missing RESULT line, a failed or skipped count) fails the run: skipped counts as failed (COMMON 9).
A timeout is NOT A RESULT (ticket 0800): the program gave no verdict, and it is reported as such, never as a failed
test: a line `build_scenebundle_tests: TIMEOUT <exe>: no verdict after <s> s (NOT A RESULT; ...)`, and when timeouts
are the only problem the last line is `build_scenebundle_tests: NO VERDICT: ...` with exit 4. Every --run/--run-only ends
with `build_scenebundle_tests: SUMMARY <p> passed, <f> failed, <t> timed out of <n> test program(s)` before its last
line. A line `PYCHECK asset_set <install> <machine> <vt> <vt_hash> <vt_files>` in a test's output is checked here
against tools/visualcheck/visualcheck.py asset_set() on the same folder (the C++ port must give the same answer).
Under --asan any `ERROR: AddressSanitizer` in the output fails the run too.
--run-only (ticket 0482): builds nothing and runs the programs already in DIR/bin[-asan] the same way (a missing one
fails); server/tests/render/run_all.py builds once and runs with it.

Exit 0 built (and passed with --run; last line `build_scenebundle_tests: OK: ...`), 1 a step or a test failed (last line
`build_scenebundle_tests: FAILED: <why>`), 3 the launch gate refused, 4 no test failed but one or more timed out (last
line `build_scenebundle_tests: NO VERDICT: ...`; rerun the named program alone with a larger --timeout or
--timeout-scale, ticket 0800).
"""
import argparse
import importlib.util
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
NOWIN = 0x08000000  # CREATE_NO_WINDOW
# Issue 0440: the server's unit scene (server/src/units/*.cpp, as server.exe and unitfeed.exe compile it) is a common unit
# too, so that sceneshot and its test replay render_units=1 bundles; it links c5r_models.lib (render/build.py lays it out
# beside c5render.lib as <render build>/src/units/c5r_models.lib) and needs models/include (c5r/units/draw.h).
COMMON_SRC = sorted((REPO / 'server' / 'src' / 'render').glob('*.cpp')) + [
    REPO / 'shell' / 'frame' / 'ring_sink.cpp', REPO / 'server' / 'src' / 'feed' / 'srv_feed.cpp',
    REPO / 'options' / 'src' / 'vp64_options.cpp'] + sorted((REPO / 'server' / 'src' / 'units').glob('*.cpp'))
# Ticket 4722 (epic 0042): srv_units_anim.cpp and srv_units_scene.cpp call the trigger tap and stream in server/src/effects/,
# so every builder that compiles server/src/units/*.cpp also compiles server/src/effects/*.cpp (as build_server.py does)
COMMON_SRC += sorted((REPO / 'server' / 'src' / 'effects').glob('*.cpp'))
# Ticket 4875 (format version 9): the model module (server/src/render/landmarks/, cities/, as server.exe compiles them), so
# that the player and its tests replay render_cities / render_landmarks bundles (modeltap/ members)
COMMON_SRC += sorted((REPO / 'server' / 'src' / 'render' / 'landmarks').glob('*.cpp'))
COMMON_SRC += sorted((REPO / 'server' / 'src' / 'render' / 'cities').glob('*.cpp'))
INCLUDES = [REPO / 'server' / 'src' / 'render', REPO / 'server' / 'src', REPO / 'shell' / 'common', REPO / 'shell' / 'frame',
            REPO / 'render' / 'include', REPO / 'options' / 'include', REPO / 'models' / 'include', HERE]


def models_lib(c5render_dir):
    """c5r_models.lib of a render build folder (issue 0440: the unit scene's link dependency)."""
    return Path(c5render_dir) / 'src' / 'units' / 'c5r_models.lib'
LIBS = ['d3d11.lib', 'dxgi.lib', 'dxguid.lib', 'd3dcompiler.lib', 'windowscodecs.lib', 'ole32.lib', 'psapi.lib',
        'user32.lib', 'advapi32.lib', 'kernel32.lib', 'shell32.lib']
ASAN_RUNTIME = ['clang_rt.asan_dynamic-x86_64.dll', 'clang_rt.asan_dynamic-x86_64.pdb']
RESULT_RE = re.compile(r'^RESULT: (\d+) passed, (\d+) failed, (\d+) skipped$')
# Ticket 0482: per-program timeouts in seconds, a floor over --timeout (a larger --timeout still wins). Ticket 0800:
# sceneshot_test 7200 s (was 1500 s; it ran past 3600 s on a loaded workstation four times on 2026-11-25/26).
TIMEOUTS = {'sceneshot_test': 7200}
# Ticket 0800: a timeout is not a test result; exit 4 when timeouts are the only problem of a run.
EXIT_NO_VERDICT = 4


def timeout_for(stem, default, scale=1.0):
    """The timeout of one test program: TIMEOUTS' value for its stem if larger than the default, else the default; times
    --timeout-scale (ticket 0800), at least 1 s."""
    return max(1, int(round(max(default, TIMEOUTS.get(stem, 0)) * scale)))
SUPPORT_CPP = '''// generated by server/tests/render/scenebundle/build_scenebundle_tests.py: server.exe's SrvOptions() for the tests
#include "vp64_options.h"
static Vp64Opt::Options g_options;   // every option at its default (Options() calls SetDefaults)
const Vp64Opt::Options& SrvOptions() { return g_options; }
'''


def fail(msg):
    print(f'build_scenebundle_tests: FAILED: {msg}', flush=True)
    sys.exit(1)


def run(cmd, **kw):
    print('+ ' + ' '.join(str(c) for c in cmd)[:600], flush=True)
    cp = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, errors='replace', creationflags=NOWIN, **kw)
    if (cp.stdout + cp.stderr).strip():
        print((cp.stdout + cp.stderr).rstrip()[-6000:], flush=True)
    return cp.returncode


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


def check_asset_set(line):
    """PYCHECK asset_set <install> <machine> <vt> <vt_hash> <vt_files>: the C++ port against visualcheck.asset_set()."""
    parts = line.split(' ')
    if len(parts) != 7:
        return f'malformed PYCHECK line: {line!r}'
    _, _, install, machine, vt, vt_hash, vt_files = parts
    vc = load_module('visualcheck_for_scenebundle', REPO / 'tools' / 'visualcheck' / 'visualcheck.py')
    with tempfile.TemporaryDirectory() as td:
        got = vc.asset_set(Path(install), machine, vc.HashCache(Path(td)))
    want = {'vt': got.get('vt'), 'vt_hash': got.get('vt_hash') or '-', 'vt_files': str(got.get('vt_files'))}
    have = {'vt': vt.replace('_', ' '), 'vt_hash': vt_hash, 'vt_files': vt_files}
    if want != have:
        return f'asset_set differs on {install}: visualcheck.py {want}, C++ {have}'
    print(f'build_scenebundle_tests: PYCHECK asset_set OK ({install}: {want})', flush=True)
    return None


def run_tests(bdir, work_root, exes, asan, timeout, scale=1.0):
    """Runs the programs; returns (failures, timeouts): failures are test results ('<exe>: <why>'), timeouts are the
    programs that gave no verdict ('<exe> after <s> s'; ticket 0800: NOT A RESULT, never counted as a failure)."""
    failures, timeouts = [], []
    for exe in exes:
        work = work_root / exe.stem
        if work.exists():
            shutil.rmtree(work)
        work.mkdir(parents=True)
        limit = timeout_for(exe.stem, timeout, scale)
        print(f'build_scenebundle_tests: RUN {exe.name} (timeout {limit} s)', flush=True)
        try:
            cp = subprocess.run([str(exe), '--work', str(work)], capture_output=True, text=True, errors='replace',
                                creationflags=NOWIN, cwd=str(bdir), timeout=limit)
        except subprocess.TimeoutExpired:
            timeouts.append(f'{exe.name} after {limit} s')
            print(f'build_scenebundle_tests: TIMEOUT {exe.name}: no verdict after {limit} s (NOT A RESULT; rerun it alone '
                  f'with --only {exe.stem} and a larger --timeout or --timeout-scale)', flush=True)
            continue
        out = (cp.stdout + cp.stderr).rstrip()
        print(out[-12000:], flush=True)
        lines = [ln.rstrip() for ln in out.splitlines() if ln.strip()]
        m = RESULT_RE.match(lines[-1]) if lines else None
        if cp.returncode != 0:
            failures.append(f'{exe.name}: exit {cp.returncode}')
        elif not m:
            failures.append(f'{exe.name}: no RESULT line at the end')
        elif int(m.group(1)) == 0 or int(m.group(2)) or int(m.group(3)):
            failures.append(f'{exe.name}: {lines[-1]}')
        if asan and 'ERROR: AddressSanitizer' in out:
            failures.append(f'{exe.name}: AddressSanitizer report')
        for ln in lines:
            if ln.startswith('PYCHECK asset_set '):
                why = check_asset_set(ln)
                if why:
                    failures.append(f'{exe.name}: {why}')
    return failures, timeouts


def finish_run(bdir, exes, failures, timeouts):
    """The end of a --run/--run-only: FAIL and SUMMARY lines, then OK (exit 0), FAILED (exit 1) or, when timeouts are the
    only problem, NO VERDICT (exit 4, ticket 0800)."""
    for f in failures:
        print(f'build_scenebundle_tests: FAIL {f}', flush=True)
    bad = {f.split(':', 1)[0] for f in failures} | {t.split(' after ', 1)[0] for t in timeouts}
    print(f'build_scenebundle_tests: SUMMARY {len(exes) - len(bad)} passed, {len({f.split(":", 1)[0] for f in failures})} '
          f'failed, {len(timeouts)} timed out of {len(exes)} test program(s)', flush=True)
    tail = f'; timed out (no verdict, NOT A RESULT): {", ".join(timeouts)}' if timeouts else ''
    if failures:
        fail(f'{len(failures)} failure(s) in {len(exes)} test program(s){tail}')
    if timeouts:
        print(f'build_scenebundle_tests: NO VERDICT: {len(timeouts)} of {len(exes)} test program(s) timed out: '
              f'{", ".join(timeouts)}' + (f'; the other {len(exes) - len(timeouts)} passed' if len(exes) > len(timeouts)
                                          else ''), flush=True)
        sys.exit(EXIT_NO_VERDICT)
    print(f'build_scenebundle_tests: OK: {len(exes)} test program(s) passed in {bdir}', flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--asan', action='store_true')
    ap.add_argument('--c5render', type=Path, help='a folder holding c5render.lib of the same variant (else built)')
    ap.add_argument('--run', action='store_true', help='run every built test program (see the convention above)')
    ap.add_argument('--only', action='append', default=[], help='build and run only these test stems (repeatable)')
    ap.add_argument('--timeout', type=int, default=600,
                    help='seconds per test program (default 600); TIMEOUTS raise it for their programs')
    ap.add_argument('--timeout-scale', type=float, default=1.0, metavar='F',
                    help='multiply every program\'s timeout, TIMEOUTS included (default 1; ticket 0800, a loaded machine)')
    ap.add_argument('--run-only', action='store_true',
                    help='build nothing: run the test programs already in DIR/bin[-asan] (ticket 0482, run_all.py); '
                         'a missing program fails')
    a = ap.parse_args()
    sys.path.insert(0, str(REPO / 'perf' / 'quietlock'))
    try:
        import gate
        gate.require('scenebundle tests ' + ('run' if a.run_only else 'build') +
                     ' (server/tests/render/scenebundle/build_scenebundle_tests.py)')
    except ImportError:
        print('launch gate: warning: perf/quietlock/gate.py not found; launch goes on', file=sys.stderr)
    tests = sorted(HERE.glob('*.cpp'))
    if a.only:
        tests = [t for t in tests if t.stem in a.only]
    if not tests:
        fail('no test *.cpp in ' + str(HERE))
    out = a.out.resolve()
    if a.run_only:
        sfx = '-asan' if a.asan else ''
        bdir = out / ('bin' + sfx)
        exes = [bdir / (t.stem + '.exe') for t in tests]
        missing = [e.name for e in exes if not e.exists()]
        if missing:
            fail(f'--run-only: not built in {bdir}: ' + ', '.join(missing))
        failures, timeouts = run_tests(bdir, out / ('work' + sfx), exes, a.asan, a.timeout, a.timeout_scale)
        finish_run(bdir, exes, failures, timeouts)
        return
    out.mkdir(parents=True, exist_ok=True)
    rdir = a.c5render.resolve() if a.c5render else out / ('c5render-asan' if a.asan else 'c5render')
    if not a.c5render:
        cmd = [sys.executable, '-I', str(REPO / 'render' / 'build.py'), '--build-dir', str(rdir), '--target', 'c5render']
        if a.asan:
            cmd.append('--asan')
        if run(cmd, cwd=str(REPO)) != 0:
            fail('render/build.py --target c5render')
    lib = rdir / 'c5render.lib'
    if not lib.exists():
        fail(f'{lib} missing')
    mlib = models_lib(rdir)
    if not mlib.exists():
        fail(f'{mlib} missing (the unit scene links it: render/build.py --target c5render builds it)')
    x64build = load_module('x64build', REPO / 'tools' / 'x64-dll' / 'build.py')
    env = x64build.load_vcvars_env(lambda m, echo=True: print(m), out / 'vcvars-env.json')
    env = dict(env, VSCMD_SKIP_SENDTELEMETRY='1')
    sfx = '-asan' if a.asan else ''
    bdir, odir = out / ('bin' + sfx), out / ('obj' + sfx)
    bdir.mkdir(parents=True, exist_ok=True)
    odir.mkdir(parents=True, exist_ok=True)
    cl = None
    if a.asan:
        cl = shutil.which('cl.exe', path=env.get('PATH') or env.get('Path'))
        if not cl:
            fail('cl.exe not on the vcvarsall PATH')
        cc = [cl, '/nologo', '/MD', '/EHsc', '/std:c++14', '/W3', '/WX', '/Zi', '/Oy-', '/O2', '/fsanitize=address',
              '/D_CRT_SECURE_NO_WARNINGS']
    else:
        cc = [str(x64build.CLANG_CL), '-m64', '/nologo', '/MD', '/EHsc', '/std:c++14', '/W3', '/WX', '/O2', '/Zi',
              '-fuse-ld=lld', '/D_CRT_SECURE_NO_WARNINGS']
    inc = [f'/I{p}' for p in INCLUDES]
    cdir = odir / 'common'
    cdir.mkdir(parents=True, exist_ok=True)
    par = ['/MP'] if a.asan else []
    if run(cc + par + inc + ['/c', f'/Fo{cdir}\\', f'/Fd{cdir / "vc.pdb"}', '/FS'] + [str(s) for s in COMMON_SRC],
           env=env, cwd=str(cdir)) != 0:
        fail('the common objects (server/src/render/*.cpp and the units beside them)')
    objs = [cdir / (s.stem + '.obj') for s in COMMON_SRC]
    missing = [o.name for o in objs if not o.exists()]
    if missing:
        fail('common objects missing: ' + ', '.join(missing))
    sdir = odir / 'support'
    sdir.mkdir(parents=True, exist_ok=True)
    (sdir / 'scenebundle_support.cpp').write_text(SUPPORT_CPP, encoding='utf-8')
    if run(cc + inc + ['/c', f'/Fo{sdir}\\', f'/Fd{sdir / "vc.pdb"}', str(sdir / 'scenebundle_support.cpp')],
           env=env, cwd=str(sdir)) != 0:
        fail('scenebundle_support.cpp')
    support = sdir / 'scenebundle_support.lib'
    librarian = str(Path(cl).parent / 'lib.exe') if a.asan else str(Path(x64build.CLANG_CL).parent / 'llvm-lib.exe')
    if run([librarian, '/nologo', f'/OUT:{support}', str(sdir / 'scenebundle_support.obj')], env=env, cwd=str(sdir)) != 0 \
            or not support.exists():
        fail('scenebundle_support.lib')
    exes = []
    for t in tests:
        tdir = odir / t.stem
        tdir.mkdir(parents=True, exist_ok=True)
        exe = bdir / (t.stem + '.exe')
        rc = run(cc + inc + [f'/Fe{exe}', f'/Fo{tdir}\\', f'/Fd{tdir / "vc.pdb"}', str(t)] + [str(o) for o in objs] +
                 ['/link', '/DEBUG', str(lib), str(mlib), str(support)] + LIBS, env=env, cwd=str(tdir))
        if rc != 0 or not exe.exists():
            fail(f'{exe.name}')
        exes.append(exe)
    if a.asan:
        for name in ASAN_RUNTIME:
            src = Path(cl).parent / name
            if src.exists():
                shutil.copy2(src, bdir / name)
            elif name.endswith('.dll'):
                fail(f'{src} missing (the ASan runtime of the cl.exe that built the binaries)')
    print(f'build_scenebundle_tests: built {len(exes)} test program(s) in {bdir}', flush=True)
    if a.run:
        failures, timeouts = run_tests(bdir, out / ('work' + sfx), exes, a.asan, a.timeout, a.timeout_scale)
        finish_run(bdir, exes, failures, timeouts)
    else:
        print(f'build_scenebundle_tests: OK: {bdir}', flush=True)


if __name__ == '__main__':
    main()
