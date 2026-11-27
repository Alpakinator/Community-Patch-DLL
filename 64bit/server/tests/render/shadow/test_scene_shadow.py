"""Issue 4308 (epic 0169): the models' shadow in server.exe's terrain scene (server/src/render/srv_render_scene.cpp) on the
stand-in. The ice's scene cases run from server/tests/render/ice/scene_ice_test.py (the same program).

Runs test_scene_shadow.exe (built by build_scene_shadow.py into C5_SHADOW_SCENE_BUILD/bin): a fake camera block and
height-field tap made by the program, the scene on WARP with the EXE's terrain programs (C5R_EXE, read-only) and a fake
shadow source in place of the tap's reader (and, for shadow_absent_bound, the real reader on a pid without a section).
Every case asserts its own outputs inside the program; here each case's line must say PASS and the exit code 0. Negative
cases: ShadowCopiesFromFrame's refusals, a refused slot (the last good triple stays), Failed (LOUD once, fatal once), OFF
(LOUD once, never fatal), the Absent bound, refused option values. A missing binary or C5R_EXE fails (no skip).

    set C5_SHADOW_SCENE_BUILD=C:\\c5-64bit-work\\0169-4308\\shscene
    set C5R_EXE=%USERPROFILE%\\tools\\ghidra-projects\\bin\\CivilizationV_DX11.exe
    set PYTEST_DISABLE_PLUGIN_AUTOLOAD=1
    python -I -m pytest server/tests/render/shadow/test_scene_shadow.py -q -p no:cacheprovider
"""
import os
import subprocess
from pathlib import Path

CASES = ["shadow_copies", "shadow_none_is_todays_picture", "shadow_qualities_bound", "shadow_refused_keeps_last_good",
         "shadow_failed_is_fatal_once", "shadow_off_loud_once", "shadow_absent_bound", "options"]


def exe():
    d = os.environ.get("C5_SHADOW_SCENE_BUILD", "")
    p = Path(d) / "bin" / "test_scene_shadow.exe"
    assert d and p.exists(), "C5_SHADOW_SCENE_BUILD must name a build_scene_shadow.py --out folder with bin/test_scene_shadow.exe"
    assert os.environ.get("C5R_EXE"), "C5R_EXE must name CivilizationV_DX11.exe (read-only)"
    return str(p)


def run(*args, timeout=600):
    p = subprocess.run([exe(), *args], capture_output=True, text=True, timeout=timeout, creationflags=0x08000000)
    return p.returncode, p.stdout + p.stderr


def test_scene_shadow_cases():
    rc, out = run("shadow")
    for case in CASES:
        assert ("CASE %s PASS" % case) in out, out
    assert rc == 0, out


def test_scene_shadow_unknown_case_is_refused():
    rc, out = run("no_such_case")
    assert rc == 2 and "no case matches" in out, out


def test_scene_shadow_every_case_is_listed_here():
    """The program's shadow cases and this file's agree (a new case cannot be forgotten here)."""
    rc, out = run("shadow")
    names = [ln.split()[1] for ln in out.splitlines() if ln.startswith("CASE ")]
    assert rc == 0 and sorted(names) == sorted(CASES), out
