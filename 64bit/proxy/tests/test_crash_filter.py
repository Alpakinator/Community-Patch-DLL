"""Offline test of the proxy's chained exception filter (ticket 0419): proxy/src/proxy_crash_filter.cpp with the stand-in
proxy/tests/crash_filter_standin.cpp that faults. Nothing is played; no game is started. Every stand-in runs on a hidden
desktop (COMMON rule 14; tools/harness/tests/desktops.py through proxy/tools/test_proxy_rt.py's OnDesktop).

    python -I proxy/tests/test_crash_filter.py --scratch <empty or new dir> [--no-asan] [-v]

1. build    the stand-in EXE and its fault DLL in the proxy's toolchain (clang-cl -m32 in the VS 2008 environment, VC9
            headers, /MT), with /W3.
2. unit     `crash_filter_standin.exe unit`: the record's text for synthetic exception records (first line, every line a
            known fact, the access line, the cuts), DescribeAddress (EXE, kernel32, heap, NULL), Install (the chain, a
            second call, a too-long path) and Uninstall (the previous filter back; a later filter left in place, LOUD).
3. faults   one stand-in per case. Before it faults it installs a "previous" filter of its own (it writes prev.txt and
            returns EXCEPTION_EXECUTE_HANDLER, as the EXE's own filter lets the process end), then the proxy's filter.
            Checks: the exit code is the exception's code; prev.txt shows the previous filter was called with that code
            (the chain); exe-crash.txt's first line has the code, its name, the module + RVA (exact where the stand-in
            prints it: ud2 in the EXE with its VA at the EXE file's ImageBase, ud2 in a DLL without a VA), the thread; the access line of
            an access violation; every line is a known fact (no bytes, no memory: D83); the LOUD line in vpproxy.log.
            Cases: av-read, av-write, av-exec, ud2, dll-ud2, div0, cpp (0xE06D7363), stack (0xC00000FD: the filter runs
            on what is left of the stack), two (two threads fault at once: one record), and the negatives nolog (no log
            handle: the record still), badpath (the record cannot be written: the log says so, the chain still runs),
            uninstalled (Install then Uninstall: no record, the previous filter alone), noprev (no previous filter: the
            system's default ends the process with the code; SEM_NOGPFAULTERRORBOX, no box), fastfail (int 0x29 never
            reaches a filter: no record; documents the limit).
4. asan     the unit mode under MSVC /fsanitize=address (x86), with a canary that must be caught (COMMON rule 9), under
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
REPO = HERE.parents[1]
sys.path.insert(0, str(REPO / 'proxy' / 'tools'))
import test_proxy_rt as rt  # noqa: E402  (toolchain, hidden desktops, OnDesktop, vcvarsall)

SRC = REPO / 'proxy' / 'src'
STANDIN = HERE / 'crash_filter_standin.cpp'
FILTER = SRC / 'proxy_crash_filter.cpp'
RESULTS = []
VERBOSE = False
DESKS = {}
KEYS = ('exception ', 'code: ', 'where: ', 'module base: ', 'rva: ', "va at the EXE's ImageBase: ", 'access: ', 'thread: ',
        'process: ', 'time: ', 'flags: ', 'written by: ', 'address: ')
EXE = 'crash_filter_standin.exe'


def check(name, ok, detail=''):
    RESULTS.append((name, bool(ok)))
    print(('ok   ' if ok else 'FAIL ') + name + (('  [' + str(detail) + ']') if (detail and (VERBOSE or not ok)) else ''))
    return ok


def desk():
    return DESKS.setdefault('d', rt.desktops.HiddenDesktop('C5T0419'))


def text(p):
    try:
        return Path(p).read_bytes().decode('utf-8', errors='replace')
    except OSError:
        return ''


def clang(out, srcs, extra=()):
    out = Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    cp = rt.tc.run_in_env('x86', [rt.tc.CLANG, '-m32', '/MT', '/O2', '/EHsc', '/W3', '-fuse-ld=lld',
                                  '/D_CRT_SECURE_NO_WARNINGS', '/I' + str(SRC)] + list(extra) + [str(s) for s in srcs] +
                          ['/Fe' + str(out), 'user32.lib'], cwd=str(out.parent))
    return cp.returncode == 0 and out.exists(), (cp.stdout + cp.stderr)[-3000:]


def build(s):
    d = s / 'bin'
    ok, out = clang(d / EXE, [STANDIN, FILTER])
    if not check('build: %s (clang-cl -m32, VC9 headers, /MT, /W3) with proxy_crash_filter.cpp' % EXE, ok, out):
        return None
    warn = [ln for ln in out.splitlines() if 'proxy_crash_filter' in ln and 'warning' in ln]
    check('build: no warning in proxy_crash_filter.cpp', not warn, '\n'.join(warn))
    ok, out = clang(d / 'crash_filter_dll.dll', [STANDIN], ['/LD', '/DCRASH_FILTER_DLL'])
    if not check('build: crash_filter_dll.dll (the fault in another module)', ok, out):
        return None
    return d


def run(exe, args, out, timeout=60):
    env = {k: v for k, v in os.environ.items()}
    p = rt.OnDesktop(desk().name, [str(exe)] + [str(a) for a in args], env, out, cwd=Path(exe).parent)
    rc = p.wait(timeout)
    return rc, p.text()


def unit_summary(label, out, rc):
    for ln in out.splitlines():
        if ln.startswith('FAIL') or VERBOSE:
            print('    ' + ln)
    m = re.search(r'crash_filter_unit: (\d+)/(\d+) checks passed', out)
    return check('%s: %s' % (label, m.group(0) if m else 'no summary line (exit %s)' % rc),
                 rc == 0 and m and m.group(1) == m.group(2) and 'AddressSanitizer' not in out, out[-2000:])


def known_lines(rec):
    lines = [ln for ln in rec.split('\r\n') if ln]
    return all(ln.startswith(KEYS) for ln in lines) and '\n' not in rec.replace('\r\n', '')


# case: (mode, exit code, record expected, first-line pattern, access line or None, prev expected)
CASES = [
    ('av-read', 0xC0000005, True, r'exception 0xC0000005 EXCEPTION_ACCESS_VIOLATION at crash_filter_standin\.exe\+0x[0-9A-F]+'
                                  r' \(VA 0x[0-9A-F]{8}\), thread \d+ \(the proxy\'s boot thread\)$', 'read of 0x00000010', True),
    ('av-write', 0xC0000005, True, r'exception 0xC0000005 EXCEPTION_ACCESS_VIOLATION at crash_filter_standin\.exe\+0x',
     'write to 0x00000020', True),
    ('av-exec', 0xC0000005, True, r'exception 0xC0000005 EXCEPTION_ACCESS_VIOLATION at an address in no module '
                                  r'\(0x00000030\), thread \d+', None, True),
    ('ud2', 0xC000001D, True, None, None, True),
    ('dll-ud2', 0xC000001D, True, None, None, True),
    ('div0', 0xC0000094, True, r'exception 0xC0000094 EXCEPTION_INT_DIVIDE_BY_ZERO at crash_filter_standin\.exe\+0x', None,
     True),
    ('cpp', 0xE06D7363, True, r'exception 0xE06D7363 an unhandled C\+\+ exception \(MSVC 0xE06D7363\) at '
                              r'(?i:kernelbase)\.dll\+0x', None, True),
    ('stack', 0xC00000FD, True, r'exception 0xC00000FD EXCEPTION_STACK_OVERFLOW at crash_filter_standin\.exe\+0x', None, True),
    ('two', 0xC0000005, True, r'exception 0xC0000005 EXCEPTION_ACCESS_VIOLATION at crash_filter_standin\.exe\+0x[0-9A-F]+ '
                              r'\(VA 0x[0-9A-F]{8}\), thread \d+$', 'write to 0x00000040', True),
    ('nolog', 0xC0000005, True, r'exception 0xC0000005 EXCEPTION_ACCESS_VIOLATION', 'read of 0x00000010', True),
    ('badpath', 0xC0000005, False, None, None, True),
    ('uninstalled', 0xC0000005, False, None, None, True),
    ('noprev', 0xC0000005, True, r'exception 0xC0000005 EXCEPTION_ACCESS_VIOLATION', 'read of 0x00000010', False),
    ('fastfail', 0xC0000409, False, None, None, False),
]


def fault_case(bindir, s, mode, code, want_rec, pat, access, want_prev):
    w = s / 'faults' / mode
    if w.exists():
        shutil.rmtree(w)
    w.mkdir(parents=True)
    rc, out = run(bindir / EXE, [mode, w], s / 'faults' / (mode + '.out'))
    rec = text(w / 'exe-crash.txt')
    prev = text(w / 'prev.txt')
    log = text(w / 'vpproxy.log')
    first = rec.split('\r\n', 1)[0]
    tag = 'fault %s' % mode
    check('%s: exit code 0x%08X' % (tag, code), rc == code, 'rc %s; %s' % ('0x%08X' % rc if rc is not None else None,
                                                                         out[-600:]))
    check('%s: the stand-in reached its fault (installed, no FAIL line)' % tag,
          'install: exe-crash filter: installed (ticket 0419)' in out and 'standin: FAIL' not in out, out[-600:])
    if want_prev:
        check('%s: the previous filter was called with 0x%08X (the chain)' % (tag, code),
              prev.startswith('prev saw 0x%08X' % code), prev or '(no prev.txt)')
    else:
        check('%s: no previous filter ran' % tag, prev == '', prev)
    if not want_rec:
        check('%s: no exe-crash.txt' % tag, not (w / 'exe-crash.txt').exists(), rec[:300])
        if mode == 'badpath':
            check('%s: vpproxy.log says the record could not be written (error 3) and that it handed on' % tag,
                  'LOUD: exe-crash:' in log and 'could not be written (error 3); handed on to the previous filter' in log,
                  log[-600:])
        if mode in ('uninstalled', 'fastfail'):
            check('%s: no LOUD line in vpproxy.log' % tag, 'exe-crash' not in log, log[-400:])
        if mode == 'uninstalled':
            check('%s: Uninstall said "removed"' % tag, 'uninstall: exe-crash filter: removed' in out, out[-400:])
        return
    check('%s: exe-crash.txt written' % tag, bool(rec), out[-400:])
    exp = re.search(r'expect-where: (\S+)', out)
    if exp:
        check('%s: the first line names %s (exact module + RVA)' % (tag, exp.group(1)),
              (' at %s ' % exp.group(1)) in first or (' at %s,' % exp.group(1)) in first, first)
        va = re.search(r'expect-va: (0x[0-9A-F]{8})', out)
        if va:
            check('%s: the VA at the EXE file\'s ImageBase %s' % (tag, va.group(1)), '(VA %s)' % va.group(1) in first,
                  first)
        if 'expect-no-va' in out:
            check('%s: no VA for a place in a DLL' % tag, '(VA ' not in first, first)
        check('%s: the first line starts with the code 0x%08X EXCEPTION_ILLEGAL_INSTRUCTION' % (tag, code),
              first.startswith('exception 0x%08X EXCEPTION_ILLEGAL_INSTRUCTION at ' % code), first)
    if pat:
        check('%s: the first line matches %r' % (tag, pat[:60]), re.search(pat, first) is not None, first)
    if access:
        check('%s: access line "%s"' % (tag, access), ('\r\naccess: %s\r\n' % access) in rec, rec[:600])
    check('%s: every line is a known fact (no bytes, no memory: D83), under 2 KB' % tag,
          known_lines(rec) and len(rec) < 2048 and rec.endswith('\r\n'), rec[:900])
    check('%s: the record names its process' % tag, re.search(r'\r\nprocess: \d+\r\n', rec) is not None, rec[:600])
    if mode == 'two':
        check('%s: one record (the once-flag): one "exception" line' % tag, rec.count('exception 0x') == 1, rec[:600])
        check('%s: one LOUD line' % tag, log.count('LOUD: exe-crash:') == 1, log[-600:])
    if mode == 'nolog':
        check('%s: no vpproxy.log written' % tag, not (w / 'vpproxy.log').exists(), log[:200])
    elif mode != 'two':
        check('%s: vpproxy.log has the LOUD line with the clock and the record\'s first line' % tag,
              re.match(r'\[ 12\.500000\] \[\s*\d+\] LOUD: exe-crash: an unhandled exception in the game\'s process '
                       r'\(ticket 0419\): ', log) is not None and first in log and
              ('; record %s\\exe-crash.txt; %s\n' % (w, 'handed on to the previous filter' if want_prev else
                                                     'no previous filter: the system\'s default handling follows')) in log,
              log[-700:])


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
    flags = ['/nologo', '/EHsc', '/W3', '/Zi', '/Oy-', '/fsanitize=address', '/D_CRT_SECURE_NO_WARNINGS', '/I' + str(SRC)]
    for exe, srcs, fl in (('canary.exe', [d / 'canary.cpp'], ['/nologo', '/Zi', '/Od', '/fsanitize=address']),
                          (EXE, [STANDIN, FILTER, 'user32.lib'], flags)):
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
    cp = subprocess.run(memcap + [str(d / EXE), 'unit'], cwd=str(d), env=env, capture_output=True, text=True,
                        errors='replace', timeout=300)
    unit_summary('asan unit', cp.stdout + cp.stderr, cp.returncode)


def main():
    global VERBOSE
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--scratch', required=True, type=Path)
    ap.add_argument('--no-asan', action='store_true')
    ap.add_argument('--only', default='', help='comma-separated fault modes (default: all)')
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    VERBOSE = a.v
    s = a.scratch.resolve()
    s.mkdir(parents=True, exist_ok=True)
    try:
        bindir = build(s)
        if bindir is not None:
            rc, out = run(bindir / EXE, ['unit'], s / 'unit.out')
            unit_summary('unit (proxy toolchain)', out, rc)
            only = set(x for x in a.only.split(',') if x)
            for c in CASES:
                if not only or c[0] in only:
                    fault_case(bindir, s, *c)
        if not a.no_asan:
            test_asan(s)
    finally:
        if 'd' in DESKS:
            DESKS['d'].close()
    bad = [n for n, ok in RESULTS if not ok]
    print('test_crash_filter: %d/%d passed' % (len(RESULTS) - len(bad), len(RESULTS)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
