"""Task 3903 (epic 0301): the offline scene harness's sources, the clock passed in and the settle call.

Runs sources_test.exe (built from server/tests/render/scenebundle/sources_test.cpp with the render units of server.exe;
C5_SCENEBUNDLE_TEST_BUILD names the build folder holding bin/sources_test.exe, or bin-asan/ with C5_SCENEBUNDLE_ASAN=1)
once with every case: the water tap's sink and file entry, Scene::loadField, the clock passed in (held = frozen, the
state then N frames = the live frames, waves included; on the fake module and with the real module on WARP), the file
and recording overlay sources, the live, recording and file feeds (a residency replay), and the settle call. Every
case asserts its own outputs inside the program, the negatives included (contract 10: a refused image, a texture of
the wrong size or format, a feed record out of order, a settle that times out, a stopped residency); here each case's
line must say PASS and the exit code 0. A missing binary, C5R_EXE or installed game fails (no skip, COMMON 9).

    set C5_SCENEBUNDLE_TEST_BUILD=C:\\c5-64bit-work\\0301\\3903\\st
    set C5R_EXE=%USERPROFILE%\\tools\\ghidra-projects\\bin\\CivilizationV_DX11.exe
    python -m pytest server/tests/render/scenebundle/test_sources.py -q -p no:cacheprovider
"""
import os
import subprocess
from pathlib import Path

CASES = ["water_load_equals_poll", "water_sink_null_is_today", "water_load_refused", "water_scene_load_image",
         "clock_held_equals_freeze", "clock_state_replay", "clock_state_refused", "field_load_equals_tap", "field_load_refused",
         "scene_clock_held_equals_freeze", "scene_clock_state_replay", "overlay_file_binds_same", "overlay_file_refused",
         "overlay_recording", "feed_replay_residency", "feed_file_order", "feed_file_out_of_order", "feed_file_refused",
         "feed_recording_pass_through", "settle_until_no_bake_pending", "settle_timeout", "settle_stopped",
         "settle_no_source", "bundle_forms"]


def exe():
    d = os.environ.get("C5_SCENEBUNDLE_TEST_BUILD", "")
    sub = "bin-asan" if os.environ.get("C5_SCENEBUNDLE_ASAN") == "1" else "bin"
    p = Path(d) / sub / "sources_test.exe"
    assert d and p.exists(), "C5_SCENEBUNDLE_TEST_BUILD must name a build folder with %s/sources_test.exe" % sub
    assert os.environ.get("C5R_EXE"), "C5R_EXE must name CivilizationV_DX11.exe (read-only)"
    return str(p)


def run(*args, timeout=600):
    p = subprocess.run([exe(), *args], capture_output=True, text=True, timeout=timeout, creationflags=0x08000000)
    return p.returncode, p.stdout + p.stderr


def test_sources_cases(tmp_path):
    rc, out = run("--work", str(tmp_path))   # the bundle case writes its synthetic bundle there
    for case in CASES:
        assert ("CASE %s PASS" % case) in out, out
    assert rc == 0, out


def test_sources_unknown_case_is_refused():
    rc, out = run("no_such_case")
    assert rc == 2 and "no case matches" in out, out


def test_sources_every_case_is_listed_here():
    """The program's case list and this file's agree (a new case cannot be forgotten here)."""
    rc, out = run("feed_file_refused")
    assert rc == 0 and "CASE feed_file_refused PASS" in out, out
    src = (Path(__file__).resolve().parent / "sources_test.cpp").read_text(encoding="utf-8")
    names = [ln.split('"')[1] for ln in src.splitlines() if ln.strip().startswith('{"') and ', Case' in ln]
    assert sorted(names) == sorted(CASES), names
