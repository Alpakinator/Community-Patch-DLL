"""Issue 3007: the EXE camera read in shell_composite.dll and the hex grid of shell_server.exe --scene grid, on the
stand-in (hidden desktop, rule 14).

test_host --fake-exe lays out a fake CivilizationV_DX11.exe image (the byte patterns of shell/common/exe_camera.h, the
camera globals, a GameViewState, the Tiny ground-truth map's grid constants) and hands its base to the DLL in
VPSHELL_EXE_BASE. The DLL validates it, reads the camera at every Present and publishes it in the section's camera
block; the server draws the grid with it (view 0 yellow, wrap view 1 cyan, STALE red). Checked from the logs and from
back-buffer dumps: the read path (OK, first read, grid constants), map wrap, the kill switch (live), the stale detector
(a publisher that stops while Presents continue), the server's own consistency check (a corrupted view), the DLL's
checks (view globals frozen while the camera moves; view mode 2 = Strategic View; a writer on another thread), and the
refusal of an EXE whose TimeDateStamp or patterns differ (also the real test_host image without --fake-exe). Plus two
static checks: the patterns against CivilizationV_DX11.exe on disk (read only), and the Python mirror of the camera
block against the server's own offsets.
"""
import json
import re
import struct
import time
from pathlib import Path

import numpy as np
import pytest

import camblock
import exe_golden
import shellrun

FREE = (400, 60, 1000, 170)       # test_host's map area free of UI (1280x720)
HOST = ("--vsync", "0", "--occluded-sleep", "0", "--load", "after", "--fake-exe")


def colours(bmp, box=None):
    """Counts of yellow (view 0), cyan (view 1) and red (STALE) line pixels in the dump (optionally inside box)."""
    img = shellrun.Image(bmp)
    a = np.frombuffer(img.px, dtype=np.uint8).reshape(img.h, img.w, 4)
    if not img.top_down:
        a = a[::-1]
    if box:
        x0, y0, x1, y1 = box
        a = a[y0:y1, x0:x1]
    b, g, r = a[..., 0].astype(int), a[..., 1].astype(int), a[..., 2].astype(int)
    return {"yellow": int(((r > 200) & (g > 200) & (b < 80)).sum()),
            "cyan": int(((r < 80) & (g > 200) & (b > 200)).sum()),
            "red": int(((r > 200) & (g < 80) & (b < 80)).sum())}


def start(run, ini=None, host=(), server=("--rate", "60"), seconds=9):
    cfg = dict(mode="composite", insert="clear", trace_frames="0", stats_ms="500")
    cfg.update(ini or {})
    run.set_ini(**cfg)
    run.start_server("--scene", "grid", *server, seconds=seconds + 20)
    run.start_host(*HOST, *host, seconds=seconds)


def wait_state(run, state, timeout_s=10.0, start=0):
    return run.wait_log(r"grid: state \S+ -> %s\b" % state, which="server", timeout_s=timeout_s, start=start)


def finish(run, seconds=30):
    assert run.host.wait(seconds) == 0, shellrun.tail(run.host_log, 10)


# --------------------------------------------------------------------------------------------------------------
# static checks

def test_signatures_match_exe_on_disk():
    """Ticket 4964: every pattern of exe_camera.h, expanded with each build's values from the local build table, equals
    the bytes of that build's golden EXE file (read only, never the owner's Steam install) at the symbol's VA, for both known
    builds; each fully compared pattern of 16 bytes or more occurs exactly once in the image (exe_golden.py)."""
    assert len(exe_golden.rows("shell/common/exe_camera.h")) == 11
    bad = exe_golden.check_patterns("shell/common/exe_camera.h")
    assert bad == {b: [] for b in exe_golden.BUILDS}, bad


def test_layout_matches_server(tmp_path, hidden_desktop, built):
    out = tmp_path / "layout.json"
    p = shellrun.launch([str(built / "shell_server.exe"), "--print-layout", str(out)], hidden_desktop, console=True)
    assert p.wait(20) == 0
    p.close()
    lay = json.loads(out.read_text())
    mine = camblock.offsets()
    for name, _fmt in camblock.FIELDS:
        assert lay[name] == mine[name], (name, lay[name], mine[name])
    assert lay["offset"] == camblock.CAMERA_OFFSET and lay["section_bytes"] == camblock.SECTION_BYTES
    # version 3 since epic 3200 (tap, bake-tap and depth blocks; their offsets: test_terrain_scene.test_layout_v3)
    assert lay["sizeof"] >= mine["_end"] and lay["version"] == 3


