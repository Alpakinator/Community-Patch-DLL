"""Issue 3522: hermetic tests of server.exe's crash dump filter (server/src/srv_crashdump.cpp). No game, no server.

    python -I server/tests/crashdump/run_crashdump_test.py --out DIR [--asan] [-v]

Builds server/tests/crashdump/crashdump_test.cpp + server/src/srv_crashdump.cpp (clang-cl x64, the server's toolchain,
/DSRV_CRASHDUMP_TEST) into DIR and runs it on a hidden desktop (COMMON rule 14; it shows nothing anyway and has the
error boxes off, as server.exe):
  av / throw          each against a control run without the filter: the same exit code (0xC0000005; a C++ throw that
                      escapes ends through the runtime's terminate as before), exactly one server-<pid>-<yyyymmdd-hhmmss>.dmp
                      and no .tmp; dbghelp (MiniDumpReadDumpStream) opens it: exception record with the code, the
                      faulting thread = the thread the program printed, listed in the thread list, the address inside
                      crashdump_test.exe; no full memory (under 64 MB)
  av --twice          the filter installed twice: the second install says so, still one dump, the same exit code
  unwritable          the run folder is a path under a file: no file anywhere, the same exit code
  fail-write          MiniDumpWriteDump made to fail (test build only): no .dmp and no .tmp left, the same exit code
  ok / ok ""          no fault: no dump, exit 0; an empty folder: "NOT installed"
  --asan              the same program under MSVC /fsanitize=address (x64; a canary must be caught): ok, ok --twice, throw
                      (its dump checked) and check of the clang run's dumps, with no AddressSanitizer report. The av
                      case is not run under ASan: ASan's own access-violation handler reports it first.
Also importable: build(out) gives the test program (proxy/tools/test_proxy_session.py uses it as a crashing fake
server.exe: it installs the filter on server.cfg's run_dir and faults).
Exit 0 when every check passes. Outputs only under --out. Start it through perf/quietlock/quietlock.py run.
"""
import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(REPO / 'proxy' / 'tools'))
import test_proxy_rt as rt  # noqa: E402  (toolchain, hidden desktops, vcvarsall)

SRCS = [HERE / 'crashdump_test.cpp', REPO / 'server' / 'src' / 'srv_crashdump.cpp']
AV = 0xC0000005
DUMP_RE = re.compile(r'^server-(\d+)-\d{8}-\d{6}\.dmp$')
DESKS = {}
check = rt.check


def desk():
    return DESKS.setdefault('cd', rt.desktops.HiddenDesktop('C5T3522'))


def build(out):
    """clang-cl x64 (/MD, /W3 /WX as the server's own code) with SRV_CRASHDUMP_TEST. Returns (ok, text)."""
    out = Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    cp = rt.tc.run_in_env('x64', [rt.tc.CLANG, '-m64', '/MD', '/O2', '/Zi', '/EHsc', '/std:c++14', '/W3', '/WX',
                                  '/DWIN32_LEAN_AND_MEAN', '/D_CRT_SECURE_NO_WARNINGS', '/DSRV_CRASHDUMP_TEST',
                                  '-fuse-ld=lld', '/I' + str(REPO / 'server' / 'src')] + [str(s) for s in SRCS] +
                          ['/Fe' + str(out)], cwd=str(out.parent))
    return cp.returncode == 0 and out.exists(), cp.stdout + cp.stderr


def run(exe, args, out_txt, env=None, cwd=None):
    p = rt.OnDesktop(desk().name, [exe] + [str(a) for a in args], env or dict(rt.os.environ), out_txt,
                     cwd=cwd or Path(exe).parent)
    rc = p.wait(120)
    return rc, p.text()


def files(d):
    d = Path(d)
    return sorted(p.name for p in d.iterdir()) if d.is_dir() else []


