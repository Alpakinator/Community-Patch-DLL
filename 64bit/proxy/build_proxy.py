"""Build the x86 proxy DLL (epic 2300, task 2302) in one command.

    python proxy/build_proxy.py --out <dir> --src <fork source> [--gen <generated dir>] [--config release|debug]
                                [--unlocked] [--jobs N] [--repo-commit <commit>]

The proxy is `CvGameCore_Expansion2.dll` for the 32-bit EXE: x86, clang-cl -m32 in the VS 2008 environment (VC9
headers, msvcr90, C++11 language at most), the DLL's include dirs and defines (proxy/gen/toolchain.py), exporting only
DllGetGameContext and importing lua51_Win32.dll like the real DLL (the engine's Lua module is used).

Steps (each logged to <out>/build-proxy.log; summary in <out>/build-proxy.json):
0. pure     proxy/tools/gen_pure_list.py --check (issue 2801; since issue 3503): the build fails when proxy/src holds a
            compiled purity list, when the audited lists (proxy/model/pure-methods.tsv, lua-pure-bindings.tsv) are
            malformed or no longer match proxy/model/purity-baseline.json, or name a method or Lua function the model
            lacks. The lists themselves reach the proxy as purity.tsv at run time (proxy-design section 16.9).
1. src      --src is checked against deps/dll.lock through tools/x64-dll/build.py resolve_src (as server/build_server.py
            does): a git checkout at the locked commit or a plain copy whose source fingerprint matches.
2. gen      python proxy/gen/gen_runtime.py --out <out>/gen --hooks-proxy proxy/src --hooks-server server/src/remote,
            unless --gen names a directory the generator already wrote (the server build passes its own so both sides
            share one schema hash).
2b. exebuild  tools/exebuild/emit_header.py --out <out>/gen-exebuild (ticket 4960, epic 0590, D202): the EXE address
            header vp_exe_tables.gen.h from the local per-build tables (setting exe_tables); the build fails, naming the
            setting, without it. <out>/gen-exebuild and exebuild/include are on the include path.
3. vpipc    ipc/src/*.cpp compiled for x86 (/MD, VC9) into <out>/obj/vpipc_x86.lib.
4. compile  <gen>/common/vp_ids.gen.cpp, <gen>/proxy/*.cpp and proxy/src/*.cpp with the DLL's flags; first
            <out>/obj/buildinfo/vp_build_info.h (proxy/gen/build_info.py: repo commit, dirty flag, DLL pin; issue
            2704), which vpproxy.log's first line reports with the proxy's own SHA-256 (proxy/include/vp_buildid.h).
            Issue 2801: built from a copy of this repository without .git (an archive), git cannot name the commit (and
            a parent work tree would give its own): the repo field is then "<commit> (archive)" from --repo-commit or
            the first word of <repo>/.repo-commit, else "unknown (archive: ...)" with a LOUD line in the build log.
5. link     lld-link /DLL with a .def exporting DllGetGameContext only: <out>/bin/CvGameCore_Expansion2.dll + .pdb;
            the msvcr90 side-by-side manifest is written by the linker to a file and embedded as resource id 2 with
            the SDK v6.0A mt.exe (llvm-mt cannot: research/03-dll-engine-boundary/ipc-design-and-measurements.md 7.3).
5b. helper  proxy/helper/vp64helper.cpp + proxy_cfgini.cpp (issue 3520: config.ini on/off/status, the failure popup;
            D119, D125) -> <out>/bin/vp64helper.exe: x86,
            static CRT, GUI subsystem, imports only kernel32, user32, gdi32 and shell32 (checked; no network, D120).
            Shipped beside the DLL. Tests: proxy/tools/test_vp64helper.py.
6. check    imports (MSVCR90, MSVCP90, KERNEL32, lua51_Win32, CvGameDatabaseWin32Final Release (issue 2504) and
            system DLLs only), exports (DllGetGameContext only), the
            manifest resource (type 24, id 2).
            Ticket 0609: every x86 TU (steps 3, 4, 5b) is compiled with ONE STL setting (STL_SETTING: _SECURE_SCL=0,
            _HAS_ITERATOR_DEBUGGING=0) and a forced #pragma detect_mismatch; before each link every object is checked
            to carry it (check_stl_objects) and lld-link fails on a mixed pair (proxy-build-settings.md).
7. purity-test  proxy/tools/test_purity.py in full (issue 0480: step 0 alone missed the full test's derive checks
            failing on main for ten hours, 0478), with --src and the built DLL (only hashed), scratch <out>/purity-test
            (removed after a pass). It runs only when its inputs (the model, the purity tools, the loader, proxy/gen)
            or the source's commit changed since the last pass on this machine, recorded as a stamp in
            C:/c5-64bit-work/cache/purity-test/ ($C5_PURITY_TEST_CACHE); about 110 s then, 0 s otherwise. Not run for
            an --unlocked source off the pin (rows drop there; the release driver's purity stage covers it), nor
            where VS 2022 is missing (LOUD); --no-purity-test leaves it out (LOUD). A failed check fails the build.
Offline. Outputs only under --out (outside the repository), and step 7's pass stamp in its cache folder. Exit 0 on success, 1 on failure with a last line
`build_proxy: FAILED: <reason>`.
"""
import argparse
import concurrent.futures
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
sys.path.insert(0, str(HERE / 'gen'))
import toolchain as tc  # noqa: E402
import build_info  # noqa: E402  proxy/gen/build_info.py (issue 2704)

X64 = REPO / 'tools' / 'x64-dll'
_spec = importlib.util.spec_from_file_location('x64build', X64 / 'build.py')
x64build = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(x64build)
sys.path.insert(0, str(X64))
import linkcheck  # noqa: E402  tools/x64-dll/linkcheck.py (one-off 9023, P10)

