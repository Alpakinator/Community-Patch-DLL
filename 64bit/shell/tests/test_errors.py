"""Issue 3013: a negative test for every error path of the shell programs that can run on the stand-in (COMMON rule 9).
Each test provokes one failure and checks the outputs and the exit code after it. The inventory (which path, which test,
which paths need the live game and why) is in shell/README.md, "Error paths and their tests".

Everything runs on the hidden desktop (rule 14). Broken inputs come from test seams that exist for this purpose only:
test_host --fake-exe-bad / --fake-cam-bad / --format rgb10a2, the DLL's ini keys fault_test and hook_test,
shell_server --test-abandon, and sections written from Python (camblock.Section).
"""
import ctypes
import os
import re
import struct
import subprocess
import sys
import time
from pathlib import Path

import numpy as np
import pytest

import camblock
import shellrun

FREE = (400, 60, 1000, 170)
GREY = (38, 38, 38)
GAME = ("--game", "--vary", "1", "--mt", "1", "--vsync", "0", "--occluded-sleep", "0", "--load", "after")
PLAIN = ("--vsync", "0", "--occluded-sleep", "0", "--load", "after")
_k = ctypes.WinDLL("kernel32")


def qpc() -> int:
    v = ctypes.c_longlong()
    _k.QueryPerformanceCounter(ctypes.byref(v))
    return v.value


def free_area(bmp):
    img = shellrun.Image(bmp)
    px = list(img.region(*FREE))
    return shellrun.fraction(px, shellrun.is_server), shellrun.fraction(px, lambda c: c == GREY)


def grid_colours(bmp):
    img = shellrun.Image(bmp)
    a = np.frombuffer(img.px, dtype=np.uint8).reshape(img.h, img.w, 4)
    x0, y0, x1, y1 = FREE
    a = a[y0:y1, x0:x1].astype(int)
    b, g, r = a[..., 0], a[..., 1], a[..., 2]
    return int(((r > 200) & (g > 200) & (b < 80)).sum()) + int(((r < 80) & (g > 200) & (b > 200)).sum()) + \
        int(((r > 200) & (g < 80) & (b < 80)).sum())


def finish(run, seconds=40):
    code = run.host.wait(seconds)
    assert code == 0, (code, shellrun.tail(run.host_log, 10), shellrun.tail(run.composite_log, 10))


def steady(run, after=None):
    lines = run.log_lines()
    if after is not None:
        i = next(k for k, ln in enumerate(lines) if re.search(after, ln))
        lines = lines[i + 1:]
    return shellrun.stats(lines)[1:]


def server(tmp_path, desktop, built, *args, name="srv", wait=30):
    """shell_server.exe with args on the hidden desktop; returns (exit code, stdout+stderr text, log text)."""
    out, log = tmp_path / ("%s.out" % name), tmp_path / ("%s.log" % name)
    p = shellrun.launch([str(built / "shell_server.exe"), "--log", str(log), *args], desktop, console=True, out_path=out)
    code = p.wait(wait)
    if code is None:
        p.kill()
    p.close()
    return code, out.read_text(errors="replace") if out.exists() else "", log.read_text(errors="replace") if log.exists() else ""


# ==============================================================================================================
# shell_server.exe: command line, start-up failures, dump
# ==============================================================================================================

@pytest.mark.parametrize("bad, said", [
    (["--bogus", "1"], "unknown argument --bogus"),
    (["stray"], "unknown argument stray"),
    (["--size", "abc"], "bad value for --size"),
    (["--size", "8x8"], "bad value for --size"),
    (["--size", "1920x1080x"], "bad value for --size"),
    (["--rate", "fast"], "bad value for --rate"),
    (["--rate", "-5"], "bad value for --rate"),
    (["--scene", "grids"], "bad value for --scene"),
    (["--grid-stale", "pink"], "bad value for --grid-stale"),
    (["--edge-div", "0"], "bad value for --edge-div"),
    (["--stale-frames", "1.5"], "bad value for --stale-frames"),
    (["--luid", "xyz"], "bad value for --luid"),
    (["--seconds"], "--seconds needs a value"),
])
def test_server_refuses_bad_command_line(tmp_path, hidden_desktop, built, bad, said):
    """Exit 2 with the reason, before anything starts: no log, no device, no section."""
    code, out, log = server(tmp_path, hidden_desktop, built, *bad)
    assert code == 2, (code, out)
    assert said in out, out
    assert log == "" and not (tmp_path / "srv.log").exists()


def test_server_no_matching_adapter(tmp_path, hidden_desktop, built):
    code, out, log = server(tmp_path, hidden_desktop, built, "--luid", "DEADBEEF", "--seconds", "5")
    assert code == 1, (code, log)
    assert "no matching adapter" in log
    assert "section" not in log and "device created" not in log


