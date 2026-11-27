"""Issue 4103 (epic 0166): the decal tap's reader, its file form and the per-tile query, on images made by the program.

Runs decal_tap_test.exe (built by build_decal_tests.py into C5_DECAL_BUILD/bin, or bin-asan with C5_DECAL_ASAN=1) once
per case. Every case asserts its own outputs inside the program (each refusal's ackResult, counter and file-form code;
the tiles a generation raises; the per-tile order and edges); here each case's line must say PASS, the exit code be 0,
and no AddressSanitizer report appear. Negative cases of the runner itself: an unknown case (exit 2) and a case made to
fail (exit 1, FAIL lines). A missing binary fails (no skip).

    set C5_DECAL_BUILD=C:\\c5-64bit-work\\0166\\4103\\build
    python -I -m pytest server/tests/render/decal/test_decal_tap.py -q -p no:cacheprovider
"""
import os
import subprocess
from pathlib import Path

import pytest

CASES = ["section_and_file_equal", "refusals", "refusals_other", "torn", "changes_tiles", "tile_query", "area", "walk", "fuzz",
         "delta", "selftest_fail"]
EXPECT_FAIL = {"selftest_fail"}


def exe():
    d = os.environ.get("C5_DECAL_BUILD", "")
    sub = "bin-asan" if os.environ.get("C5_DECAL_ASAN") == "1" else "bin"
    p = Path(d) / sub / "decal_tap_test.exe"
    assert d and p.exists(), f"C5_DECAL_BUILD must name a build_decal_tests.py --out folder with {sub}/decal_tap_test.exe"
    return str(p)


def run(*args, timeout=300):
    p = subprocess.run([exe(), *args], capture_output=True, text=True, timeout=timeout, creationflags=0x08000000)
    return p.returncode, p.stdout + p.stderr


@pytest.mark.parametrize("case", [c for c in CASES if c not in EXPECT_FAIL])
def test_decal_case(case, tmp_path):
    rc, out = run(case, str(tmp_path))
    assert rc == 0 and ("CASE %s PASS" % case) in out and "FAIL line" not in out, out
    assert "AddressSanitizer" not in out, out


def test_a_failing_check_fails_the_case(tmp_path):
    rc, out = run("selftest_fail", str(tmp_path))
    assert rc == 1 and "CASE selftest_fail FAIL" in out and "FAIL line" in out, out


def test_unknown_case_is_refused():
    rc, out = run("no_such_case")
    assert rc == 2 and "no case matches" in out, out


def test_every_case_is_listed_here():
    """The program's case list and this file's agree (a new case cannot be forgotten here)."""
    rc, out = run()
    names = [ln.split()[1] for ln in out.splitlines() if ln.startswith("CASE ")]
    assert rc == 0 and sorted(names) == sorted(CASES), out