# --------------------------------------------------------------------------------------------------------------
# the read path on the fake EXE

@pytest.mark.parametrize("build", exe_golden.BUILDS)
def test_grid_drawn_with_fake_camera(run, build):
    """Ticket 4964: the same read path on a fake image of each known build (its stamp, SizeOfImage and addresses)."""
    start(run, host=("--fake-exe-build", build))
    line = run.wait_log(r"camera: EXE image at 0x[0-9A-F]+ \(VPSHELL_EXE_BASE.*byte patterns match - camera read enabled", timeout_s=10)
    assert ("Steam build %s " % build) in line and "11 byte patterns match" in line, line
    run.wait_log(r"camera: status - -> OK", timeout_s=10)
    first = run.wait_log(r"camera: first OK read on thread", timeout_s=5)
    assert "fov 29.01" in first and "near 50.0 far 4000.0" in first
    grid = run.wait_log(r"camera: grid constants read \(grid seq 1\): valid 1", timeout_s=5)
    assert "samples 3584x2560 (map 56x36 plots) wrap 1 period 3583.0" in grid
    wait_state(run, "OK")
    built_line = run.wait_log(r"grid: built for grid seq 1: map 56 x 36 plots", which="server", timeout_s=5)
    assert "flat z" in built_line
    run.wait_log(r" stats .*cam=OK", timeout_s=5)
    time.sleep(1.0)
    c = colours(run.shot(), FREE)
    finish(run)
    assert c["yellow"] > 150 and c["cyan"] == 0 and c["red"] == 0, c
    # Every frame is OK from the interval after the first OK read on. Not from the host's start: the DLL maps the section
    # at most once per second, so a server that creates it a little after the host's first Present delays the first
    # publish by up to 1 s (seen 2026-11-11 in the ASan run of issue 3013: section created 75 ms after the first try).
    lines = run.log_lines()
    first_ok = next(i for i, ln in enumerate(lines) if "camera: first OK read" in ln)
    rows = shellrun.stats(lines[first_ok + 1:])[1:]
    assert rows and all(r["cam_ok"] == r["frames"] for r in rows), [(r["cam_ok"], r["frames"]) for r in rows]
    assert sum(r["cam_bad"] for r in rows) == 0
    assert not any(r["disabled"] for r in rows)
    srv = [ln for ln in run.log_lines("server") if "grid 1s:" in ln]
    assert any("state=OK" in ln and int(re.search(r"drawn=(\d+)", ln).group(1)) > 30 for ln in srv), srv[-3:]
    assert all("srv_incons=0" in ln for ln in srv)


@pytest.mark.parametrize("views", [1, 2])
def test_map_wrap_views(run, views):
    """Camera 50 units west of the east seam (x 1791.5): with g_NumViews 2 the wrap copy (Translation(+period) * view 0,
    cyan) fills the screen beyond the seam; with 1 nothing is drawn there."""
    start(run, host=("--fake-cam-x", "1740", "--fake-cam-views", str(views)))
    wait_state(run, "OK")
    run.wait_log(r" stats .*cam=OK", timeout_s=5)
    time.sleep(1.0)
    c = colours(run.shot(), FREE)
    finish(run)
    srv = [ln for ln in run.log_lines("server") if "grid 1s:" in ln and "state=OK" in ln]
    assert srv and all(("views=%d" % views) in ln for ln in srv[1:]), srv[-2:]
    assert c["yellow"] > 50, c
    if views == 2:
        assert c["cyan"] > 50, c
    else:
        assert c["cyan"] == 0, c