def check_dump(exe, dump, out_txt, code, tid, env=None):
    """The exception address must lie in the program (an access violation) or in KernelBase.dll (RaiseException of a
    C++ throw). tid -1: not compared."""
    rc, out = run(exe, ['check', dump], out_txt, env)
    m = re.search(r'CHECK ok code 0x([0-9A-F]{8}) address 0x([0-9A-F]+) tid (\d+) thread_listed (\d) threads (\d+) '
                  r'modules (\d+) in_module (\S+)', out)
    where = 'kernelbase.dll' if code == 0xE06D7363 else 'crashdump_test.exe'
    ok = (rc == 0 and m is not None and int(m.group(1), 16) == code and (tid < 0 or int(m.group(3)) == tid) and
          m.group(4) == '1' and m.group(7).lower() == where and Path(dump).stat().st_size < 64 << 20)
    return ok, out.strip() + ' size %d' % Path(dump).stat().st_size


def tid_of(out):
    m = re.search(r'^TID (\d+)', out, re.M)
    return int(m.group(1)) if m else -1


def cases(s, exe, tag, env=None, with_av=True):
    """The fault cases with one build of the program; tag names the build in the check names."""
    n = [0]

    def go(name, args):
        n[0] += 1
        d = s / tag / name
        shutil.rmtree(d, ignore_errors=True)
        d.mkdir(parents=True)
        rc, out = run(exe, [args[0], d] + args[1:], s / tag / ('%02d-%s.txt' % (n[0], name)), env)
        return d, rc, out

    modes = (('av', AV), ('throw', 0xE06D7363)) if with_av else (('throw', 0xE06D7363),)
    for mode, code in modes:
        _, rc0, out0 = go(mode + '-control', [mode, '--no-filter'])
        d, rc, out = go(mode, [mode])
        names = files(d)
        dumps = [x for x in names if DUMP_RE.match(x)]
        check('%s %s: the same exit code as without the filter (0x%08X)' % (tag, mode, rc0 or 0),
              rc is not None and rc == rc0 and rc0 != 0, 'with %s without %s; %s' % (rc, rc0, out[-300:]))
        check('%s %s: exactly one server-<pid>-<stamp>.dmp, no .tmp, the pid is the program\'s' % (tag, mode),
              len(dumps) == 1 and len(names) == 1 and 'installed' in out,
              '%s; %s' % (names, out[-300:]))
        if len(dumps) == 1:
            ok, txt = check_dump(exe, d / dumps[0], s / tag / ('check-%s.txt' % mode), code, tid_of(out), env)
            check('%s %s: dbghelp opens the dump: exception record 0x%08X, the faulting thread listed, the address in '
                  '%s, no full memory' % (tag, mode, code, 'KernelBase.dll' if mode == 'throw' else 'the program'),
                  ok, txt)
        check('%s %s (control): no dump without the filter' % (tag, mode), not files(s / tag / (mode + '-control')))
    if with_av:
        d, rc, out = go('twice', ['av', '--twice'])
        names = files(d)
        check('%s av --twice: the second install says "already installed", one dump, exit code 0xC0000005' % tag,
              rc == AV and len(names) == 1 and DUMP_RE.match(names[0]) is not None and
              'already installed, folder replaced' in out, '%s %s %s' % (rc, names, out[-300:]))
        # unwritable: the folder lies under a file
        top = s / tag / 'unwritable'
        shutil.rmtree(top, ignore_errors=True)
        top.mkdir(parents=True)
        (top / 'blocker').write_text('a file, not a folder\n', encoding='ascii')
        rc, out = run(exe, ['av', top / 'blocker' / 'run'], s / tag / 'unwritable.txt', env)
        check('%s unwritable: no file anywhere, the same exit code 0xC0000005' % tag,
              rc == AV and files(top) == ['blocker'] and (top / 'blocker').is_file(), '%s %s %s' % (rc, files(top), out))
        d, rc, out = go('failwrite', ['av', '--fail-write'])
        check('%s fail-write: the write fails after the .tmp was opened: no .dmp and no .tmp left, exit code 0xC0000005'
              % tag, rc == AV and not files(d), '%s %s %s' % (rc, files(d), out[-300:]))
    d, rc, out = go('ok', ['ok', '--twice'])
    check('%s ok --twice: no fault, no dump, exit 0' % tag, rc == 0 and not files(d) and 'OK no fault' in out,
          '%s %s' % (rc, out))
    rc, out = run(exe, ['ok', ''], s / tag / 'empty.txt', env)
    check('%s ok "": an empty run folder is "NOT installed: no run folder", exit 0' % tag,
          rc == 0 and 'NOT installed: no run folder' in out, out)


