"""A/B of the .Civ5Map reader (ticket 0343) and writer (ticket 0326) against the 32-bit closed library.

    python -I run_ab.py --out DIR [--src FORK] [--unlocked] [--db GAMEPLAY.db] [--config debug|release]
                        [--maps ROOT_OR_FILE ...] [--no-default-maps] [--skip-build] [--asan]
                        [--steam-install GAME_DIR] [--allow-unknown-exe-build]

The default maps are read from the Steam install only while its DX11 EXE is Steam build 4390913 (ticket 0583, D200;
tools/harness/exe_build.py): after Firaxis' 2026-11-19 update pass --steam-install with the golden pre-update copy.

Steps (all output under DIR; the map files and the database are only read):
 1. typedesc.py: the game's type lists from the gameplay database (default: the release descriptor's test_db);
 2. build three wbdump executables:
      closed-x86  wbdump.cpp + the fork's CvWorldBuilderMapWin32.obj + FireWorksWin32.obj   (the reference)
      ours-x86    wbdump.cpp + closedlib-stubs/CvWorldBuilderMap/*.cpp + FireWorksWin32.obj
      ours-x64    wbdump.cpp + build_stubs.py's CvWorldBuilderMap.lib and FireWorks.lib
    (x86 in the VS 2008 environment with the DLL's flags and its PCH's _SECURE_SCL=0, x64 as build_stubs.py);
 3. run each over every map: by default every .Civ5Map under the Steam install and the Hormigas map pack and
    Play The World Extended folders of the MODS folder; plus negative inputs written to DIR/neg (a missing file,
    truncations of the largest map, an unknown version);
 4. three passes (the game's type lists with the scenario read: what the DLL does; bScenario=false; empty game
    lists, so every type is translated to "invalid"), each comparing closed-x86 with ours-x86 and with ours-x64 line
    by line per map (every section is a length and an FNV-1a hash of a canonical image: wbdump.cpp; for a
    difference, rerun one map with `wbdump.exe TYPES --full MAP` on each side). One explained difference: in
    version 9 the reference reads players x 256 bytes of the visibility from past the end of its block (heap
    contents, different on every run); there only the part from the file (visibility-v9head) is compared.
    The writer (ticket 0326): each executable also saves every loaded map in five cases (wbdump.cpp SaveCase: the
    re-save with and without the scenario part, after SwapPlayers + ClearScenarioData, the DLL's Save Map sequence,
    a smaller map) into DIR/saves/<pass>-<executable>/, and every file is compared byte for byte with the
    reference's (compare_saves; explained: the version 9 visibility tail above and, in version 6-11 scenarios, the
    allocator bytes the 32-bit conversion leaves undefined, legacy_undefined); the writer's
    refusals (a missing folder, an empty and a NULL path: false and no file) are checked in every executable.
    Writes DIR/ab-report.json and DIR/ab-summary.txt.
 5. --asan (COMMON rule 9): a fourth executable, ours-x64-asan (the reader, the dump and the stub logger compiled with
    MSVC cl /fsanitize=address /Od, the FireWorks library as built), run on the first pass with a 2 GB job memory cap and
    compared like the others; an AddressSanitizer report ends it non-zero and fails the run.
Exit 0: every map compared, no difference, every reference load of a real map succeeded, every negative input
refused by both, every re-saved file byte-identical (or explained) and every refusal of the writer as expected; 1: a
difference or a failed load or save; 2: a setup or build error.
"""
import argparse
import hashlib
import json
import os
import struct
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
STUBS = REPO / 'tools' / 'x64-dll' / 'closedlib-stubs'
sys.path.insert(0, str(STUBS))
import build_stubs as bs  # noqa: E402

sys.path.insert(0, str(REPO / 'tools' / 'release'))
import descriptor  # noqa: E402

STEAM = Path(r"C:\Program Files (x86)\Steam\steamapps\common\Sid Meier's Civilization V")
MODS = Path.home() / 'Documents' / 'My Games' / "Sid Meier's Civilization 5" / 'MODS'
MOD_MAPS = ['Hormigas Enhanced Map Pack (43 Civs) (v 1)', 'Play The World Extended (Brave New World Edition) (v 3)']
VS2008 = Path(r'C:\Program Files (x86)\Microsoft Visual Studio 9.0\Common7\Tools\vsvars32.bat')
X86_DEFINES = ['FXS_IS_DLL', 'WIN32', '_WINDOWS', '_USRDLL', 'EXTERNAL_PAUSING', 'CVGAMECOREDLL_EXPORTS',
               'FINAL_RELEASE', '_CRT_SECURE_NO_WARNINGS', '_WINDLL', 'NDEBUG', 'STRONG_ASSUMPTIONS',
               'VPRELEASE_ERRORMSG', '_SECURE_SCL=0', '_HAS_ITERATOR_DEBUGGING=0']