def test_kill_switch_live(run):
    """camera=0 while running: the DLL publishes status OFF and reads nothing, the server hides the grid; camera=1
    brings it back."""
    start(run, seconds=16)
    wait_state(run, "OK")
    run.wait_log(r" stats .*cam=OK", timeout_s=5)
    n0, s0 = len(run.log_lines()), len(run.log_lines("server"))
    run.set_ini(camera="0")
    run.wait_log(r"camera: status OK -> OFF", timeout_s=5, start=n0)
    wait_state(run, "HIDDEN", start=s0)
    time.sleep(1.2)
    off = colours(run.shot(), FREE)
    n1, s1 = len(run.log_lines()), len(run.log_lines("server"))
    rows = shellrun.stats([ln for ln in run.log_lines()[n0:n1] if " stats " in ln and "cam=OFF" in ln])[1:]
    run.set_ini(camera="1")
    run.wait_log(r"camera: status OFF -> OK", timeout_s=5, start=n1)
    wait_state(run, "OK", start=s1)
    time.sleep(1.0)
    on = colours(run.shot(), FREE)
    finish(run)
    assert off == {"yellow": 0, "cyan": 0, "red": 0}, off
    assert on["yellow"] > 150, on
    assert rows and all(r["cam_ok"] == 0 and r["cam_bad"] == r["frames"] for r in rows), rows
    assert any("status=OFF" in ln for ln in run.log_lines("server"))


@pytest.mark.parametrize("stale_mode", ["tint", "hide"])
def test_stale_publisher_detected(run, stale_mode):
    """camera_test=stall: the DLL keeps counting Presents but publishes nothing (a stuck or dead publisher). The server
    sees presentCount run ahead of the last block by more than --stale-frames, logs STALE and draws the last good
    camera in red (tint) or nothing (hide)."""
    start(run, server=("--rate", "60", "--grid-stale", stale_mode), seconds=12)
    wait_state(run, "OK")
    s0 = len(run.log_lines("server"))
    run.set_ini(camera_test="stall")
    line = wait_state(run, "STALE", start=s0)
    lag = int(re.search(r"present (\d+), block frame (\d+)", line).group(1)) - int(re.search(r"block frame (\d+)", line).group(1))
    time.sleep(1.0)
    c = colours(run.shot(), FREE)
    finish(run)
    assert lag > 15, line
    assert c["yellow"] == 0 and c["cyan"] == 0, c
    if stale_mode == "tint":
        assert c["red"] > 150, c
    else:
        assert c["red"] == 0, c


def test_stale_is_not_raised_when_host_idle(run):
    """Negative control of the stale detector: no Present, no publish (the host stops presenting = minimised or
    loading): presentCount does not advance, so the server does not call the camera STALE."""
    start(run, seconds=5)
    wait_state(run, "OK")
    assert run.host.wait(30) == 0
    time.sleep(2.0)
    run.server.kill()
    srv = run.log_lines("server")
    assert not any("-> STALE" in ln for ln in srv), [ln for ln in srv if "grid: state" in ln]


def test_corrupt_view_caught_by_server(run):
    """camera_test=corrupt: view 0 shifted by 40 units while the block still says OK. The server's own check
    (camWorld * view0 = I) refuses it and hides the grid."""
    start(run, ini=dict(camera_test="corrupt"))
    line = run.wait_log(r"grid: camera block says OK but fails the server's own check", which="server", timeout_s=10)
    assert re.search(r"trans 4\d\.\d+", line), line
    wait_state(run, "SRV_INCONSISTENT")
    time.sleep(1.0)
    c = colours(run.shot(), FREE)
    finish(run)
    assert c == {"yellow": 0, "cyan": 0, "red": 0}, c
    srv = [ln for ln in run.log_lines("server") if "grid 1s:" in ln]
    assert any(int(re.search(r"srv_incons=(\d+)", ln).group(1)) > 0 for ln in srv)


def test_frozen_view_globals_inconsistent(run):
    """The view globals stop updating while camWorld keeps moving (what another scene's camera or a stale global looks
    like): the DLL reports INCONSISTENT, the server tolerates it for --bad-frames frames, then hides the grid."""
    start(run, host=("--fake-cam-pan", "60", "--fake-cam-freeze-at", "4"), seconds=10)
    wait_state(run, "OK")
    run.wait_log(r"camera: status OK -> INCONSISTENT", timeout_s=10)
    wait_state(run, "HIDDEN")
    time.sleep(1.0)
    c = colours(run.shot(), FREE)
    finish(run)
    assert c == {"yellow": 0, "cyan": 0, "red": 0}, c
    rows = shellrun.stats(run.log_lines())
    assert any(r["cam_bad"] > 0 and r["cam_ok"] == 0 for r in rows[-3:]), rows[-3:]


