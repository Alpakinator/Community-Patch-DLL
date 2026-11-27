"""Offline test of the combat hold (ticket 4773, route S): proxy/src/proxy_combat_hold.cpp + combat_hold_test.cpp against a
fake queue B. Nothing is played; no game is started; nothing of the EXE is read.

    python -I proxy/tests/combat_hold/test_combat_hold.py --scratch <empty or new dir> [--no-asan]
        [--binding-include DIR ... | --no-binding] [-v]

1. core     the core (take, holds, drop, watchdog, checks) built with VP_COMBAT_HOLD_OFFLINE: the proxy's toolchain
            (clang-cl -m32 in the VS 2008 environment, VC9 headers, /MT), run, then run with --expect-fail (the program's
            own failure path: exit 1, one FAIL line, the summary one short).
2. binding  the same with VP_COMBAT_HOLD_BINDING: the binding (TargetOf) against a fake image header and the build table
            (proxy/src/proxy_exe.h and the emitted vp_exe_tables.gen.h, epic 0590 stage 2). The table is emitted into
            the scratch folder by tools/exebuild/emit_header.py when proxy/src/proxy_exe.h is on the branch; before that,
            --binding-include names folders that hold proxy_exe.h and an emitted header (never committed, D202). Without
            either the binding check FAILS; --no-binding leaves it out explicitly (and says so).
3. asan     1 and 2 under MSVC /fsanitize=address (x86), with a canary that must be caught (COMMON rule 9), run under
            shell/tools/memcap.py.
Exit code 0 when every check passes. Outputs only under --scratch. Python 3 standard library only.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(REPO / 'proxy' / 'gen'))
import toolchain as tc  # noqa: E402

SRC = REPO / 'proxy' / 'src'
EXEBUILD_INCLUDE = REPO / 'exebuild' / 'include'
EMIT_HEADER = REPO / 'tools' / 'exebuild' / 'emit_header.py'
VCVARSALL = Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat")
SOURCES = [HERE / 'combat_hold_test.cpp', SRC / 'proxy_combat_hold.cpp']
RESULTS = []
VERBOSE = False


def check(name, ok, detail=''):
    RESULTS.append((name, bool(ok)))
    print(('ok   ' if ok else 'FAIL ') + name + (('  [' + detail + ']') if (detail and (VERBOSE or not ok)) else ''))
    return ok


def run_unit(argv, label, cwd, env=None, expect_fail=False):
    cp = subprocess.run([str(a) for a in argv] + (['--expect-fail'] if expect_fail else []) + (['-v'] if VERBOSE else []),
                        capture_output=True, text=True, errors='replace', cwd=str(cwd), env=env, timeout=600)
    out = cp.stdout + cp.stderr
    m = re.search(r'combat_hold_test: (\d+)/(\d+) checks passed', out)
    if expect_fail:
        return check('%s (negative, --expect-fail): exit code 1, one FAIL line, the summary one short' % label,
                     cp.returncode == 1 and m and int(m.group(2)) - int(m.group(1)) == 1 and out.count('FAIL line') == 1,
                     'exit %d; %s' % (cp.returncode, out[-1500:]))
    for ln in out.splitlines():
        if ln.startswith('FAIL') or ln.startswith('  (') or VERBOSE:
            print('    ' + ln)
    return check('%s: %s' % (label, m.group(0) if m else 'no summary line (exit %d)' % cp.returncode),
                 cp.returncode == 0 and m and m.group(1) == m.group(2) and 'AddressSanitizer' not in out, out[-2000:])


def binding_includes(s, a):
    """The include folders of the binding build, or None (a FAIL check says why)."""
    if a.binding_include:
        dirs = [Path(d).resolve() for d in a.binding_include]
        found = [d for d in dirs if (d / 'proxy_exe.h').is_file()]
        gen = [d for d in dirs if (d / 'vp_exe_tables.gen.h').is_file()]
        if not check('binding: --binding-include holds proxy_exe.h and vp_exe_tables.gen.h', found and gen,
                     ', '.join(str(d) for d in dirs)):
            return None
        return dirs
    if not (SRC / 'proxy_exe.h').is_file():
        check('binding: proxy/src/proxy_exe.h is on this branch (epic 0590 stage 2); else pass --binding-include or '
              '--no-binding', False)
        return None
    d = s / 'gen-exebuild'
    d.mkdir(parents=True, exist_ok=True)
    cp = subprocess.run([sys.executable, '-I', str(EMIT_HEADER), '--out', str(d)], capture_output=True, text=True,
                        errors='replace')
    if not check('binding: the EXE address header emitted (tools/exebuild/emit_header.py, the local tables)',
                 cp.returncode == 0 and (d / 'vp_exe_tables.gen.h').is_file(), (cp.stdout + cp.stderr)[-1500:]):
        return None
    return [d]


def variants(binding_dirs):
    v = [('core', ['VP_COMBAT_HOLD_OFFLINE'], [])]
    if binding_dirs is not None:
        v.append(('binding', ['VP_COMBAT_HOLD_OFFLINE', 'VP_COMBAT_HOLD_BINDING'], binding_dirs))
    return v


def test_proxy_toolchain(s, binding_dirs):
    for label, defs, incs in variants(binding_dirs):
        d = s / ('proxy-' + label)
        d.mkdir(parents=True, exist_ok=True)
        exe = d / 'combat_hold_test.exe'
        cp = tc.run_in_env('x86', [tc.CLANG, '-m32', '/MT', '/O2', '/EHsc', '-Wno-everything', '-fuse-ld=lld'] +
                           ['/D' + x for x in defs] + ['/I' + str(SRC), '/I' + str(EXEBUILD_INCLUDE)] +
                           ['/I' + str(i) for i in incs] + [str(x) for x in SOURCES] + ['/Fe' + str(exe)], cwd=str(d))
        if not check('%s, proxy toolchain: built (clang-cl -m32, VC9 headers)' % label, cp.returncode == 0 and exe.exists(),
                     (cp.stdout + cp.stderr)[-3000:]):
            continue
        run_unit([exe], '%s, proxy toolchain' % label, d)
        run_unit([exe], '%s, proxy toolchain' % label, d, expect_fail=True)


def vcvars_env(arch):
    out = subprocess.run('call "%s" %s >nul && set' % (VCVARSALL, arch), shell=True, capture_output=True, text=True,
                         env=dict(os.environ, VSCMD_SKIP_SENDTELEMETRY='1'))
    if out.returncode:
        return None
    env = {}
    for line in out.stdout.splitlines():
        k, sep, v = line.partition('=')
        if sep and k:
            env[k] = v
    return env


def test_asan(s, binding_dirs):
    env = vcvars_env('x86')
    if not check('asan: vcvarsall x86', env is not None):
        return
    cl = shutil.which('cl', path=env.get('PATH', env.get('Path', '')))
    if not check('asan: cl.exe found', cl is not None):
        return
    d = s / 'asan'
    d.mkdir(parents=True, exist_ok=True)
    (d / 'canary.cpp').write_text('#include <stdio.h>\n#include <stdlib.h>\nint main() { char* volatile p = (char*)malloc(16);'
                                  ' p[16] = 1; printf("canary NOT caught\\n"); free(p); return 0; }\n', encoding='ascii')
    cp = subprocess.run([cl, '/nologo', '/Zi', '/Od', '/fsanitize=address', str(d / 'canary.cpp'), '/Fe' + str(d / 'canary.exe'),
                         '/Fo' + str(d) + '\\', '/Fd' + str(d / 'canary.pdb')], cwd=str(d), env=env, capture_output=True,
                        text=True, errors='replace')
    if not check('asan: canary.exe compiled with MSVC /fsanitize=address', cp.returncode == 0, (cp.stdout + cp.stderr)[-2000:]):
        return
    memcap = [sys.executable, '-I', str(REPO / 'shell' / 'tools' / 'memcap.py'), '--mb', '2048', '--']
    cp = subprocess.run(memcap + [str(d / 'canary.exe')], cwd=str(d), env=env, capture_output=True, text=True,
                        errors='replace')
    check('asan (negative): the canary\'s heap-buffer-overflow is reported (ASan active)',
          cp.returncode != 0 and 'heap-buffer-overflow' in cp.stdout + cp.stderr, (cp.stdout + cp.stderr)[-800:])
    for label, defs, incs in variants(binding_dirs):
        o = d / label
        o.mkdir(parents=True, exist_ok=True)
        exe = o / 'combat_hold_test.exe'
        flags = ['/nologo', '/EHsc', '/W3', '/Zi', '/Oy-', '/fsanitize=address', '/D_CRT_SECURE_NO_WARNINGS'] + \
            ['/D' + x for x in defs] + ['/I' + str(SRC), '/I' + str(EXEBUILD_INCLUDE)] + ['/I' + str(i) for i in incs]
        cp = subprocess.run([cl] + flags + [str(x) for x in SOURCES] + ['/Fe' + str(exe), '/Fo' + str(o) + '\\',
                                                                         '/Fd' + str(o / 'combat_hold_test.pdb')],
                            cwd=str(o), env=env, capture_output=True, text=True, errors='replace')
        if not check('%s, asan: compiled with MSVC /fsanitize=address' % label, cp.returncode == 0,
                     (cp.stdout + cp.stderr)[-2000:]):
            continue
        run_unit(memcap + [exe], '%s, asan' % label, o, env=env)


def main():
    global VERBOSE
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--scratch', required=True, type=Path)
    ap.add_argument('--no-asan', action='store_true')
    ap.add_argument('--binding-include', action='append', default=[],
                    help='a folder with proxy_exe.h or the emitted vp_exe_tables.gen.h (repeatable; before 0590 stage 2)')
    ap.add_argument('--no-binding', action='store_true', help='leave the binding variant out (said in the output)')
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    VERBOSE = a.v
    s = a.scratch.resolve()
    s.mkdir(parents=True, exist_ok=True)
    if a.no_binding:
        print('note: binding variant NOT run (--no-binding)')
        bdirs = None
    else:
        bdirs = binding_includes(s, a)
    test_proxy_toolchain(s, bdirs)
    if not a.no_asan:
        test_asan(s, bdirs)
    bad = [n for n, ok in RESULTS if not ok]
    print('test_combat_hold: %d/%d passed%s' % (len(RESULTS) - len(bad), len(RESULTS),
                                                 ' (binding NOT run)' if bdirs is None else ''))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