X86_INCLUDES = ['CvGameCoreDLL_Expansion2', r'CvWorldBuilderMap\include', r'CvGameCoreDLLUtil\include',
                r'CvLocalization\include', r'CvGameDatabase\include', r'FirePlace\include',
                r'FirePlace\include\FireWorks', r'ThirdPartyLibs\Lua51\include']
VC90_MANIFEST = ("/manifestdependency:type='win32' name='Microsoft.VC90.CRT' version='9.0.21022.8' "
                 "processorArchitecture='x86' publicKeyToken='1fc8b3b9a1e18e3b'")


# The EXE exports the FireWorks code resolves by name (wbhost_exports.cpp defines them as WbX_<name>).
EXPORTS = ['FJobSetAddJobImpl', 'FJobSetInitImpl', 'FJobSetUninitImpl', 'FJobSetWaitImpl', 'FSBAAllocate',
           'FSBADeallocate', 'FSBADisable', 'FSBAGetMaxAllocatorSize', 'FSBAHasAllocatorForSize', 'FSBAIsDisabled',
           'FSBAIsSmallBlock', 'FSBAPreventCreation', 'FStringA_GetStringInitData', 'FStringW_GetStringInitData',
           'GetFileSystemMgr', 'GetLogFileMgr', 'GetMemTrackerForDLL', 'GetSmallObjectAllocator',
           'GetSmallObjectAllocatorInitParams', 'GetTempHeap', 'PeekLogFileMgr']
EXPORT_ARGS = ['/EXPORT:%s=WbX_%s' % (n, n) for n in EXPORTS]


class Log:
    def __init__(self, path):
        self.f = open(path, 'w', encoding='utf-8')

    def __call__(self, text=''):
        print(text)
        self.f.write(text + '\n')
        self.f.flush()


def run(cmd, env=None, cwd=None, timeout=None):
    r = subprocess.run(cmd, env=env, cwd=cwd, capture_output=True, text=True, errors='replace', timeout=timeout)
    return r.returncode, (r.stdout + r.stderr).strip()


def vs2008_env():
    out = subprocess.run('cmd /d /s /c ""%s" >NUL && set"' % VS2008, capture_output=True, text=True,
                         errors='replace', env=dict(os.environ, VSCMD_SKIP_SENDTELEMETRY='1')).stdout
    env = dict(os.environ)
    for line in out.splitlines():
        if '=' in line:
            k, v = line.split('=', 1)
            env[k] = v
    return env


