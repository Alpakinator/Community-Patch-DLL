"""Ticket 0418 (D118 for an EXE crash): hermetic tests of server/src/remote/srv_exe_crash.cpp.

    python -I server/tests/remote/test_exe_crash_report.py --out DIR [--build-out B] [--asan] [-v]

Part 1, the unit (always): server/tests/remote/exe_crash_unit.cpp + srv_exe_crash.cpp (clang-cl x64, /W3 /WX as the
server's own code) play the server and a stand-in EXE that ends itself with an exit code. Each case has its own layout
under DIR: <case>/proxy (vpproxy.ini, a FAKE vp64helper.exe = proxy/tools/rt_tests/fake_helper.cpp, which records its
command line in helper-calls.txt and shows nothing; the test program in proxy/VP64/ as server.exe sits in the D28
layout), <case>/player (Logs/Lua.log, the reports root VP64/reports) and the run folder <player>/Logs/VP64/run/boot-1
(server.cfg, server.log, vpproxy.log, render.log). Every run removes VPPROXY_TEST_CONFIG_INI and C5_DOCUMENTS from its
environment (unless the case sets its own), and every case gives the server a player folder, so nothing reaches the real
Documents folder. The logs carry %USERPROFILE%, the user name and a SteamID64 to check the scrub.
  crash         0xC0000005 with a save load: one report folder under <player>/VP64/reports (player folder found from the
                run folder), report.txt [failure] stage exe-crash, the code and its name, the times, server.log's last
                FAILED/LOUD line = the exe-crash line; the tails of server.log, vpproxy.log, render.log and Lua.log;
                vpproxy.ini and server.cfg copied; popup-summary.txt; nothing unscrubbed; the helper started once as
                `popup --report "<folder>" --options "<proxy>\\vpproxy.ini"`; the three "service threads" all return the
                clause; exit 3
  twice         a second crash into the same reports root: a second, distinct folder
  normal-*      exit codes 0, 3 (the proxy's own exit after a server death), 0xC000013A, 0x77: no report, no helper,
                "not a crash code"
  stackbuf      0xC0000409 with VPPROXY_TEST_CONFIG_INI naming another player folder, a run folder outside the pattern,
                and the proxy's crash record <run>/exe-crash.txt (ticket 0419): the report under that folder, the record
                copied and quoted
  options       the player folder from server.cfg options_file (run folder outside the pattern, no env)
  pack-off / no-ini / forced-off: no report and no helper, the LOUD line says why; forced-on (no vpproxy.ini,
                exe_crash_report=1): a report, the helper without --options
  no-helper     the report is written, the helper "does not exist: not started"
  bad-root      report_root under a file: "report: FAILED", no folder; the helper is still started (with the root)
  proxy-dir     server.cfg proxy_dir names another folder (its vpproxy.ini pack=1, its helper); the D28 one says pack=0
  still-running the EXE has not ended when its channel fails: after 5 s "still running", no report
  kill-*        test_exe_kill=attach:200 (default code), boot:100:C0000409: the EXE is ended by the server with that code,
                LOUD TEST ONLY lines, a report; a malformed value is LOUD "not understood" and kills nothing
  helper-args   exe_crash_helper_args=--no-ui is appended, LOUD TEST ONLY
  codes         IsCrashCode over a fixed list
  fatal         ticket 0641: FatalWords / WriteFatalRecord (the server's reason for a deliberate end, which the proxy's
                report shows): the token and the player's words per exit code (1, 2, 4, 5 per render module, 6, other),
                server-fatal.txt's five lines scrubbed and one line each; no record for 0, 3, a save-variant code, or
                without a run folder
  Ticket 0519 (the proxy's crash record <run>/exe-crash.txt as proof when the exit code is not an exception code; the
  child runs 4 s and a stand-in for the proxy writes the record with the child's pid once the peer prints it):
  rec-255       exit 255 + a record of this pid: a report with the record's code, [failure] names the exit code and the
                proof, [exe] the exit code, the record copied and quoted, the helper once, the clause names both
  norec-255 / stale-pid / stale-time / rec-nopid / rec-0: no report, no helper ("not a crash code" and why)
  rec-firstline / rec-nocode: the code from the record's first line; none in the record: the exit code
  rec-pack-off  pack=0: no report, no popup, the clause returned; rec-c5: an exception exit code with a record: as before
Part 2, with --build-out B (a build_server.py --out tree): the real server.exe with a vpipc stand-in EXE
(ticket 0641: render5 / render5-pack0, a deliberate end at kRt_Boot by a refused render_size: exit 5, server-fatal.txt;
pack=1 ends at once with no server-report.txt and no call tables, pack=0 writes them as before)
(exe_crash_standin.cpp, built against B like vp_selftest): crash 0xC0000005 (report, helper, server exit 3,
server-report.txt's result names the EXE's code), exit 0 (nothing new, the result line as before), hold +
test_exe_kill=attach:500, incall (kRt_Boot, SetDLLIFace, Init with the real x64 DLL; the stand-in ends inside the first
engine call the DLL makes back, so the server's call sees PeerDied: the path that ended the process mid-report in the
first live run; the report must be complete); ticket 0519: rec255 (the stand-in writes the proxy's record, then ends
with 255: a report), norec255 and rec255-pack0 (none); then the REAL helper (B/proxy/bin/vp64helper.exe popup --no-ui on a hidden desktop) on the
reports: its first line says the game itself crashed, the server's summary follows.
--asan: the unit cases once more with an MSVC /fsanitize=address build (x64) and a canary that must be caught.
Every program runs on a hidden desktop (COMMON rule 14). Exit 0 when every check passes. Outputs only under --out.
Start it through perf/quietlock/quietlock.py run.
"""
import argparse
import ctypes
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(REPO / 'proxy' / 'tools'))
import test_proxy_rt as rt  # noqa: E402  (toolchain, hidden desktops, vcvarsall, check)

SRCS = [HERE / 'exe_crash_unit.cpp', REPO / 'server' / 'src' / 'remote' / 'srv_exe_crash.cpp']
FAKE_HELPER = REPO / 'proxy' / 'tools' / 'rt_tests' / 'fake_helper.cpp'
LIBS = ['user32.lib', 'advapi32.lib']
check = rt.check
DESKS = {}
STEAMID = '76561198000000042'
_k32 = ctypes.WinDLL('kernel32', use_last_error=True)
_k32.OpenProcess.restype = ctypes.c_void_p
_k32.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
_k32.GetExitCodeProcess.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong)]
_k32.CloseHandle.argtypes = [ctypes.c_void_p]


def desk():
    return DESKS.setdefault('d', rt.desktops.HiddenDesktop('C5T0418'))


def safe_env(extra=None, base=None):
    e = {k: v for k, v in (base or os.environ).items() if k.upper() not in ('C5_DOCUMENTS', 'VPPROXY_TEST_CONFIG_INI',
                                                                            'VPPROXY_INI', 'VP64_OPTIONS')}
    e.update(extra or {})
    return e


