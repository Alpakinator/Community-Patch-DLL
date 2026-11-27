"""Ticket 4907 (epic 0465): the live-terrain region hook's core (server/src/render/srv_liveterrain*.cpp) on the test stub
of 4903's producer.

Runs test_liveterrain.exe (built by test_liveterrain_build.py into C5_LIVETERRAIN_BUILD/bin, or bin-asan with
C5_LIVETERRAIN_ASAN=1). CPU only: no device, no install, no EXE. Every case asserts its own outputs inside the program; here
each case's line must say PASS and the exit code 0. A missing binary fails (no skip).

    set C5_LIVETERRAIN_BUILD=C:\\c5-64bit-work\\0465\\4907\\lt-tests
    set PYTEST_DISABLE_PLUGIN_AUTOLOAD=1
    python -m pytest server/tests/render/liveterrain/test_liveterrain.py -q -p no:cacheprovider
"""
import os
import subprocess
from pathlib import Path

CASES = ["table_and_filter", "apply_and_hash", "live_inline", "create_failures", "threaded", "journal", "journal_merged", "bundle_text",
         "map_seed",   # ticket 4912
         "land_water",   # ticket 4914 (1)
         "coast_degenerate",   # MC #9538 item 1 (epic 0615 fix forward)
         "land_water_off",   # MC #9538 item 4
         "coast_counts",   # MC #9538 item 2
         "river_edges",   # ticket 0631
         "river_band_mark",   # ticket 0717
         "unknown_wonder_pass"]   # ticket 0613


def exe():
    d = os.environ.get("C5_LIVETERRAIN_BUILD", "")
    sub = "bin-asan" if os.environ.get("C5_LIVETERRAIN_ASAN") == "1" else "bin"
    p = Path(d) / sub / "test_liveterrain.exe"
    assert d and p.exists(), "C5_LIVETERRAIN_BUILD must name a test_liveterrain_build.py --out folder with %s/test_liveterrain.exe" % sub
    return str(p)


def run(*args, timeout=300):
    p = subprocess.run([exe(), *args], capture_output=True, text=True, timeout=timeout, creationflags=0x08000000)
    return p.returncode, p.stdout + p.stderr


def test_liveterrain_cases():
    rc, out = run()
    for case in CASES:
        assert ("CASE %s PASS" % case) in out, out
    assert "ERROR: AddressSanitizer" not in out, out
    assert "0 failed" in out and rc == 0, out


def test_liveterrain_unknown_case_is_refused():
    rc, out = run("no_such_case")
    assert rc == 2 and "no case matches" in out, out


def test_liveterrain_every_case_is_listed_here():
    """The program's case list and this file's agree (a new case cannot be forgotten here)."""
    rc, out = run()
    names = [ln.split()[1] for ln in out.splitlines() if ln.startswith("CASE ")]
    assert rc == 0 and sorted(names) == sorted(CASES), out