def test_server_bad_section_name(tmp_path, hidden_desktop, built):
    code, out, log = server(tmp_path, hidden_desktop, built, "--section", "Local\\c5_no_dir_%d\\x" % os.getpid(), "--seconds", "5")
    assert code == 1, (code, log)
    assert re.search(r"CreateFileMapping\('Local\\c5_no_dir_\d+\\x'\) failed \d+", log), log[-500:]
    assert "texture generation" not in log


def test_server_dump_and_layout_unwritable(tmp_path, hidden_desktop, built):
    nodir = tmp_path / "no_such_dir"
    code, out, log = server(tmp_path, hidden_desktop, built, "--section", "Local\\VPShellErr_%d_dump" % os.getpid(),
                            "--name", "Local\\VPShellErrTex_%d_dump" % os.getpid(), "--rate", "0", "--size", "64x64",
                            "--dump", str(nodir / "x.bmp"), "--dump-after", "2", "--seconds", "20")
    assert code == 1, (code, log[-500:])
    assert "dump of the server's frame to" in log and "FAILED" in log and "requested but not written" in log
    code, out, _ = server(tmp_path, hidden_desktop, built, "--print-layout", str(nodir / "l.json"), name="layout")
    assert code == 1 and "--print-layout: cannot write" in out, (code, out)
    assert not nodir.exists()


def test_server_grid_dump_without_camera(tmp_path, hidden_desktop, built):
    """--scene grid with nobody publishing a camera: nothing is drawn, the requested dump is never written: exit 1."""
    bmp = tmp_path / "g.bmp"
    code, out, log = server(tmp_path, hidden_desktop, built, "--section", "Local\\VPShellErr_%d_nocam" % os.getpid(),
                            "--name", "Local\\VPShellErrTex_%d_nocam" % os.getpid(), "--scene", "grid", "--rate", "60",
                            "--size", "64x64", "--dump", str(bmp), "--seconds", "2")
    assert code == 1, (code, log[-500:])
    assert "grid: state - -> NO_CAMERA" in log and "requested but not written" in log
    assert not bmp.exists()


# ==============================================================================================================
# shell_server.exe --scene grid with a camera block written from Python (no DLL, no game)
# ==============================================================================================================

def fake_block(**over):
    """The test_host --fake-exe camera (target 0,0, 600 units at 45 degrees, 1280x720) and the Tiny map's grid
    constants (shell/standin/test_host.cpp FakeExeInit), as a block with status OK at present 1."""
    c, s, dist = np.cos(np.pi / 4), np.sin(np.pi / 4), 600.0
    cw = np.array([[1, 0, 0, 0], [0, c, s, 0], [0, -s, c, 0], [0, -dist * s, dist * c, 1]], dtype=np.float64)
    v = np.linalg.inv(cw)
    fov, nz, fz, aspect = 29.0145, 50.0, 4000.0, 1280.0 / 720.0
    ys = 1.0 / np.tan(fov * np.pi / 360.0)
    p = [ys / aspect, 0, 0, 0, 0, ys, 0, 0, 0, 0, fz / (nz - fz), -1, 0, 0, nz * fz / (nz - fz), 0]
    b = {"status": 1, "presentCount": 1, "frameStamp": 1, "viewMode": 1, "numViews": 1, "proj": p,
         "view": list(v.flatten()) + [0.0] * 48, "camWorld": list(cw.flatten()), "fovDeg": fov, "aspect": aspect,
         "nearZ": nz, "farZ": fz, "vpX": 0, "vpY": 0, "vpW": 1280, "vpH": 720, "vpMinZ": 0.0, "vpMaxZ": 1.0,
         "bbW": 1280, "bbH": 720, "gridSeq": 1, "gridValid": 1, "wrapX": 1, "wrapPeriod": 3583.0,
         "aabbMin": [-1791.5, -1108.047, 0], "aabbMax": [1791.5, 1108.047, 255], "worldHeight": 2216.094,
         "w2h": [0.0156255, -2.29105e-7, -0.0156252, 0, -0.0078125, 0.0156247, -0.00781216, 0, 0, 0, 0, 0, 0.5, -2.0, 1.5, 1],
         "h2w": [63.9986, 0.000938416, 0, 0, 32.0, 64.0019, 0, 0, 0, 0, 0, 0, 32.0007, 128.0033, 0, 1],
         "sx": 1.0, "sy": 0.866, "heightScale": 0.2, "ox": 1792.0, "oy": 1108.48, "samplesX": 3584, "samplesY": 2560}
    b.update(over)
    return b


