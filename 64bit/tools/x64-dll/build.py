"""Scripted x64 build of the Vox Populi DLL (CvGameCore_Expansion2.dll).

Issue 1001 (epic 1000), issue 1405 (epic 1400). Reads sources from `--src`: a git checkout of the DLL fork at
the commit pinned in `deps/dll.lock` (verified; `--unlocked` builds any other commit), or a plain source copy of
that commit without `.git` (for example a `git archive` export). Writes every output (objects, PCH, DLL, PDB,
map, logs, census lists) under `--out`; nothing is written into the source tree. `commit_id.inc` (the
CURRENT_GAMECORE_VERSION string, untracked in the fork) is generated in `<out>/gen/` and mapped over
`<src>/commit_id.inc` with a clang VFS overlay. `--shorten` adds -Wshorten-64-to-32 and checks the unique sites
against `narrowing-baseline.tsv` (the narrowing gate): any site not in the baseline fails the build.

Toolchain (team-1000 coordinator, Minor decision, 2026-11-10): clang-cl 19.1.7 -m64 + lld-link, MSVC 14.43
STL/CRT headers and libs via vcvars64.bat (VS 2022 Community), Windows SDK 10.0.26100.0, /std:c++14, /MD.

See tools/x64-dll/README.md for usage, flags and measured timings.
"""

from __future__ import annotations

import argparse
import collections
import concurrent.futures as cf
import ctypes
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

LLVM_BIN = Path(r'C:\Program Files\LLVM\bin')
CLANG_CL = LLVM_BIN / 'clang-cl.exe'
LLD_LINK = LLVM_BIN / 'lld-link.exe'
VCVARS64 = Path(r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat')
HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]


def _load_dll_source():
    """tools/x64-dll/dll_source.py by path: build.py is itself loaded by path from other folders (ticket 4701)."""
    import importlib.util
    spec = importlib.util.spec_from_file_location('dll_source', HERE / 'dll_source.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


dll_source = _load_dll_source()
# The DLL source location without --src: the one setting of dll_source.py (VP_DLL_SRC, deps/dll-source.ini, else
# the upstream layout `..`); used only if it is at the locked commit (resolve_src). Ticket 4701.
DEFAULT_SRC = dll_source.dll_source()
DEFAULT_SRC_ORIGIN = dll_source.origin()
DLL_LOCK = REPO / 'deps' / 'dll.lock'
NARROWING_BASELINE = HERE / 'narrowing-baseline.tsv'
SHORTEN_FLAG = '-Wshorten-64-to-32'
# Ticket 4810: with --shorten every diagnostic names the PHYSICAL file and line, not the '#line'-remapped ones (the
# speed packages of epic 0504 put '#line N' after each block so __LINE__ does not move; the gate reads the line's text).
# A diagnostics option only: code and LTO bitcode objects are byte-identical with and without it (probe, ticket 4810).
PHYSICAL_LOC_ARGS = ['-Xclang', '-fno-diagnostics-use-presumed-location']

CORE_DLL = 'CvGameCore_Expansion2'
DEF_FILE = r'CvGameCoreDLL_Expansion2\CvGameCoreDLL.def'
PCH_CPP = r'CvGameCoreDLL_Expansion2\_precompile.cpp'
PCH_H = 'CvGameCoreDLLPCH.h'
PCH = 'CvGameCoreDLLPCH.pch'

INCLUDE_DIRS = [
    r'CvWorldBuilderMap\include',
    r'CvGameCoreDLLUtil\include',
    r'CvLocalization\include',
    r'CvGameDatabase\include',
    r'FirePlace\include',
    r'FirePlace\include\FireWorks',
    r'ThirdPartyLibs\Lua51\include',
    r'CvGameCoreDLL_Expansion2',  # the Lua\CvLua*.cpp need it to find the PCH header under /Yu
]
SHARED_PREDEFS = [
    # build_vp_clang.py
    'FXS_IS_DLL', 'WIN32', '_WINDOWS', '_USRDLL', 'EXTERNAL_PAUSING', 'CVGAMECOREDLL_EXPORTS', 'FINAL_RELEASE',
    '_CRT_SECURE_NO_WARNINGS', '_WINDLL',
    # x64 additions (issue 1001)
    '_HAS_AUTO_PTR_ETC=1', '_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS', 'VP_OUT_OF_PROCESS',
]
PREDEFS = {
    'release': SHARED_PREDEFS + ['STRONG_ASSUMPTIONS', 'NDEBUG', 'VPRELEASE_ERRORMSG'],
    'debug': SHARED_PREDEFS + ['VPDEBUG'],
}
# Ticket 0159 (decision D107, the owner 2026-11-13): `fast` = release's compile flags and defines without -flto, linked without
# /LTCG. Same code paths as release (asserts, logging, defines), only no whole-program optimisation: the link takes
# seconds and well under 1 GB instead of minutes and 6-13 GB. For functional work only, never for timing: the DLL
# carries a marker section (CONFIG_MARKER) that the timing tools refuse (timing_refusal below).
PREDEFS['fast'] = list(PREDEFS['release'])
# Ticket 4801 (epic 0504 P1): `pgo-instr` = release with IR profile instrumentation (-fprofile-generate), a whole-program
# link like release, the profile runtime and tools/x64-dll/pgo/pgo_flush.c linked in. It trains a profile and is never
# pinned, shipped or timed: marked like fast (CONFIG_MARKER), so the timing tools refuse it. A release build consumes a
# profile with --profile FILE (a measurement build; research/08-performance/speedup-p1-pgo.md).
PREDEFS['pgo-instr'] = list(PREDEFS['release'])
# Ticket 0513 (decision D184, the owner 2026-11-17): `release-pgo` = release compiled with a merged PGO profile (the fifth
# configuration; research/06-tooling-and-workflows/x64-dll-build-configurations.md 4b). Same defines, flags and link as
# release plus /clang:-fprofile-use and the .vppgo section naming the profile's SHA-256; its compile and link lines are
# the ones 4801's `release --profile` measured (-13.3 % H, -15.0 % M per turn, identical games once bug 0508 is fixed).
# The profile is a locked build input: deps/dll.lock `profile` (SHA-256, size, pin, training set, tools) and
# `profile-file` (where the file lives; it is not committed). Timing allowed: it is what ships once the MC makes it the
# release default. `release` without a profile keeps its bytes (upstream's check job, every pin before the switch).
PREDEFS['release-pgo'] = list(PREDEFS['release'])
CONFIGS = ('debug', 'release', 'fast', 'pgo-instr', 'release-pgo')
# configurations whose link takes a place of perf/quietlock/linkplace.py (and whose compile is -flto)
WHOLE_PROGRAM = ('release', 'pgo-instr', 'release-pgo')
PGO_INSTR = 'pgo-instr'
RELEASE_PGO = 'release-pgo'
# The retrain rule (ticket 0513; speedup-p1-pgo.md 10.4): a release-pgo build whose compile reports more than this
# share of the profile's functions as hash mismatches or missing, or whose profile was trained on another commit than
# the source's (retrain before each release, that is at every new pin), records pgo_retrain.needed = true; the release
# take stops on it. A stale profile costs speed, never correctness (unmatched functions compile as without a profile).
PGO_RETRAIN_SHARE = 0.01
PGO_RT_DIR = LLVM_BIN.parent / 'lib' / 'clang' / '19' / 'lib' / 'windows'  # clang_rt.profile-x86_64.lib (LLVM 19.1.7)
PGO_RT_LIB = 'clang_rt.profile-x86_64.lib'
PGO_FLUSH_C = HERE / 'pgo' / 'pgo_flush.c'
PGO_FLUSH_SYMBOL = 'vp_pgo_flush_init'
# Ticket 4932 (epic 0454 X2): the DLL's render messages, compiled only for a source that has the header (as hidden
# embarks, cl_args); docs/extras/flags-and-messages-design.md 2.4.
RENDER_EVENTS_HEADER = 'CvGameCoreDLL_Expansion2/VP64RenderEvents.h'
RENDER_EVENTS_DEFINE = 'VP64_RENDER_EVENTS'
CL_SUPPRESS = [
    'invalid-offsetof', 'tautological-constant-out-of-range-compare', 'comment', 'enum-constexpr-conversion',
    'c++11-narrowing',  # x86 parity: clang-cl in VC9 mode only warns on these (CvTeam.cpp)
]

# ---- the major-civilisation count (issue 4252, epic 0334; MC decisions G2 and G3, thread 0334 #1614) ----
# Upstream's 43-civilisation DLL is the same source with `#define MOD_GLOBAL_MAX_MAJOR_CIVS (43)` uncommented in
# CustomModsGlobal.h (research/03-dll-engine-boundary/player-limits.md 1.1). We build it from the unchanged pin with the
# define on the DLL's compiler line instead (G2: no fork branch). 22 is the default and means the define is ABSENT: the
# source tests `#if defined(MOD_GLOBAL_MAX_MAJOR_CIVS)` in three places, so `=22` would compile other code than the
# default build. Only 22 and 43 are validated (G3); 2..62 are what the header's comment allows (63 leaves
# MAX_MINOR_CIVS 0 and does not compile, player-limits.md 6.1). The server and the proxy never take the define (G1: they
# read the count at run time), so it is not in PREDEFS, which build_server.py and the proxy read.
MAJOR_CIVS_DEFINE = 'MOD_GLOBAL_MAX_MAJOR_CIVS'
MAJOR_CIVS_DEFAULT = 22
MAJOR_CIVS_VALIDATED = (22, 43)
MAJOR_CIVS_RANGE = (2, 62)
MAJOR_CIVS_HEADER = 'CvGameCoreDLLUtil/include/CustomModsGlobal.h'
_MAJOR_DEFINE_RE = re.compile(r'^\s*#\s*define\s+' + MAJOR_CIVS_DEFINE + r'\b\s*(?P<val>.*?)\s*$')


class MajorCivsError(Exception):
    """A --major-civs value or a source's header line that the build refuses (one line; exit non-zero)."""


def check_major_civs(n, unvalidated: bool = False) -> int:
    """The validated count, or MajorCivsError: outside 2..62 always; another value than 22 or 43 only with
    unvalidated=True (--unvalidated-major-civs)."""
    try:
        n = int(n)
    except (TypeError, ValueError):
        raise MajorCivsError(f'--major-civs {n!r}: not a number')
    lo, hi = MAJOR_CIVS_RANGE
    if not lo <= n <= hi:
        raise MajorCivsError(f'--major-civs {n}: outside {lo}..{hi} (the header allows 2 to 62; 63 and above leave '
                             f'no city-state slot and do not compile: research/03-dll-engine-boundary/player-limits.md '
                             f'6.1)')
    if n not in MAJOR_CIVS_VALIDATED and not unvalidated:
        raise MajorCivsError(f'--major-civs {n}: not validated (only {" and ".join(map(str, MAJOR_CIVS_VALIDATED))} '
                             'are, MC decision G3); add --unvalidated-major-civs to build it anyway')
    return n


def _strip_block_comments(text: str) -> str:
    """The text with /* ... */ comments replaced by spaces (newlines kept, so line numbers stay)."""
    return re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), text, flags=re.S)


def header_major_civs(src) -> tuple:
    """(value or None, 'path:line: text' or None) of the source's own MOD_GLOBAL_MAX_MAJOR_CIVS line in
    CustomModsGlobal.h: None when every such line is commented out (the default 22). Upstream cut Release-3.8.1,
    3.10.11 and 4.2.5 with the line uncommented (player-limits.md 1.1), so the build reads it instead of assuming 22.
    MajorCivsError when the header is missing, has two live lines, or a value that is not a plain number."""
    p = Path(src) / MAJOR_CIVS_HEADER
    try:
        text = p.read_text(encoding='utf-8', errors='replace')
    except OSError as e:
        raise MajorCivsError(f'{p}: cannot read the header that holds {MAJOR_CIVS_DEFINE} ({e})')
    live = []
    for i, line in enumerate(_strip_block_comments(text).splitlines(), 1):
        code = line.split('//', 1)[0]
        m = _MAJOR_DEFINE_RE.match(code)
        if m:
            live.append((i, m.group('val'), text.splitlines()[i - 1].strip()))
    if not live:
        return None, None
    if len(live) > 1:
        raise MajorCivsError(f'{p}: {len(live)} live {MAJOR_CIVS_DEFINE} lines (' +
                             ', '.join(f'line {i}' for i, _, _ in live) + ')')
    i, val, raw = live[0]
    m = re.fullmatch(r'\(?\s*(\d+)\s*\)?', val)
    if not m:
        raise MajorCivsError(f'{p}:{i}: {raw!r}: not a plain number')
    return int(m.group(1)), f'{MAJOR_CIVS_HEADER}:{i}: {raw}'