def build_clang(out, srcs, defines=()):
    out = Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    cp = rt.tc.run_in_env('x64', [rt.tc.CLANG, '-m64', '/MD', '/O2', '/Zi', '/EHsc', '/std:c++14', '/W3', '/WX',
                                  '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS', '-fuse-ld=lld',
                                  '/I' + str(REPO / 'server' / 'src')] + list(defines) + [str(s) for s in srcs] +
                          ['/Fe' + str(out)] + LIBS, cwd=str(out.parent))
    return cp.returncode == 0 and out.exists(), cp.stdout + cp.stderr


def text(p):
    return rt.text(p)


def files(d):
    d = Path(d)
    return sorted(p.name for p in d.iterdir()) if d.is_dir() else []


def run_on_desk(exe, args, out, env, cwd=None, timeout=120):
    p = rt.OnDesktop(desk().name, [str(exe)] + [str(a) for a in args], env, out, cwd=cwd or Path(exe).parent)
    rc = p.wait(timeout)
    return rc, p.text()


def private_strings():
    vals = [os.environ.get('USERPROFILE', ''), os.environ.get('USERNAME', ''), os.environ.get('COMPUTERNAME', ''), STEAMID]
    return [v for v in vals if len(v) >= 3]


def unscrubbed(folder):
    """Files of the report folder that hold a private string (whole-word for the names)."""
    bad = []
    for p in Path(folder).iterdir():
        t = p.read_bytes().decode('utf-8', errors='replace')
        for v in private_strings():
            pat = re.escape(v) if ('\\' in v or v == STEAMID) else r'(?<![A-Za-z0-9])' + re.escape(v) + r'(?![A-Za-z0-9])'
            if re.search(pat, t, re.I):
                bad.append('%s has %r' % (p.name, v[:4] + '...'))
    return bad


class Case:
    """One layout: <dir>/proxy (+ VP64/<exe>), <dir>/player, a run folder, server.cfg."""

    def __init__(self, root, name, unit_exe, helper_exe, pack='1', helper=True, ini=True, run_pattern=True):
        self.d = Path(root) / name
        shutil.rmtree(self.d, ignore_errors=True)
        self.proxy = self.d / 'proxy'
        (self.proxy / 'VP64').mkdir(parents=True)
        self.exe = self.proxy / 'VP64' / Path(unit_exe).name
        shutil.copy2(unit_exe, self.exe)
        if Path(unit_exe).with_suffix('.pdb').exists():
            shutil.copy2(Path(unit_exe).with_suffix('.pdb'), self.exe.with_suffix('.pdb'))
        if ini:
            (self.proxy / 'vpproxy.ini').write_text('# 0418 test layout\nmode=on\npack=%s\nhandin=test hand-in line\n' % pack,
                                                   encoding='ascii')
        if helper:
            shutil.copy2(helper_exe, self.proxy / 'vp64helper.exe')
        self.player = self.d / 'player'
        (self.player / 'Logs').mkdir(parents=True)
        prof = os.environ.get('USERPROFILE', 'C:\\Users\\nobody')
        (self.player / 'Logs' / 'Lua.log').write_text(
            '[1.0] Lua line one\n[2.0] Lua loaded %s\\Documents\\x.lua for %s, steam %s\n' %
            (prof, os.environ.get('USERNAME', 'nobody'), STEAMID), encoding='utf-8')
        self.run = (self.player / 'Logs' / 'VP64' / 'run' / 'boot-1') if run_pattern else (self.d / 'run')
        self.run.mkdir(parents=True)
        (self.run / 'server.log').write_text('[ 0.001] build: test\n[ 0.002] remote: an earlier FAILED: line\n',
                                             encoding='utf-8')
        (self.run / 'vpproxy.log').write_text('[0.1] vpproxy boot at %s\\AppData\\x\n' % prof, encoding='utf-8')
        (self.run / 'render.log').write_text('[0.2] render: started\n', encoding='utf-8')
        self.cfg = {'run_dir': str(self.run), 'game_dir': str(self.d), 'dll': str(self.proxy / 'VP64' / 'none.dll')}
        self.reports = self.player / 'VP64' / 'reports'

    def write_cfg(self, **over):
        self.cfg.update(over)
        p = self.run / 'server.cfg'
        p.write_text('# 0418 unit\n' + ''.join('%s=%s\n' % kv for kv in self.cfg.items()), encoding='utf-8')
        return p

    def helper_calls(self, where=None, want=0):
        """The fake helper's records. The helper runs detached and may write after the server has ended: with want > 0
        wait up to 10 s for that many lines (a case that expects none checks server.log's "helper: started" too)."""
        p = (where or self.proxy) / 'helper-calls.txt'
        for _ in range(100):
            got = [ln for ln in text(p).splitlines() if ln.strip()]
            if len(got) >= want:
                return got
            time.sleep(0.1)
        return got

    def report_dirs(self, root=None):
        r = Path(root or self.reports)
        return sorted(p for p in r.iterdir() if p.is_dir()) if r.is_dir() else []

    def server_log(self):
        return text(self.run / 'server.log')


def peer(c, args, env=None, timeout=120):
    cfg = c.write_cfg()
    rc, out = run_on_desk(c.exe, ['peer', cfg] + list(args), c.d / ('peer-%d.txt' % int(time.time() * 1000)),
                          env or safe_env(), cwd=c.d, timeout=timeout)
    return rc, out


def results(out):
    return [ln.split(' ', 2)[2] if len(ln.split(' ', 2)) > 2 else '' for ln in out.splitlines() if ln.startswith('RESULT ')]


def child_exit(out):
    m = re.search(r'CHILD exit 0x([0-9A-F]{8})', out)
    return int(m.group(1), 16) if m else None


def check_report(tag, name, folder, code_line, extra_files=(), want_tails=('server.log', 'vpproxy.log', 'render.log',
                                                                          'Lua.log')):
    r = text(folder / 'report.txt')
    names = files(folder)
    check('%s %s: report.txt [failure] says stage exe-crash and %s' % (tag, name, code_line),
          '[failure]\nstage: exe-crash\ncode: ' + code_line in r.replace('\r\n', '\n'), r[:600])
    for t in want_tails:
        check('%s %s: %s.tail.txt attached' % (tag, name, t), (t + '.tail.txt') in names, str(names))
    for f in extra_files:
        check('%s %s: %s copied' % (tag, name, f), f in names, str(names))
    check('%s %s: popup-summary.txt says the game itself crashed (exe-crash: ...)' % (tag, name),
          text(folder / 'popup-summary.txt').startswith('The game itself crashed (exe-crash: ' + code_line.split(' ')[0]),
          text(folder / 'popup-summary.txt'))
    check('%s %s: nothing unscrubbed (profile folder, user and computer names, SteamID64)' % (tag, name),
          not unscrubbed(folder), str(unscrubbed(folder)))
    return r


