"""Offline test of the proxy's farm builder guard (ticket 0535): proxy/src/proxy_farm_guard.cpp with
shell/composite/farm_guard.cpp, the program proxy/tests/farm_guard_proxy_test.cpp. Nothing is played; no game is started;
the program opens no window.

    python -I proxy/tests/test_farm_guard_proxy.py --scratch <empty or new dir> [--no-asan] [-v]

1. build    farm_guard_proxy_test.exe in the proxy's toolchain (clang-cl -m32 in the VS 2008 environment, VC9 headers, /MT,
            /W3, -DPROXY_FARM_GUARD_NO_SHELL); no warning in the two guard files.
2. unit     the program: the proxy without a composite installs the guard (the watcher thread swaps the node to the shared
            page's hook stub; the builder gets the same object and argument, the stack balanced; the burst's check and
            graph line on the calling thread, the burst line from the watcher); the kill switch (VPSHELL_FARM_GUARD=0 puts
            the builder's word back, 1 swaps again; farm_guard=0 in the ini from the start installs none); farm_builder=0
            only with the composite; the unload (the stub returns at once, its argument popped) and the reload (the next
            boot finds the page by VP64_FARM_GUARD and re-arms it; a later registration retires the old node); a trip kept
            across boots.
3. negative the program without its scratch folder exits 2 with no OK line.
4. asan     the same program under MSVC /fsanitize=address (x86), with a canary that must be caught (COMMON rule 9), under
            shell/tools/memcap.py.
Exit code 0 when every check passes. Outputs only under --scratch. Python 3 standard library only.
"""
import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(REPO / 'proxy' / 'tools'))
import test_proxy_rt as rt  # noqa: E402  (toolchain, vcvarsall)

SRC = REPO / 'proxy' / 'src'
COMPOSITE = REPO / 'shell' / 'composite'
COMMON = REPO / 'shell' / 'common'
PROG = HERE / 'farm_guard_proxy_test.cpp'
SRCS = [PROG, SRC / 'proxy_farm_guard.cpp', COMPOSITE / 'farm_guard.cpp']
INCS = ['/I' + str(SRC), '/I' + str(COMPOSITE), '/I' + str(COMMON)]
EXE = 'farm_guard_proxy_test.exe'
MIN_CHECKS = 62   # the program's checks on 2026-11-21 (44 on 2026-11-18 + 8 for the EXE check + 10 for 0655's post; a run with fewer has lost some)
RESULTS = []
VERBOSE = False


def check(name, ok, detail=''):
    RESULTS.append((name, bool(ok)))
    print(('ok   ' if ok else 'FAIL ') + name + (('  [' + str(detail) + ']') if (detail and (VERBOSE or not ok)) else ''))
    return ok


def build(s):
    d = s / 'bin'
    d.mkdir(parents=True, exist_ok=True)
    out = d / EXE
    cp = rt.tc.run_in_env('x86', [rt.tc.CLANG, '-m32', '/MT', '/O2', '/EHsc', '/W3', '-fuse-ld=lld', '/D_CRT_SECURE_NO_WARNINGS',
                                  '/DPROXY_FARM_GUARD_NO_SHELL'] + INCS + [str(x) for x in SRCS] + ['/Fe' + str(out)],
                          cwd=str(d))
    text = cp.stdout + cp.stderr
    if not check('build: %s (clang-cl -m32, VC9 headers, /MT, /W3)' % EXE, cp.returncode == 0 and out.exists(), text[-3000:]):
        return None
    warn = [ln for ln in text.splitlines() if ('proxy_farm_guard' in ln or 'farm_guard.cpp' in ln) and 'warning' in ln]
    check('build: no warning in proxy_farm_guard.cpp or farm_guard.cpp', not warn, '\n'.join(warn))
    return out


def unit(label, exe, s, env=None, memcap=False):
    work = s / ('work-' + label.replace(' ', '-'))
    if work.exists():
        shutil.rmtree(work)
    cmd = [str(exe), str(work)]
    if memcap:
        cmd = [sys.executable, '-I', str(REPO / 'shell' / 'tools' / 'memcap.py'), '--mb', '2048', '--'] + cmd
    cp = subprocess.run(cmd, cwd=str(exe.parent), env=env, capture_output=True, text=True, errors='replace', timeout=300,
                        creationflags=subprocess.CREATE_NO_WINDOW)
    out = cp.stdout + cp.stderr
    for ln in out.splitlines():
        if ln.startswith('FAIL') or VERBOSE:
            print('    ' + ln)
    m = re.search(r'farm_guard_proxy_test: OK (\d+) checks', out)
    return check('%s: %s' % (label, m.group(0) if m else 'no OK line (exit %s)' % cp.returncode),
                 cp.returncode == 0 and m and int(m.group(1)) >= MIN_CHECKS and 'AddressSanitizer' not in out, out[-3000:])


