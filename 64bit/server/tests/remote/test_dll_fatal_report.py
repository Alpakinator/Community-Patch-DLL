"""Ticket 0422 (D118 for the server's DLL stopping on a failed check): hermetic tests of server/src/remote/srv_dll_fatal.cpp
and the dll-fatal stage of srv_exe_crash.cpp's report.

    python -I server/tests/remote/test_dll_fatal_report.py --out DIR [--build-out B] [--asan] [-v]

The unit (always): server/tests/remote/dll_fatal_unit.cpp + srv_dll_fatal.cpp + srv_exe_crash.cpp (clang-cl x64, /W3 /WX
as the server's own code) play the server; dll_fatal_standin.cpp, built as a DLL with the same flags (/O2), stands in
for the x64 DLL: its exports fail a check of the fork's PRECONDITION shape (a noinline failure function: a call that
formats the message, the "dialog" call with expr, file, line and message, ud2). The unit installs the vectored handler
for the stand-in, then an unhandled-exception filter as the DLL's CustomFilter does (it records whether the report and
dll-fatal.txt already exist, writes crashlogs\\crashes.log and returns EXCEPTION_EXECUTE_HANDLER). Layouts as
test_exe_crash_report.py (the fake vp64helper.exe records its command line; the logs carry %USERPROFILE%, the user name
and a SteamID64 for the scrub). Cases:
  check      StandinCheck(-1) on a second thread: the process ends with 0xC000001D after the DLL-like filter, which saw
             the report folder and dll-fatal.txt already written; one report folder: report.txt [failure] stage
             dll-fatal, code 0xC000001D, check "eImprovementNum > -1", the stand-in's file and the line of its check,
             message (none), how it was read (rcx<-...), where (dll_fatal_standin.dll+0x...), the Lua call and the served
             calls (the unit's texts), "still running" in [exe], server.log's last FAILED or LOUD line = the dll-fatal LOUD
             line, the tails, popup-summary.txt "The game stopped on an internal check in the game's DLL (dll-fatal: ...
             at dll_fatal_standin.cpp:<line>)", nothing unscrubbed; the helper once as popup --report "<folder>" --options
             "<proxy>\\vpproxy.ini"; dll-fatal.txt names the folder; the fail hook ran
  msg        StandinCheckMsg(-1): the same, message "improvement -1 is out of range"
  no-lua     unit_lua=0: "the Lua call that led there: none on this thread"
  twice      the check fails on two threads at once: one report, one helper, the filter sees the report
  bare       a ud2 with no call before it: a report that says the text could not be read and why
  (negative) ok: the check passes, no report; av: 0xC0000005 inside the stand-in, no report, no marker; own: a ud2
             outside the stand-in's image, no report; pack-off: no report, no marker, no helper, LOUD "no report and no
             popup", the fail hook still runs; no-helper: the report, "does not exist: not started"
With --build-out B (a build_server.py --out tree): the REAL helper (B/proxy/bin/vp64helper.exe popup --no-ui) on the
check report: its first line says the game stopped on an internal check in the game's DLL, the server's summary follows.
--asan: the unit once more as an MSVC /fsanitize=address build (x64; the stand-in stays the clang build) with a canary
that must be caught; no AddressSanitizer memory-error report in any run.
Every program runs on a hidden desktop (COMMON rule 14). Exit 0 when every check passes. Outputs only under --out.
Start it through perf/quietlock/quietlock.py run.
"""
import argparse
import importlib.util
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
_spec = importlib.util.spec_from_file_location('test_exe_crash_report', HERE / 'test_exe_crash_report.py')
ecr = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ecr)  # 0418's layouts, builds, hidden desktop and checks
rt = ecr.rt
check = rt.check
text, files, unscrubbed = ecr.text, ecr.files, ecr.unscrubbed

SRCS = [HERE / 'dll_fatal_unit.cpp', REPO / 'server' / 'src' / 'remote' / 'srv_dll_fatal.cpp',
        REPO / 'server' / 'src' / 'remote' / 'srv_exe_crash.cpp']
STANDIN = HERE / 'dll_fatal_standin.cpp'
LIBS = ecr.LIBS
CODE_UD2 = 0xC000001D
CODE_AV = 0xC0000005


