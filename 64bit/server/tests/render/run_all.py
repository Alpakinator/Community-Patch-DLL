"""One runner for the server's render test suites (ticket 0482; auditor cycle 7, H4). Own code, 2026-11-17
(GPL-3.0-or-later). Started from render/evidence/0431/4555/suites_4555.sh, which worked.

    python -I server/tests/render/run_all.py --out DIR [--asan] [--exe EXE] [--game DIR] [--who ID]
                                             [--only SUITE ...] [--no-build] [--main DIR] [--list]
                                             [--sb-timeout S] [--sb-timeout-scale F]

What it does, in a fixed order:
1. Builds every test program the selected suites need, each through its own builder (build_render_tests.py,
   cut_tap/build_cut_tap_tests.py, decal/build_decal_tests.py, ice/build_ice_tests.py --setmesh,
   overlay/test_reader_build.py, shadow/build_shadow_tests.py, fow/test_fow_build.py, overlay/test_scene_build.py,
   shadow/build_scene_shadow.py, water/build_water_tests.py, forest/test_forest_build.py,
   scenebundle/build_scenebundle_tests.py, server/tools/sceneshot/build_sceneshot.py --smoke), into DIR/<name>. c5render
   is built once (render/build.py --target c5render into DIR/rt/c5render[-asan]) and named to every builder that links
   it (--c5render). A build is done once even when several suites need it; a failed build fails every suite needing it.
2. Runs each suite: pytest (python -I -m pytest -q -p no:cacheprovider, PYTEST_DISABLE_PLUGIN_AUTOLOAD=1) with the
   environment variables its modules read set to those builds, or the scene-bundle programs through their builder's
   --run-only (its convention: RESULT line, skipped counts as failed, sceneshot_test 7200 s). Ticket 0800: --sb-timeout S
   is forwarded as the builder's --timeout (seconds per program, default the builder's 600; its TIMEOUTS floor stays) and
   --sb-timeout-scale F as its --timeout-scale (every program's limit times F, sceneshot_test's 7200 s included): on a
   loaded workstation pass e.g. --sb-timeout-scale 2. A program that times out gave no verdict: it is NOT A RESULT and is
   never counted as a failed test (see Output).
Every build and every suite starts through `perf/quietlock/quietlock.py run --who ID` (never --take). A start refused by
the lock or a launch gate ("refused, command not started", "launch gate: REFUSED") waits with `quietlock.py wait` (no
--who, 170 s per call) and starts again, up to --gate-wait minutes in all; a stop file DIR/STOP ends the run before the
next step (TaskStop does not stop the children of a wrapper).

Output: one line per build step, then one summary line per suite
    run_all: SUITE <name> PASS|FAIL|TIMEOUT <passed> passed, <failed> failed, <skipped> skipped[, <errors> errors]
             [, <t> timed out] <s> s [<why>]
TIMEOUT (ticket 0800, scenebundle-programs only): no test failed, but a program gave no verdict in its time; the reason
names it (`sceneshot_test.exe after 7200 s: NOT A RESULT, rerun ...`). The last line is `run_all: OK: <n> suite(s) passed
(<variant>)`, `run_all: FAILED: <k> of <n> suite(s) (<variant>): ...` (with `; timed out (no verdict): ...` when there were
timeouts too), or `run_all: NO VERDICT: <k> of <n> suite(s) timed out (<variant>): ...` when timeouts are the only problem.
Logs: DIR/logs[-asan]/<step>.log; the summary also in DIR/summary[-asan].txt.
Exit 0 every selected suite passed; 1 a suite failed or skipped a test (a skip counts as failed, COMMON 9); 2 usage;
4 no suite failed, but one or more timed out (no verdict; rerun with a larger --sb-timeout-scale); 9 stopped by DIR/STOP.

Variants: plain (clang-cl, the server's toolchain) by default; --asan builds the MSVC /fsanitize=address variant
(DIR/<name>/bin-asan) and runs the same suites on it. ASAN_OPTIONS' log_path is set to DIR/asan-logs/<suite> (quoted,
as server/tests/render/conftest.py does); the conftest's autouse fixture fails a test on a new report, and a suite with
any report file fails here too. Three suites' modules read only <build>/bin (overlay-scene, shadow-ice-scene,
water-scene): under --asan their variable names DIR/<name>/asan-view, whose bin is a fresh copy of bin-asan with the
ASan runtime beside it.

Inputs: C5R_EXE (CivilizationV_DX11.exe, read-only) and C5R_GAME_DIR (an installed game, read-only) from --exe/--game or
the environment; there is no default (no local path in this file). C5_GAME_DIR defaults to C5R_GAME_DIR. Ticket 0589
(D200): the EXE, the install's EXE and an inherited C5_GAME_DIR's EXE must be build 4390913 (tools/harness/exe_build.py's
rule, via gameinstall.py; C5_ALLOW_UNKNOWN_EXE_BUILD=1 overrides), else the run stops at the start (exit 2). Inputs some
modules read with their own defaults (C5R_TINY_TAP of test_residency_source.py, C5_ICE_TINY_IMAGE / C5_ICE_HUGE_IMAGE of
test_ice_reader.py, C5_SCENE_LIBRARY of test_offline_equals_live.py) are passed through from the environment.
--main DIR adds the suite unchanged-vs-main (scenebundle/test_host_unchanged.py, test_particles_unchanged.py: A/B checks
of issue 0440 and ticket 0403 against builds from main's files): DIR is the --out of a plain run_all.py run on main
(DIR/rt/bin, DIR/fow). Plain only.

The scene library's working copy (ticket 0611): when C5_SCENE_LIBRARY points into a copy made by
`tools/scenelib/scenelib.py sync` (its parent folder holds scenelib-manifest.json) and offline-equals-live is selected,
`scenelib.py check` runs at the start (a failed check ends the run before any build, exit 1), again just before that
suite and once after it; a failed check fails the suite, so a copy that is missing, unfinished or older than its master
never gives a result. C5_SCENE_LIBRARY on the master (no manifest beside it) or unset: nothing changes.

The frozen asset root (ticket 0625): C5_SCENE_ASSET_ROOT (ticket 0614; teams export it globally) is passed only to the
suites whose players read the library's bundles (offline-equals-live); every build and every other suite runs without it,
because a player started with --install or --mods (sceneshot_test's cases) refuses it as a usage error.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
QL = REPO / 'perf' / 'quietlock' / 'quietlock.py'
ASAN_RUNTIME = 'clang_rt.asan_dynamic-x86_64.dll'
REFUSED = ('refused, command not started', 'launch gate: REFUSED')
PY = [sys.executable, '-I']
T = 'server/tests/render/'


class Ctx:
    def __init__(self, out, asan, main):
        self.out, self.asan, self.main = out, asan, main
        self.sfx = '-asan' if asan else ''

    def d(self, name):
        return self.out / name

    def bin(self, name):
        return self.out / name / ('bin' + self.sfx)

    def c5r(self):
        return self.out / 'rt' / ('c5render' + self.sfx)


def _asan(c):
    return ['--asan'] if c.asan else []


# The builds: name -> (needs, argv(ctx)). Order of this dict is the build order.
BUILDS = {
    'c5render': ([], lambda c: PY + [str(REPO / 'render' / 'build.py'), '--build-dir', str(c.c5r()), '--target',
                                     'c5render'] + _asan(c)),
    'rt': (['c5render'], lambda c: PY + [str(HERE / 'build_render_tests.py'), '--out', str(c.d('rt')),
                                         '--skip-c5render'] + _asan(c)),
    'cut': ([], lambda c: PY + [str(HERE / 'cut_tap' / 'build_cut_tap_tests.py'), '--out', str(c.d('cut'))] + _asan(c)),
    'decal': ([], lambda c: PY + [str(HERE / 'decal' / 'build_decal_tests.py'), '--out', str(c.d('decal'))] + _asan(c)),
    'ice': (['c5render'], lambda c: PY + [str(HERE / 'ice' / 'build_ice_tests.py'), '--out', str(c.d('ice')), '--setmesh',
                                          '--c5render', str(c.c5r())] + _asan(c)),
    'ovr-reader': ([], lambda c: PY + [str(HERE / 'overlay' / 'test_reader_build.py'), '--out', str(c.d('ovr-reader'))]
                   + _asan(c)),
    'shadow-reader': ([], lambda c: PY + [str(HERE / 'shadow' / 'build_shadow_tests.py'), '--out',
                                          str(c.d('shadow-reader'))] + _asan(c)),
    'fow': (['c5render'], lambda c: PY + [str(HERE / 'fow' / 'test_fow_build.py'), '--out', str(c.d('fow')),
                                          '--c5render', str(c.c5r())] + _asan(c)),
    'ovl': (['c5render'], lambda c: PY + [str(HERE / 'overlay' / 'test_scene_build.py'), '--out', str(c.d('ovl')),
                                          '--c5render', str(c.c5r())] + _asan(c)),
    'shadow': (['c5render'], lambda c: PY + [str(HERE / 'shadow' / 'build_scene_shadow.py'), '--out', str(c.d('shadow')),
                                             '--c5render', str(c.c5r())] + _asan(c)),
    'water': (['c5render'], lambda c: PY + [str(HERE / 'water' / 'build_water_tests.py'), '--out', str(c.d('water')),
                                            '--c5render', str(c.c5r())] + _asan(c)),
    'forest': (['c5render'], lambda c: PY + [str(HERE / 'forest' / 'test_forest_build.py'), '--out', str(c.d('forest')),
                                             '--c5render', str(c.c5r())] + _asan(c)),
    'sb': (['c5render'], lambda c: PY + [str(HERE / 'scenebundle' / 'build_scenebundle_tests.py'), '--out', str(c.d('sb')),
                                         '--c5render', str(c.c5r())] + _asan(c)),
    'sceneshot': (['c5render', 'sb'], lambda c: PY + [str(REPO / 'server' / 'tools' / 'sceneshot' / 'build_sceneshot.py'),
                                                      '--out', str(c.d('sceneshot')), '--c5render', str(c.c5r()),
                                                      '--common-objs', str(c.d('sb') / ('obj' + c.sfx) / 'common'),
                                                      '--smoke'] + _asan(c)),
}


def asan_flag(c):
    return '1' if c.asan else '0'


def view(c, name):
    """The folder a bin-only module is pointed at: DIR/<name> plain; DIR/<name>/asan-view (bin = a copy of bin-asan)."""
    return c.d(name) / 'asan-view' if c.asan else c.d(name)


# The suites, in their fixed order: name, builds, kind, what it runs, env(ctx), bin-only views (copied under --asan).
SUITES = [
    ('render-host', ['rt'], 'pytest',
     [T + 'test_render_host.py', T + 'test_residency_source.py', T + 'test_bake_yield.py', T + 'test_bake_fit.py', T + 'test_prep_ahead.py',   # 0712
      T + 'test_decal_names_mods.py', T + 'test_decal_wiring.py', T + 'water/test_water_host.py', T + 'test_gameinstall.py',   # 0589: gameinstall
      T + 'test_run_all.py'],   # 0800: this runner's scene-bundle timeouts
     lambda c: {'C5_RENDER_TEST_BUILD': str(c.bin('rt'))}, []),
    ('cut-tap', ['cut'], 'pytest', [T + 'cut_tap/test_cut_tap_reader.py'],
     lambda c: {'C5_CUT_BUILD': str(c.d('cut')), 'C5_CUT_ASAN': asan_flag(c)}, []),
    ('decal-tap', ['decal'], 'pytest', [T + 'decal/test_decal_tap.py'],
     lambda c: {'C5_DECAL_BUILD': str(c.d('decal')), 'C5_DECAL_ASAN': asan_flag(c)}, []),
    ('ice-reader', ['ice'], 'pytest', [T + 'ice/test_ice_reader.py'],
     lambda c: {'C5_ICE_BUILD': str(c.d('ice')), 'C5_ICE_ASAN': asan_flag(c)}, []),
    ('overlay-reader', ['ovr-reader'], 'pytest', [T + 'overlay/test_reader_overlay.py'],
     lambda c: {'C5_OVERLAY_READER': str(c.bin('ovr-reader') / 'test_reader_tool.exe')}, []),
    ('shadow-reader', ['shadow-reader'], 'pytest', [T + 'shadow/test_reader_shadow.py'],
     lambda c: {'C5_SHADOW_READER': str(c.bin('shadow-reader') / 'shadow_reader_tool.exe')}, []),
    ('fow-scene', ['fow'], 'pytest', [T + 'fow/test_fow_scene.py'],
     lambda c: {'C5_FOW_SCENE_BUILD': str(c.d('fow')), 'C5_FOW_SCENE_ASAN': asan_flag(c)}, []),
    ('overlay-scene', ['ovl'], 'pytest', [T + 'overlay/test_scene_overlay.py'],
     lambda c: {'C5_OVERLAY_SCENE_BUILD': str(view(c, 'ovl'))}, ['ovl']),
    ('shadow-ice-scene', ['shadow'], 'pytest', [T + 'shadow/test_scene_shadow.py', T + 'ice/scene_ice_test.py'],
     lambda c: {'C5_SHADOW_SCENE_BUILD': str(view(c, 'shadow'))}, ['shadow']),
    ('water-scene', ['water'], 'pytest', [T + 'water/test_water_scene.py'],
     lambda c: {'C5_WATER_BUILD': str(view(c, 'water'))}, ['water']),
    ('forest-scene', ['forest'], 'pytest', [T + 'forest/test_forest_scene.py'],
     lambda c: {'C5_FOREST_SCENE_BUILD': str(c.d('forest')), 'C5_FOREST_SCENE_ASAN': asan_flag(c)}, []),
    ('scenebundle-programs', ['sb'], 'sbrun', [], lambda c: {}, []),
    ('scenebundle-py', ['rt', 'sb'], 'pytest',
     [T + 'scenebundle/test_recorder_host.py', T + 'scenebundle/test_sources.py', T + 'scenebundle/test_scenerun.py'],
     lambda c: {'C5_RENDER_TEST_BUILD': str(c.bin('rt')), 'C5_SCENEBUNDLE_TEST_BUILD': str(c.d('sb')),
                'C5_SCENEBUNDLE_ASAN': asan_flag(c)}, []),
    ('offline-equals-live', ['sceneshot'], 'pytest', [T + 'scenebundle/test_offline_equals_live.py'],
     lambda c: {'C5_SCENESHOT_EXE': str(c.bin('sceneshot') / 'sceneshot.exe')}, []),
    ('unchanged-vs-main', ['rt', 'fow'], 'pytest',
     [T + 'scenebundle/test_host_unchanged.py', T + 'scenebundle/test_particles_unchanged.py'],
     lambda c: {'C5_RENDER_TEST_BUILD': str(c.bin('rt')), 'C5_RENDER_TEST_BUILD_MAIN': str(c.main / 'rt' / 'bin'),
                'C5_FOW_SCENE_BUILD': str(c.d('fow')), 'C5_FOW_SCENE_BUILD_MAIN': str(c.main / 'fow')}, []),
]
MAIN_ONLY = 'unchanged-vs-main'
# ticket 0611: the suites that read C5_SCENE_LIBRARY, and the check of a working copy of the library
LIBRARY_SUITES = ('offline-equals-live',)
# ticket 0625: the suites whose players read the library's bundles with the frozen asset root (C5_SCENE_ASSET_ROOT, ticket
# 0614) get the variable; every other build and suite runs without it (sceneshot_test's cases name their own --install)
ASSET_ROOT_VAR = 'C5_SCENE_ASSET_ROOT'
ASSET_ROOT_SUITES = LIBRARY_SUITES
SCENELIB = REPO / 'tools' / 'scenelib' / 'scenelib.py'
SCENELIB_MANIFEST = 'scenelib-manifest.json'


def library_copy(env):
    """The working copy's root when C5_SCENE_LIBRARY points into one (its parent holds scenelib's manifest), else None
    (unset or the master: used as before)."""
    lib = env.get('C5_SCENE_LIBRARY')
    if not lib:
        return None
    root = Path(lib).resolve().parent
    return root if (root / SCENELIB_MANIFEST).is_file() else None


def copy_check(root, env):
    """(ok, scenelib's last line) of `scenelib.py check --copy ROOT` (cheap: index.json and directory listings)."""
    try:
        cp = subprocess.run(PY + ['-X', 'utf8', str(SCENELIB), 'check', '--copy', str(root)], capture_output=True,
                            text=True, errors='replace', env=env, cwd=str(REPO), timeout=900)
    except (OSError, subprocess.SubprocessError) as e:
        return False, 'scenelib check did not run: %s' % e
    lines = [ln for ln in (cp.stdout + cp.stderr).splitlines() if ln.strip()]
    return cp.returncode == 0, (lines[-1] if lines else 'scenelib check: no output, exit %d' % cp.returncode)


def suite_env(env0, name, own, asset_root):
    """A suite's environment: env0 (never holding C5_SCENE_ASSET_ROOT) with the suite's own variables, and the asset root
    only for ASSET_ROOT_SUITES (ticket 0625)."""
    env = dict(env0, **own)
    env.pop(ASSET_ROOT_VAR, None)
    if asset_root and name in ASSET_ROOT_SUITES:
        env[ASSET_ROOT_VAR] = asset_root
    return env


SUMMARY_RE = re.compile(r'(\d+) (passed|failed|skipped|errors?|xfailed|xpassed|deselected)')


def stamp():
    return time.strftime('%H:%M:%S')


def say(msg, sink=None):
    print(msg, flush=True)
    if sink is not None:
        sink.append(msg)


def stopped(c):
    return (c.out / 'STOP').exists()


def gated(c, who, what, argv, env, log, gate_wait_s):
    """argv through `quietlock.py run --who WHO` (no --take); a refused start waits (quietlock.py wait) and starts again.
    Returns the command's exit code; the log gets every attempt."""
    deadline = time.time() + gate_wait_s
    attempt = 0
    while True:
        attempt += 1
        start = log.stat().st_size if log.exists() else 0
        with open(log, 'ab') as f:
            f.write(f'=== attempt {attempt} {time.strftime("%Y-%m-%d %H:%M:%S")}: {" ".join(argv)}\n'.encode())
            f.flush()
            rc = subprocess.call(PY + [str(QL), 'run', '--who', who, '--what', what, '--'] + argv, cwd=str(REPO), env=env,
                                 stdout=f, stderr=subprocess.STDOUT)
        with open(log, 'rb') as f:
            f.seek(start)
            text = f.read().decode('utf-8', 'replace')
        if rc == 0 or not any(r in text for r in REFUSED):
            return rc
        if time.time() >= deadline:
            say(f'run_all: {what}: refused by the lock or a gate; gave up after {gate_wait_s / 60:.0f} min')
            return rc
        say(f'run_all: {what}: refused by the lock or a gate ({stamp()}); waiting (quietlock.py wait), then again')
        free = False
        while not free and time.time() < deadline:
            if stopped(c):
                return rc
            left = max(1.0, min(170.0, deadline - time.time()))
            free = subprocess.call(PY + [str(QL), 'wait', '--timeout', '%.0f' % left], cwd=str(REPO), env=env,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) == 0
        if not free:
            say(f'run_all: {what}: the lock was not free within {gate_wait_s / 60:g} min (--gate-wait); not started')
            return rc
        time.sleep(min(30.0, max(0.0, deadline - time.time())))   # a memory floor refusal: the wait returns at once


def pytest_counts(text):
    """The counts of pytest's last summary line ('N passed, M skipped in 1.2s'), or None if there is none."""
    for ln in reversed(text.splitlines()):
        if re.search(r'\bin [\d.]+s\b', ln) and SUMMARY_RE.search(ln):
            got = {'passed': 0, 'failed': 0, 'skipped': 0, 'errors': 0}
            for n, k in SUMMARY_RE.findall(ln):
                k = 'errors' if k.startswith('error') else k
                if k in got:
                    got[k] += int(n)
            return got
    return None


SB_TIMEOUT_RE = re.compile(r'^build_scenebundle_tests: TIMEOUT (\S+): no verdict after (\d+) s', re.M)
SB_SUMMARY_RE = re.compile(r'^build_scenebundle_tests: SUMMARY (\d+) passed, (\d+) failed, (\d+) timed out', re.M)
SB_NO_VERDICT = 4   # build_scenebundle_tests.py's exit when timeouts are a run's only problem (ticket 0800)


def sb_counts(text):
    """The scene-bundle programs' counts from build_scenebundle_tests.py's output: its SUMMARY line (ticket 0800), else the
    OK line and the FAIL lines (a builder from before 0800); 'timeouts' lists '<exe> after <s> s' (NOT A RESULT)."""
    timeouts = [f'{e} after {s} s' for e, s in SB_TIMEOUT_RE.findall(text)]
    s = SB_SUMMARY_RE.findall(text)
    if s:
        p, f, _t = (int(x) for x in s[-1])
        return {'passed': p, 'failed': f, 'skipped': 0, 'errors': 0, 'timeouts': timeouts}
    m = re.search(r'build_scenebundle_tests: OK: (\d+) test program', text)
    fails = len(re.findall(r'^build_scenebundle_tests: FAIL ', text, re.M))
    return {'passed': int(m.group(1)) if m else 0, 'failed': fails, 'skipped': 0, 'errors': 0, 'timeouts': timeouts}


def sb_argv(c, a):
    """build_scenebundle_tests.py --run-only, with run_all's --sb-timeout / --sb-timeout-scale forwarded (ticket 0800)."""
    argv = PY + [str(HERE / 'scenebundle' / 'build_scenebundle_tests.py'), '--out', str(c.d('sb')), '--run-only'] + _asan(c)
    if a.sb_timeout is not None:
        argv += ['--timeout', str(a.sb_timeout)]
    if a.sb_timeout_scale != 1.0:
        argv += ['--timeout-scale', '%g' % a.sb_timeout_scale]
    return argv


def verdict(n, rc, kind):
    """(status, reasons) of one suite run from its counts and exit code: PASS, FAIL, or TIMEOUT (ticket 0800: the
    scene-bundle programs that timed out gave no verdict; with no failure besides them the suite is TIMEOUT, never FAIL)."""
    reasons = []
    timeouts = n.get('timeouts') or []
    if rc != 0 and not (kind == 'sbrun' and rc == SB_NO_VERDICT and timeouts):
        reasons.append(f'exit {rc}')
    if n['skipped']:
        reasons.append('skipped counts as failed')
    if n['passed'] == 0 and not timeouts:
        reasons.append('nothing passed')
    return reasons, timeouts


def make_view(c, name):
    """Under --asan: DIR/<name>/asan-view/bin = a fresh copy of DIR/<name>/bin-asan (for modules that read bin/ only)."""
    src, dst = c.d(name) / 'bin-asan', c.d(name) / 'asan-view' / 'bin'
    if not (src / ASAN_RUNTIME).exists():
        return f'{src} has no {ASAN_RUNTIME} (not an ASan build)'
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst)
    return None


