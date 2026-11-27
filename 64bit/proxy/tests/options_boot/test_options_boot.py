"""Offline test of ticket 0624: the options file in a D33 session (proxy/src/proxy_options.cpp, proxy_options.h) and the
renderer guard's rule. Nothing is played; no game is started; nothing of the EXE is read.

    python -I proxy/tests/options_boot/test_options_boot.py --scratch <empty or new dir> [--no-asan] [--old-rev REV] [-v]

1. old      the reproduction: proxy_options.cpp as it was before 0624 (git show REV:..., default 7682d4511, main
            before the fix; its include of the generated-header proxy_rt_internal.h replaced by the one declaration it
            needs, in the scratch copy only) built into options_boot_test.cpp's VP_OPTIONS_BOOT_OLD form: a deleted file
            is re-created at the table's defaults (ServerRenderer 0, every server feature 0) with no LOUD line, and an
            older release's file that lacks the set's keys gives their defaults without a word. Its two checks PASS when
            the gap is there (they describe the old behaviour); the printed lines are the reproduction's evidence.
2. new      the new code with the proxy's toolchain (clang-cl -m32, VC9 headers, /MT): the set's parser and its
            refusals, the created file's text, the guard's rule over all 16 cases, OptionsBoot + OptionsGuard end to end
            (created-with-set, created-without-set and a bad set refused, a stock session unchanged, an existing file
            that lacks keys, a hand-edited ServerRenderer = 0, the two test hooks with their LOUD line, the composite guard
            of item 5 with its hooks and negatives); then once with
            --expect-fail (the program's own failure path: exit 1, one more FAIL line, the summary one short).
3. asan     2 under MSVC /fsanitize=address (x86), with a canary that must be caught (COMMON rule 9), under
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
OPT_INC = REPO / 'options' / 'include'
OPT_SRC = REPO / 'options' / 'src' / 'vp64_options.cpp'
TEST_CPP = HERE / 'options_boot_test.cpp'
VCVARSALL = Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat")
OLD_INCLUDE = '#include "proxy_rt_internal.h"'
OLD_DECL = ('#include <string>  // test_options_boot.py: in place of proxy_rt_internal.h (generated headers)\n'
            'namespace ProxyRtInt { void Log(const char* fmt, ...); }')
RESULTS = []
VERBOSE = False


def check(name, ok, detail=''):
    RESULTS.append((name, bool(ok)))
    print(('ok   ' if ok else 'FAIL ') + name + (('  [' + detail + ']') if (detail and (VERBOSE or not ok)) else ''))
    return ok


def summary(out):
    return re.search(r'options_boot_test: (\d+)/(\d+) checks passed', out)


def run_unit(argv, label, cwd, scratch, env=None, expect_fail=False, show=False):
    args = [str(a) for a in argv] + [str(scratch)] + (['--expect-fail'] if expect_fail else []) + (['-v'] if VERBOSE else [])
    cp = subprocess.run(args, capture_output=True, text=True, errors='replace', cwd=str(cwd), env=env, timeout=600)
    out = cp.stdout + cp.stderr
    m = summary(out)
    if expect_fail:
        return check('%s (negative, --expect-fail): exit code 1, one more FAIL line, the summary one short' % label,
                     cp.returncode == 1 and m and int(m.group(2)) - int(m.group(1)) == 1 and
                     out.count('FAIL the deliberate failure') == 1, 'exit %d; %s' % (cp.returncode, out[-1500:]))
    for ln in out.splitlines():
        if ln.startswith('FAIL') or ln.startswith('  (') or VERBOSE or (show and ln.startswith('old:')):
            print('    ' + ln)
    return check('%s: %s' % (label, m.group(0) if m else 'no summary line (exit %d)' % cp.returncode),
                 cp.returncode == 0 and m and m.group(1) == m.group(2) and 'AddressSanitizer' not in out, out[-2000:])


def clang(d, exe, sources, defs=()):
    return tc.run_in_env('x86', [tc.CLANG, '-m32', '/MT', '/O2', '/EHsc', '-Wno-everything', '-fuse-ld=lld'] +
                         ['/D' + x for x in defs] + ['/I' + str(SRC), '/I' + str(OPT_INC)] +
                         [str(x) for x in sources] + ['/Fe' + str(exe)], cwd=str(d))


def test_old(s, rev):
    d = s / 'old'
    d.mkdir(parents=True, exist_ok=True)
    cp = subprocess.run(['git', '-C', str(REPO), 'show', '%s:proxy/src/proxy_options.cpp' % rev], capture_output=True,
                        text=True, errors='replace')
    if not check('old: proxy_options.cpp of %s read' % rev, cp.returncode == 0 and OLD_INCLUDE in cp.stdout,
                 cp.stderr[-500:]):
        return
    if not check('old: %s is the code before 0624 (OptionsBoot() without parameters)' % rev,
                 'std::string OptionsBoot()' in cp.stdout):
        return
    src = d / 'proxy_options_old.cpp'
    src.write_text(cp.stdout.replace(OLD_INCLUDE, OLD_DECL, 1), encoding='utf-8')
    exe = d / 'options_boot_old.exe'
    cp = clang(d, exe, [TEST_CPP, src, OPT_SRC], ['VP_OPTIONS_BOOT_OLD'])
    if not check('old: built (clang-cl -m32, VC9 headers)', cp.returncode == 0 and exe.exists(), (cp.stdout + cp.stderr)[-3000:]):
        return
    run_unit([exe], 'old (the reproduction; its checks describe the gap)', d, s / 'run-old', show=True)


def test_new(s):
    d = s / 'proxy'
    d.mkdir(parents=True, exist_ok=True)
    exe = d / 'options_boot_test.exe'
    cp = clang(d, exe, [TEST_CPP, SRC / 'proxy_options.cpp', OPT_SRC])
    if not check('new, proxy toolchain: built (clang-cl -m32, VC9 headers)', cp.returncode == 0 and exe.exists(),
                 (cp.stdout + cp.stderr)[-3000:]):
        return
    run_unit([exe], 'new, proxy toolchain', d, s / 'run-new')
    run_unit([exe], 'new, proxy toolchain', d, s / 'run-new-x', expect_fail=True)


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


def test_asan(s):
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
    exe = d / 'options_boot_test.exe'
    flags = ['/nologo', '/EHsc', '/W3', '/Zi', '/Oy-', '/fsanitize=address', '/D_CRT_SECURE_NO_WARNINGS',
             '/I' + str(SRC), '/I' + str(OPT_INC)]
    cp = subprocess.run([cl] + flags + [str(TEST_CPP), str(SRC / 'proxy_options.cpp'), str(OPT_SRC), '/Fe' + str(exe),
                                        '/Fo' + str(d) + '\\', '/Fd' + str(d / 'options_boot_test.pdb')],
                        cwd=str(d), env=env, capture_output=True, text=True, errors='replace')
    if not check('new, asan: compiled with MSVC /fsanitize=address', cp.returncode == 0, (cp.stdout + cp.stderr)[-2000:]):
        return
    run_unit(memcap + [exe], 'new, asan', d, s / 'run-asan', env=env)


def main():
    global VERBOSE
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--scratch', required=True, type=Path)
    ap.add_argument('--no-asan', action='store_true')
    ap.add_argument('--old-rev', default='7682d4511', help='the commit whose proxy_options.cpp is "before 0624"')
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    VERBOSE = a.v
    s = a.scratch.resolve()
    s.mkdir(parents=True, exist_ok=True)
    test_old(s, a.old_rev)
    test_new(s)
    if a.no_asan:
        print('note: asan NOT run (--no-asan)')
    else:
        test_asan(s)
    bad = [n for n, ok in RESULTS if not ok]
    print('test_options_boot: %d/%d passed%s' % (len(RESULTS) - len(bad), len(RESULTS), ' (asan NOT run)' if a.no_asan else ''))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