def unit_cases(s, unit, fake, tag, env_base=None):
    root = s / tag
    root.mkdir(parents=True, exist_ok=True)
    E = lambda extra=None: safe_env(extra, env_base)
    AV = 'EXCEPTION_ACCESS_VIOLATION'

    # crash
    c = Case(root, 'crash', unit, fake)
    c.cfg['unit_child_code'] = 'C0000005'
    rc, out = peer(c, ['load'], E())
    res = results(out)
    dirs = c.report_dirs()
    check('%s crash: exit 3, the child ended with 0xC0000005' % tag, rc == 3 and child_exit(out) == 0xC0000005,
          '%s %s' % (rc, out[-400:]))
    check('%s crash: the three service threads all return the clause' % tag,
          len(res) == 3 and all(x == 'the EXE ended with exception code 0xC0000005 (%s)' % AV for x in res), str(res))
    check('%s crash: one report folder under <player>/VP64/reports (player folder from the run folder)' % tag,
          len(dirs) == 1, str(dirs))
    crash_folder = dirs[0] if dirs else None
    if crash_folder:
        r = check_report(tag, 'crash', crash_folder, '0xC0000005 ' + AV, ('vpproxy.ini', 'server.cfg', 'popup-summary.txt'))
        rn = r.replace('\r\n', '\n')
        check('%s crash: the times (the EXE ran, the save load, kRt_Boot, the attach)' % tag,
              re.search(r'the EXE started .* ran \d+\.\d s\n', rn) and
              re.search(r'the last save load began: \d+\.\d s before the EXE was seen gone \(1 save load', rn) and
              'kRt_Boot (the x64 DLL loaded): ' in rn and 'the server attached: ' in rn, rn[:900])
        check('%s crash: server.log\'s last FAILED or LOUD line is the exe-crash line' % tag,
              re.search(r'server\.log, last FAILED or LOUD line: .*remote: LOUD: exe-crash: the EXE ended with exception '
                        r'code 0xC0000005', rn) is not None, rn[:1200])
        check('%s crash: the record of ticket 0419 is "none"' % tag,
              'exe-crash.txt, ticket 0419): none in the run folder' in rn, '')
        check('%s crash: [exe] names the exit code; [hashes] has the EXE and server.exe; [system] has windows' % tag,
              'exit code: 0xC0000005 (%s)' % AV in rn and '\nEXE: ' in rn and '\nserver.exe: ' in rn and
              '\nwindows: ' in rn, '')
        hc = c.helper_calls(want=1)
        want = 'popup --report "%s" --options "%s"' % (crash_folder, c.proxy / 'vpproxy.ini')
        check('%s crash: the helper started once: %s' % (tag, want), len(hc) == 1 and hc[0].endswith(want), str(hc))
    log = c.server_log()
    check('%s crash: server.log: the exit code line, the LOUD line, "report: written", "helper: started"' % tag,
          'ended with exit code 0xC0000005' in log and 'remote: LOUD: exe-crash: the EXE ended with exception code' in log
          and 'exe-crash: report: written to' in log and 'exe-crash: helper: started' in log, log[-1500:])
    rc, out = peer(c, [], E())
    dirs2 = c.report_dirs()
    check('%s twice: a second crash into the same root makes a second, distinct folder' % tag,
          rc == 3 and len(dirs2) == 2 and len(c.helper_calls(want=2)) == 2, '%s %s' % (dirs2, c.helper_calls()))
    if len(dirs2) == 2:
        check('%s twice: no save load in that session' % tag,
              'the last save load began: none in this server\'s session' in text(dirs2[1] / 'report.txt') or
              'the last save load began: none in this server\'s session' in text(dirs2[0] / 'report.txt'), '')

    # normal exits: nothing new
    for code in ('0', '3', 'C000013A', '77'):
        c = Case(root, 'normal-' + code, unit, fake)
        c.cfg['unit_child_code'] = code
        rc, out = peer(c, [], E())
        res = results(out)
        check('%s normal exit 0x%s: no report, no helper, empty clauses, "not a crash code", exit 3' % (tag, code),
              rc == 3 and not c.report_dirs() and not c.reports.exists() and not c.helper_calls() and
              'helper: started' not in c.server_log() and res == ['', '', ''] and 'not a crash code; no exe-crash report' in c.server_log() and
              child_exit(out) == int(code, 16), '%s %s %s' % (rc, res, c.server_log()[-500:]))

    # stackbuf: VPPROXY_TEST_CONFIG_INI, a run folder outside the pattern, the 0419 record
    c = Case(root, 'stackbuf', unit, fake, run_pattern=False)
    c.cfg['unit_child_code'] = 'C0000409'
    other = c.d / 'player9'
    other.mkdir()
    (c.run / 'exe-crash.txt').write_text('exception 0xC0000005 at CivilizationV_DX11.exe+0x37C1C0 thread 1234 (test record)\n'
                                         'second line\n', encoding='utf-8')
    rc, out = peer(c, [], E({'VPPROXY_TEST_CONFIG_INI': str(other / 'config.ini')}))
    dirs = c.report_dirs(other / 'VP64' / 'reports')
    check('%s stackbuf: the report under VPPROXY_TEST_CONFIG_INI\'s folder' % tag, rc == 3 and len(dirs) == 1,
          '%s %s' % (rc, dirs))
    if dirs:
        r = check_report(tag, 'stackbuf', dirs[0], '0xC0000409 STATUS_STACK_BUFFER_OVERRUN (fail fast)', ('exe-crash.txt',),
                         want_tails=('server.log', 'vpproxy.log', 'render.log'))
        check('%s stackbuf: the 0419 record\'s first line is quoted' % tag,
              'exe-crash.txt, ticket 0419): exception 0xC0000005 at CivilizationV_DX11.exe+0x37C1C0 thread 1234 (test '
              'record)' in r, '')
        check('%s stackbuf: no Lua.log of the other folder: named "not attached"' % tag,
              'not attached: ' in r and 'Lua.log' in r, '')

    # options_file
    c = Case(root, 'options', unit, fake, run_pattern=False)
    p2 = c.d / 'player2'
    p2.mkdir()
    (p2 / 'VP64Options.ini').write_text('[Renderer]\nBakedTileCameraRadius = -1\n', encoding='ascii')
    c.cfg['options_file'] = str(p2 / 'VP64Options.ini')
    rc, out = peer(c, [], E())
    dirs = c.report_dirs(p2 / 'VP64' / 'reports')
    check('%s options: the player folder from server.cfg options_file; VP64Options.ini copied' % tag,
          rc == 3 and len(dirs) == 1 and 'VP64Options.ini' in files(dirs[0]), '%s %s' % (rc, dirs))

    # the switches
    for name, kw, over, why in (('pack-off', {'pack': '0'}, {}, 'says pack=0'),
                                ('no-ini', {'ini': False}, {}, 'cannot be read'),
                                ('forced-off', {}, {'exe_crash_report': '0'}, 'exe_crash_report=0')):
        c = Case(root, name, unit, fake, **kw)
        c.cfg.update(over)
        rc, out = peer(c, [], E())
        res = results(out)
        check('%s %s: no report, no helper, the LOUD line says why (%s), the clause is still returned' % (tag, name, why),
              rc == 3 and not c.reports.exists() and not c.helper_calls() and 'helper: started' not in c.server_log() and
              re.search(r'LOUD: exe-crash: the EXE ended .*; no report and no popup \(.*' + re.escape(why),
                        c.server_log()) is not None and res[0].startswith('the EXE ended with exception code'),
              '%s %s' % (res, c.server_log()[-600:]))
    c = Case(root, 'forced-on', unit, fake, ini=False)
    c.cfg['exe_crash_report'] = '1'
    rc, out = peer(c, [], E())
    dirs = c.report_dirs()
    hc = c.helper_calls(want=1)
    check('%s forced-on: a report without vpproxy.ini; the helper without --options' % tag,
          rc == 3 and len(dirs) == 1 and len(hc) == 1 and '--options' not in hc[0] and 'popup --report' in hc[0],
          '%s %s' % (dirs, hc))

    c = Case(root, 'no-helper', unit, fake, helper=False)
    rc, out = peer(c, [], E())
    check('%s no-helper: the report written, "does not exist: not started"' % tag,
          rc == 3 and len(c.report_dirs()) == 1 and 'vp64helper.exe does not exist: not started' in c.server_log(),
          c.server_log()[-500:])

    c = Case(root, 'bad-root', unit, fake)
    (c.d / 'blocker').write_text('a file\n', encoding='ascii')
    c.cfg['report_root'] = str(c.d / 'blocker' / 'reports')
    rc, out = peer(c, [], E())
    hc = c.helper_calls(want=1)
    check('%s bad-root: "report: FAILED", no folder, the helper still started with the root' % tag,
          rc == 3 and 'exe-crash: report: FAILED: the reports folder' in c.server_log() and not c.reports.exists() and
          len(hc) == 1 and ('--report "%s"' % (c.d / 'blocker' / 'reports')) in hc[0], '%s %s' % (hc, c.server_log()[-400:]))

    c = Case(root, 'proxy-dir', unit, fake, pack='0')
    other = c.d / 'otherproxy'
    other.mkdir()
    (other / 'vpproxy.ini').write_text('pack = "1"\n', encoding='ascii')
    shutil.copy2(fake, other / 'vp64helper.exe')
    c.cfg['proxy_dir'] = str(other)
    rc, out = peer(c, [], E())
    check('%s proxy-dir: server.cfg proxy_dir wins (its pack=1, its helper; the D28 folder says pack=0)' % tag,
          rc == 3 and len(c.report_dirs()) == 1 and len(c.helper_calls(other, want=1)) == 1 and not c.helper_calls(),
          '%s %s' % (c.helper_calls(other), c.server_log()[-400:]))

    c = Case(root, 'still-running', unit, fake)
    c.cfg['unit_child_ms'] = '20000'
    t0 = time.time()
    rc, out = peer(c, ['early'], E())
    dt = time.time() - t0
    check('%s still-running: "still running" after about 5 s, no report, empty clauses' % tag,
          rc == 3 and 'is still running 5000 ms after its channel failed' in c.server_log() and not c.reports.exists() and
          results(out) == ['', '', ''] and 4.5 < dt < 15, '%.1f s %s' % (dt, c.server_log()[-400:]))

    for name, val, code in (('kill-attach', 'attach:200', 0xC0000005), ('kill-boot', 'boot:100:C0000409', 0xC0000409)):
        c = Case(root, name, unit, fake)
        c.cfg['test_exe_kill'] = val
        rc, out = peer(c, ['kill'], E())
        log = c.server_log()
        check('%s %s: the server ends the EXE with 0x%08X, LOUD TEST ONLY lines, a report' % (tag, name, code),
              rc == 3 and child_exit(out) == code and 'LOUD: TEST ONLY: server.cfg test_exe_kill=' + val in log and
              'LOUD: TEST ONLY: test_exe_kill: ending the EXE' in log and len(c.report_dirs()) == 1,
              '%s %s %s' % (rc, child_exit(out), log[-600:]))
    c = Case(root, 'kill-bad', unit, fake)
    c.cfg['test_exe_kill'] = 'later:abc'
    c.cfg['unit_child_code'] = '0'
    rc, out = peer(c, [], E())
    check('%s kill-bad: "not understood", nothing killed (the EXE ends with its own 0), no report' % tag,
          rc == 3 and 'test_exe_kill=later:abc not understood' in c.server_log() and child_exit(out) == 0 and
          not c.reports.exists(), c.server_log()[-400:])

    c = Case(root, 'helper-args', unit, fake)
    c.cfg['exe_crash_helper_args'] = '--no-ui'
    rc, out = peer(c, [], E())
    hc = c.helper_calls(want=1)
    check('%s helper-args: appended, LOUD TEST ONLY' % tag,
          len(hc) == 1 and hc[0].endswith(' --no-ui') and 'LOUD: TEST ONLY: server.cfg exe_crash_helper_args=--no-ui' in
          c.server_log(), str(hc))

    # codes
    c = Case(root, 'codes', unit, fake)
    rc, out = run_on_desk(c.exe, ['codes'], c.d / 'codes.txt', E())
    got = dict((int(m.group(1), 16), (m.group(2) == '1', m.group(3)))
               for m in re.finditer(r'CODE 0x([0-9A-F]{8}) crash=(\d) name=(.*)', out))
    want_crash = {0xC0000005: AV, 0xC0000409: 'STATUS_STACK_BUFFER_OVERRUN (fail fast)', 0xC00000FD: 'EXCEPTION_STACK_OVERFLOW',
                  0xC0000374: 'STATUS_HEAP_CORRUPTION', 0xE06D7363: 'an unhandled C++ exception (MSVC 0xE06D7363)',
                  0x80000002: 'EXCEPTION_DATATYPE_MISALIGNMENT', 0x80000003: 'EXCEPTION_BREAKPOINT',
                  0x80000004: 'EXCEPTION_SINGLE_STEP', 0xC0001234: 'NTSTATUS error', 0xFFFFFFFF: 'NTSTATUS error'}
    want_not = [0, 1, 3, 5, 6, 15, 0x77, 0x8000DEAD, 0x80000001, 0x80000005, 0xC000013A, 0x40010004]
    bad = [hex(k) for k, n in want_crash.items() if got.get(k) != (True, n)] + \
          [hex(k) for k in want_not if got.get(k) != (False, '')]
    check('%s codes: crash codes named, normal codes not crashes (%d codes)' % (tag, len(got)),
          rc == 0 and not bad and len(got) == 22, str(bad))
    record_cases(root, unit, fake, tag, E)
    return crash_folder


