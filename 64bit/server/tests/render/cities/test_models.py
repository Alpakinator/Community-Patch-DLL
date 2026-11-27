"""Ticket 4875 (epic 0561): the server's side of the model tap: the tap reader, the identity-set publisher, the mode
writer of VPMODE_CITIES / VPMODE_LANDMARKS (D118 per bit), the module with a stand-in layer, the feed's model database flag.

Runs models_test.exe (built by build_models_tests.py into C5_MODELS_BUILD/bin, or bin-asan with C5_MODELS_ASAN=1) once
per case. Every case asserts its own outputs inside the program (each refusal's reason, counter and ackResult; the set's
bits and classes; the word's bits and store order; the fatal texts); here each case's line must say PASS, the exit code
be 0, and no AddressSanitizer report appear. Negative cases of the runner itself: an unknown case (exit 2) and a case made
to fail (exit 1, FAIL lines). A missing binary fails (no skip).

    set C5_MODELS_BUILD=C:\\c5-64bit-work\\0561\\4875\\build
    set PYTEST_DISABLE_PLUGIN_AUTOLOAD=1
    python -I -m pytest server/tests/render/cities/test_models.py -q -p no:cacheprovider
"""
import os
import subprocess
from pathlib import Path

import pytest

CASES = ["reader_accepts", "reader_refusals", "reader_torn", "set_build", "set_publish", "set_check_negative",
         "mode_sets_and_keeps_units", "mode_stale_and_hold", "mode_load_ends_loss", "mode_stall_steps", "mode_filter_fatal",
         "mode_install_bound", "scene_full_path", "scene_liveness_without_units", "geom_reader","scene_set_refused", "scene_failures", "scene_file_image", "scene_pairing",
         "scene_recorded_image", "scene_geometry_member", "scene_set_rule", "feed_flag", "feed_model_load_begin", "scene_camera_rows", "keep_ground_box", "keep_rule", "scene_keep_switch",
         "store_rule", "scene_store_switch", "scene_store_ownership", "scene_cut_tap",   # ticket 0789
         "server_draws_held",   # ticket 0788
         "selftest_fail"]
EXPECT_FAIL = {"selftest_fail"}


def exe():
    d = os.environ.get("C5_MODELS_BUILD", "")
    sub = "bin-asan" if os.environ.get("C5_MODELS_ASAN") == "1" else "bin"
    p = Path(d) / sub / "models_test.exe"
    assert d and p.exists(), f"C5_MODELS_BUILD must name a build_models_tests.py --out folder with {sub}/models_test.exe"
    return str(p)


def run(*args, timeout=300):
    p = subprocess.run([exe(), *args], capture_output=True, text=True, timeout=timeout, creationflags=0x08000000)
    return p.returncode, p.stdout + p.stderr


@pytest.mark.parametrize("case", [c for c in CASES if c not in EXPECT_FAIL])
def test_models_case(case, tmp_path):
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
