"""Offline test of ticket 0805's shutdown step (proxy-design 38): ProxyLuaExec::InertImageValues of proxy/src/proxy_luaexec.cpp,
compiled with VP_LUAEXEC_UNIT_TEST into inert_test.cpp, against stock Lua 5.1.4 built from source (tools/x64-dll/lua51/fetch.py,
the verified tarball, never committed). Nothing is played; no game is started.

    python -I proxy/tests/reload_after_game/test_inert.py --scratch <empty or new dir> [--no-asan] [-v]

1. proxy    built with the proxy's toolchain (clang-cl -m32 in the VS 2008 environment, VC9 headers, /MT), run.
2. asan     the same sources under MSVC /fsanitize=address (x86), with a canary that must be caught (COMMON rule 9), run
            under shell/tools/memcap.py.
3. luaexec  proxy/tests/luaexec/test_luaexec.py's own unit (the runner) still builds and passes with the changed file
            (run with --also-luaexec; it is the other builder of proxy_luaexec.cpp in unit-test mode).
Exit code 0 when every check passes (a skipped part is a failure). Outputs only under --scratch. Python 3 standard
library only. The build steps are proxy/tests/luaexec/test_luaexec.py's (imported), with this folder's source.
"""
import argparse
import importlib.util
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
_spec = importlib.util.spec_from_file_location('test_luaexec', REPO / 'proxy' / 'tests' / 'luaexec' / 'test_luaexec.py')
TL = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(TL)

SRC = REPO / 'proxy' / 'src'
RESULTS = []
VERBOSE = False


def check(name, ok, detail=''):
    RESULTS.append((name, bool(ok)))
    print(('ok   ' if ok else 'FAIL ') + name + (('  [' + detail + ']') if (detail and (VERBOSE or not ok)) else ''))
    return ok


def sources(lua_src):
    return [HERE / 'inert_test.cpp'] + sorted(p for p in lua_src.glob('*.c') if p.name not in TL.LUA_SKIP)


def summary(label, out, rc):
    for ln in out.splitlines():
        if ln.startswith('FAIL') or VERBOSE:
            print('    ' + ln)
    m = re.search(r'inert_test: (\d+)/(\d+) checks passed', out)
    return check('%s: %s' % (label, m.group(0) if m else 'no summary line (exit %d)' % rc),
                 rc == 0 and m and m.group(1) == m.group(2) and int(m.group(2)) > 0 and 'AddressSanitizer' not in out,
                 out[-2000:])


def test_proxy_toolchain(s, lua_src):
    d = s / 'proxy'
    d.mkdir(parents=True, exist_ok=True)
    exe = d / 'inert_test.exe'
    cp = TL.tc.run_in_env('x86', [TL.tc.CLANG, '-m32', '/MT', '/O2', '/EHsc', '-Wno-everything', '-fuse-ld=lld',
                                  '/I' + str(SRC), '/I' + str(lua_src)] + [str(x) for x in sources(lua_src)] +
                          ['/Fe' + str(exe)], cwd=str(d))
    if not check('proxy toolchain: built (clang-cl -m32, VC9 headers)', cp.returncode == 0 and exe.exists(),
                 (cp.stdout + cp.stderr)[-3000:]):
        return
    cp = subprocess.run([str(exe)] + (['-v'] if VERBOSE else []), capture_output=True, text=True, errors='replace',
                        cwd=str(d), timeout=120)
    summary('proxy toolchain', cp.stdout + cp.stderr, cp.returncode)


def test_asan(s, lua_src):
    env = TL.vcvars_env('x86')
    if not check('asan: vcvarsall x86', env is not None):
        return
    import shutil
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
                          ('inert_test.exe', sources(lua_src), flags)):
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
    cp = subprocess.run(memcap + [str(d / 'inert_test.exe')] + (['-v'] if VERBOSE else []), cwd=str(d), env=env,
                        capture_output=True, text=True, errors='replace', timeout=300)
    summary('asan', cp.stdout + cp.stderr, cp.returncode)


def test_luaexec(s):
    cp = subprocess.run([sys.executable, '-I', str(REPO / 'proxy' / 'tests' / 'luaexec' / 'test_luaexec.py'),
                         '--scratch', str(s / 'luaexec')], capture_output=True, text=True, errors='replace', timeout=900)
    m = re.search(r'test_luaexec: (\d+)/(\d+) passed', cp.stdout)
    check('luaexec: proxy/tests/luaexec/test_luaexec.py with the changed proxy_luaexec.cpp: %s'
          % (m.group(0) if m else 'no summary (exit %d)' % cp.returncode),
          cp.returncode == 0 and m is not None and m.group(1) == m.group(2), (cp.stdout + cp.stderr)[-1500:])


def main():
    global VERBOSE
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--scratch', required=True, type=Path)
    ap.add_argument('--no-asan', action='store_true', help='leave out the ASan part (the run then FAILS: skipped counts as failed)')
    ap.add_argument('--also-luaexec', action='store_true', help='also run proxy/tests/luaexec/test_luaexec.py')
    ap.add_argument('--lua-tarball', default=None, help='an explicit Lua 5.1.4 tarball (else fetch.py finds it)')
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    VERBOSE = a.v
    s = a.scratch.resolve()
    s.mkdir(parents=True, exist_ok=True)
    lua_src = Path(TL.fetch.ensure_lua(s / 'lua', tarball=a.lua_tarball))
    check('lua 5.1.4 source unpacked (%s)' % lua_src, (lua_src / 'lapi.c').exists())
    test_proxy_toolchain(s, lua_src)
    if a.no_asan:
        check('asan: skipped by --no-asan (a skipped part fails the run)', False)
    else:
        test_asan(s, lua_src)
    if a.also_luaexec:
        test_luaexec(s)
    bad = [n for n, ok in RESULTS if not ok]
    print('test_inert: %d/%d passed' % (len(RESULTS) - len(bad), len(RESULTS)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