# ---------------------------------------------------------------------------------------------- ticket 0519 (records)
def proxy_record(pid, code=0xC0000005, name='EXCEPTION_ACCESS_VIOLATION', code_line=True, first_line=True):
    """<run>/exe-crash.txt as the proxy's chained filter writes it (proxy_crash_filter.cpp FormatRecord, CRLF)."""
    lines = []
    if first_line:
        lines.append('exception 0x%08X %s at CivilizationV_DX11.exe+0x136580 (VA 0x00536580), thread 4242 (the proxy\'s '
                     'boot thread)' % (code, name))
    if code_line:
        lines.append('code: 0x%08X %s' % (code, name))
    lines += ['where: CivilizationV_DX11.exe+0x136580', 'module base: 0x00E40000 (the EXE; its file\'s ImageBase 0x00400000)',
              'rva: 0x00136580', 'va at the EXE\'s ImageBase: 0x00536580 (the knowledge base\'s address form)',
              'access: read of 0x51B82000', 'thread: 4242 (the proxy\'s boot thread)', 'process: %s' % pid,
              'time: %s.000 (local)' % time.strftime('%Y-%m-%d %H:%M:%S'), 'flags: 0x0',
              'written by: the proxy\'s chained unhandled-exception filter (ticket 0419); no code bytes, no stack and no '
              'memory of the process (D83)']
    return ''.join(ln + '\r\n' for ln in lines)


