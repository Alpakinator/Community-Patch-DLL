"""Issue 3607 (epic 3600): server.exe's water scene (server/src/render/srv_water_scene.cpp) on the stand-in.

Runs water_scene_test.exe (built by build_water_tests.py into C5_WATER_BUILD/bin) once per case: a fake camera block
and a fake water tap section made by the program, a fake water module that records what the scene hands it, and one
case with the real c5r::water module on WARP with the installed EXE (read-only; ticket 0589: C5_GAME_DIR, else
C5R_GAME_DIR, else the setting render_game_dir of tools/release/settings.py, never the Steam install, and only build
4390913 unless C5_ALLOW_UNKNOWN_EXE_BUILD=1; server/tests/render/test_install.h).
Every case asserts its own outputs inside the program; here each case's line must say PASS and the exit code 0.
Negative cases: no tap, a refused image (hash), a stale load, an image of another load, a NaN camera and a camera of
size 0, a module that does not initialise (fake, and the real one with a missing EXE), render_water=0 and placeholder;
install_rules (0589): the install's order, the placeholder with nothing set, and the EXE build check on made-up files.
A missing binary fails (no skip).

    set C5_WATER_BUILD=C:\\c5-64bit-work\\3607\\water-build
    python -m pytest server/tests/render/water/test_water_scene.py -q -p no:cacheprovider
"""
import os
import subprocess
from pathlib import Path

import pytest

CASES = ["install_rules",   # ticket 0589
         "no_tap", "module_draws", "clock", "freeze", "refused", "stale", "other_load", "nan_camera", "module_missing",
         "modes", "waves_drawn", "waves_bad", "log_once", "real_module_bad_exe", "real_module_draws",
         "shared_index_handed", "real_module_shared_index",   # issue 0198: the scene's index handed to the waves
         "reflection_hook", "reflection_hook_fails", "reflection_off", "reflection_bad_camera",   # issue 0382
         "reflection_models", "reflection_models_ice_off", "reflection_models_bad_camera",   # issue 4464: the models' hook
         "reflection_models_fail",
         "field_region"]   # ticket 4909: the live edit's region update and its fallbacks


def exe():
    d = os.environ.get("C5_WATER_BUILD", "")
    p = Path(d) / "bin" / "water_scene_test.exe"
    assert d and p.exists(), "C5_WATER_BUILD must name a build_water_tests.py --out folder with bin/water_scene_test.exe"
    return str(p)


def run(*args, timeout=180):
    p = subprocess.run([exe(), *args], capture_output=True, text=True, timeout=timeout, creationflags=0x08000000)
    return p.returncode, p.stdout + p.stderr


@pytest.mark.parametrize("case", CASES)
def test_water_scene_case(case):
    rc, out = run(case)
    assert rc == 0 and ("CASE %s PASS" % case) in out, out


def test_unknown_case_is_refused():
    rc, out = run("no_such_case")
    assert rc == 2 and "no case matches" in out, out


def test_every_case_is_listed_here():
    """The program's case list and this file's agree (a new case cannot be forgotten here)."""
    rc, out = run()
    names = [ln.split()[1] for ln in out.splitlines() if ln.startswith("CASE ")]
    assert rc == 0 and sorted(names) == sorted(CASES), out