LLVM_BIN = Path(tc.CLANG).parent
LLVM_LIB = LLVM_BIN / 'llvm-lib.exe'
READOBJ = LLVM_BIN / 'llvm-readobj.exe'
SDK_MT = Path(r'C:\Program Files\Microsoft SDKs\Windows\v6.0A\bin\mt.exe')
VPIPC_DEFS = ['WIN32_LEAN_AND_MEAN', 'NOMINMAX', '_WIN32_WINNT=0x0601', '_CRT_SECURE_NO_WARNINGS']
DLL_NAME = 'CvGameCore_Expansion2.dll'
# Imports the proxy may have: the CRT (msvcr90 and msvcp90, the VC9 STL: both in the Microsoft.VC90.CRT assembly the
# manifest names), Lua (the engine's module) and Windows system DLLs.
# Since issue 2504 also the engine's database library (the real DLL imports it too): the proxy reads the engine's
# connection (PRAGMA database_list, fingerprint, BinaryIO::Save) and replays the DLL's writes on it.
DB_DLL = 'cvgamedatabasewin32final release.dll'
ALLOWED_IMPORTS = {'msvcr90.dll', 'msvcp90.dll', 'kernel32.dll', 'lua51_win32.dll', DB_DLL, 'user32.dll', 'advapi32.dll',
                   'ntdll.dll',
                   'winmm.dll', 'shell32.dll', 'ole32.dll', 'oleaut32.dll', 'gdi32.dll', 'psapi.dll', 'dbghelp.dll',
                   'shlwapi.dll', 'version.dll', 'ws2_32.dll', 'kernelbase.dll'}

# Ticket 0535: the farm builder guard of the composite, compiled into the proxy unchanged (one source, two modules; the proxy
# owns it on every 64-bit start: proxy/src/proxy_farm_guard.cpp).
FARM_GUARD_SRCS = [REPO / 'shell' / 'composite' / 'farm_guard.cpp']

# Ticket 0609: ONE setting of the VC9 STL's checking macros for every x86 translation unit this script compiles (the proxy
# DLL with vpipc, the options file, the farm guard and the generated code; vp64helper.exe). They change the layout of
# std::vector, map, list, set and their iterators, and a std:: member function keeps one mangled name in both forms: two
# settings in one module let the linker pair a caller of one layout with a callee of the other (the 4798 spike's crashes,
# research/06-tooling-and-workflows/proxy-build-settings.md). The value is the DLL's own (CvGameCoreDLLPCH.h): off.
STL_SETTING = (('_SECURE_SCL', '0'), ('_HAS_ITERATOR_DEBUGGING', '0'))
STL_MARK = re.compile(rb'/FAILIFMISMATCH:"?(vp_stl_[A-Z_]+)=([^"\s]*)"?')


def stl_setting_args(obj):
    """Compiler arguments of the one setting: /D per macro, -Werror=macro-redefined (a file defining another value
    fails; after -Wno-everything) and a forced include, written here, whose #pragma detect_mismatch puts the value the
    TU sees into its object: lld-link then fails on two objects of one module with different values."""
    h = obj / 'stlsetting' / 'vp_stl_setting.h'
    h.parent.mkdir(parents=True, exist_ok=True)
    h.write_text('// written by proxy/build_proxy.py (ticket 0609): forced include of every x86 TU\n'
                 '#define VP_STL_STR2(x) #x\n#define VP_STL_STR(x) VP_STL_STR2(x)\n' +
                 ''.join('#pragma detect_mismatch("vp_stl%s", VP_STL_STR(%s))\n' % (m, m) for m, _ in STL_SETTING),
                 encoding='ascii')
    return ['/D%s=%s' % kv for kv in STL_SETTING] + ['-Werror=macro-redefined', '/FI' + str(h)]


def check_stl_objects(objs, module):
    """Every object of one module carries each STL marker once, with STL_SETTING's value: an object compiled without
    stl_setting_args (no marker, which lld-link cannot see) or with another value fails. Returns facts."""
    want = dict(('vp_stl' + m, v) for m, v in STL_SETTING)
    bad = []
    for o in objs:
        got = {}
        for k, v in STL_MARK.findall(Path(o).read_bytes()):
            got.setdefault(k.decode(), set()).add(v.decode())
        if {k: {v} for k, v in want.items()} != got:
            bad.append('%s %s' % (Path(o).name, ', '.join('%s=%s' % (k, '/'.join(sorted(got.get(k, ['missing']))))
                                                          for k in want)))
    log('check stl      %s: %s, %d objects%s' % ('ok' if not bad else 'FAIL', module, len(objs),
                                                 ', all ' + ' '.join('%s=%s' % kv for kv in STL_SETTING) if not bad
                                                 else '; not ' + ' '.join('%s=%s' % kv for kv in STL_SETTING) + ': ' +
                                                 '; '.join(bad)))
    if bad:
        raise BuildError('%s: %d object(s) without the one STL setting (ticket 0609): %s' % (module, len(bad),
                                                                                           '; '.join(bad)))
    return {'objects': len(objs), 'setting': dict(STL_SETTING)}

LOG = []
SRC_ROOTS = []   # issue 0115: the folders sources are compiled from by their relative name (set in main: <out>, gen)


def log(s=''):
    print(s, flush=True)
    LOG.append(s)


class BuildError(Exception):
    pass


def repo_identity(repo, repo_commit=None):
    """Issue 2801: the repo field of the build id. Returns (commit text or None, note). None: the repository has .git
    and build_info.py's git values stand. Without .git: --repo-commit, else the first word of <repo>/.repo-commit, else
    unknown."""
    if (repo / '.git').exists():
        return None, 'git work tree' + (' (--repo-commit %s ignored: git names the commit)' % repo_commit
                                         if repo_commit else '')
    if repo_commit:
        return '%s (archive)' % repo_commit.strip(), 'no .git: --repo-commit'
    f = repo / '.repo-commit'
    if f.is_file():
        words = f.read_text(encoding='utf-8', errors='replace').split()
        if words:
            return '%s (archive)' % words[0], 'no .git: %s' % f
    return 'unknown (archive: no .git, no --repo-commit, no .repo-commit)', None