def peer_record(c, env, record=None, age_s=0, timeout=120):
    """The peer whose child runs 4 s; once the child's pid is printed, a stand-in for the proxy writes
    <run>/exe-crash.txt = record(pid) (record=None: none), its write time age_s seconds in the past. Returns rc, output
    and the child's pid."""
    c.cfg.setdefault('unit_child_ms', '4000')
    cfg = c.write_cfg()
    p = rt.OnDesktop(desk().name, [str(c.exe), 'peer', str(cfg)], env, c.d / ('peer-%d.txt' % int(time.time() * 1000)),
                     cwd=c.d)
    pid = None
    for _ in range(400):
        m = re.search(r'CHILD pid (\d+)', p.text())
        if m:
            pid = int(m.group(1))
            break
        time.sleep(0.025)
    if pid and record is not None:
        path = c.run / 'exe-crash.txt'
        path.write_bytes(record(pid).encode('ascii'))
        if age_s:
            t = time.time() - age_s
            os.utime(path, (t, t))
    rc = p.wait(timeout)
    return rc, p.text(), pid


def record_cases(root, unit, fake, tag, E):
    """Ticket 0519: an exit code that is not an exception code with the proxy's crash record exe-crash.txt."""
    AV = 'EXCEPTION_ACCESS_VIOLATION'
    nothing = lambda c, out, rc: (rc == 3 and not c.reports.exists() and not c.helper_calls() and
                                  'helper: started' not in c.server_log() and results(out) == ['', '', ''] and
                                  'not a crash code; no exe-crash report' in c.server_log())

    # record + 255: the report, the record's code, the exit code and the proof in [failure], the helper once
    c = Case(root, 'rec-255', unit, fake)
    c.cfg['unit_child_code'] = 'FF'
    rc, out, pid = peer_record(c, E(), proxy_record)
    res = results(out)
    dirs = c.report_dirs()
    clause = ('the EXE ended with exit code 0x000000FF (255) after a crash its proxy recorded: exception code 0xC0000005 '
              '(%s)' % AV)
    check('%s rec-255: exit 3, the child ended with 255, the three service threads return "%s"' % (tag, clause),
          rc == 3 and child_exit(out) == 0xFF and len(res) == 3 and all(x == clause for x in res), '%s %s %s' % (rc, res,
                                                                                                             out[-600:]))
    check('%s rec-255: one report folder' % tag, len(dirs) == 1, str(dirs))
    if dirs:
        r = check_report(tag, 'rec-255', dirs[0], '0xC0000005 ' + AV, ('exe-crash.txt', 'vpproxy.ini', 'server.cfg'))
        rn = r.replace('\r\n', '\n')
        check('%s rec-255: [failure] says the exit code 255 and the record\'s proof (process %s, after the EXE started)'
              % (tag, pid),
              '\nexit code: the EXE ended with 0x000000FF (255), not an exception code (after a crash, Windows Error '
              'Reporting' in rn and re.search(r'\nproof of the crash: the proxy\'s crash record: exe-crash\.txt names this '
                                              r'EXE \(process %d\), written .* s after the EXE started\) \(ticket 0519\)\n'
                                              % pid, rn) is not None, rn[:1500])
        check('%s rec-255: the record\'s first line quoted; [exe] says exit code 0x000000FF (255; not an exception code'
              % tag,
              'exe-crash.txt, ticket 0419): exception 0xC0000005 %s at CivilizationV_DX11.exe+0x136580' % AV in rn and
              '\nexit code: 0x000000FF (255; not an exception code: the crash is proven by the proxy\'s crash record' in rn,
              rn[:2500])
        hc = c.helper_calls(want=1)
        want = 'popup --report "%s" --options "%s"' % (dirs[0], c.proxy / 'vpproxy.ini')
        check('%s rec-255: the helper started once: %s' % (tag, want), len(hc) == 1 and hc[0].endswith(want), str(hc))
    log = c.server_log()
    check('%s rec-255: server.log: "not a crash code, but the proxy\'s crash record proves a crash", the LOUD line, '
          '"report: written", "helper: started"' % tag,
          'not a crash code, but the proxy\'s crash record proves a crash: 0xC0000005 %s' % AV in log and
          'remote: LOUD: exe-crash: the EXE ended with exit code 0x000000FF (255) after a crash' in log and
          'exe-crash: report: written to' in log and 'exe-crash: helper: started' in log, log[-1500:])

    # 255 without a record; a stale record (another pid; this pid but written before the EXE started); a record that
    # names no pid; a record with exit code 0 (the game's own clean end): none
    for name, code, rec, age, why in (
            ('norec-255', 'FF', None, 0, 'no exe-crash.txt (the proxy\'s crash record) in the run folder'),
            ('stale-pid', 'FF', lambda p: proxy_record(p + 4), 0, 'not this EXE (pid'),
            ('stale-time', 'FF', proxy_record, 3600, 'before this EXE started'),
            ('rec-nopid', 'FF', lambda p: proxy_record('(none)'), 0, 'has no "process:" line with a pid'),
            ('rec-0', '0', proxy_record, 0, 'but exit code 0 is the game\'s own clean end')):
        c = Case(root, name, unit, fake)
        c.cfg['unit_child_code'] = code
        rc, out, pid = peer_record(c, E(), rec, age)
        check('%s %s (exit 0x%s): no report, no helper, empty clauses, "not a crash code" and "%s"' % (tag, name, code, why),
              nothing(c, out, rc) and why in c.server_log() and child_exit(out) == int(code, 16),
              '%s %s %s' % (rc, results(out), c.server_log()[-700:]))

    # the code from the record's first line (no "code:" line); a record naming no code (the exit code is the code)
    for name, rec, code_line in (
            ('rec-firstline', lambda p: proxy_record(p, 0xC0000409, 'STATUS_STACK_BUFFER_OVERRUN', code_line=False),
             '0xC0000409 STATUS_STACK_BUFFER_OVERRUN'),
            ('rec-nocode', lambda p: proxy_record(p, code_line=False, first_line=False),
             '0x000000FF (the crash record names no exception code)')):
        c = Case(root, name, unit, fake)
        c.cfg['unit_child_code'] = 'FF'
        rc, out, pid = peer_record(c, E(), rec)
        dirs = c.report_dirs()
        check('%s %s: a report whose code is %s, the helper once' % (tag, name, code_line),
              rc == 3 and len(dirs) == 1 and len(c.helper_calls(want=1)) == 1 and
              ('[failure]\nstage: exe-crash\ncode: ' + code_line + '\n') in text(dirs[0] / 'report.txt').replace('\r\n', '\n'),
              '%s %s %s' % (rc, dirs, c.server_log()[-700:]))

    # pack=0: the record is proof, but the shipped-layout switch is off: no report, no popup, the clause returned
    c = Case(root, 'rec-pack-off', unit, fake, pack='0')
    c.cfg['unit_child_code'] = 'FF'
    rc, out, pid = peer_record(c, E(), proxy_record)
    res = results(out)
    check('%s rec-pack-off: no report, no helper, the LOUD line says pack=0, the clause is still returned' % tag,
          rc == 3 and not c.reports.exists() and not c.helper_calls() and 'helper: started' not in c.server_log() and
          re.search(r'LOUD: exe-crash: the EXE ended with exit code 0x000000FF .*; no report and no popup \(.*says pack=0',
                    c.server_log()) is not None and res and res[0] == clause, '%s %s' % (res, c.server_log()[-700:]))

    # an exception exit code with a record of this EXE: as before (the code is the exit code's, the record is quoted)
    c = Case(root, 'rec-c5', unit, fake)
    c.cfg['unit_child_code'] = 'C0000005'
    rc, out, pid = peer_record(c, E(), lambda p: proxy_record(p, 0xC0000409, 'STATUS_STACK_BUFFER_OVERRUN'))
    res = results(out)
    dirs = c.report_dirs()
    check('%s rec-c5: exit 0xC0000005 with a record: the clause, the code and [exe] as before, the record quoted' % tag,
          rc == 3 and len(dirs) == 1 and res == ['the EXE ended with exception code 0xC0000005 (%s)' % AV] * 3 and
          '[failure]\nstage: exe-crash\ncode: 0xC0000005 %s\nexe: ' % AV in text(dirs[0] / 'report.txt').replace('\r\n', '\n')
          and 'exit code: 0xC0000005 (%s)' % AV in text(dirs[0] / 'report.txt') and
          'exe-crash.txt, ticket 0419): exception 0xC0000409 STATUS_STACK_BUFFER_OVERRUN' in text(dirs[0] / 'report.txt'),
          '%s %s %s' % (rc, res, dirs))