def parse_args(argv=None):
    names = [s[0] for s in SUITES]
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, help='the build and log folder (C:/c5-64bit-work/<ticket>/..., COMMON 15)')
    ap.add_argument('--asan', action='store_true', help='the MSVC /fsanitize=address variant')
    ap.add_argument('--exe', help='CivilizationV_DX11.exe, read-only (else C5R_EXE)')
    ap.add_argument('--game', help='an installed game, read-only (else C5R_GAME_DIR)')
    ap.add_argument('--who', default=os.environ.get('C5_JOB_OWNER') or 'render-run-all',
                    help='owner of the registered jobs (default C5_JOB_OWNER, else render-run-all)')
    ap.add_argument('--only', action='append', default=[], choices=names, metavar='SUITE',
                    help='run only these suites (repeatable): ' + ', '.join(names))
    ap.add_argument('--no-build', action='store_true', help='build nothing; use the builds already in --out')
    ap.add_argument('--main', type=Path, help='the --out of a plain run on main: adds ' + MAIN_ONLY)
    ap.add_argument('--gate-wait', type=float, default=120.0, metavar='MIN',
                    help='how long a refused start may wait for the lock or a gate in all (default 120)')
    ap.add_argument('--list', action='store_true', help='print the suites and what each runs, then exit')
    ap.add_argument('--sb-timeout', type=int, metavar='S',
                    help='scenebundle-programs: seconds per test program, forwarded as build_scenebundle_tests.py '
                         '--timeout (default its 600; its TIMEOUTS floor, sceneshot_test 7200 s, stays; ticket 0800)')
    ap.add_argument('--sb-timeout-scale', type=float, default=1.0, metavar='F',
                    help='scenebundle-programs: multiply every program\'s timeout, sceneshot_test\'s included, forwarded '
                         'as --timeout-scale (default 1; e.g. 2 on a loaded workstation; ticket 0800)')
    a = ap.parse_args(argv)
    if a.list:
        return a
    if a.sb_timeout is not None and a.sb_timeout < 1:
        ap.error('--sb-timeout must be at least 1 s')
    if not a.sb_timeout_scale > 0:
        ap.error('--sb-timeout-scale must be above 0')
    if not a.out:
        ap.error('--out DIR is needed')
    if a.main and a.asan:
        ap.error('--main (unchanged-vs-main) is plain only: its builds compare byte for byte with plain builds of main')
    if MAIN_ONLY in a.only and not a.main:
        ap.error(MAIN_ONLY + ' needs --main DIR')
    a.exe = a.exe or os.environ.get('C5R_EXE')
    a.game = a.game or os.environ.get('C5R_GAME_DIR')
    if not a.exe or not Path(a.exe).is_file():
        ap.error('CivilizationV_DX11.exe is needed (--exe or C5R_EXE; read-only): got %r' % a.exe)
    if not a.game or not Path(a.game).is_dir():
        ap.error('an installed game is needed (--game or C5R_GAME_DIR; read-only): got %r' % a.game)
    # ticket 0589 (D200): only build 4390913 (tools/harness/exe_build.py's rule; C5_ALLOW_UNKNOWN_EXE_BUILD=1 overrides):
    # the EXE, the install's EXE and an inherited C5_GAME_DIR's EXE (the suites read it before C5R_GAME_DIR)
    sys.path.insert(0, str(HERE))
    import gameinstall
    checks = [('--exe/C5R_EXE', Path(a.exe)), ('--game/C5R_GAME_DIR', Path(a.game) / gameinstall.EXE_NAME)]
    if os.environ.get('C5_GAME_DIR'):
        checks.append(('C5_GAME_DIR', Path(os.environ['C5_GAME_DIR']) / gameinstall.EXE_NAME))
    for what, exe in checks:
        why = gameinstall.exe_refusal(exe) if exe.is_file() else '%s does not exist' % exe
        if why:
            ap.error('%s: %s' % (what, why))
    return a