def test_strategic_view_mode_not_map(run):
    """GVS+0x758 becomes 2 (Strategic View): status NOT_MAP, grid hidden (in Strategic View g_View/g_Proj keep the last
    3D values and g_NumViews counts strategic views, research section 10)."""
    start(run, host=("--fake-cam-mode-at", "4:2"), seconds=9)
    wait_state(run, "OK")
    run.wait_log(r"camera: status OK -> NOT_MAP \(present \d+, view mode 2", timeout_s=10)
    wait_state(run, "HIDDEN")
    time.sleep(1.0)
    c = colours(run.shot(), FREE)
    finish(run)
    assert c == {"yellow": 0, "cyan": 0, "red": 0}, c


def test_writer_on_another_thread(run):
    """The camera written nonstop from another thread while the DLL reads at Present: no block the DLL calls OK fails the
    server's independent check; torn reads are retried or reported (TORN / INCONSISTENT), never published as OK."""
    start(run, host=("--fake-cam-writer", "thread", "--fake-cam-pan", "300"), seconds=9)
    wait_state(run, "OK")
    run.wait_log(r" stats .*cam_ok=[1-9]", timeout_s=8)
    time.sleep(2.0)
    finish(run)
    rows = shellrun.stats(run.log_lines())[1:]
    srv = [ln for ln in run.log_lines("server") if "grid 1s:" in ln]
    ok = sum(r["cam_ok"] for r in rows)
    assert ok > 100, rows
    assert all("srv_incons=0" in ln for ln in srv), [ln for ln in srv if "srv_incons=0" not in ln]
    retry = sum(r["cam_retry"] for r in rows)
    bad = sum(r["cam_bad"] for r in rows)
    print("writer thread: ok %d bad %d retries %d" % (ok, bad, retry))


@pytest.mark.parametrize("bad,build", [("tds", "4390913"), ("tds", "26577625"), ("sig", "4390913"), ("sig", "26577625"),
                                       ("soi", "26577625"), ("real-host", None)])
def test_refused_exe_never_read(run, bad, build):
    """A TimeDateStamp of no known build, a wrong pattern byte, a known stamp with another SizeOfImage (ticket 4964: the
    stamp alone is not a build), or the real test_host image (no --fake-exe): REFUSED and logged, status REFUSED published
    every frame, grid hidden, the composite itself unaffected."""
    run.set_ini(mode="composite", insert="clear", trace_frames="0", stats_ms="500")
    run.start_server("--scene", "grid", "--rate", "60", seconds=25)
    host = ("--vsync", "0", "--occluded-sleep", "0", "--load", "after")
    if bad != "real-host":
        host += ("--fake-exe", "--fake-exe-build", build, "--fake-exe-bad", bad)
    run.start_host(*host, seconds=6)
    line = run.wait_log(r"camera: REFUSED", timeout_s=10)
    stamp = {"4390913": 0x546CD0A8, "26577625": 0x6AF3837D}.get(build, 0)
    if bad == "tds":
        assert "TimeDateStamp 0x%08X SizeOfImage" % (stamp + 1) in line and "is no EXE build this reader knows" in line, line
    elif bad == "soi":
        assert "TimeDateStamp 0x%08X SizeOfImage" % stamp in line and "is no EXE build this reader knows" in line, line
    elif bad == "sig":
        assert re.search(r"pattern 'UpdateViews: GetViewProj\(&g_View\[0\], &g_Proj\)' at cam_updateviews_getviewproj "
                         r"\(VA 0x[0-9A-F]{8}\): mismatch at byte 1", line), line
    else:
        assert "is no EXE build this reader knows" in line, line
    run.wait_log(r"camera: status - -> REFUSED", timeout_s=5)
    wait_state(run, "HIDDEN")
    run.wait_log(r" stats .*cam=REFUSED", timeout_s=5)
    c = colours(run.shot(), FREE)
    finish(run)
    assert c == {"yellow": 0, "cyan": 0, "red": 0}, c
    rows = shellrun.stats(run.log_lines())[1:]
    assert rows and all(r["cam_ok"] == 0 for r in rows)
    assert sum(r["comp_n"] for r in rows) > 0 and not any(r["disabled"] for r in rows)
    assert not any("first OK read" in ln for ln in run.log_lines())