def build(a, out, log):
    exes = {}
    # x64: the stub libraries, then the dump.
    libs = out / 'stubs-x64'
    rc, text = run([sys.executable, '-I', str(STUBS / 'build_stubs.py'), '--out', str(libs), '--config', a.config,
                    '--src', str(a.src), '--groups', 'CvWorldBuilderMap,FireWorks', '--offline']
                   + (['--unlocked'] if a.unlocked else []))
    log('build_stubs x64 (CvWorldBuilderMap, FireWorks): exit %d' % rc)
    if rc != 0:
        log(text[-4000:])
        return None
    env64 = bs.vcvars_env()
    args64 = [x for x in bs.cpp_args(a.src, a.config, 'MD') if x not in ('/Zl', '/WX')]
    args64 += ['/D_ALLOW_KEYWORD_MACROS', '/I' + str(STUBS / 'FireWorks'), '/I' + str(STUBS / 'common')]
    objs64 = []
    for src in ('wbdump.cpp', 'wbhost_exports.cpp', 'wbhost_fs.cpp'):
        obj = out / 'obj-x64' / (Path(src).stem + '.obj')
        obj.parent.mkdir(parents=True, exist_ok=True)
        rc, text = run([str(bs.CLANG_CL)] + args64 + ['/c', str(HERE / src), '/Fo' + str(obj)], env=env64)
        if rc != 0:
            log('compile x64 %s: exit %d\n%s' % (src, rc, text[-4000:]))
            return None
        objs64.append(str(obj))
    exe = out / 'ours-x64' / 'wbdump.exe'
    exe.parent.mkdir(parents=True, exist_ok=True)
    rc, text = run([str(bs.LLD_LINK), '/nologo', '/machine:x64', '/subsystem:console', '/debug', '/out:' + str(exe)]
                   + EXPORT_ARGS + objs64 + [str(libs / 'CvWorldBuilderMap.lib'), str(libs / 'FireWorks.lib'),
                               'kernel32.lib', 'user32.lib', 'winmm.lib'], env=env64)
    if rc != 0:
        log('link x64: exit %d\n%s' % (rc, text[-4000:]))
        return None
    exes['ours-x64'] = exe
    log('linked %s' % exe)
    if a.asan:
        # MSVC cl /fsanitize=address (COMMON rule 9; clang 19's own runtime cannot intercept this machine's ucrtbase).
        asan_objs = []
        cl_args = ['/nologo', '/c', '/MD', '/EHsc', '/Zc:wchar_t', '/Z7', '/Od', '/std:c++14', '/W1',
                   '/fsanitize=address', '/D_DISABLE_STRING_ANNOTATION', '/D_DISABLE_VECTOR_ANNOTATION',
                   '/D_ALLOW_KEYWORD_MACROS', '/wd4005', '/wd4244', '/wd4267', '/wd4996']
        cl_args += [x for x in args64 if x.startswith(('/D', '/I'))]
        cl_args += ['/I' + str(a.src / d) for d in bs.INCLUDE_DIRS]
        tools = Path(env64['VCToolsInstallDir'])
        hostbin = tools / 'bin' / 'Hostx64' / 'x64'
        srcs = [HERE / 'wbdump.cpp', HERE / 'wbhost_exports.cpp', HERE / 'wbhost_fs.cpp', STUBS / 'common' / 'stublog.cpp']
        srcs += sorted((STUBS / 'CvWorldBuilderMap').glob('*.cpp'))
        for src in srcs:
            obj = out / 'obj-x64-asan' / (src.stem + '.obj')
            obj.parent.mkdir(parents=True, exist_ok=True)
            rc, text = run([str(hostbin / 'cl.exe')] + cl_args + [str(src), '/Fo' + str(obj)], env=env64)
            if rc != 0:
                log('compile x64 asan %s: exit %d\n%s' % (src.name, rc, text[-4000:]))
                return None
            asan_objs.append(str(obj))
        exe = out / 'ours-x64-asan' / 'wbdump.exe'
        exe.parent.mkdir(parents=True, exist_ok=True)
        rc, text = run([str(hostbin / 'link.exe'), '/nologo', '/machine:x64', '/subsystem:console', '/debug', '/out:' + str(exe)]
                       + EXPORT_ARGS + asan_objs + [str(libs / 'FireWorks.lib'), 'kernel32.lib', 'user32.lib',
                                                    'winmm.lib'], env=env64)
        if rc != 0:
            log('link x64 asan: exit %d\n%s' % (rc, text[-4000:]))
            return None
        import shutil
        shutil.copy2(tools / 'bin' / 'Hostx64' / 'x64' / 'clang_rt.asan_dynamic-x86_64.dll',
                     exe.parent / 'clang_rt.asan_dynamic-x86_64.dll')
        exes['ours-x64-asan'] = exe
        log('linked %s (AddressSanitizer)' % exe)

    # x86: VS 2008 headers and libraries, the DLL's flags.
    env86 = vs2008_env()
    args86 = ['-m32', '/nologo', '/c', '/MD', '/EHsc', '/Zc:wchar_t', '/O2', '-msse3', '-Wno-everything', '/Z7']
    args86 += ['/D' + d for d in X86_DEFINES] + ['/I' + str(a.src / i) for i in X86_INCLUDES]
    args86 += ['/I' + str(STUBS / 'FireWorks'), '/I' + str(STUBS / 'common')]
    common = []
    # clang.cpp of the fork: the _ftol3/_ltod3 helpers the closed objects call (as the DLL's own x86 build links).
    for src in (HERE / 'wbdump.cpp', HERE / 'wbhost_exports.cpp', HERE / 'wbhost_fs.cpp', a.src / 'clang.cpp'):
        obj = out / 'obj-x86' / (src.stem + '.obj')
        obj.parent.mkdir(parents=True, exist_ok=True)
        rc, text = run([str(bs.CLANG_CL)] + args86 + [str(src), '/Fo' + str(obj)], env=env86)
        if rc != 0:
            log('compile x86 %s: exit %d\n%s' % (src.name, rc, text[-4000:]))
            return None
        common.append(str(obj))
    ours = []
    for src in sorted((STUBS / 'CvWorldBuilderMap').glob('*.cpp')) + [HERE / 'wbhost_stublog.cpp']:
        obj = out / 'obj-x86' / ('wb_' + src.stem + '.obj')
        rc, text = run([str(bs.CLANG_CL)] + args86 + [str(src), '/Fo' + str(obj)], env=env86)
        if rc != 0:
            log('compile x86 %s: exit %d\n%s' % (src.name, rc, text[-4000:]))
            return None
        ours.append(str(obj))
    fireworks = str(a.src / 'FirePlace' / 'lib' / 'FireWorksWin32.obj')
    closed = str(a.src / 'CvWorldBuilderMap' / 'lib' / 'CvWorldBuilderMapWin32.obj')
    for who, extra in (('closed-x86', [closed]), ('ours-x86', ours)):
        exe = out / who / 'wbdump.exe'
        exe.parent.mkdir(parents=True, exist_ok=True)
        rc, text = run([str(bs.LLD_LINK), '/nologo', '/machine:x86', '/subsystem:console', '/debug',
                        '/safeseh:no', '/out:' + str(exe), '/manifest:embed', VC90_MANIFEST] + EXPORT_ARGS + common + extra +
                       [fireworks, 'msvcrt.lib', 'msvcprt.lib', 'kernel32.lib', 'user32.lib', 'winmm.lib',
                        'advapi32.lib'], env=env86)
        if rc != 0:
            log('link %s: exit %d\n%s' % (who, rc, text[-4000:]))
            return None
        exes[who] = exe
        log('linked %s' % exe)
    return exes