def grid_render(tmp_path, desktop, built, tag, block, *args, seq_odd=False, seconds=20):
    """Write `block` into a fresh section and run the grid scene with --dump. Returns (exit code, log, image or None)."""
    name = "Local\\VPShellErr_%d_%s" % (os.getpid(), tag)
    sec = camblock.Section(name)
    try:
        sec.write_camera(block)
        if seq_odd:
            sec.write(camblock.CAMERA_OFFSET, struct.pack("<I", 7))
        bmp = tmp_path / ("%s.bmp" % tag)
        code, out, log = server(tmp_path, desktop, built, "--section", name, "--name", "Local\\VPShellErrTex_%d_%s" % (os.getpid(), tag),
                                "--scene", "grid", "--rate", "0", "--size", "1280x720", "--dump", str(bmp), "--dump-after", "2",
                                "--seconds", str(seconds), *args, name=tag)
    finally:
        sec.close()
    img = None
    if bmp.exists():
        i = shellrun.Image(bmp)
        img = np.frombuffer(i.px, dtype=np.uint8).reshape(i.h, i.w, 4)
    return code, log, img


def yellow(img):
    b, g, r = img[..., 0].astype(int), img[..., 1].astype(int), img[..., 2].astype(int)
    return (r > 200) & (g > 200) & (b < 80)


def test_server_heightmap_missing_or_wrong_size(tmp_path, hidden_desktop, built):
    """A heightmap that cannot be opened, or whose sample count does not match the map (also an odd byte count):
    logged, and the grid is drawn at flat z, pixel for pixel the same as without a heightmap."""
    code, log, flat = grid_render(tmp_path, hidden_desktop, built, "flat", fake_block())
    assert code == 0 and flat is not None and yellow(flat).sum() > 1000, log[-500:]
    code, log, none = grid_render(tmp_path, hidden_desktop, built, "missing", fake_block(), "--heightmap", str(tmp_path / "none.raw"))
    assert code == 0 and "grid: cannot open heightmap" in log and "flat z" in log, log[-800:]
    assert np.array_equal(none, flat)
    odd = tmp_path / "odd.raw"
    odd.write_bytes(b"\x10" * 1001)
    code, log, wrong = grid_render(tmp_path, hidden_desktop, built, "wrong", fake_block(), "--heightmap", str(odd))
    assert code == 0 and "heightmap has 500 samples, the map needs 3584 x 2560 - flat z" in log, log[-800:]
    assert np.array_equal(wrong, flat)


def test_server_water_z_clamp_and_off(tmp_path, hidden_desktop, built):
    """Issue 3012's clamp, exactly: an all-water heightmap (z 0) with the default --water-z (10 since 2026-11-13, was
    12) draws the same pixels as flat z 10; with --water-z -1 (off) the same pixels as flat z 0; and the two differ."""
    hm = tmp_path / "water.raw"
    hm.write_bytes(bytes(3584 * 2560 * 2))
    one = ("--edge-div", "1")   # same segments as the flat grid, so the comparison can be exact
    _, _, f12 = grid_render(tmp_path, hidden_desktop, built, "f12", fake_block(), "--flat-z", "10")
    _, _, f0 = grid_render(tmp_path, hidden_desktop, built, "f0", fake_block(), "--flat-z", "0")
    code, log, w12 = grid_render(tmp_path, hidden_desktop, built, "w12", fake_block(), "--heightmap", str(hm), *one)
    assert code == 0 and "z from the heightmap" in log, log[-500:]
    code, log, woff = grid_render(tmp_path, hidden_desktop, built, "woff", fake_block(), "--heightmap", str(hm), "--water-z", "-1", *one)
    assert code == 0 and "z from the heightmap" in log, log[-500:]
    assert yellow(f12).sum() > 1000 and yellow(f0).sum() > 1000
    assert np.array_equal(w12, f12)
    assert np.array_equal(woff, f0)
    assert (yellow(f12) & yellow(f0)).sum() < 0.5 * yellow(f12).sum()


def _nan_over(defect):
    """Issue 3015: camera blocks that say OK but carry a non-finite value in a matrix other than the projection."""
    b = fake_block()
    nan, inf = float("nan"), float("inf")
    if defect == "nan-view0":
        return dict(view=[nan] * 16 + list(b["view"][16:]))
    if defect == "nan-view0-x":
        v = list(b["view"]); v[12] = nan
        return dict(view=v)
    if defect == "nan-wrap-view":
        v0 = list(b["view"][:16])
        return dict(numViews=2, view=v0 + [nan] * 16 + [0.0] * 32)
    if defect == "inf-camworld":
        cw = list(b["camWorld"]); cw[13] = inf
        return dict(camWorld=cw)
    raise KeyError(defect)


