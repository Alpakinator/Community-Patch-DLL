"""build (CL step 6): the x64 DLL, libraries, server and proxy (server/build_server.py, gated, --config release
--offline) and the 32-bit plain and instance-aware (-ia) DLLs (the fork's build_vp_clang.py under quietlock.py run),
one build at a time, then the layout under <work>/layout/ (never <pins folder>/dlls/, the setting pins_dir of
tools/release/settings.py: the MC moves pins):

    x64-<commit8>-p<tools8>/   bin/, proxy/bin/, proxy/build-proxy.json, SHA256.txt, README.txt
    i0ship-<commit8>/          CvGameCore_Expansion2.dll and .pdb, SHA256.txt, README.txt
    i0ship-<commit8>-ia/       the same, plus the instruments patch

Sources: the x64 commit as a plain copy (git archive) when its source fingerprint equals deps/dll.lock's (build.py
accepts it as the pinned source), else a detached worktree of the fork built with --unlocked; the 32-bit plain build in
a detached worktree (version "... Clean", as the pins); the -ia build in a git archive with the instruments patch
applied and update_commit_id.bat stubbed (9045's build9045.py). The instruments patch is --ia-patch when given, else
chosen by the i0 commit's base (issue 0310, IA_PATCHES): the patch resolved on the newest upstream base the i0 commit
contains, 9045's for 5.4.8-based pins and 3515's when the i0 tree is upstream master's. A narrowing-gate failure of the x64 build is a stop of
CL step 7 with the new sites. Each of the three builds is recorded as it finishes, so a rerun with the same inputs
resumes at the build that did not finish.

The major-civilisation count (issue 4252, epic 0334; MC decisions G2, G3): config `major_civs` (--major-civs N, default
22). Every source's own `MOD_GLOBAL_MAX_MAJOR_CIVS` line in CustomModsGlobal.h is read first (tools/x64-dll/build.py
resolve_major_civs): a live line that is not N is a stop, never a silent build of another count. For N other than 22:
- x64: tools/x64-dll/build_all.py --major-civs N (the define on the DLL compile only) into <build>/deps, then
  server/build_server.py --deps of it (the server and the proxy take no define, G1); build_all fails unless the smoke
  run's DLL says "Supporting N major civilizations" and its version string carries "+mN".
- 32-bit plain and -ia: git archives (never a worktree: the build script is changed in the copy), the fork's
  build_vp_clang.py given the define in its RELEASE_PREDEFS and DEBUG_PREDEFS (inject_x86_define; the fork's DLL source
  is unchanged and no fork commit is made), version stubs "<describe>+mN Copy" and "<describe>+ia<n>+mN Copy".
- Names: x64-<commit8>-mN-p<tools8>, i0ship-<commit8>-mN, i0ship-<commit8>-mN-ia; the candidate locks' keys
  release-N, verified-N (dll.lock) and release-N, instance-N (i0.lock) instead of release, verified, instance.
For 22 every command, name and key is the one before issue 4252.

The increment 2 shell (issue 0464): when the release's descriptor says shell_config "i2" (issue 2602), the stage also
builds the compositor with shell/build.cmd (C5_SHELL_BUILD = <work>/build/shell-<tools7>, under quietlock.py run; its
c5render step is a gated launch point) from this repository at the commit the server and the proxy are built from, and
lays out beside the proxy, in x64-.../proxy/bin/: shell_composite.dll (and its .pdb), shell_composite.sha256 naming the
proxy and the compositor (proxy/tools/shell_manifest.py's format), shell_composite.ini (shell/composite/'s: the
compositor's built-in defaults are not that file's) and MinHook-LICENSE.txt (the licence of MinHook,
linked into the compositor; its NOTICE asks the notice to stay with the binaries). The layout is then checked as
pack.py will check it (pack.check_shell_config(layout, 'i2')). Thin (shell_config "thin" or absent): no compositor
build, and every command, input, name and file is the one before 0464 (tests/test_take_release_i2_compositor.py runs
the stage of before and this one on the same inputs). The stage fails when the run's descriptor names a shell_config
and the descriptor this stage reads (testdata's candidate) names another or none (_common.shell_config_of).
Ticket 0574 (D195): shell_config "i3" builds and lays out exactly what "i2" does (its option set is the pack's, not the
build's); the inputs and outputs then say "i3".
"""
from __future__ import annotations

import importlib.util
import os
import re
import shutil
import sys
import tarfile
import time
from pathlib import Path

from take_release import REPO, StageResult, release_settings, sha256_file
from stages import _common
from stages.lock import lock_text

