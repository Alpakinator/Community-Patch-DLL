"""Unit tests of the DLL's render messages, server side and DLL side (ticket 4932, epic 0454 X2; design:
docs/extras/flags-and-messages-design.md section 2; contract research/03-dll-engine-boundary/render-events.md).

    python -I perf/quietlock/quietlock.py run --who <id> --what "render events tests" -- \
        python -I server/tests/feed/run_render_events_test.py --out DIR --dll-src FORK [--asan] [--bench]

FORK is a checkout of the fork branch x64-render-events (its CvGameCoreDLL_Expansion2/VP64RenderEvents.h and .inl are
the real DLL side under test). Every program and source of the test is built into DIR:
  1. test_render_events.exe (clang-cl x64 /std:c++14 /W4 /WX, the server's toolchain): server/tests/feed/
     test_render_events.cpp with srv_render_events.cpp, srv_feed.cpp, srv_feed_dbcopy.cpp, compiled against the fork's
     header (its static_asserts must hold). The EXE exports VP64RenderEvent, as server.exe does.
  2. Stand-in DLLs: standin_on.dll (the real header and .inl with /DVP64_RENDER_EVENTS: its static initialiser must find
     the EXE's export), standin_none.dll (the header without the define: the macros expand to nothing, no version
     export), standin_v2.dll (a version export that says 2). The EXE loads all three (match, missing, mismatch).
  3. nohost.exe (no VP64RenderEvent export): loads standin_on.dll; its pointer must stay NULL and a send must do nothing.
  4. Negative compiles of srv_render_events.cpp: against a header whose carrier kind is 1002, and against one whose
     version is 2: each must FAIL with its static_assert's text. Against no header: must compile (a pin before the
     branch); the unit built that way says "source has VP64RenderEvents.h: 0".
  5. The runner's own negative: the test built with -DRE_TEST_INJECT_FAIL must exit 1.
  --bench: the send cost (R5) through standin_on.dll, feed on and off (BENCH lines).
  --asan: MSVC cl /fsanitize=address build of test_render_events.exe with a canary that must be caught; the tests again
     (stand-ins included) with no AddressSanitizer report. Runs under shell/tools/memcap.py (2048 MB).
Exit 0 when every part passes, 1 when one fails, 2 when a build that must succeed fails. Python standard library.
"""
import argparse
import importlib.util
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
FEED = REPO / 'server' / 'src' / 'feed'
SRC = [FEED / 'srv_render_events.cpp', FEED / 'srv_feed.cpp', FEED / 'srv_feed_dbcopy.cpp', HERE / 'test_render_events.cpp']
VCVARSALL = Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat")
NOWIN = 0x08000000  # CREATE_NO_WINDOW
CANARY = r'''#include <stdio.h>
#include <stdlib.h>
int main() { char* volatile p = (char*)malloc(16); p[16] = 1; printf("canary NOT caught\n"); free(p); return 0; }
'''
STANDIN_ON = r'''// stand-in for the game DLL built WITH VP64_RENDER_EVENTS (ticket 4932): the fork's real header and .inl
#include <windows.h>
#include "VP64RenderEvents.h"
#include "VP64RenderEvents.inl"
extern "C" __declspec(dllexport) int StandinActive() { return VP64_RENDER_EVENTS_ACTIVE() ? 1 : 0; }
extern "C" __declspec(dllexport) void StandinSend(int k, int p, int a0, int a1, int a2, int a3)
{
	VP64_RENDER_EVENT(k, p, a0, a1, a2, a3);
}
BOOL APIENTRY DllMain(HANDLE, DWORD, LPVOID) { return TRUE; }
'''
STANDIN_NONE = r'''// stand-in for the game DLL built WITHOUT VP64_RENDER_EVENTS (ticket 4932): the header alone, macros empty
#include <windows.h>
#include "VP64RenderEvents.h"
static int s_evaluated = 0;
static int Touch() { return ++s_evaluated; }
extern "C" __declspec(dllexport) int StandinActive() { return VP64_RENDER_EVENTS_ACTIVE() ? 1 : 0; }
extern "C" __declspec(dllexport) void StandinSend(int k, int p, int a0, int a1, int a2, int a3)
{
	VP64_RENDER_EVENT(k, p, a0, a1, a2, a3);
	VP64_RENDER_EVENT(Touch(), 0, 0, 0, 0, 0);   // must not evaluate its arguments
	(void)k; (void)p; (void)a0; (void)a1; (void)a2; (void)a3; (void)Touch;
}
extern "C" __declspec(dllexport) int StandinEvaluated() { return s_evaluated; }
BOOL APIENTRY DllMain(HANDLE, DWORD, LPVOID) { return TRUE; }
'''
STANDIN_V2 = r'''// stand-in for a DLL whose render-message kinds are another version (ticket 4932)
#include <windows.h>
extern "C" __declspec(dllexport) unsigned int DllGetRenderEventsVersion() { return 2; }
BOOL APIENTRY DllMain(HANDLE, DWORD, LPVOID) { return TRUE; }
'''
NOHOST = r'''// a host EXE without the VP64RenderEvent export (a headless test host, an older server): ticket 4932
#include <windows.h>
#include <stdio.h>
typedef int (*ActiveFn)();
typedef void (*SendFn)(int, int, int, int, int, int);
int main(int argc, char** argv)
{
	if(argc < 3) return 2;
	HMODULE h = LoadLibraryA(argv[1]);
	HMODULE hn = LoadLibraryA(argv[2]);
	if(!h || !hn) { printf("LOADFAIL\n"); return 3; }
	ActiveFn active = (ActiveFn)(void*)GetProcAddress(h, "StandinActive");
	SendFn send = (SendFn)(void*)GetProcAddress(h, "StandinSend");
	ActiveFn evaluated = (ActiveFn)(void*)GetProcAddress(hn, "StandinEvaluated");
	SendFn sendNone = (SendFn)(void*)GetProcAddress(hn, "StandinSend");
	ActiveFn activeNone = (ActiveFn)(void*)GetProcAddress(hn, "StandinActive");
	if(!active || !send || !evaluated || !sendNone || !activeNone) { printf("NOEXPORT\n"); return 4; }
	send(1000, 1, 2, 3, 4, 5);   // must do nothing: no host export
	sendNone(1000, 1, 2, 3, 4, 5);
	printf("NOHOST active=%d none_active=%d none_evaluated=%d\n", active(), activeNone(), evaluated());
	return 0;
}
'''