def apply_repo_identity(bpath, bvals, repo, repo_commit, say):
    """Rewrites VP_BUILD_REPO_COMMIT / VP_BUILD_REPO_DIRTY of the header build_info.write_header made when the
    repository is an archive copy (issue 2801). Shared with server/build_server.py."""
    commit, note = repo_identity(Path(repo), repo_commit)
    if commit is None:
        say('[build id] repo: %s' % note)
        return bvals
    if note is None:
        say('[build id] LOUD: the repository %s has no .git and no --repo-commit or .repo-commit: the build id says '
            '"repo unknown"' % repo)
    else:
        say('[build id] repo %s (%s)' % (commit, note))
    bvals = dict(bvals, VP_BUILD_REPO_COMMIT=commit, VP_BUILD_REPO_DIRTY='unknown')
    text = Path(bpath).read_text(encoding='ascii', errors='replace').splitlines()
    out = []
    for line in text:
        for k in ('VP_BUILD_REPO_COMMIT', 'VP_BUILD_REPO_DIRTY'):
            if line.startswith('#define %s ' % k):
                line = '#define %s "%s"' % (k, str(bvals[k]).replace('\\', '/').replace('"', "'"))
        out.append(line)
    Path(bpath).write_text('\n'.join(out) + '\n', encoding='ascii', errors='replace')
    return bvals


def check_pure_list():
    """Issue 2801 (verification 2700 finding 5), issue 3503: the audited purity lists hold and proxy/src has no
    compiled list (gen_pure_list.py --check); raises BuildError otherwise."""
    cp = subprocess.run([sys.executable, str(HERE / 'tools' / 'gen_pure_list.py'), '--check'], capture_output=True,
                        text=True, errors='replace')
    out = (cp.stdout + cp.stderr).strip()
    log('[pure] gen_pure_list.py --check: %s (rc %d)' % (out, cp.returncode))
    if cp.returncode != 0:
        raise BuildError('the purity lists do not hold (gen_pure_list.py --check: %s)' % out)


EXEBUILD_INCLUDE = REPO / 'exebuild' / 'include'
EMIT_HEADER = REPO / 'tools' / 'exebuild' / 'emit_header.py'


def emit_exe_tables(gen_dir):
    """Step 2b (ticket 4960): vp_exe_tables.gen.h into gen_dir from the local tables; BuildError (naming the setting
    exe_tables, C5_EXE_TABLES and the generator command) when they are missing or stale."""
    cp = subprocess.run([sys.executable, '-I', str(EMIT_HEADER), '--out', str(gen_dir)], capture_output=True, text=True,
                        errors='replace')
    out = (cp.stdout + cp.stderr).strip()
    log('[exebuild] %s (rc %d)' % (out, cp.returncode))
    if cp.returncode != 0:
        raise BuildError('the EXE address header (tools/exebuild/emit_header.py): %s' % out)


PURITY_TEST = HERE / 'tools' / 'test_purity.py'
# The files the full purity test's verdict depends on (issue 0480): the model (lists, baseline, the regen section's
# files), the tools, the loader and its unit test, the model generator.
PURITY_INPUTS = ['proxy/model/*', 'proxy/tools/derive_purity.py', 'proxy/tools/gen_pure_list.py', 'proxy/tools/test_purity.py',
                 'proxy/tools/rt_tests/purity_unit.cpp', 'proxy/src/proxy_purity.cpp', 'proxy/src/proxy_purity.h',
                 'proxy/gen/*.py']
PURITY_CACHE_DEFAULT = Path('C:/c5-64bit-work/cache/purity-test')


def purity_inputs_key(src_identity):
    """SHA-256 over the purity test's inputs (PURITY_INPUTS, CRLF folded to LF so a checkout's line endings do not
    count) and the DLL source's identity (the locked commit or fingerprint)."""
    h = hashlib.sha256()
    files = sorted({p for pat in PURITY_INPUTS for p in REPO.glob(pat) if p.is_file()})
    for p in files:
        h.update(p.relative_to(REPO).as_posix().encode() + b'\0')
        h.update(hashlib.sha256(p.read_bytes().replace(b'\r\n', b'\n')).digest())
    h.update(('src ' + str(src_identity)).encode())
    return h.hexdigest(), len(files)


# Ticket 0574 (MC #7044/#7046): a pin whose source changes audited rows. derive_purity.py at the pin drops them (the safe
# direction: those calls cross to the server); the full test then needs a source where every audited row is kept, the
# baseline's audited_at commit (as the release driver's purity stage does, 0480). Only rows named in an allow-list may
# drop; any other dropped row fails the build. Both come from the environment (the release driver's build stage sets
# them from the release descriptor's purity_allowed_drops and a plain copy of audited_at); without them a drop fails.
DERIVE_PURITY = HERE / 'tools' / 'derive_purity.py'
ALLOWED_DROPS_ENV = 'C5_PURITY_ALLOWED_DROPS'   # a file: one "kind<TAB>key" row per allowed dropped row; '#' comments
AUDITED_SRC_ENV = 'C5_PURITY_AUDITED_SRC'       # a plain copy of proxy/model/purity-baseline.json's audited_at commit


def read_allowed_drops(path):
    """{(kind, key)} from an allow-list file (kind<TAB>key per row; '#' and empty lines skipped)."""
    out = set()
    for ln in Path(path).read_text(encoding='utf-8').splitlines():
        if not ln.strip() or ln.lstrip().startswith('#'):
            continue
        parts = ln.split('\t')
        if len(parts) < 2 or parts[0] not in ('method', 'lua') or not parts[1].strip():
            raise BuildError('%s: a row that is not "method|lua<TAB>key": %r' % (path, ln))
        out.add((parts[0], parts[1].strip()))
    return out