DEPENDS = ['survey', 'branches', 'testdata', 'lock']  # testdata: the smoke database (MC #414)
DLL = 'CvGameCore_Expansion2.dll'
LLVM_BIN = 'C:\\Program Files\\LLVM\\bin'
# The instance-aware instruments patches by the upstream base each was resolved on (issue 0310), oldest base first. With
# no --ia-patch (config ia_patch = BY_BASE) the -ia step takes the patch whose base is the newest of these that the i0
# commit contains (merge-base --is-ancestor in the fork): a 5.4.8-based pin contains only Release-5.4.8, an i0 branch
# cut from upstream master contains ba8aafdca too. A newer master or release that 3515's patch no longer applies to
# stops at ia-apply (CL 3) as before: resolve it, then add its row here or pass --ia-patch.
BY_BASE = 'by-base'
IA_PATCHES = (
    {'name': '9045', 'base': '7307aefdf985a4f401a7996a896283557b896e2b', 'base_name': 'Release-5.4.8',
     'patch': str(REPO / 'tools/d10/evidence/9045/ia-ee893bc18-440878983.patch')},
    {'name': '3515', 'base': 'ba8aafdcaba01a10480addd14790257e368fb02b', 'base_name': 'upstream master',
     'patch': str(REPO / 'tools/d10/evidence/3515/ia-8be73b6f5-440878983.patch')},
    # DLL pin batch 3 (ticket 0463): not an upstream base but the i0 pin 39f17f754, whose #13395 conflicts with 9045's
    # patch in Lua/CvLuaSupport.h; an i0 commit that contains the pin takes this patch (it contains 9045's base too).
    {'name': '0463', 'base': '39f17f7545926e3e19d37e1f8242f7b9cde412cd', 'base_name': 'i0-ship-batch3',
     'patch': str(REPO / 'tools/d10/evidence/0463/ia-39f17f754-440878983.patch')},
    # Ticket 4968 (epic 0590): the i0 pin batch 5 5d18d3d6c with 0463's instruments plus fork branch
    # ia-luaexec-no-loadstring (d45303627, ticket 0591): the Lua channel's runner compiles without loadstring, which
    # Civ V build 26577625 sets to nil. One diff against the pin (tools/d10/evidence/4968/s01_ia_patch.sh). An i0
    # commit that contains the pin takes this patch; an older one keeps 0463's.
    {'name': '4968', 'base': '5d18d3d6cbe4075d66271ec55a2e6cfe3e466687', 'base_name': 'i0-ship-batch5',
     'patch': str(REPO / 'tools/d10/evidence/4968/ia-5d18d3d6c-4968.patch')},
    # Ticket 0766 (D259, 2026-11-25): 4968's instruments (no-loadstring runner included) on upstream Release-5.5
    # b0bce8c2f, eight conflicts resolved (upstream removed LuaSupport::InitLuaFramework; its SqliteLogger kept, the
    # instruments' Mem* table registrations appended; the i0-only cache stamp of bug 0116 left out). Applies with plain
    # git apply to a git archive of the tag; an i0 commit on 5.5 (0764's i0-vp55) gets it by base, and its own carried
    # commits may still conflict with it. research/06-tooling-and-workflows/headless-and-parallel-testing.md 6.5.
    {'name': '0766', 'base': 'b0bce8c2f23c865cf5b0779986654366c88cef62', 'base_name': 'Release-5.5',
     'patch': str(REPO / 'tools/d10/evidence/0766/ia-b0bce8c2f-0766.patch')},
)
# The increment 2 shell (issue 0464)
COMPOSITE = 'shell_composite.dll'
COMPOSITE_PDB = 'shell_composite.pdb'
COMPOSITE_SHA = 'shell_composite.sha256'
MINHOOK_NOTICE = 'MinHook-LICENSE.txt'
# The compositor's configuration, read beside it (by the compositor and by the proxy's model-fog switch). Its built-in
# defaults are not this file's (insert, tap, lockstep, handover, depth_write, shadow_tap, ice_tap differ), so the
# layout carries it: shell/build.cmd's own copy is the same file.
COMPOSITE_INI = 'shell_composite.ini'
COMPOSITE_INI_SRC = REPO / 'shell' / 'composite' / COMPOSITE_INI
MINHOOK_LICENSE = REPO / 'shell' / 'third_party' / 'minhook' / 'LICENSE.txt'
COMPOSITE_SHELLS = ('i2', 'i3')   # the shell configurations whose layout carries the compositor (i3: ticket 0574)
BUILD_SHELL = ['cmd', '/c', str(REPO / 'shell' / 'build.cmd')]   # tool 'build_shell' when the driver's table has one


def _module(name, path):
    """A tools module loaded once from its file under a name of its own."""
    if name not in sys.modules:
        spec = importlib.util.spec_from_file_location(name, str(path))
        mod = importlib.util.module_from_spec(spec)
        sys.modules[name] = mod
        spec.loader.exec_module(mod)
    return sys.modules[name]


def release_pack():
    """tools/release/pack.py (build_composite, check_shell_config, PackError)."""
    return _module('take_release_build_pack', REPO / 'tools' / 'release' / 'pack.py')


def shell_manifest():
    """proxy/tools/shell_manifest.py (manifest_text: the format the proxy and pack.py read)."""
    return _module('take_release_build_shell_manifest', REPO / 'proxy' / 'tools' / 'shell_manifest.py')


def x64build():
    """tools/x64-dll/build.py as a module (resolve_major_civs, check_major_civs, MAJOR_CIVS_*), loaded once under a name
    of its own (this stage module is also named after `build`)."""
    name = 'take_release_x64dll_build'
    if name not in sys.modules:
        spec = importlib.util.spec_from_file_location(name, str(REPO / 'tools' / 'x64-dll' / 'build.py'))
        mod = importlib.util.module_from_spec(spec)
        sys.modules[name] = mod
        spec.loader.exec_module(mod)
    return sys.modules[name]


def major_of(ctx) -> int:
    return int(ctx.config.get('major_civs') or x64build().MAJOR_CIVS_DEFAULT)


def major_suffix(n) -> str:
    """'' for 22, '-m43' for 43: the folder and layout names."""
    tag = x64build().major_civs_tag(n)
    return f'-{tag}' if tag else ''


def major_key(key, n) -> str:
    """A lock key of the count: 'release' for 22, 'release-43' for 43."""
    return key if int(n) == x64build().MAJOR_CIVS_DEFAULT else f'{key}-{int(n)}'


def major_check(ctx, src, what):
    """(StageResult stop or None, resolve_major_civs() dict) for the source folder of one build."""
    xb = x64build()
    try:
        return None, xb.resolve_major_civs(src, major_of(ctx), bool(ctx.config.get('unvalidated_major_civs')))
    except xb.MajorCivsError as e:
        return StageResult.stop(f'{what}: {e}',
                                'run again with --major-civs equal to the source\'s own CustomModsGlobal.h line, or '
                                'with a source whose MOD_GLOBAL_MAX_MAJOR_CIVS lines are commented out '
                                '(research/03-dll-engine-boundary/player-limits.md 1.1, 8.1)', cl_step='6'), None


X86_DEFINE_MARK = '# take_release (issue 4252): the major-civilisation define'
X86_ANCHOR_RE = re.compile(r'^DEBUG_PREDEFS = .*$', re.M)


def inject_x86_define(src: Path, n: int):
    """Give the fork's 32-bit build script in the COPY src the define MOD_GLOBAL_MAX_MAJOR_CIVS=n: three lines after its
    `DEBUG_PREDEFS = ...` line (before `PREDEFS = {` reads both lists), and one printed line naming it. build_vp_clang.py
    has no option for a define and clang-cl ignores the CL / _CL_ variables (tested, clang 19.1.7, 2026-11-15). Returns
    None, or why the script does not have the expected form (the caller stops). A second call with the same n is a
    no-op."""
    p = Path(src) / 'build_vp_clang.py'
    text = p.read_text(encoding='utf-8')
    define = f'{x64build().MAJOR_CIVS_DEFINE}={int(n)}'
    if X86_DEFINE_MARK in text:
        return None if f"['{define}']" in text else f'{p} already carries another major-civilisation define'
    anchors = X86_ANCHOR_RE.findall(text)
    if len(anchors) != 1:
        return f'{p}: {len(anchors)} "DEBUG_PREDEFS = ..." lines (1 expected)'
    m = X86_ANCHOR_RE.search(text)
    rest = text[m.end():]
    if 'RELEASE_PREDEFS = ' not in text[:m.start()] or not re.search(r'^PREDEFS = \{', rest, re.M):
        return f'{p}: RELEASE_PREDEFS before and "PREDEFS = {{" after the DEBUG_PREDEFS line expected'
    add = (f"\n{X86_DEFINE_MARK}\nRELEASE_PREDEFS = RELEASE_PREDEFS + ['{define}']\n"
           f"DEBUG_PREDEFS = DEBUG_PREDEFS + ['{define}']\n"
           f"print('build_vp_clang: /D{define} added to the compile by take_release (issue 4252)', flush=True)")
    p.write_text(text[:m.end()] + add + rest, encoding='utf-8', newline='')
    return None


