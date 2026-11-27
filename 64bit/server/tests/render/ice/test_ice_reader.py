"""Issue 4307 (epic 0169): the ice tap's reader (server/src/render/srv_ice_tap.cpp) and its file form, on images made by
the program (ice_tap_test.exe, build_ice_tests.py).

Runs ice_tap_test.exe (built into C5_ICE_BUILD/bin, or bin-asan with C5_ICE_ASAN=1) once per case. Every case asserts its
own outputs inside the program: each refusal's reason, ackResult, counter, lastWhy text and file-form code, and that the
last good mesh stays (refusals: 24 image cases, 7 block cases, a short view); the section and the file form give the same
mesh byte for byte (section_and_file_equal); a torn control block is counted and never acked (torn); a refused first
image is an install failure until a good one arrives (first_refused); the producer's states OFF, WRITING, NO_MESH, FAILED
CHANGED (transient) and FAILED PATTERN / DESC (install failure with the producer's text) (producer_states); single-byte
changes and truncations never crash (fuzz). Here each case's line must say PASS, the exit code be 0, and no
AddressSanitizer report appear. Negative cases of the runner itself: an unknown case (exit 2) and a case made to fail
(exit 1, FAIL lines). A missing binary fails (no skip).

    set C5_ICE_BUILD=C:\\c5-64bit-work\\0169-4307\\reader
    set PYTEST_DISABLE_PLUGIN_AUTOLOAD=1
    python -I -m pytest server/tests/render/ice/test_ice_reader.py -q -p no:cacheprovider
"""
import importlib.util
import os
import subprocess
from pathlib import Path

import pytest

CASES = ["section_and_file_equal", "refusals", "torn", "first_refused", "producer_states", "fuzz", "selftest_fail"]
EXPECT_FAIL = {"selftest_fail"}


def exe():
    d = os.environ.get("C5_ICE_BUILD", "")
    sub = "bin-asan" if os.environ.get("C5_ICE_ASAN") == "1" else "bin"
    p = Path(d) / sub / "ice_tap_test.exe"
    assert d and p.exists(), f"C5_ICE_BUILD must name a build_ice_tests.py --out folder with {sub}/ice_tap_test.exe"
    return str(p)


def run(*args, timeout=300):
    p = subprocess.run([exe(), *args], capture_output=True, text=True, timeout=timeout, creationflags=0x08000000)
    return p.returncode, p.stdout + p.stderr


@pytest.mark.parametrize("case", [c for c in CASES if c not in EXPECT_FAIL])
def test_ice_case(case, tmp_path):
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


LIVE_IMAGES = [Path(os.environ.get("C5_ICE_TINY_IMAGE", "F:/c5-64bit-work/0169/4307/tiny/load1/ice.c5ice")),
               Path(os.environ.get("C5_ICE_HUGE_IMAGE", "F:/c5-64bit-work/0169/4307/ref_huge/ice.c5ice"))]


def release_setting(env, key):
    """Ticket 4716 (D174), as server/tests/render/conftest.py's release_setting: `env` if set, else the path setting
    `key` of tools/release/settings.py, as render/build.py reads it; with neither, a path that does not exist (the test
    fails on it). Was a literal path in a user folder, the value the setting gives on the workstation."""
    if env in os.environ:
        return Path(os.environ[env])
    spec = importlib.util.spec_from_file_location("c5_release_settings",
                                                  Path(__file__).resolve().parents[4] / "tools" / "release" / "settings.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    v = mod.get(key)
    return Path(v) if v is not None else Path("<%s not set: setting %s of tools/release/settings.py>" % (env, key))


def test_setmesh_accepts_the_readers_meshes():
    """The reader's Mesh of the live Tiny image (1 chunk) and of the huge reference set (8 chunks) is accepted by
    c5r::ice::Renderer::setMesh (4306's own checks, on a hardware device with the EXE's IceShader and the install's
    textures); a chunk moved past the IB is refused. Needs build_ice_tests.py --setmesh; the images are game data on F:
    (polar-ice.md 9, D83), named by C5_ICE_TINY_IMAGE / C5_ICE_HUGE_IMAGE; missing ones fail (no skip)."""
    p = Path(exe()).parent / "ice_setmesh_test.exe"
    assert p.exists(), "build with build_ice_tests.py --setmesh"
    exe_path = release_setting("C5R_EXE", "render_exe")
    game = release_setting("C5R_GAME_DIR", "render_game_dir")
    assert exe_path.exists() and game.exists() and all(i.exists() for i in LIVE_IMAGES), (exe_path, game, LIVE_IMAGES)
    r = subprocess.run([str(p), "--exe", str(exe_path), "--game", str(game), *map(str, LIVE_IMAGES)], capture_output=True,
                       text=True, timeout=300, creationflags=0x08000000)
    out = r.stdout + r.stderr
    assert r.returncode == 0 and "SETMESH 0 of 2 images failed" in out, out
    assert ": 1 chunks, 96785 triangles, VB 489112 bytes, IB 580710 bytes" in out, out
    assert ": 8 chunks, 427774 triangles, VB 2198304 bytes, IB 2566644 bytes" in out, out
    assert "SETMESH refused as expected: setMesh: chunk 0: indices 290355..580710 lie past the IB's 290355" in out, out
    assert "AddressSanitizer" not in out, out
    r = subprocess.run([str(p)], capture_output=True, text=True, timeout=60, creationflags=0x08000000)
    assert r.returncode == 2 and "usage:" in r.stdout


def test_tools_refuse_bad_arguments(tmp_path):
    rc, out = run("read")
    assert rc == 2 and "--pid required" in out, out
    rc, out = run("read", "--pid", "1", "--bogus")
    assert rc == 2 and "unknown argument --bogus" in out, out
    rc, out = run("read", "--pid", "4294967290", "--seconds", "0.3")
    assert rc == 3 and "state Absent" in out, out
    rc, out = run("file", str(tmp_path / "none.c5ice"))
    assert rc == 1 and "refused (Io): cannot open" in out, out
    junk = tmp_path / "junk.c5ice"
    junk.write_bytes(b"\0" * 200)
    rc, out = run("file", str(junk))
    assert rc == 1 and "refused (BadMagic): magic: image magic 0x00000000" in out, out
