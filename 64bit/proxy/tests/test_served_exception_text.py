"""Ticket 0451: the text of a C++ exception thrown inside a served call reaches the proxy's LOUD line and the server's
exe_exception report byte for byte (proxy/src/proxy_rt.cpp copies e.what() inside its catch handler; before 0451 it
read the text after the handler had destroyed the exception object, a use after free).

    python -I proxy/tests/test_served_exception_text.py --b <build_server.py --out> --src <DLL source> --deps <build_all out>
                                                        --out DIR [--scenario NAME ...] [--expect-fail]

Runs proxy/tools/hutest/build_hutest.py once per scenario (default: streamread, variant-nodb-on43, variant-short). In
each, the fake engine's stream throws std::runtime_error("hutest: read past the end of the engine stream" in streamread,
"... of the save body" in the variant scenarios) from kRtE_StreamRead (proxy/tools/hutest/hutest_host.cpp; the message is a heap-allocated std::string inside the exception
object, so a read after the handler sees freed heap). Checks per scenario, from the scenario's run folder:
  - hutest's own verdict: build_hutest.py exits 0 (a Python error counts as a failure);
  - vpproxy.log has the LOUD line "served call rtE:StreamRead (0xFFF10010) threw <TEXT> inside the EXE or the proxy";
  - vpproxy.log has "served call failed: status 21: exe_exception: rtE:StreamRead threw <TEXT>" ending the line;
  - server/server-report.txt has "(exe_exception: rtE:StreamRead threw <TEXT>)";
  - every line of vpproxy.log that names the throw is printable ASCII (no freed-heap bytes).
--expect-fail (the negative control on a build without 0451 whose heap layout exposes the defect, for example 0313's
master build): exit 0 only when at least one text check fails. Exit codes: 0 pass, 1 a check failed, 2 usage.
Outputs only under --out. Start it through perf/quietlock/quietlock.py run (each hutest run starts server.exe).
"""
import argparse
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
BUILD_HUTEST = REPO / 'proxy' / 'tools' / 'hutest' / 'build_hutest.py'
# The text each scenario's fake engine stream throws (hutest_host.cpp: the engine stream for streamread, the save body
# for the variant scenarios).
TEXTS = {'streamread': 'hutest: read past the end of the engine stream'}
TEXT_DEFAULT = 'hutest: read past the end of the save body'
DEFAULT = ['streamread', 'variant-nodb-on43', 'variant-short']


def read(p):
    return p.read_bytes().decode('latin-1') if p.exists() else ''


def check_run(name, rc, run):
    """Returns (hutest_ok, text_failures)."""
    TEXT = TEXTS.get(name, TEXT_DEFAULT)
    plog = read(run / 'vpproxy.log')
    report = read(run / 'server' / 'server-report.txt')
    bad = []
    loud = 'LOUD: served call rtE:StreamRead (0xFFF10010) threw %s inside the EXE or the proxy' % TEXT
    if loud not in plog:
        bad.append('vpproxy.log has no %r' % loud)
    failed = 'served call failed: status 21: exe_exception: rtE:StreamRead threw %s' % TEXT
    if not any(l.rstrip('\r').endswith(failed) for l in plog.splitlines()):
        bad.append('vpproxy.log has no line ending in %r' % failed)
    rep = '(exe_exception: rtE:StreamRead threw %s)' % TEXT
    if rep not in report:
        bad.append('server-report.txt has no %r' % rep)
    for l in plog.splitlines():
        if 'rtE:StreamRead' in l and 'threw' in l and any(ord(c) < 0x20 and c not in '\t\r' or ord(c) > 0x7e for c in l):
            bad.append('vpproxy.log names the throw with non-ASCII bytes: %r' % l[:200])
            break
    return rc == 0, bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--b', type=Path, required=True, help='a build_server.py --out folder (server.exe and the proxy)')
    ap.add_argument('--src', type=Path, required=True, help='the DLL source the build used')
    ap.add_argument('--deps', type=Path, required=True, help='the build_all.py --out folder the build used')
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--scenario', action='append', help='hutest scenario (repeatable; default %s)' % ', '.join(DEFAULT))
    ap.add_argument('--expect-fail', action='store_true', help='negative control: pass only if a text check fails')
    a = ap.parse_args()
    names = a.scenario or DEFAULT
    a.out.mkdir(parents=True, exist_ok=True)
    total_bad, hutest_bad = 0, 0
    for n in names:
        d = a.out / n
        log = a.out / (n + '.log')
        with open(log, 'w', encoding='utf-8', errors='replace') as f:
            rc = subprocess.run([sys.executable, '-I', str(BUILD_HUTEST), '--b', str(a.b), '--src', str(a.src),
                                 '--deps', str(a.deps), '--out', str(d), '--scenario', n],
                                stdout=f, stderr=subprocess.STDOUT, cwd=str(REPO),
                                env=dict(__import__('os').environ, PYTHONIOENCODING='utf-8')).returncode
        ok, bad = check_run(n, rc, d / ('run-scn-' + n))
        total_bad += len(bad)
        hutest_bad += 0 if ok else 1
        print('%s %s: hutest rc=%d, text checks %s' % ('ok  ' if ok and not bad else 'FAIL', n, rc,
                                                       'ok' if not bad else '; '.join(bad)), flush=True)
    if a.expect_fail:
        verdict = total_bad > 0
        print('test_served_exception_text (negative control): %s: %d text check(s) failed over %d scenario(s)'
              % ('PASS' if verdict else 'FAIL', total_bad, len(names)))
    else:
        verdict = total_bad == 0 and hutest_bad == 0
        print('test_served_exception_text: %s: %d scenario(s), %d text check(s) failed, %d hutest verdict(s) failed'
              % ('PASS' if verdict else 'FAIL', len(names), total_bad, hutest_bad))
    return 0 if verdict else 1


if __name__ == '__main__':
    sys.exit(main())