def write_version_stub(dst: Path, stub: str):
    """update_commit_id.bat of a source copy, replaced by one that writes CURRENT_GAMECORE_VERSION = stub."""
    with open(Path(dst) / 'update_commit_id.bat', 'w', newline='\r\n') as f:
        f.write('@echo off\n')
        f.write(f'echo const char CURRENT_GAMECORE_VERSION[] = "{stub}"; //autogenerated > "%~dp0commit_id.inc"\n')


def prepare_i0_major(ctx, commit, describe, n):
    """The plain 32-bit build of another count than 22: a git archive of the i0 commit with the define and the version
    stub "<describe>+mN Copy" (a worktree would turn "Dirty"). Returns (StageResult or None, src)."""
    tag = x64build().major_civs_tag(n)
    dst = ctx.work / 'src' / f'i0{tag}-{commit[:8]}'
    if (dst / '.take_release-major').is_file():
        return None, dst
    _archive(ctx, commit, dst)
    res, _ = major_check(ctx, dst, f'the 32-bit source {commit[:9]}')
    if res:
        return res, None
    why = inject_x86_define(dst, n)
    if why:
        return StageResult.failed(f'the define cannot be given to the 32-bit build: {why}'), None
    stub = f'{describe}+{tag} Copy'
    write_version_stub(dst, stub)
    (dst / '.take_release-major').write_text(f'{commit} {n} {stub}\n')
    return None, dst


def ia_candidates(ctx):
    return ctx.config.get('ia_patches') or IA_PATCHES   # config ia_patches: a test hook


def inputs(ctx):
    ia = ctx.config.get('ia_patch') or BY_BASE
    if ia == BY_BASE:
        sha = {c['name']: sha256_file(c['patch']) if Path(c['patch']).is_file() else None for c in ia_candidates(ctx)}
    else:
        sha = sha256_file(ia) if Path(ia).is_file() else None
    inp = {'jobs': ctx.config.get('jobs'), 'ia_patch': str(ia), 'ia_patch_sha256': sha, 'ia': ctx.config.get('ia', True)}
    if major_of(ctx) != x64build().MAJOR_CIVS_DEFAULT:   # issue 4252; absent for 22, so a 22 run's inputs hash is unchanged
        inp['major_civs'] = major_of(ctx)
    if getattr(ctx.desc, 'shell_config', None) in COMPOSITE_SHELLS:   # issue 0464, i3 ticket 0574; absent for thin
        inp['shell_config'] = ctx.desc.shell_config
    if getattr(ctx.desc, 'purity_allowed_drops', None):   # ticket 0574; absent: the inputs hash is unchanged
        inp['purity_allowed_drops'] = [list(x) for x in ctx.desc.purity_allowed_drops]
    cfg = x64_config(ctx)
    if cfg != 'release':   # ticket 0513; absent for release: the inputs hash is unchanged
        inp['x64_config'] = cfg
        try:
            lp = x64build().lock_profile(dll_lock(ctx))
        except ValueError:
            lp = None
        inp['pgo_profile_sha256'] = lp['sha256'] if lp else None   # a new locked profile is a new build input
    return inp


def purity_drop_env(ctx, env: dict):
    """Ticket 0574 (MC #7044/#7046): for a release whose descriptor names purity_allowed_drops, the allow-list file
    (<work>/inputs/purity-allowed-drops.tsv) and a plain copy of proxy/model/purity-baseline.json's audited_at commit
    go to proxy/build_proxy.py's step 7 (C5_PURITY_ALLOWED_DROPS, C5_PURITY_AUDITED_SRC): a pin that drops exactly
    those audited rows is then tested on the audited source; any other dropped row still fails the build. Returns a
    StageResult on a failure, else None (env changed in place; unchanged without the field)."""
    drops = getattr(ctx.desc, 'purity_allowed_drops', None)
    if not drops:
        return None
    f = ctx.dir('inputs') / 'purity-allowed-drops.tsv'
    f.write_bytes(('# purity_allowed_drops of the release descriptor (ticket 0574)\n'
                   + ''.join(f'{k}\t{n}\n' for k, n in drops)).encode('utf-8'))
    base = REPO / 'proxy' / 'model' / 'purity-baseline.json'
    try:
        import json
        commit = json.loads(base.read_text(encoding='utf-8'))['audited_at']
        audited = _archive(ctx, commit, ctx.scratch('src') / f'purity-audited-{commit[:8]}')
    except (OSError, ValueError, KeyError, RuntimeError) as e:
        return StageResult.failed(f'purity_allowed_drops: the audited source ({base} audited_at) could not be '
                                  f'copied from the fork: {e}')
    env['C5_PURITY_ALLOWED_DROPS'] = str(f)
    env['C5_PURITY_AUDITED_SRC'] = str(audited)
    ctx.say('purity: allowed drops', len(drops), 'audited source', audited)
    return None


def pick_ia_patch(ctx, i0c):
    """(patch Path, how it was chosen) for the -ia build of the i0 commit, or (None, why there is none)."""
    given = ctx.config.get('ia_patch') or BY_BASE
    if given != BY_BASE:
        return Path(given), 'given (--ia-patch)'

    def contains(base, commit):
        return ctx.git('merge-base', '--is-ancestor', base, commit, check=False) is not None
    cands = ia_candidates(ctx)
    inside = [c for c in cands if contains(c['base'], i0c)]
    newest = [c for c in inside if all(o is c or contains(o['base'], c['base']) for o in inside)]
    if not newest:
        bases = ', '.join(f"{c['base_name']} ({c['base'][:9]})" for c in cands)
        return None, f'the i0 commit {i0c[:9]} contains none of the instruments patches\' bases ({bases})'
    c = newest[-1]
    if not Path(c['patch']).is_file():
        return None, f"the instruments patch {c['patch']} (base {c['base_name']}) is missing"
    return Path(c['patch']), f"by base: {c['base_name']} ({c['base'][:9]}), {c['name']}'s patch"


def _archive(ctx, commit, dst: Path, marker='.take_release-src'):
    """dst = a plain copy of commit (git archive), reused when its marker names the same commit."""
    m = dst / marker
    if m.is_file() and m.read_text().strip() == commit:
        return dst
    _common.fresh_dir(dst)
    dst.mkdir(parents=True)
    tar = dst.with_name(dst.name + '.tar')
    ctx.git('archive', '-o', tar, commit)
    with tarfile.open(tar) as t:
        try:
            t.extractall(dst, filter='data')
        except TypeError:  # Python without extraction filters
            t.extractall(dst)
    tar.unlink()
    return dst