def check_line():
    """The source line of StandinCheck's and StandinCheckMsg's checks."""
    lines = STANDIN.read_text(encoding='utf-8').splitlines()
    a = [i + 1 for i, ln in enumerate(lines) if ln.strip().startswith('STANDIN_PRECONDITION(eImprovementNum > -1)')]
    b = [i + 1 for i, ln in enumerate(lines) if ln.strip().startswith('STANDIN_PRECONDITION_MSG(eImprovementNum > -1')]
    return (a[0] if a else -1), (b[0] if b else -1)


def run(c, what, standin, env=None, **over):
    c.cfg.update({'unit_standin': str(standin), 'report_root': str(c.reports)})
    c.cfg.update(over)
    cfg = c.write_cfg()
    rc, out = ecr.run_on_desk(c.exe, ['fatal', cfg, what], c.d / ('fatal-%s.txt' % what), env or ecr.safe_env(),
                              cwd=c.d, timeout=120)
    return (rc & 0xFFFFFFFF if rc is not None else None), out


def filter_line(out):
    m = re.search(r'FILTER 0x([0-9A-F]{8}) report=(\d) marker=(\d)', out)
    return (int(m.group(1), 16), m.group(2) == '1', m.group(3) == '1') if m else None


def unit_cases(s, unit, standin, fake, tag, env_base=None):
    root = s / tag
    root.mkdir(parents=True, exist_ok=True)
    E = lambda extra=None: ecr.safe_env(extra, env_base)
    line_a, line_b = check_line()
    folder_check = None

    for name, what, msg, line in (('check', 'check', '(none)', line_a),
                                  ('msg', 'msg', 'improvement -1 is out of range', line_b)):
        c = ecr.Case(root, name, unit, fake)
        rc, out = run(c, what, standin, E())
        fl = filter_line(out)
        dirs = c.report_dirs()
        check('%s %s: the process ends with 0xC000001D through the DLL-like filter, which found the report folder and '
              'dll-fatal.txt already written' % (tag, name),
              rc == CODE_UD2 and fl == (CODE_UD2, True, True) and 'RETURNED' not in out, '%s %s %s' % (rc, fl, out[-800:]))
        check('%s %s: one report folder' % (tag, name), len(dirs) == 1, str(dirs))
        if not dirs:
            continue
        folder = dirs[0]
        if name == 'check':
            folder_check = folder
        r = text(folder / 'report.txt').replace('\r\n', '\n')
        check('%s %s: [failure] stage dll-fatal, code 0xC000001D, check, file, line %d, message %r' % (tag, name, line, msg),
              '[failure]\nstage: dll-fatal\ncode: 0xC000001D EXCEPTION_ILLEGAL_INSTRUCTION' in r and
              '\ncheck: eImprovementNum > -1\n' in r and re.search(r'\nfile: .*dll_fatal_standin\.cpp\n', r) and
              ('\nline: %d\n' % line) in r and ('\nmessage: %s\n' % msg) in r, r[:1500])
        check('%s %s: how it was read (the moves before the call), where (the stand-in + RVA), the Lua call, the served '
              'calls, the thread' % (tag, name),
              re.search(r'\nhow the check was read: from the register moves before the call that precedes the ud2 \(rcx<-\w+, '
                        r'rdx<-\w+, r8<-\w+', r) and re.search(r'\nwhere: dll_fatal_standin\.dll\+0x[0-9A-F]+ \(module RVA', r)
              and '\nthe Lua call that led there: Plot.IsResourceConnectedByImprovement(a Plot object, -1) on the ui channel\n'
              in r and '\nthe engine calls being served on this thread (outermost first): runtime::kRt_LuaCall\n' in r and
              re.search(r'\nthread: \d+\n', r), r[:1800])
        check('%s %s: the EXE line, its start ("still running"), the times; [exe] says the EXE was still running; no 0419 '
              'record line' % (tag, name),
              re.search(r'\nexe: .*\(pid \d+\)\n', r) and 'and was still running\n' in r and
              re.search(r'the last save load began: \d+\.\d s before the DLL stopped \(1 save load', r) and
              'the EXE was still running when this report was written' in r and 'ticket 0419' not in r, r[:1800])
        check('%s %s: server.log\'s last FAILED or LOUD line is the dll-fatal LOUD line' % (tag, name),
              re.search(r'server\.log, last FAILED or LOUD line: .*remote: LOUD: dll-fatal: the DLL stopped on a failed '
                        r'check', r) is not None, r[:1800])
        check('%s %s: the DLL\'s crashlogs named; title names ticket 0422' % (tag, name),
              'The DLL writes its own crashlogs\\crashes.log and a minidump into ' in r and
              'the DLL stopped on a failed check, ticket 0422)' in r, '')
        names = files(folder)
        for t in ('server.log', 'vpproxy.log', 'render.log', 'Lua.log'):
            check('%s %s: %s.tail.txt attached' % (tag, name, t), (t + '.tail.txt') in names, str(names))
        summ = text(folder / 'popup-summary.txt')
        check('%s %s: popup-summary.txt names the check and its place' % (tag, name),
              summ.startswith('The game stopped on an internal check in the game\'s DLL (dll-fatal: eImprovementNum > -1 at '
                              'dll_fatal_standin.cpp:%d); the game had to end.' % line), summ)
        check('%s %s: nothing unscrubbed (profile folder, user and computer names, SteamID64)' % (tag, name),
              not unscrubbed(folder), str(unscrubbed(folder)))
        hc = c.helper_calls(want=1)
        want = 'popup --report "%s" --options "%s"' % (folder, c.proxy / 'vpproxy.ini')
        check('%s %s: the helper started once: %s' % (tag, name, want), len(hc) == 1 and hc[0].endswith(want), str(hc))
        mk = text(c.run / 'dll-fatal.txt').replace('\r\n', '\n')
        check('%s %s: dll-fatal.txt for the proxy: its fixed first line, the report folder, the check' % (tag, name),
              mk.startswith('dll-fatal: the server wrote the D119 report for this stop (ticket 0422)\nreport: %s\n' % folder)
              and 'check: eImprovementNum > -1 (dll_fatal_standin.cpp:%d)' % line in mk, mk)
        check('%s %s: the fail hook ran with the check (server-report.txt in the server)' % (tag, name),
              'FAILHOOK the DLL stopped on a failed check: eImprovementNum > -1 (dll_fatal_standin.cpp:%d) at '
              'dll_fatal_standin.dll+0x' % line in out, out[-800:])
        log = c.server_log()
        check('%s %s: server.log: the LOUD line, "report: written", "helper: started", the hand-on line' % (tag, name),
              'remote: LOUD: dll-fatal: the DLL stopped on a failed check' in log and 'dll-fatal: report: written to' in log
              and 'dll-fatal: helper: started' in log and 'handing the exception on to the DLL\'s own filter' in log,
              log[-1500:])

    # no Lua call on the thread
    c = ecr.Case(root, 'no-lua', unit, fake)
    rc, out = run(c, 'check', standin, E(), unit_lua='0')
    dirs = c.report_dirs()
    check('%s no-lua: a report that says "none on this thread"' % tag,
          rc == CODE_UD2 and len(dirs) == 1 and
          '\nthe Lua call that led there: none on this thread\n' in text(dirs[0] / 'report.txt').replace('\r\n', '\n'),
          '%s %s' % (rc, dirs))

    # two threads
    c = ecr.Case(root, 'twice', unit, fake)
    rc, out = run(c, 'twice', standin, E())
    fl = filter_line(out)
    check('%s twice: two threads fail the check at once: one report, one helper, the filter saw the report' % tag,
          rc == CODE_UD2 and len(c.report_dirs()) == 1 and len(c.helper_calls(want=1)) == 1 and fl and fl[1] and fl[2],
          '%s %s %s' % (rc, fl, c.report_dirs()))

    # a ud2 without the call before it
    c = ecr.Case(root, 'bare', unit, fake)
    rc, out = run(c, 'bare', standin, E())
    dirs = c.report_dirs()
    r = text(dirs[0] / 'report.txt').replace('\r\n', '\n') if dirs else ''
    check('%s bare: a ud2 with no call before it: a report, "the text could not be read (no call ...)", the summary '
          'says so' % tag,
          rc == CODE_UD2 and len(dirs) == 1 and
          '\ncheck: the text could not be read (no call (E8 rel32) just before the ud2)\n' in r and
          text(dirs[0] / 'popup-summary.txt').startswith('The game stopped on an internal check in the game\'s DLL '
                                                         '(dll-fatal: the check\'s text could not be read)'),
          '%s %s %s' % (rc, dirs, r[:900]))

    # negatives
    c = ecr.Case(root, 'ok', unit, fake)
    rc, out = run(c, 'ok', standin, E())
    check('%s ok (negative): the check passes: RETURNED 4, exit 0, no report, no marker, no helper' % tag,
          rc == 0 and 'RETURNED 4' in out and not c.reports.exists() and not (c.run / 'dll-fatal.txt').exists() and
          not c.helper_calls(), '%s %s' % (rc, out[-500:]))
    for name, what, code in (('av', 'av', CODE_AV), ('own', 'own', CODE_UD2)):
        c = ecr.Case(root, name, unit, fake)
        rc, out = run(c, what, standin, E())
        fl = filter_line(out)
        # Under ASan an access violation is ASan's own deadly signal (its report, exit 1) before any filter: the point
        # here is only that the handler leaves it alone.
        asan_av = tag == 'asan' and name == 'av' and rc == 1 and fl is None and 'AddressSanitizer: access-violation' in out
        check('%s %s (negative): exit 0x%08X through the filter, no report, no marker, no helper, no dll-fatal line'
              % (tag, name, code),
              (asan_av or (rc == code and fl == (code, False, False))) and not c.reports.exists() and
              not (c.run / 'dll-fatal.txt').exists() and not c.helper_calls() and 'dll-fatal:' not in
              c.server_log().replace('dll-fatal: vectored handler installed', ''), '%s %s %s' % (rc, fl, out[-600:]))
        # ticket 0627: 'dll-fatal:' with its colon, the tag every dll-fatal line carries. The bare word also matched the
        # run's own path in a logged line (an --out folder named ...dll-fatal-report...), so the negatives failed there.
    c = ecr.Case(root, 'pack-off', unit, fake, pack='0')
    rc, out = run(c, 'check', standin, E())
    log = c.server_log()
    check('%s pack-off (negative): no report, no marker, no helper; LOUD "no report and no popup (auto: ... pack=0)"; '
          'the fail hook still ran; exit 0xC000001D' % tag,
          rc == CODE_UD2 and not c.reports.exists() and not (c.run / 'dll-fatal.txt').exists() and not c.helper_calls()
          and re.search(r'dll-fatal: LOUD: no report and no popup \(auto: .* says pack=0', log) is not None and
          'FAILHOOK the DLL stopped on a failed check' in out, '%s %s' % (rc, log[-800:]))
    c = ecr.Case(root, 'no-helper', unit, fake, helper=False)
    rc, out = run(c, 'check', standin, E())
    check('%s no-helper: the report and the marker, "does not exist: not started"' % tag,
          rc == CODE_UD2 and len(c.report_dirs()) == 1 and (c.run / 'dll-fatal.txt').exists() and
          'does not exist: not started' in c.server_log(), c.server_log()[-600:])
    return folder_check