def main(argv=None):
    a = parse_args(argv)
    if a.list:
        for name, builds, kind, files, _, views in SUITES:
            runs = 'build_scenebundle_tests.py --run-only' if kind == 'sbrun' else ' '.join(f[len(T):] for f in files)
            print(f'{name}: builds {", ".join(builds)}; runs {runs}' + (' (needs --main)' if name == MAIN_ONLY else ''))
        return 0
    c = Ctx(a.out.resolve(), a.asan, a.main.resolve() if a.main else None)
    variant = 'asan' if a.asan else 'plain'
    suites = [s for s in SUITES if (s[0] in a.only if a.only else (s[0] != MAIN_ONLY or a.main))]
    logs = c.out / ('logs' + c.sfx)
    logs.mkdir(parents=True, exist_ok=True)
    env0 = dict(os.environ, VSCMD_SKIP_SENDTELEMETRY='1', PYTEST_DISABLE_PLUGIN_AUTOLOAD='1', C5_JOB_OWNER=a.who,
                C5_QUIETLOCK_WHO=a.who, C5R_EXE=str(Path(a.exe)), C5R_GAME_DIR=str(Path(a.game)))
    env0.setdefault('C5_GAME_DIR', env0['C5R_GAME_DIR'])
    # conftest.py reads C5_RENDER_TEST_BUILD in every session: never let it fall back to its default folder. Suites that
    # run render_host_test.exe set the variant's folder; the others get the plain one, so that the conftest's ASan set-up
    # (which needs the runtime beside the binaries) is not asked for when rt was not built (run_all sets ASAN_OPTIONS).
    env0['C5_RENDER_TEST_BUILD'] = str(c.d('rt') / 'bin')
    # ticket 0625: an exported C5_SCENE_ASSET_ROOT goes only to ASSET_ROOT_SUITES (see suite_env)
    asset_root = env0.pop(ASSET_ROOT_VAR, None)
    gate_wait_s = a.gate_wait * 60
    lines = []
    say(f'run_all: {variant} run of {len(suites)} suite(s) into {c.out}, owner {a.who}, {time.strftime("%Y-%m-%d %H:%M:%S")}',
        lines)
    if asset_root:
        say(f'run_all: {ASSET_ROOT_VAR}={asset_root}: passed only to {", ".join(ASSET_ROOT_SUITES)}', lines)
    if not a.main and not a.only:
        say(f'run_all: note: {MAIN_ONLY} is not selected (it needs --main DIR, a plain run of main)', lines)
    lib_copy = library_copy(env0) if any(s[0] in LIBRARY_SUITES for s in suites) else None
    if lib_copy is not None:
        ok, last = copy_check(lib_copy, env0)
        say(f'run_all: scene library copy {lib_copy}: {last}', lines)
        if not ok:
            say('run_all: FAILED: the scene library copy failed its check (tools/scenelib/scenelib.py sync, or point '
                'C5_SCENE_LIBRARY at the master); nothing built or run', lines)
            (c.out / f'summary{c.sfx}.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')
            return 1

    # 1. the builds, each once, in BUILDS order, with what they need first
    want = []

    def need(b):
        for d in BUILDS[b][0]:
            need(d)
        if b not in want:
            want.append(b)
    for s in suites:
        for b in s[1]:
            need(b)
    built = {}
    if a.no_build:
        say('run_all: --no-build: using the builds already in ' + str(c.out), lines)
        built = {b: (True, 'reused') for b in want}
    for b in ([] if a.no_build else [b for b in BUILDS if b in want]):
        if stopped(c):
            say('run_all: STOPPED by ' + str(c.out / 'STOP'), lines)
            return 9
        missing = [d for d in BUILDS[b][0] if not built.get(d, (False,))[0]]
        if missing:
            built[b] = (False, 'needs ' + ', '.join(missing))
            say(f'run_all: BUILD {b} NOT STARTED (needs {", ".join(missing)})', lines)
            continue
        t0 = time.time()
        log = logs / f'build_{b}.log'
        if log.exists():
            log.unlink()
        rc = gated(c, a.who, f'0482 run_all build {b} ({variant})', BUILDS[b][1](c), env0, log, gate_wait_s)
        built[b] = (rc == 0, f'exit {rc}')
        say(f'run_all: BUILD {b} {"OK" if rc == 0 else "FAILED exit %d" % rc} {time.time() - t0:.0f} s log {log}', lines)

    # 2. the suites
    results = []
    for name, builds, kind, files, envf, views in suites:
        if stopped(c):
            say('run_all: STOPPED by ' + str(c.out / 'STOP'), lines)
            return 9
        t0 = time.time()
        bad = [b for b in builds if not built.get(b, (False,))[0]]
        if bad:
            results.append((name, 'FAIL'))
            say(f'run_all: SUITE {name} FAIL 0 passed, 0 failed, 0 skipped 0 s (build {", ".join(bad)} failed)', lines)
            continue
        why = None
        if c.asan:
            for v in views:
                why = why or make_view(c, v)
        checked = lib_copy is not None and name in LIBRARY_SUITES
        if checked and not why:
            ok, last = copy_check(lib_copy, env0)
            why = None if ok else 'scene library copy: ' + last
        if why:
            results.append((name, 'FAIL'))
            say(f'run_all: SUITE {name} FAIL 0 passed, 0 failed, 0 skipped 0 s ({why})', lines)
            continue
        env = suite_env(env0, name, envf(c), asset_root)
        adir = None
        if c.asan:
            adir = c.out / 'asan-logs' / name
            if adir.exists():
                shutil.rmtree(adir)
            adir.mkdir(parents=True)
            env['C5_ASAN_LOG_DIR'] = str(adir)
            env['ASAN_OPTIONS'] = 'log_path="%s"' % (adir / 'asan').as_posix()
        log = logs / f'suite_{name}.log'
        if log.exists():
            log.unlink()
        if kind == 'sbrun':
            argv = sb_argv(c, a)
        else:
            argv = PY + ['-m', 'pytest', '-q', '-p', 'no:cacheprovider', '-rfEs'] + files
        rc = gated(c, a.who, f'0482 run_all suite {name} ({variant})', argv, env, log, gate_wait_s)
        text = log.read_text(encoding='utf-8', errors='replace')
        n = sb_counts(text) if kind == 'sbrun' else pytest_counts(text)
        reasons = []
        if n is None:
            n = {'passed': 0, 'failed': 0, 'skipped': 0, 'errors': 0}
            reasons.append('no pytest summary line')
        more, timeouts = verdict(n, rc, kind)
        reasons += more
        if adir is not None:
            reports = sorted(p.name for p in adir.glob('asan*'))
            if reports:
                reasons.append(f'{len(reports)} AddressSanitizer report(s) in {adir}')
        if checked:
            ok, last = copy_check(lib_copy, env0)
            if not ok:
                reasons.append('scene library copy after the suite: ' + last)
        bad = bool(reasons or n['failed'] or n['errors'])
        status = 'FAIL' if bad else ('TIMEOUT' if timeouts else 'PASS')
        if timeouts:
            reasons.append(f'timed out, no verdict (NOT A RESULT): {", ".join(timeouts)}; rerun with a larger '
                           f'--sb-timeout-scale (now {a.sb_timeout_scale:g})')
        results.append((name, status))
        counts = f'{n["passed"]} passed, {n["failed"]} failed, {n["skipped"]} skipped' + \
            (f', {n["errors"]} errors' if n['errors'] else '') + (f', {len(timeouts)} timed out' if timeouts else '')
        say(f'run_all: SUITE {name} {status} {counts} {time.time() - t0:.0f} s'
            + (f' ({"; ".join(reasons)})' if reasons else '') + f' log {log}', lines)

    return finish(c, results, variant, lines)


def finish(c, results, variant, lines):
    """The last line and the exit code from [(suite, PASS|FAIL|TIMEOUT)]: 1 on any FAIL, else 4 on any TIMEOUT (ticket
    0800: no verdict, never reported as failed), else 0."""
    failed = [n for n, s in results if s == 'FAIL']
    timed = [n for n, s in results if s == 'TIMEOUT']
    tail = f'; timed out (no verdict): {", ".join(timed)}' if timed else ''
    if failed:
        say(f'run_all: FAILED: {len(failed)} of {len(results)} suite(s) ({variant}): {", ".join(failed)}{tail}', lines)
        rc = 1
    elif timed:
        say(f'run_all: NO VERDICT: {len(timed)} of {len(results)} suite(s) timed out ({variant}): {", ".join(timed)}; '
            f'nothing failed (a timeout is NOT A RESULT: rerun with a larger --sb-timeout-scale)', lines)
        rc = SB_NO_VERDICT
    else:
        say(f'run_all: OK: {len(results)} suite(s) passed ({variant})', lines)
        rc = 0
    (c.out / f'summary{c.sfx}.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return rc


if __name__ == '__main__':
    sys.exit(main())
