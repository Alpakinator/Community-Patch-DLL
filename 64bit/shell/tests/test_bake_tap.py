"""Issue 3204 (epic 3200): the tile-cache tap of shell_composite.dll (composite/bake_tap.cpp) and its reader
(tools/bake_reader.py), on the stand-in (hidden desktop, rule 14).

test_host has no EXE, so the DLL's test seam `bake_test=<mode>` builds a synthetic TerrainSystem / tile grid / view table
in the DLL's own memory, with real BC1 test textures of known colours (bake_tap.cpp, BuildTestWorld: 4 x 2 tiles, tiles
0-5 with both caches, tile 6 with t0 only, tile 7 without caches). The same scan, resolve, copy and publish code then runs
as in the game. Checked: the manifest (header, slots, loadSeq, states), the shared textures opened by name from this
(x64) process and their decoded colours, a new load (stale loadSeq), every hostile input (missing cache texture, a handle
past the view table, a view pointer whose vtable is not D3D11's, null and unmapped tile pointers, absurd counts and tile
fields, no terrain renderer), a manifest section that already exists too small, the real-EXE path refusing an image
without the tap's byte patterns, the kill switch, and every refusal of the reader (exit codes). Plus a static check of
the tap's byte patterns against the EXE on disk.
"""
import ctypes
import ctypes.wintypes as wt
import json
import os
import re
import struct
import subprocess
import sys
import time
from pathlib import Path

import pytest

import exe_golden
import shellrun

READER = shellrun.SHELL / "tools" / "bake_reader.py"
HOST = ("--vsync", "0", "--occluded-sleep", "0", "--load", "after")

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.CreateFileMappingW.restype = wt.HANDLE
k32.CreateFileMappingW.argtypes = [wt.HANDLE, ctypes.c_void_p, wt.DWORD, wt.DWORD, wt.DWORD, wt.LPCWSTR]
k32.MapViewOfFile.restype = ctypes.c_void_p
k32.MapViewOfFile.argtypes = [wt.HANDLE, wt.DWORD, wt.DWORD, wt.DWORD, ctypes.c_size_t]
k32.UnmapViewOfFile.argtypes = [ctypes.c_void_p]
k32.CloseHandle.argtypes = [wt.HANDLE]
INVALID = wt.HANDLE(-1)


