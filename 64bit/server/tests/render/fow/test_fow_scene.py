"""Task 3954 (epic 0168): the fog of war in server.exe's terrain scene (server/src/render/srv_fow_scene.cpp) on the stand-in.

Runs test_fow_scene.exe (built by test_fow_build.py into C5_FOW_SCENE_BUILD/bin, or bin-asan with C5_FOW_SCENE_ASAN=1)
once: a fake camera block and height-field tap made by the program, the scene on WARP with the EXE's terrain programs
(C5R_EXE, read-only), a fake overlay source, fake c5r::fow modules and a fake plot table (case
install_failure_fatal_once runs the real modules: the stubs until 3952 / 3955 land). Every case asserts its own outputs
inside the program; here each case's line must say PASS and the exit code 0. Negative cases (done G): option values
refused, no overlay pair (decision 5), an install failure (fatal once, never a frame after it), faults in play (never
fatal), overflow and sequence gaps, bad fog bits. A missing binary or C5R_EXE fails (no skip). The real modules' cases
read the installed game from C5R_GAME_DIR, which this file sets for the program from the setting render_game_dir of
tools/release/settings.py when it is not set (ticket 4716: the program has no default path of its own).

    set C5_FOW_SCENE_BUILD=C:\\c5-64bit-work\\0168\\3954\\tests
    set C5R_EXE=%USERPROFILE%\\tools\\ghidra-projects\\bin\\CivilizationV_DX11.exe
    python -m pytest server/tests/render/fow/test_fow_scene.py -q -p no:cacheprovider
"""
import importlib.util
import os
import subprocess
from pathlib import Path


def program_env():
    """The program's environment: C5R_GAME_DIR as given, or, when it is unset or empty (the program's own test), the
    path setting render_game_dir of tools/release/settings.py as render/build.py reads it; with neither, a path that
    does not exist (the cases that need it fail on it). Ticket 4716 (D174): was a literal path in a user folder in
    test_fow_scene.cpp, the value the setting gives on the workstation."""
    env = dict(os.environ)
    if not env.get("C5R_GAME_DIR"):
        settings = Path(__file__).resolve().parents[4] / "tools" / "release" / "settings.py"
        spec = importlib.util.spec_from_file_location("c5_release_settings", settings)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        v = mod.get("render_game_dir")
        env["C5R_GAME_DIR"] = str(v) if v is not None else "<C5R_GAME_DIR not set: setting render_game_dir>"
    return env

CASES = ["all_visible_todays_picture", "one_plot_per_class", "snapshot_replaces_table", "overflow_rereads", "no_overlay_pair",
         "option", "install_failure_fatal_once", "in_play_fault_never_fatal", "fow_level", "clock", "real_modules",
         "tile_styles", "tile_styles_follow", "tile_styles_late_table",   # the last three: issue 4305
         "model_fog_handover", "model_fog_not_wanted",   # ticket 0339 (listed here by 0394: the program had them, this list not)
         "particles", "particles_fail",   # ticket 0340
         "stall_not_counted",   # ticket 0429
         "slow_load_waits",   # ticket 0491
         "particles_sequence", "particle_state",   # ticket 0403
         "held_frame_prepares", "held_frame_off", "held_frame_waits_for_styles",
         "held_frame_stale_dropped",   # ticket 0424 (listed here by 0536: the program had them, this list not)
         "zoom_out_step_leaves_no_pixel_without_mask", "far_jump_mask_not_empty", "at_rest_unchanged",
         "camera_window_moves_overlay", "camera_window_handover",   # ticket 0674
         "camera_window_history",   # ticket 0793
         "hold_report",   # ticket 0691 (MC #11169)
         "field_hold_on_reload",   # ticket 0691 (cand4's reload deadlock, 2026-11-23)
         "units_plot_table_5052"]   # ticket 5052 (T)


def exe():
    d = os.environ.get("C5_FOW_SCENE_BUILD", "")
    sub = "bin-asan" if os.environ.get("C5_FOW_SCENE_ASAN") == "1" else "bin"
    p = Path(d) / sub / "test_fow_scene.exe"
    assert d and p.exists(), "C5_FOW_SCENE_BUILD must name a test_fow_build.py --out folder with %s/test_fow_scene.exe" % sub
    assert os.environ.get("C5R_EXE"), "C5R_EXE must name CivilizationV_DX11.exe (read-only)"
    return str(p)


def run(*args, timeout=600):
    p = subprocess.run([exe(), *args], capture_output=True, text=True, timeout=timeout, creationflags=0x08000000,
                       env=program_env())
    return p.returncode, p.stdout + p.stderr


def test_fow_scene_cases():
    rc, out = run()
    for case in CASES:
        assert ("CASE %s PASS" % case) in out, out
    assert "ERROR: AddressSanitizer" not in out, out
    assert rc == 0, out


def test_fow_scene_unknown_case_is_refused():
    rc, out = run("no_such_case")
    assert rc == 2 and "no case matches" in out, out


def test_fow_scene_every_case_is_listed_here():
    """The program's case list and this file's agree (a new case cannot be forgotten here)."""
    rc, out = run()
    names = [ln.split()[1] for ln in out.splitlines() if ln.startswith("CASE ")]
    assert rc == 0 and sorted(names) == sorted(CASES), out