def test_asan(s, clang_exe):
    env = rt.vcvars_env('x64')
    if not check('asan: vcvarsall x64', env is not None):
        return
    cl = shutil.which('cl', path=env.get('PATH', env.get('Path', '')))
    if not check('asan: cl.exe found', cl is not None):
        return
    d = s / 'asan'
    d.mkdir(parents=True, exist_ok=True)
    (d / 'canary.cpp').write_text('#include <stdio.h>\n#include <stdlib.h>\nint main() { char* volatile p = (char*)malloc(16);'
                                  ' p[16] = 1; printf("canary NOT caught\\n"); free(p); return 0; }\n', encoding='ascii')
    flags = ['/nologo', '/EHsc', '/W3', '/Zi', '/Oy-', '/fsanitize=address', '/DWIN32_LEAN_AND_MEAN',
             '/D_CRT_SECURE_NO_WARNINGS', '/DSRV_CRASHDUMP_TEST', '/I' + str(REPO / 'server' / 'src')]
    for exe, srcs, fl in (('canary.exe', [d / 'canary.cpp'], ['/nologo', '/Zi', '/Od', '/fsanitize=address']),
                          ('crashdump_test.exe', SRCS, flags)):
        cp = subprocess.run([cl] + fl + [str(x) for x in srcs] + ['/Fe' + str(d / exe), '/Fo' + str(d) + '\\',
                                                                  '/Fd' + str(d / (exe + '.pdb'))],
                            cwd=str(d), env=env, capture_output=True, text=True, errors='replace')
        if not check('asan: %s compiled with MSVC /fsanitize=address (x64)' % exe, cp.returncode == 0,
                     (cp.stdout + cp.stderr)[-2000:]):
            return
    rc, out = run(d / 'canary.exe', [], d / 'canary.txt', env)
    check('asan (negative): the canary\'s heap-buffer-overflow is reported (ASan active)',
          rc != 0 and 'heap-buffer-overflow' in out, out[-600:])
    first = len(rt.RESULTS)
    cases(s, d / 'crashdump_test.exe', 'asan', env, with_av=False)
    # the clang run's dumps read by the ASan build's checker
    for mode, code in (('av', AV), ('throw', 0xE06D7363)):
        dd = s / 'clang' / mode
        dumps = [x for x in files(dd) if DUMP_RE.match(x)]
        if dumps:
            tid = tid_of(rt.text(next((s / 'clang').glob('*-%s.txt' % mode))))
            ok, txt = check_dump(d / 'crashdump_test.exe', dd / dumps[0], d / ('check-clang-%s.txt' % mode), code, -1, env)
            check('asan: the ASan build\'s checker opens the clang run\'s %s dump (code 0x%08X)' % (mode, code),
                  'CHECK ok code 0x%08X' % code in txt and 'tid %d ' % tid in txt, txt)
    reports = [p for p in (s / 'asan').glob('*.txt') if 'AddressSanitizer' in rt.text(p) and p.name != 'canary.txt']
    check('asan: no AddressSanitizer report in any run of the ASan build (%d checks above)' % (len(rt.RESULTS) - first),
          not reports, ', '.join(p.name for p in reports))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--asan', action='store_true')
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    rt.VERBOSE = a.v
    s = a.out.resolve()
    s.mkdir(parents=True, exist_ok=True)
    try:
        exe = s / 'clang' / 'crashdump_test.exe'
        ok, txt = build(exe)
        if check('build: crashdump_test.cpp + srv_crashdump.cpp (clang-cl x64, /W3 /WX, SRV_CRASHDUMP_TEST)', ok,
                 txt[-2000:]):
            cases(s, exe, 'clang')
            if a.asan:
                test_asan(s, exe)
    finally:
        for dk in DESKS.values():
            dk.close()
    bad = [nm for nm, ok in rt.RESULTS if not ok]
    print('run_crashdump_test: %d/%d checks passed' % (len(rt.RESULTS) - len(bad), len(rt.RESULTS)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