def expected_rgb(i):
    """The one colour of synthetic texture i (bake_tap.cpp BuildTestWorld: RGB565 r = 2i, g = 4i + 8, b = 31 - i)."""
    r, g, b = (2 * i) & 31, (4 * i + 8) & 63, (31 - i) & 31
    return [r * 255 // 31, g * 255 // 63, b * 255 // 31]


def reader(*args, timeout=60):
    p = subprocess.run([sys.executable, str(READER), *args], capture_output=True, text=True, timeout=timeout)
    return p.returncode, p.stdout + p.stderr


def start(run, mode="ok", manifest=None, host=(), **ini):
    name = manifest or "Local\\VPShellBakeTest_%d_%s" % (os.getpid(), run.dir.parent.name[-12:])
    cfg = dict(mode="composite", insert="clear", trace_frames="0", stats_ms="500", bake_tap="1", bake_test=mode,
               bake_manifest=name)
    cfg.update(ini)
    run.set_ini(**cfg)
    run.start_host(*HOST, *host, seconds=12)
    return name


def finish(run):
    assert run.host.wait(40) == 0, shellrun.tail(run.host_log, 10)


def bake_lines(run):
    return [ln for ln in run.log_lines() if "bake_tap:" in ln]


# --------------------------------------------------------------------------------------------------------------
# static

def test_bake_signatures_match_exe_on_disk():
    """Ticket 4964: the tap's 7 patterns against both builds' golden EXEs, with each build's table values (exe_golden)."""
    assert len(exe_golden.rows("shell/composite/bake_tap.cpp")) == 7
    bad = exe_golden.check_patterns("shell/composite/bake_tap.cpp")
    assert bad == {b: [] for b in exe_golden.BUILDS}, bad


# --------------------------------------------------------------------------------------------------------------
# the tap on the synthetic grid

def wait_tapped(run, timeout_s=15):
    return run.wait_log(r"bake_tap: load (\d+) tapped: (\d+) slots \((\d+) both, (\d+) t0 only\)", timeout_s=timeout_s)


def test_tap_ok_textures_readable_and_new_load(run, tmp_path):
    name = start(run, "ok")
    run.wait_log(r"bake_tap: TEST MODE bake_test=ok", timeout_s=10)
    ln = wait_tapped(run)
    m = re.search(r"load 1 tapped: (\d+) slots \((\d+) both, (\d+) t0 only\), (\d+) copies, ([\d.]+) ms CPU", ln)
    assert m and m.groups()[:4] == ("7", "6", "1", "13"), ln
    out = tmp_path / "dump"
    js = tmp_path / "r.json"
    rc, txt = reader("--manifest", name, "--expect-load", "1", "--open", "7", "--dump", str(out), "--json", str(js))
    assert rc == 0, txt
    r = json.loads(js.read_text())
    assert r["manifest"]["slotCount"] == 7 and r["manifest"]["tilesX"] == 4 and r["manifest"]["tilesY"] == 2
    assert r["manifest"]["capacity"] == 64 and r["manifest"]["loadSeq"] == 1
    states = sorted((s["tileX"], s["tileY"], s["state"], s["tileIndex"]) for s in r["slots"])
    assert states == [(0, 0, 1, 0), (0, 1, 1, 1), (1, 0, 1, 2), (1, 1, 1, 3), (2, 0, 1, 4), (2, 1, 1, 5), (3, 0, 2, 6)], states
    assert all(s["generation"] == 1 and s["width"] == 64 and s["mips"] == 3 and s["fmt"][0] == 71 for s in r["slots"])
    # world rectangle needs the camera block's grid constants: none on the stand-in without --fake-exe
    assert len(r["opened"]) == 13
    for e in r["opened"]:
        slot, which = map(int, re.search(r"\.(\d+)\.(\d)$", e["name"]).groups())
        tile = next(s for s in r["slots"] if s["slot"] == slot)["tileIndex"]
        want = expected_rgb(2 * tile + which)
        assert all(abs(a - b) <= 1 for a, b in zip(e["mean_rgb"], want)), (e, want)
    # a new load: the slots are reset and re-copied under loadSeq 2; a reader expecting load 1 is refused as stale
    run.trigger("shell_composite.bakeload")
    run.wait_log(r"bake_tap: load 2 tapped: 7 slots", timeout_s=10)
    rc, txt = reader("--manifest", name, "--expect-load", "1")
    assert rc == 5 and "stale" in txt, txt
    rc, txt = reader("--manifest", name, "--expect-load", "2", "--json", str(js))
    assert rc == 0, txt
    assert all(s["generation"] == 1 for s in json.loads(js.read_text())["slots"])
    finish(run)
    assert not any("DISABLED" in ln or "FAILED" in ln for ln in bake_lines(run)), bake_lines(run)


def test_tap_control_block_with_server(run, tmp_path):
    """With a server the DLL also publishes VPShellBakeTap in the main section; the reader follows it."""
    run.start_server("--rate", "60", seconds=40)
    name = start(run, "ok")
    wait_tapped(run)
    js = tmp_path / "c.json"
    for _ in range(50):   # the block is written at every Present once the section is mapped
        rc, txt = reader("--section", run.section, "--json", str(js))
        if rc == 0:
            break
        time.sleep(0.2)
    assert rc == 0, txt
    c = json.loads(js.read_text())["control"]
    assert c["state"] == 1 and c["failReason"] == 0 and c["loadSeq"] == 1 and c["slotCount"] == 7
    assert c["manifestName"] == name and c["tilesX"] == 4 and c["copiesTotal"] == 13 and c["copyMsTotal"] > 0
    finish(run)


@pytest.mark.parametrize("mode,expect_log,slots", [
    ("missing", r"tile 1: cache t8 handle 0x3F has no D3D11 texture", {"both": 5, "t0": 2}),
    ("nullview", r"tile 2: cache t0 handle 0x7FFF has no D3D11 texture", {"both": 5, "t0": 1}),
    ("badvtbl", r"tile 0: cache t0 handle 0x10 has no D3D11 texture", {"both": 5, "t0": 1}),
    ("badtile", r"tile 5 at 0x[0-9A-F]+ skipped: tile size out of range", {"both": 5, "t0": 0}),
])
def test_hostile_tiles_are_skipped(run, tmp_path, mode, expect_log, slots):
    """Bad entries are skipped and logged; every other tile is tapped; the host runs to the end."""
    name = start(run, mode)
    run.wait_log(expect_log, timeout_s=10)
    wait_tapped(run)
    js = tmp_path / "r.json"
    rc, txt = reader("--manifest", name, "--expect-load", "1", "--json", str(js))
    assert rc == 0, txt
    st = [s["state"] for s in json.loads(js.read_text())["slots"]]
    assert st.count(1) == slots["both"] and st.count(2) == slots["t0"], st
    if mode == "badtile":
        run.wait_log(r"tile 6 at 0x[0-9A-F]+ skipped: tile origin out of range", timeout_s=5)
    finish(run)
    assert not any("DISABLED" in ln for ln in bake_lines(run))


def test_unmapped_tile_pointer_disables_tap(run, tmp_path):
    """Grid entry 3 is below 64 KB (out of range, skipped), entry 4 unmapped (access violation): the SEH guard catches
    the fault, the tap disables itself for good and logs it; the host keeps running and exits normally."""
    name = start(run, "garbage")
    run.wait_log(r"tile 3 at 0x00001234 skipped: tile pointer out of range", timeout_s=10)
    run.wait_log(r"bake_tap: DISABLED for the life of the process \(fault reading the tile \(tile 4 at 0xDEAD0000\)\)", timeout_s=10)
    time.sleep(1.0)
    finish(run)
    assert not any("tapped:" in ln for ln in bake_lines(run))
    rc, txt = reader("--manifest", name)
    assert rc == 3, txt   # the host is gone: the section with it


def test_absurd_counts_fail_the_load(run):
    start(run, "hugecount")
    run.wait_log(r"bake_tap: FAILED for this load \(tile grid out of range: 100000 x 2 tiles, count 2147483647", timeout_s=10)
    time.sleep(1.0)
    finish(run)
    assert not any("tapped:" in ln for ln in bake_lines(run))


def test_no_terrain_renderer_yet(run):
    name = start(run, "nulltr")
    run.wait_log(r"bake_tap: load 1: slots reset", timeout_s=10)
    time.sleep(2.0)
    rc, txt = reader("--manifest", name, "--expect-load", "1")
    assert rc == 7 and "no valid slot" in txt, txt
    finish(run)
    assert not any("bake_tap: tile grid" in ln or "DISABLED" in ln for ln in bake_lines(run))


def test_manifest_section_too_small(run):
    name = "Local\\VPShellBakeSmall_%d" % os.getpid()
    h = k32.CreateFileMappingW(INVALID, None, 0x04, 0, 4096, name)
    assert h
    try:
        start(run, "ok", manifest=name)
        run.wait_log(r"bake_tap: section .* already exists with 4096 bytes, the manifest needs 6208 - refused", timeout_s=10)
        run.wait_log(r"bake_tap: DISABLED for the life of the process \(manifest section\)", timeout_s=5)
        rc, txt = reader("--manifest", name)
        assert rc == 4, txt   # our 4 KB of zeros: magic 0
        finish(run)
    finally:
        k32.CloseHandle(h)


# Bug 0120 (2026-11-13): the tap's refusals come from BakeTap_OnPresent, so they need a Present after the hook commit,
# and test_host counts the time spent inside LoadLibrary (the commit) toward --seconds. Under machine load the commit
# took 6.5 s (9038 V1, 2026-11-12: "hooks committed (dummy device): 18 enabled in 6547.2 ms") and a 6-s host quit before
# its next Present. So these hosts live SHORT_HOST_S and the test first waits for the commit line itself, with room for a
# commit far slower than any seen, before it waits for the line under test.
SHORT_HOST_S = 20
HOOK_COMMIT_WAIT_S = 18


def start_short_host(run, *extra):
    run.start_host(*HOST, *extra, seconds=SHORT_HOST_S)
    run.wait_log(r"hooks committed \(", timeout_s=HOOK_COMMIT_WAIT_S)


@pytest.mark.parametrize("build", exe_golden.BUILDS)
def test_real_path_refuses_an_exe_without_the_patterns(run, build):
    """No bake_test: the real path. test_host --fake-exe (of either build, ticket 4964) passes the composite's camera
    validation but has none of the tap's byte patterns, so the tap refuses before any read. Without --fake-exe the
    composite itself refuses the EXE."""
    run.set_ini(mode="composite", insert="clear", trace_frames="0", stats_ms="500", bake_tap="1", bake_test="")
    start_short_host(run, "--fake-exe", "--fake-exe-build", build)
    run.wait_log(r"bake_tap: REFUSED: pattern 'TerrainSystem object \(mov ecx, &TerrainSystem\)' at bake_ts_site "
                 r"\(VA 0x[0-9A-F]{8}\): (mismatch at byte \d+|unreadable)", timeout_s=10)
    run.wait_log(r"bake_tap: DISABLED for the life of the process \(EXE refused\)", timeout_s=5)
    finish(run)


def test_real_path_refuses_unvalidated_exe(run):
    run.set_ini(mode="composite", insert="clear", trace_frames="0", stats_ms="500", bake_tap="1", bake_test="")
    start_short_host(run)
    run.wait_log(r"bake_tap: REFUSED: the composite did not validate the EXE image", timeout_s=10)
    finish(run)


def test_kill_switch_and_unknown_mode(run):
    run.set_ini(mode="composite", insert="clear", trace_frames="0", stats_ms="500", bake_tap="0", bake_test="ok")
    start_short_host(run)
    run.wait_log(r" stats ", timeout_s=10)
    finish(run)
    assert bake_lines(run) == []


def test_unknown_test_mode_is_named(run):
    run.set_ini(mode="composite", insert="clear", trace_frames="0", stats_ms="500", bake_tap="1", bake_test="bogus")
    start_short_host(run)
    run.wait_log(r"bake_tap: bake_test=bogus is not a known mode - ignored", timeout_s=10)
    run.wait_log(r"bake_tap: REFUSED: the composite did not validate the EXE image", timeout_s=5)
    finish(run)


# --------------------------------------------------------------------------------------------------------------
# the reader's refusals on crafted sections (no host)

class Section:
    def __init__(self, name, size, data=b""):
        self.h = k32.CreateFileMappingW(INVALID, None, 0x04, 0, size, name)
        assert self.h
        self.p = k32.MapViewOfFile(self.h, 0x2 | 0x4, 0, 0, 0)
        ctypes.memmove(self.p, data, len(data))

    def close(self):
        k32.UnmapViewOfFile(ctypes.c_void_p(self.p))
        k32.CloseHandle(self.h)


def manifest_bytes(count=1, cap=1, slot_bytes=96, load=3, magic=0x4B414243, version=1, state=1):
    hdr = struct.pack("<9I", magic, version, count, slot_bytes, load, 1234, cap, 2, 2) + b"\0" * 28
    slots = b""
    for i in range(count):
        slots += struct.pack("<12I", 2, state, i, i, 0, 1, 1, 64, 64, 3, 71, 71) + b"\0" * 48
    return hdr + slots


@pytest.mark.parametrize("case,code,text", [
    ("missing", 3, "not found"),
    ("magic", 4, "magic"),
    ("slotsize", 4, "slot size 80"),
    ("toosmall", 4, "too small"),
    ("overcount", 4, "capacity"),
    ("stale", 5, "stale"),
    ("empty", 7, "no valid slot"),
    ("ok", 0, "1 slots (1 valid)"),
])
def test_reader_refusals(case, code, text):
    name = "Local\\VPShellBakeCraft_%d_%s" % (os.getpid(), case)
    sec = None
    if case != "missing":
        data = {"magic": manifest_bytes(magic=0), "slotsize": manifest_bytes(slot_bytes=80),
                "toosmall": manifest_bytes(cap=200), "overcount": manifest_bytes(count=3, cap=2),
                "stale": manifest_bytes(), "empty": manifest_bytes(state=0), "ok": manifest_bytes()}[case]
        sec = Section(name, 4096, data)
    try:
        rc, txt = reader("--manifest", name, "--expect-load", "3" if case != "stale" else "4")
        assert rc == code and text in txt, (rc, txt)
    finally:
        if sec:
            sec.close()


def test_reader_usage():
    rc, txt = reader()
    assert rc == 2 and "required" in txt
    rc, txt = reader("--pid", "x")
    assert rc == 2


# --------------------------------------------------------------------------------------------------------------
# bake_coverage.py (offline)

COVERAGE = shellrun.SHELL / "tools" / "bake_coverage.py"


def _coverage(tmp_path, slots, grid=None):
    g = grid if grid is not None else {"grid_sx": 1.0, "grid_sy": 1.0, "grid_ox": 128.0, "grid_oy": 128.0,
                                       "heightmap_header": {"samples_x_0C": 256, "samples_y_10": 256}}
    (tmp_path / "g.json").write_text(json.dumps(g))
    (tmp_path / "r.json").write_text(json.dumps({"slots": slots}))
    # orthographic top-down camera over the whole 256 x 256 sample map (4 x 4 tiles): x, y in [-128, 128] -> NDC
    proj = [[1 / 130.0, 0, 0, 0], [0, 1 / 130.0, 0, 0], [0, 0, 0.001, 0], [0, 0, 0.5, 1]]
    ident = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
    (tmp_path / "c.json").write_text(json.dumps({"views": [ident], "proj": proj}))
    p = subprocess.run([sys.executable, str(COVERAGE), "--grid", str(tmp_path / "g.json"), "--reader",
                        str(tmp_path / "r.json"), "--z", "0", str(tmp_path / "c.json")], capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def test_coverage_all_cached(tmp_path):
    rc, txt = _coverage(tmp_path, [{"tileX": x, "tileY": y, "state": 1} for x in range(4) for y in range(4)])
    assert rc == 0, txt
    assert json.loads(txt)[0]["visible_tiles"] == 16


def test_coverage_missing_tiles_listed(tmp_path):
    slots = [{"tileX": x, "tileY": y, "state": 1 if (x, y) != (2, 2) else 0} for x in range(4) for y in range(3)]
    rc, txt = _coverage(tmp_path, slots)
    assert rc == 1, txt
    d = json.loads(txt)[0]
    assert d["visible_tiles"] == 16 and d["with_cache"] == 11 and [2, 2] in d["missing"], d


def test_coverage_bad_input(tmp_path):
    rc, txt = _coverage(tmp_path, [], grid={"grid_sx": 1.0})
    assert rc == 2 and "bad input" in txt, txt