def resolve_major_civs(src, requested=MAJOR_CIVS_DEFAULT, unvalidated: bool = False) -> dict:
    """The major-civilisation count of this build. Returns {major_civs, define (the /D flag or None), header (the live
    header line or None), tag ('' for 22, else 'm<N>': layout names and the version string), note}.
    A live header line is either the requested value (then no /D: the header already sets it) or MajorCivsError,
    never a silent 22."""
    n = check_major_civs(requested, unvalidated)
    hval, hline = header_major_civs(src)
    if hval is not None:
        if hval != n:
            raise MajorCivsError(f'the source sets {MAJOR_CIVS_DEFINE} to {hval} ({hline}), --major-civs is {n}: '
                                 f'build it with --major-civs {hval}, or from a source whose line is commented out')
        define, note = None, f'{n} major civilizations from the source\'s own header line ({hline}); no /D'
    elif n == MAJOR_CIVS_DEFAULT:
        define, note = None, f'{n} major civilizations (the default: {MAJOR_CIVS_DEFINE} not defined; the header\'s ' \
                             'lines are commented out)'
    else:
        define = f'/D{MAJOR_CIVS_DEFINE}={n}'
        note = f'{n} major civilizations: {define} on the DLL\'s compiler line (the header\'s lines are commented out)'
    tag = '' if n == MAJOR_CIVS_DEFAULT else f'm{n}'
    return {'major_civs': n, 'define': define, 'header': hline, 'tag': tag, 'note': note}


def major_civs_tag(n) -> str:
    """'' for the default 22, else 'm<N>': the layout names (x64-<commit8>-m43-p<tools8>) and the version suffix."""
    return '' if int(n) == MAJOR_CIVS_DEFAULT else f'm{int(n)}'
DEFAULT_LIBS = [
    'winmm.lib', 'kernel32.lib', 'user32.lib', 'gdi32.lib', 'winspool.lib', 'comdlg32.lib', 'advapi32.lib',
    'shell32.lib', 'ole32.lib', 'oleaut32.lib', 'uuid.lib', 'odbc32.lib', 'odbccp32.lib',
]

# Grouping of unresolved symbols by closed library: the probe's rules
# (tools/x64-dll/evidence/2026-11-10-probe/probe-group-unresolved.py). First match wins.
GROUP_RULES = [
    ('CvGameDatabase', r'Database::'),
    ('CvLocalization', r'Localization|CvLocalization'),
    ('CvWorldBuilderMap', r'WorldBuilder|FRelationshipBitset|\bMod(DataEntry|Type|Enum)::'),
    ('lua51', r'(\blua_|\bluaL_|\bluaopen_)'),
    ('FLua', r'FLua|\bLua::'),
    ('CvGameCoreDLLUtil', r'operator<<\(class FDataStream &, enum |operator>>\(class FDataStream &, enum |CvString|'
                          r'CvStringUtils|CvEnum|FromUTF8|ToUTF8|CvAssert|CvDllPrinter'),
    ('FireWorks', r'\bF[A-Z]\w*|\bFire[A-Z]\w*|LOGFILEMGR|\bgDLL'),
]

DIAG_RE = re.compile(r'^(?P<file>.+?)\((?P<line>\d+),(?P<col>\d+)\):\s+(?P<kind>warning|error|fatal error):\s+'
                     r'(?P<msg>.*?)(?:\s+\[(?P<flag>-W[^\],]+)[^\]]*\])?\s*$')
UNDEF_RE = re.compile(r'^lld-link: (?:warning|error): undefined symbol: (?P<sym>.+?)\s*$')
REF_RE = re.compile(r'^>>> referenced (?:by (?P<by>.+)|(?P<more>\d+) more times)\s*$')
REF_CONT_RE = re.compile(r'^>>>\s{2,}(?P<at>\S.*?)\s*$')  # with debug info: "by <src>:<line>" then "<obj>:(<fn>)"


class Log:
    """build.log: one file, flushed per line, also echoed to stdout when `echo`."""

    def __init__(self, path: Path, append: bool = False):
        self.f = open(path, 'a' if append else 'w', encoding='utf-8', newline='\n')
        if append:
            self.f.write('\n')

    def __call__(self, text: str = '', echo: bool = True):
        self.f.write(text + '\n')
        self.f.flush()
        if echo:
            print(text, flush=True)

    def close(self):
        self.f.close()


def die(log, msg, code=1):
    log(f'FAILED: {msg}')
    log.close()
    sys.exit(code)


class SrcError(Exception):
    pass


def read_dll_lock(path: Path = DLL_LOCK) -> dict:
    """deps/dll.lock: `key = value` lines, indented continuation lines, `#` comments."""
    d, key = {}, None
    for raw in path.read_text(encoding='utf-8').splitlines():
        if not raw.strip() or raw.lstrip().startswith('#'):
            continue
        if raw[:1].isspace() and key:
            d[key] += '\n' + raw.strip()
            continue
        k, sep, v = raw.partition('=')
        if sep:
            key = k.strip()
            d[key] = v.strip()
    return d


def git_out(src: Path, *args) -> str:
    cp = subprocess.run(['git', '-C', str(src)] + list(args), capture_output=True, text=True, errors='replace')
    return cp.stdout.strip() if cp.returncode == 0 else ''


# Fingerprint of the build inputs (issue 1508): lets a plain copy without .git be checked against the locked commit.
# v1 = SHA-256 over the sorted lines "<path>\t<sha256 of the file with CRLF -> LF>\n" of every file whose path starts
# with one of FINGERPRINT_DIRS and ends with one of FINGERPRINT_EXTS, plus the files in FINGERPRINT_FILES. CRLF is
# folded because `git archive` and checkouts apply the fork's `eol=crlf` attributes while the blobs may hold LF.
# deps/dll.lock records it; tools/x64-dll/src_fingerprint.py computes it from git or from a directory.
FINGERPRINT_VERSION = 'v1'
FINGERPRINT_DIRS = ('CvGameCoreDLL_Expansion2/', 'CvGameCoreDLLUtil/', 'CvGameDatabase/', 'CvLocalization/',
                    'CvWorldBuilderMap/', 'FirePlace/', 'ThirdPartyLibs/')
FINGERPRINT_EXTS = ('.h', '.hpp', '.inl', '.c', '.cpp', '.def')
FINGERPRINT_FILES = ('build_vp_clang.py',)


def _fp_selected(rel: str) -> bool:
    return rel in FINGERPRINT_FILES or (rel.startswith(FINGERPRINT_DIRS) and rel.lower().endswith(FINGERPRINT_EXTS))


def _fp_digest(entries) -> tuple:
    """entries: iterable of (posix path, raw bytes). Returns (hex digest, file count)."""
    lines = sorted(rel + '\t' + hashlib.sha256(data.replace(b'\r\n', b'\n')).hexdigest() + '\n' for rel, data in entries)
    return hashlib.sha256(''.join(lines).encode('utf-8')).hexdigest(), len(lines)


def fingerprint_dir(src) -> tuple:
    """Fingerprint of a source tree on disk (a plain copy). Returns (hex, file count)."""
    src = Path(src)

    def files():
        for top in FINGERPRINT_FILES + FINGERPRINT_DIRS:
            p = src / top
            if p.is_file():
                yield top, p.read_bytes()
            elif p.is_dir():
                for f in p.rglob('*'):
                    rel = f.relative_to(src).as_posix()
                    if f.is_file() and _fp_selected(rel):
                        yield rel, f.read_bytes()
    return _fp_digest(files())


def fingerprint_git(repo, commit: str) -> tuple:
    """Fingerprint of `commit` in the git repository `repo`, from its blobs (git ls-tree + git cat-file)."""
    ls = subprocess.run(['git', '-C', str(repo), 'ls-tree', '-r', '-z', commit], capture_output=True, check=True)
    objs = []
    for rec in ls.stdout.split(b'\0'):
        if not rec:
            continue
        meta, rel = rec.split(b'\t', 1)
        rel = rel.decode('utf-8')
        _mode, kind, sha = meta.split()
        if kind == b'blob' and _fp_selected(rel):
            objs.append((rel, sha.decode()))
    cat = subprocess.run(['git', '-C', str(repo), 'cat-file', '--batch'],
                         input=''.join(o[1] + '\n' for o in objs).encode(), capture_output=True, check=True).stdout
    entries, pos = [], 0
    for rel, _sha in objs:
        nl = cat.index(b'\n', pos)
        size = int(cat[pos:nl].split()[2])
        entries.append((rel, cat[nl + 1:nl + 1 + size]))
        pos = nl + 1 + size + 1
    return _fp_digest(entries)


def lock_fingerprint(lock: dict):
    """deps/dll.lock `fingerprint = v1 <hex> <n> files`. Returns (version, hex, n) or None."""
    parts = lock.get('fingerprint', '').split()
    if len(parts) >= 3 and parts[2].isdigit():
        return parts[0], parts[1], int(parts[2])
    return None


def resolve_src(src, unlocked: bool = False) -> dict:
    """Check --src against deps/dll.lock. Returns {src, mode, head, describe, dirty, lock_commit, note}.

    mode 'git': a git working tree at the locked commit without tracked changes.
    mode 'git-unlocked': any other git state, only with unlocked=True (fork development).
    mode 'copy': no `.git` (a plain source copy, e.g. `git archive <commit> | tar -x`); accepted only if its source
    fingerprint (fingerprint_dir) equals the `fingerprint` line of deps/dll.lock (issue 1508).
    Raises SrcError with a one-line reason otherwise; every caller exits non-zero on it before compiling anything."""
    lock = read_dll_lock()
    commit = lock['commit']
    given = src is not None
    src = Path(src if given else DEFAULT_SRC).resolve()
    how = (f'deps/dll.lock pins {commit[:9]} ({lock.get("describe", "?")}). Pass --src <git checkout at that commit> '
           f'or a plain copy of it (git -C <fork> archive {commit[:9]} | tar -x -C <dir>), or --unlocked to build '
           'another commit on purpose')
    whence = '' if given else f' (the default, from {DEFAULT_SRC_ORIGIN})'
    if not (src / 'build_vp_clang.py').is_file() or not (src / 'CvGameCoreDLL_Expansion2').is_dir():
        raise SrcError(f'--src {src}{whence}: not a DLL fork source tree (no build_vp_clang.py / '
                       'CvGameCoreDLL_Expansion2). ' + how)
    info = {'src': src, 'lock_commit': commit, 'lock_describe': lock.get('describe', '')}
    if (src / '.git').exists():
        head = git_out(src, 'rev-parse', 'HEAD')
        changes = git_out(src, 'status', '--porcelain', '--untracked-files=no')
        info.update(head=head, describe=git_out(src, 'describe', '--tags', '--long', 'HEAD') or f'No-Tag {head[:9]}',
                    dirty=bool(changes))
        if head == commit and not changes:
            info.update(mode='git', note=f'git checkout at the locked commit {commit[:9]}, no tracked changes')
        elif unlocked:
            info.update(mode='git-unlocked', note=f'--unlocked: HEAD {head[:9]}'
                        f'{", tracked changes" if changes else ""}; deps/dll.lock pins {commit[:9]}')
        else:
            what = f'HEAD is {head[:9] or "unknown"}' if head != commit else 'HEAD is the locked commit'
            if changes:
                what += f' with {len(changes.splitlines())} tracked change(s)'
            raise SrcError(f'--src {src}{whence}: {what}. ' + how)
    else:
        if not info['lock_describe']:
            raise SrcError(f'{DLL_LOCK} has no describe line; needed to build from a copy without .git')
        want = lock_fingerprint(lock)
        if not want or want[0] != FINGERPRINT_VERSION:
            raise SrcError(f'{DLL_LOCK} has no `fingerprint = {FINGERPRINT_VERSION} <sha256> <n> files` line, needed to '
                           'check a copy without .git (python tools/x64-dll/src_fingerprint.py --git <fork clone> '
                           'prints it)')
        got, nfiles = fingerprint_dir(src)
        if got != want[1]:
            raise SrcError(f'--src {src}{whence}: plain copy (no .git) that is not the locked commit {commit[:9]}: source '
                           f'fingerprint {got[:16]}... over {nfiles} files, deps/dll.lock has {want[1][:16]}... over '
                           f'{want[2]} files (another commit, edited files, or extra or missing sources under '
                           f'{", ".join(d.rstrip("/") for d in FINGERPRINT_DIRS)}). ' + how)
        info.update(mode='copy', head=commit, describe=info['lock_describe'], dirty=False,
                    note=f'plain source copy (no .git), fingerprint matches the locked commit {commit[:9]} '
                         f'({nfiles} files)')
    return info


def version_string(info: dict, major_tag: str = '') -> str:
    """CURRENT_GAMECORE_VERSION: "<describe> <Clean|Dirty|Copy>", with "+<tag>" after the describe for another major
    count than 22 (issue 4252: "Release-5.4.8-51-g04b1211b8+m43 Copy"), so a 43 DLL never reports itself as the 22 one
    (the save carries the string, player-limits.md 5.1). The form follows the instance-aware builds' "+ia<n>"."""
    status = 'Copy' if info['mode'] == 'copy' else ('Dirty' if info['dirty'] else 'Clean')
    return f'{info["describe"]}{"+" + major_tag if major_tag else ""} {status}'


def write_commit_overlay(out: Path, info: dict, major_tag: str = '') -> Path:
    """<out>/gen/commit_id.inc (the string update_commit_id.bat would write) plus a clang VFS overlay that maps it
    over <src>/commit_id.inc, so `#include "../commit_id.inc"` finds it whether or not the source has one.
    Status word: Clean/Dirty as the batch for a git tree, Copy for a plain copy (git status is unknown there).
    major_tag: resolve_major_civs()'s tag ('' for 22: the string is unchanged)."""
    gen = out / 'gen'
    gen.mkdir(parents=True, exist_ok=True)
    inc = gen / 'commit_id.inc'
    text = (f'const char CURRENT_GAMECORE_VERSION[] = "{version_string(info, major_tag)}"; '
            '//autogenerated by tools/x64-dll/build.py, do not commit this file!\n')
    if not inc.exists() or inc.read_text(encoding='utf-8') != text:  # keep the timestamp when unchanged
        inc.write_text(text, encoding='utf-8')
    overlay = gen / 'vfsoverlay.yaml'
    overlay.write_text(json.dumps({'version': 0, 'case-sensitive': 'false', 'roots': [
        {'name': str(info['src']), 'type': 'directory',
         'contents': [{'name': 'commit_id.inc', 'type': 'file', 'external-contents': str(inc)}]}]}, indent=1),
        encoding='utf-8')
    return overlay


