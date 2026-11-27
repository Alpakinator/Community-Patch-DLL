"""One command for the x64 DLL (issues 1405, 1703): stubs -> CvGameDatabase library -> CvLocalization library -> DLL
-> smoke host -> smoke run.

    python tools/x64-dll/build_all.py [--src <fork source>] --out <dir> [--config debug|release]
                                      [--lua-tarball PATH] [--sqlite-zip PATH] [--offline] [--unlocked]
                                      [--no-gate] [--jobs N] [--major-civs N [--unvalidated-major-civs]]

`--src`: the DLL fork source at the commit pinned in deps/dll.lock, either a git checkout of that commit
without tracked changes, or a plain copy of it without .git (`git archive` export). Any other commit needs
`--unlocked`. Without `--src`: the location of the one setting in tools/x64-dll/dll_source.py (VP_DLL_SRC,
deps/dll-source.ini, else the upstream layout `..`; ticket 4701), checked the same way. Every output goes under `--out`: stubs/ (closed-library stubs, lua51_x64.dll/.lib,
CvGameDatabase_x64.dll/.lib and CvLocalization_x64.dll/.lib), dll/ (CvGameCore_Expansion2.dll and build logs), host/ (smoke_host.exe), run/ (smoke
run), logs/<step>.log (each step's full output), build_all.json (steps, exit codes, seconds).

The DLL step compiles with -Wshorten-64-to-32 and runs the narrowing gate against
tools/x64-dll/narrowing-baseline.tsv (`--no-gate` skips both). Exit 0 only when every step succeeded; otherwise the
exit code of the failing step (at least 1) and a last line `build_all: FAILED at step <name> (exit N): <reason>`.
One-off 9023 (P10): the dll step counts as failed, even with exit 0, when its log lacks build.py's success line
(`OK: <dll>`), when `dll/build-summary.json` is older than the step or names no DLL, or when the DLL is older than the
step; `build_all.json` reports a DLL only from this run's summary; the OK output names the DLL's SHA-256, size and
timestamp (`build_all: dll: <path> sha256 ... size ... mtime ...`).

Issue 4252 (epic 0334): `--major-civs N` (default 22) builds the DLL with `/DMOD_GLOBAL_MAX_MAJOR_CIVS=N` (only the dll
step; the libraries, the stubs and the smoke host take no define, as the server and the proxy). The value is checked
before any step (22 and 43 validated; `--unvalidated-major-civs` for another value of 2..62; a live line in the
source's CustomModsGlobal.h must equal N) and recorded in build_all.json (`major_civs`, `major_civs_define`). The smoke
step counts as failed, even with exit 0, unless the DLL's own start line in <out>/run/work/Logs/CustomMods.log says
`Supporting N major civilizations` and its version line carries "+mN" (none for 22); both lines go into build_all.json
(`smoke_major_civs`) and the OK output.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import build as x64build  # noqa: E402  tools/x64-dll/build.py (resolve_src)
import linkcheck  # noqa: E402  tools/x64-dll/linkcheck.py (one-off 9023, P10)

def default_db() -> Path:
    """The smoke run's default gameplay database (issue 3502): $C5_RELEASE_TEST_DB when set (the release driver's build
    stage sets it to the test-data stage's database, the one the DLL is built for: MC condition #414), else the release
    descriptor's test_db."""
    env = os.environ.get('C5_RELEASE_TEST_DB', '').strip()
    if env:
        return Path(env)
    sys.path.insert(0, str(HERE.parents[1] / 'tools' / 'release'))
    import descriptor  # noqa: E402  tools/release/descriptor.py (issue 3501)
    return descriptor.load().test_db


BUILD_DB = HERE / 'cvgamedatabase' / 'build_db.py'
BUILD_LOC = HERE / 'cvlocalization' / 'build_loc.py'
# Closed-library stub groups replaced by real libraries (issues 1402, 1701/1703). Their folders are deleted; passing
# them to --exclude-groups also removes a stale <group>.lib an older build_stubs.py left in a reused --out.
REPLACED_STUB_GROUPS = ('CvGameDatabase', 'CvLocalization')
# Outputs each library step must leave in stubs/ (build.py --stubs links every .lib there).
STEP_OUTPUTS = {'cvgamedatabase': ('CvGameDatabase_x64.dll', 'CvGameDatabase_x64.lib'),
                'cvlocalization': ('CvLocalization_x64.dll', 'CvLocalization_x64.lib')}