# ---------------------------------------------------------------------------------------------------------- part 2
def load_x64build():
    spec = importlib.util.spec_from_file_location('x64build', REPO / 'tools' / 'x64-dll' / 'build.py')
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def build_standin(b):
    x64 = load_x64build()
    sdir = b / 'server'
    tdir = sdir / 'test0418'
    tdir.mkdir(parents=True, exist_ok=True)
    env = x64.load_vcvars_env(lambda *a, **k: None, sdir / 'vcvars-env.json')
    obj = tdir / 'exe_crash_standin.obj'
    cmd = [str(x64.CLANG_CL), '-m64', '/c', '/nologo', '/MD', '/EHsc', '/Zi', '/std:c++14', '/W3', '/WX', '/O2',
           '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS', '/I' + str(REPO / 'ipc' / 'include'),
           '/I' + str(REPO / 'proxy' / 'include'), '/I' + str(b / 'gen' / 'common'), '/Fo' + str(obj),
           str(HERE / 'exe_crash_standin.cpp')]
    cp = subprocess.run(cmd, capture_output=True, text=True, errors='replace', env=env)
    if cp.returncode:
        return None, cp.stdout + cp.stderr
    exe = tdir / 'exe_crash_standin.exe'
    objs = [obj] + sorted((sdir / 'obj' / 'vpipc').glob('*.obj')) + [sdir / 'obj' / 'gen' / 'vp_ids.gen.obj']
    cp = subprocess.run([str(x64.LLD_LINK), '/OUT:' + str(exe), '/MACHINE:X64', '/SUBSYSTEM:CONSOLE', '/DEBUG'] +
                        [str(o) for o in objs] + ['kernel32.lib'], capture_output=True, text=True, errors='replace',
                        env=env)
    return (exe if cp.returncode == 0 else None), cp.stdout + cp.stderr


