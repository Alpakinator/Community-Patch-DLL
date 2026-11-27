"""Shared toolchain settings for the proxy generator scripts (epic 2100).

Both configurations mirror the DLL's own builds:
  x86 = build_vp_clang.py in the fork (clang-cl -m32 in the VS 2008 environment: VC9 headers, C++11 default)
  x64 = tools/x64-dll/build.py (clang-cl -m64 with the MSVC 14.4x STL from vcvars64, /std:c++14, VP_OUT_OF_PROCESS)
Every Windows path is a raw string (COMMON.md rule 12).

The DLL source (issue 3503, epic 3500): every script of the model chain takes `--src <fork source>` and resolves it with
resolve_source() below (a git checkout at the lock's commit without tracked changes, or a plain copy whose source
fingerprint equals the lock's; `--lock` names another lock file, for a release candidate; `--unlocked` accepts any
fork-shaped tree, labelled by `--commit`). use_source() then sets FORK and PINNED_COMMIT here and in the environment
(VP_PROXY_GEN_SRC, VP_PROXY_GEN_COMMIT), so worker processes of the scripts see the same source. Without `--src` and
without VP_PROXY_GEN_SRC, FORK_DEFAULT is used, unchecked: since ticket 4708 that is the one DLL source setting of
tools/x64-dll/dll_source.py (VP_DLL_SRC, else deps/dll-source.ini, else the upstream layout `..`), here the fork clone,
labelled COMMIT_DEFAULT (e9d0c4837, that clone's HEAD; not checked).
The compiler scratch folder BUILD_ROOT is VP_PROXY_GEN_WORK, else <temp>/claude/team2100 (tempfile.gettempdir()).
"""
import importlib.util
import os
import subprocess
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
DLL_SOURCE = os.path.join(REPO, 'tools', 'x64-dll', 'dll_source.py')   # the one DLL source setting (ticket 4701)


def load_dll_source():
    """tools/x64-dll/dll_source.py, loaded by path (its folder name is not a package name)."""
    spec = importlib.util.spec_from_file_location('dll_source', DLL_SOURCE)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# Ticket 4708: the default is the one setting, no path of its own (was the literal path of the fork clone, the value
# deps/dll-source.ini gives here). VP_PROXY_GEN_SRC still overrides it; use_source() sets it for worker processes.
FORK_DEFAULT = str(load_dll_source().dll_source())
COMMIT_DEFAULT = 'e9d0c4837a7012bcd66e8756ab32f3532e932031'
FORK = os.environ.get('VP_PROXY_GEN_SRC') or FORK_DEFAULT
PINNED_COMMIT = os.environ.get('VP_PROXY_GEN_COMMIT') or COMMIT_DEFAULT
BUILD_ROOT = os.environ.get('VP_PROXY_GEN_WORK') or os.path.join(tempfile.gettempdir(), 'claude', 'team2100')
PROXY = os.path.join(REPO, 'proxy')
X64_BUILD = os.path.join(REPO, 'tools', 'x64-dll', 'build.py')   # the deps/dll.lock reader and source fingerprint
DLL_LOCK = os.path.join(REPO, 'deps', 'dll.lock')


class SourceError(Exception):
    """--src (or --lock) refused; the message is one line."""


def _x64build():
    spec = importlib.util.spec_from_file_location('x64build_tc', X64_BUILD)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def resolve_source(src, lock=None, unlocked=False, commit=None):
    """Check a DLL fork source for the model chain (issue 3503). Returns {src, head, mode, note, lock_commit}.

    lock: a lock file in deps/dll.lock's format (default deps/dll.lock); its `commit` and `fingerprint` lines are used.
    mode 'git': a git checkout at the lock's commit without tracked changes; 'copy': a plain copy (no .git) whose source
    fingerprint (tools/x64-dll/build.py fingerprint_dir) equals the lock's; 'unlocked': any fork-shaped tree, only with
    unlocked=True, labelled by `commit` (else the git HEAD, else 'unknown'). Raises SourceError otherwise."""
    if not src:
        raise SourceError('--src is required (a DLL fork source: a git checkout or a plain copy at the lock commit)')
    root = os.path.abspath(src)
    if not os.path.isfile(os.path.join(root, 'build_vp_clang.py')) or \
            not os.path.isdir(os.path.join(root, 'CvGameCoreDLL_Expansion2')):
        raise SourceError('--src %s: not a DLL fork source tree (no build_vp_clang.py / CvGameCoreDLL_Expansion2)' % root)
    xb = _x64build()
    lock_path = os.path.abspath(lock or DLL_LOCK)
    is_git = os.path.exists(os.path.join(root, '.git'))
    head = xb.git_out(xb.Path(root), 'rev-parse', 'HEAD') if is_git else ''
    if unlocked:
        label = commit or head or 'unknown'
        return {'src': root, 'head': label, 'mode': 'unlocked', 'lock_commit': '',
                'note': '--unlocked: %s %s' % ('git checkout at' if is_git else 'plain copy labelled', label[:12])}
    if not os.path.isfile(lock_path):
        raise SourceError('--lock %s: no such file' % lock_path)
    lk = xb.read_dll_lock(xb.Path(lock_path))
    want = lk.get('commit', '')
    if len(want) != 40:
        raise SourceError('%s: no 40-digit `commit` line' % lock_path)
    if is_git:
        dirty = xb.git_out(xb.Path(root), 'status', '--porcelain', '--untracked-files=no')
        if head != want or dirty:
            raise SourceError('--src %s: git HEAD %s%s, the lock %s pins %s (pass the lock commit, or --unlocked)'
                              % (root, head[:9] or 'unknown', ' with tracked changes' if dirty else '', lock_path,
                                 want[:9]))
        return {'src': root, 'head': head, 'mode': 'git', 'lock_commit': want,
                'note': 'git checkout at the lock commit %s, no tracked changes' % want[:9]}
    fp = xb.lock_fingerprint(lk)
    if not fp or fp[0] != xb.FINGERPRINT_VERSION:
        raise SourceError('%s: no `fingerprint = %s <sha256> <n> files` line (needed to check a plain copy)'
                          % (lock_path, xb.FINGERPRINT_VERSION))
    got, n = xb.fingerprint_dir(root)
    if got != fp[1]:
        raise SourceError('--src %s: plain copy whose source fingerprint %s... (%d files) is not the lock\'s %s... '
                          '(%s pins %s); pass a copy of that commit, or --unlocked' % (root, got[:16], n, fp[1][:16],
                                                                                         lock_path, want[:9]))
    return {'src': root, 'head': want, 'mode': 'copy', 'lock_commit': want,
            'note': 'plain source copy, fingerprint matches the lock commit %s (%d files)' % (want[:9], n)}