SUPPORTING_RE = re.compile(r'Supporting (\d+) major civilizations')
VERSION_RE = re.compile(r'Gamecore was built from git version (.+?)\s*$')


def smoke_major_civs(run: Path) -> dict:
    """What the smoke run's DLL said at start (CvDllContext.cpp:248-250, in <run>/work/Logs/CustomMods.log):
    {'major_civs': int or None, 'version': str or None, 'log': path}."""
    log = Path(run) / 'work' / 'Logs' / 'CustomMods.log'
    res = {'major_civs': None, 'version': None, 'log': str(log)}
    try:
        text = log.read_text(encoding='utf-8', errors='replace')
    except OSError:
        return res
    for line in text.splitlines():
        m = SUPPORTING_RE.search(line)
        if m and res['major_civs'] is None:
            res['major_civs'] = int(m.group(1))
        m = VERSION_RE.search(line)
        if m and res['version'] is None:
            res['version'] = m.group(1)
    return res


def smoke_major_problem(seen: dict, want: int):
    """None when the smoke run's DLL said `Supporting <want> major civilizations` and its version string carries the
    tag of a non-default count ("+m43"; no "+m" for 22), else the reason (issue 4252)."""
    if seen['major_civs'] is None:
        return f'step smoke exited 0 but {seen["log"]} has no "Supporting N major civilizations" line'
    if seen['major_civs'] != want:
        return (f'step smoke: the DLL says "Supporting {seen["major_civs"]} major civilizations", --major-civs is '
                f'{want}')
    tag = x64build.major_civs_tag(want)
    version = seen['version'] or ''
    if tag and f'+{tag} ' not in version + ' ':
        return f'step smoke: the DLL\'s version string {version!r} does not carry "+{tag}"'
    if not tag and re.search(r'\+m\d+\b', version):
        return f'step smoke: the DLL\'s version string {version!r} carries a major-count tag, --major-civs is {want}'
    return None