def derive_dropped(src, dll, scratch, run=None):
    """[(kind, key, reason)] the derivation drops at this source (report.tsv rows whose decision is not kept). run: a
    test hook replacing the subprocess (gets the command, returns (rc, text))."""
    out = scratch / 'derive-at-pin'
    shutil.rmtree(out, ignore_errors=True)
    cmd = [sys.executable, '-I', str(DERIVE_PURITY), '--src', str(src), '--dll', str(dll), '--out', str(out)]
    if run is None:
        cp = subprocess.run(cmd, capture_output=True, text=True, errors='replace', timeout=900)
        rc, text = cp.returncode, cp.stdout + cp.stderr
    else:
        rc, text = run(cmd)
    rep = out / 'report.tsv'
    if rc != 0 or not rep.is_file():
        raise BuildError('derive_purity.py at the pin failed (rc %d): %s' % (rc, text.strip()[-500:]))
    rows = []
    for ln in rep.read_text(encoding='utf-8', errors='replace').splitlines()[1:]:
        p = ln.split('\t')
        if len(p) >= 3 and p[2] != 'kept':
            rows.append((p[0], p[1], p[3] if len(p) > 3 else '-'))
    return rows


def purity_test_source(src, dll, scratch, env=None, run=None):
    """(source for the full test, its identity suffix, dropped rows): src when the pin drops no audited row; else the
    audited source, when every dropped row is in the allow-list (one LOUD line each). BuildError otherwise."""
    env = os.environ if env is None else env
    dropped = derive_dropped(src, dll, scratch, run)
    if not dropped:
        return src, '', []
    allow_file = env.get(ALLOWED_DROPS_ENV, '')
    allowed = read_allowed_drops(allow_file) if allow_file else set()
    for kind, key, why in dropped:
        ok = (kind, key) in allowed
        log('[purity-test] LOUD: the pin drops the audited %s row %s (%s): %s' % (
            kind, key, why, 'in the allow-list %s' % allow_file if ok else 'NOT in the allow-list'))
    bad = [(k, n) for k, n, _ in dropped if (k, n) not in allowed]
    if bad:
        raise BuildError('the pin drops %d audited purity row(s) not in the allow-list (%s): %s; re-audit them, or name '
                         'them in the release descriptor\'s purity_allowed_drops' % (
                             len(bad), allow_file or '%s not set' % ALLOWED_DROPS_ENV,
                             ', '.join('%s %s' % b for b in bad)))
    audited = env.get(AUDITED_SRC_ENV, '')
    if not audited or not Path(audited).is_dir():
        raise BuildError('the pin drops %d allowed audited row(s); the full purity test needs the audited source '
                         '(%s = a plain copy of purity-baseline.json\'s audited_at commit), not given' % (
                             len(dropped), AUDITED_SRC_ENV))
    log('[purity-test] %d allowed dropped row(s): the full test runs on the audited source %s' % (len(dropped), audited))
    return Path(audited), ' audited ' + str(audited), dropped


def full_purity_test(src, info, dll, out, skip):
    """Step 7 (issue 0480): proxy/tools/test_purity.py in full (unit, ASan, derive with its seeded cases, refusals,
    check, regen), run only when its inputs changed since the last pass on this machine: a pass is a stamp
    <cache>/<key>.json (cache: $C5_PURITY_TEST_CACHE, else C:/c5-64bit-work/cache/purity-test). About 110 s when it runs
    [L 2026-11-17]. Skipped, said so, for --unlocked sources off the pin (derive drops rows there, the test needs every
    row kept; the release driver's purity stage tests those) and where the test's VS 2022 vcvarsall is missing. Returns
    the summary record; raises BuildError when the test fails (its scratch <out>/purity-test is then kept)."""
    if skip:
        log('[purity-test] LOUD: --no-purity-test: the full purity test was NOT run (issue 0480); the purity lists of '
            'this build are checked only by step 0')
        return {'result': 'skipped (--no-purity-test)'}
    if info.get('mode') == 'git-unlocked':
        log('[purity-test] not run: --unlocked source (%s); the release driver\'s purity stage runs it on such sources'
            % info.get('note', ''))
        return {'result': 'skipped (unlocked source)'}
    spec = importlib.util.spec_from_file_location('test_purity_for_build', PURITY_TEST)
    tp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tp)
    if not Path(tp.VCVARSALL).is_file():
        log('[purity-test] LOUD: not run: %s missing (the test\'s MSVC ASan section needs VS 2022)' % tp.VCVARSALL)
        return {'result': 'skipped (no VS 2022)'}
    scratch = out / 'purity-test'
    # ticket 0574: a pin that drops audited rows is tested on the audited source, allowed rows only
    test_src, ident, dropped = purity_test_source(src, dll, out / 'purity-derive')
    dropped_rec = ['%s %s' % (k, n) for k, n, _ in dropped]
    key, nfiles = purity_inputs_key(str(info.get('head') or info.get('lock_commit') or info.get('note')) + ident)
    cache = Path(os.environ.get('C5_PURITY_TEST_CACHE') or PURITY_CACHE_DEFAULT)
    stamp = cache / (key + '.json')
    if stamp.is_file():
        log('[purity-test] passed before for these inputs (%d files, key %s): %s' % (nfiles, key[:16], stamp))
        return {'result': 'cached pass', 'key': key, 'stamp': str(stamp), 'dropped_allowed': dropped_rec}
    src = test_src
    t = time.perf_counter()
    log('[purity-test] inputs changed (key %s, %d files): running %s' % (key[:16], nfiles, PURITY_TEST))
    try:
        cp = subprocess.run([sys.executable, '-I', str(PURITY_TEST), '--src', str(src), '--dll', str(dll),
                             '--scratch', str(scratch)], capture_output=True, text=True, errors='replace', timeout=1800)
        rc, text = cp.returncode, cp.stdout + cp.stderr
    except subprocess.TimeoutExpired:
        rc, text = -9, 'timeout after 1800 s'
    LOG.append(text)
    secs = round(time.perf_counter() - t, 1)
    summ = re.search(r'^test_purity: (\d+)/(\d+) checks passed', text, re.M)
    if rc != 0 or not summ or summ.group(1) != summ.group(2):
        fails = [ln for ln in text.splitlines() if ln.startswith('FAIL')]
        print('\n'.join(fails[:12]) or text[-3000:])
        raise BuildError('the full purity test failed (proxy/tools/test_purity.py, rc %d, %s, %.0f s; scratch kept: '
                         '%s); run it by hand to see why, or build with --no-purity-test to go on without it'
                         % (rc, summ.group(0) if summ else 'no summary', secs, scratch))
    rec = {'result': 'pass', 'checks': '%s/%s' % (summ.group(1), summ.group(2)), 'seconds': secs, 'key': key,
           'files': nfiles, 'src': str(src), 'when': time.strftime('%Y-%m-%d %H:%M:%S'),
           'dropped_allowed': dropped_rec}
    cache.mkdir(parents=True, exist_ok=True)
    stamp.write_text(json.dumps(rec, indent=1) + '\n', encoding='utf-8')
    shutil.rmtree(scratch, ignore_errors=True)
    log('[purity-test] %s passed in %.0f s; stamp %s' % (rec['checks'], secs, stamp))
    return dict(rec, stamp=str(stamp))