def load_vcvars_env(log, cache: Path) -> dict:
    """Environment of `vcvars64.bat` (INCLUDE, LIB, LIBPATH, VCToolsVersion, WindowsSDKVersion, PATH...).
    Cached in --out (vcvars64.bat takes 6-60 s on this machine); --clean drops the cache."""
    if cache.exists():
        log(f'vcvars64 environment: cached in {cache}')
        return json.loads(cache.read_text(encoding='utf-8'))
    env = run_vcvars(log)
    cache.write_text(json.dumps(env), encoding='utf-8')
    return env


def run_vcvars(log) -> dict:
    marker = '===VCVARS-ENV==='
    # VSCMD_SKIP_SENDTELEMETRY: vcvars otherwise starts a background PowerShell for its telemetry (bug 0108; 4000
    # group C, 2026-11-13)
    cp = subprocess.run(f'cmd /d /s /c ""{VCVARS64}" >NUL && echo {marker} && set"', capture_output=True,
                        text=True, errors='replace', env=dict(os.environ, VSCMD_SKIP_SENDTELEMETRY='1'))
    if cp.returncode != 0 or marker not in cp.stdout:
        die(log, f'vcvars64.bat failed (rc={cp.returncode}): {cp.stderr.strip()}')
    if cp.stderr.strip():
        log(f'vcvars64.bat stderr (harmless if only vswhere): {cp.stderr.strip()}', echo=False)
    env = {}
    for line in cp.stdout.split(marker, 1)[1].splitlines():
        if '=' in line:
            k, v = line.split('=', 1)
            env[k] = v
    # Our LLVM first, so lld-link finds nothing older; vcvars puts MSVC's own LLVM copy on PATH otherwise.
    env['PATH'] = str(LLVM_BIN) + os.pathsep + env.get('PATH', '')
    return env


def first_line(cmd, env=None, cwd=None) -> str:
    try:
        cp = subprocess.run(cmd, capture_output=True, text=True, errors='replace', env=env, cwd=cwd)
        text = (cp.stdout + cp.stderr).strip()
        return text.splitlines()[0] if text else ''
    except OSError as e:
        return f'? ({e})'


def read_cpp_list(src: Path) -> list:
    """The 172 translation units of the fork's build_vp_clang.py `CPP = [...]` list (the PCH TU is separate)."""
    text = (src / 'build_vp_clang.py').read_text(encoding='utf-8')
    m = re.search(r'^CPP = \[(.*?)^\]', text, re.S | re.M)
    if not m:
        raise RuntimeError('CPP = [...] not found in build_vp_clang.py')
    return [c.replace('\\\\', '\\') for c in re.findall(r"'([^']+\.cpp)'", m.group(1))]


# Repeatable builds (issue 0115, 2026-11-13). Two builds of the same source and tool commit gave different DLLs because
# (1) __FILE__ strings (asserts, the DLL's own file macros) held the absolute --src path, e.g.
# D:\c5-64bit-work\9029\src-76992d23\..., and a path of another length moves every later string in .rdata and every
# reference to it in .text; (2) the debug directory held the absolute PDB path; (3) the PE TimeDateStamp and the PDB
# GUID were the link time; (4) __DATE__/__TIME__ in the DLL source (a second build differed in "03:17:25" against
# "03:27:54"). Fixes: map the source and output folders to fixed names in every string the compiler writes; no compiler
# command line and a fixed compilation folder in the CodeView records; SOURCE_DATE_EPOCH for __DATE__/__TIME__ (clang
# honours it); /Brepro (lld-link: the PDB GUID is a hash of the PDB's contents and the timestamp a hash of the output,
# so the PDB must repeat too: /PDBSOURCEPATH replaces the linker's working folder in the PDB, and the links run in the
# output folder with relative paths, see link_path); /PDBALTPATH:%_PDB% (only the PDB's file name in the debug
# directory).
PDB_SOURCE_PATH = 'C:\\vpbuild'
REPRO_LINK = ['/Brepro', '/PDBALTPATH:%_PDB%', '/PDBSOURCEPATH:' + PDB_SOURCE_PATH]


# Issue 0358 (2026-11-15): the whole-program link's code for CvPlayer::doInstantGreatPersonProgress depends on the
# linker process's ENVIRONMENT, not only on its inputs. Relinks of one build's byte-equal inputs in one folder gave the
# DLL 125E2E2A... with the build's environment and DCE69943... with one extra 53-character variable (the two DLLs of
# issue 4252's pair). The build's environment is the caller's (a Claude session's ids and tokens, PWD, the gate's job
# id, ...) plus vcvars, so it differs between sessions and runs. So every DLL link runs with LINK_ENV_KEYS only, taken
# from the vcvars environment, and a fixed PATH (research/06-tooling-and-workflows/x64-lto-link-repeatability.md).
LINK_ENV_KEYS = ('LIB', 'SYSTEMROOT', 'TEMP', 'TMP')

# Issue 0373 (2026-11-15): the minimal environment did not make the release link repeatable. LTO's dead store
# elimination (DSEPass, LLVM 19.1.7) kept or dropped one dead store in doInstantGreatPersonProgress by chance with
# byte-equal inputs. The setting below makes it decide the same way every time; it is given to every whole-program
# (release) link and to no other configuration (research/06-tooling-and-workflows/x64-lto-link-repeatability.md
# sections 0 and 5).
LTO_DETERMINISM_LINK = ['/mllvm:-dse-memoryssa-scanlimit=1000']


def link_env(env: dict) -> dict:
    """The fixed, minimal environment of every lld-link run of the DLL (issue 0358): LIB (the default libraries'
    folders), SYSTEMROOT, TEMP and TMP from `env` (looked up without regard to case), PATH = our LLVM folder and
    system32. Nothing else of the caller's environment reaches the linker. LINK and _LINK_ (lld-link reads extra
    options from them) are never passed."""
    up = {k.upper(): v for k, v in env.items()}
    sysroot = up.get('SYSTEMROOT') or up.get('WINDIR') or 'C:\\WINDOWS'
    e = {'LIB': up.get('LIB', ''), 'SYSTEMROOT': sysroot, 'TEMP': up.get('TEMP', ''), 'TMP': up.get('TMP', '')}
    e['PATH'] = str(LLVM_BIN) + os.pathsep + sysroot + '\\system32'
    return e


def link_env_record(e: dict) -> dict:
    """What build-summary.json records of the link environment: its names, size and SHA-256 (two links of one
    build are comparable by it; values are not secrets but long)."""
    blob = '\0'.join(f'{k}={e[k]}' for k in sorted(e)).encode('utf-8')
    return {'names': sorted(e), 'chars': len(blob), 'sha256': hashlib.sha256(blob).hexdigest()}


def repro_cl_args(*maps) -> list:
    """clang-cl flags that replace each (folder, name) prefix in __FILE__ and debug strings (longest folder first),
    keep the command line out of the CodeView build-info records and give them a fixed compilation folder.
    /Brepro (issue 0131): clang-cl writes the compile time into every native object's COFF header TimeDateStamp
    otherwise; the closed-library stubs are such objects, and with stubs of another time stamp the DLL's LTO link
    gave other code (upstream-fix-batch-1.md 8c). LTO bitcode objects have no such field."""
    pairs = sorted(((str(Path(p)), name) for p, name in maps if p), key=lambda t: -len(t[0]))
    return ['/clang:-ffile-prefix-map=%s=%s' % (p, name) for p, name in pairs] + \
        ['/clang:-gno-codeview-command-line', '/clang:-fdebug-compilation-dir=VPBUILD', '/Brepro']


def source_date_epoch() -> str:
    """SOURCE_DATE_EPOCH for __DATE__/__TIME__: the caller's, else the commit time of this repository's HEAD (the tool
    commit), else 0."""
    v = os.environ.get('SOURCE_DATE_EPOCH', '').strip()
    if v.isdigit():
        return v
    try:
        cp = subprocess.run(['git', '-C', str(REPO), 'log', '-1', '--format=%ct', 'HEAD'], capture_output=True, text=True)
        if cp.returncode == 0 and cp.stdout.strip().isdigit():
            return cp.stdout.strip()
    except OSError:
        pass
    return '0'


def repro_env(env: dict) -> dict:
    """A copy of a compiler environment with SOURCE_DATE_EPOCH set (issue 0115)."""
    e = dict(env)
    e['SOURCE_DATE_EPOCH'] = source_date_epoch()
    return e


def source_cwd(source, *roots):
    """(working folder, source path to pass) for one compile: the source relative to the first root that holds it.
    clang names a file's anonymous namespace by a hash of the main file's name as given (MS ABI `?A0x<hash>`, in RTTI
    names and in the PDB), so an absolute source path makes types differ between two checkouts (issue 0115)."""
    s = Path(source)
    for r in roots:
        if r is None:
            continue
        try:
            return str(Path(r)), str(s.relative_to(Path(r)))
        except ValueError:
            continue
    return None, str(s)


def objname_args(obj, base) -> list:
    """The object's own name in its CodeView S_OBJNAME record, relative to `base` (clang writes the absolute /Fo path
    otherwise, and the PDB carries it: issue 0115)."""
    return ['-Xclang', '-object-file-name=' + link_path(obj, base)]


def link_path(p, cwd) -> str:
    """`p` relative to the link's working folder when both are on one drive (the PDB records the paths the linker was
    given; relative ones are made absolute against PDB_SOURCE_PATH), else unchanged."""
    try:
        return os.path.relpath(str(p), str(cwd))
    except ValueError:
        return str(p)


# Marker defines (ticket 0573, generalised by ticket 0574 with lead-0504, MC #6806/#6824): a product feature committed
# behind a define in the fork carries a marker header in CvGameCoreDLL_Expansion2; a source with the marker gets the
# feature's defines, in this order, and every other source's compile line stays exactly what it was (pins keep their
# hashes). Never the PREDEFS line. VP64_MARKER_DEFINES=0 in the environment turns them all off (define-off and as-pin
# builds: perf/speedkit/defineoff.py and build_as_pin.py set it). The defines given are named in build-summary.json.
MARKER_DEFINES = (
    ('VP64HiddenEmbark.h', ('VP64_HIDDEN_EMBARK',)),                          # ticket 0573 (hidden embarks)
    ('VP64SpeedDanger.h', ('VP64_SPEED_DANGER', 'VP64_SPEED_DANGER_DG7')),    # epic 0504's danger package
)
MARKER_OPT_OUT_ENV = 'VP64_MARKER_DEFINES'


def marker_opt_out() -> bool:
    """True when VP64_MARKER_DEFINES=0 (no marker define at all)."""
    return os.environ.get(MARKER_OPT_OUT_ENV, '').strip() == '0'


def marker_defines(src: Path) -> list:
    """The marker defines a source gets (MARKER_DEFINES order), [] under the opt-out or without markers."""
    if marker_opt_out():
        return []
    out = []
    for header, defines in MARKER_DEFINES:
        if (Path(src) / 'CvGameCoreDLL_Expansion2' / header).is_file():
            out += list(defines)
    return out


def marker_record(src: Path) -> dict:
    """build-summary.json's record of the marker defines: the defines given, the markers found, the opt-out."""
    found = [h for h, _ in MARKER_DEFINES if (Path(src) / 'CvGameCoreDLL_Expansion2' / h).is_file()]
    return {'marker_defines': marker_defines(src), 'markers_found': found, 'marker_defines_opt_out': marker_opt_out()}


def render_events_state(src: Path, wanted: bool = True) -> str:
    """Ticket 4932 (epic 0454 X2): 'on' when the DLL compile gets /DVP64_RENDER_EVENTS (a source with the feature's
    header, not switched off), 'off' (the header is there, --no-render-events), 'absent' (no header: every pin before
    the fork branch x64-render-events, whose command line stays as before and keeps its hashes)."""
    if not (Path(src) / RENDER_EVENTS_HEADER).is_file():
        return 'absent'
    return 'on' if wanted else 'off'


def pgo_cl_args(config: str, profile_use=None) -> list:
    """Ticket 4801: the PGO flags appended to a compile line. pgo-instr: IR instrumentation with no output name compiled
    in (LLVM_PROFILE_FILE names it at run time; a folder here would be an absolute path inside the DLL). profile_use (a
    release-pgo build's profile copy, ticket 0513; 4801's `release --profile`): the profile, and
    -pgo-warn-missing-function so that functions the profile does not know are counted (diagnostics only, no code
    change). Empty for every other build: release's line is unchanged."""
    if config == PGO_INSTR:
        return ['/clang:-fprofile-generate']
    if profile_use is not None:
        return [f'/clang:-fprofile-use={profile_use}', '-mllvm', '-pgo-warn-missing-function']
    return []