@pytest.mark.parametrize("defect, said, state", [
    ("implausible", "implausible map size 2000 x 36", None),
    ("too-wide", "implausible map size 257 x 36", None),          # issue 3018 V6: the limit is 256 x 160 plots
    ("too-high", "implausible map size 56 x 161", None),
    ("no-grid", None, "NO_GRID"),
    ("views0", "fails the server's own check", "SRV_INCONSISTENT"),
    ("nonfinite", "fails the server's own check", "SRV_INCONSISTENT"),
    ("nan-view0", "fails the server's own check", "SRV_INCONSISTENT"),       # issue 3015
    ("nan-view0-x", "fails the server's own check", "SRV_INCONSISTENT"),
    ("nan-wrap-view", "fails the server's own check", "SRV_INCONSISTENT"),
    ("inf-camworld", "fails the server's own check", "SRV_INCONSISTENT"),
    ("torn", None, "NO_CAMERA"),
    ("stale", None, "STALE"),
])
def test_server_refuses_bad_camera_block(tmp_path, hidden_desktop, built, defect, said, state):
    """A camera block the server must not draw with: nothing drawn, the dump never written (exit 1), the reason logged."""
    over = {"implausible": dict(samplesX=64 * 2000), "too-wide": dict(samplesX=64 * 257),
            "too-high": dict(samplesY=64 * (161 + 4)), "no-grid": dict(gridValid=0), "views0": dict(numViews=0),
            "nonfinite": dict(proj=[float("inf")] + [0.0] * 15), "torn": {}, "stale": dict(presentCount=100)}.get(defect)
    if over is None:
        over = _nan_over(defect)
    code, log, img = grid_render(tmp_path, hidden_desktop, built, "blk_" + defect.replace("-", ""), fake_block(**over),
                                 "--grid-stale", "hide", seq_odd=defect == "torn", seconds=3)
    assert code == 1 and img is None, (code, log[-800:])
    assert "requested but not written" in log
    if said:
        assert said in log, log[-800:]
    if state:
        assert re.search(r"grid: state \S+ -> %s\b" % state, log), log[-800:]
    if defect == "torn":
        assert re.search(r"snap_fail=[1-9]", log), log[-800:]


def test_server_resize_requests_and_luid_mismatch(tmp_path, hidden_desktop, built):
    """Requests written into the section by a client: out of range (99999 x 100) acknowledged and ignored; a client
    adapter LUID that differs from the server's logged as MISMATCH; a valid request (640 x 360) honoured with a new
    texture generation."""
    name = "Local\\VPShellErr_%d_req" % os.getpid()
    log = tmp_path / "req.log"
    p = shellrun.launch([str(built / "shell_server.exe"), "--section", name, "--name", "Local\\VPShellErrTex_%d_req" % os.getpid(),
                         "--rate", "60", "--size", "320x200", "--seconds", "20", "--log", str(log)], hidden_desktop, console=True)
    try:
        def wait_for(pred, what, t=8.0):
            end = time.time() + t
            while time.time() < end:
                if pred():
                    return
                time.sleep(0.05)
            raise AssertionError("no %s; log tail: %s" % (what, shellrun.tail(log, 8)))

        wait_for(lambda: log.exists() and "texture generation 1" in log.read_text(errors="replace"), "texture generation 1")
        sec = camblock.Section(name)
        try:
            u32 = lambda off: struct.unpack("<I", sec.read(off, 4))[0]  # noqa: E731
            seq = u32(336) + 1
            sec.write(340, struct.pack("<II", 99999, 100))
            sec.write(336, struct.pack("<I", seq))
            wait_for(lambda: u32(348) == seq, "ackSeq for the bad request")
            time.sleep(0.3)
            assert u32(16) == 1 and (u32(20), u32(24)) == (320, 200)
            sec.write(360, struct.pack("<Q", 0xDEAD0000BEEF))
            sec.write(352, struct.pack("<I", 4242))
            wait_for(lambda: "client pid 4242" in log.read_text(errors="replace"), "client pid line")
            sec.write(340, struct.pack("<II", 640, 360))
            sec.write(336, struct.pack("<I", seq + 1))
            wait_for(lambda: u32(16) == 2, "texture generation 2")
            assert (u32(20), u32(24)) == (640, 360)
        finally:
            sec.close()
        text = log.read_text(errors="replace")
        assert "client requested 99999x100" not in text
        assert re.search(r"client pid 4242, client adapter LUID 0000DEAD0000BEEF  \*\* MISMATCH", text), text[-800:]
        assert "client requested 640x360 (seq %d)" % (seq + 1) in text
    finally:
        p.kill()
        p.close()


# ==============================================================================================================
# shell_composite.dll in test_host.exe: the server side fails
# ==============================================================================================================

def test_section_not_initialised(run):
    """A section exists but no server initialised it (magic 0): STALE with that reason, nothing drawn, no texture opened."""
    sec = camblock.Section(run.section)
    try:
        run.set_ini(mode="composite", insert="clear", trace_frames="0")
        run.start_host(*PLAIN, seconds=5)
        run.wait_log(r"server STALE \(section not initialised\)", timeout_s=10)
        run.wait_log(r" stats ", timeout_s=5)
        srv, grey = free_area(run.shot())
        finish(run)
    finally:
        sec.close()
    rows = steady(run)
    assert rows and all(r["stale"] == 1 and r["nodraw"] > 0 and r["srv_new"] == 0 for r in rows), rows
    assert not any(r["disabled"] for r in rows)
    assert not any("opened shared texture" in ln for ln in run.log_lines())
    assert srv < 0.01 and grey > 0.99


