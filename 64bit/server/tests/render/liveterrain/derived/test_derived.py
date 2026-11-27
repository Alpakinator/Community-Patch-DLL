"""Ticket 4904 (epic 0465): the trees on the new ground after a live window (server/src/render/srv_liveterrain_derived.*).

Runs test_derived.exe (built by test_derived_build.py into C5_4904_DERIVED_BUILD/bin) once: the frame check, the unit
cases and the two recorded chains of 4903 (Tiny FH, 4 steps; huge, 2 steps). Each case's line must say PASS and the exit
code be 0. A missing binary, C5R_GAME_DIR or recorded input fails (no skip).

    set C5_4904_DERIVED_BUILD=C:\\c5-64bit-work\\0465\\4904\\build\\derived
    set C5R_GAME_DIR=%USERPROFILE%\\Desktop\\pure_vp\\Sid Meier's Civilization V
    set C5_4904_DATA=...   (optional; default C:/c5-64bit-work/keep/liveterrain-derived-4903, made by make_inputs.sh)
    set C5_4904_TINY_TAP=<a Tiny .c5hf of 4906's bundles>   (default: PT01's load0003.c5hf on F:)
    python -m pytest server/tests/render/liveterrain/derived/test_derived.py -q -p no:cacheprovider
"""
import os
import subprocess
from pathlib import Path

CASES = ["frame", "units", "tiny_fh", "huge_chain"]


def exe():
    d = os.environ.get("C5_4904_DERIVED_BUILD", "")
    p = Path(d) / "bin" / "test_derived.exe"
    assert d and p.exists(), "C5_4904_DERIVED_BUILD must name a test_derived_build.py --out folder with bin/test_derived.exe"
    assert os.environ.get("C5R_GAME_DIR"), "C5R_GAME_DIR must name an install (read-only)"
    return str(p)


def run(*args, timeout=900):
    p = subprocess.run([exe(), *args], capture_output=True, text=True, timeout=timeout, creationflags=0x08000000)
    return p.returncode, p.stdout + p.stderr


def test_derived_cases():
    rc, out = run()
    for case in CASES:
        assert ("CASE %s PASS" % case) in out, out
    assert '"plots_differing_from_whole_map":0' in out and '"outside_changed":0' in out, out
    assert rc == 0, out


def test_derived_unknown_case_is_refused():
    rc, out = run("no_such_case")
    assert rc == 2 and "no case matches" in out, out