def cl_args(src: Path, config: str, shorten: bool, overlay: Path = None, major_define: str = None,
            render_events: bool = True, profile_use=None) -> list:
    """The clang-cl flags of every DLL compile. major_define: resolve_major_civs()'s /D flag, appended after the
    configuration's defines (None for 22: the line is then exactly the one before issue 4252). render_events: False
    leaves VP64_RENDER_EVENTS out for a source that has its header (ticket 4932's identity test, --no-render-events).
    profile_use: the profile copy of a release-pgo build (ticket 0513; required there, refused everywhere else: 4801's
    `release --profile` is now `release-pgo`); None leaves the line as before."""
    if profile_use is not None and config != RELEASE_PGO:
        raise ValueError(f'a profile is consumed by the {RELEASE_PGO} configuration only, not {config}')
    if config == RELEASE_PGO and profile_use is None:
        raise ValueError(f'the {RELEASE_PGO} configuration needs a profile')
    args = ['-m64', '-msse3', '/c', '/MD', '/GS', '/EHsc', '/fp:precise', '/Zc:wchar_t', '/Zi', '/FS',
            '/std:c++14', '-ferror-limit=0', '-fdiagnostics-absolute-paths']
    args += repro_cl_args((src, 'VPSRC'), (overlay.parent if overlay else None, 'VPOUT'))
    if overlay:
        args += ['-vfsoverlay', str(overlay)]
    if config in WHOLE_PROGRAM:   # release, pgo-instr, release-pgo
        args += ['/Ox', '/Ob2', '/Zo', '-flto']
    elif config == 'fast':  # ticket 0159: release without whole-program optimisation
        args += ['/Ox', '/Ob2', '/Zo']
    else:
        args += ['/Od', '/Oy-']
    args += [f'/D{d}' for d in PREDEFS[config]]
    if major_define:
        args.append(major_define)
    args += [f'/D{d}' for d in marker_defines(src)]   # tickets 0573, 0574: only a source with the feature's marker
    if render_events_state(src, render_events) == 'on':   # ticket 4932: the same rule for the render messages
        args.append(f'/D{RENDER_EVENTS_DEFINE}')
    args += [f'/I{src / d}' for d in INCLUDE_DIRS]
    args += [f'-Wno-{w}' for w in CL_SUPPRESS]
    if shorten:
        args += [SHORTEN_FLAG] + PHYSICAL_LOC_ARGS
    return args + pgo_cl_args(config, profile_use)


def obj_path(out: Path, cpp: str) -> Path:
    return out / 'obj' / Path(cpp).with_suffix('.obj')


def compile_one(env, base, src: Path, out: Path, cpp: str, pch_mode: str):
    obj = obj_path(out, cpp)
    obj.parent.mkdir(parents=True, exist_ok=True)
    # issue 0115: compiled in the source folder with the relative name (anonymous-namespace hashes, __FILE__)
    cmd = [str(CLANG_CL)] + base + objname_args(obj, out) + [cpp, f'/Fo{obj}', f'{pch_mode}{PCH_H}',
                                                         f'/Fp{out / PCH}']
    t0 = time.perf_counter()
    cp = subprocess.run(cmd, capture_output=True, text=True, errors='replace', env=env, cwd=str(src))
    return cpp, cp.returncode, cp.stdout + cp.stderr, time.perf_counter() - t0, cmd


def summarize_diags(outputs: dict, out: Path, log):
    """Count compiler diagnostics per flag: occurrences (over all TUs) and unique file:line:col sites."""
    occ = collections.Counter()
    sites = collections.defaultdict(set)
    shorten_sites = set()  # (file, line, col, msg)
    errors = []
    for cpp, text in outputs.items():
        for line in text.splitlines():
            m = DIAG_RE.match(line)
            if not m:
                continue
            if m['kind'] != 'warning':
                errors.append(line)
                continue
            flag = m['flag'] or '(no flag)'
            occ[flag] += 1
            sites[flag].add(f"{os.path.normpath(m['file'])}({m['line']},{m['col']}): {m['msg']}")
            if flag == SHORTEN_FLAG:
                shorten_sites.add((os.path.normpath(m['file']), int(m['line']), int(m['col']), m['msg']))
    with open(out / 'warnings.txt', 'w', encoding='utf-8', newline='\n') as f:
        for flag in sorted(sites):
            f.write(f'## {flag} ({occ[flag]} occurrences, {len(sites[flag])} unique sites)\n')
            for s in sorted(sites[flag]):
                f.write(s + '\n')
            f.write('\n')
    log('Warnings per category (occurrences over all TUs / unique sites):')
    if not occ:
        log('  none')
    for flag, n in occ.most_common():
        log(f'  {flag}: {n} / {len(sites[flag])}')
    log(f'Errors: {len(errors)}')
    return ({flag: {'occurrences': n, 'unique_sites': len(sites[flag])} for flag, n in occ.items()}, len(errors),
            shorten_sites)


# ---- narrowing gate (issue 1405) ----
# A site is keyed by file (relative to --src, '/'), column, warning text (which names both types) and the source
# line's text (whitespace collapsed): line numbers are not part of the key, so edits elsewhere in a file do not
# count as new sites. Keys are a multiset: the same statement twice in one file counts twice.
BASELINE_HEADER = 'config\tcount\tfile\tcol\tmessage\tcode\tlines'


class NarrowingSiteError(ValueError):
    """A -Wshorten-64-to-32 site whose source line cannot be read (ticket 4810: no silent empty key)."""


def narrowing_keys(sites: set, src: Path) -> dict:
    """{key: [line, ...]} for the unique -Wshorten-64-to-32 sites. The sites name physical lines (PHYSICAL_LOC_ARGS,
    ticket 4810); a site whose file cannot be read or whose line is past the file's end raises NarrowingSiteError
    (before 4810 it was keyed with an empty code text)."""
    cache = {}
    res = collections.defaultdict(list)
    bad = []
    for file, line, col, msg in sites:
        p = Path(file)
        if not p.is_absolute():  # never with -fdiagnostics-absolute-paths; clang's cwd is --src
            p = Path(src) / p
        try:
            rel = p.resolve().relative_to(src).as_posix()
        except ValueError:
            rel = '<outside-src>/' + p.name
        if p not in cache:
            try:
                cache[p] = p.read_text(encoding='utf-8', errors='replace').splitlines()
            except OSError:
                cache[p] = None
        lines = cache[p]
        if lines is None or not 0 < line <= len(lines):
            bad.append(f'{file}({line},{col}): ' + ('file not readable' if lines is None else
                                                     f'line past the end ({len(lines)} lines)'))
            continue
        code = ' '.join(lines[line - 1].split())
        res[(rel, col, msg.replace('\t', ' '), code.replace('\t', ' '))].append(line)
    if bad:
        raise NarrowingSiteError(f'{len(bad)} {SHORTEN_FLAG} site(s) name a source line that cannot be read (a '
                                 f"'#line' remap the compiler did not undo?): " + '; '.join(sorted(bad)[:10]))
    return res


LINE_PROBE_SOURCE = ('/* Generated by tools/x64-dll/build.py (ticket 4810): the narrowing gate needs physical lines. */\n'
                     'typedef unsigned long long vp_u64;\n'
                     'int vp_line_probe(vp_u64 a)\n'
                     '{\n'
                     '#line 1000 "vp-line-probe-logical.c"\n'
                     '    return a;\n'
                     '}\n')
LINE_PROBE_LINE = 6  # the physical line of `return a;` above


def check_physical_lines(env, gen: Path):
    """None when this clang-cl, given the gate's flags, names the physical line of a site after a '#line' directive;
    else the reason (the gate would key sites by the wrong source text). Compiles LINE_PROBE_SOURCE in <gen>."""
    gen.mkdir(parents=True, exist_ok=True)
    (gen / 'vp_line_probe.c').write_text(LINE_PROBE_SOURCE, encoding='utf-8', newline='\n')
    cmd = ([str(CLANG_CL), '-m64', '/c', '/GS-', '-fdiagnostics-absolute-paths', SHORTEN_FLAG] + PHYSICAL_LOC_ARGS
           + ['vp_line_probe.c', '/Fovp_line_probe.obj'])
    cp = subprocess.run(cmd, capture_output=True, text=True, errors='replace', env=env, cwd=str(gen))
    text = cp.stdout + cp.stderr
    got = [(Path(m['file']).name, int(m['line'])) for m in map(DIAG_RE.match, text.splitlines())
           if m and m['flag'] == SHORTEN_FLAG]
    if cp.returncode != 0 or got != [('vp_line_probe.c', LINE_PROBE_LINE)]:
        return (f'line probe: expected one {SHORTEN_FLAG} site at vp_line_probe.c line {LINE_PROBE_LINE}, got {got} '
                f'(clang-cl rc={cp.returncode}): {text.strip()[:400]}')
    return None


def read_baseline(path: Path) -> dict:
    """{config: {key: count}}"""
    base = collections.defaultdict(dict)
    if not path.exists():
        return base
    for raw in path.read_text(encoding='utf-8').splitlines():
        if not raw or raw.startswith('#') or raw == BASELINE_HEADER:
            continue
        f = raw.split('\t')
        base[f[0]][(f[2], int(f[3]), f[4], f[5])] = int(f[1])
    return base


def write_baseline(path: Path, config: str, keys: dict, info: dict, log):
    base = read_baseline(path)
    base[config] = {k: len(v) for k, v in keys.items()}
    lines_of = {config: {k: v for k, v in keys.items()}}
    old = {}
    if path.exists():  # keep the informative line numbers of the other configs
        for raw in path.read_text(encoding='utf-8').splitlines():
            f = raw.split('\t')
            if len(f) == 7 and f[0] != 'config':
                old[(f[0], f[2], int(f[3]), f[4], f[5])] = f[6]
    stamp = time.strftime('%Y-%m-%d %H:%M:%S %z')
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# -Wshorten-64-to-32 baseline for the narrowing gate of tools/x64-dll/build.py --shorten (issue 1405).\n')
        f.write('# Key: config, file, col, message, code (line numbers are informative only). count = sites with that key.\n')
        f.write(f'# {config}: {sum(base[config].values())} unique sites, written {stamp} from {info["mode"]} '
                f'{info["head"][:9]} ({info["describe"]}).\n')
        for c in sorted(base):
            if c != config:
                f.write(f'# {c}: {sum(base[c].values())} unique sites (kept from the previous file).\n')
        f.write(BASELINE_HEADER + '\n')
        for c in sorted(base):
            for k in sorted(base[c]):
                ln = (','.join(str(x) for x in sorted(lines_of[c][k])) if c in lines_of
                      else old.get((c,) + k, ''))
                f.write(f'{c}\t{base[c][k]}\t{k[0]}\t{k[1]}\t{k[2]}\t{k[3]}\t{ln}\n')
    log(f'Narrowing baseline written: {path} ({config}: {sum(base[config].values())} unique sites)')


def narrowing_gate(path: Path, config: str, keys: dict, out: Path, log):
    """Returns (ok, n_new, n_gone). Writes <out>/narrowing-new.txt listing the sites not in the baseline."""
    base = read_baseline(path).get(config)
    total = sum(len(v) for v in keys.values())
    if not base:
        log(f'Narrowing gate: no "{config}" rows in {path}; run with --write-narrowing-baseline first')
        return False, total, 0
    new, gone = [], []
    for k, lines in keys.items():
        extra = len(lines) - base.get(k, 0)
        if extra > 0:
            new.append((k, sorted(lines), extra))
    for k, n in base.items():
        missing = n - len(keys.get(k, []))
        if missing > 0:
            gone.append((k, missing))
    n_new, n_gone = sum(e for _, _, e in new), sum(m for _, m in gone)
    with open(out / 'narrowing-new.txt', 'w', encoding='utf-8', newline='\n') as f:
        f.write(f'# narrowing gate, config {config}: {total} unique sites now, baseline {sum(base.values())}, '
                f'{n_new} new, {n_gone} gone\n')
        for (rel, col, msg, code), lines, extra in sorted(new):
            f.write(f'NEW\t{rel}:{",".join(map(str, lines))}:{col}\t{msg}\t{code}\t(+{extra})\n')
        for (rel, col, msg, code), m in sorted(gone):
            f.write(f'GONE\t{rel}:{col}\t{msg}\t{code}\t(-{m})\n')
    log(f'Narrowing gate ({config}): {total} unique {SHORTEN_FLAG} sites, baseline {sum(base.values())}, '
        f'{n_new} new, {n_gone} gone; {out / "narrowing-new.txt"}')
    for (rel, col, msg, code), lines, extra in sorted(new):
        log(f'  NEW {rel}:{",".join(map(str, lines))}:{col}: {msg} | {code}')
    if gone and not new:
        log('  (sites gone: the baseline can be tightened with --write-narrowing-baseline)')
    return not new, n_new, n_gone


def parse_undefined(text: str):
    """[[symbol, refcount, first_referencer]] in lld's report order. first_referencer is
    "<obj>:(<function>)", followed by " @ <source>:<line>" when lld had debug info to print it."""
    res = []
    first = False
    for line in text.splitlines():
        m = UNDEF_RE.match(line)
        if m:
            res.append([m['sym'], 0, ''])
            continue
        if not res:
            continue
        m = REF_RE.match(line)
        if m:
            if m['by'] is not None:
                res[-1][1] += 1
                first = not res[-1][2]
                if first:
                    res[-1][2] = m['by']
            else:
                res[-1][1] += int(m['more'])
            continue
        m = REF_CONT_RE.match(line)
        if m and first:  # the "by" line was the source location; this one is the object and function
            res[-1][2] = f"{m['at']} @ {res[-1][2]}"
            first = False
    return res