def steam_maps_refusal(a):
    """Ticket 0583 (D200): the default maps come from the Steam install (--steam-install, default STEAM); None when its
    DX11 EXE is Steam build 4390913 (or --allow-unknown-exe-build, or --no-default-maps), else why not."""
    if a.no_default_maps:
        return None
    sys.path.insert(0, str(REPO))
    try:
        from tools.harness import exe_build as eb
    finally:
        sys.path.pop(0)
    try:
        eb.check_install(a.steam_install, allow=a.allow_unknown_exe_build, what='the Steam install of the default maps')
    except eb.UnknownExeBuild as e:
        return str(e)
    return None


def find_maps(a):
    roots = list(a.maps or [])
    if not a.no_default_maps:
        roots += [str(a.steam_install)] + [str(MODS / m) for m in MOD_MAPS]
    maps = []
    for r in roots:
        p = Path(r)
        if p.is_file():
            maps.append(p)
        elif p.is_dir():
            maps += sorted(x for x in p.rglob('*') if x.suffix.lower() == '.civ5map')
    seen, out = set(), []
    for m in maps:
        k = str(m.resolve()).lower()
        if k not in seen:
            seen.add(k)
            out.append(m)
    return out


def negatives(maps, out):
    """Inputs every reader must refuse: a missing file, truncations of the largest scenario map, version 13."""
    neg = out / 'neg'
    neg.mkdir(parents=True, exist_ok=True)
    items = [neg / 'does-not-exist.Civ5Map']
    big = max(maps, key=lambda m: m.stat().st_size)
    data = big.read_bytes()
    for cut in (0, 5, 9, 10, 41, 300, len(data) // 2, len(data) - 1):
        p = neg / ('cut-%07d.Civ5Map' % cut)
        p.write_bytes(data[:cut])
        items.append(p)
    p = neg / 'version13.Civ5Map'
    p.write_bytes(bytes([0x8D]) + data[1:])
    items.append(p)
    p = neg / 'oversize.Civ5Map'
    p.write_bytes(data[:1] + struct.pack('<II', 200, 200) + data[9:])
    items.append(p)
    return items, big


def job_capped(proc, limit_mb):
    """Puts a started process in a job object whose committed memory is capped at limit_mb (the sanitizer run)."""
    import ctypes
    from ctypes import wintypes as wt
    k32 = ctypes.WinDLL('kernel32', use_last_error=True)

    class IO(ctypes.Structure):
        _fields_ = [(n, ctypes.c_ulonglong) for n in ('r', 'w', 'o', 'rb', 'wb', 'ob')]

    class BASIC(ctypes.Structure):
        _fields_ = [('PerProcessUserTimeLimit', ctypes.c_longlong), ('PerJobUserTimeLimit', ctypes.c_longlong),
                    ('LimitFlags', wt.DWORD), ('MinimumWorkingSetSize', ctypes.c_size_t),
                    ('MaximumWorkingSetSize', ctypes.c_size_t), ('ActiveProcessLimit', wt.DWORD),
                    ('Affinity', ctypes.c_size_t), ('PriorityClass', wt.DWORD), ('SchedulingClass', wt.DWORD)]

    class EXT(ctypes.Structure):
        _fields_ = [('Basic', BASIC), ('Io', IO), ('ProcessMemoryLimit', ctypes.c_size_t),
                    ('JobMemoryLimit', ctypes.c_size_t), ('PeakProcessMemoryUsed', ctypes.c_size_t),
                    ('PeakJobMemoryUsed', ctypes.c_size_t)]
    k32.CreateJobObjectW.restype = wt.HANDLE
    job = k32.CreateJobObjectW(None, None)
    info = EXT()
    info.Basic.LimitFlags = 0x200 | 0x2000  # JOB_OBJECT_LIMIT_JOB_MEMORY | JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    info.JobMemoryLimit = limit_mb * 1024 * 1024
    ok = k32.SetInformationJobObject(wt.HANDLE(job), 9, ctypes.byref(info), ctypes.sizeof(info))
    ok = ok and k32.AssignProcessToJobObject(wt.HANDLE(job), wt.HANDLE(int(proc._handle)))
    return job if ok else None


def run_dump(exe, types, maps, out_file, full=False, extra=(), cap_mb=None, resave=None):
    lst = out_file.with_suffix('.args')
    cmd = [str(exe), str(types)] + (['--full'] if full else []) + list(extra)
    if resave is not None:
        resave.mkdir(parents=True, exist_ok=True)
        for old in resave.glob('*.Civ5Map'):
            old.unlink()
        cmd += ['--resave', str(resave), '--keep']
    cmd += [str(m) for m in maps]
    t0 = time.time()
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, errors='replace',
                         cwd=str(exe.parent))
    job = job_capped(p, cap_mb) if cap_mb else None
    if cap_mb:
        print('run_ab: %s under a %d MB job memory cap: %s' % (exe.parent.name, cap_mb, 'set' if job else 'NOT set'))
    stdout, stderr = p.communicate(timeout=3600)
    out_file.write_text(stdout, encoding='utf-8')
    lst.write_text('\n'.join(cmd) + '\n', encoding='utf-8')
    if cap_mb and not job:
        return 99, time.time() - t0, 'the job memory cap could not be set'
    return p.returncode, time.time() - t0, stderr


