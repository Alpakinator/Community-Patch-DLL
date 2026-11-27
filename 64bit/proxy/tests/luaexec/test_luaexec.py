"""Offline test of the Lua channel's runner without loadstring (ticket 0591, epic 0590): proxy/src/proxy_luaexec.cpp
compiled with VP_LUAEXEC_UNIT_TEST into luaexec_test.cpp, against stock Lua 5.1.4 built from source
(tools/x64-dll/lua51/fetch.py: the verified tarball of deps/third-party.lock, never committed), in a state that nils
_G.loadstring, io and os.execute/... as Firaxis' build 26577625 does. Nothing is played; no game is started.

    python -I proxy/tests/luaexec/test_luaexec.py --scratch <empty or new dir> [--no-asan] [-v]

1. proxy    built with the proxy's toolchain (clang-cl -m32 in the VS 2008 environment, VC9 headers, /MT), run.
2. asan     the same sources under MSVC /fsanitize=address (x86), with a canary that must be caught (COMMON rule 9), run
            under shell/tools/memcap.py.
Exit code 0 when every check passes (a skipped part is a failure). Outputs only under --scratch. Python 3 standard
library only.
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
sys.path.insert(0, str(REPO / 'tools' / 'x64-dll' / 'lua51'))
import toolchain as tc  # noqa: E402
import fetch  # noqa: E402

SRC = REPO / 'proxy' / 'src'
VCVARSALL = Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat")
LUA_SKIP = {'lua.c', 'luac.c', 'print.c'}
RESULTS = []
VERBOSE = False


def check(name, ok, detail=''):
    RESULTS.append((name, bool(ok)))
    print(('ok   ' if ok else 'FAIL ') + name + (('  [' + detail + ']') if (detail and (VERBOSE or not ok)) else ''))
    return ok


def sources(lua_src):
    # proxy_luaexec.cpp is #included by luaexec_test.cpp (its helpers are in an anonymous namespace).
    return [HERE / 'luaexec_test.cpp'] + sorted(p for p in lua_src.glob('*.c') if p.name not in LUA_SKIP)


def summary(label, out, rc):
    for ln in out.splitlines():
        if ln.startswith('FAIL') or VERBOSE:
            print('    ' + ln)
    m = re.search(r'luaexec_test: (\d+)/(\d+) checks passed', out)
    return check('%s: %s' % (label, m.group(0) if m else 'no summary line (exit %d)' % rc),
                 rc == 0 and m and m.group(1) == m.group(2) and int(m.group(2)) > 0 and 'AddressSanitizer' not in out,
                 out[-2000:])


def test_proxy_toolchain(s, lua_src):
    d = s / 'proxy'
    d.mkdir(parents=True, exist_ok=True)
    exe = d / 'luaexec_test.exe'
    cp = tc.run_in_env('x86', [tc.CLANG, '-m32', '/MT', '/O2', '/EHsc', '-Wno-everything', '-fuse-ld=lld',
                               '/I' + str(SRC), '/I' + str(lua_src)] + [str(x) for x in sources(lua_src)] +
                       ['/Fe' + str(exe)], cwd=str(d))
    if not check('proxy toolchain: built (clang-cl -m32, VC9 headers)', cp.returncode == 0 and exe.exists(),
                 (cp.stdout + cp.stderr)[-3000:]):
        return
    cp = subprocess.run([str(exe)] + (['-v'] if VERBOSE else []), capture_output=True, text=True, errors='replace',
                        cwd=str(d), timeout=120)
    summary('proxy toolchain', cp.stdout + cp.stderr, cp.returncode)


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


def test_asan(s, lua_src):
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
    flags = ['/nologo', '/EHsc', '/W3', '/Zi', '/Oy-', '/fsanitize=address', '/D_CRT_SECURE_NO_WARNINGS',
             '/I' + str(SRC), '/I' + str(lua_src)]
    for exe, srcs, fl in (('canary.exe', [d / 'canary.cpp'], ['/nologo', '/Zi', '/Od', '/fsanitize=address']),
                          ('luaexec_test.exe', sources(lua_src), flags)):
        cp = subprocess.run([cl] + fl + [str(x) for x in srcs] + ['/Fe' + str(d / exe), '/Fo' + str(d) + '\\',
                                                                  '/Fd' + str(d / (exe + '.pdb'))],
                            cwd=str(d), env=env, capture_output=True, text=True, errors='replace')
        if not check('asan: %s compiled with MSVC /fsanitize=address' % exe, cp.returncode == 0,
                     (cp.stdout + cp.stderr)[-2000:]):
            return
    memcap = [sys.executable, '-I', str(REPO / 'shell' / 'tools' / 'memcap.py'), '--mb', '2048', '--']
    cp = subprocess.run(memcap + [str(d / 'canary.exe')], cwd=str(d), env=env, capture_output=True, text=True,
                        errors='replace')
    check('asan (negative): the canary\'s heap-buffer-overflow is reported (ASan active)',
          cp.returncode != 0 and 'heap-buffer-overflow' in cp.stdout + cp.stderr, (cp.stdout + cp.stderr)[-800:])
    cp = subprocess.run(memcap + [str(d / 'luaexec_test.exe')] + (['-v'] if VERBOSE else []), cwd=str(d), env=env,
                        capture_output=True, text=True, errors='replace', timeout=300)
    summary('asan', cp.stdout + cp.stderr, cp.returncode)


def main():
    global VERBOSE
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--scratch', required=True, type=Path)
    ap.add_argument('--no-asan', action='store_true', help='leave out the ASan part (the run then FAILS: skipped counts as failed)')
    ap.add_argument('--lua-tarball', default=None, help='an explicit Lua 5.1.4 tarball (else fetch.py finds it)')
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    VERBOSE = a.v
    s = a.scratch.resolve()
    s.mkdir(parents=True, exist_ok=True)
    lua_src = Path(fetch.ensure_lua(s / 'lua', tarball=a.lua_tarball))
    check('lua 5.1.4 source unpacked (%s)' % lua_src, (lua_src / 'lapi.c').exists())
    test_proxy_toolchain(s, lua_src)
    if a.no_asan:
        check('asan: skipped by --no-asan (a skipped part fails the run)', False)
    else:
        test_asan(s, lua_src)
    bad = [n for n, ok in RESULTS if not ok]
    print('test_luaexec: %d/%d passed' % (len(RESULTS) - len(bad), len(RESULTS)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