def _worktree(ctx, commit, dst: Path):
    """dst = a detached worktree of the fork at commit (reused when it is one at that commit)."""
    if (dst / '.git').exists():
        head = ctx.git('rev-parse', 'HEAD', cwd=dst, check=False)
        if head == commit:
            return dst
    if dst.exists():
        _common.fresh_dir(dst)
        ctx.git('worktree', 'prune', check=False)
    ctx.git('worktree', 'add', '--detach', dst, commit)
    return dst


def _tools_commit(ctx):
    head = ctx.git('rev-parse', 'HEAD', cwd=REPO, check=False) or 'unknown'
    dirty = bool(ctx.git('status', '--porcelain', '--untracked-files=no', cwd=REPO, check=False))
    return head, dirty


def _narrowing_stop(out: Path):
    for f in sorted(out.rglob('narrowing-new.txt')):
        new = [ln for ln in f.read_text(encoding='utf-8', errors='replace').splitlines() if ln.startswith('NEW\t')]
        if new:
            sites = '; '.join(ln.split('\t')[1] for ln in new[:3]) + (' ...' if len(new) > 3 else '')
            return StageResult.stop(
                f'the narrowing gate found {len(new)} new -Wshorten-64-to-32 site(s) ({sites}), listed in {f}',
                'triage each site (tools/x64-dll/truncation-triage.md); fix it in the carry (rerun from branches) '
                'or, when it is safe, write the baseline with tools/x64-dll/build.py --shorten '
                '--write-narrowing-baseline (tools/x64-dll/README.md), then run again', cl_step='7',
                narrowing_new=str(f))
    return None


def built_config(out: Path):
    """The configuration build.py recorded for the x64 DLL (build-summary.json, else build_all.json): MC #226, the
    build stage's outputs carry it (0159 added `fast`, release's flags without whole-program optimisation)."""
    import json
    for name in ('build-summary.json', 'build_all.json'):
        for p in sorted(Path(out).rglob(name)):
            try:
                c = json.loads(p.read_text(encoding='utf-8')).get('config')
            except (OSError, ValueError):
                c = None
            if c:
                return c
    return None


def repeat_check(out: Path):
    """build.py's repeatability check of the x64 link (0131: /Brepro; build-summary.json `repeat_check`), or None."""
    import json
    for p in sorted(Path(out).rglob('build-summary.json')):
        try:
            return json.loads(p.read_text(encoding='utf-8')).get('repeat_check')
        except (OSError, ValueError):
            return None
    return None


# ---- the x64 DLL's configuration: release, or release-pgo with the locked profile (ticket 0513, D184) ----
X64_CONFIGS = ('release', 'release-pgo')
PGO_SUFFIX = '-pgo'   # the layout of a release-pgo DLL: x64-<commit8>[-mN]-p<tools7>-pgo (as fast's `-fast`)


def dll_lock(ctx) -> dict:
    """deps/dll.lock of this repository (config dll_lock: another file, a test hook)."""
    p = ctx.config.get('dll_lock')
    return x64build().read_dll_lock(Path(p)) if p else x64build().read_dll_lock()


def x64_config(ctx) -> str:
    """The x64 DLL's configuration of this take: config x64_config when given (a test hook and a per-take override),
    else deps/dll.lock `release-config` (the MC's switch at a pin batch: "Making it the release default stays the MC's
    step"), else release."""
    return (ctx.config.get('x64_config') or dll_lock(ctx).get('release-config') or 'release').strip()


def pgo_plan(ctx, commit=None):
    """(StageResult stop or None, None for a plain release take, else {config, file, sha256, bytes, pin, name}).
    release-pgo consumes deps/dll.lock's locked profile: the file (config pgo_profile, else the lock's profile-file)
    must exist and have the lock's SHA-256, a mismatch is a stop; with `commit` (the x64 commit the take builds) the
    profile must have been trained on it (the retrain rule: retrain before each release). Nothing is built before."""
    cfg = x64_config(ctx)
    if cfg not in X64_CONFIGS:
        return StageResult.stop(f'x64 configuration {cfg!r} (deps/dll.lock release-config or config x64_config): not '
                                f'one of {", ".join(X64_CONFIGS)}', 'fix the lock line or the option', cl_step='6'), None
    if cfg == 'release':
        return None, None
    xb = x64build()
    how = ('retrain the profile on the commit the take builds (tools/x64-dll/pgo/train.py, '
           'research/08-performance/speedup-p1-pgo.md section 11), lock it in deps/dll.lock (profile, profile-file), '
           'then run again; or take this release without PGO (deps/dll.lock release-config release)')
    try:
        lp = xb.lock_profile(dll_lock(ctx))
    except ValueError as e:
        return StageResult.stop(str(e), how, cl_step='6'), None
    if not lp:
        return StageResult.stop('release-pgo: deps/dll.lock has no profile line', how, cl_step='6'), None
    f = ctx.config.get('pgo_profile') or lp.get('file')
    if not f and lp.get('file_problem'):   # ticket 0774: profile-file names a setting that is not set
        return StageResult.stop(f'release-pgo: {lp["file_problem"]}', how, cl_step='6'), None
    if not f or not Path(f).is_file():
        return StageResult.stop(f'release-pgo: the locked profile file {f or "(no profile-file line)"} is missing',
                                'copy the profile with SHA-256 ' + lp['sha256'] + ' there (it is not committed; '
                                'speedup-p1-pgo.md 11 names its archive), or ' + how, cl_step='6'), None
    got = sha256_file(f).lower()
    if got != lp['sha256']:
        return StageResult.stop(f'release-pgo: the profile {f} has SHA-256 {got}, deps/dll.lock locks {lp["sha256"]}: '
                                'refused (a profile is a build input like the source)', how, cl_step='6'), None
    if commit and not xb.same_commit(lp['pin'], commit):
        return StageResult.stop(f'release-pgo: the locked profile was trained on {lp["pin"][:9]}, this take builds '
                                f'{commit[:9]}', how, cl_step='6'), None
    return None, {'config': cfg, 'file': str(f), 'sha256': lp['sha256'], 'bytes': lp['bytes'], 'pin': lp['pin'],
                  'name': lp['name']}


