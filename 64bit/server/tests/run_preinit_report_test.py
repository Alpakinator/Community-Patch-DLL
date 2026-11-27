"""Bug 0128 (issue 3309, 2026-11-13): the remote-mode server reports and exits cleanly when it fails before the attach.

  python server/tests/run_preinit_report_test.py --bin <build_server.py --out>/bin --out DIR [--timeout S]

Before SrvRemote::Init (srv_rt.cpp) the critical sections of the call counters and the unserved-method table are not
initialised. A failure there (a library that does not load, the allow-list file, the localization start-up, the
attach) went to SrvRemote::ReportAndExit, whose WriteReport entered those critical sections: the server faulted, its
unhandled-exception filter re-entered ReportAndExit and waited forever, and the proxy waited out its attach timeout
(60 s). This test runs server.exe --remote with a server.cfg that fails at each of those points, without any proxy, and
checks for every case, after the failure:
  - the exit code (1) within the timeout (default 30 s; a server that hangs is killed and the case fails),
  - server-report.txt exists and has "result: FAILED: <the case's reason>" and the line "report: written before the
    attach",
  - server.log has "remote: exit code 1 (" and no exception line,
  - no proxy-calls.tsv (the call tables exist only after Init).
Cases: loc_icu (loc_db an SQLite file, icu_dat missing: bug 0128's own case), loc_db (loc_db missing), lua (lua_dll
missing), allow (unexpected_allow missing), attach (no proxy: the attach times out after connect_timeout_ms=1500).
Run folders: DIR/<case>/. Exit 0 when every case passes, 1 when one fails, 2 on a usage error. Start it through
`perf/quietlock/quietlock.py run` (it starts server.exe). No window: the server is a console program started with
CREATE_NO_WINDOW and fails before it loads the DLL.
"""
import argparse
import os
import shutil
import sqlite3
import subprocess
import sys
import time
from pathlib import Path

CREATE_NO_WINDOW = 0x08000000


def write_cfg(run: Path, b: Path, **over):
    cfg = {'run_dir': str(run), 'game_dir': str(run), 'dll': str(b / 'CvGameCore_Expansion2.dll'),
           'lua_dll': str(b / 'lua51_x64.dll'), 'db_dll': str(b / 'CvGameDatabase_x64.dll'),
           'loc_dll': str(b / 'CvLocalization_x64.dll'), 'loc_db': '', 'dll_logs': 0, 'fail_on_unexpected': 1,
           'connect_timeout_ms': 1500}
    cfg.update(over)
    path = run / 'server.cfg'
    path.write_text('# bug 0128 pre-Init test (server/tests/run_preinit_report_test.py)\n' +
                    ''.join('%s=%s\n' % (k, v) for k, v in cfg.items()), encoding='utf-8')
    return path


def run_case(name, b: Path, out: Path, timeout: float, over, why_has):
    run = out / name
    if run.exists():
        shutil.rmtree(run)
    run.mkdir(parents=True)
    over = dict((k, (v(run) if callable(v) else v)) for k, v in over.items())
    cfg = write_cfg(run, b, **over)
    section = 'Local\\VP64PreInitTest_%d_%s' % (os.getpid(), name)
    t0 = time.time()
    hung = False
    with (run / 'server-stdout.txt').open('w', encoding='utf-8') as so:
        p = subprocess.Popen([str(b / 'server.exe'), '--remote', '--section', section, '--config', str(cfg)],
                             stdout=so, stderr=subprocess.STDOUT, cwd=str(run), creationflags=CREATE_NO_WINDOW)
        try:
            rc = p.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            hung = True
            p.kill()
            rc = p.wait()
    dt = time.time() - t0
    report = run / 'server-report.txt'
    rtext = report.read_text(encoding='utf-8', errors='replace') if report.exists() else ''
    log = run / 'server.log'
    ltext = log.read_text(encoding='utf-8', errors='replace') if log.exists() else ''
    fails = []
    if hung:
        fails.append('no exit within %.0f s (killed; exit code after the kill %d)' % (timeout, rc))
    elif rc != 1:
        fails.append('exit code %d (0x%08X), expected 1' % (rc, rc & 0xFFFFFFFF))
    if not report.exists():
        fails.append('no server-report.txt')
    else:
        if ('result: FAILED: ' + why_has) not in rtext:
            fails.append('server-report.txt has no "result: FAILED: %s"' % why_has)
        if 'report: written before the attach' not in rtext:
            fails.append('server-report.txt has no "report: written before the attach"')
    if 'remote: exit code 1 (' not in ltext:
        fails.append('server.log has no "remote: exit code 1 ("')
    exc = [l for l in ltext.splitlines() if 'exception' in l.lower()]
    if exc:
        fails.append('server.log has an exception line: %s' % exc[0][:200])
    if (run / 'proxy-calls.tsv').exists():
        fails.append('proxy-calls.tsv written before Init')
    res = [l for l in rtext.splitlines() if l.startswith('result: ')]
    print('%-8s %s  exit %s in %.1f s; %s' % (name, 'PASS' if not fails else 'FAIL', 'hung' if hung else rc, dt,
                                             res[0][:160] if res else '(no result line)'), flush=True)
    for f in fails:
        print('         - ' + f, flush=True)
    return not fails


def make_locdb(run: Path) -> str:
    path = run / 'Localization-Merged.db'
    con = sqlite3.connect(str(path))
    con.execute('CREATE TABLE Languages (ID INTEGER PRIMARY KEY, Type TEXT)')
    con.execute("INSERT INTO Languages (Type) VALUES ('en_US')")
    con.commit()
    con.close()
    return str(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--bin', type=Path, required=True, help='server/build_server.py --out <dir>/bin')
    ap.add_argument('--out', type=Path, required=True, help='scratch folder outside the repository')
    ap.add_argument('--timeout', type=float, default=30.0)
    a = ap.parse_args()
    b = a.bin.resolve()
    if not (b / 'server.exe').is_file():
        print('run_preinit_report_test: %s has no server.exe' % b)
        return 2
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    print('server.exe %s' % (b / 'server.exe'), flush=True)
    cases = [
        ('loc_icu', {'loc_db': make_locdb, 'icu_dat': lambda r: str(r / 'Locale' / 'icudt44l.dat')},
         'localization: Localization::Initialize failed (ICU data '),
        ('loc_db', {'loc_db': lambda r: str(r / 'no-such-Localization.db')},
         'localization: cannot open the localization database '),
        ('lua', {'lua_dll': lambda r: str(r / 'no-such-lua51_x64.dll')}, 'cannot load '),
        ('allow', {'unexpected_allow': lambda r: str(r / 'no-such-allowlist.txt')}, 'unexpected_allow: cannot open '),
        ('attach', {}, 'attach_server('),
    ]
    ok = [run_case(n, b, out, a.timeout, over, why) for n, over, why in cases]
    print('run_preinit_report_test: %d cases, %d passed, %d failed: %s' % (len(ok), sum(ok), len(ok) - sum(ok),
                                                                           'PASS' if all(ok) else 'FAIL'), flush=True)
    return 0 if all(ok) else 1


if __name__ == '__main__':
    sys.exit(main())