def capture_env(bat):
    cp = subprocess.run('cmd /d /s /c ""%s" >NUL && set"' % bat, capture_output=True, text=True, errors='replace')
    env = {}
    for line in cp.stdout.splitlines():
        if '=' in line:
            k, v = line.split('=', 1)
            env[k] = v
    if 'INCLUDE' not in env:
        raise BuildError('could not capture the VS 2008 environment from %s' % bat)
    return env


def run(cmd, env, cwd=None):
    cp = subprocess.run(cmd, capture_output=True, text=True, errors='replace', env=env, cwd=cwd)
    out = '\n'.join(l for l in (cp.stdout + cp.stderr).splitlines()
                    if l.strip() and 'vswhere.exe' not in l and l.strip() != 'operable program or batch file.')
    return cp.returncode, out


def compile_all(jobs, env, tus, label):
    """tus: list of (src, obj, flags). Raises BuildError on any failure."""
    t0 = time.perf_counter()

    def one(tu):
        src, obj, flags = tu
        t = time.perf_counter()
        # issue 0115: the object's S_OBJNAME relative to <out>/obj (the PDB carries it); the source compiled from its
        # root (this repository or <out>) by its relative name, as anonymous-namespace hashes follow the name
        cwd, rel = x64build.source_cwd(src, REPO, *SRC_ROOTS)
        rc, out = run([tc.CLANG, '/nologo', '/c'] + flags + x64build.objname_args(obj, Path(obj).parents[1]) +
                      ['/Fo' + str(obj), rel], env, cwd=cwd)
        # clang-cl may echo the source name; keep diagnostics only
        out = '\n'.join(l for l in out.splitlines() if l.strip() != Path(src).name)
        return src, rc == 0 and Path(obj).exists(), out, time.perf_counter() - t

    failed = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        for src, ok, out, dt in ex.map(one, tus):
            log('  [%s] %-4s %-48s %5.1f s' % (label, 'ok' if ok else 'FAIL', Path(src).name, dt))
            if out:
                LOG.append(out[-6000:])
                if not ok:
                    print(out[-6000:])
            if not ok:
                failed.append(Path(src).name)
    log('  [%s] %d TUs in %.1f s' % (label, len(tus), time.perf_counter() - t0))
    if failed:
        raise BuildError('%s: %d TU(s) failed: %s' % (label, len(failed), ', '.join(failed)))


def readobj(flag, path):
    cp = subprocess.run([str(READOBJ), flag, str(path)], capture_output=True, text=True, errors='replace')
    return cp.stdout


def check_dll(dll):
    """Imports, exports and the manifest resource. Returns (ok, facts)."""
    facts = {}
    imp = readobj('--coff-imports', dll)
    # (.+ not \S+: the database library's name has a space)
    dlls = sorted(set(d.strip().lower() for d in re.findall(r'Name: (.+\.dll)', imp, re.I)))
    bad = [d for d in dlls if d not in ALLOWED_IMPORTS]
    facts['imports'] = dlls
    ok_imp = 'msvcr90.dll' in dlls and 'lua51_win32.dll' in dlls and DB_DLL in dlls and not bad
    log('check imports  %s: %s%s' % ('ok' if ok_imp else 'FAIL', ', '.join(dlls),
                                     ('; not allowed: ' + ', '.join(bad)) if bad else ''))
    exp = readobj('--coff-exports', dll)
    exports = re.findall(r'Name: (\S+)', exp)
    facts['exports'] = exports
    ok_exp = exports == ['DllGetGameContext']
    log('check exports  %s: %s' % ('ok' if ok_exp else 'FAIL', ', '.join(exports) or '(none)'))
    res = readobj('--coff-resources', dll)
    # llvm-readobj prints the resource tree: Type: MANIFEST (ID 24) ... Name: (ID 2)
    ok_man = bool(re.search(r'Type: (kRT_)?MANIFEST \(ID 24\)', res)) and bool(re.search(r'Name: \(ID 2\)', res))
    facts['manifest_resource'] = ok_man
    log('check manifest %s: %s' % ('ok' if ok_man else 'FAIL', 'RT_MANIFEST id 2 present' if ok_man else res[-800:]))
    return ok_imp and ok_exp and ok_man, facts