def real_helper(s, helper, folder):
    rc, out = ecr.run_on_desk(helper, ['popup', '--report', folder, '--options', s / 'nonexistent.ini', '--no-ui'],
                              s / 'real-helper.txt', ecr.safe_env({'VPPROXY_TEST_CONFIG_INI': 'C:/no-such-0422/config.ini'}))
    lines = [ln[len('popup: line: '):] for ln in out.replace('\r\n', '\n').splitlines() if ln.startswith('popup: line: ')]
    summary = text(Path(folder) / 'popup-summary.txt').strip()
    check('real helper: first line "The game stopped on an internal check in the game\'s DLL, so the game had to end.", '
          'the server\'s summary follows, no "goes on"',
          rc == 0 and lines[:1] == ['The game stopped on an internal check in the game\'s DLL, so the game had to end.'] and
          lines[1:2] == [summary] and 'goes on' not in out, '%s %r' % (rc, lines[:3]))


def test_asan(s, standin, fake):
    env = rt.vcvars_env('x64')
    if not check('asan: vcvarsall x64', env is not None):
        return
    cl = shutil.which('cl', path=env.get('PATH', env.get('Path', '')))
    if not check('asan: cl.exe found', cl is not None):
        return
    d = s / 'asan-build'
    d.mkdir(parents=True, exist_ok=True)
    (d / 'canary.cpp').write_text('#include <stdio.h>\n#include <stdlib.h>\nint main() { char* volatile p = (char*)malloc(16);'
                                  ' p[16] = 1; printf("canary NOT caught\\n"); free(p); return 0; }\n', encoding='ascii')
    flags = ['/nologo', '/EHsc', '/W3', '/Zi', '/Oy-', '/fsanitize=address', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX',
             '/D_CRT_SECURE_NO_WARNINGS', '/I' + str(REPO / 'server' / 'src')]
    for exe, srcs, fl, libs in (('canary.exe', [d / 'canary.cpp'], ['/nologo', '/Zi', '/Od', '/fsanitize=address'], []),
                                ('dll_fatal_unit.exe', SRCS, flags, LIBS)):
        cp = subprocess.run([cl] + fl + [str(x) for x in srcs] + ['/Fe' + str(d / exe), '/Fo' + str(d) + '\\',
                                                                  '/Fd' + str(d / (exe + '.pdb'))] + libs,
                            cwd=str(d), env=env, capture_output=True, text=True, errors='replace')
        if not check('asan: %s compiled with MSVC /fsanitize=address (x64)' % exe, cp.returncode == 0,
                     (cp.stdout + cp.stderr)[-2000:]):
            return
    rc, out = ecr.run_on_desk(d / 'canary.exe', [], d / 'canary.txt', env)
    check('asan (negative): the canary\'s heap-buffer-overflow is reported (ASan active)',
          rc != 0 and 'heap-buffer-overflow' in out, out[-600:])
    first = len(rt.RESULTS)
    unit_cases(s, d / 'dll_fatal_unit.exe', standin, fake, 'asan', env_base=env)
    # The deliberate stops end the process through ASan's own handler too; only a memory error is a finding.
    kinds = r'heap-buffer-overflow|heap-use-after-free|stack-buffer-overflow|stack-use-after|global-buffer-overflow|' \
            r'double-free|attempting free|memcpy-param-overlap|container-overflow|use-after-poison|dynamic-stack-buffer'
    reports = [p for p in (s / 'asan').rglob('*.txt') if re.search(kinds, text(p))]
    check('asan: no AddressSanitizer memory-error report in any run of the ASan build (%d checks above)'
          % (len(rt.RESULTS) - first), not reports, ', '.join(str(p) for p in reports[:5]))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--build-out', type=Path, help='a build_server.py --out tree (the real helper)')
    ap.add_argument('--asan', action='store_true')
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    rt.VERBOSE = a.v
    s = a.out.resolve()
    shutil.rmtree(s, ignore_errors=True)
    s.mkdir(parents=True)
    try:
        fake = s / 'fake' / 'vp64helper.exe'
        ok, txt = ecr.build_clang(fake, [ecr.FAKE_HELPER])
        check('build: the fake vp64helper.exe (proxy/tools/rt_tests/fake_helper.cpp, x64)', ok, txt[-1500:])
        standin = s / 'standin' / 'dll_fatal_standin.dll'
        ok1, txt = ecr.build_clang(standin, [STANDIN], defines=['/LD'])
        check('build: dll_fatal_standin.dll (clang-cl x64 /O2, /W3 /WX)', ok1, txt[-2000:])
        unit = s / 'clang' / 'dll_fatal_unit.exe'
        ok2, txt = ecr.build_clang(unit, SRCS)
        if ok and ok1 and check('build: dll_fatal_unit.cpp + srv_dll_fatal.cpp + srv_exe_crash.cpp (clang-cl x64, /W3 /WX)',
                                ok2, txt[-2000:]):
            folder = unit_cases(s, unit, standin, fake, 'clang')
            if a.build_out:
                helper = a.build_out.resolve() / 'proxy' / 'bin' / 'vp64helper.exe'
                if check('real helper: %s exists' % helper, helper.exists()) and folder:
                    real_helper(s, helper, folder)
            if a.asan:
                test_asan(s, standin, fake)
    finally:
        for dk in ecr.DESKS.values():
            dk.close()
    bad = [nm for nm, ok in rt.RESULTS if not ok]
    print('test_dll_fatal_report: %d/%d checks passed' % (len(rt.RESULTS) - len(bad), len(rt.RESULTS)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
