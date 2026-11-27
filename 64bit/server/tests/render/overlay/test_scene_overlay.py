"""Issue 3704 (epic 0167): the overlay in server.exe's terrain scene (server/src/render/srv_render_scene.cpp) on the stand-in.

Runs test_scene_overlay.exe (built by test_scene_build.py into C5_OVERLAY_SCENE_BUILD/bin) once per case: a fake camera
block and height-field tap made by the program, the scene on WARP with the EXE's terrain programs (C5R_EXE, read-only),
and a fake overlay source in place of the tap's reader. Every case asserts its own outputs inside the program; here
each case's line must say PASS and the exit code 0. Negative cases (done G): pairs the scene must refuse (numViews 0, a
NaN matrix), wrap views that fit no copy, a Failed source (decision 5: LOUD once, the fatal text with the reason), the
transparent states, render_overlay values that are refused. A missing binary or C5R_EXE fails (no skip).

    set C5_OVERLAY_SCENE_BUILD=C:\\c5-64bit-work\\0167\\3704\\scenetests
    set C5R_EXE=%USERPROFILE%\\tools\\ghidra-projects\\bin\\CivilizationV_DX11.exe
    python -m pytest server/tests/render/overlay/test_scene_overlay.py -q -p no:cacheprovider
"""
import os
import subprocess
from pathlib import Path

CASES = ["copies", "todays_picture", "pair_bound", "failed_is_fatal_once", "states_transparent", "bad_pair", "option",
         "fatal_hook"]


def exe():
    d = os.environ.get("C5_OVERLAY_SCENE_BUILD", "")
    p = Path(d) / "bin" / "test_scene_overlay.exe"
    assert d and p.exists(), "C5_OVERLAY_SCENE_BUILD must name a test_scene_build.py --out folder with bin/test_scene_overlay.exe"
    assert os.environ.get("C5R_EXE"), "C5R_EXE must name CivilizationV_DX11.exe (read-only)"
    return str(p)


def run(*args, timeout=300):
    p = subprocess.run([exe(), *args], capture_output=True, text=True, timeout=timeout, creationflags=0x08000000)
    return p.returncode, p.stdout + p.stderr


def test_scene_overlay_cases():
    rc, out = run()
    for case in CASES:
        assert ("CASE %s PASS" % case) in out, out
    assert rc == 0, out


def test_scene_overlay_unknown_case_is_refused():
    rc, out = run("no_such_case")
    assert rc == 2 and "no case matches" in out, out


def test_scene_overlay_every_case_is_listed_here():
    """The program's case list and this file's agree (a new case cannot be forgotten here)."""
    rc, out = run()
    names = [ln.split()[1] for ln in out.splitlines() if ln.startswith("CASE ")]
    assert rc == 0 and sorted(names) == sorted(CASES), out