def add_source_args(ap, required=True):
    """The --src/--lock/--unlocked/--commit/--work options shared by the model chain's scripts."""
    ap.add_argument('--src', required=required, help='DLL fork source (git checkout or plain copy at the lock commit)')
    ap.add_argument('--lock', help='lock file to check --src against (default deps/dll.lock)')
    ap.add_argument('--unlocked', action='store_true', help='accept any fork-shaped source (fork development, a test)')
    ap.add_argument('--commit', help='with --unlocked: the label recorded as the source commit')
    ap.add_argument('--work', help='scratch folder for compiler outputs (default BUILD_ROOT)')


def use_source(info, work=None):
    """Make `info` (resolve_source's result) the source of every helper here, and of worker processes."""
    global FORK, PINNED_COMMIT, BUILD_ROOT
    FORK = info['src']
    PINNED_COMMIT = info['head']
    os.environ['VP_PROXY_GEN_SRC'] = FORK
    os.environ['VP_PROXY_GEN_COMMIT'] = PINNED_COMMIT
    if work:
        BUILD_ROOT = os.path.abspath(work)
        os.environ['VP_PROXY_GEN_WORK'] = BUILD_ROOT

CLANG = r'C:\Program Files\LLVM\bin\clang-cl.exe'
LLD_LINK = r'C:\Program Files\LLVM\bin\lld-link.exe'
ENV_BAT = {
    'x86': r'C:\Program Files (x86)\Microsoft Visual Studio 9.0\Common7\Tools\vsvars32.bat',
    'x64': r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat',
}
INCLUDES = ['CvGameCoreDLL_Expansion2', r'CvWorldBuilderMap\include', r'CvGameCoreDLLUtil\include',
            r'CvLocalization\include', r'CvGameDatabase\include', r'FirePlace\include',
            r'FirePlace\include\FireWorks', r'ThirdPartyLibs\Lua51\include']
DEFINES = ['FXS_IS_DLL', 'WIN32', '_WINDOWS', '_USRDLL', 'EXTERNAL_PAUSING', 'CVGAMECOREDLL_EXPORTS',
           'FINAL_RELEASE', '_CRT_SECURE_NO_WARNINGS', '_WINDLL', 'NDEBUG', 'STRONG_ASSUMPTIONS',
           'VPRELEASE_ERRORMSG']
ARCH_ARGS = {
    'x86': ['-m32'],
    'x64': ['-m64', '/std:c++14', '/D_HAS_AUTO_PTR_ETC=1', '/D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS',
            '/DVP_OUT_OF_PROCESS'],
}


def dll_args(arch):
    """clang-cl arguments of the DLL's own build for one arch (without /c and sources)."""
    return ARCH_ARGS[arch] + ['/MD', '/EHsc', '/Zc:wchar_t', '-msse3', '-Wno-everything'] + \
        ['/D' + d for d in DEFINES] + ['/I' + os.path.join(FORK, i) for i in INCLUDES]


def run_in_env(arch, argv, cwd=None):
    """Run argv (list) inside the arch's compiler environment; returns CompletedProcess (text)."""
    cmd = 'cmd /d /s /c ""%s" >NUL && %s"' % (ENV_BAT[arch], ' '.join('"%s"' % a for a in argv))
    # bug 0108: vcvars otherwise starts a background PowerShell for its telemetry
    env = dict(os.environ, VSCMD_SKIP_SENDTELEMETRY='1')
    return subprocess.run(cmd, capture_output=True, text=True, errors='replace', cwd=cwd, env=env)


def clang(arch, src, extra, cwd=None, dll_flags=True):
    argv = [CLANG] + (dll_args(arch) if dll_flags else ARCH_ARGS[arch]) + extra + [src]
    return run_in_env(arch, argv, cwd=cwd)


def rel(path):
    p = os.path.normpath(path)
    for root in (FORK, REPO):
        if p.lower().startswith(root.lower() + os.sep):
            return p[len(root) + 1:].replace('\\', '/')
    return p.replace('\\', '/')