def e2e(s, b, fake, standin):
    """The real server.exe in a D28-like layout: <case>/proxy/VP64/{server.exe, its libraries}."""
    bins = b / 'bin'
    out = {}
    root = s / 'e2e'
    root.mkdir(parents=True, exist_ok=True)
    for name, code, hold, over in (('crash', 'C0000005', False, {}), ('exit0', '0', False, {}),
                                   ('kill', '0', True, {'test_exe_kill': 'attach:500'}),
                                   ('incall', 'C0000005', 'incall', {}),
                                   ('rec255', 'FF', 'record', {}), ('norec255', 'FF', False, {}),
                                   ('rec255-pack0', 'FF', 'record', {}),
                                   # ticket 0641: a deliberate end at kRt_Boot (render_size refused, exit 5)
                                   ('render5', 'C0000005', 'incall', {'render': '1', 'render_size': '10x10'}),
                                   ('render5-pack0', 'C0000005', 'incall', {'render': '1', 'render_size': '10x10'})):
        c = Case(root, name, standin, fake, pack='0' if name.endswith('pack0') else '1')
        for f in ('server.exe', 'lua51_x64.dll', 'CvGameDatabase_x64.dll', 'CvLocalization_x64.dll'):
            shutil.copy2(bins / f, c.proxy / 'VP64' / f)
        server = c.proxy / 'VP64' / 'server.exe'
        (c.run / 'server.log').unlink()   # the real server writes its own
        c.cfg.update({'dll': str(bins / 'CvGameCore_Expansion2.dll'), 'lua_dll': str(c.proxy / 'VP64' / 'lua51_x64.dll'),
                      'db_dll': str(c.proxy / 'VP64' / 'CvGameDatabase_x64.dll'),
                      'loc_dll': str(c.proxy / 'VP64' / 'CvLocalization_x64.dll'), 'loc_db': '', 'dll_logs': '0',
                      'fail_on_unexpected': '0', 'connect_timeout_ms': '30000'})
        c.cfg.update(over)
        cfg = c.write_cfg()
        go = c.d / 'go'
        mode = ['hold'] if hold is True else (['record', str(c.run)] if hold == 'record' else ([hold] if hold else []))
        p = rt.OnDesktop(desk().name, [str(standin), str(server), str(cfg), code, str(go)] + mode,
                         safe_env(), c.d / 'standin.txt', cwd=c.d)
        spid = None
        for _ in range(600):
            m = re.search(r'SERVER pid (\d+)', p.text())
            if m:
                spid = int(m.group(1))
                break
            time.sleep(0.05)
        h = _k32.OpenProcess(0x00100000 | 0x1000, False, spid) if spid else None
        go.write_text('go\n', encoding='ascii')
        src = p.wait(90)
        scode = None
        if h:
            if _k32.WaitForSingleObject(h, 60000) == 0:
                v = ctypes.c_ulong()
                _k32.GetExitCodeProcess(h, ctypes.byref(v))
                scode = v.value
            _k32.CloseHandle(h)
        rep = text(c.run / 'server-report.txt')
        res = [ln for ln in rep.splitlines() if ln.startswith('result: ')]
        res = res[0] if res else ''
        out[name] = c
        if name == 'crash':
            dirs = c.report_dirs()
            check('e2e crash: the stand-in ended with 0xC0000005, server.exe exit 3, one report folder, the helper once',
                  src == 0xC0000005 and scode == 3 and len(dirs) == 1 and len(c.helper_calls(want=1)) == 1,
                  '%s %s %s %s %s' % (src, scode, dirs, c.helper_calls(), text(c.run / 'server.log')[-800:]))
            check('e2e crash: server-report.txt result: FAILED: the engine is gone ... the EXE ended with exception code '
                  '0xC0000005 (EXCEPTION_ACCESS_VIOLATION)',
                  re.match(r'result: FAILED: the engine is gone: channel \d serve\(\) returned \w+; the EXE ended with '
                           r'exception code 0xC0000005 \(EXCEPTION_ACCESS_VIOLATION\)', res) is not None, res)
            if dirs:
                check_report('e2e', 'crash', dirs[0], '0xC0000005 EXCEPTION_ACCESS_VIOLATION', ('server.cfg', 'vpproxy.ini'),
                             want_tails=('server.log', 'Lua.log'))
                out['crash_folder'] = dirs[0]
        elif name == 'exit0':
            check('e2e exit0: server.exe exit 3, no report, no helper, the result line as before (no EXE clause)',
                  src == 0 and scode == 3 and not c.reports.exists() and not c.helper_calls() and
                  'helper: started' not in text(c.run / 'server.log') and
                  re.match(r'result: FAILED: the engine is gone: channel \d serve\(\) returned \w+; remote mode', res)
                  is not None and 'not a crash code' in text(c.run / 'server.log'), '%s %s %s' % (src, scode, res))
        elif name == 'incall':
            slog = text(c.run / 'server.log')
            in_call = re.search(r'remote: call (\S+) \(0x[0-9a-f]{8}\) on channel \d: PeerDied', slog)
            dirs = c.report_dirs()
            check('e2e incall: the stand-in ends inside an engine call the DLL made (%s); server.exe exit 3, the result '
                  'names the EXE\'s code, report.txt complete, the helper once' % (in_call.group(1) if in_call else '?'),
                  src == 0xC0000005 and scode == 3 and in_call is not None and
                  'engine call 0x' in text(c.d / 'standin.txt') and
                  'the EXE ended with exception code 0xC0000005 (EXCEPTION_ACCESS_VIOLATION)' in res and
                  len(dirs) == 1 and '[left out]' in text(dirs[0] / 'report.txt') and len(c.helper_calls(want=1)) == 1,
                  '%s %s %s %s %s' % (src, scode, res, dirs, slog[-1200:]))
            print('     incall: the result line: %s' % res[:200])
        elif name == 'rec255':
            dirs = c.report_dirs()
            slog = text(c.run / 'server.log')
            check('e2e rec255 (ticket 0519): the stand-in wrote the proxy\'s record and ended with 255; server.exe exit 3, '
                  'one report (stage exe-crash, the record\'s code, the record copied), the helper once, the result names '
                  'the exit code and the recorded crash',
                  src == 0xFF and scode == 3 and 'record: ' in text(c.d / 'standin.txt') and len(dirs) == 1 and
                  len(c.helper_calls(want=1)) == 1 and
                  'the EXE ended with exit code 0x000000FF (255) after a crash its proxy recorded: exception code 0xC0000005 '
                  '(EXCEPTION_ACCESS_VIOLATION)' in res and
                  'not a crash code, but the proxy\'s crash record proves a crash' in slog,
                  '%s %s %s %s %s' % (src, scode, res, dirs, slog[-1200:]))
            if dirs:
                r = check_report('e2e', 'rec255', dirs[0], '0xC0000005 EXCEPTION_ACCESS_VIOLATION',
                                 ('server.cfg', 'vpproxy.ini', 'exe-crash.txt'), want_tails=('server.log', 'Lua.log'))
                check('e2e rec255: [failure] names the exit code 255 and the record of this pid as the proof',
                      'exit code: the EXE ended with 0x000000FF (255), not an exception code' in r and
                      'proof of the crash: the proxy\'s crash record: exe-crash.txt names this EXE (process' in r, r[:1500])
                out['rec255_folder'] = dirs[0]
        elif name in ('render5', 'render5-pack0'):
            # Ticket 0641: server-fatal.txt always; on the shipped layout (pack=1) the server ends at once after server.log's
            # result line, with no server-report.txt and no call tables; pack=0 keeps the full report
            slog = text(c.run / 'server.log')
            rec = text(c.run / 'server-fatal.txt')
            quick = name == 'render5'
            check('e2e %s (ticket 0641): server.exe exit 5 at kRt_Boot (render_size refused); server-fatal.txt with code '
                  'render-start; server.log\'s result line names the reason; %s' %
                  (name, 'no server-report.txt and no call tables, one line says so' if quick else
                   'server-report.txt written as before'),
                  scode == 5 and 'code: render-start' in rec and 'exit: 5' in rec and
                  "result: FAILED: the server's renderer could not start: server.cfg render_size=10x10" in slog and
                  (not (c.run / 'server-report.txt').exists()) == quick and
                  ('server-report.txt and the call tables are not written' in slog) == quick and
                  not c.helper_calls(),
                  '%s %s %s %s' % (src, scode, rec, slog[-1500:]))
        elif name in ('norec255', 'rec255-pack0'):
            slog = text(c.run / 'server.log')
            why = ('no exe-crash.txt (the proxy\'s crash record) in the run folder' if name == 'norec255' else
                   'no report and no popup (auto: ')
            check('e2e %s (ticket 0519): the stand-in ended with 255; server.exe exit 3, no report, no helper; server.log '
                  'says "%s"' % (name, why),
                  src == 0xFF and scode == 3 and not c.reports.exists() and not c.helper_calls() and
                  'helper: started' not in slog and why in slog, '%s %s %s' % (src, scode, slog[-1200:]))
        else:
            check('e2e kill: test_exe_kill=attach:500 ends the stand-in with 0xC0000005; report and helper; exit 3',
                  src == 0xC0000005 and scode == 3 and len(c.report_dirs()) == 1 and len(c.helper_calls(want=1)) == 1 and
                  'LOUD: TEST ONLY: test_exe_kill: ending the EXE' in text(c.run / 'server.log'),
                  '%s %s %s' % (src, scode, text(c.run / 'server.log')[-600:]))
    return out


def real_helper(s, helper, folder, tag):
    rc, out = run_on_desk(helper, ['popup', '--report', folder, '--options', s / 'nonexistent.ini', '--no-ui'],
                          s / ('real-helper-%s.txt' % tag), safe_env({'VPPROXY_TEST_CONFIG_INI': 'C:/no-such-0418/config.ini'}))
    lines = [ln[len('popup: line: '):] for ln in out.replace('\r\n', '\n').splitlines() if ln.startswith('popup: line: ')]
    summary = text(Path(folder) / 'popup-summary.txt').strip()
    check('real helper (%s): first line "The game itself crashed ...", the server\'s summary follows, no "goes on"' % tag,
          rc == 0 and lines[:1] == ['The game itself crashed (the game\'s own process, not the 64-bit server), so the game '
                                    'had to end.'] and lines[1:2] == [summary] and 'goes on' not in out,
          '%s %r' % (rc, lines[:3]))
    return out


def test_asan(s, fake):
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
                                ('exe_crash_unit.exe', SRCS, flags, LIBS)):
        cp = subprocess.run([cl] + fl + [str(x) for x in srcs] + ['/Fe' + str(d / exe), '/Fo' + str(d) + '\\',
                                                                  '/Fd' + str(d / (exe + '.pdb'))] + libs,
                            cwd=str(d), env=env, capture_output=True, text=True, errors='replace')
        if not check('asan: %s compiled with MSVC /fsanitize=address (x64)' % exe, cp.returncode == 0,
                     (cp.stdout + cp.stderr)[-2000:]):
            return
    rc, out = run_on_desk(d / 'canary.exe', [], d / 'canary.txt', env)
    check('asan (negative): the canary\'s heap-buffer-overflow is reported (ASan active)',
          rc != 0 and 'heap-buffer-overflow' in out, out[-600:])
    first = len(rt.RESULTS)
    unit_cases(s, d / 'exe_crash_unit.exe', fake, 'asan', env_base=env)
    reports = [p for p in (s / 'asan').rglob('*.txt') if 'AddressSanitizer' in text(p)]
    check('asan: no AddressSanitizer report in any run of the ASan build (%d checks above)' % (len(rt.RESULTS) - first),
          not reports, ', '.join(str(p) for p in reports[:5]))