def test_server_clean_exit_marks_stale(run):
    """The server ends normally (it clears magic): the DLL calls it STALE (section not initialised) and keeps showing the
    last frame (hide_when_stale=0)."""
    run.set_ini(mode="composite", insert="clear", trace_frames="0")
    run.start_server("--rate", "60", seconds=2.5)
    run.start_host(*PLAIN, seconds=8)
    run.wait_log(r" stats .*srv_new=[1-9]", timeout_s=10)
    assert run.server.wait(15) == 0
    run.wait_log(r"server STALE \(section not initialised\)", timeout_s=5)
    time.sleep(0.5)
    srv, grey = free_area(run.shot())
    finish(run)
    assert srv > 0.9 and grey < 0.01, (srv, grey)
    assert not any(r["disabled"] for r in shellrun.stats(run.log_lines()))


def test_open_shared_texture_failure(run):
    """The section names a texture that does not exist: OpenSharedResourceByName fails, logged once (repeats are counted,
    not logged), retried every 250 ms, nothing drawn, nothing disabled."""
    sec = camblock.Section(run.section)
    try:
        tex = "Local\\VPShellNoSuchTex_%d" % os.getpid()
        name = tex.encode("utf-16-le") + b"\0\0"
        hdr = struct.pack("<IIIIIIIIQ", 0x4C485356, 2, 2, 4242, 1, 1280, 720, 87, 0) + name
        sec.write(0, hdr)
        sec.write(320, struct.pack("<q", 10_000_000))
        run.set_ini(mode="composite", insert="clear", trace_frames="0")
        run.start_host(*PLAIN, seconds=5)
        t_end = time.time() + 6
        while run.host.alive() and time.time() < t_end:   # keep the heartbeat fresh, as a live server would
            sec.write(312, struct.pack("<q", qpc()))
            time.sleep(0.05)
        finish(run)
    finally:
        sec.close()
    lines = run.log_lines()
    fails = [ln for ln in lines if "OpenSharedResourceByName('%s') failed 0x" % tex in ln]
    assert len(fails) == 1, fails
    rows = steady(run)
    assert rows and sum(r["nodraw"] for r in rows) > 0 and sum(r["srv_new"] for r in rows) == 0
    assert not any(r["disabled"] for r in rows)
    assert not any("opened shared texture" in ln for ln in lines)


def test_acquire_timeouts_counted(run):
    """timeout_ms > 0 with a slow server: AcquireSync(1, 2 ms) times out in most frames; counted as timeouts, the
    previous copy is drawn, nothing disabled."""
    run.set_ini(mode="composite", insert="clear", trace_frames="0", timeout_ms="2")
    run.start_server("--rate", "5", seconds=20)
    run.start_host(*PLAIN, seconds=6)
    run.wait_log(r" stats .*srv_new=[1-9]", timeout_s=10)
    time.sleep(0.5)
    srv, _ = free_area(run.shot())
    finish(run)
    rows = steady(run)
    assert sum(r["timeouts"] for r in rows) > 50, [r["timeouts"] for r in rows]
    assert sum(r["srv_new"] for r in rows) > 0
    assert not any(r["disabled"] for r in rows)
    assert srv > 0.9


def test_abandoned_keyed_mutex(run):
    """The server dies holding the keyed mutex with a new frame announced (--test-abandon): the DLL's AcquireSync
    returns WAIT_ABANDONED, the shared texture is closed, the host runs on, nothing is disabled."""
    run.set_ini(mode="composite", insert="clear", trace_frames="0", stale_ms="500")
    run.start_host(*PLAIN, seconds=8)
    time.sleep(1.0)
    # --size = the host's back buffer: no resize request, so the mutex the server dies holding is the one the DLL has
    # open (2026-11-11 under ASan: a resize to generation 2 coincided with the abandon and the DLL never opened it)
    run.start_server("--rate", "60", "--size", "1280x720", "--test-abandon", "150", seconds=20)
    run.wait_log(r"opened shared texture .*gen 1\)", timeout_s=5)
    assert run.server.wait(15) == 3
    line = run.wait_log(r"AcquireSync\(1\) returned 0x", timeout_s=5)
    run.wait_log(r"server STALE", timeout_s=5)
    finish(run)
    assert "returned 0x00000080 (abandoned: server died holding it) - closing the shared texture" in line, line
    assert any("test-abandon: terminating" in ln for ln in run.log_lines("server"))
    assert not any(r["disabled"] for r in shellrun.stats(run.log_lines()))


# ==============================================================================================================
# shell_composite.dll: its own failures and refusals
# ==============================================================================================================