def pgo_check_built(deps: Path, plan: dict):
    """After build_all.py --config release-pgo: (StageResult or None, record). The DLL step's summary must name the
    locked profile (profile.sha256 and the DLL's own .vppgo section, dll_profile) and the retrain rule must not ask
    for a retrain (pgo_retrain.needed); either is a stop."""
    import json
    p = Path(deps) / 'dll' / 'build-summary.json'
    try:
        s = json.loads(p.read_text(encoding='utf-8'))
    except (OSError, ValueError) as e:
        return StageResult.failed(f'release-pgo: {p} unreadable ({e})'), None
    if s.get('config') != 'release-pgo':
        return StageResult.failed(f'release-pgo: {p} records config {s.get("config")!r}'), None
    sha = (s.get('profile') or {}).get('sha256')
    if sha != plan['sha256'] or s.get('dll_profile') != plan['sha256']:
        return StageResult.failed(f'release-pgo: {p} names the profile {sha} and the DLL\'s .vppgo {s.get("dll_profile")}'
                                  f', not the locked {plan["sha256"]}'), None
    v = s.get('pgo_retrain') or {}
    if v.get('needed') is not False:
        return StageResult.stop('release-pgo: the retrain rule asks for a new profile: '
                                + ('; '.join(v.get('reasons') or []) or f'no verdict in {p}'),
                                'retrain the profile (tools/x64-dll/pgo/train.py, speedup-p1-pgo.md 11), lock it, '
                                'run again', cl_step='6'), None
    return None, {'x64_config': 'release-pgo', 'profile_sha256': sha, 'profile_pin': plan['pin'],
                  'pgo_diagnostics': s.get('pgo_diagnostics'), 'pgo_retrain': v}


def build_x64(ctx, commit, src: Path, unlocked: bool, out: Path, major: int = 22):
    """server/build_server.py (a gated launch point). Returns (StageResult or None, record). major (issue 4252): for
    another count than 22, tools/x64-dll/build_all.py --major-civs first (gated), then build_server.py --deps of it.
    Ticket 0513: a release-pgo take (pgo_plan) also builds the DLL first, with build_all.py --config release-pgo
    --profile <the locked file>, then build_server.py --config release --deps of it; the record carries x64_config
    and the profile."""
    res, plan = pgo_plan(ctx, commit)
    if res:
        return res, None
    _common.fresh_dir(out)
    cmd = ctx.tool('build_server') + ['--src', src, '--out', out, '--config', 'release', '--offline', '--jobs',
                                      ctx.config.get('jobs', 6)] + (['--unlocked'] if unlocked else [])
    # D107 / 0159: the release link waits in place in tools/x64-dll/build.py for the machine-wide whole-program link
    # place (perf/quietlock/linkplace.py); the team's link slot (tools/release/linkslot.py) is retired.
    # The smoke run's database is the one the DLL is built for (MC condition #414): build_all.default_db() reads it.
    env = dict(os.environ, C5_RELEASE_TEST_DB=str(ctx.outputs('testdata')['test_db']))
    res = purity_drop_env(ctx, env)   # ticket 0574: allowed purity drops for build_proxy's step 7
    if res:
        return res, None
    pgo_rec = {}
    if major != x64build().MAJOR_CIVS_DEFAULT or plan:
        deps = out / 'deps'
        dcmd = ctx.tool('build_all') + ['--src', src, '--out', deps, '--config', plan['config'] if plan else 'release',
                                        '--offline', '--jobs', ctx.config.get('jobs', 6)]
        if major != x64build().MAJOR_CIVS_DEFAULT:   # the 22 command line stays as it was
            dcmd += ['--major-civs', major]
            dcmd += ['--unvalidated-major-civs'] if ctx.config.get('unvalidated_major_civs') else []
        dcmd += ['--profile', plan['file']] if plan else []
        dcmd += ['--unlocked'] if unlocked else []
        what = ' '.join(([f'--major-civs {major}'] if major != x64build().MAJOR_CIVS_DEFAULT else []) +
                        (['--config release-pgo'] if plan else []))
        r = ctx.run(dcmd, name='x64-dll' + (f'-m{major}' if major != x64build().MAJOR_CIVS_DEFAULT else '') +
                    ('-pgo' if plan else ''), gated=True, env=env)
        if r.rc != 0:
            stop = _narrowing_stop(out)
            if stop:
                return stop, None
            return StageResult.failed(f'tools/x64-dll/build_all.py {what} exit {r.rc}; {r.log}\n{r.tail(10)}'), None
        if plan:
            res, pgo_rec = pgo_check_built(deps, plan)
            if res:
                return res, None
        cmd += ['--deps', deps]
    r = ctx.run(cmd, name='x64', gated=True, env=env)
    if r.rc != 0:
        stop = _narrowing_stop(out)
        if stop:
            return stop, None
        return StageResult.failed(f'server/build_server.py exit {r.rc}; {r.log}\n{r.tail(10)}'), None
    need = [out / 'bin' / DLL, out / 'bin' / 'server.exe', out / 'proxy' / 'bin' / DLL]
    missing = [str(p) for p in need if not p.is_file()]
    if missing:
        return StageResult.failed(f'build_server.py exit 0 but missing: {", ".join(missing)}'), None
    ba_path = next(iter(sorted(out.rglob('build_all.json'))), None)
    ba = {}
    if ba_path:
        import json
        ba = json.loads(ba_path.read_text(encoding='utf-8'))
    dll = ba.get('dll') or {}
    if ba.get('major_civs', x64build().MAJOR_CIVS_DEFAULT) != major:
        return StageResult.failed(f'{ba_path} records major_civs {ba.get("major_civs", 22)}, the build asked for '
                                  f'{major}'), None
    rec = {'out': str(out), 'commit': commit, 'src': str(src), 'unlocked': unlocked, 'seconds': round(r.seconds),
           'config': built_config(out), 'repeat_check': repeat_check(out),
           'build_all': str(ba_path) if ba_path else None,
           'steps': [f"{s.get('step')} rc {s.get('rc', 'skipped')}" for s in ba.get('steps', [])],
           'unresolved': dll.get('unresolved'), 'narrowing_sites': dll.get('narrowing_sites'),
           'narrowing_gate': dll.get('narrowing_gate'), 'dll_sha256': sha256_file(need[0]).upper()}
    if major != x64build().MAJOR_CIVS_DEFAULT:
        rec.update(major_civs=major, smoke_major_civs=ba.get('smoke_major_civs'))
    if plan:   # ticket 0513: the laid-out DLL itself names the locked profile
        got = x64build().dll_profile(need[0])
        if got != plan['sha256']:
            return StageResult.failed(f'release-pgo: {need[0]} names the profile {got} in its .vppgo section, not the '
                                      f'locked {plan["sha256"]}'), None
        rec.update(pgo_rec, config='release-pgo')
    return None, rec


def build_i0(ctx, src: Path, name: str):
    """The fork's build_vp_clang.py --config release in src, under quietlock.py run. Returns (StageResult, record)."""
    co = src / 'clang-output'
    if co.exists():
        shutil.rmtree(co)  # the previous build's output in the driver's own source folder
    env = dict(os.environ)
    env['PATH'] = ctx.config.get('llvm_bin', LLVM_BIN) + ';' + str(src).replace('/', '\\') + ';' + env.get('PATH', '')
    t0 = time.time()
    r = ctx.run(ctx.tool('build_vp_clang') + ['--config', 'release'], name=name, cwd=src, env=env,
                what=f'{ctx.who} take_release {ctx.tag} 32-bit DLL build {src.name}')
    dll = co / 'Release' / DLL
    if r.rc != 0:
        return StageResult.failed(f'build_vp_clang.py ({name}) exit {r.rc}; {r.log}\n{r.tail(10)}'), None
    if not dll.is_file() or dll.stat().st_mtime < t0 - 5:
        return StageResult.failed(f'build_vp_clang.py ({name}) exit 0 but {dll} is missing or older than the build'), None
    return None, {'src': str(src), 'dll': str(dll), 'pdb': str(co / 'Release' / 'CvGameCore_Expansion2.pdb'),
                  'seconds': round(r.seconds), 'dll_sha256': sha256_file(dll).upper()}


