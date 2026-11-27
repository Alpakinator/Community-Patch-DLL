"""Ticket 4554 (epic 0431): the trees in server.exe's renderer (server/src/render/srv_forest_scene.cpp) on fake sources.

Runs test_forest_scene.exe (built by test_forest_build.py into C5_FOREST_SCENE_BUILD/bin, or bin-asan with
C5_FOREST_SCENE_ASAN=1) once: a fake plot table, batches, a synthetic height field and camera blocks made by the program,
the real c5r::terrain (the EXE's terrain programs and the Screen Overlay pass) and c5r::forest (the EXE's ForestShader,
the install's `_2` files) on WARP. Every case asserts its own outputs inside the program; here each case's line must say
PASS and the exit code 0. Negative cases: every install failure of design 9.5 (fatal once with exit code 5, then without a
hook the scene draws on without trees), the frame failures (the Screen Overlay target, the fog ramp, the layouts), the
feature ids never published, in-play faults (never fatal), a feature event outside the table, render_forest=0.
A missing binary, C5R_EXE or C5R_GAME_DIR fails (no skip).

    set C5_FOREST_SCENE_BUILD=C:\\c5-64bit-work\\4554\\tests
    set C5R_EXE=%USERPROFILE%\\tools\\ghidra-projects\\bin\\CivilizationV_DX11.exe
    set C5R_GAME_DIR=%USERPROFILE%\\Desktop\\pure_vp\\Sid Meier's Civilization V
    python -m pytest server/tests/render/forest/test_forest_scene.py -q -p no:cacheprovider
"""
import os
import subprocess
from pathlib import Path

CASES = ["snapshot", "feature_change", "overflow", "ids_by_name", "ids_never_published", "visibility",
         "stage_overlay_before_trees", "render_forest_off", "install_failures", "frame_failures", "in_play_faults",
         "field_reload", "shadow_seam", "live_window", "residency_generation", "scene_wiring", "host_config", "cut_tap_rebuild", "cut_tap_failures", "factory"]


def exe():
    d = os.environ.get("C5_FOREST_SCENE_BUILD", "")
    sub = "bin-asan" if os.environ.get("C5_FOREST_SCENE_ASAN") == "1" else "bin"
    p = Path(d) / sub / "test_forest_scene.exe"
    assert d and p.exists(), "C5_FOREST_SCENE_BUILD must name a test_forest_build.py --out folder with %s/test_forest_scene.exe" % sub
    assert os.environ.get("C5R_EXE"), "C5R_EXE must name CivilizationV_DX11.exe (read-only)"
    assert os.environ.get("C5R_GAME_DIR"), "C5R_GAME_DIR must name an install (read-only)"
    return str(p)


def run(*args, timeout=900):
    p = subprocess.run([exe(), *args], capture_output=True, text=True, timeout=timeout, creationflags=0x08000000)
    return p.returncode, p.stdout + p.stderr


def test_forest_scene_cases():
    rc, out = run()
    for case in CASES:
        assert ("CASE %s PASS" % case) in out, out
    assert "ERROR: AddressSanitizer" not in out, out
    assert rc == 0, out


def test_forest_scene_unknown_case_is_refused():
    rc, out = run("no_such_case")
    assert rc == 2 and "no case matches" in out, out


def test_forest_scene_every_case_is_listed_here():
    """The program's case list and this file's agree (a new case cannot be forgotten here)."""
    rc, out = run()
    names = [ln.split()[1] for ln in out.splitlines() if ln.startswith("CASE ")]
    assert rc == 0 and sorted(names) == sorted(CASES), out