@pytest.mark.parametrize("where, said", [
    ("composite", "inside composite - composite disabled, hooks pass through"),
    ("present", "in Present hook - composite disabled"),
    ("exec", "in ExecuteCommandList hook (after) - composite disabled"),
])
def test_exception_in_hook_disables_composite(run, where, said):
    """One access violation inside a hook body (fault_test): caught by the SEH guard, DISABLED from then on, nothing
    drawn, the host's own frame intact and the host exits normally."""
    run.set_ini(mode="composite", insert="tagged", tag_rule="after", trace_frames="0", fault_test=where)
    run.start_server("--rate", "60", seconds=25)
    run.start_host(*GAME, seconds=6)
    line = run.wait_log(r"EXCEPTION 0x", timeout_s=15)
    run.wait_log(r" stats .*DISABLED", timeout_s=5)
    time.sleep(0.6)
    srv, grey = free_area(run.shot())
    finish(run)
    assert "EXCEPTION 0xC0000005 " + said in line, line
    after = steady(run, after=r"EXCEPTION 0x")
    assert after and all(r["disabled"] and r["comp_n"] == 0 for r in after), after
    assert srv < 0.01 and grey > 0.99, (srv, grey)
    assert len([ln for ln in run.log_lines() if "EXCEPTION 0x" in ln]) == 1


def test_hook_guard_refuses_foreign_targets(run):
    """hook_test=foreign: the host EXE's entry point, the DLL's own DllMain, a kernel32 function and a heap block are
    offered to HookTarget; each is refused (decision D4: only d3d11.dll / dxgi.dll), the number of hook targets does not
    change, and the first bytes at each address, read back from the host, are what they were."""
    run.set_ini(mode="composite", insert="clear", trace_frames="0", hook_test="foreign")
    run.start_server("--rate", "60", seconds=20)
    run.start_host(*PLAIN, seconds=6)
    done = run.wait_log(r"hook_test: done", timeout_s=15)
    lines = run.log_lines()
    tries = [re.search(r"hook_test: (\S+) ([0-9A-F]{8}) bytes ((?:[0-9A-F]{2} ){4}[0-9A-F]{2})", ln) for ln in lines]
    tries = [m for m in tries if m]
    assert [m.group(1) for m in tries] == ["exe-entry", "own-dllmain", "kernel32", "heap"]
    for m in tries:
        assert run.host.read_memory(int(m.group(2), 16), 5) == bytes.fromhex(m.group(3)), m.group(0)
    verdicts = [ln.split("hook_test: ")[1] for ln in lines if re.search(r"hook_test: \S+ -> ", ln)]
    assert verdicts == ["exe-entry -> refused", "own-dllmain -> refused", "kernel32 -> refused", "heap -> refused"], verdicts
    n = re.search(r"hook targets (\d+) -> (\d+)", done)
    assert n.group(1) == n.group(2)
    text = "\n".join(lines)
    assert len(re.findall(r"not in d3d11.dll/dxgi.dll - NOT hooked", text)) == 3
    assert re.search(r"is in test_host.exe, not in d3d11", text) and re.search(r"is in shell_composite.dll, not in d3d11", text)
    assert re.search(r"is in kernel(32|base)\.dll, not in d3d11", text, re.I) and "is in no module - not hooked" in text
    finish(run)


def test_bad_ini_values_fall_back(run):
    """Values the DLL cannot use fall back to their defaults or limits (mode, insert, nth, tag_rule, stats_ms) and the
    composite works with them; a bad tag_match is caught by test_tagged.py::test_wrong_rule_is_caught."""
    run.set_ini(mode="sideways", insert="99", nth="-4", tag_rule="7", stats_ms="5", timeout_ms="abc", trace_frames="0")
    run.start_server("--rate", "60", seconds=20)
    # ticket 0490: insert falls back to tagged (the shipped ini's), which needs the game-like host's command lists
    run.start_host(*GAME, seconds=5)
    cfg = run.wait_log(r"\bconfig: mode=", timeout_s=10)
    run.wait_log(r" stats .*srv_new=[1-9]", timeout_s=10)
    srv, _ = free_area(run.shot())
    finish(run)
    # ticket 0490: the default insertion is the shipped ini's (tagged; was clear)
    assert "mode=composite insert=tagged nth=1 rule=after" in cfg and "timeout=0 " in cfg, cfg
    els = [float(re.search(r"el=(\d+)ms", ln).group(1)) for ln in run.log_lines() if " stats " in ln]
    assert len(els) > 10 and min(els) >= 100, els[:5]
    assert srv > 0.9


def test_shot_of_unsupported_format(run):
    """A back buffer in R10G10B10A2: the .shot trigger is refused with the format logged, no file, the host runs on."""
    run.set_ini(mode="composite", insert="clear", trace_frames="0")
    run.start_server("--rate", "60", seconds=20)
    run.start_host(*PLAIN, "--format", "rgb10a2", seconds=5)
    run.wait_log(r" stats .*srv_new=[1-9]", timeout_s=10)
    run.trigger("shell_composite.shot")
    run.wait_log(r"shot: unsupported back-buffer format 24", timeout_s=5)
    finish(run)
    assert not list(run.dir.glob("backbuffer_*.bmp"))
    assert not any(r["disabled"] for r in shellrun.stats(run.log_lines()))