def obj_and_source(by: str) -> str:
    """The part of a first_referencer that does not depend on name demangling. Only the object's file name:
    under LTO the object is `<output dll>.lto.obj`, whose directory differs between the two links."""
    obj_fn, _, at = by.partition(' @ ')
    return Path(obj_fn.split(':(')[0]).name + ' @ ' + at


def group_of(sym: str) -> str:
    for name, rx in GROUP_RULES:
        if re.search(rx, sym):
            return name
    return 'other'


def write_census(out: Path, undef: list, mangled: list, log):
    """unresolved.txt (sorted, unique, demangled as lld prints them), unresolved-grouped.txt, unresolved.tsv."""
    names = sorted({u[0] for u in undef})
    (out / 'unresolved.txt').write_text(''.join(n + '\n' for n in names), encoding='utf-8', newline='\n')
    groups = collections.defaultdict(list)
    for n in names:
        groups[group_of(n)].append(n)
    with open(out / 'unresolved-grouped.txt', 'w', encoding='utf-8', newline='\n') as f:
        for g in sorted(groups):
            f.write(f'## {g} ({len(groups[g])})\n')
            for n in groups[g]:
                f.write(n + '\n')
            f.write('\n')
    with open(out / 'unresolved.tsv', 'w', encoding='utf-8', newline='\n') as f:
        f.write('group\trefs\tmangled\tdemangled\tfirst_referenced_by\n')
        rows = []
        for i, (sym, refs, by) in enumerate(undef):
            mg = mangled[i][0] if mangled else ''
            rows.append((group_of(sym), sym, refs, mg, by))
        for g, sym, refs, mg, by in sorted(rows):
            by = by.replace(str(out) + '\\', '')  # object paths relative to --out
            f.write(f'{g}\t{refs}\t{mg}\t{sym}\t{by}\n')
    counts = {g: len(v) for g, v in groups.items()}
    log(f'Unresolved symbols: {len(names)}')
    for g, n in sorted(counts.items(), key=lambda kv: -kv[1]):
        log(f'  {g}: {n}')
    if counts.get('other'):
        log('  WARNING: symbols outside every group; see unresolved-grouped.txt "## other"')
    return len(names), counts


# ---- the configuration marker (ticket 0159) ----
# A DLL of the `fast` configuration carries a section CONFIG_SECTION whose bytes start with CONFIG_MARKER + the
# configuration's name. It is data only (read-only, never referenced by code), kept by /INCLUDE under /OPT:REF, and it
# travels with the DLL when the file is copied or renamed: the timing tools read it (timing_refusal). Release and debug
# DLLs have no such section, so their bytes are unchanged by this ticket.
CONFIG_SECTION = '.vpcfg'
CONFIG_MARKER = b'VP-BUILD-CONFIG='
CONFIG_SYMBOL = 'vp_build_config'
MARKED_CONFIGS = ('fast', PGO_INSTR)  # ticket 4801 added pgo-instr
MARKER_NOTE = {'fast': 'not for timing', PGO_INSTR: 'instrumented: not for timing, pins or shipping'}


def marker_source(config: str) -> str:
    return ('/* Generated by tools/x64-dll/build.py (ticket 0159): marks a DLL of the "%s" configuration. Not for\n'
            '   timing: the timing tools refuse a DLL with this section. */\n'
            '#pragma section("%s", read)\n'
            '__declspec(allocate("%s")) const char %s[%d] = "%s%s; %s (tools/x64-dll/build.py)";\n'
            % (config, CONFIG_SECTION, CONFIG_SECTION, CONFIG_SYMBOL, 64 if config == 'fast' else 96,
               CONFIG_MARKER.decode(), config, MARKER_NOTE.get(config, 'not for timing')))


def _compile_gen_c(env, gen: Path, name: str, text: str, extra=()) -> Path:
    """<gen>/<name>.c written from `text` (or copied in when `text` is a Path) and compiled to <gen>/<name>.obj with
    relative names and /Brepro (issue 0131: repeats across folders). RuntimeError if clang-cl fails."""
    gen.mkdir(parents=True, exist_ok=True)
    body = text.read_text(encoding='utf-8') if isinstance(text, Path) else text
    (gen / f'{name}.c').write_text(body, encoding='utf-8', newline='\n')
    cmd = [str(CLANG_CL), '-m64', '/c', '/Brepro', '/GS-', '/clang:-gno-codeview-command-line',
           '/clang:-fdebug-compilation-dir=VPBUILD'] + list(extra) + [f'{name}.c', f'/Fo{name}.obj']
    cp = subprocess.run(cmd, capture_output=True, text=True, errors='replace', env=env, cwd=str(gen))
    if cp.returncode != 0:
        raise RuntimeError(f'{name} object: clang-cl rc={cp.returncode}: {(cp.stdout + cp.stderr).strip()}')
    return gen / f'{name}.obj'


def compile_marker(env, out: Path, config: str):
    """<out>/gen/vpconfig.obj for a marked configuration (None otherwise); raises RuntimeError if clang-cl fails.
    Compiled in <out>/gen with relative names and /Brepro, so it repeats across folders (issue 0131)."""
    if config not in MARKED_CONFIGS:
        return None
    return _compile_gen_c(env, out / 'gen', 'vpconfig', marker_source(config), ['/Zl'])


# ---- PGO (ticket 4801) ----
# A release DLL built from a profile carries a data section PROFILE_SECTION naming the profile's SHA-256, so its kind is
# visible in the file as a fast DLL's is (dll_profile). It is not a CONFIG_MARKER: the timing tools accept it (it is the
# measured arm of P1). A release build without --profile has no such section (its bytes are unchanged).
PROFILE_SECTION = '.vppgo'
PROFILE_MARKER = b'VP-BUILD-PROFILE='
PROFILE_SYMBOL = 'vp_build_profile'
PROFDATA_MAGIC = b'\xfflprofi\x81'  # IndexedInstrProf::Magic 0x8169666f72706cff, little-endian (llvm-profdata merge)


def profile_marker_source(sha256: str) -> str:
    return ('/* Generated by tools/x64-dll/build.py (ticket 4801): a release DLL built from the PGO profile below. */\n'
            '#pragma section("%s", read)\n'
            '__declspec(allocate("%s")) const char %s[96] = "%s%s (tools/x64-dll/build.py --profile)";\n'
            % (PROFILE_SECTION, PROFILE_SECTION, PROFILE_SYMBOL, PROFILE_MARKER.decode(), sha256.lower()))


def compile_pgo_objects(env, out: Path, config: str, profile_sha=None) -> list:
    """[(object, /INCLUDE symbol)] that ticket 4801 adds to a link: the flush object for pgo-instr, the profile marker
    for a release build with a profile; [] otherwise (release and the others link as before)."""
    gen = out / 'gen'
    if config == PGO_INSTR:
        # /MD: the object calls kernel32 and the CRT start-up runs its .CRT$XCU initialiser (pgo_flush.c)
        return [(_compile_gen_c(env, gen, 'vppgo_flush', PGO_FLUSH_C, ['/MD', '/O2']), PGO_FLUSH_SYMBOL)]
    if profile_sha:
        return [(_compile_gen_c(env, gen, 'vppgo', profile_marker_source(profile_sha), ['/Zl']), PROFILE_SYMBOL)]
    return []


def dll_profile(dll) -> str | None:
    """The profile SHA-256 a DLL's PROFILE_SECTION names, or None (no section: not built from a profile)."""
    try:
        data = pe_sections(dll, PROFILE_SECTION).get(PROFILE_SECTION)
    except OSError:
        return None
    if not data or not data.startswith(PROFILE_MARKER):
        return None
    return data[len(PROFILE_MARKER):].split(b' ')[0].split(b'\0')[0].decode('latin-1').strip() or None


def file_sha256(path) -> str:
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


# The compiler's PGO diagnostics (LLVM 19 IR PGO, research/08-performance/speedup-p1-pgo.md section 4): counted per
# kind over all TUs into build-summary.json `pgo_diagnostics`.
PGO_DIAG_PATTERNS = {
    'hash_mismatch': re.compile(r'function control flow change detected \(hash mismatch\)', re.I),
    'missing_function': re.compile(r'[Nn]o profile data available for function'),
    'malformed': re.compile(r'(?:profile data may be corrupt|[Mm]alformed|[Ii]nconsistent profile)'),
    'out_of_date': re.compile(r'profile data may be out of date'),
    'unprofiled_file': re.compile(r'no profile data available for file'),
}


def pgo_diagnostics(outputs: dict) -> dict:
    """{kind: count} of PGO_DIAG_PATTERNS over the compile outputs, plus `tus_with_any`."""
    counts = {k: 0 for k in PGO_DIAG_PATTERNS}
    tus = 0
    for text in outputs.values():
        hit = False
        for line in text.splitlines():
            for k, rx in PGO_DIAG_PATTERNS.items():
                if rx.search(line):
                    counts[k] += 1
                    hit = True
        tus += hit
    counts['tus_with_any'] = tus
    return counts


# ---- the locked profile and the retrain rule (ticket 0513, D184) ----
# deps/dll.lock carries the profile of the release-pgo configuration as a build input:
#   profile      = sha256 <64 hex> bytes <n> pin <40 hex> name <file name>
#                  <continuation lines: training set, tool versions, the instrumented DLL, who trained it when>
#   profile-file = <where the file lives on the build machine; the file is never committed (MC #4429 (2))>; written
#                  `<pgo_dir>/<file name>` (ticket 0774: the lock is exported, so no machine path; setting pgo_dir)
_LOCK_PROFILE_RE = re.compile(r'^sha256\s+(?P<sha256>[0-9a-fA-F]{64})\s+bytes\s+(?P<bytes>\d+)\s+pin\s+'
                              r'(?P<pin>[0-9a-fA-F]{7,40})\s+name\s+(?P<name>\S+)\s*$')


def lock_profile(lock: dict = None) -> dict | None:
    """deps/dll.lock's locked profile: {sha256 (lower case), bytes, pin, name, file (profile-file or None), text (the
    whole value)}, or None when the lock has no `profile` line. ValueError when the line is there but not of the form
    above (a hand-edited lock must not turn into "no profile" silently)."""
    lock = read_dll_lock() if lock is None else lock
    text = lock.get('profile')
    if not text:
        return None
    m = _LOCK_PROFILE_RE.match(text.splitlines()[0].strip())
    if not m:
        raise ValueError(f'deps/dll.lock profile line {text.splitlines()[0]!r}: not "sha256 <64 hex> bytes <n> pin '
                         '<commit> name <file>"')
    line = (lock.get('profile-file') or '').strip() or None
    file, problem = resolve_profile_file(line)
    return {'sha256': m['sha256'].lower(), 'bytes': int(m['bytes']), 'pin': m['pin'].lower(), 'name': m['name'],
            'file': file, 'file_line': line, 'file_problem': problem, 'text': text}