def negative(exe):
    cp = subprocess.run([str(exe)], capture_output=True, text=True, errors='replace', timeout=30,
                        creationflags=subprocess.CREATE_NO_WINDOW)
    check('negative: without its scratch folder the program exits 2 with no OK line',
          cp.returncode == 2 and 'OK' not in cp.stdout, cp.stdout[-400:])


def test_asan(s):
    env = rt.vcvars_env('x86')
    if not check('asan: vcvarsall x86', env is not None):
        return
    cl = shutil.which('cl', path=env.get('PATH', env.get('Path', '')))
    if not check('asan: cl.exe found', cl is not None):
        return
    d = s / 'asan'
    d.mkdir(parents=True, exist_ok=True)
    (d / 'canary.cpp').write_text('#include <stdio.h>\n#include <stdlib.h>\nint main() { char* volatile p = (char*)malloc(16);'
                                  ' p[16] = 1; printf("canary NOT caught\\n"); free(p); return 0; }\n', encoding='ascii')
    flags = ['/nologo', '/EHsc', '/W3', '/Zi', '/Oy-', '/fsanitize=address', '/D_CRT_SECURE_NO_WARNINGS',
             '/DPROXY_FARM_GUARD_NO_SHELL'] + INCS
    for exe, srcs, fl in (('canary.exe', [d / 'canary.cpp'], ['/nologo', '/Zi', '/Od', '/fsanitize=address']),
                          (EXE, SRCS, flags)):
        cp = subprocess.run([cl] + fl + [str(x) for x in srcs] + ['/Fe' + str(d / exe), '/Fo' + str(d) + '\\',
                                                                  '/Fd' + str(d / (exe + '.pdb'))],
                            cwd=str(d), env=env, capture_output=True, text=True, errors='replace')
        if not check('asan: %s compiled with MSVC /fsanitize=address' % exe, cp.returncode == 0,
                     (cp.stdout + cp.stderr)[-2000:]):
            return
    memcap = [sys.executable, '-I', str(REPO / 'shell' / 'tools' / 'memcap.py'), '--mb', '2048', '--']
    cp = subprocess.run(memcap + [str(d / 'canary.exe')], cwd=str(d), env=env, capture_output=True, text=True,
                        errors='replace', timeout=120)
    check('asan (negative): the canary\'s heap-buffer-overflow is reported (ASan active)',
          cp.returncode != 0 and 'heap-buffer-overflow' in cp.stdout + cp.stderr, (cp.stdout + cp.stderr)[-800:])
    unit('asan unit', d / EXE, s, env=env, memcap=True)


def main():
    global VERBOSE
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--scratch', required=True, type=Path)
    ap.add_argument('--no-asan', action='store_true')
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    VERBOSE = a.v
    s = a.scratch.resolve()
    s.mkdir(parents=True, exist_ok=True)
    # Since ticket 4965 (epic 0590) farm_guard.cpp takes its EXE addresses from the build table: the generated header is
    # made here from the local per-build tables (setting exe_tables, D202), as proxy/build_proxy.py's step 2b does. A
    # missing table fails this test naming the setting; it is never skipped.
    gen = s / 'gen-exebuild'
    cp = subprocess.run([sys.executable, '-I', str(REPO / 'tools' / 'exebuild' / 'emit_header.py'), '--out', str(gen)],
                        capture_output=True, text=True, errors='replace')
    if not check('exebuild: vp_exe_tables.gen.h emitted from the local tables (exe_tables)',
                 cp.returncode == 0 and (gen / 'vp_exe_tables.gen.h').exists(), (cp.stdout + cp.stderr)[-1500:]):
        print('test_farm_guard_proxy: 0/1 passed')
        return 1
    INCS.extend(['/I' + str(REPO / 'exebuild' / 'include'), '/I' + str(gen)])
    exe = build(s)
    if exe is not None:
        unit('unit (proxy toolchain)', exe, s)
        negative(exe)
    if not a.no_asan:
        test_asan(s)
    bad = [n for n, ok in RESULTS if not ok]
    print('test_farm_guard_proxy: %d/%d passed' % (len(RESULTS) - len(bad), len(RESULTS)))
    return 1 if bad or not RESULTS else 0


if __name__ == '__main__':
    sys.exit(main())
