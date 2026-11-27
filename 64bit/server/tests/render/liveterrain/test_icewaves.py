"""Ticket 0586 (epic 0465): the server's side of the EXE's ice and coast waves after a live terrain change
(server/src/render/srv_liveterrain_icewaves.cpp): the decisions and the gate, the live reload section, a stand-in proxy.

Runs test_icewaves.exe (built by test_liveterrain_build.py into C5_LIVETERRAIN_BUILD/bin, or bin-asan with
C5_LIVETERRAIN_ASAN=1). CPU only. Every case asserts its own outputs inside the program; here each case's line must say
PASS and the exit code 0. A missing binary fails (no skip). The proxy's side has its own test: proxy/tests/livereload/.

    set C5_LIVETERRAIN_BUILD=C:\\c5-64bit-work\\0586\\lt-tests
    set PYTEST_DISABLE_PLUGIN_AUTOLOAD=1
    python -m pytest server/tests/render/liveterrain/test_icewaves.py -q -p no:cacheprovider
"""
import os
import subprocess
from pathlib import Path

CASES = ["decisions", "failures", "landwater", "link", "fieldrefresh_off"]   # landwater: ticket 4914; fieldrefresh_off: MC #9538 item 4


def exe():
    d = os.environ.get("C5_LIVETERRAIN_BUILD", "")
    sub = "bin-asan" if os.environ.get("C5_LIVETERRAIN_ASAN") == "1" else "bin"
    p = Path(d) / sub / "test_icewaves.exe"
    assert d and p.exists(), "C5_LIVETERRAIN_BUILD must name a test_liveterrain_build.py --out folder with %s/test_icewaves.exe" % sub
    return str(p)


def run(*args, timeout=120):
    p = subprocess.run([exe(), *args], capture_output=True, text=True, timeout=timeout, creationflags=0x08000000)
    return p.returncode, p.stdout + p.stderr


def test_icewaves_cases():
    rc, out = run()
    for case in CASES:
        assert ("CASE %s PASS" % case) in out, out
    assert "ERROR: AddressSanitizer" not in out, out
    assert ", 0 failed" in out and rc == 0, out


def test_icewaves_unknown_case_is_refused():
    rc, out = run("no_such_case")
    assert rc == 2 and "no case matches" in out, out


def test_icewaves_every_case_is_listed_here():
    rc, out = run()
    names = [ln.split()[1] for ln in out.splitlines() if ln.startswith("CASE ")]
    assert rc == 0 and sorted(names) == sorted(CASES), out