def fatal_record_cases(s, unit, tag):
    """Ticket 0641: SrvExeCrash::FatalWords and WriteFatalRecord (the server's own reason for a deliberate end, read by
    the proxy's D119 report): the token and the player's words per exit code and module, the record's five lines,
    scrubbed and one line each; no record for 0, 3 and a save-variant code, nor without a run folder."""
    prof = os.environ.get('USERPROFILE', 'C:\\Users\\someone')
    D = 'The 64-bit server could no longer draw %s, so it ended the game.'
    cases = [
        ('5', 'models: cities: the readiness or the tap frames were lost in play for 30001 ms of frames that ran (D118)',
         'render-models', D % 'the cities'),
        ('5', 'models: landmarks: x', 'render-models', D % 'the landmarks'),
        ('5', 'units: the unit readiness was lost in play for 30001 ms of frames that ran', 'render-units', D % 'the units'),
        ('5', 'unit shadow: the mask', 'render-units', D % "the units' shadows"),
        ('5', 'fog of war: no mask', 'render-fog', D % 'the fog of war'),
        ('5', "the server's renderer could not start: server.cfg render_x=1", 'render-start', D % "the game's world"),
        ('5', 'map size: 9999 x 9999', 'render', D % "the game's world"),
        ('1', 'the EXE did not run the event', 'server-error',
         'The 64-bit server met an error it cannot go on from, so it ended the game.'),
        ('2', 'an exception (see server.log)', 'server-exception', 'The 64-bit server crashed, so the game had to end.'),
        ('4', 'SetGameDatabase: no such table', 'database',
         "The 64-bit server's copy of the game's database went wrong, so it ended the game."),
        ('6', 'exe_exception: the engine threw', 'engine-exception',
         "The game's engine failed inside a call from the 64-bit server, so the game had to end."),
        ('7', 'something', 'exit-7', 'The 64-bit server ended the game (exit code 7).'),
    ]
    n = 0
    for code, why, token, words in cases:
        n += 1
        run = s / tag / 'fatal' / ('run-%02d' % n)
        run.mkdir(parents=True, exist_ok=True)
        why2 = why + '\r\nsecond line in ' + prof + '\\Documents'
        rc, out = run_on_desk(unit, ['fatal', run, code, why2], run.parent / ('out-%02d.txt' % n), safe_env(), timeout=60)
        m = re.search(r'(?m)^WORDS ([^|\r\n]*)\|(.*?)\r?$', out)
        rec = run / 'server-fatal.txt'
        rtxt = rec.read_bytes().decode('utf-8', errors='replace') if rec.exists() else ''
        lines = rtxt.split('\r\n')
        check('%s fatal %s (%s): token %s, summary "%s"' % (tag, code, why[:30], token, words),
              rc == 0 and m is not None and m.group(1) == token and m.group(2) == words and 'WROTE 1' in out,
              out[-800:])
        pid = re.search(r'(?m)^PID (\d+)\r?$', out)
        check('%s fatal %s (%s): server-fatal.txt has the six lines (CRLF, one line each), the exit code, the pid (ticket '
              '0744), the token, the summary and the reason, scrubbed' % (tag, code, why[:30]),
              len(lines) == 7 and lines[6] == '' and lines[0] == 'server-fatal: the 64-bit server ended on purpose (ticket 0641)'
              and lines[1] == 'exit: %s' % code and pid is not None and lines[2] == 'pid: %s' % pid.group(1) and
              lines[3] == 'code: %s' % token and lines[4] == 'summary: ' + words
              and lines[5].startswith('reason: ' + why + '  second line in %USERPROFILE%\\Documents') and
              prof.lower() not in rtxt.lower(), rtxt[:1500])
    for code in ('0', '3', '0x20561622'):
        n += 1
        run = s / tag / 'fatal' / ('run-%02d' % n)
        run.mkdir(parents=True, exist_ok=True)
        rc, out = run_on_desk(unit, ['fatal', run, code, 'x'], run.parent / ('out-%02d.txt' % n), safe_env(), timeout=60)
        check('%s fatal %s (negative): no token, no record' % (tag, code),
              rc == 0 and 'WORDS -|' in out and 'WROTE 0' in out and not (run / 'server-fatal.txt').exists(), out[-500:])
    rc, out = run_on_desk(unit, ['fatal', '-', '5', 'models: cities: x'], s / tag / 'fatal' / 'out-norun.txt', safe_env(),
                          timeout=60)
    check('%s fatal without a run folder (negative): not written, one line' % tag,
          rc == 0 and 'WROTE 0' in out and 'server-fatal: no run folder: server-fatal.txt not written' in out, out[-500:])


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--build-out', type=Path, help='a build_server.py --out tree (part 2: the real server.exe and helper)')
    ap.add_argument('--asan', action='store_true')
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    rt.VERBOSE = a.v
    s = a.out.resolve()
    shutil.rmtree(s, ignore_errors=True)
    s.mkdir(parents=True)
    try:
        fake = s / 'fake' / 'vp64helper.exe'
        ok, txt = build_clang(fake, [FAKE_HELPER])
        check('build: the fake vp64helper.exe (proxy/tools/rt_tests/fake_helper.cpp, x64)', ok, txt[-1500:])
        unit = s / 'clang' / 'exe_crash_unit.exe'
        ok2, txt = build_clang(unit, SRCS)
        if ok and check('build: exe_crash_unit.cpp + srv_exe_crash.cpp (clang-cl x64, /W3 /WX)', ok2, txt[-2000:]):
            folder = unit_cases(s, unit, fake, 'clang')
            fatal_record_cases(s, unit, 'clang')   # ticket 0641
            if a.build_out:
                b = a.build_out.resolve()
                standin, txt = build_standin(b)
                if check('build: exe_crash_standin.exe against %s' % b, standin is not None, txt[-2000:]):
                    res = e2e(s, b, fake, standin)
                    helper = b / 'proxy' / 'bin' / 'vp64helper.exe'
                    if check('real helper: %s exists' % helper, helper.exists()):
                        if folder:
                            real_helper(s, helper, folder, 'unit crash report')
                        if res.get('crash_folder'):
                            real_helper(s, helper, res['crash_folder'], 'e2e crash report')
                        if res.get('rec255_folder'):
                            real_helper(s, helper, res['rec255_folder'], 'e2e rec255 report (ticket 0519)')
            if a.asan:
                test_asan(s, fake)
    finally:
        for dk in DESKS.values():
            dk.close()
    bad = [nm for nm, ok in rt.RESULTS if not ok]
    print('test_exe_crash_report: %d/%d checks passed' % (len(rt.RESULTS) - len(bad), len(rt.RESULTS)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