def run_step(name, cmd, logdir, env):
    """Run one step, streaming its output to the console and logs/<name>.log. Returns (rc, reason, seconds)."""
    log = logdir / f'{name}.log'
    print(f'== build_all: step {name}: {" ".join(str(c) for c in cmd)}', flush=True)
    t0 = time.perf_counter()
    last, failed_line = '', ''
    with open(log, 'w', encoding='utf-8', errors='replace', newline='\n') as f:
        f.write(' '.join(str(c) for c in cmd) + '\n')
        p = subprocess.Popen([str(c) for c in cmd], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                             errors='replace', env=env, bufsize=1)
        for line in p.stdout:
            sys.stdout.write(line)
            f.write(line)
            s = line.strip()
            if s:
                last = s
                if 'FAILED' in s:
                    failed_line = s
        rc = p.wait()
    dt = time.perf_counter() - t0
    reason = failed_line or last or '(no output)'
    print(f'== build_all: step {name}: exit {rc} in {dt:.1f} s', flush=True)
    return rc, reason, dt


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--src', type=Path, default=None,
                    help='DLL fork source at the deps/dll.lock commit: git checkout or plain copy without .git; default '
                         f'{x64build.DEFAULT_SRC} (from {x64build.DEFAULT_SRC_ORIGIN}; dll_source.py)')
    ap.add_argument('--out', type=Path, required=True, help='directory for every output (outside the repository)')
    ap.add_argument('--config', choices=['debug', 'release', x64build.RELEASE_PGO], default='debug',
                    help='debug, release, or release-pgo (ticket 0513: the dll step compiles with the locked PGO profile '
                         'of deps/dll.lock; the stubs, the libraries and the smoke host are built as release)')
    ap.add_argument('--profile', type=Path, default=None, metavar='FILE',
                    help='release-pgo: the merged profile (default deps/dll.lock profile-file); passed to build.py')
    ap.add_argument('--profile-unlocked', action='store_true',
                    help='release-pgo: a profile that is not deps/dll.lock\'s (a measurement or retrain trial); passed '
                         'to build.py and recorded')
    ap.add_argument('--lua-tarball', type=Path, help='lua-5.1.4.tar.gz (default: the cache root $CV64_DEPS_CACHE)')
    ap.add_argument('--sqlite-zip', type=Path, help='sqlite-amalgamation-3071700.zip (default: the cache root)')
    ap.add_argument('--offline', action='store_true', help='never download third-party sources (CV64_OFFLINE=1)')
    ap.add_argument('--unlocked', action='store_true', help='allow a git --src at another commit than deps/dll.lock')
    ap.add_argument('--no-gate', action='store_true', help='no -Wshorten-64-to-32 and no narrowing gate')
    ap.add_argument('--jobs', type=int, help='parallel compiles of the DLL step (default: all cores)')
    ap.add_argument('--timeout', type=int, default=300, help='smoke run timeout, seconds')
    ap.add_argument('--db', type=Path,
                    help='gameplay database for the smoke run (issue 1404; run_smoke.py copies it); default: the '
                         'release descriptor\'s test_db (tools/release/releases/<tag>.json, the current release or '
                         '$C5_RELEASE; issue 3502)')
    ap.add_argument('--quietlock-override', metavar='"WHO: WHY"',
                    help='start although another team holds the quiet-machine lock (emergencies only, '
                         'logged; COMMON rule 16)')
    ap.add_argument('--major-civs', type=int, default=x64build.MAJOR_CIVS_DEFAULT, metavar='N',
                    help='major civilizations of the DLL (issue 4252; default 22 = the define absent; 43 adds '
                         '/DMOD_GLOBAL_MAX_MAJOR_CIVS=43 to the dll step only); the smoke step checks the DLL says N')
    ap.add_argument('--unvalidated-major-civs', action='store_true',
                    help='allow a --major-civs value other than 22 and 43 (2..62)')
    ap.add_argument('--no-render-events', action='store_true',
                    help='ticket 4932: the dll step without VP64_RENDER_EVENTS for a source that has its header '
                         '(build.py --no-render-events; the identity test only)')
    a = ap.parse_args()
    if (a.profile is not None or a.profile_unlocked) and a.config != x64build.RELEASE_PGO:
        print(f'build_all: FAILED at step src (exit 1): --profile and --profile-unlocked are for --config '
              f'{x64build.RELEASE_PGO} only, not {a.config}')
        sys.exit(1)
    pgo = None
    if a.config == x64build.RELEASE_PGO:   # ticket 0513: the profile is checked before any step (build.py's own check)
        from types import SimpleNamespace
        pgo = x64build.check_pgo_args(SimpleNamespace(config=a.config, profile=a.profile, compile_only=False,
                                                      link_only=False, profile_unlocked=a.profile_unlocked))
        print(f'build_all: {x64build.RELEASE_PGO}: profile {pgo["path"]} sha256 {pgo["sha256"]} '
              f'({"locked" if pgo["locked"] else "NOT the locked profile, --profile-unlocked"})', flush=True)
    # COMMON rule 16 launch gate (perf/quietlock/gate.py, tracker 7006): refuse while another team holds the
    # quiet-machine lock; the holder's own runs pass (C5_QUIETLOCK_WHO); a gate error never blocks.
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'perf' / 'quietlock'))
    try:
        import gate as _launch_gate
    except ImportError:
        _launch_gate = None
        print('launch gate: warning: perf/quietlock/gate.py not found; launch goes on', file=sys.stderr)
    if _launch_gate:
        _launch_gate.require('an x64 DLL build (tools/x64-dll/build_all.py)', a.quietlock_override)

    try:
        info = x64build.resolve_src(a.src, a.unlocked)
    except x64build.SrcError as e:
        print(f'build_all: FAILED at step src (exit 1): {e}')
        sys.exit(1)
    src = info['src']
    try:
        major = x64build.resolve_major_civs(src, a.major_civs, a.unvalidated_major_civs)
    except x64build.MajorCivsError as e:
        print(f'build_all: FAILED at step src (exit 1): {e}')
        sys.exit(1)
    out = a.out.resolve()
    if out == src or src in out.parents:
        print('build_all: FAILED at step src (exit 1): --out must not be inside --src')
        sys.exit(1)
    if x64build.REPO == out or x64build.REPO in out.parents:
        print(f'build_all: FAILED at step src (exit 1): --out must be outside the repository {x64build.REPO}')
        sys.exit(1)
    # the configuration of every step but the dll step: release-pgo's libraries, stubs and smoke host are release's
    lib_config = 'release' if a.config == x64build.RELEASE_PGO else a.config
    stubs, dll, host, run, logs = out / 'stubs', out / 'dll', out / 'host', out / 'run', out / 'logs'
    logs.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    if a.offline:
        env['CV64_OFFLINE'] = '1'
    py = [sys.executable, '-u']
    unlocked = ['--unlocked'] if a.unlocked else []
    print(f'build_all: config {a.config}; src {src} ({info["note"]}); out {out}', flush=True)
    print(f'build_all: {major["note"]}', flush=True)

    # Issue 3502 (epic 3500): the default database is the release descriptor's test_db. (Replaced: the copy
    # <cache root>/testdb/vp548-cdrift-Civ5DebugDatabase.db, the same bytes as the 5.4.8 descriptor's test_db; the smoke
    # run copies the file it is given, so the pinned file is only read.)
    db = a.db or default_db()
    if not Path(db).is_file():
        sys.exit(f'build_all: FAILED: gameplay database {db} not found; pass --db (a Civ5DebugDatabase.db of the '
                 f'release; never the file in My Games itself)')
    if not BUILD_DB.is_file():
        sys.exit(f'build_all: {BUILD_DB} missing (the CvGameDatabase library, issue 1402)')
    if not BUILD_LOC.is_file():
        sys.exit(f'build_all: {BUILD_LOC} missing (the CvLocalization library, issue 1701)')
    stub_cmd = py + [HERE / 'closedlib-stubs' / 'build_stubs.py', '--out', stubs, '--config', lib_config, '--src', src]
    stub_cmd += unlocked + (['--lua-tarball', a.lua_tarball] if a.lua_tarball else [])
    stub_cmd += ['--offline'] if a.offline else []
    stub_cmd += ['--exclude-groups', ','.join(REPLACED_STUB_GROUPS)]  # real libraries replace these groups
    dll_cmd = py + [HERE / 'build.py', '--config', a.config, '--out', dll, '--stubs', stubs, '--src', src] + unlocked
    dll_cmd += [] if a.no_gate else ['--shorten']
    dll_cmd += ['--jobs', str(a.jobs)] if a.jobs else []
    if major['major_civs'] != x64build.MAJOR_CIVS_DEFAULT:   # issue 4252: the default command line stays as it was
        dll_cmd += ['--major-civs', str(major['major_civs'])]
        dll_cmd += ['--unvalidated-major-civs'] if a.unvalidated_major_civs else []
    dll_cmd += ['--no-render-events'] if a.no_render_events else []   # ticket 4932: default command line unchanged
    if pgo:   # ticket 0513: the profile checked above, by its path (build.py checks it again against the lock)
        dll_cmd += ['--profile', str(pgo['path'])] + (['--profile-unlocked'] if a.profile_unlocked else [])
    steps = [
        ('stubs', stub_cmd),
        ('cvgamedatabase', py + [BUILD_DB, '--out', stubs, '--arch', 'x64', '--config', lib_config, '--src', src]
         + unlocked + (['--sqlite-zip', a.sqlite_zip] if a.sqlite_zip else [])),
        ('cvlocalization', py + [BUILD_LOC, '--out', stubs, '--arch', 'x64', '--config', lib_config, '--src', src,
                                 '--db-dir', stubs] + unlocked),
        ('dll', dll_cmd),
        ('host', py + [HERE / 'smoke-host' / 'build_host.py', '--out', host, '--stubs', stubs, '--src', src,
                       '--config', lib_config] + unlocked),
        ('smoke', py + [HERE / 'smoke-host' / 'run_smoke.py', '--host', host, '--dll-dir', dll, '--dep-dir', stubs,
                        '--run', run, '--timeout', str(a.timeout), '--db', db]),
    ]
    record = {'started': time.strftime('%Y-%m-%d %H:%M:%S %z'), 'config': a.config, 'src': str(src),
              'src_mode': info['mode'], 'src_commit': info['head'], 'out': str(out),
              'major_civs': major['major_civs'], 'major_civs_define': major['define'],
              'major_civs_header': major['header'],
              'render_events': x64build.render_events_state(src, not a.no_render_events),   # ticket 4932
              'steps': []}
    if pgo:   # ticket 0513
        record['profile'] = {'path': str(pgo['path']), 'sha256': pgo['sha256'], 'locked': pgo['locked']}
        record['lib_config'] = lib_config
    t_all = time.perf_counter()
    rc_final, fail = 0, None
    dll_started = None
    for name, cmd in steps:
        if cmd is None:
            msg = 'skipped'
            print(f'== build_all: step {name}: {msg}', flush=True)
            record['steps'].append({'step': name, 'skipped': msg})
            continue
        step_started = time.time()
        if name == 'dll':
            dll_started = step_started
        rc, reason, dt = run_step(name, cmd, logs, env)
        if rc == 0 and name == 'dll':
            why = dll_step_problem(dll, logs / 'dll.log', dll_started)
            if why:
                rc, reason = 1, why
        record['steps'].append({'step': name, 'rc': rc, 'seconds': round(dt, 1), 'log': str(logs / f'{name}.log')})
        if rc == 0 and name == 'smoke':
            seen = smoke_major_civs(run)
            record['smoke_major_civs'] = seen
            why = smoke_major_problem(seen, major['major_civs'])
            if why:
                rc, reason = 1, why
            else:
                print(f'build_all: smoke: the DLL says "Supporting {seen["major_civs"]} major civilizations", '
                      f'version "{seen["version"]}"', flush=True)
        if rc == 0 and name in STEP_OUTPUTS:
            why = step_outputs_problem(name, stubs, step_started)
            if why:
                rc, reason = 1, why
        if rc != 0:
            rc_final, fail = rc if rc > 0 else 1, f'build_all: FAILED at step {name} (exit {rc}): {reason}'
            record['steps'][-1].update(rc=rc, reason=reason)
            break
    record['seconds'] = round(time.perf_counter() - t_all, 1)
    record['rc'] = rc_final
    s = fresh_dll_summary(dll, dll_started)
    if s is not None:
        record['dll'] = {k: s.get(k) for k in ('dll', 'dll_bytes', 'dll_sha256', 'dll_mtime', 'unresolved', 'link_rc',
                                               'narrowing_sites', 'narrowing_gate', 'timings_s')}
        if a.config == x64build.RELEASE_PGO:   # ticket 0513: the profile, the DLL's own .vppgo and the retrain verdict
            record['dll'].update({k: s.get(k) for k in ('config', 'profile', 'dll_profile', 'pgo_diagnostics',
                                                        'pgo_retrain')})
    (out / 'build_all.json').write_text(json.dumps(record, indent=1) + '\n', encoding='utf-8')
    if fail:
        print(fail)
        sys.exit(rc_final)
    if s is not None and s.get('dll'):
        print('build_all: ' + linkcheck.artifact_line(s['dll'], 'dll'))
    print(f'build_all: OK in {record["seconds"]:.0f} s: ' + ', '.join(
        f'{s["step"]} {s["seconds"]:.0f} s' if 'seconds' in s else f'{s["step"]} skipped' for s in record['steps']))