def test_resize_releases_and_reopens(run):
    """ResizeBuffers on the target (--churn): every D3D object of the DLL is released before the original call, the
    server is asked for the new size, a new texture generation is opened, and the composite keeps drawing."""
    run.set_ini(mode="composite", insert="clear", trace_frames="0")
    run.start_server("--rate", "60", seconds=25)
    run.start_host(*PLAIN, "--churn", "1.5", seconds=7)
    run.wait_log(r"ResizeBuffers\(1920 x 1080\) on target: releasing everything first", timeout_s=10)
    run.wait_log(r"opened shared texture .* 1920x1080 .*gen [2-9]", timeout_s=5)
    finish(run)
    text = "\n".join(run.log_lines())
    assert "requested server texture 1920x1080" in text and "requested server texture 2560x1440" in text
    assert any("client requested 1920x1080" in ln for ln in run.log_lines("server"))
    rows = steady(run)
    assert sum(r["missed_ins"] for r in rows) == 0 and not any(r["disabled"] for r in rows)
    assert sum(r["srv_new"] for r in rows[-3:]) > 0, rows[-3:]


@pytest.mark.parametrize("kind, status, grid_state", [
    ("null-app", "NO_GVS", "HIDDEN"),
    ("bad-vptr", "BAD_GVS", "HIDDEN"),
    ("views0", "INCONSISTENT", "HIDDEN"),
    ("views9", "INCONSISTENT", "HIDDEN"),
    ("wrap", "INCONSISTENT", "HIDDEN"),
    ("grid-not-ready", "OK", "NO_GRID"),
])
def test_camera_bad_state(run, kind, status, grid_state):
    """One broken piece of the EXE's camera state: the DLL publishes the matching status (never OK for a broken camera),
    the server draws no grid, the composite itself is unaffected."""
    run.set_ini(mode="composite", insert="clear", trace_frames="0", stats_ms="500")
    run.start_server("--scene", "grid", "--rate", "60", seconds=25)
    run.start_host(*PLAIN, "--fake-exe", "--fake-cam-bad", kind, seconds=6)
    run.wait_log(r"camera: status - -> %s\b" % status, timeout_s=10)
    run.wait_log(r"grid: state \S+ -> %s\b" % grid_state, which="server", timeout_s=10)
    run.wait_log(r" stats .*cam=%s\b" % status, timeout_s=5)
    bmp = run.shot()
    finish(run)
    assert grid_colours(bmp) == 0
    rows = steady(run, after=r"camera: status - -> ")
    assert rows and sum(r["comp_n"] for r in rows) > 0 and not any(r["disabled"] for r in rows)
    if status != "OK":
        assert all(r["cam_ok"] == 0 for r in rows), rows
        assert not any("first OK read" in ln for ln in run.log_lines())
    assert not any("-> OK" in ln for ln in run.log_lines("server") if "grid: state" in ln)


def test_camera_read_exception(run):
    """g_Civ5App points at unmapped memory: every read faults; caught, status EXCEPTION, one retry per second, after 5
    exceptions the camera read is off for good; the composite and the host carry on."""
    run.set_ini(mode="composite", insert="clear", trace_frames="0", stats_ms="500")
    run.start_server("--scene", "grid", "--rate", "60", seconds=30)
    run.start_host(*PLAIN, "--fake-exe", "--fake-cam-bad", "unmapped-app", seconds=9)
    run.wait_log(r"camera: EXCEPTION 0xC0000005 while reading the EXE's camera \(5 so far\) - camera read disabled for good", timeout_s=12)
    finish(run)
    exc = [ln for ln in run.log_lines() if "camera: EXCEPTION" in ln]
    assert len(exc) == 5, exc
    assert all("no read for 1 s" in ln for ln in exc[:4])
    assert any("camera: status - -> EXCEPTION" in ln for ln in run.log_lines())
    assert any(re.search(r"grid: state \S+ -> HIDDEN", ln) for ln in run.log_lines("server"))
    rows = steady(run)
    assert sum(r["comp_n"] for r in rows) > 0 and not any(r["disabled"] for r in rows)
    assert all(r["cam_ok"] == 0 for r in rows)


@pytest.mark.parametrize("bad, said", [
    ("mz", "no MZ header at 0x"),
    ("pe", "no PE header (e_lfanew 0x2000)"),
    ("base0", "no module base"),
    ("unmapped", "exception 0xC0000005 while reading the image"),
])
def test_refused_image(run, bad, said):
    """Images the camera reader must refuse before any read: no MZ, e_lfanew past 4 KB, base 0, a base in unmapped
    memory (the validation's own fault is caught). REFUSED is published, the composite runs."""
    run.set_ini(mode="composite", insert="clear", trace_frames="0", stats_ms="500")
    run.start_server("--rate", "60", seconds=20)
    env, host = {}, PLAIN
    if bad in ("mz", "pe"):
        host = PLAIN + ("--fake-exe", "--fake-exe-bad", bad)
    else:
        env = {"VPSHELL_EXE_BASE": "0" if bad == "base0" else "00001000"}
    run.start_host(*host, seconds=5, env=env)
    line = run.wait_log(r"camera: REFUSED", timeout_s=10)
    run.wait_log(r" stats .*cam=REFUSED", timeout_s=8)
    finish(run)
    assert said in line, line
    rows = steady(run)
    assert rows and all(r["cam_ok"] == 0 for r in rows) and sum(r["comp_n"] for r in rows) > 0
    assert not any(r["disabled"] for r in rows)


