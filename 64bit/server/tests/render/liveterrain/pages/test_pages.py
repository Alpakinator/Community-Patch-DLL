"""Ticket 0587 (epic 0465 follow-up): the live apply's worker pages with the windows over the field
(server/src/render/srv_liveterrain_pages.*), equal to bake::buildHeightPage of the written field.

Runs test_pages.exe (built by test_pages_build.py into C5_0587_PAGES_BUILD/bin, or bin-asan with C5_0587_PAGES_ASAN=1).
Each case's line must say PASS and the exit code be 0. A missing binary fails (no skip).

    set C5_0587_PAGES_BUILD=C:\\c5-64bit-work\\0587\\pages
    set PYTEST_DISABLE_PLUGIN_AUTOLOAD=1
    python -m pytest server/tests/render/liveterrain/pages/test_pages.py -q -p no:cacheprovider
"""
import os
import subprocess
from pathlib import Path

CASES = ["equal", "refusals", "cost"]


def exe():
    d = os.environ.get("C5_0587_PAGES_BUILD", "")
    sub = "bin-asan" if os.environ.get("C5_0587_PAGES_ASAN") == "1" else "bin"
    p = Path(d) / sub / "test_pages.exe"
    assert d and p.exists(), "C5_0587_PAGES_BUILD must name a test_pages_build.py --out folder with %s/test_pages.exe" % sub
    return str(p)


def run(*args, timeout=900):
    p = subprocess.run([exe(), *args], capture_output=True, text=True, timeout=timeout, creationflags=0x08000000)
    return p.returncode, p.stdout + p.stderr


def test_pages_cases():
    rc, out = run()
    for case in CASES:
        assert ("CASE %s PASS" % case) in out, out
    assert "differs" not in out and "NOT AS EXPECTED" not in out and "unchanged NO" not in out, out
    assert "test_pages: 0 check(s) failed" in out, out
    assert rc == 0, out


def test_pages_unknown_case_is_refused():
    rc, out = run("no_such_case")
    assert rc == 2 and "no case matches" in out, out