def prepare_ia(ctx, commit, describe, patch: Path, major: int = 22):
    """git archive of the i0 commit + the instruments patch + the version stub. Returns (StageResult or None, src).
    major (issue 4252): another count than 22 adds the define (inject_x86_define) and "+mN" to the stub."""
    pid = re.search(r'-(\d+)\.patch$', patch.name)
    ident = pid.group(1) if pid else 'x'
    tag = x64build().major_civs_tag(major)
    dst = ctx.work / 'src' / f'i0ia{tag}-{commit[:8]}-{sha256_file(patch)[:8]}'
    if (dst / '.take_release-ia').is_file():
        return None, dst
    _archive(ctx, commit, dst)
    res, _ = major_check(ctx, dst, f'the 32-bit source {commit[:9]} (-ia)')
    if res:
        return res, None
    r = ctx.run(ctx.tool('git') + ['apply', '--whitespace=nowarn', patch], name='ia-apply', cwd=dst, heavy=False)
    if r.rc != 0:
        return StageResult.stop(
            f'the instruments patch {patch.name} does not apply to {commit[:9]} ({r.log})',
            'resolve it as one-off 9045 did (research/03-dll-engine-boundary/upstream-fix-batch-1.md 8b, '
            'tools/d10/evidence/9045/scripts/s06_ia_resolve.py), then run again with --ia-patch <the resolved patch>',
            cl_step='3'), None
    if tag:
        why = inject_x86_define(dst, major)
        if why:
            return StageResult.failed(f'the define cannot be given to the -ia build: {why}'), None
    stub = f'{describe}+ia{ident}{"+" + tag if tag else ""} Copy'
    write_version_stub(dst, stub)
    (dst / '.take_release-ia').write_text(f'{commit} {patch} {stub}\n')
    return None, dst


def build_composite(ctx, out: Path):
    """shell/build.cmd into out (C5_SHELL_BUILD), under quietlock.py run (issue 0464). Returns (StageResult or None,
    record). The compositor must be a 32-bit image (pack.py's check)."""
    _common.fresh_dir(out)
    env = dict(os.environ, C5_SHELL_BUILD=str(out).replace('/', '\\'))
    r = ctx.run(_common.tool_cmd(ctx, 'build_shell', BUILD_SHELL), name='shell', env=env, cwd=REPO,
                what=f'{ctx.who} take_release {ctx.tag} compositor build (shell/build.cmd)')
    dll = out / 'bin' / COMPOSITE
    if r.rc != 0:
        return StageResult.failed(f'shell/build.cmd exit {r.rc}; {r.log}\n{r.tail(10)}'), None
    if not dll.is_file():
        return StageResult.failed(f'shell/build.cmd exit 0 but {dll} is missing'), None
    pk = release_pack()
    machine = pk.pe_machine(pk.head(dll))
    if machine != 0x14C:
        return StageResult.failed(f'{dll} is not a 32-bit PE image (machine {machine!r})'), None
    pdb = out / 'bin' / COMPOSITE_PDB
    return None, {'out': str(out), 'dll': str(dll), 'pdb': str(pdb) if pdb.is_file() else None,
                  'seconds': round(r.seconds), 'sha256': sha256_file(dll).upper()}


def layout_composite(dst: Path, comp) -> list:
    """The increment 2 shell beside the proxy in the x64 layout dst (issue 0464): the compositor, its PDB, a
    shell_composite.sha256 naming the laid-out proxy and compositor, and MinHook's licence. Returns the files to hash."""
    pb = dst / 'proxy' / 'bin'
    shutil.copy2(comp['dll'], pb / COMPOSITE)
    if comp.get('pdb') and Path(comp['pdb']).is_file():
        shutil.copy2(comp['pdb'], pb / COMPOSITE_PDB)
    text = shell_manifest().manifest_text(sha256_file(pb / DLL), sha256_file(pb / COMPOSITE),
                                          'take_release build stage, issue 0464')
    (pb / COMPOSITE_SHA).write_bytes(text.encode('ascii'))
    shutil.copyfile(COMPOSITE_INI_SRC, pb / COMPOSITE_INI)
    shutil.copyfile(MINHOOK_LICENSE, pb / MINHOOK_NOTICE)
    return [f'proxy/bin/{COMPOSITE}', f'proxy/bin/{COMPOSITE_SHA}', f'proxy/bin/{COMPOSITE_INI}',
            f'proxy/bin/{MINHOOK_NOTICE}']


def layout_i0(ctx, rec, name, readme, extra=None):
    dst = _common.fresh_dir(ctx.work / 'layout' / name)
    dst.mkdir(parents=True)
    shutil.copy2(rec['dll'], dst / DLL)
    if Path(rec['pdb']).is_file():
        shutil.copy2(rec['pdb'], dst / 'CvGameCore_Expansion2.pdb')
    if extra:
        shutil.copy2(extra, dst / Path(extra).name)
    _common.write_sha_file(dst, [DLL])
    (dst / 'README.txt').write_text(readme + f'Laid out {time.strftime("%Y-%m-%d %H:%M")} by take_release (3502).\n',
                                    encoding='utf-8')
    return dst


def _pins_dlls() -> str:
    """The pins' folder as the layout README names it: <pins_dir>/dlls/ (ticket 4711: the setting pins_dir of
    tools/release/settings.py; the same text as before where it is the workstation's folder), else a description."""
    p = release_settings.get('pins_dir')
    return f'{p.as_posix()}/dlls/' if p is not None else "the MC's pins folder"


