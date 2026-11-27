"""Issue 0440: with no recording, the render host draws the frames it drew before 0440 (the MC's condition of grant #2934:
"with no recording running, frames bit-equal to main by test").

0440 changed the recorder's lines of server/src/render/srv_render_host.cpp's DrawFrame (a frame is drawn as `recFrame`,
the frame with the recorder's clock, whenever a recorder exists; as `fr` without one) and StartRecorder. This test runs the
same scene twice with each of two builds of render_host_test.exe (server/tests/render/build_render_tests.py): A, built from
a tree whose server/src/render is main's (C5_RENDER_TEST_BUILD_MAIN), and B, built from this branch (C5_RENDER_TEST_BUILD),
with no render_test_record key: a static camera over the synthetic field of the fake composite (renderfx), the server's
own dump of its drawn frame (render_test_dump: colour BMP and depth). It asserts each build's two runs equal (the scene is
deterministic, so the comparison means something) and A's dump equal to B's, byte for byte, colour and depth.

    set C5_RENDER_TEST_BUILD=<this branch's build_render_tests.py --out>/bin
    set C5_RENDER_TEST_BUILD_MAIN=<main's build_render_tests.py --out>/bin
    set C5R_EXE=<CivilizationV_DX11.exe, read only>
    python -m pytest -q -p no:cacheprovider server/tests/render/scenebundle/test_host_unchanged.py

A missing build FAILS (never skips: COMMON 9). Every process on the hidden desktop (COMMON 14, the conftest's fixture).
Own code, 2026-11-16 (GPL-3.0-or-later).
"""
import hashlib
import os
import time
from pathlib import Path

import pytest

import renderfx as fx


def build_dir(var):
    b = Path(os.environ.get(var, ""))
    assert os.environ.get(var) and (b / "render_host_test.exe").is_file(), "%s must name a build_render_tests.py bin folder" % var
    return b


def draw_once(tmp, desktop, built, exe, tag):
    """One run: the static camera and the field, the host's own dump of the frame it drew; returns (colour, depth) bytes."""
    r = fx.Rig(tmp, desktop, built, exe, tag)
    try:
        r.sec.write_camera(fx.synth_camera(frame=1))
        r.tap(load_seq=1)
        p = r.start(seconds=6, render_test_dump=str(r.dump), render_test_dump_after=1)
        assert r.finish(p, timeout=120) == 0, r.stdout()[-2000:]
        t = r.text()
        assert "render_test_record" not in t, "a recording key reached the host"
        tot = fx.totals(t)
        assert 1 <= tot["drawn"] <= 2, tot
        deadline = time.time() + 10
        while not (r.dump.exists() and Path(str(r.dump) + ".depth.f32").exists()) and time.time() < deadline:
            time.sleep(0.1)
        colour = r.dump.read_bytes()
        depth = Path(str(r.dump) + ".depth.f32").read_bytes()
        assert len(colour) > 54 and len(depth) == fx.W * fx.H * 4, (len(colour), len(depth))
        return colour, depth
    finally:
        r.close()


def sha(b):
    return hashlib.sha256(b).hexdigest()


def test_frames_without_recording_equal_main(tmp_path, hidden_desktop, exe):
    main, branch = build_dir("C5_RENDER_TEST_BUILD_MAIN"), build_dir("C5_RENDER_TEST_BUILD")
    runs = {}
    for name, b in (("main", main), ("branch", branch)):
        for k in (1, 2):
            d = tmp_path / ("%s%d" % (name, k))
            d.mkdir()
            runs[(name, k)] = draw_once(d, hidden_desktop, b, exe, "%s%d" % (name, k))
    line = "; ".join("%s run %d colour %s depth %s" % (n, k, sha(c)[:16], sha(dp)[:16]) for (n, k), (c, dp) in sorted(runs.items()))
    print("test_host_unchanged: " + line)
    for name in ("main", "branch"):
        assert runs[(name, 1)] == runs[(name, 2)], "%s: two runs differ: the scene is not deterministic: %s" % (name, line)
    assert runs[("main", 1)][0] == runs[("branch", 1)][0], "colour differs from main: " + line
    assert runs[("main", 1)][1] == runs[("branch", 1)][1], "depth differs from main: " + line


def test_missing_main_build_fails(monkeypatch):
    """The build variable unset or wrong: an assertion, never a skip."""
    monkeypatch.setenv("C5_RENDER_TEST_BUILD_MAIN", "C:/no/such/build_0440")
    with pytest.raises(AssertionError):
        build_dir("C5_RENDER_TEST_BUILD_MAIN")