def parse(text):
    """{map path: [lines]} from a dump; the writer's refusal line (before the first map) under SAVENEG_KEY."""
    res, cur = {}, None
    for line in text.splitlines():
        if line.startswith('SAVENEG '):
            res[SAVENEG_KEY] = [line]
        elif line.startswith('MAP '):
            cur = line[4:]
            res[cur] = []
        elif cur is not None and line != 'END':
            res[cur].append(line)
    return res


# ---------------------------------------------------------------------------------------------------------------------
# The writer (ticket 0326): every loaded map is saved by each executable in five cases (wbdump.cpp SaveCase: scen =
# the re-save with the scenario part, map = without it, cleared = after SwapPlayers + ClearScenarioData(true),
# dllflow = the DLL's Save Map sequence, resized = a smaller map); the files are compared byte for byte with the
# reference's. SAVENEG: the writer's refusals (a missing folder, an empty and a NULL path) must answer false and leave
# no file, in every executable.
SAVENEG_KEY = '__SAVENEG__'
SAVENEG_OK = 'SAVENEG missing-folder 0 exists 0; empty-path 0; null-path 0'
SAVE_TAGS = ('scen', 'map', 'cleared', 'dllflow', 'resized')


def written_sections(b):
    """[(name, start, end)] of a file the writer produced (version 12, civ5map-format.md section 8)."""
    out = []
    pos = [0]

    def add(name, length):
        out.append((name, pos[0], pos[0] + length))
        pos[0] += length
    if len(b) < 42:
        return [('short', 0, len(b))]
    w, h = struct.unpack_from('<II', b, 1)
    plots = w * h
    add('header', 10)
    lens = struct.unpack_from('<7I', b, 14)
    add('map-header', 32)
    for name, length in zip(('terrain', 'features', 'wonders', 'resources', 'moddata', 'name', 'description'), lens):
        add(name, length)
    add('worldtype', 68)
    add('plots', plots * 8)
    if b[0] & 0x80 and pos[0] + 128 <= len(b):
        gd = pos[0]
        players, citystates, teams = b[gd + 80], b[gd + 81], b[gd + 82]
        s = struct.unpack_from('<11I', b, gd + 84)
        add('game-description', 84)
        add('scenario-header', 44)
        for name, length in zip(('improvements', 'unittypes', 'techs', 'policies', 'buildings', 'promotions', 'units',
                                 'unitnames', 'cities', 'victorytypes', 'gameoptions'), s):
            add(name, length)
        rel_bits = ((teams - 1) * teams) // 2 if teams else 0
        rel = rel_bits // 8 + (1 if rel_bits % 8 else 0)
        for name in ('contact', 'war', 'permanent', 'openborders', 'defensivepacts'):
            add(name, rel)
        add('influence', players * 256)
        vis_bits = teams * plots
        add('visibility', vis_bits // 8 + (1 if vis_bits % 8 else 0))
        add('teams', teams * 64)
        add('players', players * 436)
        add('citystates', citystates * 436)
        add('plotscenario', plots * 8)
    add('trailing', len(b) - pos[0])
    return out


def diff_sections(a, b):
    """{section: differing byte count} between two written files (sections from the reference's layout)."""
    res = {}
    for name, s, e in written_sections(a):
        x, y = a[s:e], b[s:e]
        n = sum(1 for i in range(min(len(x), len(y))) if x[i] != y[i]) + abs(len(x) - len(y))
        if n:
            res[name] = n
    if len(a) != len(b):
        res['length %d vs %d' % (len(a), len(b))] = abs(len(a) - len(b))
    return res


def v9_tail_only(a, b, map_path):
    """True when two re-saves of a version 9 map differ only in the last players x 256 bytes of the visibility: the
    reference read those bytes from past the end of its block (civ5map-format.md section 3)."""
    try:
        version = Path(map_path).read_bytes()[0] & 0x7F
    except OSError:
        return False
    if version != 9 or len(a) != len(b) or not a[0] & 0x80:
        return False
    secs = {n: (s, e) for n, s, e in written_sections(a)}
    if 'visibility' not in secs or 'game-description' not in secs:
        return False
    players = a[secs['game-description'][0] + 80]
    vs, ve = secs['visibility']
    tail = max(vs, ve - players * 256)
    return all(a[i] == b[i] for i in range(len(a)) if not (tail <= i < ve))


def legacy_undefined(a, map_path):
    """The bytes of a re-save of a version 6-11 scenario that the 32-bit library leaves undefined (civ5map-format.md
    section 9): the converted unit and city allocators are FireMalloc'd and only partly written by the conversion, so
    these bytes are the allocator's earlier contents (0xFF in the A/B host, zero in ours): in each entry on the free
    list (walked from the head while the index is above 0) every byte after the first 4; in every other unit byte 19
    (the padding after the flags); in every other city the name bytes after its NUL. Returns a set of offsets."""
    try:
        version = Path(map_path).read_bytes()[0] & 0x7F
    except OSError:
        return set()
    if version > 11 or not a or not a[0] & 0x80:
        return set()
    secs = {n: (s, e) for n, s, e in written_sections(a)}
    undefined = set()
    for sec, rec in (('units', 84), ('cities', 136)):
        if sec not in secs:
            continue
        s, e = secs[sec]
        if e - s < 4:
            continue
        n = (e - s - 4) // rec
        free = set()
        i = struct.unpack_from('<i', a, s)[0]
        while 0 < i < n and i not in free:
            free.add(i)
            i = struct.unpack_from('<i', a, s + 4 + i * rec)[0]
        for k in range(n):
            base = s + 4 + k * rec
            if k in free:
                undefined.update(range(base + 4, base + rec))
            elif sec == 'units':
                undefined.add(base + 19)
            else:
                name = a[base:base + 64]
                nul = name.find(b'\0')
                if nul >= 0:
                    undefined.update(range(base + nul + 1, base + 64))
    return undefined


def compare_saves(pname, dumps, inputs, out, log):
    """Byte comparison of every re-saved file with the reference's. Returns (failed, report)."""
    failed = False
    report = {}
    ref_lines = dumps['closed-x86']
    for who in [w for w in ('ours-x86', 'ours-x64', 'ours-x64-asan') if w in dumps]:
        cases = equal = 0
        explained = []
        diffs = []
        for idx, m in enumerate(inputs):
            ref = [x for x in ref_lines.get(str(m), []) if x.startswith('SAVE ')]
            oth = [x for x in dumps[who].get(str(m), []) if x.startswith('SAVE ')]
            ref_res = {x.split()[1]: x.split()[2] for x in ref}
            oth_res = {x.split()[1]: x.split()[2] for x in oth}
            for tag in sorted(set(ref_res) | set(oth_res)):
                cases += 1
                fa = out / 'saves' / ('%s-closed-x86' % pname) / ('%04d-%s.Civ5Map' % (idx, tag))
                fb = out / 'saves' / ('%s-%s' % (pname, who)) / ('%04d-%s.Civ5Map' % (idx, tag))
                a = fa.read_bytes() if fa.exists() else None
                b = fb.read_bytes() if fb.exists() else None
                if ref_res.get(tag) != oth_res.get(tag) or (a is None) != (b is None):
                    diffs.append((m, tag, 'result %s/%s, file %s/%s' % (ref_res.get(tag), oth_res.get(tag),
                                                                         a is not None, b is not None)))
                elif a == b:
                    equal += 1
                elif tag == 'scen' and v9_tail_only(a, b, m):
                    explained.append((Path(m).name, tag, 'v9-visibility-tail'))
                else:
                    und = legacy_undefined(a, m) if tag == 'scen' else set()
                    v9 = v9_tail_only(a, bytes(b[i] if i not in und else a[i] for i in range(len(b))), m) \
                        if und and len(a) == len(b) else False
                    if und and len(a) == len(b) and (v9 or all(a[i] == b[i] for i in range(len(a)) if i not in und)):
                        explained.append((Path(m).name, tag, 'legacy-undefined %d bytes%s' % (
                            sum(1 for i in und if a[i] != b[i]), ' + v9-visibility-tail' if v9 else '')))
                    else:
                        diffs.append((m, tag, diff_sections(a, b)))
        neg_ok = dumps[who].get(SAVENEG_KEY) == [SAVENEG_OK]
        key = '%s: re-save closed-x86 vs %s' % (pname, who)
        report[key] = {'cases': cases, 'byte_identical': equal, 'explained': explained, 'differing': len(diffs),
                       'first': [[str(d[0]), d[1], str(d[2])] for d in diffs[:40]],
                       'refusals': dumps[who].get(SAVENEG_KEY)}
        log('%s: %d of %d re-saved files byte-identical, %d explained (undefined bytes of the reference), %d differ; '
            'refusals %s' % (key, equal, cases, len(explained), len(diffs), 'ok' if neg_ok else
                             dumps[who].get(SAVENEG_KEY)))
        for d in diffs[:12]:
            log('  %s %s: %s' % (Path(d[0]).name, d[1], d[2]))
        if diffs or not neg_ok:
            failed = True
    if ref_lines.get(SAVENEG_KEY) != [SAVENEG_OK]:
        log('%s: the reference\'s refusals differ from the expected: %s' % (pname, ref_lines.get(SAVENEG_KEY)))
        failed = True
    # Every loaded map is saved in all five cases by the reference, each with success.
    ref_bad = []
    for m, lines in ref_lines.items():
        if m == SAVENEG_KEY or 'LOAD 1' not in lines:
            continue
        res = {x.split()[1]: x.split()[2] for x in lines if x.startswith('SAVE ')}
        if sorted(res) != sorted(SAVE_TAGS) or any(v != '1' for v in res.values()):
            ref_bad.append((Path(m).name, res))
    if ref_bad:
        log('%s: the reference did not save every loaded map in every case: %s' % (pname, ref_bad[:5]))
        failed = True
    report['%s: reference saves' % pname] = {'maps_loaded': sum(1 for m, l in ref_lines.items()
                                                                if m != SAVENEG_KEY and 'LOAD 1' in l),
                                             'incomplete': len(ref_bad)}
    return failed, report


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--src', type=Path)
    ap.add_argument('--unlocked', action='store_true')
    ap.add_argument('--db', type=Path)
    ap.add_argument('--config', choices=['debug', 'release'], default='debug')
    ap.add_argument('--maps', nargs='*')
    ap.add_argument('--no-default-maps', action='store_true')
    ap.add_argument('--skip-build', action='store_true')
    ap.add_argument('--asan', action='store_true', help='also build and run the x64 reader under AddressSanitizer')
    ap.add_argument('--steam-install', type=Path, default=STEAM, help='the game folder the default maps are read '
                    'from (default the Steam install; the golden pre-update copy works too, ticket 0583)')
    ap.add_argument('--allow-unknown-exe-build', action='store_true', help="read the default maps from an install "
                    "whose DX11 EXE is not Steam build 4390913 (ticket 0583)")
    a = ap.parse_args()
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    log = Log(out / 'run_ab.log')
    why = steam_maps_refusal(a)                     # 0583: before the build, so a refusal costs nothing
    if why:
        log('run_ab: FAILED: %s' % why)
        return 2
    try:
        a.src = Path(bs.x64build.resolve_src(a.src, a.unlocked)['src'])
    except bs.x64build.SrcError as e:
        log('run_ab: FAILED: %s' % e)
        return 2
    db = a.db or Path(descriptor.load().test_db)
    types = out / 'types.txt'
    rc = subprocess.call([sys.executable, '-I', str(HERE / 'typedesc.py'), '--db', str(db), '--out', str(types)])
    if rc != 0:
        log('run_ab: FAILED: typedesc.py exit %d' % rc)
        return 2
    log('types from %s' % db)
    if a.skip_build:
        exes = {w: out / w / 'wbdump.exe' for w in ('closed-x86', 'ours-x86', 'ours-x64')
                + (('ours-x64-asan',) if a.asan else ())}
        if not all(e.exists() for e in exes.values()):
            log('run_ab: FAILED: --skip-build without built executables')
            return 2
    else:
        exes = build(a, out, log)
        if exes is None:
            log('run_ab: FAILED: build')
            return 2
    maps = find_maps(a)
    if not maps:
        log('run_ab: FAILED: no .Civ5Map found')
        return 2
    neg, big = negatives(maps, out)
    log('%d maps (%d bytes, largest %s), %d negative inputs' % (len(maps), sum(m.stat().st_size for m in maps),
                                                                big.name, len(neg)))
    empty = out / 'types-empty.txt'
    empty.write_text('', encoding='utf-8')
    # Pass "game": the game's type lists, scenario data read (what the DLL does; the gate). Pass "noscenario":
    # Load(..., bScenario=false). Pass "notypes": empty game lists, so every type of the file is translated to
    # "invalid". Each pass is compared on its own; every map of every pass must agree.
    passes = [('game', types, []), ('noscenario', types, ['--noscenario']), ('notypes', empty, [])]
    report = {'maps': len(maps), 'negatives': len(neg), 'db': str(db), 'comparisons': {}}
    failed = False
    all_dumps = {}
    for pname, ptypes, pextra in passes:
        dumps = {}
        for who, exe in exes.items():
            if who == 'ours-x64-asan' and pname != 'game':
                continue
            name = 'dump-%s-%s.txt' % (pname, who)
            rc, dt, err = run_dump(exe, ptypes, maps + neg, out / name, extra=pextra,
                                   cap_mb=2048 if who == 'ours-x64-asan' else None,
                                   resave=out / 'saves' / ('%s-%s' % (pname, who)))
            log('run %s %s: exit %d in %.1f s%s' % (pname, who, rc, dt,
                                                    (': ' + err.strip()[-300:]) if err.strip() else ''))
            if rc != 0:
                log('run_ab: FAILED: %s exited %d' % (who, rc))
                return 1
            dumps[who] = parse((out / name).read_text(encoding='utf-8'))
        all_dumps[pname] = dumps
    report['resave'] = {}
    for pname, dumps in all_dumps.items():
        f, rep = compare_pass(pname, dumps, maps, neg, log)
        failed = failed or f
        report['comparisons'].update(rep)
        f, rep = compare_saves(pname, dumps, maps + neg, out, log)
        failed = failed or f
        report['resave'].update(rep)
    ref = all_dumps['game']['closed-x86']
    report['reference_lines'] = sum(len(v) for v in ref.values())
    report['loaded'] = sum(1 for m, lines in ref.items() if 'LOAD 1' in lines)
    (out / 'ab-report.json').write_text(json.dumps(report, indent=1), encoding='utf-8')
    summary = '%s: %d maps + %d negative inputs, 3 passes (game types, no scenario, no game types), reference ' \
              'loads %d of %d; %s' % ('PASS' if not failed else 'FAIL', len(maps), len(neg), report['loaded'],
                                      len(maps), '; '.join('%s: %d differing maps' % (k, v['maps_differing'])
                                                           for k, v in report['comparisons'].items()))
    summary += '\nwriter (re-save, byte for byte): ' + '; '.join(
        '%s: %d of %d identical, %d explained, %d differ' % (k, v['byte_identical'], v['cases'], len(v['explained']),
                                                            v['differing'])
        for k, v in report['resave'].items() if 'cases' in v)
    (out / 'ab-summary.txt').write_text(summary + '\n', encoding='utf-8')
    log(summary)
    return 1 if failed else 0


def compare_pass(pname, dumps, maps, neg, log):
    ref = dumps['closed-x86']
    report = {}
    failed = False
    neg_set = {str(p) for p in neg}
    load_fail = [m for m, lines in ref.items() if m not in neg_set and m != SAVENEG_KEY and 'LOAD 1' not in lines]
    if load_fail:
        failed = True
        log('reference load failed for %d real maps: %s' % (len(load_fail), load_fail[:5]))
    # Every negative input is refused when the scenario is read; without it a file cut inside the scenario part
    # loads (in the reference too), so there only the agreement with the reference counts.
    neg_loaded = [m for m in neg_set for who in dumps if 'LOAD 0' not in dumps[who].get(m, [])]         if pname != 'noscenario' else []
    if neg_loaded:
        failed = True
        log('negative inputs not refused: %s' % sorted(set(neg_loaded)))
    differing_maps = set()
    for who in [w for w in ('ours-x86', 'ours-x64', 'ours-x64-asan') if w in dumps]:
        diffs = []
        explained = {}
        for m, lines in ref.items():
            if m == SAVENEG_KEY:
                continue  # compare_saves
            other = dumps[who].get(m)
            if other is None:
                diffs.append((m, 'missing', ''))
                continue
            # The writer's lines are compared byte for byte by compare_saves.
            lines = [x for x in lines if not x.startswith('SAVE ')]
            other = [x for x in other if not x.startswith('SAVE ')]
            if m in neg_set:
                # A refused input: only the answers count (the object's state after a failed load is not used).
                lines = [x for x in lines if x.startswith(('LOAD', 'PREVIEW'))]
                other = [x for x in other if x.startswith(('LOAD', 'PREVIEW'))]
            v9 = any(x.startswith('visibility-v9head ') for x in lines)
            for i, (x, y) in enumerate(zip(lines, other)):
                if x != y:
                    if v9 and x.startswith(('visibility ', 'visibility-bits ')):
                        # explained: version 9's reference reads players x 256 bytes past its block (EXPLAINED below)
                        explained[x.split(' ', 1)[0]] = explained.get(x.split(' ', 1)[0], 0) + 1
                        continue
                    diffs.append((m, x, y))
            if len(lines) != len(other):
                diffs.append((m, 'lines %d' % len(lines), 'lines %d' % len(other)))
        maps_diff = sorted({d[0] for d in diffs})
        differing_maps |= set(maps_diff)
        sections = {}
        for m, x, y in diffs:
            k = x.split(' ', 1)[0]
            sections[k] = sections.get(k, 0) + 1
        report['%s: closed-x86 vs %s' % (pname, who)] = {'maps_compared': len(ref), 'maps_differing': len(maps_diff),
                                                           'explained': explained,
                                                           'differing_lines': len(diffs), 'by_section': sections,
                                                           'first': [list(d) for d in diffs[:40]]}
        log('%s: closed-x86 vs %s: %d of %d maps differ, %d lines (%s)' % (pname, who, len(maps_diff), len(ref), len(diffs),
                                                                    ', '.join('%s %d' % kv for kv in sorted(sections.items()))))
        for d in diffs[:12]:
            log('  %s\n    ref : %s\n    %s: %s' % (Path(d[0]).name, d[1], who, d[2]))
        if diffs:
            failed = True
    return failed, report


if __name__ == '__main__':
    sys.exit(main())