def layout_x64(ctx, rec, commit, tools8, dirty, major: int = 22, comp=None):
    """comp (issue 0464): the compositor build's record for an i2 release, laid out beside the proxy; None for thin."""
    out = Path(rec['out'])
    pgo = rec.get('x64_config') == 'release-pgo'   # ticket 0513: the configuration is in the layout's name
    dst = _common.fresh_dir(ctx.work / 'layout' /
                            f'x64-{commit[:8]}{major_suffix(major)}-p{tools8}{PGO_SUFFIX if pgo else ""}')
    shutil.copytree(out / 'bin', dst / 'bin')
    (dst / 'proxy').mkdir()
    shutil.copytree(out / 'proxy' / 'bin', dst / 'proxy' / 'bin')
    bp = out / 'proxy' / 'build-proxy.json'
    if bp.is_file():
        shutil.copy2(bp, dst / 'proxy' / 'build-proxy.json')
    rels = sorted(f'bin/{p.name}' for p in (dst / 'bin').iterdir() if p.suffix.lower() in ('.dll', '.exe'))
    rels.append(f'proxy/bin/{DLL}')
    if comp:
        rels += layout_composite(dst, comp)
    _common.write_sha_file(dst, rels)
    (dst / 'README.txt').write_text(
        f'x64 candidate layout of {ctx.tag} written by tools/release/take_release.py (issue 3502), '
        f'{time.strftime("%Y-%m-%d %H:%M")}.\nFork commit {commit}; server and proxy from this repository\'s commit '
        f'{tools8} (repo_dirty={int(dirty)}).\nBuilt with server/build_server.py --src {rec["src"]} --out {out} '
        f'--config release --offline{" --unlocked" if rec["unlocked"] else ""}; build_all: {", ".join(rec["steps"])}; '
        f'unresolved {rec["unresolved"]}; narrowing {rec["narrowing_sites"]} gate {rec["narrowing_gate"]}.\n'
        + (f'Major civilizations: {major} (tools/x64-dll/build_all.py --major-civs {major}, the define on the DLL '
           f'compile only; smoke: {rec.get("smoke_major_civs")}).\n' if major != x64build().MAJOR_CIVS_DEFAULT else '')
        + (f'x64 DLL configuration release-pgo (ticket 0513, D184): tools/x64-dll/build_all.py --config release-pgo '
           f'with the locked PGO profile {rec.get("profile_sha256")} (deps/dll.lock profile, trained on '
           f'{str(rec.get("profile_pin"))[:9]}); the DLL\'s .vppgo section names it.\n' if pgo else '')
        + (f'Shell configuration i2 (issue 0464): proxy/bin/{COMPOSITE} ({comp["sha256"]}) built by shell/build.cmd '
           f'into {comp["out"]} from this repository\'s commit {tools8}; {COMPOSITE_SHA} names it and the proxy; '
           f'{COMPOSITE_INI} is shell/composite/{COMPOSITE_INI}; '
           f'{MINHOOK_NOTICE} is the licence of MinHook, linked into it.\n' if comp else '')
        + f'Not a pin: the MC lays out pins under {_pins_dlls()}.\n', encoding='utf-8')
    return dst, _common.hash_files(dst, rels)