HELPER_SRCS = [HERE / 'helper' / 'vp64helper.cpp', HERE / 'helper' / 'proxy_cfgini.cpp']   # D125: the config.ini routine
HELPER_NAME = 'vp64helper.exe'
HELPER_IMPORTS = {'kernel32.dll', 'user32.dll', 'gdi32.dll', 'shell32.dll'}   # no network library (D120)


def build_helper(env, obj, bin_, opt, repro, config):
    """Step 5b (issue 3520): proxy/helper/vp64helper.cpp + proxy_cfgini.cpp -> <out>/bin/vp64helper.exe. x86, static CRT (VC9 libcmt: no
    runtime the player must have), a GUI EXE without a console whose entry is main (the wide entries pull two CRT start-up
    objects with VC9 libcmt and lld). Checked: imports only HELPER_IMPORTS. Returns facts for build-proxy.json."""
    t = time.perf_counter()
    hobj = obj / 'helper'
    hobj.mkdir(parents=True, exist_ok=True)
    objs = [hobj / (src.stem + '.obj') for src in HELPER_SRCS]
    exe = bin_ / HELPER_NAME
    for p in objs + [exe, bin_ / 'vp64helper.pdb']:
        if p.exists():
            p.unlink()
    flags = tc.ARCH_ARGS['x86'] + ['/MT', '/EHsc', '/Zc:wchar_t', '-Wno-everything'] + opt + ['/Z7'] + repro +         ['/I' + str(HERE / 'helper'), '/I' + str(HERE / 'src')] +         stl_setting_args(obj)   # ticket 0609
    for src, o in zip(HELPER_SRCS, objs):
        rc, out = run([tc.CLANG, '/c'] + flags + [str(src), '/Fo' + str(o)], env, cwd=str(hobj))
        if rc or not o.exists():
            LOG.append(out)
            raise BuildError('helper compile %s: %s' % (src.name, out[-2000:]))
    stl = check_stl_objects(objs, HELPER_NAME)   # ticket 0609
    lp = lambda q: x64build.link_path(q, obj)
    largs = [tc.LLD_LINK, '/nologo', '/MACHINE:X86', '/OUT:' + lp(exe), '/DEBUG', '/PDB:' + lp(bin_ / 'vp64helper.pdb'),
             '/DYNAMICBASE', '/NXCOMPAT', '/SUBSYSTEM:WINDOWS', '/ENTRY:mainCRTStartup', '/MANIFEST:NO'] + \
        x64build.REPRO_LINK + (['/OPT:REF', '/OPT:ICF'] if config == 'release' else []) + \
        [lp(o) for o in objs] + ['kernel32.lib', 'user32.lib', 'gdi32.lib', 'shell32.lib']
    started = time.time()
    rc, out = run(largs, env, cwd=str(obj))
    if out:
        LOG.append(out)
    why = linkcheck.link_failure(rc, out, exe, started)
    if why:
        raise BuildError('helper link: %s' % why)
    imp = readobj('--coff-imports', exe)
    dlls = sorted(set(d.strip().lower() for d in re.findall(r'Name: (.+\.dll)', imp, re.I)))
    bad = [d for d in dlls if d not in HELPER_IMPORTS]
    log('[helper] %s, %d bytes (%.1f s); imports %s%s' % (exe, exe.stat().st_size, time.perf_counter() - t,
                                                       ', '.join(dlls), ('; NOT ALLOWED: ' + ', '.join(bad)) if bad else ''))
    if bad or not dlls:
        raise BuildError('helper imports not allowed: %s' % ', '.join(bad or ['(none read)']))
    info = linkcheck.artifact_info(exe)
    return {'exe': str(exe), 'sha256': info['sha256'], 'size': info['size'], 'imports': dlls, 'stl': stl}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', required=True, type=Path, help='output directory (outside the repository)')
    ap.add_argument('--src', type=Path, required=True,
                    help='DLL fork source at the deps/dll.lock commit (required; no default since issue 9008)')
    ap.add_argument('--gen', type=Path, default=None, help='generated directory (gen_runtime.py --out) to use as is')
    ap.add_argument('--config', choices=['release', 'debug'], default='release')
    ap.add_argument('--unlocked', action='store_true', help='allow a git --src at another commit (fork development)')
    ap.add_argument('--jobs', type=int, default=min(16, os.cpu_count() or 4))
    ap.add_argument('--repo-commit', default=None,
                    help='this repository\'s commit when it is a copy without .git (an archive; issue 2801)')
    ap.add_argument('--no-purity-test', action='store_true',
                    help='leave out step 7, the full purity test (issue 0480; logged LOUD)')
    ap.add_argument('--quietlock-override', metavar='"WHO: WHY"',
                    help='start although another team holds the quiet-machine lock (emergencies only, '
                         'logged; COMMON rule 16)')
    a = ap.parse_args()
    # COMMON rule 16 launch gate (perf/quietlock/gate.py, tracker 7006): refuse while another team holds the
    # quiet-machine lock; the holder's own runs pass (C5_QUIETLOCK_WHO); a gate error never blocks.
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'perf' / 'quietlock'))
    try:
        import gate as _launch_gate
    except ImportError:
        _launch_gate = None
        print('launch gate: warning: perf/quietlock/gate.py not found; launch goes on', file=sys.stderr)
    if _launch_gate:
        _launch_gate.require('a proxy build (proxy/build_proxy.py)', a.quietlock_override)
    t_all = time.perf_counter()
    out = a.out.resolve()
    if str(out).lower().startswith(str(REPO).lower() + os.sep):
        print('build_proxy: FAILED: --out must be outside the repository', flush=True)
        return 1
    out.mkdir(parents=True, exist_ok=True)
    summary = {'started': time.strftime('%Y-%m-%d %H:%M:%S'), 'config': a.config, 'steps': {}}
    try:
        log('build_proxy.py %s, config %s, out %s' % (summary['started'], a.config, out))
        # 0. pure (issue 2801)
        check_pure_list()
        # 1. src
        try:
            info = x64build.resolve_src(a.src, a.unlocked)
        except x64build.SrcError as e:
            raise BuildError('src: %s' % e)
        src = Path(info['src'])
        log('[src] %s: %s' % (src, info.get('note', info.get('mode', ''))))
        summary['src'] = {'path': str(src), 'mode': info.get('mode'), 'note': info.get('note')}

        # 2. gen
        if a.gen:
            gen = a.gen.resolve()
            if not (gen / 'common' / 'vp_ids.gen.h').is_file() or not (gen / 'proxy').is_dir():
                raise BuildError('--gen %s: no common/vp_ids.gen.h or proxy/ (run proxy/gen/gen_runtime.py)' % gen)
            log('[gen] using %s' % gen)
        else:
            gen = out / 'gen'
            t = time.perf_counter()
            cp = subprocess.run([sys.executable, str(HERE / 'gen' / 'gen_runtime.py'), '--out', str(gen),
                                 '--hooks-proxy', str(REPO / 'proxy' / 'src'),
                                 '--hooks-server', str(REPO / 'server' / 'src' / 'remote'), '--src', str(src)],
                                capture_output=True, text=True, errors='replace')
            LOG.append(cp.stdout + cp.stderr)
            if cp.returncode != 0:
                print(cp.stdout + cp.stderr)
                raise BuildError('gen_runtime.py exited %d' % cp.returncode)
            hooked = re.search(r'^proxy .*hooked \(hand-written\) (\d+), loud defaults (\d+)', cp.stdout, re.M)
            schema = re.search(r'kAppSchemaHash (0x[0-9a-f]+)', cp.stdout)
            log('[gen] %s in %.1f s: schema %s, proxy hooks hand-written %s, loud defaults %s' % (
                gen, time.perf_counter() - t, schema.group(1) if schema else '?', hooked.group(1) if hooked else '?',
                hooked.group(2) if hooked else '?'))
        summary['gen'] = str(gen)
        SRC_ROOTS[:] = [gen.parent, out]   # issue 0115: generated sources by their name relative to gen's folder
        # 2b. exebuild (ticket 4960, epic 0590, D202): the EXE address header from the local tables
        exegen = out / 'gen-exebuild'
        emit_exe_tables(exegen)

        env = x64build.repro_env(capture_env(tc.ENV_BAT['x86']))   # issue 0115: SOURCE_DATE_EPOCH
        obj = out / 'obj'
        bin_ = out / 'bin'
        for d in (obj / 'vpipc', obj / 'proxy', bin_):
            d.mkdir(parents=True, exist_ok=True)
        # release: NDEBUG, as the server's vpipc; without it vpipc copies every received payload into pages it decommits
        # afterwards (VPIPC_VALIDITY_CHECK, channel_impl.h): about 5 us per message (measured 2026-11-10, epic 2300)
        opt = ['/O2', '/Oy-', '/DNDEBUG'] if a.config == 'release' else ['/Od', '/Oy-']
        dbg = ['/Z7']  # CodeView in the objects; the linker writes the PDB
        # issue 0115: the repository, the fork source and the build folder under fixed names in every compiler string
        repro = x64build.repro_cl_args((REPO, 'VPREPO'), (src, 'VPSRC'), (out, 'VPOUT'), (gen.parent, 'VPGEN'))

        # 3. vpipc
        vflags = ['-m32', '/MD', '/EHsc', '/W3', '/Zc:wchar_t', '-msse3'] + opt + dbg + repro + \
            ['/I' + str(REPO / 'ipc' / 'include'), '/I' + str(REPO / 'ipc' / 'src')] + ['/D' + d for d in VPIPC_DEFS] +             stl_setting_args(obj)   # ticket 0609: vpipc is linked into the proxy, one module
        vtus = [(p, obj / 'vpipc' / (p.stem + '.obj'), vflags) for p in sorted((REPO / 'ipc' / 'src').glob('*.cpp'))]
        log('[vpipc] %d sources' % len(vtus))
        compile_all(a.jobs, env, vtus, 'vpipc')
        vlib = obj / 'vpipc_x86.lib'
        rc, o = run([str(LLVM_LIB), '/nologo', '/OUT:' + x64build.link_path(vlib, obj)] +
                    [x64build.link_path(t[1], obj) for t in vtus], env, cwd=str(obj))   # issue 0115: member names
        if rc:
            LOG.append(o)
            raise BuildError('llvm-lib vpipc: %s' % o[-2000:])
        log('[vpipc] %s' % vlib)

        # 4. compile (issue 2704: the build-id header first, proxy/gen/build_info.py)
        bpath, bvals = build_info.write_header(obj / 'buildinfo', info, a.config)
        bvals = apply_repo_identity(bpath, bvals, REPO, a.repo_commit, log)  # issue 2801: archive copies
        summary['build_id'] = bvals
        log('[build id] %s: %s' % (bpath, ', '.join('%s=%s' % (k[9:].lower(), v) for k, v in bvals.items())))
        aflags = tc.ARCH_ARGS['x86'] + ['/MD', '/EHsc', '/Zc:wchar_t', '-msse3', '-Wno-everything'] + opt + dbg + repro + \
            ['/D' + d for d in tc.DEFINES] + ['/I' + str(src / i) for i in tc.INCLUDES] + \
            ['/I' + str(obj / 'buildinfo'), '/I' + str(REPO / 'proxy' / 'src'), '/I' + str(REPO / 'proxy' / 'include'),
             '/I' + str(REPO / 'ipc' / 'include'), '/I' + str(gen / 'common'), '/I' + str(gen / 'proxy'),
             '/I' + str(REPO / 'options' / 'include'),  # one-off 9042: the options file
             '/I' + str(REPO / 'shell' / 'composite'), '/I' + str(REPO / 'shell' / 'common'),  # ticket 0535: the farm guard
             '/I' + str(EXEBUILD_INCLUDE), '/I' + str(exegen)] +             stl_setting_args(obj)   # ticket 0609: the one STL setting, after -Wno-everything
        srcs = [gen / 'common' / 'vp_ids.gen.cpp'] + sorted((gen / 'proxy').glob('*.cpp')) + \
            sorted((REPO / 'proxy' / 'src').glob('*.cpp')) + sorted((REPO / 'options' / 'src').glob('*.cpp')) + \
            FARM_GUARD_SRCS
        tus = [(p, obj / 'proxy' / (p.stem.replace('.', '_') + '.obj'), aflags) for p in srcs]
        log('[compile] %d sources' % len(tus))
        compile_all(a.jobs, env, tus, 'compile')
        summary['stl'] = check_stl_objects([t[1] for t in vtus + tus], DLL_NAME)   # ticket 0609, with lld-link's own

        # 5. link
        deffile = obj / 'CvGameCore_Expansion2.def'
        deffile.write_text('LIBRARY CvGameCore_Expansion2\nEXPORTS\n\tDllGetGameContext\n', encoding='ascii')
        dll = bin_ / DLL_NAME
        pdb = bin_ / 'CvGameCore_Expansion2.pdb'
        manifest = obj / (DLL_NAME + '.manifest')
        for p in (dll, pdb, manifest):
            if p.exists():
                p.unlink()
        # issue 0115: the two import libraries of the source are copied next to the objects, and the link runs there
        # with relative paths (the PDB records every path the linker is given; lld hashes the PDB under /Brepro)
        lua_lib = obj / 'lua51_Win32.lib'
        lua_lib.write_bytes((src / 'ThirdPartyLibs' / 'Lua51' / 'lib' / 'lua51_Win32.lib').read_bytes())
        db_lib = obj / 'CvGameDatabaseWin32.lib'  # the closed import library (issue 2504)
        db_lib.write_bytes((src / 'CvGameDatabase' / 'lib' / 'CvGameDatabaseWin32.lib').read_bytes())
        lp = lambda q: x64build.link_path(q, obj)
        largs = [tc.LLD_LINK, '/nologo', '/DLL', '/MACHINE:X86', '/OUT:' + lp(dll), '/DEF:' + lp(deffile),
                 '/DEBUG', '/PDB:' + lp(pdb), '/DYNAMICBASE', '/NXCOMPAT', '/SUBSYSTEM:WINDOWS',
                 '/MANIFEST', '/MANIFESTFILE:' + lp(manifest), '/MANIFESTUAC:NO',
                 '/IMPLIB:' + lp(obj / 'CvGameCore_Expansion2.lib')] + x64build.REPRO_LINK   # issue 0115
        if a.config == 'release':
            largs += ['/OPT:REF', '/OPT:ICF']
        largs += [lp(t[1]) for t in tus] + [lp(vlib), lp(lua_lib), lp(db_lib), 'kernel32.lib', 'user32.lib', 'advapi32.lib']
        t = time.perf_counter()
        link_started = time.time()
        rc, o = run(largs, env, cwd=str(obj))
        if o:
            LOG.append(o)
        why = linkcheck.link_failure(rc, o, dll, link_started)   # one-off 9023 (P10): rc, error line, fresh DLL
        if why:
            print(o[-6000:])
            raise BuildError('lld-link: %s' % why)
        log('[link] %s, %d bytes (%.1f s)' % (dll, dll.stat().st_size, time.perf_counter() - t))
        if not manifest.exists():
            raise BuildError('the linker wrote no manifest (%s)' % manifest)
        mtext = manifest.read_text(errors='replace')
        if 'Microsoft.VC90.CRT' not in mtext:
            raise BuildError('the manifest has no Microsoft.VC90.CRT dependency:\n' + mtext)
        if not SDK_MT.exists():
            raise BuildError('SDK v6.0A mt.exe not found at %s' % SDK_MT)
        rc, o = run([str(SDK_MT), '-nologo', '-manifest', str(manifest), '-outputresource:%s;#2' % dll], env)
        if rc:
            LOG.append(o)
            raise BuildError('mt.exe exited %d: %s' % (rc, o[-2000:]))
        dep = re.search(r"name='Microsoft\.VC90\.CRT' version='([^']+)'", mtext)
        log('[manifest] embedded as resource id 2 with %s (Microsoft.VC90.CRT %s)' % (SDK_MT, dep.group(1) if dep else '?'))

        # 5b. helper (issue 3520): the error popup vp64helper.exe, shipped beside the DLL
        helper = build_helper(env, obj, bin_, opt, repro, a.config)
        summary['helper'] = helper

        # 6. check
        ok, facts = check_dll(dll)
        summary['check'] = facts
        if not ok:
            raise BuildError('output checks failed')
        # 7. purity-test (issue 0480): the full purity test when its inputs changed since the last pass here
        summary['purity_test'] = full_purity_test(src, info, dll, out, a.no_purity_test)
        summary['dll'] = str(dll)
        summary['pdb'] = str(pdb) if pdb.exists() else None
        info = linkcheck.artifact_info(dll)       # after mt.exe embedded the manifest: the final bytes
        summary['dll_sha256'], summary['dll_size'], summary['dll_mtime'] = info['sha256'], info['size'], info['mtime']
        summary['result'] = 'ok'
        log('build_proxy: OK in %.0f s: %s sha256 %s size %d mtime %s'
            % (time.perf_counter() - t_all, dll, info['sha256'], info['size'], info['mtime']))
        rc = 0
    except BuildError as e:
        summary['result'] = 'FAILED: %s' % e
        log('build_proxy: FAILED: %s' % e)
        rc = 1
    summary['seconds'] = round(time.perf_counter() - t_all, 1)
    (out / 'build-proxy.log').write_text('\n'.join(LOG) + '\n', encoding='utf-8')
    (out / 'build-proxy.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    return rc


if __name__ == '__main__':
    sys.exit(main())
