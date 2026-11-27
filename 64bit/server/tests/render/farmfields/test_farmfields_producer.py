"""Ticket 4852 (epic 0528): the farm producer in server.exe's renderer (server/src/render/srv_farmfields.cpp) on fake sources.

Runs test_farmfields_producer.exe (built by test_farmfields_build.py into C5_FARMFIELDS_BUILD/bin, or bin-asan with
C5_FARMFIELDS_ASAN=1). CPU only: no device, no install, no EXE. Every case asserts its own outputs inside the program; here
each case's line must say PASS and the exit code 0. A missing binary fails (no skip).

    set C5_FARMFIELDS_BUILD=C:\\c5-64bit-work\\0528\\4852\\tests
    set PYTEST_DISABLE_PLUGIN_AUTOLOAD=1
    python -m pytest server/tests/render/farmfields/test_farmfields_producer.py -q -p no:cacheprovider
"""
import os
import subprocess
from pathlib import Path

CASES = ["settle_busy", "not_threaded", "ids_wait_loud", "ids_by_name", "revealed_rule", "seed_fallback", "compose_through_scene",
         "bundle_round_trip", "bundle_playback_equals_recorded", "older_player_loads_farm_bundle", "negatives",
         "footprint_into_cut",   # ticket 4853
         "buildings_follow_the_fields", "buildings_threaded", "buildings_rows_and_height",
         "buildings_restored_span",   # ticket 0653
         "building_points"]   # ticket 0657


def exe():
    d = os.environ.get("C5_FARMFIELDS_BUILD", "")
    sub = "bin-asan" if os.environ.get("C5_FARMFIELDS_ASAN") == "1" else "bin"
    p = Path(d) / sub / "test_farmfields_producer.exe"
    assert d and p.exists(), "C5_FARMFIELDS_BUILD must name a test_farmfields_build.py --out folder with %s/test_farmfields_producer.exe" % sub
    return str(p)


def run(*args, timeout=600):
    p = subprocess.run([exe(), *args], capture_output=True, text=True, timeout=timeout, creationflags=0x08000000)
    return p.returncode, p.stdout + p.stderr


def test_farmfields_producer_cases():
    rc, out = run()
    for case in CASES:
        assert ("CASE %s PASS" % case) in out, out
    assert "ERROR: AddressSanitizer" not in out, out
    assert rc == 0, out


def test_farmfields_producer_unknown_case_is_refused():
    rc, out = run("no_such_case")
    assert rc == 2 and "no case matches" in out, out


def test_farmfields_producer_every_case_is_listed_here():
    """The program's case list and this file's agree (a new case cannot be forgotten here)."""
    rc, out = run()
    names = [ln.split()[1] for ln in out.splitlines() if ln.startswith("CASE ")]
    assert rc == 0 and sorted(names) == sorted(CASES), out