def step_outputs_problem(name, stubs: Path, started):
    """A library step exited 0: each of its STEP_OUTPUTS must exist and be written by this run, not left from an
    earlier one (one-off 9023, P10; the missing-file check is issue 1703's). None when all hold."""
    missing = [f for f in STEP_OUTPUTS[name] if not (Path(stubs) / f).is_file()]
    if missing:
        return f'step {name} exited 0 but {", ".join(missing)} missing in {stubs}'
    # Only the .dll: lld-link rewrites an import library (.lib) only when its content changed, so an unchanged
    # .lib keeps its old timestamp by design [I: lld's createImportLibrary, "replace it only if the contents have
    # changed"].
    old = [f for f in STEP_OUTPUTS[name] if f.endswith('.dll') and
           (Path(stubs) / f).stat().st_mtime < started - linkcheck.MTIME_SLACK_S]
    if old:
        return f'step {name} exited 0 but {", ".join(old)} in {stubs} is older than the step (an earlier build\'s)'
    return None


def fresh_dll_summary(dll_dir: Path, started):
    """build.py's build-summary.json of THIS run, or None (one-off 9023, P10): a summary older than the dll step's
    start is an earlier build's (build.py dies before writing one on a compile failure), so its DLL is not reported."""
    summary = Path(dll_dir) / 'build-summary.json'
    if started is None or not summary.is_file() or summary.stat().st_mtime < started - linkcheck.MTIME_SLACK_S:
        return None
    return json.loads(summary.read_text(encoding='utf-8'))


def dll_step_problem(dll_dir: Path, step_log: Path, started):
    """The dll step exited 0: require build.py's success line in its log (`OK: <dll>`), a fresh summary naming the
    DLL, and the DLL itself written by this run. None when all hold, else the reason (one-off 9023, P10)."""
    try:
        text = Path(step_log).read_text(encoding='utf-8', errors='replace')
    except OSError:
        text = ''
    if not any(line.startswith('OK: ') for line in text.splitlines()):
        return f'step dll exited 0 but its log {step_log} has no success line ("OK: <dll>")'
    s = fresh_dll_summary(dll_dir, started)
    if s is None or not s.get('dll'):
        return f'step dll exited 0 but {Path(dll_dir) / "build-summary.json"} is missing, older than the step or names no DLL'
    return linkcheck.link_failure(0, '', s['dll'], started, check_log=False)


if __name__ == '__main__':
    main()