def run(ctx):
    res, shell = _common.shell_config_of(ctx)   # issue 0464: 'i2' builds and lays out the compositor
    if res:
        return res
    lk = ctx.outputs('lock')
    x64c, i0c = lk['x64']['commit'], lk['i0']['commit']
    prev = ctx.previous_outputs()
    head, dirty = _tools_commit(ctx)
    tools8 = head[:7]
    srcroot = ctx.dir('src')
    major = major_of(ctx)          # issue 4252: 22 unless --major-civs
    msfx = major_suffix(major)     # '' for 22, '-m43'

    def same_major(r):
        return int(r.get('major_civs', 22)) == major

    # ticket 0513: a release-pgo take checks its locked profile before anything is built
    res, plan = pgo_plan(ctx, x64c)
    if res:
        return res

    def same_x64_config(r):
        if r.get('x64_config', 'release') != (plan['config'] if plan else 'release'):
            return False
        return not plan or r.get('profile_sha256') == plan['sha256']

    # 1. x64 (about 10 min with the full-LTO link)
    rec = prev.get('x64_build')
    # 4252: a server built at another repository commit is built again (records before 4252 have no tools_commit)
    same_tools = rec and rec.get('tools_commit', head) == head and rec.get('tools_dirty', dirty) == dirty
    if not (rec and same_tools and rec.get('commit') == x64c and same_major(rec) and same_x64_config(rec) and
            Path(rec['out'], 'bin', DLL).is_file()):
        if lk.get('fingerprint_matches_pin'):
            src, unlocked = _archive(ctx, x64c, srcroot / f'x64-{x64c[:8]}'), False
        else:
            src, unlocked = _worktree(ctx, x64c, srcroot / f'x64wt-{x64c[:8]}'), True
        res, _ = major_check(ctx, src, f'the x64 source {x64c[:9]}')
        if res:
            return res
        res, rec = build_x64(ctx, x64c, src, unlocked, ctx.work / 'build' / f'x64-{x64c[:8]}{msfx}', major)
        if res:
            return res
        rec.update(tools_commit=head, tools_dirty=dirty)
        ctx.partial(x64_build=rec)
    # 1b. the compositor of an i2 release (issue 0464), from the repository commit the server and the proxy come from
    comp = None
    if shell in COMPOSITE_SHELLS:
        comp = prev.get('composite_build')
        if not (comp and comp.get('tools_commit') == head and comp.get('tools_dirty') == dirty and
                Path(comp['dll']).is_file()):
            res, comp = build_composite(ctx, ctx.work / 'build' / f'shell-{tools8}')
            if res:
                return res
            comp.update(tools_commit=head, tools_dirty=dirty)
            ctx.partial(composite_build=comp)
    # 2. 32-bit plain (about 5 min)
    r86 = prev.get('i0_build')
    if not (r86 and r86.get('commit') == i0c and same_major(r86) and Path(r86['dll']).is_file()):
        if major == x64build().MAJOR_CIVS_DEFAULT:
            wt = _worktree(ctx, i0c, srcroot / f'i0wt-{i0c[:8]}')
            res, _ = major_check(ctx, wt, f'the 32-bit source {i0c[:9]}')
        else:
            res, wt = prepare_i0_major(ctx, i0c, lk['i0']['describe'], major)
        if res:
            return res
        res, r86 = build_i0(ctx, wt, 'i0')
        if res:
            return res
        r86['commit'] = i0c
        if major != x64build().MAJOR_CIVS_DEFAULT:
            r86['major_civs'] = major
        keep = ctx.dir('build', f'i0-{i0c[:8]}{msfx}')
        for k in ('dll', 'pdb'):
            if Path(r86[k]).is_file():
                shutil.copy2(r86[k], keep / Path(r86[k]).name)
                r86[k] = str(keep / Path(r86[k]).name)
        ctx.partial(i0_build=r86)
    # 3. 32-bit instance-aware (about 5 min)
    ria = prev.get('i0ia_build')
    want_ia = ctx.config.get('ia', True)  # --skip-ia: no -ia build (D110: no new source copy on D: without the MC)
    patch, ia_choice = pick_ia_patch(ctx, i0c) if want_ia else (None, 'not built (--skip-ia)')
    if want_ia and patch is None:
        return StageResult.stop(f'no instruments patch for the -ia build: {ia_choice}',
                                'resolve the instruments patch onto the i0 branch as one-off 3515 did '
                                '(tools/d10/evidence/3515/scripts/s08_ia_resolve.py), then run again with --ia-patch '
                                '<the resolved patch> (or --skip-ia)', cl_step='3')
    if want_ia:
        ctx.say(f'-ia instruments patch: {patch.name} ({ia_choice})')
    if want_ia and not (ria and ria.get('commit') == i0c and ria.get('patch_sha256') == sha256_file(patch) and
                        same_major(ria) and Path(ria['dll']).is_file()):
        res, src = prepare_ia(ctx, i0c, lk['i0']['describe'], patch, major)
        if res:
            return res
        res, ria = build_i0(ctx, src, 'i0ia')
        if res:
            return res
        ria.update(commit=i0c, patch=str(patch), patch_sha256=sha256_file(patch), patch_choice=ia_choice)
        if major != x64build().MAJOR_CIVS_DEFAULT:
            ria['major_civs'] = major
        ctx.partial(i0ia_build=ria)
    # 4. layout. Issue 4252 (2026-11-15): the layout's name and README name the repository commit read at the start;
    # server/build_server.py records the one it saw itself (its build id). If the repository moved or got dirty
    # meanwhile, the layout would name a build it does not contain: fail instead (a rerun builds again at one commit).
    head2, dirty2 = _tools_commit(ctx)
    if (head2, dirty2) != (head, dirty) and not ctx.config.get('allow_tools_move'):
        return StageResult.failed(f'this repository moved during the build stage ({head[:9]} dirty={int(dirty)} -> '
                                  f'{head2[:9]} dirty={int(dirty2)}): the layout would name a commit its server.exe and '
                                  'proxy were not built from. Keep the repository still during a run, then run again '
                                  '(--from build)')
    xdir, xhashes = layout_x64(ctx, rec, x64c, tools8, dirty, major, comp)
    if comp:   # issue 0464: the layout passes pack.py's i2 check here, not first at the pack stage
        pk = release_pack()
        try:
            pk.check_shell_config(xdir, 'i2')
        except pk.PackError as e:
            return StageResult.failed(f'the i2 layout {xdir} fails pack.py\'s check: {e}')
    mnote = '' if not msfx else (f'Major civilizations: {major} (MOD_GLOBAL_MAX_MAJOR_CIVS={major} given to '
                                 'build_vp_clang.py\'s predefines in the source copy; the DLL source is the commit\'s).\n')
    i0dir = layout_i0(ctx, r86, f'i0ship-{i0c[:8]}{msfx}',
                      (f'32-bit DLL of {ctx.tag}: fork commit {i0c} built in the detached worktree {r86["src"]}; '
                       if not msfx else f'32-bit DLL of {ctx.tag}: git archive of fork commit {i0c} in {r86["src"]}; ')
                      + 'build_vp_clang.py --config release. Not repeatable (__FILE__, __DATE__, link time).\n' + mnote)
    iadir = None
    if want_ia:
        iadir = layout_i0(ctx, ria, f'i0ship-{i0c[:8]}{msfx}-ia',
                          f'Instance-aware 32-bit DLL of {ctx.tag}: git archive of {i0c} + {patch.name}; '
                          f'source {ria["src"]}; build_vp_clang.py --config release.\n' + mnote, extra=patch)
    # the candidate locks get the build's keys
    d = ctx.dir('deps')
    built = time.strftime('%Y-%m-%d %H:%M')
    tag_commit = ctx.outputs('survey')['tag_commit']
    (d / 'dll.lock').write_text(lock_text('dll.lock', {**lk['x64'], 'tag': ctx.tag, 'tag_commit': tag_commit}, {
        'tools': f'c5-64bit-patch {tools8} (repo_dirty={int(dirty)})',
        major_key('verified', major): f'{built} take_release build stage: build_all {", ".join(rec["steps"])}; '
                                      f'unresolved {rec["unresolved"]}; narrowing {rec["narrowing_sites"]} gate '
                                      f'{rec["narrowing_gate"]}'
                                      + (f'; --major-civs {major}, smoke {rec.get("smoke_major_civs")}' if msfx else '')
                                      + (f'; release-pgo, profile {rec.get("profile_sha256")}'
                                         if rec.get('x64_config') == 'release-pgo' else ''),
        major_key('release', major): f'{xdir} (DLL {xhashes["bin/" + DLL]["sha256"]}, server.exe '
                                     f'{xhashes.get("bin/server.exe", {}).get("sha256")}, proxy '
                                     f'{xhashes["proxy/bin/" + DLL]["sha256"]}'
                                     + (f', compositor {xhashes["proxy/bin/" + COMPOSITE]["sha256"]}' if comp else '')
                                     + ')'}),
        encoding='utf-8')
    (d / 'i0.lock').write_text(lock_text('i0.lock', {**lk['i0'], 'tag': ctx.tag, 'tag_commit': tag_commit}, {
        major_key('release', major): f'SHA-256 {r86["dll_sha256"]} ({i0dir})',
        major_key('instance', major): f'SHA-256 {ria["dll_sha256"]} ({iadir})' if want_ia else 'not built (--skip-ia)'}),
        encoding='utf-8')
    return StageResult.ok(
        x64_commit=x64c, i0_commit=i0c, tools_commit=head, tools_dirty=dirty,
        x64_config=rec.get('config') or built_config(Path(rec['out'])),
        x64_repeat_check=rec.get('repeat_check') or repeat_check(Path(rec['out'])),
        x64_out=rec['out'], x64_src=rec['src'], x64_layout=str(xdir), x64_bin=str(xdir / 'bin'),
        proxy_dll=str(xdir / 'proxy' / 'bin' / DLL), x64_hashes=xhashes, build_all=rec['build_all'],
        build_steps=rec['steps'], unresolved=rec['unresolved'], narrowing_sites=rec['narrowing_sites'],
        narrowing_gate=rec['narrowing_gate'],
        i0_layout=str(i0dir), i0_dll=str(i0dir / DLL), i0_dll_sha256=r86['dll_sha256'],
        i0ia_layout=str(iadir) if iadir else None, i0ia_dll=str(iadir / DLL) if iadir else None,
        i0ia_dll_sha256=ria['dll_sha256'] if iadir else None,
        ia_patch=str(patch) if iadir else None, ia_patch_choice=ia_choice,
        i0_src=r86['src'], **({'major_civs': major} if msfx else {}),
        **({'pgo_profile_sha256': rec.get('profile_sha256'), 'pgo_profile_pin': rec.get('profile_pin')}
           if rec.get('x64_config') == 'release-pgo' else {}),   # ticket 0513
        **({'shell_config': shell, 'composite_dll': str(xdir / 'proxy' / 'bin' / COMPOSITE),
            'composite_sha256': xhashes['proxy/bin/' + COMPOSITE]['sha256'], 'composite_build': comp['out']}
           if comp else {}))