def test_host_refuses_bad_fake_options(tmp_path, hidden_desktop, built):
    """The stand-in's own test switches refuse an unknown value (exit 2) instead of running a different test."""
    for opt in (["--fake-exe-bad", "nope"], ["--fake-cam-bad", "nope"], ["--fake-exe-build", "12345"], ["--no-such-option"]):
        out = tmp_path / "h.out"
        p = shellrun.launch([str(built / "test_host.exe"), "--load", "none", "--seconds", "2", "--log", str(tmp_path / "h.log"), *opt],
                            hidden_desktop, out_path=out)
        assert p.wait(20) == 2, opt
        p.close()
        assert not (tmp_path / "h.log").exists()


# ==============================================================================================================
# Python tools
# ==============================================================================================================

@pytest.mark.parametrize("script, args", [
    ("run_pair.ps1", ["-Name", "x"]), ("ab.ps1", []), ("soak.ps1", []),
])
def test_ps1_tools_refuse_without_permission(tmp_path, script, args):
    """The prototype's scripts that start test_host.exe on the interactive desktop refuse without the MC's permission
    (rule 14). Rule 14b: C5_SHELL_BUILD points at an empty folder, so even a missing gate could start nothing."""
    env = {k: v for k, v in os.environ.items() if k.upper() != "C5_HARNESS_INTERACTIVE_DESKTOP"}
    env["C5_SHELL_BUILD"] = str(tmp_path)
    r = subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File",
                        str(shellrun.SHELL / "tools" / script), *args], env=env, capture_output=True, text=True, timeout=60)
    assert r.returncode != 0
    assert "refusing to start test_host.exe on the interactive desktop" in (r.stdout + r.stderr), r.stdout + r.stderr
    assert list(tmp_path.iterdir()) == []


def test_gt_reader_refusals(tmp_path, hidden_desktop, built, monkeypatch):
    """shell/tools/gt_reader.py refuses: a pid it cannot open; a process without the EXE's module; a module of that name
    whose TimeDateStamp is not the Steam EXE's (the stand-in, renamed for the test only through EXE_NAME)."""
    sys.path.insert(0, str(shellrun.SHELL / "tools"))
    import gt_reader as gt
    with pytest.raises(gt.Refused, match=r"OpenProcess\(\d+\) failed"):
        gt.Reader(0x7FFFFFF0)
    with pytest.raises(gt.Refused, match=r"has no module civilizationv_dx11.exe"):
        gt.Reader(os.getpid())
    p = shellrun.launch([str(built / "test_host.exe"), "--load", "none", "--seconds", "8", "--log", str(tmp_path / "h.log"),
                         "--vsync", "0", "--occluded-sleep", "0"], hidden_desktop)
    try:
        time.sleep(1.5)
        monkeypatch.setattr(gt, "EXE_NAME", "test_host.exe")
        # ticket 0593: the known builds come from tools/exebuild (stamp and SizeOfImage)
        with pytest.raises(gt.Refused, match=r"refusing: the EXE \(TimeDateStamp 0x[0-9A-F]{8}, SizeOfImage 0x[0-9A-F]+\)"
                                             r" is not a known build"):
            gt.Reader(p.pid)
    finally:
        p.kill()
        p.close()


def test_memcap_refuses_bad_arguments():
    tool = str(shellrun.SHELL / "tools" / "memcap.py")
    for args in (["--mb", "100"], ["--mb", "0", "--", sys.executable, "-c", "pass"]):
        r = subprocess.run([sys.executable, tool, *args], capture_output=True, text=True, timeout=60)
        assert r.returncode == 2 and "memcap: need --mb > 0 and a command" in r.stderr, (args, r.stderr)
    r = subprocess.run([sys.executable, tool, "--mb", "256", "--", sys.executable, "-c", "import sys; sys.exit(7)"],
                       capture_output=True, text=True, timeout=60)
    assert r.returncode == 7 and "peak committed memory" in r.stdout, r.stdout


def test_addrspace_unknown_process():
    r = subprocess.run([sys.executable, str(shellrun.SHELL / "tools" / "addrspace.py"), "c5_no_such_process_3013"],
                       capture_output=True, text=True, timeout=60)
    assert r.returncode != 0 and "no process matching 'c5_no_such_process_3013'" in r.stderr, r.stderr