def _settings():
    import importlib.util
    spec = importlib.util.spec_from_file_location('c5_settings', REPO / 'tools' / 'release' / 'settings.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def resolve_profile_file(line: str | None) -> tuple:
    """Ticket 0774: deps/dll.lock is exported, so `profile-file` names the machine folder through a setting
    (`<pgo_dir>/<name>`, tools/release/settings.py) or an environment variable (`%VAR%`), never by its path.
    Returns (the resolved path as a string or None, None or the one-line problem: the setting is not set)."""
    if not line:
        return None, None
    if not line.startswith('<') and '%' not in line and '$' not in line:
        return line, None   # a plain path (a test's or a measurement's lock), as written
    s = _settings()
    try:
        return str(s.resolve_placeholder(line, 'deps/dll.lock profile-file')), None
    except s.SettingError as e:
        return None, f'deps/dll.lock profile-file {line}: {e}'


PROFDATA = LLVM_BIN / 'llvm-profdata.exe'
_TOTAL_FUNCTIONS_RE = re.compile(r'^\s*Total functions:\s*(\d+)\s*$', re.M)


def profile_functions(path, profdata=PROFDATA) -> int | None:
    """The number of functions in a merged profile (`llvm-profdata show`: "Total functions: N"), or None."""
    try:
        cp = subprocess.run([str(profdata), 'show', str(path)], capture_output=True, text=True, errors='replace')
    except OSError:
        return None
    m = _TOTAL_FUNCTIONS_RE.search(cp.stdout)
    return int(m.group(1)) if cp.returncode == 0 and m else None


def same_commit(a, b) -> bool:
    """Two commit ids (full or abbreviated, at least 7 hex digits each) name the same commit: one is a prefix of the
    other."""
    a, b = str(a).strip().lower(), str(b).strip().lower()
    n = min(len(a), len(b))
    return n >= 7 and a[:n] == b[:n]


def retrain_verdict(diag: dict, functions, profile_pin=None, src_commit=None, share=PGO_RETRAIN_SHARE) -> dict:
    """The retrain rule of a profile-consuming build (ticket 0513): {needed, reasons, unmatched, functions,
    unmatched_share, threshold, profile_pin, src_commit}. needed when the hash mismatches plus the functions missing
    from the profile exceed `share` of the profile's functions, when the compiler called the profile malformed, or when
    the profile was trained on another commit than the source's (retrain before each release). An unknown function
    count is a reason too (the share cannot be checked)."""
    unmatched = int(diag.get('hash_mismatch', 0)) + int(diag.get('missing_function', 0))
    reasons = []
    frac = None
    if not functions:
        reasons.append('the profile\'s function count is unknown (llvm-profdata show), so the unmatched share cannot '
                       'be checked')
    else:
        frac = unmatched / functions
        if frac > share:
            reasons.append(f'{unmatched} of {functions} functions unmatched ({100 * frac:.2f} %), over '
                           f'{100 * share:g} %')
    if int(diag.get('malformed', 0)):
        reasons.append(f'{diag["malformed"]} malformed-profile diagnostics')
    if profile_pin and src_commit and not same_commit(profile_pin, src_commit):
        reasons.append(f'the profile was trained on {str(profile_pin)[:9]}, the source is {str(src_commit)[:9]}: '
                       'retrain before each release')
    return {'needed': bool(reasons), 'reasons': reasons, 'unmatched': unmatched, 'functions': functions,
            'unmatched_share': None if frac is None else round(frac, 6), 'threshold': share,
            'profile_pin': profile_pin, 'src_commit': src_commit}


def pe_sections(path, want=CONFIG_SECTION) -> dict:
    """{section name: raw bytes} of a PE file (only what a section header names; {} if not a PE). Only the section
    `want` is read (at most 4096 bytes); the others are named with no bytes."""
    import struct
    with open(path, 'rb') as f:
        head = f.read(4096)
        if head[:2] != b'MZ' or len(head) < 64:
            return {}
        pe = struct.unpack_from('<I', head, 0x3C)[0]
        f.seek(pe)
        hdr = f.read(24)
        if hdr[:4] != b'PE\0\0':
            return {}
        nsec, opt_size = struct.unpack_from('<H', hdr, 6)[0], struct.unpack_from('<H', hdr, 20)[0]
        f.seek(pe + 24 + opt_size)
        table = f.read(40 * nsec)
        out = {}
        for i in range(nsec):
            name = table[i * 40:i * 40 + 8].rstrip(b'\0').decode('latin-1')
            raw_size, raw_ptr = struct.unpack_from('<II', table, i * 40 + 16)
            if name == want:  # only this one is read; the others are named with no bytes
                f.seek(raw_ptr)
                out[name] = f.read(min(raw_size, 4096))
            else:
                out[name] = b''
        return out


def dll_config(dll) -> str | None:
    """The configuration a DLL's marker section names ('fast'), or None (no marker: a release or debug build, or not
    built by this script)."""
    try:
        data = pe_sections(dll).get(CONFIG_SECTION)
    except OSError:
        return None
    if not data or not data.startswith(CONFIG_MARKER):
        return None
    return data[len(CONFIG_MARKER):].split(b';')[0].split(b'\0')[0].decode('latin-1').strip() or None


def timing_refusal(path) -> str | None:
    """Why the DLL at `path` (a DLL file, or the folder that holds CvGameCore_Expansion2.dll) must not be timed, or
    None. Ticket 0159: a DLL of the `fast` configuration never passes for release. Three signs, any one refuses:
    the DLL's marker section; a build record beside it (build.py's build-summary.json, build_server.py's build.json)
    whose config is not release; a folder name of the DLL's two parent levels ending in `-fast` (the layout name)."""
    p = Path(path)
    dll = p / f'{CORE_DLL}.dll' if p.is_dir() else p
    cfg = dll_config(dll) if dll.is_file() else None
    if cfg:
        return f'{dll} is a DLL of the "{cfg}" configuration (marker section {CONFIG_SECTION}): not for timing'
    for rec in ('build-summary.json', 'build.json'):
        f = dll.parent / rec
        try:
            c = json.loads(f.read_text(encoding='utf-8')).get('config') if f.is_file() else None
        except (OSError, ValueError, AttributeError):
            c = None
        if c in MARKED_CONFIGS:
            return f'{f} records config "{c}": not for timing'
    for d in (dll.parent, dll.parent.parent):
        for c in MARKED_CONFIGS:
            if d.name.lower().endswith('-' + c):
                return f'{d}: a layout named "-{c}" holds a DLL of the "{c}" configuration: not for timing'
    return None


# ---- link ----
class _PMC(ctypes.Structure):  # PROCESS_MEMORY_COUNTERS
    _fields_ = [('cb', ctypes.c_ulong), ('PageFaultCount', ctypes.c_ulong), ('PeakWorkingSetSize', ctypes.c_size_t),
                ('WorkingSetSize', ctypes.c_size_t), ('QuotaPeakPagedPoolUsage', ctypes.c_size_t),
                ('QuotaPagedPoolUsage', ctypes.c_size_t), ('QuotaPeakNonPagedPoolUsage', ctypes.c_size_t),
                ('QuotaNonPagedPoolUsage', ctypes.c_size_t), ('PagefileUsage', ctypes.c_size_t),
                ('PeakPagefileUsage', ctypes.c_size_t)]


def process_peaks(proc) -> dict:
    """Peak private bytes (PeakPagefileUsage, = the peak of Task Manager's commit size) and peak working set of a
    finished subprocess.Popen, in MB, read from its still-open process handle; {} where that is not possible."""
    try:
        k32 = ctypes.WinDLL('kernel32')
        k32.K32GetProcessMemoryInfo.argtypes = [ctypes.c_void_p, ctypes.POINTER(_PMC), ctypes.c_ulong]
        m = _PMC()
        m.cb = ctypes.sizeof(m)
        if not k32.K32GetProcessMemoryInfo(int(proc._handle), ctypes.byref(m), m.cb):
            return {}
        return {'peak_private_mb': round(m.PeakPagefileUsage / 2**20, 1),
                'peak_working_set_mb': round(m.PeakWorkingSetSize / 2**20, 1)}
    except Exception:  # noqa: BLE001 - a measurement never fails the build
        return {}


def link_lines(out: Path, config: str, census: bool, stubs, objs: list, demangle: bool, dll: Path,
               deffile: Path, marker=None, pgo_objs=()) -> list:
    """pgo_objs: compile_pgo_objects()'s [(object, symbol)] (ticket 4801); empty leaves the line as before."""
    lp = lambda q: link_path(q, out)
    # ticket 0159: /LTCG for debug and release as before (their bytes stay the same); fast links without it
    lines = [f'/OUT:"{lp(dll)}"', f'/PDB:"{lp(dll.with_suffix(".pdb"))}"', f'/MAP:"{lp(dll.with_suffix(".map"))}"',
             f'/IMPLIB:"{lp(dll.with_suffix(".lib"))}"',
             '/MACHINE:X64', '/DLL', '/DEBUG'] + ([] if config == 'fast' else ['/LTCG']) + \
            ['/DYNAMICBASE', '/NXCOMPAT', '/SUBSYSTEM:WINDOWS',
             '/MANIFEST:EMBED', '/ERRORLIMIT:0', f'/DEF:"{lp(deffile)}"'] + REPRO_LINK
    if config in ('release', 'fast', PGO_INSTR, RELEASE_PGO):
        lines += ['/OPT:REF', '/OPT:ICF']
    if config in WHOLE_PROGRAM:
        lines += LTO_DETERMINISM_LINK  # issue 0373
    if config == PGO_INSTR:
        lines.append(f'/LIBPATH:"{PGO_RT_DIR}"')  # ticket 4801: the profile runtime (instrumented build only)
    if census:
        # /FORCE:MULTIPLE only for the census (as the probe); the normal link with the stubs is clean without it
        # (issue 1009, 2026-11-10: 0 duplicate-symbol reports).
        lines += ['/FORCE:UNRESOLVED', '/FORCE:MULTIPLE']
    if not demangle:
        lines.append('/DEMANGLE:NO')
    if marker is not None:
        lines.append(f'/INCLUDE:{CONFIG_SYMBOL}')
    lines += [f'/INCLUDE:{sym}' for _o, sym in pgo_objs]
    lines += [f'"{lp(o)}"' for o in objs]
    if marker is not None:
        lines.append(f'"{lp(marker)}"')
    lines += [f'"{lp(o)}"' for o, _sym in pgo_objs]
    if config == PGO_INSTR:
        lines.append(PGO_RT_LIB)
    if stubs:
        lines += [f'"{lp(p)}"' for p in sorted(Path(stubs).glob('*.lib'))]
    lines += DEFAULT_LIBS
    return lines


LINK_PLACE_EXIT = 4  # gave up waiting for the whole-program link place (the objects stay; --link-only links them)


def take_link_place(config: str, out: Path, log, wait_min=None, linkplace=None):
    """Ticket 0159 (D107): a whole-program link (config in WHOLE_PROGRAM) first takes a place of
    perf/quietlock/linkplace.py, waiting in place with progress lines naming the holder; gives up after the limit
    (exit LINK_PLACE_EXIT). Returns the Place, or None for a configuration that is not counted. linkplace: the module
    (tests pass a stand-in)."""
    if config not in WHOLE_PROGRAM:
        return None
    if linkplace is None:
        try:
            sys.path.insert(0, str(REPO / 'perf' / 'quietlock'))
            import linkplace  # noqa: E402
        except ImportError as e:
            log(f'WARNING: perf/quietlock/linkplace.py not importable ({e}); linking without a place')
            return None
    log(f'Whole-program link: taking a link place (perf/quietlock/linkplace.py; {linkplace.default_places()} '
        'place(s) on this machine)...')
    try:
        place = linkplace.acquire(f'x64 DLL {config} link (tools/x64-dll/build.py)', out=out, limit_min=wait_min,
                                  say=log)
    except linkplace.LinkPlaceTimeout as e:
        die(log, f'{e}\n  objects kept in {out / "obj"}', code=LINK_PLACE_EXIT)
    log(f'Link place {"taken" if place.taken else "not taken (no lock folder)"} after {place.waited_s:.0f} s')
    return place


def link(env, out: Path, src: Path, config: str, census: bool, stubs, objs: list, demangle: bool, dll: Path,
         marker=None, place=None, pgo_objs=()):
    """Returns (rc, output, seconds, peaks). place: a perf/quietlock/linkplace.py Place that records the linker's pid."""
    rsp = out / ('link.rsp' if demangle else 'link-mangled.rsp')
    # issue 0115: the PDB records every path the linker is given (and lld hashes the PDB into the GUID and the
    # timestamp under /Brepro), so every input is given relative to the link's folder; the .def is copied in first
    deffile = out / 'CvGameCoreDLL.def'
    deffile.write_bytes((src / DEF_FILE).read_bytes())
    lines = link_lines(out, config, census, stubs, objs, demangle, dll, deffile, marker, pgo_objs)
    rsp.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    t0 = time.perf_counter()
    # issue 0358: the fixed minimal environment (the LTO code depends on the process environment) and the response
    # file by its name in the link's folder, so that the command line carries no folder either
    proc = subprocess.Popen([str(LLD_LINK), f'@{rsp.name}'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, errors='replace', env=link_env(env), cwd=str(out))
    if place is not None:
        place.add_pid(proc.pid)
    text, _ = proc.communicate()
    dt = time.perf_counter() - t0
    return proc.returncode, text, dt, process_peaks(proc)


def clean(out: Path):
    for d in ('obj', 'mangled', 'gen'):
        shutil.rmtree(out / d, ignore_errors=True)
    for p in out.glob('*'):
        if p.is_file() and (p.name.startswith(CORE_DLL) or p.suffix in ('.pch', '.rsp', '.log', '.txt', '.tsv')
                            or p.name in ("build-summary.json", "vcvars-env.json")):
            p.unlink()


def check_pgo_args(a, lock: dict = None) -> dict | None:
    """Tickets 4801, 0513: the profile a release-pgo build consumes, {path, sha256, locked, lock (lock_profile() or
    None)}, else None; sys.exit with one line on a wrong use, before anything is built. The profile is --profile FILE,
    else deps/dll.lock's profile-file; its SHA-256 must be the lock's `profile` unless --profile-unlocked (a measurement
    or a retrain trial; recorded in build-summary.json as locked: false)."""
    if a.compile_only and a.link_only:
        sys.exit('build.py: FAILED: --compile-only and --link-only')
    if a.config != RELEASE_PGO:
        if a.profile is not None and a.config == 'release':
            sys.exit(f'build.py: FAILED: --profile with --config release: the configuration that consumes a profile is '
                     f'{RELEASE_PGO} (ticket 0513): --config {RELEASE_PGO} [--profile FILE [--profile-unlocked]]')
        if a.profile is not None:
            sys.exit(f'build.py: FAILED: --profile is for --config {RELEASE_PGO} only, not {a.config}')
        if a.profile_unlocked:
            sys.exit(f'build.py: FAILED: --profile-unlocked is for --config {RELEASE_PGO} only, not {a.config}')
        return None
    try:
        lp = lock_profile(lock)
    except ValueError as e:
        sys.exit(f'build.py: FAILED: {e}')
    path = a.profile
    if path is None:
        if lp and lp.get('file_problem'):
            sys.exit(f'build.py: FAILED: {lp["file_problem"]}')
        if not lp or not lp.get('file'):
            sys.exit(f'build.py: FAILED: --config {RELEASE_PGO} needs a profile: --profile FILE, or the profile and '
                     'profile-file lines of deps/dll.lock')
        path = Path(lp['file'])
    path = Path(path)
    if not path.is_file():
        sys.exit(f'build.py: FAILED: the profile {path}{"" if a.profile else " (deps/dll.lock profile-file)"}: no such '
                 'file')
    with open(path, 'rb') as f:
        magic = f.read(8)
    if magic != PROFDATA_MAGIC:
        sys.exit(f'build.py: FAILED: the profile {path}: not an indexed profile (llvm-profdata merge output; a '
                 f'.profraw must be merged first)')
    sha = file_sha256(path)
    locked = bool(lp) and lp['sha256'] == sha
    if not locked and not a.profile_unlocked:
        want = f'deps/dll.lock\'s profile {lp["sha256"]}' if lp else 'deps/dll.lock, which has no profile line'
        sys.exit(f'build.py: FAILED: the profile {path} has SHA-256 {sha}, not {want}: a {RELEASE_PGO} build consumes '
                 'the locked profile; --profile-unlocked builds from another one on purpose (a measurement or a '
                 'retrain trial; recorded)')
    return {'path': path, 'sha256': sha, 'locked': locked, 'lock': lp}


def main():
    ap = argparse.ArgumentParser(description='x64 build of the Vox Populi DLL (issues 1001, 1405).')
    ap.add_argument('--src', type=Path, default=None,
                    help='DLL fork source: a git checkout at the commit of deps/dll.lock, or a plain copy of that commit '
                         f'without .git. Default {DEFAULT_SRC} (from {DEFAULT_SRC_ORIGIN}; dll_source.py), used only '
                         'if it is at the locked commit')
    ap.add_argument('--unlocked', action='store_true',
                    help='allow a git --src at another commit or with tracked changes (fork development); recorded')
    ap.add_argument('--config', choices=list(CONFIGS), default='debug',
                    help='debug; release (whole-program optimisation, -flto + /LTCG: the build of pins, timing and '
                         'shipping; its link takes a place of perf/quietlock/linkplace.py); fast (release without '
                         'whole-program optimisation, ticket 0159: functional work only, never timing); pgo-instr '
                         '(release with IR profile instrumentation, ticket 4801: trains a PGO profile, never pinned, '
                         'shipped or timed; set LLVM_PROFILE_FILE when running it); release-pgo (release compiled with '
                         'the locked PGO profile of deps/dll.lock, ticket 0513, D184: whole-program, timing allowed; '
                         'the DLL\'s .vppgo section names the profile\'s SHA-256)')
    ap.add_argument('--profile', type=Path, default=None, metavar='FILE',
                    help=f'{RELEASE_PGO} only (tickets 4801, 0513): the merged PGO profile (llvm-profdata merge '
                         'output); default deps/dll.lock profile-file. Its SHA-256 must be deps/dll.lock\'s profile '
                         'line unless --profile-unlocked. build-summary.json records it, the PGO diagnostics and the '
                         'retrain rule\'s verdict (pgo_retrain)')
    ap.add_argument('--profile-unlocked', action='store_true',
                    help=f'{RELEASE_PGO}: build from a profile that is not deps/dll.lock\'s (a measurement or a retrain '
                         'trial); recorded as profile.locked false')
    ap.add_argument('--compile-only', action='store_true',
                    help='stop after the compile (and the narrowing gate); no link (ticket 4801: the PGO match counts '
                         'of a profile on another build come from the compile alone)')
    ap.add_argument('--link-place-wait', type=float, default=None, metavar='MIN',
                    help='release: give up after MIN minutes waiting for the whole-program link place (default '
                         'C5_LTO_LINK_WAIT_MIN, else 60)')
    ap.add_argument('--out', type=Path, required=True, help='directory for every output')
    ap.add_argument('--census', action='store_true',
                    help='link with /FORCE:UNRESOLVED and write unresolved.txt, unresolved-grouped.txt, unresolved.tsv')
    ap.add_argument('--stubs', type=Path, help='directory whose *.lib are linked (the closed-library stubs)')
    ap.add_argument('--shorten', action='store_true',
                    help='add -Wshorten-64-to-32 and run the narrowing gate against --narrowing-baseline')
    ap.add_argument('--narrowing-baseline', type=Path, default=NARROWING_BASELINE,
                    help='baseline of the narrowing gate (default: %(default)s)')
    ap.add_argument('--write-narrowing-baseline', action='store_true',
                    help="with --shorten: replace this config's rows of the baseline with the current sites")
    ap.add_argument('--jobs', type=int, default=os.cpu_count(), help='parallel compiles (default: all cores)')
    ap.add_argument('--clean', action='store_true', help='delete this script\'s outputs in --out first')
    ap.add_argument('--link-only', action='store_true', help='reuse the objects in --out, only link')
    ap.add_argument('--no-commit-id', action='store_true',
                    help="use the source tree's own commit_id.inc instead of generating one in <out>/gen")
    ap.add_argument('--mangled', choices=['auto', 'on', 'off'], default='auto',
                    help='census: second link with /DEMANGLE:NO that fills the mangled column of unresolved.tsv; '
                         'auto = on for debug (about 3 s), off for release (the LTO link takes about 11 min)')
    ap.add_argument('--major-civs', type=int, default=MAJOR_CIVS_DEFAULT, metavar='N',
                    help=f'major civilizations (issue 4252; default {MAJOR_CIVS_DEFAULT}, the define absent). Another '
                         f'value adds /D{MAJOR_CIVS_DEFINE}=N to the DLL compile, "+mN" to the version string and '
                         f'"major_civs" to build-summary.json. Validated: '
                         f'{", ".join(map(str, MAJOR_CIVS_VALIDATED))}; a live header line must equal N')
    ap.add_argument('--unvalidated-major-civs', action='store_true',
                    help=f'allow a --major-civs value other than {" and ".join(map(str, MAJOR_CIVS_VALIDATED))} '
                         f'(still {MAJOR_CIVS_RANGE[0]}..{MAJOR_CIVS_RANGE[1]})')
    ap.add_argument('--no-render-events', action='store_true',
                    help=f'ticket 4932: leave /D{RENDER_EVENTS_DEFINE} out for a source that has {RENDER_EVENTS_HEADER} '
                         '(the with-and-without identity test only; the product always has it)')
    a = ap.parse_args()
    pgo = check_pgo_args(a)  # tickets 4801, 0513: before anything else, so a wrong use costs nothing
    profile_sha = pgo['sha256'] if pgo else None

    try:
        srcinfo = resolve_src(a.src, a.unlocked)
    except SrcError as e:
        sys.exit(f'build.py: FAILED: {e}')
    src = srcinfo['src']
    try:
        major = resolve_major_civs(src, a.major_civs, a.unvalidated_major_civs)
    except MajorCivsError as e:
        sys.exit(f'build.py: FAILED: {e}')
    out = a.out.resolve()
    if a.write_narrowing_baseline and not a.shorten:
        sys.exit('build.py: FAILED: --write-narrowing-baseline needs --shorten')
    if out == src or src in out.parents:
        sys.exit('--out must not be inside --src')
    if a.stubs and not a.stubs.is_dir():
        sys.exit(f'--stubs {a.stubs}: not a directory')
    prev_summary = {}
    if a.link_only:
        prev = out / 'build-summary.json'
        prev_summary = json.loads(prev.read_text(encoding='utf-8')) if prev.exists() else {}
        if prev.exists() and prev_summary.get('config') != a.config:
            sys.exit(f'--link-only: the objects in {out} were built with another --config')
        prev_sha = (prev_summary.get('profile') or {}).get('sha256')
        if prev_sha and a.config != RELEASE_PGO:
            sys.exit(f'--link-only: the objects in {out} were compiled from a PGO profile; build again')
        if a.config == RELEASE_PGO and prev_sha != profile_sha:   # ticket 0513
            sys.exit(f'--link-only: the objects in {out} were compiled from the profile {prev_sha}, not {profile_sha}; '
                     'build again')
        if prev.exists() and json.loads(prev.read_text(encoding='utf-8')).get('major_civs',
                                                                           MAJOR_CIVS_DEFAULT) != major['major_civs']:
            sys.exit(f'--link-only: the objects in {out} were built with another --major-civs')
    if a.clean and not a.link_only:
        clean(out)
    out.mkdir(parents=True, exist_ok=True)
    log = Log(out / 'build.log', append=a.link_only)  # a link-only run keeps the compile record above it
    t_start = time.perf_counter()
    timings = {}
    summary = {'started': time.strftime('%Y-%m-%d %H:%M:%S %z'), 'config': a.config, 'census': a.census,
               'stubs': str(a.stubs) if a.stubs else None, 'shorten': a.shorten, 'src': str(src), 'out': str(out),
               'src_mode': srcinfo['mode'], 'src_commit': srcinfo['head'], 'lock_commit': srcinfo['lock_commit'],
               'whole_program': a.config in WHOLE_PROGRAM, 'for_timing': a.config not in MARKED_CONFIGS,
               'major_civs': major['major_civs'], 'major_civs_define': major['define'],
               'major_civs_header': major['header']}
    if a.config == PGO_INSTR:  # ticket 4801
        summary['instrumented'] = True
    profile_use = None
    if profile_sha:
        gen = out / 'gen'
        gen.mkdir(parents=True, exist_ok=True)
        profile_use = gen / 'vp64.profdata'  # a copy in the output folder: the compile reads it from there
        if not a.link_only:
            shutil.copyfile(pgo['path'], profile_use)
        if file_sha256(profile_use) != profile_sha:
            sys.exit(f'build.py: FAILED: the copy {profile_use} of the profile {pgo["path"]} differs from it')
        lp = pgo['lock'] or {}
        summary['profile'] = {'sha256': profile_sha, 'bytes': profile_use.stat().st_size, 'name': pgo['path'].name,
                              'given': str(pgo['path'].resolve()), 'from_lock_file': a.profile is None,
                              'locked': pgo['locked'], 'lock_sha256': lp.get('sha256'), 'lock_pin': lp.get('pin'),
                              'functions': profile_functions(profile_use)}
        if a.link_only:   # ticket 0513: the compile's diagnostics and verdict stay those of the objects
            for k in ('pgo_diagnostics', 'pgo_retrain'):
                if k in prev_summary:
                    summary[k] = prev_summary[k]

    log(f'== x64 DLL build, {summary["started"]}')
    log(f'command: {" ".join(sys.argv)}')
    log(f'src: {src} ({srcinfo["note"]})')
    log(f'out: {out}')
    log(f'config: {a.config}  census: {a.census}  stubs: {a.stubs}  shorten: {a.shorten}  jobs: {a.jobs}')
    log(f'major civilizations: {major["note"]}')
    if a.config in MARKED_CONFIGS:
        log(f'NOTE: configuration "{a.config}" (ticket 0159): release code paths without whole-program optimisation, '
            f'for functional work only. The DLL carries the marker section {CONFIG_SECTION}; the timing tools refuse '
            f'it. A layout holding it is named <name>-{a.config}.')
    if a.config == PGO_INSTR:
        log(f'NOTE: "{PGO_INSTR}" (ticket 4801) is instrumented: run it with LLVM_PROFILE_FILE=<folder>/<name>-%p.profraw; '
            'tools/x64-dll/pgo/pgo_flush.c writes the profile when the server ends itself with TerminateProcess. '
            'Never pinned, shipped or timed.')
    if profile_sha:
        p = summary['profile']
        log(f'PGO profile (tickets 4801, 0513): {pgo["path"]}{" (deps/dll.lock profile-file)" if p["from_lock_file"] else ""} '
            f'sha256 {profile_sha} ({p["bytes"]} B, {p["functions"]} functions), compiled from the copy {profile_use}; '
            + (f'the locked profile (trained on {str(p["lock_pin"])[:9]})' if p['locked'] else
               'NOT the locked profile (--profile-unlocked)'))

    t0 = time.perf_counter()
    env = repro_env(load_vcvars_env(log, out / "vcvars-env.json"))   # issue 0115: SOURCE_DATE_EPOCH
    timings['vcvars'] = time.perf_counter() - t0

    git = ['git', '-C', str(src)]
    is_git = srcinfo['mode'] != 'copy'
    versions = {
        'clang-cl': first_line([str(CLANG_CL), '--version']),
        'lld-link': first_line([str(LLD_LINK), '--version']),
        'MSVC (VCToolsVersion)': env.get('VCToolsVersion', '?').strip('\\'),
        'MSVC tools dir': env.get('VCToolsInstallDir', '?'),
        'Windows SDK': env.get('WindowsSDKVersion', '?').strip('\\'),
        'VS (VisualStudioVersion)': env.get('VisualStudioVersion', '?'),
        'python': sys.version.split()[0],
        'src mode': f'{srcinfo["mode"]} ({srcinfo["note"]})',
        'deps/dll.lock commit': srcinfo['lock_commit'],
        'src HEAD': first_line(git + ['rev-parse', 'HEAD']) if is_git else 'n/a (no .git)',
        'src describe': srcinfo['describe'],
        'src branch': first_line(git + ['rev-parse', '--abbrev-ref', 'HEAD']) if is_git else 'n/a (no .git)',
        'src tracked changes': (first_line(git + ['status', '--porcelain', '--untracked-files=no']) or 'none')
        if is_git else 'n/a (no .git)',
    }
    summary['versions'] = versions
    log('Tool versions:')
    for k, v in versions.items():
        log(f'  {k}: {v}')
    if srcinfo['mode'] == 'git' and srcinfo['lock_describe'] and srcinfo['describe'] != srcinfo['lock_describe']:
        log(f'WARNING: git describe {srcinfo["describe"]} != deps/dll.lock describe {srcinfo["lock_describe"]} '
            '(tags differ in this clone); the commit itself matches')

    overlay = None
    if a.no_commit_id:
        if major['tag']:
            die(log, f'--no-commit-id with --major-civs {major["major_civs"]}: the source\'s own commit_id.inc '
                     'would name the DLL as the 22 one; drop --no-commit-id')
        commit_inc = src / 'commit_id.inc'
        if not commit_inc.exists() and not a.link_only:
            die(log, f'--no-commit-id: {commit_inc} does not exist (it is untracked in the fork); drop --no-commit-id')
    else:
        overlay = write_commit_overlay(out, srcinfo, major['tag'])  # never writes into --src
        commit_inc = out / 'gen' / 'commit_id.inc'
    log(f'commit_id.inc ({commit_inc}): '
        f'{commit_inc.read_text(errors="replace").strip() if commit_inc.exists() else "MISSING"}')

    cpps = read_cpp_list(src)
    base = cl_args(src, a.config, a.shorten, overlay, major['define'], render_events=not a.no_render_events,
                   profile_use=profile_use)
    summary['render_events'] = render_events_state(src, not a.no_render_events)   # ticket 4932
    log(f'render messages ({RENDER_EVENTS_DEFINE}, ticket 4932): {summary["render_events"]}')
    log(f'Sources: 1 PCH ({PCH_CPP}) + {len(cpps)} from build_vp_clang.py = {len(cpps) + 1}')
    log(f'clang-cl args: {" ".join(base)}', echo=False)
    summary['sources'] = len(cpps) + 1
    summary.update(marker_record(src))   # ticket 0574: a hash traces to its marker defines
    log(f'marker defines: {summary["marker_defines"] or "none"} (markers {summary["markers_found"] or "none"}; '
        f'opt-out {MARKER_OPT_OUT_ENV}=0: {summary["marker_defines_opt_out"]})')

    objs = [obj_path(out, PCH_CPP)] + [obj_path(out, c) for c in cpps]
    if a.link_only:
        missing = [str(o) for o in objs if not o.exists()]
        if missing:
            die(log, f'--link-only: {len(missing)} objects missing in {out / "obj"}, e.g. {missing[0]}')
        log('--link-only: compile skipped')
    else:
        outputs = {}
        with open(out / 'compile.log', 'w', encoding='utf-8', newline='\n') as clog:
            log('Compiling PCH...')
            t0 = time.perf_counter()
            cpp, rc, text, dt, cmd = compile_one(env, base, src, out, PCH_CPP, '/Yc')
            timings['pch'] = time.perf_counter() - t0
            clog.write(f'==== {cpp} rc={rc} {dt:.1f}s\n{" ".join(cmd)}\n{text}\n')
            outputs[cpp] = text
            log(f'PCH rc={rc} in {dt:.1f} s')
            if rc != 0:
                die(log, f'PCH failed, see {out / "compile.log"}')

            log(f'Compiling {len(cpps)} sources with {a.jobs} jobs...')
            t0 = time.perf_counter()
            failed, per_tu = [], []
            with cf.ThreadPoolExecutor(a.jobs) as ex:
                futs = [ex.submit(compile_one, env, base, src, out, c, '/Yu') for c in cpps]
                for fut in futs:  # in list order, so compile.log is deterministic
                    cpp, rc, text, dt, cmd = fut.result()
                    clog.write(f'==== {cpp} rc={rc} {dt:.1f}s\n{text}\n')
                    outputs[cpp] = text
                    per_tu.append((dt, cpp))
                    if rc != 0:
                        failed.append(cpp)
            timings['compile'] = time.perf_counter() - t0
        ok = len(cpps) + 1 - len(failed)
        summary['compiled'] = ok
        log(f'Compiled {ok}/{len(cpps) + 1} (PCH included) in {timings["compile"]:.1f} s wall '
            f'(sum of TU times {sum(d for d, _ in per_tu):.0f} s)')
        log('Slowest TUs: ' + ', '.join(f'{Path(c).name} {d:.1f}s' for d, c in sorted(per_tu, reverse=True)[:8]))
        summary['warnings'], summary['compile_errors'], shorten_sites = summarize_diags(outputs, out, log)
        if profile_sha or a.config == PGO_INSTR:  # ticket 4801
            summary['pgo_diagnostics'] = pgo_diagnostics(outputs)
            log('PGO diagnostics: ' + ', '.join(f'{k} {v}' for k, v in summary['pgo_diagnostics'].items()))
        if profile_sha:  # ticket 0513: the retrain rule
            v = summary['pgo_retrain'] = retrain_verdict(summary['pgo_diagnostics'], summary['profile']['functions'],
                                                         summary['profile']['lock_pin'] if pgo['locked'] else None,
                                                         srcinfo['head'])
            share = 'unknown' if v['unmatched_share'] is None else f'{100 * v["unmatched_share"]:.3f} %'
            log(f'PGO retrain rule (ticket 0513): {v["unmatched"]} of {v["functions"]} functions unmatched ({share}, '
                f'threshold {100 * v["threshold"]:g} %); '
                + ('RETRAIN the profile: ' + '; '.join(v['reasons']) if v['needed'] else 'the profile is current'))
        if failed:
            for c in failed:
                log(f'  FAILED: {c}')
            die(log, f'{len(failed)} source(s) failed to compile, see {out / "compile.log"}')
        if a.shorten:
            probe = check_physical_lines(env, out / 'gen')  # ticket 4810
            if probe:
                die(log, f'narrowing gate: {probe}')
            try:
                keys = narrowing_keys(shorten_sites, src)
            except NarrowingSiteError as e:
                die(log, f'narrowing gate: {e}')
            summary['narrowing_sites'] = len(shorten_sites)
            # ticket 0159: fast compiles exactly as release does up to code generation (-flto only changes the object
            # format), so the compiler's -Wshorten-64-to-32 sites are release's: it is gated against release's rows
            # (ticket 4801: pgo-instr too; instrumentation is added after the front end; ticket 0513: release-pgo, the
            # profile is applied after the front end as well)
            bcfg = 'release' if a.config in ('fast', PGO_INSTR, RELEASE_PGO) else a.config
            if a.write_narrowing_baseline:
                write_baseline(a.narrowing_baseline, bcfg, keys, srcinfo, log)
            ok_gate, n_new, n_gone = narrowing_gate(a.narrowing_baseline, bcfg, keys, out, log)
            summary['narrowing_gate'] = {'ok': ok_gate, 'new': n_new, 'gone': n_gone,
                                         'baseline': str(a.narrowing_baseline)}
            if not ok_gate:
                (out / 'build-summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
                die(log, f'narrowing gate: {n_new} new {SHORTEN_FLAG} site(s) not in {a.narrowing_baseline.name}, '
                         f'listed in {out / "narrowing-new.txt"}', code=3)

    if a.compile_only:  # ticket 4801
        timings['total'] = time.perf_counter() - t_start
        summary['timings_s'] = {k: round(v, 1) for k, v in timings.items()}
        summary['dll'] = None
        summary['compile_only'] = True
        (out / 'build-summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
        log('OK: --compile-only: compiled, not linked')
        log.close()
        return

    dll = out / f'{CORE_DLL}.dll'
    log(f'Linking ({"census, /FORCE:UNRESOLVED" if a.census else "normal"}'
        f'{", stubs from " + str(a.stubs) if a.stubs else ""})...')
    sys.path.insert(0, str(HERE))
    import linkcheck  # noqa: E402  tools/x64-dll/linkcheck.py (one-off 9023, P10)
    try:
        marker = compile_marker(env, out, a.config)
        pgo_objs = compile_pgo_objects(env, out, a.config, profile_sha)  # ticket 4801; [] for release without one
    except RuntimeError as e:
        die(log, str(e))
    place = take_link_place(a.config, out, log, a.link_place_wait)
    if place is not None:
        summary['link_place'] = {'taken': place.taken, 'waited_s': round(place.waited_s, 1),
                                 'memory_waited': place.memory_waited}
    link_started = time.time()
    summary['link_env'] = link_env_record(link_env(env))
    log(f'link environment (issue 0358): {", ".join(summary["link_env"]["names"])} only, sha256 '
        f'{summary["link_env"]["sha256"][:16]}')
    try:
        rc, text, dt, peaks = link(env, out, src, a.config, a.census, a.stubs, objs, True, dll, marker, place,
                                   pgo_objs)
        if a.census and (a.mangled == 'on' or (a.mangled == 'auto' and a.config == 'debug')):
            (out / 'mangled').mkdir(exist_ok=True)
            mangled_link = link(env, out, src, a.config, True, a.stubs, objs, False,
                                out / 'mangled' / f'{CORE_DLL}.dll', marker, place, pgo_objs)
        else:
            mangled_link = None
    finally:
        if place is not None:
            place.release()
    timings['link'] = dt
    summary['link_peak_private_mb'] = peaks.get('peak_private_mb')
    summary['link_peak_working_set_mb'] = peaks.get('peak_working_set_mb')
    (out / 'link.log').write_text(text, encoding='utf-8', newline='\n')
    undef = parse_undefined(text)
    log(f'lld-link rc={rc} in {dt:.1f} s, peak private {peaks.get("peak_private_mb", "?")} MB, peak working set '
        f'{peaks.get("peak_working_set_mb", "?")} MB; {len(undef)} undefined-symbol reports; log {out / "link.log"}')
    summary['link_rc'] = rc
    # One-off 9023 (P10): exit 0 is not enough; no error line (census links force unresolved symbols, so their log
    # is not read for errors) and a DLL written by this link, not one left from an earlier build.
    link_bad = linkcheck.link_failure(rc, text, dll, link_started, check_log=not a.census)
    if link_bad and rc == 0:
        log(f'link check: {link_bad}')
        rc = summary['link_rc'] = 1

    if a.census or undef:
        mangled = []
        if mangled_link is not None:
            rc2, text2, dt2, _ = mangled_link
            timings['link_mangled'] = dt2
            (out / 'link-mangled.log').write_text(text2, encoding='utf-8', newline='\n')
            mangled = parse_undefined(text2)
            # Same symbol table walk, so same order; check the first referencing object and source line agree
            # pairwise (they do not depend on demangling).
            if len(mangled) != len(undef) or any(obj_and_source(m[2]) != obj_and_source(u[2])
                                                 for m, u in zip(mangled, undef)):
                log('WARNING: mangled link reported a different list; mangled column left empty')
                mangled = []
            log(f'Mangled-name link rc={rc2} in {dt2:.1f} s')
        summary['unresolved'], summary['unresolved_by_group'] = write_census(out, undef, mangled, log)
    else:
        summary['unresolved'] = 0
        log('Unresolved symbols: 0')

    head_now = first_line(git + ['rev-parse', 'HEAD']) if is_git else versions['src HEAD']
    if head_now != versions['src HEAD']:
        log(f'WARNING: --src HEAD moved during the build ({versions["src HEAD"][:9]} -> {head_now[:9]}); '
            'the objects may mix both commits')
    timings['total'] = time.perf_counter() - t_start
    summary['timings_s'] = {k: round(v, 1) for k, v in timings.items()}
    summary['dll'] = str(dll) if dll.exists() and rc == 0 else None
    if summary['dll']:
        info = linkcheck.artifact_info(dll)
        summary['dll_bytes'] = info['size']
        summary['dll_sha256'], summary['dll_mtime'] = info['sha256'], info['mtime']
        if profile_sha:  # ticket 4801: the DLL names its profile
            summary['dll_profile'] = dll_profile(dll)
            if summary['dll_profile'] != profile_sha:
                rc = summary['link_rc'] = 1
                link_bad = f'the DLL\'s {PROFILE_SECTION} section names {summary["dll_profile"]}, not {profile_sha}'
        if a.config in MARKED_CONFIGS and dll_config(dll) != a.config:
            rc = summary['link_rc'] = 1
            link_bad = f'the DLL\'s {CONFIG_SECTION} marker names {dll_config(dll)}, not {a.config}'
    log('Timings (s): ' + ', '.join(f'{k} {v:.1f}' for k, v in timings.items()))
    if summary['dll']:
        # issue 0131: every link input of this build must be free of time stamps, or another folder gives other bytes
        import repeatcheck  # noqa: E402  tools/x64-dll/repeatcheck.py
        rep = repeatcheck.check(out / 'link.rsp')
        summary['repeat_check'] = {k: rep[k] for k in ('ok', 'inputs', 'members', 'stamped')}
        log(repeatcheck.line(rep, summary['dll_sha256']))
    (out / 'build-summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    if rc != 0:
        die(log, f'{link_bad or f"lld-link rc={rc}"}, see {out / "link.log"}')
    log(f'OK: {dll} sha256 {summary["dll_sha256"]} size {summary["dll_bytes"]} mtime {summary["dll_mtime"]}')
    if (summary.get('pgo_retrain') or {}).get('needed'):   # ticket 0513: the DLL is good, the profile is stale
        log('WARNING: PGO retrain rule: ' + '; '.join(summary['pgo_retrain']['reasons']) + ' (build-summary.json '
            'pgo_retrain; the release take stops on it; research/08-performance/speedup-p1-pgo.md 11)')
    log.close()


if __name__ == '__main__':
    main()