def load_x64build():
    spec = importlib.util.spec_from_file_location('x64build', REPO / 'tools' / 'x64-dll' / 'build.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def run(cmd, **kw):
    return subprocess.run([str(c) for c in cmd], capture_output=True, text=True, errors='replace', creationflags=NOWIN,
                          **kw)


def clang(x64build, env, out, args, name):
    cmd = [x64build.CLANG_CL, '-m64', '/nologo', '/MD', '/EHsc', '/std:c++14', '/W4', '/WX', '/O2', '/Zi',
           '-fuse-ld=lld', '/D_CRT_SECURE_NO_WARNINGS'] + args
    cp = run(cmd, env=env, cwd=str(out))
    text = (cp.stdout + cp.stderr).strip()
    print(f'compile {name}: rc={cp.returncode}')
    return cp.returncode, text


def build_exe(x64build, env, out, exe, inc, extra=()):
    rc, text = clang(x64build, env, out, list(extra) + ([f'/I{inc}'] if inc else []) +
                     [f'/Fe{out / exe}', f'/Fo{out}\\'] + [str(s) for s in SRC] +
                     ['/link', 'kernel32.lib'], exe)
    if rc:
        print(text[-4000:])
    return rc == 0


def vcvars_env():
    import os
    cp = subprocess.run(f'call "{VCVARSALL}" x64 >nul && set', shell=True, capture_output=True, text=True,
                        env=dict(os.environ, VSCMD_SKIP_SENDTELEMETRY='1'))
    if cp.returncode:
        return None
    env = {}
    for line in cp.stdout.splitlines():
        k, sep, v = line.partition('=')
        if sep and k:
            env[k] = v
    return env


def check(name, ok, detail=''):
    print(f'{"PASS" if ok else "FAIL"} {name}' + (f': {detail}' if detail and not ok else ''))
    return 0 if ok else 1


def run_tests(exe, dlls, bench):
    cp = run([exe] + dlls + (['--bench'] if bench else []))
    print(cp.stdout.rstrip()[-6000:])
    return cp


def asan(out, inc, dlls):
    env = vcvars_env()
    if not env:
        print('run_render_events_test: ASan: vcvarsall x64 failed')
        return 2
    cl = shutil.which('cl', path=env.get('PATH', env.get('Path', '')))
    if not cl:
        print('run_render_events_test: ASan: cl.exe not found')
        return 2
    (out / 'asan_canary.cpp').write_text(CANARY, encoding='ascii')
    flags = ['/nologo', '/EHsc', '/W4', '/utf-8', '/Zi', '/Oy-', '/Od', '/MD', '/std:c++14', '/fsanitize=address',
             '/D_CRT_SECURE_NO_WARNINGS', f'/I{inc}']
    (out / 'asan-obj').mkdir(exist_ok=True)
    for exe, srcs, fl in (('asan_canary.exe', [out / 'asan_canary.cpp'], ['/nologo', '/Zi', '/Od', '/fsanitize=address']),
                          ('test_render_events_asan.exe', SRC, flags)):
        cmd = [cl] + fl + [str(s) for s in srcs] + ['/Fe' + str(out / exe), '/Fo' + str(out / 'asan-obj') + '\\',
                                                      '/Fd' + str(out / (exe + '.pdb'))]
        cp = run(cmd, cwd=str(out), env=env)
        print(f'asan compile {exe}: rc={cp.returncode}')
        if cp.returncode:
            print((cp.stdout + cp.stderr)[-4000:])
            return 2
    memcap = [sys.executable, '-I', str(REPO / 'shell' / 'tools' / 'memcap.py'), '--mb', '2048', '--']
    cp = run(memcap + [out / 'asan_canary.exe'], cwd=str(out), env=env)
    text = cp.stdout + cp.stderr
    if cp.returncode == 0 or 'heap-buffer-overflow' not in text:
        print(text[-3000:])
        print(f'run_render_events_test: FAIL: ASan not active (canary exit {cp.returncode})')
        return 1
    print(f'asan canary caught (exit {cp.returncode}, heap-buffer-overflow reported)')
    cp = run(memcap + [out / 'test_render_events_asan.exe'] + dlls, cwd=str(out), env=env)
    text = cp.stdout + cp.stderr
    print(text.rstrip()[-4000:])
    if cp.returncode != 0 or 'AddressSanitizer' in text or ' 0 failed' not in text:
        print(f'run_render_events_test: FAIL: ASan tests exit {cp.returncode}')
        return 1
    print('asan tests: OK (exit 0, no AddressSanitizer report)')
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--dll-src', type=Path, required=True, help='a checkout of the fork branch x64-render-events')
    ap.add_argument('--asan', action='store_true')
    ap.add_argument('--bench', action='store_true')
    a = ap.parse_args()
    sys.path.insert(0, str(REPO / 'perf' / 'quietlock'))
    try:
        import gate
        gate.require('the render events unit tests (server/tests/feed/run_render_events_test.py)', None)
    except ImportError:
        print('run_render_events_test: perf/quietlock/gate.py not found', file=sys.stderr)
        return 2
    inc = (a.dll_src / 'CvGameCoreDLL_Expansion2').resolve()
    header = inc / 'VP64RenderEvents.h'
    if not header.is_file() or not (inc / 'VP64RenderEvents.inl').is_file():
        print(f'run_render_events_test: {inc} has no VP64RenderEvents.h/.inl (not x64-render-events)')
        return 2
    out = a.out.resolve()
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    x64build = load_x64build()
    env = x64build.load_vcvars_env(lambda m, echo=True: None, out / 'vcvars-env.json')
    fails = 0

    # 1. the unit against the fork's real header
    if not build_exe(x64build, env, out, 'test_render_events.exe', inc):
        return 2
    # 2. the stand-ins
    sources = {'standin_on': (STANDIN_ON, ['/DVP64_RENDER_EVENTS']), 'standin_none': (STANDIN_NONE, []),
               'standin_v2': (STANDIN_V2, [])}
    for name, (text, defs) in sources.items():
        (out / f'{name}.cpp').write_text(text, encoding='ascii')
        rc, t = clang(x64build, env, out, ['/LD'] + defs + [f'/I{inc}', str(out / f'{name}.cpp'), f'/Fe{out / name}.dll',
                                                         f'/Fo{out / name}.obj', '/link', 'kernel32.lib'], name)
        if rc:
            print(t[-3000:])
            return 2
    dlls = [str(out / 'standin_on.dll'), str(out / 'standin_none.dll'), str(out / 'standin_v2.dll')]
    cp = run_tests(out / 'test_render_events.exe', dlls, a.bench)
    ok = cp.returncode == 0 and ' 0 failed' in cp.stdout and 'FAIL' not in cp.stdout and \
        'source has VP64RenderEvents.h: 1' in cp.stdout and 'stand-ins: not given' not in cp.stdout
    fails += check('unit tests against the fork header, stand-ins on/none/v2', ok, f'exit {cp.returncode}')
    (out / 'unit-output.txt').write_text(cp.stdout, encoding='utf-8')
    # 3. a host without the export
    (out / 'nohost.cpp').write_text(NOHOST, encoding='ascii')
    rc, t = clang(x64build, env, out, [str(out / 'nohost.cpp'), f'/Fe{out / "nohost.exe"}', f'/Fo{out / "nohost.obj"}',
                                       '/link', 'kernel32.lib'], 'nohost.exe')
    if rc:
        print(t[-3000:])
        return 2
    cp = run([out / 'nohost.exe', dlls[0], dlls[1]])
    fails += check('host without the export: the DLL pointer stays NULL, sends do nothing, empty macro evaluates nothing',
                   cp.returncode == 0 and 'NOHOST active=0 none_active=0 none_evaluated=0' in cp.stdout,
                   (cp.stdout + cp.stderr).strip())
    # 4. negative compiles
    real = header.read_text(encoding='utf-8')
    bad = {'bad_carrier': (real.replace('#define VP64_RENDER_EVENT_UNIT_CARRIER 1001',
                                        '#define VP64_RENDER_EVENT_UNIT_CARRIER 1002'),
                           'VP64_RENDER_EVENT_UNIT_CARRIER differs from srv_feed.h'),
           'bad_version': (real.replace('#define VP64_RENDER_EVENTS_VERSION 1', '#define VP64_RENDER_EVENTS_VERSION 2'),
                           'raise both together')}
    for name, (text, want) in bad.items():
        if text == real:
            fails += check(f'negative compile {name}: the header was not changed (anchor missing)', False)
            continue
        d = out / name
        d.mkdir()
        (d / 'VP64RenderEvents.h').write_text(text, encoding='utf-8')
        rc, t = clang(x64build, env, out, ['/c', f'/I{d}', str(FEED / 'srv_render_events.cpp'),
                                           f'/Fo{out / (name + ".obj")}'], f'{name} (must fail)')
        fails += check(f'negative compile {name}: fails on its static_assert', rc != 0 and want in t, t[-1500:])
    if not build_exe(x64build, env, out, 'test_render_events_noheader.exe', None):
        fails += check('compile without the header (a pin before the branch)', False)
    else:
        cp = run([out / 'test_render_events_noheader.exe'])
        fails += check('compile and tests without the header (a pin before the branch)',
                       cp.returncode == 0 and 'source has VP64RenderEvents.h: 0' in cp.stdout and ' 0 failed' in cp.stdout,
                       cp.stdout[-1500:])
    # 5. the runner's own negative
    if not build_exe(x64build, env, out, 'test_render_events_inject.exe', inc, ['/DRE_TEST_INJECT_FAIL']):
        return 2
    cp = run([out / 'test_render_events_inject.exe'])
    fails += check('negative: an injected failing check fails the binary',
                   cp.returncode == 1 and 'FAIL injected' in cp.stdout and ' 1 failed' in cp.stdout)
    if a.asan:
        r = asan(out, inc, dlls)
        if r == 2:
            return 2
        fails += r != 0
    print(f'run_render_events_test: {"OK" if not fails else "FAILED"} ({fails} failing)')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
